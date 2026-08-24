// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/CHIR.h"

#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/CHIR/Analysis/CallGraphAnalysis.h"
#include "cangjie/CHIR/Analysis/ConstAnalysisWrapper.h"
#include "cangjie/CHIR/Analysis/DevirtualizationInfo.h"
#include "cangjie/CHIR/Checker/CHIRChecker.h"
#include "cangjie/CHIR/Checker/AnnotationChecker.h"
#include "cangjie/CHIR/Checker/UnreachableBranchCheck.h"
#include "cangjie/CHIR/Checker/VarInitCheck.h"
#include "cangjie/CHIR/Interpreter/ConstEval.h"
#include "cangjie/CHIR/Optimization/ArrayLambdaOpt.h"
#include "cangjie/CHIR/Optimization/ArrayListConstStartOpt.h"
#include "cangjie/CHIR/Optimization/ConstPropagation.h"
#include "cangjie/CHIR/Optimization/DeadCodeElimination.h"
#include "cangjie/CHIR/Optimization/Devirtualization.h"
#include "cangjie/CHIR/Optimization/DevirtualizeTrivialStatics.h"
#include "cangjie/CHIR/Optimization/FunctionInline.h"
#include "cangjie/CHIR/Optimization/GetRefToArrayElem.h"
#include "cangjie/CHIR/Optimization/MergeBlocks.h"
#include "cangjie/CHIR/Optimization/OptFuncRetType.h"
#include "cangjie/CHIR/Optimization/RangePropagation.h"
#include "cangjie/CHIR/Optimization/RedundantFutureRemoval.h"
#include "cangjie/CHIR/Optimization/RedundantGetOrThrowElimination.h"
#include "cangjie/CHIR/Optimization/RedundantLoadElimination.h"
#include "cangjie/CHIR/Optimization/UnitUnify.h"
#include "cangjie/CHIR/Optimization/UselessAllocateElimination.h"
#include "cangjie/CHIR/Serializer/CHIRDeserializer.h"
#include "cangjie/CHIR/Serializer/CHIRSerializer.h"
#include "cangjie/CHIR/Transformation/BoxRecursionValueType.h"
#include "cangjie/CHIR/Transformation/ClosureConversion.h"
#include "cangjie/CHIR/Transformation/ExecutePlugin.h"
#include "cangjie/CHIR/Transformation/FlatForInExpr.h"
#include "cangjie/CHIR/Transformation/GenerateVTable/GenerateVTable.h"
#include "cangjie/CHIR/Transformation/MarkClassHasInited.h"
#include "cangjie/CHIR/Transformation/MetaTransform.h"
#include "cangjie/CHIR/Transformation/NoSideEffectMarker.h"
#include "cangjie/CHIR/Transformation/ReplaceSrcCodeImportedVal.h"
#include "cangjie/CHIR/Transformation/SanitizerCoverage.h"
#include "cangjie/CHIR/Transformation/SetMemRegion.h"
#include "cangjie/CHIR/Transformation/UpdateMemberVarPath.h"
#include "cangjie/CHIR/Utils/CHIRPrinter.h"
#include "cangjie/CHIR/Utils/Utils.h"
#include "cangjie/CHIR/Utils/Visitor/Visitor.h"
#include "cangjie/Driver/TempFileManager.h"
#include "cangjie/Utils/CheckUtils.h"
#include <unordered_set>
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
    if (!value.IsFuncWithBody()) {
        return false;
    }
    const auto func = StaticCast<const Function*>(&value);
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
    return StaticCast<const GlobalValue*>(&value)->GetRawMangledName();
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
                auto parentClass = StaticCast<Function*>(*effectedIt)->GetParentCustomTypeDef();
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
            auto parentClass = StaticCast<Function*>(mapIt->first)->GetParentCustomTypeDef();
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

void ToCHIR::DumpCHIRToFile(const std::string& suffix, bool needCheckFlag)
{
    if (needCheckFlag && !opts.NeedDumpCHIRToFile()) {
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
        debugDir = FileUtil::JoinPath(outputPath, chirPkg->GetName() + "_CHIR");
    } else {
        debugDir = FileUtil::GetFileBase(outputPath) + "_CHIR";
    }
    static bool clearDumpDir = false;
    if (!clearDumpDir) {
        if (FileUtil::FileExist(debugDir)) {
            FileUtil::RemoveDirectoryRecursively(debugDir);
        }
        clearDumpDir = true;
    }
    std::string fullPath = FileUtil::JoinPath(debugDir, fileName);
    if (!FileUtil::FileExist(fullPath)) {
        FileUtil::CreateDirs(fullPath);
    }
    CHIRPrinter::PrintPackage(*chirPkg, fullPath);
    if (suffix == "Broken_CHIR") {
        printf("broken chir dump to file: %s\n", fullPath.c_str());
    }
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
    DumpCHIRToFile("ClosureConversion");
}

void ToCHIR::UnreachableBlockReporter()
{
    Utils::ProfileRecorder recorder("RulesChecking", "UnreachableBlockWarningReporter");
    auto dce = DeadCodeElimination(builder, diag, *chirPkg);
    dce.UnreachableBlockWarningReporter(*chirPkg, opts.GetJobs(), maybeUnreachable);
}

void ToCHIR::UnreachableBlockElimination()
{
    Utils::ProfileRecorder recorder("RulesChecking", "UnreachableBlockElimination");
    auto dce = DeadCodeElimination(builder, diag, *chirPkg);
    dce.UnreachableBlockElimination(*chirPkg, opts.chirDebugOptimizer);
    DumpCHIRToFile("UnreachableBlockElimination");
}

