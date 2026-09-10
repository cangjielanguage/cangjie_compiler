// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements type argument inference.
 */

#include "cangjie/AST/Match.h"
#include "TypeCheckerImpl.h"
#include "TypeCheckUtil.h"
#include "ExtraScopes.h"
#include "LocalTypeArgumentSynthesis.h"
#include "DiagSuppressor.h"
#include "Diags.h"

using namespace Cangjie;
using namespace AST;
using namespace Sema;
using namespace TypeCheckUtil;

namespace {
// for diagnose
TyVars GetUnivTyVarsToSolve(const SubstPack& maps)
{
    TyVars ret;
    for (auto [k, v] : maps.u2i) {
        ret.insert(k);
    }
    Utils::EraseIf(
        ret, [&maps](auto tv) { return Utils::InKeys(Ptr(StaticCast<TyVar*>(maps.u2i.at(tv))), maps.inst); });
    return ret;
}

// should be enough to replace only immediate ideal types
ModalTy TryReplaceIdeal(ModalTy ty)
{
    if (!ty.Ty()) {
        return ty;
    }
    if (ty->kind == TypeKind::TYPE_IDEAL_INT) {
        return {TypeManager::GetPrimitiveTy(TypeKind::TYPE_INT64), ty.Mode()};
    }
    if (ty->kind == TypeKind::TYPE_IDEAL_FLOAT) {
        return {TypeManager::GetPrimitiveTy(TypeKind::TYPE_FLOAT64), ty.Mode()};
    }
    return ty;
}

std::string MakeOneBlameHint(const Blame& blame)
{
    auto lb = TryReplaceIdeal(blame.lb);
    auto ub = TryReplaceIdeal(blame.ub);
    switch (blame.style) {
        case BlameStyle::ARGUMENT:
            return "of type '" + lb->String() +
                "', should match parameter type '" + ub->String() + "'";
        case BlameStyle::RETURN:
            return "of return type '" + lb->String() +
                "', should match expected type '" + ub->String() + "'";
        case BlameStyle::CONSTRAINT:
            return "";
        default:
            CJC_ABORT();
            return "";
    }
}

void MakeBlameMsg(DiagnosticBuilder& builder, const SolvingErrInfo& diagInfo)
{
    switch (diagInfo.style) {
        case SolvingErrStyle::NO_CONSTRAINT:
            {
                auto tyVar = StaticCast<GenericsTy*>(diagInfo.tyVar);
                builder.AddNote(*tyVar->decl, MakeRangeForDeclIdentifier(*tyVar->decl),
                    "no information known for type variable '" + tyVar->String() + "'");
                break;
            }
        case SolvingErrStyle::ARG_MISMATCH:
            {
                auto& blame = *diagInfo.blames[0].begin();
                auto lb = TryReplaceIdeal(blame.lb);
                auto ub = TryReplaceIdeal(blame.ub);
                auto subDiag = SubDiagnostic("mismatched types");
                subDiag.AddMainHint(MakeRange(blame.src->GetBegin(), blame.src->GetEnd()),
                    "of type '" + lb->String() +
                    "', impossible to match parameter type '" + ub->String() + "'");
                builder.AddNote(subDiag);
                break;
            }
        case SolvingErrStyle::RET_MISMATCH:
            {
                auto& blame = *diagInfo.blames[0].begin();
                auto lb = TryReplaceIdeal(blame.lb);
                auto ub = TryReplaceIdeal(blame.ub);
                auto subDiag = SubDiagnostic("mismatched types");
                subDiag.AddMainHint(MakeRange(blame.src->GetBegin(), blame.src->GetEnd()),
                    "of return type '" + lb->String() +
                    "', impossible to match expected type '" + ub->String() + "'");
                builder.AddNote(subDiag);
                break;
            }
        case SolvingErrStyle::CONFLICTING_CONSTRAINTS:
            {
                auto tyVar = StaticCast<GenericsTy*>(diagInfo.tyVar);
                std::string constraintsMsg;
                std::string indentStr = "\n      ";
                for (auto lb0 : diagInfo.lbs) {
                    auto lb = TryReplaceIdeal(lb0);
                    constraintsMsg +=
                        indentStr + "'" + lb->String() + " <: " + tyVar->String() + "'";
                }
                for (auto ub0 : diagInfo.ubs) {
                    auto ub = TryReplaceIdeal(ub0);
                    constraintsMsg +=
                        indentStr + "'" + tyVar->String() + " <: " + ub->String() + "'";
                }
                builder.AddNote(*tyVar->decl, MakeRangeForDeclIdentifier(*tyVar->decl),
                    "following constraints for type variable '" + tyVar->String() +
                        "' cannot be solved:" + constraintsMsg);
                size_t blameId = 0;
                for (auto lb0 : diagInfo.lbs) {
                    auto lb = TryReplaceIdeal(lb0);
                    std::string msg = "constraint '" + lb->String() + " <: " + tyVar->String() +
                        "' may come from:";
                    for (auto blame : diagInfo.blames[blameId]) {
                        auto subDiag = SubDiagnostic(msg);
                        subDiag.AddMainHint(
                            MakeRange(blame.src->GetBegin(), blame.src->GetEnd()), MakeOneBlameHint(blame));
                        builder.AddNote(subDiag);
                    }
                    blameId++;
                }
                for (auto ub0 : diagInfo.ubs) {
                    auto ub = TryReplaceIdeal(ub0);
                    std::string msg = "constraint '" + tyVar->String() + " <: " + ub->String() + "' may come from:";
                    for (auto blame : diagInfo.blames[blameId]) {
                        auto subDiag = SubDiagnostic(msg);
                        subDiag.AddMainHint(
                            MakeRange(blame.src->GetBegin(), blame.src->GetEnd()), MakeOneBlameHint(blame));
                        builder.AddNote(subDiag);
                    }
                    blameId++;
                }
                break;
            }
        case SolvingErrStyle::DEFAULT:
            break;
        default:
            CJC_ABORT();
            break;
    }
}

void DiagnoseForCallInference(DiagnosticEngine& diag, const CallExpr& ce, const FuncDecl& fd,
    const SubstPack& maps, const SolvingErrInfo& diagInfo)
{
    CJC_ASSERT(fd.GetTy());
    CJC_NULLPTR_CHECK(ce.baseFunc);
    auto tyVars = GetUnivTyVarsToSolve(maps);
    auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_unable_to_infer_generic_func, *ce.baseFunc);
    std::string typeStr = "'" + fd.GetTy().String() + "'";
    if (auto ma = DynamicCast<MemberAccess*>(ce.baseFunc.get());
        ma && ma->baseExpr && ma->baseExpr->GetTy().IsCorrect()) {
        auto reBase = As<ASTKind::REF_EXPR>(ma->baseExpr.get());
        bool isNormalRef = !reBase || (!reBase->isThis && !reBase->isSuper); // Base is not this or super.
        if (isNormalRef) {
            typeStr += " in context type '" + ma->baseExpr->GetTy().String() + "'";
        }
    }
    TyVars fdVars;
    auto fdGeneric = fd.GetGeneric();
    if (fdGeneric) {
        for (auto& tyParam : fdGeneric->typeParameters) {
            fdVars.emplace(StaticCast<TyVar*>(tyParam->DataTy()));
        }
    }
    std::string constraintStr;
    std::string fdConstraints;
    for (auto ty : tyVars) {
        if (auto gty = DynamicCast<GenericsTy*>(ty); gty && !gty->upperBounds.empty()) {
            std::string str = gty->String() + " <: " + Ty::GetTypesToStableStr(gty->upperBounds, " & ");
            if (fdVars.find(ty) != fdVars.end()) {
                fdConstraints += str + ", ";
            } else {
                constraintStr += str + ", ";
            }
        }
    }
    // Concat context constraints and function constraints in order.
    constraintStr += fdConstraints;
    if (!constraintStr.empty()) {
        typeStr += " with constraint '" + constraintStr.substr(0, constraintStr.find_last_of(',')) + "'";
    }
    if (fd.ShouldDiagnose()) {
        // too much redundant information in these cases
        if (diagInfo.style != SolvingErrStyle::CONFLICTING_CONSTRAINTS &&
            diagInfo.style != SolvingErrStyle::NO_CONSTRAINT) {
            builder.AddNote(fd, MakeRangeForDeclIdentifier(fd), "for function type " + typeStr);
        }
        MakeBlameMsg(builder, diagInfo);
    }
}

