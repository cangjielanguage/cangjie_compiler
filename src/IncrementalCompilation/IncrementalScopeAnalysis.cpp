// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/IncrementalCompilation/IncrementalScopeAnalysis.h"

#include "cangjie/AST/Node.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/IncrementalCompilation/ASTCacheCalculator.h"
#include "CompilationCacheSerialization.h"
#include "cangjie/IncrementalCompilation/IncrementalCompilationLogger.h"
#include "cangjie/IncrementalCompilation/Utils.h"
#include "cangjie/Parse/ASTHasher.h"

#include "ASTDiff.h"
#include "PollutionAnalyzer.h"

using namespace Cangjie;
using namespace Cangjie::AST;
using namespace Cangjie::IncrementalCompilation;

namespace {
struct DumpInfo {
    std::string identifier;
    std::string fileName;
    size_t lineNum;
    size_t columnNum;

    DumpInfo(const std::string& identifier, const std::string& fileName, size_t lineNum, size_t columnNum)
        : identifier(identifier), fileName(fileName), lineNum(lineNum), columnNum(columnNum)
    {
    }

    bool operator<(const DumpInfo& rhs) const
    {
        // compare by file, line, and column in dictionary order
        // Note that some compiler-generated decls do not have line info, and for these decls their identifier is
        // compared in dictionary order instead
        return std::tie(fileName, lineNum, columnNum, identifier) <
            std::tie(rhs.fileName, rhs.lineNum, rhs.columnNum, rhs.identifier);
    }
};

void DumpDeclsToRecompile(const std::unordered_set<Ptr<AST::Decl>>& declsToRecompile)
{
    auto& logger = IncrementalCompilationLogger::GetInstance();
    if (!logger.IsEnable()) {
        return;
    }
    if (!declsToRecompile.empty()) {
        logger.LogLn("changed decls to recompile: " + std::to_string(declsToRecompile.size()));
        std::vector<DumpInfo> infos;
        for (auto decl : declsToRecompile) {
            if (decl->identifier.Empty()) {
                if (decl->astKind == ASTKind::VAR_WITH_PATTERN_DECL) {
                    infos.emplace_back("VarWithPatternDecl", GetTrimmedPath(decl->curFile.get()), decl->begin.line,
                        decl->begin.column);
                } else if (decl->astKind == ASTKind::EXTEND_DECL) {
                    infos.emplace_back(
                        "ExtendDecl", GetTrimmedPath(decl->curFile.get()), decl->begin.line, decl->begin.column);
                } else {
                    CJC_ABORT();
                }
            } else {
                infos.emplace_back(decl->identifier.Val(), GetTrimmedPath(decl->curFile.get()),
                    decl->identifier.Begin().line, decl->identifier.Begin().column);
            }
        }
        std::sort(infos.begin(), infos.end());
        for (const auto& info : infos) {
            logger.LogLn("the changed decl after Spreading is:" + info.identifier + " in file " + info.fileName +
                " in line " + std::to_string(info.lineNum) + " in column " + std::to_string(info.columnNum));
        }
    }
}

void DumpDeleted(const std::list<RawMangledName>& deleted)
{
    auto& logger = IncrementalCompilationLogger::GetInstance();
    if (!logger.IsEnable()) {
        return;
    }
    if (!deleted.empty()) {
        logger.LogLn("deleted decls: " + std::to_string(deleted.size()));
        for (auto del : deleted) {
            logger.LogLn("the deleted decl is:" + del);
        }
    }
}

} // namespace

void IncreResult::Dump() const
{
    if (!IncrementalCompilationLogger::GetInstance().IsEnable()) {
        return;
    }
    auto& logger = IncrementalCompilationLogger::GetInstance();
    if (kind != IncreKind::INCR) {
        switch (kind) {
            case IncreKind::ROLLBACK:
                logger.LogLn("incremental compile result kind: rollback");
                return;
            case IncreKind::EMPTY_PKG:
                logger.LogLn("incremental compile result kind: empty package");
                return;
            case IncreKind::NO_CHANGE:
                logger.LogLn("incremental compile result kind: no change");
                return;
            default:
                CJC_ABORT();
                return;
        }
    }
    std::stringstream out{};
    std::string deliStr = std::string(DELIMITER_NUM, '=');
    logger.LogLn(deliStr);
    logger.LogLn("incremental compilation triggered");
    logger.LogLn("begin dump incremental compile result:");
    DumpDeclsToRecompile(declsToRecompile);
    logger.LogLn(deliStr);
    DumpDeleted(deleted);
    logger.LogLn(deliStr);
}

