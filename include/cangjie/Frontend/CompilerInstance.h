// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the CompilerInstance, which holds all compile context.
 */

#ifndef CANGJIE_FRONTEND_COMPILERINSTANCE_H
#define CANGJIE_FRONTEND_COMPILERINSTANCE_H

#include <memory>
#include <string>
#include <vector>

#include "cangjie/AST/ASTContext.h"
#include "cangjie/AST/Node.h"
#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/CHIR/Analysis/AnalysisWrapper.h"
#include "cangjie/CHIR/Analysis/ConstAnalysis.h"
#include "cangjie/CHIR/Analysis/TypeAnalysis.h"
#include "cangjie/CHIR/CHIRBuilder.h"
#include "cangjie/Frontend/CompileStrategy.h"
#include "cangjie/Frontend/CompilerInvocation.h"
#include "cangjie/IncrementalCompilation/IncrementalScopeAnalysis.h"
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
#include "cangjie/MetaTransformation/MetaTransform.h"
#endif
#include "cangjie/Modules/ImportManager.h"

namespace Cangjie {
using HANDLE = void*;
class GenericInstantiationManager;
class MacroExpansion;
class TypeChecker;
class PackageManager;
class BaseMangler;
class TestManager;
/**
 * Compile util to some stage.
 */
enum class CompileStage {
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    LOAD_PLUGINS,      /**< Load compiler plugins. */
#endif
    PARSE,             /**< Parse the source code. */
    CONDITION_COMPILE, /**< Conditional compile. */
    IMPORT_PACKAGE,    /**< Import packages. */
    MACRO_EXPAND,      /**< Expand macros. */
    AST_DIFF,          /**< Diff the AST to get incremental compilation scope. */
    SEMA,              /**< TypeCheck. */
    DESUGAR_AFTER_SEMA,
    GENERIC_INSTANTIATION, /**< GenericInstantiation. */
    OVERFLOW_STRATEGY,     /**< Overflow strategy. */
    MANGLING,              /**< Mangling all decls. */
    CHIR,                  /**< CHIR. */
    CODEGEN,               /**< Generate target code. */
    SAVE_RESULTS,          /**< Save AST and CHIR results to files. */

    COMPILE_STAGE_NUMBER /**< The number of compile stage  */
};

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
class CHIRData {
public:
    void InitData(std::unordered_map<unsigned int, std::string>* fileNameMap, size_t threadNum);

    CHIR::CHIRContext& GetCHIRContext();

    void AppendNewPackage(CHIR::Package* package);
    std::vector<CHIR::Package*> GetAllCHIRPackages() const;
    CHIR::Package* GetCurrentCHIRPackage() const;

    void SetImplicitFuncs(const std::unordered_map<std::string, CHIR::FuncBase*>& funcs);
    std::unordered_map<std::string, CHIR::FuncBase*> GetImplicitFuncs() const;

    void SetConstVarInitFuncs(const std::vector<CHIR::FuncBase*>& funcs);
    std::vector<CHIR::FuncBase*> GetConstVarInitFuncs() const;

    CHIR::AnalysisWrapper<CHIR::ConstAnalysis, CHIR::ConstDomain>& GetConstAnalysisResultRef();
    const CHIR::AnalysisWrapper<CHIR::ConstAnalysis, CHIR::ConstDomain>& GetConstAnalysisResult() const;

private:
    CHIR::CHIRContext cctx;
    std::vector<CHIR::Package*> chirPkgs;
    // used by codegen
    std::unordered_map<std::string, CHIR::FuncBase*> implicitFuncs;
    // used by interpreter
    std::vector<CHIR::FuncBase*> initFuncsForConstVar;
    // only for AnalysisWrapper
    CHIR::CHIRBuilder builder{cctx, 0};
    // provide the capability and results of constant analysis, used by cjlint
    CHIR::AnalysisWrapper<CHIR::ConstAnalysis, CHIR::ConstDomain> constAnalysisWrapper{builder};
};
#endif

/**
 *  The base compiler instance, holds the data which has full compile lifetime.
 */
class CompilerInstance {
    friend class CompileStrategy;
    friend class MacroExpansion;
    friend class MacroEvaluation;
    friend class TypeChecker;
    friend class GenericInstantiationManager;
    friend class FullCompileStrategy;
    friend class IncrementalCompileStrategy;

public:
    CompilerInstance(CompilerInvocation& invocation, DiagnosticEngine& diag);
    virtual ~CompilerInstance();

