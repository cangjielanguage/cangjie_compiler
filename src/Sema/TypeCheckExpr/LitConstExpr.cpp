// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "TypeCheckerImpl.h"

#include "Diags.h"
#include "TypeCheckUtil.h"

using namespace Cangjie;
using namespace Sema;
using namespace TypeCheckUtil;

namespace {
ModalTy GetInnerNumericType(ModalTy boxModalTy, ModalTy basicTy)
{
    // This function only used after type was checked by "IsLitBoxableType" for integer and float LitConst.
    // that guaranteed boxTy must be N-dims Option<T> where T is numeric type.
    if (!boxModalTy->IsEnum() || boxModalTy->HasInvalidTy()) {
        return boxModalTy->IsNumeric() ? boxModalTy : basicTy;
    }
    auto enumTy = RawStaticCast<EnumTy*>(boxModalTy.Ty());
    CJC_ASSERT(!enumTy->typeArgs.empty());
    return GetInnerNumericType(enumTy->typeArgs[0], basicTy);
}
} // namespace

bool TypeChecker::TypeCheckerImpl::ChkLitConstExprOfTypeBool(ModalTy target, LitConstExpr& lce)
{
    if (target->IsBooleanSubType()) {
        lce.SetTy(target);
        return true;
    } else if (typeManager.IsLitBoxableType(TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN), target.Ty())) {
        lce.SetTy({TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN)});
        return true;
    } else if (target->IsAny() || target->IsCType()) {
        lce.SetTy({TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN)});
        return true;
    } else {
        diag.Diagnose(lce, DiagKind::sema_cannot_convert_literal, "a boolean", target.String());
        lce.SetTy(TypeManager::GetNonNullTy(lce.GetTy()));
        return false;
    }
}

bool TypeChecker::TypeCheckerImpl::ChkLitConstExprOfTypeUnit(ModalTy target, LitConstExpr& lce)
{
    if (target->IsAny() || target->IsUnit() || target->IsCType() ||
        typeManager.IsLitBoxableType(TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT), target.Ty())) {
        lce.SetTy({TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT)});
        return true;
    } else {
        DiagMismatchedTypesWithFoundTy(diag, lce, target.String(), "Unit");
        lce.SetTy({TypeManager::GetInvalidTy()});
        return false;
    }
}

bool TypeChecker::TypeCheckerImpl::ChkLitConstExprOfTypeInteger(ModalTy target, LitConstExpr& lce)
{
    TypeKind intSuffixTokenKind = lce.GetNumLitTypeKind();
    TypeKind defaultIntTokenKind =
        intSuffixTokenKind == TypeKind::TYPE_IDEAL_INT ? TypeKind::TYPE_INT64 : intSuffixTokenKind;
    if (target->IsIntegerSubType()) {
        if (intSuffixTokenKind == target.Kind() || intSuffixTokenKind == TypeKind::TYPE_IDEAL_INT) {
            lce.SetTy(target);
            return true;
        } else {
            diag.Diagnose(lce, DiagKind::sema_cannot_convert_literal, lce.stringValue, target.String());
            return false;
        }
    } else if (typeManager.IsLitBoxableType(TypeManager::GetPrimitiveTy(defaultIntTokenKind), target.Ty())) {
        // Check for extendable or option boxable type as int64.
        lce.SetTy(GetInnerNumericType(target, ModalTy{TypeManager::GetPrimitiveTy(defaultIntTokenKind)}));
        return true;
    } else if (target->IsAny() || target->IsCType()) {
        lce.SetTy(GetInnerNumericType(target, ModalTy{TypeManager::GetPrimitiveTy(defaultIntTokenKind)}));
        return true;
    } else if (!target->IsInvalid()) {
        diag.Diagnose(lce, DiagKind::sema_cannot_convert_literal, "an integer", target.String());
        lce.SetTy({TypeManager::GetInvalidTy()});
        return false;
    }
    lce.SetTy({TypeManager::GetNonNullTy(lce.GetTy())});
    return false;
}

