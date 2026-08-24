// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements typecheck apis for MatchExpr.
 */

#include <algorithm>
#include <memory>
#include <set>
#include <variant>
#include <vector>

#include "Diags.h"
#include "JoinAndMeet.h"
#include "PatternUsefulness.h"
#include "TypeCheckerImpl.h"

#include "cangjie/AST/Node.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Sema/TypeManager.h"
#include "cangjie/Utils/CheckUtils.h"
#include "cangjie/Utils/Utils.h"

using namespace Cangjie;
using namespace AST;
using namespace Sema;

namespace {
const std::unordered_map<ASTKind, std::string> PATTERN_ASTKIND_TO_STRING_MAP = {
    {ASTKind::CONST_PATTERN, "constant"},
    {ASTKind::WILDCARD_PATTERN, "wildcard"},
    {ASTKind::VAR_PATTERN, "binding"},
    {ASTKind::TUPLE_PATTERN, "tuple"},
    {ASTKind::TYPE_PATTERN, "type"},
    {ASTKind::ENUM_PATTERN, "enum"},
};

void CheckMatchExprSetTy(MatchExpr& me, ModalTy target, TypeManager& typeManager, ImportManager& impMgr,
    const std::set<ModalTy>& matchCaseTys)
{
    // If any of branch equal to the target type, do not join branches' types (avoiding unexpected common supertype).
    if (matchCaseTys.find(target) != matchCaseTys.end()) {
        me.SetTy(target);
        return;
    }
    auto joinAndMeet = JoinAndMeet(typeManager, matchCaseTys, {}, &impMgr, me.curFile);
    auto joinRes = joinAndMeet.JoinAsVisibleTy();
    me.SetTy(std::get_if<ModalTy>(&joinRes) ? std::get<ModalTy>(joinRes) : target);
}
} // namespace

