// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares class CjoManagerImpl.
 */

#ifndef CANGJIE_MODULES_CJO_MANAGERIMPL_H
#define CANGJIE_MODULES_CJO_MANAGERIMPL_H

#include "cangjie/Modules/CjoManager.h"

namespace Cangjie {
class CjoManagerImpl {
public:
    explicit CjoManagerImpl(const CjoManager::Config& config);
    struct PackageInfo {
        Ptr<ASTLoader> loader;
        Ptr<AST::Package> pkg;
        OwnedPtr<AST::PackageDecl> pkgDecl;
        std::unordered_map<std::string, Ptr<AST::Decl>> exportIDDeclMap;
        std::map<std::string, AST::OrderedDeclSet> declMap;
        std::map<std::string, Ptr<AST::Decl>> implicitDeclMap;
        std::string cjoPath;
        bool onlyUsedByMacro{false};
    };
    std::unordered_map<std::string, OwnedPtr<CjoManagerImpl::PackageInfo>>& GetPackageNameMap()
    {
        return packageNameMap;
    }
    Ptr<CjoManagerImpl::PackageInfo> GetPackageInfo(const std::string& fullPackageName) const;
    DiagnosticEngine& GetDiag()
    {
        return diag;
    }
    Ptr<std::unordered_map<std::string, Ptr<AST::Decl>>> GetExportIdDeclMap(const std::string& fullPackageName) const;
    bool GetCanInline() const
    {
        return canInline;
    }
    OwnedPtr<ASTLoader> ReadCjo(const std::string& fullPackageName, const std::string& cjoPath,
        const CjoManager& cjoManager, bool printErr = true) const;
    void AddImportedPackages(OwnedPtr<AST::Package>& pkg)
    {
        importedPackages.emplace_back(std::move(pkg));
    }
    void RemoveImportedPackages(const Ptr<AST::Package> pkg)
    {
        for (auto it = importedPackages.cbegin(); it != importedPackages.cend(); ++it) {
            if (it->get() == pkg) {
                it = importedPackages.erase(it);
                return;
            }
        }
    }
    auto AddLoadedPackages(std::string pkgName)
    {
        return loadedPackages.emplace(pkgName);
    }
    bool AlreadyLoaded(std::string pkgName)
    {
        return std::find(loadedPackages.begin(), loadedPackages.end(), pkgName) != loadedPackages.end();
    }
    bool IsReExportBy(const std::string& srcPackage, const std::string& reExportPackage) const;
    void AddImportedPackageName(Ptr<const AST::ImportSpec> importSpec, std::pair<std::string, bool> pkgNamePair)
    {
        importedPackageNameMap.emplace(importSpec, pkgNamePair);
    }
    std::string GetPackageNameByImport(const AST::ImportSpec& importSpec) const
    {
        auto found = importedPackageNameMap.find(&importSpec);
        return found == importedPackageNameMap.end() ? "" : found->second.first;
    }
    bool IsImportPackage(const AST::ImportSpec& importSpec) const
    {
        auto found = importedPackageNameMap.find(&importSpec);
        return found == importedPackageNameMap.end() ? false : found->second.second;
    }
    void UpdateSearchPath(const std::string& cangjieModules)
    {
        searchPath.clear();
        searchPath.insert(searchPath.end(), globalOptions.importPaths.cbegin(), globalOptions.importPaths.cend());
        searchPath.emplace_back(".");
        searchPath.insert(searchPath.end(), globalOptions.environment.cangjiePaths.cbegin(),
            globalOptions.environment.cangjiePaths.cend());
        searchPath.emplace_back(cangjieModules);
    }
    const std::vector<std::string>& GetSearchPath() const
    {
        return searchPath;
    }
    void SetPackageCjoCache(const std::string& fullPackageName, const std::vector<uint8_t>& cjoData)
    {
        if (fullPackageName.empty() || cjoData.empty()) {
            return;
        }
        cjoFileCacheMap[fullPackageName] = cjoData;
    }
    std::unordered_map<std::string, std::vector<uint8_t>>& GetCjoFileCacheMap()
    {
        return cjoFileCacheMap;
    }
    bool IsVisitedPackage(const std::string& fullPackageName)
    {
        return visitedPkgs.count(fullPackageName) != 0;
    }
    void AddVisitedPackage(const std::string& fullPackageName)
    {
        visitedPkgs.emplace(fullPackageName);
    }
    void AddImportsToMap(const AST::ImportSpec& import, const std::string& importedPackage,
        std::map<std::string, AST::OrderedDeclSet>& declMap) const;
    void ClearCjoCache()
    {
        cjoFileCacheMap.clear();
    }
    void ClearVisitedPkgs()
    {
        visitedPkgs.clear();
    }
    std::optional<std::vector<std::string>> PreReadCommonPartCjoFiles(CjoManager& cjoManager);
    Ptr<ASTLoader> GetCommonPartCjo(std::string expectedName);
    const GlobalOptions& GetGlobalOptions()
    {
        return globalOptions;
    }

private:
    DiagnosticEngine& diag;
    TypeManager& typeManager;
    const GlobalOptions& globalOptions;
    bool& importSrcCode;
    std::vector<std::string> searchPath;
    /** Only used to hold ownership of imported packages. */
    std::vector<OwnedPtr<AST::Package>> importedPackages;
    std::unordered_map<std::string, OwnedPtr<CjoManagerImpl::PackageInfo>> packageNameMap;
    std::unordered_map<std::string, std::vector<uint8_t>> cjoFileCacheMap;
    std::unordered_map<Ptr<const AST::ImportSpec>, std::pair<std::string, bool>> importedPackageNameMap;
    // Searching cache.
    std::unordered_set<std::string> visitedPkgs;

    // Indirectly imported packages which have been used is recorded in loader. Load their decls on demand.
    std::unordered_set<std::string> loadedPackages;
    // common part loader also stored in `packageNameMap`.
    OwnedPtr<ASTLoader> commonPartLoader;
    bool canInline{false};
};
} // namespace Cangjie
#endif
