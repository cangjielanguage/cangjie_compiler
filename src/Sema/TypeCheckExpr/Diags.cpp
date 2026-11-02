// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "Diags.h"

#include "TypeCheckUtil.h"
#include "cangjie/Basic/DiagnosticEngine.h"

using namespace Cangjie;
using namespace AST;

namespace Cangjie::Sema {
void DiagInvalidMultipleAssignExpr(
    DiagnosticEngine& diag, const Node& leftNode, const Expr& rightExpr, const std::string& because)
{
    auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_mismatched_types_multiple_assign, rightExpr);
    builder.AddMainHintArguments(rightExpr.GetTy().String());
    builder.AddHint(leftNode, because);
}

void DiagInvalidBinaryExpr(DiagnosticEngine& diag, const BinaryExpr& be)
{
    CJC_NULLPTR_CHECK(be.leftExpr);
    CJC_NULLPTR_CHECK(be.rightExpr);
    CJC_ASSERT(be.leftExpr->GetTy().IsCorrect());
    CJC_ASSERT(be.rightExpr->GetTy().IsCorrect());
    const std::string& opStr = TOKENS[static_cast<int>(be.op)];
    auto range = be.operatorPos.IsZero() ? MakeRange(be.begin, be.end) : MakeRange(be.operatorPos, opStr);
    auto leftTy = be.leftExpr->GetTy();
    auto rightTy = be.rightExpr->GetTy();
    auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_invalid_binary_expr, be, range, opStr, leftTy.String(),
        rightTy.String());
    if (leftTy->IsFunc() || leftTy->IsCFunc() || leftTy->IsTuple()) {
        // func and tuple type cannot be extended
        return;
    }
    if (TypeCheckUtil::IsOverloadableOperator(be.op)) {
        std::string note("you may want to implement 'operator func " + opStr + "(");
        if (!leftTy.IsDataType()) {
            note += "this" + leftTy.Mode().AsTypeSuffixString() + ", ";
        }
        // explictly drop mode of leftTy, because it is already shown as ThisParam
        note += "right: " + rightTy.String() + ")' for type '" + leftTy->String() + "'";
        if (be.op == TokenKind::EXP) {
            if (be.leftExpr->TyKind() == TypeKind::TYPE_INT64) {
                note += ", or to provide a right operand of type 'UInt64'";
            } else if (be.leftExpr->TyKind() == TypeKind::TYPE_FLOAT64) {
                note += ", or to provide a right operand of type 'Int64' or 'Float64'";
            } else if (be.rightExpr->TyKind() == TypeKind::TYPE_INT64) {
                note += ", or to provide a left operand of type 'Float64'";
            } else if (be.rightExpr->TyKind() == TypeKind::TYPE_FLOAT64) {
                note += ", or to provide a left operand of type 'Float64'";
            } else if (be.rightExpr->TyKind() == TypeKind::TYPE_UINT64) {
                note += ", or to provide a left operand of type 'Int64'";
            }
        }
        builder.AddNote(note);
    }
}

void DiagInvalidUnaryExpr(DiagnosticEngine& diag, const UnaryExpr& ue)
{
    if (!ue.ShouldDiagnose()) {
        return;
    }
    CJC_NULLPTR_CHECK(ue.expr);
    CJC_ASSERT(ue.expr->GetTy().IsCorrect());
    const std::string& opStr = TOKENS[static_cast<int>(ue.op)];
    auto ty = ue.expr->GetTy();
    auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_invalid_unary_expr, ue, opStr, ty.String());
    if (ty->IsExtendable()) {
        std::string note("you may want to implement 'operator func " + opStr + "(");
        if (!ty.IsDataType()) {
            note += "this" + ty.Mode().AsTypeSuffixString();
        }
        note += ")' for type '" + ty.String() + "'";
        builder.AddNote(note);
    }
}

void DiagInvalidUnaryExprWithTarget(DiagnosticEngine& diag, const UnaryExpr& ue, ModalTy target)
{
    if (!ue.ShouldDiagnose()) {
        return;
    }
    CJC_NULLPTR_CHECK(ue.expr);
    CJC_ASSERT(ue.expr->GetTy().IsCorrect());
    CJC_ASSERT(target.IsCorrect());
    const std::string& opStr = TOKENS[static_cast<int>(ue.op)];
    auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_invalid_unary_expr_with_target, ue, opStr,
        ue.expr->GetTy().String(), target.String());
}

void DiagInvalidSubscriptExpr(
    DiagnosticEngine& diag, const SubscriptExpr& se, ModalTy baseTy, const std::vector<ModalTy>& indexTys)
{
    if (!se.ShouldDiagnose()) {
        return;
    }
    CJC_ASSERT(!indexTys.empty());
    CJC_ASSERT(baseTy.IsCorrect());
    CJC_ASSERT(Ty::AreTysCorrect(indexTys));
    std::string indexPrefix = "type" + (indexTys.size() > 1 ? std::string("s ") : " ");
    std::string indexStr = indexPrefix + "'";
    for (size_t i = 0; i < indexTys.size(); ++i) {
        if (i != 0) {
            indexStr += "', '";
        }
        indexStr += indexTys[i].String();
    }
    indexStr += "'";
    auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_invalid_subscript_expr, se, baseTy.String(), indexStr);
    if (baseTy->IsExtendable()) {
        std::string note("you may want to implement 'operator func [](");
        if (!baseTy.IsDataType()) {
            note += "this" + baseTy.Mode().AsTypeSuffixString() + ", ";
        }
        for (size_t i = 0; i < indexTys.size(); ++i) {
            note += "index" + std::to_string(i) + ": " + indexTys[i].String();
            if (i != indexTys.size() - 1) {
                note += ", ";
            }
        }
        note += ")' for type '" + baseTy.String() + "'";
        builder.AddNote(note);
    }
}

void DiagUnableToInferExpr(DiagnosticEngine& diag, const Expr& expr)
{
    (void)diag.DiagnoseRefactor(DiagKindRefactor::sema_unable_to_infer_expr, expr);
}
} // namespace Cangjie::Sema
