// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the IncrementalCompilerInstance.
 */

#include "cangjie/FrontendTool/IncrementalCompilerInstance.h"
#include <sstream>

#include "cangjie/AST/Utils.h"
#include "cangjie/Basic/StringConvertor.h"
#include "cangjie/Basic/Version.h"
#include "cangjie/Driver/TempFileManager.h"
#include "cangjie/Modules/ImportManager.h"
#include "cangjie/Modules/PackageManager.h"
#include "cangjie/Parse/ASTHasher.h"
#include "cangjie/Sema/IncrementalUtils.h"
#include "cangjie/Utils/ConstantsUtils.h"
#include "cangjie/Utils/FileUtil.h"
#include "cangjie/Utils/ProfileRecorder.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/SourceMgr.h"
#include "cangjie/IncrementalCompilation/IncrementalCompilationLogger.h"


#if (defined RELEASE)
#include "cangjie/Utils/Signal.h"
#endif

using namespace Cangjie;
using namespace AST;

IncrementalCompilerInstance::~IncrementalCompilerInstance()
{
}

bool IncrementalCompilerInstance::InitCompilerInstance()
{
    return CompilerInstance::InitCompilerInstance();
}

void IncrementalCompilerInstance::SaveCompilationResult(Cangjie::IncreResult&& result)
{
    kind = result.kind;
    rawMangleName2DeclMap = result.mangle2decl;
    switch (kind) {
        case IncreKind::ROLLBACK:
            declsToBeReCompiled.clear();
            [[fallthrough]];
        case IncreKind::INCR:
            cachedInfo = std::move(result.cacheInfo);
            CacheCompileArgs();
            declsToBeReCompiled = std::move(result.declsToRecompile);
            rawIncrRemovedDecls = std::move(result.deleted);
            // Copy a non-const version for 'Sema' usage.
            for (auto& [mangle, decl] : result.mangle2decl) {
                if (!decl->TestAttr(Attribute::IMPORTED)) {
                    mangledName2DeclMap.emplace(mangle, Ptr(const_cast<Decl*>(decl.get())));
                }
            }
            for (auto changed : declsToBeReCompiled) {
                if (changed->TestAttr(Attribute::IMPORTED) && (changed->IsFunc() || IsGlobalOrStaticVar(*changed))) {
                    cacheMangles.EmplaceImportedInlineDeclPtr(*changed);
                } else {
                    Sema::CollectRemovedManglesForReCompile(*changed, cachedInfo.semaInfo,
                        cacheMangles.incrRemovedDecls);
                }
            }
            for (auto removed : rawIncrRemovedDecls) {
                Sema::CollectRemovedMangles(removed, cachedInfo.semaInfo, cacheMangles.incrRemovedDecls);
            }
            // NOTE: since 'declsToBeReCompiled' and 'rawIncrRemovedDecls' have been process,
            // this loop will only remove the re-boxed built-in types' related decls.
            for (auto typeMangle : result.reBoxedTypes) {
                Sema::CollectRemovedMangles(typeMangle, cachedInfo.semaInfo, cacheMangles.incrRemovedDecls);
            }
            break;
        case IncreKind::NO_CHANGE:
            cachedInfo.bitcodeFilesName = std::move(result.cacheInfo.bitcodeFilesName);
            break;
        case IncreKind::EMPTY_PKG:
            break;
        case IncreKind::INVALID:
            CJC_ABORT();
            break;
    }
}

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
namespace {
std::optional<size_t> GetCachedSplitNum(const std::string& cachedCodeGenPath)
{
    std::optional<size_t> splitNum{std::nullopt};
    llvm::SMDiagnostic err;
    auto context = llvm::LLVMContext();
#ifdef _WIN32
    auto tempPath = StringConvertor::NormalizeStringToUTF8(cachedCodeGenPath);
    CJC_ASSERT(tempPath.has_value() && "Incorrect file name encoding.");
    auto module = llvm::parseIRFile(tempPath.value(), err, context);
#else
    auto module = llvm::parseIRFile(cachedCodeGenPath, err, context);
#endif
    if (module == nullptr) {
        Errorln("Illegal cached codegen info detected during incremental compilation.");
        return splitNum;
    }
    auto splitNumMD = module->getNamedMetadata("splitNum");
    for (llvm::MDNode* mdNode : splitNumMD->operands()) {
        auto numMD = llvm::cast<llvm::ConstantAsMetadata>(mdNode->getOperand(0));
        splitNum = llvm::cast<llvm::ConstantInt>(numMD->getValue())->getZExtValue();
    }
    return splitNum;
}

bool CheckCachedBitcode(const std::string& packageName, size_t splitNum, const GlobalOptions& globalOp)
{
    if (splitNum == 0) {
        return false;
    }
    for (size_t i = 0; i < splitNum; ++i) {
        std::string cachedBcPath =
            globalOp.GenerateCachedPathNameForCodeGen(std::to_string(i) + '-' + packageName, "_cache.bc");
        if (!FileUtil::FileExist(cachedBcPath)) {
            IncrementalCompilationLogger::GetInstance().LogLn(
                "the cached bitcode file is lost." + cachedBcPath + " index " + std::to_string(i));
            return false;
        }
    }
    return true;
}

bool CheckCachedBitcodeInfo(const std::string& packageName, const GlobalOptions& options)
{
    auto cgCachePath = options.GenerateCachedPathNameForCodeGen(packageName, ".cgCache");
    if (!FileUtil::FileExist(cgCachePath)) {
        IncrementalCompilationLogger::GetInstance().LogLn(
            "load cached codegen info failed, roll back to full compilation");
        return false;
    }
    auto splitNum = GetCachedSplitNum(cgCachePath);
    if (!splitNum.has_value()) {
        IncrementalCompilationLogger::GetInstance().LogLn("get cached split num failed, roll back to full compilation");
        return false;
    }
    bool bcCachedGood = CheckCachedBitcode(packageName, splitNum.value(), options);
    if (!bcCachedGood) {
        IncrementalCompilationLogger::GetInstance().LogLn(
            "load cached bitcode file failed, roll back to full compilation");
        return false;
    }
    return true;
}

bool CheckCachedObjFile(const GlobalOptions& options, CompilationCache& cachedInfo)
{
    auto& logger = IncrementalCompilationLogger::GetInstance();
    for (const auto& fileName : cachedInfo.bitcodeFilesName) {
        auto objFile = options.GetHashedObjFileName(fileName) + ".o";
        if (!FileUtil::FileExist(objFile)) {
            logger.LogLn("cached obj file not exist, roll back to full compilation");
            return false;
        }
    }
    return true;
}

bool CheckCachedBitcodeInfoForLTO(const GlobalOptions& options, CompilationCache& cachedInfo)
{
    auto& logger = IncrementalCompilationLogger::GetInstance();
    for (const auto& fileName : cachedInfo.bitcodeFilesName) {
        auto objFile = options.GetHashedObjFileName(FileUtil::GetFileNameWithoutExtension(fileName)) + ".bc";
        if (!FileUtil::FileExist(objFile)) {
            logger.LogLn("load cached bitcode file for opt failed, roll back to full compilation");
            return false;
        }
    }
    return true;
}

} // namespace
#endif

