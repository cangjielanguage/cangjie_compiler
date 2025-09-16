// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements typecheck apis for Pattern.
 */

#include "TypeCheckerImpl.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "BuiltInOperatorUtil.h"
#include "Collector.h"
#include "DiagSuppressor.h"
#include "Diags.h"
#include "JoinAndMeet.h"
#include "TypeCheckUtil.h"

#include "cangjie/AST/ASTContext.h"
#include "cangjie/AST/Clone.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Types.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Basic/Print.h"
#include "cangjie/Frontend/CompilerInstance.h"
#include "cangjie/Sema/CommonTypeAlias.h"
#include "cangjie/Sema/TypeManager.h"
#include "cangjie/Utils/CheckUtils.h"
#include "cangjie/Utils/Utils.h"

using namespace Cangjie;
using namespace Sema;
using namespace TypeCheckUtil;
using namespace AST;

namespace {
void InvalidateNonTopLevelCache(const ASTContext& ctx)
{
    std::string query = "scope_level=0";
    ctx.searcher->InvalidateCacheBut(query);
}

// Caller guarantees not re-enter for same node.
OwnedPtr<EnumPattern> VarOrEnumPatternToEnumPattern(ASTContext& ctx, const VarOrEnumPattern& vep, bool buildTrie)
{
    OwnedPtr<EnumPattern> ep = MakeOwned<EnumPattern>();
    CopyNodeWithFileID(ep.get(), &vep);
    ep->begin = vep.begin;
    ep->end = vep.end;
    ep->constructor = CreateRefExpr(vep.identifier, nullptr, vep.begin);
    CopyNodeWithFileID(ep->constructor.get(), &vep);
    ep->constructor->begin = vep.begin;
    ep->constructor->end = vep.end;
    // Set symbol for LSP.
    ep->constructor->DisableAttr(Attribute::COMPILER_ADD);
    if (vep.symbol) {
        auto nodeInfo = NodeInfo(*ep->constructor, vep.identifier, vep.symbol->scopeLevel, vep.symbol->scopeName);
        Collector::AddSymbol(ctx, nodeInfo, buildTrie);
        ctx.DeleteCurrentInvertedIndexes(&vep);
        // Preserve top-level symbol search cache to ensure faster next call to GetToplevelDecls.
        InvalidateNonTopLevelCache(ctx);
    }
    return ep;
}

// Caller guarantees not re-enter for same node.
OwnedPtr<VarPattern> VarOrEnumPatternToVarPattern(ASTContext& ctx, const VarOrEnumPattern& vep, bool buildTrie)
{
    OwnedPtr<VarPattern> vp = MakeOwned<VarPattern>(vep.identifier, vep.begin);
    CopyNodeWithFileID(vp.get(), &vep);
    vp->begin = vep.begin;
    vp->end = vep.end;
    CopyNodeWithFileID(vp->varDecl.get(), &vep);
    vp->varDecl->begin = vep.begin;
    vp->varDecl->end = vep.end;
    vp->varDecl->fullPackageName = vep.GetFullPackageName();
    vp->varDecl->identifier.SetPos(vep.identifier.Begin(), vep.identifier.End());
    vp->varDecl->identifier.SetRaw(vep.identifier.IsRaw());
    // Set symbol for LSP.
    if (vep.symbol) {
        auto nodeInfo = NodeInfo(*vp->varDecl, vep.identifier, vep.symbol->scopeLevel, vep.symbol->scopeName);
        Collector::AddSymbol(ctx, nodeInfo, buildTrie);
        ctx.DeleteCurrentInvertedIndexes(&vep);
        // Preserve top-level symbol search cache to ensure faster next call to GetToplevelDecls.
        InvalidateNonTopLevelCache(ctx);
    }
    return vp;
}

void FillEnumPatternMemberAccessTypeArgumentsTy(TypeManager& typeManager, EnumTy& targetTy, MemberAccess& ma)
{
    Ptr<EnumDecl> ed = ma.GetTarget() ? As<ASTKind::ENUM_DECL>(ma.GetTarget()->outerDecl) : nullptr;
    if (!ed || !ed->generic || !ma.baseExpr) {
        return;
    }
    MultiTypeSubst typeMapping;
    if (auto ref = DynamicCast<NameReferenceExpr*>(ma.baseExpr.get()); ref && ref->instTys.empty()) {
        // instTys is empty, use targetTy to generate the typeMapping.
        typeManager.GenerateGenericMapping(typeMapping, targetTy);
        for (auto& typeParam : ed->generic->typeParameters) {
            ref->instTys.emplace_back(GetMappedTy(typeMapping, StaticCast<GenericsTy*>(typeParam->ty)));
        }
    } else if (ma.baseExpr->ty) {
        // instTys is not empty, use ma.baseExpr->ty to generate the typeMapping.
        typeManager.GenerateGenericMapping(typeMapping, *ma.baseExpr->ty);
    }
    ma.baseExpr->ty = typeManager.GetBestInstantiatedTy(ed->ty, typeMapping);
    ma.ty = typeManager.GetBestInstantiatedTy(ma.ty, typeMapping);
}

void SetTyForEnumPatternConstructor(TypeManager& typeManager, EnumTy& targetTy, const EnumPattern& ep)
{
    if (!ep.constructor) {
        return;
    }
    if (ep.constructor->astKind == ASTKind::MEMBER_ACCESS) {
        MemberAccess& ma = static_cast<MemberAccess&>(*ep.constructor);
        if (!ma.baseExpr || !ma.baseExpr->ty || !ma.baseExpr->ty->IsEnum()) {
            return;
        }
        FillEnumPatternMemberAccessTypeArgumentsTy(typeManager, targetTy, ma);
    } else if (targetTy.GetGenericTy()) {
        CJC_ASSERT(ep.constructor->astKind == ASTKind::REF_EXPR);
        MultiTypeSubst typeMapping;
        typeManager.GenerateGenericMapping(typeMapping, targetTy);
        ep.constructor->ty = typeManager.GetBestInstantiatedTy(ep.constructor->ty, typeMapping);
    }
}

bool IsSubtypeBoxed(TypeManager& typeManager, Ty& leaf, Ty& root);

bool IsTupleSubtypeBoxed(TypeManager& typeManager, const TupleTy& leaf, const TupleTy& root)
{
    if (leaf.typeArgs.size() != root.typeArgs.size()) {
        return false;
    }
    for (size_t i = 0; i < leaf.typeArgs.size(); i++) {
        CJC_NULLPTR_CHECK(leaf.typeArgs[i]);
        CJC_NULLPTR_CHECK(root.typeArgs[i]);
        if (!IsSubtypeBoxed(typeManager, *leaf.typeArgs[i], *root.typeArgs[i])) {
            return false;
        }
    }
    return true;
}

bool IsFuncSubTypeBoxed(TypeManager& typeManager, const FuncTy& leaf, const FuncTy& root)
{
    CJC_NULLPTR_CHECK(leaf.retTy);
    CJC_NULLPTR_CHECK(root.retTy);
    if (leaf.paramTys.size() != root.paramTys.size()) {
        return false;
    }
    for (size_t i = 0; i < leaf.paramTys.size(); i++) {
        CJC_NULLPTR_CHECK(root.paramTys[i]);
        CJC_NULLPTR_CHECK(leaf.paramTys[i]);
        if (!IsSubtypeBoxed(typeManager, *root.paramTys[i], *leaf.paramTys[i])) {
            return false;
        }
    }
    return leaf.isC == root.isC && leaf.hasVariableLenArg == root.hasVariableLenArg &&
        IsSubtypeBoxed(typeManager, *leaf.retTy, *root.retTy);
}

// Check implicitly boxed subtype relationships.
// For TupleTy and FuncTy, the covariant/contravariant relationships are checked recursively with implicit box.
bool IsSubtypeBoxed(TypeManager& typeManager, Ty& leaf, Ty& root)
{
    if (leaf.IsTuple() && root.IsTuple()) {
        return IsTupleSubtypeBoxed(typeManager, static_cast<TupleTy&>(leaf), static_cast<TupleTy&>(root));
    } else if (leaf.IsFunc() && root.IsFunc()) {
        return IsFuncSubTypeBoxed(typeManager, static_cast<FuncTy&>(leaf), static_cast<FuncTy&>(root));
    } else {
        return typeManager.IsSubtype(&leaf, &root, true, false);
    }
}
}; // namespace

