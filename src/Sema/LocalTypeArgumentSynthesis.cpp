// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * Local type argument synthesis.
 */
#include "LocalTypeArgumentSynthesis.h"
#include "TypeCheckerImpl.h"

#include <algorithm>
#include <mutex>
#include <utility>

#include "JoinAndMeet.h"
#include "Promotion.h"
#include "TyVarConstraintGraph.h"
#include "TypeCheckUtil.h"

#include "cangjie/AST/Node.h"
#include "cangjie/Sema/TypeManager.h"
#include "cangjie/AST/ASTCasting.h"

using namespace Cangjie;
using namespace AST;

namespace {
std::vector<size_t> GetOrderedCheckingIndexes(const std::vector<Ptr<AST::Ty>>& tys)
{
    // Ordering index of types to make index of non-ideal types precedes the index of ideal types.
    // Synthesis non-ideal types first to restrict possible range of ideal type.
    // Since cj allows auto box option type, we should place option type before non-option type to allows
    //  'A'& 'Option<A>' & 'Equatable<T>' results in upper bound 'Equatable<Option<A>>'.
    std::vector<size_t> ideals;
    std::vector<size_t> options;
    std::vector<size_t> others;
    for (size_t i = 0; i < tys.size(); ++i) {
        if (Ty::IsTyCorrect(tys[i])) {
            if (tys[i]->IsIdeal()) {
                (void)ideals.emplace_back(i);
            } else if (tys[i]->IsCoreOptionType()) {
                (void)options.emplace_back(i);
            } else {
                (void)others.emplace_back(i);
            }
        } else {
            (void)others.emplace_back(i);
        }
    }
    std::stable_sort(options.begin(), options.end(), [&tys](auto l, auto r) {
        return TypeCheckUtil::CountOptionNestedLevel(*tys[l]) > TypeCheckUtil::CountOptionNestedLevel(*tys[r]);
    });
    options.insert(options.end(), others.begin(), others.end());
    options.insert(options.end(), ideals.begin(), ideals.end());
    return options;
}
} // namespace

LocalTypeArgumentSynthesis::~LocalTypeArgumentSynthesis()
{
    curTyVar = nullptr;
}

std::optional<TypeSubst> LocalTypeArgumentSynthesis::SynthesizeTypeArguments(bool allowPartial)
{
    static std::mutex lock;
    std::lock_guard<std::mutex> guard(lock);
    CJC_ASSERT(argPack.argTys.size() == argPack.argBlames.size());
    CJC_ASSERT(argPack.argTys.size() == argPack.paramTys.size());
    for (auto tv : argPack.tyVarsToSolve) {
        CJC_ASSERT(tv->isPlaceholder);
    }
    CopyUpperbound();
    cms = ConstraintWithMemos{{InitConstraints(argPack.tyVarsToSolve), {}, false, false}};
    std::vector<size_t> orderedIndexes = GetOrderedCheckingIndexes(argPack.argTys);
    for (auto i : orderedIndexes) {
        if (!Ty::IsTyCorrect(argPack.argTys[i]) || !Ty::IsTyCorrect(argPack.paramTys[i])) {
            return {};
        }
        if (needDiagMsg && errMsg.style == SolvingErrStyle::DEFAULT) {
            auto [tmpCms, tmpMsg] = Unify(
                cms, {*argPack.argTys[i], {argPack.argBlames[i]}}, {*argPack.paramTys[i], {argPack.argBlames[i]}});
            cms = tmpCms;
            errMsg = tmpMsg;
        } else {
            cms =
                Unify(cms, {*argPack.argTys[i], {argPack.argBlames[i]}}, {*argPack.paramTys[i], {argPack.argBlames[i]}})
                    .first;
        }
        if (cms.empty()) {
            MaybeSetErrMsg(MakeMsgMismatchedArg(argPack.argBlames[i]));
            return {};
        }
    }

    if (Ty::IsTyCorrect(argPack.funcRetTy) && argPack.funcRetTy->HasGeneric() && Ty::IsTyCorrect(argPack.retTyUB)) {
        // Only consider function's return type when the return type contains generic type.
        // Add a constraint that the function's return type should be smaller than the type required by the context.
        if (needDiagMsg && errMsg.style == SolvingErrStyle::DEFAULT) {
            auto [tmpCms, tmpMsg] =
                Unify(cms, {*argPack.funcRetTy, {argPack.retBlame}}, {*argPack.retTyUB, {argPack.retBlame}});
            cms = tmpCms;
            errMsg = tmpMsg;
        } else {
            cms = Unify(cms, {*argPack.funcRetTy, {argPack.retBlame}}, {*argPack.retTyUB, {argPack.retBlame}}).first;
        }
    }
    if (cms.empty()) {
        MaybeSetErrMsg(MakeMsgMismatchedRet(argPack.retBlame));
        return {};
    }

    if (!allowPartial && !needDiagMsg) {
        Utils::EraseIf(cms, [this](const ConstraintWithMemo& cm) { return !DoesCSCoverAllTyVars(cm.constraint); });
    }

    if (auto optSubst = SolveConstraints(allowPartial)) {
        auto subst = ResetIdealTypesInSubst(*optSubst);
        return {subst};
    } else {
        return {};
    }
}

// copy and instantiate generic upperbound from universal ty var to instance ty var
void LocalTypeArgumentSynthesis::CopyUpperbound()
{
    for (auto& [univ, inst] : tyMgr.GetInstMapping().u2i) {
        auto instTv = RawStaticCast<GenericsTy*>(inst);
        if (!Utils::In(instTv, argPack.tyVarsToSolve)) {
            continue;
        }
        for (auto upper : univ->upperBounds) {
            CJC_NULLPTR_CHECK(upper);
            instTv->upperBounds.emplace(tyMgr.InstOf(upper));
            if (gcBlames.count(univ) > 0 && gcBlames.at(univ).count(upper) > 0) {
                gcBlamesInst[instTv][tyMgr.InstOf(upper)] = gcBlames.at(univ).at(upper);
            }
        }
    }
}

Constraint LocalTypeArgumentSynthesis::InitConstraints(const TyVars& tyVarsToSolve)
{
    Constraint res;
    for (auto& tyVar : tyVarsToSolve) {
        // Type variables must be of generic types by definition.
        if (tyVar == nullptr) {
            res = {};
            break;
        }
        if (tyVar->IsGeneric()) {
            auto ubs = RawStaticCast<GenericsTy*>(tyVar)->upperBounds;
            TyVarBounds bounds;
            for (auto ub : ubs) {
                bounds.ubs.insert(ub);
                for (auto node : gcBlamesInst[tyVar][ub]) {
                    bounds.ub2Blames[ub].insert({.src = node, .style = BlameStyle::CONSTRAINT});
                }
            }
            InsertConstraint(res, *tyVar, bounds);
        }
    }
    return res;
}

