// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "TypeCheckerImpl.h"

#include "DiagSuppressor.h"

using namespace Cangjie;
using namespace AST;

ModalTy TypeChecker::TypeCheckerImpl::SynSyncExpr(ASTContext& ctx, SynchronizedExpr& se)
{
    ChkSyncExpr(ctx, ModalTy{}, se);
    return se.GetTy();
}

bool TypeChecker::TypeCheckerImpl::ChkSyncExpr(ASTContext& ctx, ModalTy tgtTy, SynchronizedExpr& se)
{
    bool isWellTyped = true;
    auto lockDecl = importManager.GetSyncDecl("Lock");
    if (lockDecl) {
        isWellTyped = Check(ctx, lockDecl->GetTy(), se.mutex.get()) && isWellTyped;
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
        isWellTyped = Synthesize({ctx, SynPos::EXPR_ARG}, b.at(0).get()).IsCorrect() && isWellTyped;
        // Handle the mutex.lock().
        { // Create a scope for DiagSuppressor. Suppress errors raised by mutex.lock().
            auto ds = DiagSuppressor(diag);
            if (Synthesize({ctx, SynPos::EXPR_ARG}, b.at(1).get()).IsCorrect()) {
                ds.ReportDiag();
            } else {
                isWellTyped = false;
            }
        }
        // The child at 2 is a try expression.
        auto te = RawStaticCast<TryExpr*>(b.at(2).get());
        isWellTyped = (tgtTy ? ChkTryExpr(ctx, tgtTy, *te) : SynTryExpr(ctx, *te).IsCorrect()) && isWellTyped;
        se.desugarExpr->SetTy(isWellTyped ? te->GetTy() : ModalTy{TypeManager::GetInvalidTy()});
        se.SetTy(se.desugarExpr->GetTy());
    } else {
        isWellTyped = false;
        se.SetTy({TypeManager::GetInvalidTy()});
    }
    return isWellTyped;
}
