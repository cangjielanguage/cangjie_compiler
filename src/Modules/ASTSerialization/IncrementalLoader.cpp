// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 * This file implements the AST Loader related classes.
 */

#include "ASTLoaderImpl.h"

#include <algorithm>

#include "flatbuffers/CjoFormat_generated.h"

#include "cangjie/AST/Utils.h"
#include "cangjie/Basic/Version.h"
#include "cangjie/Utils/CheckUtils.h"
#include "cangjie/AST/ASTCasting.h"
#include "cangjie/Utils/FileUtil.h"
#include "cangjie/IncrementalCompilation/IncrementalCompilationLogger.h"

using namespace Cangjie;
using namespace AST;

namespace {
inline bool IsChangedDeclMayOmitType(const Decl& decl)
{
    // Return type of constructor is always omitted and will not be changed.
    return decl.toBeCompiled && !decl.TestAttr(Attribute::CONSTRUCTOR) &&
        (decl.astKind == ASTKind::FUNC_DECL || decl.astKind == ASTKind::VAR_DECL || decl.TestAttr(Attribute::GENERIC));
}

inline bool DoNotLoadCache(const Decl& decl)
{
    return Utils::In(decl.astKind, {ASTKind::PRIMARY_CTOR_DECL, ASTKind::TYPE_ALIAS_DECL, ASTKind::FUNC_PARAM});
}
} // namespace

/** For incremental compilation to loading cached types. */
std::unordered_set<std::string> ASTLoader::LoadCachedTypeForPackage(
    const Package& sourcePackage, const std::map<std::string, Ptr<Decl>>& mangledName2DeclMap)
{
    return pImpl->LoadCachedTypeForPackage(sourcePackage, mangledName2DeclMap);
}

void ASTLoader::ASTLoaderImpl::LoadCachedTypeForDecl(const PackageFormat::Decl& decl, Decl& astDecl)
{
    astDecl.exportId = decl.exportId()->str();
    astDecl.mangledName = decl.mangledName()->str();
    // Add target for type decl.
    AddDeclToImportedPackage(astDecl);
    if (auto fd = DynamicCast<FuncDecl*>(&astDecl); fd && fd->funcBody && !fd->funcBody->paramLists.empty()) {
        auto info = decl.info_as_FuncInfo();
        CJC_NULLPTR_CHECK(info);
        CJC_NULLPTR_CHECK(info->funcBody());
        auto paramList = info->funcBody()->paramLists()->Get(0);
        uoffset_t length = static_cast<uoffset_t>(paramList->params()->size());
        auto paramLen = fd->funcBody->paramLists[0]->params.size();
        CJC_ASSERT(paramLen == length);
        for (uoffset_t offset = 0; offset < length; ++offset) {
            auto index = paramList->params()->Get(offset);
            (void)allLoadedDecls.emplace(index, fd->funcBody->paramLists[0]->params[offset].get());
        }
    }
    // Load target for unchanged annotation decl.
    if (!astDecl.toBeCompiled && GetAttributes(decl).TestAttr(Attribute::IS_ANNOTATION)) {
        astDecl.EnableAttr(Attribute::IS_ANNOTATION);
        auto& annotations = astDecl.annotations;
        auto found = std::find_if(annotations.begin(), annotations.end(),
            [](const auto& it) { return it->kind == AnnotationKind::ANNOTATION; });
        CJC_ASSERT(found != annotations.end());
        auto info = decl.info_as_ClassInfo();
        CJC_NULLPTR_CHECK(info);
        found->get()->target = info->annoTargets();
        found->get()->runtimeVisible = info->runtimeVisible();
    }
    // Add target for generic parameter decl.
    if (auto generic = astDecl.GetGeneric(); generic && decl.generic()) {
        auto genericIdx = decl.generic();
        generic->EnableAttr(Attribute::INCRE_COMPILE);
        CJC_NULLPTR_CHECK(genericIdx->typeParameters());
        uoffset_t length = static_cast<uoffset_t>(genericIdx->typeParameters()->size());
        auto paramLen = generic->typeParameters.size();
        CJC_ASSERT(paramLen == length);
        for (uoffset_t i = 0; i < length; i++) {
            auto gpdIdx = genericIdx->typeParameters()->Get(i);
            CJC_ASSERT(gpdIdx != INVALID_FORMAT_INDEX);
            auto gpd = generic->typeParameters[i].get();
            gpd->exportId = GetFormatDeclByIndex(gpdIdx)->exportId()->str();
            gpd->outerDecl = &astDecl;
            AddDeclToImportedPackage(*gpd);
            (void)allLoadedDecls.emplace(gpdIdx, gpd);
        }
    }
}