bool TypeChecker::TypeCheckerImpl::ChkPattern(ASTContext& ctx, Ty& target, Pattern& p, bool isPatternInMatch)
{
    switch (p.astKind) {
        case ASTKind::WILDCARD_PATTERN: {
            auto wp = StaticAs<ASTKind::WILDCARD_PATTERN>(&p);
            return ChkWildcardPattern(target, *wp);
        }
        case ASTKind::CONST_PATTERN: {
            auto cp = StaticAs<ASTKind::CONST_PATTERN>(&p);
            return ChkConstPattern(ctx, target, *cp);
        }
        case ASTKind::TYPE_PATTERN: {
            auto tp = StaticAs<ASTKind::TYPE_PATTERN>(&p);
            return ChkTypePattern(ctx, target, *tp);
        }
        case ASTKind::VAR_PATTERN: {
            auto vp = StaticAs<ASTKind::VAR_PATTERN>(&p);
            return ChkVarPattern(ctx, target, *vp);
        }
        case ASTKind::ENUM_PATTERN: {
            auto ep = StaticAs<ASTKind::ENUM_PATTERN>(&p);
            return ChkEnumPattern(ctx, target, *ep);
        }
        case AST::ASTKind::VAR_OR_ENUM_PATTERN: {
            auto vep = StaticAs<ASTKind::VAR_OR_ENUM_PATTERN>(&p);
            return ChkVarOrEnumPattern(ctx, target, *vep);
        }
        case ASTKind::TUPLE_PATTERN: {
            auto tp = StaticAs<ASTKind::TUPLE_PATTERN>(&p);
            return ChkTuplePattern(ctx, target, *tp, isPatternInMatch);
        }
        // Handle invalid patterns explicitly.
        case ASTKind::INVALID_PATTERN: {
            p.ty = TypeManager::GetInvalidTy();
            return false;
        }
        default: {
            Errorln("unhandled pattern");
            p.ty = TypeManager::GetInvalidTy();
            return false;
        }
    }
}