void LocalTypeArgumentSynthesis::InsertConstraint(Constraint& c, TyVar& tyVar, TyVarBounds& tvb) const
{
    auto found = c.find(&tyVar);
    if (found == c.end()) {
        c.emplace(&tyVar, tvb);
    } else {
        TyVarBounds& old = found->second;
        for (auto lb : tvb.lbs) {
            old.lbs.insert(lb);
            auto& lb2Blames = tvb.lb2Blames[lb];
            old.lb2Blames[lb].insert(lb2Blames.begin(), lb2Blames.end());
        }
        for (auto ub : tvb.ubs) {
            old.ubs.insert(ub);
            auto& ub2Blames = tvb.ub2Blames[ub];
            old.ub2Blames[ub].insert(ub2Blames.begin(), ub2Blames.end());
        }
    }
}

std::pair<LocalTypeArgumentSynthesis::ConstraintWithMemos, SolvingErrInfo> LocalTypeArgumentSynthesis::Unify(
    const ConstraintWithMemos& newCMS, const Tracked<Ty>& argTTy, const Tracked<Ty>& paramTTy)
{
    LocTyArgSynArgPack dummyArgPack = {
        argPack.tyVarsToSolve, {}, {}, {}, TypeManager::GetInvalidTy(), TypeManager::GetInvalidTy(), Blame()};
    ConstraintWithMemos res;
    SolvingErrInfo msg;
    std::for_each(newCMS.cbegin(), newCMS.cend(), [this, &msg, &res, &argTTy, &paramTTy, &dummyArgPack](auto& cm) {
        auto newSynIns = LocalTypeArgumentSynthesis(tyMgr, dummyArgPack, {}, needDiagMsg);
        newSynIns.cms = {cm};
        newSynIns.deterministic = deterministic;
        if (newSynIns.UnifyOne(argTTy, paramTTy)) {
            // The result of correct unification will be kept.
            // If there are any errors during the unification, the result will not be recorded into the res variable.
            res.insert(res.end(), newSynIns.cms.begin(), newSynIns.cms.end());
        } else if (msg.style == SolvingErrStyle::DEFAULT) {
            msg = newSynIns.errMsg;
        }
    });
    return {res, msg};
}

bool LocalTypeArgumentSynthesis::UnifyAndTrim(
    const ConstraintWithMemos& curCMS, const Tracked<Ty>& argTTy, const Tracked<Ty>& paramTTy)
{
    auto [newCMS, msg] = Unify(curCMS, argTTy, paramTTy);
    MaybeSetErrMsg(msg);
    return VerifyAndSetCMS(newCMS);
}

bool LocalTypeArgumentSynthesis::VerifyAndSetCMS(const LocalTypeArgumentSynthesis::ConstraintWithMemos& newCMS)
{
    if (!cms.empty() && newCMS.empty()) {
        cms = {};
        return false;
    } else {
        cms = newCMS;
        return true;
    }
}

bool LocalTypeArgumentSynthesis::UnifyOne(const Tracked<Ty>& argTTy, const Tracked<Ty>& paramTTy)
{
    auto& argTy = argTTy.ty;
    auto& paramTy = paramTTy.ty;
    // Handle the base case.
    if (cms.size() != 1) {
        return false;
    }
    if (&argTy == &paramTy) {
        return true;
    }
    if (argTy.IsQuest() || paramTy.IsQuest()) {
        return true;
    }

    if (paramTy.IsIntersection()) {
        return UnifyParamIntersectionTy(argTTy, {static_cast<IntersectionTy&>(paramTy), paramTTy.blames});
    } else if (argTy.IsIntersection()) {
        return UnifyArgIntersectionTy({static_cast<IntersectionTy&>(argTy), argTTy.blames}, paramTTy);
    } else if (argTy.IsUnion()) {
        return UnifyArgUnionTy({static_cast<UnionTy&>(argTy), argTTy.blames}, paramTTy);
    } else if (paramTy.IsUnion()) {
        return UnifyParamUnionTy(argTTy, {static_cast<UnionTy&>(paramTy), paramTTy.blames});
    }

    MemoForUnifiedTys& memo = cms.front().memo;
    auto inProcessing = std::pair<Ptr<Ty>, Ptr<Ty>>(&argTy, &paramTy);
    if (Utils::In(inProcessing, memo)) {
        return true;
    }

    if ((paramTy.IsGeneric() && StaticCast<TyVar*>(&paramTy)->isPlaceholder) ||
        (argTy.IsGeneric() && StaticCast<TyVar*>(&argTy)->isPlaceholder)) {
        memo.insert(inProcessing);
        return UnifyTyVar(argTTy, paramTTy);
    }

    // When the 'paramTy' has more option nesting level than the 'argTy':
    // If both the 'argTy' and the 'paramTy' are Option type, unify their typeArgs.
    // Otherwise If the 'paramTy' is Option type, unify the 'paramTy''s type argument with the 'argTy'.
    if (TypeCheckUtil::CountOptionNestedLevel(paramTy) > TypeCheckUtil::CountOptionNestedLevel(argTy)) {
        CJC_ASSERT(paramTy.typeArgs.size() == 1 && paramTy.typeArgs.front() != nullptr);
        if (argTy.IsCoreOptionType() && paramTy.IsCoreOptionType()) {
            memo.insert(inProcessing);
            CJC_ASSERT(argTy.typeArgs.size() == 1 && argTy.typeArgs.front() != nullptr);
            return UnifyOne({*argTy.typeArgs[0], argTTy.blames}, {*paramTy.typeArgs[0], paramTTy.blames});
        }
        memo.insert(inProcessing);
        return UnifyOne(argTTy, {*paramTy.typeArgs[0], paramTTy.blames});
    }

    // Context type variables are first promoted
    if (argTy.IsGeneric() || paramTy.IsGeneric()) {
        return UnifyContextTyVar(argTTy, paramTTy);
    } else if (paramTy.IsNominal() && argTy.IsNominal()) {
        return UnifyNominal(argTTy, paramTTy);
    } else if (argTy.IsBuiltin() && paramTy.IsInterface()) {
        return UnifyBuiltInExtension(argTTy, {static_cast<InterfaceTy&>(paramTy), paramTTy.blames});
    } else if (paramTy.IsPrimitive() && argTy.IsPrimitive()) {
        return UnifyPrimitiveTy(static_cast<PrimitiveTy&>(argTy), static_cast<PrimitiveTy&>(paramTy));
    } else if ((argTy.IsArray() && paramTy.IsArray()) || (argTy.IsPointer() && paramTy.IsPointer())) {
        return UnifyBuiltInTy(argTTy, paramTTy);
    } else if (argTy.IsFunc() && paramTy.IsFunc()) {
        return UnifyFuncTy(
            {static_cast<FuncTy&>(argTy), argTTy.blames}, {static_cast<FuncTy&>(paramTy), paramTTy.blames});
    } else if (argTy.IsTuple() && paramTy.IsTuple()) {
        return UnifyTupleTy(
            {static_cast<TupleTy&>(argTy), argTTy.blames}, {static_cast<TupleTy&>(paramTy), paramTTy.blames});
    } else {
        return tyMgr.IsSubtype(&argTy, &paramTy);
    }
}

