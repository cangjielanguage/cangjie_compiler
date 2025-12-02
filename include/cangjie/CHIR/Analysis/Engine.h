// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_ANALYSIS_ENGINE_H
#define CANGJIE_CHIR_ANALYSIS_ENGINE_H

#include "cangjie/CHIR/Analysis/Results.h"
#include "cangjie/CHIR/Utils/CHIRCasting.h"
#include "cangjie/CHIR/Utils/Utils.h"
#include "cangjie/CHIR/IR/Value/Value.h"

#include <deque>
#include <unordered_set>

namespace Cangjie::CHIR {

template <typename Domain> struct LambdaState {
    /**
     * @brief lambda expression to store state.
     */
    const Lambda* lambda;

    /**
     * @brief entry states of lambda expression.
     */
    std::unique_ptr<std::unordered_map<Block*, Domain>> entrySets;

    LambdaState() = delete;

    /**
     * @brief lambda state constructor.
     * @param lambda lambda to store state.
     * @param entrySets entry states of lambda.
     */
    LambdaState(const Lambda* lambda, std::unique_ptr<std::unordered_map<Block*, Domain>> entrySets)
        : lambda(lambda), entrySets(std::move(entrySets))
    {
    }

    /**
     * @brief move constructor of lambda state.
     * @param rhs state move from.
     */
    LambdaState(LambdaState&& rhs)
    {
        this->lambda = rhs.lambda;
        this->entrySets = std::move(rhs.entrySets);
    }

    /**
     * @brief move operator.
     * @param rhs state move from.
     * @return result of move operator.
     */
    LambdaState& operator=(LambdaState&& rhs)
    {
        this->lambda = rhs.lambda;
        this->entrySets = std::move(rhs.entrySets);
        return *this;
    }
};
template <typename Domain, typename = std::enable_if_t<std::is_base_of<AbstractDomain<Domain>, Domain>::value>>
class Engine {
    friend class Results<Domain>;

public:
    Engine() = delete;

    /**
     * @brief constructor of analysis framework.
     * @param func the function to analyse.
     * @param analysis_ analysis pass.
     */
    Engine(const Func* func, std::unique_ptr<Analysis<Domain>> analysis_)
        : func(func), analysis(std::move(analysis_)), entrySets(std::make_unique<std::unordered_map<Block*, Domain>>())
    {
    }

    /**
     * @brief do one iterator to analyse.
     * @return analysis results.
     */
    std::unique_ptr<Results<Domain>> IterateToFixpoint()
    {
        if (!func->GetBody() || DoesExceedBlockLimit()) {
            // the perpose of checking entry block is to skip invalid IR after function inline in CHIR
            // delele it if IR is valid after fix error in function inline
            return nullptr;
        }
        if (func->TestAttr(Attribute::SKIP_ANALYSIS)) {
            return nullptr;
        }
        for (auto bb : func->GetBody()->GetBlocks()) {
            entrySets->emplace(bb, analysis->Bottom());
        }
        auto& entrybbState = entrySets->at(func->GetEntryBlock());
        analysis->InitializeFuncEntryState(entrybbState);

#ifdef AnalysisDevDebug
        std::cout << "==================" << func->GetSrcCodeIdentifier() << ": " << analysis->GetAnalysisName()
                  << "==================" << std::endl;
        std::cout << "=======after Initialize()=======" << std::endl;
        for (auto bb : func->GetBody()->GetBlocks()) {
            std::cout << bb->GetIdentifier() << ": " << entrySets->at(bb).ToString() << std::endl;
        }
#endif
        IterateSingleUnitToFixpoint(func->GetEntryBlock(), entrySets.get());
#ifdef AnalysisDevDebug
        std::cout << "=======after IterateToFixpoint()=======" << std::endl;
        for (auto bb : func->GetBody()->GetBlocks()) {
            std::cout << bb->GetIdentifier() << ": " << entrySets->at(bb).ToString() << std::endl;
        }
        std::cout << std::endl;
#endif
        uint64_t worklistIdx = 0;
        while (worklistIdx < lambdaWorklist.size()) {
            auto& lambdaUnit = lambdaWorklist[worklistIdx];

            auto& lambda = lambdaUnit.lambda;
            analysis->UpdateCurrentLambda(lambda);
            auto lambdaEntrySets = lambdaUnit.entrySets.get();

            auto& envState = lambdaEnvState.at(lambda);
            analysis->HandleVarStateCapturedByLambda(envState, lambda);
            auto [it, _] = lambdaEntrySets->emplace(lambda->GetEntryBlock(), std::move(envState));
            analysis->InitializeLambdaEntryState(it->second);
            for (auto bb : lambda->GetBody()->GetBlocks()) {
                lambdaEntrySets->try_emplace(bb, analysis->Bottom());
            }

#ifdef AnalysisDevDebug
            std::cout << "==================" << lambda->GetSrcCodeIdentifier() << ": " << analysis->GetAnalysisName()
                      << "==================" << std::endl;
            std::cout << "=======after Initialize()=======" << std::endl;
            for (auto bb : lambda->GetBody()->GetBlocks()) {
                std::cout << bb->GetIdentifier() << ": " << lambdaEntrySets->at(bb).ToString() << std::endl;
            }
#endif
            IterateSingleUnitToFixpoint(lambda->GetEntryBlock(), lambdaEntrySets);
            ++worklistIdx;
#ifdef AnalysisDevDebug
            std::cout << "=======after IterateToFixpoint()=======" << std::endl;
            for (auto bb : lambda->GetBody()->GetBlocks()) {
                std::cout << bb->GetIdentifier() << ": " << lambdaEntrySets->at(bb).ToString() << std::endl;
            }
            std::cout << std::endl;
#endif
        }

        analysis->SetToStable();
        return std::make_unique<Results<Domain>>(
            func, std::move(analysis), std::move(entrySets), std::move(lambdaWorklist));
    }

