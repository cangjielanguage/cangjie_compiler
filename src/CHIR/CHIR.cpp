// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/CHIR.h"

#include "cangjie/CHIR/Analysis/CallGraphAnalysis.h"
#include "cangjie/CHIR/Analysis/DevirtualizationInfo.h"
#include "cangjie/CHIR/CHIRPrinter.h"
#include "cangjie/CHIR/Checker/ConstSafetyCheck.h"
#include "cangjie/CHIR/Checker/UnreachableBranchCheck.h"
#include "cangjie/CHIR/Checker/VarInitCheck.h"
#include "cangjie/CHIR/IRChecker.h"
#include "cangjie/CHIR/Interpreter/ConstEval.h"
#include "cangjie/CHIR/Serializer/CHIRDeserializer.h"
#include "cangjie/CHIR/Serializer/CHIRSerializer.h"
#include "cangjie/CHIR/Transformation/ArrayLambdaOpt.h"
#include "cangjie/CHIR/Transformation/ArrayListConstStartOpt.h"
#include "cangjie/CHIR/Transformation/BoxRecursionValueType.h"
#include "cangjie/CHIR/Transformation/ClosureConversion.h"
#include "cangjie/CHIR/Transformation/ConstPropagation.h"
#include "cangjie/CHIR/Transformation/DeadCodeElimination.h"
#include "cangjie/CHIR/Transformation/Devirtualization.h"
#include "cangjie/CHIR/Transformation/FlatForInExpr.h"
#include "cangjie/CHIR/Transformation/FunctionInline.h"
#include "cangjie/CHIR/Transformation/GetRefToArrayElem.h"
#include "cangjie/CHIR/Transformation/MarkClassHasInited.h"
#include "cangjie/CHIR/Transformation/MergeBlocks.h"
#include "cangjie/CHIR/Transformation/NoSideEffectMarker.h"
#include "cangjie/CHIR/Transformation/RangePropagation.h"
#include "cangjie/CHIR/Transformation/RedundantFutureRemoval.h"
#include "cangjie/CHIR/Transformation/RedundantGetOrThrowElimination.h"
#include "cangjie/CHIR/Transformation/RedundantLoadElimination.h"
#include "cangjie/CHIR/Transformation/SanitizerCoverage.h"
#include "cangjie/CHIR/Transformation/UnitUnify.h"
#include "cangjie/CHIR/Transformation/UselessAllocateElimination.h"
#include "cangjie/CHIR/Visitor/Visitor.h"
#include "cangjie/Driver/TempFileManager.h"
#include "cangjie/Utils/CheckUtils.h"
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
#include "cangjie/MetaTransformation/MetaTransform.h"
#endif
#include "cangjie/Utils/ProfileRecorder.h"

