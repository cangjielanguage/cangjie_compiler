// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_ANALYSIS_RESULTS_H
#define CANGJIE_CHIR_ANALYSIS_RESULTS_H

#include "cangjie/CHIR/Analysis/Analysis.h"
#include "cangjie/CHIR/Analysis/Utils.h"
#include "cangjie/CHIR/Utils/CHIRCasting.h"
#include "cangjie/CHIR/IR/Value/Value.h"

#include <functional>

namespace Cangjie::CHIR {

template <typename Domain> struct LambdaState;
/**
 * @brief abstract results after analysis
 * @tparam Domain abstract domain which results is.
 */
template <typename Domain, typename = std::enable_if_t<std::is_base_of<AbstractDomain<Domain>, Domain>::value>>
class Results {
public:
    Results() = delete;
    /**
     * @brief constructor of abstract results for analysis
     * @param func function to analyse.
     * @param analysis analysis pass.
     * @param entrySets entry state from block to domain.
     * @param lambdaResults lambda results if func has lambda.
     */
    Results(const Func* func, std::unique_ptr<Analysis<Domain>> analysis,
        std::unique_ptr<std::unordered_map<Block*, Domain>> entrySets, std::vector<LambdaState<Domain>> lambdaResults)
        : func(func),
          analysis(std::move(analysis)),
          entrySets(std::move(entrySets)),
          lambdaResults(std::move(lambdaResults))
    {
        for (auto& lambdaRes : this->lambdaResults) {
            lambdaResultsMap.emplace(lambdaRes.lambda, lambdaRes.entrySets.get());
        }
    }

    /**
     * @brief main method to generate results
     * @param actionBeforeVisitExpr lambda function before visit.
     * @param actionAfterVisitExpr lambda function after visit.
     * @param actionOnTerminator lambda function on terminator.
     */
    void VisitWith(std::function<void(const Domain&, Expression*, size_t)> actionBeforeVisitExpr,
        std::function<void(const Domain&, Expression*, size_t)> actionAfterVisitExpr,
        std::function<void(const Domain&, Terminator*, std::optional<Block*>)> actionOnTerminator)
    {
        for (auto bb : func->GetBody()->GetBlocks()) {
            VisitBlockWith(actionBeforeVisitExpr, actionAfterVisitExpr, actionOnTerminator, *bb, entrySets.get());
        }
        for (auto& lambdaUnit : lambdaResults) {
            if (!lambdaUnit.lambda->GetBody()) {
                // This lambda maybe in a dead block and thus it has been deleted.
                continue;
            }
            for (auto bb : lambdaUnit.lambda->GetBody()->GetBlocks()) {
                VisitBlockWith(
                    actionBeforeVisitExpr, actionAfterVisitExpr, actionOnTerminator, *bb, lambdaUnit.entrySets.get());
            }
        }
    }

    const Func* func;

private:
// Add only cangjie native backend for cjmp
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    std::vector<Block*> GetBlocksToAnalyse(const Expression& expr) const
    {
        if (auto initVarFunc = TryGetInstanceVarInitFromApply(expr)) {
            return initVarFunc->GetBody()->GetBlocks();
        }

        return {};
    }

    void VisitBlockNonTerminatorExpressionsWith(
        std::function<void(const Domain&, Expression*, size_t)> actionBeforeVisitExpr,
        std::function<void(const Domain&, Expression*, size_t)> actionAfterVisitExpr,
        const Block& block, Domain& state)
    {
        auto exprs = block.GetNonTerminatorExpressions();
        for (size_t i = 0; i < exprs.size(); ++i) {
            auto& expr = exprs[i];
            actionBeforeVisitExpr(state, expr, i);
            SimulatingProcessingSingleExpression(state, expr);

            auto blocks = GetBlocksToAnalyse(*expr);
            for (auto innerBlock : blocks) {
                VisitBlockNonTerminatorExpressionsWith(actionBeforeVisitExpr, actionAfterVisitExpr, *innerBlock, state);
            }

            actionAfterVisitExpr(state, expr, i);
        }
    }
#endif

    void VisitBlockWith(std::function<void(const Domain&, Expression*, size_t)> actionBeforeVisitExpr,
        std::function<void(const Domain&, Expression*, size_t)> actionAfterVisitExpr,
        std::function<void(const Domain&, Terminator*, std::optional<Block*>)> actionOnTerminator, Block& block,
        std::unordered_map<Block*, Domain>* entryStates = nullptr)
    {
        if (!entryStates) {
            if (entrySets->find(&block) != entrySets->end()) {
                entryStates = entrySets.get();
            } else {
                auto parentLambda = StaticCast<Lambda*>(block.GetParentBlockGroup()->GetOwnerExpression());
                entryStates = lambdaResultsMap.at(parentLambda);
            }
        }
        auto it = entryStates->find(&block);
        if (it == entryStates->end()) {
            return;
        }
        auto state = it->second; // should be a copy
        if (state.IsBottom()) {
            return;
        }
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
        VisitBlockNonTerminatorExpressionsWith(actionBeforeVisitExpr, actionAfterVisitExpr, block, state);
#endif
        auto terminator = block.GetTerminator();
        if (auto lambda = IsApplyToLambda(terminator); lambda) {
            // If it's apply to a lambda, we need to clear the state of vars captured by the lambda.
            analysis->HandleVarStateCapturedByLambda(state, lambda);
        }
        auto targetSucc = analysis->PropagateTerminatorEffect(state, terminator);
        actionOnTerminator(state, terminator, targetSucc);
    }

    void SimulatingProcessingSingleExpression(Domain& state, Expression* expr)
    {
        if (expr->GetExprKind() == ExprKind::LAMBDA) {
            return analysis->PreHandleLambdaExpression(state, StaticCast<const Lambda*>(expr));
        }
        if (auto lambda = IsApplyToLambda(expr); lambda) {
            // If it's apply to a lambda, we need to clear the state of vars captured by the lambda.
            analysis->HandleVarStateCapturedByLambda(state, lambda);
        }
        analysis->PropagateExpressionEffect(state, expr);
    }

    /// The analysis we are doing.
    std::unique_ptr<Analysis<Domain>> analysis;

    /// This unordered_map collects the abstract state solution at the entry point of each block.
    std::unique_ptr<std::unordered_map<Block*, Domain>> entrySets;

    std::unordered_map<const Lambda*, std::unordered_map<Block*, Domain>*> lambdaResultsMap;
    std::vector<LambdaState<Domain>> lambdaResults;
};

} // namespace Cangjie::CHIR

#endif