    /**
     * Set different CompileStrategy to do the real compile jobs.
     */
    void SetCompileStrategy(CompileStrategy* newCompileStrategy)
    {
        delete this->compileStrategy;
        this->compileStrategy = newCompileStrategy;
    }
    /**
     * Initialize the compilation flow by
     * 1) check pre-conditions
     * 2) set the action functions in compilation flow
     */
    virtual bool InitCompilerInstance();

    /**
     * Perform compile to some @p stage.
     */
    virtual bool Compile(CompileStage stage = CompileStage::CHIR);
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    bool PerformPluginLoad();
#endif

    /**
     * Perform parse.
     */
    virtual bool PerformParse();

    virtual Ptr<AST::File> GetFileByPath(const std::string& filePath)
    {
        (void)filePath;
        return nullptr;
    }

    /**
     * Perform conditonal compilation.
     */
    virtual bool PerformConditionCompile();

    /**
     * Perform import package.
     */
    virtual bool PerformImportPackage();

    /**
     * Perform macro expand.
     */
    virtual bool PerformMacroExpand();

    /**
     * Perform AST diff to get incremental compilation scope.
     */
    virtual bool PerformIncrementalScopeAnalysis();
    bool CalculateASTCache();

    /**
     * Perform typecheck and other semantic check jobs.
     */
    virtual bool PerformSema();

    /**
     * Perform desugar after sema.
     */
    virtual bool PerformDesugarAfterSema();

    /**
     * Perform generic instantiation.
     */
    virtual bool PerformGenericInstantiation();

    /**
     * Perform overflow strategy.
     */
    virtual bool PerformOverflowStrategy();

    /**
     * Perform Mangle.
     */
    virtual bool PerformMangling();
    bool GenerateCHIRForPkg(AST::Package& pkg);
    virtual bool PerformCHIRCompilation();

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    std::unordered_map<unsigned int, std::string>& GetFileNameMap()
    {
        return fileNameMap;
    }
#endif

    /**
     * Dump the parsed tokens for debugging.
     */
    virtual bool DumpTokens();

    /**
     * Dump the symbols for debugging.
     */
    virtual void DumpSymbols();

    /**
     * Dump the expanded macro for debugging.
     */
    virtual void DumpMacro();

    /**
     * Codegen to target code.
     */
    virtual bool PerformCodeGen()
    {
        return true;
    }

    /**
     * Export AST and generate cjo, bchir.
     */
    virtual bool PerformCjoAndBchirSaving()
    {
        return true;
    }

    /**
     * Get raw ptrs of all Packages after Compile.
     *
     * @return empty vector If Parse failed.
     */
    std::vector<Ptr<AST::Package>> GetPackages()
    {
        return pkgs;
    }

    /**
     * Get raw ptrs of source Packages after Compile.
     *
     * @return empty vector If Parse failed.
     */
    std::vector<Ptr<AST::Package>> GetSourcePackages() const;

    /**
     * Get Source Manager.
     */
    SourceManager& GetSourceManager();
    /**
     * Get ASTContext by Package.
     *
     * @param pkg Package pointer.
     * @return ASTContext of the @p pkg, if not found, return nullptr.
     */
    ASTContext* GetASTContextByPackage(Ptr<AST::Package> pkg) const;

    /**
     * Get dependent package of current package.
     * Information is written in json format.
     */
    const std::vector<std::string>& GetDepPkgInfo()
    {
        return depPackageInfo;
    }

    /**
     * Import all packages from other libs.
     */
    bool ImportPackages();

    /**
     * Get ExtendDecl Set from primitive type or Decl.
     * @param type can be primitive type or Decl.
     * @return set of Ptr<ExtendDecl>, if not found, return empty set.
     */
    std::set<Ptr<AST::ExtendDecl>> GetExtendDecls(
        const std::variant<Ptr<AST::Ty>, Ptr<AST::InheritableDecl>>& type) const;