namespace Cangjie::CHIR {
static void FlattenEffectMap(OptEffectCHIRMap& effectMap)
{
    auto mergeToEffectedNodes = [](std::unordered_set<Ptr<Value>>& src, std::unordered_set<Ptr<Value>>& patch) {
        for (auto node : patch) {
            src.emplace(node);
        }
    };
    auto pushToStack = [](std::stack<Ptr<Value>>& stack, std::unordered_set<Ptr<Value>>& nodes) {
        for (auto n : nodes) {
            stack.push(n);
        }
    };
    // flatten effect map, for example:
    // source effect map:
    // a -> b,c
    // b -> d,e
    // e -> f
    // after flattening:
    // a -> b,c,d,e,f
    // b -> d,e,f
    // e -> f
    OptEffectCHIRMap newEffectMap;
    for (auto& mapIt : effectMap) {
        newEffectMap.emplace(mapIt);
        std::stack<Ptr<Value>> stack;
        pushToStack(stack, mapIt.second);
        while (!stack.empty()) {
            Ptr<Value> curNode = stack.top();
            stack.pop();
            auto it = effectMap.find(curNode);
            if (it != effectMap.end()) {
                mergeToEffectedNodes(newEffectMap[mapIt.first], it->second);
                pushToStack(stack, it->second);
            }
        }
    }
    effectMap = newEffectMap;
}

static bool IsDesugaredNoneStaticConstructor(const Value& value)
{
    const auto func = DynamicCast<const Func*>(&value);
    if (func == nullptr) {
        return false;
    }
    return func->TestAttr(Attribute::COMPILER_ADD) && !func->TestAttr(Attribute::STATIC) &&
        (func->GetFuncKind() == FuncKind::CLASS_CONSTRUCTOR || func->GetFuncKind() == FuncKind::STRUCT_CONSTRUCTOR);
}

static std::unordered_set<std::string> GetNoneStaticMemberVars(const CustomTypeDef& def)
{
    std::unordered_set<std::string> vars;
    for (auto& info : def.GetDirectInstanceVars()) {
        vars.emplace(info.rawMangledName);
    }
    return vars;
}

static std::string GetRawMangledName(const Value& value)
{
    if (auto func = DynamicCast<const Func*>(&value); func) {
        return func->GetRawMangledName();
    } else {
        auto var = VirtualCast<const GlobalVar*>(&value);
        return var->GetRawMangledName();
    }
}

static std::unordered_set<std::string> GetRawMangledNameSet(const std::unordered_set<Ptr<Value>>& set)
{
    std::unordered_set<std::string> result;
    for (const auto v : set) {
        auto name = GetRawMangledName(*v);
        if (!name.empty()) {
            result.emplace(name);
        }
    }
    return result;
}

static void UpdateOptEffectMapBecauseOfInit(OptEffectCHIRMap& oldMap, OptEffectStrMap& newMap)
{
    auto mapIt = oldMap.begin();
    while (mapIt != oldMap.end()) {
        std::unordered_set<std::string> effectedNodePatch;
        auto effectedIt = mapIt->second.begin();
        while (effectedIt != mapIt->second.end()) {
            if (IsDesugaredNoneStaticConstructor(**effectedIt)) {
                auto parentClass = VirtualCast<Func*>(*effectedIt)->GetParentCustomTypeDef();
                CJC_NULLPTR_CHECK(parentClass);
                effectedNodePatch.merge(GetNoneStaticMemberVars(*parentClass));
                effectedIt = mapIt->second.erase(effectedIt);
            } else {
                ++effectedIt;
            }
        }
        if (!effectedNodePatch.empty()) {
            auto keyName = GetRawMangledName(*mapIt->first);
            if (!keyName.empty()) {
                newMap[keyName].merge(effectedNodePatch);
            }
        }
        if (IsDesugaredNoneStaticConstructor(*(mapIt->first))) {
            auto parentClass = VirtualCast<Func*>(mapIt->first)->GetParentCustomTypeDef();
            CJC_NULLPTR_CHECK(parentClass);
            auto effectedSet = GetRawMangledNameSet(mapIt->second);
            effectedSet.merge(effectedNodePatch);
            for (auto member : GetNoneStaticMemberVars(*parentClass)) {
                auto tmp = effectedSet;
                newMap[member].merge(tmp);
            }
            mapIt = oldMap.erase(mapIt);
        } else {
            ++mapIt;
        }
    }
}

static void DropOutIllegalString(OptEffectStrMap& newMap)
{
    auto mapIt = newMap.begin();
    while (mapIt != newMap.end()) {
        auto effectedIt = mapIt->second.begin();
        while (effectedIt != mapIt->second.end()) {
            if ((*effectedIt).empty() || *effectedIt == mapIt->first || *effectedIt == "$BOX") {
                effectedIt = mapIt->second.erase(effectedIt);
            } else {
                ++effectedIt;
            }
        }
        if (mapIt->first.empty() || mapIt->first == "$BOX" || mapIt->second.empty()) {
            mapIt = newMap.erase(mapIt);
        } else {
            ++mapIt;
        }
    }
}

static void UpdateEffectMapToString(OptEffectCHIRMap& oldMap, OptEffectStrMap& newMap)
{
    Utils::ProfileRecorder recorder("CHIR", "UpdateEffectMapToString");
    FlattenEffectMap(oldMap);
    UpdateOptEffectMapBecauseOfInit(oldMap, newMap);
    for (auto& mapIt : oldMap) {
        auto keyName = GetRawMangledName(*mapIt.first);
        if (!keyName.empty()) {
            newMap[keyName].merge(GetRawMangledNameSet(mapIt.second));
        }
    }
    DropOutIllegalString(newMap);
}

static void SetLambdaForWrapper(const Func& curFunc, Value& wrapperFunc)
{
    if (curFunc.IsLambda()) {
        if (auto lambdaFunc = DynamicCast<Func*>(&wrapperFunc)) {
            lambdaFunc->SetFuncKind(FuncKind::LAMBDA);
        }
    }
}

void ToCHIR::DumpCHIRDebug(const std::string& suffix)
{
    if (!opts.chirDumpDebugMode) {
        return;
    }
    CJC_NULLPTR_CHECK(chirPkg);
    CJC_ASSERT(!CHIR_READABLE_FILE_EXTENSION.empty());
    const std::string extension = CHIR_READABLE_FILE_EXTENSION.front() == '.' ? CHIR_READABLE_FILE_EXTENSION.substr(1)
                                                                              : CHIR_READABLE_FILE_EXTENSION;
    // do not dump when not debugging compute annotations
    if (!opts.computeAnnotationsDebug && isComputingAnnos) {
        return;
    }
    std::string fileName = std::to_string(debugFileIndex++) + "_";
    if (isComputingAnnos) {
        fileName += "ComputeAnnotations_";
    }
    fileName += suffix + "." + extension;
    std::string debugDir;
    if (FileUtil::IsDir(outputPath)) {
        debugDir = FileUtil::JoinPath(outputPath, chirPkg->GetName() + "_CHIR_Debug");
    } else {
        debugDir = FileUtil::GetFileBase(outputPath) + "_CHIR_Debug";
    }
    static bool checkDebugDir = false;
    if (!checkDebugDir) {
        if (FileUtil::FileExist(debugDir)) {
            for (auto file : FileUtil::GetAllFilesUnderCurrentPath(debugDir, extension)) {
                std::string fullPath = FileUtil::JoinPath(debugDir, file);
                FileUtil::Remove(fullPath);
            }
        }
        checkDebugDir = true;
    }
    std::string fullPath = FileUtil::JoinPath(debugDir, fileName);
    if (!FileUtil::FileExist(fullPath)) {
        FileUtil::CreateDirs(fullPath);
    }
    CHIRPrinter::PrintPackage(*chirPkg, fullPath);
}

void ToCHIR::DoClosureConversion()
{
    Utils::ProfileRecorder recorder("CHIR", "ClosureConversion");
    CJC_NULLPTR_CHECK(builder.GetObjectTy()->GetClassDef());
    auto closure = ClosureConversion(*chirPkg, builder, opts, srcCodeImportedFuncs);
    closure.Convert();
    uselessClasses = closure.GetUselessClassDef();
    uselessLambda = closure.GetUselessLambda();

    if (opts.enIncrementalCompilation) {
        ccOutFuncsRawMangle = closure.GetCCOutFuncsRawMangle();
    }
    DumpCHIRDebug("ClosureConversion");
}

void ToCHIR::UnreachableBlockReporter()
{
    Utils::ProfileRecorder recorder("CHIR", "UnreachableBlockWarningReporter");
    auto dce = CHIR::DeadCodeElimination(builder, diag, pkg.fullPackageName);
    dce.UnreachableBlockWarningReporter(*chirPkg, opts.GetJobs(), maybeUnreachable);
}

void ToCHIR::UnreachableBlockElimination()
{
    Utils::ProfileRecorder recorder("CHIR Opt", "UnreachableBlockElimination");
    auto dce = CHIR::DeadCodeElimination(builder, diag, pkg.fullPackageName);
    dce.UnreachableBlockElimination(*chirPkg, opts.chirDebugOptimizer);
    DumpCHIRDebug("UnreachableBlockElimination");

    RunMergingBlocks("CHIR Opt", "MergingBlockAfterUnreachableBlock");
}

void ToCHIR::RunMarkClassHasInited()
{
    Utils::ProfileRecorder recorder("CHIR", "MarkClassHasInited");
    MarkClassHasInited::RunOnPackage(*chirPkg, builder);
    DumpCHIRDebug("MarkClassHasInited");
}

void ToCHIR::NothingTypeExprElimination()
{
    Utils::ProfileRecorder recorder("CHIR Opt", "NothingTypeExprElimination");
    auto dce = CHIR::DeadCodeElimination(builder, diag, pkg.fullPackageName);
    dce.NothingTypeExprElimination(*chirPkg, opts.chirDebugOptimizer);
    DumpCHIRDebug("NothingTypeExprElimination");
}

void ToCHIR::UnreachableBranchReporter()
{
    Utils::ProfileRecorder recorder("CHIR Opt", "UnreachableBranchReporter");
    auto check = CHIR::UnreachableBranchCheck(&constAnalysisWrapper, diag, pkg.fullPackageName);
    check.RunOnPackage(*chirPkg, opts.GetJobs());
}

void ToCHIR::UselessExprElimination()
{
    if (!opts.IsCHIROptimizationLevelOverO2()) {
        return;
    }
    Utils::ProfileRecorder recorder("CHIR Opt", "UselessExprElimination");
    auto dce = CHIR::DeadCodeElimination(builder, diag, pkg.fullPackageName);
    dce.UselessExprElimination(*chirPkg, opts.chirDebugOptimizer);
    DumpCHIRDebug("UselessExprElimination");
}

void ToCHIR::UselessFuncElimination()
{
    Utils::ProfileRecorder recorder("CHIR Opt", "UselessFuncElimination");
    auto dce = CHIR::DeadCodeElimination(builder, diag, pkg.fullPackageName);
    dce.UselessFuncElimination(*chirPkg, opts);
    DumpCHIRDebug("UselessFuncElimination");
}

void ToCHIR::ReportUnusedCode()
{
    Utils::ProfileRecorder recorder("CHIR Opt", "ReportUnusedCode");
    auto dce = CHIR::DeadCodeElimination(builder, diag, pkg.fullPackageName);
    dce.ReportUnusedCode(*chirPkg, opts);
}

void ToCHIR::RedundantLoadElimination()
{
    if (!opts.IsOptimizationExisted(GlobalOptions::OptimizationFlag::REDUNDANT_LOAD)) {
        return;
    }
    Utils::ProfileRecorder recorder("CHIR Opt", "RedundantLoadElimination");
    auto rle = CHIR::RedundantLoadElimination();
    rle.RunOnPackage(chirPkg, opts.chirDebugOptimizer);
    DumpCHIRDebug("RedundantLoadElimination");
}

void ToCHIR::UselessAllocateElimination()
{
    if (!opts.IsCHIROptimizationLevelOverO2()) {
        return;
    }
    Utils::ProfileRecorder recorder("CHIR Opt", "UselessAllocateElimination");
    UselessAllocateElimination::RunOnPackage(*chirPkg, opts.chirDebugOptimizer);
    DumpCHIRDebug("UselessAllocateElimination");
}

void ToCHIR::RunGetRefToArrayElemOpt()
{
    if (!opts.IsCHIROptimizationLevelOverO2() || opts.interpFullBchir) {
        return;
    }
    Utils::ProfileRecorder recorder("CHIR Opt", "ArrayGetRefOpt");
    GetRefToArrayElem::RunOnPackage(*chirPkg, builder);
    DumpCHIRDebug("ArrayGetRefOpt");
}

void ToCHIR::Devirtualization(DevirtualizationInfo& devirtInfo)
{
    if (!opts.IsOptimizationExisted(GlobalOptions::OptimizationFlag::DEVIRTUALIZATION) ||
        opts.enIncrementalCompilation) {
        return;
    }

    Utils::ProfileRecorder recorder("CHIR Opt", "Devirtualization");
    devirtInfo.FreshRetMap();
    auto funcs = CHIR::Devirtualization::CollectContainInvokeExprFuncs(chirPkg);
    // Only process functions containing invoke expressions.
    if (funcs.empty()) {
        return;
    }
    size_t threadNum = opts.GetJobs();
    TypeValue::SetCHIRBuilder(&builder);
    AnalysisWrapper<TypeAnalysis, TypeDomain> typeAnalysisWrapper(builder);
    typeAnalysisWrapper.RunOnPackage(
        chirPkg, opts.chirDebugOptimizer, threadNum, devirtInfo.GetReturnTypeMap());
    auto devirt = CHIR::Devirtualization(&typeAnalysisWrapper, devirtInfo);
    devirt.RunOnFuncs(funcs, builder, opts.chirDebugOptimizer);

    // if get frozen inst funcs after first devirtualization, opt them in the second round
    if (!devirt.GetFrozenInstFuns().empty()) {
        auto& frozenFuncs = devirt.GetFrozenInstFuns();
        for (auto& func : frozenFuncs) {
            auto res = typeAnalysisWrapper.RunOnFunc(func, opts.chirDebugOptimizer, devirtInfo.GetReturnTypeMap());
            devirt.AppendFrozenFuncState(func, std::move(res));
        }
        devirt.RunOnFuncs(frozenFuncs, builder, opts.chirDebugOptimizer);

        auto pass = FunctionInline(
            builder, opts.optimizationLevel, opts.chirDebugOptimizer);
        for (auto& func : frozenFuncs) {
            pass.Run(*func);
        }
    }
    DumpCHIRDebug("Devirtualization");
}

void ToCHIR::RedundantGetOrThrowElimination()
{
    if (!opts.enableChirRGetOrThrowE) {
        return;
    }
    Utils::ProfileRecorder recorder("CHIR Opt", "RedundantGetOrThrowElimination");
    auto rGetOrThrowE = CHIR::RedundantGetOrThrowElimination();
    rGetOrThrowE.RunOnPackage(chirPkg, opts.chirDebugOptimizer);
    DumpCHIRDebug("RedundantGetOrThrowElimination");
}

void ToCHIR::FlatForInExpr()
{
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    Utils::ProfileRecorder recorder("CHIR", "FlatForInExpr");
    auto flatForInExpr = CHIR::FlatForInExpr(builder);
    flatForInExpr.RunOnPackage(*chirPkg);
    DumpCHIRDebug("FlatForInExpr");
#endif
}

bool ToCHIR::RunVarInitChecking()
{
    Utils::ProfileRecorder recorder("CHIR Opt", "VarInitCheck");
    auto vic = CHIR::VarInitCheck(&diag);
    vic.RunOnPackage(chirPkg, opts.GetJobs());
    return diag.GetErrorCount() == 0;
}

void ToCHIR::RunArrayListConstStartOpt()
{
    if (!opts.IsCHIROptimizationLevelOverO2()) {
        return;
    }
    Utils::ProfileRecorder recorder("CHIR Opt", "ArrayListOpt");
    auto functionInline = FunctionInline(builder, opts.optimizationLevel, opts.chirDebugOptimizer);
    auto pass = ArrayListConstStartOpt(builder, opts, functionInline);
    pass.RunOnPackage(chirPkg);
    MergeEffectMap(pass.GetEffectMap(), effectMap);
    DumpCHIRDebug("RunArrayListConstStartOpt");
}

void ToCHIR::RunFunctionInline(DevirtualizationInfo& devirtInfo)
{
    if (!opts.IsOptimizationExisted(GlobalOptions::OptimizationFlag::FUNC_INLINING)) {
        return;
    }
    Utils::ProfileRecorder::Start("CHIR Opt", "FunctionInline");
    CallGraphAnalysis callGraphAnalysis(chirPkg, devirtInfo);
    // Collect all call graph information.
    callGraphAnalysis.DoCallGraphAnalysis(opts.chirDebugOptimizer);
    auto pass = FunctionInline(builder, opts.optimizationLevel, opts.chirDebugOptimizer);
    for (auto func : callGraphAnalysis.postOrderSCCFunctionlist) {
        if (!func) {
            continue;
        }
        // 1. $toAny is a function needed by reflection, it's used by runtime and it mustn't be optimized
        // 2. annotation factory functions mustn't be optimized
        if (func->GetSrcCodeIdentifier() == "$toAny" ||
            func->GetFuncKind() == FuncKind::ANNOFACTORY_FUNC) {
            continue;
        }
        pass.Run(*func);
    }
    MergeEffectMap(pass.GetEffectMap(), effectMap);
    Utils::ProfileRecorder::Stop("CHIR Opt", "FunctionInline");
    DumpCHIRDebug("FunctionInline");

    RunMergingBlocks("CHIR Opt", "MergingBlockAfterInline");

    // do useless function elimination after inline
    UselessFuncElimination();
}

void ToCHIR::RunUnreachableMarkBlockRemoval()
{
    Utils::ProfileRecorder recorder("CHIR", "Clear Blocks Marked as Unreachable");
    auto dce = CHIR::DeadCodeElimination(builder, diag, pkg.fullPackageName);
    dce.ClearUnreachableMarkBlock(*chirPkg);
    DumpCHIRDebug("ClearBlocksMarkAsUnreachable");
}

void ToCHIR::RunMergingBlocks(const std::string& firstName, const std::string& secondName)
{
    Utils::ProfileRecorder recorder(firstName, secondName);
    MergeBlocks::RunOnPackage(*chirPkg, builder, opts);
    DumpCHIRDebug(secondName);
}

void ToCHIR::RunConstantAnalysis()
{
    Utils::ProfileRecorder recorder("CHIR Opt", "Constant Analysis");
    constAnalysisWrapper.RunOnPackage(chirPkg, opts.chirDebugOptimizer, opts.GetJobs(), &diag);
}

bool ToCHIR::RunConstantPropagation()
{
    Utils::ProfileRecorder recorder("CHIR Opt", "Constant Propagation & Safety Check");
    size_t threadNum = opts.GetJobs();
    if (threadNum == 1) {
        auto cp = CHIR::ConstPropagation(builder, &constAnalysisWrapper, opts);
        cp.RunOnPackage(chirPkg, opts.chirDebugOptimizer, ci.isCJLint);
        MergeEffectMap(cp.GetEffectMap(), effectMap);
    } else {
        bool isDebug = opts.chirDebugOptimizer;
        bool isCJLint = ci.isCJLint;
        std::vector<Func*> globalFuncs = chirPkg->GetGlobalFuncs();
        size_t funcNum = globalFuncs.size();
        std::vector<std::unique_ptr<CHIR::CHIRBuilder>> builderList = ConstructSubBuilders(threadNum, funcNum);
        Utils::TaskQueue taskQueue(threadNum);
        std::vector<std::unique_ptr<CHIR::ConstPropagation>> cpList;
        for (size_t idx = 0; idx < funcNum; ++idx) {
            auto func = globalFuncs.at(idx);
            auto cp = std::make_unique<CHIR::ConstPropagation>(*builderList[idx], &constAnalysisWrapper, opts);
            taskQueue.AddTask<void>([constPropagation = cp.get(), func, isDebug, isCJLint]() {
                return constPropagation->RunOnFunc(func, isDebug, isCJLint);
            });
            cpList.emplace_back(std::move(cp));
        }
        taskQueue.RunAndWaitForAllTasksCompleted();
        for (auto& cp : cpList) {
            MergeEffectMap(cp->GetEffectMap(), effectMap);
        }
        for (auto& subBd : builderList) {
            (*subBd).MergeAllocatedInstance();
        }
        builder.GetChirContext().MergeTypes();
    }
    DumpCHIRDebug("ConstantPropagation");
    return diag.GetErrorCount() == 0;
}

bool ToCHIR::RunConstantPropagationAndSafetyCheck()
{
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    return RunConstantPropagation();
#endif
}

void ToCHIR::RunRangePropagation()
{
    if (!opts.IsCHIROptimizationLevelOverO2()) {
        return;
    }
    Utils::ProfileRecorder recorder("CHIR Opt", "Range Propagation");
    AnalysisWrapper<RangeAnalysis, RangeDomain> vra(builder);
    vra.RunOnPackage(chirPkg, opts.chirDebugOptimizer, opts.GetJobs(), &diag);
    size_t threadNum = opts.GetJobs();
    if (threadNum == 1) {
        auto cp = CHIR::RangePropagation(builder, &vra, &diag, opts.enIncrementalCompilation);
        cp.RunOnPackage(chirPkg, opts.chirDebugOptimizer);
        MergeEffectMap(cp.GetEffectMap(), effectMap);
    } else {
        bool isDebug = opts.chirDebugOptimizer;
        std::vector<Func*> globalFuncs = chirPkg->GetGlobalFuncs();
        size_t funcNum = globalFuncs.size();
        std::vector<std::unique_ptr<CHIR::CHIRBuilder>> builderList =
            CHIR::ToCHIR::ConstructSubBuilders(threadNum, funcNum);
        Utils::TaskQueue taskQueue(threadNum);
        std::vector<std::unique_ptr<CHIR::RangePropagation>> cpList;
        for (size_t idx = 0; idx < funcNum; ++idx) {
            auto func = globalFuncs.at(idx);
            auto cp = std::make_unique<CHIR::RangePropagation>(
                *builderList[idx], &vra, &diag, opts.enIncrementalCompilation);
            taskQueue.AddTask<void>(
                [rangePropagation = cp.get(), func, isDebug]() { return rangePropagation->RunOnFunc(func, isDebug); });
            cpList.emplace_back(std::move(cp));
        }
        taskQueue.RunAndWaitForAllTasksCompleted();
        for (auto& cp : cpList) {
            MergeEffectMap(cp->GetEffectMap(), effectMap);
        }
        for (auto& subBd : builderList) {
            (*subBd).MergeAllocatedInstance();
        }
        builder.GetChirContext().MergeTypes();
    }
    DumpCHIRDebug("RangePropagation");
    return;
}

void ToCHIR::RunArrayLambdaOpt()
{
    if (!opts.IsOptimizationExisted(GlobalOptions::OptimizationFlag::ARRAY_LAMBDA_OPT)) {
        return;
    }
    Utils::ProfileRecorder recorder("CHIR Opt", "ArrayLambdaOpt");
    auto arrayLambdaOpt = CHIR::ArrayLambdaOpt(builder);
    arrayLambdaOpt.RunOnPackage(chirPkg, opts.chirDebugOptimizer);
    DumpCHIRDebug("ArrayLambdaOpt");
}

void ToCHIR::RunRedundantFutureOpt()
{
    if (!opts.IsOptimizationExisted(GlobalOptions::OptimizationFlag::REDUNDANT_FUTURE)) {
        return;
    }
    Utils::ProfileRecorder recorder("CHIR Opt", "RedundantFutureOpt");
    RedundantFutureRemoval(*chirPkg, opts.chirDebugOptimizer).RunOnPackage();
    DumpCHIRDebug("RedundantFutureOpt");
}

void ToCHIR::RunSanitizerCoverage()
{
    if (!opts.IsSancovOptionEnabled()) {
        return;
    }
    Utils::ProfileRecorder recorder("CHIR", "Sanitizer Coverage");
    auto sanCovOpt = CHIR::SanitizerCoverage(opts, builder);
    sanCovOpt.RunOnPackage(chirPkg, diag, opts.chirDebugOptimizer);
    DumpCHIRDebug("Sanitizer_Coverage");
}

void ToCHIR::RunNoSideEffectMarkerOpt()
{
    Utils::ProfileRecorder recorder("CHIR Opt", "No Side Effect Marker");
    CHIR::NoSideEffectMarker::RunOnPackage(chirPkg, opts.chirDebugOptimizer);
    DumpCHIRDebug("No_Side_Effect_Marker");
}

void ToCHIR::RunUnitUnify()
{
    if (!opts.IsCHIROptimizationLevelOverO2()) {
        return;
    }
    Utils::ProfileRecorder recorder("CHIR Opt", "Unit Unify");
    auto unitUnify = CHIR::UnitUnify(builder);
    unitUnify.RunOnPackage(chirPkg, opts.chirDebugOptimizer);
    DumpCHIRDebug("Unit_Unify");
}

DevirtualizationInfo ToCHIR::CollectDevirtualizationInfo()
{
    DevirtualizationInfo devirtInfo(chirPkg);
    if (opts.IsOptimizationExisted(GlobalOptions::OptimizationFlag::FUNC_INLINING) ||
        (opts.IsOptimizationExisted(GlobalOptions::OptimizationFlag::DEVIRTUALIZATION) &&
         !opts.enIncrementalCompilation)) {
        // Collect all inheritance tree information.
        Utils::ProfileRecorder::Start("CHIR Opt", "Collect Devirt Info");
        devirtInfo.CollectInfo();
        Utils::ProfileRecorder::Stop("CHIR Opt", "Collect Devirt Info");
    }
    return devirtInfo;
}

void ToCHIR::MarkNoSideEffect()
{
    Utils::ProfileRecorder recorder("CHIR Opt", "MarkNoSideEffect");
    static const std::unordered_set<std::string> WHITE_LIST = {
        "8std.math5roundFd$$d",
        "8std.core6StringS2==F6String$$b",
        "8std.core7Extend!l<:8HashableX8hashCodeF$$l",
        "8std.core7Extend!d<:8HashableX8hashCodeF$$l",
        "8std.core6StringS7compareF6String$$8Ordering",
        "8std.core7Extend!8Ordering<:10Comparable<8Ordering>X7compareF8Ordering$$8Ordering"
    };
    for (auto func : chirPkg->GetGlobalFuncs()) {
        if (WHITE_LIST.find(func->GetRawMangledName()) != WHITE_LIST.end()) {
            func->EnableAttr(Attribute::NO_SIDE_EFFECT);
        }
    }
    for (Value* value : chirPkg->GetImportedVarAndFuncs()) {
        if (value->IsImportedVar()) {
            continue;
        }
        if (WHITE_LIST.find(VirtualCast<FuncBase*>(value)->GetRawMangledName()) != WHITE_LIST.end()) {
            value->EnableAttr(Attribute::NO_SIDE_EFFECT);
        }
    }
    DumpCHIRDebug("MarkNoSideEffect");
}

bool ToCHIR::RunOptimizationPassAndRulesChecking()
{
    Utils::ProfileRecorder recorder("CHIR", "CHIR Opt");

    NothingTypeExprElimination();
    RunConstantAnalysis();
    if (!RunVarInitChecking()) {
        return false;
    }
    UnreachableBranchReporter();
    // this instantance of block elimination is to maintain dead code warnings
    UnreachableBlockElimination();
    ReportUnusedCode();
    RunArrayListConstStartOpt();
    if (!RunConstantPropagationAndSafetyCheck()) {
        return false;
    }
    UnreachableBlockElimination();
    UselessFuncElimination();
    UselessExprElimination();
    MarkNoSideEffect();
    RunUnitUnify();
    auto devirtInfo = CollectDevirtualizationInfo();
    RunFunctionInline(devirtInfo);
    RedundantLoadElimination();
    RedundantGetOrThrowElimination();

    RunRangePropagation();
    UselessAllocateElimination();
    Devirtualization(devirtInfo);
    RunArrayLambdaOpt();
    RunRedundantFutureOpt();
    RunNoSideEffectMarkerOpt();
    RunGetRefToArrayElemOpt();
    return true;
}

bool ToCHIR::RunConstantEvaluation()
{
    if (!opts.IsConstEvalEnabled() || opts.enIncrementalCompilation) {
        return true;
    }
    Utils::ProfileRecorder recorder("CHIR", "Constant Evaluation");
    auto ce = CHIR::Interpreter::ConstEvalPass(ci, builder, sourceManager, opts, diagEngine);
    std::vector<Cangjie::CHIR::Interpreter::Bchir> bchirPackages;
    ce.RunOnPackage(*chirPkg, initFuncsForConstVar, bchirPackages);
    DumpCHIRDebug("after_const_eval");
    return true;
}

bool ToCHIR::RunIRChecker(const Phase& phase) const
{
    if (!opts.chirWFC) {
        return true;
    }
    std::string suffix;
    switch (phase) {
        case Phase::RAW:
            suffix = "after translation";
            break;
        case Phase::OPT:
            suffix = "after compiler optimization";
            break;
        case Phase::PLUGIN:
            suffix = "after perform pulgin";
            break;
        case Phase::ANALYSIS_FOR_CJLINT:
            suffix = "after analysis for cjlint";
            break;
    }
    Utils::ProfileRecorder recorder("CHIR", "IRCheck " + suffix);
    CJC_NULLPTR_CHECK(chirPkg);
    return IRCheck(*chirPkg, opts, builder, phase);
}

void ToCHIR::RecordCodeInfoAtTheBegin()
{
    if (!opts.enableTimer && !opts.enableMemoryCollect) {
        return;
    }
    Utils::ProfileRecorder recorder("CHIR", "RecordCodeInfo");
    std::function<int64_t(void)> getASTNodeQuantity = [this]() -> int64_t {
        int64_t astNodeCnt = 0;
        AST::Walker(&pkg, [&astNodeCnt](auto /* node */) {
            astNodeCnt++;
            return AST::VisitAction::WALK_CHILDREN;
        }).Walk();
        return astNodeCnt;
    };
    Utils::ProfileRecorder::RecordCodeInfo("AST node", getASTNodeQuantity);
    Utils::ProfileRecorder::RecordCodeInfo(
        "generic ins func in AST", static_cast<int64_t>(pkg.genericInstantiatedDecls.size()));
    std::function<int64_t(void)> getGenericInstantiatedAstNode = [this]() -> int64_t {
        int64_t astNodeCnt = 0;
        for (auto& decl : pkg.genericInstantiatedDecls) {
            AST::Walker(decl.get(), [&astNodeCnt](auto /* node */) {
                astNodeCnt++;
                return AST::VisitAction::WALK_CHILDREN;
            }).Walk();
        }
        return astNodeCnt;
    };
    Utils::ProfileRecorder::RecordCodeInfo("generic ins ast node", getGenericInstantiatedAstNode);
    Utils::ProfileRecorder::RecordCodeInfo(
        "import pkg", static_cast<int64_t>(importManager.GetAllImportedPackages(true).size()));
    Utils::ProfileRecorder::RecordCodeInfo("src file", static_cast<int64_t>(pkg.files.size()));
    Utils::ProfileRecorder::RecordCodeInfo(
        "global func in CHIR after trans", static_cast<int64_t>(chirPkg->GetGlobalFuncs().size()));
    Utils::ProfileRecorder::RecordCodeInfo("global var in CHIR", static_cast<int64_t>(chirPkg->GetGlobalVars().size()));
    int64_t funcInlineCnt = std::count_if(
        pkg.inlineFuncDecls.begin(), pkg.inlineFuncDecls.end(), [](auto func) { return func && func->isInline; });
    Utils::ProfileRecorder::RecordCodeInfo("imported inline func", funcInlineCnt);
    std::function<int64_t(void)> getCurPkgInstantiatedAstNode = [this]() -> int64_t {
        int64_t astNodeCnt = 0;
        for (auto& decl : pkg.genericInstantiatedDecls) {
            auto genericDecl = decl->genericDecl;
            CJC_NULLPTR_CHECK(genericDecl->curFile);
            if (genericDecl->curFile->curPackage->fullPackageName != pkg.fullPackageName) {
                continue;
            }
            AST::Walker(decl.get(), [&astNodeCnt](auto /* node */) {
                astNodeCnt++;
                return AST::VisitAction::WALK_CHILDREN;
            }).Walk();
        }
        return astNodeCnt;
    };
    Utils::ProfileRecorder::RecordCodeInfo("cur pkg generic ins ast node", getCurPkgInstantiatedAstNode);
}

void ToCHIR::RecordCodeInfoAtTheEnd()
{
    if (!opts.enableTimer && !opts.enableMemoryCollect) {
        return;
    }
    Utils::ProfileRecorder recorder("CHIR", "RecordCodeInfo");
    Utils::ProfileRecorder::RecordCodeInfo("all CHIR node", static_cast<int64_t>(builder.GetAllNodesNum()));
    Utils::ProfileRecorder::RecordCodeInfo("all CHIR type", static_cast<int64_t>(builder.GetTypesNum()));
    Utils::ProfileRecorder::RecordCodeInfo(
        "global func after CHIR stage", static_cast<int64_t>(chirPkg->GetGlobalFuncs().size()));
    int64_t wrapperFuncNum = 0;
    int64_t funcExprNum = 0;
    int64_t wrapperFuncExprNum = 0;
    for (auto func : chirPkg->GetGlobalFuncs()) {
        if (!func->GetBody()) {
            continue;
        }
        int64_t exprNum = 0;
        for (auto b : func->GetBody()->GetBlocks()) {
            exprNum += static_cast<int64_t>(b->GetExpressions().size());
        }
        funcExprNum += exprNum;
        if (func->Get<WrappedRawMethod>() != nullptr) {
            wrapperFuncNum++;
            wrapperFuncExprNum += exprNum;
        }
    }
    Utils::ProfileRecorder::RecordCodeInfo("wrapper func after CHIR stage", wrapperFuncNum);
    Utils::ProfileRecorder::RecordCodeInfo("expr num in global func after CHIR stage", funcExprNum);
    Utils::ProfileRecorder::RecordCodeInfo("expr num in wrapper func after CHIR stage", wrapperFuncExprNum);
}

void ToCHIR::RecordCHIRExprNum(const std::string& suffix)
{
    if (!opts.enableTimer && !opts.enableMemoryCollect) {
        return;
    }
    Utils::ProfileRecorder recorder("CHIR", "RecordCodeInfo");
    Utils::ProfileRecorder::RecordCodeInfo(
        "valid CHIR func num after " + suffix, static_cast<int64_t>(chirPkg->GetGlobalFuncs().size()));

    int64_t allNonGenericExprsNum = 0;
    int64_t allInstantiatedExprsNum = 0;
    int64_t curPkgInstantiatedExprsNum = 0;
    for (auto func : chirPkg->GetGlobalFuncs()) {
        auto exprsNum = func->GetExpressionsNum();
        allNonGenericExprsNum += static_cast<int64_t>(exprsNum);
        if (func->GetGenericDecl() == nullptr) {
            continue;
        }
        if (func->TestAttr(Attribute::GENERIC_INSTANTIATED) ||
            (func->GetOuterDeclaredOrExtendedDef() &&
                func->GetOuterDeclaredOrExtendedDef()->TestAttr(Attribute::GENERIC_INSTANTIATED))) {
            allInstantiatedExprsNum += static_cast<int64_t>(exprsNum);
            if (func->GetGenericDecl() && !func->GetGenericDecl()->TestAttr(Attribute::IMPORTED)) {
                curPkgInstantiatedExprsNum += static_cast<int64_t>(exprsNum);
            }
        }
    }
    Utils::ProfileRecorder::RecordCodeInfo("valid CHIR expr num after " + suffix, allNonGenericExprsNum);
    Utils::ProfileRecorder::RecordCodeInfo("valid CHIR generic ins expr num after " + suffix, allInstantiatedExprsNum);
    Utils::ProfileRecorder::RecordCodeInfo(
        "valid CHIR cur pkg generic ins expr num after " + suffix, curPkgInstantiatedExprsNum);

    int64_t allGenericExprsNum = 0;
    int64_t curPkgGenericExprsNum = 0;
    int64_t allGenericFuncNum = 0;
    int64_t curPkgGenericFuncNum = 0;
    for (auto func : chirPkg->GetGlobalFuncs()) {
        if (!func->IsInGenericContext()) {
            continue;
        }
        auto exprsNum = func->GetExpressionsNum();
        allGenericExprsNum += static_cast<int64_t>(exprsNum);
        allGenericFuncNum++;
        if (!func->TestAttr(Attribute::IMPORTED)) {
            curPkgGenericExprsNum += static_cast<int64_t>(exprsNum);
            curPkgGenericFuncNum++;
        }
    }
    Utils::ProfileRecorder::RecordCodeInfo("valid CHIR generic func num after " + suffix, allGenericFuncNum);
    Utils::ProfileRecorder::RecordCodeInfo("valid CHIR generic expr num after " + suffix, allGenericExprsNum);
    Utils::ProfileRecorder::RecordCodeInfo("valid CHIR cur pkg generic func num after " + suffix, curPkgGenericFuncNum);
    Utils::ProfileRecorder::RecordCodeInfo(
        "valid CHIR cur pkg generic expr num after " + suffix, curPkgGenericExprsNum);
}

bool ToCHIR::RunAnalysisForCJLint()
{
    Utils::ProfileRecorder recorder("CHIR", "CHIR Opt");
    NothingTypeExprElimination();
    RunConstantAnalysis();
    if (!RunVarInitChecking()) {
        return false;
    }
    UnreachableBlockElimination();
    if (RunConstantPropagationAndSafetyCheck()) {
        constAnalysisWrapper.InvalidateAllAnalysisResults();
        RunConstantAnalysis();
        return true;
    }
    return false;
}

void ToCHIR::EraseDebugExpr()
{
    Utils::ProfileRecorder recorder("CHIR", "EraseUselessDebugExpr");
    // Erase useless debug expressions for codegen in not -g mode,
    // For the reason of error when enable parallel mode in codegen.
    // These expressions are needed for report warning, so only can be removed at the end of CHIR stage.
    for (auto func : chirPkg->GetGlobalFuncs()) {
        for (auto block : func->GetBody()->GetBlocks()) {
            auto exprs = block->GetExpressions();
            for (size_t i = 0; i < exprs.size(); i++) {
                if (exprs[i]->GetExprKind() == ExprKind::DEBUGEXPR &&
                    (exprs[i]->GetOperand(0)->IsGlobal() || exprs[i]->GetOperand(0)->IsCompileTimeValue())) {
                    exprs[i]->RemoveSelfFromBlock();
                }
            }
        }
    }
    DumpCHIRDebug("EraseUselessDebugExpr");
}

void ToCHIR::CFFIFuncWrapper()
{
    Utils::ProfileRecorder recorder("CHIR", "CFFIFuncWrapper");
    std::vector<Func*> cfuncs;
    std::vector<ImportedFunc*> foreignFuncs;
    for (auto curFunc : chirPkg->GetGlobalFuncs()) {
        if (curFunc->GetType()->IsCFunc()) {
            cfuncs.emplace_back(curFunc);
        }
    }
    for (auto foreignFunc : chirPkg->GetImportedVarAndFuncs()) {
        if (foreignFunc->GetType()->IsCFunc()) {
            CJC_ASSERT(foreignFunc->IsImportedFunc());
            foreignFuncs.emplace_back(StaticCast<ImportedFunc*>(foreignFunc));
        }
    }
    for (auto curFunc : cfuncs) {
        if (curFunc->Get<LinkTypeInfo>() != Linkage::EXTERNAL) {
            if (!IsAllApply(curFunc)) {
                auto [wrapperFunc, res] = DoCFFIFuncWrapper(*curFunc, false, false);
                SetLambdaForWrapper(*curFunc, *wrapperFunc);
                ReplaceUsesWithWrapper(*curFunc, res, *wrapperFunc);
            }
            continue;
        }
        auto [wrapperFunc, res] = DoCFFIFuncWrapper(*curFunc, false);
        if (!IsAllApply(curFunc)) {
            ReplaceUsesWithWrapper(*curFunc, res, *wrapperFunc);
        }
    }
    for (auto curFunc : foreignFuncs) {
        // wrapper function for it.
        if (curFunc->Get<CHIR::LinkTypeInfo>() != Linkage::EXTERNAL) {
            continue;
        }
        bool isForeign = curFunc->TestAttr(Attribute::FOREIGN);
        auto [wrapperFunc, res] = DoCFFIFuncWrapper(*curFunc, isForeign);
        // sanitizer_cov func have empty package name.
        const auto& funcPkgName = curFunc->GetPackageName().empty() ? pkg.fullPackageName : curFunc->GetPackageName();
        if (funcPkgName != pkg.fullPackageName) {
            // NOTE: res maybe null!
            ReplaceUsesWithWrapper(*curFunc, res, *wrapperFunc, true);
        }
    }
    DumpCHIRDebug("CFFIFuncWrapper");
}

template <typename T>
std::pair<Value*, Apply*> ToCHIR::DoCFFIFuncWrapper(T& curFunc, bool isForeign, bool isExternal)
{
    auto ident = curFunc.GetSrcCodeIdentifier();
    if (curFunc.GetFuncKind() == FuncKind::LAMBDA) {
        ident = curFunc.GetIdentifierWithoutPrefix();
        auto pos = ident.find(CFFI_FUNC_SUFFIX);
        if (pos != std::string::npos) {
            ident.erase(pos, CFFI_FUNC_SUFFIX.size());
        }
    }
    const auto& funcPkgName = curFunc.GetPackageName().empty() ? pkg.fullPackageName : curFunc.GetPackageName();
    if (isForeign || (curFunc.TestAttr(Attribute::PRIVATE) && isExternal)) {
        ident = funcPkgName + ":" + ident;
    }
    if (funcPkgName != pkg.fullPackageName) {
        auto wrapperFunc = builder.CreateImportedVarOrFunc<ImportedFunc>(
            curFunc.GetFuncType(), ident, ident, "", funcPkgName, curFunc.GetGenericTypeParams());
        wrapperFunc->SetCFFIWrapper(true);
        return std::make_pair(wrapperFunc, nullptr);
    }
    auto wrapperFunc =
        builder.CreateFunc(curFunc.GetDebugLocation(), curFunc.GetFuncType(), ident, ident, "", funcPkgName);
    auto body = builder.CreateBlockGroup(*wrapperFunc);
    wrapperFunc->SetCFFIWrapper(true);
    curFunc.EnableAttr(Attribute::NO_INLINE);
    curFunc.template Set<LinkTypeInfo>(Linkage::INTERNAL);
    wrapperFunc->InitBody(*body);
    wrapperFunc->EnableAttr(Attribute::COMPILER_ADD);
    auto linkage = isExternal ? Linkage::EXTERNAL : Linkage::INTERNAL;
    wrapperFunc->template Set<LinkTypeInfo>(linkage);
    std::vector<Value*> args{};
    auto curFuncType = curFunc.GetFuncType();
    auto paramTys = curFuncType->GetParamTypes();
    for (auto paramTy : paramTys) {
        builder.CreateParameter(paramTy, INVALID_LOCATION, *wrapperFunc);
    }
    auto& funcParams = wrapperFunc->GetParams();
    std::for_each(funcParams.begin(), funcParams.end(), [&args](auto item) { args.emplace_back(item); });

    // create body
    auto entry = builder.CreateBlock(body);
    body->SetEntryBlock(entry);
    auto retType = curFunc.GetReturnType();
    auto res = builder.CreateExpression<Apply>(
        curFunc.GetDebugLocation(), retType, &curFunc, FuncCallContext{.args = args}, entry);
    entry->AppendExpression(res);
    auto retAlloc =
        builder.CreateExpression<Allocate>(DebugLocation(), builder.GetType<RefType>(retType), retType, entry);
    entry->AppendExpression(retAlloc);
    wrapperFunc->SetReturnValue(*retAlloc->GetResult());

    // create exit
    entry->AppendExpression(builder.CreateExpression<Store>(
        builder.GetType<UnitType>(), res->GetResult(), retAlloc->GetResult(), entry));
    entry->AppendExpression(builder.CreateTerminator<Exit>(entry));
    return std::make_pair(wrapperFunc, res);
}

template <typename T> bool ToCHIR::IsAllApply(const T* curFunc)
{
    auto users = curFunc->GetUsers();
    for (size_t index = 0; index < users.size(); index++) {
        if (users[index]->GetExprKind() != ExprKind::APPLY &&
            users[index]->GetExprKind() != ExprKind::APPLY_WITH_EXCEPTION) {
            return false;
        }
        if (users[index]->GetExprKind() == ExprKind::APPLY) {
            auto applyNode = StaticCast<Apply*>(users[index]);
            if (applyNode->GetCallee() != curFunc) {
                return false;
            }
        }
        if (users[index]->GetExprKind() == ExprKind::APPLY_WITH_EXCEPTION) {
            auto applyNode = StaticCast<ApplyWithException*>(users[index]);
            if (applyNode->GetCallee() != curFunc) {
                return false;
            }
        }
    }
    return true;
}

void ToCHIR::CreateBoxTypeForRecursionValueType()
{
    Utils::ProfileRecorder recorder("CHIR", "BoxRecursionEnum");
    auto transformer = BoxRecursionValueType(*chirPkg, builder);
    transformer.CreateBoxTypeForRecursionValueType();
    DumpCHIRDebug("BoxRecursionEnum");
}

namespace {
void EmitCHIR(const std::string& outputPath, const CHIR::Package& package, ToCHIR::Phase phase, bool dump)
{
    std::string path;
    if (FileUtil::IsDir(outputPath)) {
        path = FileUtil::JoinPath(outputPath, package.GetName() + CHIR_SERIALIZATION_FILE_EXTENSION);
    } else {
        path = outputPath;
    }
    CHIRSerializer::Serialize(package, path, phase);
    if (dump) {
        const std::string dumpFileName =
            FileUtil::GetFileNameWithoutExtension(path) + "_Emit_Debug" + CHIR_READABLE_FILE_EXTENSION;
        // print serialize extension info which just for serialization not necessary for chir nodes
        CHIRPrinter::PrintCHIRSerializeInfo(phase, dumpFileName);
        // print package
        CHIRPrinter::PrintPackage(package, dumpFileName);
    }
}
} // namespace

bool ToCHIR::ComputeAnnotations(std::vector<const AST::Decl*>&& annoOnly)
{
    isComputingAnnos = true;
    if (!TranslateToCHIR(std::move(annoOnly))) {
        return false;
    }
    FlatForInExpr();
    RunUnreachableMarkBlockRemoval();
    RunMergingBlocks("CHIR", "MergingBlock");
    NothingTypeExprElimination();
    UnreachableBlockElimination();
    DoClosureConversion();
    if (!RunConstantEvaluation()) {
        return false;
    }
    isComputingAnnos = false;
    debugFileIndex = 0;
    return true;
}

bool ToCHIR::Run()
{
    CJC_NULLPTR_CHECK(typeManager);
    CJC_NULLPTR_CHECK(gim);
    CJC_NULLPTR_CHECK(&needToOptGenericDecl);
    CJC_NULLPTR_CHECK(&needToOptString);
    CJC_NULLPTR_CHECK(&releaseCHIRMemory);
    CJC_NULLPTR_CHECK(&cangjieHome);

    if (!TranslateToCHIR({})) {
        return false;
    }
    if (opts.emitCHIRPhase == GlobalOptions::CandidateEmitCHIRPhase::RAW) {
        EmitCHIR(outputPath, *chirPkg, Phase::RAW, opts.chirDumpDebugMode);
        return true;
    }
    RecordCHIRExprNum("trans");
    RecordCodeInfoAtTheBegin();
    UnreachableBlockReporter();
    FlatForInExpr();
    RunUnreachableMarkBlockRemoval();
    RunMergingBlocks("CHIR", "MergingBlock");
    RunMarkClassHasInited();

    if (ci.isCJLint) {
        return RunAnalysisForCJLint() && RunIRChecker(Phase::ANALYSIS_FOR_CJLINT);
    }
    if (!RunOptimizationPassAndRulesChecking()) {
        return false;
    }
    RecordCHIRExprNum("opt");
    RemoveUnusedImports(false);
    DoClosureConversion();
    RecordCHIRExprNum("cc");
    CreateBoxTypeForRecursionValueType();
    if (!RunConstantEvaluation()) {
        return false;
    }
    RemoveUnusedImports(true);

    // annotation check depends on const eval
    if (!RunAnnotationChecks()) {
        return false;
    }
    RunSanitizerCoverage();
    if (opts.enIncrementalCompilation) {
        UpdateEffectMapToString(effectMap, strEffectMap);
    }

    UpdatePosOfMacroExpandNode();
    EraseDebugExpr();
    CFFIFuncWrapper();
    RecordCHIRExprNum("CHIR stage");
    RecordCodeInfoAtTheEnd();
    if (!RunIRChecker(Phase::OPT)) {
        return false;
    }
    if (opts.emitCHIRPhase == GlobalOptions::CandidateEmitCHIRPhase::OPT) {
        EmitCHIR(outputPath, *chirPkg, Phase::OPT, opts.chirDumpDebugMode);
    } else if (opts.saveTemps) {
        auto tempFileInfo =
            TempFileManager::Instance().CreateNewFileInfo({.fileName = chirPkg->GetName()}, TempFileKind::O_CHIR);
        CHIRSerializer::Serialize(*chirPkg, tempFileInfo.filePath, Phase::OPT);
    }
    return true;
}

namespace {
Cangjie::Position ConvertCHIRPos2ASTPos(unsigned int fileId, const Position& pos)
{
    return Cangjie::Position{fileId, static_cast<int>(pos.line), static_cast<int>(pos.column)};
}
Position ConvertASTPos2CHIRPos(const Cangjie::Position& pos)
{
    return Position{static_cast<unsigned int>(pos.line), static_cast<unsigned int>(pos.column)};
}
} // namespace

void ToCHIR::UpdatePosOfMacroExpandNode()
{
    if (!opts.enableCompileDebug && !opts.displayLineInfo) {
        return;
    }
    Utils::ProfileRecorder recorder("CHIR", "UpdatePosOfMacroExpandNode");
    for (auto& func : chirPkg->GetGlobalFuncs()) {
        Visitor::Visit(*func, [this](Expression& expression) {
            auto& pos = expression.GetDebugLocation();
            if (pos.IsInvalidMacroPos()) {
                return VisitResult::CONTINUE;
            }
            auto begin = ConvertCHIRPos2ASTPos(pos.GetFileID(), pos.GetBeginPos());
            auto key = static_cast<uint64_t>(begin.Hash64());
            const auto it = std::as_const(diag.posRange2MacroCallMap).lower_bound(key);
            if (it == diag.posRange2MacroCallMap.cend()) {
                // means this expression is not from macro expanded ast node.
                return VisitResult::CONTINUE;
            }
            if (auto macrocall = it->second; macrocall) {
                auto modifiedPos = pos;
                modifiedPos.SetBeginPos(ConvertASTPos2CHIRPos(macrocall->GetDebugPos(begin)));
                auto end = ConvertCHIRPos2ASTPos(pos.GetFileID(), pos.GetEndPos());
                modifiedPos.SetEndPos(ConvertASTPos2CHIRPos(macrocall->GetDebugPos(end)));
                expression.SetDebugLocation(modifiedPos);
            }
            return VisitResult::CONTINUE;
        });
    }
}

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
bool ToCHIR::PerformPlugin(CHIR::Package& package)
{
    bool succeed = true;
    bool hasPluginForCHIR = false;
#ifndef CANGJIE_ENABLE_GCOV
    try {
#endif
        Utils::ProfileRecorder recorder("CHIR", "Plugin Execution");
        CHIRPluginManager chirPluginManager = ci.metaTransformPluginBuilder.BuildCHIRPluginManager(builder);
        chirPluginManager.ForEachMetaTransformConcept([&package, &hasPluginForCHIR](MetaTransformConcept& mtc) {
            if (!mtc.IsForCHIR()) {
                return;
            }
            hasPluginForCHIR = true;
            if (mtc.IsForFunc()) {
                for (auto func : package.GetGlobalFuncs()) {
                    static_cast<MetaTransform<CHIR::Func>*>(&mtc)->Run(*func);
                }
            } else if (mtc.IsForPackage()) {
                static_cast<MetaTransform<CHIR::Package>*>(&mtc)->Run(package);
            } else {
                CJC_ASSERT(false && "Should not reach here.");
            }
        });
#ifndef CANGJIE_ENABLE_GCOV
    } catch (...) {
        succeed = false;
    }
#endif
    if (!succeed) {
        diag.DiagnoseRefactor(DiagKindRefactor::plugin_throws_exception, DEFAULT_POSITION);
    } else if (hasPluginForCHIR && builder.IsEnableIRCheckerAfterPlugin()) {
        DumpCHIRDebug("PLUGIN");
        Utils::ProfileRecorder rec2("CHIR", "IRCheck after plugins");
        succeed = IRCheck(package, opts, builder, Phase::PLUGIN);
    }
    return succeed;
}
#endif

bool ToCHIR::TranslateToCHIR(std::vector<const AST::Decl*>&& annoOnly)
{
    Utils::ProfileRecorder recorder("CHIR", "AST to CHIR Translation");
    std::unordered_map<AST::Ty*, Type*> typeMap;
    CHIR::CHIRTypeCache chirTypeCache(typeMap);
    auto chirType = std::make_unique<CHIRType>(builder, chirTypeCache);
    auto ast2chirBuilder = AST2CHIR::AST2CHIRBuilder();
    auto ast2CHIR = ast2chirBuilder.SetGenericInstantiationManager(gim)
                        ->SetImportManager(importManager)
                        ->SetSourceManager(sourceManager)
                        ->SetTypeManager(*typeManager)
                        ->SetGlobalOptions(opts)
                        ->SetIncreKind(kind)
                        ->SetCachedInfo(cachedInfo)
                        ->SetDiag(diag)
                        ->SetCHIRBuilder(builder)
                        ->SetCHIRType(*chirType)
                        ->SetOutputPath(outputPath)
                        ->SetComputeAnnotations(isComputingAnnos)
                        ->Build();
    ast2CHIR.SetAnnoOnlyDecls(std::move(annoOnly));
    auto res = ast2CHIR.ToCHIRPackage(pkg);
    if (diag.GetErrorCount() != 0 || !res) {
        return false;
    }
    chirPkg = ast2CHIR.GetPackage();
    DumpCHIRDebug("AST_CHIR");
    if (!opts.enIncrementalCompilation && !RunIRChecker(Phase::RAW)) {
        return false;
    }
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    /// ===============   Meta Transformation for CHIR  ===============
    PerformPlugin(*ast2CHIR.GetPackage());
#endif
    /// ===============================================================
    if (opts.enIncrementalCompilation) {
        curVirtFuncWrapDep = ast2CHIR.GetCurVirtualFuncWrapperDepForIncr();
        delVirtFuncWrapForIncr = ast2CHIR.GetDeleteVirtualFuncWrapperForIncr();
    }

    srcCodeImportedFuncs = ast2CHIR.GetSrcCodeImportedFuncs();
    srcCodeImportedVars = ast2CHIR.GetSrcCodeImportedVars();
    implicitFuncs = ast2CHIR.GetImplicitFuncs();
    initFuncsForConstVar = ast2CHIR.GetInitFuncsForConstVar();
    maybeUnreachable = ast2CHIR.GetMaybeUnreachableBlocks();
    if (isComputingAnnos) {
        annoFactoryFuncs = ast2CHIR.GetAnnoFactoryFuncs();
        globalNominalCache = std::move(chirTypeCache.globalNominalCache);
    }

    for (auto& file : pkg.files) {
        for (auto& macrocall : file->originalMacroCallNodes) {
            auto key = static_cast<uint64_t>(macrocall->begin.Hash64());
            diag.posRange2MacroCallMap[key] = macrocall.get();
            key = static_cast<uint64_t>(macrocall->end.Hash64());
            diag.posRange2MacroCallMap[key] = nullptr;
        }
    }
    return true;
}

VarInitDepMap ToCHIR::GetVarInitDepMap() const
{
    VarInitDepMap dep;
    for (auto var : chirPkg->GetGlobalVars()) {
        // local const doesn't have raw mangle name
        if (var->GetRawMangledName().empty()) {
            continue;
        }
        // some simple init doesn't have init func, e.g literal init
        if (!var->GetInitFunc()) {
            continue;
        }
        dep[var->GetRawMangledName()] = var->GetInitFunc()->GetIdentifierWithoutPrefix();
    }
    return dep;
}

} // namespace Cangjie::CHIR
