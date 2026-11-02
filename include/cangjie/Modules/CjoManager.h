// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the cjo file related classes.
 */

#ifndef CANGJIE_MODULES_CJO_MANAGER_H
#define CANGJIE_MODULES_CJO_MANAGER_H

#include "cangjie/AST/Node.h"
#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Sema/TypeManager.h"

namespace Cangjie {
class ASTLoader;

class CjoManager {
public:
    struct Config {
        DiagnosticEngine& diag;
        TypeManager& typeManager;
        const GlobalOptions& globalOptions;
        bool& importSrcCode;
    };
    explicit CjoManager(const Config& config);
    ~CjoManager();

    void AddSourcePackage(AST::Package& pkg) const;
    void AddImportedPackageFromASTNode(OwnedPtr<AST::Package>&& pkg) const;
    bool LoadPackageHeader(const std::string& fullPackageName, const std::string& cjoPath) const;
    void LoadAllDeclsAndRefs() const;
    bool NeedCollectDependency(const std::string& curName, bool isCurMacro, const std::string& depName) const;
    /**
     * Loads the declaration of each package in packages on demand.
     * If @p fromLsp is false, only the dependent packages of each package in @p packages are loaded.
     * Otherwise, the packages in @p packages are also loaded.
     */
    void LoadPackageDeclsOnDemand(const std::vector<Ptr<AST::Package>>& packages, bool fromLsp = false) const;
    /**
     * Collect visible package of current 'fullPackageName'
     * @param importedPackage the package which imports 'fullPackageName'. Empty for source package.
     */
    void AddPackageDeclMap(const std::string& fullPackageName, const std::string& importedPackage = "");
    /**
     * Add generated declaration 'decl' to the declMap making 'decl' visible in the package where 'decl' is declared.
     * 'decl' visibility within other packages should work with default mechanisms after loading from cjo.
     * Should be called only after building index.
     */
    void AddGeneratedDeclToDeclMap(AST::Decl& decl) const;
    /** For loading cached types during incremental compilation. */
    std::unordered_set<std::string> LoadCachedPackage(const AST::Package& pkg,
        const std::string& cjoPath, const std::map<std::string, Ptr<AST::Decl>>& mangledName2DeclMap) const;
    /** For --scan-dependency of cjo. */
    std::string GetPackageDepInfo(const std::string& cjoPath) const;

    Ptr<AST::PackageDecl> GetPackageDecl(const std::string& fullPackageName) const;
    /* Load files from common part to current package.
     * This is required to correctly handle imports from common part.
     */
    bool LoadFilesOfCommonPart(Ptr<AST::Package> pkg);
    std::vector<OwnedPtr<ASTLoader>>& GetCommonPartCjos(std::string expectedName) const;
    Ptr<AST::Package> GetPackage(const std::string& fullPackageName) const;
    std::vector<Ptr<AST::PackageDecl>> GetAllPackageDecls(bool includeMacroPkg = false) const;

    void RemovePackage(const std::string& fullPkgName, const Ptr<AST::Package> package) const;

    const std::map<std::string, AST::OrderedDeclSet>& GetPackageMembers(
        const std::string& fullPackageName) const;
    const AST::OrderedDeclSet& GetPackageMembersByName(
        const std::string& fullPackageName, const std::string& name) const;
    const AST::OrderedDeclSet& GetImplicitPackageMembersByName(
        const std::string& fullPackageName, const std::string& name) const;

    std::string GetPackageCjoPath(const std::string& fullPackageName) const;
    /** return {fullPackageName, cjoPath} */
    std::pair<std::string, std::string> GetPackageCjo(const AST::ImportSpec& importSpec) const;
    std::vector<std::string> GetFullPackageNames(const AST::ImportSpec& import) const;
    // for single import "import a.b.c", the possible imported cjo's are a.b and a.b.c
    // for other import specs, the possible name is unique.
    std::vector<std::string> GetPossibleCjoNames(const AST::ImportSpec& import) const;
    std::string GetPackageNameByImport(const AST::ImportSpec& importSpec) const;

    bool IsImportPackage(const AST::ImportSpec& importSpec) const;
    /**
     * @brief Remove import-to-package mappings for the given import specs.
     * @param imports Import specs to remove from importedPackageNameMap.
     *
     * Used when a source file is replaced during macro debug, before re-resolving its imports.
     * Freed ImportSpec pointers may be reused; stale entries would make GetPackageNameByImport
     * return an incorrect package name.
     */
    void RemoveImportedPackageNames(const std::vector<OwnedPtr<AST::ImportSpec>>& imports) const;

    bool IsOnlyUsedByMacro(const std::string& fullPackageName) const;
    void SetOnlyUsedByMacro(const std::string& fullPackageName, bool onlyUsedByMacro) const;
    bool IsMacroRelatedPackageName(const std::string& fullPackageName) const;

    void UpdateSearchPath(const std::string& cangjieModules) const;

    const std::vector<std::string>& GetSearchPath() const;

    bool GetCanInline() const;

    enum class CjoChangeState : uint8_t {
        UNCHANGED = 0,
        CHANGED = 1,
        ADDED = 2,
        DELETED = 3,
    };
    /**
     * For LSP, set cached cjo data @p cjoData and optional corresponding sha256 digest @p encrypt
     * for @param fullPackageName and @p changeState.
     */
    void SetPackageCjoCache(
        const std::string& fullPackageName,
        const std::vector<uint8_t>& cjoData,
        CjoChangeState changeState = CjoChangeState::ADDED) const;

    void ClearCjoCache() const;
    void DeleteASTLoaders() const noexcept;

    void ClearVisitedPkgs() const;
    void ClearForReBuildIndex() const;
    bool HasBuildIndex() const;
    void SetHasBuildIndex(bool value) const;
    DiagnosticEngine& GetDiag() const;
    Ptr<std::unordered_map<std::string, Ptr<AST::Decl>>> GetExportIdDeclMap(const std::string& fullPackageName) const;

private:
    class CjoManagerImpl* impl;
};
} // namespace Cangjie
#endif
