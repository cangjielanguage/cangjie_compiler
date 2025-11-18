// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the TypeChecker related classes, which provides typecheck capabilities.
 */

#ifndef CANGJIE_SEMA_TYPECHECKER_IMPL_H
#define CANGJIE_SEMA_TYPECHECKER_IMPL_H

#include <unordered_set>

#include "Promotion.h"
#include "ScopeManager.h"
#include "TypeCheckUtil.h"
#include "cangjie/AST/Clone.h"
#include "cangjie/AST/Symbol.h"
#include "cangjie/AST/Types.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Modules/ImportManager.h"
#include "cangjie/Sema/TypeChecker.h"
#include "cangjie/Sema/TypeManager.h"
#include "cangjie/Utils/ProfileRecorder.h"

namespace Cangjie {
class Synthesizer;

struct MatchingStat {
    uint32_t matchedArgs; /**< The count of matched arguments for current candidate. */
    bool argNameValid;    /**< Whether the checking of arguments' name passed. */
};

struct FunctionMatchingUnit {
public:
    int64_t id = -1;                         /**< The index in targets.*/
    AST::FuncDecl& fd;                       /**< Current unit's function, one of the candidates. */
    std::vector<Ptr<AST::Ty>> tysInArgOrder; /**< The parameters types in arguments order.*/
    SubstPack typeMapping;                   /**< For generic type comparison.*/
    std::pair<std::vector<Diagnostic>, MatchingStat> diags; /**< Stashed diag msg.*/
    CstVersionID ver; /**< ID to fetch constraints generated for this candidate.*/
    FunctionMatchingUnit(AST::FuncDecl& f, const std::vector<Ptr<AST::Ty>>& tys, const SubstPack& map) : fd(f)
    {
        tysInArgOrder = tys;
        typeMapping = map;
    }

    FunctionMatchingUnit(const FunctionMatchingUnit& fmu) = delete;
    FunctionMatchingUnit& operator=(const FunctionMatchingUnit& fmu) = delete;
    FunctionMatchingUnit(FunctionMatchingUnit&&) = default;
    FunctionMatchingUnit& operator=(FunctionMatchingUnit&&) = delete;
};

struct FunctionCandidate {
    AST::FuncDecl& fd; /**< The reference of candidate function declare. */
    AST::CallExpr& ce; /**< The reference of call expression. */
    std::vector<std::vector<Ptr<AST::Ty>>>
        argCombinations; /**< The combinations of call expression's arguments types. */
    MatchingStat stat;   /**< Checking status of current candidate */
};

struct ArgumentTypeUnit {
public:
    std::vector<Ptr<AST::Ty>> argTys;        /**< The arguments types in order.*/
    std::vector<Ptr<AST::Ty>> tysInArgOrder; /**< The parameters types in arguments order.*/
    AST::TyVarEnv assumptionCollection;      /**< For generic type comparison.*/
    ArgumentTypeUnit(const std::vector<Ptr<AST::Ty>>& tys, const std::vector<Ptr<AST::Ty>>& paramTys,
        const AST::TyVarEnv& assumption)
        : argTys(tys), tysInArgOrder(paramTys), assumptionCollection(assumption)
    {
    }
};

struct LookupInfo {
    Ptr<AST::Ty> baseTy = nullptr;
    Ptr<const AST::File> file = nullptr;
    bool lookupInherit = true;
    bool lookupExtend = true;
    bool isSetter = false;
};

struct CollectDeclsInfo {
    std::vector<Ptr<AST::FuncDecl>> funcDecls;
    std::vector<Ptr<AST::FuncDecl>> initFuncDecls;
    std::vector<Ptr<AST::Decl>> nonFuncDecls;
    std::vector<Ptr<AST::Decl>> staticFuncDecls;
};

enum class MatchResult {
    NONE, UNIQUE, AMBIGUOUS
};

class TypeChecker::TypeCheckerImpl {
public:
    explicit TypeCheckerImpl(CompilerInstance* ci);

    /**
     * Using control statement "for" to finish packages' typecheck. It invokes two functions as followed.
     * @see PrepareTypeCheck
     * @see TypeCheck
     */
    void TypeCheckForPackages(const std::vector<Ptr<AST::Package>>& pkgs)
    {
        auto contexts = PreTypeCheck(pkgs);
        DoTypeCheck(contexts);
        PostTypeCheck(contexts);
    }
    /**
     * Perform auto boxing and recursive type resolving of enum.
     */
    void PerformDesugarAfterInstantiation(ASTContext& ctx, AST::Package& pkg);

    // Desugar after sema.
    void PerformDesugarAfterSema(const std::vector<Ptr<AST::Package>>& pkgs);

    /**
     * Synthesize the given @p expr in given @p scopeName and return the found candidate decls or types.
     * If the @p hasLocal is true, the target will be found from local scope firstly.
     * @param ctx cached sema context.
     * @param scopeName the scopeName of current position.
     * @param expr the expression waiting to found candidate decls or types.
     * @param hasLocalDecl whether the given expression is existed in the given scope.
     * @return found candidate decls or types.
     */
    Candidate SynReferenceSeparately(ASTContext& ctx, const std::string& scopeName, AST::Expr& expr, bool hasLocalDecl);
    void RemoveTargetNotMeetExtendConstraint(const Ptr<AST::Ty> baseTy, std::vector<Ptr<AST::Decl>>& targets);

private:
    /**
     * Main entry of the synthesis mode of the type checking.
     */
    Ptr<AST::Ty> Synthesize(ASTContext& ctx, Ptr<AST::Node> node);
    bool SynthesizeAndReplaceIdealTy(ASTContext& ctx, AST::Node& node);
    /**
     * Main entry of the check mode of the type checking.
     */
    bool Check(ASTContext& ctx, Ptr<AST::Ty> target, Ptr<AST::Node> node);
    bool IsChecked(ASTContext& ctx, AST::Node& node) const;
    std::optional<bool> PerformBasicChecksForCheck(ASTContext& ctx, Ptr<AST::Ty> target, Ptr<AST::Node> node) const;
    /**
     * Cahched version of Synthesize and Check.
     *
     * Note that since cache key does not include any info from
     * ASTContext except for diags, this version must be used only at selected
     * places, such that the differences in ASTContext (notably, the types of Decls)
     * between two invokes with the same cache key won't affect the type check result.
     * Currently only for type check phase of overloading resolution.
     *
     * Once a change in AST or ASTContext could affect correctness of the cache,
     * the affected entries must be cleared. Currently, this could happen when allocating
     * new nodes during DesugarInTypeCheck, and checking lambda with omitted param type.
     */
    Ptr<AST::Ty> SynthesizeWithCache(ASTContext& ctx, Ptr<AST::Node> node);
    bool CheckWithCache(ASTContext& ctx, Ptr<AST::Ty> target, Ptr<AST::Node> node);
    /**
     * Cahched version of Synthesize and Check. But only cache failed results.
     * Only for resolving overloaded builtin operators,
     * because this procedure lacks a post-check phase and will stop on success,
     * thus the successful check will always need to fully execute.
     * */
    Ptr<AST::Ty> SynthesizeWithNegCache(ASTContext& ctx, Ptr<AST::Node> node);
    bool CheckWithNegCache(ASTContext& ctx, Ptr<AST::Ty> target, Ptr<AST::Node> node);
    /*
     * Use cached version of Synthesize and Check only when the same key was used
     * the last time this AST was fully checked, and not cleared ever since.
     * Also, it will NOT recover diags.
     * For function call post-check and any execution path that won't reach post-check.
     */
    Ptr<AST::Ty> SynthesizeWithEffectiveCache(ASTContext& ctx, Ptr<AST::Node> node, bool recoverDiag);
    bool CheckWithEffectiveCache(ASTContext& ctx, Ptr<AST::Ty> target, Ptr<AST::Node> node, bool recoverDiag);
    Ptr<AST::Ty> SynthesizeAndCache(ASTContext& ctx, Ptr<AST::Node> node, const AST::CacheKey& key);
    bool CheckAndCache(ASTContext& ctx, Ptr<AST::Ty> target, Ptr<AST::Node> node, const AST::CacheKey& key);
    /** ======== PreCheck related functions implemented in src/Sema/PreCheck.cpp. ======== */
    /**
     * Declaration redefinition check.
     * Note that declMap, which is used to look up an target declaration of a reference with known reference name and
     * scope name, is also built in this stage.
     */
    void CheckRedefinition(ASTContext& ctx);
    /**
     * Perform redefinition check for all declarations @p syms of current package @p ctx ,
     * invoked by CheckRedefinition.
     */
    void CheckRedefinitionInDeclHelper(ASTContext& ctx, std::vector<AST::Symbol*>& syms);
    /**
     * Perform collision with sub-package names for all toplevel declarations in package @p pkg ,
     * invoked by CheckRedefinition.
     */
    void CheckConflictDeclWithSubPackage(const AST::Package& pkg);
    /**
     * Perform redefinition check for one symbol. If the declaration corresponding to the symbol @p sym is not
     * collected in declMap, add it to the declMap. If there are declarations with same @p names ,
     * check whether redefinition happens according to the specification.
     */
    void CollectDeclMapAndCheckRedefinitionForOneSymbol(ASTContext& ctx, const AST::Symbol& sym, const Names& names);

    /**
     * A collection of Functions which get sematic Ty from given AST Type.
     * @param Node: the AST Type.
     * @param stage: the stage of the PreCheck, which controls the LookUp parameters during declaration or reference
     * name resolution.
     * @return Sematic Ty.
     */
    Ptr<AST::Ty> GetTyFromASTType(ASTContext& ctx, Ptr<AST::Node> type);
    Ptr<AST::Ty> GetTyFromASTType(ASTContext& ctx, AST::RefType& rt);
    Ptr<AST::Ty> GetTyFromASTType(ASTContext& ctx, AST::QualifiedType& qt);
    Ptr<AST::Ty> GetTyFromASTCFuncType(ASTContext& ctx, AST::RefType& rt);
    Ptr<AST::Ty> GetTyFromASTType(ASTContext& ctx, AST::VArrayType& varrayType);
    Ptr<AST::Ty> GetTyFromASTType(ASTContext& ctx, AST::TupleType& tupleType);
    Ptr<AST::Ty> GetTyFromASTType(ASTContext& ctx, AST::FuncType& funcType);
    Ptr<AST::Ty> GetTyFromASTType(ASTContext& ctx, AST::OptionType& optionType);
    std::vector<Ptr<AST::Ty>> GetTyFromASTType(ASTContext& ctx, std::vector<OwnedPtr<AST::Type>>& typeArguments);
    Ptr<AST::Ty> GetTyFromASTType(AST::Decl& decl, const std::vector<Ptr<AST::Ty>>& typeArgs);
    std::vector<Ptr<AST::Ty>> GetTyFromASTType(const std::vector<OwnedPtr<AST::GenericParamDecl>>& typeParameters);
    Ptr<AST::Ty> GetTyFromBuiltinDecl(const AST::BuiltInDecl& bid, const std::vector<Ptr<AST::Ty>>& typeArgs);
    Ptr<AST::Ty> GetBuiltInArrayType(const std::vector<Ptr<AST::Ty>>& typeArgs);
    Ptr<AST::Ty> GetBuiltInVArrayType(const std::vector<Ptr<AST::Ty>>& typeArgs);
    Ptr<AST::Ty> GetBuiltinCFuncType(const std::vector<Ptr<AST::Ty>>& typeArgs);
    Ptr<AST::Ty> GetBuiltInPointerType(const std::vector<Ptr<AST::Ty>>& typeArgs);