bool TypeChecker::TypeCheckerImpl::ChkLitConstExprOfTypeFloat(ModalTy targetTy, LitConstExpr& lce)
{
    TypeKind intSuffixTokenKind = lce.GetNumLitTypeKind();
    TypeKind defaultFloat64TokenKind =
        intSuffixTokenKind == TypeKind::TYPE_IDEAL_FLOAT ? TypeKind::TYPE_FLOAT64 : intSuffixTokenKind;
    if (targetTy->IsFloatingSubType()) {
        if (intSuffixTokenKind == targetTy.Kind() || intSuffixTokenKind == TypeKind::TYPE_IDEAL_FLOAT) {
            lce.SetTy(targetTy);
            return true;
        } else {
            diag.Diagnose(lce, DiagKind::sema_cannot_convert_literal, lce.stringValue, targetTy.String());
            return false;
        }
    } else if (typeManager.IsLitBoxableType(TypeManager::GetPrimitiveTy(defaultFloat64TokenKind), targetTy.Ty())) {
        // Check for extendable or option boxable type as float64.
        lce.SetTy(GetInnerNumericType(targetTy, ModalTy{TypeManager::GetPrimitiveTy(defaultFloat64TokenKind)}));
        return true;
    } else if (targetTy->IsAny() || targetTy->IsCType()) {
        lce.SetTy(GetInnerNumericType(targetTy, ModalTy{TypeManager::GetPrimitiveTy(defaultFloat64TokenKind)}));
        return true;
    } else {
        diag.Diagnose(lce, DiagKind::sema_cannot_convert_literal, "a floating-point", targetTy.String());
        lce.SetTy({TypeManager::GetNonNullTy(lce.GetTy())});
        return false;
    }
}

bool TypeChecker::TypeCheckerImpl::ChkLitConstExprOfTypeChar(ModalTy targetTy, LitConstExpr& lce)
{
    if (targetTy.Ty() == TypeManager::GetPrimitiveTy(TypeKind::TYPE_RUNE)) {
        lce.SetTy(targetTy);
        return true;
    } else if (typeManager.IsLitBoxableType(TypeManager::GetPrimitiveTy(TypeKind::TYPE_RUNE), targetTy.Ty())) {
        lce.SetTy({TypeManager::GetPrimitiveTy(TypeKind::TYPE_RUNE)});
        return true;
    } else if (targetTy->IsAny()) {
        lce.SetTy({TypeManager::GetPrimitiveTy(TypeKind::TYPE_RUNE)});
        return true;
    } else {
        diag.Diagnose(lce, DiagKind::sema_cannot_convert_literal, "a character", targetTy.String());
        lce.SetTy(TypeManager::GetNonNullTy(lce.GetTy()));
        return false;
    }
}

ModalTy TypeChecker::TypeCheckerImpl::SynLitConstStringExpr(ASTContext& ctx, LitConstExpr& lce)
{
    // For string literal expr.
    if (!lce.siExpr) {
        lce.SetTy(Synthesize({ctx, SynPos::EXPR_ARG}, lce.ref.get()));
        return lce.GetTy();
    }
    // For String Interpolation.
    // 1. Get Struct-String and Interface-ToString type.
    auto stringDecl = importManager.GetCoreDecl<InheritableDecl>(STD_LIB_STRING);
    auto toStringInterface = importManager.GetCoreDecl<InheritableDecl>(TOSTRING_NAME);
    if (!stringDecl || !toStringInterface) {
        lce.SetTy({TypeManager::GetInvalidTy()});
        return lce.GetTy();
    }
    // 2. Check all interpolated expressions.
    auto strExpr = lce.siExpr.get();
    bool isWellTyped = true;
    for (auto& expr : strExpr->strPartExprs) {
        if (expr->astKind != ASTKind::INTERPOLATION_EXPR) {
            isWellTyped = Check(ctx, stringDecl->GetTy(), expr.get()) && isWellTyped;
            continue;
        }
        auto ie = StaticCast<InterpolationExpr*>(expr.get());
        CJC_NULLPTR_CHECK(ie->block);
        ie->block->SetTy(Synthesize({ctx, SynPos::EXPR_ARG}, ie->block.get()));
        // use data type when check ToString impl
        if (!typeManager.IsSubtype(ie->block->DataTy(), toStringInterface->DataTy())) {
            if (Ty::IsTyCorrect(ie->block->DataTy())) {
                diag.Diagnose(*ie->block, DiagKind::sema_invalid_string_implementation, ie->block->DataTy()->String());
            }
            isWellTyped = false;
        } else if (ie->block->GetTy()->IsNothing()) {
            // After typechecker, it will desugar as 'expr.toString()', and Nothing type can't access member.
            diag.DiagnoseRefactor(DiagKindRefactor::sema_undeclared_identifier, *ie->block, "toString");
            isWellTyped = false;
        }
        if (!isWellTyped) {
            ie->block->SetTy({TypeManager::GetInvalidTy()});
        }
        ie->SetTy(ie->block->GetTy());
    }
    // If not all interpolated expression check passed, directly quit current check.
    if (!isWellTyped) {
        lce.SetTy({TypeManager::GetInvalidTy()});
        return lce.GetTy();
    }
    lce.siExpr->SetTy(stringDecl->GetTy());
    lce.SetTy(stringDecl->GetTy());
    return lce.GetTy();
}

