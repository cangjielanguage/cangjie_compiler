// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_ANALYSIS_ANALYSIS_H
#define CANGJIE_CHIR_ANALYSIS_ANALYSIS_H

#include "cangjie/CHIR/IR/Expression/Terminator.h"

#include <optional>

namespace Cangjie::CHIR {
/**
 * @brief mark status whether value is reachable.
 */
enum class ReachableKind : uint8_t { UNREACHABLE, REACHABLE };

/**
 * @brief abstract conception of domain for CHIR IR analysis.
 * @tparam Domain the specific domain that the analysis pass focus on.
 */
template <typename Domain> class AbstractDomain {
public:
    /**
     * @brief destructor of abstract domain.
     */
    virtual ~AbstractDomain()
    {
    }

    /**
     * @brief merge two abstract domain.
     * @param rhs other abstract domain.
     * @return whether changed after join function.
     */
    virtual bool Join(const Domain& rhs)
    {
        (void)rhs;
        return false;
    }

    /**
     * @brief whether abstract domain is bottom.
     * @return results of whether abstract domain is bottom.
     */
    virtual bool IsBottom() const
    {
        return kind == ReachableKind::UNREACHABLE;
    }

    /**
     * @brief create string from this abstract domain.
     * @return created string.
     */
    virtual std::string ToString() const
    {
        return "";
    }

protected:
    explicit AbstractDomain() : kind(ReachableKind::UNREACHABLE)
    {
    }

    ReachableKind kind;
};

/**
 * @brief abstract analysis pass for CHIR IR analysis.
 * @tparam Domain the specific domain that the analysis pass focus on.
 */
template <typename Domain, typename = std::enable_if_t<std::is_base_of_v<AbstractDomain<Domain>, Domain>>>
class Analysis {
public:
    /**
     * @brief destructor of abstract analysis pass.
     */
    virtual ~Analysis()
    {
    }

    /**
     * @brief return bottom of domain for analysis.
     * @return bottom state of analysis,
     */
    virtual Domain Bottom() = 0;

    /**
     * @brief abstract function to initialize function entry state.
     * @param state entry state
     */
    virtual void InitializeFuncEntryState(Domain& state)
    {
        (void)state;
    }

    /**
     * @brief abstract function to update state of current lambda.
     * @param lambda lambda to update state.
     */
    virtual void UpdateCurrentLambda(const Lambda* lambda)
    {
        this->currentLambda = lambda;
    }

    /**
     * @brief abstract function to initialize lambda entry state.
     * @param state entry state
     */
    virtual void InitializeLambdaEntryState(Domain& state)
    {
        (void)state;
    }

    /// @brief If it's apply to a lambda, we need to clear the state of vars captured by the lambda.
    virtual void HandleVarStateCapturedByLambda(Domain& state, const Lambda* lambda)
    {
        (void)state;
        (void)lambda;
    }

    /// abstract function to update state of lambda expression
    virtual void PreHandleLambdaExpression(Domain& state, const Lambda* lambda)
    {
        (void)state;
        (void)lambda;
    }

    /// abstract function to update state of all expressions.
    virtual void PropagateExpressionEffect(Domain& state, const Expression* expression)
    {
        (void)state;
        (void)expression;
    }

    /// abstract function to update state of all terminators.
    virtual std::optional<Block*> PropagateTerminatorEffect(Domain& state, const Terminator* terminator)
    {
        (void)state;
        (void)terminator;
        return std::nullopt;
    }

    /// abstract function
    static bool Filter(const Func& method)
    {
        (void)method;
        return true;
    }

    /// abstract function to check in queue times for one certain block
    virtual bool CheckInQueueTimes(const Block* block, Domain& curState)
    {
        (void)block;
        (void)curState;
        return false;
    }

    /// get analysis name
    static std::string GetAnalysisName()
    {
        return name;
    }

    /// get block limit number to check whether a function should be analysed
    static std::optional<unsigned> GetBlockLimit()
    {
        return blockLimit;
    }

    /// manually set state to stable
    void SetToStable()
    {
        isStable = true;
    }

protected:
    explicit Analysis(const Func* func, bool isDebug = false) : func(func), isDebug(isDebug)
    {
    }

    /// The function that will be analysed.
    const Func* func;

    /// Will print the debug message if it's true.
    bool isDebug;

    /// The current function that being analysed. It can be a nested function.
    std::optional<const Lambda*> currentLambda{std::nullopt};

    /// If the result of this analysis is stable.
    bool isStable{false};

    /// The name of this data-flow analysis.
    static const std::string name;

    /// Limit on the number of blocks of a function that can be analysed.
    static const std::optional<unsigned> blockLimit;
};

} // namespace Cangjie::CHIR

#endif
