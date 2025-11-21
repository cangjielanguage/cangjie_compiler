// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "TypeCheckerImpl.h"

#include "DiagSuppressor.h"
#include "Diags.h"
#include "JoinAndMeet.h"
#include "TypeCheckUtil.h"

#include "cangjie/AST/Match.h"

using namespace Cangjie;
using namespace Sema;
using namespace TypeCheckUtil;

Ptr<Ty> TypeChecker::TypeCheckerImpl::SynTryWithResourcesExpr(ASTContext& ctx, TryExpr& te)
{
    auto resourceDecl = importManager.GetCoreDecl("Resource");
    if (resourceDecl == nullptr) {
        te.ty = TypeManager::GetInvalidTy();
        return te.ty;
    }
    Ptr<Ty> resourceTy = resourceDecl->ty;
    bool isWellTyped = true;
    for (auto& vd : te.resourceSpec) {
        CJC_NULLPTR_CHECK(vd);
        if (!SynthesizeAndReplaceIdealTy(ctx, *vd)) {
            isWellTyped = false;
            vd->ty = TypeManager::GetInvalidTy(); // Avoid chaining errors.
            continue;
        }
        if (vd->ty->IsNothing() || !typeManager.IsSubtype(vd->ty, resourceTy)) {
            DiagMismatchedTypes(
                diag, *vd, *resourceTy, "the resource specification should implement interface 'Resource'");
            vd->ty = TypeManager::GetInvalidTy(); // Avoid chaining errors.
        }
    }
    CJC_NULLPTR_CHECK(te.tryBlock);
    isWellTyped = SynthesizeAndReplaceIdealTy(ctx, *te.tryBlock) && isWellTyped;
    isWellTyped = ChkTryExprCatchPatterns(ctx, te) && isWellTyped;
    for (auto& catchBlock : te.catchBlocks) {
        CJC_NULLPTR_CHECK(catchBlock);
        isWellTyped = SynthesizeAndReplaceIdealTy(ctx, *catchBlock) && isWellTyped;
    }
    isWellTyped = ChkTryExprFinallyBlock(ctx, te) && isWellTyped;
    if (isWellTyped) {
        te.ty = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
    } else {
        te.ty = TypeManager::GetInvalidTy();
    }
    return te.ty;
}

Ptr<Ty> TypeChecker::TypeCheckerImpl::SynTryExpr(ASTContext& ctx, TryExpr& te)
{
    if (!te.resourceSpec.empty()) {
        return SynTryWithResourcesExpr(ctx, te);
    }

    CJC_NULLPTR_CHECK(te.tryBlock);
    bool isWellTyped = SynthesizeAndReplaceIdealTy(ctx, *te.tryBlock);
    auto optJTy = SynTryExprCatches(ctx, te);

    isWellTyped = optJTy.has_value() && isWellTyped;
    isWellTyped = ChkTryExprFinallyBlock(ctx, te) && isWellTyped;

    te.ty = isWellTyped ? *optJTy : TypeManager::GetInvalidTy();
    return te.ty;
}

std::optional<Ptr<Ty>> TypeChecker::TypeCheckerImpl::SynTryExprCatches(ASTContext& ctx, TryExpr& te)
{
    CJC_NULLPTR_CHECK(te.tryBlock);
    Ptr<Ty> jTy = Ty::IsTyCorrect(te.tryBlock->ty) ? te.tryBlock->ty : TypeManager::GetNothingTy();
    if (te.catchPatterns.empty() || te.catchBlocks.empty()) {
        return {jTy};
    }
    bool isWellTyped = ChkTryExprCatchPatterns(ctx, te);
    for (auto& catchBlock : te.catchBlocks) {
        bool isTypeCorrect = SynthesizeAndReplaceIdealTy(ctx, *catchBlock);
        isWellTyped = isWellTyped && isTypeCorrect;
        if (!isTypeCorrect) {
            continue;
        }
        auto joinRes = JoinAndMeet(
            typeManager, std::initializer_list<Ptr<Ty>>{jTy, catchBlock->ty}, {}, &importManager, te.curFile)
                           .JoinAsVisibleTy();
        // Do not overwrite the previous jTy immediately for the sake of error reporting. Use a fresh type here.
        Ptr<Ty> tmpJTy{};
        if (auto optErrs = JoinAndMeet::SetJoinedType(tmpJTy, joinRes)) {
            isWellTyped = false;
            if (te.ShouldDiagnose()) {
                diag.Diagnose(*catchBlock, DiagKind::sema_diag_report_error_message,
                              "The type of this catch block is '" + Ty::ToString(catchBlock->ty) +
                              "', which mismatches the smallest common supertype '" + jTy->String() +
                              "' of previous branches.")
                    .AddNote(te, DiagKind::sema_diag_report_note_message, *optErrs);
            }
        } else {
            // Only overwrite jTy if the join operation succeeds.
            jTy = tmpJTy;
        }
    }
    return isWellTyped ? std::make_optional(jTy) : std::nullopt;
}