bool TypeChecker::TypeCheckerImpl::ChkWildcardPattern(Ty& target, WildcardPattern& p) const
{
    p.ty = &target;
    return true;
}

bool TypeChecker::TypeCheckerImpl::ChkConstPattern(ASTContext& ctx, Ty& target, ConstPattern& p)
{
    CJC_NULLPTR_CHECK(p.literal);
    // 1. Check the type of the literal in the constant pattern.
    if (target.IsRune() && IsSingleRuneStringLiteral(*p.literal)) {
        p.literal->ty = &target;
        p.ty = &target;
    } else if (target.kind == TypeKind::TYPE_UINT8 && IsSingleByteStringLiteral(*p.literal)) {
        p.literal->ty = &target;
        p.ty = &target;
        ChkLitConstExprRange(StaticCast<LitConstExpr&>(*p.literal));
    } else if (!Check(ctx, &target, p.literal.get())) {
        p.ty = TypeManager::GetInvalidTy();
        return false;
    }
    // 2. The literals are compared by their values, i.e., the types must be checked exactly equal,
    //    auto-boxed Options are not allowed.
    if (!typeManager.IsTyEqual(p.literal->ty, &target)) {
        DiagMismatchedTypes(diag, *p.literal, target);
        p.ty = TypeManager::GetInvalidTy();
        return false;
    }
    // 3. String interpolations are not allowed in constant patterns.
    if (auto lce = DynamicCast<LitConstExpr*>(p.literal.get()); lce && lce->siExpr) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_interpolation_in_const_pattern, p);
        p.ty = TypeManager::GetInvalidTy();
        return false;
    }
    // 4. Check if the selector can be compared with the pattern.
    p.ty = typeManager.TryGreedySubst(&target);
    const auto& typeCandidates = GetBinaryOpTypeCandidates(TokenKind::EQUAL);
    if (Utils::InKeys(p.ty->kind, typeCandidates)) {
        return true;
    }
    // String literals are compared by the `==` method.
    return ChkOpOverloadForConstPattern(ctx, target, p);
}