void ASTLoader::ASTLoaderImpl::PrepareForLoadTypeCache(const Package& pkg)
{
    for (uoffset_t i = 0; i < package->imports()->size(); i++) {
        std::string importItem = package->imports()->Get(i)->str();
        importedFullPackageNames.emplace_back(importItem);
    }
    importedPackageName = pkg.fullPackageName;
    // Default file ids' values for cache are 0 which is kept for dummy 'LoadPos' usage during loding cached type.
    // NOTE: Size should be the lager value between current and privous number of files.
    allFileIds.resize(std::max(pkg.files.size(), static_cast<size_t>(package->allFiles()->size())));
    // Prepare file id info for loading cache.
    for (uint32_t i = 0; i < pkg.files.size(); ++i) {
        allFileIds[i] = pkg.files[i]->begin.fileID;
    }
}

void ASTLoader::ASTLoaderImpl::CollectRemovedDefaultImpl(
    const Decl& astDecl, const PackageFormat::Decl& decl, std::unordered_set<std::string>& needRemoved) const
{
    auto body = astDecl.astKind == ASTKind::CLASS_DECL ? decl.info_as_ClassInfo()->body()
        : astDecl.astKind == ASTKind::INTERFACE_DECL   ? decl.info_as_InterfaceInfo()->body()
        : astDecl.astKind == ASTKind::ENUM_DECL        ? decl.info_as_EnumInfo()->body()
        : astDecl.astKind == ASTKind::STRUCT_DECL      ? decl.info_as_StructInfo()->body()
        : astDecl.astKind == ASTKind::EXTEND_DECL      ? decl.info_as_ExtendInfo()->body()
                                                  : nullptr;
    if (!astDecl.toBeCompiled || !body) {
        return;
    }
    for (auto index : *body) {
        auto member = GetFormatDeclByIndex(index);
        CJC_NULLPTR_CHECK(member);
        // Copied default implementation inside changed decl should be removed.
        if (member->mangledBeforeSema()->str().empty() && GetAttributes(*member).TestAttr(Attribute::DEFAULT)) {
            CollectRemovedDecls(*member, needRemoved);
        }
    }
}

namespace {
void CollectRemovedDecl(const std::string& mangledName, std::unordered_set<std::string>& needRemoved)
{
    needRemoved.emplace(mangledName);
    if (IncrementalCompilationLogger::GetInstance().IsEnable()) {
        IncrementalCompilationLogger::GetInstance().LogLn("[CollectRemovedDecl] removed mangled: " + mangledName);
    }
}
} // namespace