bool TypeChecker::TypeCheckerImpl::ChkLitConstExprOfTypeString(ASTContext& ctx, ModalTy target, LitConstExpr& lce)
{
    auto ty = SynLitConstStringExpr(ctx, lce);
    bool isWellTyped = typeManager.IsLitBoxableType(ty.Ty(), target.Ty()) || target->IsAny();
    if (!isWellTyped && !CanSkipDiag(lce)) {
        DiagMismatchedTypesWithFoundTy(diag, lce, target.String(), "Struct-String");
    } else {
        lce.SetTy(lce.GetTy().With(target.Mode()));
    }
    return isWellTyped;
}

bool TypeChecker::TypeCheckerImpl::ChkLitConstExpr(ASTContext& ctx, ModalTy target, LitConstExpr& lce)
{
    switch (lce.kind) {
        case LitConstKind::BOOL:
            return ChkLitConstExprOfTypeBool(target, lce);
        case LitConstKind::UNIT:
            return ChkLitConstExprOfTypeUnit(target, lce);
        case LitConstKind::INTEGER:
        case LitConstKind::RUNE_BYTE:
            return ChkLitConstExprOfTypeInteger(target, lce) && ChkLitConstExprRange(lce);
        case LitConstKind::FLOAT:
            return ChkLitConstExprOfTypeFloat(target, lce) && ChkLitConstExprRange(lce);
        case LitConstKind::RUNE:
            return ChkLitConstExprOfTypeChar(target, lce);
        case LitConstKind::STRING:
        case LitConstKind::JSTRING:
            return ChkLitConstExprOfTypeString(ctx, target, lce);
        default:
            lce.SetTy({TypeManager::GetInvalidTy()});
            return false;
    }
}

ModalTy TypeChecker::TypeCheckerImpl::SynLitConstExpr(ASTContext& ctx, LitConstExpr& lce)
{
    switch (lce.kind) {
        case LitConstKind::BOOL:
            lce.SetTy({TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN)});
            break;
        case LitConstKind::UNIT:
            lce.SetTy({TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT)});
            break;
        case LitConstKind::INTEGER:
        case LitConstKind::RUNE_BYTE:
        case LitConstKind::FLOAT:
            if (TypeKind kind = lce.GetNumLitTypeKind(); kind == TypeKind::TYPE_INVALID) {
                lce.SetTy({TypeManager::GetInvalidTy()});
            } else {
                lce.SetTy({TypeManager::GetPrimitiveTy(kind)});
            }
            break;
        case LitConstKind::STRING:
        case LitConstKind::JSTRING:
            lce.SetTy(SynLitConstStringExpr(ctx, lce));
            break;
        case LitConstKind::RUNE:
            lce.SetTy({TypeManager::GetPrimitiveTy(TypeKind::TYPE_RUNE)});
            break;
        case LitConstKind::NONE:
            lce.SetTy({TypeManager::GetInvalidTy()});
            break;
        default:
            CJC_ABORT();
            break;
    }
    ChkLitConstExprRange(lce);
    return lce.GetTy();
}
