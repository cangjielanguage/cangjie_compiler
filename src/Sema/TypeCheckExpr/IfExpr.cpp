// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "TypeCheckerImpl.h"

#include "Diags.h"
#include "JoinAndMeet.h"
#include "TypeCheckUtil.h"

#include "cangjie/AST/Match.h"

using namespace Cangjie;
using namespace Sema;
using namespace TypeCheckUtil;

namespace {
bool IsIfExprWithoutElse(const IfExpr& ie)
{
    if (!ie.elseBody) {
        return true;
    } else if (auto elseIfBody = As<ASTKind::IF_EXPR>(ie.elseBody.get())) {
        return IsIfExprWithoutElse(*elseIfBody);
    }
    return false;
}
} // namespace

// Syntax : if t1 then t2 else t3.
ModalTy TypeChecker::TypeCheckerImpl::SynIfExpr(const CheckerContext& ctx, IfExpr& ie)
{
    SynPos pos = ctx.SynthPos();
    CJC_NULLPTR_CHECK(ie.condExpr);
    bool isWellTyped = CheckCondition(ctx.Ctx(), *ie.condExpr, false);
    isWellTyped = ie.thenBody && Synthesize(ctx.With(SynPos::EXPR_ARG), ie.thenBody.get()).IsCorrect() && isWellTyped;

    if (IsIfExprWithoutElse(ie)) {
        // For the case that if-elseif without ending 'else' branch.
        isWellTyped =
            (!ie.elseBody || Synthesize(ctx.With(SynPos::EXPR_ARG), ie.elseBody.get()).IsCorrect()) && isWellTyped;
        ie.SetTy(isWellTyped ? ModalTy{TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT)}
            : ModalTy{TypeManager::GetInvalidTy()});
        return ie.GetTy();
    }

    isWellTyped = ie.elseBody && Synthesize(ctx.With(SynPos::EXPR_ARG), ie.elseBody.get()).IsCorrect() && isWellTyped;
    if (!isWellTyped) {
        ie.SetTy({TypeManager::GetInvalidTy()});
        return {TypeManager::GetInvalidTy()};
    }

    // Keep ideal literal types pending for the then/else branches of an if expression used as
    // the body of a generic-call lambda argument, so the result stays IDEAL and the generic type
    // argument can be unified against the expected type (e.g. `TypeTest({ => if (c) { 0 } else { 1 } })`
    // expecting `() -> Int32`).
    if (ctx.Ctx().inFuncArgLambdaBody == 0) {
        ReplaceIdealTy(*ie.thenBody);
        ReplaceIdealTy(*ie.elseBody);
    }
    ie.thenBody->SetTy(typeManager.GetThisRealTy(ie.thenBody->GetTy()));
    ie.elseBody->SetTy(typeManager.GetThisRealTy(ie.elseBody->GetTy()));
    // when an if expr is unused, its type is set to unit if possible
    if (pos == SynPos::UNUSED) {
        ie.SetTy({TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT)});
        return ie.GetTy();
    }
    auto thenTy = ie.thenBody->GetTy();
    auto elseTy = ie.elseBody->GetTy();
    if (Ty::IsTyCorrect(thenTy) && Ty::IsTyCorrect(elseTy)) {
        auto joinRes = JoinAndMeet(typeManager, {thenTy, elseTy}, {}, &importManager, ie.curFile).JoinAsVisibleTy();
        auto [optErrs, joinedIfTy] = JoinAndMeet::SetJoinedType(ie.GetTy(), joinRes);
        ie.SetTy(joinedIfTy);
        if (optErrs) {
            std::string errMsg = "types " + thenTy.String() + " and " + elseTy.String();
            errMsg = ie.sourceExpr && ie.sourceExpr->astKind == ASTKind::IF_AVAILABLE_EXPR
                ? errMsg + " of the two lambda of this '@IfAvailable' expression mismatch"
                : errMsg + " of the two branches of this 'if' expression mismatch";
            diag.Diagnose(ie, DiagKind::sema_diag_report_error_message, errMsg).AddNote(*optErrs);
        }
    }
    return ie.GetTy();
}

