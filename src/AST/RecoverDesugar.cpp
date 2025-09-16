// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This function includes all functions recovering node from desugar state.
 */

#include "cangjie/AST/RecoverDesugar.h"

#include "cangjie/AST/Match.h"

namespace Cangjie::AST {
namespace {
void UnsetCallExprOfNode(NameReferenceExpr& nre)
{
    nre.isAlone = true;
    nre.callOrPattern = nullptr;
}

OwnedPtr<Expr> TryRecoverFunctionCallExpr(OwnedPtr<Expr> base)
{
    if (base->astKind == ASTKind::MEMBER_ACCESS) {
        auto ma = RawStaticCast<MemberAccess*>(base.get());
        if (ma->field == "()") {
            return std::move(ma->baseExpr);
        }
    }
    return base;
}

void RecoverToPipelineExpr(BinaryExpr& be)
{
    CJC_NULLPTR_CHECK(be.desugarExpr);
    // Recover to PipelineExpr.
    auto& callExpr = StaticCast<CallExpr&>(*be.desugarExpr);
    be.rightExpr = TryRecoverFunctionCallExpr(std::move(callExpr.baseFunc));
    if (auto nre = DynamicCast<NameReferenceExpr*>(be.rightExpr.get())) {
        UnsetCallExprOfNode(*nre);
    }
    CJC_ASSERT(callExpr.args.size() == 1);
    be.leftExpr = std::move(callExpr.args[0]->expr);
    be.desugarExpr = OwnedPtr<Expr>();
}

void RecoverToCompositionExpr(BinaryExpr& be)
{
    CJC_NULLPTR_CHECK(be.desugarExpr);
    // Recover to CompositionExpr.
    auto& callExpr = StaticCast<CallExpr&>(*be.desugarExpr);
    CJC_ASSERT(callExpr.args.size() == 2); // there are 2 arguments
    CJC_NULLPTR_CHECK(callExpr.args[0]);
    be.leftExpr = TryRecoverFunctionCallExpr(std::move(callExpr.args[0]->expr));
    if (auto nre = DynamicCast<NameReferenceExpr*>(be.leftExpr.get())) {
        UnsetCallExprOfNode(*nre);
    }
    CJC_NULLPTR_CHECK(callExpr.args[1]);
    be.rightExpr = TryRecoverFunctionCallExpr(std::move(callExpr.args[1]->expr));
    if (auto nre = DynamicCast<NameReferenceExpr*>(be.rightExpr.get())) {
        UnsetCallExprOfNode(*nre);
    }
    be.desugarExpr = OwnedPtr<Expr>();
}
} // namespace

void RecoverToSubscriptExpr(SubscriptExpr& se)
{
    if (se.desugarExpr == nullptr) {
        return;
    }
    // Recover to SubscriptExpr.
    if (auto callExpr = DynamicCast<CallExpr*>(se.desugarExpr.get()); callExpr) {
        RecoverToCallExpr(*callExpr);
        auto ma = StaticCast<MemberAccess*>(callExpr->baseFunc.get());
        se.baseExpr = std::move(ma->baseExpr);
        CJC_ASSERT(se.indexExprs.size() == callExpr->args.size());
        for (size_t i = 0; i < callExpr->args.size(); ++i) {
            se.indexExprs[i] = std::move(callExpr->args[i]->expr);
        }
        se.desugarExpr = OwnedPtr<Expr>();
        if (auto nre = DynamicCast<NameReferenceExpr*>(se.baseExpr.get())) {
            UnsetCallExprOfNode(*nre);
        }
    }
}

void RecoverToUnaryExpr(UnaryExpr& ue)
{
    if (ue.desugarExpr == nullptr) {
        return;
    }
    // Recover to UnaryExpr.
    auto& callExpr = StaticCast<CallExpr&>(*ue.desugarExpr);
    RecoverToCallExpr(callExpr);
    auto ma = StaticCast<MemberAccess*>(callExpr.baseFunc.get());
    ue.expr = std::move(ma->baseExpr);
    if (auto nre = DynamicCast<NameReferenceExpr*>(ue.expr.get())) {
        UnsetCallExprOfNode(*nre);
    }
    ue.desugarExpr = OwnedPtr<Expr>();
}

void RecoverToBinaryExpr(BinaryExpr& be)
{
    if (be.desugarExpr == nullptr) {
        return;
    }
    // Case for tuple equal desugar.
    if (be.desugarExpr->astKind != AST::ASTKind::CALL_EXPR) {
        be.desugarExpr = OwnedPtr<Expr>();
        return;
    }
    RecoverToCallExpr(StaticCast<CallExpr&>(*be.desugarExpr));
    if (be.op == TokenKind::PIPELINE) {
        RecoverToPipelineExpr(be);
        return;
    } else if (be.op == TokenKind::COMPOSITION) {
        RecoverToCompositionExpr(be);
        return;
    }
    // Recover to BinaryExpr.
    auto callExpr = StaticCast<CallExpr*>(be.desugarExpr.get());
    auto ma = StaticCast<MemberAccess*>(callExpr->baseFunc.get());
    be.leftExpr = std::move(ma->baseExpr);
    be.rightExpr = std::move(callExpr->args[0]->expr);
    if (auto nre = DynamicCast<NameReferenceExpr*>(be.leftExpr.get())) {
        UnsetCallExprOfNode(*nre);
    }
    be.curFile->trashBin.emplace_back(std::move(be.desugarExpr));
    be.desugarExpr = OwnedPtr<Expr>();
}

void RecoverToAssignExpr(AssignExpr& ae)
{
    if (ae.desugarExpr == nullptr) {
        return;
    }
    if (ae.desugarExpr->astKind == AST::ASTKind::CALL_EXPR && ae.leftValue->astKind == AST::ASTKind::SUBSCRIPT_EXPR) {
        // Recover to AssignExpr.
        auto& callExpr = StaticCast<CallExpr&>(*ae.desugarExpr);
        RecoverToCallExpr(callExpr);
        auto ma = StaticCast<MemberAccess*>(callExpr.baseFunc.get());
        auto se = StaticCast<SubscriptExpr*>(ae.leftValue.get());
        se->baseExpr = std::move(ma->baseExpr);
        CJC_ASSERT(!callExpr.args.empty());
        for (size_t i = 0; i < callExpr.args.size() - 1; ++i) { // The last arg is right expr.
            se->indexExprs[i] = std::move(callExpr.args[i]->expr);
            se->indexExprs[i]->mapExpr = nullptr;
        }
        ae.leftValue->curFile = ae.curFile;
        if (ae.isCompound) {
            // Desugar of compound assignExpr will create binaryExpr as the last funcArg of the call.
            auto be = StaticCast<BinaryExpr*>(callExpr.args.back()->expr.get());
            ae.rightExpr = std::move(be->rightExpr);
        } else {
            ae.rightExpr = std::move(callExpr.args.back()->expr);
        }
        if (se->baseExpr) {
            se->baseExpr->mapExpr = nullptr;
            if (auto nre = DynamicCast<NameReferenceExpr*>(se->baseExpr.get())) {
                UnsetCallExprOfNode(*nre);
            }
        }
        ae.desugarExpr = OwnedPtr<Expr>();
    } else if (ae.desugarExpr->astKind == AST::ASTKind::ASSIGN_EXPR) {
        // Recover to AssignExpr.
        auto assignExpr = StaticCast<AssignExpr*>(ae.desugarExpr.get());
        auto& callExpr = StaticCast<CallExpr&>(*assignExpr->rightExpr);
        RecoverToCallExpr(callExpr);
        ae.leftValue = std::move(assignExpr->leftValue);
        ae.rightExpr = std::move(callExpr.args[0]->expr);
        ae.desugarExpr = OwnedPtr<Expr>();
        ae.leftValue->mapExpr = nullptr;
    }
}

void RecoverCallFromArrayExpr(CallExpr& ce)
{
    if (!ce.desugarExpr || ce.desugarExpr->astKind != ASTKind::ARRAY_EXPR) {
        return;
    }
    auto ae = StaticAs<ASTKind::ARRAY_EXPR>(ce.desugarExpr.get());
    for (auto& it : ae->args) {
        (void)ce.args.emplace_back(std::move(it));
    }
    ce.desugarExpr = OwnedPtr<Expr>();
}

void RecoverToCallExpr(CallExpr& ce)
{
    if (ce.desugarExpr == nullptr) {
        return;
    }
    // We should recover the `CallExpr` recursively, since `desugarExpr` can be nested,
    // e.g., function calling operator with variadic arguments.
    if (ce.desugarExpr->desugarExpr) {
        // The `desugarExpr` of a `CallExpr` must be `CallExpr`.
        CallExpr& desugarCallExpr = StaticCast<CallExpr&>(*ce.desugarExpr);
        RecoverToCallExpr(desugarCallExpr);
        ce.callKind = desugarCallExpr.callKind;
    }
    // Recover to CallExpr.
    if (ce.callKind == CallKind::CALL_VARIADIC_FUNCTION) {
        RecoverFromVariadicCallExpr(ce);
    } else if (auto callExpr = DynamicCast<CallExpr*>(ce.desugarExpr.get()); callExpr) {
        if (auto ma = DynamicCast<MemberAccess*>(callExpr->baseFunc.get()); ma) {
            if (ma->field == "()") {
                ce.baseFunc = std::move(ma->baseExpr);
                ce.args = std::move(callExpr->args);
                ce.curFile->trashBin.emplace_back(std::move(ce.desugarExpr));
            }
        }
    } else if (Is<ArrayExpr*>(ce.desugarExpr.get())) {
        RecoverCallFromArrayExpr(ce);
    } else if (auto pointerExpr = DynamicCast<PointerExpr*>(ce.desugarExpr.get()); pointerExpr) {
        if (pointerExpr->arg) {
            (void)ce.args.emplace_back(std::move(pointerExpr->arg));
        }
        ce.desugarExpr = OwnedPtr<Expr>();
    }
}

void RecoverFromVariadicCallExpr(CallExpr& ce)
{
    if (!ce.desugarExpr || ce.desugarExpr->astKind != ASTKind::CALL_EXPR) {
        return;
    }
    auto callExpr = StaticAs<ASTKind::CALL_EXPR>(ce.desugarExpr.get());
    ce.callKind = callExpr->callKind;
    ce.baseFunc = std::move(callExpr->baseFunc);
    CJC_ASSERT(!callExpr->args.empty()); // At least one `ArrayLit`.
    size_t idx = 0;
    size_t arrayLitIdx = 0;
    size_t arraySize = 0;
    for (; idx < callExpr->args.size(); ++idx) {
        if (idx + 1 < callExpr->args.size() && callExpr->args[idx + 1]->name.Empty()) {
            // Fixed positional arguments.
            ce.args[idx] = std::move(callExpr->args[idx]);
            continue;
        }
        arrayLitIdx = idx;
        auto arrayLit = StaticAs<ASTKind::ARRAY_LIT>(callExpr->args[arrayLitIdx]->expr.get());
        arraySize = arrayLit->children.size();
        // Variadic arguments.
        for (size_t i = 0; i < arraySize; ++i) {
            ce.args[arrayLitIdx + i]->expr = std::move(arrayLit->children[i]);
        }
        break;
    }
    idx++;
    // Named arguments.
    for (; idx < callExpr->args.size(); ++idx) {
        CJC_ASSERT(idx + arraySize > 0);
        auto originIdx = (idx + arraySize) - 1;
        CJC_ASSERT(originIdx < ce.args.size());
        ce.args[originIdx] = std::move(callExpr->args[idx]);
    }
    ce.desugarExpr = OwnedPtr<Expr>();
}
} // namespace Cangjie::AST