bool IncrementalCompilerInstance::PerformIncrementalScopeAnalysis()
{
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    Utils::ProfileRecorder recorder("Main Stage", "AST Diff");
    CJC_ASSERT(srcPkgs.size() == 1);
    auto& package = srcPkgs[0];

    // calculate the AST cache info
    bool res = CalculateASTCache();
    if (!res) {
        return true;
    }

    auto& options = invocation.globalOptions;
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    if (!options.pluginPaths.empty()) {
        kind = IncreKind::ROLLBACK;
        Warningf("`--incremental-compile` is incompatible with `--plugin` and will be ignored.\n");
        return true;
    }
#endif

    if (!declsWithDuplicatedRawMangleName.empty()) {
        kind = IncreKind::ROLLBACK;
        IncrementalCompilationLogger::GetInstance().LogLn(
            "illegal source code leads to duplicated raw mangle name, roll back to full compilation");
        return true;
    }

    auto increRes = IncrementalScopeAnalysis({rawMangleName2DeclMap, std::move(astCacheInfo), *package, options,
                                                 importManager, cachedInfo, fileMap, std::move(directExtends)});
    if (increRes.kind == IncreKind::INCR) {
        // check cached bitcode infos
        const std::string& packageName = package->fullPackageName;
        bool isCachedBitcodeGood = CheckCachedBitcodeInfo(packageName, options);
        if (!isCachedBitcodeGood) {
            increRes.kind = IncreKind::ROLLBACK;
        } else {
            // set deleted mangles Name for codegen
            cacheMangles.incrRemovedDecls.insert(
                increRes.deletedMangleNames.begin(), increRes.deletedMangleNames.end());
        }
    }
    if (increRes.kind == IncreKind::NO_CHANGE) {
        // check cached obj file
        for (auto& srcPkg : GetSourcePackages()) {
            options.UpdateCachedDirName(srcPkg->fullPackageName);
        }
        if (!options.IsLTOEnabled() && !CheckCachedObjFile(options, increRes.cacheInfo)) {
            increRes.kind = IncreKind::ROLLBACK;
        }
        if (options.IsLTOEnabled() && !CheckCachedBitcodeInfoForLTO(options, increRes.cacheInfo)) {
            increRes.kind = IncreKind::ROLLBACK;
        }
    }

    increRes.Dump();
    SaveCompilationResult(std::move(increRes));
#endif
    return true;
}

