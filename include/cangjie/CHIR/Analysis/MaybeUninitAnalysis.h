// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_ANALYSIS_MAYBE_UNINIT_ANALYSIS_H
#define CANGJIE_CHIR_ANALYSIS_MAYBE_UNINIT_ANALYSIS_H

#include "cangjie/CHIR/Analysis/GenKillAnalysis.h"
#include "cangjie/CHIR/Type/CustomTypeDef.h"
#include "cangjie/CHIR/Value.h"

namespace Cangjie::CHIR {
/**
 * @brief info for analysing init functions.
 */
struct ConstructorInitInfo {
    CustomTypeDef* thisCustomDef;
    ClassDef* superClassDef;
    
    /// If the function is the constructor of a class, the numbers of members in its super class.
    size_t superMemberNums = 0;
    /// all members = superMemberNums + localMemberNums.
    size_t localMemberNums = 0;
};

class MaybeUninitAnalysis;

/**
 * @brief maybe not init domain, indicate a status which values whether have not been init.
 */
class MaybeUninitDomain : public GenKillDomain<MaybeUninitDomain> {
    friend class MaybeUninitAnalysis;

public:
    MaybeUninitDomain() = delete;

    /**
     * @brief constructor of maybe not init domain.
     * @param domainSize domain size of one function.
     * @param ctorInitInfo extra info for init function check.
     * @param allocateIdxMap allocate map to analysis in this pass.
     */
    MaybeUninitDomain(size_t domainSize, const ConstructorInitInfo* ctorInitInfo,
        std::unordered_map<const Value*, size_t>* allocateIdxMap);

    /// constructor of maybe not init domain.
    ~MaybeUninitDomain() override
    {
    }

    /**
     * @brief join two domain to one.
     * @param rhs other domain to join.
     * @return return true is join state is changed.
     */
    bool Join(const MaybeUninitDomain& rhs) override;

    /**
     * @brief check whether location is maybe not inited.
     * @param location location to check status.
     * @return status if location is maybe not inited.
     */
    std::optional<bool> IsMaybeUninitedAllocation(const Value* location) const;

    /**
     * @brief get position of certain location.
     * @param location location to get position.
     * @return position.
     */
    const std::set<unsigned>& GetMaybeInitedPos(const Value* location) const;

    /**
     * @brief extra info to indicate if status of value in init function or its super class.
     */
    enum class UninitedMemberKind { SUPER_MEMBER, LOCAL_MEMBER, NA };

    /**
     * @brief check if member var is not init in init function.
     * @param memberIndex index of member var.
     * @return info to indicate if member var is in super class or this class.
     */
    UninitedMemberKind IsMaybeUninitedMember(size_t memberIndex) const;

    /// get position of certain member index.
    const std::set<unsigned>& GetMaybeInitedPos(size_t memberIndex) const;

    /// return all uninited local members including super class.
    std::vector<size_t> GetMaybeUninitedLocalMembers() const;

private:
    /// init local member to uninited.
    void SetAllLocalMemberInited();
    /// extra info for init function check.
    const ConstructorInitInfo* ctorInitInfo;
    /// allocate map from location to index.
    std::unordered_map<const Value*, size_t>* allocateIdxMap;
    /// maybe not init position array.
    std::vector<std::set<unsigned>> maybeInitedPos;
};

/**
 * @brief partially specialized member to MaybeUninitDomain of analysis
 */
template <> const std::string Analysis<MaybeUninitDomain>::name;
template <> const std::optional<unsigned> Analysis<MaybeUninitDomain>::blockLimit;
template <> const AnalysisKind GenKillDomain<MaybeUninitDomain>::mustOrMaybe;

/**
 * @brief maybe init analysis, analyse a status which values whether have not been init.
 */
class MaybeUninitAnalysis final : public GenKillAnalysis<MaybeUninitDomain> {
public:
    MaybeUninitAnalysis() = delete;

    /**
     * @brief maybe not init analysis constructor.
     * @param func function to analyse.
     * @param ctorInitInfo extra info for init function check.
     */
    MaybeUninitAnalysis(const Func* func, const ConstructorInitInfo* ctorInitInfo);

    /// maybe not init analysis destructor.
    ~MaybeUninitAnalysis() final
    {
    }

    /// return Bottom of MaybeUninitDomain
    MaybeUninitDomain Bottom() override;

    /**
     * @brief use input state to initialize entry state of functions.
     * @param state input entry state of analysing function.
     */
    void InitializeFuncEntryState(MaybeUninitDomain& state) override;

    /**
     * @brief propagate state to next expression.
     * @param state current state of this function.
     * @param expression next expression to analyse.
     */
    void PropagateExpressionEffect(MaybeUninitDomain& state, const Expression* expression) override;

    /**
     * @brief propagate state to next terminator.
     * @param state current state of this function.
     * @param expression next terminator to analyse.
     * @return blocks return after analysis.
     */
    std::optional<Block*> PropagateTerminatorEffect(MaybeUninitDomain& state, const Terminator* expression) override;

private:
    void HandleAllocateExpr(MaybeUninitDomain& state, const Allocate* allocate);

    void HandleStoreExpr(MaybeUninitDomain& state, const Store* store);

    void HandleStoreElemRefExpr(MaybeUninitDomain& state, const StoreElementRef* store) const;

    void HandleApplyExpr(MaybeUninitDomain& state, const Apply* apply) const;

    const ConstructorInitInfo* ctorInitInfo;
    std::unordered_map<const Value*, size_t> allocateIdxMap{};
};

/**
 * init allocate map for VIC analysis from all expressions
 */
void SaveAllocateMap(
    const BlockGroup& body, size_t& allocateIdx, std::unordered_map<const Value *, size_t>& allocateIdxMap);
} // namespace Cangjie::CHIR

#endif