void ASTLoader::ASTLoaderImpl::CollectRemovedDecls(
    const PackageFormat::Decl& decl, std::unordered_set<std::string>& needRemoved) const
{
    if (decl.kind() == PackageFormat::DeclKind_FuncDecl) {
        CollectRemovedDecl(decl.mangledName()->str(), needRemoved);
        auto info = decl.info_as_FuncInfo();
        CJC_NULLPTR_CHECK(info);
        CJC_ASSERT(info->funcBody() && info->funcBody()->paramLists());
        auto fpls = info->funcBody()->paramLists();
        CJC_NULLPTR_CHECK(fpls);
        for (auto fpl : *fpls) {
            auto desugars = fpl->desugars();
            CJC_NULLPTR_CHECK(desugars);
            for (auto desugarIndex : *fpl->desugars()) {
                if (desugarIndex == INVALID_FORMAT_INDEX) {
                    continue;
                }
                auto member = GetFormatDeclByIndex(desugarIndex);
                CJC_NULLPTR_CHECK(member);
                CollectRemovedDecl(member->mangledName()->str(), needRemoved);
            }
        }
    } else if (decl.kind() == PackageFormat::DeclKind_PropDecl) {
        auto info = decl.info_as_PropInfo();
        CJC_NULLPTR_CHECK(info);
        for (auto index : *info->setters()) {
            auto setter = GetFormatDeclByIndex(index);
            CollectRemovedDecl(setter->mangledName()->str(), needRemoved);
        }
        for (auto index : *info->getters()) {
            auto getter = GetFormatDeclByIndex(index);
            CollectRemovedDecl(getter->mangledName()->str(), needRemoved);
        }
    } else if (decl.kind() == PackageFormat::DeclKind_VarDecl) {
        auto attr = GetAttributes(decl);
        if (attr.TestAttr(Attribute::GLOBAL) || attr.TestAttr(Attribute::STATIC)) {
            CollectRemovedDecl(decl.mangledName()->str(), needRemoved);
        }
    } else {
        auto body = decl.kind() == PackageFormat::DeclKind_ClassDecl ? decl.info_as_ClassInfo()->body()
            : decl.kind() == PackageFormat::DeclKind_InterfaceDecl   ? decl.info_as_InterfaceInfo()->body()
            : decl.kind() == PackageFormat::DeclKind_EnumDecl        ? decl.info_as_EnumInfo()->body()
            : decl.kind() == PackageFormat::DeclKind_StructDecl      ? decl.info_as_StructInfo()->body()
            : decl.kind() == PackageFormat::DeclKind_ExtendDecl      ? decl.info_as_ExtendInfo()->body()
                                                                     : nullptr;
        if (!body) {
            return;
        }
        if (decl.kind() != PackageFormat::DeclKind_ExtendDecl) {
            CollectRemovedDecl(decl.mangledName()->str(), needRemoved);
        }
        for (auto index : *body) {
            auto member = GetFormatDeclByIndex(index);
            CJC_NULLPTR_CHECK(member);
            CollectRemovedDecls(*member, needRemoved);
        }
    }
}

void ASTLoader::ASTLoaderImpl::ClearInstantiatedCache(
    const std::vector<uoffset_t>& instantiatedDeclIndexes, std::unordered_set<std::string>& needRemoved)
{
    // Load previous generated instantiated decls.
    for (auto idx : instantiatedDeclIndexes) {
        auto declInfo = GetFormatDeclByIndex(idx);
        auto genericDecl = GetDeclFromIndex(declInfo->genericDecl());
        if (!genericDecl || genericDecl->toBeCompiled) {
            // Ignore the instantiated version when:
            // 1. The generic version is not found from current package;
            // 2. The generic version is modified and need to be recompiled.
            CollectRemovedDecls(*declInfo, needRemoved);
        }
    }
}

template <class DeclT> void ASTLoader::ASTLoaderImpl::LoadDesugarDecl(DeclT& decl, uoffset_t index)
{
    if (decl.toBeCompiled) {
        return;
    }
    // Load desugared decl if current main or macro decl does not need to be compiled in incremental step.
    decl.desugarDecl = LoadDecl<FuncDecl>(index);
    CJC_NULLPTR_CHECK(decl.desugarDecl);
    decl.desugarDecl->EnableAttr(Attribute::INCRE_COMPILE);
    decl.desugarDecl->DisableAttr(Attribute::IMPORTED);
}

bool ASTLoader::ASTLoaderImpl::HasInlineDefaultParamFunc(const PackageFormat::Decl& decl) const
{
    std::vector<std::string> ret;
    if (decl.kind() == PackageFormat::DeclKind_FuncDecl) {
        ret.push_back(decl.mangledName()->str());
        auto info = decl.info_as_FuncInfo();
        CJC_NULLPTR_CHECK(info);
        CJC_ASSERT(info->funcBody() && info->funcBody()->paramLists());
        auto pl = info->funcBody()->paramLists();
        CJC_NULLPTR_CHECK(pl);
        for (auto l : *pl) {
            auto desugars = l->desugars();
            CJC_NULLPTR_CHECK(desugars);
            for (auto desugarIndex : *desugars) {
                if (desugarIndex == INVALID_FORMAT_INDEX) {
                    continue;
                }
                auto& desugarDecl = *GetFormatDeclByIndex(desugarIndex);
                auto attrs = GetAttributes(desugarDecl);
                auto info_ = desugarDecl.info_as_FuncInfo();
                if (attrs.TestAttr(Attribute::HAS_INITIAL) && attrs.TestAttr(Attribute::IMPLICIT_ADD) && info_ &&
                    info_->isInline() && cjoManager.GetCanInline()) {
                    return true;
                }
            }
        }
    }
    return false;
}