namespace Cangjie {
ModalTy TypeChecker::TypeCheckerImpl::SynMatchExpr(ASTContext& ctx, MatchExpr& me)
{
    if (me.selector) {
        return SynMatchExprHasSelector(ctx, me);
    }
    return SynMatchExprNoSelector(ctx, me);
}

bool TypeChecker::TypeCheckerImpl::ChkMatchExpr(ASTContext& ctx, ModalTy target, MatchExpr& me)
{
    if (me.selector) {
        return ChkMatchExprHasSelector(ctx, target, me);
    }
    return ChkMatchExprNoSelector(ctx, target, me);
}

ModalTy TypeChecker::TypeCheckerImpl::SynMatchExprHasSelector(ASTContext& ctx, MatchExpr& me)
{
    // Synthesize selector's ty.
    CJC_NULLPTR_CHECK(me.selector);
    Synthesize({ctx, SynPos::NONE}, me.selector.get());
    ReplaceIdealTy(*me.selector);

    // Check each case.
    if (me.sugarKind == Expr::SugarKind::QUEST) {
        return SynQuestSugarMatchCaseBody(ctx, me);
    } else {
        // NoSugar, As, Is
        return SynNormalMatchCaseBody(ctx, me);
    }
}

ModalTy TypeChecker::TypeCheckerImpl::SynNormalMatchCaseBody(ASTContext& ctx, MatchExpr& me)
{
    std::set<ModalTy> matchCaseTyVec;
    CJC_NULLPTR_CHECK(me.selector);
    auto selectorTy = me.selector->GetTy();
    bool isMatchCorrect = !me.matchCases.empty();
    for (auto& mc : me.matchCases) {
        CJC_NULLPTR_CHECK(mc);
        CJC_ASSERT(!mc->patterns.empty());
        mc->SetCtxExprForPatterns(me.selector.get());
        if (Ty::IsTyCorrect(selectorTy)) {
            // Check whether pattern is compatible with selectorTy.
            bool isPatOK = ChkMatchCasePatterns(ctx, selectorTy, *mc, me.selector.get());
            // Check the actions in a match case anyway.
            bool isGuardOK = ChkMatchCasePatGuard(ctx, *mc);
            bool areActionsOK = ChkMatchCaseActions(ctx, ModalTy{}, *mc);
            if (isPatOK && isGuardOK && areActionsOK) {
                matchCaseTyVec.insert(mc->GetTy());
            } else {
                isMatchCorrect = false;
            }
        }
    }
    if (!isMatchCorrect) {
        me.SetTy({TypeManager::GetInvalidTy()});
        return me.GetTy();
    }

    if (me.selector->GetTy()->HasPlaceholder()) {
        me.selector->SetTy(typeManager.TryGreedySubst(me.selector->GetTy()));
        for (auto& mc : me.matchCases) {
            (void)ChkMatchCasePatterns(ctx, me.selector->GetTy(), *mc, me.selector.get());
        }
    }
    // Join match expr's ty.
    auto joinAndMeet = JoinAndMeet(typeManager, matchCaseTyVec, {}, &importManager, me.curFile);
    auto joinRes = joinAndMeet.JoinAsVisibleTy();
    auto [optErrs, joinedMeTy] = JoinAndMeet::SetJoinedType(me.GetTy(), joinRes);
    me.SetTy(joinedMeTy);
    if (optErrs) {
        if (me.sugarKind == Expr::SugarKind::IF_LET) {
            auto builder = diag.Diagnose(me, DiagKind::sema_diag_report_error_message,
                "types " + me.matchCases[0]->GetTy().String() + " and " + me.matchCases[1]->GetTy().String() +
                    " of the two branches of this 'if' expression mismatch");
            builder.AddNote(*optErrs);
        } else {
            auto builder = diag.Diagnose(me, DiagKind::sema_type_incompatible, "MatchCase");
            builder.AddNote(*optErrs);
        }
    }

    // Check pattern exhaustiveness and set unreachable attr of match cases.
    // Hotfix: ignore for desugared matchExpr.
    if (me.GetTy().IsCorrect() && me.sugarKind == Expr::SugarKind::NO_SUGAR &&
        !PatternUsefulness::CheckMatchExprHasSelectorExhaustivenessAndReachability(ctx.diag, typeManager, me)) {
        me.SetTy({TypeManager::GetInvalidTy()});
        return me.GetTy();
    }
    return me.GetTy();
}

ModalTy TypeChecker::TypeCheckerImpl::SynQuestSugarMatchCaseBody(ASTContext& ctx, MatchExpr& me)
{
    auto selectorTy = me.selector->GetTy();
    if (!Ty::IsTyCorrect(selectorTy)) {
        me.SetTy({TypeManager::GetInvalidTy()});
        return me.GetTy();
    } else if (!selectorTy->IsCoreOptionType()) {
        auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_optional_chain_non_optional, me);
        builder.AddMainHintArguments(selectorTy->String());
        me.SetTy({TypeManager::GetInvalidTy()});
        return me.GetTy();
    }
    // Match desugared from e? always have 2 cases.
    CJC_ASSERT(me.matchCases.size() == 2);
    auto& mc0 = me.matchCases[0];
    // Match desugared from e? always have patterns.
    CJC_ASSERT(mc0->patterns.size() == 1);
    CJC_NULLPTR_CHECK(mc0->patterns.front());
    mc0->SetCtxExprForPatterns(me.selector.get());
    // Desugared patterns are always compatible with selectorTy. Therefore, no error will be raised if e is correct.
    (void)ChkMatchCasePatterns(ctx, selectorTy, *mc0, me.selector.get());
    // Desugared patterns have no pattern guards and hence skip the invocation of ChkMatchCasePatGuard.
    (void)ChkMatchCaseActions(ctx, {}, *mc0);

    auto& mc1 = me.matchCases[1];
    CJC_ASSERT(mc1->patterns.size() == 1);
    CJC_NULLPTR_CHECK(mc1->patterns.front());
    mc1->SetCtxExprForPatterns(me.selector.get());
    ChkMatchCasePatterns(ctx, selectorTy, *mc1, me.selector.get());
    // Case 1 is merely a None constructor, whose type is the SAME as the type of CASE0. Return value can be ignored.
    (void)Check(ctx, mc0->GetTy(), mc1->exprOrDecls.get());
    mc1->SetTy(mc1->exprOrDecls->GetTy());

    me.SetTy(mc0->GetTy());
    return me.GetTy();
}