static const std::string GetName(const AST::Decl& decl)
{
    if (decl.astKind == AST::ASTKind::EXTEND_DECL) {
        return "extend";
    } else {
        return decl.identifier;
    }
}

static const std::string OutputDeclInfo(const RawMangled2DeclMap& rawMangled2Decl, const std::string& rawMangledName)
{
    auto srcIt = rawMangled2Decl.find(rawMangledName);
    if (srcIt == rawMangled2Decl.end()) {
        return rawMangledName;
    }
    CJC_NULLPTR_CHECK(srcIt->second);
    const AST::Decl* decl = srcIt->second;
    return GetName(*decl) + "(l:" + std::to_string(decl->begin.line) + ", c:" + std::to_string(decl->begin.column) +
        ")";
}

static void PrintCHIROptEffectMap(
    const OptEffectStrMap& effectMap, const RawMangled2DeclMap& rawMangled2Decl)
{
    auto& logger = IncrementalCompilationLogger::GetInstance();
    if (!logger.IsEnable()) {
        return;
    }
    logger.LogLn("[CHIR Effect Map] START");
    // convert `std::unordered_map` to `std::map`
    std::map<std::string, std::set<std::string>> orderedMap;
    for (auto effectIt : effectMap) {
        std::set<std::string> orderedDecls;
        for (auto nameIt : effectIt.second) {
            orderedDecls.emplace(nameIt);
        }
        orderedMap.emplace(effectIt.first, orderedDecls);
    }
    std::string msg;
    for (auto mapIt : std::as_const(orderedMap)) {
        msg = OutputDeclInfo(rawMangled2Decl, mapIt.first) + " -> ";
        for (auto name : mapIt.second) {
            msg += OutputDeclInfo(rawMangled2Decl, name) + ",";
        }
        logger.LogLn(msg);
    }
    logger.LogLn("[CHIR Effect Map] END");
}

namespace Cangjie {
///@{
/// used by IncrementalCompilerInstance
extern void MergeCHIROptEffectMap(
    const OptEffectStrMap& newMap, OptEffectStrMap& lastCachedMap, const RawMangled2DeclMap& rawMangled2Decl)
{
    for (auto mapIt : newMap) {
        CJC_ASSERT(!mapIt.first.empty());
        CJC_ASSERT(mapIt.second.find(mapIt.first) == mapIt.second.end());
        if (mapIt.second.empty()) {
            continue;
        }
        auto& effectInfo = lastCachedMap[mapIt.first];
        for (auto effectedIt : mapIt.second) {
            CJC_ASSERT(!effectedIt.empty());
            effectInfo.emplace(effectedIt);
        }
    }
    PrintCHIROptEffectMap(lastCachedMap, rawMangled2Decl);
}

extern void DeleteRemovedNodesInCHIROptEffectMap(
    const std::list<std::string>& removedDecls, OptEffectStrMap& effectMap)
{
    auto it = effectMap.begin();
    while (it != effectMap.end()) {
        auto rawIt = std::find(removedDecls.begin(), removedDecls.end(), it->first);
        if (rawIt != removedDecls.end()) {
            it = effectMap.erase(it);
            continue;
        }
        auto effectedIt = it->second.begin();
        while (effectedIt != it->second.end()) {
            auto rawIt2 = std::find(removedDecls.begin(), removedDecls.end(), *effectedIt);
            if (rawIt2 != removedDecls.end()) {
                effectedIt = it->second.erase(effectedIt);
            } else {
                ++effectedIt;
            }
        }
        ++it;
    }
}

extern void DeleteRecompiledNodesInCHIROptEffectMap(
    const std::unordered_set<Ptr<AST::Decl>>& recompiledNodes, OptEffectStrMap& effectMap)
{
    std::unordered_set<std::string> recompiledMangledName;
    for (auto decl : recompiledNodes) {
        if (auto mainDecl = DynamicCast<AST::MainDecl*>(decl); mainDecl) {
            recompiledMangledName.emplace(mainDecl->desugarDecl->rawMangleName);
        } else {
            recompiledMangledName.emplace(decl->rawMangleName);
        }
    }

    // if one decl is recompiled, we need to remove it from old `effectMap`
    // structure of `effectMap` is `src decl -> effected decls`, means `src decl` is optimized in `effected decls`
    // we only remove recompiled decl from `effected decls`, mustn't remove it from `src decl`
    auto it = effectMap.begin();
    while (it != effectMap.end()) {
        auto effectedIt = it->second.begin();
        while (effectedIt != it->second.end()) {
            if (recompiledMangledName.find(*effectedIt) != recompiledMangledName.cend()) {
                effectedIt = it->second.erase(effectedIt);
            } else {
                ++effectedIt;
            }
        }
        if (it->second.empty()) {
            it = effectMap.erase(it);
        } else {
            ++it;
        }
    }
}
///@}
}

