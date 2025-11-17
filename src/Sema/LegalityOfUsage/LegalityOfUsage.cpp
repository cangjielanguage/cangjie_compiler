// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements legality of usage checking after sema type completed.
 */

#include "TypeCheckerImpl.h"

#include "Diags.h"
#include "InitializationChecker.h"

namespace Cangjie {
using namespace AST;

namespace {
void SetFuncBodyCaptureKind(FuncBody& fb)
{
    if (!fb.body) {
        return;
    }
    // Collect mutable variables declared in the function `fb`.
    std::unordered_set<Ptr<Decl>> mutVars;
    Walker(fb.body.get(), [&mutVars](Ptr<Node> n) {
        if (auto varDecl = DynamicCast<VarDecl*>(n); varDecl && varDecl->isVar) {
            mutVars.emplace(varDecl);
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    auto visitor = [&fb, &mutVars](Ptr<const Node> n) {
        if (n->astKind != ASTKind::CALL_EXPR) {
            return VisitAction::WALK_CHILDREN;
        }
        // In the function body `fb`, if it calls some function that has captured some mutable variables,
        // we store the called function's body in `targetFuncBody`.
        auto& ce = static_cast<const CallExpr&>(*n);
        Ptr<FuncBody> targetFuncBody = nullptr;
        if (auto refExpr = DynamicCast<RefExpr*>(ce.baseFunc.get()); refExpr) {
            if (auto target = DynamicCast<FuncDecl*>(refExpr->ref.target);
                target && target->funcBody && target->funcBody->captureKind != CaptureKind::NO_CAPTURE) {
                targetFuncBody = target->funcBody.get();
            }
        } else if (auto lambdaExpr = DynamicCast<LambdaExpr*>(ce.baseFunc.get()); lambdaExpr) {
            CJC_ASSERT(lambdaExpr->funcBody); // Parser guarantees.
            if (lambdaExpr->funcBody->captureKind != CaptureKind::NO_CAPTURE) {
                targetFuncBody = lambdaExpr->funcBody.get();
            }
        }
        // If the called function captures mutable variables, we check whether the variables are defined inside `fb`.
        if (targetFuncBody) {
            std::copy_if(targetFuncBody->capturedVars.begin(), targetFuncBody->capturedVars.end(),
                std::inserter(fb.capturedVars, fb.capturedVars.begin()),
                [&mutVars](
                    Ptr<const NameReferenceExpr> varRe) { return mutVars.find(varRe->GetTarget()) == mutVars.cend(); });
            if (!fb.capturedVars.empty()) {
                fb.captureKind = CaptureKind::TRANSITIVE_CAPTURE;
                return VisitAction::STOP_NOW;
            }
        }
        return VisitAction::WALK_CHILDREN;
    };
    Walker walker(fb.body.get(), visitor);
    walker.Walk();
}
} // namespace

void TypeChecker::TypeCheckerImpl::CheckLegalityOfUsage(ASTContext& ctx, AST::Package& pkg)
{
    // Check whether value type decl contains value type recursive dependency.
    CheckValueTypeRecursive(pkg);
    // Check legality of reference usage.
    CheckLegalityOfReference(ctx, pkg);
    CheckStaticMembersWithGeneric(pkg);
    CheckUsageOfDeprecated(pkg);
    // Check initialization.
    if (!ci->invocation.globalOptions.disableSemaVic) {
        InitializationChecker::Check(*ci, ctx, &pkg);
    }
    CheckGlobalVarInitialization(pkg);
    // CFunc must be called in an unsafe block.
    CheckLegalityOfUnsafeAndInout(pkg);
    // Check structure declaration inheritance.
    CheckInheritance(pkg);
    CheckClosures(ctx, pkg);
    CheckAccessLevelValidity(pkg);
    CheckAllInvocationHasImpl(ctx, pkg);
    CheckSubscriptLegality(pkg);
}

void TypeChecker::TypeCheckerImpl::CheckStaticMemberWithGeneric(
    Decl& member, const std::vector<Ptr<Ty>>& outerGenericTys)
{
    // Static member variables, properties and init func can't depend on generic parameters of outer type.
    if (!member.TestAttr(Attribute::STATIC) || outerGenericTys.empty()) {
        return;
    }
    if (member.astKind != ASTKind::VAR_DECL && member.astKind != ASTKind::PROP_DECL && !IsStaticInitializer(member)) {
        return;
    }
    auto preVisitor = [this, outerGenericTys](Ptr<Node> node) {
        if (node == nullptr || (!Is<RefExpr>(node) && !Is<RefType>(node))) {
            return VisitAction::WALK_CHILDREN;
        }
        // Because static member var and prop cannot themselves declare generic parameters, static member var and
        // prop cannot contain any outside generic types.
        if (node->ty && node->ty->HasGeneric() && !node->begin.IsZero()) {
            std::set<Ptr<Ty>> targetTys;
            for (auto& usedTy : node->ty->GetGenericTyArgs()) {
                if (usedTy && Utils::In(usedTy, outerGenericTys)) {
                    targetTys.emplace(usedTy);
                    break;
                }
            }
            Sema::DiagForStaticVariableDependsGeneric(diag, *node, targetTys);
            return VisitAction::SKIP_CHILDREN;
        }
        // If the static var/let's initialization expression contains a static member function call of a generic class,
        // it's also not legal.
        // eg: class A<T> { static func foo() {1}; static var a = foo(); }
        //     'static var a = foo()' will be same as 'static var a = A<T>.foo()'.
        auto ref = node->astKind == ASTKind::REF_EXPR ? StaticCast<RefExpr>(node)->ref : StaticCast<RefType>(node)->ref;
        auto needDiag = ref.target &&
            (ref.target->astKind == ASTKind::FUNC_DECL || ref.target->astKind == ASTKind::PROP_DECL) &&
            ref.target->TestAttr(Attribute::STATIC) && ref.target->outerDecl && ref.target->outerDecl->ty &&
            ref.target->outerDecl->ty->HasGeneric();
        if (needDiag) {
            Sema::DiagForStaticVariableDependsGeneric(diag, *node, ref.target->outerDecl->ty->GetGenericTyArgs());
        }
        return VisitAction::SKIP_CHILDREN;
    };
    Walker(&member, preVisitor).Walk();
}

void TypeChecker::TypeCheckerImpl::CheckStaticMembersWithGeneric(const Package& pkg)
{
    IterateToplevelDecls(pkg, [this](auto& decl) {
        if (!decl || !decl->generic || decl->generic->typeParameters.empty()) {
            return;
        }
        for (auto& member : decl->GetMemberDecls()) {
            std::vector<Ptr<Ty>> outersideGenericParamTys;
            for (auto& tp : decl->generic->typeParameters) {
                CJC_ASSERT(tp && Ty::IsTyCorrect(tp->ty));
                outersideGenericParamTys.emplace_back(tp->ty);
            }
            CheckStaticMemberWithGeneric(*member, outersideGenericParamTys);
        }
    });
}

void TypeChecker::TypeCheckerImpl::CheckValueTypeRecursive(const Package& pkg)
{
    IterateToplevelDecls(pkg, [this](auto& decl) {
        if (decl->astKind == ASTKind::ENUM_DECL || decl->astKind == ASTKind::STRUCT_DECL) {
            CheckValueTypeRecursiveDFS(decl.get());
        }
    });
    for (auto& instantiatedDecl : pkg.genericInstantiatedDecls) {
        if (instantiatedDecl->astKind == ASTKind::ENUM_DECL || instantiatedDecl->astKind == ASTKind::STRUCT_DECL) {
            CheckValueTypeRecursiveDFS(instantiatedDecl.get());
        }
    }
}

void TypeChecker::TypeCheckerImpl::CheckClosures(const ASTContext& ctx, Node& node) const
{
    // 1. mark all reference capture status.
    Walker(&node, [&ctx, this](auto node) {
        if (auto ma = DynamicCast<MemberAccess*>(node); ma && ma->baseExpr && IsThisOrSuper(*ma->baseExpr)) {
            MarkAndCheckRefExprVarCaptureStatus(ctx, *ma);
        } else if (auto re = DynamicCast<RefExpr*>(node); re) {
            MarkAndCheckRefExprVarCaptureStatus(ctx, *re);
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    // 2. set all funcBody capture status.
    Walker(&node, nullptr, [](auto node) {
        if (auto fb = DynamicCast<FuncBody*>(node); fb) {
            SetFuncBodyCaptureKind(*fb);
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    // 3. diagnose for invalid capture.
    Walker(&node, [&ctx, this](Ptr<Node> n) {
        CheckLegalUseOfClosure(ctx, *n);
        return VisitAction::WALK_CHILDREN;
    }).Walk();
}

void TypeChecker::TypeCheckerImpl::CheckSubscriptLegality(Node& node)
{
    auto postVisit = [this](Ptr<Node> node) -> VisitAction {
        if (node->astKind != ASTKind::SUBSCRIPT_EXPR) {
            return VisitAction::WALK_CHILDREN;
        }
        auto se = StaticCast<SubscriptExpr*>(node);
        // Checking the Validity of VArray Subscript Access
        if (!se->baseExpr || !Ty::IsTyCorrect(se->baseExpr->ty) || !Is<VArrayTy>(se->baseExpr->ty)) {
            return VisitAction::WALK_CHILDREN;
        }
        auto varrTy = StaticCast<VArrayTy*>(se->baseExpr->ty);
        CJC_ASSERT(!se->indexExprs.empty());
        if (se->indexExprs[0]->isConst) {
            auto index = se->indexExprs[0]->constNumValue.asInt;
            if (index.IsOutOfRange()) {
                return VisitAction::SKIP_CHILDREN;
            }
            if (index.Uint64() >= static_cast<uint64_t>(varrTy->size) && se->ShouldDiagnose(true)) {
                auto builder = diag.Diagnose(*se, DiagKind::sema_builtin_index_in_bound, VARRAY_NAME);
                std::string hint;
                if (index.Int64() < 0) {
                    hint = "'VArray' index can not be negative";
                } else {
                    hint = "'VArray' index " + index.GetValue() + " is past the end of 'VArray' (which contains " +
                        std::to_string(varrTy->size) + " elements)";
                }
                builder.AddHint(*se->indexExprs[0], hint);
                return VisitAction::SKIP_CHILDREN;
            }
        }
        return VisitAction::WALK_CHILDREN;
    };
    Walker walker(&node, nullptr, postVisit);
    walker.Walk();
}

void TypeChecker::TypeCheckerImpl::CheckAllInvocationHasImpl(const ASTContext& ctx, Node& node)
{
    auto preVisit = [this, &ctx](Ptr<Node> node) -> VisitAction {
        if (node->astKind != ASTKind::MEMBER_ACCESS) {
            return VisitAction::WALK_CHILDREN;
        }
        auto& ma = *RawStaticCast<MemberAccess*>(node);
        auto target = ma.GetTarget();
        if (!target) {
            return VisitAction::WALK_CHILDREN;
        }
        bool isInterfaceAccess = ma.baseExpr && ma.baseExpr->ty && ma.baseExpr->ty->IsInterface();
        bool isStaticFuncOrProp = target->TestAttr(Attribute::STATIC) && target->IsFuncOrProp() && target->outerDecl;
        if (isInterfaceAccess && isStaticFuncOrProp) {
            std::unordered_set<Ptr<AST::Decl>> traversedDecls = {};
            MultiTypeSubst typeMapping;
            if (ma.matchedParentTy && target->outerDecl->ty) {
                typeMapping = promotion.GetPromoteTypeMapping(*ma.matchedParentTy, *target->outerDecl->ty);
            }
            auto baseDecl = Ty::GetDeclPtrOfTy(ma.baseExpr->ty);
            MultiTypeSubst typeMapping1;
            if (baseDecl->ty) {
                typeMapping1 = promotion.GetPromoteTypeMapping(*ma.baseExpr->ty, *baseDecl->ty);
            }
            typeMapping.merge(typeMapping1);
            auto ret = CheckInvokeTargetHasImpl(ctx, *ma.baseExpr->ty, *target, typeMapping, traversedDecls);
            if (ret.first && ret.second) {
                auto retTarget = ret.second->GetTarget();
                std::string strType = retTarget->IsFunc() ? "function" : "property";
                std::string strNote = "indirect use of unimplemented " + strType + " reference here";
                auto builder = diag.DiagnoseRefactor(
                    DiagKindRefactor::sema_interface_call_with_unimplemented_call, ma, strType, retTarget->identifier);
                builder.AddNote(MakeRange(ret.second->begin, ret.second->end), strNote);
                return VisitAction::SKIP_CHILDREN;
            }
        }
        return VisitAction::WALK_CHILDREN;
    };
    Walker walker(&node, preVisit);
    walker.Walk();
}
} // namespace Cangjie
