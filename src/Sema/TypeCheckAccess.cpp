// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements access control check apis.
 */

#include "TypeCheckerImpl.h"

#include <queue>

#include "Diags.h"

#include "cangjie/AST/ASTContext.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"
#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Modules/ModulesUtils.h"
#include "cangjie/Sema/TypeManager.h"
#include "cangjie/Utils/CheckUtils.h"

using namespace Cangjie;
using namespace AST;
using namespace Sema;

namespace {
bool MaybeStruct(const Ty& ty)
{
    if (!Ty::IsTyCorrect(&ty)) {
        return false;
    } else if (ty.IsStruct()) {
        return true;
    } else if (!ty.IsGeneric()) {
        return false;
    }
    const GenericsTy& gty = static_cast<const GenericsTy&>(ty);
    for (Ptr<Ty> upperBound : gty.upperBounds) {
        if (upperBound && MaybeStruct(*upperBound)) {
            return true;
        }
    }
    // It there are non-interface types (e.g., classes) in the upper bounds, `ty` cannot be struct.
    return !Utils::In(gty.upperBounds, [](Ptr<const Ty> ty) {
        CJC_NULLPTR_CHECK(ty);
        return !ty->IsInterface();
    });
};

void CheckMutationInStructNonMut(DiagnosticEngine& diag, const StructDecl& sd, const Expr& expr)
{
    CJC_NULLPTR_CHECK(sd.body);
    // Collect the value type instance members of the `struct`,
    // these `varDecls` are not allowed to be assigned in non-mut function.
    std::unordered_set<Ptr<Decl>> varDecls;
    for (auto& decl : sd.body->decls) {
        if (auto vd = DynamicCast<VarDecl*>(decl.get()); vd && !vd->TestAttr(Attribute::STATIC) &&
            Ty::IsTyCorrect(vd->ty) && !vd->ty->IsArray()) {
            varDecls.emplace(vd);
        }
    }
    // Get the `baseExpr` of the form `x` (`RefExpr`) or `this.x.*` (`MemberAccess`).
    // Then check whether the target of the `x` is in the forbidden `varDecls`.
    Ptr<const Expr> baseExpr = &expr;
    while (baseExpr != nullptr) {
        auto target = baseExpr->GetTarget();
        if (Is<const RefExpr*>(baseExpr)) {
            if (varDecls.find(target) != varDecls.cend()) {
                diag.Diagnose(expr, DiagKind::sema_cannot_modify_var, target->identifier.Val());
            }
            break;
        } else if (auto ma = DynamicCast<const MemberAccess*>(baseExpr); ma) {
            if (ma->baseExpr->ty->IsClassLike()) {
                // don't check member access of field of class like type, unless it is a member access of this
                // e.g. let T be a class type with field v
                // this.a = T() // this is a mutation to this
                // this.a.v = T() // this is not a mutation, because a is of class type
                break;
            }
            if (auto re = DynamicCast<RefExpr*>(ma->baseExpr.get());
                re && re->isThis && varDecls.find(target) != varDecls.cend()) {
                diag.Diagnose(expr, DiagKind::sema_cannot_modify_var, target->identifier.Val());
                break;
            }
            baseExpr = ma->baseExpr.get();
        } else {
            break;
        }
    }
}
} // namespace

std::vector<Ptr<Decl>> TypeChecker::TypeCheckerImpl::GetAccessibleDecls(
    const ASTContext& ctx, const Expr& e, const std::vector<Ptr<Decl>>& targets) const
{
    std::vector<Ptr<Decl>> res;
    for (auto t : targets) {
        if (t == nullptr) {
            continue;
        }
        Symbol* sym = ScopeManager::GetCurSymbolByKind(SymbolKind::STRUCT, ctx, e.scopeName);
        bool ret = TypeCheckUtil::IsLegalAccess(sym, *t, e, importManager, typeManager);
        if (ret) {
            res.emplace_back(t);
        }
    }
    return res;
}

Ptr<Decl> TypeChecker::TypeCheckerImpl::GetAccessibleDecl(
    const ASTContext& ctx, const Expr& e, const std::vector<Ptr<Decl>>& targets) const
{
    auto res = GetAccessibleDecls(ctx, e, targets);
    return res.empty() ? nullptr : res[0];
}

