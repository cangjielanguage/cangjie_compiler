// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_ANALYSIS_REACHING_DEFINITION_ANALYSIS_H
#define CANGJIE_CHIR_ANALYSIS_REACHING_DEFINITION_ANALYSIS_H

#include "cangjie/CHIR/Analysis/Analysis.h"
#include "cangjie/CHIR/Analysis/FlatSet.h"
#include "cangjie/CHIR/Utils/CHIRCasting.h"
#include "cangjie/CHIR/IR/Value/Value.h"

namespace Cangjie::CHIR {

class ReachingDefinitionAnalysis;

/**
 * @brief reaching definition domain, indicate a state whether values is unreachable.
 */
class ReachingDefinitionDomain final : public AbstractDomain<ReachingDefinitionDomain> {
    friend class ReachingDefinitionAnalysis;

public:
    ReachingDefinitionDomain() = delete;
    /**
     * @brief constructor of ReachingDefinitionDomain
     * @param allocateIdxMap allocate map to analysis in this pass.
     */
    ReachingDefinitionDomain(std::unordered_map<const Value*, size_t>* allocateIdxMap);

    /// constructor of ReachingDefinitionDomain.
    ~ReachingDefinitionDomain() final
    {
    }

    /**
     * @brief join two domain to one.
     * @param rhs other domain to join.
     * @return return true is join state is changed.
     */
    bool Join(const ReachingDefinitionDomain& rhs) override;

    /// return formatted info string.
    std::string ToString() const override;

    /**
     * @brief check store location is reachable
     * @param location location to check
     * @return store expression if location is reached, otherwise nullptr
     */
    const Store* CheckReachingDef(const Value* location) const;
    /**
     * @brief check load location is reachable
     * @param location location to check
     * @return load expression if location is reached, otherwise nullptr
     */
    const Load* CheckReachingLoadDef(const Value* location) const;

private:
    /// reaching status of store expressions.
    std::vector<FlatSet<const Store*>> reachingDefs;
    /// reaching status of load expressions.
    std::vector<FlatSet<const Load*>> reachingLoadDefs;
    /// allocate index map of one function.
    std::unordered_map<const Value*, size_t>* allocateIdxMap;
};

/**
 * @brief partially specialized member to ReachingDefinitionDomain of analysis
 */
template <> const std::string Analysis<ReachingDefinitionDomain>::name;
template <> const std::optional<unsigned> Analysis<ReachingDefinitionDomain>::blockLimit;

/**
 * @brief reaching definition analysis, analyse allocate is reachable.
 */
class ReachingDefinitionAnalysis final : public Analysis<ReachingDefinitionDomain> {
public:
    ReachingDefinitionAnalysis() = delete;
    /**
     * @brief constructor for reaching definition analysis.
     * @param func function to analyse.
     */
    explicit ReachingDefinitionAnalysis(const Func* func);

    /// reaching definition analysis destructor.
    ~ReachingDefinitionAnalysis() final
    {
    }

    /// return Bottom of ReachingDefinitionDomain
    ReachingDefinitionDomain Bottom() override;

    /**
     * @brief use input state to initialize entry state of functions.
     * @param state input entry state of analysing function.
     */
    void InitializeFuncEntryState(ReachingDefinitionDomain& state) override;

    /// special state work for lambda expression.
    void HandleVarStateCapturedByLambda(ReachingDefinitionDomain& state, const Lambda* lambda) override;

    /**
     * @brief propagate state to next expression.
     * @param state current state of this function.
     * @param expression next expression to analyse.
     */
    void PropagateExpressionEffect(ReachingDefinitionDomain& state, const Expression* expression) override;

    /**
     * @brief propagate state to next terminator.
     * @param state current state of this function.
     * @param terminator next terminator to analyse.
     * @return blocks return after analysis.
     */
    std::optional<Block*> PropagateTerminatorEffect(
        ReachingDefinitionDomain& state, const Terminator* terminator) override;

private:
    void HandleStoreExpr(ReachingDefinitionDomain& state, const Store* store);
    void HanldeStoreElemRefExpr(ReachingDefinitionDomain& state, const StoreElementRef& storeElemRef);
    void HandleGetElemRefExpr(ReachingDefinitionDomain& state, const GetElementRef* getElemRef);
    void HandleLoadExpr(ReachingDefinitionDomain& state, const Load* load);

    template <typename TApply> void HandleApplyExpr(ReachingDefinitionDomain& state, const TApply* apply)
    {
        // If it's a apply to a mut function of a tracked struct.
        if (auto callee = apply->GetCallee(); callee->TestAttr(Attribute::MUT)) {
            auto structArg = apply->GetArgs()[0];
            if (!StaticCast<RefType*>(structArg->GetType())->GetBaseType()->IsStruct()) {
                return;
            }
            if (auto it = allocateIdxMap.find(structArg); it != allocateIdxMap.end()) {
                state.reachingDefs[it->second].SetToBound(/* isTop = */ true);
                state.reachingLoadDefs[it->second].SetToBound(/* isTop = */ true);
            }
        }
    }

    template <typename TIntrinsic>
    void HandleIntrinsicExpr(ReachingDefinitionDomain& state, const TIntrinsic* intrinsic)
    {
        // If the tracked object is the parameter of the inout intrinsic
        if (intrinsic->GetIntrinsicKind() == CHIR::IntrinsicKind::INOUT_PARAM) {
            CJC_ASSERT(intrinsic->GetNumOfOperands() == 1);
            auto operand = intrinsic->GetOperand(0);
            if (auto it = allocateIdxMap.find(operand); it != allocateIdxMap.end()) {
                state.reachingDefs[it->second].SetToBound(/* isTop = */ true);
                state.reachingLoadDefs[it->second].SetToBound(/* isTop = */ true);
            }
        }
    }

    /// allocate index map of one function.
    std::unordered_map<const Value*, size_t> allocateIdxMap;
};
} // namespace Cangjie::CHIR

#endif