    /**
     * Obtains the extended members visible to the current file through the type declaration.
     * @param type can be primitive type or Decl.
     * @return vector of Ptr<ExtendDecl>, if not found, return empty set.
     */
    std::vector<Ptr<AST::Decl>> GetAllVisibleExtendMembers(
        const std::variant<Ptr<AST::Ty>, Ptr<AST::InheritableDecl>>& type, const AST::File& curFile) const;

    /**
     * Get the candidate decls or types of given @p expr in given @p scopeName from sema cache @p ctx.
     * If the @p hasLocal is true, the target will be found from local scope firstly.
     * NOTE: used by lsp.
     * @param ctx cached sema context.
     * @param scopeName the scopeName of current position.
     * @param expr the expression waiting to found candidate decls or types.
     * @param hasLocalDecl whether the given expression is existed in the given scope.
     * @return found candidate decls or types.
     */
    Candidate GetGivenReferenceTarget(
        ASTContext& ctx, const std::string& scopeName, AST::Expr& expr, bool hasLocalDecl) const;

    bool UpdateAndWriteCachedInfoToDisk();

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    /**
     * Record the handle for compiler plugin
     */
    void AddPluginHandle(HANDLE handle)
    {
        (void)pluginHandles.emplace_back(handle);
    }

    /**
     * Unload the handles for all compiler plugins
     */
    bool UnloadPluginHandle()
    {
        metaTransformPluginBuilder = {};
        // plugins should be unloaded after metaTransformPluginBuilder deconstruction.
        for (auto& handle : pluginHandles) {
            if (InvokeRuntime::CloseSymbolTable(handle) != 0) {
                Errorln("close plugin dynamic library failed.");
                return false;
            }
        }
        pluginHandles.clear();
        return true;
    }
#endif

    /**
     * Infomation written to cached file and needed by incremental compiling
     */
    CompilationCache cachedInfo;

    /**
     * Infomation generated in CHIR stage
     */
    struct CHIRInfo {
        OptEffectStrMap optEffectMap;
        std::unordered_set<Ptr<const AST::StructTy>> ccTys;
        VirtualWrapperDepMap curVirtFuncWrapDep;
        VirtualWrapperDepMap delVirtFuncWrapForIncr;
        std::set<std::string> ccOutFuncsRawMangle;
        VarInitDepMap varInitDepMap;
    };
    CHIRInfo chirInfo;
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    MetaTransformPluginBuilder metaTransformPluginBuilder;
#endif
    /**
     * CompilerInvocation, storing anything external the Instance needs.
     */
    CompilerInvocation& invocation;

    /**
     * The compiler home path, can be set or detect from executable path.
     */
    std::string cangjieHome;

    /**
     * The standard modules path, with os/target/backend information.
     */
    std::string cangjieModules;

    /**
     * Input source file paths.
     */
    std::vector<std::string> srcFilePaths;

    /**
     * Input source file directories.
     */
    std::unordered_set<std::string> srcDirs;

    /**
     * Collect all diagnostic information during compilation, then print to stderr.
     */
    DiagnosticEngine& diag;

    /**
     * Determine the compile order.
     */
    PackageManager* packageManager = nullptr;

    /**
     * TypeManager hold all types.
     */
    TypeManager* typeManager = nullptr;

    /**
     * ImportManager hold all import related information for TypeCheck.
     */
    ImportManager importManager;

    /**
     * TestManager provides test specific functionality which should be reflected in the compiler logic.
     */
    TestManager* testManager = nullptr;

    /**
     * GenericInstantiationManager hold generic infos.
     */
    GenericInstantiationManager* gim = nullptr;
    // Compile one Package from source files.
    bool compileOnePackageFromSrcFiles = false;
    // Read source code from cache.
    bool loadSrcFilesFromCache = false;
    // the source code cache map use for LSP. Key is path, Value is source code.
    std::unordered_map<std::string, std::string> bufferCache;

    // tokensEvalInMacro for DumpMacro, error report.
    std::vector<std::string> tokensEvalInMacro;

    /**
     * The current processing package, used in modular compilation.
     */
    Ptr<AST::Package> currentPkg{nullptr};

    /**
     * @brief Get the CHIRContext
     */
    CHIR::CHIRContext& GetCHIRContext();

