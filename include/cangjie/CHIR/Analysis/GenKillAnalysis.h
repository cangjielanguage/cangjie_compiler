// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_ANALYSIS_GENKILL_ANALYSIS_H
#define CANGJIE_CHIR_ANALYSIS_GENKILL_ANALYSIS_H

#include <sstream>
#include "cangjie/CHIR/Analysis/Analysis.h"

namespace Cangjie::CHIR {

/**
 * @brief A GenKill Analysis is an analysis that track the states of facts of a certain kind. It focuses if a fact
 * is true, or not. `Gen` a fact means a fact becomes true, and `Kill` a fact means a fact becomes false.
 * In a GenKill Analysis, with the execution of different expressions, a fact mey be `Gen`-ed or `Kill`-ed.
 *
 * For example, if we are doing a maybe-uninit analysis(we focus on the variables that maybe unintialised),
 * when we meet an Allocate expression, we could know that this Allocation creates an uninitialised variable,
 * and this is a `Gen` point; when we meet a Store expression which puts a value to the place created by
 * the Allocate expression before, we could know that this variable has been initialised, and this is a `Kill`
 * point.
 *
 * A GenKill Analysis must be a must analysis or a maybe analysis. A `must` analysis determines whether a
 * fact is true on *all* paths through the program. And a `maybe` analysis determines wheter a fact is true
 * on *some* path throught the program. Thus, a `must` analysis and a `maybe` analysis behave differently
 * on the `Join` operation.
 */
enum class AnalysisKind { MUST, MAYBE };

/**
 * @brief generate and kill domain
 * @tparam Domain abastract value for generate and kill domain
 */
template <typename Domain> class GenKillDomain : public AbstractDomain<Domain> {
public:
    GenKillDomain() = delete;

    /**
     * @brief gen kill domain constructor
     * @param domainSize domain size
     */
    GenKillDomain(size_t domainSize) : AbstractDomain<Domain>()
    {
        states = std::vector<bool>(domainSize);
    }
    
    ~GenKillDomain() override
    {
    }

    /**
     * @brief join domain with other domain.
     * @param rhs other domain to join with.
     * @return whether changed after join.
     */
    bool Join(const Domain& rhs) override
    {
        this->kind = ReachableKind::REACHABLE;
        auto size = states.size();
        bool changed = false;
        auto& rhsStates = rhs.states;
        for (size_t i = 0; i < size; ++i) {
            bool oldState = states[i];
            states[i] = (mustOrMaybe == AnalysisKind::MUST) ? (oldState & rhsStates[i]) : (oldState | rhsStates[i]);
            changed |= oldState ^ states[i];
        }
        return changed;
    }

    /// output string of generate and kill abstract domain.
    std::string ToString() const override
    {
        if (this->kind == ReachableKind::UNREACHABLE) {
            return "Unreachable";
        } else {
            std::stringstream ss;
            ss << "Reachable(";
            for (auto b : states) {
                ss << b << " ";
            }
            ss << ")";
            return ss.str();
        }
    }

    /// set state of input index to generate.
    inline void Gen(const size_t index)
    {
        states[index] = true;
    }

    /// set state of input index to kill.
    inline void Kill(const size_t index)
    {
        states[index] = false;
    }

    /// set all states to generate.
    void GenAll()
    {
        auto size = states.size();
        states.assign(size, true);
    }

    /// set all states to kill.
    void KillAll()
    {
        auto size = states.size();
        states.assign(size, false);
    }

    /// propagate state from index from to index to.
    inline void PropagateFrom(const size_t from, const size_t to)
    {
        states[to] = states[from];
    }

    /// check whether state of input index is generate.
    inline bool IsTrueAt(const size_t index) const
    {
        return states[index];
    }

    /**
     * @brief The kind of a GenKillAnalysis. A GenKillAnalysis must be a must analysis or a maybe analysis.
     * A `must` analysis determines whether a fact is true on *all* paths through the program.
     * A `maybe` analysis determines wheter a fact is true on *some* path throught the program.
     */
    static const AnalysisKind mustOrMaybe;

protected:
    /// The current abstract state.
    std::vector<bool> states;
};

/**
 * @brief analysis pass of generate and kill domain.
 * @tparam Domain abstract domain.
 */
template <typename Domain, typename = std::enable_if_t<std::is_base_of_v<GenKillDomain<Domain>, Domain>>>
class GenKillAnalysis : public Analysis<Domain> {
public:
    GenKillAnalysis() = delete;

    /// constructor of generate and kill analysis.
    explicit GenKillAnalysis(const Func* func) : Analysis<Domain>(func)
    {
    }

    /// destructor of generate and kill analysis.
    ~GenKillAnalysis() override
    {
    }

    /// get domain size from analysis.
    size_t GetDomainSize()
    {
        return domainSize;
    }

protected:
    /** The size of the GenKill domain. */
    size_t domainSize;
};

} // namespace Cangjie::CHIR

#endif