// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file manages type check information that should be controlled by scope.
 */

#include "ExtraScopes.h"
#include "Promotion.h"
#include "TypeCheckUtil.h"
#include "cangjie/AST/Match.h"
#include <algorithm>

namespace Cangjie {
using namespace AST;
using namespace TypeCheckUtil;

namespace {
inline bool IsCurrentGeneric(const FuncDecl& fd, const CallExpr& ce)
{
    return GetCurrentGeneric(fd, ce) != nullptr;
}

bool IsBaseTypeOmittedTypeArgs(const MemberAccess& ma)
{
    // Caller guarantees 'ma.baseExpr' and 'ma.baseExpr->GetTy()' valid.
    // For the case like 'A.add()' or 'pkg.A.add()' that type 'A' is generic type without type argument.
    auto baseTarget = TypeCheckUtil::GetRealTarget(ma.baseExpr->GetTarget());
    bool isGenericType = baseTarget && baseTarget->IsTypeDecl() && baseTarget->TestAttr(Attribute::GENERIC);
    if (!isGenericType) {
        return false;
    }
    if (IsThisOrSuper(*ma.baseExpr)) {
        return false; // Return false is base expr is 'this' or 'super'.
    }
    auto nre = DynamicCast<NameReferenceExpr*>(ma.baseExpr.get());
    return nre && nre->instTys.empty() && NeedFurtherInstantiation(ma.baseExpr->GetTypeArgs());
}

// Type args from type-alias expansion (compiler-added) are not user-provided when they contain
// type variables (need inference). When all are concrete (e.g. TypeC = TypeClass<Int64>), keep them.
std::vector<Ptr<Type>> GetEffectiveTypeArgsForCall(Expr& baseFunc, std::vector<Ptr<Type>> typeArgs)
{
    if (baseFunc.astKind != ASTKind::REF_EXPR) {
        return typeArgs;
    }
    auto* ref = StaticCast<RefExpr*>(&baseFunc);
    if (!ref->compilerAddedTyArgs) {
        return typeArgs;
    }
    bool hasTyVar = std::any_of(typeArgs.begin(), typeArgs.end(),
        [](const Ptr<Type>& ta) { return ta && ta->GetTy() && ta->TyKind() == TypeKind::TYPE_GENERICS; });
    return hasTyVar ? std::vector<Ptr<Type>>{} : typeArgs;
}
}

TyVarScope::TyVarScope(TypeManager& tyMgr) : tyMgr(tyMgr)
{
    tyMgr.tyVarScopes.push_back(this);
}

TyVarScope::~TyVarScope()
{
    for (auto tyVar: tyVars) {
        tyMgr.ReleaseTyVar(tyVar);
    }
    tyMgr.tyVarScopes.pop_back();
}

void TyVarScope::AddTyVar(Ptr<GenericsTy> tyVar)
{
    tyVars.push_back(tyVar);
}

InstCtxScope::InstCtxScope(TypeChecker::TypeCheckerImpl& typeChecker)
    :tyMgr(typeChecker.typeManager), diag(typeChecker.diag), typeChecker(typeChecker)
{
    tyMgr.instCtxScopes.push_back(this);
}

InstCtxScope::~InstCtxScope()
{
    tyMgr.instCtxScopes.pop_back();
}

void InstCtxScope::SetRefDecl(const AST::Decl& decl, AST::ModalTy instTy)
{
    if (!decl.generic) {
        return;
    }
    auto& genParams = decl.generic->typeParameters;
    auto typeArgs = instTy->TyArgs();
    CJC_ASSERT(genParams.size() == typeArgs.size());
    refMaps = {};
    for (size_t i = 0; i < typeArgs.size(); i++) {
        auto itv = tyMgr.AllocTyVar();
        refMaps.u2i[StaticCast<GenericsTy*>(genParams[i]->DataTy())] = itv;
        refMaps.inst[itv].insert(typeArgs[i]);
    }
    maps = curMaps;
    MergeSubstPack(maps, refMaps);
}

bool InstCtxScope::GenerateTypeMappingByCallContext(
    const ASTContext& ctx, const FuncDecl& fd, const CallExpr& ce, SubstPack& typeMapping)
{
    CJC_NULLPTR_CHECK(ce.baseFunc);
    CJC_ASSERT(ce.baseFunc->astKind == ASTKind::REF_EXPR);
    bool invalid = !fd.outerDecl || !fd.outerDecl->GetTy().IsCorrect();
    if (invalid) {
        return false;
    }
    auto structDecl = fd.outerDecl;
    if (!structDecl->TestAttr(Attribute::GENERIC)) {
        return true; // If current outer nominal decl is not generic, ignore.
    }
    auto sym = ScopeManager::GetCurSymbolByKind(SymbolKind::STRUCT, ctx, ce.scopeName);
    if (!sym || !sym->node || !sym->node->GetTy()) {
        return false;
    }
    auto prRes = Promotion(tyMgr).Promote(sym->node->GetTy(), structDecl->GetTy());
    bool generated = false;
    for (auto promoteTy : prRes) {
        if (!Ty::IsTyCorrect(promoteTy)) {
            continue;
        }
        // If the current candidate is a generic function inside generic structure,
        // just check the constraint of generic structure without diagnose (using function in typeManger)
        // otherwise check the constraint and report error.
        bool isConstraintFit = IsCurrentGeneric(fd, ce)
            ? tyMgr.CheckGenericDeclInstantiation(structDecl, promoteTy->TyArgs())
            : typeChecker.CheckGenericDeclInstantiation(structDecl, promoteTy->typeArgs, ce);
        bool invalidArgSize = structDecl->GetTy()->typeArgs.size() != promoteTy->typeArgs.size();
        if (!isConstraintFit || invalidArgSize) {
            continue;
        }
        GenerateTypeMapping(tyMgr, typeMapping, *structDecl, promoteTy->TyArgs());
        generated = true;
    }
    return generated;
}

// Caller guarantees 'fd.outerDecl' is extendDecl.
bool InstCtxScope::GenerateExtendGenericTypeMapping(
    const ASTContext& ctx, const FuncDecl& fd, const CallExpr& ce, SubstPack& typeMapping)
{
    auto extend = RawStaticCast<ExtendDecl*>(fd.outerDecl);
    if (!extend->extendedType || !extend->extendedType->GetTy().IsCorrect()) {
        return false;
    }
    if (!IsCurrentGeneric(fd, ce) && extend->TestAttr(Attribute::GENERIC) &&
        !ce.baseFunc->GetTypeArgs().empty()) {
        diag.Diagnose(ce, DiagKind::sema_non_generic_function_with_type_argument);
        return false;
    }
    if (ce.baseFunc->astKind == ASTKind::REF_EXPR) {
        // Base expr is 'RefExpr' and parent of candidate function may be generic decl.
        if (!GenerateTypeMappingByCallContext(ctx, fd, ce, typeMapping)) {
            return false;
        }
    }
    if (ce.baseFunc->astKind != ASTKind::MEMBER_ACCESS) {
        return true;
    }

    auto ma = StaticAs<ASTKind::MEMBER_ACCESS>(ce.baseFunc.get());
    if (!ma->baseExpr || !ma->baseExpr->GetTy().IsCorrect()) {
        return false;
    }

    if (extend->generic) {
        // in case base expr missing ty arg, the placeholder will be generated here
        // But the placeholders are only for extend's generic args.
        // The baseExpr's ty args will be inferred in FillTypeArgumentsTy.
        for (auto& extGenParam : extend->generic->typeParameters) {
            tyMgr.MakeInstTyVar(typeMapping, *StaticCast<GenericsTy*>(extGenParam->DataTy()));
        }
    }
    if (IsBaseTypeOmittedTypeArgs(*ma)) {
        return true;
    }
    // Since base expression is member access,
    // member access's type should be able to promote a valid type with extended type,
    // and the extended type must have same number of type arguments with the promoted type.
    auto prTys = Promotion(tyMgr).Promote(
        ma->baseExpr->GetTy(), tyMgr.GetInstantiatedTy(extend->extendedType->GetTy(), typeMapping.u2i));
    ModalTy promotedTy = prTys.empty() ? ModalTy{TypeManager::GetInvalidTy()} : *prTys.begin();
    if (!Ty::IsTyCorrect(promotedTy)) {
        return false;
    }
    auto baseArgs = tyMgr.GetTypeArgs(*promotedTy);
    std::vector<ModalTy> baseArgsModal;
    baseArgsModal.reserve(baseArgs.size());
    for (auto dt : baseArgs) {
        baseArgsModal.emplace_back(dt);
    }
    if (extend->TestAttr(Attribute::GENERIC) &&
        !typeChecker.CheckGenericDeclInstantiation(fd.outerDecl, baseArgsModal, ce)) {
        return false;
    }
    GenerateTypeMapping(tyMgr, typeMapping, *extend, baseArgs);
    RelayMappingFromExtendToExtended(tyMgr, typeMapping, *extend);
    return true;
}

void InstCtxScope::GenerateSubstPackByTyArgs(
    SubstPack& tmaps, const std::vector<Ptr<Type>>& typeArgs, const Generic& generic) const
{
    // Add u2i mapping anyway
    // Add inst mapping when we can get the type parameters and type arguments.
    auto tyParamSize = generic.typeParameters.size();
    // Error case. Maybe shouldn't reach here.
    if (tyParamSize < typeArgs.size()) {
        return;
    }
    for (size_t i = 0; i < tyParamSize; ++i) {
        if (!generic.typeParameters[i]) {
            continue;
        }
        auto uTy = generic.typeParameters[i]->DataTy();
        if (Ty::IsTyCorrect(uTy)) {
            auto uGenTy = StaticCast<GenericsTy*>(uTy);
            if (tmaps.u2i.count(uGenTy) == 0) {
                auto iGenTy = tyMgr.AllocTyVar();
                tmaps.u2i.emplace(uGenTy, iGenTy);
            }
            if (i >= typeArgs.size()) {
                continue;
            }
            if (typeArgs[i] && typeArgs[i]->GetTy().IsCorrect() && !typeArgs[i]->GetTy()->HasIntersectionTy()) {
                tmaps.inst[StaticCast<GenericsTy*>(tmaps.u2i[uGenTy])] = {typeArgs[i]->DataTy()};
            }
        }
    }
}

// When the baseExpr of other expr is a memberAccess,
// we could build the type mapping by the memberAccess's baseExpr's type.
void TypeChecker::TypeCheckerImpl::GenerateTypeMappingForBaseExpr(const Expr& baseExpr, SubstPack& typeMapping)
{
    if (baseExpr.astKind != ASTKind::MEMBER_ACCESS) {
        return;
    }
    auto& ma = static_cast<const MemberAccess&>(baseExpr);
    if (!ma.baseExpr->GetTy().IsCorrect()) {
        return;
    }
    CJC_ASSERT(!ma.baseExpr->GetTy()->HasIntersectionTy());
    if (IsThisOrSuper(*ma.baseExpr)) {
        typeManager.GenerateGenericMapping(typeMapping, *ma.baseExpr->GetTy());
        return;
    }
    auto directBase = ma.baseExpr->GetTarget();
    auto realBase = GetRealTarget(ma.baseExpr.get(), ma.baseExpr->GetTarget());
    SubstPack directMapping;
    if (directBase && (directBase != realBase)) {
        // in case of alias, where directBase is the alias decl, realBase is the real type decl
        typeManager.MakeInstTyVar(directMapping, *directBase);
    }
    if (realBase && realBase->astKind == ASTKind::PACKAGE_DECL) {
        return;
    }
    auto maTarget = ma.GetTarget();
    // NOTE: member access of enum constructor is also considered as type decl's member access for inference.
    bool typeDeclMemberAccess = realBase && maTarget && maTarget->outerDecl &&
        (realBase->IsTypeDecl() || realBase->TestAttr(Attribute::ENUM_CONSTRUCTOR));
    if (typeDeclMemberAccess) {
        CJC_NULLPTR_CHECK(maTarget->outerDecl->GetTy());
        auto instBaseTy = typeManager.GetInstantiatedTy(ma.baseExpr->GetTy(), directMapping.u2i);
        auto promoteMapping = promotion.GetPromoteTypeMapping(instBaseTy.Ty(), maTarget->outerDecl->DataTy());
        if (realBase->GetTy() != instBaseTy) {
            typeManager.PackMapping(typeMapping, GenerateTypeMapping(*realBase, instBaseTy->TyArgs()));
        }
        auto baseTypeArgs = ma.baseExpr->GetTypeArgs();
        std::unordered_set<DataTy> baseTyArgs;
        std::for_each(
            baseTypeArgs.begin(), baseTypeArgs.end(), [&baseTyArgs](auto type) { baseTyArgs.emplace(type->DataTy()); });
        auto genericTys = GetAllGenericTys(realBase->DataTy());
        for (auto it = promoteMapping.begin(); it != promoteMapping.end(); ++it) {
            // If mapped 'ty' exists in 'realBase' generic types
            // and is not found in user defined type args, remove it from mapping.
            Utils::EraseIf(it->second,
                [&genericTys, &baseTyArgs](auto ty) { return genericTys.count(ty) != 0 && baseTyArgs.count(ty) == 0; });
        }
        auto genericTysInst = GetAllGenericTys(ma.baseExpr->DataTy());
        /* in case the used type is alias, the ty vars to be solved are type parameters of the alias decl
         * but in case the type args are already given by users, they don't need to be solved */
        Utils::EraseIf(directMapping.u2i, [&genericTysInst](auto it) { return genericTysInst.count(it.first) == 0; });
        MergeSubstPack(typeMapping, directMapping);
        typeManager.PackMapping(typeMapping, promoteMapping);
    } else {
        typeManager.GenerateGenericMapping(typeMapping, *ma.baseExpr->GetTy());
    }
}

void InstCtxScope::SetRefDeclSimple(const AST::FuncDecl& fd, const AST::CallExpr& ce)
{
    for (auto tv : GetTyVars(fd, ce, true)) {
        refMaps.u2i.emplace(tv, tyMgr.AllocTyVar());
    }
    maps = curMaps;
    MergeSubstPack(maps, refMaps);
}

bool InstCtxScope::SetRefDecl(ASTContext& ctx, AST::FuncDecl& fd, CallExpr& ce)
{
    refMaps = {};
    std::vector<Ptr<Type>> typeArgs = GetEffectiveTypeArgsForCall(*ce.baseFunc, ce.baseFunc->GetTypeArgs());
    bool isInStructDecl = fd.outerDecl && fd.outerDecl->IsNominalDecl() && !IsTypeObjectCreation(fd, ce);
    if (IsGenericUpperBoundCall(*ce.baseFunc, fd)) { // Handle upperbound function.
        tyMgr.GenerateTypeMappingForUpperBounds(
            refMaps, *RawStaticCast<MemberAccess*>(ce.baseFunc.get()), fd);
    } else if (isInStructDecl && fd.outerDecl->astKind == ASTKind::EXTEND_DECL) { // Handle extend function.
        if (!GenerateExtendGenericTypeMapping(ctx, fd, ce, refMaps)) {
            return false;
        }
    } else {
        if (!IsCurrentGeneric(fd, ce) && !typeArgs.empty()) {
            diag.Diagnose(ce, DiagKind::sema_non_generic_function_with_type_argument);
            return false;
        }
        // Base expr is RefExpr, and the context of reference may be generic.
        if (ce.baseFunc->astKind == ASTKind::REF_EXPR && isInStructDecl) {
            // When the callee and caller are in different structure decl and there are generic decl between them,
            // check the inheritance relationship and generate typeMapping.
            // NOTE: It is possible that reference context and the candidate function are both generic.
            if (!GenerateTypeMappingByCallContext(ctx, fd, ce, refMaps)) {
                return false;
            }
        } else if (ce.baseFunc->astKind == ASTKind::MEMBER_ACCESS) {
            ReplaceTarget(ce.baseFunc.get(), &fd); // Update target for typeMapping generation.
            // Check the base of memberAccess and generate typeMapping by base.
            typeChecker.GenerateTypeMappingForBaseExpr(*ce.baseFunc, refMaps);
        }
    }
    if (auto generic = GetCurrentGeneric(fd, ce)) {
        GenerateSubstPackByTyArgs(refMaps, typeArgs, *generic);
    }
    maps = curMaps;
    MergeSubstPack(maps, refMaps);
    return true;
}
}
