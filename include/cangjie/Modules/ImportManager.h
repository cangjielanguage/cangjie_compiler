// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the ImportManager related classes, which provides import capabilities.
 */

#ifndef CANGJIE_MODULES_IMPORTMANAGER_H
#define CANGJIE_MODULES_IMPORTMANAGER_H

#include <cstdint>
#include <memory>
#include <queue>
#include <set>
#include <unordered_map>

#include "cangjie/AST/ASTCasting.h"
#include "cangjie/AST/Node.h"
#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/CHIR/Interpreter/BCHIR.h"
#include "cangjie/Modules/CjoManager.h"
#include "cangjie/Utils/CheckUtils.h"

#include "CjoManager.h"

namespace Cangjie {
class DefaultCompilerInstance;
class TypeManager;
class ASTLoader;
class ASTWriter;
class CjoManager;

struct ExternalLibCfg {
    std::string name;
    std::string path;
    std::unordered_set<std::string> exports;
};

enum class DepType { DIRECT, INDIRECT, BOTH };

/**
 * ImportManager is used to manage all imported packages. It can do these things:
 * 1. Load a saved package to a @see PackageDecl node using deserialization method.
 * 2. Export external decls in current package AST using serialization method.
 * 3. Manage all imported packages.
 * 4. Merge ASTs from source files into one Package AST and generate file-realImports Map based on source file asts.
 * 5. Provide methods for decl lookup in sema.
 */
class ImportManager {
public:
    explicit ImportManager(DiagnosticEngine& d, TypeManager& typeManager, const GlobalOptions& opts)
        : diag(d), opts(opts), typeManager(typeManager)
    {
        cjoManager = MakeOwned<CjoManager>(CjoManager::Config{diag, typeManager, opts, importSrcCode});
        CJC_NULLPTR_CHECK(cjoManager);
        dependencyGraph = MakeOwned<DependencyGraph>(*cjoManager);
        CJC_NULLPTR_CHECK(dependencyGraph);
    }

    ~ImportManager();

    /**
     * Export external decls of a Package AST to a buffer.
     */
    void ExportAST(
        bool saveFileWithAbsPath, std::vector<uint8_t>& astData, const AST::Package& pkg,
        const std::function<void(ASTWriter&)> additionalSerializations = [](ASTWriter&) {});
    /**
     * Export all decls signature of a Package AST to a buffer.
     */
    std::vector<uint8_t> ExportASTSignature(const AST::Package& pkg);

    /**
     * Pre-save full exporting decls after sema's desugar before generic instantiation.
     * NOTE: avoid export boxed decl creation.
     */
    void ExportDeclsWithContent(bool saveFileWithAbsPath, AST::Package& package);

    /**
     * Get all imported package decls.
     * @param includedMacroPkg indicates whether macro packages should be collected.
     */
    std::vector<Ptr<AST::PackageDecl>> GetAllImportedPackages(bool includeMacroPkg = false) const;

    /**
     * Get all imported packageDecls of current node,
     * LSP will call it to get all imported packages in @param fullPackageName.
     */
    const std::vector<Ptr<AST::PackageDecl>>& GetCurImportedPackages(const std::string& fullPackageName) const;

    /** This should not be used for source package. */
    const AST::OrderedDeclSet& GetPackageMembersByName(const AST::Package& package, const std::string& name) const;
    /** Get accessible 'targetPackage' decls from 'srcPackage'. NOTE: used by LSP */
    AST::OrderedDeclSet GetPackageMembers(
        const std::string& srcFullPackageName, const std::string& targetFullPackageName) const;
    std::vector<std::pair<std::string, std::vector<Ptr<AST::Decl>>>> GetImportedDecls(const AST::File& file) const;

    /**
     * Get all accessible decls by @param file and @param name, and return decls.
     */
    std::vector<Ptr<AST::Decl>> GetImportedDeclsByName(const AST::File& file, const std::string& name) const;