void ToCHIR::NothingTypeExprElimination()
{
    Utils::ProfileRecorder recorder("RulesChecking", "NothingTypeExprElimination");
    auto dce = DeadCodeElimination(builder, diag, *chirPkg);
    dce.NothingTypeExprElimination(*chirPkg, opts.chirDebugOptimizer);
    DumpCHIRToFile("NothingTypeExprElimination");
}

void ToCHIR::UnreachableBranchReporter()
{
    Utils::ProfileRecorder recorder("RulesChecking", "UnreachableBranchReporter");
    auto check = CHIR::UnreachableBranchCheck(&constAnalysisWrapper, diag, chirPkg->GetName());
    check.RunOnPackage(*chirPkg, opts.GetJobs());
}

void ToCHIR::UselessExprElimination()
{
    if (!opts.IsCHIROptimizationLevelOverO2()) {
        return;
    }
    Utils::ProfileRecorder recorder("RulesChecking", "UselessExprElimination");
    auto dce = DeadCodeElimination(builder, diag, *chirPkg);
    dce.UselessExprElimination(*chirPkg, opts.chirDebugOptimizer);
    DumpCHIRToFile("UselessExprElimination");
}

void ToCHIR::UselessFuncElimination(const std::string& passName)
{
    if (!opts.IsCHIROptimizationLevelOverO2() || opts.enableCoverage || opts.enIncrementalCompilation ||
        chirPkg->GetName() == Cangjie::REFLECT_PACKAGE_NAME) {
        return;
    }
    Utils::ProfileRecorder recorder(passName, "UselessFuncElimination");
    auto dce = DeadCodeElimination(builder, diag, *chirPkg);
    dce.UselessFuncElimination(*chirPkg, opts);
    DumpCHIRToFile("UselessFuncElimination");
}

void ToCHIR::ReportUnusedCode()
{
    Utils::ProfileRecorder recorder("RulesChecking", "ReportUnusedCode");
    auto dce = DeadCodeElimination(builder, diag, *chirPkg);
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
    DumpCHIRToFile("RedundantLoadElimination");
}

void ToCHIR::UselessAllocateElimination()
{
    if (!opts.IsCHIROptimizationLevelOverO2()) {
        return;
    }
    Utils::ProfileRecorder recorder("CHIR Opt", "UselessAllocateElimination");
    UselessAllocateElimination::RunOnPackage(*chirPkg, opts.chirDebugOptimizer);
    DumpCHIRToFile("UselessAllocateElimination");
}

void ToCHIR::RunGetRefToArrayElemOpt()
{
    if (!opts.IsCHIROptimizationLevelOverO2() || opts.interpFullBchir) {
        return;
    }
    Utils::ProfileRecorder recorder("CHIR Opt", "ArrayGetRefOpt");
    GetRefToArrayElem::RunOnPackage(*chirPkg, builder);
    DumpCHIRToFile("ArrayGetRefOpt");
}

void ToCHIR::Devirtualization(DevirtualizationInfo& devirtInfo)
{
    if (!opts.IsOptimizationExisted(GlobalOptions::OptimizationFlag::DEVIRTUALIZATION) ||
        opts.enIncrementalCompilation) {
        return;
    }

    Utils::ProfileRecorder recorder("CHIR Opt", "Devirtualization");
    devirtInfo.FreshRetMap();
    devirtInfo.CollectConstMemberVarType();
    auto funcs = CHIR::Devirtualization::CollectContainInvokeExprFuncs(chirPkg);
    // Only process functions containing invoke expressions.
    if (funcs.empty()) {
        return;
    }
    size_t threadNum = opts.GetJobs();
    TypeValue::SetCHIRBuilder(&builder);
    AnalysisWrapper<TypeAnalysis, TypeDomain> typeAnalysisWrapper(builder);
    typeAnalysisWrapper.RunOnPackage(chirPkg, opts.chirDebugOptimizer, threadNum, devirtInfo);
    auto devirt = CHIR::Devirtualization(&typeAnalysisWrapper, devirtInfo);
    devirt.RunOnFuncs(funcs, builder, opts.chirDebugOptimizer);
    auto devirtStatics = CHIR::DevirtualizeTrivialStatics();
    devirtStatics.RunOnPackage(*chirPkg, builder, opts.chirDebugOptimizer);

    // if get frozen inst funcs after first devirtualization, opt them in the second round
    if (!devirt.GetFrozenInstFuns().empty()) {
        auto& frozenFuncs = devirt.GetFrozenInstFuns();
        for (auto& func : frozenFuncs) {
            auto res = typeAnalysisWrapper.RunOnFunc(func, opts.chirDebugOptimizer, devirtInfo);
            devirt.AppendFrozenFuncState(func, std::move(res));
        }
        devirt.RunOnFuncs(frozenFuncs, builder, opts.chirDebugOptimizer);

        auto pass = FunctionInline(
            builder, opts.optimizationLevel, opts.chirDebugOptimizer);
        for (auto& func : frozenFuncs) {
            pass.Run(*func);
        }
    }
    DumpCHIRToFile("Devirtualization");
}

void ToCHIR::RedundantGetOrThrowElimination()
{
    if (!opts.enableChirRGetOrThrowE) {
        return;
    }
    Utils::ProfileRecorder recorder("CHIR Opt", "RedundantGetOrThrowElimination");
    auto rGetOrThrowE = CHIR::RedundantGetOrThrowElimination();
    rGetOrThrowE.RunOnPackage(chirPkg, opts.chirDebugOptimizer);
    DumpCHIRToFile("RedundantGetOrThrowElimination");
}