bool LocalTypeArgumentSynthesis::UnifyTyVar(const Tracked<Ty>& argTTy, const Tracked<Ty>& paramTTy)
{
    CJC_ASSERT(cms.size() == 1);
    auto& argTy = argTTy.ty;
    auto& paramTy = paramTTy.ty;
    // If paramTy is the generic parameter to be solved.
    Ptr<TyVar> tyVar = nullptr;
    Ptr<Ty> lb = TypeManager::GetInvalidTy();
    Ptr<Ty> ub = TypeManager::GetInvalidTy();
    std::set<Blame> lbBlames;
    std::set<Blame> ubBlames;

    auto unifyBound = [&tyVar, this](Ptr<Ty>& one, Ptr<Ty>& other, const Tracked<Ty>& bound, bool isUb) {
        one = &bound.ty;

        if (one->IsNothing()) {
            cms.front().hasNothingTy = true;
        } else if (one->IsAny()) {
            cms.front().hasAnyTy = true;
        } else if (auto ctt = DynamicCast<ClassThisTy*>(one)) {
            // For inferred type, the class this type should be substituted as original class type.
            one = tyMgr.GetClassTy(*ctt->declPtr, ctt->typeArgs);
        }

        if (deterministic && IsGreedySolution(*tyVar, *one, isUb)) {
            other = one;
            auto& eq = cms.front().constraint[tyVar].eq;
            if (eq.empty()) {
                eq.insert(one);
            }
        }
    };

    if (auto genParam = DynamicCast<TyVar*>(&paramTy); genParam && genParam->isPlaceholder) { // case T = X.
        tyVar = genParam;
        lbBlames = argTTy.blames;
        unifyBound(lb, ub, argTTy, false);
    } else if (auto genArg = DynamicCast<TyVar*>(&argTy); genArg && genArg->isPlaceholder) { // case X = T.
        tyVar = genArg;
        ubBlames = paramTTy.blames;
        unifyBound(ub, lb, paramTTy, true);
    } else {
        return false;
    }
    this->curTyVar = tyVar;

    // Recursively constrain existing bounds with the newly added one.
    return UnifyTyVarCollectConstraints(*tyVar, {*lb, lbBlames}, {*ub, ubBlames});
}

bool LocalTypeArgumentSynthesis::UnifyTyVarCollectConstraints(
    TyVar& tyVar, const Tracked<Ty>& lbTTy, const Tracked<Ty>& ubTTy)
{
    if (cms.size() != 1) {
        return false;
    }
    auto& lb = lbTTy.ty;
    auto& ub = ubTTy.ty;
    Constraint& c = cms.front().constraint;
    if (Ty::IsTyCorrect(&lb)) {
        TyVarBounds bounds;
        bounds.lbs.insert(&lb);
        bounds.lb2Blames[&lb] = lbTTy.blames;
        InsertConstraint(c, tyVar, bounds);
    }
    if (Ty::IsTyCorrect(&ub)) {
        TyVarBounds bounds;
        bounds.ubs.insert(&ub);
        bounds.ub2Blames[&ub] = ubTTy.blames;
        InsertConstraint(c, tyVar, bounds);
    }

    if (Ty::IsTyCorrect(&lb)) {
        // the reference to `c` may be invalidated in the following loop; must copy here
        auto ubs = c[&tyVar].ubs;
        auto ub2Blames = c[&tyVar].ub2Blames;
        // lb and ub are initialized to INVALID_TY and thus are not null.
        // Elements in ubs are tested to be not null in UnifyTyVarCollectNewConstraints.
        // The join adds option box to lb if option-boxed lb already exists in the type var's lbs.
        Ptr<Ty> lbTy{};
        if (deterministic) {
            lbTy = &lb;
        } else {
            auto joinRes = JoinAndMeet(tyMgr, c[&tyVar].lbs, argPack.tyVarsToSolve).JoinAsVisibleTy();
            if (JoinAndMeet::SetJoinedType(lbTy, joinRes).has_value() || lbTy->IsAny()) {
                lbTy = &lb;
            }
        }
        std::optional<StableTys> st;
        auto [tyL, tyR] = GetMaybeStableIters(ubs, st);
        for (auto ub0 = tyL; ub0 != tyR; ++ub0) {
            if (!UnifyAndTrim(cms, {*lbTy, lbTTy.blames}, {**ub0, ub2Blames[*ub0]})) {
                MaybeSetErrMsg(MakeMsgConflictingConstraints(tyVar, {lbTTy}, {{**ub0, ub2Blames[*ub0]}}));
                return false;
            }
        }
        // with known sum, but the sum doesn't include lb
        if (deterministic && !tyMgr.TyVarHasNoSum(tyVar)) {
            auto& sum = c[&tyVar].sum;
            if (!lb.IsNothing() && sum.count(&lb) == 0) {
                return false;
            }
        }
    }
    if (Ty::IsTyCorrect(&ub)) {
        // the reference to `c` may be invalidated in the following loop; must copy here
        auto lbs = c[&tyVar].lbs;
        auto lb2Blames = c[&tyVar].lb2Blames;
        std::optional<StableTys> st;
        auto [tyL, tyR] = GetMaybeStableIters(lbs, st);
        for (auto lb0 = tyL; lb0 != tyR; ++lb0) {
            if (!UnifyAndTrim(cms, {**lb0, lb2Blames[*lb0]}, ubTTy)) {
                MaybeSetErrMsg(MakeMsgConflictingConstraints(tyVar, {{**lb0, lb2Blames[*lb0]}}, {ubTTy}));
                return false;
            }
        }
    }
    // with known sum, but the sum doesn't include eq
    if (deterministic && !tyMgr.TyVarHasNoSum(tyVar)) {
        auto& sum = c[&tyVar].sum;
        auto& eq = c[&tyVar].eq;
        if (!eq.empty() && !(*eq.begin())->IsNothing() && sum.count(*eq.begin()) == 0) {
            return false;
        }
    }
    return true;
}

// Unify type variables not those to be solved.
bool LocalTypeArgumentSynthesis::UnifyContextTyVar(const Tracked<Ty>& argTTy, const Tracked<Ty>& paramTTy)
{
    auto& argTy = argTTy.ty;
    auto& paramTy = paramTTy.ty;
    if (&argTy == &paramTy) {
        return true;
    }
    if (auto gTy = DynamicCast<GenericsTy*>(&argTy); gTy && !gTy->isPlaceholder) {
        // Need to check argTy <: argUBound <: paramTy.
        // Hotfix, should be changed later.
        // Unify empty intersection ty as argument is same with unify with type of 'Any'.
        auto& ubs = gTy->upperBounds;
        if (ubs.empty()) {
            return UnifyOne({*tyMgr.GetAnyTy(), {}}, paramTTy);
        }
        auto ubTy = ubs.size() == 1 ? *ubs.begin() : tyMgr.GetIntersectionTy(ubs);
        return UnifyOne({*ubTy, argTTy.blames}, paramTTy);
    }

    if (paramTy.IsGeneric() && !StaticCast<TyVar*>(&paramTy)->isPlaceholder) {
        // Need to check argTy <: paramLBound <: paramTy. However, currently T === Nothing
        return false;
    }
    return false;
}