ModalTy TypeChecker::TypeCheckerImpl::SynMatchExprNoSelector(ASTContext& ctx, MatchExpr& me)
{
    std::set<ModalTy> matchCaseTyVec;
    bool hasInvalidCase = false;
    for (auto& mco : me.matchCaseOthers) {
        if (!SynMatchCaseNoSelector(ctx, *mco).IsCorrect()) {
            hasInvalidCase = true;
        } else {
            matchCaseTyVec.insert(mco->GetTy());
        }
    }
    if (hasInvalidCase) {
        me.SetTy({TypeManager::GetInvalidTy()});
        return me.GetTy();
    }

    // Join match expr's ty.
    auto joinAndMeet = JoinAndMeet(typeManager, matchCaseTyVec, {}, &importManager, me.curFile);
    auto joinRes = joinAndMeet.JoinAsVisibleTy();
    auto [optErrs, joinedMeTy] = JoinAndMeet::SetJoinedType(me.GetTy(), joinRes);
    me.SetTy(joinedMeTy);
    if (optErrs) {
        auto builder = diag.Diagnose(me, DiagKind::sema_type_incompatible, "MatchCase");
        builder.AddNote(*optErrs);
    }

    // Check exhaustiveness and set unreachable attr of match cases.
    if (!CheckMatchExprNoSelectorExhaustiveness(me, false)) {
        me.SetTy({TypeManager::GetInvalidTy()});
        return me.GetTy();
    }
    return me.GetTy();
}

bool TypeChecker::TypeCheckerImpl::ChkMatchExprHasSelector(ASTContext& ctx, AST::ModalTy target, AST::MatchExpr& me)
{
    CJC_NULLPTR_CHECK(me.selector);
    std::set<ModalTy> matchCaseTyVec;
    bool isMatchCorrect =
        Synthesize({ctx, SynPos::EXPR_ARG}, me.selector.get()) && ReplaceIdealTy(*me.selector);
    auto selectorTy = me.selector->GetTy();

    for (auto& mc : me.matchCases) {
        CJC_NULLPTR_CHECK(mc);
        CJC_ASSERT(!mc->patterns.empty());
        mc->SetCtxExprForPatterns(me.selector.get());
        // Check whether pattern has the sameTy with selectorTy.
        if (Ty::IsTyCorrect(selectorTy)) {
            bool isPatOK = ChkMatchCasePatterns(ctx, selectorTy, *mc, me.selector.get());
            bool isGuardOK = ChkMatchCasePatGuard(ctx, *mc);
            bool areActionsOK = ChkMatchCaseActions(ctx, target, *mc);
            if (isPatOK && isGuardOK && areActionsOK) {
                matchCaseTyVec.insert(mc->GetTy());
            } else {
                isMatchCorrect = false;
            }
        }
    }

    if (isMatchCorrect) {
        CheckMatchExprSetTy(me, target, typeManager, importManager, matchCaseTyVec);
        // Check pattern exhaustiveness and set unreachable attr of match cases.
        if (me.sugarKind == Expr::SugarKind::NO_SUGAR &&
            !PatternUsefulness::CheckMatchExprHasSelectorExhaustivenessAndReachability(ctx.diag, typeManager, me)) {
            isMatchCorrect = false;
        }
    }

    me.SetTy(isMatchCorrect ? me.GetTy() : ModalTy{TypeManager::GetInvalidTy()});
    return isMatchCorrect;
}

bool TypeChecker::TypeCheckerImpl::ChkMatchExprNoSelector(ASTContext& ctx, AST::ModalTy target, AST::MatchExpr& me)
{
    bool isWellTyped = true;
    std::set<ModalTy> matchCaseTyVec;
    for (auto& mco : me.matchCaseOthers) {
        if (!ChkMatchCaseNoSelector(ctx, target, *mco)) {
            isWellTyped = false;
        } else {
            matchCaseTyVec.insert(mco->GetTy());
        }
    }

    if (isWellTyped) {
        CheckMatchExprSetTy(me, target, typeManager, importManager, matchCaseTyVec);
        // Check pattern exhaustiveness and set unreachable attr of match cases.
        if (!CheckMatchExprNoSelectorExhaustiveness(me, false)) {
            isWellTyped = false;
        }
    }
    me.SetTy(isWellTyped ? me.GetTy() : ModalTy{TypeManager::GetInvalidTy()});
    return isWellTyped;
}

ModalTy TypeChecker::TypeCheckerImpl::SynMatchCaseNoSelector(ASTContext& ctx, MatchCaseOther& mco)
{
    // Type of patternGuard (matchExpr) is boolean.
    if (Is<WildcardExpr>(mco.matchExpr.get())) {
        mco.matchExpr->SetTy(ModalTy{TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN)});
    } else {
        if (!Check(ctx, ModalTy{TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN)}, mco.matchExpr.get())) {
            mco.matchExpr->SetTy({TypeManager::GetInvalidTy()});
        }
    }

    // Synthesize the ty of exprOrDecls of each case.
    mco.SetTy(Synthesize({ctx, SynPos::EXPR_ARG}, mco.exprOrDecls.get()));
    return mco.GetTy();
}