    /** Check legality of attributes for all decls. */
    void CheckAllDeclAttributes(const ASTContext& ctx);
    /**
     * Set the semantic Ty for a declaration @p decl directly by its ASTKind, name and typeArguments.
     */
    void SetDeclTy(AST::Decl& decl);
    /**
     * Set the semantic Ty for a TypeAliasDecl @p tad and its aliased Ty.
     */
    void SetTypeAliasDeclTy(ASTContext& ctx, AST::TypeAliasDecl& tad);
    /**
     * 1.Set declaration's semantic types only by its ASTKind, name and type arguments.
     * 2.Check declaration's attributes.
     * @param ctx type Checker context.
     * @param root the root node of AST.
     */
    void ResolveDecls(ASTContext& ctx);
    void ResolveTypeAlias(const std::vector<Ptr<ASTContext>>& contexts);
    /**
     * 1.Set declaration's semantic types only by its ASTKind, name and type arguments.
     * 2.Check declaration's attributes.
     * @param ctx type Checker context.
     * @param decl the declaration to be resolved.
     */
    void ResolveOneDecl(ASTContext& ctx, AST::Decl& decl);
    /**
     * Resolve all reference types' semantic type according to its name and its type arguments, more over,
     * substituting its ty when it is an aliased one.
     * @param ctx type Checker context.
     */
    void ResolveNames(ASTContext& ctx);
    void SubstituteTypeAliasForAlias(AST::TypeAliasDecl& tad);
    /**
     * Resolve all reference types' semantic type according to its name and its type arguments, more over,
     * substituting its ty when it is an aliased one.
     * @param ctx type Checker context.
     * @param root the reference type.
     */
    void SetTypeTy(ASTContext& ctx, AST::Type& type);
    /**
     * Cause of circular dependence between two or more packages,
     * firstly, build symbol table for all packages and typecheck redefinition except functions.
     */
    void PrepareTypeCheck(ASTContext& ctx, AST::Package& pkg);
    /**
     * Collect mem2Decls map in ASTContext for quick lookup during type check
     */
    void CollectDeclsWithMember(Ptr<AST::Package> pkg, ASTContext& ctx);
    /**
     * The main Entry of PreCheck.
     * @param contexts the context of source packages to be checked.
     */
    void PreCheck(const std::vector<Ptr<ASTContext>>& contexts);
    void PreCheckUsage(ASTContext& ctx, const AST::Package& pkg);
    void PreCheckInvalidInherit(const ASTContext& ctx, const AST::Package& pkg);
    /**
     * Check if there is a TypeAlias circle after all Decls are resolved.
     * @param ctx type Checker context.
     */
    void TypeAliasCircleCheck(const ASTContext& ctx);
    /**
     * Check if the TypeAliasDecl @p tad is in type alias circle,
     * if so, add it into the circle path queue @p path.
     */
    void CheckTypeAliasCycleForOneDecl(AST::TypeAliasDecl& tad, std::deque<Ptr<AST::Decl>>& path);
    /**
     * Check if the typeArguments @p typeArgs is in type alias circle,
     * if so, add it into the circle path queue @p path.
     */
    void CheckTypeAliasCycleForTypeArgsRecursively(
        std::vector<Ptr<AST::Type>>& typeArgs, std::deque<Ptr<AST::Decl>>& path);
    /**
     * Check if a reference type @p type is in type alias circle,
     * if so, add it into the circle path queue @p path.
     */
    void CheckTypeAliasCycleForOneType(AST::Type& type, std::deque<Ptr<AST::Decl>>& path);
    /**
     * Check if there is type inheritance cycle after all Decls are resolved.
     * @param ctx type Checker context.
     */
    void StructDeclCircleOrDupCheck(ASTContext& ctx);
    template <typename T>
    void CheckInheritanceCycleHelper(ASTContext& ctx, const AST::Decl& decl, const Cangjie::AST::Type& te,
        std::deque<Ptr<AST::Decl>>& path, Ptr<AST::ExtendDecl> extendDecl = nullptr);
    /**
     * Check whether the @p root exists cyclic dependency.
     */
    void CheckInheritanceCycleDFS(
        ASTContext& ctx, AST::Decl& decl, std::deque<Ptr<AST::Decl>>& path, Ptr<AST::ExtendDecl> extendDecl = nullptr);
    void CheckInheritanceCycleWithExtend(
        ASTContext& ctx, const AST::InheritableDecl& decl, std::deque<Ptr<AST::Decl>>& path);
    /**
     * Check if the structure declaration corresponding to the symbol @p sym is in inheritance cycle.
     */
    void StructDeclCircleOrDupCheckForOneSymbol(ASTContext& ctx, const AST::Symbol& sym);
    /**
     * Check if the structure declaration corresponding to the symbol @p sym is in inheritance cycle.
     */
    void CheckInheritanceCycleDFSHandleVisiting(
        const AST::Decl& decl, const std::deque<Ptr<AST::Decl>>& path, Ptr<const AST::ExtendDecl> extendDecl);
    /**
     * Collect assumption for all generic Decls and check constraints sanity.
     * @param ctx type Checker context.
     */
    void CollectAndCheckAssumption(ASTContext& ctx);
    /**
     * Collect assumption for all generic Decls and check constraints sanity for one generic Decl @p decl.
     */
    void CollectAssumption(ASTContext& ctx, const AST::Decl& decl);
    /**
     * Check all upper bound legality.
     */
    bool CheckUpperBoundsLegality(const AST::Generic& generic);
    /**
     * Check upper bound legality recursively.
     */
    bool CheckUpperBoundsLegalityRecursively(const AST::Ty& upper);
    /**
     * Check assumption legality for a Decl @p decl including:
     * 1. Generic constraints check.
     * 2. Generic upper bounds legality check.
     * 3. Generic assumption sanity check.
     */
    void CheckAssumption(ASTContext& ctx, const AST::Decl& decl);
    /**
     * Expose generic upper bounds transitively for generic constraints in @p generic.
     * For example, if there are constraints: where T <: U, U <: Int32, T will also have constraint T <: Int32 by
     * exposing to U.
     */
    void ExposeGenericUpperBounds(ASTContext& ctx, const AST::Generic& generic) const;
    /**
     * Perform assumption sanity check for generic assumptions in @p generic.
     */
    void AssumptionSanityCheck(const AST::Generic& generic);
    /**
     * Perform assumption sanity check for one generic ty @p genericTy whose assumptions is saved in its upperBounds.
     */
    void SanityCheckForOneGenericTy(AST::GenericsTy& genericTy);
    /**
     * Assumption sanity check Rule 1: There can be no recursive constraint if the upper bound of
     * a type argument is a class irrelevant type.
     */
    bool ValidRecursiveConstraintCheck(const AST::Generic& generic);
    /**
     * Assumption sanity check Rule 3:
     * Class irrelevant and classlike relevant types cannot exist at same time in the upper bound.
     */
    bool SanityCheckForVarietyTypesOfUpperBounds(AST::GenericsTy& genericTy,
        const std::set<Ptr<AST::Ty>>& classIrrelevantUpperBounds, const std::set<Ptr<AST::Ty>>& classlikeUpperBounds);
    /**
     * Assumption sanity check Rule 4: If there are multiple classes, they must be in one inheritance chain.
     */
    void SanityCheckForClassUpperBounds(AST::GenericsTy& genericTy, const std::set<Ptr<AST::Ty>>& classUpperBounds);
    /**
     * Check and reduce upper bounds where there are upper bounds in same inheritance chain,
     * using their maximum common child type.
     */
    bool CheckAndReduceUpperBounds(AST::GenericsTy& genericTy, const std::set<Ptr<AST::Ty>>& upperBounds);
    /**
     * Add assumption for all type alias declarations.
     */
    void IgnoreAssumptionForTypeAliasDecls(const ASTContext& ctx) const;
    void AddAssumptionForExtendDecls(ASTContext& ctx);
    /**
     * Add assumption for type according to its type's assumption.
     * For type alias A<T1..Tn> = C<a1..am>. C and a1..an may have constraints and
     * related constraints must be imposed on T1..Tn to make type check on C pass.
     * For achieving this, we do:
     * 1. Construct a reversed map (revTypeMapping) from T1..Tn to their constraints declared in C<a1..am>
     * by applying GetRevTypeMapping.
     * 2. Construct all assumption collection map (allAssumptionMap) following subtype constraints
     * 3. Construct a type arguments applied map (typeArgAppliedMap) from C<U1..Um> to C<a1..am>
     * 4. For each type in T1..Tn, find revTypeMapping[Ti], this get Ti's correspond type parameter
     * 5. Get its constraints from allAssumptionMap[Pi]. This may contains generic arguments that are not declared
     * in type alias, so we need to substitute them by typeArgAppliedMap.
     * 6. Add the constraints to type alias.
     */
    void AddAssumptionForType(ASTContext& ctx, const AST::Type& type, AST::Generic& generic);
    /**
     * Add assumption to the upperBound of generic parameters' ty of generic declarations @p generic according to
     * its aliased type @p aliasedTypeTarget's assumption.
     */
    void AddUpperBoundOnTypeParameters(ASTContext& ctx, const AST::Generic& generic, const AST::Decl& typeTarget,
        MultiTypeSubst& revTypeMapping, TyVarUB& allAssumptionMap, const TypeSubst& typeArgAppliedMap);
    void GetAllAssumptions(AST::TyVarEnv& source, AST::TyVarEnv& newMap);
    /**
     * Add Object to all ClassDecls' inheritedTypes if there is no one.
     */
    void AddSuperClassObjectForClassDecl(ASTContext& ctx);
    void CheckAndAddSubDecls(
        const AST::Type& type, AST::ClassDecl& cd, bool& hasSuperClass, int& superClassLikeNum) const;
    bool HasSuperClass(AST::ClassDecl& cd) const;
    /**
     * If a class doesn't have super class, set Core.Object class as its super class.
     * NOTICE: it will change AST Node!
     */
    void AddObjectSuperClass(ASTContext& ctx, AST::ClassDecl& cd);
    void AddDefaultSuperCall(const AST::FuncBody& funcBody) const;
    /**
     * If there is no default constructor, insert one.
     * NOTICE: it will change AST Node!
     */
    void AddDefaultCtor(AST::InheritableDecl& decl) const;
    void AddDefaultFunction(AST::Node& root);
    /**
     * Check function redefinition at PreCheck stage for the scenario including:
     * 1. Functions in the Enum.
     * 2. Functions are Foreign.
     * 3. Static and non-static duplications in class-like context.
     * 4. Same signature.
     */
    void PreCheckFuncRedefinition(const ASTContext& ctx);
    /**
     * Check function redefinition at PreCheck stage for the functions in the Enum.
     */
    void PreCheckMacroRedefinition(const std::vector<Ptr<AST::FuncDecl>>& funcs) const;
    void PreCheckFuncRedefinitionForEnum(const std::vector<Ptr<AST::FuncDecl>>& funcs);
    /**
     * Check function redefinition at PreCheck stage for the functions related to FFI feature.
     * For C FFI, this check includes foreign func and @C func.
     * NOTE: C FFI FFI cannot be used at the same time currently.
     */
    void PreCheckFuncRedefinitionForFFI(const std::vector<Ptr<AST::FuncDecl>>& funcs);
    /**
     * Check function redefinition at PreCheck stage for the static and non-static duplications as member decl.
     */
    void PreCheckFuncStaticConflict(const std::vector<Ptr<AST::FuncDecl>>& funcs);
    /**
     * Check function redefinition at PreCheck stage for the functions with same signature.
     */
    bool PreCheckFuncRedefinitionWithSameSignature(std::vector<Ptr<AST::FuncDecl>> funcs, bool needReportErr = true);

    void PreCheckExtend(ASTContext& ctx, AST::ExtendDecl& ed);
    void PreCheckAllExtendInterface();
    void CheckSpecializationExtend(const AST::InheritableDecl& extendedDecl, const AST::ExtendDecl& extendDecl,
        const std::set<Ptr<AST::ExtendDecl>, AST::CmpNodeByPos> otherExtendDecls);
    void CheckSpecializationExtendDupImstantation(const AST::Ty& extendedDeclTy, const AST::ExtendDecl& compareExtend,
        const AST::InheritableDecl& beComparedDecl, const TypeSubst& instantMapping, const bool checkParent = false);
    void BuildImportedExtendMap();
    void BuildExtendMap(ASTContext& ctx);
    void CheckExtendRules(const ASTContext& ctx);
    /* Set integer overflow strategy before sema typechecking. */
    void SetIntegerOverflowStrategy() const;
    void CheckDefaultParamFuncsEntry(AST::File& file);
    AST::VisitAction CheckDefaultParamFunc(AST::StructDecl& sd);
    AST::VisitAction CheckDefaultParamFunc(AST::ClassDecl& cd, const AST::File& file);
    AST::VisitAction CheckDefaultParamFunc(const AST::InterfaceDecl& ifd) const;
    void GetSingleParamFunc(AST::Decl& decl);
    void CheckPrimaryCtorForClassOrStruct(AST::InheritableDecl& id, const std::string& typeName);
    /**
     * ReturnExpr must in FuncBody and JumpExpr must in LoopExpr.
     */
    void CheckReturnAndJump(const ASTContext& ctx);
    void SetEnumEleTy(AST::Decl& constructor);
    void SetEnumEleTyHandleFuncDecl(AST::FuncDecl& funcDecl);
    void PreSetDeclType(const ASTContext& ctx);
    void ReplaceThisTypeInFunc(const AST::FuncDecl& funcDecl);

    /**
     * Substitute typeAlias typeArguments, and return function targets for a reference node.
     */
    Ptr<AST::Decl> GetRealTarget(Ptr<AST::Expr> const node, Ptr<AST::Decl> const target);

    /**
     * Search target by namespace(like 'class', 'interface', 'struct', 'enum') and field name.
     */
    std::vector<Ptr<AST::Decl>> FieldLookup(
        const ASTContext& ctx, Ptr<AST::Decl> decl, const std::string& fieldName, const LookupInfo& info = {});

    std::vector<Ptr<ASTContext>> PreTypeCheck(const std::vector<Ptr<AST::Package>>& pkgs);
    void DoTypeCheck(std::vector<Ptr<ASTContext>>& contexts)
    {
        Utils::ProfileRecorder recorder("Semantic", "In TypeCheck");
        // Type checking to add semantic type values.
        for (auto& ctx : contexts) {
            TypeCheck(*ctx, *ctx->curPackage);
        }
    }
    void PostTypeCheck(std::vector<Ptr<ASTContext>>& contexts);
    /**
     * Cause of circular dependence between two or more packages,
     * secondly, typecheck function redefinition and variable initialization.
     */
    void TypeCheck(ASTContext& ctx, AST::Package& pkg);
    void TypeCheckTopLevelDecl(ASTContext& ctx, AST::Decl& decl);
    void TypeCheckImportedGenericMember(ASTContext& ctx);
    void CheckOverflow(AST::Node& node);
    void CheckWhetherHasProgramEntry();

    /**
     * Desugar APIs during sema check.
     */
    void DesugarArrayCall(ASTContext& ctx, AST::CallExpr& ce);
    void DesugarPointerCall(ASTContext& ctx, AST::CallExpr& ce);
    /** Desugar 'propDecl' after typecheck but still inside 'Sema' stage. Keep this order for cjLint usage. */
    void DesugarForPropDecl(AST::Node& pkg);
    /** Desugar for string interpolation expr. */
    std::vector<Ptr<AST::Decl>> MatchToStringImpl(const ASTContext& ctx, const AST::File& file, AST::Ty& ty);
    OwnedPtr<AST::CallExpr> DesugarStrPartExpr(
        const ASTContext& ctx, AST::Expr& expr, const std::vector<Ptr<AST::Decl>> appendDecls, AST::VarDecl& sbItem);
    void DesugarStrInterpolationExpr(ASTContext& ctx, AST::LitConstExpr& litConstExpr);
    /**
     * Desugar APIs after sema check.
     */
    void PerformDesugarAfterTypeCheck(ASTContext& ctx, AST::Package& pkg);
    void GenerateMainInvoke();
    void TryDesugarForCoalescing(AST::Node& root) const;
    void DesugarForCoalescing(AST::BinaryExpr& binaryExpr) const;
    void DesugarForInExpr(ASTContext& ctx, AST::ForInExpr& forInExpr);
    void DesugarForInCloseRange(ASTContext& ctx, AST::ForInExpr& forInExpr);
    void DesugarForInNonCloseRange(ASTContext& ctx, AST::ForInExpr& forInExpr);
    void DesugarForInIter(ASTContext& ctx, AST::ForInExpr& forInExpr);
    void DesugarForInString(ASTContext& ctx, AST::ForInExpr& forInExpr);
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    void ReArrangeForInExpr(ASTContext& ctx, AST::ForInExpr& forInExpr);
#endif
    void ReArrangeForInRangeExpr(ASTContext& ctx, AST::ForInExpr& forInExpr);
    void ReArrangeForInIterExpr(ASTContext& ctx, AST::ForInExpr& forInExpr);
    void ReArrangeForInStringExpr(ASTContext& ctx, AST::ForInExpr& forInExpr);
    void DesugarTryWithResourcesExpr(ASTContext& ctx, AST::TryExpr& te);