    /**
     * @brief Get astPkg2chirPkgMap
     */
    const std::unordered_map<Ptr<AST::Package>, Ptr<CHIR::Package>>& GetASTPkg2CHIRPkgMap()
    {
        return astPkg2chirPkgMap;
    }

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    // used only by cjlint
    const CHIR::AnalysisWrapper<CHIR::ConstAnalysis, CHIR::ConstDomain>& GetConstAnalysisWrapper() const;

    std::vector<CHIR::Package*> GetAllCHIRPackages() const;
#else
    // used only by cjlint
    const CHIR::AnalysisWrapper<CHIR::ConstAnalysis, CHIR::ConstDomain>& GetConstAnalysisWrapper() const
    {
        return constAnalysisWrapper;
    }
    std::vector<CHIR::Package*> GetAllCHIRPackages() const
    {
        std::vector<CHIR::Package*> res;
        for (auto& it : astPkg2chirPkgMap) {
            res.emplace_back(it.second);
        }
        return res;
    }
#endif

    /**
     * @brief mangler
     */
    std::unique_ptr<BaseMangler> mangler;

    /**
     * Map between the mangle name (before sema) with the Decl.
     */
    std::map<std::string, Ptr<AST::Decl>> mangledName2DeclMap;

    // Every compiler instance has a source manager.
    SourceManager sm;

    /// Incremental compilation state.
    IncreKind kind{IncreKind::INVALID};

    ///@{
    /// Overriden by CJLint
    bool releaseCHIRMemory = true;
    bool needToOptString = false;
    bool needToOptGenericDecl = false;
    bool isCJLint = false;
    ///@}

    /************** Used by Incremental Compilation Start **************/
    RawMangled2DeclMap rawMangleName2DeclMap;
    ASTCache astCacheInfo;
    std::unordered_set<Ptr<const AST::Decl>> declsWithDuplicatedRawMangleName;
    std::unordered_map<RawMangledName, std::list<std::pair<Ptr<AST::ExtendDecl>, int>>> directExtends;
    FileMap fileMap;
    std::vector<const AST::Decl*> order;

    /// Used by CJLint
    bool IsBuildTrie() const
    {
        return buildTrie;
    }

    void SetBuildTrie(bool flag)
    {
        buildTrie = flag;
    }

    /************** Used by Incremental Compilation End **************/

    bool DeserializeCHIR();
    std::vector<OwnedPtr<AST::Package>> srcPkgs;

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    CHIRData chirData;
    // we need to remove this later
    std::unordered_map<Ptr<AST::Package>, Ptr<CHIR::Package>> astPkg2chirPkgMap;
#endif

protected:
    /**
     * Perform functions map according to different CompileStage.
     */
    std::unordered_map<CompileStage, std::function<bool(CompilerInstance*)>> performMap;

    CompileStrategy* compileStrategy = nullptr;
    TypeChecker* typeChecker = nullptr;
    // Modularize compilation.
    bool ModularizeCompilation();
    // Build Trie tree or not;
    bool buildTrie = true;

    std::unordered_map<unsigned int, std::string> fileNameMap;

    // Merge source packages and imported packages, also init ASTContext here.
    void MergePackages();
    // Allowing only add source package once. Used for LSPCompilerInstance.
    void AddSourceToMember();
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    void ManglingHelpFunction(const BaseMangler& baseMangler);
#endif

    void CacheCompileArgs();
    void CacheSemaUsage(SemanticInfo&& info);
    void UpdateMangleNameForCachedInfo();

private:
    // Guess the CANGJIE_HOME.
    bool DetectCangjieHome();

    // Guess the modules file path.
    bool DetectCangjieModules();

    // Merged source packages and imported packages.
    std::vector<Ptr<AST::Package>> pkgs;

    // Package to ASTContext map.
    std::unordered_map<Ptr<AST::Package>, std::unique_ptr<ASTContext>> pkgCtxMap;

    std::vector<std::string> depPackageInfo;

    virtual void UpdateCachedInfo();
    bool WriteCachedInfo();
    bool ShouldWriteCacheFile() const;

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    std::vector<HANDLE> pluginHandles;
#endif
};
} // namespace Cangjie
#endif // CANGJIE_FRONTEND_COMPILERINSTANCE_H
