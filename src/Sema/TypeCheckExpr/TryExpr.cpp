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

    bool isWellTyped;
    if (!te.handlers.empty() && te.tryLambda) {
        // For a try-handle expression, the try block has been replaced by a lambda,
        // but only if there were no syntax errors.
        isWellTyped = SynthesizeAndReplaceIdealTy(ctx, *te.tryLambda);
    } else {
        CJC_NULLPTR_CHECK(te.tryBlock);
        isWellTyped = SynthesizeAndReplaceIdealTy(ctx, *te.tryBlock);
    }

    auto optJTy = SynTryExprCatchesAndHandles(ctx, te);
    isWellTyped = optJTy.has_value() && isWellTyped;
    isWellTyped = ChkTryExprFinallyBlock(ctx, te) && isWellTyped;

    te.ty = isWellTyped ? *optJTy : TypeManager::GetInvalidTy();
    return te.ty;
}

std::optional<Ptr<Ty>> TypeChecker::TypeCheckerImpl::SynTryExprCatchesAndHandles(ASTContext& ctx, TryExpr& te)
{
    CJC_NULLPTR_CHECK(te.tryBlock);
    Ptr<Ty> jTy = Ty::IsTyCorrect(te.tryBlock->ty) ? te.tryBlock->ty : TypeManager::GetNothingTy();
    if (te.tryLambda && Ty::IsTyCorrect(te.tryLambda->ty)) {
        jTy = DynamicCast<FuncTy*>(te.tryLambda->ty)->retTy;
    }
    if ((te.catchPatterns.empty() || te.catchBlocks.empty()) && te.handlers.empty()) {
        return {jTy};
    }
    bool isWellTyped = ChkTryExprCatchPatterns(ctx, te) && ChkTryExprHandlePatterns(ctx, te);
    if (!te.handlers.empty()) {
        isWellTyped = isWellTyped && ValidateBlockInTryHandle(*te.tryBlock);
    }
    for (auto& catchBlock : te.catchBlocks) {
        if (!SynthesizeAndReplaceIdealTy(ctx, *catchBlock) ||
            (!te.handlers.empty() && !ValidateBlockInTryHandle(*catchBlock))) {
            isWellTyped = false;
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

    for (auto& handler : te.handlers) {
        if (!SynHandler(ctx, handler, jTy, te)) {
            isWellTyped = false;
        }
    }
    return isWellTyped ? std::make_optional(jTy) : std::nullopt;
}

std::optional<Ptr<ClassTy>> TypeChecker::TypeCheckerImpl::PromoteToCommandTy(const AST::Node& cause, AST::Ty& cmdTy)
{
    // Check if the type of expression performed is derived from `core.Command<T>` interface
    // For class type and generic type.
    if (cmdTy.IsInvalid()) {
        // If the command type is erroneous, the error has already been reported before
        // calling this function, do not report again
        return std::nullopt;
    }
    auto cmdClassDecl = importManager.GetImportedDecl(EFFECT_PACKAGE_NAME, CLASS_COMMAND);
    if (!cmdClassDecl) {
        return std::nullopt;
    }
    auto promotedTys = promotion.Promote(cmdTy, *cmdClassDecl->ty);
    if (promotedTys.size() != 1) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_command_incompatible_type, cause, Ty::ToString(&cmdTy));
        return std::nullopt;
    }
    return StaticCast<ClassTy*>(*promotedTys.begin());
}

bool TypeChecker::TypeCheckerImpl::SynHandler(ASTContext& ctx, Handler& handler, Ptr<Ty> tgtTy, TryExpr& te)
{
    // We need to validate the handler before synthesizing since it's necessary for
    // resume expressions
    if (!ValidateHandler(handler)) {
        return false;
    }
    if (!SynthesizeAndReplaceIdealTy(ctx, *handler.block)) {
        return false;
    }

    auto joinRes = JoinAndMeet(
        typeManager, std::initializer_list<Ptr<Ty>>{tgtTy, handler.block->ty}, {}, &importManager, te.curFile)
                       .JoinAsVisibleTy();
    // Do not overwrite the previous jTy immediately for the sake of error reporting. Use a fresh type here.
    Ptr<Ty> tmpJTy{};
    if (auto optErrs = JoinAndMeet::SetJoinedType(tmpJTy, joinRes)) {
        if (te.ShouldDiagnose()) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_mismatching_handle_block, *handler.block,
                Ty::ToString(handler.block->ty), tgtTy->String())
                .AddNote(te, DiagKind::sema_diag_report_note_message, *optErrs);
        }
        return false;
    } else {
        // Only overwrite jTy if the join operation succeeds.
        tgtTy = tmpJTy;
    }
    return ChkHandler(ctx, handler, *tgtTy);
}