    OwnedPtr<AST::Expr> ConstructOptionMatch(OwnedPtr<AST::Expr> selector, OwnedPtr<AST::Block> someExpr,
        OwnedPtr<AST::Block> otherExpr, AST::RefExpr& someVar, Ptr<AST::Ty> someTy) const;

    void DesugarTryToFrame(ASTContext& ctx, AST::TryExpr& te);
    void DesugarPerform(ASTContext& ctx, AST::PerformExpr& pe);
    void DesugarResume(ASTContext& ctx, AST::ResumeExpr& re);
    void DesugarImmediateResume(ASTContext& ctx, AST::ResumeExpr& re);
    OwnedPtr<AST::Expr> GetHelperFrameMethod(
        AST::Node& base, const std::string& methodName, std::vector<Ptr<AST::Ty>> typeArgs);
    void CreateResult(
        ASTContext& ctx, const AST::TryExpr& te, AST::VarDecl& frame, std::vector<OwnedPtr<AST::Node>>& block);
    void CreateSetHandler(
        ASTContext& ctx, AST::TryExpr& te, AST::VarDecl& frame, std::vector<OwnedPtr<AST::Node>>& block);
    void CreateSetFinally(
        ASTContext& ctx, AST::TryExpr& te, AST::VarDecl& frame, std::vector<OwnedPtr<AST::Node>>& block);
    AST::VarDecl& CreateFrame(ASTContext& ctx, AST::TryExpr& te, std::vector<OwnedPtr<AST::Node>>& block);
    void EncloseTryLambda(ASTContext& ctx, OwnedPtr<AST::LambdaExpr>& tryLambda);

    /* Synthesize specialized for desugar after sema. Will not recover previous desugar results */
    Ptr<AST::Ty> SynthesizeWithoutRecover(ASTContext& ctx, Ptr<AST::Node> node);
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    void PerformToAnyInsertion(AST::Package& pkg);
    OwnedPtr<AST::FuncDecl> CreateToAny(AST::Decl& outerDecl);
#endif

    void DesugarJArrayCtorCall(const ASTContext& ctx, AST::CallExpr& ce);
    void DesugarTokenCallExpr(ASTContext& ctx, AST::CallExpr& ce);
    void DesugarSpawnExpr(const ASTContext& ctx, AST::SpawnExpr& se);
    void DesugarSpawnArgExpr(const ASTContext& ctx, const AST::SpawnExpr& se);

    /** Get decls with symbol by context searcher. */
    std::vector<AST::Symbol*> GetToplevelDecls(const ASTContext& ctx) const;
    std::vector<AST::Symbol*> GetAllDecls(const ASTContext& ctx) const;
    std::vector<AST::Symbol*> GetGenericCandidates(const ASTContext& ctx) const;
    std::vector<AST::Symbol*> GetAllStructDecls(const ASTContext& ctx) const;

    void WarmupCache(const ASTContext& ctx) const;

    std::vector<AST::Symbol*> GetSymsByASTKind(
        const ASTContext& ctx, AST::ASTKind astKind, const Order& order = Sort::posDesc) const;

    /**
     * Get the target members for the extends of @param ty.
     */
    std::vector<Ptr<AST::Decl>> ExtendFieldLookup(
        const ASTContext& ctx, const AST::File& file, Ptr<AST::Ty> ty, const std::string& fieldName);
    /**
     * Find the symbols from name and scope name.
     * @param ctx: the current ASTContext.
     * @param name: the name of the symbol.
     * @param scopeName: the scopeName of the symbol.
     * @param node: the astNode of symbol.
     */
    std::vector<Ptr<AST::Decl>> Lookup(const ASTContext& ctx, const std::string& name, const std::string& scopeName,
        const AST::Node& node, bool isSetter = false);
    std::vector<Ptr<AST::Decl>> LookupTopLevel(const ASTContext& ctx, const std::string& name,
        const std::string& scopeName, const AST::Node& node, bool isSetter = false);

    /**
     * Analyze the linkage of functions and mark them as internal if possible.
     */
    void AnalyzeFunctionLinkage(AST::Package& pkg) const;

    void CheckSealedInheritance(const AST::Decl& child, const AST::Type& parent);
    void CheckThreadContextInheritance(const AST::Decl& decl, const AST::Type& parent);
    void CheckClassDecl(ASTContext& ctx, AST::ClassDecl& cd);
    void CheckInterfaceDecl(ASTContext& ctx, AST::InterfaceDecl& id);
    void CheckStructDecl(ASTContext& ctx, AST::StructDecl& sd);
    void BuildImportedEnumConstructorMap(ASTContext& ctx);
    void BuildEnumConstructorMap(ASTContext& ctx) const;
    void CheckEnumDecl(ASTContext& ctx, AST::EnumDecl& ed);
    void TypeCheckCompositeBody(
        ASTContext& ctx, const AST::Decl& structDecl, const std::vector<OwnedPtr<AST::Decl>>& body);
    void CheckVarWithPatternDecl(ASTContext& ctx, AST::VarWithPatternDecl& vpd);
    // T should be VarWithPatternDecl or VarDecl. A way to avoid restructuring the AST.h file.
    template <typename T> void SynchronizeTypeAndInitializer(ASTContext& ctx, T& vd);

    void CheckVarDecl(ASTContext& ctx, AST::VarDecl& vd);
    void CheckPropDecl(ASTContext& ctx, AST::PropDecl& pd);
    void UpdateMemberVariableTy(const AST::Decl& decl, const AST::EnumTy& eTy);

    Ptr<AST::Ty> SynBlock(ASTContext& ctx, AST::Block& b);
    bool ChkBlock(ASTContext& ctx, AST::Ty& target, AST::Block& b);
    /**
     * Arithmetic operator { +, -, *, /, ** }
     */
    std::optional<Ptr<AST::Ty>> SynArithmeticOrRelationalExpr(
        ASTContext& ctx, AST::BinaryExpr& be, bool isArithmetic = true);
    Ptr<AST::Ty> SynLogicalExpr(ASTContext& ctx, AST::BinaryExpr& be);
    Ptr<AST::Ty> SynShiftExpr(ASTContext& ctx, AST::BinaryExpr& be);
    Ptr<AST::Ty> SynFlowExpr(ASTContext& ctx, AST::BinaryExpr& be);
    Ptr<AST::Ty> SynCoalescingExpr(ASTContext& ctx, AST::BinaryExpr& be);
    bool ChkArithmeticExpr(ASTContext& ctx, AST::Ty& target, AST::BinaryExpr& be);
    // Check exponentiation expressions with target types.
    bool ChkExpoExpr(ASTContext& ctx, AST::Ty& tgtTy, AST::BinaryExpr& be);
    bool ChkExpoExprBase(ASTContext& ctx, const AST::BinaryExpr& be, AST::Ty& baseTy);
    bool ChkExpoExprExponent(ASTContext& ctx, AST::Expr& exponent, std::vector<Ptr<AST::Ty>> exTys);
    bool CheckExponentByBaseTy(ASTContext& ctx, AST::Ty& baseTy, const AST::Expr& base, AST::Expr& exponent);
    Ptr<AST::Ty> SynExpoExpr(ASTContext& ctx, AST::BinaryExpr& be);
    bool ChkLogicalExpr(ASTContext& ctx, AST::Ty& target, AST::BinaryExpr& be);
    bool ChkRelationalExpr(ASTContext& ctx, AST::Ty& target, AST::BinaryExpr& be);
    bool ChkShiftExpr(ASTContext& ctx, AST::Ty& target, AST::BinaryExpr& be);
    bool ChkCoalescingExpr(ASTContext& ctx, Ptr<AST::Ty> tgtTy, AST::BinaryExpr& be);
    bool IsCoalescingLeftTyValid(AST::Ty& ty) const;
    /**
     * Try to find a unique Ty that can constrain the ty var.
     * The new constraint will be added only if the result is UNIQUE.
     */
    MatchResult PickConstaintFromTys(TyVar& tv, std::set<Ptr<AST::Ty>> tys, bool isUB);
    MatchResult PickConstaintFromTys(
        AST::Ty& tv1, AST::Ty& tv2, std::set<std::pair<Ptr<AST::Ty>, Ptr<AST::Ty>>> tys, bool isUB);
    std::optional<Ptr<AST::Ty>> PerformBasicChecksForSynthesize(ASTContext& ctx, Ptr<AST::Node> node) const;

    /**
     * Check if the compiler can add a built-in == (or !=) for these two tuple types.
     * If and only if all element types of two tuple types supports == (or !=),
     * the compiler provides a built-in == (or !=) for these two tuple types.
     */
    bool CheckTupleCanEqual(ASTContext& ctx, AST::BinaryExpr& be);
    bool ChkFlowExpr(ASTContext& ctx, Ptr<AST::Ty> target, AST::BinaryExpr& be);
    bool CheckFlowOperandsHaveNamedParam(const AST::CallExpr& ce);
    void DiagnoseForBinaryExpr(ASTContext& ctx, AST::BinaryExpr& be);
    void DiagnoseForUnaryExpr(ASTContext& ctx, AST::UnaryExpr& ue);
    void DiagnoseForUnaryExprWithTarget(ASTContext& ctx, AST::UnaryExpr& ue, AST::Ty& target);
    bool ChkBinaryExpr(ASTContext& ctx, AST::Ty& target, AST::BinaryExpr& be);
    Ptr<AST::Ty> SynBinaryExpr(ASTContext& ctx, AST::BinaryExpr& be);
    Ptr<AST::Ty> SynIncOrDecExpr(ASTContext& ctx, AST::IncOrDecExpr& ide);
    bool ChkIncOrDecExpr(ASTContext& ctx, AST::Ty& target, AST::IncOrDecExpr& ide);
    Ptr<AST::Ty> SynTypeConvExpr(ASTContext& ctx, AST::TypeConvExpr& tce);
    Ptr<AST::Ty> SynNumTypeConvExpr(AST::TypeConvExpr& tce);
    bool ChkCFuncConstructorExpr(ASTContext& ctx, AST::CallExpr& ce);
    bool ChkTypeConvExpr(ASTContext& ctx, AST::Ty& targetTy, AST::TypeConvExpr& tce);
    Ptr<AST::Ty> SynLoopControlExpr(const ASTContext& ctx, AST::JumpExpr& je) const;
    bool ChkLoopControlExpr(const ASTContext& ctx, AST::JumpExpr& je) const;
    Ptr<AST::Ty> SynLamExpr(ASTContext& ctx, AST::LambdaExpr& le);
    bool SolveLamExprParamTys(ASTContext& ctx, AST::LambdaExpr& le);
    void ResetLambdaForReinfer(ASTContext& ctx, const AST::LambdaExpr& le);
    void TryInferFromSyntaxInfo(ASTContext& ctx, const AST::LambdaExpr& le);
    bool ChkLamExpr(ASTContext& ctx, AST::Ty& target, AST::LambdaExpr& le);
    bool ChkLamParamTys(ASTContext& ctx, AST::LambdaExpr& le, const std::vector<Ptr<AST::Ty>>& tgtParamTys,
        std::vector<Ptr<AST::Ty>>& lamParamTys);
    bool ChkLamBody(ASTContext& ctx, AST::FuncBody& lamFb);
    Ptr<AST::Ty> SynIfExpr(ASTContext& ctx, AST::IfExpr& ie);
    bool ChkIfExpr(ASTContext& ctx, AST::Ty& tgtTy, AST::IfExpr& ie);
    Ptr<AST::Ty> ReplaceThisTy(Ptr<AST::Ty> now);
    bool ChkIfExprNoElse(ASTContext& ctx, AST::Ty& target, AST::IfExpr& ie);
    bool ChkIfExprTwoBranches(ASTContext& ctx, AST::Ty& target, AST::IfExpr& ie);
    /// Check and diagnose conditions in if and while
    /// The target type is necessarily Bool and thus omitted.
    bool CheckCondition(ASTContext& ctx, AST::Expr& e, bool suppressIntroducingVariableError);
    bool CheckBinaryCondition(ASTContext& ctx, AST::BinaryExpr& e, bool suppressIntroducingVariableError);