    /**
     * @brief do one iterator to analyse func body.
     * @param entryBlock number of entry blocks.
     * @param entryStates states of entry blocks.
     */
    void IterateSingleUnitToFixpoint(Block* entryBlock, std::unordered_map<Block*, Domain>* entryStates)
    {
        std::deque<Block*> worklist = TopologicalSort(entryBlock);
        std::unordered_set<Block*> worklistSet(worklist.begin(), worklist.end());
#ifdef AnalysisDevDebug
        std::vector<Block*> temp(worklist.begin(), worklist.end());
        std::cout << "=======initialized worklist=======" << std::endl;
        for (auto bb : temp) {
            std::cout << bb->GetIdentifier() << " ";
        }
        std::cout << std::endl;
        std::cout << "=======iterating=======" << std::endl;
#endif
        auto state = analysis->Bottom();
        while (!worklist.empty()) {
            auto bb = worklist.front();
            worklist.pop_front();
            worklistSet.erase(bb);
#ifdef AnalysisDevDebug
            std::cout << "processing " << bb->GetIdentifier() << std::endl;
#endif
            state = entryStates->at(bb); // should not have `&`
            if (state.IsBottom()) {
                continue;
            }
#ifdef AnalysisDevDebug
            std::cout << "entry: " << state.ToString() << std::endl;
#endif
            auto exprs = bb->GetExpressions();
            if (exprs.empty()) {
                continue;
            }
            auto terminator = StaticCast<Terminator*>(exprs.back());
            exprs.pop_back();

            for (auto exp : exprs) {
                if (exp->GetExprKind() == ExprKind::LAMBDA) {
                    auto lambda = StaticCast<const Lambda*>(exp);
                    analysis->PreHandleLambdaExpression(state, lambda);
                    if (auto it = lambdaEnvState.find(lambda); it != lambdaEnvState.end()) {
                        it->second = state; // should be a copy assignment
                    } else {
                        lambdaEnvState.emplace(lambda, state);
                    }
                    if (lambdaWorklistSet.find(lambda) == lambdaWorklistSet.end()) {
                        lambdaWorklistSet.emplace(lambda);
                        lambdaWorklist.emplace_back(lambda, std::make_unique<std::unordered_map<Block*, Domain>>());
                    }
                } else {
                    // If it's apply to a lambda, we need to clear the state of vars captured by the lambda.
                    if (auto lambda = IsApplyToLambda(exp); lambda) {
                        analysis->HandleVarStateCapturedByLambda(state, lambda);
                    }
                    analysis->PropagateExpressionEffect(state, exp);
                }
#ifdef AnalysisDevDebug
                std::cout << exp->GetResult()->ToString() << std::endl;
                std::cout << state.ToString() << std::endl;
#endif
            }
            if (auto lambda = IsApplyToLambda(terminator); lambda) {
                analysis->HandleVarStateCapturedByLambda(state, lambda);
            }
            std::optional<Block*> targetSucc = analysis->PropagateTerminatorEffect(state, terminator);
#ifdef AnalysisDevDebug
            std::cout << "exit: " << state.ToString() << std::endl;
#endif
            if (analysis->CheckInQueueTimes(bb, state)) {
                // if inqueue over certain times, quit analysis and set top to state.
                //   then spread top state to its successors
                targetSucc = std::nullopt;
            }
            auto succs = targetSucc.has_value() ? std::vector<Block*>{targetSucc.value()} : bb->GetSuccessors();
            for (auto succ : succs) {
#ifdef AnalysisDevDebug
                std::cout << succ->GetIdentifier() << " is joining with " << bb->GetIdentifier() << std::endl;
                std::cout << succ->GetIdentifier() << ":\n" << entryStates->at(succ).ToString() << std::endl;
                std::cout << bb->GetIdentifier() << ":\n" << state.ToString() << std::endl;
#endif
                auto hasChanged = entryStates->at(succ).Join(state);
                if (hasChanged && worklistSet.find(succ) == worklistSet.end()) {
                    worklist.push_back(succ);
                    worklistSet.insert(succ);
                }
            }
        }
    }

private:
    /**
     * @brief check whether exceed block limit to ensure whether do opt in this function.
     * @return flag whether exceed block limit.
     */
    bool DoesExceedBlockLimit()
    {
        if (!analysis->GetBlockLimit().has_value()) {
            return false;
        }
        return func->GetBody()->GetBlocks().size() > analysis->GetBlockLimit().value();
    }

    const Func* func;
    std::unique_ptr<Analysis<Domain>> analysis;
    std::unique_ptr<std::unordered_map<Block*, Domain>> entrySets;

    std::vector<LambdaState<Domain>> lambdaWorklist{};
    std::unordered_set<const Lambda*> lambdaWorklistSet{};
    std::unordered_map<const Lambda*, Domain> lambdaEnvState{};
};
} // namespace Cangjie::CHIR

#endif
