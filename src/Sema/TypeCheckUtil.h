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

#include "ScopeManager.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Modules/ImportManager.h"
#include "cangjie/Sema/TypeManager.h"

namespace Cangjie::TypeCheckUtil {
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

using FuncSig = std::pair<std::string, std::vector<Ptr<AST::Ty>>>;
struct FuncSigCmp {
    bool operator()(const FuncSig& lhs, const FuncSig& rhs) const
    {
        if (lhs.first != rhs.first) {
            return lhs.first < rhs.first;
        }
        if (lhs.second.size() != rhs.second.size()) {
            return lhs.second.size() < rhs.second.size();
        }
        // If 'lhs' and 'rhs' have same size of func parameter types,
        // compare each type's kind, type name, type definition position and ptr value in order.
        for (size_t i = 0; i < lhs.second.size(); ++i) {
            if (CompTyByNames(lhs.second[i], rhs.second[i])) {
                return true;
            }
        }
        return false;
    }
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

inline std::vector<Ptr<AST::Ty>> GetInstanationTys(const AST::Expr& expr)
{
    auto ref = DynamicCast<const AST::NameReferenceExpr*>(&expr);
    return ref ? ref->instTys : std::vector<Ptr<AST::Ty>>{};
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
        (!AST::Ty::IsTyCorrect(target.ty) || target.ty->HasQuestTy());
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
size_t CountOptionNestedLevel(const AST::Ty& ty);
Ptr<AST::Ty> UnboxOptionType(Ptr<AST::Ty> ty);

/**
 * Check if ThisType compatibility in class inheritance
 */
bool CheckThisTypeCompatibility(const AST::FuncDecl& parentFunc, const AST::FuncDecl& childFunc);
bool IsFuncReturnThisType(const AST::FuncDecl& fd);
/** Return true if @p pkg has main_decl. */
bool HasMainDecl(AST::Package& pkg);
/** Compare numeric type by builtin strategy. */
ComparisonRes CompareIntAndFloat(const AST::Ty& left, const AST::Ty& right);
/**
 * Util functions for manipulating 'MultiTypeSubst'.
 */
std::set<TypeSubst> ExpandMultiTypeSubst(const MultiTypeSubst& mts, const std::set<Ptr<AST::Ty>>& usefulTys);
std::vector<SubstPack> ExpandMultiTypeSubst(const SubstPack& maps, const std::set<Ptr<AST::Ty>>& usefulTys);
/**
 * Reduce type mapping to only contains direct mapping from given generic ty vars to instantiated tys.
 */
MultiTypeSubst ReduceMultiTypeSubst(TypeManager& tyMgr, const TyVars& tyVars,
    const MultiTypeSubst& mts);
TypeSubst MultiTypeSubstToTypeSubst(const MultiTypeSubst& mts);
TypeSubst GenerateTypeMappingByTy(const Ptr<AST::Ty> genericTy, const Ptr<AST::Ty> instantTy);
TypeSubst GenerateTypeMapping(const AST::Decl& decl, const std::vector<Ptr<AST::Ty>>& typeArgs);
void GenerateTypeMapping(
    TypeManager& tyMgr, SubstPack& m, const AST::Decl& decl, const std::vector<Ptr<AST::Ty>>& typeArgs);
void RelayMappingFromExtendToExtended(TypeManager& tyMgr, SubstPack& m, const AST::ExtendDecl& decl);
TypeSubst InverseMapping(const TypeSubst& typeMapping);
void MergeTypeSubstToMultiTypeSubst(MultiTypeSubst& mts, const TypeSubst& typeMapping);
void MergeMultiTypeSubsts(MultiTypeSubst& target, const MultiTypeSubst& src);
/* u2i map can't have conflict between the two */
void MergeSubstPack(SubstPack& target, const SubstPack& src);
/** Get set of generic sema tys used in given @p ty */
std::unordered_set<Ptr<AST::Ty>> GetAllGenericTys(Ptr<AST::Ty> const ty);
std::vector<Ptr<AST::Ty>> GetDeclTypeParams(const AST::Decl& decl);
/** Get mapped type of given @p tyVar . If the tyVar is not mapped in TypeSubst/MultiTypeSubst, return itself. */
Ptr<AST::Ty> GetMappedTy(const MultiTypeSubst& mts, TyVar* tyVar);
Ptr<AST::Ty> GetMappedTy(const TypeSubst& typeMapping, TyVar* tyVar);
/** Occurs check for tyVars in @p typeMapping */
bool HaveCyclicSubstitution(TypeManager& tyMgr, const TypeSubst& typeMapping);

/**
 * Get parameter tys of given function declaration @p fd.
 */
std::vector<Ptr<AST::Ty>> GetParamTys(const AST::FuncDecl& fd);
std::vector<Ptr<AST::Ty>> GetFuncBodyParamTys(const AST::FuncBody& fb);
/**
 * Check whether src is an override or implementation of target.
 */
bool IsOverrideOrShadow(TypeManager& typeManager, const AST::FuncDecl& src, const AST::FuncDecl& target,
    const Ptr<AST::Ty> baseTy = nullptr, const Ptr<AST::Ty> expectInstParent = nullptr);
bool IsOverrideOrShadow(
    TypeManager& typeManager, const AST::PropDecl& src, const AST::PropDecl& target, Ptr<AST::Ty> baseTy = nullptr);
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
Ptr<AST::FuncDecl> GetUsableGetterForProperty(AST::PropDecl& pd);
Ptr<AST::FuncDecl> GetUsableSetterForProperty(AST::PropDecl& pd);
/** Collect all related extends of 'decl' and it's super classes' extends if exist. */
std::set<Ptr<AST::ExtendDecl>> CollectAllRelatedExtends(TypeManager& tyMgr, AST::InheritableDecl& boxedDecl);
std::unordered_set<Ptr<AST::Ty>> GetContextGenericTys(const ASTContext& ctx, const AST::Expr& expr);
OwnedPtr<AST::FuncDecl> CreateDefaultCtor(AST::InheritableDecl& decl, bool isStatic = false);
Ptr<AST::FuncBody> GetCurFuncBody(const ASTContext& ctx, const std::string& scopeName);
/** Get the outer inheritable decl where the current context is. */
inline Ptr<AST::InheritableDecl> GetCurInheritableDecl(const ASTContext& ctx, const std::string& scopeName)
{
    auto sym = ScopeManager::GetCurSymbolByKind(SymbolKind::STRUCT, ctx, scopeName);
    return sym ? DynamicCast<AST::InheritableDecl*>(sym->node) : nullptr;
}

/* Utils for TypeCheckCall and TypeArgumentInference */
bool IsEnumCtorWithoutTypeArgs(const AST::Expr& expr, Ptr<const AST::Decl> target);
TyVars GetTyVars(const AST::FuncDecl& fd, const AST::CallExpr& ce, bool ignoreContext = false);
TyVars GetTyVarsToSolve(const SubstPack& maps);
bool HasTyVarsToSolve(const SubstPack& maps);
bool HasUnsolvedTyVars(const TypeSubst& subst, const std::set<Ptr<TyVar>>& tyVars);
std::vector<Ptr<AST::Ty>> GetParamTysInArgsOrder(TypeManager& tyMgr, const AST::CallExpr& ce, const AST::FuncDecl& fd);
Ptr<AST::Generic> GetCurrentGeneric(const AST::FuncDecl& fd, const AST::CallExpr& ce);
std::string GetArgName(const AST::FuncDecl& fd, const AST::FuncArg& arg);
std::optional<std::pair<Ptr<AST::Ty>, size_t>> GetParamTyAccordingToArgName(
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
inline bool HasCFuncAttr(const AST::Decl& decl)
{
    return decl.TestAnyAttr(AST::Attribute::C, AST::Attribute::FOREIGN);
}

void AddArrayLitConstructor(AST::ArrayLit& al);

bool IsNeedRuntimeCheck(TypeManager& typeManager, AST::Ty& srcTy, AST::Ty& targetTy);

Ptr<AST::TypeAliasDecl> GetLastTypeAliasTarget(AST::TypeAliasDecl& decl);

// find the type that is subtype/supertype of all types. subtype or supertype is specified by lessThan
Ptr<AST::Ty> FindSmallestTy(
    const std::set<Ptr<AST::Ty>>& tys, const std::function<bool(Ptr<AST::Ty>, Ptr<AST::Ty>)>& lessThan);
bool LessThanAll(Ptr<AST::Ty> ty, const std::set<Ptr<AST::Ty>>& tys,
    const std::function<bool(Ptr<AST::Ty>, Ptr<AST::Ty>)>& lessThan);
void TryEnforceCandidate(TyVar& tv, const std::set<Ptr<AST::Decl>>& candidates, TypeManager& tyMgr);
std::set<Ptr<AST::Ty>> TypeMapToTys(const std::map<AST::TypeKind, AST::TypeKind>& m, bool fromKey);
// get generic params for the decl and outer decl(if there is) and extended decl(if there is)
std::set<Ptr<AST::Ty>> GetGenericParamsForDecl(const AST::Decl& decl);
// get generic params for the decl of the type
std::set<Ptr<AST::Ty>> GetGenericParamsForTy(const AST::Ty& ty);
// get generic params for all decls used in the call
std::set<Ptr<AST::Ty>> GetGenericParamsForCall(const AST::CallExpr& ce, const AST::FuncDecl& fd);

OwnedPtr<AST::ThrowExpr> CreateThrowException(
    const AST::ClassDecl& exceptionDecl, std::vector<OwnedPtr<AST::Expr>> args,
    AST::File& curFile, TypeManager& typeManager);
std::optional<std::pair<Ptr<AST::FuncDecl>, Ptr<AST::Ty>>> FindInitDecl(
    const AST::InheritableDecl& decl, TypeManager& typeManager,
    std::vector<OwnedPtr<AST::Expr>>& valueArgs, const std::vector<Ptr<AST::Ty>> instTys = {});
std::optional<std::pair<Ptr<AST::FuncDecl>, Ptr<AST::Ty>>> FindInitDecl(
    const AST::InheritableDecl& decl, TypeManager& typeManager,
    const std::vector<Ptr<AST::Ty>> valueParamTys, const std::vector<Ptr<AST::Ty>> instTys = {});
OwnedPtr<AST::CallExpr> CreateInitCall(
    const std::pair<Ptr<AST::FuncDecl>, Ptr<AST::Ty>> initDeclInfo,
    std::vector<OwnedPtr<AST::Expr>>& valueArgs,
    AST::File& curFile, const std::vector<Ptr<AST::Ty>> instTys = {});

Ptr<AST::FuncDecl> GenerateGetTypeForTypeParamIntrinsic(
    AST::Package& pkg, TypeManager& typeManager, Ptr<AST::Ty> strTy);

// Generates declaration of intrinsic that is roughly eqivalent to expression `TypeLeft is TypeRight`,
// where `TypeLeft` and `TypeRight` are types and the result is true iff TypeLeft is subtype of TypeRight:
// func isSubtypeTypes<T, E>() {
//   return T is E
// }
Ptr<AST::FuncDecl> GenerateIsSubtypeTypesIntrinsic(AST::Package& pkg, TypeManager& typeManager);

OwnedPtr<AST::GenericParamDecl> CreateGenericParamDecl(
    AST::Decl& decl, const std::string& name, TypeManager& typeManager);
OwnedPtr<AST::GenericParamDecl> CreateGenericParamDecl(AST::Decl& decl, TypeManager& typeManager);

static bool const IS_GENERIC_INSTANTIATION_ENABLED =
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    false;
#endif

template <typename T> T* GetMemberDecl(
    const AST::Decl& decl, const std::string& identifier, std::vector<Ptr<AST::Ty>> paramTys, TypeManager& typeManager)
{
    for (auto& member : decl.GetMemberDecls()) {
        if (member->identifier != identifier) {
            continue;
        }
        bool isSuitableDecl = true;
        if (auto funcMember = DynamicCast<AST::FuncDecl>(member.get()); funcMember) {
            auto originalParamTys = RawStaticCast<const AST::FuncTy*>(funcMember->ty)->paramTys;
            if (originalParamTys.size() != paramTys.size()) {
                continue;
            }
            for (std::vector<Ptr<AST::Ty>>::size_type i = 0; i < paramTys.size(); i++) {
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
} // namespace Cangjie::TypeCheckUtil
#endif