    bool SynLetPatternDestructor(
        ASTContext& ctx, AST::LetPatternDestructor& lpd, bool suppressIntroducingVariableError);
    std::optional<Ptr<AST::ClassTy>> PromoteToCommandTy(const AST::Node& cause, AST::Ty& cmdTy);
    Ptr<AST::Ty> SynThrowExpr(ASTContext& ctx, AST::ThrowExpr& te);
    Ptr<AST::Ty> SynPerformExpr(ASTContext& ctx, AST::PerformExpr& pe);
    Ptr<AST::Ty> SynResumeExpr(ASTContext& ctx, AST::ResumeExpr& re);
    Ptr<AST::Ty> SynTryExpr(ASTContext& ctx, AST::TryExpr& te);
    Ptr<AST::Ty> SynTryWithResourcesExpr(ASTContext& ctx, AST::TryExpr& te);
    std::optional<Ptr<AST::Ty>> SynTryExprCatchesAndHandles(ASTContext& ctx, AST::TryExpr& te);
    bool SynHandler(ASTContext& ctx, AST::Handler& handler, Ptr<AST::Ty> tgtTy, AST::TryExpr& te);
    bool ChkTryExpr(ASTContext& ctx, AST::Ty& tgtTy, AST::TryExpr& te);
    bool ChkTryExprCatchesAndHandles(ASTContext& ctx, AST::Ty& tgtTy, AST::TryExpr& te);
    bool ChkTryExprCatchPatterns(ASTContext& ctx, AST::TryExpr& te);
    bool ChkTryExprHandlePatterns(ASTContext& ctx, AST::TryExpr& te);
    bool ChkHandler(ASTContext& ctx, AST::Handler& handler, AST::Ty& tgtTy);
    bool ValidateBlockInTryHandle(AST::Block& block);
    bool ValidateHandler(AST::Handler& h);
    bool ChkTryExprFinallyBlock(ASTContext& ctx, const AST::TryExpr& te);
    bool ChkQuoteExpr(ASTContext& ctx, AST::Ty& target, AST::QuoteExpr& qe);
    Ptr<AST::Ty> SynQuoteExpr(ASTContext& ctx, AST::QuoteExpr& qe);
    Ptr<AST::Ty> SynUnaryExpr(ASTContext& ctx, AST::UnaryExpr& ue);
    bool ChkUnaryExpr(ASTContext& ctx, AST::Ty& target, AST::UnaryExpr& ue);
    Ptr<AST::Ty> SynBuiltinUnaryExpr(ASTContext& ctx, AST::UnaryExpr& ue);
    Ptr<AST::Ty> SynParenExpr(ASTContext& ctx, AST::ParenExpr& pe);
    bool ChkParenExpr(ASTContext& ctx, AST::Ty& target, AST::ParenExpr& pe);
    Ptr<AST::Ty> SynAssignExpr(ASTContext& ctx, AST::AssignExpr& ae);
    Ptr<AST::Ty> SynMultipleAssignExpr(ASTContext& ctx, AST::AssignExpr& ae);
    bool ChkAssignExpr(ASTContext& ctx, AST::Ty& target, AST::AssignExpr& ae);
    bool IsAssignable(AST::Expr& e, bool isCompound, const std::vector<Diagnostic>& diags) const;
    bool IsShiftAssignValid(const AST::AssignExpr& ae);
    Ptr<AST::Ty> SynLitConstExpr(ASTContext& ctx, AST::LitConstExpr& lce);
    Ptr<AST::Ty> SynLitConstStringExpr(ASTContext& ctx, AST::LitConstExpr& lce);
    bool ChkLitConstExpr(ASTContext& ctx, AST::Ty& target, AST::LitConstExpr& lce);
    bool ChkLitConstExprOfTypeBool(AST::Ty& target, AST::LitConstExpr& lce);
    bool ChkLitConstExprOfTypeUnit(AST::Ty& target, AST::LitConstExpr& lce);
    bool ChkLitConstExprOfTypeInteger(AST::Ty& target, AST::LitConstExpr& lce);
    bool ChkLitConstExprOfTypeFloat(AST::Ty& targetTy, AST::LitConstExpr& lce);
    bool ChkLitConstExprOfTypeChar(AST::Ty& targetTy, AST::LitConstExpr& lce);
    bool ChkLitConstExprOfTypeString(ASTContext& ctx, AST::Ty& target, AST::LitConstExpr& lce);
    Ptr<AST::Ty> SynWhileExpr(ASTContext& ctx, AST::WhileExpr& we);
    bool ChkWhileExpr(ASTContext& ctx, AST::Ty& target, AST::WhileExpr& we);
    Ptr<AST::Ty> SynDoWhileExpr(ASTContext& ctx, AST::DoWhileExpr& dwe);
    bool ChkDoWhileExpr(ASTContext& ctx, AST::Ty& target, AST::DoWhileExpr& dwe);
    Ptr<AST::Ty> SynTupleLit(ASTContext& ctx, AST::TupleLit& tl);
    bool ChkTupleLit(ASTContext& ctx, AST::Ty& target, AST::TupleLit& tl);
    Ptr<AST::Ty> SynReturnExpr(ASTContext& ctx, AST::ReturnExpr& re);
    bool ChkReturnExpr(ASTContext& ctx, AST::ReturnExpr& re);
    bool CheckReturnInConstructors(ASTContext& ctx, const AST::ReturnExpr& re);
    Ptr<AST::Ty> SynFuncArg(ASTContext& ctx, AST::FuncArg& fa);
    bool ChkFuncArg(ASTContext& ctx, AST::Ty& target, AST::FuncArg& fa);
    bool ChkFuncArgWithInout(ASTContext& ctx, AST::Ty& target, AST::FuncArg& fa);
    bool ChkInoutFuncArg(const AST::FuncArg& fa);
    bool ChkInoutRefExpr(AST::RefExpr& re, bool isBase = false);
    bool ChkInoutMemberAccess(const AST::MemberAccess& ma);
    Ptr<AST::Ty> SynFuncParam(ASTContext& ctx, AST::FuncParam& fp);
    bool ChkFuncParam(ASTContext& ctx, AST::Ty& target, AST::FuncParam& fp);
    Ptr<AST::Ty> SynIsExpr(ASTContext& ctx, AST::IsExpr& ie);
    bool ChkIsExpr(ASTContext& ctx, AST::Ty& target, AST::IsExpr& ie);
    Ptr<AST::Ty> SynAsExpr(ASTContext& ctx, AST::AsExpr& ae);
    bool ChkAsExpr(ASTContext& ctx, AST::Ty& target, AST::AsExpr& ae);
    Ptr<AST::Ty> SynOptionalChainExpr(ASTContext& ctx, AST::OptionalChainExpr& oce);
    bool ChkOptionalChainExpr(ASTContext& ctx, AST::Ty& target, AST::OptionalChainExpr& oce);
    /**
     * Checks whether @param target is an auto-boxed Option of @param ty
     */
    bool CheckOptionBox(AST::Ty& target, AST::Ty& ty);

    bool ChkRangeExpr(ASTContext& ctx, AST::Ty& target, AST::RangeExpr& re);
    Ptr<AST::Ty> SynRangeExpr(ASTContext& ctx, AST::RangeExpr& re);
    Ptr<AST::Ty> SynRangeExprInferElemTy(const AST::RangeExpr& re, ASTContext& ctx);
    bool CheckRangeElements(ASTContext& ctx, Ptr<AST::Ty> elemTy, const AST::RangeExpr& re);

    Ptr<AST::Ty> SynArrayLit(ASTContext& ctx, AST::ArrayLit& al);
    bool ChkArrayLit(ASTContext& ctx, AST::Ty& target, AST::ArrayLit& al);
    Ptr<AST::Ty> GetArrayTypeByInterface(AST::Ty& interfaceTy);
    Ptr<AST::Ty> SynArrayExpr(ASTContext& ctx, AST::ArrayExpr& ae);
    Ptr<AST::Ty> SynVArrayExpr(ASTContext& ctx, AST::ArrayExpr& ve);
    bool ChkArrayExpr(ASTContext& ctx, AST::Ty& target, AST::ArrayExpr& ae);
    bool ChkVArrayExpr(ASTContext& ctx, AST::Ty& target, AST::ArrayExpr& ve);
    bool ChkVArrayArg(ASTContext& ctx, AST::ArrayExpr& ve);
    bool ChkSizedArrayExpr(ASTContext& ctx, AST::Ty& target, AST::ArrayExpr& ae);
    bool ChkSingeArgArrayExpr(ASTContext& ctx, AST::Ty& target, AST::ArrayExpr& ae);
    bool ChkSingeArgArrayWithoutElemTy(ASTContext& ctx, AST::Ty& target, AST::ArrayExpr& ae);
    bool ChkArrayArgs(AST::ArrayExpr& ae);
    /**
     * Check whether the alias is a built-in type.
     * @param kind Check whether the alias is of the specified type. If it's TYPE_ANY, Check Array/CPointer/CString.
     */
    bool IsBuiltinTypeAlias(const AST::Decl& decl, const AST::TypeKind kind = AST::TypeKind::TYPE_ANY) const;
    bool ChkSizedArrayElement(ASTContext& ctx, AST::Ty& elemTargetTy, AST::ArrayExpr& ae);
    bool ChkSizedArrayWithoutElemTy(ASTContext& ctx, AST::Ty& target, AST::ArrayExpr& ae);
    bool IsCallOfBuiltInType(const AST::CallExpr& ce, const AST::TypeKind kind) const;
    bool ChkArrayCall(ASTContext& ctx, AST::Ty& target, AST::CallExpr& ce);
    bool ChkCFuncCall(ASTContext& ctx, AST::Ty& target, AST::CallExpr& ce);
    bool ChkBuiltinCall(ASTContext& ctx, AST::Ty& target, AST::CallExpr& ce);
    bool ChkPointerCall(ASTContext& ctx, AST::Ty& target, AST::CallExpr& ce);
    bool ChkVArrayCall(ASTContext& ctx, AST::Ty& target, AST::CallExpr& ce);
    bool ChkPointerExpr(ASTContext& ctx, AST::Ty& target, AST::PointerExpr& cpe);
    /**
     * Check whether the CString is invoked.
     */
    bool ChkCStringCall(ASTContext& ctx, AST::Ty& target, AST::CallExpr& ce);
    Ptr<AST::Ty> SynPointerExpr(ASTContext& ctx, AST::PointerExpr& cptrExpr);
    Ptr<AST::Ty> SynMatchExpr(ASTContext& ctx, AST::MatchExpr& me);
    Ptr<AST::Ty> SynMatchExprHasSelector(ASTContext& ctx, AST::MatchExpr& me);
    Ptr<AST::Ty> SynMatchExprNoSelector(ASTContext& ctx, AST::MatchExpr& me);
    Ptr<AST::Ty> SynMatchCaseNoSelector(ASTContext& ctx, AST::MatchCaseOther& mco);
    Ptr<AST::Ty> SynNormalMatchCaseBody(ASTContext& ctx, AST::MatchExpr& me);
    Ptr<AST::Ty> SynQuestSugarMatchCaseBody(ASTContext& ctx, AST::MatchExpr& me);
    bool ChkMatchExpr(ASTContext& ctx, AST::Ty& target, AST::MatchExpr& me);
    bool ChkMatchExprHasSelector(ASTContext& ctx, AST::Ty& target, AST::MatchExpr& me);
    bool ChkMatchExprNoSelector(ASTContext& ctx, AST::Ty& target, AST::MatchExpr& me);
    bool ChkPatternsSameASTKind(const ASTContext& ctx, const std::vector<OwnedPtr<AST::Pattern>>& patterns);
    bool ChkNoVarPatternInOrPattern(const ASTContext& ctx, const std::vector<OwnedPtr<AST::Pattern>>& ps);
    bool ChkMatchCasePatterns(ASTContext& ctx, Ptr<AST::Ty> target, AST::MatchCase& mc);
    bool ChkMatchCasePatGuard(ASTContext& ctx, const AST::MatchCase& mc);
    bool ChkMatchCaseActions(ASTContext& ctx, Ptr<AST::Ty> target, AST::MatchCase& mc);
    bool ChkMatchCaseNoSelector(ASTContext& ctx, AST::Ty& target, AST::MatchCaseOther& mco);
    bool ChkSubscriptExpr(ASTContext& ctx, Ptr<AST::Ty> target, AST::SubscriptExpr& se);
    Ptr<AST::Ty> SynSubscriptExpr(ASTContext& ctx, AST::SubscriptExpr& se);
    bool ChkTupleAccess(ASTContext& ctx, Ptr<AST::Ty> target, AST::SubscriptExpr& se, AST::TupleTy& tupleTy);
    bool ChkVArrayAccess(ASTContext& ctx, Ptr<AST::Ty> target, AST::SubscriptExpr& se, AST::VArrayTy& varrTy);
    Ptr<AST::Ty> SynCallExpr(ASTContext& ctx, AST::CallExpr& ce);
    Ptr<AST::Ty> SynTrailingClosure(ASTContext& ctx, AST::TrailingClosureExpr& tc);
    bool ChkTrailingClosureExpr(ASTContext& ctx, AST::Ty& target, AST::TrailingClosureExpr& tc);
    void CheckMacroCall(ASTContext& ctx, AST::Node& macroNode);