bool LocalTypeArgumentSynthesis::UnifyFuncTy(const Tracked<FuncTy>& argTTy, const Tracked<FuncTy>& paramTTy)
{
    auto& argTy = argTTy.ty;
    auto& paramTy = paramTTy.ty;
    if (argTy.paramTys.size() != paramTy.paramTys.size()) {
        return false;
    }
    for (size_t i = 0; i < paramTy.paramTys.size(); ++i) {
        if (!paramTy.paramTys[i] || !argTy.paramTys[i]) {
            return false;
        }
        if (!UnifyAndTrim(cms, {*paramTy.paramTys[i], paramTTy.blames}, {*argTy.paramTys[i], argTTy.blames})) {
            return false;
        }
    }
    if (!argTy.retTy || !paramTy.retTy) {
        return false;
    }
    if (!UnifyAndTrim(cms, {*argTy.retTy, argTTy.blames}, {*paramTy.retTy, paramTTy.blames})) {
        return false;
    }
    return true;
}

bool LocalTypeArgumentSynthesis::UnifyTupleTy(const Tracked<TupleTy>& argTTy, const Tracked<TupleTy>& paramTTy)
{
    auto& argTy = argTTy.ty;
    auto& paramTy = paramTTy.ty;
    if (argTy.typeArgs.size() != paramTy.typeArgs.size()) {
        return false;
    }
    for (size_t i = 0; i < paramTy.typeArgs.size(); ++i) {
        if (!argTy.typeArgs[i] || !paramTy.typeArgs[i]) {
            return false;
        }
        if (!UnifyOne({*argTy.typeArgs[i], argTTy.blames}, {*paramTy.typeArgs[i], paramTTy.blames})) {
            return false;
        }
    }
    return true;
}

bool LocalTypeArgumentSynthesis::UnifyNominal(const Tracked<Ty>& argTTy, const Tracked<Ty>& paramTTy)
{
    auto& argTy = argTTy.ty;
    auto& paramTy = paramTTy.ty;
    auto prTys = std::make_unique<Promotion>(tyMgr)->Promote(argTy, paramTy);
    if (prTys.empty()) {
        return false;
    }
    ConstraintWithMemos res;
    std::optional<StableTys> st;
    auto [tyL, tyR] = GetMaybeStableIters(prTys, st);

    for (auto ty = tyL; ty != tyR; ++ty) {
        auto prTy = *ty;
        if (!Ty::IsTyCorrect(prTy)) {
            continue;
        }
        if (!Ty::IsTyArgsSizeEqual(paramTy, *prTy)) {
            continue;
        }
        auto currentCms = cms;
        for (size_t i = 0; i < prTy->typeArgs.size(); ++i) {
            if (!prTy->typeArgs[i] || !paramTy.typeArgs[i]) {
                continue;
            }
            // For nominal types, I1<A> <: I2<B> iff A <: B and B <: A.
            if (needDiagMsg && errMsg.style == SolvingErrStyle::DEFAULT) {
                auto [tmpCms, tmpMsg] =
                    Unify(currentCms, {*prTy->typeArgs[i], argTTy.blames}, {*paramTy.typeArgs[i], paramTTy.blames});
                errMsg = tmpMsg;
                auto [tmpCms2, tmpMsg2] =
                    Unify(tmpCms, {*paramTy.typeArgs[i], paramTTy.blames}, {*prTy->typeArgs[i], argTTy.blames});
                currentCms = tmpCms2;
                MaybeSetErrMsg(tmpMsg2);
            } else {
                currentCms =
                    Unify(currentCms, {*prTy->typeArgs[i], argTTy.blames}, {*paramTy.typeArgs[i], paramTTy.blames})
                        .first;
                currentCms =
                    Unify(currentCms, {*paramTy.typeArgs[i], paramTTy.blames}, {*prTy->typeArgs[i], argTTy.blames})
                        .first;
            }
        }
        res.insert(res.end(), currentCms.begin(), currentCms.end());
        if (deterministic && !res.empty()) {
            break;
        }
    }
    if (!res.empty()) {
        cms = res;
        errMsg = {};
    }
    return this->cms.empty() || !res.empty();
}

bool LocalTypeArgumentSynthesis::UnifyBuiltInExtension(const Tracked<Ty>& argTTy, const Tracked<InterfaceTy>& paramTTy)
{
    return UnifyNominal(argTTy, {paramTTy.ty, paramTTy.blames});
}

bool LocalTypeArgumentSynthesis::UnifyPrimitiveTy(PrimitiveTy& argTy, PrimitiveTy& paramTy)
{
    if (!tyMgr.IsSubtype(&argTy, &paramTy)) {
        return false;
    }
    if (argTy.IsIdeal() && !paramTy.IsIdeal()) {
        UpdateIdealTysInConstraints(paramTy);
    } else if (paramTy.IsIdeal() && !argTy.IsIdeal()) {
        UpdateIdealTysInConstraints(argTy);
    }
    return true;
}

void LocalTypeArgumentSynthesis::UpdateIdealTysInConstraints(PrimitiveTy& tgtTy)
{
    if (cms.size() != 1) {
        return;
    }
    Constraint& c = cms.front().constraint;
    if (!Ty::IsTyCorrect(this->curTyVar)) {
        return;
    }

    Ptr<Ty> idealInt = TypeManager::GetPrimitiveTy(TypeKind::TYPE_IDEAL_INT);
    Ptr<Ty> idealFloat = TypeManager::GetPrimitiveTy(TypeKind::TYPE_IDEAL_FLOAT);
    auto& lbs = c[this->curTyVar].lbs;
    // Actually only one of the contains is true otherwise errors will be reported beforehand when checking
    // argTy <: paramTy and the program will not run up to here.
    if (Utils::In(idealInt, lbs)) {
        lbs.erase(idealInt);
        lbs.insert(&tgtTy);
    } else if (Utils::In(idealFloat, lbs)) {
        lbs.erase(idealFloat);
        lbs.insert(&tgtTy);
    }

    auto& ubs = c[this->curTyVar].ubs;
    if (Utils::In(idealInt, ubs)) {
        ubs.erase(idealInt);
        ubs.insert(&tgtTy);
    } else if (Utils::In(idealFloat, ubs)) {
        ubs.erase(idealFloat);
        ubs.insert(&tgtTy);
    }
}