    /**
     * Get possible packageDecl and status of whether a conflict occurs by a node and qualified name.
     */
    std::pair<Ptr<AST::PackageDecl>, bool> GetImportedPackageDecl(
        Ptr<const AST::Node> node, const std::string& packageName) const;

    /**
     * Get all direct and indirect imported packageDecls of a package whose fullPackageName is @param fullPackageName
     */
    const std::vector<Ptr<AST::PackageDecl>>& GetAllDependentPackageDecls(
        const std::string& fullPackageName, bool includeMacroPkg = false) const;

    /**
     * Get direct and indirect imported package names by @param fullPkgName.
     * NOTE: exclude packages which is only used by macro expansion.
     */
    const std::set<std::string>& GetAllDependentPackageNames(const std::string& fullPackageName) const;

    /**
     * Get information of dependent packages of @param packages in json format.
     */
    std::vector<std::string> GeneratePkgDepInfo(const std::vector<Ptr<AST::Package>>& packages) const;

    std::vector<std::string> GeneratePkgDepInfoByCjo(const std::string& cjoPath);

    /**
     * Get direct imported package names by @param package.
     * NOTE: Can be used after parser step. used by LSP.
     */
    std::set<std::string> CollectDirectDepPkg(const AST::Package& package) const;

    /**
     * Build map index for importmanager.
     */
    bool BuildIndex(const std::string& cangjieModules, const GlobalOptions& globalOptions,
        std::vector<Ptr<AST::Package>>& packages);

    /**
     * Load package from package name and cjo path.
     * Note: The function is used by lsp.
     */
    Ptr<AST::Package> LoadPackageFromCjo(const std::string& fullPkgName, const std::string& cjoPath) const;

    Ptr<AST::Decl> GetImportedDecl(const std::string& fullPackageName, const std::string& name) const;
    template <typename T> T* GetImportedDecl(const std::string& fullPackageName, const std::string& name) const
    {
        Ptr<AST::Decl> decl = GetImportedDecl(fullPackageName, name);
        return DynamicCast<T*>(decl);
    }
    Ptr<AST::Decl> GetAstDecl(const std::string& name) const
    {
        return GetImportedDecl(AST_PACKAGE_NAME, name);
    }
    template <typename T> T* GetAstDecl(const std::string& name) const
    {
        auto decl = GetAstDecl(name);
        return DynamicCast<T*>(decl);
    }
    Ptr<AST::Decl> GetCoreDecl(const std::string& name) const
    {
        return GetImportedDecl(CORE_PACKAGE_NAME, name);
    }
    template <typename T> T* GetCoreDecl(const std::string& name) const
    {
        Ptr<AST::Decl> decl = GetCoreDecl(name);
        return DynamicCast<T*>(decl);
    }
    Ptr<AST::Decl> GetSyncDecl(const std::string& name) const
    {
        return GetImportedDecl(SYNC_PACKAGE_NAME, name);
    }

    /**
     * Resolve imports for 'curPackage'
     */
    void ResolveImports(const AST::Package& pkg);

    std::unordered_set<std::string> GetUsedSTDLibFiles(DepType type)
    {
        std::unordered_set<std::string> result;
        for (auto& item : stdDepsMap) {
            if (item.second == type || item.second == DepType::BOTH) {
                (void)result.emplace(item.first);
            }
        }
        return result;
    }

    bool IsMacroRelatedPackageName(const std::string& fullPackageName) const;

    /**
     * Use for macro debug: Update File Node Import info.
     */
    void UpdateFileNodeImportInfo(AST::Package& package, const AST::File& file, OwnedPtr<AST::File>& newFile);