    /**
     * Check call expressions' related APIs.
     */
    bool ChkCallExpr(ASTContext& ctx, Ptr<AST::Ty> target, AST::CallExpr& ce);
    bool ChkDesugarExprOfCallExpr(ASTContext& ctx, Ptr<AST::Ty> target, AST::CallExpr& ce);
    bool CheckCallKind(const AST::CallExpr& ce, Ptr<AST::Decl> decl, AST::CallKind& type);
    /**
     * Check call expression's base expression which is member access's or normal reference type.
     * Get current target and function candidates.
     */
    bool GetCallBaseCandidates(const ASTContext& ctx, const AST::CallExpr& ce, AST::Expr& expr, Ptr<AST::Decl>& target,
        std::vector<Ptr<AST::FuncDecl>>& candidates);
    bool CheckRefConstructor(const ASTContext& ctx, const AST::CallExpr& ce, const AST::RefExpr& re);
    /**
     * Check call expression's base expression. Get current target and function candidates.
     */
    bool ChkCallBaseExpr(ASTContext& ctx, AST::CallExpr& ce, Ptr<AST::Decl>& targetDecl, Ptr<AST::Ty>& targetRet,
        std::vector<Ptr<AST::FuncDecl>>& candidates);
    bool ChkCallBaseRefExpr(
        ASTContext& ctx, AST::CallExpr& ce, Ptr<AST::Decl>& target, std::vector<Ptr<AST::FuncDecl>>& candidates);
    bool ChkCallBaseMemberAccess(
        ASTContext& ctx, AST::CallExpr& ce, Ptr<AST::Decl>& target, std::vector<Ptr<AST::FuncDecl>>& candidates);
    bool ChkCurryCallBase(ASTContext& ctx, AST::CallExpr& ce, Ptr<AST::Ty>& targetRet);
    bool CheckNonNormalCall(ASTContext& ctx, Ptr<AST::Ty> target, AST::CallExpr& ce);
    bool ChkFunctionCallExpr(ASTContext& ctx, Ptr<AST::Ty> target, AST::CallExpr& ce);
    bool ChkVariadicCallExpr(ASTContext& ctx, Ptr<AST::Ty> target, AST::CallExpr& ce,
        const std::vector<Ptr<AST::FuncDecl>>& candidates, std::vector<Diagnostic>& diagnostics);
    bool SynArgsOfNothingBaseExpr(ASTContext& ctx, AST::CallExpr& ce);
    /**
     * One call expression may have multiple function candidates.
     * Filter them with parameters' conditions before legality check.
     * @param ctx Type Checker context.
     * @param inCandidates The possible target functions list to be filtered.
     * @param ce The call expression target.
     * @return Filtered candidates list.
     */
    std::vector<Ptr<AST::FuncDecl>> FilterCandidates(
        const ASTContext& ctx, const std::vector<Ptr<AST::FuncDecl>>& inCandidates, const AST::CallExpr& ce);
    std::vector<Ptr<AST::FuncDecl>> FilterCandidatesWithReExport(
        const ASTContext& ctx, const std::vector<Ptr<AST::FuncDecl>>& inCandidates, const AST::CallExpr& ce);
    /**
     * Override member funcs in Class/Interface inheritance will be filter in previous lookup stage.
     * So 2 cases considered here (generic function's comparison not considered here),
     *   1) Nested functions will shadow other functions.
     *   2) Class/Interface funcs will shadow top-level functions.
     *   3) implemented version in extend will shadow its abstract func in its extend type or interface.
     * @param candidates FuncDecl candidates. all should be synthesized.
     */
    void FilterShadowedFunc(std::vector<Ptr<AST::FuncDecl>>& candidates);
    /**
     * Implemented version in extend will shadow its abstract func in its extend type or interface
     */
    void FilterExtendImplAbstractFunc(std::vector<Ptr<AST::FuncDecl>>& candidates);
    void RemoveShadowedFunc(
        const AST::FuncDecl& fd, int64_t currentLevel, int64_t targetLevel, std::vector<Ptr<AST::FuncDecl>>& funcs);
    /**
     * In CallExpr, the called function may has default parameters and callExpr can omit the specification of
     * some parameters. Reorder all arguments.
     * @param fd The target function found for callExpr.
     * @return The matched target of callExpr, may be a nullptr.
     */
    std::vector<Ptr<AST::FuncDecl>> ReorderCallArgument(ASTContext& ctx, FunctionMatchingUnit& fmu, AST::CallExpr& ce);
    AST::Ty* GetCallTy(ASTContext& ctx, const AST::CallExpr& ce, const AST::FuncDecl& target) const;
    /**
     * Check whether arguments match function declare. In this case target function is not overloaded.
     * @param fd Target function to be checked.
     * @param ce The call expression target.
     * @param target Given upper-bound of function return type
     * @return Matched function decl.
     */
    std::vector<Ptr<AST::FuncDecl>> CheckFunctionMatch(
        ASTContext& ctx, FunctionCandidate& candidate, Ptr<AST::Ty> target, SubstPack& typeMapping);
    std::vector<Ptr<AST::FuncDecl>> GetOrderedCandidates(const ASTContext& ctx, const AST::CallExpr& ce,
        std::vector<Ptr<AST::FuncDecl>>& candidates,
        std::unordered_map<Ptr<AST::FuncDecl>, int64_t>& fdScopeLevelMap) const;
    std::vector<Ptr<AST::FuncDecl>> MatchFunctionForCall(ASTContext& ctx, std::vector<Ptr<AST::FuncDecl>>& candidates,
        AST::CallExpr& ce, Ptr<AST::Ty> target, SubstPack& typeMapping);
    /**
     * Get valid function types for given candidates of @p expr
     * returns: genericIgnored, std::vector<function, function type, type mapping>.
     */
    using FuncTyPair = std::pair<bool, std::vector<std::tuple<Ptr<AST::FuncDecl>, Ptr<AST::Ty>, TypeSubst>>>;
    FuncTyPair CollectValidFuncTys(
        ASTContext& ctx, std::vector<Ptr<AST::FuncDecl>>& funcs, AST::Expr& expr, Ptr<AST::FuncTy> targetTy = nullptr);
    /**
     * Determines whether the parameters of the funcDecl are compatible with the arguments in the callExpr.
     * In case of compatibility, the parameter types corresponding to the arguments are collected.
     * @param fd Target function to be checked.
     * @param argMapping Record the type of the parameter corresponding to the argument. Will be used in function
     * overload resolution.
     * @return Whether funcDecl is a legal target of the callExpr. If there is a parameter's type is incompatible with
     * the argument, return false. Otherwise, return true.
     */
    OwnedPtr<FunctionMatchingUnit> CheckCandidate(
        ASTContext& ctx, FunctionCandidate& candidate, Ptr<AST::Ty> targetRet, SubstPack& typeMapping);
    /**
     * Check whether the parameters type is compatible with arguments type.
     * The arguments can be parameters subtype.
     */
    bool CheckCallCompatible(ASTContext& ctx, FunctionCandidate& candidate);
    /**
     * When the candidate is a generic function, or function is a member of generic class/interface/struct...,
     * we should use this function to do compatibility check. First we will build a unordered map to represent
     * the mapping of generic types. Then we check the compatibility. Pay attention to checking generic constraints.
     */
    bool CheckGenericCallCompatible(
        ASTContext& ctx, FunctionCandidate& candidate, SubstPack& typeMapping, Ptr<AST::Ty> targetRet);
    void FilterTypeMappings(
        const AST::Expr& expr, AST::FuncDecl& fd, std::vector<MultiTypeSubst>& typeMappings);
    bool CheckCandidateConstrains(const AST::CallExpr& ce, const AST::FuncDecl& fd, const SubstPack& typeMapping);
    bool CheckAndGetMappingForTypeDecl(const AST::Expr& baseExpr, const AST::Decl& typeDecl,
        const AST::Decl& targetDecl, const SubstPack& typeMapping);
    /**
     * Get combinations of all possible function types for every argument of function call.
     */
    std::vector<std::set<Ptr<AST::Ty>>> GetArgTyPossibilities(ASTContext& ctx, AST::CallExpr& ce);
    std::vector<std::vector<Ptr<AST::Ty>>> GetArgsCombination(ASTContext& ctx, AST::CallExpr& ce);
    // check if any level of baseExpr(case it's MemberAccess) is of placeholder ty
    // will Synthesize the expr if it doesn't have a ty yet
    bool HasBaseOfPlaceholderTy(ASTContext& ctx, Ptr<AST::Node> n);
    /**
     * Build the generic type mapping table. The constraints should be checked in the process. True is returned only if
     * the mapping meets the constraints.
     */
    bool NeedSynthesis(const AST::CallExpr& ce, const AST::FuncDecl& fd, Ptr<const AST::Generic> generic,
        const std::vector<Ptr<AST::Type>>& typeArgs) const;
    std::vector<SubstPack> GenerateTypeMappingForCall(
        ASTContext& ctx, FunctionCandidate& candidate, Ptr<AST::Ty> retTarget);
    bool GenerateExtendGenericTypeMapping(
        const ASTContext& ctx, FunctionCandidate& candidate, MultiTypeSubst& typeMapping);
    SubstPack GenerateGenericTypeMapping(const ASTContext& ctx, const AST::Expr& expr);
    /**
     * Build the generic type mapping table while the baseExpr of other expr is a memberAccess.
     */
    void GenerateTypeMappingForBaseExpr(const AST::Expr& baseExpr, MultiTypeSubst& typeMapping);
    void GenerateTypeMappingForBaseExpr(const AST::Expr& baseExpr, SubstPack& typeMapping);
    /**
     * Build the generic type mapping table by current context type and candidate function's context
     * when call base is direct function reference call.
     */
    bool GenerateTypeMappingByCallContext(
        const ASTContext& ctx, const AST::FuncDecl& fd, const AST::CallExpr& ce, MultiTypeSubst& typeMapping);
    TypeSubst GenerateTypeMappingByTyArgs(
        const std::vector<Ptr<AST::Type>>& typeArgs, const AST::Generic& generic) const;
    std::vector<SubstPack> GenerateTypeMappingByInference(
        ASTContext& ctx, const FunctionCandidate& candidate, Ptr<AST::Ty> retTarget);
    ErrOrSubst PrepareTyArgsSynthesis(ASTContext& ctx, const FunctionCandidate& candidate, Ptr<AST::Ty> const retTyUB);
    std::optional<TypeSubst> PropagatePlaceholderAndSolve(ASTContext& ctx, AST::CallExpr& ce,
        const std::vector<Ptr<AST::Ty>>& paramTys, const Ptr<AST::Ty> retTy, Ptr<AST::Ty> const retTyUB);
    // static and deterministic version of unify, never result in branched versions of constraint
    // don't consider error reporting, for now
    // returns whether the unification is possible
    // on success, the cst parameter will be updated
    // on failure, the cst parameter will not change
    bool Unify(Constraint& cst, AST::Ty& argTy, AST::Ty& paramTy);
    // static version of solving, only need to handle one possible version of constraint
    std::optional<TypeSubst> SolveConstraints(const Constraint& cst);
    /**
     * After the function overload resolved, instantiate args and function target type for generic call.
     * @return empty vector if instantiation fails.
     */
    std::vector<Ptr<AST::FuncDecl>> UpdateFuncGenericType(
        ASTContext& ctx, FunctionMatchingUnit& fmu, AST::CallExpr& ce);
    void ReplaceIdealTypeInSubstPack(SubstPack& maps);
    /**
     * Replace all T according to typeMapping.
     */
    void SpreadInstantiationTy(AST::Node& node, const SubstPack& typeMapping);
    /**
     * Resolve the most matching function, and return the function's index in targets.
     * @param candidates The vector of matched candidates.
     * @param ce The callExpr need to be resolved.
     * @return The most matching functions index.
     */
    std::vector<size_t> ResolveOverload(
        std::vector<OwnedPtr<FunctionMatchingUnit>>& candidates, const AST::CallExpr& ce);
    FunctionMatchingUnit* FindFuncWithMaxChildRetTy(
        const AST::CallExpr& ce, std::vector<std::unique_ptr<FunctionMatchingUnit>>& candidates);
    /**
     * Check matched results and report associated/reasonable set of diagnoses.
     * @param ctx current expression's ASTContext/
     * @param ce The callExpr need to be resolved.
     * @param legals The vector of matched candidates.
     * @param diagnoses The vector of diagnostics of all function candidates and their count of matched arguments.
     * @return The vector of matched function decls.
     */
    std::vector<Ptr<AST::FuncDecl>> CheckMatchResult(ASTContext& ctx, AST::CallExpr& ce,
        std::vector<OwnedPtr<FunctionMatchingUnit>>& legals, std::vector<FunctionMatchingUnit>& illegals);
    void ReInferCallArgs(ASTContext& ctx, const AST::CallExpr& ce, const FunctionMatchingUnit& legal);
    void RecoverCallArgs(ASTContext& ctx, const AST::CallExpr& ce, const std::vector<Ptr<AST::Ty>>& argsTys);
    void FillEnumTypeArgumentsTy(const AST::Decl& ctorDecl, const SubstPack& typeMapping, AST::MemberAccess& ma);
    void FillTypeArgumentsTy(const AST::FuncDecl& fd, const AST::CallExpr& ce, SubstPack& typeMapping);
    /**
     * Instantiates the type argument introduced from the outer layer decl.
     */
    void InstantiatePartOfTheGenericParameters(std::vector<OwnedPtr<FunctionMatchingUnit>>& candidates);
    /**
     * Compare arguments and parameters. Choose the most consistent one.
     * Each parameter of the result must be the best match.
     * @return Returns the comparison result of column i and column j in vectors of argMapping.
     */
    bool CompareFuncCandidates(FunctionMatchingUnit& i, FunctionMatchingUnit& j, const AST::CallExpr& ce);
    /**
     * Check non-static calling in static method.
     * @return If callExpr is in static method and target is non-static, returns false. Else returns true.
     */
    bool CheckStaticCallNonStatic(const ASTContext& ctx, const AST::CallExpr& ce, const AST::FuncDecl& result);
    Ptr<AST::Decl> GetDeclOfThisType(const AST::Expr& expr) const;
    std::optional<Ptr<AST::Ty>> DynamicBindingThisType(
        AST::Expr& baseExpr, const AST::FuncDecl& fd, const SubstPack& typeMapping = {});
    bool CheckFuncPtrCall(ASTContext& ctx, Ptr<AST::Ty> target, AST::CallExpr& ce, AST::FuncTy& funcTy);
    bool IsGenericCall(const ASTContext& ctx, const AST::CallExpr& ce, const AST::FuncDecl& fd) const;
    bool CheckArgsWithParamName(const AST::CallExpr& ce, const AST::FuncDecl& fd);
    bool PostCheckCallExpr(const ASTContext& ctx, AST::CallExpr& ce, AST::FuncDecl& func, const SubstPack& typeMapping);
    void PostProcessForLSP(AST::CallExpr& ce, const std::vector<Ptr<AST::FuncDecl>>& result) const;
    void CheckUnsafeInvoke(const AST::CallExpr& ce);
    void CheckToTokensImpCallExpr(const AST::CallExpr& ce);
    DiagKind GetErrorKindForCall(const std::vector<Ptr<AST::FuncDecl>>& candidatesBeforeCheck,
        const std::vector<Ptr<AST::FuncDecl>>& candidatesAfterCheck, const AST::CallExpr& ce) const;
    void DiagnoseForCall(const std::vector<Ptr<AST::FuncDecl>>& candidatesBeforeCheck,
        const std::vector<Ptr<AST::FuncDecl>>& candidatesAfterCheck, AST::CallExpr& ce, const AST::Decl& decl);