bool LocalTypeArgumentSynthesis::UnifyBuiltInTy(const Tracked<Ty>& argTTy, const Tracked<Ty>& paramTTy)
{
    // Array/CPointer type must have exactly one type argument by definition.
    // TypeArgument of these built-in type are invariant.
    if (argTTy.ty.IsTyArgsSingleton() && paramTTy.ty.IsTyArgsSingleton()) {
        return UnifyOne({*argTTy.ty.typeArgs[0], argTTy.blames}, {*paramTTy.ty.typeArgs[0], paramTTy.blames}) &&
            UnifyOne({*paramTTy.ty.typeArgs[0], paramTTy.blames}, {*argTTy.ty.typeArgs[0], argTTy.blames});
    } else {
        return false;
    }
}

bool LocalTypeArgumentSynthesis::UnifyParamIntersectionTy(
    const Tracked<Ty>& argTTy, const Tracked<IntersectionTy>& paramTTy)
{
    std::optional<StableTys> st;
    auto [tyL, tyR] = GetMaybeStableIters(paramTTy.ty.tys, st);
    // A <: B & C holds if A <: B AND A <: C holds
    for (auto ty = tyL; ty != tyR; ++ty) {
        if (!UnifyAndTrim(cms, argTTy, {**ty, paramTTy.blames})) {
            return false;
        }
    }
    return true;
}

bool LocalTypeArgumentSynthesis::UnifyArgIntersectionTy(
    const Tracked<IntersectionTy>& argTTy, const Tracked<Ty>& paramTTy)
{
    auto& argTy = argTTy.ty;
    if (argTy.tys.empty()) {
        return UnifyOne({*tyMgr.GetAnyTy(), {}}, paramTTy);
    } else if (argTy.tys.size() == 1) {
        return UnifyOne({**argTy.tys.begin(), argTTy.blames}, paramTTy);
    } // else: see below

    // A & B <: C holds if either A <: C OR B <: C.
    ConstraintWithMemos res;
    std::optional<StableTys> st;
    auto [tyL, tyR] = GetMaybeStableIters(argTy.tys, st);
    for (auto ty = tyL; ty != tyR; ++ty) {
        auto [newCMS, msg] = Unify(cms, {**ty, argTTy.blames}, paramTTy);
        MaybeSetErrMsg(msg);
        res.insert(res.end(), newCMS.begin(), newCMS.end());
        if (deterministic && !res.empty()) {
            break;
        }
    }
    return VerifyAndSetCMS(res);
}

bool LocalTypeArgumentSynthesis::UnifyParamUnionTy(const Tracked<Ty>& argTTy, const Tracked<UnionTy>& paramTTy)
{
    auto& argTy = argTTy.ty;
    auto& paramTy = paramTTy.ty;
    // A <: B V C holds if A <: B OR A <: C holds
    if (paramTy.tys.empty()) {
        return argTy.IsNothing();
    } else if (paramTy.tys.size() == 1) {
        return UnifyOne(argTTy, {**paramTy.tys.begin(), paramTTy.blames});
    } // else: see below

    ConstraintWithMemos res;
    std::optional<StableTys> st;
    auto [tyL, tyR] = GetMaybeStableIters(paramTy.tys, st);
    for (auto ty = tyL; ty != tyR; ++ty) {
        auto [newCMS, msg] = Unify(cms, argTTy, {**ty, paramTTy.blames});
        MaybeSetErrMsg(msg);
        res.insert(res.end(), newCMS.begin(), newCMS.end());
        if (deterministic && !res.empty()) {
            break;
        }
    }

    return VerifyAndSetCMS(res);
}

bool LocalTypeArgumentSynthesis::UnifyArgUnionTy(const Tracked<UnionTy>& argTTy, const Tracked<Ty>& paramTTy)
{
    std::optional<StableTys> st;
    auto [tyL, tyR] = GetMaybeStableIters(argTTy.ty.tys, st);
    // A V B <: C holds if A <: C AND B <: C holds
    for (auto ty = tyL; ty != tyR; ++ty) {
        if (!UnifyAndTrim(cms, {**ty, argTTy.blames}, paramTTy)) {
            return false;
        }
    }
    return true;
}

std::optional<TypeSubst> LocalTypeArgumentSynthesis::SolveConstraints(bool allowPartial)
{
    TypeSubsts substs;

    std::for_each(cms.begin(), cms.end(), [this, &substs, &allowPartial](const ConstraintWithMemo& cm) {
        TypeSubst subst;
        // Build a type variables' dependency graph for topological sorting from the constraints.
        auto tyVarConstraintGraph = TyVarConstraintGraph(cm.constraint, argPack.tyVarsToSolve, tyMgr);
        while (true) {
            auto thisM = tyVarConstraintGraph.TopoOnce(cm.constraint);
            if (thisM.empty()) {
                break;
            }
            // Update 'lbs' and 'ubs' in constraints with solved typeMapping.
            thisM = ApplyTypeSubstForCS(subst, thisM);
            if (auto optThisSubst = FindSolution(thisM, cm.hasNothingTy, cm.hasAnyTy)) {
                // Substitute the graph with the newly solved ones.
                tyVarConstraintGraph.ApplyTypeSubst(*optThisSubst);
                subst.merge(*optThisSubst);
            } else {
                return;
            }
        }
        if (allowPartial || !HasUnsolvedTyVars(subst)) {
            substs.insert(subst);
        }
    });

    if (!substs.empty()) {
        errMsg = {};
    }
    return GetBestSolution(substs, allowPartial);
}

namespace {
Ptr<Ty> MeetUpperBounds(TypeManager& tyMgr, Ptr<TyVar> tyVar, const UpperBounds& ubs, const TyVars& ignoredTyVars)
{
    // Classify the upperbound into tys which is a generic type with 'tyVar' in its typeArgs and other tys.
    // eg: T <: Interface<T>
    // First calculate meet result with ty without tyVars. If there exists valid result 'tyM',
    // than instantiating 'tysWithTyVar' with the mapping "tyVar -> tyM", and calculate final meet result
    // using substituted tys and 'tyM'.
    Ptr<Ty> tyM = nullptr; // Must set by 'SetMetType'.
    std::set<Ptr<Ty>> tysWithoutTyVar;
    std::set<Ptr<Ty>> tysWithTyVar;
    // Step 1, classify tys.
    std::for_each(ubs.begin(), ubs.end(),
        [&](auto ty) { ty->Contains(tyVar) ? tysWithTyVar.emplace(ty) : tysWithoutTyVar.emplace(ty); });
    auto meetRes = JoinAndMeet(tyMgr, tysWithoutTyVar, ignoredTyVars).MeetAsVisibleTy();
    JoinAndMeet::SetMetType(tyM, meetRes);
    if (Ty::IsTyCorrect(tyM) && !tysWithTyVar.empty()) {
        tysWithoutTyVar.clear();
        // Step 2, substitute tys with the 'tyVar'.
        for (auto& it : tysWithTyVar) {
            tysWithoutTyVar.emplace(tyMgr.GetInstantiatedTy(it, {std::make_pair(tyVar, tyM)}));
        }
        tysWithoutTyVar.emplace(tyM);
        // Step 3, meet the final result.
        // For the case 'T <: Interface<T>', the valid meet result will only be the given 'tyM',
        // the result will never be any of the instantiated ty substituted in step 2.
        meetRes = JoinAndMeet(tyMgr, tysWithoutTyVar, ignoredTyVars).MeetAsVisibleTy();
        JoinAndMeet::SetMetType(tyM, meetRes);
    }
    return tyM;
}
} // namespace