static bool HasSimpleInitialiser(const AST::VarDecl& decl)
{
    auto& init = decl.initializer;
    if (!init) {
        return false;
    }
    if (init->astKind == ASTKind::LIT_CONST_EXPR) {
        auto kind = StaticCast<LitConstExpr>(init.get())->kind;
        return kind != LitConstKind::JSTRING && kind != LitConstKind::UNIT;
    }
    return false;
}

bool IncrementalCompilerInstance::PerformSema()
{
    Utils::ProfileRecorder recorder("Main Stage", "Semantic");
    if (auto& logger = IncrementalCompilationLogger::GetInstance(); logger.IsEnable()) {
        std::string message = kind == IncreKind::NO_CHANGE
            ? "no change, skip sema"
            : (kind == IncreKind::ROLLBACK ? "full sema" : "incremental sema");
        logger.LogLn(message);
    }
    if (kind == IncreKind::NO_CHANGE || kind == IncreKind::EMPTY_PKG) {
        return true;
    } else if (kind == IncreKind::ROLLBACK) {
        return CompilerInstance::PerformSema();
    }
    CJC_NULLPTR_CHECK(typeManager);
    // Load cached types. Should only exist exactly one source package.
    for (auto srcPkg : GetSourcePackages()) {
        srcPkg->EnableAttr(Attribute::INCRE_COMPILE);
        Sema::HandleCtorForIncr(*srcPkg, mangledName2DeclMap, cachedInfo.semaInfo);
        cacheMangles.incrRemovedDecls.merge(importManager.LoadCachedTypeForPackage(*srcPkg, mangledName2DeclMap));
    }
    Sema::MarkIncrementalCheckForCtor(declsToBeReCompiled);

    auto visit = [](Decl& decl) {
        // NOTE: Avoid some sema rules to be rechecked.
        // To guarantee the member decls will be checked, the type decl cannot be marked with following attributes.
        if (!decl.IsNominalDecl() && decl.astKind != ASTKind::PROP_DECL) {
            decl.EnableAttr(Attribute::IS_CHECK_VISITED, Attribute::INITIALIZED, Attribute::INITIALIZATION_CHECKED,
                Attribute::INCRE_COMPILE);
        }
        // Remove body to avoid walker walk-in.
        if (auto fd = DynamicCast<FuncDecl*>(&decl); fd && fd->funcBody) {
            fd->funcBody->body.reset();
            return;
        }
        if (auto vd = DynamicCast<VarDecl*>(&decl)) {
            if (!vd->initializer) {
                return;
            }
            vd->EnableAttr(Attribute::DEFAULT);
            if (HasSimpleInitialiser(*vd)) {
                vd->DisableAttr(Attribute::IS_CHECK_VISITED);
            }
        }
        // PropDecl's getter/setter will be counted separately and should not be cleared from propDecl.
    };
    for (auto& [_, decl] : mangledName2DeclMap) {
        if (!decl->toBeCompiled && !IsInDeclWithAttribute(*decl, Attribute::GENERIC) && Ty::IsTyCorrect(decl->ty) &&
            (decl->astKind != ASTKind::FUNC_DECL || !decl->TestAttr(Attribute::DEFAULT))) {
        // NOTE: for now, generic definitions and default implementations should be kept for code re-generation.
            visit(*decl);
        }
    }
    return CompilerInstance::PerformSema();
}

bool IncrementalCompilerInstance::PerformOverflowStrategy()
{
    Utils::ProfileRecorder recorder("Main Stage", "Overflow Strategy");
    if (kind == IncreKind::NO_CHANGE || kind == IncreKind::EMPTY_PKG) {
        return true;
    }
    return CompilerInstance::PerformOverflowStrategy();
}