    /** Check Pattern's related APIs. */
    bool ChkPattern(ASTContext& ctx, AST::Ty& target, AST::Pattern& p, bool isPatternInMatch = true);
    bool ChkWildcardPattern(AST::Ty& target, AST::WildcardPattern& p) const;
    bool ChkConstPattern(ASTContext& ctx, AST::Ty& target, AST::ConstPattern& p);
    bool ChkOpOverloadForConstPattern(ASTContext& ctx, AST::Ty& target, AST::ConstPattern& p);
    bool ChkTypePattern(ASTContext& ctx, AST::Ty& target, AST::TypePattern& p);
    bool ChkVarPattern(const ASTContext& ctx, AST::Ty& target, AST::VarPattern& p);
    bool ChkTuplePattern(ASTContext& ctx, AST::Ty& target, AST::TuplePattern& p, bool isPatternInMatch = true);
    bool ChkEnumPattern(ASTContext& ctx, AST::Ty& target, AST::EnumPattern& p);
    bool ChkVarOrEnumPattern(ASTContext& ctx, AST::Ty& target, AST::VarOrEnumPattern& p);
    bool ChkExceptTypePattern(ASTContext& ctx, AST::ExceptTypePattern& etp, std::vector<Ptr<AST::Ty>>& included);
    bool ChkHandlePatterns(ASTContext& ctx, AST::Handler& h,
        std::vector<Ptr<AST::Ty>>& included);
    std::optional<Ptr<AST::Ty>> ChkCommandTypePattern(
        ASTContext& ctx, AST::CommandTypePattern& ctp, std::vector<Ptr<AST::Ty>>& included);
    bool ChkTryWildcardPattern(Ptr<AST::Ty> target, AST::WildcardPattern& p, std::vector<Ptr<AST::Ty>>& included);
    void FindEnumPatternTarget(ASTContext& ctx, Ptr<AST::EnumDecl> ed, AST::EnumPattern& ep);
    std::vector<Ptr<AST::Decl>> FindEnumPatternTargets(ASTContext& ctx, Ptr<AST::EnumDecl> ed, AST::EnumPattern& ep);

    void UpdateAnyTy();
    void UpdateCTypeTy();
    bool IsIrrefutablePattern(const AST::Pattern& pattern);
    Ptr<AST::Ty> SynForInExpr(ASTContext& ctx, AST::ForInExpr& fie);
    bool ChkForInExpr(ASTContext& ctx, AST::Ty& target, AST::ForInExpr& fie);
    Ptr<AST::Ty> SynSpawnExpr(ASTContext& ctx, AST::SpawnExpr& se);
    bool ChkSpawnExpr(ASTContext& ctx, AST::Ty& tgtTy, AST::SpawnExpr& se);
    bool ChkSpawnExprSimple(ASTContext& ctx, AST::Ty& tgtTy, AST::SpawnExpr& se);
    bool CheckSpawnArgValid(const ASTContext& ctx, const AST::Expr& arg);
    Ptr<AST::Ty> SynSyncExpr(ASTContext& ctx, AST::SynchronizedExpr& se);
    bool ChkSyncExpr(ASTContext& ctx, Ptr<AST::Ty> tgtTy, AST::SynchronizedExpr& se);

    /**
     * Resolve referenced objects. When referencing declaration like var, class,
     * struct, interface..., the target can be uniquely determined. When
     * referencing a function, all targets matched by the identifier are stored
     * in targets.
     */
    void InferRefExpr(ASTContext& ctx, AST::RefExpr& re);
    void InferCFuncExpr(ASTContext& ctx, AST::RefExpr& re);
    bool SynTargetOnUsed(ASTContext& ctx, const AST::NameReferenceExpr& nre, AST::Decl& target);
    void CheckThisOrSuper(const ASTContext& ctx, AST::RefExpr& re);
    Ptr<AST::Ty> InferTypeOfThis(AST::RefExpr& re, AST::InheritableDecl& objDecl);
    Ptr<AST::Ty> InferTypeOfSuper(AST::RefExpr& re, const AST::InheritableDecl& objDecl);
    Ptr<AST::Ty> ReplaceWithGenericTyInInheritableDecl(
        Ptr<AST::Ty> ty, const AST::Decl& outerDecl, const AST::InheritableDecl& id);
    void CheckUsageOfThis(const ASTContext& ctx, const AST::RefExpr& re) const;
    void CheckUsageOfSuper(const ASTContext& ctx, const AST::RefExpr& re) const;
    void CheckThisOrSuperInInitializer(const AST::Node& node, const AST::RefExpr& re) const;
    /** When expression's type argument not empty, check whether argument size is matched. */
    bool IsRefTypeArgSizeValid(const AST::Expr& expr, std::vector<Ptr<AST::Decl>>& targets);
    /** Filter targets of reference @p re first and check whether matched number of targets valid. */
    bool FilterAndCheckTargetsOfRef(const ASTContext& ctx, AST::RefExpr& re, std::vector<Ptr<AST::Decl>>& targets);
    bool FilterInvalidEnumTargets(const AST::NameReferenceExpr& nre, std::vector<Ptr<AST::Decl>>& targets);
    void DiagMemberAccessNotFound(const AST::MemberAccess& ma);
    bool FilterAndCheckTargetsOfNameAccess(
        const ASTContext& ctx, const AST::MemberAccess& ma, std::vector<Ptr<AST::Decl>>& targets);
    Ptr<AST::Decl> FilterAndGetTargetsOfObjAccess(
        const ASTContext& ctx, AST::MemberAccess& ma, std::vector<Ptr<AST::Decl>>& targets);
    /**
     * Filter of reference @p expr when expr's type needs to be inferred.
     *  1. when expr does not have type argument but target is generic.
     *  2. found multiple function candidates.
     * @param expr [in] reference expression which target type is not given.
     * @param targets [in/out]
     */
    bool FilterTargetsForFuncReference(const AST::Expr& expr, std::vector<Ptr<AST::Decl>>& targets);
    bool IsLegalAccessFromStaticFunc(const ASTContext& ctx, const AST::RefExpr& re, const AST::Decl& decl);
    /** Filter targets of reference @p re . */
    void FilterCandidatesForRef(const ASTContext& ctx, const AST::RefExpr& re, std::vector<Ptr<AST::Decl>>& targets);
    /** Filter valid function targets of reference @p re when @p candidates are all functions. */
    void FilterIncompatibleCandidatesForCall(const AST::CallExpr& ce, std::vector<Ptr<AST::FuncDecl>>& candidates);
    /** Instantiate @p re 's sema type with given type arguments. */
    void InstantiateReferenceType(
        const ASTContext& ctx, AST::NameReferenceExpr& expr, const TypeSubst& instantiateMap = {});
    void CanTargetOfRefBeCaptured(const ASTContext& ctx, const AST::NameReferenceExpr& nre, const AST::Decl& decl,
        const AST::FuncBody& curFuncBody) const;
    void CanTargetOfRefBeCapturedCaseNominalDecl(const ASTContext& ctx, const AST::NameReferenceExpr& nre,
        const AST::Decl& decl, const AST::FuncBody& curFuncBody) const;
    void CanTargetOfRefBeCapturedCaseMutFunc(const ASTContext& ctx, const AST::NameReferenceExpr& nre,
        const AST::Decl& decl, const AST::FuncBody& curFuncBody) const;
    void SetCaptureKind(const ASTContext& ctx, const AST::NameReferenceExpr& nre, AST::FuncBody& curFuncBody) const;
    void MarkAndCheckRefExprVarCaptureStatus(const ASTContext& ctx, const AST::NameReferenceExpr& nre) const;
    void CheckImmutableFuncAccessMutableFunc(
        const Position& pos, const AST::Node& srcNode, const AST::Decl& destNode, bool isLeftStructValue) const;
    void CheckForbiddenFuncReferenceAccess(const Position& pos, const AST::FuncDecl& fd, const AST::Decl& decl) const;
    /**
     * Check whether given node can match target type. refNode can be RefExpr or MemberAccess.
     */
    bool ChkRefExpr(ASTContext& ctx, AST::Ty& target, AST::NameReferenceExpr& refNode);
    /**
     * Report a warning when capture a variable in outer scope, but has a same name decl in inter scope.
     * example:let x = 4
     *         func f() {
     *             func f1() {
     *                 x  // Need report a warning.
     *             }
     *             let x = 2
     *         }
     */
    void CheckWarningOfCaptureVariable(const ASTContext& ctx, const AST::RefExpr& re) const;

    /**
     * MemberAccess's semaType depends on its target. The base can be ClassDecl,
     * InterfaceDecl, EnumDecl, PackageDecl or class object(including super,
     * this and builtin data structures object).
     */
    void InferMemberAccess(ASTContext& ctx, AST::MemberAccess& ma);
    void InferArrayStaticAccess(const ASTContext& ctx, AST::MemberAccess& ma);
    void InferBuiltInStaticAccess(const ASTContext& ctx, AST::MemberAccess& ma, const AST::BuiltInDecl& bid);
    void InferInstanceAccess(const ASTContext& ctx, AST::MemberAccess& ma);
    void InferStaticAccess(const ASTContext& ctx, AST::MemberAccess& ma, AST::Decl& targetOfBaseExpr);
    void CheckExtendField(const ASTContext& ctx, AST::MemberAccess& ma);
    /** Filter targets that @p ma 's instantiated types does not satisfied with extend's generic constraints. */
    bool FilterTargetsInExtend(
        const AST::NameReferenceExpr& nre, Ptr<AST::Ty> baseTy, std::vector<Ptr<AST::Decl>>& targets);
    Ptr<AST::Decl> GetBaseDeclInMemberAccess(ASTContext& ctx, const AST::MemberAccess& ma);
    /** Get the target of @p MemberAccess whose baseExpr's ty is @p baseExprTy. */
    Ptr<AST::Decl> GetObjMemberAccessTarget(const ASTContext& ctx, AST::MemberAccess& ma, AST::Ty& baseExprTy);
    /** Get target function from extend function like 1.add(). */
    Ptr<AST::Decl> GetIdealTypeFuncTargetFromExtend(
        const ASTContext& ctx, AST::MemberAccess& ma, const AST::Ty& baseExprTy);
    /**
     * Get the target of @p MemberAccess whose baseExpr's ty is @p baseExprTy.
     * For example: func Foo2<U>(a:U) where U <: Bar2<U> {
     * var b : U = a.member    // Field access by using assumption upper bound.
     * return b
     * }
     * In this case a.member will be the member in Bar2<U>.
     * */
    Ptr<AST::Decl> GetMemberAccessExposedTarget(
        const ASTContext& ctx, AST::MemberAccess& ma, const AST::GenericsTy& genericsTy, bool isStaticAccess);
    /**
     * Check the targets set obtained from upper bounds because all upperBounds' matched members or methods will
     * be collected.
     */
    std::vector<Ptr<AST::Decl>> GetUpperBoundTargets(
        const ASTContext& ctx, const AST::MemberAccess& ma, AST::Ty& baseExprTy, const bool isStaticAccess);
    Ptr<AST::Decl> CheckUpperBoundTargetsCaseFuncCall(const ASTContext& ctx, AST::MemberAccess& ma,
        const std::unordered_map<Ptr<AST::Ty>, std::vector<Ptr<AST::Decl>>>& allTargets);
    Ptr<AST::Decl> CheckUpperBoundTargetsCaseOthers(const ASTContext& ctx, AST::MemberAccess& ma,
        const std::unordered_map<Ptr<AST::Ty>, std::vector<Ptr<AST::Decl>>>& allTargets);
    void TryInitializeBaseSum(ASTContext& ctx, AST::MemberAccess& ma);
    bool FilterSumUpperbound(AST::MemberAccess& ma, AST::GenericsTy& tv, const AST::Decl& d);
    void FilterSumUpperbound(const ASTContext& ctx, AST::MemberAccess& ma, AST::GenericsTy& tv,
        std::vector<Ptr<AST::Decl>>& targets,
        const std::unordered_map<Ptr<AST::Ty>, std::vector<Ptr<AST::Decl>>>& allTargets);
    /**
     * Check type legality recursively.
     */
    void CheckReferenceTypeLegality(ASTContext& ctx, AST::Type& t);
    void CheckRefType(ASTContext& ctx, AST::RefType& rt);
    void CheckCFuncType(ASTContext& ctx, const AST::RefType& rt);
    void CheckTupleType(ASTContext& ctx, AST::TupleType& tt);
    void CheckFuncType(ASTContext& ctx, AST::FuncType& ft);
    void CheckOptionType(ASTContext& ctx, const AST::OptionType& ot);
    void CheckVArrayType(ASTContext& ctx, const AST::VArrayType& vt);
    std::tuple<bool, std::string> CheckVArrayWithRefType(AST::Ty& ty, std::unordered_set<Ptr<AST::Ty>>& traversedTy);
    void CheckQualifiedType(const ASTContext& ctx, AST::QualifiedType& qt);
    bool IsGenericTypeWithTypeArgs(AST::Type& type) const;
    // Returns true if further checks can be omitted.
    bool CheckRefExprCheckTyArgs(const AST::RefType& rt, const AST::Decl& target);
    bool CheckRefTypeCheckAccessLegality(const ASTContext& ctx, AST::RefType& rt, const AST::Decl& target);
    void CheckRefTypeWithRealTarget(AST::RefType& rt);
    void HandleAliasForRefType(AST::RefType& rt, Ptr<AST::Decl>& target);