std::optional<TypeSubst> LocalTypeArgumentSynthesis::FindSolution(
    Constraint& thisM, const bool hasNothingTy, const bool hasAnyTy)
{
    TypeSubst thisSubst;
    bool newInfo;
    SolvingErrInfo msg;
    do {
        newInfo = false;
        TyVars tyVarsOfThisM = Utils::GetKeys(thisM);
        std::optional<StableTyVars> st;
        auto [tyL, tyR] = GetMaybeStableIters(tyVarsOfThisM, st);
        for (auto it = tyL; it != tyR; ++it) {
            Ptr<TyVar> tyVar = *it;
            if (needDiagMsg && thisM[tyVar].lbs.empty() && thisM[tyVar].ubs.empty()) {
                msg = MakeMsgNoConstraint(*tyVar);
                break;
            }

            Ptr<Ty> tyJ{};
            auto joinRes = JoinAndMeet(tyMgr, thisM[tyVar].lbs, tyVarsOfThisM).JoinAsVisibleTy();
            JoinAndMeet::SetJoinedType(tyJ, joinRes);
            Ptr<Ty> tyM = MeetUpperBounds(tyMgr, tyVar, thisM[tyVar].ubs, tyVarsOfThisM);
            bool validAnyTy = hasAnyTy || (deterministic && thisM[tyVar].ubs.count(tyMgr.GetAnyTy()) > 0);
            bool validNothingTy =
                hasNothingTy || (deterministic && thisM[tyVar].lbs.count(TypeManager::GetNothingTy()) > 0);
            if (IsValidSolution(*tyJ, validNothingTy, validAnyTy)) {
                thisSubst.emplace(std::make_pair(tyVar, tyJ));
                newInfo = true;
                thisM.erase(tyVar);
            } else if (tyJ->HasIdealTy() && !tyM->IsNumeric()) {
                tyMgr.ReplaceIdealTy(&tyJ);
                thisSubst.emplace(std::make_pair(tyVar, tyJ));
                newInfo = true;
                thisM.erase(tyVar);
            } else if (IsValidSolution(*tyM, validNothingTy, validAnyTy)) {
                thisSubst.emplace(std::make_pair(tyVar, tyM));
                newInfo = true;
                thisM.erase(tyVar);
            } else if (tyM->HasIdealTy()) {
                tyMgr.ReplaceIdealTy(&tyM);
                thisSubst.emplace(std::make_pair(tyVar, tyM));
                newInfo = true;
                thisM.erase(tyVar);
            } else if (needDiagMsg) {
                StableTys lbSt(thisM[tyVar].lbs.begin(), thisM[tyVar].lbs.end());
                StableTys ubSt(thisM[tyVar].ubs.begin(), thisM[tyVar].ubs.end());
                std::vector<Tracked<Ty>> lbs;
                for (auto lb : lbSt) {
                    lbs.push_back({*lb, thisM[tyVar].lb2Blames[lb]});
                }
                std::vector<Tracked<Ty>> ubs;
                for (auto ub : ubSt) {
                    ubs.push_back({*ub, thisM[tyVar].ub2Blames[ub]});
                }
                msg = MakeMsgConflictingConstraints(*tyVar, lbs, ubs);
            }
        }
        auto newThisM = ApplyTypeSubstForCS(thisSubst, thisM);
        thisM = newThisM;
    } while (newInfo);
    if (errMsg.style == SolvingErrStyle::DEFAULT) {
        errMsg = msg;
    }
    return {thisSubst};
}

bool LocalTypeArgumentSynthesis::IsValidSolution(const Ty& ty, const bool hasNothingTy, const bool hasAnyTy) const
{
    bool solution = !ty.HasInvalidTy() && !ty.IsNothing() && !ty.IsAny() && !ty.HasIdealTy() && !ty.IsCType();
    solution = solution || (hasNothingTy && ty.IsNothing());
    solution = solution || (hasAnyTy && ty.IsAny());
    return solution;
}

bool LocalTypeArgumentSynthesis::HasUnsolvedTyVars(const TypeSubst& subst)
{
    auto tyVars = argPack.tyVarsToSolve;
    // A valid solution should constain substitution for all of type variables
    // and each substituted type should not contain any of type variable.
    return std::any_of(tyVars.begin(), tyVars.end(), [&subst](auto& tyVar) {
        return !Utils::InKeys(tyVar, subst) ||
            std::any_of(subst.begin(), subst.end(), [tyVar](auto it) { return it.second->Contains(tyVar); });
    });
}

size_t LocalTypeArgumentSynthesis::CountUnsolvedTyVars(const TypeSubst& subst)
{
    auto tyVars = argPack.tyVarsToSolve;
    size_t counter = 0;
    for (auto& tyVar: tyVars) {
        if (!Utils::InKeys(tyVar, subst) ||
            std::any_of(subst.begin(), subst.end(), [tyVar](auto it) { return it.second->Contains(tyVar); })) {
            counter++;
        }
    };
    return counter;
}

bool LocalTypeArgumentSynthesis::DoesCSCoverAllTyVars(const Constraint& m)
{
    auto tyVars = argPack.tyVarsToSolve;
    return std::all_of(tyVars.begin(), tyVars.end(),
        [&m](auto tyVar) { return Utils::InKeys(tyVar, m) && (!m.at(tyVar).lbs.empty() || !m.at(tyVar).ubs.empty()); });
}

std::optional<TypeSubst> LocalTypeArgumentSynthesis::GetBestSolution(const TypeSubsts& substs, bool allowPartial)
{
    // Here requires a function which compares the input substitutions and select the best one (if exists).
    // The best one is the one in which the instantiated types are subtypes of other solutions.
    // For example: given D <: C, then [X |-> D] is better than [X |-> C]. A counter example: given
    // [X |-> D, Y |-> C] and [X |-> C, Y |-> D], no one is better than the other; hence there is no best solution.
    if (substs.empty() || argPack.tyVarsToSolve.empty()) {
        return {};
    }
    if (substs.size() == 1) {
        return {*substs.begin()};
    }
    // Caller guarantees all elements in 'substs' have all tyVarsToSolve.
    std::vector<TypeSubst> candidates(substs.begin(), substs.end());
    std::vector<bool> maximals(candidates.size(), true);
    if (allowPartial) {
        std::vector<size_t> unsolvedCount;
        for (auto& tySub: candidates) {
            unsolvedCount.push_back(CountUnsolvedTyVars(tySub));
        }
        auto minCount = *std::min_element(unsolvedCount.begin(), unsolvedCount.end());
        for (size_t i = 0; i < unsolvedCount.size(); i++) {
            if (unsolvedCount[i] > minCount) {
                maximals[i] = false;
            }
        }
    }
    for (auto tyVar : argPack.tyVarsToSolve) {
        CompareCandidates(tyVar, candidates, maximals);
    }
    if (auto idx = GetBestIndex(maximals)) {
        return {candidates[*idx]};
    } else {
        return {};
    }
}

