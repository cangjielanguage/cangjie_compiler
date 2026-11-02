// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "TypeCheckerImpl.h"

using namespace Cangjie;
using namespace AST;

ModalTy TypeChecker::TypeCheckerImpl::SynParenExpr(const CheckerContext& ctx, ParenExpr& pe)
{
    Synthesize(ctx, pe.expr.get());
    if (!pe.expr || !pe.expr->GetTy().IsCorrect()) {
        pe.SetTy({TypeManager::GetInvalidTy()});
        return {TypeManager::GetInvalidTy()};
    }

    // Keep ideal literal types pending for a parenthesized literal in the body of a generic-call
    // lambda argument; parentheses must not break contextual type propagation (e.g. `TypeTest({=> (0)})`
    // expecting `() -> Int32`).
    if (pe.expr->GetTy()->IsIdeal() && ctx.Ctx().inFuncArgLambdaBody == 0) {
        ReplaceIdealTy(*pe.expr);
    }
    pe.SetTy(pe.expr->GetTy());
    if (pe.expr->isConst) {
        pe.isConst = true;
        pe.constNumValue = pe.expr->constNumValue;
    }
    return pe.GetTy();
}

bool TypeChecker::TypeCheckerImpl::ChkParenExpr(ASTContext& ctx, ModalTy target, ParenExpr& pe)
{
    if (Check(ctx, target, pe.expr.get())) {
        CJC_NULLPTR_CHECK(pe.expr); // When the Check's result is true, pe.expr must not be nullptr.
        pe.SetTy(pe.expr->GetTy());
        if (pe.expr->isConst) {
            pe.isConst = true;
            pe.constNumValue = pe.expr->constNumValue;
        }
        return true;
    } else {
        pe.SetTy({TypeManager::GetInvalidTy()});
        return false;
    }
}
