// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "TypeCheckerImpl.h"

#include "Desugar/DesugarInTypeCheck.h"
#include "DiagSuppressor.h"
#include "Diags.h"
#include "JoinAndMeet.h"
#include "TypeCheckUtil.h"

#include "cangjie/AST/Match.h"
#include "cangjie/AST/RecoverDesugar.h"

using namespace Cangjie;
using namespace Sema;
using namespace TypeCheckUtil;

bool TypeChecker::TypeCheckerImpl::ChkSubscriptExpr(ASTContext& ctx, ModalTy target, SubscriptExpr& se)
{
    if (se.desugarExpr) {
        return typeManager.IsSubtype(se.desugarExpr->GetTy(), target);
    }
    se.SetTy({TypeManager::GetInvalidTy()}); // Set invalid ty at first, will be updated later.
    bool invalid = !se.baseExpr || se.indexExprs.empty();
    if (invalid) {
        return false;
    }
    SetIsNotAlone(*se.baseExpr);
    ModalTy baseTy = Synthesize({ctx, SynPos::EXPR_ARG}, se.baseExpr.get());
    std::vector<ModalTy> indexTys{};
    for (auto& expr : se.indexExprs) {
        indexTys.push_back(Synthesize({ctx, SynPos::EXPR_ARG}, expr.get()));
    }
    if (!Ty::IsTyCorrect(baseTy) || !Ty::AreTysCorrect(indexTys)) {
        return false;
    }
    // NOTE: Tuple and VArray type support built-in 'SubscriptExpr', others are all operator overload.
    if (auto tupleTy = DynamicCast<TupleTy>(baseTy.Ty()); tupleTy && se.indexExprs.size() == 1) {
        se.isTupleAccess = true;
        return ChkTupleAccess(ctx, target, se, *tupleTy, baseTy.Mode());
    }
    if (auto varrTy = DynamicCast<VArrayTy>(baseTy.Ty()); varrTy) {
        return ChkVArrayAccess(ctx, target, se, *varrTy, baseTy.Mode());
    }
    auto ds = DiagSuppressor(diag);
    DesugarOperatorOverloadExpr(ctx, se); // Desugar to callExpr.
    // The type of baseExpr should not be inferred here!
    bool isWellTyped = !target ? Synthesize({ctx, SynPos::EXPR_ARG}, se.desugarExpr.get()).IsCorrect()
                               : Check(ctx, target, se.desugarExpr.get());
    if (isWellTyped) {
        ds.ReportDiag();
        se.SetTy(se.desugarExpr->GetTy());
        CJC_ASSERT(se.desugarExpr->astKind == ASTKind::CALL_EXPR);
        auto desugaredCE = StaticAs<ASTKind::CALL_EXPR>(se.desugarExpr.get());
        CJC_ASSERT(desugaredCE->resolvedFunction != nullptr);
        ReplaceTarget(&se, desugaredCE->resolvedFunction);
        return true;
    }
    auto synTy = Synthesize({ctx, SynPos::EXPR_ARG}, se.desugarExpr.get());
    bool retTyMismatch = Ty::IsTyCorrect(target) && Ty::IsTyCorrect(synTy);
    RecoverToSubscriptExpr(se);
    // Also recover base and index's type.
    baseTy = typeManager.ReplaceIdealTy(std::move(baseTy));
    se.baseExpr->SetTy(se.baseExpr->GetTy() ? se.baseExpr->GetTy() : baseTy);
    for (size_t i = 0; i < se.indexExprs.size(); ++i) {
        indexTys[i] = typeManager.ReplaceIdealTy(indexTys[i]);
        se.indexExprs[i]->SetTy(se.indexExprs[i]->GetTy().IsCorrect() ? se.indexExprs[i]->GetTy() : indexTys[i]);
    }
    if (!ds.HasError() && se.ShouldDiagnose(true)) { // Only report subscript diagnoses if no error has beed reported.
        ds.ReportDiag(); // Report warnings.
        if (retTyMismatch) {
            CJC_NULLPTR_CHECK(target);
            DiagMismatchedTypesWithFoundTy(diag, se, target, synTy);
        } else {
            DiagInvalidSubscriptExpr(diag, se, baseTy, indexTys);
        }
    }
    ds.ReportDiag();
    return false;
}