void ToCHIR::FlatForInExpr()
{
    Utils::ProfileRecorder recorder("AST to CHIR Translation", "FlatForInExpr");
    auto flatForInExpr = CHIR::FlatForInExpr(builder);
    flatForInExpr.RunOnPackage(*chirPkg);
    DumpCHIRToFile("FlatForInExpr");
}

bool ToCHIR::RunVarInitChecking()
{
    Utils::ProfileRecorder recorder("RulesChecking", "VarInitCheck");
    auto vic = CHIR::VarInitCheck(diag);
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
    auto pass = ArrayListConstStartOpt(builder, functionInline);
    pass.RunOnPackage(*chirPkg);
    MergeEffectMap(pass.GetEffectMap(), effectMap);
    DumpCHIRToFile("RunArrayListConstStartOpt");
}

void ToCHIR::RunFunctionInline(DevirtualizationInfo& devirtInfo)
{
    if (!opts.IsOptimizationExisted(GlobalOptions::OptimizationFlag::FUNC_INLINING)) {
        return;
    }
    Utils::ProfileRecorder::Start("CHIR Opt", "FunctionInline");
    CallGraphAnalysis callGraphAnalysis(chirPkg, devirtInfo);
    // Collect all call graph information.
    callGraphAnalysis.DoCallGraphAnalysis(false);
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
    DumpCHIRToFile("FunctionInline");
    Utils::ProfileRecorder::Stop("CHIR Opt", "FunctionInline");

    RunMergingBlocks("CHIR Opt", "MergingBlockAfterInline");

    // do useless function elimination after inline
    UselessFuncElimination("CHIR Opt");
}

void ToCHIR::RunUnreachableMarkBlockRemoval()
{
    Utils::ProfileRecorder recorder("RulesChecking", "Clear Blocks Marked as Unreachable");
    auto dce = DeadCodeElimination(builder, diag, *chirPkg);
    dce.ClearUnreachableMarkBlock(*chirPkg);
    DumpCHIRToFile("ClearBlocksMarkAsUnreachable");
}

void ToCHIR::RunMergingBlocks(const std::string& firstName, const std::string& secondName)
{
    Utils::ProfileRecorder recorder(firstName, secondName);
    MergeBlocks::RunOnPackage(*chirPkg, builder, opts);
    DumpCHIRToFile(secondName);
}

void ToCHIR::RunConstantAnalysis()
{
    Utils::ProfileRecorder recorder("RulesChecking", "Constant Analysis");
    constAnalysisWrapper.RunOnPackage(chirPkg, opts.chirDebugOptimizer, opts.GetJobs(), diag);
}

void ToCHIR::RunConstantPropagation()
{
    Utils::ProfileRecorder recorder("RulesChecking", "Constant Propagation & Safety Check");
    size_t threadNum = opts.GetJobs();
    DeadCodeElimination dce(builder, diag, *chirPkg);
    if (threadNum == 1) {
        auto cp = CHIR::ConstPropagation(builder, &constAnalysisWrapper, opts);
        MergeEffectMap(cp.GetEffectMap(), effectMap);
        cp.RunOnPackage(chirPkg, ci.isCJLint);
        dce.UnreachableBlockElimination(cp.GetFuncsNeedRemoveBlocks(), opts.chirDebugOptimizer);
    } else {
        bool isCJLint = ci.isCJLint;
        std::vector<Function*> globalFuncs = chirPkg->GetGlobalFuncsWithBody();
        size_t funcNum = globalFuncs.size();
        std::vector<std::unique_ptr<CHIR::CHIRBuilder>> builderList = ConstructSubBuilders(threadNum, funcNum);
        Utils::TaskQueue taskQueue(threadNum);
        std::vector<std::unique_ptr<CHIR::ConstPropagation>> cpList;
        for (size_t idx = 0; idx < funcNum; ++idx) {
            auto func = globalFuncs.at(idx);
            auto cp = std::make_unique<CHIR::ConstPropagation>(*builderList[idx], &constAnalysisWrapper, opts);
            taskQueue.AddTask<void>([constPropagation = cp.get(), func, isCJLint]() {
                return constPropagation->RunOnFunc(func, isCJLint);
            });
            cpList.emplace_back(std::move(cp));
        }
        taskQueue.RunAndWaitForAllTasksCompleted();
        for (auto& cp : cpList) {
            MergeEffectMap(cp->GetEffectMap(), effectMap);
            dce.UnreachableBlockElimination(cp->GetFuncsNeedRemoveBlocks(), opts.chirDebugOptimizer);
        }
        for (auto& subBd : builderList) {
            (*subBd).MergeAllocatedInstance();
        }
        builder.GetChirContext().MergeTypes();
    }
    DumpCHIRToFile("ConstantPropagation");
}