bool TypeChecker::TypeCheckerImpl::ChkMatchCasePatGuard(ASTContext& ctx, const MatchCase& mc)
{
    bool ret = true;
    if (mc.patternGuard) {
        ModalTy boolTy{TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN)};
        ret = Check(ctx, boolTy, mc.patternGuard.get());
    }
    return ret;
}

bool TypeChecker::TypeCheckerImpl::ChkMatchCaseActions(ASTContext& ctx, ModalTy target, MatchCase& mc)
{
    bool ret = true;
    if (!mc.exprOrDecls) {
        mc.SetTy({TypeManager::GetInvalidTy()});
        return false;
    }
    if (!target) { // Synthesize the ty of exprOrDecls of each case.
        mc.exprOrDecls->SetTy(Synthesize({ctx, SynPos::EXPR_ARG}, mc.exprOrDecls.get()));
        mc.SetTy(mc.exprOrDecls->GetTy());
    } else if (Check(ctx, target, mc.exprOrDecls.get())) { // Check whether exprOrDecls->GetTy() has given target ty.
        mc.SetTy(mc.exprOrDecls->GetTy());
    } else {
        mc.SetTy({TypeManager::GetInvalidTy()});
        ret = false;
    }
    return ret;
}

bool TypeChecker::TypeCheckerImpl::ChkNoVarPatternInOrPattern(
    const ASTContext& ctx, const std::vector<OwnedPtr<AST::Pattern>>& ps)
{
    CJC_ASSERT(!ps.empty());
    if (ps.size() == 1) {
        return true;
    }
    std::vector<Ptr<Node>> varPatterns;
    for (auto& pattern : ps) {
        CJC_NULLPTR_CHECK(pattern);
        Walker(pattern.get(), [&varPatterns, &ctx](Ptr<Node> node) {
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
    }
    if (varPatterns.empty()) {
        return true;
    }
    auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_var_in_or_pattern, *varPatterns.front());
    auto iter = varPatterns.cbegin() + 1; // Skip the first `Var` as it has been reported in main hint.
    while (iter != varPatterns.cend()) {
        builder.AddHint(**iter);
        ++iter;
    }
    return false;
}

bool TypeChecker::TypeCheckerImpl::ChkPatternsSameASTKind(const ASTContext& ctx,
    const std::vector<OwnedPtr<Pattern>>& patterns)
{
    CJC_ASSERT(!patterns.empty());
    if (patterns.size() == 1) {
        CJC_NULLPTR_CHECK(patterns.front());
        return true;
    }
    if (std::all_of(patterns.cbegin(), patterns.cend(), [](const OwnedPtr<Pattern>& pattern) {
            CJC_NULLPTR_CHECK(pattern);
            return pattern->astKind == ASTKind::VAR_OR_ENUM_PATTERN || pattern->astKind == ASTKind::ENUM_PATTERN;
        })) {
        // We don't know the real type of the `VarOrEnumPattern` by far, and there are two possible cases:
        // 1. if it is a `VarPattern`, the `CheckMatchCaseNoVarPatternsInOr` will report errors latter.
        // 2. if it is an `EnumPattern`, it should be compiled correctly, e.g., `Some(_) | None`.
        return true;
    }
    auto patternToString = [&ctx](const Pattern& p) {
        auto kind = p.astKind;
        if (kind == ASTKind::VAR_OR_ENUM_PATTERN) {
            kind = ctx.IsEnumConstructor(StaticCast<const VarOrEnumPattern&>(p).identifier) ? ASTKind::ENUM_PATTERN
                                                                                            : ASTKind::VAR_PATTERN;
        }
        auto iter = PATTERN_ASTKIND_TO_STRING_MAP.find(kind);
        std::string res = iter == PATTERN_ASTKIND_TO_STRING_MAP.cend() ? "invalid" : iter->second;
        return res + " pattern";
    };
    const OwnedPtr<Pattern>& front = patterns.front();
    for (auto iter = patterns.cbegin() + 1; iter != patterns.cend(); ++iter) {
        CJC_NULLPTR_CHECK(*iter);
        if ((*iter)->astKind != front->astKind) {
            auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_different_or_pattern, *front);
            builder.AddMainHintArguments(patternToString(*front));
            builder.AddHint(**iter, patternToString(**iter));
            return false;
        }
    }
    return true;
}

