// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares Translate AST to CHIR.0
 */

#ifndef CANGJIE_CHIR_AST2CHIR_H
#define CANGJIE_CHIR_AST2CHIR_H

#include <deque>
#include <tuple>

#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/PrintNode.h"
#include "cangjie/AST/Walker.h"

#include "cangjie/AST/Utils.h"
#include "cangjie/CHIR/ImplicitImportedFuncMgr.h"
#include "cangjie/CHIR/AST2CHIR/AST2CHIRNodeMap.h"
#include "cangjie/CHIR/AST2CHIR/GlobalVarInitializer.h"
#include "cangjie/CHIR/Package.h"
#include "cangjie/CHIR/Type/CHIRType.h"
#include "cangjie/CHIR/Value.h"
#include "cangjie/IncrementalCompilation/CompilationCache.h"
#include "cangjie/Mangle/CHIRMangler.h"
#include "cangjie/Modules/ImportManager.h"
#include "cangjie/Sema/GenericInstantiationManager.h"
#include "cangjie/Utils/ProfileRecorder.h"

#include "cangjie/AST/Types.h"
#include "cangjie/Sema/TypeManager.h"

namespace Cangjie::CHIR {
class AST2CHIR {
public:
    class AST2CHIRBuilder {
    public:
        const GlobalOptions* opts;
        const GenericInstantiationManager* gim;
        ImportManager* import;
        SourceManager* sourceManager;
        TypeManager* types;
        DiagAdapter* diag;
        IncreKind kind;
        CompilationCache* cachedInfo;
        CHIRBuilder* builder;

        CHIRType* chirType;
        std::string outputPath;
        bool isComputingAnnos{}; // is in computing annotations mode

        // Note: this variable is used only in sancov.
        /**
         * @brief Build AST2CHIR instance.
         */
        AST2CHIR Build()
        {
            CJC_NULLPTR_CHECK(opts);
            CJC_NULLPTR_CHECK(import);
            CJC_NULLPTR_CHECK(types);
            CJC_NULLPTR_CHECK(diag);
            return AST2CHIR(*this);
        }
        /**
         * @brief set global options.
         *
         * @param globalOptions global options to set.
         */
        AST2CHIRBuilder* SetGlobalOptions(const GlobalOptions& globalOptions)
        {
            opts = &globalOptions;
            return this;
        }
        /**
         * @brief set incremetal kind.
         *
         * @param increKind incremetal kind to set.
         */
        AST2CHIRBuilder* SetIncreKind(const IncreKind& increKind)
        {
            kind = increKind;
            return this;
        }
        AST2CHIRBuilder* SetCachedInfo(CompilationCache& newinfo)
        {
            cachedInfo = &newinfo;
            return this;
        }
        /**
         * @brief set generic instantiate manager.
         *
         * @param genericInstantiateManager generic instantiate manager to set.
         */
        AST2CHIRBuilder* SetGenericInstantiationManager(const GenericInstantiationManager* genericInstantiateManager)
        {
            gim = genericInstantiateManager;
            return this;
        }
        /**
         * @brief set import manager.
         *
         * @param manager import manager to set.
         */
        AST2CHIRBuilder* SetImportManager(ImportManager& manager)
        {
            import = &manager;
            return this;
        }

        /**
         * @brief set source manager.
         *
         * @param manager source manager to set.
         */
        AST2CHIRBuilder* SetSourceManager(SourceManager& manager)
        {
            sourceManager = &manager;
            return this;
        }
        AST2CHIRBuilder* SetDiag(DiagAdapter& ciDiag)
        {
            diag = &ciDiag;
            return this;
        }
        /**
         * @brief set typeManager context.
         *
         * @param typeManager typeManager context to set.
         */
        AST2CHIRBuilder* SetTypeManager(TypeManager& typeManager)
        {
            types = &typeManager;
            return this;
        }

        /**
         * @brief set CHIR type manager.
         *
         * @param type CHIR type manager.
         */
        AST2CHIRBuilder* SetCHIRType(CHIRType& type)
        {
            chirType = &type;
            return this;
        }

        /**
         * @brief set CHIR Builder.
         *
         * @param builder CHIR Builder.
         */
        AST2CHIRBuilder* SetCHIRBuilder(CHIRBuilder& bd)
        {
            builder = &bd;
            return this;
        }

        /**
         * @brief set CHIR output path.
         *
         * @param path output path.
         */
        AST2CHIRBuilder* SetOutputPath(const std::string& path)
        {
            outputPath = path;
            return this;
        }
        AST2CHIRBuilder* SetComputeAnnotations(bool value)
        {
            isComputingAnnos = value;
            return this;
        }
    };
    ~AST2CHIR()
    {
    }