bool IsConcrete(const TyVars& tyVarsToSolve, AST::ModalTy ty)
{
    if (!Ty::IsTyCorrect(ty)) {
        return false;
    }
    auto allTyVars = ty->GetGenericTyArgs();
    return std::all_of(allTyVars.begin(), allTyVars.end(),
        [&tyVarsToSolve](ModalTy genTy) { return tyVarsToSolve.count(RawStaticCast<TyVar*>(genTy.Ty())) == 0; });
}

/**
 *  Turn unsolved TyVars in a Ty into QuestTy.
 *  Will only do so if QuestTy at the position can be supported.
 *  Otherwise a nullopt will be returned.
 */
std::optional<AST::ModalTy> UnsolvedAsQuest(TypeManager& tyMgr, const TyVars& tyVarsToSolve, AST::ModalTy ty)
{
    if (!Ty::IsTyCorrect(ty)) {
        return {ty};
    }
    if (auto funcTy = DynamicCast<FuncTy*>(ty.Ty())) {
        std::vector<ModalTy> paramTys;
        for (auto& it : funcTy->paramTys) {
            if (!IsConcrete(tyVarsToSolve, it)) {
                return std::nullopt;
            }
            paramTys.push_back(it);
        }
        if (auto retType = UnsolvedAsQuest(tyMgr, tyVarsToSolve, funcTy->retTy)) {
            Ptr<FuncTy> fin = tyMgr.GetFunctionTy(
                paramTys, *retType, {funcTy->IsCFunc(), funcTy->isClosureTy, funcTy->hasVariableLenArg});
            return {{fin, ty.Mode()}};
        }
        return std::nullopt;
    }
    if (IsConcrete(tyVarsToSolve, ty)) {
        return ty;
    }
    return {{TypeManager::GetQuestTy(), ty.Mode()}};
}