bool TypeChecker::TypeCheckerImpl::ChkOpOverloadForConstPattern(ASTContext& ctx, Ty& target, ConstPattern& p)
{
    auto boolTy = TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN);
    auto callExpr = MakeOwnedNode<CallExpr>();
    ctx.RemoveTypeCheckCache(*callExpr);
    auto callBase = MakeOwnedNode<MemberAccess>();
    ctx.RemoveTypeCheckCache(*callBase);
    callBase->scopeName = p.scopeName;
    callBase->baseExpr = MakeOwnedNode<RefExpr>();
    callBase->baseExpr->ty = &target;
    // ensure synthesize skip the dummy node
    ctx.SkipSynForCorrectTy(*callBase->baseExpr);
    callBase->field = "==";
    callExpr->baseFunc = std::move(callBase);
    CJC_NULLPTR_CHECK(p.literal);
    auto funcArg = AST::CreateFuncArg(ASTCloner::Clone(p.literal.get()));
    funcArg->scopeName = p.scopeName;
    callExpr->args.push_back(std::move(funcArg));
    AddCurFile(*callExpr, p.curFile);
    { // Create a scope for DiagSuppressor.
        auto ds = DiagSuppressor(diag);
        if (Check(ctx, boolTy, callExpr.get())) {
            ds.ReportDiag();
            p.operatorCallExpr = std::move(callExpr);
            return true;
        }
    }
    p.ty = TypeManager::GetNonNullTy(p.ty);
    diag.Diagnose(p, DiagKind::sema_not_overload_in_match);
    return false;
}

bool TypeChecker::TypeCheckerImpl::ChkTypePattern(ASTContext& ctx, Ty& target, TypePattern& p)
{
    CJC_NULLPTR_CHECK(p.pattern);
    CJC_NULLPTR_CHECK(p.type);
    p.type->ty = Synthesize(ctx, p.type.get());
    CJC_NULLPTR_CHECK(p.type->ty);
    if (typeManager.IsSubtype(&target, p.type->ty, true, false)) {
        p.needRuntimeTypeCheck = false;
        p.matchBeforeRuntime = true;
    } else {
        if (IsSubtypeBoxed(typeManager, target, *p.type->ty)) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_unreachable_pattern, p)
                .AddNote("the selector is of type '" + target.String() + "', which is not a subtype of '" +
                    p.type->ty->String());
        }
        p.needRuntimeTypeCheck = IsNeedRuntimeCheck(typeManager, target, *p.type->ty);
        p.matchBeforeRuntime = false;
    }
    if (!ChkPattern(ctx, *p.type->ty, *p.pattern)) {
        p.ty = TypeManager::GetInvalidTy();
        return false;
    }
    p.ty = p.type->ty;
    return true;
}

bool TypeChecker::TypeCheckerImpl::ChkVarPattern(const ASTContext& ctx, Ty& target, VarPattern& p)
{
    if (p.varDecl->identifier != V_COMPILER) {
        auto decls = ctx.GetDeclsByName({p.varDecl->identifier, p.varDecl->scopeName});
        auto iter = std::find_if(decls.cbegin(), decls.cend(), [&p](auto decl) { return decl != p.varDecl.get(); });
        if (iter != decls.cend()) {
            auto decl = *iter;
            // The variable has been defined in this MatchCase, e.g., `case (x, x) => {}`.
            // Or it conflicts with definition in while-let body, e.g., `while (let a <- 1) { let a = 1 }`
            DiagRedefinitionWithFoundNode(diag, *p.varDecl, *decl);
            p.ty = TypeManager::GetInvalidTy();
            p.varDecl->ty = TypeManager::GetInvalidTy();
            p.varDecl->fullPackageName = p.GetFullPackageName();
            p.varDecl->EnableAttr(Attribute::IS_CHECK_VISITED);
            return false;
        }
    }
    p.ty = &target;
    p.varDecl->ty = p.ty;
    p.varDecl->EnableAttr(Attribute::IS_CHECK_VISITED);
    return true;
}

