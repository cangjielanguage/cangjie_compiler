// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * Split CHIRPackage to SubCHIRPackage.
 */

#include "CJNative/CHIRSplitter.h"

#include <queue>

#include "llvm/IR/Module.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/SourceMgr.h"

#include "Base/CGTypes/CGEnumType.h"
#include "CGPkgContext.h"
#include "Utils/CGUtils.h"
#include "cangjie/Basic/StringConvertor.h"
#include "cangjie/CHIR/CHIRCasting.h"
#include "cangjie/CHIR/Package.h"
#include "cangjie/CHIR/Type/EnumDef.h"
#include "cangjie/CHIR/Type/StructDef.h"
#include "cangjie/CHIR/Type/Type.h"
#include "cangjie/CHIR/Value.h"
#include "cangjie/Utils/ProfileRecorder.h"

namespace Cangjie {
namespace CodeGen {
namespace {
std::size_t GetSubCHIRPackageIdx(std::size_t splitNum)
{
    static std::size_t counter = 0;
    return counter++ % splitNum;
}
} // namespace

bool ChirTypeDefCmp::operator()(const CHIR::CustomTypeDef* lhs, const CHIR::CustomTypeDef* rhs) const
{
    return lhs->GetIdentifierWithoutPrefix() < rhs->GetIdentifierWithoutPrefix();
}

bool ChirValueCmp::operator()(const CHIR::Value* lhs, const CHIR::Value* rhs) const
{
    return lhs->GetIdentifierWithoutPrefix() < rhs->GetIdentifierWithoutPrefix();
}

SubCHIRPackage::SubCHIRPackage(std::size_t splitNum)
    : mainModule(false), subCHIRPackageIdx(GetSubCHIRPackageIdx(splitNum)), exprNumInChirFuncs(0), splitNum(splitNum)
{
}

void SubCHIRPackage::Clear()
{
    chirCustomDefs.clear();
    chirGVs.clear();
    chirFuncs.clear();
    chirForeigns.clear();
    chirImportedCFuncs.clear();
}

CHIRSplitter::CHIRSplitter(const CGPkgContext& cgPkgCtx)
    : cgPkgCtx(cgPkgCtx), splitNum(0), index(0), subCHIRPackagesCache()
{
}

void CHIRSplitter::CalcSplitsNum()
{
    if (subCHIRPackagesCache.splitNum.has_value()) {
        splitNum = subCHIRPackagesCache.splitNum.value();
        return;
    }

    auto& options = cgPkgCtx.GetGlobalOptions();
    auto& chirPkg = cgPkgCtx.GetCHIRPackage();
    splitNum = options.aggressiveParallelCompile.value_or(1);
    if (splitNum > chirPkg.GetGlobalFuncs().size()) {
        splitNum = chirPkg.GetGlobalFuncs().size();
    }
}

// Split chirPkg into splitNum SubCHIRPackages.
// chirPkg: complete data of the packet transferred from CHIR
// splitNum: parallelism/splitting degree
std::vector<SubCHIRPackage> CHIRSplitter::SplitCHIRPackage()
{
    Utils::ProfileRecorder recorder("EmitIR", "SplitCHIRPackage");

    LoadSubCHIRPackagesInfo();
    CalcSplitsNum(); // Compute the degree of parallelism
    std::vector<SubCHIRPackage> subCHIRPackages;
    subCHIRPackages.reserve(splitNum);
    for (std::size_t idx = 0; idx < splitNum; ++idx) {
        subCHIRPackages.emplace_back(SubCHIRPackage(splitNum));
    }
    SplitCHIRFuncs(subCHIRPackages);
    SplitCHIRClasses(subCHIRPackages);
    SplitCHIREnums(subCHIRPackages);
    SplitCHIRStructs(subCHIRPackages);
    SplitCHIRExtends(subCHIRPackages);
    SplitCHIRGlobalVars(subCHIRPackages);
    SplitCHIRImportedCFuncs(subCHIRPackages);
    SaveSubCHIRPackagesInfo();
    return subCHIRPackages;
}

namespace {
struct SubCHIRPackageCmp {
    bool operator()(const SubCHIRPackage& lhs, const SubCHIRPackage& rhs) const
    {
        return lhs.exprNumInChirFuncs == rhs.exprNumInChirFuncs ? lhs.subCHIRPackageIdx < rhs.subCHIRPackageIdx
                                                                : lhs.exprNumInChirFuncs < rhs.exprNumInChirFuncs;
    }
};

auto FindTargetSubCHIRPackage(const CHIR::Value& chirValue,
    const std::set<SubCHIRPackage, SubCHIRPackageCmp>& subCHIRPackagesSet,
    const std::map<std::string, std::size_t>& cache)
{
    auto target = subCHIRPackagesSet.begin();
    if (auto iterOfCache = cache.find(chirValue.GetIdentifierWithoutPrefix()); iterOfCache != cache.end()) {
        auto idxInCache = iterOfCache->second;
        target = std::find_if(subCHIRPackagesSet.begin(), subCHIRPackagesSet.end(),
            [idxInCache](const SubCHIRPackage& sub) { return sub.subCHIRPackageIdx == idxInCache; });
    }
    return target;
};

void SplitSpecialFuncs(CHIR::Func& globalInitFunc, CHIR::Func& globalInitLiteralFunc,
    const std::vector<CHIR::Func*>& toAnyFuncs, std::set<SubCHIRPackage, SubCHIRPackageCmp>& subCHIRPackagesSet,
    std::map<std::string, std::size_t>& cache)
{
    auto target = FindTargetSubCHIRPackage(globalInitFunc, subCHIRPackagesSet, cache);
    auto targetSubCHIRPackage = subCHIRPackagesSet.extract(target);
    auto& subCHIRPackage = targetSubCHIRPackage.value();
    subCHIRPackage.mainModule = true;
    subCHIRPackage.chirFuncs.emplace(&globalInitFunc);
    subCHIRPackage.chirFuncs.emplace(&globalInitLiteralFunc);
    subCHIRPackage.exprNumInChirFuncs += globalInitFunc.GetExpressionsNum() + globalInitLiteralFunc.GetExpressionsNum();
    cache.emplace(globalInitFunc.GetIdentifierWithoutPrefix(), subCHIRPackage.subCHIRPackageIdx);
    cache.emplace(globalInitLiteralFunc.GetIdentifierWithoutPrefix(), subCHIRPackage.subCHIRPackageIdx);
    for (auto toAny : toAnyFuncs) {
        subCHIRPackage.chirFuncs.emplace(toAny);
        subCHIRPackage.exprNumInChirFuncs += toAny->GetExpressionsNum();
        cache.emplace(toAny->GetIdentifierWithoutPrefix(), subCHIRPackage.subCHIRPackageIdx);
    }
    subCHIRPackagesSet.insert(std::move(targetSubCHIRPackage));
}

void SplitNormalFunc(const std::multimap<std::size_t, CHIR::Func*>& sortedChirFuncs,
    std::set<SubCHIRPackage, SubCHIRPackageCmp>& subCHIRPackagesSet, std::map<std::string, std::size_t>& cache)
{
    for (auto iter = sortedChirFuncs.rbegin(); iter != sortedChirFuncs.rend(); iter++) {
        auto func = iter->second;
        auto target = FindTargetSubCHIRPackage(*func, subCHIRPackagesSet, cache);
        auto targetSubCHIRPackage = subCHIRPackagesSet.extract(target);
        auto& subCHIRPackage = targetSubCHIRPackage.value();
        subCHIRPackage.chirFuncs.emplace(func);
        subCHIRPackage.exprNumInChirFuncs += iter->first;
        cache.emplace(func->GetIdentifierWithoutPrefix(), subCHIRPackage.subCHIRPackageIdx);
        subCHIRPackagesSet.insert(std::move(targetSubCHIRPackage));
    }
}

void SplitForeign(const CHIR::Package& chirPkg, std::set<SubCHIRPackage, SubCHIRPackageCmp>& subCHIRPackagesSet,
    std::map<std::string, std::size_t>& cache)
{
    for (auto importedValue : chirPkg.GetImportedVarAndFuncs()) {
        if (!importedValue->TestAttr(CHIR::Attribute::FOREIGN) ||
            importedValue->GetSourcePackageName() != chirPkg.GetName()) {
            continue;
        }
        auto foreign = DynamicCast<CHIR::ImportedFunc*>(importedValue);
        CJC_NULLPTR_CHECK(foreign);
        auto target = FindTargetSubCHIRPackage(*foreign, subCHIRPackagesSet, cache);
        auto targetSubCHIRPackage = subCHIRPackagesSet.extract(target);
        auto& subCHIRPackage = targetSubCHIRPackage.value();
        subCHIRPackage.chirForeigns.emplace(foreign);
        subCHIRPackage.exprNumInChirFuncs += 1;
        cache.emplace(foreign->GetIdentifierWithoutPrefix(), subCHIRPackage.subCHIRPackageIdx);
        subCHIRPackagesSet.insert(std::move(targetSubCHIRPackage));
    }
}
}; // namespace

// Split chirPkg.GetGlobalFuncs evenly into splitNum subCHIRPackages,
// so that the total number of expressions of all functions in each subCHIRPackage is close to.
void CHIRSplitter::SplitCHIRFuncs(std::vector<SubCHIRPackage>& subCHIRPackages)
{
    std::set<SubCHIRPackage, SubCHIRPackageCmp> subCHIRPackagesSet;
    for (auto subCHIRPackage : subCHIRPackages) {
        subCHIRPackagesSet.emplace(subCHIRPackage);
    }

    auto& chirPkg = cgPkgCtx.GetCHIRPackage();
    // 1. Sorts chirFuncs based on the number of expressions in a chirFunc.
    auto globalInitFunc = chirPkg.GetPackageInitFunc();
    std::string targetSuffix =
        SPECIAL_NAME_FOR_INIT_LITERAL_FUNCTION + MANGLE_FUNC_PARAM_TYPE_PREFIX + MANGLE_VOID_TY_SUFFIX;
    std::string globalInitFuncName = globalInitFunc->GetIdentifierWithoutPrefix();
    // init func must have suffix iiHv, index 4 is the start of ii. 2 is the length of il.
    auto globalInitLiteralFunc = VirtualCast<CHIR::Func*>(const_cast<CGPkgContext&>(cgPkgCtx).FindCHIRGlobalValue(
        globalInitFuncName.replace(globalInitFuncName.size() - 4, 2, "il")));
    std::vector<CHIR::Func*> toAnyFuncs{};
    std::multimap<std::size_t, CHIR::Func*> sortedChirFuncs{};
    for (auto chirFunc : chirPkg.GetGlobalFuncs()) {
        if (chirPkg.GetName() == REFLECT_PACKAGE_NAME && chirFunc->GetSrcCodeIdentifier() == "toAny") {
            toAnyFuncs.emplace_back(chirFunc);
        } else if (chirFunc != globalInitFunc && chirFunc != globalInitLiteralFunc) {
            sortedChirFuncs.emplace(chirFunc->GetExpressionsNum(), chirFunc);
        }
    }
    // 2. Add the chirFunc with the most expressions to the subCHIRPackage with the least exprNumInChirFuncs.
    SplitSpecialFuncs(
        *globalInitFunc, *globalInitLiteralFunc, toAnyFuncs, subCHIRPackagesSet, subCHIRPackagesCache.funcsCache);
    SplitNormalFunc(sortedChirFuncs, subCHIRPackagesSet, subCHIRPackagesCache.funcsCache);
    SplitForeign(chirPkg, subCHIRPackagesSet, subCHIRPackagesCache.foreignsCache);

    for (auto subCHIRPackage : subCHIRPackagesSet) {
        subCHIRPackages[subCHIRPackage.subCHIRPackageIdx] = subCHIRPackage;
    }
}

unsigned long CHIRSplitter::FindIdxInCache(const std::string& key)
{
    auto iter = subCHIRPackagesCache.classesCache.find(key);
    if (iter != subCHIRPackagesCache.classesCache.cend()) {
        return iter->second;
    }
    iter = subCHIRPackagesCache.structsCache.find(key);
    if (iter != subCHIRPackagesCache.structsCache.cend()) {
        return iter->second;
    }
    iter = subCHIRPackagesCache.enumsCache.find(key);
    if (iter != subCHIRPackagesCache.enumsCache.cend()) {
        return iter->second;
    }
    return index++ % splitNum;
}

void CHIRSplitter::SplitCHIRClasses(std::vector<SubCHIRPackage>& subCHIRPackages)
{
    for (auto chirClass : cgPkgCtx.GetCHIRPackage().GetClasses()) {
        size_t idx = 0;
        if (cgPkgCtx.GetGlobalOptions().enIncrementalCompilation) {
            auto key = chirClass->GetPackageName() + ":" + chirClass->GetSrcCodeIdentifier();
            idx = FindIdxInCache(key);
            subCHIRPackagesCache.classesCache.emplace(key, idx);
        } else {
            idx = index++ % splitNum;
        }
        subCHIRPackages[idx].chirCustomDefs.emplace(chirClass);
    }
}

void CHIRSplitter::SplitCHIREnums(std::vector<SubCHIRPackage>& subCHIRPackages)
{
    for (auto chirEnum : cgPkgCtx.GetCHIRPackage().GetEnums()) {
        size_t idx = 0;
        if (cgPkgCtx.GetGlobalOptions().enIncrementalCompilation) {
            auto key = chirEnum->GetPackageName() + ":" + chirEnum->GetSrcCodeIdentifier();
            idx = FindIdxInCache(key);
            subCHIRPackagesCache.enumsCache.emplace(key, idx);
        } else {
            idx = index++ % splitNum;
        }
        subCHIRPackages[idx].chirCustomDefs.emplace(chirEnum);
    }
    for (auto chirEnum : cgPkgCtx.GetCHIRPackage().GetImportedEnums()) {
        size_t idx = 0;
        if (cgPkgCtx.GetGlobalOptions().enIncrementalCompilation) {
            auto key = chirEnum->GetPackageName() + ":" + chirEnum->GetSrcCodeIdentifier();
            idx = FindIdxInCache(key);
            subCHIRPackagesCache.enumsCache.emplace(key, idx);
        } else {
            idx = index++ % splitNum;
        }
        subCHIRPackages[idx].chirCustomDefs.emplace(chirEnum);
    }
}

void CHIRSplitter::SplitCHIRStructs(std::vector<SubCHIRPackage>& subCHIRPackages)
{
    for (auto chirStruct : cgPkgCtx.GetCHIRPackage().GetStructs()) {
        size_t idx = 0;
        if (cgPkgCtx.GetGlobalOptions().enIncrementalCompilation) {
            auto key = chirStruct->GetPackageName() + ":" + chirStruct->GetSrcCodeIdentifier();
            idx = FindIdxInCache(key);
            subCHIRPackagesCache.structsCache.emplace(key, idx);
        } else {
            idx = index++ % splitNum;
        }
        subCHIRPackages[idx].chirCustomDefs.emplace(chirStruct);
    }
}

void CHIRSplitter::SplitCHIRExtends(std::vector<SubCHIRPackage>& subCHIRPackages)
{
    for (auto chirExtendDefs : cgPkgCtx.GetCHIRPackage().GetExtends()) {
        auto key = chirExtendDefs->GetIdentifierWithoutPrefix();
        auto iter = subCHIRPackagesCache.extendDefCache.find(key);
        auto idx = iter == subCHIRPackagesCache.extendDefCache.end() ? (index++ % splitNum) : iter->second;
        subCHIRPackages[idx].chirCustomDefs.emplace(chirExtendDefs);
        subCHIRPackagesCache.extendDefCache.emplace(key, idx);
    }
}

void CHIRSplitter::SplitCHIRGlobalVars(std::vector<SubCHIRPackage>& subCHIRPackages)
{
    for (auto chirGV : cgPkgCtx.GetCHIRPackage().GetGlobalVars()) {
        auto key = chirGV->GetIdentifierWithoutPrefix();
        auto iter = subCHIRPackagesCache.gvsCache.find(key);
        auto idx = iter == subCHIRPackagesCache.gvsCache.cend() ? (index++ % splitNum) : iter->second;
        subCHIRPackages[idx].chirGVs.emplace(chirGV);
        subCHIRPackagesCache.gvsCache.emplace(key, idx);
    }
}

void CHIRSplitter::SplitCHIRImportedCFuncs(std::vector<SubCHIRPackage>& subCHIRPackages)
{
    for (auto importedValue : cgPkgCtx.GetCHIRPackage().GetImportedVarAndFuncs()) {
        // We only process imported non-public CFunc here,
        // whether it's explicitly imported, implicitly imported, or a foreign function.
        if (importedValue->IsImportedVar() || !StaticCast<CHIR::FuncType*>(importedValue->GetType())->IsCFunc() ||
            (!importedValue->TestAttr(CHIR::Attribute::PRIVATE) &&
             !importedValue->TestAttr(CHIR::Attribute::FOREIGN))) {
            continue;
        }
        auto importedCFunc = DynamicCast<CHIR::ImportedFunc*>(importedValue);
        CJC_NULLPTR_CHECK(importedCFunc);
        auto iter = subCHIRPackagesCache.importedCFuncsCache.find(importedCFunc->GetIdentifierWithoutPrefix());
        auto idx = iter == subCHIRPackagesCache.importedCFuncsCache.cend() ? (index++ % splitNum) : iter->second;
        subCHIRPackages[idx].chirImportedCFuncs.emplace(importedCFunc);
        subCHIRPackagesCache.importedCFuncsCache.emplace(importedCFunc->GetIdentifierWithoutPrefix(), idx);
    }
}

void CHIRSplitter::LoadSubCHIRPackagesInfo()
{
    if (!cgPkgCtx.IsIncrementEnabled()) {
        return;
    }

    auto path = cgPkgCtx.GetGlobalOptions().GenerateCachedPathNameForCodeGen(cgPkgCtx.GetCurrentPkgName(), ".cgCache");
    if (!FileUtil::FileExist(path)) {
        return;
    }

#ifdef _WIN32
    auto tempPath = StringConvertor::NormalizeStringToUTF8(path);
    CJC_ASSERT(tempPath.has_value() && "Incorrect file name encoding.");
    path = tempPath.value();
#endif
    llvm::SMDiagnostic err;
    auto context = llvm::LLVMContext();
    auto module = llvm::parseIRFile(path, err, context);

    auto splitNumMD = module->getNamedMetadata("splitNum");
    for (llvm::MDNode* mdNode : splitNumMD->operands()) {
        auto numMD = llvm::cast<llvm::ConstantAsMetadata>(mdNode->getOperand(0));
        subCHIRPackagesCache.splitNum = llvm::cast<llvm::ConstantInt>(numMD->getValue())->getZExtValue();
    }

    auto parseEntries = [](const llvm::MDNode& mdNode) -> std::tuple<std::string, uint64_t> {
        auto nameMD = llvm::cast<llvm::MDString>(mdNode.getOperand(0));
        auto name = nameMD->getString().str();
        auto idxMD = llvm::cast<llvm::ConstantAsMetadata>(mdNode.getOperand(1));
        auto idx = llvm::cast<llvm::ConstantInt>(idxMD->getValue())->getZExtValue();
        return {name, idx};
    };
    auto classesMD = module->getNamedMetadata("classes");
    for (llvm::MDNode* mdNode : classesMD->operands()) {
        const auto [name, idx] = parseEntries(*mdNode);
        subCHIRPackagesCache.classesCache.emplace(name, idx);
    }
    auto enumsMD = module->getNamedMetadata("enums");
    for (llvm::MDNode* mdNode : enumsMD->operands()) {
        const auto [name, idx] = parseEntries(*mdNode);
        subCHIRPackagesCache.enumsCache.emplace(name, idx);
    }
    auto structsMD = module->getNamedMetadata("structs");
    for (llvm::MDNode* mdNode : structsMD->operands()) {
        const auto [name, idx] = parseEntries(*mdNode);
        subCHIRPackagesCache.structsCache.emplace(name, idx);
    }

    auto extendsMD = module->getNamedMetadata("extends");
    for (llvm::MDNode* mdNode : extendsMD->operands()) {
        const auto [name, idx] = parseEntries(*mdNode);
        subCHIRPackagesCache.extendDefCache.emplace(name, idx);
    }

    auto gvsMD = module->getNamedMetadata("gvs");
    for (llvm::MDNode* mdNode : gvsMD->operands()) {
        const auto [name, idx] = parseEntries(*mdNode);
        subCHIRPackagesCache.gvsCache.emplace(name, idx);
    }
    auto funcsMD = module->getNamedMetadata("funcs");
    for (llvm::MDNode* mdNode : funcsMD->operands()) {
        const auto [name, idx] = parseEntries(*mdNode);
        subCHIRPackagesCache.funcsCache.emplace(name, idx);
    }
    auto foreignsMD = module->getNamedMetadata("foreigns");
    for (llvm::MDNode* mdNode : foreignsMD->operands()) {
        const auto [name, idx] = parseEntries(*mdNode);
        subCHIRPackagesCache.foreignsCache.emplace(name, idx);
    }
}

// Using flatbuffers might be a better way
void CHIRSplitter::SaveSubCHIRPackagesInfo()
{
    if (!cgPkgCtx.GetGlobalOptions().enIncrementalCompilation) {
        return;
    }

    auto context = llvm::LLVMContext();
    auto module = llvm::Module(cgPkgCtx.GetCurrentPkgName(), context);
    auto i32Ty = llvm::Type::getInt32Ty(context);

    auto splitNumMD = module.getOrInsertNamedMetadata("splitNum");
    auto numMD = llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(i32Ty, splitNum));
    splitNumMD->addOperand(llvm::MDTuple::get(context, {numMD}));