void ToCHIR::RunRangePropagation()
{
    if (!opts.IsCHIROptimizationLevelOverO2()) {
        return;
    }
    Utils::ProfileRecorder::Start("CHIR Opt", "Range Propagation");
    AnalysisWrapper<RangeAnalysis, RangeDomain> vra(builder);
    vra.RunOnPackage(chirPkg, opts.chirDebugOptimizer, opts.GetJobs(), diag);
    size_t threadNum = opts.GetJobs();
    DeadCodeElimination dce(builder, diag, *chirPkg);
    if (threadNum == 1) {
        auto cp = CHIR::RangePropagation(builder, &vra, diag, opts.enIncrementalCompilation);
        cp.RunOnPackage(chirPkg, opts.chirDebugOptimizer);
        MergeEffectMap(cp.GetEffectMap(), effectMap);
        dce.UnreachableBlockElimination(cp.GetFuncsNeedRemoveBlocks(), opts.chirDebugOptimizer);
    } else {
        bool isDebug = opts.chirDebugOptimizer;
        std::vector<Function*> globalFuncs = chirPkg->GetGlobalFuncsWithBody();
        size_t funcNum = globalFuncs.size();
        std::vector<std::unique_ptr<CHIR::CHIRBuilder>> builderList =
            CHIR::ToCHIR::ConstructSubBuilders(threadNum, funcNum);
        Utils::TaskQueue taskQueue(threadNum);
        std::vector<std::unique_ptr<CHIR::RangePropagation>> cpList;
        for (size_t idx = 0; idx < funcNum; ++idx) {
            auto func = globalFuncs.at(idx);
            auto cp = std::make_unique<CHIR::RangePropagation>(
                *builderList[idx], &vra, diag, opts.enIncrementalCompilation);
            taskQueue.AddTask<void>(
                [rangePropagation = cp.get(), func, isDebug]() { return rangePropagation->RunOnFunc(func, isDebug); });
            cpList.emplace_back(std::move(cp));
        }
        taskQueue.RunAndWaitForAllTasksCompleted();
        for (auto& cp : cpList) {
            MergeEffectMap(cp->GetEffectMap(), effectMap);
            dce.UnreachableBlockElimination(cp->GetFuncsNeedRemoveBlocks(), opts.chirDebugOptimizer);
        }
        for (auto& subBd : builderList) {
            (*subBd).MergeAllocatedInstance();
        }
        builder.GetChirContext().MergeTypes();
    }
    DumpCHIRToFile("RangePropagation");
    Utils::ProfileRecorder::Stop("CHIR Opt", "Range Propagation");

    RunMergingBlocks("CHIR Opt", "MergingBlockAfterRangeAnalysis");
}

void ToCHIR::RunArrayLambdaOpt()
{
    if (!opts.IsOptimizationExisted(GlobalOptions::OptimizationFlag::ARRAY_LAMBDA_OPT)) {
        return;
    }
    Utils::ProfileRecorder recorder("CHIR Opt", "ArrayLambdaOpt");
    auto arrayLambdaOpt = CHIR::ArrayLambdaOpt(builder);
    arrayLambdaOpt.RunOnPackage(*chirPkg);
    DumpCHIRToFile("ArrayLambdaOpt");
}

void ToCHIR::RunRedundantFutureOpt()
{
    if (!opts.IsOptimizationExisted(GlobalOptions::OptimizationFlag::REDUNDANT_FUTURE)) {
        return;
    }
    Utils::ProfileRecorder recorder("CHIR Opt", "RedundantFutureOpt");
    RedundantFutureRemoval(*chirPkg, opts.chirDebugOptimizer).RunOnPackage();
    DumpCHIRToFile("RedundantFutureOpt");
}

void ToCHIR::RunSanitizerCoverage()
{
    if (!opts.IsSancovOptionEnabled()) {
        return;
    }
    Utils::ProfileRecorder recorder("CHIR", "Sanitizer Coverage");
    auto sanCovOpt = CHIR::SanitizerCoverage(opts, builder);
    sanCovOpt.RunOnPackage(chirPkg, diag, opts.chirDebugOptimizer);
    DumpCHIRToFile("Sanitizer_Coverage");
}

void ToCHIR::RunUnitUnify()
{
    if (!opts.IsCHIROptimizationLevelOverO2()) {
        return;
    }
    Utils::ProfileRecorder recorder("CHIR Opt", "Unit Unify");
    auto unitUnify = CHIR::UnitUnify(builder);
    unitUnify.RunOnPackage(chirPkg, opts.chirDebugOptimizer);
    DumpCHIRToFile("Unit_Unify");
}

DevirtualizationInfo ToCHIR::CollectDevirtualizationInfo()
{
    Utils::ProfileRecorder recorder("CHIR Opt", "Collect Devirt Info");
    DevirtualizationInfo devirtInfo(chirPkg, opts);
    if (opts.IsOptimizationExisted(GlobalOptions::OptimizationFlag::FUNC_INLINING) ||
        (opts.IsOptimizationExisted(GlobalOptions::OptimizationFlag::DEVIRTUALIZATION) &&
            !opts.enIncrementalCompilation)) {
        // Collect all inheritance tree information.
        devirtInfo.CollectInfo();
    }
    return devirtInfo;
}

void ToCHIR::OptimizeFuncReturnType()
{
    Utils::ProfileRecorder recorder("CHIR Opt", "Optimize Function Return Type");
    OptFuncRetType optFuncRetType(*chirPkg, builder);
    optFuncRetType.Unit2Void();
    DumpCHIRToFile("OptimizeFuncReturnType");
}

void ToCHIR::RunOptimizationPass()
{
    Utils::ProfileRecorder recorder("CHIR", "CHIR Opt");
    OptimizeFuncReturnType();
    RunArrayListConstStartOpt();
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
    RunGetRefToArrayElemOpt();
}

void ToCHIR::SetMemoryRegion()
{
    Utils::ProfileRecorder recorder("CHIR", "SetMemoryRegion");
    auto setMemRegion = SetMemRegion(builder);
    setMemRegion.RunOnPackage(*chirPkg);
    DumpCHIRToFile("SetMemoryRegion");
}

