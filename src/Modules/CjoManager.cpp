// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the CjoManager related classes.
 */

#include "cangjie/Modules/CjoManager.h"

#include <queue>

#include "CjoManagerImpl.h"
#include "cangjie/AST/ASTCasting.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Modules/ASTSerialization.h"
#include "cangjie/Modules/ModulesUtils.h"
#include "cangjie/Utils/ProfileRecorder.h"

using namespace Cangjie;
using namespace AST;

namespace Cangjie {
namespace {
void AddDeclToMap(Decl& decl, std::map<std::string, OrderedDeclSet>& declMap)
{
    if (decl.astKind == ASTKind::EXTEND_DECL) {
        return; // ExtendDecl cannot be referenced by name.
    }
    if (auto vpd = DynamicCast<VarWithPatternDecl*>(&decl); vpd) {
        // A VarWithPatternDecl is viewed as a collection of VarDecls.
        Walker walker(vpd->irrefutablePattern.get(), [&declMap](Ptr<Node> node) {
            if (auto vd = DynamicCast<VarDecl>(node)) {
                declMap[vd->identifier].emplace(vd);
            }
            return VisitAction::WALK_CHILDREN;
        });
        walker.Walk();
    } else if (auto funcDecl = DynamicCast<FuncDecl*>(&decl);
        funcDecl == nullptr || (!funcDecl->TestAttr(Attribute::MAIN_ENTRY) && funcDecl->identifier != MAIN_INVOKE)) {
        // Main function won't be imported.
        declMap[decl.identifier].emplace(&decl);
    }
}

bool CanInline(const GlobalOptions& opts)
{
    return opts.chirLLVM && opts.optimizationLevel > GlobalOptions::OptimizationLevel::O1 && !opts.enableCompileTest &&
        !opts.enableHotReload;
}
} // namespace

CjoManager::CjoManager(const CjoManager::Config& config) : impl{new CjoManagerImpl{config}}
{
}

CjoManager::~CjoManager()
{
    DeleteASTLoaders();
    delete impl;
}

void CjoManager::DeleteASTLoaders() const noexcept
{
    for (auto& p : impl->GetPackageNameMap()) {
        delete p.second->loader.get();
        p.second->loader = nullptr;
    }
}

CjoManagerImpl::CjoManagerImpl(const CjoManager::Config& config)
    : diag(config.diag),
      typeManager(config.typeManager),
      globalOptions(config.globalOptions),
      importSrcCode(config.importSrcCode),
      canInline(CanInline(config.globalOptions))
{
}

Ptr<CjoManagerImpl::PackageInfo> CjoManagerImpl::GetPackageInfo(const std::string& fullPackageName) const
{
    auto iter = packageNameMap.find(fullPackageName);
    if (iter != packageNameMap.cend()) {
        CJC_NULLPTR_CHECK(iter->second);
        return iter->second.get();
    }
    return nullptr;
}

bool CjoManager::IsOnlyUsedByMacro(const std::string& fullPackageName) const
{
    auto info = impl->GetPackageInfo(fullPackageName);
    return info ? info->onlyUsedByMacro : false;
}

void CjoManager::SetOnlyUsedByMacro(const std::string& fullPackageName, bool onlyUsedByMacro) const
{
    auto info = impl->GetPackageInfo(fullPackageName);
    if (info != nullptr) {
        info->onlyUsedByMacro = onlyUsedByMacro;
    }
}

bool CjoManager::IsMacroRelatedPackageName(const std::string& fullPackageName) const
{
    auto info = impl->GetPackageInfo(fullPackageName);
    return info != nullptr ? info->onlyUsedByMacro : false;
}

void CjoManager::UpdateSearchPath(const std::string& cangjieModules) const
{
    impl->UpdateSearchPath(cangjieModules);
}

const std::vector<std::string>& CjoManager::GetSearchPath() const
{
    return impl->GetSearchPath();
}

bool CjoManager::GetCanInline() const
{
    return impl->GetCanInline();
}

void CjoManager::SetPackageCjoCache(
    const std::string& fullPackageName, const std::vector<uint8_t>& cjoData, CjoChangeState changeState) const
{
    impl->SetPackageCjoCache(fullPackageName, cjoData, changeState);
}

void CjoManager::ClearCjoCache() const
{
    impl->ClearCjoCache();
}

void CjoManager::ClearVisitedPkgs() const
{
    impl->ClearVisitedPkgs();
}

void CjoManager::ClearForReBuildIndex() const
{
    impl->ClearForReBuildIndex();
}

bool CjoManager::HasBuildIndex() const
{
    return impl->HasBuildIndex();
}

void CjoManager::SetHasBuildIndex(bool value) const
{
    impl->SetHasBuildIndex(value);
}

DiagnosticEngine& CjoManager::GetDiag() const
{
    return impl->GetDiag();
}

Ptr<std::unordered_map<std::string, Ptr<AST::Decl>>> CjoManager::GetExportIdDeclMap(
    const std::string& fullPackageName) const
{
    return impl->GetExportIdDeclMap(fullPackageName);
}

std::vector<OwnedPtr<ASTLoader>>& CjoManager::GetCommonPartCjos(std::string packageName) const
{
    return impl->GetCommonPartCjos(packageName, *this);
}

Ptr<PackageDecl> CjoManager::GetPackageDecl(const std::string& fullPackageName) const
{
    auto info = impl->GetPackageInfo(fullPackageName);
    if (info == nullptr) {
        return nullptr;
    }
    return info->pkgDecl.get();
}

Ptr<Package> CjoManager::GetPackage(const std::string& fullPackageName) const
{
    auto info = impl->GetPackageInfo(fullPackageName);
    if (info == nullptr) {
        return nullptr;
    }
    return info->pkg.get();
}

std::string CjoManager::GetPackageDepInfo(const std::string& cjoPath) const
{
    auto loader = impl->ReadCjo("", cjoPath, *this);
    if (loader == nullptr) {
        return "";
    }
    return loader->LoadPackageDepInfo();
}

Ptr<std::unordered_map<std::string, Ptr<AST::Decl>>> CjoManagerImpl::GetExportIdDeclMap(
    const std::string& fullPackageName) const
{
    auto iter = packageNameMap.find(fullPackageName);
    if (iter != packageNameMap.cend()) {
        CJC_NULLPTR_CHECK(iter->second);
        return &iter->second->exportIDDeclMap;
    }
    return nullptr;
}

const std::map<std::string, OrderedDeclSet>& CjoManager::GetPackageMembers(const std::string& fullPackageName) const
{
    const static std::map<std::string, OrderedDeclSet> EMPTY_MAP;
    auto iter = impl->GetPackageNameMap().find(fullPackageName);
    if (iter != impl->GetPackageNameMap().cend()) {
        CJC_NULLPTR_CHECK(iter->second);
        return iter->second->declMap;
    }
    return EMPTY_MAP;
}

const OrderedDeclSet& CjoManager::GetPackageMembersByName(
    const std::string& fullPackageName, const std::string& name) const
{
    const static OrderedDeclSet EMPTY_DECLS;
    auto& declMap = GetPackageMembers(fullPackageName);
    auto iter = declMap.find(name);
    if (iter != declMap.cend()) {
        return iter->second;
    }
    return EMPTY_DECLS;
}

Ptr<Decl> CjoManager::GetImplicitPackageMembersByName(const std::string& fullPackageName, const std::string& name) const
{
    auto info = impl->GetPackageInfo(fullPackageName);
    if (!info) {
        return nullptr;
    }
    auto found = info->implicitDeclMap.find(name);
    return found == info->implicitDeclMap.end() ? nullptr : found->second;
}

bool CjoManager::LoadPackageHeader(const std::string& fullPackageName, const std::string& cjoPath) const
{
    auto iter = impl->GetPackageNameMap().find(fullPackageName);
    if (iter != impl->GetPackageNameMap().cend()) {
        return true;
    }
    auto loader = impl->ReadCjo(fullPackageName, cjoPath, *this);
    if (loader == nullptr) {
        return false;
    }
    auto pkg = loader->LoadPackageDependencies();
    if (pkg == nullptr) {
        return false;
    }
    auto pkgInfo = MakeOwned<CjoManagerImpl::PackageInfo>(CjoManagerImpl::PackageInfo(
        {.loader = loader.release(), .pkg = pkg.get(), .pkgDecl = MakeOwned<PackageDecl>(*pkg)}));
    impl->AddImportedPackages(pkg);
    impl->GetPackageNameMap().emplace(fullPackageName, std::move(pkgInfo));
    return true;
}

bool CjoManagerImpl::IsReExportBy(const std::string& srcPackage, const std::string& reExportPackage) const
{
    auto info = GetPackageInfo(srcPackage);
    CJC_NULLPTR_CHECK(info);
    for (auto& file : info->pkg->files) {
        for (auto& importSpec : file->imports) {
            if (!importSpec->IsReExport()) {
                continue;
            }
            auto found = importedPackageNameMap.find(importSpec.get());
            if (found != importedPackageNameMap.end() && found->second.first == reExportPackage) {
                return true;
            }
        }
    }
    return false;
}

bool CjoManager::NeedCollectDependency(const std::string& curName, bool isCurMacro, const std::string& depName) const
{
    if (depName == curName || impl->AlreadyLoaded(depName)) {
        return false;
    }

    // If current is macro package, only load decls for dependent macro package,
    // otherwise, load decls for all dependent package (macro package was filtered before).
    // NOTE: non-macro package's will never be used through macro package.
    if (auto depPd = GetPackageDecl(depName); depPd) {
        bool isDepMacro = depPd->srcPackage->isMacroPackage;
        if (!isCurMacro || isDepMacro || depName == AST_PACKAGE_NAME || impl->IsReExportBy(curName, depName)) {
            return true;
        }
    }

    return false;
}

bool CjoManager::LoadFilesOfCommonPart(Ptr<Package> pkg)
{
    if (!impl->GetGlobalOptions().IsCompilingCJMPSpecific() && impl->GetGlobalOptions().commonPartCjos.empty()) {
        return true;
    }
    CJC_NULLPTR_CHECK(pkg);
    // Remove existing isCommon files from pkg before loading new common part for lsp incremental compilation.
    Utils::EraseIf(pkg->files, [](const auto& file) { return file->TestAttr(Attribute::FROM_COMMON_PART); });
    for (Ptr<ASTLoader> commonLoader : GetCommonPartCjos(pkg->fullPackageName)) {
        if (!commonLoader->PreloadCommonPartOfPackage(*pkg)) {
            return false;
        }
    }
    return true;
}

void CjoManager::LoadPackageDeclsOnDemand(const std::vector<Ptr<Package>>& packages, bool fromLsp) const
{
    Utils::ProfileRecorder recorder("ResolveImportedPackages", "LoadPackageDeclsOnDemand");
    // Add all directly imported package's loader.
    std::queue<Ptr<CjoManagerImpl::PackageInfo>> q;
    for (auto pkg : packages) {
        if (fromLsp) {
            q.push(impl->GetPackageInfo(pkg->fullPackageName));
        }
        for (auto& file : pkg->files) {
            for (auto& import : file->imports) {
                auto pkgName = GetPackageNameByImport(*import);
                if (!pkgName.empty()) {
                    q.push(impl->GetPackageInfo(pkgName));
                }
            }
        }
    }

    std::vector<Ptr<ASTLoader>> loaders;
    loaders.reserve(q.size());
    // Load common part cjo
    for (auto pkg : packages) {
        if (impl->GetGlobalOptions().IsCompilingCJMPSpecific() || !impl->GetGlobalOptions().commonPartCjos.empty()) {
            std::string expectedPackageName = pkg->fullPackageName;
            for (Ptr<ASTLoader> commonLoader : GetCommonPartCjos(expectedPackageName)) {
                commonLoader->LoadPackageDecls();
                loaders.emplace_back(commonLoader);
            }
        }
    }

    Utils::ProfileRecorder::Start("LoadPackageDeclsOnDemand", "LoadPackageDecls");
    while (!q.empty()) {
        auto cur = q.front();
        q.pop();
        if (cur == nullptr || cur->loader == nullptr) {
            continue; // If any error happens during loading 'cjo', the loader will be null.
        }
        const auto& pkgName = cur->loader->GetImportedPackageName();
        if (auto [_, success] = impl->AddLoadedPackages(pkgName); !success) {
            continue;
        }
        loaders.emplace_back(cur->loader);
        cur->loader->LoadPackageDecls();
        bool isCurMacro = cur->pkg->isMacroPackage;
        const auto& deps = cur->loader->GetDependentPackageNames();
        for (const auto& pkg : deps) {
            if (NeedCollectDependency(pkgName, isCurMacro, pkg)) {
                q.push(impl->GetPackageInfo(pkg));
            }
        }
    }
    Utils::ProfileRecorder::Stop("LoadPackageDeclsOnDemand", "LoadPackageDecls");

    Utils::ProfileRecorder::Start("LoadPackageDeclsOnDemand", "LoadRefs");
    for (auto loader : loaders) {
        loader->LoadRefs();
    }
    Utils::ProfileRecorder::Stop("LoadPackageDeclsOnDemand", "LoadRefs");

    // For LSP, packages are only used to indexing, so no need to substitute TypeAliasTy.
    if (!fromLsp) {
        // Node's ty may reference TypeAliasTy from imported package, need to substitute them to real type.
        // It must be done after all type references are loaded.
        impl->SubstituteImportedTypeAliasTy(packages);
    }
}

void CjoManager::LoadAllDeclsAndRefs() const
{
    // 'packageNameMap' also contains source package, we only need to load for imported package.
    for (auto& p : impl->GetPackageNameMap()) {
        auto& pkgInfo = p.second;
        CJC_NULLPTR_CHECK(pkgInfo);
        if (pkgInfo->loader) {
            pkgInfo->loader->LoadPackageDecls();
        }
    }
    for (auto& p : impl->GetPackageNameMap()) {
        if (p.second->loader) {
            p.second->loader->LoadRefs();
        }
    }
}

void CjoManagerImpl::ReplaceTypeAliasInNode(Ptr<Node> node)
{
    auto replaceAliasUseTy = [this](Ptr<Node> node) {
        if (node->astKind == ASTKind::TYPE_ALIAS_DECL) {
            return VisitAction::WALK_CHILDREN;
        }
        if (!node->GetTy().IsCorrect() || !node->GetTy()->HasAliasTy()) {
            return VisitAction::WALK_CHILDREN;
        }
        ModalTy key = node->GetTy();
        auto cacheIt = typeAliasCache.find(key);
        if (cacheIt != typeAliasCache.end()) {
            node->SetTy(cacheIt->second);
        } else {
            node->SetTy(typeManager.SubstituteTypeAliasInTy(key));
            typeAliasCache.emplace(key, node->GetTy());
        }
        return VisitAction::WALK_CHILDREN;
    };
    Walker(node, replaceAliasUseTy).Walk();
}

void CjoManagerImpl::SubstituteImportedTypeAliasTy(const std::vector<Ptr<Package>>& srcPackages)
{
    Utils::ProfileRecorder recorder("LoadPackageDeclsOnDemand", "SubstituteImportedTypeAliasTy");
    for (auto& pkgName2PkgInfo : GetPackageNameMap()) {
        // For cjlint tool could have more than one src-package.
        bool isSrcPackage = Utils::In(srcPackages,
            [&pkgName2PkgInfo](Ptr<Package> pkg) { return pkg->fullPackageName == pkgName2PkgInfo.first; });
        if (isSrcPackage && globalOptions.commonPartCjos.size() == 0) {
            continue;
        }
        auto& pkgInfo = pkgName2PkgInfo.second;
        for (auto& file : pkgInfo->pkg->files) {
            // For compile platform part, need only substitute type alias in common part files.
            if (isSrcPackage && !file->TestAttr(Attribute::FROM_COMMON_PART)) {
                continue;
            }
            ReplaceTypeAliasInNode(file.get());
        }
    }
    typeAliasCache.clear();
}

std::vector<OwnedPtr<ASTLoader>>& CjoManagerImpl::GetCommonPartCjos(
    std::string packageName, const CjoManager& cjoManager)
{
    if (commonPartLoaders.empty()) {
        if (globalOptions.commonPartCjos.empty()) {
            diag.DiagnoseRefactor(DiagKindRefactor::module_common_part_path_is_required, DEFAULT_POSITION);
        }
        for (auto commonPartCjo : globalOptions.commonPartCjos) {
            commonPartLoaders.emplace_back(ReadCjo(packageName, commonPartCjo, cjoManager));
        }
    }

    return commonPartLoaders;
}

OwnedPtr<ASTLoader> CjoManagerImpl::ReadCjo(
    const std::string& fullPackageName, const std::string& cjoPath, const CjoManager& cjoManager, bool printErr) const
{
    std::vector<uint8_t> buffer;
    if (auto found = cjoFileCacheMap.find(fullPackageName); found != cjoFileCacheMap.end()) {
        buffer = found->second.data;
    } else {
        std::string failedReason;
        FileUtil::ReadBinaryFileToBuffer(cjoPath, buffer, failedReason);
        if (printErr && !failedReason.empty()) {
            diag.DiagnoseRefactor(
                DiagKindRefactor::module_read_file_to_buffer_failed, DEFAULT_POSITION, cjoPath, failedReason);
            return nullptr;
        }
    }
    auto loader = MakeOwned<ASTLoader>(std::move(buffer), fullPackageName, typeManager, cjoManager, globalOptions);
    loader->SetImportSourceCode(importSrcCode);
    return loader;
}

std::unordered_set<std::string> CjoManager::LoadCachedPackage(const AST::Package& pkg, const std::string& cjoPath,
    const std::map<std::string, Ptr<AST::Decl>>& mangledName2DeclMap) const
{
    auto loader = impl->ReadCjo(pkg.fullPackageName, cjoPath, *this, false);
    if (loader == nullptr) {
        return {};
    }
    auto ret = loader->LoadCachedTypeForPackage(pkg, mangledName2DeclMap);
    for (auto& file : pkg.files) {
        impl->ReplaceTypeAliasInNode(file.get());
    }
    return ret;
}

void CjoManager::AddSourcePackage(AST::Package& pkg) const
{
    auto pkgInfo = MakeOwned<CjoManagerImpl::PackageInfo>(
        CjoManagerImpl::PackageInfo({.pkg = &pkg, .pkgDecl = MakeOwned<PackageDecl>(pkg)}));
    impl->GetPackageNameMap().emplace(pkg.fullPackageName, std::move(pkgInfo));
}

void CjoManager::AddImportedPackageFromASTNode(OwnedPtr<AST::Package>&& pkg) const
{
    auto pkgInfo = MakeOwned<CjoManagerImpl::PackageInfo>(
        CjoManagerImpl::PackageInfo({.pkg = pkg.get(), .pkgDecl = MakeOwned<PackageDecl>(*pkg)}));
    impl->GetPackageNameMap().emplace(pkg->fullPackageName, std::move(pkgInfo));
    pkg->EnableAttr(Attribute::TOOL_ADD);
    impl->AddImportedPackages(pkg);
}

void CjoManagerImpl::AddImportsToMap(
    const ImportSpec& import, const std::string& importedPackage, std::map<std::string, OrderedDeclSet>& declMap) const
{
    auto pkgInfo = GetPackageInfo(importedPackage);
    if (!pkgInfo) {
        return; // Failed to load current package.
    }
    auto importLevel = GetAccessLevel(import);
    if (import.content.kind == ImportKind::IMPORT_ALL) {
        for (auto& [name, decls] : pkgInfo->declMap) {
            auto& targetMap = declMap[name];
            Modules::AddImportedDeclToMap(decls, targetMap, importLevel);
        }
        return;
    }
    auto& decls = pkgInfo->declMap[import.content.identifier];
    if (import.content.kind == ImportKind::IMPORT_SINGLE) {
        auto& targetMap = declMap[import.content.identifier];
        Modules::AddImportedDeclToMap(decls, targetMap, importLevel);
    } else if (import.content.kind == ImportKind::IMPORT_ALIAS) {
        auto& targetMap = declMap[import.content.aliasName];
        Modules::AddImportedDeclToMap(decls, targetMap, importLevel);
    }
}

void CjoManager::AddPackageDeclMap(const std::string& fullPackageName, const std::string& importedPackage)
{
    auto pkgInfo = impl->GetPackageInfo(fullPackageName);
    // Failed to load current package or already collect the decls.
    if (!pkgInfo || impl->IsVisitedPackage(fullPackageName)) {
        return;
    }
    impl->AddVisitedPackage(fullPackageName);
    auto relation = importedPackage.empty() ? Modules::PackageRelation::CHILD
                                            : Modules::GetPackageRelation(importedPackage, fullPackageName);
    for (auto& file : pkgInfo->pkg->files) {
        // For imported package, 'file->decls' will only contains public decls.
        for (auto& decl : file->decls) {
            if (decl->astKind == ASTKind::MACRO_EXPAND_DECL) {
                continue; // Macro expand decl cannot be reference.
            }
            AddDeclToMap(*decl, pkgInfo->declMap);
        }
        for (auto& decl : file->exportedInternalDecls) {
            if (decl->TestAttr(Attribute::IMPLICIT_USED)) {
                pkgInfo->implicitDeclMap.emplace(decl->identifier, decl.get());
            }
        }
    }
    for (auto& file : pkgInfo->pkg->files) {
        for (auto& import : file->imports) {
            if (import->IsImportMulti()) {
                continue;
            }
            auto pkgName = GetPackageNameByImport(*import);
            if (pkgName.empty()) {
                continue;
            }
            AddPackageDeclMap(pkgName, fullPackageName);
            if (import->IsReExport() && Modules::IsVisible(*import, relation)) {
                impl->AddImportsToMap(*import, pkgName, pkgInfo->declMap);
            }
        }
    }
}

void CjoManager::AddGeneratedDeclToDeclMap(AST::Decl& decl) const
{
    CJC_ASSERT(decl.TestAttr(Attribute::COMPILER_ADD)); // Should be generated by the compiler.
    CJC_ASSERT(!decl.fullPackageName.empty()); // Package should be specified as it is used as a source for declaration.
    CJC_ASSERT(!decl.outerDecl); // Should be top-level declaration.

    auto pkgInfo = impl->GetPackageInfo(decl.fullPackageName);
    // Unable to get package info or package wasn't visited yet.
    // Add generated declaration only after visiting the whole package.
    if (!pkgInfo || !impl->IsVisitedPackage(decl.fullPackageName)) {
        return;
    }

    AddDeclToMap(decl, pkgInfo->declMap);
}

std::string CjoManager::GetPackageCjoPath(const std::string& fullPackageName) const
{
    if (auto found = impl->GetCjoFileCacheMap().find(fullPackageName); found != impl->GetCjoFileCacheMap().end()) {
        return fullPackageName; // Set dummy path for cached cjo data.
    }
    std::string cjoPath = "";
    if (impl->GetCjoPathFromFindCache(fullPackageName, cjoPath)) {
        return cjoPath;
    }
    cjoPath = FileUtil::FindSerializationFile(fullPackageName, SERIALIZED_FILE_EXTENSION, GetSearchPath());
    impl->CacheCjoPathForFind(fullPackageName, cjoPath);
    return cjoPath;
}

std::pair<std::string, std::string> CjoManager::GetPackageCjo(const AST::ImportSpec& importSpec) const
{
    std::string cjoPath;
    std::string cjoName;
    for (auto it : GetPossibleCjoNames(importSpec)) {
        cjoName = it;
        if (auto found = impl->GetCjoFileCacheMap().find(FileUtil::ToPackageName(cjoName));
            found != impl->GetCjoFileCacheMap().end()) {
            cjoPath = cjoName; // Set dummy path for cached cjo data.
        } else {
            cjoPath = FileUtil::FindSerializationFile(
                FileUtil::ToPackageName(cjoName), SERIALIZED_FILE_EXTENSION, GetSearchPath());
        }
        if (!cjoPath.empty()) {
            break;
        }
    }
    CJC_ASSERT(!cjoName.empty());
    auto cjoPackageName = FileUtil::ToPackageName(cjoName);
    // Store importSpec with packageName.
    std::string possibleName = importSpec.content.GetImportedPackageName();
    impl->AddImportedPackageName(&importSpec,
        std::make_pair(
            cjoPackageName, cjoPackageName == possibleName && importSpec.content.kind != ImportKind::IMPORT_ALL));
    return {cjoPackageName, cjoPath};
}

std::vector<std::string> CjoManager::GetPossibleCjoNames(const ImportSpec& import) const
{
    // Multi-imports are desugared after parser which should not be used for get package name.
    CJC_ASSERT(import.content.kind != ImportKind::IMPORT_MULTI);
    if (auto it = GetPackageNameByImport(import); !it.empty()) {
        return {FileUtil::ToCjoFileName(it)};
    }
    std::vector<std::string> possibleNames;
    for (auto& name : import.content.GetPossiblePackageNames()) {
        possibleNames.emplace_back(FileUtil::ToCjoFileName(name));
    }
    return possibleNames;
}

std::string CjoManager::GetPackageNameByImport(const AST::ImportSpec& importSpec) const
{
    return impl->GetPackageNameByImport(importSpec);
}

bool CjoManager::IsImportPackage(const AST::ImportSpec& importSpec) const
{
    return impl->IsImportPackage(importSpec);
}

void CjoManager::RemoveImportedPackageNames(const std::vector<OwnedPtr<AST::ImportSpec>>& imports) const
{
    impl->RemoveImportedPackageNames(imports);
}

std::vector<Ptr<AST::PackageDecl>> CjoManager::GetAllPackageDecls(bool includeMacroPkg) const
{
    std::vector<Ptr<AST::PackageDecl>> ret;
    for (auto& p : impl->GetPackageNameMap()) {
        auto& pkgInfo = p.second;
        CJC_ASSERT(pkgInfo && pkgInfo->pkgDecl);
        // Temporarily contains source package to keep same implementation as before.
        if (!pkgInfo->pkg->TestAttr(Attribute::IMPORTED) || includeMacroPkg ||
            (!pkgInfo->onlyUsedByMacro && !pkgInfo->pkg->isMacroPackage)) {
            ret.emplace_back(pkgInfo->pkgDecl.get());
        }
    }
    return ret;
}

void CjoManager::RemovePackage(const std::string& fullPkgName, const Ptr<Package> package) const
{
    impl->RemoveImportedPackages(package);
    if (auto found = impl->GetPackageNameMap().find(fullPkgName); found != impl->GetPackageNameMap().end()) {
        delete found->second->loader.get();
        found->second->loader = nullptr;
        impl->GetPackageNameMap().erase(fullPkgName);
    }
}

void CjoManagerImpl::ClearForReBuildIndex()
{
    // Delete all AST loaders first
    for (auto& p : packageNameMap) {
        delete p.second->loader.get();
        p.second->loader = nullptr;
    }
    // Clear all package-related maps and caches
    packageNameMap.clear();
    importedPackageNameMap.clear();
    visitedPkgs.clear();
    loadedPackages.clear();
    importedPackages.clear();
    // Reset BuildIndex flag
    hasBuildIndex = false;
    // Don't clear cjoFileCacheMap and cjoPathCache, because they are used for rebuild index.
}
} // namespace Cangjie