    std::vector<std::string> GetImportedStdMacroPackages() const
    {
        static const std::vector<std::string> STD_MACRO_PACKAGES{
            "std.unittest.testmacro", "std.unittest.mock.mockmacro", "std.deriving"};
        std::vector<std::string> importedMacroPackages;
        for (auto& packageName : STD_MACRO_PACKAGES) {
            if (auto pd = GetPackageDecl(packageName)) {
                importedMacroPackages.push_back(packageName);
            }
        }
        return importedMacroPackages;
    }
    /**
     * Get the direct imported macro package name for macro expand
     * ======================
     * // useMacro.cj
     * import macro_def_pkg1.*
     * import macro_def_pkg2.*
     * import other_pkg.*
     * @M1 class A{}
     * ======================
     * We need to get macro_def_pkg1, macro_def_pkg2 for macro expansion.
     */
    std::unordered_set<Ptr<AST::Package>> GetImportedPkgsForMacro()
    {
        return directMacroDeps;
    }

    /** Find cjo and load cached type for given source package, return codegen mangledName of remove decls. */
    std::unordered_set<std::string> LoadCachedTypeForPackage(
        const AST::Package& pkg, const std::map<std::string, Ptr<AST::Decl>>& mangledName2DeclMap);

    /**
     * For LSP, set cached cjo data @p cjoData and optional corresponding sha256 digest @p encrypt
     * for @param fullPackageName .
     */
    void SetPackageCjoCache(const std::string& fullPackageName, const std::vector<uint8_t>& cjoData) const;

    /**
     * For LSP, clear all cached cjo data.
     */
    void ClearPackageCjoCache() const;

    /** @brief Set the BCHIR data for package. Used by LSP */
    void SetPackageBchirCache(const std::string& fullPackageName, const std::vector<uint8_t>& bchirData);

    /** @brief Returns a pointer for the cached BCHIR data for fullPackageName if it exists or nullptr otherwise. */
    const std::vector<uint8_t>* GetBchirCache(const std::string& fullPackageName);

    Ptr<CjoManager> GetCjoManager() const
    {
        return cjoManager.get();
    }

    /** @brief Clear the cache for BCHIR packages. */
    void ClearPackageBCHIRCache()
    {
        bchirFileCacheMap.clear();
    }

    /** Set BCHIRCache. only for lsp. */
    void SetUseBCHIRCache(bool shouldImport)
    {
        useBCHIRCache = shouldImport;
    }

    /** Get whether the source code will be reparsed during importation. */
    bool IsUseBCHIRCache() const
    {
        return useBCHIRCache;
    }

    /**
     * For LSP, set whether need to import source code in astLoader.
     * LSP will set the status to 'false'.
     */
    void SetSourceCodeImportStatus(bool shouldImport)
    {
        importSrcCode = shouldImport;
    }
    /** Get whether the source code will be reparsed during importation. */
    bool IsSourceCodeImported()
    {
        return importSrcCode;
    }

    /**
     * Find packageDecl from the map which contains both source and imported packages.
     * @param fullPackageName [in]: full package name 'module/package'
     * @return if found, return the packageDecl, otherwise return nullptr.
     */
    Ptr<AST::PackageDecl> GetPackageDecl(const std::string& fullPackageName) const;
    Ptr<AST::Package> GetPackage(const std::string& fullPackageName) const;
    void SetImportedPackageFromASTNode(std::vector<OwnedPtr<AST::Package>>& pkgs);
    using DeclImportsMap = std::unordered_map<Ptr<const AST::Decl>, std::vector<Ptr<const AST::ImportSpec>>>;
    const DeclImportsMap& GetImportsOfDecl(const std::string& fullPackageName) const
    {
        static const DeclImportsMap EMPTY{};
        auto found = declsImportedByNodeMap.find(fullPackageName);
        return found == declsImportedByNodeMap.end() ? EMPTY : found->second;
    }
    static std::string GetMainPartPkgNameForTestPkg(const std::string& testPkgName);
    static bool IsTestPackage(const std::string& pkgName);

