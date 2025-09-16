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
#include "cangjie/AST/RecoverDesugar.h"

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
Ptr<Ty> TypeChecker::TypeCheckerImpl::SynIfExpr(ASTContext& ctx, IfExpr& ie)
{
    CJC_NULLPTR_CHECK(ie.condExpr);
    bool isWellTyped = CheckCondition(ctx, *ie.condExpr, false);
    isWellTyped = ie.thenBody && Ty::IsTyCorrect(Synthesize(ctx, ie.thenBody.get())) && isWellTyped;

    if (IsIfExprWithoutElse(ie)) {
        // For the case that if-elseif without ending 'else' branch.
        isWellTyped = (!ie.elseBody || Ty::IsTyCorrect(Synthesize(ctx, ie.elseBody.get()))) && isWellTyped;
        ie.ty = isWellTyped ? RawStaticCast<Ty*>(TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT))
                            : TypeManager::GetInvalidTy();
        return ie.ty;
    }

    isWellTyped = ie.elseBody && Ty::IsTyCorrect(Synthesize(ctx, ie.elseBody.get())) && isWellTyped;
    if (!isWellTyped) {
        ie.ty = TypeManager::GetInvalidTy();
        return TypeManager::GetInvalidTy();
    }

    ReplaceIdealTy(*ie.thenBody);
    ReplaceIdealTy(*ie.elseBody);
    ie.thenBody->ty = ReplaceThisTy(ie.thenBody->ty);
    ie.elseBody->ty = ReplaceThisTy(ie.elseBody->ty);
    auto thenTy = ie.thenBody->ty;
    auto elseTy = ie.elseBody->ty;
    if (Ty::IsTyCorrect(thenTy) && Ty::IsTyCorrect(elseTy)) {
        auto joinRes = JoinAndMeet(typeManager, {thenTy, elseTy}, {}, &importManager, ie.curFile).JoinAsVisibleTy();
        if (auto optErrs = JoinAndMeet::SetJoinedType(ie.ty, joinRes)) {
            std::string errMsg = "types " + Ty::ToString(thenTy) + " and " + Ty::ToString(elseTy);
            errMsg = ie.sourceExpr && ie.sourceExpr->astKind == ASTKind::IF_AVAILABLE_EXPR
                ? errMsg + " of the two lambda of this '@IfAvailable' expression mismatch"
                : errMsg + " of the two branches of this 'if' expression mismatch";
            diag.Diagnose(ie, DiagKind::sema_diag_report_error_message, errMsg).AddNote(*optErrs);
        }
    }
    return ie.ty;
}

bool TypeChecker::TypeCheckerImpl::ChkIfExpr(ASTContext& ctx, Ty& tgtTy, IfExpr& ie)
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
        ie.ty = TypeManager::GetInvalidTy();
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
    Ptr<Ty> boolTy = TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN);
    for (auto& p : lpd.patterns) {
        p->ctxExpr = lpd.initializer.get();
        PropagateCtxExpr(*p, *p->ctxExpr);
    }
    Synthesize(ctx, lpd.initializer.get());
    ReplaceIdealTy(*lpd.initializer);
    auto selectorTy = lpd.initializer->ty;
    // cannot have multiple pattern in one let with different astKind, e.g. let A|_ <- xxx
    // in this case, no need to check further
    if (!ChkPatternsSameASTKind(ctx, lpd.patterns)) {
        lpd.ty = TypeManager::GetInvalidTy();
        return false;
    }
    // intended shortcut &&: no need to check var pattern if it has already been reported by parent AST
    bool good = !suppressIntroducingVariableError && ChkNoVarPatternInOrPattern(ctx, lpd.patterns);
    bool subpatternsGood{true};
    for (auto& p : lpd.patterns) {
        subpatternsGood = ChkPattern(ctx, *selectorTy, *p) && subpatternsGood;
    }
    if (Ty::IsTyCorrect(selectorTy) && subpatternsGood) {
        lpd.ty = boolTy;
        return good;
    } else {
        lpd.ty = TypeManager::GetInvalidTy();
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
        paren->ty = paren->expr->ty;
        return res;
    }

    Ptr<Ty> boolTy = TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN);
    if (Check(ctx, boolTy, &e)) {
        return true;
    }

    auto shouldDiag = e.ShouldDiagnose() && !CanSkipDiag(e);
    if (shouldDiag) {
        DiagMismatchedTypes(diag, e, *boolTy);
    }
    return false;
}

bool TypeChecker::TypeCheckerImpl::CheckBinaryCondition(
    ASTContext& ctx, BinaryExpr& e, bool suppressIntroducingVariableError)
{
    Ptr<Ty> boolTy = TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN);
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
        e.ty = boolTy;
    } else {
        if (e.ShouldDiagnose() && !CanSkipDiag(e)) {
            DiagMismatchedTypes(diag, e, *boolTy);
        }
        e.ty = TypeManager::GetInvalidTy();
    }
    res = res && !suppressIntroducingVariableError;
    return res;
}

bool TypeChecker::TypeCheckerImpl::ChkIfExprNoElse(ASTContext& ctx, Ty& target, IfExpr& ie)
{
    Ptr<Ty> unitTy = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
    ie.ty = unitTy;
    Synthesize(ctx, ie.thenBody.get());
    Synthesize(ctx, ie.elseBody.get());
    // The ifExpr may only have 'then' branch or as the case that if-elseif without ending 'else' branch.
    bool isWellTyped = Ty::IsTyCorrect(ie.thenBody->ty) && (!ie.elseBody || Ty::IsTyCorrect(ie.elseBody->ty));
    bool isTargetMatched = typeManager.IsSubtype(unitTy, &target);
    if (isWellTyped && !isTargetMatched) {
        DiagMismatchedTypesWithFoundTy(
            diag, ie, target, *unitTy, "the type of an 'if' expression without an 'else' branch is always 'Unit'");
    }
    return isWellTyped && isTargetMatched;
}

bool TypeChecker::TypeCheckerImpl::ChkIfExprTwoBranches(ASTContext& ctx, Ty& target, IfExpr& ie)
{
    // Now both thenBody and elseBody are guaranteed to be non-nullable.
    if (!Check(ctx, &target, ie.thenBody.get())) {
        if (ie.ShouldDiagnose() && !CanSkipDiag(*ie.thenBody) && !typeManager.IsSubtype(ie.thenBody->ty, &target)) {
            DiagMismatchedTypes(diag, *ie.thenBody, target);
        }
    }
    if (!Check(ctx, &target, ie.elseBody.get())) {
        if (ie.ShouldDiagnose() && !CanSkipDiag(*ie.elseBody) && !typeManager.IsSubtype(ie.elseBody->ty, &target)) {
            DiagMismatchedTypes(diag, *ie.elseBody, target);
        }
    }
    if (!Ty::IsTyCorrect(ie.thenBody->ty) || !Ty::IsTyCorrect(ie.elseBody->ty)) {
        return false;
    }
    ie.ty = &target;
    return true;
}