    /**
    * @brief translate AST package to CHIR package
    *
    * @param node AST package
    * @return bool return false if IR is illegal
    */
    bool ToCHIRPackage(AST::Package& node);

    CHIR::Package* GetPackage() const
    {
        return package;
    }

    std::unordered_set<Func*>&& GetSrcCodeImportedFuncs()
    {
        return std::move(srcCodeImportedFuncs);
    }

    std::unordered_set<GlobalVar*>&& GetSrcCodeImportedVars()
    {
        return std::move(srcCodeImportedVars);
    }

    std::unordered_map<std::string, FuncBase*>&& GetImplicitFuncs()
    {
        return std::move(implicitFuncs);
    }

    std::vector<FuncBase*>&& GetInitFuncsForConstVar()
    {
        for (auto f : initFuncsForAnnoFactory) {
            initFuncsForConstVar.push_back(f);
        }
        return std::move(initFuncsForConstVar);
    }

    std::unordered_map<Block*, Terminator*>&& GetMaybeUnreachableBlocks()
    {
        return std::move(maybeUnreachable);
    }

    std::vector<std::pair<const AST::Decl*, Func*>>&& GetAnnoFactoryFuncs()
    {
        return std::move(annoFactoryFuncs);
    }

    void SetAnnoOnlyDecls(std::vector<const AST::Decl*>&& annoOnly)
    {
        annoOnlyDecls = std::move(annoOnly);
    }

private:
    explicit AST2CHIR(AST2CHIRBuilder& builderToCHIR)
        : opts(*builderToCHIR.opts),
          gim(builderToCHIR.gim),
          importManager(*builderToCHIR.import),
          sourceManager(*builderToCHIR.sourceManager),
          types(*builderToCHIR.types),
          diag(*builderToCHIR.diag),
          cachedInfo(*builderToCHIR.cachedInfo),
          kind(builderToCHIR.kind),
          builder(*builderToCHIR.builder),
          chirType(*builderToCHIR.chirType),
          outputPath(builderToCHIR.outputPath),
          isComputingAnnos{builderToCHIR.isComputingAnnos}
    {
    }

    void AST2CHIRCheck();
    void CollectImplicitFuncs();
    void AddToImplicitFuncs(AST::FuncDecl& funcDecl, std::vector<ImplicitImportedFunc>& registeredImplicitFuncs,
        std::unordered_set<Ptr<const AST::Decl>>& implicitlyImportedDecls) const;
    void CollectImportedDecls(const AST::Package& node);
    void CollectDeclsInCurPkg(AST::Package& node);
    void CollectImportedGenericInstantiatedDecl(
        const AST::Package& node, std::unordered_set<std::string>& mangledNameSet);
    void CollectImportedDeclUsedInCurPkg(AST::Decl& decl);
    void CollectImportedPropDecl(AST::PropDecl& propDecl);
    void CollectImportedGenericDecl(AST::Decl& decl);
    void CollectDecls(AST::Decl& decl, bool instantiated);
    void CollectDeclsFromEnumDecl(AST::EnumDecl& enumDecl);
    void CollectDeclsFromExtendDecl(AST::ExtendDecl& extendDecl);
    void CollectDeclsFromClassLikeDecl(AST::ClassLikeDecl& classLikeDecl);
    void CollectDeclsFromStructDecl(const AST::StructDecl& structDecl);
    void CollectMemberDecl(AST::Decl& decl);
    void CollectFuncDecl(AST::FuncDecl& funcDecl);
    void CollectImportedFuncDeclAndDesugarParams(AST::FuncDecl& funcDecl);
    void CollectImportedGlobalOrStaticVarDecl(AST::VarDecl& varDecl);

    void CollectDeclToList(AST::Decl& decl, std::vector<Ptr<const AST::Decl>>& astNodes);
    void CollectFuncDeclToList(AST::FuncDecl& func, std::vector<Ptr<const AST::Decl>>& list);
    void CollectDesugarDecl(AST::Decl& decl);

    void CollectInstantiatedDecls(const AST::Decl& decl);
    void CollectVarandVarwithpatternDecl(AST::Decl& decl);

    /**
     * @brief create all top-level func decl's shell and var decls, cache them to global symbol table.
     */
    void CacheTopLevelDeclToGlobalSymbolTable();
    void CreateAnnoOnlyDeclSig(const AST::Decl& decl);
    void CreatePseudoDefForAnnoOnlyDecl(const AST::Decl& decl);
    void CacheCustomTypeDefToGlobalSymbolTable();
    void CreateCustomTypeDef(const AST::Decl& decl, bool isImported);
    void TranslateAllCustomTypeTy();
    void TranslateNominalDecls(const AST::Package& pkg);
    void SetFuncAttributeAndLinkageType(const AST::FuncDecl& astFunc, FuncBase& chirFunc);