bool IncrementalCompilerInstance::PerformDesugarAfterSema()
{
    Utils::ProfileRecorder recorder("Main Stage", "Desugar after Sema");
    if (kind == IncreKind::NO_CHANGE || kind == IncreKind::EMPTY_PKG) {
        return true;
    }
    return CompilerInstance::PerformDesugarAfterSema();
}

bool IncrementalCompilerInstance::PerformGenericInstantiation()
{
    Utils::ProfileRecorder recorder("Main Stage", "Generic Instantiation");
    if (kind == IncreKind::NO_CHANGE || kind == IncreKind::EMPTY_PKG) {
        return true;
    }
    bool ret = CompilerInstance::PerformGenericInstantiation();
    if (ret) {
        for (auto& srcPkg : GetSourcePackages()) {
            changedTypes.merge(Sema::CollectChangedStructTypes(*srcPkg, declsToBeReCompiled));
            IterateAllExportableDecls(*srcPkg, [this](auto& decl) {
                // If non-export function/variable is marked as external linkage, codegen should change its linkage.
                if (!decl.IsExportedDecl() && decl.linkage == Linkage::EXTERNAL && !decl.mangledName.empty()) {
                    cacheMangles.newExternalDecls.emplace(decl.mangledName);
                }
            });
        }
    }
    return ret;
}

bool IncrementalCompilerInstance::PerformMangling()
{
    if (kind == IncreKind::NO_CHANGE || kind == IncreKind::EMPTY_PKG) {
        return true;
    }
    bool success = DefaultCompilerInstance::PerformMangling();
    if (success) {
        // mangle of full exported decl is empty in cjo, we need update it after mangling.
        cacheMangles.UpdateImportedInlineDeclsMangle();
    }
    return success;
}

void IncrementalCompilerInstance::UpdateCachedInfo()
{
    UpdateMangleNameForCachedInfo();
    UpdateCHIROptEffectMap();
    if (kind == IncreKind::ROLLBACK) {
        cachedInfo.virtualFuncDep = std::move(chirInfo.curVirtFuncWrapDep);
        cachedInfo.ccOutFuncs = std::move(chirInfo.ccOutFuncsRawMangle);
        cachedInfo.varInitDepMap = std::move(chirInfo.varInitDepMap);
    } else if (kind == IncreKind::INCR) {
        for (auto& [raw, _] : chirInfo.delVirtFuncWrapForIncr) {
            cachedInfo.virtualFuncDep.erase(raw);
        }
        for (auto& [raw, warp] : chirInfo.curVirtFuncWrapDep) {
            cachedInfo.virtualFuncDep[raw] = warp;
        }
        for (auto& rm : std::as_const(rawIncrRemovedDecls)) {
            cachedInfo.varInitDepMap.erase(rm);
        }
        for (auto& [raw, warp] : chirInfo.varInitDepMap) {
            cachedInfo.varInitDepMap[raw] = warp;
        }
        cachedInfo.ccOutFuncs.merge(std::move(chirInfo.ccOutFuncsRawMangle));
    }
    // Update usage of compiler added.
    for (auto pkg : GetSourcePackages()) {
        Sema::CollectCompilerAddedDeclUsage(*pkg, cachedInfo.semaInfo);
    }
}

namespace Cangjie {
/// implemented in IncrementalScopeAnalysis
void MergeCHIROptEffectMap(
    const OptEffectStrMap& newMap, OptEffectStrMap& lastCachedMap, const RawMangled2DeclMap& rawMangled2Decl);
void DeleteRemovedNodesInCHIROptEffectMap(const std::list<std::string>& removedDecls, OptEffectStrMap& effectMap);
void DeleteRecompiledNodesInCHIROptEffectMap(
    const std::unordered_set<Ptr<AST::Decl>>& recompiledNodes, OptEffectStrMap& effectMap);
}

void IncrementalCompilerInstance::UpdateCHIROptEffectMap()
{
    DeleteRemovedNodesInCHIROptEffectMap(rawIncrRemovedDecls, cachedInfo.chirOptInfo);
    DeleteRecompiledNodesInCHIROptEffectMap(declsToBeReCompiled, cachedInfo.chirOptInfo);

    MergeCHIROptEffectMap(chirInfo.optEffectMap, cachedInfo.chirOptInfo, rawMangleName2DeclMap);
}