struct TyArgSynState {
    TyVars tyVarsToSolve;
    ModalTy retTarget;
    std::vector<ModalTy> argTys;
    std::unordered_set<size_t> ignoredEnumCtor;
    // bitmap for arugments failed to be synthesized
    std::vector<bool> failSet;
    // param tys with quest type, used as check target for args in failSet
    std::vector<ModalTy> questParamTys;
    // number of type vars not yet solved
    size_t unsolvedCount;
    // any new information derived from this iteration
    bool newInfo = true;
    // if iterative solving fails, and any arg is lambda, could try to infer the lambda's param types from body
    bool lastResortUnused = true;
    // the solution
    std::optional<TypeSubst> solution = {};
};

bool MayNeedNextIteration(TyArgSynState& stat, size_t newUnsolvedCount)
{
    if (!stat.solution || newUnsolvedCount == 0) {
        // final fail or success
        stat.unsolvedCount = newUnsolvedCount;
        return false;
    } else if (newUnsolvedCount >= stat.unsolvedCount) {
        // no new solved ty var
        return false;
    } else {
        stat.unsolvedCount = newUnsolvedCount;
        return true;
    }
}

bool PrepareQuestParamTy(TypeManager& tyMgr, const std::vector<ModalTy>& paramsTy, TyArgSynState& stat)
{
    // prepare param ty with new info
    bool anyNewTarget = false;
    for (size_t i = 0; i < paramsTy.size(); ++i) {
        stat.questParamTys[i] = ModalTy{};
        if (stat.failSet[i] && stat.ignoredEnumCtor.count(i) == 0) {
            // paramTy where the solution is substituted. Some placeholder type vars may not be solved.
            auto paramTyPartial =
                tyMgr.GetInstantiatedTy(paramsTy[i], *stat.solution);
            // paramTy where the unsolved type vars are replaced by QuestTy
            auto paramTyQuest = UnsolvedAsQuest(tyMgr, stat.tyVarsToSolve, paramTyPartial);
            if (paramTyQuest) {
                anyNewTarget = true;
                stat.questParamTys[i] = *paramTyQuest;
            }
        }
    }
    return anyNewTarget;
}

std::vector<ModalTy> ValidateArgTys(CallExpr& ce, TyArgSynState& stat)
{
    std::vector<ModalTy> validArgTys;
    stat.ignoredEnumCtor.clear();
    for (size_t i = 0; i < ce.args.size(); ++i) {
        // For code 'Some(None)' the sema type of 'None' needs to be inferred,
        // so it should be ignored for function type inference.
        if (ce.args[i]->expr && IsEnumCtorWithoutTypeArgs(*ce.args[i]->expr, ce.args[i]->expr->GetTarget())) {
            stat.failSet[i] = true;
            stat.ignoredEnumCtor.emplace(i);
        } else if (Ty::IsTyCorrect(stat.argTys[i])) {
            stat.failSet[i] = false;
            validArgTys.emplace_back(stat.argTys[i]);
        }
    }
    return validArgTys;
}

