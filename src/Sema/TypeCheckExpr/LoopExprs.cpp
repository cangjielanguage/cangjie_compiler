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
ModalTy GetIterableTy(TypeManager& tyMgr, ImportManager& importManager, Promotion& promotion, ModalTy ty)
{
    // Promote implemented iterable type except nothing type.
    if (ty->IsNothing()) {
        return {TypeManager::GetInvalidTy()};
    }
    auto iterableInterface = importManager.GetCoreDecl("Iterable");
    if (auto genTy = DynamicCast<GenericsTy*>(ty.get()); genTy && genTy->isPlaceholder) {
        if (auto placeholderItTy = tyMgr.ConstrainByCtor(*genTy, *iterableInterface->GetTy())) {
            return placeholderItTy;
        } else {
            return {TypeManager::GetInvalidTy()};
        }
    }
    if (iterableInterface) {
        auto prTys = promotion.Promote(ty, iterableInterface->GetTy());
        CJC_ASSERT(prTys.size() <= 1);
        return prTys.empty() ? ModalTy{TypeManager::GetInvalidTy()} : *prTys.begin();
    }
    return {TypeManager::GetInvalidTy()};
}
} // namespace

bool TypeChecker::TypeCheckerImpl::ChkWhileExpr(ASTContext& ctx, ModalTy target, WhileExpr& we)
{
    ModalTy unitTy{TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT)};
    bool isWellTyped = typeManager.IsSubtype(unitTy, target);
    if (!isWellTyped) {
        DiagMismatchedTypesWithFoundTy(diag, we, target, unitTy);
    }
    isWellTyped = SynWhileExpr(ctx, we).IsCorrect() && isWellTyped;
    return isWellTyped;
}

ModalTy TypeChecker::TypeCheckerImpl::SynWhileExpr(ASTContext& ctx, WhileExpr& we)
{
    bool isWellTyped = CheckCondition(ctx, *we.condExpr, false);
    isWellTyped = Synthesize({ctx, SynPos::UNUSED}, we.body.get()).IsCorrect() && isWellTyped;
    we.SetTy(
        isWellTyped ? ModalTy{TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT)} : ModalTy{TypeManager::GetInvalidTy()});
    return we.GetTy();
}

bool TypeChecker::TypeCheckerImpl::ChkDoWhileExpr(ASTContext& ctx, ModalTy target, DoWhileExpr& dwe)
{
    ModalTy unitTy{TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT)};
    bool isWellTyped = typeManager.IsSubtype(unitTy, target);
    if (!isWellTyped) {
        DiagMismatchedTypesWithFoundTy(diag, dwe, target, unitTy);
    }
    isWellTyped = SynDoWhileExpr(ctx, dwe).IsCorrect() && isWellTyped;
    return isWellTyped;
}

ModalTy TypeChecker::TypeCheckerImpl::SynDoWhileExpr(ASTContext& ctx, DoWhileExpr& dwe)
{
    bool isWellTyped = Synthesize({ctx, SynPos::UNUSED}, dwe.body.get()).IsCorrect();
    isWellTyped = CheckCondition(ctx, *dwe.condExpr, false) && isWellTyped;
    dwe.SetTy(
        isWellTyped ? ModalTy{TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT)} : ModalTy{TypeManager::GetInvalidTy()});
    return dwe.GetTy();
}

bool TypeChecker::TypeCheckerImpl::ChkForInExpr(ASTContext& ctx, ModalTy target, ForInExpr& fie)
{
    ModalTy unitTy{TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT)};
    bool isWellTyped = typeManager.IsSubtype(unitTy, target);
    if (!isWellTyped) {
        DiagMismatchedTypesWithFoundTy(diag, fie, target, unitTy);
    }
    isWellTyped = SynForInExpr(ctx, fie).IsCorrect() && isWellTyped;
    if (!isWellTyped) {
        fie.SetTy({TypeManager::GetInvalidTy()});
    }
    return isWellTyped;
}

ModalTy TypeChecker::TypeCheckerImpl::SynForInExpr(ASTContext& ctx, ForInExpr& fie)
{
    CJC_NULLPTR_CHECK(fie.inExpression);
    CJC_NULLPTR_CHECK(fie.pattern);

    bool isWellTyped =
        Synthesize({ctx, SynPos::EXPR_ARG}, fie.inExpression.get()).IsCorrect() && ReplaceIdealTy(*fie.inExpression);

    // Implemented iterable in stdlib.
    CJC_NULLPTR_CHECK(fie.inExpression->GetTy());
    ModalTy iterableTy = GetIterableTy(typeManager, importManager, promotion, fie.inExpression->GetTy());
    ModalTy inPatternTy{TypeManager::GetInvalidTy()};
    if (Ty::IsTyCorrect(iterableTy)) {
        CJC_ASSERT(!iterableTy->typeArgs.empty());
        inPatternTy = {iterableTy->typeArgs[0].Ty(), iterableTy.Mode()};
    } else {
        isWellTyped = false;
        if (!CanSkipDiag(*fie.inExpression)) {
            diag.Diagnose(*fie.inExpression, DiagKind::sema_expr_in_forin_must_has_iterator,
                fie.inExpression->GetTy().String());
        }
    }

    isWellTyped = Check(ctx, inPatternTy, fie.pattern.get()) && isWellTyped;
    if (fie.patternGuard) {
        // PatternGuard's ty should be boolean.
        if (!Check(ctx, ModalTy{TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN)}, fie.patternGuard.get())) {
            isWellTyped = false;
            if (!CanSkipDiag(*fie.patternGuard)) {
                diag.Diagnose(*fie.patternGuard, DiagKind::sema_wrong_forin_guard);
            }
        }
    }

    isWellTyped = Synthesize({ctx, SynPos::UNUSED}, fie.body.get()).IsCorrect() && isWellTyped;
    if (!IsIrrefutablePattern(*fie.pattern)) {
        isWellTyped = false;
        diag.Diagnose(fie, DiagKind::sema_forin_pattern_must_be_irrefutable);
    }

    fie.SetTy(
        isWellTyped ? ModalTy{TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT)} : ModalTy{TypeManager::GetInvalidTy()});
    return fie.GetTy();
}
