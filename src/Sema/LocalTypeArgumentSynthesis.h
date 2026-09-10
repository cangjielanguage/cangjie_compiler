// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares a class for calculating type arguments for function calls.
 */

#ifndef CANGJIE_SEMA_LOCALTYPEARGUMENTSYNTHESIS_H
#define CANGJIE_SEMA_LOCALTYPEARGUMENTSYNTHESIS_H

#include "cangjie/AST/ASTContext.h"
#include "cangjie/AST/Types.h"
#include "cangjie/Sema/CommonTypeAlias.h"
#include "cangjie/Sema/TypeManager.h"

namespace Cangjie {
struct LocTyArgSynArgPack {
    TyVars tyVarsToSolve;
    std::vector<AST::ModalTy> argTys;
    std::vector<AST::ModalTy> paramTys;
    std::vector<Blame> argBlames;
    AST::ModalTy funcRetTy = {};   // Nullable
    AST::ModalTy retTyUB = {}; // Nullable
    Blame retBlame;
};

using MemoForUnifiedTys = std::set<std::pair<AST::ModalTy, AST::ModalTy>>;

class LocalTypeArgumentSynthesis {
    struct ConstraintWithMemo {
        Constraint constraint;
        MemoForUnifiedTys memo;
        bool hasNothingTy;
        bool hasAnyTy;
    };
    using ConstraintWithMemos = std::vector<ConstraintWithMemo>;

    template <typename T>
    struct Tracked {
        T& ty;
        std::set<Blame> blames;
    };

public:
    /* If needDiagMsg is true, blames have to be provided in arg pack. The solving will also use a less efficient but
     * stable version that guarantees a stable error message if type args can't be solved. */
    LocalTypeArgumentSynthesis(
        TypeManager& tyMgr, const LocTyArgSynArgPack& argPack, const GCBlames& gcBlames, bool needDiagMsg = false)
        : tyMgr(tyMgr), argPack(argPack), gcBlames(gcBlames), needDiagMsg(needDiagMsg)
    {
        if (!needDiagMsg) {
            this->argPack.argBlames = std::vector<Blame>(argPack.argTys.size(), Blame());
        }
    }
    ~LocalTypeArgumentSynthesis();
    // The main function that synthesize type arguments to a generic function call.
    // allowPartial: whether partial solutions should be returnd. By default no.
    std::optional<TypeSubst> SynthesizeTypeArguments(bool allowPartial = false);

    bool HasUnsolvedTyVars(const TypeSubst& subst);
    size_t CountUnsolvedTyVars(const TypeSubst& subst);
    SolvingErrInfo GetErrInfo();

    // there's also a wrapper in TypeCheckerImpl. that one is recommended
    static bool Unify(TypeManager& tyMgr, Constraint& cst, AST::ModalTy argTy, AST::ModalTy paramTy);
    static std::optional<TypeSubst> SolveConstraints(TypeManager& tyMgr, const Constraint& cst);

private:
    TypeManager& tyMgr;
    LocTyArgSynArgPack argPack;
    ConstraintWithMemos cms{};
    // The current type variable for which we are establishing constraints.
    Ptr<TyVar> curTyVar = nullptr;
    const GCBlames& gcBlames; // reference to context, for initializing upperbounds from generic constraints
    GCBlames gcBlamesInst{}; // same as above, but with unsolved ty vars substituted by the instantiated version
    SolvingErrInfo errMsg; // final error message
    bool needDiagMsg;
    bool deterministic = false;

    Constraint InitConstraints(const TyVars& tyVarsToSolve);
    void InsertConstraint(Constraint& c, TyVar& tyVar, TyVarBounds& tvb) const;

    // Unify two types by imposing subtyping relation argTy <: paramTy and generate corresponding constraints.
    // The function accept constraints and returns new ones without modifying the existing ones, which eases the work
    // of the caller, since in certain situations, the caller needs to try to unify different pairs of argTy and paramTy
    // from the same state (i.e., cms) and only preserves valid ones.
    // The string part in return value is potential error message.
    std::pair<ConstraintWithMemos, SolvingErrInfo> Unify(
        const ConstraintWithMemos& newCMS, const Tracked<AST::ModalTy>& argTTy, const Tracked<AST::ModalTy>& paramTTy);
    // Directly merge result of sub-Unify with cms & errMsg of parent instance. Used in cases where failure of
    // a sub-Unify also indicates the failure of the parent Unify.
    bool UnifyAndTrim(
        const ConstraintWithMemos& curCMS, const Tracked<AST::ModalTy>& argTTy, const Tracked<AST::ModalTy>& paramTTy);

    // Return the **best** type substitution regarding the subtyping relation if it exists.
    std::optional<TypeSubst> SolveConstraints(bool allowPartial = false);

