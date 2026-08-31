// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the Utility functions for TypeCheck.
 */

#ifndef CANGJIE_SEMA_TYPECHECKUTIL_H
#define CANGJIE_SEMA_TYPECHECKUTIL_H

#include <unordered_set>

#include "ScopeManager.h"
#include "cangjie/AST/Cache.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Modules/ImportManager.h"
#include "cangjie/Sema/TypeManager.h"

#include <unordered_set>

namespace Cangjie::TypeCheckUtil {
using MemSigSet = std::unordered_set<AST::MemSig, AST::MemSigHash>;

// The comparison result of two types.
// If left < right, return LT.
// If left > right, return GT.
// If left == right, return EQ.
enum class ComparisonRes { LT, GT, EQ };

/*
 * Questable nodes need to allow QuestTy in target type and
 * use synthesized type instead of target type as its own ty.
 */
bool IsQuestableNode(const AST::Node& n);
bool AcceptPlaceholderTarget(const AST::Node& n);

std::vector<AST::TypeKind> GetIdealTypesByKind(AST::TypeKind type);

constexpr std::string_view WILDCARD_CHAR{"_"};

struct FuncSig {
    std::string identifier;
    std::optional<ModalInfo> thisMode;
    std::vector<AST::ModalTy> paramTys;
};
struct FuncSigCmp {
    bool operator()(const FuncSig& lhs, const FuncSig& rhs) const;
};
using FuncSig2Decl = std::map<FuncSig, Ptr<AST::FuncDecl>, FuncSigCmp>;

template <typename T> inline OwnedPtr<T> MakeOwnedNode()
{
    auto ptr = MakeOwned<T>();
    ptr->EnableAttr(AST::Attribute::COMPILER_ADD);
    return ptr;
}

template <typename T, typename... _Args> inline OwnedPtr<T> MakeOwnedNode(_Args&&... args)
{
    auto ptr = MakeOwned<T>(std::forward<_Args>(args)...);
    ptr->EnableAttr(AST::Attribute::COMPILER_ADD);
    return ptr;
}

template <typename T> bool IsAllFuncDecl(T& results)
{
    for (auto it : results) {
        if (it && it->astKind != AST::ASTKind::FUNC_DECL && it->astKind != AST::ASTKind::MACRO_DECL) {
            return false;
        }
    }
    return true;
}

inline std::vector<AST::DataTy> GetInstanationTys(const AST::Expr& expr)
{
    auto ref = DynamicCast<const AST::NameReferenceExpr*>(&expr);
    return ref ? ref->instTys : std::vector<AST::DataTy>{};
}

template <typename T> void RemoveDuplicateElements(std::vector<T>& candidates)
{
    // Remove duplicate elements in vector.
    std::set<T> uniqueCandidates;
    for (auto it = candidates.begin(); it != candidates.end();) {
        if (uniqueCandidates.find(*it) != uniqueCandidates.end()) {
            it = candidates.erase(it);
        } else {
            uniqueCandidates.insert(*it);
            ++it;
        }
    }
}

inline bool NeedSynOnUsed(const AST::Decl& target)
{
    // Type decls, func param and other well-typed imported decls are no need to be checked again recursively,
    // because the ty is already set at PreCheck stage.
    // Source imported function will not been checked from toplevel, so we must synthesize it when used.
    return !target.IsTypeDecl() && target.astKind != AST::ASTKind::FUNC_PARAM &&
        (!target.GetTy().IsCorrect() || target.GetTy()->HasQuestTy());
}

std::string GetFullInheritedTy(AST::ExtendDecl& extend);
void UpdateInstTysWithTypeArgs(AST::NameReferenceExpr& expr);
void SetIsNotAlone(AST::Expr& baseExpr);
void ModifyTargetOfRef(AST::RefExpr& re, Ptr<AST::Decl> decl, const std::vector<Ptr<AST::Decl>>& targets);
void AddFuncTargetsForMemberAccess(AST::MemberAccess& ma, const std::vector<Ptr<AST::Decl>>& targets);
void ReplaceTarget(Ptr<AST::Node> node, Ptr<AST::Decl> target, bool insertTarget = true);
void MarkParamWithInitialValue(AST::Node& root);

bool HasIntersectionTy(const std::vector<Ptr<AST::Type>>& types);
bool NeedFurtherInstantiation(const std::vector<Ptr<AST::Type>>& types);
bool IsOverloadableOperator(TokenKind op);
bool CanSkipDiag(const AST::Node& node);
bool IsFieldOperator(const std::string& field);
bool IsGenericUpperBoundCall(const AST::Expr& expr, AST::Decl& target);
bool IsNode1ScopeVisibleForNode2(const AST::Node& node1, const AST::Node& node2);
size_t CountOptionNestedLevel(AST::DataTy ty);
AST::ModalTy UnboxOptionType(AST::ModalTy ty);

/**
 * Check if ThisType compatibility in class inheritance
 */
bool CheckThisTypeCompatibility(const AST::FuncDecl& parentFunc, const AST::FuncDecl& childFunc);
bool IsFuncReturnThisType(const AST::FuncDecl& fd);
/** Return true if @p pkg has main_decl. */
bool HasMainDecl(AST::Package& pkg);
ComparisonRes CompareIntAndFloat(const AST::Ty& left, const AST::Ty& right);
/**
 * Util functions for manipulating 'MultiTypeSubst'.
 */
std::set<TypeSubst> ExpandMultiTypeSubst(
    TypeManager& tm, const MultiTypeSubst& mts, const std::set<AST::DataTy>& usefulTys);
std::vector<SubstPack> ExpandMultiTypeSubst(const SubstPack& maps, const std::set<AST::DataTy>& usefulTys);
/**
 * Reduce type mapping to only contains direct mapping from given generic ty vars to instantiated tys.
 */
MultiTypeSubst ReduceMultiTypeSubst(TypeManager& tyMgr, const TyVars& tyVars, const MultiTypeSubst& mts);
TypeSubst MultiTypeSubstToTypeSubst(const MultiTypeSubst& mts);
TypeSubst GenerateTypeMappingByTy(AST::DataTy genericTy, AST::DataTy instantTy);
TypeSubst GenerateTypeMapping(const AST::Decl& decl, const std::vector<AST::DataTy>& typeArgs);
void GenerateTypeMapping(
    TypeManager& tyMgr, SubstPack& m, const AST::Decl& decl, const std::vector<AST::DataTy>& typeArgs);
void RelayMappingFromExtendToExtended(TypeManager& tyMgr, SubstPack& m, const AST::ExtendDecl& decl);
TypeSubst InverseMapping(const TypeSubst& typeMapping);
void MergeTypeSubstToMultiTypeSubst(MultiTypeSubst& mts, const TypeSubst& typeMapping);
void MergeMultiTypeSubsts(MultiTypeSubst& target, const MultiTypeSubst& src);
/* u2i map can't have conflict between the two */
void MergeSubstPack(SubstPack& target, const SubstPack& src);
/** Get set of generic sema tys used in given @p ty */
std::unordered_set<AST::DataTy> GetAllGenericTys(AST::DataTy ty);
std::vector<AST::DataTy> GetDeclTypeParams(const AST::Decl& decl);
/** Get mapped type of given @p tyVar . If the tyVar is not mapped in TypeSubst/MultiTypeSubst, return itself. */
AST::DataTy GetMappedTy(const MultiTypeSubst& mts, TyVar* tyVar);
AST::DataTy GetMappedTy(const TypeSubst& typeMapping, TyVar* tyVar);
/** Occurs check for tyVars in @p typeMapping */
bool HaveCyclicSubstitution(TypeManager& tyMgr, const TypeSubst& typeMapping);

/**
 * Get parameter tys of given function declaration @p fd.
 */
std::vector<AST::ModalTy> GetParamTys(const AST::FuncDecl& fd);
std::vector<AST::ModalTy> GetFuncBodyParamTys(const AST::FuncBody& fb);
/**
 * Check whether src is an override or implementation of target.
 */
bool IsOverrideOrShadow(TypeManager& typeManager, const AST::FuncDecl& src, const AST::FuncDecl& target,
    AST::DataTy baseTy = {}, ModalInfo baseMode = {}, AST::DataTy expectInstParent = {}, ModalInfo parentMode = {});
bool IsOverrideOrShadow(
    TypeManager& typeManager, const AST::PropDecl& src, const AST::PropDecl& target, AST::DataTy baseTy = {});
MultiTypeSubst GenerateTypeMappingBetweenFuncs(
    TypeManager& typeManager, const AST::FuncDecl& src, const AST::FuncDecl& target);
/** Get real target decl since given decl maybe typealias decl. */
Ptr<AST::Decl> GetRealTarget(Ptr<AST::Decl> decl);
/**
 * Return function targets for a reference node.
 */
std::vector<Ptr<AST::FuncDecl>> GetFuncTargets(const AST::Node& node);
/**
 * Given a referenced target @p decl from RefExpr or MemberAccess.
 * Return the pair of 'isGetter' status and the real member in nominal decl.
 */
std::pair<bool, Ptr<AST::Decl>> GetRealMemberDecl(AST::Decl& decl);
/**
 * Given a member @p decl found in nominal decl and a possible getter status @p isGetter for propDecl.
 * Return the real used decl as a referenced target.
 * Return 'decl' it self for non-propDecl, return getter/setter function for propDecl.
 */
Ptr<AST::Decl> GetUsedMemberDecl(AST::Decl& decl, bool isGetter);
std::string DeclKindToString(const AST::Decl& decl);
/**
 * Get string of given decls' ast type.
 */
std::string GetTypesStr(std::vector<Ptr<AST::Decl>>& decls);
std::pair<Ptr<AST::FuncDecl>, Ptr<AST::FuncDecl>> GetUsableGetterSetterForProperty(AST::PropDecl& pd);

/// Get usable accessor for prop. when such accessor is not found in this prop, try to find in parent classes if any.
/// Only prop with the same modal as @ref pd will be considered.
Ptr<AST::FuncDecl> GetUsableAccessorForProperty(AST::PropDecl& pd, bool isGetter);

/** Collect all related extends of 'decl' and it's super classes' extends if exist. */
std::set<Ptr<AST::ExtendDecl>> CollectAllRelatedExtends(TypeManager& tyMgr, AST::InheritableDecl& boxedDecl);
OwnedPtr<AST::FuncDecl> CreateDefaultCtor(AST::InheritableDecl& decl, bool isStatic = false);
/// FuncBody of a func-like node (FuncDecl / LambdaExpr / MacroDecl / PrimaryCtorDecl).
/// Returns null for nodes that are not one of these kinds. Used by GetCurFuncBody and by any
/// checker that already holds the node it wants the body of (so it does not have to go through the scope lookup).
Ptr<AST::FuncBody> GetFuncBody(AST::Node& funcLike);
Ptr<AST::FuncBody> GetCurFuncBody(const ASTContext& ctx, const std::string& scopeName);
/** Get the outer inheritable decl where the current context is. */
inline Ptr<AST::InheritableDecl> GetCurInheritableDecl(const ASTContext& ctx, const std::string& scopeName)
{
    auto sym = ScopeManager::GetCurSymbolByKind(SymbolKind::STRUCT, ctx, scopeName);
    return sym ? DynamicCast<AST::InheritableDecl*>(sym->node) : nullptr;
}

/* Utils for TypeCheckCall and TypeArgumentInference */
bool IsCStringConstructor(const AST::FuncDecl& fd);
bool IsEnumCtorWithoutTypeArgs(const AST::Expr& expr, Ptr<const AST::Decl> target);
TyVars GetTyVars(const AST::FuncDecl& fd, const AST::CallExpr& ce, bool ignoreContext = false);
TyVars GetTyVarsToSolve(const SubstPack& maps);
bool HasTyVarsToSolve(const SubstPack& maps);
bool HasUnsolvedTyVars(const TypeSubst& subst, const std::set<Ptr<TyVar>>& tyVars);
std::vector<AST::ModalTy> GetParamTysInArgsOrder(TypeManager& tyMgr, const AST::CallExpr& ce, const AST::FuncDecl& fd);
Ptr<AST::Generic> GetCurrentGeneric(const AST::FuncDecl& fd, const AST::CallExpr& ce);
std::string GetArgName(const AST::FuncDecl& fd, const AST::FuncArg& arg);
std::optional<std::pair<AST::ModalTy, size_t>> GetParamTyAccordingToArgName(
    const AST::FuncDecl& fd, const std::string argName);
inline bool IsTypeObjectCreation(const AST::FuncDecl& fd, const AST::CallExpr& ce)
{
    // Get type variables from the outer (class, struct, enum) declaration of a constructor function
    // when call by type name (enum constructor is alway treated as called by typename).
    bool isTypeNameCall =
        ce.callKind == AST::CallKind::CALL_OBJECT_CREATION || ce.callKind == AST::CallKind::CALL_STRUCT_CREATION;
    return (isTypeNameCall && fd.TestAttr(AST::Attribute::CONSTRUCTOR)) ||
        fd.TestAttr(AST::Attribute::ENUM_CONSTRUCTOR);
}

/**
 * Get a specific modifier of a given declaration @p d.
 */
Ptr<const AST::Modifier> FindModifier(const AST::Decl& d, TokenKind kind);

/**
 * Returns the first annotation occurrence of a given kind on the declaration, returns null pointer
 * if no annotation of the given kind is found.
 */
Ptr<AST::Annotation> FindFirstAnnotation(const AST::Decl& decl, AST::AnnotationKind kind);

ModalInfo GetThisParamModal(const AST::FuncDecl& fd);
ModalInfo GetThisParamModal(const AST::Decl& decl);

bool HasDefaultImpl(const AST::Decl& decl);

/**
 * From @p scopeName's current scope, walk outward to the first non-static FuncDecl
 * Returns that function's `this` parameter modal.
 */
ModalInfo GetCurThisModal(const ASTContext& ctx, const std::string& scopeName);

inline bool HasCFuncAttr(const AST::Decl& decl)
{
    return decl.TestAnyAttr(AST::Attribute::C, AST::Attribute::FOREIGN);
}

void AddArrayLitConstructor(AST::ArrayLit& al);

bool IsNeedRuntimeCheck(TypeManager& typeManager, AST::DataTy srcTy, AST::DataTy targetTy);

Ptr<AST::TypeAliasDecl> GetLastTypeAliasTarget(AST::TypeAliasDecl& decl);

// find the type that is subtype/supertype of all types. subtype or supertype is specified by lessThan
AST::DataTy FindSmallestTy(
    const std::set<AST::DataTy>& tys, const std::function<bool(AST::DataTy, AST::DataTy)>& lessThan);
bool LessThanAll(
    AST::DataTy ty, const std::set<AST::DataTy>& tys, const std::function<bool(AST::DataTy, AST::DataTy)>& lessThan);
// `memSigs` are the member usages that produced `candidates`; when given, ambiguous generic
// candidates whose generic params cannot all be determined by these usages are not added to
// the sum constraint, since their placeholder type args would stay free forever.
void TryEnforceCandidate(TyVar& tv, const std::set<Ptr<AST::Decl>>& candidates, TypeManager& tyMgr,
    const std::vector<AST::MemSig>& memSigs = {}, const MemSigSet& resultConstrainedMemSigs = {});
/// all used TypeKind's are Copy type, use DataTy is enough
std::set<AST::DataTy> TypeMapToTys(const std::map<AST::TypeKind, AST::TypeKind>& m, bool fromKey);
// get generic params for the decl and outer decl(if there is) and extended decl(if there is)
std::set<AST::DataTy> GetGenericParamsForDecl(const AST::Decl& decl);
// get generic params for the decl of the type
std::set<AST::DataTy> GetGenericParamsForTy(AST::ModalTy ty);
// get generic params for all decls used in the call
std::set<AST::DataTy> GetGenericParamsForCall(const AST::CallExpr& ce, const AST::FuncDecl& fd);

OwnedPtr<AST::ThrowExpr> CreateThrowException(const AST::ClassDecl& exceptionDecl,
    std::vector<OwnedPtr<AST::Expr>> args, AST::File& curFile, TypeManager& typeManager);
std::optional<std::pair<Ptr<AST::FuncDecl>, AST::DataTy>> FindInitDecl(const AST::InheritableDecl& decl,
    TypeManager& typeManager, std::vector<OwnedPtr<AST::Expr>>& valueArgs, const std::vector<AST::DataTy> instTys = {});
std::optional<std::pair<Ptr<AST::FuncDecl>, AST::DataTy>> FindInitDecl(const AST::InheritableDecl& decl,
    TypeManager& typeManager, const std::vector<AST::ModalTy> valueParamTys,
    const std::vector<AST::DataTy> instTys = {});
OwnedPtr<AST::CallExpr> CreateInitCall(const std::pair<Ptr<AST::FuncDecl>, AST::DataTy> initDeclInfo,
    std::vector<OwnedPtr<AST::Expr>>& valueArgs, AST::File& curFile, const std::vector<AST::DataTy> instTys = {});

Ptr<AST::FuncDecl> GenerateGetTypeForTypeParamIntrinsic(AST::Package& pkg, TypeManager& typeManager);

// Generates declaration of intrinsic that is roughly eqivalent to expression `TypeLeft is TypeRight`,
// where `TypeLeft` and `TypeRight` are types and the result is true iff TypeLeft is subtype of TypeRight:
// func isSubtypeTypes<T, E>() {
//   return T is E
// }
Ptr<AST::FuncDecl> GenerateIsSubtypeTypesIntrinsic(AST::Package& pkg, TypeManager& typeManager);

OwnedPtr<AST::GenericParamDecl> CreateGenericParamDecl(
    AST::Decl& decl, const std::string& name, TypeManager& typeManager);
OwnedPtr<AST::GenericParamDecl> CreateGenericParamDecl(AST::Decl& decl, TypeManager& typeManager);

template <typename T>
T* GetMemberDecl(
    const AST::Decl& decl, const std::string& identifier, std::vector<AST::ModalTy> paramTys, TypeManager& typeManager)
{
    for (auto& member : decl.GetMemberDecls()) {
        if (member->identifier != identifier) {
            continue;
        }
        bool isSuitableDecl = true;
        if (auto funcMember = DynamicCast<AST::FuncDecl>(member.get()); funcMember) {
            auto originalParamTys = RawStaticCast<const AST::FuncTy*>(funcMember->DataTy())->paramTys;
            if (originalParamTys.size() != paramTys.size()) {
                continue;
            }
            for (std::vector<AST::ModalTy>::size_type i = 0; i < paramTys.size(); i++) {
                // Object super type is added later, at "desugar after type instantiation" stage,
                // so we check it here explicitly
                if (originalParamTys[i]->IsObject() && paramTys[i]->IsClass()) {
                    continue;
                }
                if (!typeManager.IsSubtype(paramTys[i], originalParamTys[i])) {
                    isSuitableDecl = false;
                }
            }
        }
        if (isSuitableDecl) {
            return DynamicCast<T*>(member.get());
        }
    }

    return nullptr;
}

/**
 * Filter out the targets without access rights in the searched targets.
 * @param curComposite Represents the position of the referrer.
 * NOTICE: Whether the modifier will be downgraded in spec is not yet determined.
 */
bool IsLegalAccess(AST::Symbol* curComposite, const AST::Decl& d, const AST::Node& node, ImportManager& importManager,
    TypeManager& typeManager);

/**
 * Find the corresponding common declaration for a given specific declaration.
 * @param specificDecl The specific declaration to find the common declaration for.
 * @return The corresponding common declaration if found, nullptr otherwise.
 */
Ptr<AST::Decl> FindCorrespondingCommonDecl(const AST::Decl& specificDecl);

/**
 * Work for all members of a declaration, including getters and setters of property declarations.
 * @param decl The declaration to work for its members.
 * @param worker The worker function to be applied to each member declaration.
 */
inline void WorkForMembers(AST::Decl& decl, const std::function<void(AST::Decl&)>& worker)
{
    if (decl.astKind == AST::ASTKind::PROP_DECL) {
        auto& pd = static_cast<AST::PropDecl&>(decl);
        std::for_each(pd.getters.begin(), pd.getters.end(), [&worker](auto& fd) { worker(*fd); });
        std::for_each(pd.setters.begin(), pd.setters.end(), [&worker](auto& fd) { worker(*fd); });
    } else {
        worker(decl);
    }
}

inline void WorkForMembersOfDecl(const AST::Decl& decl, const std::function<void(AST::Decl&)>& worker)
{
    for (auto& member : decl.GetMemberDecls()) {
        WorkForMembers(*member, worker);
    }
}
} // namespace Cangjie::TypeCheckUtil
#endif