std::vector<Blame> MakeArgBlames(const TyArgSynState& stat, const CallExpr& ce, const std::vector<ModalTy>& argTys,
    const std::vector<ModalTy>& paramTys, TypeManager& tyMgr)
{
    CJC_ASSERT(argTys.size() == paramTys.size());
    std::vector<Blame> blames;
    size_t j = 0;
    for (size_t i = 0; i < ce.args.size(); i++) {
        if (stat.ignoredEnumCtor.count(i) == 0 && Ty::IsTyCorrect(stat.argTys[i])) {
            CJC_ASSERT(j < argTys.size()); // should be guaranteed by ValidateArgTys
            blames.push_back({.src = ce.args[i].get(),
                .lb = argTys[j],
                .ub = tyMgr.RecoverUnivTyVar(paramTys[j]),
                .style = BlameStyle::ARGUMENT});
            j++;
        }
    }
    return blames;
}

void DisableBodyInferForArgs(ASTContext& ctx, CallExpr& ce)
{
    for (auto& arg : ce.args) {
        Walker(arg.get(), [&ctx](Ptr<Node> node) {
            if (auto lam = DynamicCast<LambdaExpr>(node)) {
                ctx.funcArgReachable.emplace(lam);
                return VisitAction::SKIP_CHILDREN;
            }
            if (IsQuestableNode(*node)) {
                return VisitAction::WALK_CHILDREN;
            } else {
                return VisitAction::SKIP_CHILDREN;
            }
        }).Walk();
    }
}

bool NeedLastTry(CallExpr& ce)
{
    bool ret = false;
    for (auto& arg : ce.args) {
        Walker(arg.get(), [&ret](Ptr<Node> node) {
            if (Is<LambdaExpr>(node)) {
                ret = true;
                return VisitAction::STOP_NOW;
            }
            if (IsQuestableNode(*node)) {
                return VisitAction::WALK_CHILDREN;
            } else {
                return VisitAction::SKIP_CHILDREN;
            }
        }).Walk();
    }
    return ret;
}

void TryUpdateEnumTyByTarget(const CallExpr& ce, const FuncDecl& fd, ModalTy retTarget)
{
    if (auto ma = DynamicCast<MemberAccess*>(ce.baseFunc.get()); ma && ma->baseExpr) {
        // To handle omiting the generic parameter but it can be defined by target ty.
        // If target type of enum ctor is given, must not synthesize later. Example: var b: E<Int64> = E.e(1)
        bool updateEnumTy = IsEnumCtorWithoutTypeArgs(*ma, &fd) && Ty::GetDeclPtrOfTy(retTarget) == fd.outerDecl;
        if (updateEnumTy) {
            ma->baseExpr->SetTy(retTarget);
        }
    }
}

void ConstrainByUpperbound(TypeManager& tyMgr)
{
    for (auto& [univ, inst] : tyMgr.GetInstMapping().u2i) {
        auto instTv = RawStaticCast<GenericsTy*>(inst);
        auto& toSolve = tyMgr.GetUnsolvedTyVars();
        if (!Utils::In(instTv, toSolve)) {
            continue;
        }
        for (auto upper : univ->upperBounds) {
            CJC_NULLPTR_CHECK(upper);
            tyMgr.constraints[instTv].ubs.insert(tyMgr.InstOf({upper}).Ty());
        }
    }
}
} // namespace

std::optional<TypeSubst> TypeChecker::TypeCheckerImpl::PropagatePlaceholderAndSolve(
    ASTContext& ctx, CallExpr& ce, const std::vector<ModalTy>& paramTys, ModalTy retTy, ModalTy retTyUB)
{
    if (!NeedLastTry(ce)) {
        return {};
    }
    DiagSuppressor ds(diag);
    for (auto tv : GetTyVarsToSolve(typeManager.GetInstMapping())) {
        typeManager.MarkAsUnsolvedTyVar(*tv);
    }
    ConstrainByUpperbound(typeManager);
    if (retTyUB && !typeManager.IsSubtype(retTy, retTyUB)) {
        return {};
    }
    for (size_t i = 0; i < ce.args.size(); ++i) {
        auto wasCorrect = ce.args[i]->GetTy().IsCorrect();
        if (!CheckWithCache(ctx, paramTys[i], ce.args[i].get())) {
            if (!wasCorrect) {
                ce.args[i]->SetTy({TypeManager::GetInvalidTy()});
            }
            return {};
        }
        if (!wasCorrect) {
            ce.args[i]->SetTy({Ty::GetInitialTy()});
        }
    }
    return SolveConstraints(typeManager.constraints);
}

