// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the TyVar Constraints Solving class, which provides topological algorithm to process TyVar
 * Constraints Solving.
 */

#ifndef CANGJIE_SEMA_TYVARCONSTRAINTGRAPH_H
#define CANGJIE_SEMA_TYVARCONSTRAINTGRAPH_H

#include "cangjie/AST/ASTCasting.h"
#include "cangjie/AST/Types.h"
#include "cangjie/Sema/TypeManager.h"

namespace Cangjie {
/*
 * A graph built from constraints (type variables and their lower and upper bounds).
 * The graph records and analyses the dependency of type variables by a topological sorting.
 */
class TyVarConstraintGraph {
    // A map mapping type variables to their constraints.
public:
    TyVarConstraintGraph(const Constraint& m, const TyVars& mayUsedTyVars, TypeManager& tyMgr) : tyMgr(tyMgr)
    {
        PreProcessConstraintGraph(m, mayUsedTyVars);
    }

    // Used to build the graph.
    void PreProcessConstraintGraph(const Constraint& m, const TyVars& mayUsedTyVars);

    // The method TopoOnce tries to get a set of type variables that are most independent and can be solved first.
    Constraint TopoOnce(const Constraint& m);

    // Substitute some type variables in the graph with their instantiated types.
    void ApplyTypeSubst(const TypeSubst& subst)
    {
        std::map<Ptr<TyVar>, int> newIndegree{};
        for (auto pair : std::as_const(indegree)) {
            if (auto tv = DynamicCast<TyVar*>(tyMgr.GetInstantiatedTy(pair.first, subst))) {
                newIndegree.emplace(tv, pair.second);
            }
        }
        this->indegree = newIndegree;

        std::map<Ptr<TyVar>, TyVars> newEdges{};
        for (auto pair : std::as_const(edges)) {
            if (auto tv = DynamicCast<TyVar*>(tyMgr.GetInstantiatedTy(pair.first, subst))) {
                newEdges.emplace(tv, StaticToTyVars(tyMgr.ApplyTypeSubstForTys(subst, pair.second)));
            }
        }
        this->edges = newEdges;

        TyVars newUsedTyVars{};
        for (auto tv : usedTyVars) {
            if (auto tv2 = DynamicCast<TyVar*>(tyMgr.GetInstantiatedTy(tv, subst))) {
                newUsedTyVars.emplace(tv2);
            }
        }
        this->usedTyVars = newUsedTyVars;

        TyVars newSolvedTyVars{};
        for (auto tv : solvedTyVars) {
            if (auto tv2 = DynamicCast<TyVar*>(tyMgr.GetInstantiatedTy(tv, subst))) {
                newSolvedTyVars.emplace(tv2);
            }
        }
        this->solvedTyVars = newSolvedTyVars;
    }

private:
    void FindLoopConstraints(const Constraint& m, Ptr<TyVar> start, Constraint& tyVarsInLoop);
    bool HasLoop(Ptr<TyVar> start, std::stack<Ptr<TyVar>>& loopPath);
    std::map<Ptr<TyVar>, int> indegree;
    std::map<Ptr<TyVar>, TyVars> edges;
    std::map<Ptr<TyVar>, bool> isVisited;
    TyVars solvedTyVars;
    TyVars usedTyVars;
    bool hasNext{true};
    TypeManager& tyMgr;
};
} // namespace Cangjie
#endif // CANGJIE_SEMA_TYVARCONSTRAINTGRAPH_H
