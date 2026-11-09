// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "TypeCheckerImpl.h"

#include "Diags.h"
#include "TypeCheckUtil.h"

#include "cangjie/AST/RecoverDesugar.h"

using namespace Cangjie;
using namespace Sema;
using namespace TypeCheckUtil;

ModalTy TypeChecker::TypeCheckerImpl::SynTypeConvExpr(ASTContext& ctx, TypeConvExpr& tce)
{
    CJC_NULLPTR_CHECK(tce.expr);
    CJC_NULLPTR_CHECK(tce.type);
    Synthesize({ctx, SynPos::EXPR_ARG}, tce.expr.get());
    ReplaceIdealTy(*tce.expr);
    if (tce.type->astKind == ASTKind::PRIMITIVE_TYPE) {
        return SynNumTypeConvExpr(tce);
    }

    // CPointer to CFunc is handled elsewhere.
    // Therefore, the function should return already.
    // Otherwise, there must be errors reported.
    tce.SetTy({TypeManager::GetInvalidTy()});
    return tce.GetTy();
}

ModalTy TypeChecker::TypeCheckerImpl::SynNumTypeConvExpr(TypeConvExpr& tce)
{
    if (!tce.expr->GetTy().IsCorrect()) {
        tce.SetTy({TypeManager::GetInvalidTy()});
        return tce.GetTy();
    }
    // No postfix mode on TypeConv; result mode follows the operand type.
    tce.SetTy(ModalTy{
        TypeManager::GetPrimitiveTy(StaticCast<PrimitiveType>(tce.type.get())->kind), tce.expr->GetTy().Mode()});
    if (!tce.GetTy().IsCorrect()) {
        tce.SetTy({TypeManager::GetInvalidTy()});
        return tce.GetTy();
    }
    // Case 0: expr is of Nothing type, e.g., `UInt32(return)`
    bool isExprNothing =
        (tce.TyKind() == TypeKind::TYPE_RUNE || tce.GetTy()->IsNumeric()) && tce.expr->GetTy()->IsNothing();
    // Case 1: Rune to UInt32, e.g., `UInt32('a')`
    bool isRuneToUInt32 = tce.TyKind() == TypeKind::TYPE_UINT32 && tce.expr->TyKind() == TypeKind::TYPE_RUNE;
    // Case 2: Integer to Rune, e.g., `Rune(97)`
    bool isIntegerToChar = tce.TyKind() == TypeKind::TYPE_RUNE && tce.expr->GetTy()->IsInteger();
    // Case 3: convert between numeric types
    bool isBetweenNumeric = tce.GetTy()->IsNumeric() && tce.expr->GetTy()->IsNumeric();
    if (isExprNothing || isRuneToUInt32 || isIntegerToChar || isBetweenNumeric) {
        return tce.GetTy();
    }
    // Otherwise, return false.
    if (!CanSkipDiag(*tce.expr)) {
        diag.Diagnose(*tce.expr, DiagKind::sema_numeric_convert_must_be_numeric);
    }
    tce.SetTy({TypeManager::GetInvalidTy()});
    return tce.GetTy();
}

bool TypeChecker::TypeCheckerImpl::ChkTypeConvExpr(ASTContext& ctx, ModalTy targetTy, TypeConvExpr& tce)
{
    // Result mode follows the operand (see SynNumTypeConvExpr); require converted type <: target.
    if (SynTypeConvExpr(ctx, tce).IsCorrect() && typeManager.IsSubtype(tce.GetTy(), targetTy)) {
        return true;
    }
    if (!CanSkipDiag(tce)) {
        DiagMismatchedTypes(diag, tce, targetTy);
    }
    tce.SetTy({TypeManager::GetInvalidTy()});
    return false;
}