bool TypeChecker::TypeCheckerImpl::ChkTryExpr(ASTContext& ctx, Ty& tgtTy, TryExpr& te)
{
    if (!te.resourceSpec.empty()) {
        auto ty = SynTryWithResourcesExpr(ctx, te);
        if (!Ty::IsTyCorrect(ty)) {
            return false;
        }
        if (!typeManager.IsSubtype(ty, &tgtTy)) {
            DiagMismatchedTypes(diag, te, tgtTy, "try-with-resources expressions are of type 'Unit'");
            return false;
        }
        return true;
    }
    CJC_NULLPTR_CHECK(te.tryBlock);
    bool isWellTyped = true;
    if (!Check(ctx, &tgtTy, te.tryBlock.get())) {
        isWellTyped = false;
        if (!CanSkipDiag(*te.tryBlock) && !typeManager.IsSubtype(te.tryBlock->ty, &tgtTy)) {
            DiagMismatchedTypes(diag, *te.tryBlock, tgtTy);
        }
    }
    if (!te.catchPatterns.empty() && !te.catchBlocks.empty()) {
        isWellTyped = ChkTryExprCatches(ctx, tgtTy, te) && isWellTyped;
    }
    isWellTyped = ChkTryExprFinallyBlock(ctx, te) && isWellTyped;
    te.ty = isWellTyped ? &tgtTy : TypeManager::GetInvalidTy();
    return isWellTyped;
}

bool TypeChecker::TypeCheckerImpl::ChkTryExprCatches(ASTContext& ctx, Ty& tgtTy, TryExpr& te)
{
    bool isWellTyped = ChkTryExprCatchPatterns(ctx, te);
    for (auto& catchBlock : te.catchBlocks) {
        if (Check(ctx, &tgtTy, catchBlock.get())) {
            continue;
        }
        isWellTyped = false;
        if (!CanSkipDiag(*catchBlock) && !typeManager.IsSubtype(catchBlock->ty, &tgtTy)) {
            DiagMismatchedTypes(diag, *catchBlock, tgtTy);
            // Do not return immediately. Report errors for each case.
        }
    }
    return isWellTyped;
}

bool TypeChecker::TypeCheckerImpl::ChkTryExprFinallyBlock(ASTContext& ctx, const TryExpr& te)
{
    if (!te.finallyBlock) {
        return true;
    }
    bool isWellTyped = true;
    if (te.isDesugaredFromSyncBlock) {
        // Suppress errors raised from the desugared mutex.unlock(), which should not be reported anyway.
        auto ds = DiagSuppressor(diag);
        if (Ty::IsTyCorrect(Synthesize(ctx, te.finallyBlock.get()))) {
            ds.ReportDiag();
        } else {
            isWellTyped = false;
        }
    } else {
        isWellTyped = Ty::IsTyCorrect(Synthesize(ctx, te.finallyBlock.get())) && isWellTyped;
    }
    te.finallyBlock->ty =
        isWellTyped ? StaticCast<Ty*>(TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT)) : TypeManager::GetInvalidTy();
    return isWellTyped;
}

bool TypeChecker::TypeCheckerImpl::ChkTryExprCatchPatterns(ASTContext& ctx, TryExpr& te)
{
    std::vector<Ptr<Ty>> included{};
    auto exception = importManager.GetCoreDecl<ClassDecl>(CLASS_EXCEPTION);
    for (auto& pattern : te.catchPatterns) {
        CJC_NULLPTR_CHECK(pattern);
        if (pattern->astKind == ASTKind::WILDCARD_PATTERN) {
            if (exception == nullptr ||
                !ChkTryWildcardPattern(exception->ty, *StaticAs<ASTKind::WILDCARD_PATTERN>(pattern.get()), included)) {
                return false;
            }
            included.push_back(exception->ty);
        } else if (pattern->astKind == ASTKind::EXCEPT_TYPE_PATTERN) {
            if (!ChkExceptTypePattern(ctx, *StaticAs<ASTKind::EXCEPT_TYPE_PATTERN>(pattern.get()), included)) {
                return false;
            }
        } else {
            return false;
        }
    }
    return true;
}