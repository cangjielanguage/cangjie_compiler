// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "TypeCheckerImpl.h"

#include "DiagSuppressor.h"

using namespace Cangjie;
using namespace AST;

Ptr<Ty> TypeChecker::TypeCheckerImpl::SynSyncExpr(ASTContext& ctx, SynchronizedExpr& se)
{
    ChkSyncExpr(ctx, nullptr, se);
    return se.ty;
}

bool TypeChecker::TypeCheckerImpl::ChkSyncExpr(ASTContext& ctx, Ptr<Ty> tgtTy, SynchronizedExpr& se)
{
    bool isWellTyped = true;
    auto lockDecl = importManager.GetSyncDecl("Lock");
    if (lockDecl) {
        isWellTyped = Check(ctx, lockDecl->ty, se.mutex.get()) && isWellTyped;
    } else {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_use_expr_without_import, *se.mutex, "sync", "synchronized");
        // Do not return false immediately so that more (and independent) error messages could be reported.
    }

    // Given sync (e) { b }, always check b if b exists (even if e is ill-typed).
    if (se.desugarExpr) {
        auto& b = RawStaticCast<Block*>(se.desugarExpr.get())->body;
        // The desugared expression must have 3 children: a mutex declaration, mutex.lock() and a try expression.
        CJC_ASSERT(b.size() == 3);
        // Handle the mutex variable declaration.
        isWellTyped = Ty::IsTyCorrect(Synthesize(ctx, b.at(0).get())) && isWellTyped;
        // Handle the mutex.lock().
        { // Create a scope for DiagSuppressor. Suppress errors raised by mutex.lock().
            auto ds = DiagSuppressor(diag);
            if (Ty::IsTyCorrect(Synthesize(ctx, b.at(1).get()))) {
                ds.ReportDiag();
            } else {
                isWellTyped = false;
            }
        }
        // The child at 2 is a try expression.
        auto te = RawStaticCast<TryExpr*>(b.at(2).get());
        isWellTyped = (tgtTy ? ChkTryExpr(ctx, *tgtTy, *te) : Ty::IsTyCorrect(SynTryExpr(ctx, *te))) && isWellTyped;
        se.desugarExpr->ty = isWellTyped ? te->ty : TypeManager::GetInvalidTy();
        se.ty = se.desugarExpr->ty;
    } else {
        isWellTyped = false;
        se.ty = TypeManager::GetInvalidTy();
    }
    return isWellTyped;
}