static std::optional<CompilationCache> LoadCacheInfo(
    const std::string& path, const RawMangled2DeclMap& mangledName2DeclMap)
{
    std::vector<uint8_t> data;
    std::string failedReason;
    if (!FileUtil::ReadBinaryFileToBuffer(path, data, failedReason)) {
        return {};
    }

    HashedASTLoader loader(std::move(data));
    auto [succ, cachedInfo] = loader.DeserializeData(mangledName2DeclMap);
    if (!succ) {
        return {};
    }
    return cachedInfo;
}

namespace {
struct ImportPackageWalker {
    static std::tuple<ASTCache, RawMangled2DeclMap, SrcImportedDepMap, TypeMap> Get(
        const AST::Package& sourcePackage, const ImportManager& man)
    {
        ASTCache ret{};
        RawMangled2DeclMap retMap{};
        SrcImportedDepMap srcImportedDeclDeps{};
        TypeMap relations{}; // extra relations of imported packages
        std::vector<Ptr<AST::Decl>> collectedDecls;
        ImportPackageWalker w{retMap, srcImportedDeclDeps};
        for (auto& p : man.GetAllImportedPackages()) {
            if (p->srcPackage == &sourcePackage) {
                continue;
            }
            for (auto decl : GetAllDecls(*p)) {
                // Some compiler-added decls do not have raw mangle name, skip them
                if (decl->rawMangleName.empty()) {
                    continue;
                }

                w.UpdateMangleNameAndCollectDep(*decl);
                retMap.emplace(decl->rawMangleName, decl);
                collectedDecls.emplace_back(decl);
            }
        }
        for (auto decl : collectedDecls) {
            auto declCache = w.VisitTopLevel(*decl);
            ret.emplace(decl->rawMangleName, declCache);
            relations.CollectImportedDeclExtraRelation(*decl);
        }

        return {std::move(ret), std::move(retMap), std::move(srcImportedDeclDeps), std::move(relations)};
    }

    static std::list<Ptr<AST::Decl>> GetAllDecls(const AST::PackageDecl& package)
    {
        std::list<Ptr<AST::Decl>> res{};
        for (auto& file : package.srcPackage->files) {
            for (auto& decl : file->decls) {
                res.push_back(decl.get());
            }
            for (auto& decl : file->exportedInternalDecls) {
                res.push_back(decl.get());
            }
        }
        return res;
    }