    void FlatternPattern(const AST::Pattern& pattern, bool isLocalConst);
    void CreateAndCacheGlobalVar(const AST::VarDecl& decl, bool isLocalConst);
    void CreateGlobalVarSignature(const std::vector<Ptr<const AST::Decl>>& decls, bool isLocalConst = false);

    void CreateFuncSignatureAndSetGlobalCache(const AST::FuncDecl& funcDecl);
    void CreateImportedFuncSignatureAndSetGlobalCache(const AST::FuncDecl& funcDecl);
    void CreateImportedValueSignatureAndSetGlobalCache(const AST::VarDecl& varDecl);
    void CreatePseudoImportedFuncSignatureAndSetGlobalCache(const AST::FuncDecl& funcDecl);

    void TranslateAllDecls(const AST::Package& pkg, const InitOrder& initOrder);
    void TranslateTopLevelDeclsInParallel();
    void TranslateInParallel(const std::vector<Ptr<const AST::Decl>>& decls);

    /** @brief Returns true if the AST2Chir process is unsuccessfull **/
    bool HasFailed() const;

    // Register all source file names
    void RegisterAllSources();

    void SetInitFuncForStaticVar();
    void TranslateInitOfGlobalVars(const AST::Package& pkg, const InitOrder& initOrder);
    void CollectTopLevelDecls(AST::Package& pkg);
    void CacheSomeDeclsToGlobalSymbolTable();

    void CollectStaticInitFuncInfo();
    void CollectFuncsAndVars();
    void CollectLocalConstFuncAndVars(const AST::Package& pkg);
    std::pair<InitOrder, bool> SortGlobalVarDecl(const AST::Package& pkg);

    void SetGenericDecls() const;
    void SetExtendInfo();
    void UpdateExtendParent();

    Translator CreateTranslator();
    /* Micro refactoring for CJMP. */
    void TranslateFuncParams(const AST::FuncDecl& funcDecl, Func& func) const;
    void TranslateVecDecl(const std::vector<Ptr<const AST::Decl>>& decls, Translator& trans) const;

    /* Add methods for CJMP. */
    // Try to deserialize common part for CJMP.
    bool TryToDeserializeCHIR();
    // Check whether the decl is deserialized for CJMP.
    bool MaybeDeserialized(const AST::Decl& decl) const;
    // Try to get deserialized node from package including CustomTypeDef (excluding Extend), Func, GlobalVar,
    // ImportedValue.
    template<typename T>
    T* TryGetDeserialized(const AST::Decl& decl)
    {
        if (!MaybeDeserialized(decl)) {
            return nullptr;
        }
        std::string key = GLOBAL_VALUE_PREFIX + decl.mangledName;
        if (decl.TestAttr(AST::Attribute::FOREIGN) && decl.IsFunc()) {
            key = decl.rawMangleName;
        }
        T* res = nullptr;
        if constexpr (std::is_base_of_v<CustomTypeDef, T>) {
            auto it = deserializedDefs.find(key);
            res = it == deserializedDefs.end() ? nullptr : static_cast<T*>(it->second);
        } else {
            // get value
            auto it = deserializedVals.find(key);
            res = it == deserializedVals.end() ? nullptr : dynamic_cast<T*>(it->second);
        }
        if (res && !decl.TestAttr(AST::Attribute::PLATFORM)) {
            deserializedDecls.insert(&decl);
        }
        return res;
    }
    // build symbol table for deserialized decls from common part.
    void BuildDeserializedTable();

    // Reset platform func for CJMP.
    void ResetPlatformFunc(const AST::FuncDecl& funcDecl, Func& func);
    // Check whether the decl need be translated for CJMP.
    inline bool NeedTranslate(const AST::Decl& decl) const
    {
        return deserializedDecls.find(&decl) == deserializedDecls.end();
    }
    void ProcessCommonAndPlatformNominals();
    const GlobalOptions& opts;
    const GenericInstantiationManager* gim;
    ImportManager& importManager;
    SourceManager& sourceManager;
    TypeManager& types;
    DiagAdapter& diag;
    CompilationCache& cachedInfo;
    IncreKind kind;
    VirtualWrapperDepMap curVirtFuncWrapDep;
    VirtualWrapperDepMap delVirtFuncWrapForIncr;
    // cache global func decl, global var decl
    AST2CHIRNodeMap<Value> globalCache;