    bool IsExtendAccessible(
        const AST::File& file, const AST::ExtendDecl& ed, const Ptr<DiagnosticBuilder> builder = nullptr) const;
    // handles valid enum, struct, class, interface ty only, return true in all other cases
    bool IsTyAccessible(const AST::File& file, const AST::Ty& ty) const;
    bool IsExtendMemberImported(const Ptr<AST::ExtendDecl> extend, const AST::File& file, const AST::Decl& member,
        const Ptr<DiagnosticBuilder> builder) const;
    bool IsExtendMemberAccessible(const AST::File& file, const AST::Decl& member, AST::Ty& baseTy,
        const Ptr<DiagnosticBuilder> builder = nullptr) const;
    void AddUsedMacroDecls(const Ptr<const AST::File> file, Ptr<const AST::Decl> decl);
    std::map<std::string, std::set<Ptr<const AST::Decl>, AST::CmpNodeByPos>>& GetUsedMacroDecls(const AST::File& file);

public:
    /**
     * Whether import prelude libraries by default, default value is false, and
     * it will be set by CompilerInstance during compilation.
     */
    bool implicitPrelude{false};

    std::unordered_map<std::string, Cangjie::CHIR::Interpreter::Bchir> importedBchirs;

    const std::vector<std::string>& GetSearchPath() const
    {
        return cjoManager->GetSearchPath();
    }
    void DeleteASTWriters() noexcept;
    void DeleteASTLoaders() noexcept;

    std::unordered_map<std::string, std::string> GetDepPkgCjdPaths()
    {
        return cjdFilePaths;
    }

    DiagnosticEngine& GetDiagnosticEngine() const
    {
        return diag;
    }

private:
    DiagnosticEngine& diag;
    const GlobalOptions& opts;

    bool useBCHIRCache{false};

    std::unordered_map<std::string, std::vector<uint8_t>> bchirFileCacheMap;
    /*
     * Store all accessible decls for each file in source package.
     * fileHash -> {declName, declSet}
     */
    std::map<uint64_t, std::map<std::string, AST::OrderedDeclSet>> fileImportedDeclsMap;

    /*
     * Store all used macro decls for each file in source package.
     * indexOfPackage -> {packageName, declSet}
     */
    std::map<size_t, std::map<std::string, std::set<Ptr<const AST::Decl>, AST::CmpNodeByPos>>> fileUsedMacroDeclsMap;

    /*
     * Store all accessible decls for in source package. {declName, declSet}.
     */
    std::map<std::string, AST::OrderedDeclSet> importedDeclsMap;
    std::unordered_map<Ptr<const AST::Package>, ASTWriter*> astWriters;

    /** Store the cjo file path of STD packages on used. */
    std::unordered_map<std::string, DepType> stdDepsMap;

    /** Direct imported macro packages. */
    std::unordered_set<Ptr<AST::Package>> directMacroDeps;
    /** Map of fullPackageName -> imported decl to the 'ImportSpec' which imports the decl. */
    std::unordered_map<std::string, DeclImportsMap> declsImportedByNodeMap;
    /** NOTE: Collect imported alias decl and alias-import only. */
    std::unordered_map<Ptr<AST::Decl>, std::set<std::string>> declToTypeAlias;

    /**
     * @brief Try deserializing BCHIR.
     */
    bool deserializeBCHIR{false};
    /**
     * When it is true, import generic, inline and default implementation's source code.
     */
    bool importSrcCode{true};

private:
    /**
     * Used in `ResolveImportedPackageForFile` to handle package which has been parsed.
     * @return `false` if error occurred in recursive call of `ResolveImportedPackageForFile`,
     *          return `true` for normal case.
     */
    bool HandleParsedPackage(
        const AST::Package& package, const std::string& filePath, bool isUsedAsCommon, bool isRecursive);

    void SaveDepPkgCjdPath(const std::string& fullPackageName, const std::string& cjoPath);