bool TypeChecker::TypeCheckerImpl::ChkHandler(ASTContext& ctx, Handler& handler, Ty& tgtTy)
{
    if (!handler.commandPattern || !handler.desugaredLambda) {
        // Parse error has already been reported.
        return false;
    }
    if (!ValidateHandler(handler)) {
        return false;
    }
    auto cmdTy = StaticAs<ASTKind::COMMAND_TYPE_PATTERN>(handler.commandPattern.get())->pattern->ty;
    if (cmdTy->IsInvalid()) {
        return false;
    }
    std::vector<Ptr<Ty>> args;
    args.emplace_back(cmdTy);
    Ptr<Ty> handleLambdaTy = typeManager.GetFunctionTy(args, &tgtTy);
    if (!Check(ctx, handleLambdaTy, handler.desugaredLambda)) {
        DiagMismatchedTypes(diag, *handler.desugaredLambda->funcBody->body, tgtTy);
        return false;
    }
    return true;
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
    if (!te.handlers.empty()) {
        // Careful: if there are handlers, then the body of the try is empty because we turned
        // it into a lambda during parsing
        auto tryLambdaTy = typeManager.GetFunctionTy({}, &tgtTy);
        if (!te.tryLambda || !Check(ctx, tryLambdaTy, te.tryLambda)) {
            isWellTyped = false;
            if (!CanSkipDiag(*te.tryBlock) && !typeManager.IsSubtype(te.tryBlock->ty, &tgtTy)) {
                DiagMismatchedTypes(diag, *te.tryBlock, tgtTy);
            }
        }
    } else if (!Check(ctx, &tgtTy, te.tryBlock.get())) {
        isWellTyped = false;
        if (!CanSkipDiag(*te.tryBlock) && !typeManager.IsSubtype(te.tryBlock->ty, &tgtTy)) {
            DiagMismatchedTypes(diag, *te.tryBlock, tgtTy);
        }
    }
    isWellTyped = ChkTryExprCatchesAndHandles(ctx, tgtTy, te) && isWellTyped;
    isWellTyped = ChkTryExprFinallyBlock(ctx, te) && isWellTyped;
    te.ty = isWellTyped ? &tgtTy : TypeManager::GetInvalidTy();
    return isWellTyped;
}

bool TypeChecker::TypeCheckerImpl::ChkTryExprCatchesAndHandles(ASTContext& ctx, Ty& tgtTy, TryExpr& te)
{
    bool isWellTyped = ChkTryExprCatchPatterns(ctx, te) && ChkTryExprHandlePatterns(ctx, te);
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
    for (auto& handler : te.handlers) {
        if (!ChkHandler(ctx, handler, tgtTy)) {
            isWellTyped = false;
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
        if (!te.handlers.empty() && te.finallyLambda) {
            auto finallyLamTy = typeManager.GetFunctionTy({}, TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT));
            isWellTyped = Check(ctx, finallyLamTy, te.finallyLambda) && isWellTyped;
        }
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

bool TypeChecker::TypeCheckerImpl::ChkTryExprHandlePatterns(ASTContext& ctx, TryExpr& te)
{
    std::vector<Ptr<Ty>> included{};
    for (auto& handler : te.handlers) {
        if (handler.commandPattern.get()->astKind != ASTKind::COMMAND_TYPE_PATTERN ||
            !handler.desugaredLambda) {
            // Parse error has already been reported.
            continue;
        }
        if (!ChkHandlePatterns(ctx, handler, included)) {
            return false;
        }
    }
    return true;
}

bool TypeChecker::TypeCheckerImpl::ValidateHandler(Handler& h)
{
    bool valid = ValidateBlockInTryHandle(*h.block);
    std::vector<Ptr<Node>> scopeChanges;
    if (valid) {
        // If the handler is immediate, then we need to walk the body of the handle blocks
        // and bind any implicit `resume` to it.
        if (!h.desugaredLambda) {
            // The AST was invalid, we do not bother to check (an error was already reported)
            return false;
        }
        std::function<VisitAction(Ptr<Node>)> bind = [&h](Ptr<Node> node) -> VisitAction {
            switch (node->astKind) {
                case ASTKind::RESUME_EXPR: {
                    auto re = StaticAs<ASTKind::RESUME_EXPR>(node);
                    re->enclosing = &h;
                    return VisitAction::WALK_CHILDREN;
                }
                case ASTKind::FUNC_BODY:
                    return VisitAction::SKIP_CHILDREN;
                default:
                    return VisitAction::WALK_CHILDREN;
            }
        };
        // It's important that we walk the body of the desugared lambda, since we're
        // binding the resumeExpr nodes contained within
        Walker(h.desugaredLambda->funcBody->body, bind).Walk();
        // This is redundant but we need to bind the `resume`s inside the block so that
        // we can typecheck it
        Walker(h.block, bind).Walk();
    }
    return valid;
}

bool TypeChecker::TypeCheckerImpl::ValidateBlockInTryHandle(Block& block)
{
    bool hasReturns = false;
    const auto& findReturns = [this, &hasReturns](Ptr<Node> node) -> VisitAction {
        switch (node->astKind) {
            case ASTKind::FUNC_BODY:
                return VisitAction::SKIP_CHILDREN;
            case ASTKind::TRY_EXPR:
                if (StaticAs<ASTKind::TRY_EXPR>(node)->handlers.empty()) {
                    return VisitAction::WALK_CHILDREN;
                } else {
                    return VisitAction::SKIP_CHILDREN;
                }
            case ASTKind::RETURN_EXPR:
                diag.DiagnoseRefactor(DiagKindRefactor::sema_return_in_try_handle_block, *node);
                hasReturns = true;
                return VisitAction::STOP_NOW;
            default:
                return VisitAction::WALK_CHILDREN;
        }
    };
    Walker(&block, findReturns).Walk();
    return !hasReturns;
}