bool TypeChecker::TypeCheckerImpl::ChkTupleAccess(
    ASTContext& ctx, ModalTy target, SubscriptExpr& se, TupleTy& tupleTy, ModalInfo modal)
{
    if (se.baseExpr == nullptr || se.indexExprs.size() != 1) {
        return false;
    }
    if (se.indexExprs[0]->astKind != ASTKind::LIT_CONST_EXPR) {
        diag.Diagnose(*se.indexExprs[0], DiagKind::sema_builtin_invalid_index, "tuple");
        return false;
    }
    if (!Check(ctx, {TypeManager::GetPrimitiveTy(TypeKind::TYPE_INT64)}, se.indexExprs[0].get())) {
        diag.Diagnose(*se.indexExprs[0], DiagKind::sema_builtin_invalid_index, "tuple");
        return false;
    }
    auto idxExpr = StaticAs<ASTKind::LIT_CONST_EXPR>(se.indexExprs[0].get());
    uint64_t index = idxExpr->constNumValue.asInt.Uint64();
    if (index >= tupleTy.typeArgs.size()) {
        if (se.ShouldDiagnose(true)) {
            diag.Diagnose(*se.indexExprs[0], DiagKind::sema_builtin_index_in_bound, "tuple");
        }
        return false;
    }
    if (!target) { // Type inferring.
        se.SetTy({tupleTy.TyArg(index), modal});
        return true;
    }
    if (auto tl = AST::As<ASTKind::TUPLE_LIT>(se.baseExpr.get()); tl) {
        CJC_ASSERT(index < tl->children.size());
        // Update tuple ty if baseExpr is a TupleLit whenever check succeed or failed,
        // because the check result will affect child expr of tuple lit which
        // may changing ideal ty to exact ty or changing valid ty to invalid ty.
        if (!Check(ctx, target, tl->children[index].get())) {
            // Reset the ty of tuple lit to allow re-synthesize of the tuple lit.
            tl->SetTy({TypeManager::GetInvalidTy()});
            return false;
        }
        std::vector<DataTy> elemTy;
        std::set<ModalTy> elemModalTys;
        for (auto& it : tl->children) {
            if (it != nullptr && ReplaceIdealTy(*it)) {
                elemTy.emplace_back(it->DataTy());
                elemModalTys.insert(it->GetTy());
            } else {
                elemTy.emplace_back(TypeManager::GetInvalidTy());
            }
        }
        auto commonMode = JoinAndMeet::JoinMode(typeManager, elemModalTys);
        tl->SetTy({typeManager.GetTupleTy(elemTy), commonMode});
        se.SetTy({elemTy[index], commonMode});
    } else {
        ModalTy elemTy{tupleTy.TyArg(index), modal};
        if (!typeManager.IsSubtype(elemTy, target)) {
            DiagMismatchedTypesWithFoundTy(diag, se, target.String(), elemTy.String());
            return false;
        }
        se.SetTy(elemTy);
    }
    return true;
}

bool TypeChecker::TypeCheckerImpl::ChkVArrayAccess(
    ASTContext& ctx, ModalTy target, SubscriptExpr& se, VArrayTy& varrTy, ModalInfo modal)
{
    if (se.indexExprs.size() != 1) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_varray_subscript_num, se);
        return false;
    }
    {
        DiagSuppressor ds(diag); // only for examing missing error report
        ModalTy i64{TypeManager::GetPrimitiveTy(TypeKind::TYPE_INT64)};
        auto idxExpr = se.indexExprs[0].get();
        if (!Check(ctx, i64, idxExpr)) {
            if (!ds.HasError()) {
                DiagMismatchedTypesWithFoundTy(diag, *idxExpr, i64.String(), idxExpr->GetTy().String());
            }
            ds.ReportDiag();
            return false;
        }
        ds.ReportDiag();
    }
    CJC_ASSERT(!varrTy.typeArgs.empty() && varrTy.typeArgs[0]);
    ModalTy elemTy{varrTy.TyArg(0), modal};
    if (!target) { // Type inferring.
        se.SetTy(elemTy);
        return true;
    }
    // check subvalue type.
    if (!typeManager.IsSubtype(elemTy, target)) {
        DiagMismatchedTypesWithFoundTy(diag, se, target.String(), elemTy.String());
        return false;
    }
    se.SetTy(elemTy);
    return true;
}

ModalTy TypeChecker::TypeCheckerImpl::SynSubscriptExpr(ASTContext& ctx, SubscriptExpr& se)
{
    ChkSubscriptExpr(ctx, {}, se);
    return se.GetTy();
}