ErrOrSubst TypeChecker::TypeCheckerImpl::PrepareTyArgsSynthesis(
    ASTContext& ctx, const FunctionCandidate& candidate, ModalTy const retTyUB)
{
    auto& [fd, ce, argCombinations, _] = candidate;
    // Guarantees the RawStaticCast<FuncTy*> below.
    CJC_ASSERT(fd.GetTy().IsCorrect() && fd.GetTy()->IsFunc());
    DisableBodyInferForArgs(ctx, ce);
    TyArgSynState stat;
    // If args not empty, get argTys. Index 0 is set as current args' types combination.
    stat.argTys = argCombinations.empty() ? std::vector<ModalTy>{} : argCombinations[0];
    if (stat.argTys.size() != ce.args.size()) {
        return SolvingErrInfo{};
    }
    if (NeedSynOnUsed(fd)) {
        Synthesize({ctx, SynPos::NONE}, &fd);
    }
    stat.failSet = std::vector<bool>(stat.argTys.size(), true);
    stat.questParamTys = std::vector<ModalTy>(stat.argTys.size(), ModalTy{});
    stat.tyVarsToSolve = GetTyVarsToSolve(typeManager.GetInstMapping());
    stat.unsolvedCount = stat.tyVarsToSolve.size() + 1;
    // Suppressing errors generated in 'Synthesize', only report them when type inference failed.
    // If type inference succeed, all real errors in argument expression will be thrown during parameter checking.
    auto ds = DiagSuppressor(diag);
    // no need to consider any constraint generated here, since they are all tentative or local
    // any information worth considering will be re-considered during re-check of arguments
    auto cs = PData::CommitScope(typeManager.constraints);
    for (size_t i = 0; i < stat.argTys.size(); ++i) {
        // Must not re-synthesize already checked argument here.
        if (Ty::IsTyCorrect(stat.argTys[i])) {
            stat.failSet[i] = false;
        }
    }
    stat.retTarget = retTyUB;
    auto paramsTyInOrder = GetParamTysInArgsOrder(typeManager, ce, fd);
    for (size_t i = 0; i < paramsTyInOrder.size(); ++i) {
        paramsTyInOrder[i] = typeManager.InstOf(paramsTyInOrder[i]);
    }
    ModalTy funcRetTy = typeManager.InstOf(RawStaticCast<FuncTy*>(fd.DataTy())->retTy);
    LocTyArgSynArgPack argPack;

    while (stat.newInfo) {
        // 1. synthesize/check func args
        for (size_t i = 0; i < stat.argTys.size(); ++i) {
            if (stat.failSet[i] && stat.questParamTys[i].Ty()) {
                // never report error from speculative check with questParamTy
                auto ds2 = DiagSuppressor(diag);
                ce.args[i]->Clear();
                CheckWithCache(ctx, stat.questParamTys[i], ce.args[i].get());
                stat.argTys[i] = ce.args[i]->GetTy();
            } else if (Ty::IsInitialTy(stat.argTys[i].Ty())) {
                // Initially, every arg goes into this branch. If the inference eventually fails,
                // errors from this synthesize should be reported
                stat.argTys[i] = SynthesizeWithCache({ctx, SynPos::EXPR_ARG}, ce.args[i].get());
            } else if (stat.failSet[i] && !stat.lastResortUnused) {
                // If some arg is a lambda, try to infer its param type from its body,
                // as a last resort in case no enough contextual info is provided.
                auto ds3 = DiagSuppressor(diag);
                stat.argTys[i] = Synthesize({ctx, SynPos::EXPR_ARG}, ce.args[i].get());
            }
        }
        // 2. collect valid arg tys & update failSet
        std::vector<ModalTy> validArgTys = ValidateArgTys(ce, stat);
        std::vector<ModalTy> paramTys;
        for (size_t i = 0; i < paramsTyInOrder.size(); ++i) {
            if (!stat.failSet[i]) {
                paramTys.push_back(paramsTyInOrder[i]);
            }
        }
        // 3. prepare input pack & infer type args
        auto retBlame = Blame{.src = &ce,
            .lb = typeManager.RecoverUnivTyVar(funcRetTy),
            .ub = retTyUB,
            .style = BlameStyle::RETURN};
        argPack = {stat.tyVarsToSolve, validArgTys, paramTys,
            MakeArgBlames(stat, ce, validArgTys, paramTys, typeManager), funcRetTy, stat.retTarget, retBlame};
        auto synCtx = LocalTypeArgumentSynthesis(typeManager, argPack, ctx.gcBlames, false);
        stat.solution = synCtx.SynthesizeTypeArguments(true);
        size_t newUnsolvedCount = stat.solution ? synCtx.CountUnsolvedTyVars(*stat.solution) : 0;
        // 4. check if there is new info & prepare next iteration
        stat.newInfo = MayNeedNextIteration(stat, newUnsolvedCount);
        if (stat.newInfo) {
            stat.newInfo = PrepareQuestParamTy(typeManager, paramsTyInOrder, stat);
        }
        if (!stat.newInfo && stat.unsolvedCount > 0 && stat.lastResortUnused) {
            stat.lastResortUnused = false;
            auto sol = PropagatePlaceholderAndSolve(ctx, ce, paramsTyInOrder, funcRetTy, retTyUB);
            if (sol && !HasUnsolvedTyVars(*sol, stat.tyVarsToSolve)) {
                stat.solution = sol;
                stat.unsolvedCount = 0;
            }
        }
    }

    if (!stat.solution || stat.unsolvedCount > 0) {
        stat.solution = {}; // clear partially solved solution
        for (auto idx : stat.ignoredEnumCtor) {
            diag.Diagnose(*ce.args[idx]->expr, DiagKind::sema_generic_type_without_type_argument);
        }
        // inference failure will be reported later
        ds.ReportDiag();
        auto diagSyn = LocalTypeArgumentSynthesis(typeManager, argPack, ctx.gcBlames, true);
        (void)diagSyn.SynthesizeTypeArguments(false);
        return diagSyn.GetErrInfo();
    }
    PData::Reset(typeManager.constraints);
    return *stat.solution;
}