    // The UnifyXXX series have SIDE EFFECT that the Constraints m and the UnifiedTys memo will be updated.
    // Constraints m: generated constraints,
    // UnifiedTys memo: memoization of already unified types,
    // Ty argTy and Ty paramTy: two types to be unified.
    // The series of functions return false if obvious errors are detected.
    bool UnifyOne(const Tracked<AST::ModalTy>& argTTy, const Tracked<AST::ModalTy>& paramTTy);
    bool UnifyTyVar(const Tracked<AST::ModalTy>& argTTy, const Tracked<AST::ModalTy>& paramTTy);
    bool UnifyTyVarCollectConstraints(
        TyVar& tyVar, const Tracked<AST::ModalTy>& lbTTy, const Tracked<AST::ModalTy>& ubTTy);
    bool UnifyContextTyVar(const Tracked<AST::Ty>& argTTy, const Tracked<AST::Ty>& paramTTy);
    bool UnifyBuiltInTy(const Tracked<AST::Ty>& argTTy, const Tracked<AST::Ty>& paramTTy);
    // Nominal types are types that have names, defined by class, interface, enum, and struct.
    bool UnifyNominal(const Tracked<AST::Ty>& argTTy, const Tracked<AST::Ty>& paramTTy);
    // Primitive/Array types can only subtype interface types (by extensions).
    bool UnifyBuiltInExtension(const Tracked<AST::Ty>& argTTy, const Tracked<AST::InterfaceTy>& paramTTy);
    bool UnifyTupleTy(const Tracked<AST::TupleTy>& argTTy, const Tracked<AST::TupleTy>& paramTTy);
    bool UnifyFuncTy(const Tracked<AST::FuncTy>& argTTy, const Tracked<AST::FuncTy>& paramTTy);
    bool UnifyPrimitiveTy(AST::PrimitiveTy& argTy, AST::PrimitiveTy& paramTy);
    bool UnifyParamIntersectionTy(const Tracked<AST::Ty>& argTTy, const Tracked<AST::IntersectionTy>& paramTTy);
    bool UnifyArgIntersectionTy(const Tracked<AST::IntersectionTy>& argTTy, const Tracked<AST::Ty>& paramTTy);
    bool UnifyParamUnionTy(const Tracked<AST::Ty>& argTTy, const Tracked<AST::UnionTy>& paramTTy);
    bool UnifyArgUnionTy(const Tracked<AST::UnionTy>& argTTy, const Tracked<AST::Ty>& paramTTy);
    void UpdateIdealTysInConstraints(AST::PrimitiveTy& tgtTy);

    std::optional<TypeSubst> FindSolution(Constraint& thisM, const bool hasNothingTy, const bool hasAnyTy);
    bool IsValidSolution(AST::ModalTy ty, const bool hasNothingTy, const bool hasAnyTy) const;
    bool DoesCSCoverAllTyVars(const Constraint& m);
    TypeSubst ResetIdealTypesInSubst(TypeSubst& m);
    Constraint ApplyTypeSubstForCS(const TypeSubst& subst, const Constraint& cs);
    std::optional<TypeSubst> GetBestSolution(const TypeSubsts& substs, bool allowPartial = false);
    std::optional<size_t> GetBestIndex(const std::vector<bool>& maximals) const;
    void CompareCandidates(Ptr<TyVar> tyVar, const std::vector<TypeSubst>& candidates, std::vector<bool>& maximals);
    std::pair<std::set<AST::ModalTy>::iterator, std::set<AST::ModalTy>::iterator> GetMaybeStableIters(
        const std::set<AST::ModalTy>& s, std::optional<StableTys>& ss) const;
    std::pair<TyVars::iterator, TyVars::iterator> GetMaybeStableIters(
        const TyVars& s, std::optional<StableTyVars>& ss) const;

    SolvingErrInfo MakeMsgNoConstraint(TyVar& v) const;
    SolvingErrInfo MakeMsgConflictingConstraints(TyVar& v,
        const std::vector<Tracked<AST::ModalTy>>& lbTTys, const std::vector<Tracked<AST::ModalTy>>& ubTTys) const;
    SolvingErrInfo MakeMsgMismatchedArg(const Blame& blame) const;
    SolvingErrInfo MakeMsgMismatchedRet(const Blame& blame) const;
    void MaybeSetErrMsg(const SolvingErrInfo& s);

    void CopyUpperbound();

    bool IsGreedySolution(const TyVar& tv, AST::ModalTy bound, bool isUpperbound);

    bool VerifyAndSetCMS(const ConstraintWithMemos& newCMS);
};

template <> struct LocalTypeArgumentSynthesis::Tracked<AST::ModalTy> {
    AST::ModalTy ty;
    std::set<Blame> blames;
};

} // namespace Cangjie
#endif // CANGJIE_SEMA_LOCALTYPEARGUMENTSYNTHESIS_H