    /**
     * @brief Convert decl's rawMangleName to appendedMangleName based on ty,
     * used to compare imported decls at ASTDiff
     *
     * @param decl Imported AST::Decl from cjo
     *
     * erase characters after '$$' on rawMangleName and concatenate ty->String()
     * e.g `func foo(){Int32(0)}`
     * rawMangleName is _CN1A3foob$bFE$$E
     * after processing: _CN1A3foob$bFE$$Int32E
     */
    static void ReplaceTypeWithTyInMangleName(AST::Decl& decl)
    {
        // macro return type is always tokens; primary ctor always returns nothing;
        // main decl is never imported, so only func and var decl are covered here
        if (Utils::NotIn(decl.astKind, {AST::ASTKind::FUNC_DECL, AST::ASTKind::VAR_DECL}) ||
            decl.TestAnyAttr(AST::Attribute::CONSTRUCTOR, AST::Attribute::MACRO_FUNC) || !decl.GetTy().IsCorrect() ||
            decl.rawMangleName == TO_ANY) {
            // 1. Ignore VarWithPatternDecl because there is no VarWithPatternDecl in cjo.
            // 2. Constructor's return type will not be changed.
            return;
        }
        auto& mangledName = decl.rawMangleName;
        auto pos = mangledName.rfind("$$");
        CJC_ASSERT(pos != std::string::npos);
        const static auto DELIMITER_LENGTH = std::string("$$").size();
        (void)mangledName.erase(pos + DELIMITER_LENGTH);
        if (decl.astKind == AST::ASTKind::FUNC_DECL) {
            auto funcTy = StaticCast<AST::FuncTy*>(decl.DataTy());
            CJC_ASSERT(funcTy && funcTy->retTy);
            mangledName += funcTy->retTy.String();
        } else {
            mangledName += decl.GetTy().String();
        }
    }

    ImportPackageWalker(RawMangled2DeclMap& map, SrcImportedDepMap& srcImportedDeps)
        : map{map}, srcImportedDeps{srcImportedDeps}
    {
    }
    RawMangled2DeclMap& map; // mangled -> decl is collected here
    SrcImportedDepMap& srcImportedDeps;

    bool SkipImportedBodyHash(const AST::Decl& decl) const
    {
        if (decl.IsConst()) {
            return false;
        }
        if (auto varDecl = DynamicCast<const AST::VarDecl*>(&decl)) {
            if (varDecl->initializer == nullptr) {
                return true;
            }
        }
        if (auto funcDecl = DynamicCast<const AST::FuncDecl*>(&decl)) {
            CJC_ASSERT(funcDecl->funcBody);
            if (funcDecl->funcBody->body != nullptr) {
                return false;
            }
            // if any parameter has desugar decl, it has a default value and the default value function can be inlined,
            // so body hash of the function cannot be ignored
            for (auto& param: funcDecl->funcBody->paramLists[0]->params) {
                if (param->desugarDecl && param->desugarDecl->funcBody && param->desugarDecl->funcBody->body) {
                    return false;
                }
            }
            return true;
        }

        return false;
    }

    TopLevelDeclCache VisitTopLevel(AST::Decl& decl)
    {
        (void)map.emplace(decl.rawMangleName, &decl);
        TopLevelDeclCache ret{};
        for (auto member : GetMembers(decl)) {
            // some compiler-added decls do not have raw mangle name, skip them
            if (member->rawMangleName.empty()) {
                continue;
            }
            ret.members.push_back(VisitMember(*member));
        }
        ret.instVarHash = decl.hash.instVar;
        ret.virtHash = decl.hash.virt;
        ret.sigHash = decl.hash.sig;
        ret.srcUse = decl.hash.srcUse;
        ret.bodyHash = SkipImportedBodyHash(decl) ? 0 : ASTHasher::ImportedDeclBodyHash(decl);
        // no gvid for imported decl
        return ret;
    }

    MemberDeclCache VisitMember(AST::Decl& decl)
    {
        (void)map.emplace(decl.rawMangleName, &decl);
        MemberDeclCache ret{};
        for (auto member : IncrementalCompilation::GetMembers(decl)) {
            // some compiler-added decls do not have raw mangle name, skip them
            if (member->rawMangleName.empty()) {
                continue;
            }
            ret.members.push_back(VisitMember(*member));
        }
        ret.rawMangle = decl.rawMangleName;
        ret.sigHash = decl.hash.sig;
        ret.srcUse = decl.hash.srcUse;
        ret.bodyHash = SkipImportedBodyHash(decl) ? 0 : ASTHasher::ImportedDeclBodyHash(decl);
        return ret;
    }