bool TypeChecker::TypeCheckerImpl::ChkEnumPattern(ASTContext& ctx, Ty& target, EnumPattern& p)
{
    CJC_NULLPTR_CHECK(p.constructor);
    bool mayMatch = false;
    auto maybeInferred = typeManager.TryGreedySubst(&target);
    if (auto tempEnumTy = DynamicCast<EnumTy*>(maybeInferred); tempEnumTy) {
        FindEnumPatternTarget(ctx, tempEnumTy->decl, p);
        // If selector is a generic instantiated enum, set the pattern ty to instantiated ty.
        SetTyForEnumPatternConstructor(typeManager, *tempEnumTy, p);
        mayMatch = true;
    } else if (target.IsPlaceholder()) {
        // in case the select has placeholder ty var, only try to find target from pattern
        FindEnumPatternTarget(ctx, nullptr, p);
        auto enumTy = Is<FuncTy>(p.constructor->ty) ? p.constructor->ty->typeArgs[1] : p.constructor->ty;
        auto placeholderEnumTy = typeManager.ConstrainByCtor(StaticCast<GenericsTy&>(target), *enumTy);
        if (placeholderEnumTy) {
            SetTyForEnumPatternConstructor(typeManager, *StaticCast<EnumTy*>(placeholderEnumTy), p);
            mayMatch = true;
        }
    }
    if (mayMatch) {
        if (auto enumTy = DynamicCast<EnumTy*>(p.constructor->ty);
            enumTy && typeManager.IsTyEqual(p.constructor->ty, &target)) {
            p.ty = p.constructor->ty;
            return true;
        } else if (auto funcTy = DynamicCast<FuncTy*>(p.constructor->ty); funcTy) {
            if (!IsFuncTyEnumPatternMatched(ctx, target, *funcTy, p)) {
                p.ty = TypeManager::GetInvalidTy();
                return false;
            }
            p.ty = funcTy->retTy;
            return true;
        }
    }
    diag.Diagnose(p, DiagKind::sema_pattern_not_match, "enum");
    p.ty = TypeManager::GetInvalidTy();
    return false;
}

bool TypeChecker::TypeCheckerImpl::IsFuncTyEnumPatternMatched(
    ASTContext& ctx, Ty& target, const FuncTy& funcTy, const EnumPattern& p)
{
    if (funcTy.paramTys.size() != p.patterns.size()) {
        (void)diag.Diagnose(p, DiagKind::sema_pattern_not_match, "enum");
        return false;
    }
    for (size_t i = 0; i < p.patterns.size(); i++) {
        CJC_NULLPTR_CHECK(p.patterns[i]);
        if (funcTy.paramTys[i] == nullptr || !ChkPattern(ctx, *funcTy.paramTys[i], *p.patterns[i])) {
            return false;
        }
    }
    if (!typeManager.IsTyEqual(funcTy.retTy, &target)) {
        (void)diag.Diagnose(p, DiagKind::sema_pattern_not_match, "enum");
        return false;
    }
    return true;
}

