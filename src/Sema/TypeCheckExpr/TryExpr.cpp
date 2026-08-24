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

ModalTy TypeChecker::TypeCheckerImpl::SynTryWithResourcesExpr(ASTContext& ctx, TryExpr& te)
{
    auto resourceDecl = importManager.GetCoreDecl("Resource");
    if (resourceDecl == nullptr) {
        te.SetTy({TypeManager::GetInvalidTy()});
        return te.GetTy();
    }
    ModalTy resourceTy = resourceDecl->GetTy();
    bool isWellTyped = true;
    for (auto& vd : te.resourceSpec) {
        CJC_NULLPTR_CHECK(vd);
        if (!SynthesizeAndReplaceIdealTy({ctx, SynPos::NONE}, *vd)) {
            isWellTyped = false;
            vd->SetTy({TypeManager::GetInvalidTy()}); // Avoid chaining errors.
            continue;
        }
        if (vd->GetTy()->IsNothing() || !typeManager.IsSubtype(vd->GetTy(), resourceTy)) {
            DiagMismatchedTypes(
                diag, *vd, resourceTy, "the resource specification should implement interface 'Resource'");
            vd->SetTy({TypeManager::GetInvalidTy()}); // Avoid chaining errors.
        }
    }
    CJC_NULLPTR_CHECK(te.tryBlock);
    isWellTyped = SynthesizeAndReplaceIdealTy({ctx, SynPos::EXPR_ARG}, *te.tryBlock) && isWellTyped;
    isWellTyped = ChkTryExprCatchPatterns(ctx, te) && isWellTyped;
    for (auto& catchBlock : te.catchBlocks) {
        CJC_NULLPTR_CHECK(catchBlock);
        isWellTyped = SynthesizeAndReplaceIdealTy({ctx, SynPos::EXPR_ARG}, *catchBlock) && isWellTyped;
    }
    isWellTyped = ChkTryExprFinallyBlock(ctx, te) && isWellTyped;
    if (isWellTyped) {
        te.SetTy({TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT)});
    } else {
        te.SetTy({TypeManager::GetInvalidTy()});
    }
    return te.GetTy();
}

ModalTy TypeChecker::TypeCheckerImpl::SynTryExpr(ASTContext& ctx, TryExpr& te)
{
    if (!te.resourceSpec.empty()) {
        return SynTryWithResourcesExpr(ctx, te);
    }

    bool isWellTyped;
    if (!te.handlers.empty() && te.tryLambda) {
        // For a try-handle expression, the try block has been replaced by a lambda,
        // but only if there were no syntax errors.
        isWellTyped = SynthesizeAndReplaceIdealTy({ctx, SynPos::EXPR_ARG}, *te.tryLambda);
    } else {
        CJC_NULLPTR_CHECK(te.tryBlock);
        isWellTyped = SynthesizeAndReplaceIdealTy({ctx, SynPos::EXPR_ARG}, *te.tryBlock);
    }

    auto optJTy = SynTryExprCatchesAndHandles(ctx, te);
    isWellTyped = optJTy.has_value() && isWellTyped;
    isWellTyped = ChkTryExprFinallyBlock(ctx, te) && isWellTyped;

    te.SetTy(isWellTyped ? optJTy.value() : ModalTy{TypeManager::GetInvalidTy()});
    return te.GetTy();
}

