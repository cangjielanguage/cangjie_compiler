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
#include <unordered_set>

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
    auto AddLoadedPackages(const std::string& pkgName)
    {
        return loadedPackages.emplace(pkgName);
    }
    bool AlreadyLoaded(std::string pkgName)
    {
        return loadedPackages.find(pkgName) != loadedPackages.end();
    }
    bool IsReExportBy(const std::string& srcPackage, const std::string& reExportPackage) const;
    void AddImportedPackageName(Ptr<const AST::ImportSpec> importSpec, std::pair<std::string, bool> pkgNamePair)
    {
        importedPackageNameMap.emplace(importSpec, pkgNamePair);
    }
    void RemoveImportedPackageNames(const std::vector<OwnedPtr<AST::ImportSpec>>& imports)
    {
        for (auto& import : imports) {
            (void)importedPackageNameMap.erase(import.get());
        }
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
    void SetPackageCjoCache(
        const std::string& fullPackageName, const std::vector<uint8_t>& cjoData, CjoManager::CjoChangeState changeState)
    {
        if (fullPackageName.empty() || cjoData.empty()) {
            return;
        }
        cjoFileCacheMap[fullPackageName] = {
            .state = changeState,
            .data = cjoData,
        };
    }
    struct CjoCacheInfo {
        CjoManager::CjoChangeState state{CjoManager::CjoChangeState::UNCHANGED};
        std::vector<uint8_t> data;
    };
    using CjoCacheMap = std::unordered_map<std::string, CjoCacheInfo>;
    CjoCacheMap& GetCjoFileCacheMap()
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
    void ClearForReBuildIndex();
    bool HasBuildIndex() const
    {
        return hasBuildIndex;
    }
    void SetHasBuildIndex(bool value)
    {
        hasBuildIndex = value;
    }
    std::vector<OwnedPtr<ASTLoader>>& GetCommonPartCjos(std::string expectedName, const CjoManager& cjoManager);
    const GlobalOptions& GetGlobalOptions()
    {
        return globalOptions;
    }
    /**
     * @brief Get the Cjo Path From Cache
     *
     * @param cjoName
     * @param cjoPath
     * @return true if found
     * @return false if not found
     */
    bool GetCjoPathFromFindCache(const std::string& cjoName, std::string& cjoPath) const
    {
        auto found = cjoPathFindCache.find(cjoName);
        if (found == cjoPathFindCache.end()) {
            return false;
        }
        cjoPath = found->second;
        return true;
    }
    void CacheCjoPathForFind(const std::string& cjoName, const std::string& cjoPath)
    {
        cjoPathFindCache[cjoName] = cjoPath;
    }

    /**
     * @brief Substitute imported TypeAliasTy in all loaded packages.
     * @param srcPackages The source packages where imported TypeAliasTy come from.
     */
    void SubstituteImportedTypeAliasTy(const std::vector<Ptr<AST::Package>>& srcPackages);

    /**
     * @brief Replace TypeAliasTy in a node.
     * @param node The node to replace TypeAliasTy.
     */
    void ReplaceTypeAliasInNode(Ptr<AST::Node> node);

private:
    DiagnosticEngine& diag;
    TypeManager& typeManager;
    const GlobalOptions& globalOptions;
    bool& importSrcCode;
    std::vector<std::string> searchPath;
    /** Only used to hold ownership of imported packages. */
    std::vector<OwnedPtr<AST::Package>> importedPackages;
    std::unordered_map<std::string, OwnedPtr<CjoManagerImpl::PackageInfo>> packageNameMap;
    CjoCacheMap cjoFileCacheMap;
    std::unordered_map<Ptr<const AST::ImportSpec>, std::pair<std::string, bool>> importedPackageNameMap;
    // Searching cache.
    std::unordered_set<std::string> visitedPkgs;

    // Indirectly imported packages which have been used is recorded in loader. Load their decls on demand.
    std::unordered_set<std::string> loadedPackages;
    // common part loader also stored in `packageNameMap`.
    std::vector<OwnedPtr<ASTLoader>> commonPartLoaders;
    bool canInline{false};

    // cache cjo file path result for skip FindSerializationFile call, key is possible cjo name without extension, value
    // is cjo path (empty string means not found).
    std::unordered_map<std::string, std::string> cjoPathFindCache;
    // Flag to track if BuildIndex has been called
    bool hasBuildIndex{false};
    // Cache for substituted type alias to real type.
    std::unordered_map<AST::ModalTy, AST::ModalTy> typeAliasCache;
};
} // namespace Cangjie
#endif