    CHIRBuilder& builder;
    CHIRType& chirType;

    std::vector<Ptr<AST::Node>> allTopLevelNodes;
    /** @brief all files that after sorting in this package */
    std::vector<AST::File*> pkgFiles;
    std::unordered_map<std::string, FuncBase*> implicitFuncs;
    std::vector<FuncBase*> initFuncsForConstVar;
    std::vector<Func*> initFuncsForAnnoFactory;
    std::unordered_map<Block*, Terminator*> maybeUnreachable;

    std::string outputPath;
    bool isComputingAnnos{};
    CHIR::Package* package{nullptr};
    bool failure{false};
    std::set<std::string> dependencyPkg;

    // ======================== Imported Pkg Top-Level Decl Part ======================== //

    // See REG_IMPLICIT_IMPORTED_NON_GENERIC_FUNC for more details.
    std::unordered_set<Ptr<const AST::Decl>> implicitDecls{};
    // including imported global var and static var.
    std::vector<Ptr<const AST::Decl>> importedGlobalAndStaticVars{};
    // including imported global func and member func.
    std::vector<Ptr<const AST::Decl>> importedGlobalAndMemberFuncs{};
    // including imported nominal decl: ClassDecl、InterfaceDecl、StructDecl、EnumDecl.
    std::vector<Ptr<const AST::Decl>> importedNominalDecls{};

    // =============  instantiated in current pkg, who's generic definition is also in imported pkg============= //
    std::vector<Ptr<const AST::Decl>> importedGenericInstantiatedNominalDecls{};

    // ======================== Current Pkg Top-Level Decl Part ======================== //
    // including global VarDecl、global VarWithPatternDecl、static member VarDecl.
    std::vector<Ptr<const AST::Decl>> globalAndStaticVars{};
    /// during computeAnnotations, translate only the annoFactoryFuncs of these decls.
    /// These are decls that are not preserved when selecting const subpkg but have @!CustomAnnotations's.
    /// \See ComputeAnnotations.cpp
    std::vector<const AST::Decl*> annoOnlyDecls{};
    // including global FuncDecl、instantiated global FuncDecl、 MemberDecl(instance/static/open/abstract/instantiated).
    std::vector<Ptr<const AST::Decl>> globalAndMemberFuncs{};
    // including ClassDecl、InterfaceDecl、StructDecl、ExtendDecl、EnumDecl.
    std::vector<Ptr<const AST::Decl>> nominalDecls{};

    // Definition in other language, thus no body
    std::vector<Ptr<const AST::Decl>> foreignFuncs{};

    // ======================== Generic Top-Level Decl Part ======================== //

    // Collect all top level Geneirc nominalDecl: ClassLikeDecl、EnumDecl、StructDecl
    std::vector<Ptr<const AST::Decl>> genericNominalDecls{};

    // ======================== For Global Variable Initialization ======================== //

    // The static vars which are initialized in `static init func`.
    std::unordered_set<Ptr<const AST::Decl>> varsInitedByStaticInitFunc;
    // The info about `static init func`, static vars initialized in it and the parent custom type decl
    StaticInitInfoMap staticInitFuncInfoMap;

    // All funcs, global vars and static vars which need to analyze dependencies for variable initialization
    ElementList<Ptr<const AST::Decl>> funcsAndVars;

    // All local const vars which need to be lifted as global vars
    ElementList<Ptr<const AST::Decl>> localConstVars;
    ElementList<Ptr<const AST::FuncDecl>> localConstFuncs;

    // anno factory funcs
    std::vector<std::pair<const AST::Decl*, Func*>> annoFactoryFuncs;

    // File and its containing vars map. Note that the static vars initialized in `static init func`
    // are not included here
    std::unordered_map<Ptr<const AST::File>, std::vector<Ptr<const AST::Decl>>> fileAndVarMap;

    std::unordered_set<Ptr<const AST::Decl>> usedSrcImportedNonGenericDecls;
    /* Add fields for CJMP. */
    bool outputCHIR{false}; // Output type is CHIR
    bool mergingPlatform{false}; // Merging platform part over already compiled chir
    std::unordered_set<Ptr<const AST::Decl>> deserializedDecls; // decls which don't need to be retranslated.
    // Deserialized node cache table, key is identifier.
    std::unordered_map<std::string, CustomTypeDef*> deserializedDefs;
    std::unordered_map<std::string, Value*> deserializedVals;

    std::unordered_set<Func*> srcCodeImportedFuncs;
    std::unordered_set<GlobalVar*> srcCodeImportedVars;

    bool creatingLocalConstVarSignature{false};
};
} // namespace Cangjie::CHIR
#endif
