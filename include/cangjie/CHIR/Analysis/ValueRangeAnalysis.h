// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_ANALYSIS_VALUE_RANGE_ANALYSIS_H
#define CANGJIE_CHIR_ANALYSIS_VALUE_RANGE_ANALYSIS_H

#include <type_traits>

#include "cangjie/CHIR/Analysis/BoolDomain.h"
#include "cangjie/CHIR/Analysis/SIntDomain.h"
#include "cangjie/CHIR/Analysis/ValueAnalysis.h"
#include "cangjie/CHIR/Utils/DiagAdapter.h"
#include "cangjie/CHIR/Utils/Utils.h"

namespace Cangjie::CHIR {
class ValueRange {
public:
    enum class RangeKind : uint8_t { BOOL, SINT };

    ValueRange() = delete;

    explicit ValueRange(RangeKind kind);

    virtual ~ValueRange();

    /// join two range, return nullopt if no change happened.
    virtual std::optional<std::unique_ptr<ValueRange>> Join(const ValueRange& rhs) const = 0;

    virtual std::string ToString() const = 0;

    virtual std::unique_ptr<ValueRange> Clone() const = 0;

    /// get range kind, now suppert BOOL or SINT.
    RangeKind GetRangeKind() const;

protected:
    RangeKind kind;
};

class BoolRange : public ValueRange {
public:
    explicit BoolRange(BoolDomain domain);

    ~BoolRange() override = default;

    /// join two range, return nullopt if no change happened.
    std::optional<std::unique_ptr<ValueRange>> Join(const ValueRange& rhs) const override;

    std::string ToString() const override;

    std::unique_ptr<ValueRange> Clone() const override;

    /// get range kind, get BOOL for this range type.
    const BoolDomain& GetVal() const;

private:
    BoolDomain domain;
};

class SIntRange : public ValueRange {
public:
    explicit SIntRange(SIntDomain domain);

    ~SIntRange() override = default;

    /// join two range, return nullopt if no change happened.
    std::optional<std::unique_ptr<ValueRange>> Join(const ValueRange& rhs) const override;

    std::string ToString() const override;

    std::unique_ptr<ValueRange> Clone() const override;

    /// get range kind, get BOOL for this range type.
    const SIntDomain& GetVal() const;

private:
    SIntDomain domain;
};

/**
 * @brief the abstract value domain of range value
 */
using RangeValueDomain = ValueDomain<ValueRange>;
/**
 * @brief the state of range value domain
 */
using RangeDomain = State<RangeValueDomain>;
/**
 * @brief partially specialized analysis import value.
 */
template <> const std::string Analysis<RangeDomain>::name;
template <> const std::optional<unsigned> Analysis<RangeDomain>::blockLimit;
template <> RangeDomain::ChildrenMap ValueAnalysis<RangeValueDomain>::globalChildrenMap;
template <> RangeDomain::AllocatedRefMap ValueAnalysis<RangeValueDomain>::globalAllocatedRefMap;
template <> RangeDomain::AllocatedObjMap ValueAnalysis<RangeValueDomain>::globalAllocatedObjMap;
template <> std::vector<std::unique_ptr<Ref>> ValueAnalysis<RangeValueDomain>::globalRefPool;
template <> std::vector<std::unique_ptr<AbstractObject>> ValueAnalysis<RangeValueDomain>::globalAbsObjPool;
template <> RangeDomain ValueAnalysis<RangeValueDomain>::globalState;

/**
 * @brief heck whether global var need range analysis.
 * @param gv global var to check.
 * @return flag global var need analyse
 */
template <> bool IsTrackedGV<RangeValueDomain>(const GlobalVar& gv);

/**
 * @brief literal value analysis function
 * @param literal input literal value to analyse
 * @return range value literalValue is.
 */
template <> RangeValueDomain HandleNonNullLiteralValue<RangeValueDomain>(const LiteralValue* literal);

/**
 * @brief range analysis for CHIR IR.
 */
class RangeAnalysis final : public ValueAnalysis<RangeValueDomain> {
public:
    RangeAnalysis() = delete;
    /**
     * @brief range analysis constructor.
     * @param func function to analyse
     * @param builder CHIR builder for generating IR.
     * @param isDebug flag whether print debug log.
     * @param diag reporter to report warning or error.
     */
    RangeAnalysis(const Func* func, CHIRBuilder& builder, bool isDebug, const Ptr<DiagAdapter>& diag);