    /**
     * Resolve all packages imported.
     */
    bool ResolveImportedPackages(const std::vector<Ptr<AST::Package>>& packages);
    bool ResolveImportedPackageHeaders(const AST::Package& package, bool isRecursive = false);

    /**
     * Resolve all packages imported for file.
     */
    bool ResolveImportedPackageForFile(AST::File& file, bool isRecursive = false);

    void HandleSTDPackage(const std::string& fullPackageName, const std::string& cjoPath, bool isRecursive = false);

    void AddImportedDeclsForSourcePackage(const AST::Package& pkg);

    /**
     * Get information of dependency packages of @param pkg in json format.
     */
    std::string GeneratePkgDepInfo(const AST::Package& pkg, bool exportCJO = true) const;

    bool CheckCjoPathLegality(const OwnedPtr<AST::ImportSpec>& import, const std::string& cjoPath,
        const std::string& fullPackageName, bool isRecursive, bool isMainPartPkgForTestPkg);

    void UpdateMacroPackageUsage(const AST::Package& pkg);
    void CheckImports(const AST::Package& pkg);
    void CheckRedefinition(const AST::Package& pkg);

    bool IsTypeAccessible(const AST::File& file, const AST::Type& type) const;
    bool IsExtendAllUpperBoundsImported(
        const AST::ExtendDecl& ed, const AST::File& file, const Ptr<DiagnosticBuilder> builder) const;
    bool IsDeclAccessible(const AST::File& file, AST::Decl& decl) const;
    const Ptr<AST::Type> FindImplmentInterface(
        const AST::File& file, const AST::Decl& member, const Ptr<AST::Type>& id) const;

    friend class PackageManager;

    class DependencyGraph {
    public:
        explicit DependencyGraph(CjoManager& cjoManager) : cjoManager(cjoManager)
        {
        }
        const std::vector<Ptr<AST::PackageDecl>>& GetDirectDependencyPackageDecls(const std::string& fullPackageName);
        const std::vector<Ptr<AST::PackageDecl>>& GetAllDependencyPackageDecls(
            const std::string& fullPackageName, bool includeMacroPkg);
        const std::set<std::string>& GetAllDependencyPackageNames(
            const std::string& fullPackageName, bool includeMacroPkg);
        void AddDependenciesForPackage(AST::Package& pkg);

    private:
        const std::map<std::string, std::set<Ptr<const AST::ImportSpec>, AST::CmpNodeByPos>>& GetEdges(
            const std::string& fullPackageName) const;

        void AddDependenciesForImport(AST::Package& pkg, const AST::ImportSpec& import);
        void AddDependency(const std::string& u, const std::string& v, const AST::ImportSpec& import);

        CjoManager& cjoManager;
        std::map<std::string, std::map<std::string, std::set<Ptr<const AST::ImportSpec>, AST::CmpNodeByPos>>>
            dependencyMap;
        std::unordered_map<std::string, std::unordered_set<std::string>> pkgReExportMap;
        //! Caches for the query APIs. These caches are build on demand.
        std::unordered_map<std::string, std::vector<Ptr<AST::PackageDecl>>> cacheDirectDependencyPackageDecls;
        std::unordered_map<std::pair<std::string, bool>, std::vector<Ptr<AST::PackageDecl>>, HashPair>
            cacheDependencyPackageDecls;
        std::unordered_map<std::string, std::set<std::string>> cacheDependencyPackageNames;
    };

    //! Key data structures used by `ImportManager`:
    OwnedPtr<CjoManager> cjoManager;
    OwnedPtr<DependencyGraph> dependencyGraph;
    Ptr<AST::Package> curPackage;
    TypeManager& typeManager;

    // Key is cjd fullPackageName, and value is .cj.d file path.
    std::unordered_map<std::string, std::string> cjdFilePaths;
};
} // namespace Cangjie
#endif // CANGJIE_MODULES_IMPORTMANAGER_H