// Should be called after import package, before typecheck.
std::unordered_set<std::string> ASTLoader::ASTLoaderImpl::LoadCachedTypeForPackage(
    const Package& sourcePackage, const std::map<std::string, Ptr<Decl>>& mangledName2DeclMap)
{
    // 1. Verify data and prepare cache for imported package name.
    if (!VerifyForData("cached type")) {
        return {};
    }
    CacheLoadingStatus ctx(isLoadCache);
    package = PackageFormat::GetPackage(data.data());
    // 2. Prepare cache for file ids.
    // NOTE: 'ASTDiff' guarantees files in previous compilation are same as the current compilation.
    //       So the 'fileID' is same with the 'fileIndex'.
    PrepareForLoadTypeCache(sourcePackage);
    // 3. Load type cache for decls which are not changed, and collect some unfounded for incr compile to remove
    auto& logger = IncrementalCompilationLogger::GetInstance();
    logger.LogLn("[LoadCachedTypeForPackage] begin collect some unfounded for incr compile to remove:");
    std::vector<uoffset_t> instantiatedDeclIndexes;
    std::vector<uoffset_t> genericIdx;
    std::unordered_set<std::string> unfounded;
    for (uoffset_t i = 0; i < package->allDecls()->size(); i++) {
        // Only toplevel decls are loaded.
        // NOTE: FormattedIndex is vector offset plus 1.
        auto realIdx = i + 1;
        auto decl = GetFormatDeclByIndex(realIdx);
        CJC_NULLPTR_CHECK(decl);
        if (GetAttributes(*decl).TestAttr(Attribute::GENERIC_INSTANTIATED)) {
            instantiatedDeclIndexes.emplace_back(realIdx);
            continue;
        }
        auto mangleIdx = decl->mangledBeforeSema();
        CJC_NULLPTR_CHECK(mangleIdx);
        auto found = mangledName2DeclMap.find(mangleIdx->str());
        if (found == mangledName2DeclMap.end()) {
            // Record 'mangledName' of decls which have rawMangledName.
            if (!mangleIdx->str().empty()) {
                CollectRemovedDecls(*decl, unfounded);
            }
            continue;
        }
        if (IsChangedDeclMayOmitType(*found->second) || DoNotLoadCache(*found->second)) {
            if (found->second->TestAttr(Attribute::GENERIC)) {
                // Add generic index for finding generic reference of instantiation.
                allLoadedDecls.emplace(realIdx, found->second);
                genericIdx.emplace_back(realIdx);
            }
            continue;
        }
        if (auto mainDecl = DynamicCast<MainDecl*>(found->second)) {
            LoadDesugarDecl(*mainDecl, realIdx);
        } else if (auto macroDecl = DynamicCast<MacroDecl*>(found->second)) {
            LoadDesugarDecl(*macroDecl, realIdx);
        } else {
            LoadCachedTypeForDecl(*decl, *found->second);
            CollectRemovedDefaultImpl(*found->second, *decl, unfounded);
            (void)allLoadedDecls.emplace(realIdx, found->second);
            // If the decl's definition body was exported, it should be marked as re-compiled.
            auto info = decl->info_as_FuncInfo();
            auto index = decl->info_as_VarInfo()     ? decl->info_as_VarInfo()->initializer()
                : decl->info_as_VarWithPatternInfo() ? decl->info_as_VarWithPatternInfo()->initializer()
                : decl->info_as_AliasInfo()          ? decl->info_as_AliasInfo()->aliasedTy()
                : decl->info_as_ParamInfo()          ? decl->info_as_ParamInfo()->defaultVal()
                                                     : INVALID_FORMAT_INDEX;
            if (index != INVALID_FORMAT_INDEX || (info && info->isInline() && cjoManager.GetCanInline()) ||
                HasInlineDefaultParamFunc(*decl)) {
                found->second->toBeCompiled = true;
            }
        }
    }
    // 3. Clear instantiated cache, and remove generic index from 'allLoadedDecls'
    ClearInstantiatedCache(instantiatedDeclIndexes, unfounded);
    std::for_each(genericIdx.begin(), genericIdx.end(), [this](auto idx) { allLoadedDecls.erase(idx); });
    // NOTE:
    // For first stage of incremental compilation, the source imported non-generic decls in caced packge can be ignored.
    // That kind of decl is not allowed in incremental compilation.
    // NOT support LSP situation for now.
    allTypes.resize(package->allTypes()->size(), nullptr);
    LoadRefs();
    return unfounded;
}