Ptr<Decl> TypeChecker::TypeCheckerImpl::CheckFuncAccessControl(
    const ASTContext& ctx, const Expr& e, const std::vector<Ptr<Decl>>& targets) const
{
    auto accessibleDecl = GetAccessibleDecl(ctx, e, targets);
    bool invalidFuncAccess =
        accessibleDecl == nullptr && !targets.empty() && targets[0] && targets[0]->astKind == ASTKind::FUNC_DECL;
    if (invalidFuncAccess) {
        diag.Diagnose(e, DiagKind::sema_invalid_access_function, targets[0]->identifier.Val());
    }
    return accessibleDecl;
}

bool TypeChecker::TypeCheckerImpl::CheckNonFuncAccessControl(
    const ASTContext& ctx, const Expr& e, const Decl& target) const
{
    Symbol* sym = ScopeManager::GetCurSymbolByKind(SymbolKind::STRUCT, ctx, e.scopeName);
    bool ret = TypeCheckUtil::IsLegalAccess(sym, target, e, importManager, typeManager);
    if (!ret) {
        ctx.diag.Diagnose(e, DiagKind::sema_invalid_access_control, target.identifier.Val());
    }
    return ret;
}

void TypeChecker::TypeCheckerImpl::CheckMemberAccessInCtorParamOrCtorArg(const ASTContext& ctx, const FuncDecl& fd)
{
    if (fd.funcBody == nullptr) {
        return;
    }
    for (auto& paramList : fd.funcBody->paramLists) {
        for (auto& fp : paramList->params) {
            if (fp->assignment) {
                CheckIllegalMemberWalker(ctx, fp->assignment.get(), true, "default parameter value of the constructor");
            }
        }
    }
    if (fd.constructorCall != ConstructorCall::NONE && !fd.funcBody->body->body.empty()) {
        if (auto ce = DynamicCast<CallExpr*>(fd.funcBody->body->body.begin()->get()); ce) {
            auto refExpr = As<ASTKind::REF_EXPR>(ce->baseFunc.get());
            if (!refExpr || (!refExpr->isThis && !refExpr->isSuper)) {
                return;
            }
            for (auto& i : ce->args) {
                CheckIllegalMemberWalker(ctx, i.get(), refExpr->isThis, "arguments of constructor call");
            }
        }
    }
}

void TypeChecker::TypeCheckerImpl::CheckIllegalMemberWalker(
    const ASTContext& ctx, Ptr<Node> node, bool reportThis, const std::string& errorStr)
{
    Walker walker(node, [this, &ctx, reportThis, errorStr](Ptr<Node> node) -> VisitAction {
        if (auto re = DynamicCast<RefExpr*>(node); re) {
            if (reportThis && re->isThis && re->isAlone) {
                diag.Diagnose(*re, DiagKind::sema_assignment_of_member_variable_cannot_use_this_or_super,
                    re->ref.identifier.Val(), errorStr);
            }
            CheckIllegalMemberHelper(ctx, reportThis, errorStr, *re);
            return VisitAction::WALK_CHILDREN;
        } else if (auto ma = DynamicCast<MemberAccess*>(node); ma) {
            auto refExpr = As<ASTKind::REF_EXPR>(ma->baseExpr.get());
            // call member this.xx/super.xx in struct or class constructor
            if (refExpr && ((refExpr->isThis && reportThis) || refExpr->isSuper)) {
                diag.Diagnose(*ma, DiagKind::sema_assignment_of_member_variable_cannot_use_this_or_super,
                    refExpr->ref.identifier.Val(), errorStr);
                return VisitAction::SKIP_CHILDREN;
            }
            if (refExpr && (refExpr->isThis || refExpr->isSuper)) {
                CheckIllegalMemberHelper(ctx, reportThis, errorStr, *refExpr);
            }
            return VisitAction::SKIP_CHILDREN;
        } else {
            return VisitAction::WALK_CHILDREN;
        }
    });
    walker.Walk();
}