std::optional<ModalTy> TypeChecker::TypeCheckerImpl::SynTryExprCatchesAndHandles(ASTContext& ctx, TryExpr& te)
{
    CJC_NULLPTR_CHECK(te.tryBlock);
    ModalTy jTy = te.tryBlock->GetTy().IsCorrect() ? te.tryBlock->GetTy() : ModalTy{TypeManager::GetNothingTy()};
    if (te.tryLambda && te.tryLambda->GetTy().IsCorrect()) {
        jTy = DynamicCast<FuncTy>(te.tryLambda->DataTy())->retTy;
    }
    if ((te.catchPatterns.empty() || te.catchBlocks.empty()) && te.handlers.empty()) {
        return {jTy};
    }
    bool isWellTyped = ChkTryExprCatchPatterns(ctx, te) && ChkTryExprHandlePatterns(ctx, te);
    if (!te.handlers.empty()) {
        isWellTyped = isWellTyped && ValidateBlockInTryHandle(*te.tryBlock);
    }
    for (auto& catchBlock : te.catchBlocks) {
        if (!SynthesizeAndReplaceIdealTy({ctx, SynPos::EXPR_ARG}, *catchBlock) ||
            (!te.handlers.empty() && !ValidateBlockInTryHandle(*catchBlock))) {
            isWellTyped = false;
            continue;
        }
        auto joinRes = JoinAndMeet(
            typeManager, std::initializer_list<ModalTy>{jTy, catchBlock->GetTy()}, {}, &importManager, te.curFile)
                           .JoinAsVisibleTy();
        // Do not overwrite the previous jTy immediately for the sake of error reporting; pass current jTy (sink only).
        if (auto [optErrs, tmpJTy] = JoinAndMeet::SetJoinedType(jTy, joinRes); optErrs) {
            isWellTyped = false;
            if (te.ShouldDiagnose()) {
                diag.Diagnose(*catchBlock, DiagKind::sema_diag_report_error_message,
                    "The type of this catch block is '" + catchBlock->GetTy().String() +
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

std::optional<Ptr<ClassTy>> TypeChecker::TypeCheckerImpl::PromoteToCommandTy(const AST::Node& cause, AST::ModalTy cmdTy)
{
    // Check if the type of expression performed is derived from `core.Command<T>` interface
    // For class type and generic type.
    if (cmdTy->IsInvalid()) {
        // If the command type is erroneous, the error has already been reported before
        // calling this function, do not report again
        return std::nullopt;
    }
    auto cmdClassDecl = importManager.GetImportedDecl(EFFECT_PACKAGE_NAME, CLASS_COMMAND);
    if (!cmdClassDecl) {
        return std::nullopt;
    }
    auto promotedTys = promotion.Promote(cmdTy, cmdClassDecl->GetTy());
    if (promotedTys.size() != 1) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_command_incompatible_type, cause, cmdTy.String());
        return std::nullopt;
    }
    return StaticCast<ClassTy*>(promotedTys.begin()->get());
}

bool TypeChecker::TypeCheckerImpl::SynHandler(ASTContext& ctx, Handler& handler, ModalTy tgtTy, TryExpr& te)
{
    // We need to validate the handler before synthesizing since it's necessary for
    // resume expressions
    if (!ValidateHandler(handler)) {
        return false;
    }
    if (!SynthesizeAndReplaceIdealTy({ctx, SynPos::EXPR_ARG}, *handler.block)) {
        return false;
    }

    auto joinRes = JoinAndMeet(
        typeManager, std::initializer_list<ModalTy>{tgtTy, handler.block->GetTy()}, {}, &importManager, te.curFile)
                       .JoinAsVisibleTy();
    // Do not overwrite tgtTy on error; pass current tgtTy (sink only for now).
    auto [optErrs, tmpJTy] = JoinAndMeet::SetJoinedType(tgtTy, joinRes);
    if (optErrs) {
        if (te.ShouldDiagnose()) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_mismatching_handle_block, *handler.block,
                handler.block->GetTy().String(), tgtTy.String())
                .AddNote(te, DiagKind::sema_diag_report_note_message, *optErrs);
        }
        return false;
    }
    // Only overwrite tgtTy if the join operation succeeds.
    tgtTy = tmpJTy;
    return ChkHandler(ctx, handler, tgtTy);
}

bool TypeChecker::TypeCheckerImpl::ChkHandler(ASTContext& ctx, Handler& handler, ModalTy tgtTy)
{
    if (!handler.commandPattern || !handler.desugaredLambda) {
        // Parse error has already been reported.
        return false;
    }
    if (!ValidateHandler(handler)) {
        return false;
    }
    auto cmdTy = StaticAs<ASTKind::COMMAND_TYPE_PATTERN>(handler.commandPattern.get())->pattern->GetTy();
    if (cmdTy->IsInvalid()) {
        return false;
    }
    std::vector<ModalTy> args;
    args.emplace_back(cmdTy);
    ModalTy handleLambdaTy{typeManager.GetFunctionTy(args, tgtTy)};
    if (!Check(ctx, handleLambdaTy, handler.desugaredLambda)) {
        DiagMismatchedTypes(diag, *handler.desugaredLambda->funcBody->body, tgtTy);
        return false;
    }
    return true;
}

bool TypeChecker::TypeCheckerImpl::ChkTryExpr(ASTContext& ctx, ModalTy tgtTy, TryExpr& te)
{
    if (!te.resourceSpec.empty()) {
        auto ty = SynTryWithResourcesExpr(ctx, te);
        if (!Ty::IsTyCorrect(ty)) {
            return false;
        }
        if (!typeManager.IsSubtype(ty, tgtTy)) {
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
        auto tryLambdaTy = typeManager.GetFunctionTy({}, tgtTy);
        if (!te.tryLambda || !Check(ctx, DataTy{tryLambdaTy}, te.tryLambda)) {
            isWellTyped = false;
            if (!CanSkipDiag(*te.tryBlock) && !typeManager.IsSubtype(te.tryBlock->GetTy(), tgtTy)) {
                DiagMismatchedTypes(diag, *te.tryBlock, tgtTy);
            }
        }
    } else if (!Check(ctx, tgtTy, te.tryBlock.get())) {
        isWellTyped = false;
        if (!CanSkipDiag(*te.tryBlock) && !typeManager.IsSubtype(te.tryBlock->GetTy(), tgtTy)) {
            DiagMismatchedTypes(diag, *te.tryBlock, tgtTy);
        }
    }
    isWellTyped = ChkTryExprCatchesAndHandles(ctx, tgtTy, te) && isWellTyped;
    isWellTyped = ChkTryExprFinallyBlock(ctx, te) && isWellTyped;
    te.SetTy(isWellTyped ? tgtTy : ModalTy{TypeManager::GetInvalidTy()});
    return isWellTyped;
}

bool TypeChecker::TypeCheckerImpl::ChkTryExprCatchesAndHandles(ASTContext& ctx, ModalTy tgtTy, TryExpr& te)
{
    bool isWellTyped = ChkTryExprCatchPatterns(ctx, te) && ChkTryExprHandlePatterns(ctx, te);
    for (auto& catchBlock : te.catchBlocks) {
        if (Check(ctx, tgtTy, catchBlock.get())) {
            continue;
        }
        isWellTyped = false;
        if (!CanSkipDiag(*catchBlock) && !typeManager.IsSubtype(catchBlock->GetTy(), tgtTy)) {
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
        // value of finally is not used
        if (Synthesize({ctx, SynPos::UNUSED}, te.finallyBlock.get()).IsCorrect()) {
            ds.ReportDiag();
        } else {
            isWellTyped = false;
        }
    } else {
        isWellTyped = Synthesize({ctx, SynPos::UNUSED}, te.finallyBlock.get()).IsCorrect() && isWellTyped;
        if (!te.handlers.empty() && te.finallyLambda) {
            auto finallyLamTy = typeManager.GetFunctionTy({}, {TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT)});
            isWellTyped = Check(ctx, {static_cast<DataTy>(finallyLamTy)}, te.finallyLambda) && isWellTyped;
        }
    }
    te.finallyBlock->SetTy(
        isWellTyped ? ModalTy{TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT)} : ModalTy{TypeManager::GetInvalidTy()});
    return isWellTyped;
}

bool TypeChecker::TypeCheckerImpl::ChkTryExprCatchPatterns(ASTContext& ctx, TryExpr& te)
{
    std::vector<ModalTy> included{};
    auto exception = importManager.GetCoreDecl<ClassDecl>(CLASS_EXCEPTION);
    for (auto& pattern : te.catchPatterns) {
        CJC_NULLPTR_CHECK(pattern);
        if (pattern->astKind == ASTKind::WILDCARD_PATTERN) {
            if (exception == nullptr ||
                !ChkTryWildcardPattern(
                    exception->GetTy(), *StaticAs<ASTKind::WILDCARD_PATTERN>(pattern.get()), included)) {
                return false;
            }
            included.push_back(exception->GetTy());
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
    std::vector<ModalTy> included{};
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