bool TypeChecker::TypeCheckerImpl::ChkIfExpr(ASTContext& ctx, ModalTy tgtTy, IfExpr& ie)
{
    CJC_NULLPTR_CHECK(ie.condExpr);
    CJC_NULLPTR_CHECK(ie.thenBody);

    bool isWellTyped = CheckCondition(ctx, *ie.condExpr, false);

    if (IsIfExprWithoutElse(ie)) {
        isWellTyped = ChkIfExprNoElse(ctx, tgtTy, ie) && isWellTyped;
    } else {
        isWellTyped = ChkIfExprTwoBranches(ctx, tgtTy, ie) && isWellTyped;
    }

    if (!isWellTyped) {
        ie.SetTy({TypeManager::GetInvalidTy()});
    }
    return isWellTyped;
}

static std::vector<Ptr<Node>> CollectVariables(const ASTContext& ctx, Node& n)
{
    std::vector<Ptr<Node>> varPatterns;
    Walker(&n, [&varPatterns, &ctx](Ptr<Node> node) {
        CJC_NULLPTR_CHECK(node);
        bool isVarPattern = node->astKind == ASTKind::VAR_PATTERN ||
            (node->astKind == ASTKind::VAR_OR_ENUM_PATTERN &&
                !ctx.IsEnumConstructor(StaticCast<VarOrEnumPattern&>(*node).identifier));
        if (isVarPattern) {
            varPatterns.emplace_back(node);
            return VisitAction::SKIP_CHILDREN;
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    return varPatterns;
}

namespace {
// any binding introduced by the pattern shouldn't be seen by the destructed expr
// propagate this info for later lookup
void PropagateCtxExpr(Pattern& pat, Expr& initializer)
{
    Walker(&pat, [&initializer](Ptr<Node> n) {
        if (auto vp = DynamicCast<VarPattern>(n)) {
            vp->ctxExpr = &initializer;
        }
        if (!Is<Pattern>(n)) {
            return VisitAction::SKIP_CHILDREN;
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
}
}

bool TypeChecker::TypeCheckerImpl::SynLetPatternDestructor(
    ASTContext& ctx, LetPatternDestructor& lpd, bool suppressIntroducingVariableError)
{
    CJC_NULLPTR_CHECK(lpd.initializer);
    ModalTy boolTy{TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN)};
    for (auto& p : lpd.patterns) {
        p->ctxExpr = lpd.initializer.get();
        PropagateCtxExpr(*p, *p->ctxExpr);
    }
    Synthesize({ctx, SynPos::EXPR_ARG}, lpd.initializer.get());
    ReplaceIdealTy(*lpd.initializer);
    auto selectorTy = lpd.initializer->GetTy();
    // cannot have multiple pattern in one let with different astKind, e.g. let A|_ <- xxx
    // in this case, no need to check further
    if (!ChkPatternsSameASTKind(ctx, lpd.patterns)) {
        lpd.SetTy({TypeManager::GetInvalidTy()});
        return false;
    }
    // intended shortcut &&: no need to check var pattern if it has already been reported by parent AST
    bool good = !suppressIntroducingVariableError && ChkNoVarPatternInOrPattern(ctx, lpd.patterns);
    bool subpatternsGood{true};
    for (auto& p : lpd.patterns) {
        subpatternsGood = ChkPattern(ctx, selectorTy, *p, true, lpd.initializer.get()) && subpatternsGood;
    }
    if (Ty::IsTyCorrect(selectorTy) && subpatternsGood) {
        lpd.SetTy(boolTy);
        return good;
    } else {
        lpd.SetTy({TypeManager::GetInvalidTy()});
        return false;
    }
}

bool TypeChecker::TypeCheckerImpl::CheckCondition(ASTContext& ctx, Expr& e, bool suppressIntroducingVariableError)
{
    if (e.astKind == ASTKind::LET_PATTERN_DESTRUCTOR) {
        return SynLetPatternDestructor(ctx, StaticCast<LetPatternDestructor&>(e), suppressIntroducingVariableError);
    }
    if (auto bin = DynamicCast<BinaryExpr>(&e)) {
        return CheckBinaryCondition(ctx, *bin, suppressIntroducingVariableError);
    }
    if (auto paren = DynamicCast<ParenExpr>(&e)) {
        bool res = CheckCondition(ctx, *paren->expr, suppressIntroducingVariableError);
        paren->SetTy(paren->expr->GetTy());
        return res;
    }

    ModalTy boolTy{TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN)};
    if (Check(ctx, boolTy, &e)) {
        return true;
    }

    auto shouldDiag = e.ShouldDiagnose() && !CanSkipDiag(e);
    if (shouldDiag) {
        DiagMismatchedTypes(diag, e, boolTy);
    }
    return false;
}

bool TypeChecker::TypeCheckerImpl::CheckBinaryCondition(
    ASTContext& ctx, BinaryExpr& e, bool suppressIntroducingVariableError)
{
    ModalTy boolTy{TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN)};
    if (!IsCondition(e)) {
        return Check(ctx, boolTy, &e);
    }

    if (!suppressIntroducingVariableError && e.op == TokenKind::OR) {
        auto vars = CollectVariables(ctx, e);
        if (!vars.empty()) {
            auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_var_in_or_condition, *vars.front());
            auto iter = vars.cbegin() + 1; // Skip the first var as it has been reported in main hint.
            while (iter != vars.cend()) {
                builder.AddHint(**iter++);
            }
            suppressIntroducingVariableError = true;
        }
    }
    auto leftGood = CheckCondition(ctx, *e.leftExpr, suppressIntroducingVariableError);
    auto rightGood = CheckCondition(ctx, *e.rightExpr, suppressIntroducingVariableError);

    auto res = leftGood && rightGood;
    if (res) {
        e.SetTy(boolTy);
    } else {
        if (e.ShouldDiagnose() && !CanSkipDiag(e)) {
            DiagMismatchedTypes(diag, e, boolTy);
        }
        e.SetTy({TypeManager::GetInvalidTy()});
    }
    res = res && !suppressIntroducingVariableError;
    return res;
}

bool TypeChecker::TypeCheckerImpl::ChkIfExprNoElse(ASTContext& ctx, ModalTy target, IfExpr& ie)
{
    ModalTy unitTy{TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT)};
    ie.SetTy(unitTy);
    Synthesize({ctx, SynPos::EXPR_ARG}, ie.thenBody.get());
    Synthesize({ctx, SynPos::EXPR_ARG}, ie.elseBody.get());
    // The ifExpr may only have 'then' branch or as the case that if-elseif without ending 'else' branch.
    bool isWellTyped = ie.thenBody->GetTy().IsCorrect() && (!ie.elseBody || ie.elseBody->GetTy().IsCorrect());
    bool isTargetMatched = typeManager.IsSubtype(unitTy, target);
    if (isWellTyped && !isTargetMatched) {
        DiagMismatchedTypesWithFoundTy(
            diag, ie, target, unitTy, "the type of an 'if' expression without an 'else' branch is always 'Unit'");
    }
    return isWellTyped && isTargetMatched;
}

bool TypeChecker::TypeCheckerImpl::ChkIfExprTwoBranches(ASTContext& ctx, ModalTy target, IfExpr& ie)
{
    // Now both thenBody and elseBody are guaranteed to be non-nullable.
    if (!Check(ctx, target, ie.thenBody.get())) {
        if (ie.ShouldDiagnose() && !CanSkipDiag(*ie.thenBody) && !typeManager.IsSubtype(ie.thenBody->GetTy(), target)) {
            DiagMismatchedTypes(diag, *ie.thenBody, target);
        }
    }
    if (!Check(ctx, target, ie.elseBody.get())) {
        if (ie.ShouldDiagnose() && !CanSkipDiag(*ie.elseBody) && !typeManager.IsSubtype(ie.elseBody->GetTy(), target)) {
            DiagMismatchedTypes(diag, *ie.elseBody, target);
        }
    }
    if (!ie.thenBody->GetTy().IsCorrect() || !ie.elseBody->GetTy().IsCorrect()) {
        return false;
    }
    ie.SetTy(target);
    return true;
}