    ~RangeAnalysis() override;

    /**
     * @brief get bool domain of CHIR value from state.
     * @param state state to get domain.
     * @param value CHIR value to get domain.
     * @return domain found in state.
     */
    static BoolDomain GetBoolDomainFromState(const RangeDomain& state, const Ptr<Value>& value);

    /**
     * @brief get SInt domain of CHIR value from state.
     * @param state state to get domain.
     * @param value CHIR value to get domain.
     * @return domain found in state.
     */
    static const SIntDomain& GetSIntDomainFromState(const RangeDomain& state, const Ptr<Value>& value);

    /**
     * @brief check this block analyse times, quit analysing in this block if inqueue more than a number.
     * @param block block to check inqueue times.
     * @param curState state to check.
     * @return true if InQueue time exceed the maximum else false.
     */
    bool CheckInQueueTimes(const Block* block, RangeDomain& curState) override;

private:
    template <class Domain,
        typename = typename std::enable_if<std::is_same_v<Domain, SIntDomain> || std::is_same_v<Domain, BoolDomain>>>
    void PrintDebugMessage(const Ptr<const Expression>& expr, const Domain& domain) const
    {
        std::stringstream ss;
        ss << "[RangeAnalysis] The value of " +
                ExprKindMgr::Instance()->GetKindName(static_cast<size_t>(expr->GetExprKind())) +
                ToPosInfo(expr->GetDebugLocation()) + " has been set to " << domain << "\n";
        std::cout << ss.str();
    }

    void PrintBranchOptMessage(const Ptr<const Expression>& expr, bool isTrueBlockRemained) const;

    // ======== Transfer functions for normal expressions based on ExprMajorKind ======== //

    void HandleNormalExpressionEffect(RangeDomain& state, const Expression* expression) override;

    void HandleUnaryExpr(RangeDomain& state, const UnaryExpression* unaryExpr) const;

    void HandleBinaryExpr(RangeDomain& state, const BinaryExpression* binaryExpr);

    void HandleOthersExpr(RangeDomain& state, const Expression* expression);

    // ======================= Transfer functions for terminators ======================= //

    std::optional<Block*> HandleTerminatorEffect(RangeDomain& state, const Terminator* terminator) override;

    std::optional<Block*> HandleBranchTerminator(const RangeDomain& state, const Branch* branch) const;

    std::optional<Block*> HandleMultiBranchTerminator(const RangeDomain& state, const MultiBranch* multi) const;

    enum class ExceptionKind : uint8_t { SUCCESS, FAIL, NA };

    // =============== Transfer functions for TypeCast expression =============== //
    SIntDomain ComputeTypeCast(RangeDomain& state, PtrSymbol oldSymbol, const SIntDomain& v, IntWidth dstSize,
        bool dstUnsigned, OverflowStrategy ov) const;

    template <typename TTypeCast> ExceptionKind HandleTypeCast(RangeDomain& state, const TTypeCast* cast)
    {
        auto from = cast->GetSourceTy();
        auto to = cast->GetTargetTy();
        if (!from->IsInteger() || !to->IsInteger()) {
            state.SetToTopOrTopRef(cast->GetResult(), cast->GetResult()->GetType()->IsRef());
            return ExceptionKind::NA;
        }
        auto value = cast->GetSourceValue();
        const auto& sourceDomain = GetSIntDomainFromState(state, value);
        auto res = ComputeTypeCast(
            state, value, sourceDomain, ToWidth(*to), to->IsUnsignedInteger(), cast->GetOverflowStrategy());
        state.Update(cast->GetResult(), std::make_unique<SIntRange>(res));
        return ExceptionKind::NA;
    }

    BoolDomain GenerateBoolRangeFromBinaryOp(RangeDomain& state, const Ptr<const BinaryExpression>& binaryExpr) const;

    DiagAdapter* diag;

    std::unordered_map<const Block*, uint32_t> inqueueTimes;
};
} // namespace Cangjie::CHIR
#endif