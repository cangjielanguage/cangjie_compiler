// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "TypeCheckerImpl.h"

#include "Diags.h"
#include "LocalTypeArgumentSynthesis.h"
#include "TypeCheckUtil.h"

using namespace Cangjie;
using namespace Sema;
using namespace TypeCheckUtil;

bool TypeChecker::TypeCheckerImpl::CheckRangeElements(ASTContext& ctx, ModalTy elemTy, const RangeExpr& re)
{
    bool isWellTyped = !re.startExpr || Check(ctx, elemTy, re.startExpr.get());
    isWellTyped = (!re.stopExpr || Check(ctx, elemTy, re.stopExpr.get())) && isWellTyped;
    // When target type of rangeExpr is not given, the elemTy is type of start or stop.
    bool inconsistent = !ctx.HasTargetTy(&re) && re.startExpr && re.stopExpr && !isWellTyped;
    if (inconsistent) {
        if (!CanSkipDiag(*re.startExpr) && !CanSkipDiag(*re.stopExpr)) {
            diag.Diagnose(re, DiagKind::sema_inconsistency_range_elemType);
        }
    }
    if (isWellTyped && !CheckGenericDeclInstantiation(re.decl, std::vector<ModalTy>{elemTy}, re)) {
        isWellTyped = false;
    }
    if (re.stepExpr) {
        if (!Check(ctx, {TypeManager::GetPrimitiveTy(TypeKind::TYPE_INT64)}, re.stepExpr.get())) {
            if (!CanSkipDiag(*re.stepExpr)) {
                diag.Diagnose(re, DiagKind::sema_range_step_not_int64);
            }
            isWellTyped = false;
        } else if (re.stepExpr->isConst) {
            // Step expr cannot be zero.
            if (re.stepExpr->constNumValue.asInt.Uint64() == 0) {
                diag.Diagnose(*re.stepExpr, DiagKind::sema_step_non_zero_range);
            }
        }
    }
    return isWellTyped;
}

bool TypeChecker::TypeCheckerImpl::ChkRangeExpr(ASTContext& ctx, ModalTy target, RangeExpr& re)
{
    re.SetTy({TypeManager::GetInvalidTy()}); // Set type invalid first, will be updated if check passed.
    re.decl = importManager.GetCoreDecl<StructDecl>("Range");
    if (!re.decl) {
        diag.Diagnose(re, DiagKind::sema_no_core_object);
        return false;
    }
    if (!Ty::IsTyCorrect(target)) {
        return false;
    }

    auto synAndCheckRangeTy = [this, &ctx, &re, &target]() {
        re.SetTy(SynRangeExpr(ctx, re));
        bool isWellTyped = typeManager.IsSubtype(re.GetTy(), target);
        if (!isWellTyped && re.GetTy().IsCorrect()) {
            DiagMismatchedTypes(diag, re, target);
        }
        return isWellTyped;
    };

    ModalTy targetTy = TypeCheckUtil::UnboxOptionType(target);
    bool isWellTyped = Ty::IsTyCorrect(targetTy) && (targetTy->IsStruct() || targetTy->IsInterface());
    if (!isWellTyped) {
        (void)synAndCheckRangeTy();
        re.SetTy({TypeManager::GetInvalidTy()});
        return false;
    }

    auto rangeTy = targetTy;
    if (targetTy->IsInterface()) {
        auto candiTys = promotion.Downgrade(re.decl->GetTy(), target);
        if (candiTys.empty()) {
            // If range type cannot be inferred from target type, syn range expr.
            if (synAndCheckRangeTy()) {
                return true;
            }
            DiagUnableToInferExpr(diag, re);
            return false;
        }
        rangeTy = *candiTys.begin();
    } else if (!targetTy->IsRange() || rangeTy->typeArgs.size() != 1) {
        return synAndCheckRangeTy();
    }
    if (!CheckRangeElements(ctx, rangeTy->typeArgs[0], re)) {
        return false;
    }
    re.SetTy(rangeTy);
    isWellTyped = typeManager.IsSubtype(re.GetTy(), target);
    if (!isWellTyped) {
        DiagMismatchedTypes(diag, re, target);
    }
    return isWellTyped;
}

ModalTy TypeChecker::TypeCheckerImpl::SynRangeExpr(ASTContext& ctx, RangeExpr& re)
{
    re.SetTy({TypeManager::GetInvalidTy()}); // Set type invalid first, will be updated if check passed.
    re.decl = importManager.GetCoreDecl<StructDecl>("Range");
    if (!re.decl) {
        return re.GetTy();
    }
    ModalTy elemTy = SynRangeExprInferElemTy(re, ctx);
    if (Ty::IsTyCorrect(elemTy) && elemTy->IsIdeal() && elemTy->IsInteger()) {
        elemTy = {TypeManager::GetPrimitiveTy(TypeKind::TYPE_INT64)};
    }

    if (!CheckRangeElements(ctx, elemTy, re)) {
        return re.GetTy();
    }
    re.SetTy({typeManager.GetStructTy(*re.decl, {elemTy.Ty()})});
    return re.GetTy();
}

ModalTy TypeChecker::TypeCheckerImpl::SynRangeExprInferElemTy(const RangeExpr& re, ASTContext& ctx)
{
    // If type of startExpr is Nothing or startExpr is litconst when only one of start/stop is litconst,
    // using type of stopExpr or default Int64 type.
    ModalTy startTy =
        re.startExpr ? Synthesize({ctx, SynPos::EXPR_ARG}, re.startExpr.get()) : ModalTy{TypeManager::GetInvalidTy()};
    bool useStartLit = !Is<LitConstExpr>(re.startExpr.get()) || Is<LitConstExpr>(re.stopExpr.get());
    if (Ty::IsTyCorrect(startTy) && !startTy->IsNothing() && useStartLit) {
        return startTy;
    }
    ModalTy stopTy =
        re.stopExpr ? Synthesize({ctx, SynPos::EXPR_ARG}, re.stopExpr.get()) : ModalTy{TypeManager::GetInvalidTy()};
    if (Ty::IsTyCorrect(stopTy) && !stopTy->IsNothing()) {
        return stopTy;
    } else if (Ty::IsTyCorrect(startTy)) {
        return startTy;
    }
    // Element type is Int64 by default.
    return ModalTy{TypeManager::GetPrimitiveTy(TypeKind::TYPE_INT64)};
}
