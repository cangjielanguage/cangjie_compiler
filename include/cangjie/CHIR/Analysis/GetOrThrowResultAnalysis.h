// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_ANALYSIS_GETORTHROW_RESULT_ANALYSIS_H
#define CANGJIE_CHIR_ANALYSIS_GETORTHROW_RESULT_ANALYSIS_H

#include "cangjie/CHIR/Analysis/Analysis.h"
#include "cangjie/CHIR/Analysis/FlatSet.h"
#include "cangjie/CHIR/IR/Value/Value.h"

namespace Cangjie::CHIR {
/**
 * @brief domain definition of get or throw optimization pass.
 */
class GetOrThrowResultAnalysis;
class GetOrThrowResultDomain final : public AbstractDomain<GetOrThrowResultDomain> {
public:
    friend class GetOrThrowResultAnalysis;
    GetOrThrowResultDomain() = delete;
    /**
     * @brief constructor for domain definition of get or throw optimization pass.
     * @param ArgIdxMap index map, value of domain.
     */
    GetOrThrowResultDomain(std::unordered_map<const Value*, size_t>* ArgIdxMap);
    ~GetOrThrowResultDomain()
    {
    }

    /// join two domains
    bool Join(const GetOrThrowResultDomain& rhs) override;

    /// output string of get or throw domain.
    std::string ToString() const override;

    /**
     * @brief check location's domain result.
     * @param location location value to check its domain.
     * @return return apply if location has its domain.
     */
    const Apply* CheckGetOrThrowResult(const Value* location) const;

private:
    /// results of get or throw analysis
    std::vector<FlatSet<const Apply*>> GetOrThrowResults;
    /// from location to index
    std::unordered_map<const Value*, size_t>* ArgIdxMap;
};

template <> const std::string Analysis<GetOrThrowResultDomain>::name;
template <> const std::optional<unsigned> Analysis<GetOrThrowResultDomain>::blockLimit;
class GetOrThrowResultAnalysis final : public Analysis<GetOrThrowResultDomain> {
public:
    GetOrThrowResultAnalysis() = delete;
    /**
     * @brief constructor of get or throw analysis.
     * @param func function to analyse.
     * @param isDebug flag whether print debug log.
     */
    GetOrThrowResultAnalysis(const Func* func, bool isDebug);
    ~GetOrThrowResultAnalysis()
    {
    }

    /// return Bottom of GetOrThrowResultDomain
    GetOrThrowResultDomain Bottom() override;

    /**
     * @brief use input state to initialize entry state of functions.
     * @param state input entry state of analysing function.
     */
    void InitializeFuncEntryState(GetOrThrowResultDomain& state) override;

    /**
     * @brief propagate state to next expression.
     * @param state current state of this function.
     * @param expression next expression to analyse.
     */
    void PropagateExpressionEffect(GetOrThrowResultDomain& state, const Expression* expression) override;

    /**
     * @brief propagate state to next terminator.
     * @param state current state of this function.
     * @param terminator next terminator to analyse.
     * @return blocks return after analysis.
     */
    std::optional<Block*> PropagateTerminatorEffect(
        GetOrThrowResultDomain& state, const Terminator* terminator) override;

private:
    /// from location to index
    std::unordered_map<const Value*, size_t> ArgIdxMap;
};
} // namespace Cangjie::CHIR

#endif