bool TypeChecker::TypeCheckerImpl::ChkVarOrEnumPattern(ASTContext& ctx, Ty& target, VarOrEnumPattern& p)
{
    if (p.pattern != nullptr) {
        // We have to ChkPattern again because of the LSPCompilerInstance.
        if (!ChkPattern(ctx, target, *p.pattern)) {
            p.ty = TypeManager::GetInvalidTy();
            return false;
        }
        p.ty = &target;
        return true;
    }
    if (ctx.IsEnumConstructor(p.identifier)) {
        p.pattern = VarOrEnumPatternToEnumPattern(ctx, p, ci->buildTrie);
        if (!ChkPattern(ctx, target, *p.pattern)) {
            p.ty = TypeManager::GetInvalidTy();
            return false;
        }
    } else {
        p.pattern = VarOrEnumPatternToVarPattern(ctx, p, ci->buildTrie);
        auto& decl = *StaticAs<ASTKind::VAR_PATTERN>(p.pattern.get())->varDecl;
        auto name = std::make_pair(p.identifier, p.scopeName);
        ctx.AddDeclName(name, decl);
        if (!ChkPattern(ctx, target, *p.pattern)) {
            ctx.RemoveDeclByName(name, decl);
            p.ty = TypeManager::GetInvalidTy();
            return false;
        }
    }
    if (auto ep = DynamicCast<EnumPattern*>(p.pattern.get()); ep) {
        // In order to support the find-references feature for LSP, we have to insert the constructor to the `users`
        // of the `Decl`, by setting the third argument (`insertTarget`) as `true`.
        ReplaceTarget(ep->constructor.get(), ep->constructor->GetTarget(), true);
    }
    p.ty = &target;
    return true;
}

bool TypeChecker::TypeCheckerImpl::ChkTuplePattern(ASTContext& ctx, Ty& target, TuplePattern& p, bool isPatternInMatch)
{
    if (auto tupleTy = DynamicCast<TupleTy*>(&target); tupleTy) {
        if (tupleTy->typeArgs.size() != p.patterns.size()) {
            diag.Diagnose(p, DiagKind::sema_tuple_pattern_with_correct_size_expected);
            p.ty = TypeManager::GetInvalidTy();
            return false;
        }
        for (size_t i = 0; i < p.patterns.size(); i++) {
            if (!Check(ctx, tupleTy->typeArgs[i], p.patterns[i].get())) {
                p.ty = TypeManager::GetInvalidTy();
                return false;
            }
        }
    } else {
        if (isPatternInMatch) {
            DiagMismatchedTypesWithFoundTy(diag, p, target.String(), "Tuple");
        } else {
            diag.Diagnose(p, DiagKind::sema_tuple_pattern_not_match, "initializer");
        }
        p.ty = TypeManager::GetInvalidTy();
        return false;
    }
    p.ty = &target;
    return true;
}

std::vector<Ptr<Decl>> TypeChecker::TypeCheckerImpl::FindEnumPatternTargets(
    ASTContext& ctx, Ptr<EnumDecl> ed, EnumPattern& ep)
{
    if (auto ma = DynamicCast<MemberAccess*>(ep.constructor.get()); ma) {
        InferMemberAccess(ctx, *ma);
        if (!ma->target || !ma->target->outerDecl || ma->target->outerDecl->astKind != ASTKind::ENUM_DECL) {
            return {};
        }
        std::vector<Ptr<Decl>> result{ma->target};
        result.insert(result.end(), ma->targets.begin(), ma->targets.end());
        return result;
    } else if (ed) {
        return FieldLookup(ctx, ed, ep.GetIdentifier(), {.lookupExtend = false});
    } else if (auto re = DynamicCast<RefExpr*>(ep.constructor.get())) {
        // in case selector is placeholder
        re->callOrPattern = &ep;
        InferRefExpr(ctx, *re);
        if (!re->ref.target || !re->ref.target->outerDecl || re->ref.target->outerDecl->astKind != ASTKind::ENUM_DECL) {
            return {};
        }
        std::vector<Ptr<Decl>> result{re->ref.target};
        result.insert(result.end(), re->ref.targets.begin(), re->ref.targets.end());
        return result;
    }
    return {};
}