bool ToCHIR::RunConstantEvaluation()
{
    if (!opts.IsConstEvalEnabled() || opts.enIncrementalCompilation || !opts.commonPartCjos.empty()) {
        return true;
    }
    Utils::ProfileRecorder recorder("CHIR", "Constant Evaluation");
    auto ce = CHIR::Interpreter::ConstEvalPass(ci, builder, sourceManager, opts, diagEngine);
    std::vector<Cangjie::CHIR::Interpreter::Bchir> bchirPackages;
    ce.RunOnPackage(*chirPkg, initFuncsForConstVar, bchirPackages);
    DumpCHIRToFile("after_const_eval");
    return true;
}

bool ToCHIR::RunIRChecker(const Phase& phase)
{
    if (!opts.chirWFC) {
        return true;
    }
    // skip CJMP common package
    if (opts.experimentalMode && opts.outputMode == GlobalOptions::OutputMode::CHIR) {
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
        default:
            CJC_ABORT();
    }
    Utils::ProfileRecorder recorder("CHIR", "IRCheck " + suffix);
    CJC_NULLPTR_CHECK(chirPkg);
    auto checker = CHIRChecker(*chirPkg, opts, builder);
    std::unordered_set<CHIRChecker::Rule> rules;
    // after AST2CHIR, there are many empty block, we need to clean them
    if (phase != Phase::RAW && phase != Phase::PLUGIN) {
        rules.emplace(CHIRChecker::Rule::EMPTY_BLOCK);
    }
    // we need to translate correct InvokeStatic, but after function inline,
    // some InvokeStatic may be optimized to Apply, we will handle it later
    if (phase == Phase::RAW) {
        rules.emplace(CHIRChecker::Rule::CHIR_GET_RTTI_STATIC_TYPE);
    }
    // CodeGen shouldn't know `GetInstantiateValue`, this expression should gone after closure conversion
    if (phase == Phase::OPT) {
        rules.emplace(CHIRChecker::Rule::GET_INSTANTIATE_VALUE_SHOULD_GONE);
        rules.emplace(CHIRChecker::Rule::RETURN_TYPE_NEED_BE_VOID);
    } else {
        rules.emplace(CHIRChecker::Rule::IMPORTED_CONST_VAR_SHOULD_HAVE_INITIALIZER);
    }
    // there may be something wrong, we will check this rule after CJMP's scheme done
    if (opts.commonPartCjos.size() <= 0) {
        rules.emplace(CHIRChecker::Rule::CHECK_FUNC_BODY);
    }
    auto ok = checker.CheckPackage(rules);
    if (!ok) {
        // print to file anyway
        DumpCHIRToFile("Broken_CHIR", false);
    }
    return ok;
}