namespace {
void UpdateIncrRemovedDeclsForVirtualFuncDep(CachedMangleMap& cacheMangles, std::list<std::string>& rawIncrRemovedDecls,
    VirtualWrapperDepMap& virtualFuncDep, VirtualWrapperDepMap& delVirtFuncWrapForIncr)
{
    auto& logger = IncrementalCompilationLogger::GetInstance();
    for (auto& delRaw : rawIncrRemovedDecls) {
        if (auto it = virtualFuncDep.find(delRaw); it != virtualFuncDep.end()) {
            logger.LogLn("[delVirDep][remove] raw: " + delRaw + " mg: " + it->second);
            cacheMangles.incrRemovedDecls.emplace(it->second);
        }
    }
    for (auto& [delRaw, del] : delVirtFuncWrapForIncr) {
            logger.LogLn("[delVirDep][change] raw: " + delRaw + " mg: " + del);
        cacheMangles.incrRemovedDecls.emplace(del);
    }
}
void UpdateIncrRemovedDeclsForVarInitDep(CachedMangleMap& cacheMangles, std::list<std::string>& rawIncrRemovedDecls,
    VarInitDepMap& varInitDepMap)
{
    auto& logger = IncrementalCompilationLogger::GetInstance();
    for (auto& delRaw : std::as_const(rawIncrRemovedDecls)) {
        if (auto it = varInitDepMap.find(delRaw); it != varInitDepMap.end()) {
            logger.LogLn("[delVarInitDep][remove] raw: " + delRaw + " mg: " + it->second);
            cacheMangles.incrRemovedDecls.emplace(it->second);
        }
    }
}
} // namespace

bool IncrementalCompilerInstance::PerformCHIRCompilation()
{
    Utils::ProfileRecorder recorder("Main Stage", "CHIR");
    if (kind == IncreKind::NO_CHANGE || kind == IncreKind::EMPTY_PKG) {
        return true;
    }
    auto ret = CompilerInstance::PerformCHIRCompilation();
    for (auto ty : chirInfo.ccTys) {
        changedTypes.insert(ty);
    }
    if (kind == IncreKind::INCR) {
        UpdateIncrRemovedDeclsForVirtualFuncDep(cacheMangles, rawIncrRemovedDecls, cachedInfo.virtualFuncDep,
            chirInfo.delVirtFuncWrapForIncr);
        UpdateIncrRemovedDeclsForVarInitDep(cacheMangles, rawIncrRemovedDecls, cachedInfo.varInitDepMap);
    }
    return ret;
}

void IncrementalCompilerInstance::LoadCachedCodegenResult() const
{
    for (auto fileName : cachedInfo.bitcodeFilesName) {
        auto cachePath = invocation.globalOptions.GenerateCachedPathNameForCodeGen(fileName, ".bc");
        TempFileInfo bcFileInfo = TempFileInfo{fileName, cachePath, "", true};
        invocation.globalOptions.frontendOutputFiles.emplace_back(bcFileInfo);
    }
}

bool IncrementalCompilerInstance::PerformCodeGen()
{
    Utils::ProfileRecorder recorder("Main Stage", "CodeGen");
    if (kind == IncreKind::NO_CHANGE) {
        for (auto& srcPkg : GetSourcePackages()) {
            LoadCachedCodegenResult();
            invocation.globalOptions.UpdateCachedDirName(srcPkg->fullPackageName);
        }
        return true;
    }
    // Before CodeGen, the dependency relationship of a package contains only some packages.
    // So this function rearranges the dependencies of all packages.
    RearrangeImportedPackageDependence();
    bool ret = true;
    for (auto& srcPkg : GetSourcePackages()) {
        ret = ret && CodegenOnePackage(*srcPkg, kind == IncreKind::INCR);
    }
    return ret;
}

bool IncrementalCompilerInstance::PerformCjoAndBchirSaving()
{
    Utils::ProfileRecorder recorder("Main Stage", "Save cjo and bchir");
    if (kind == IncreKind::NO_CHANGE || kind == IncreKind::EMPTY_PKG) {
        return true;
    }
    bool ret = true;
    for (auto& srcPkg : GetSourcePackages()) {
        ret = ret && SaveCjoAndBchir(*srcPkg);
        if (ret && !srcPkg->IsEmpty()) {
            // Write astData for incremental compilation in cache path.
            std::string cachedCjo =
                invocation.globalOptions.GenerateCachedPathName(srcPkg->fullPackageName, SERIALIZED_FILE_EXTENSION);
            ret = ret && FileUtil::WriteBufferToASTFile(cachedCjo, importManager.ExportASTSignature(*srcPkg));
        }
    }
    return ret;
}