// Use enumType to determine enumPattern's target recursively.
void TypeChecker::TypeCheckerImpl::FindEnumPatternTarget(ASTContext& ctx, Ptr<EnumDecl> ed, EnumPattern& ep)
{
    CJC_NULLPTR_CHECK(ep.constructor);
    // Search the enum element in enumDecl.
    std::vector<Ptr<Decl>> result = FindEnumPatternTargets(ctx, ed, ep);
    if (result.empty()) {
        diag.Diagnose(ep, DiagKind::sema_undeclared_identifier, ep.GetIdentifier());
        ep.constructor->ty = TypeManager::GetInvalidTy();
        return;
    }
    // Clear any existing target since the result haven't been checked.
    ReplaceTarget(ep.constructor.get(), nullptr);
    // Search all the result to find the right one and set the target.
    for (auto& it : result) {
        if (ep.patterns.empty()) {
            // If 'EnumPattern' has no sub-pattern, the target is matched when the type of decl is EnumTy.
            if (it->ty && it->ty->kind == TypeKind::TYPE_ENUM) {
                ReplaceTarget(ep.constructor.get(), it, false);
                break;
            }
        } else {
            // If 'EnumPattern' has sub-patterns,
            // the target is matched when the type of decl is FuncTy with same number of parameters.
            if (auto funcTy = DynamicCast<FuncTy*>(it->ty); funcTy && ep.patterns.size() == funcTy->paramTys.size()) {
                CJC_ASSERT(it->astKind == ASTKind::FUNC_DECL);
                ReplaceTarget(ep.constructor.get(), it, false);
                break;
            }
        }
    }
    auto target = ep.constructor->GetTarget();
    if (!target || !Ty::IsTyCorrect(target->ty)) {
        diag.Diagnose(ep, DiagKind::sema_enum_pattern_param_size_error);
        ep.constructor->ty = TypeManager::GetInvalidTy();
        return;
    }
    ep.constructor->ty = target->ty;
    if (ep.patterns.empty()) {
        return;
    }
    auto fd = StaticAs<ASTKind::FUNC_DECL>(target);
    CJC_NULLPTR_CHECK(fd->funcBody);
    CJC_ASSERT(!fd->funcBody->paramLists.empty());
    CJC_ASSERT(fd->funcBody->paramLists[0]->params.size() >= ep.patterns.size());
    for (size_t i = 0; i < ep.patterns.size(); ++i) {
        auto subTarget = Ty::GetDeclPtrOfTy(fd->funcBody->paramLists[0]->params[i]->ty);
        bool hasSubEnumPattern =
            ep.patterns[i]->astKind == ASTKind::ENUM_PATTERN && subTarget && subTarget->astKind == ASTKind::ENUM_DECL;
        if (hasSubEnumPattern) {
            FindEnumPatternTarget(
                ctx, RawStaticCast<EnumDecl*>(subTarget), *RawStaticCast<EnumPattern*>(ep.patterns[i].get()));
        }
    }
}

bool TypeChecker::TypeCheckerImpl::ChkTryWildcardPattern(
    Ptr<Ty> target, WildcardPattern& p, std::vector<Ptr<Ty>>& included)
{
    p.ty = target;
    if (auto classTy = DynamicCast<ClassTy*>(target); classTy) {
        if (std::find(included.begin(), included.end(), classTy) != included.end()) {
            diag.Diagnose(p, DiagKind::sema_useless_exception_type);
        }
    }
    return true;
}

bool TypeChecker::TypeCheckerImpl::ChkExceptTypePattern(
    ASTContext& ctx, ExceptTypePattern& etp, std::vector<Ptr<Ty>>& included)
{
    bool result = true;
    std::set<Ptr<Ty>> typeTys;
    auto exception = importManager.GetCoreDecl<ClassDecl>(CLASS_EXCEPTION);
    auto error = importManager.GetCoreDecl<ClassDecl>(CLASS_ERROR);
    bool foundClass = exception && error;
    for (auto& type : etp.types) {
        CJC_NULLPTR_CHECK(type);
        Synthesize(ctx, type.get());
        CJC_NULLPTR_CHECK(type->ty);
        if (!foundClass || type->ty->IsNothing() ||
            (!typeManager.IsSubtype(type->ty, exception->ty) && !typeManager.IsSubtype(type->ty, error->ty))) {
            diag.Diagnose(*type, DiagKind::sema_except_catch_type_error);
            result = false;
            continue;
        }
        if (Utils::In(included, [this, &type](Ptr<Ty> ty) { return typeManager.IsSubtype(type->ty, ty); })) {
            diag.Diagnose(*type, DiagKind::sema_useless_exception_type);
        } else {
            included.emplace_back(type->ty);
        }
        typeTys.emplace(type->ty);
    }
    if (!result || typeTys.empty()) {
        etp.ty = TypeManager::GetInvalidTy();
        return false;
    }
    auto joinRes = JoinAndMeet(typeManager, typeTys, {}, &importManager, etp.curFile).JoinAsVisibleTy();
    auto optErrs = JoinAndMeet::SetJoinedType(etp.ty, joinRes);
    CJC_NULLPTR_CHECK(etp.ty);
    if (optErrs.has_value()) {
        diag.Diagnose(etp, DiagKind::sema_type_incompatible, "pattern").AddNote(*optErrs);
        return false;
    }
    return Check(ctx, etp.ty, etp.pattern.get());
}