void ToCHIR::RecordCodeInfoAtTheBegin()
{
    if (!opts.enableTimer && !opts.enableMemoryCollect) {
        return;
    }
    Utils::ProfileRecorder recorder("CHIR", "RecordCodeInfo");
    CJC_NULLPTR_CHECK(pkg);
    std::function<int64_t(void)> getASTNodeQuantity = [this]() -> int64_t {
        int64_t astNodeCnt = 0;
        AST::Walker(pkg, [&astNodeCnt](auto /* node */) {
            astNodeCnt++;
            return AST::VisitAction::WALK_CHILDREN;
        }).Walk();
        return astNodeCnt;
    };
    Utils::ProfileRecorder::RecordCodeInfo("AST node", getASTNodeQuantity);
    Utils::ProfileRecorder::RecordCodeInfo(
        "generic ins func in AST", static_cast<int64_t>(pkg->genericInstantiatedDecls.size()));
    std::function<int64_t(void)> getGenericInstantiatedAstNode = [this]() -> int64_t {
        int64_t astNodeCnt = 0;
        for (auto& decl : pkg->genericInstantiatedDecls) {
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
    Utils::ProfileRecorder::RecordCodeInfo("src file", static_cast<int64_t>(pkg->files.size()));
    std::function<int64_t(void)> getCurPkgInstantiatedAstNode = [this]() -> int64_t {
        int64_t astNodeCnt = 0;
        for (auto& decl : pkg->genericInstantiatedDecls) {
            auto genericDecl = decl->genericDecl;
            CJC_NULLPTR_CHECK(genericDecl->curFile);
            if (genericDecl->curFile->curPackage->fullPackageName != pkg->fullPackageName) {
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
        "global func after CHIR stage", static_cast<int64_t>(chirPkg->GetGlobalFuncsWithBody().size()));
    int64_t wrapperFuncNum = 0;
    int64_t funcExprNum = 0;
    int64_t wrapperFuncExprNum = 0;
    for (auto func : chirPkg->GetGlobalFuncsWithBody()) {
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
        "valid CHIR func num after " + suffix, static_cast<int64_t>(chirPkg->GetGlobalFuncsWithBody().size()));

    int64_t allNonGenericExprsNum = 0;
    int64_t allInstantiatedExprsNum = 0;
    int64_t curPkgInstantiatedExprsNum = 0;
    for (auto func : chirPkg->GetGlobalFuncsWithBody()) {
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
    for (auto func : chirPkg->GetGlobalFuncsWithBody()) {
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
    RunConstantPropagation();
    if (diag.GetErrorCount() > 0) {
        return false;
    }
    constAnalysisWrapper.InvalidateAllAnalysisResults();
    RunConstantAnalysis();
    return true;
}

void ToCHIR::EraseDebugExpr()
{
    Utils::ProfileRecorder recorder("CHIR", "EraseUselessDebugExpr");
    // Erase useless debug expressions for codegen in not -g mode,
    // For the reason of error when enable parallel mode in codegen.
    // These expressions are needed for report warning, so only can be removed at the end of CHIR stage.
    for (auto func : chirPkg->GetGlobalFuncsWithBody()) {
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
    DumpCHIRToFile("EraseUselessDebugExpr");
}

void ToCHIR::CFFIFuncWrapper()
{
    Utils::ProfileRecorder recorder("CHIR", "CFFIFuncWrapper");
    std::vector<Function*> cfuncs;
    std::vector<Function*> foreignFuncs;
    for (auto curFunc : chirPkg->GetGlobalFuncsWithBody()) {
        if (curFunc->GetType()->IsCFunc()) {
            cfuncs.emplace_back(curFunc);
        }
    }
    for (auto foreignFunc : chirPkg->GetGlobalFuncsWithoutBody()) {
        if (foreignFunc->GetType()->IsCFunc()) {
            CJC_ASSERT(foreignFunc->IsImportedFunc());
            foreignFuncs.emplace_back(StaticCast<Function*>(foreignFunc));
        }
    }
    for (auto curFunc : cfuncs) {
        if (curFunc->Get<LinkTypeInfo>() != Linkage::EXTERNAL) {
            if (!IsAllApply(curFunc)) {
                auto [wrapperFunc, res] = DoCFFIFuncWrapper(*curFunc, false, false);
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
        const auto& funcPkgName = curFunc->GetPackageName().empty() ? chirPkg->GetName() : curFunc->GetPackageName();
        if (funcPkgName != chirPkg->GetName()) {
            // NOTE: res maybe null!
            ReplaceUsesWithWrapper(*curFunc, res, *wrapperFunc, true);
        }
    }
    DumpCHIRToFile("CFFIFuncWrapper");
}

template <typename T>
std::pair<Value*, Apply*> ToCHIR::DoCFFIFuncWrapper(T& curFunc, bool isForeign, bool isExternal)
{
    auto ident = curFunc.GetSrcCodeIdentifier();
    if (curFunc.IsLambda()) {
        ident = curFunc.GetIdentifierWithoutPrefix();
        auto pos = ident.find(CFFI_FUNC_SUFFIX);
        if (pos != std::string::npos) {
            ident.erase(pos, CFFI_FUNC_SUFFIX.size());
        }
    }
    const auto& funcPkgName = curFunc.GetPackageName().empty() ? chirPkg->GetName() : curFunc.GetPackageName();
    if (isForeign || (curFunc.TestAttr(Attribute::PRIVATE) && isExternal)) {
        ident = funcPkgName + ":" + ident;
    }
    if (funcPkgName != chirPkg->GetName()) {
        auto wrapperFunc = builder.CreateFunction(
            curFunc.GetFuncType(), ident, ident, "", funcPkgName, curFunc.GetGenericTypeParams());
        wrapperFunc->EnableAttr(Attribute::IMPORTED);
        wrapperFunc->SetCFFIWrapper(true);
        return std::make_pair(wrapperFunc, nullptr);
    }
    auto wrapperFunc =
        builder.CreateFunction(curFunc.GetFuncType(), ident, ident, "", funcPkgName);
    wrapperFunc->SetDebugLocation(curFunc.GetDebugLocation());
    wrapperFunc->SetFuncKind(curFunc.GetFuncKind());
    if (wrapperFunc->IsLambda()) {
        auto originalLambdaInfo = FuncSigInfo {
            .funcName = curFunc.GetSrcCodeIdentifier(),
            .funcType = curFunc.GetOriginalLambdaType(),
            .genericTypeParams = curFunc.GetGenericTypeParams()
        };
        wrapperFunc->SetOriginalLambdaInfo(originalLambdaInfo);
    }
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
            users[index]->GetExprKind() != ExprKind::TRY_APPLY) {
            return false;
        }
        if (users[index]->GetExprKind() == ExprKind::APPLY) {
            auto applyNode = StaticCast<Apply*>(users[index]);
            if (applyNode->GetCallee() != curFunc) {
                return false;
            }
        }
        if (users[index]->GetExprKind() == ExprKind::TRY_APPLY) {
            auto applyNode = StaticCast<TryApply*>(users[index]);
            if (applyNode->GetCallee() != curFunc) {
                return false;
            }
        }
    }
    return true;
}

namespace {
void EmitCHIR(const std::string& outputPath, const CHIR::Package& package, ToCHIR::Phase phase, bool dump)
{
    std::string path;
    if (FileUtil::IsDir(outputPath)) {
        path = FileUtil::JoinPath(outputPath, package.GetName() + std::string(CHIR_SERIALIZATION_FILE_EXTENSION));
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
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    if (opts.outputMode == GlobalOptions::OutputMode::CHIR) {
        auto fileName =
            FileUtil::JoinPath(opts.output, chirPkg->GetName()) + std::string(CHIR_SERIALIZATION_FILE_EXTENSION);
        CHIRSerializer::Serialize(*chirPkg, fileName, CHIR::ToCHIR::RAW);
        return true;
    }
#endif
    Canonicalization();
    if (!opts.enIncrementalCompilation && !RunIRChecker(Phase::RAW)) {
        return false;
    }
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    /// ===============   Meta Transformation for CHIR  ===============
    if (!PerformPlugin()) {
        return false;
    }
#endif
    RunUnreachableMarkBlockRemoval();
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

bool ToCHIR::RulesChecking()
{
    Utils::ProfileRecorder recorder("CHIR", "RulesChecking");
    UnreachableBlockReporter();
    RunUnreachableMarkBlockRemoval();
    RunMergingBlocks("RulesChecking", "MergingBlock");
    NothingTypeExprElimination();
    RunConstantAnalysis();
    if (!RunVarInitChecking()) {
        return false;
    }
    UnreachableBranchReporter();
    // this instantance of block elimination is to maintain dead code warnings
    UnreachableBlockElimination();
    RunMergingBlocks("RulesChecking", "MergingBlockAfterUnreachableBlock");
    ReportUnusedCode();
    UselessFuncElimination("RulesChecking");
    UselessExprElimination();
    // check some rules in constant propagation:
    // 1. Array and VArray out of bounds
    // 2. arithmetic operation overflow
    RunConstantPropagation();
    ci.chirData->FreeConstAnalysisWrapper();
    return diag.GetErrorCount() == 0;
}

bool ToCHIR::Run()
{
    CJC_NULLPTR_CHECK(typeManager);
    CJC_NULLPTR_CHECK(gim);
    CJC_NULLPTR_CHECK(&needToOptGenericDecl);
    CJC_NULLPTR_CHECK(&needToOptString);
    CJC_NULLPTR_CHECK(&releaseCHIRMemory);
    CJC_NULLPTR_CHECK(&cangjieHome);

    RecordCodeInfoAtTheBegin();

    // 1. AST to CHIR
    if (!TranslateToCHIR({})) {
        return false;
    }
    // 2. for cjmp, while compiling common package, just be here and save CHIR to file.
    if (opts.outputMode == GlobalOptions::OutputMode::CHIR) {
        auto fileName =
            FileUtil::JoinPath(opts.output, chirPkg->GetName()) + std::string(CHIR_SERIALIZATION_FILE_EXTENSION);
        CHIRSerializer::Serialize(*chirPkg, fileName, CHIR::ToCHIR::RAW);
        return true;
    }
    // 3. run plugin for CHIR
    if (!PerformPlugin()) {
        return false;
    }
    // 4. Canonicalization, after this pass, the CHIR is ready for analysis and optimization.
    Canonicalization();
    if (!opts.enIncrementalCompilation && !RunIRChecker(Phase::RAW)) {
        return false;
    }
    if (opts.emitCHIRPhase == GlobalOptions::CandidateEmitCHIRPhase::RAW) {
        EmitCHIR(outputPath, *chirPkg, Phase::RAW, opts.NeedDumpCHIR());
        return true;
    }
    RecordCHIRExprNum("trans");

    if (ci.isCJLint) {
        return RunAnalysisForCJLint() && RunIRChecker(Phase::ANALYSIS_FOR_CJLINT);
    }
    // 5. analysis and check rules
    if (!RulesChecking()) {
        return false;
    }
    // 6. run optimization pass
    RunOptimizationPass();
    RecordCHIRExprNum("opt");
    // 7. do closure conversion
    DoClosureConversion();
    RecordCHIRExprNum("cc");
    // 8. run constant evaluation
    if (!RunConstantEvaluation()) {
        return false;
    }
    // 9. replace source code imported functions and variables with imported symbols,
    //    this pass must be after `RunConstantEvaluation`, because we need to calculate const var from imported package
    ReplaceSrcCodeImportedVal(*chirPkg).Run(
        srcCodeImportedFuncs, srcCodeImportedVars, uselessClasses, uselessLambda);

    // 10. annotation check depends on const eval
    if (!AnnotationChecker(*chirPkg, diag).Run()) {
        return false;
    }

    RunSanitizerCoverage();
    if (opts.enIncrementalCompilation) {
        UpdateEffectMapToString(effectMap, strEffectMap);
    }

    EraseDebugExpr();
    CFFIFuncWrapper();
    RecordCHIRExprNum("CHIR stage");
    RecordCodeInfoAtTheEnd();
    if (!RunIRChecker(Phase::OPT)) {
        return false;
    }
    if (opts.emitCHIRPhase == GlobalOptions::CandidateEmitCHIRPhase::OPT) {
        EmitCHIR(outputPath, *chirPkg, Phase::OPT, opts.NeedDumpCHIR());
    } else if (opts.saveTemps) {
        auto tempFileInfo =
            TempFileManager::Instance().CreateNewFileInfo({.fileName = chirPkg->GetName()}, TempFileKind::O_CHIR);
        CHIRSerializer::Serialize(*chirPkg, tempFileInfo.filePath, Phase::OPT);
    }
    return true;
}

bool ToCHIR::ExecuteCppPlugins()
{
    CHIRPluginManager chirPluginManager = ci.metaTransformPluginBuilder.BuildCHIRPluginManager(builder);
    chirPluginManager.ForEachMetaTransformConcept([this](MetaTransformConcept& mtc) {
        if (!mtc.IsForCHIR()) {
            return;
        }
        if (mtc.IsForFunc()) {
            for (auto func : chirPkg->GetGlobalFuncsWithBody()) {
                static_cast<MetaTransform<CHIR::Function>*>(&mtc)->Run(*func);
            }
        } else if (mtc.IsForPackage()) {
            static_cast<MetaTransform<CHIR::Package>*>(&mtc)->Run(*chirPkg);
        } else {
            CJC_ASSERT(false && "Should not reach here.");
        }
    });
    return true;
}

bool ToCHIR::ExecuteCjPlugins()
{
    bool succeed = true;
    ExecutePlugin executePlugin(builder);
    // 1. serialize package, get memory pointer and size
    if (!executePlugin.SerializePackage(*chirPkg)) {
        succeed = false;
    }
    // 2. execute all plugins, in fact, all plugins have been registered before, here we just need to
    // get `execute` function from any plugin, that's why we only pass the first plugin path not all plugins paths
    if (succeed && !executePlugin.Execute(opts.pluginPaths[0])) {
        succeed = false;
    }
    // 3. deserialize plugin result, get new package of cpp
    if (succeed) {
        if (auto newPackage = executePlugin.DeserializePluginResult(
            srcCodeImportedFuncs, srcCodeImportedVars, initFuncsForConstVar, maybeUnreachable)) {
            chirPkg = newPackage;
        } else {
            succeed = false;
        }
    }
    // 4. free cached data, serialize and deserialize data
    if (succeed && !executePlugin.FreeCachedData()) {
        succeed = false;
    }
    return succeed;
}

bool ToCHIR::PerformPlugin()
{
    if (opts.pluginPaths.empty()) {
        return true;
    }
    Utils::ProfileRecorder recorder("CHIR", "PerformPlugin");
    bool succeed = true;
#ifndef CANGJIE_ENABLE_GCOV
    try {
#endif
        if (ci.metaTransformPluginBuilder.IsCppPlugin()) {
            succeed = ExecuteCppPlugins();
        } else {
            succeed = ExecuteCjPlugins();
        }
#ifndef CANGJIE_ENABLE_GCOV
    } catch (...) {
        succeed = false;
    }
#endif
    if (!succeed) {
        diag.DiagnoseRefactor(DiagKindRefactor::plugin_throws_exception, DEFAULT_POSITION);
    } else {
        DumpCHIRToFile("PLUGIN");
    }
    return succeed;
}

void ToCHIR::Canonicalization()
{
    Utils::ProfileRecorder record("CHIR", "Canonicalization");
    {
        Utils::ProfileRecorder tempRecorder("Canonicalization", "GenerateVTable");
        // 1. create vtable
        auto allDefs = chirPkg->GetAllCustomTypeDef();
        auto generator = GenerateVTable(*chirPkg, allDefs, builder, opts, "GenerateVTable");
        generator.CreateVTable();
        generator.UpdateOperatorVirFunc();
        generator.CreateVirtualFuncWrapper(kind, cachedInfo, curVirtFuncWrapDep, delVirtFuncWrapForIncr);
        generator.SetSrcFuncType();
        generator.CreateMutFuncWrapper();

        // 2. calculate virtual method offset and store it in Invoke/InvokeStatic
        // 3. update callee of Apply, it may be replaced by mut wrapper func
        generator.UpdateFuncCall();
        DumpCHIRToFile("CreateVTable");
    }

    // 4. add has invited flag to class which has finalizer, in case of finalize before init
    MarkClassHasInited(builder).RunOnPackage(*chirPkg);

    // 5. update member var path, from name to offset
    UpdateMemberVarPath(*chirPkg, builder).Run();

    // 6. set mark on some functions that have no side effect
    NoSideEffectMarker(*chirPkg).Run();

    // 7. create box type for recursion value type
    BoxRecursionValueType(*chirPkg, builder).Run();

    // 8. set memory region
    SetMemoryRegion();

    DumpCHIRToFile("Canonicalization");
}

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
    CJC_NULLPTR_CHECK(pkg);
    auto res = ast2CHIR.ToCHIRPackage(*pkg);
    if (diag.GetErrorCount() != 0 || !res) {
        return false;
    }
    chirPkg = ast2CHIR.GetPackage();
    DumpCHIRToFile("AST_CHIR");
    FlatForInExpr();

    srcCodeImportedFuncs = ast2CHIR.GetSrcCodeImportedFuncs();
    srcCodeImportedVars = ast2CHIR.GetSrcCodeImportedVars();
    initFuncsForConstVar = ast2CHIR.GetInitFuncsForConstVar();
    maybeUnreachable = ast2CHIR.GetMaybeUnreachableBlocks();
    if (isComputingAnnos) {
        annoFactoryFuncs = ast2CHIR.GetAnnoFactoryFuncs();
        globalNominalCache = std::move(chirTypeCache.globalNominalCache);
    }
    ClearASTResources();
    return true;
}

VarInitDepMap ToCHIR::GetVarInitDepMap() const
{
    VarInitDepMap dep;
    for (auto var : chirPkg->GetGlobalVarsWithInit()) {
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

void ToCHIR::ClearASTResources()
{
    // in cjmp, `save cjo` is after CHIR stage, there is a bug if move `save cjo` before CHIR
    if (ci.invocation.globalOptions.outputMode == GlobalOptions::OutputMode::CHIR) {
        return;
    }
    // cjlint can compile many packages at same time, we can't release all AST resource after one package is done
    // cjlint need to modify its strategy because cjc can't support to compile many packages at same time
    if (ci.isCJLint) {
        return;
    }
    Utils::ProfileRecorder recorder("AST to CHIR Translation", "ClearASTResources");
    pkg = nullptr;
    annoFactoryFuncs.clear();
    ci.DestroyASTResources();
}

std::string PhaseToString(const ToCHIR::Phase phase)
{
    switch (phase) {
        case ToCHIR::Phase::RAW:
            return "raw";
        case ToCHIR::Phase::OPT:
            return "opt";
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
        case ToCHIR::Phase::PLUGIN:
            return "plugin";
        case ToCHIR::Phase::ANALYSIS_FOR_CJLINT:
            return  "analysis for cjlint";
#endif
        default:
            CJC_ABORT();
    }
    CJC_ABORT();
    return "";
}

} // namespace Cangjie::CHIR
