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

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
const uint8_t CJO_MAJOR_VERSION = 0;
const uint8_t CJO_MINOR_VERSION = 1;
const uint8_t CJO_PATCH_VERSION = 0;
#endif

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
    bool NeedCollectDependency(std::string curName, bool isCurMacro, std::string depName) const;
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
    /** For loading cached types during incremental compilation. */
    std::unordered_set<std::string> LoadCachedPackage(const AST::Package& pkg,
        const std::string& cjoPath, const std::map<std::string, Ptr<AST::Decl>>& mangledName2DeclMap) const;
    /** For --scan-dependency of cjo. */
    std::string GetPackageDepInfo(const std::string& cjoPath) const;

    Ptr<AST::PackageDecl> GetPackageDecl(const std::string& fullPackageName) const;
    std::optional<std::vector<std::string>> PreReadCommonPartCjoFiles();
    Ptr<ASTLoader> GetCommonPartCjo(std::string expectedName) const;
    Ptr<AST::Package> GetPackage(const std::string& fullPackageName) const;
    std::vector<Ptr<AST::PackageDecl>> GetAllPackageDecls(bool includeMacroPkg = false) const;

    void RemovePackage(const std::string& fullPkgName, const Ptr<AST::Package> package) const;

    const std::map<std::string, AST::OrderedDeclSet>& GetPackageMembers(
        const std::string& fullPackageName) const;
    const AST::OrderedDeclSet& GetPackageMembersByName(
        const std::string& fullPackageName, const std::string& name) const;
    Ptr<AST::Decl> GetImplicitPackageMembersByName(const std::string& fullPackageName, const std::string& name) const;

    std::optional<std::string> GetPackageCjoPath(std::string fullPackageName) const;
    /** return {fullPackageName, cjoPath} */
    std::pair<std::string, std::string> GetPackageCjo(const AST::ImportSpec& importSpec) const;
    std::vector<std::string> GetFullPackageNames(const AST::ImportSpec& import) const;
    // for single import "import a.b.c", the possible imported cjo's are a.b and a.b.c
    // for other import specs, the possible name is unique.
    std::vector<std::string> GetPossibleCjoNames(const AST::ImportSpec& import) const;
    std::string GetPackageNameByImport(const AST::ImportSpec& importSpec) const;

    bool IsImportPackage(const AST::ImportSpec& importSpec) const;

    bool IsOnlyUsedByMacro(const std::string& fullPackageName) const;
    void SetOnlyUsedByMacro(const std::string& fullPackageName, bool onlyUsedByMacro) const;
    bool IsMacroRelatedPackageName(const std::string& fullPackageName) const;

    void UpdateSearchPath(const std::string& cangjieModules) const;

    const std::vector<std::string>& GetSearchPath() const;

    bool GetCanInline() const;

    /**
     * For LSP, set cached cjo data @p cjoData and optional corresponding sha256 digest @p encrypt
     * for @param fullPackageName .
     */
    void SetPackageCjoCache(const std::string& fullPackageName, const std::vector<uint8_t>& cjoData) const;

    void ClearCjoCache() const;
    void DeleteASTLoaders() const noexcept;

    void ClearVisitedPkgs() const;
    DiagnosticEngine& GetDiag() const;
    Ptr<std::unordered_map<std::string, Ptr<AST::Decl>>> GetExportIdDeclMap(const std::string& fullPackageName) const;

private:
    class CjoManagerImpl* impl;
};
} // namespace Cangjie
#endif