void TypeChecker::TypeCheckerImpl::CheckIllegalMemberHelper(
    const ASTContext& ctx, bool reportThis, const std::string& errorStr, const NameReferenceExpr& nre)
{
    auto target = nre.GetTarget();
    bool isInstanceMember = target != nullptr && target->outerDecl && target->outerDecl->IsNominalDecl() &&
        !target->TestAnyAttr(Attribute::CONSTRUCTOR, Attribute::ENUM_CONSTRUCTOR, Attribute::STATIC);
    if (isInstanceMember) {
        Symbol* symOfExprStruct = ScopeManager::GetCurSymbolByKind(SymbolKind::STRUCT, ctx, nre.scopeName);
        if (symOfExprStruct == nullptr) {
            return; // If the reference is not inside any structure decl, quit now.
        }
        // The target has been checked that is defined inside nominal declaration.
        Symbol* symOfDeclStruct = ScopeManager::GetCurSymbolByKind(SymbolKind::STRUCT, ctx, target->scopeName);
        CJC_NULLPTR_CHECK(symOfDeclStruct);
        CJC_NULLPTR_CHECK(symOfDeclStruct->node);
        CJC_NULLPTR_CHECK(symOfExprStruct->node);
        bool inSameDecl = symOfExprStruct == symOfDeclStruct;
        std::string identifier = nre.astKind == ASTKind::REF_EXPR ? StaticCast<RefExpr>(&nre)->ref.identifier.Val()
                                                                  : StaticCast<MemberAccess>(&nre)->field.Val();
        Position pos =
            nre.astKind == ASTKind::REF_EXPR ? nre.GetBegin() : StaticCast<MemberAccess>(&nre)->field.Begin();
        if (inSameDecl && reportThis) {
            // Report error when this reference and declaration are in same decl.
            diag.Diagnose(
                pos, DiagKind::sema_assignment_of_member_variable_cannot_use_this_or_super, identifier, errorStr);
        } else if (!inSameDecl && typeManager.IsSubtype(symOfExprStruct->node->ty, symOfExprStruct->node->ty)) {
            // Report error when this reference and declaration are in decls which have inheritance relation.
            diag.Diagnose(
                pos, DiagKind::sema_assignment_of_member_variable_cannot_use_this_or_super, identifier, errorStr);
        }
    }
}

void TypeChecker::TypeCheckerImpl::CheckMutationInStruct(const ASTContext& ctx, const Expr& expr) const
{
    auto target = expr.GetTarget();
    if (!target || target->astKind != ASTKind::VAR_DECL) {
        return;
    }
    auto vd = StaticAs<ASTKind::VAR_DECL>(target);
    // `static` variables are checked by other functions.
    if (vd->TestAttr(Attribute::STATIC)) {
        return;
    }
    Symbol* outFunc = ScopeManager::GetOutMostSymbol(ctx, SymbolKind::FUNC_LIKE, expr.scopeName);
    if (!outFunc || !outFunc->node || outFunc->node->TestAnyAttr(Attribute::CONSTRUCTOR, Attribute::MUT)) {
        return;
    }
    Ptr<StructDecl> sd = nullptr;
    Symbol* outDecl = ScopeManager::GetCurOuterDeclOfScopeLevelX(ctx, expr, 0);
    // The `expr` may be nested in a `struct` or an `extend` of `struct`, and we use `StructTy` to get the `struct`.
    if (outDecl && Ty::IsTyCorrect(outDecl->node->ty) && outDecl->node->ty->IsStruct()) {
        sd = RawStaticCast<StructTy*>(outDecl->node->ty)->decl;
    }
    if (!sd) {
        return;
    }
    CheckMutationInStructNonMut(diag, *sd, expr);
}