    void UpdateMangleNameAndCollectDep(AST::Decl& decl)
    {
        ReplaceTypeWithTyInMangleName(decl);
        switch (decl.astKind) {
            // primary ctors are desugared into normal ctors in imported packages;
            // macro are not collected as they are used only before ASTDiff but not after it;
            // main are never exported; so only normal functions are considered here.
            case ASTKind::FUNC_DECL: {
                auto& d = StaticCast<AST::FuncDecl&>(decl);
                // collect source dependencies only when the function decl has a body in imported packages
                if (d.funcBody && d.funcBody->body) {
                    CollectSrcImportedDep(decl, *d.funcBody->body);
                }
                break;
            }
            case ASTKind::VAR_DECL: {
                auto& d = StaticCast<AST::VarDecl&>(decl);
                if (d.initializer) {
                    CollectSrcImportedDep(decl, *d.initializer);
                }
                break;
            }
            default:
                break;
        }
        for (auto member : GetMembers(decl)) {
            // some compiler-added decls do not have raw mangle name, skip them
            if (member->rawMangleName.empty()) {
                continue;
            }
            UpdateMangleNameAndCollectDep(*member);
        }
    }

    void CollectSrcImportedDep(const AST::Decl& decl, AST::Expr& bodyExpr) const
    {
        AST::Walker(&bodyExpr, [&](auto node) {
            if (auto usedDecl = node->GetTarget()) {
                srcImportedDeps[usedDecl].insert(&decl);
            }
            return AST::VisitAction::WALK_CHILDREN;
        }).Walk();
    }
};
} // namespace

static IncreResult TerminateAnalysisAndRollback(
    CompilationCache&& cache, IncrementalScopeAnalysisArgs&& args, const std::string& msg)
{
    auto [importedASTCache, importedMangled2Decl, sourcePopulations, rel] =
        ImportPackageWalker::Get(args.srcPackage, args.importer);
    cache.curPkgASTCache = std::move(args.astCacheInfo);
    cache.importedASTCache = std::move(importedASTCache);
    importedMangled2Decl.insert(args.rawMangleName2DeclMap.begin(), args.rawMangleName2DeclMap.end());
    IncrementalCompilationLogger::GetInstance().LogLn(msg);
    return {IncreKind::ROLLBACK, {}, {}, {}, std::move(cache), std::move(importedMangled2Decl)};
}

static bool IsCCOutFuncNeedToRecompileOrDelete(
    const CompilationCache& preCache, const PollutionResult& pollutionRes)
{
    if (preCache.ccOutFuncs.empty()) {
        return false;
    }
    for (auto& rawMangle : pollutionRes.deleted) {
        if (auto it = preCache.ccOutFuncs.find(rawMangle); it != preCache.ccOutFuncs.end()) {
            IncrementalCompilationLogger::GetInstance().LogLn("closure convert out func recompile: " + rawMangle);
            return true;
        }
    }
    for (auto decl : pollutionRes.declsToRecompile) {
        if (auto it = preCache.ccOutFuncs.find(decl->rawMangleName); it != preCache.ccOutFuncs.end()) {
            IncrementalCompilationLogger::GetInstance().LogLn(
                "closure convert out func recompile: " + decl->rawMangleName);
            return true;
        }
    }
    return false;
}

namespace {
void BuildMangleMapForCachedInfo(
    const MemberDeclCache& memCache, std::unordered_map<RawMangledName, std::string>& mangleMap)
{
    mangleMap[memCache.rawMangle] = memCache.cgMangle;
    for (auto& m : memCache.members) {
        BuildMangleMapForCachedInfo(m, mangleMap);
    }
}

void BuildMangleMapForCachedInfo(const std::string& rawMangle, const TopLevelDeclCache& topCache,
    std::unordered_map<RawMangledName, std::string>& mangleMap)
{
    mangleMap[rawMangle] = topCache.cgMangle;
    for (auto& m : topCache.members) {
        BuildMangleMapForCachedInfo(m, mangleMap);
    }
}

std::list<std::string> GetCgManglesForDelete(
    const CompilationCache& cachedInfo, const std::list<RawMangledName>& deleted)
{
    if (deleted.empty()) {
        return {};
    }
    std::unordered_map<RawMangledName, std::string> mangleMap{};
    for (const auto& it : cachedInfo.curPkgASTCache) {
        auto rawMangle = it.first;
        BuildMangleMapForCachedInfo(rawMangle, it.second, mangleMap);
    }
    for (const auto& it : cachedInfo.importedASTCache) {
        auto rawMangle = it.first;
        BuildMangleMapForCachedInfo(rawMangle, it.second, mangleMap);
    }
    std::list<std::string> deletedMangleNames;
    for (auto d : deleted) {
        if (auto it = mangleMap.find(d); it != mangleMap.end()) {
            deletedMangleNames.push_back(it->second);
        } else {
            CJC_ABORT();
        }
    }
    return deletedMangleNames;
}
}