void LocalTypeArgumentSynthesis::CompareCandidates(
    Ptr<TyVar> tyVar, const std::vector<TypeSubst>& candidates, std::vector<bool>& maximals)
{
    auto checkForNumeric = [&maximals](auto& tyI, auto& tyJ, size_t i, size_t j) {
        auto res = TypeCheckUtil::CompareIntAndFloat(tyI, tyJ);
        if (res == TypeCheckUtil::ComparisonRes::GT) {
            maximals[i] = false;
        } else if (res == TypeCheckUtil::ComparisonRes::LT) {
            maximals[j] = false;
        }
    };
    for (size_t i = 0; i < candidates.size(); ++i) {
        if (!maximals[i]) {
            continue;
        }
        auto tyI = tyMgr.GetInstantiatedTy(tyVar, candidates[i]);
        CJC_NULLPTR_CHECK(tyI);
        for (size_t j = i + 1; j < candidates.size(); ++j) {
            if (!maximals[j]) {
                continue;
            }
            auto tyJ = tyMgr.GetInstantiatedTy(tyVar, candidates[j]);
            CJC_NULLPTR_CHECK(tyJ);
            if (tyI->IsNumeric() && tyJ->IsNumeric()) {
                // If candidates are numberic types, comparing them with built-in comparator.
                checkForNumeric(*tyI, *tyJ, i, j);
            } else if (!tyMgr.IsSubtype(tyI, tyJ)) {
                maximals[i] = false;
            } else if (!tyMgr.IsSubtype(tyJ, tyI)) {
                maximals[j] = false;
            }
            if (!maximals[i]) {
                break;
            }
        }
    }
}

std::optional<size_t> LocalTypeArgumentSynthesis::GetBestIndex(const std::vector<bool>& maximals) const
{
    std::vector<size_t> res;
    for (size_t i = 0; i < maximals.size(); i++) {
        if (maximals.at(i)) {
            res.push_back(i);
        }
    }
    if (res.size() == 1) {
        return {res.front()};
    } else {
        return {};
    }
}

void TyVarConstraintGraph::PreProcessConstraintGraph(const Constraint& m, const TyVars& mayUsedTyVars)
{
    for (const auto& constraint : m) {
        if (mayUsedTyVars.count(constraint.first) == 0) {
            continue;
        }
        usedTyVars.emplace(constraint.first);
        for (const auto& lb : constraint.second.lbs) {
            for (auto& lbGen : lb->GetGenericTyArgs(mayUsedTyVars)) {
                usedTyVars.emplace(lbGen);
                if (edges[lbGen].count(constraint.first) == 0) {
                    indegree[constraint.first]++;
                    edges[lbGen].emplace(constraint.first);
                }
            }
        }
        for (const auto& ub : constraint.second.ubs) {
            for (auto& ubGen : ub->GetGenericTyArgs(mayUsedTyVars)) {
                usedTyVars.emplace(ubGen);
                if (edges[ubGen].count(constraint.first) == 0) {
                    indegree[constraint.first]++;
                    edges[ubGen].emplace(constraint.first);
                }
            }
        }
    }
    for (const auto& usedKey : usedTyVars) {
        if (indegree.count(usedKey) == 0) {
            indegree[usedKey] = 0;
        }
        isVisited[usedKey] = false;
    }
}
Constraint TyVarConstraintGraph::TopoOnce(const Constraint& m)
{
    if (!hasNext) {
        return Constraint();
    }
    Constraint solvedConstraints;
    for (const auto& ty : std::as_const(indegree)) {
        if (ty.second == 0 && solvedTyVars.count(ty.first) == 0) {
            solvedTyVars.emplace(ty.first);
            if (auto found = m.find(ty.first); found != m.cend()) {
                solvedConstraints[ty.first] = found->second;
            }
            isVisited[ty.first] = true;
        }
    }
    if (solvedTyVars.size() == usedTyVars.size()) {
        // all constraints are solved.
        hasNext = false;
        return solvedConstraints;
    }
    if (solvedConstraints.empty()) {
        // contains loop.
        for (const auto& ty : std::as_const(indegree)) {
            if (ty.second != 1) {
                continue;
            }
            FindLoopConstraints(m, ty.first, solvedConstraints);
            if (!solvedConstraints.empty()) {
                break;
            }
        }
    }
    for (const auto& solvedConstraint : std::as_const(solvedConstraints)) {
        for (const auto& e : edges[solvedConstraint.first]) {
            indegree[e]--;
        }
    }
    return solvedConstraints;
}

void TyVarConstraintGraph::FindLoopConstraints(const Constraint& m, Ptr<TyVar> start, Constraint& tyVarsInLoop)
{
    std::stack<Ptr<TyVar>> loopPath;
    if (HasLoop(start, loopPath)) {
        while (!loopPath.empty()) {
            solvedTyVars.emplace(loopPath.top());
            tyVarsInLoop[loopPath.top()] = m.at(loopPath.top());
            loopPath.pop();
        }
    }
}

bool TyVarConstraintGraph::HasLoop(Ptr<TyVar> start, std::stack<Ptr<TyVar>>& loopPath)
{
    if (isVisited[start]) {
        return true;
    }
    loopPath.push(start);
    isVisited[start] = true;
    for (auto const& out : edges[start]) {
        if (HasLoop(out, loopPath)) {
            return true;
        }
    }
    isVisited[start] = false;
    loopPath.pop();
    return false;
}

TypeSubst LocalTypeArgumentSynthesis::ResetIdealTypesInSubst(TypeSubst& m)
{
    TypeSubst res;
    for (const auto& pair : std::as_const(m)) {
        Ptr<TyVar> tyVar = pair.first;
        Ptr<Ty> instTy = pair.second;
        tyMgr.ReplaceIdealTy(&instTy);
        res.emplace(tyVar, instTy);
    }
    return res;
}