bool TypeChecker::TypeCheckerImpl::ChkHandlePatterns(ASTContext& ctx, Handler& h,
    std::vector<Ptr<Ty>>& included)
{
    auto& ctp = *StaticAs<ASTKind::COMMAND_TYPE_PATTERN>(h.commandPattern.get());
    CJC_ASSERT(ctp.types.size() >= 1);
    auto maybeCtpTyAsCommand = ChkCommandTypePattern(ctx, ctp, included);
    if (!maybeCtpTyAsCommand) {
        return false;
    }
    auto& ctpTyAsCommand = **maybeCtpTyAsCommand;
    h.commandResultTy = ctpTyAsCommand.typeArgs[0];

    return Check(ctx, ctp.ty, ctp.pattern.get());
}

std::optional<Ptr<Ty>> TypeChecker::TypeCheckerImpl::ChkCommandTypePattern(
    ASTContext& ctx, CommandTypePattern& ctp, std::vector<Ptr<Ty>>& included)
{
    CJC_ASSERT(ctp.types.size() == 1);
    bool result = true;
    std::set<Ptr<Ty>> typeTys;
    auto command = importManager.GetImportedDecl(EFFECT_PACKAGE_NAME, CLASS_COMMAND);
    if (!command) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_command_handle_type_error, *ctp.types[0]);
        return {};
    }
    std::optional<Ptr<Ty>> cmdTypeAsCommand;
    CJC_ASSERT(ctp.types.size() == 1);
    auto cmdTypePat = ctp.types[0].get();
    CJC_NULLPTR_CHECK(cmdTypePat);
    Synthesize(ctx, cmdTypePat);
    CJC_NULLPTR_CHECK(cmdTypePat->ty);
    auto prCTys = promotion.Promote(*cmdTypePat->ty, *command->ty);
    if (cmdTypePat->ty->IsNothing() || prCTys.empty()) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_command_handle_type_error, *cmdTypePat);
        result = false;
        ctp.ty = TypeManager::GetInvalidTy();
        return {};
    }
    CJC_ASSERT(prCTys.size() == 1);
    if (Utils::In(included, [this, &cmdTypePat](Ty* ty) { return typeManager.IsSubtype(cmdTypePat->ty, ty); })) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_useless_command_type, *cmdTypePat);
    } else {
        included.emplace_back(cmdTypePat->ty);
    }
    typeTys.emplace(cmdTypePat->ty);
    cmdTypeAsCommand = *prCTys.begin();

    if (!result || typeTys.empty()) {
        ctp.ty = TypeManager::GetInvalidTy();
        return {};
    }
    auto joinRes = JoinAndMeet(typeManager, typeTys).JoinAsVisibleTy();
    auto optErrs = JoinAndMeet::SetJoinedType(ctp.ty, joinRes);
    CJC_NULLPTR_CHECK(ctp.ty);
    if (optErrs.has_value()) {
        diag.Diagnose(ctp, DiagKind::sema_type_incompatible, "pattern").AddNote(*optErrs);
        return {};
    }
    return cmdTypeAsCommand;
}