bool TypeChecker::TypeCheckerImpl::ChkMatchCasePatterns(
    ASTContext& ctx, ModalTy target, MatchCase& mc, Ptr<Expr> selector)
{
    if (!ChkNoVarPatternInOrPattern(ctx, mc.patterns) || !ChkPatternsSameASTKind(ctx, mc.patterns)) {
        for (auto& pattern : mc.patterns) {
            CJC_NULLPTR_CHECK(pattern);
            pattern->SetTy({TypeManager::GetInvalidTy()});
        }
        return false;
    }
    bool ret = true;
    for (auto& pattern : mc.patterns) {
        ModalTy safeTarget = target.Ty() ? target : ModalTy{TypeManager::GetInvalidTy()};
        if (!ChkPattern(ctx, safeTarget, *pattern, true, selector)) {
            ret = false;
        }
    }
    return ret;
}

bool TypeChecker::TypeCheckerImpl::ChkMatchCaseNoSelector(ASTContext& ctx, ModalTy target, MatchCaseOther& mco)
{
    bool ret = true;
    // Type of patternGuard (matchExpr) is boolean.
    if (Is<WildcardExpr>(mco.matchExpr.get())) {
        mco.matchExpr->SetTy(ModalTy{TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN)});
    } else {
        if (!Check(ctx, ModalTy{TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN)}, mco.matchExpr.get())) {
            mco.matchExpr->SetTy({TypeManager::GetInvalidTy()});
            ret = false;
        }
    }

    // Check whether exprOrDecls->GetTy() has given target ty.
    if (Check(ctx, target, mco.exprOrDecls.get())) {
        mco.SetTy(mco.exprOrDecls->GetTy());
    } else {
        mco.SetTy({TypeManager::GetInvalidTy()});
        ret = false;
    }
    return ret;
}

bool TypeChecker::TypeCheckerImpl::CheckMatchExprNoSelectorExhaustiveness(MatchExpr& me, bool hasDefault)
{
    bool ret = true;
    size_t defaultCase{0};
    for (size_t i = 0; i < me.matchCaseOthers.size(); ++i) {
        auto matchCaseOther = me.matchCaseOthers[i].get();
        if (!hasDefault && Is<WildcardExpr>(matchCaseOther->matchExpr.get())) {
            hasDefault = true;
            defaultCase = i;
        }
        if (Ty::IsInitialTy(matchCaseOther->DataTy())) {
            ret = false;
            diag.Diagnose(*matchCaseOther, DiagKind::sema_match_case_has_no_type);
            continue;
        }
    }
    if (!hasDefault) {
        ret = false;
        diag.Diagnose(me, DiagKind::sema_match_case_must_have_default);
    }
    // The cases after default should be set UNREACHABLE.
    for (size_t i = defaultCase + 1; i < me.matchCaseOthers.size(); i++) {
        me.matchCaseOthers[i]->EnableAttr(Attribute::UNREACHABLE);
    }
    return ret;
}

bool TypeChecker::TypeCheckerImpl::IsIrrefutablePattern(const Pattern& pattern)
{
    switch (pattern.astKind) {
        case AST::ASTKind::INVALID_PATTERN:
        case AST::ASTKind::CONST_PATTERN:
        case AST::ASTKind::TYPE_PATTERN:
            return false;
        case AST::ASTKind::WILDCARD_PATTERN:
        case AST::ASTKind::VAR_PATTERN:
            return true;
        case AST::ASTKind::TUPLE_PATTERN: {
            auto& tuplePattern = static_cast<const TuplePattern&>(pattern);
            return std::all_of(tuplePattern.patterns.cbegin(), tuplePattern.patterns.cend(),
                [this](const OwnedPtr<Pattern>& p) { return IsIrrefutablePattern(*p); });
        }
        case AST::ASTKind::ENUM_PATTERN: {
            if (!pattern.DataTy() || !pattern.GetTy()->IsEnum()) {
                return false;
            }
            auto& enumPattern = static_cast<const EnumPattern&>(pattern);
            auto enumTy = RawStaticCast<EnumTy*>(pattern.DataTy());
            return enumTy && enumTy->declPtr && enumTy->declPtr->constructors.size() == 1 &&
                std::all_of(enumPattern.patterns.cbegin(), enumPattern.patterns.cend(),
                    [this](const OwnedPtr<Pattern>& p) { return IsIrrefutablePattern(*p); });
        }
        default:
            CJC_ABORT();
            return false;
    }
}
}; // namespace Cangjie