Constraint LocalTypeArgumentSynthesis::ApplyTypeSubstForCS(const TypeSubst& subst, const Constraint& cs)
{
    Constraint res;
    for (auto& it : cs) {
        Ptr<TyVar> tyVar = it.first;
        TyVarBounds newBounds;
        for (auto lb : it.second.lbs) {
            auto newLb = tyMgr.GetInstantiatedTy(lb, subst);
            newBounds.lbs.insert(newLb);
            if (it.second.lb2Blames.count(lb) > 0) {
                newBounds.lb2Blames[newLb] = it.second.lb2Blames.at(lb);
            }
        }
        for (auto ub : it.second.ubs) {
            auto newUb = tyMgr.GetInstantiatedTy(ub, subst);
            newBounds.ubs.insert(newUb);
            if (it.second.ub2Blames.count(ub) > 0) {
                newBounds.ub2Blames[newUb] = it.second.ub2Blames.at(ub);
            }
        }
        res.emplace(tyVar, newBounds);
    }
    return res;
}

SolvingErrInfo LocalTypeArgumentSynthesis::GetErrInfo()
{
    /* Recover names as context ty vars. */
    if (errMsg.tyVar) {
        errMsg.tyVar = StaticCast<TyVar*>(tyMgr.RecoverUnivTyVar(errMsg.tyVar));
    }
    for (auto& ty: errMsg.lbs) {
        ty = tyMgr.RecoverUnivTyVar(ty);
    }
    for (auto& ty: errMsg.ubs) {
        ty = tyMgr.RecoverUnivTyVar(ty);
    }
    return errMsg;
}

std::pair<std::set<Ptr<Ty>>::iterator, std::set<Ptr<Ty>>::iterator> LocalTypeArgumentSynthesis::GetMaybeStableIters(
    const std::set<Ptr<Ty>>& s, std::optional<StableTys>& ss) const
{
    auto tyL = s.cbegin();
    auto tyR = s.cend();
    if (needDiagMsg) {
        ss = {StableTys(tyL, tyR)};
        tyL = ss.value().cbegin();
        tyR = ss.value().cend();
    }
    return {tyL, tyR};
}

std::pair<TyVars::iterator, TyVars::iterator> LocalTypeArgumentSynthesis::GetMaybeStableIters(
    const TyVars& s, std::optional<StableTyVars>& ss) const
{
    auto tyL = s.cbegin();
    auto tyR = s.cend();
    if (needDiagMsg) {
        ss = {StableTyVars(tyL, tyR)};
        tyL = ss.value().cbegin();
        tyR = ss.value().cend();
    }
    return {tyL, tyR};
}

SolvingErrInfo LocalTypeArgumentSynthesis::MakeMsgConflictingConstraints(
    TyVar& v, const std::vector<Tracked<AST::Ty>>& lbTTys, const std::vector<Tracked<AST::Ty>>& ubTTys) const
{
    auto ret = SolvingErrInfo{.style = SolvingErrStyle::CONFLICTING_CONSTRAINTS, .tyVar = &v};
    for (auto tty : lbTTys) {
        ret.lbs.emplace_back(&tty.ty);
        ret.blames.push_back(tty.blames);
    }
    for (auto tty : ubTTys) {
        ret.ubs.emplace_back(&tty.ty);
        ret.blames.push_back(tty.blames);
    }
    return ret;
}

SolvingErrInfo LocalTypeArgumentSynthesis::MakeMsgNoConstraint(TyVar& v) const
{
    return {
        .style = SolvingErrStyle::NO_CONSTRAINT,
        .tyVar = &v
    };
}

SolvingErrInfo LocalTypeArgumentSynthesis::MakeMsgMismatchedArg(const Blame& blame) const
{
    return {
        .style = SolvingErrStyle::ARG_MISMATCH,
        .blames = {{blame}}
    };
}

SolvingErrInfo LocalTypeArgumentSynthesis::MakeMsgMismatchedRet(const Blame& blame) const
{
    return {
        .style = SolvingErrStyle::RET_MISMATCH,
        .blames = {{blame}}
    };
}

void LocalTypeArgumentSynthesis::MaybeSetErrMsg(const SolvingErrInfo& s)
{
    if (needDiagMsg && errMsg.style == SolvingErrStyle::DEFAULT) {
        errMsg = s;
    }
}

bool TypeChecker::TypeCheckerImpl::Unify(Constraint& cst, AST::Ty& argTy, AST::Ty& paramTy)
{
    return LocalTypeArgumentSynthesis::Unify(typeManager, cst, argTy, paramTy);
}

std::optional<TypeSubst> TypeChecker::TypeCheckerImpl::SolveConstraints(const Constraint& cst)
{
    return LocalTypeArgumentSynthesis::SolveConstraints(typeManager, cst);
}

bool LocalTypeArgumentSynthesis::Unify(
    TypeManager& tyMgr, Constraint& cst, AST::Ty& argTy, AST::Ty& paramTy)
{
    LocTyArgSynArgPack dummyArgPack = {
        {}, {}, {}, {}, TypeManager::GetInvalidTy(), TypeManager::GetInvalidTy(), Blame()};
    auto synIns = LocalTypeArgumentSynthesis(tyMgr, dummyArgPack, {}, false);
    synIns.cms = {{cst}};
    synIns.deterministic = true;
    if (synIns.UnifyOne({argTy, {}}, {paramTy, {}})) {
        CJC_ASSERT(synIns.cms.size() > 0);
        cst = synIns.cms[0].constraint;
        return true;
    }
    return false;
}

std::optional<TypeSubst> LocalTypeArgumentSynthesis::SolveConstraints(TypeManager& tyMgr, const Constraint& cst)
{
    LocTyArgSynArgPack dummyArgPack = {
        tyMgr.GetUnsolvedTyVars(), {}, {}, {}, TypeManager::GetInvalidTy(), TypeManager::GetInvalidTy(), Blame()};
    auto synIns = LocalTypeArgumentSynthesis(tyMgr, dummyArgPack, {}, false);
    synIns.cms = {{cst}};
    synIns.deterministic = true;
    return synIns.SolveConstraints(true);
}

bool LocalTypeArgumentSynthesis::IsGreedySolution(const TyVar& tv, const Ty& bound, bool isUpperbound)
{
    // the bound is universal ty var
    bool tyParam = bound.IsGeneric() && !bound.IsPlaceholder();
    // the bound is placeholder ty var, and depth is no deeper than this one
    // NOTE: if the bound's ty var is introduced in a deeper scope, it will leak out of its scope if used as a solution
    bool outerTyVar =
        bound.IsPlaceholder() && (tyMgr.ScopeDepthOfTyVar(StaticCast<TyVar&>(bound)) <= tyMgr.ScopeDepthOfTyVar(tv));
    // the bound doesn't have inheritance
    bool finalType = (isUpperbound && bound.IsClass() && !IsInheritableClass(*StaticCast<ClassTy&>(bound).decl)) ||
        (!bound.IsGeneric() && !bound.IsClassLike() && !bound.IsAny() && !bound.IsNothing());
    // the solution must be Any/Nothing
    bool anyOrNothing = (bound.IsAny() && !isUpperbound) || (bound.IsNothing() && isUpperbound);
    return tyParam || outerTyVar || finalType || anyOrNothing;
}