    void GetRevTypeMapping(
        std::vector<Ptr<AST::Ty>>& params, std::vector<Ptr<AST::Ty>>& args, MultiTypeSubst& revTyMap);
    TypeSubst GetGenericTysToInstTysMapping(AST::Ty& genericTy, AST::Ty& instTy) const;
    /**
     * Check TypeAlias entry.
     */
    void CheckTypeAlias(ASTContext& ctx, AST::TypeAliasDecl& tad);
    void CheckTypeAliasAccess(const AST::TypeAliasDecl& tad);
    std::vector<Ptr<AST::Ty>> GetUnusedTysInTypeAlias(const AST::TypeAliasDecl& tad) const;
    /**
     * Get RefType or QualifiedType type arguments application map
     */
    void GetTypeArgsOfType(Ptr<AST::Type> type, std::vector<Ptr<AST::Type>>& params);
    bool ReplaceIdealTy(AST::Node& node);
    bool ChkLitConstExprRange(AST::LitConstExpr& lce);
    bool ChkFloatTypeOverflow(const AST::LitConstExpr& lce);
    bool CheckThisTypeOfFuncBody(const AST::FuncBody& fb) const;
    bool IsIndexAssignmentOperator(const AST::FuncDecl& fd) const;
    void CheckFuncDecl(ASTContext& ctx, AST::FuncDecl& fd);
    /**
     * Check program entry specifically, whose return type must be `Int64`
     */
    void CheckEntryFunc(AST::FuncDecl& fd);
    bool CheckNormalFuncBody(ASTContext& ctx, AST::FuncBody& fb, std::vector<Ptr<AST::Ty>>& paramTys);
    bool CheckFuncBody(ASTContext& ctx, AST::FuncBody& fb);
    void AddRetTypeNode(AST::FuncBody& fb) const;
    bool CheckBodyRetType(ASTContext& ctx, AST::FuncBody& fb);
    void CheckFuncParamList(ASTContext& ctx, AST::FuncParamList& fpl);
    Ptr<AST::Ty> CalcFuncRetTyFromBody(const AST::FuncBody& fb);
    void ReplaceFuncRetTyWithThis(AST::FuncBody& fb, Ptr<AST::Ty> ty);
    void CheckCtorFuncBody(ASTContext& ctx, AST::FuncBody& fb);
    bool CheckReturnThisInFuncBody(const AST::FuncBody& fb) const;
    /**
     * If a constructor contains super-calling or init-calling in its body, the
     * the super-calling or init-calling must be in the first line.
     */
    void CheckConstructor(ASTContext& ctx, const AST::Decl& decl, AST::FuncDecl& fd);
    void CheckConstructorSuper(const AST::CallExpr& ce, const AST::Decl& decl, AST::FuncDecl& fd, bool& needEraseSuper);
    void CheckCallsInConstructor(
        ASTContext& ctx, const AST::Decl& decl, AST::FuncDecl& fd, AST::Node& firstExprOrDecl, bool needEraseSuper);
    void CheckFinalizer(const AST::FuncDecl& fd);
    void CheckOperatorOverloadFunc(const AST::FuncDecl& fd);
    void HandIndexOperatorOverload(const AST::FuncDecl& fd, const AST::FuncTy& funcTy);
    bool NeedCheckBodyReturn(const AST::FuncBody& fb) const;
    void CheckAnnotations(ASTContext& ctx, AST::Decl& decl);
    void CheckAnnotationDecl(ASTContext& ctx, AST::Annotation& ann);
    OwnedPtr<AST::CallExpr> CheckCustomAnnotation(ASTContext& ctx, const AST::Decl& decl, AST::Annotation& ann);
    bool HasModifier(const std::set<AST::Modifier>& modifiers, TokenKind kind) const;

    void CheckLegalityOfUsage(ASTContext& ctx, AST::Package& pkg);
    void CheckClosures(const ASTContext& ctx, AST::Node& node) const;
    /**
     * Check whether the members are legal according to the members of
     * super class and interfaces. For example some functions should be overridden;
     * some shadowing attempts may lead to error.
     * @param pkg The package node which contains structure decls.
     */
    void CheckInheritance(AST::Package& pkg);
    void CheckLegalUseOfClosure(AST::Expr& e, DiagKind kind) const;
    void CheckLegalUseOfClosure(const ASTContext& ctx, AST::Node& node) const;
    bool IsCapturedInCFuncLambda(const ASTContext& ctx, const AST::RefExpr& re) const;
    bool IsCapturedCStructOfClosure(const AST::VarDecl& decl) const;

    /**
     * Check non-static member accessed by static variable.
     */
    void CheckStaticVarAccessNonStatic(const AST::VarDecl& vd);

    /**
     * Filter out the targets without access rights in the searched targets.
     * @param curComposite Represents the position of the referrer.
     * NOTICE: Whether the modifier will be downgraded in spec is not yet determined.
     */
    bool IsLegalAccess(AST::Symbol* curComposite, const AST::Decl& d, const AST::Node& node) const;

    /**
     * Check non-function access control of the objective target. The target cannot be a
     * function here. If is legal, return true, else return false;
     */
    bool CheckNonFuncAccessControl(const ASTContext& ctx, const AST::Expr& e, const AST::Decl& target) const;

    /**
     * Check function access control of the objective target such as A().f.
     */
    Ptr<AST::Decl> CheckFuncAccessControl(
        const ASTContext& ctx, const AST::Expr& e, const std::vector<Ptr<AST::Decl>>& targets) const;
    /**
     * Get all accessible decls of expression @p e from given @p targets.
     */
    std::vector<Ptr<AST::Decl>> GetAccessibleDecls(
        const ASTContext& ctx, const AST::Expr& e, const std::vector<Ptr<AST::Decl>>& targets) const;
    /**
     * Get first accessible decl of expression @p e from given @p targets.
     */
    Ptr<AST::Decl> GetAccessibleDecl(
        const ASTContext& ctx, const AST::Expr& e, const std::vector<Ptr<AST::Decl>>& targets) const;

        /**
     * Check Whether there exists ref or access of member function before
     * the finishing the initialization of the class
     */
    void CheckMemberAccessInCtorParamOrCtorArg(const ASTContext& ctx, const AST::FuncDecl& fd);
    void CheckIllegalMemberWalker(
        const ASTContext& ctx, Ptr<AST::Node> node, bool reportThis, const std::string& errorStr);
    void CheckIllegalMemberHelper(
        const ASTContext& ctx, bool reportThis, const std::string& errorStr, const AST::NameReferenceExpr& nre);

    /** Checking Initialization APIs. */
    void CheckGlobalVarInitialization(ASTContext& ctx, const AST::Package& package);

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    void CheckStaticMembersWithGeneric(const AST::Package& pkg);
    void CheckStaticMemberWithGeneric(AST::Decl& member, const std::vector<Ptr<AST::Ty>>& outerGenericTys);
#endif

    /**
     * Check if the type nodes in the generic constraint are valid.
     */
    void CheckUpperBound(ASTContext& ctx, const AST::GenericConstraint& genericConstraint);
    /**
     * Check the generic constraint:
     * 1. call 'CheckUpperBound' to check types' legality.
     * 2. if left value of generic constraints is in the typeParameters.
     */
    void CheckGenericConstraints(ASTContext& ctx, const AST::Generic& generic);
    /**
     * Get the type of generic declaration.
     */
    Ptr<AST::Ty> GetGenericType(AST::Decl& d, const std::vector<Ptr<AST::Type>>& typeArgs);
    /**
     * Check generic Expr: check if the expr's typeArguments fulfil requirements of the target function's
     * typeParameters, then instantiate the typeParameters of target function.
     */
    void CheckGenericExpr(AST::Expr& expr);
    /**
     * Check whether the @p typeArgs meet the @p decl's generic constraints.
     */
    bool HasIncompleteStaticFuncOrProp(
        const ASTContext& ctx, AST::InheritableDecl& cd, std::vector<Ptr<AST::Decl>>& staticMemberFuncsOrProps);
    bool CheckInstTypeCompleteness(const ASTContext& ctx, const AST::NameReferenceExpr& expr);
    bool CheckInstTyWithUpperbound(const ASTContext& ctx, TypeSubst& typeMapping, const AST::NameReferenceExpr& expr);
    bool CheckCallGenericDeclInstantiation(
        Ptr<const AST::Decl> d, const std::vector<Ptr<AST::Type>>& typeArgs, const AST::Expr& checkNode);
    /**
     * Check if the typeArguments fulfil requirements of given decl's typeParameters
     * @param d the decl which defines the 'typeParameters' to be checked.
     * @param arguments the type arguments used to instantiated given decl.
     * @param checkNode the ast node which using the instantiate type of given decl.
     */
    bool CheckGenericDeclInstantiation(Ptr<const AST::Decl> d,
        const std::variant<std::vector<Ptr<AST::Type>>, std::vector<Ptr<AST::Ty>>>& arguments,
        const AST::Node& checkNode);
    /**
     * Check whether the @p pkg exists value type recursive.
     */
    void CheckValueTypeRecursive(const AST::Package& pkg);
    void CheckValueTypeRecursiveDFS(Ptr<AST::Decl> root, std::deque<Ptr<AST::Decl>> path = {});
    void CheckValueTypeRecursiveDFSSwitch(Ptr<AST::Decl> root, const std::deque<Ptr<AST::Decl>>& path);
    void CheckRecursiveConstructorCall(const std::vector<OwnedPtr<AST::Decl>>& decls);

    /** ======== Generic assumption related functions implemented in src/Sema/Assumption.cpp. ======== */
    /**
     * Main entrance of assumption. Collect type constraints recursively.
     * @param typeConstraintCollection the type constraints set, which is modified during the assumption period.
     * @param decl the declaration to be assumption.
     * @param typeMapping the instantiation substitute map.
     */
    void Assumption(AST::TyVarEnv& typeConstraintCollection, GCBlames& blames, const AST::Decl& decl,
        const TypeSubst& typeMapping = std::map<Ptr<TyVar>, Ptr<AST::Ty>>());
    /**
     * Collect type constraints recursively if the upper bounds of @p gc have generic constraints with
     * instantiation substitute map @p typeMapping .
     */
    void PerformAssumptionForOneGenericConstraint(AST::TyVarEnv& typeConstraintCollection, GCBlames& blames,
        const AST::GenericConstraint& gc, const TypeSubst& typeMapping);
    /** Collect type constraints for an upper bound @p upperBound with instantiation substitute map @p typeMapping . */
    void AssumeOneUpperBound(AST::TyVarEnv& typeConstraintCollection, GCBlames& blames, const AST::Type& upperBound,
        const TypeSubst& typeMapping);
    /**
     * Collect type constraints for an reference type upper bound @p referenceTypeUpperBound with instantiation
     * substitute map @p typeMapping .
     */
    void PerformAssumeReferenceTypeUpperBound(AST::TyVarEnv& typeConstraintCollection, GCBlames& blames,
        const AST::Type& referenceTypeUpperBound, const TypeSubst& typeMapping);

    /**
     * Check that whether a structure declaration inherits a duplicate interface directly.
     * For example: class C<T> <: I<T> & I<T> {}
     * @param decl the declaration to be checked. This function is invoked in PreCheck stage.
     */
    void CheckDupInterfaceInStructDecl(AST::InheritableDecl& decl);
    /**
     * The main entry function of the check that whether a structure declaration inherits a duplicate interface
     * instantiated by given type arguments. There is a AST walker inside, the reference node including refExpr, refType
     * will trigger the check.
     * */
    void CheckInstDupSuperInterfacesEntry(AST::Node& n);
    void CheckUnusedImportSpec(AST::Package& pkg);
    AST::VisitAction CheckInstDupSuperInterfaces(const AST::Type& type);
    AST::VisitAction CheckInstDupSuperInterfaces(const AST::Expr& expr);
    /**
     * The main entry function of the check that whether a structure declaration inherits a duplicate interface
     * instantiated by given type arguments. The @p triggerNode is the reference node which triggers the instantiation
     * with given type arguments. The @p decl is the declaration to be checked. The @p instantiateMap is the type
     * mapping built by the given type arguments.
     */
    void CheckInstDupSuperInterfaces(const AST::Node& triggerNode, AST::InheritableDecl& decl,
        const TypeSubst& instantiateMap, bool checkExtend = true);
    /**
     * Check whether the declaration corresponding to the @p ty inherits or implements duplicate interfaces with
     * given type mapping @p instantiateMap. If yes, return declaration of the interface with 'true'. Otherwise, return
     * declaration of the 'ty' with `false`.
     * @param passedClassLikeDecls a set of checked nodes to avoid duplicate check.
     */
    Ptr<AST::Decl> GetDupSuperInterface(const AST::Node& triggerNode, AST::InheritableDecl& decl,
        const TypeSubst& instantiateMap, std::unordered_set<Ptr<AST::ClassLikeDecl>>& passedClassLikeDecls,
        bool checkExtend = true);
    /**
     * Check whether the @p interfaceTy instantiated by given type mapping @p instantiateMap exists in
     * the implemented interfaceTy set @p res. If yes, it means duplicate interface are inherited or implemented,
     * declaration of the interface with 'true' will be returned.
     * Otherwise, return declaration of the 'ty' with `false`.
     * @param passedClassLikeDecls a set of checked nodes to avoid duplicate check.
     */
    Ptr<AST::Decl> GetDupInterfaceRecursively(const AST::Node& triggerNode, AST::Ty& interfaceTy,
        const TypeSubst& instantiateMap, std::unordered_set<Ptr<AST::InterfaceTy>>& res,
        std::unordered_set<Ptr<AST::ClassLikeDecl>>& passedClassLikeDecls);
    /**
     * Check whether whether all the extend declaration of @p decl inherits or implements duplicate interfaces
     * with given type mapping @p instantiateMap exists in the implemented interfaceTy set @p res. If yes, it means
     * duplicate interface are inherited or implemented, declaration of the interface with 'true' will be returned.
     * Otherwise, return declaration of the 'ty' with `false`.
     * @param passedClassLikeDecls a set of checked nodes to avoid duplicate check.
     */
    Ptr<AST::Decl> GetExtendDupSuperInterface(const AST::Node& triggerNode, const AST::InheritableDecl& decl,
        const TypeSubst& instantiateMap, std::unordered_set<Ptr<AST::InterfaceTy>>& res,
        std::unordered_set<Ptr<AST::ClassLikeDecl>>& passedClassLikeDecls);

