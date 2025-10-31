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

void CjoManager::SetPackageCjoCache(const std::string& fullPackageName, const std::vector<uint8_t>& cjoData) const
{
    impl->SetPackageCjoCache(fullPackageName, cjoData);
}

void CjoManager::ClearCjoCache() const
{
    impl->ClearCjoCache();
}

void CjoManager::ClearVisitedPkgs() const
{
    impl->ClearVisitedPkgs();
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

std::optional<std::vector<std::string>> CjoManager::PreReadCommonPartCjoFiles()
{
    return impl->PreReadCommonPartCjoFiles(*this);
}

Ptr<ASTLoader> CjoManager::GetCommonPartCjo(std::string expectedName) const
{
    return impl->GetCommonPartCjo(expectedName);
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

bool CjoManager::NeedCollectDependency(std::string curName, bool isCurMacro, std::string depName) const
{
    if (depName == curName || impl->AlreadyLoaded(depName)) {
        return false;
    }

    auto pkgInfo = impl->GetPackageInfo(depName);
    if (!pkgInfo) {
        // This common dependency was not imported in current platform part, so it need to be add manually
        auto cjoPath = GetPackageCjoPath(depName);
        if (!cjoPath || !LoadPackageHeader(depName, *cjoPath)) {
            DiagnosticBuilder builder = GetDiag().DiagnoseRefactor(DiagKindRefactor::package_search_error,
                DEFAULT_POSITION, depName);
            builder.AddHelp(DiagHelp(Modules::NO_CJO_HELP_INFO));
            return false;
        }
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

void CjoManager::LoadFilesOfCommonPart(Ptr<Package> pkg)
{
    if (!impl->GetGlobalOptions().IsCompilingCJMP()) {
        return;
    }
    auto commonLoader = GetCommonPartCjo(pkg->fullPackageName);
    if (!commonLoader) {
        return;
    }
    commonLoader->PreloadCommonPartOfPackage(*pkg);
}

void CjoManager::LoadPackageDeclsOnDemand(const std::vector<Ptr<Package>>& packages, bool fromLsp) const
{
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
    // Load common part cjo
    for (auto pkg : packages) {
        if (impl->GetGlobalOptions().IsCompilingCJMP()) {
            std::string expectedPackageName = pkg->fullPackageName;
            auto commonLoader = GetCommonPartCjo(expectedPackageName);
            if (!commonLoader) {
                continue;
            }
            commonLoader->LoadPackageDecls();
            loaders.emplace_back(commonLoader);
        }
    }

    while (!q.empty()) {
        auto cur = q.front();
        q.pop();
        if (cur == nullptr || cur->loader == nullptr) {
            continue; // If any error happens during loading 'cjo', the loader will be null.
        }
        auto pkgName = cur->loader->GetImportedPackageName();
        if (auto [_, success] = impl->AddLoadedPackages(pkgName); !success) {
            continue;
        }
        loaders.emplace_back(cur->loader);
        cur->loader->LoadPackageDecls();
        bool isCurMacro = cur->pkg->isMacroPackage;
        auto deps = cur->loader->GetDependentPackageNames();
        for (auto pkg : deps) {
            if (NeedCollectDependency(pkgName, isCurMacro, pkg)) {
                q.push(impl->GetPackageInfo(pkg));
            }
        }
    }

    for (auto loader : loaders) {
        loader->LoadRefs();
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

// Reading common part .cjo is required before parsing to keep fileID stable.
// This method only reads file content and does not build ast nodes.
std::optional<std::vector<std::string>> CjoManagerImpl::PreReadCommonPartCjoFiles(CjoManager& cjoManager)
{
    // use `cjoFileCacheMap`
    std::vector<uint8_t> buffer;
    std::string failedReason;

    if (!globalOptions.commonPartCjo) {
        diag.DiagnoseRefactor(DiagKindRefactor::module_common_part_path_is_required, DEFAULT_POSITION);
        return std::nullopt;
    }

    CJC_ASSERT(globalOptions.commonPartCjo);
    std::string commonPartCjoPath = *globalOptions.commonPartCjo;
    FileUtil::ReadBinaryFileToBuffer(commonPartCjoPath, buffer, failedReason);
    if (!failedReason.empty()) {
        diag.DiagnoseRefactor(
            DiagKindRefactor::module_read_file_to_buffer_failed, DEFAULT_POSITION, commonPartCjoPath, failedReason);
        return {};
    }

    // name of package is unknown before parsing and reading .cjo, so fake is used.
    std::string fakeName = "";
    commonPartLoader = MakeOwned<ASTLoader>(std::move(buffer), fakeName, typeManager, cjoManager, globalOptions);
    commonPartLoader->SetImportSourceCode(importSrcCode);
    commonPartLoader->PreReadAndSetPackageName();

    return commonPartLoader->ReadFileNames();
}

Ptr<ASTLoader> CjoManagerImpl::GetCommonPartCjo(std::string expectedName)
{
    CJC_ASSERT(commonPartLoader);
    CJC_ASSERT(globalOptions.commonPartCjo);

    std::string realName = commonPartLoader->PreReadAndSetPackageName();
    if (realName != expectedName) {
        diag.DiagnoseRefactor(
            DiagKindRefactor::module_common_cjo_wrong_package, DEFAULT_POSITION, realName, expectedName);
        return nullptr;
    }

    return commonPartLoader.get();
}

OwnedPtr<ASTLoader> CjoManagerImpl::ReadCjo(
    const std::string& fullPackageName, const std::string& cjoPath, const CjoManager& cjoManager, bool printErr) const
{
    std::vector<uint8_t> buffer;
    if (auto found = cjoFileCacheMap.find(fullPackageName); found != cjoFileCacheMap.end()) {
        buffer = found->second;
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
    return loader->LoadCachedTypeForPackage(pkg, mangledName2DeclMap);
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

std::optional<std::string> CjoManager::GetPackageCjoPath(std::string fullPackageName) const
{
    if (auto found = impl->GetCjoFileCacheMap().find(fullPackageName); found != impl->GetCjoFileCacheMap().end()) {
        return fullPackageName; // Set dummy path for cached cjo data.
    } else {
        return FileUtil::FindSerializationFile(fullPackageName, SERIALIZED_FILE_EXTENSION, GetSearchPath());
    }

    return std::nullopt;
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
            cjoPath = FileUtil::FindSerializationFile(FileUtil::ToPackageName(cjoName),
                SERIALIZED_FILE_EXTENSION, GetSearchPath());
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
    if (import.content.prefixPaths.empty()) {
        return {import.content.identifier};
    }
    std::string name;
    std::string_view dot = TOKENS[static_cast<int>(TokenKind::DOT)];
    bool needDc{import.content.hasDoubleColon};
    for (size_t i{needDc ? 1UL : 0UL}; i < import.content.prefixPaths.size(); ++i) {
        name += import.content.prefixPaths[i];
        if (i != import.content.prefixPaths.size() - 1) {
            name += dot;
        }
        needDc = false;
    }
    auto appendOrg = [&import](const std::string& name) {
        if (import.content.hasDoubleColon) {
            return name + std::string{ORG_NAME_SEPARATOR} + import.content.prefixPaths[0];
        }
        return name;
    };
    if (import.content.kind == ImportKind::IMPORT_ALL) {
        return {appendOrg(name)};
    }
    if (auto it = GetPackageNameByImport(import); !it.empty()) {
        return {FileUtil::ToCjoFileName(it)};
    }
    // if needDc, this import must be of from a::b
    // in this case, the only possible pacakge name is a::b
    if (needDc) {
        return {appendOrg(import.content.identifier)};
    }
    auto maybePackageName = name + std::string{dot} + import.content.identifier.Val();
    return {appendOrg(maybePackageName), appendOrg(name)};
}

std::string CjoManager::GetPackageNameByImport(const AST::ImportSpec& importSpec) const
{
    return impl->GetPackageNameByImport(importSpec);
}

bool CjoManager::IsImportPackage(const AST::ImportSpec& importSpec) const
{
    return impl->IsImportPackage(importSpec);
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
} // namespace Cangjie