// Generate the type mapping table if the current call involves generic types. Before overload resolution, the fd may
// not be the right target, so the generation of typeMapping may fail as well. This function will return empty in this
// case.
std::vector<SubstPack> TypeChecker::TypeCheckerImpl::GenerateTypeMappingForCall(
    ASTContext& ctx, FunctionCandidate& candidate, ModalTy retTarget)
{
    auto& ce = candidate.ce;
    auto& fd = candidate.fd;
    InstCtxScope ic(*this);
    TryUpdateEnumTyByTarget(ce, fd, retTarget);
    if (!ic.SetRefDecl(ctx, fd, ce)) {
        return {};
    }
    if (HasTyVarsToSolve(typeManager.GetInstMapping())) {
        return GenerateTypeMappingByInference(ctx, candidate, retTarget);
    }
    return {typeManager.GetInstMapping()};
}

std::vector<SubstPack> TypeChecker::TypeCheckerImpl::GenerateTypeMappingByInference(
    ASTContext& ctx, const FunctionCandidate& candidate, ModalTy retTarget)
{
    auto& ce = candidate.ce;
    auto& fd = candidate.fd;
    if (!fd.GetTy().IsCorrect() || !fd.GetTy()->IsFunc()) {
        return {};
    }
    std::vector<SubstPack> typeMappings;
    size_t combinationSize = candidate.argCombinations.size();
    size_t cbIndex = 0;
    auto curCandidate = candidate;
    SolvingErrInfo diagInfo;
    do { // Solve type mappings with all possible arguments combinations.
        curCandidate.argCombinations[0] = candidate.argCombinations[cbIndex];
        cbIndex++;
        auto errOrSubst = PrepareTyArgsSynthesis(ctx, curCandidate, retTarget);
        if (std::holds_alternative<TypeSubst>(errOrSubst)) {
            // Copy context mapping and merge with solution to get complete type mapping.
            auto currentSubst = typeManager.GetInstMapping();
            auto newSubst = std::get<TypeSubst>(errOrSubst);
            typeManager.PackMapping(currentSubst, newSubst);
            typeMappings.emplace_back(currentSubst);
        } else {
            diagInfo = std::get<SolvingErrInfo>(errOrSubst);
        }
    } while (cbIndex < combinationSize);
    if (typeMappings.empty() && !Utils::In(ce.args, [](const auto& arg) { return !arg->GetTy().IsCorrect(); })) {
        DiagnoseForCallInference(diag, ce, fd, typeManager.GetInstMapping(), diagInfo);
    }
    return typeMappings;
}