    void CheckExtendDecl(ASTContext& ctx, AST::ExtendDecl& ed);
    void SetExtendExternalAttr(const ASTContext& ctx, AST::ExtendDecl& ed);
    void CheckExtendOrphanRule(const ASTContext& ctx, AST::ExtendDecl& ed);
    void CheckImmutExtendInhertMutSuper(const AST::Type& inheritedType, const AST::ExtendDecl& ed);
    /**
     * Check if a type implements duplicate interfaces,
     * remove duplicate interfaces and invalid interfaces in extendDecl->interfaces.
     */
    void CheckExtendInterfaces(AST::Ty& ty, const std::set<Ptr<AST::ExtendDecl>, AST::CmpNodeByPos>& extendDecls);
    void CheckDefImplWithoutOutsideGeneric(AST::Decl& inhertDecl, const AST::ExtendDecl& extend);
    void CheckExtendDupDefImplByDiffTypArgs(
        const std::set<Ptr<AST::ExtendDecl>, AST::CmpNodeByPos>& extendDecls, const AST::ExtendDecl& extend);
    void CheckExtendGenerics(const AST::ExtendDecl& ed);
    void CheckExtendedTypeValidity(const AST::Type& extendedType);
    bool IsImplementation(
        AST::Ty& baseTy, AST::InterfaceTy& iTy, const AST::Decl& interfaceMember, const AST::Decl& childMember);
    bool HasOverrideDefaultImplement(
        const AST::InheritableDecl& decl, const AST::Decl& defaultImplement, AST::InterfaceTy& superTy);
    OwnedPtr<AST::Decl> GetCloneDecl(AST::Decl& decl, AST::InheritableDecl& inheritableDecl, AST::InterfaceTy& superTy);
    void CloneAndInsert(
        std::unordered_map<Ptr<AST::Decl>, std::unordered_set<Ptr<AST::Decl>>>& originFuncToCopyFuncsMap,
        AST::Decl& decl, AST::InheritableDecl& inheritableDecl, AST::InterfaceTy& superTy);
    std::unordered_map<Ptr<AST::Decl>, std::unordered_set<Ptr<AST::Decl>>> CopyDefaultImplement(
        const AST::Package& pkg);
    void HandleDefaultImplement(const AST::Package& pkg);

    Ptr<AST::Ty> SubstituteTypeAliasInTy(
        AST::Ty& ty, bool needSubstituteGeneric = false, const TypeSubst& typeMapping = {});
    Ptr<AST::Ty> GetUnaliasedTypeFromTypeAlias(
        const AST::TypeAliasTy& target, const std::vector<Ptr<AST::Ty>>& typeArgs);
    void SubstituteTypeForTypeAliasTypeMapping(
        const AST::TypeAliasDecl& tad, const std::vector<Ptr<AST::Ty>>& typeArgs, TypeSubst& typeMapping) const;
    TypeSubst GenerateTypeMappingForTypeAliasDecl(const AST::TypeAliasDecl& tad) const;
    TypeSubst GenerateTypeMappingForTypeAliasDeclVisit(
        const AST::TypeAliasDecl& tad, std::unordered_set<Ptr<const AST::TypeAliasDecl>>& visited) const;

    // usage could be an AST::Type or AST::Expr, both of which can have written type arguements
    template <typename T>
    TypeSubst GenerateTypeMappingForTypeAliasUse(const AST::TypeAliasDecl& tad, const T& usage) const
    {
        TypeSubst typeMapping;
        if (!tad.type) {
            return typeMapping;
        }
        std::vector<Ptr<AST::Ty>> typeArgs;
        for (auto& it : usage.GetTypeArgs()) {
            typeArgs.push_back(it->ty);
        }
        auto target = tad.type->GetTarget();
        if (!target || target->astKind != AST::ASTKind::TYPE_ALIAS_DECL) {
            return TypeCheckUtil::GenerateTypeMapping(tad, typeArgs);
        }
        typeMapping = GenerateTypeMappingForTypeAliasDecl(tad);
        SubstituteTypeForTypeAliasTypeMapping(tad, typeArgs, typeMapping);
        return typeMapping;
    }

    void SubstituteTypeArguments(
        const AST::TypeAliasDecl& tad, std::vector<OwnedPtr<AST::Type>>& typeArguments, const TypeSubst& typeMapping);
    // Declarations of higher accessibility cannot use declarations of lower accessibility.
    // Check all non-private toplevel declarations in package.
    void CheckAccessLevelValidity(AST::Package& package);
    void CheckNominalDeclAccessLevelValidity(const AST::InheritableDecl& id);
    void CheckFuncAccessLevelValidity(const AST::FuncDecl& fd);
    void CheckPatternVarAccessLevelValidity(AST::Pattern& pattern);
    void CheckNonPrivateDeclAccessLevelValidity(AST::Decl& decl);

    void CheckCHIRClassDependencies();

    void MarkOutermostBinaryExpressions(AST::Package& pkg) const;
    // FFI Sema Prepare Check.
    void PreCheckAnnoForFFI(AST::Node& root);
    /**
     * Set the ABI information to Foreign Modifier. By default Foreign Modifier has Attribute::C.
     */
    void SetForeignABIAttr(AST::Decl& decl);
    AST::Attribute GetDefaultABI();

    // C FFI Sema Prepare Check.
    void PreCheckAnnoForCFFI(AST::Decl& decl);

    // C FFI Sema Check.
    void CheckCFuncParam(const AST::FuncParam& fp);
    void CheckCFuncParamType(const AST::Type& type);
    void CheckCFuncReturnType(const AST::Type& type);
    void UnsafeCheck(const AST::FuncBody& fb);
    void CheckCTypeMember(const AST::Decl& decl);
    bool IsInCFunc(const ASTContext& ctx, const AST::RefExpr& re) const;
    void CheckInvalidRefInCFunc(const ASTContext& ctx, const AST::RefExpr& re) const;
    void CheckInvalidRefInCFunc(const ASTContext& ctx, const AST::RefExpr& re, const AST::Decl& target) const;
    void CheckEnumFuncDeclIsCStructParam(const AST::FuncDecl& funcDecl);
    bool CheckMatchExprNoSelectorExhaustiveness(AST::MatchExpr& me, bool hasDefault);
    void SetFuncDeclConstructorCall(AST::FuncDecl& fd) const;
    bool IsFuncTyEnumPatternMatched(
        ASTContext& ctx, AST::Ty& target, const AST::FuncTy& funcTy, const AST::EnumPattern& p);

    /**
     * Post legality checks after sema typecheck finished.
     */
    void CheckAccessLegalityOfRefExpr(const ASTContext& ctx, AST::RefExpr& re);
    void CheckAccessLegalityOfMemberAccess(const ASTContext& ctx, AST::MemberAccess& ma);
    void CheckTypeArgLegalityOfJArrayCtor(const AST::NameReferenceExpr& re);
    void CheckStaticMemberAccessLegality(const AST::MemberAccess& ma, const AST::Decl& target);
    void CheckInstanceMemberAccessLegality(const ASTContext& ctx, const AST::MemberAccess& ma, const AST::Decl& target);
    void CheckLegalityOfReference(ASTContext& ctx, AST::Node& node);
    void CheckLegalityOfUnsafeAndInout(AST::Node& root);

    bool ShouldSkipDeprecationDiagnostic(const Ptr<AST::Decl> target, bool strict);
    void CheckUsageOfDeprecatedWithTarget(
        const Ptr<AST::Node> usage,
        const Ptr<AST::Decl> target
    );
    void CheckUsageOfDeprecated(AST::Node& node);
    void CheckUsageOfDeprecatedParameters(const Ptr<AST::Node> usage);
    void CheckUsageOfDeprecatedNominative(
        const Ptr<AST::Node> usage,
        const Ptr<AST::Decl> target
    );
    void CheckOverridingOrRedefiningOfDeprecated(
        const Ptr<AST::Decl> overridden,
        const Ptr<AST::Decl> overriding,
        const std::string& declType
    );
    void CheckOverridingOrRedefinitionOfDeprecatedFunction(
        const Ptr<AST::ClassDecl> cd,
        const Ptr<AST::Decl> member
    );
    void CheckOverridingOrRedefinitionOfDeprecatedProperty(
        const Ptr<AST::ClassDecl> cd,
        const Ptr<AST::Decl> member
    );
    void CheckOverridingOrRedefinitionOfDeprecated(const Ptr<AST::ClassDecl> cd);
    void CheckUsageOfDeprecatedSetter(const Ptr<AST::Node> usage);
    void CheckDeprecationLevelOnInheritors(const Ptr<AST::ClassLikeDecl> classLike);
    bool IsDeprecatedStrict(const Ptr<AST::Decl> decl) const;
    std::optional<std::string> GetDiagnoseKindOfFuncDecl(
            const Ptr<AST::Node> usage,
            const Ptr<AST::Decl> target
    ) const;
    std::string GetDiagnoseKindOfVarDecl(const Ptr<AST::Decl> target) const;
    /**
     * Return name and/or kind of deprecated declaration.
     * Return std::nullopt if the declaration deprecation should not be reported.
    */
    std::optional<std::string> GetKindfOfDeprecatedDeclaration(
        const Ptr<AST::Decl> target, const Ptr<AST::Node> usage) const;
    void DiagnoseDeprecatedUsage(
        const Ptr<AST::Node> usage,
        const Ptr<AST::Decl> target,
        const std::string& nameOfDiagnose = ""
    );

    /**
     * Check memberAccess's targets are legal or not.
     * @param ma member access node.
     * @param targetOfBaseDecl The base namespace of memberAccess, can be package name or type name.
     */
    void IsNamespaceMemberAccessLegal(
        const AST::MemberAccess& ma, AST::Decl& targetOfBaseDecl, const AST::Decl& target);
    void CheckForbiddenMemberAccess(const ASTContext& ctx, const AST::MemberAccess& ma, const AST::Decl& target) const;
    void CheckMutationInStruct(const ASTContext& ctx, const AST::Expr& expr) const;
    void CheckLetInstanceAccessMutableFunc(const ASTContext& ctx, const AST::MemberAccess& ma, const AST::Decl& target);
    bool CheckIfUseInout(const AST::FuncDecl& decl);
    void CheckConstEvaluation(AST::Package& pkg);

    std::optional<Ptr<AST::Ty>> SynLiteralInBinaryExprFromRight(ASTContext& ctx, AST::BinaryExpr& be);
    Ptr<AST::Ty> SynLiteralInBinaryExprFromLeft(ASTContext& ctx, AST::BinaryExpr& be);
    void DiagnoseForSubscriptAssignExpr(ASTContext& ctx, const AST::AssignExpr& ae, std::vector<Diagnostic>& diags);
    std::optional<Ptr<AST::Ty>> InferAssignExprCheckCaseOverloading(
        ASTContext& ctx, AST::AssignExpr& ae, std::vector<Diagnostic>& diags);
    bool PreCheckCompoundAssign(
        ASTContext& ctx, const AST::AssignExpr& ae, const AST::Ty& lTy, const std::vector<Diagnostic>& diags);
    std::optional<Ptr<AST::Ty>> InferBinaryExprCaseBuiltIn(
        ASTContext& ctx, AST::BinaryExpr& be, Ptr<AST::Ty>& inferRet);
    std::optional<bool> CheckBinaryExprCaseBuiltIn(ASTContext& ctx, AST::BinaryExpr& be, Ptr<AST::Ty> target);
    Ptr<AST::Ty> SynLiteralInBinaryExpr(ASTContext& ctx, AST::BinaryExpr& be);
    void SynBinaryLeafs(ASTContext& ctx, AST::BinaryExpr& be);
    void HandleAlias(Ptr<AST::Expr> expr, std::vector<Ptr<AST::Decl>>& targets);
    std::vector<Ptr<AST::Ty>> RecursiveSubstituteTypeAliasInTy(
        Ptr<const AST::Ty> ty, bool needSubstituteGeneric, const TypeSubst& typeMapping = {});
    template <class T>
    void SubstituteTypeArguments(std::vector<OwnedPtr<AST::Type>>& typeArguments, T& type, const TypeSubst& typeMapping)
    {
        std::vector<Ptr<AST::Ty>> typeArgs;
        for (auto& it : typeArguments) {
            typeArgs.push_back(it->ty);
        }
        if (!typeArguments.empty()) {
            Ptr<AST::File> file = typeArguments[0]->curFile;
            std::move(typeArguments.begin(), typeArguments.end(), std::back_inserter(file->trashBin));
            typeArguments.clear();
        }
        for (auto& it : type.typeArguments) {
            auto newTypeArg = AST::ASTCloner::Clone(it.get());
            newTypeArg->ty = newTypeArg->ty ? SubstituteTypeAliasInTy(*newTypeArg->ty, true, typeMapping)
                                            : TypeManager::GetInvalidTy();
            if (auto ity = DynamicCast<AST::IntersectionTy*>(newTypeArg->ty); ity && ity->tys.empty()) {
                continue;
            }
            newTypeArg->EnableAttr(AST::Attribute::COMPILER_ADD);
            typeArguments.emplace_back(std::move(newTypeArg));
        }
    }

    /**
     * Check which functions can be inlined.
     * @param Packages need to be checked.
     */
    void CheckInlineFunctions(const std::vector<Ptr<AST::Package>>& pkgs) const;
    void PerformRecursiveTypesElimination();
    /**
     * Check whether static invocations of all interfaces contain unimplemented invocations.
     * @param node the top level node to be checked.
     */
    void CheckAllInvocationHasImpl(const ASTContext& ctx, AST::Node& node);
    void CheckSubscriptLegality(AST::Node& node);
    std::pair<bool, Ptr<AST::RefExpr>> CheckInvokeTargetHasImpl(const ASTContext& ctx, AST::Ty& interfaceTy,
        AST::Decl& decl, MultiTypeSubst& typeMapping, std::unordered_set<Ptr<AST::Decl>>& traversedDecls);
    Ptr<AST::Decl> GetImplementedTargetIfExist(
        const ASTContext& ctx, const AST::Ty& interfaceTy, AST::Decl& target, const MultiTypeSubst& typeMapping);
    friend class Synthesizer;
    friend class TypeChecker;
    friend class EnumSugarChecker;
    friend class InstCtxScope;
    bool ChkIfAvailableExpr(ASTContext& ctx, AST::Ty& ty, AST::IfAvailableExpr& ie);
    Ptr<AST::Ty> SynIfAvailableExpr(ASTContext& ctx, AST::IfAvailableExpr& iae);

    /** Members */
    Promotion promotion;
    TypeManager& typeManager;
    CompilerInstance* const ci{nullptr};
    DiagnosticEngine& diag;
    ImportManager& importManager;
    ScopeManager scopeManager;
    std::unordered_map<Ptr<AST::File>, std::unordered_set<Ptr<AST::Decl>>> mainFunctionMap;
    std::unordered_map<Ptr<const AST::FuncDecl>, bool> inoutCache;
    Triple::BackendType backendType;
    // outermost @Derpecated declaration
    Ptr<AST::Node> deprecatedContext = nullptr;
    // strict version of outermost @Deprecated declaration
    Ptr<AST::Node> strictDeprecatedContext = nullptr;
};
} // namespace Cangjie
#endif