    auto classesMD = module.getOrInsertNamedMetadata("classes");
    for (auto& klass : std::as_const(subCHIRPackagesCache.classesCache)) {
        auto nameMD = llvm::MDString::get(context, klass.first);
        auto idxMD = llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(i32Ty, klass.second));
        classesMD->addOperand(llvm::MDTuple::get(context, {nameMD, idxMD}));
    }
    auto enumsMD = module.getOrInsertNamedMetadata("enums");
    for (auto& en : std::as_const(subCHIRPackagesCache.enumsCache)) {
        auto nameMD = llvm::MDString::get(context, en.first);
        auto idxMD = llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(i32Ty, en.second));
        enumsMD->addOperand(llvm::MDTuple::get(context, {nameMD, idxMD}));
    }
    auto structsMD = module.getOrInsertNamedMetadata("structs");
    for (auto& st : std::as_const(subCHIRPackagesCache.structsCache)) {
        auto nameMD = llvm::MDString::get(context, st.first);
        auto idxMD = llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(i32Ty, st.second));
        structsMD->addOperand(llvm::MDTuple::get(context, {nameMD, idxMD}));
    }

    auto extendsMD = module.getOrInsertNamedMetadata("extends");
    for (auto extend : std::as_const(subCHIRPackagesCache.extendDefCache)) {
        auto nameMD = llvm::MDString::get(context, extend.first);
        auto idxMD = llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(i32Ty, extend.second));
        extendsMD->addOperand(llvm::MDTuple::get(context, {nameMD, idxMD}));
    }

    auto gvsMD = module.getOrInsertNamedMetadata("gvs");
    for (auto& gv : std::as_const(subCHIRPackagesCache.gvsCache)) {
        auto nameMD = llvm::MDString::get(context, gv.first);
        auto idxMD = llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(i32Ty, gv.second));
        gvsMD->addOperand(llvm::MDTuple::get(context, {nameMD, idxMD}));
    }
    auto funcsMD = module.getOrInsertNamedMetadata("funcs");
    for (auto& func : std::as_const(subCHIRPackagesCache.funcsCache)) {
        auto nameMD = llvm::MDString::get(context, func.first);
        auto idxMD = llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(i32Ty, func.second));
        funcsMD->addOperand(llvm::MDTuple::get(context, {nameMD, idxMD}));
    }
    auto foreignsMD = module.getOrInsertNamedMetadata("foreigns");
    for (auto& foreign : std::as_const(subCHIRPackagesCache.foreignsCache)) {
        auto nameMD = llvm::MDString::get(context, foreign.first);
        auto idxMD = llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(i32Ty, foreign.second));
        foreignsMD->addOperand(llvm::MDTuple::get(context, {nameMD, idxMD}));
    }

    auto path = cgPkgCtx.GetGlobalOptions().GenerateCachedPathNameForCodeGen(cgPkgCtx.GetCurrentPkgName(), ".cgCache");
#ifdef _WIN32
    auto tempPath = StringConvertor::NormalizeStringToUTF8(path);
    CJC_ASSERT(tempPath.has_value() && "Incorrect file name encoding.");
    path = tempPath.value();
#endif
    if (cgPkgCtx.GetGlobalOptions().NeedDumpIRToFile()) {
        DumpIR(module, path);
    } else {
        CodeGen::SaveToBitcodeFile(module, path);
    }
}
} // namespace CodeGen
} // namespace Cangjie