bool TypeChecker::TypeCheckerImpl::CheckIfUseInout(const FuncDecl& decl)
{
    if (auto found = inoutCache.find(&decl); found != inoutCache.end()) {
        return found->second;
    }
    bool res = false;
    auto preVisit = [&res, &decl](Ptr<Node> node) -> VisitAction {
        auto ce = DynamicCast<CallExpr*>(node);
        if (!ce || ce->desugarExpr) {
            return VisitAction::WALK_CHILDREN;
        }
        for (const auto& arg : ce->args) {
            if (!arg->withInout) {
                continue;
            }
            auto tempExpr = arg->expr.get();
            while (tempExpr) {
                if (auto target = tempExpr->GetTarget(); target && target->outerDecl == decl.outerDecl) {
                    res = true;
                    return VisitAction::STOP_NOW;
                }
                if (auto re = DynamicCast<RefExpr*>(tempExpr); re) {
                    res = re->isThis;
                    return res ? VisitAction::STOP_NOW : VisitAction::SKIP_CHILDREN;
                }
                if (auto ma = DynamicCast<MemberAccess*>(tempExpr); ma) {
                    tempExpr = ma->baseExpr.get();
                }
            }
        }
        return VisitAction::WALK_CHILDREN;
    };
    Walker(decl.funcBody.get(), preVisit).Walk();
    return inoutCache.emplace(&decl, res).first->second;
}

// let instance of struct cannot access mut function.
void TypeChecker::TypeCheckerImpl::CheckLetInstanceAccessMutableFunc(
    const ASTContext& ctx, const MemberAccess& ma, const Decl& target)
{
    CJC_NULLPTR_CHECK(ma.baseExpr);
    if (!target.TestAttr(Attribute::MUT) || target.astKind != ASTKind::FUNC_DECL || !ma.baseExpr->ty ||
        !MaybeStruct(*ma.baseExpr->ty)) {
        return;
    }
    bool useInout = CheckIfUseInout(static_cast<const FuncDecl&>(target));
    Ptr<const MemberAccess> tempMa = &ma;
    while (tempMa->baseExpr != nullptr) {
        auto baseExpr = tempMa->baseExpr.get();
        // Get the real `baseExpr` by diving into the parentheses.
        while (baseExpr != nullptr && baseExpr->astKind == ASTKind::PAREN_EXPR) {
            baseExpr = StaticAs<ASTKind::PAREN_EXPR>(baseExpr)->expr.get();
        }
        bool inoutHeapAddr = useInout && baseExpr->ty->IsClassLike();
        if (inoutHeapAddr) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_inout_modify_heap_variable, *baseExpr);
        }
        // If the target of the `baseExpr` is a property or a `VarDecl` introduced by `let`, and it is of value type,
        // it cannot access mutable function.
        auto vd = DynamicCast<VarDecl*>(baseExpr->GetTarget());
        bool immutableAccessMutableFunc = vd && (vd->astKind == ASTKind::PROP_DECL || !vd->isVar) &&
            Ty::IsTyCorrect(vd->ty) && !vd->ty->IsClassLike();
        if (immutableAccessMutableFunc) {
            DiagImmutableAccessMutableFunc(diag, ma, *tempMa);
            return;
        }
        if (baseExpr->astKind == ASTKind::MEMBER_ACCESS) {
            tempMa = StaticAs<ASTKind::MEMBER_ACCESS>(baseExpr);
        } else if (baseExpr->astKind == ASTKind::REF_EXPR) {
            inoutHeapAddr = useInout && vd && vd->outerDecl && vd->outerDecl->ty->IsClassLike();
            if (inoutHeapAddr) {
                diag.DiagnoseRefactor(DiagKindRefactor::sema_inout_modify_heap_variable, *baseExpr);
            }
            break;
        } else if (Ty::IsTyCorrect(baseExpr->ty) && !baseExpr->ty->IsClassLike()) {
            DiagImmutableAccessMutableFunc(diag, ma, *tempMa);
            return;
        } else {
            break;
        }
    }
    if (ma.callOrPattern == nullptr) {
        auto range = ma.field.ZeroPos() ? MakeRange(ma.begin, ma.end) : MakeRange(ma.field);
        auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_use_mutable_func_alone, ma, range, ma.field.Val());
        builder.AddNote(
            target, MakeRangeForDeclIdentifier(target), "'" + target.identifier + "' is a mutable funciton");
    } else {
        CheckMutationInStruct(ctx, *ma.baseExpr);
    }
}