IncreResult Cangjie::IncrementalScopeAnalysis(IncrementalScopeAnalysisArgs&& args)
{
    // Load the compilation cache
    std::string cachePath = args.op.GenerateCachedPathName(args.srcPackage.fullPackageName, CACHED_AST_EXTENSION);
    auto loadingRes = LoadCacheInfo(cachePath, args.rawMangleName2DeclMap);
    bool badCache = !loadingRes.has_value();
    std::string cjoPath = args.op.GenerateCachedPathName(args.srcPackage.fullPackageName, SERIALIZED_FILE_EXTENSION);
    if (badCache || !FileUtil::FileExist(cjoPath)) {
        return TerminateAnalysisAndRollback(
            CompilationCache{}, std::move(args), "load cached info failed, roll back to full compilation");
    }

    auto& prevCompilationCache = loadingRes.value();
    bool compilationArgsChange = args.op.ToSerialized() != prevCompilationCache.compileArgs;
    if (compilationArgsChange) {
        return TerminateAnalysisAndRollback(
            std::move(prevCompilationCache), std::move(args), "compile args changed, roll back to full compilation");
    }

    bool specsChange = ASTHasher::HashSpecs(args.srcPackage) != prevCompilationCache.specs;
    if (specsChange) {
        return TerminateAnalysisAndRollback(std::move(prevCompilationCache), std::move(args),
            "package or import specs changed, roll back to full compilation");
    }

    auto [importedASTCache, importedMangled2Decl, sourcePopulations, importRelations] =
        ImportPackageWalker::Get(args.srcPackage, args.importer);

    // Compare the previous compilation cache with the current AST (including the imported part)
    auto [modifiedDecls, mangle2decl] = ASTDiff({prevCompilationCache,
        importedASTCache, importedMangled2Decl, args.rawMangleName2DeclMap, args.astCacheInfo, args.fileMap, args.op});
    modifiedDecls.Dump();

    if (!modifiedDecls.aliases.empty()) {
        return TerminateAnalysisAndRollback(
            std::move(prevCompilationCache), std::move(args), "type alias changed, roll back to full compilation");
    }

    // Analyse the pollution scope caused by the raw code changes
    auto srcPkgDecl = args.importer.GetPackageDecl(args.srcPackage.fullPackageName);
    CJC_NULLPTR_CHECK(srcPkgDecl);
    auto pollutionRes = PollutionAnalyzer::Get({std::move(modifiedDecls), *srcPkgDecl, sourcePopulations,
        prevCompilationCache.semaInfo, prevCompilationCache.chirOptInfo, prevCompilationCache.fileMap, args.importer,
        mangle2decl, std::move(args.directExtends), std::move(importRelations)});
    // not support closure convert incr
    if (IsCCOutFuncNeedToRecompileOrDelete(prevCompilationCache, pollutionRes)) {
        DumpDeclsToRecompile(pollutionRes.declsToRecompile);
        DumpDeleted(pollutionRes.deleted);
        return TerminateAnalysisAndRollback(std::move(prevCompilationCache), std::move(args),
            "closure convert out func recompile, roll back to full compilation");
    }
    // Mark changed decls to be recompiled.
    if (pollutionRes.kind == IncreKind::INCR) {
        for (auto decl : pollutionRes.declsToRecompile) {
            decl->toBeCompiled = true;
        }
    }
    auto deletedMangleNames = GetCgManglesForDelete(prevCompilationCache, pollutionRes.deleted);
    // Update compilation cache as the result of current compilation process
    prevCompilationCache.curPkgASTCache = std::move(args.astCacheInfo);
    prevCompilationCache.importedASTCache = std::move(importedASTCache);

    return {pollutionRes.kind, std::move(pollutionRes.declsToRecompile), std::move(pollutionRes.deleted),
        deletedMangleNames, std::move(prevCompilationCache), std::move(mangle2decl),
        std::move(pollutionRes.reBoxedTypes)};
}
