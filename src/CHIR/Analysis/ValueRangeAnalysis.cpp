// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Analysis/ValueRangeAnalysis.h"

#include <climits>
#include "cangjie/CHIR/Checker/OverflowChecking.h"
#include "cangjie/CHIR/Analysis/Arithmetic.h"
#include "cangjie/CHIR/Utils/Utils.h"

namespace Cangjie::CHIR {
ValueRange::ValueRange(RangeKind kind) : kind(kind)
{
}

ValueRange::~ValueRange()
{
}

ValueRange::RangeKind ValueRange::GetRangeKind() const
{
    return kind;
}

BoolRange::BoolRange(BoolDomain domain) : ValueRange(RangeKind::BOOL), domain(std::move(domain))
{
}

std::optional<std::unique_ptr<ValueRange>> BoolRange::Join(const ValueRange& rhs) const
{
    CJC_ASSERT(rhs.GetRangeKind() == RangeKind::BOOL);
    auto rhsRange = StaticCast<const BoolRange&>(rhs);
    if (domain.IsSame(rhsRange.domain)) {
        return std::nullopt;
    }
    return std::make_unique<BoolRange>(BoolRange{BoolDomain::Union(domain, rhsRange.domain)});
}

std::string BoolRange::ToString() const
{
    std::stringstream ss;
    ss << domain;
    return ss.str();
}

std::unique_ptr<ValueRange> BoolRange::Clone() const
{
    return std::make_unique<BoolRange>(domain);
}

const BoolDomain& BoolRange::GetVal() const
{
    return domain;
}

SIntRange::SIntRange(SIntDomain domain) : ValueRange(RangeKind::SINT), domain(std::move(domain))
{
}

namespace {
// SIntDomain::IsSame compares numeric bounds only. Symbolic relations have to be compared separately,
// otherwise a join treats two different relations as identical.
bool SameSymbolicBounds(const SIntDomain& a, const SIntDomain& b)
{
    auto ia = a.SymbolicBounds().Begin();
    auto ib = b.SymbolicBounds().Begin();
    const auto enda = a.SymbolicBounds().End();
    const auto endb = b.SymbolicBounds().End();
    for (; ia != enda && ib != endb; ++ia, ++ib) {
        if (ia->first.get() != ib->first.get() || !(ia->second == ib->second)) {
            return false;
        }
    }
    return ia == enda && ib == endb;
}
} // namespace

std::optional<std::unique_ptr<ValueRange>> SIntRange::Join(const ValueRange& rhs) const
{
    CJC_ASSERT(rhs.GetRangeKind() == RangeKind::SINT);
    auto rhsRange = StaticCast<const SIntRange&>(rhs);
    if (domain.IsSame(rhsRange.domain) && SameSymbolicBounds(domain, rhsRange.domain)) {
        return std::nullopt;
    }
    // Union is the upper bound. If this domain is already that bound (the other side only added
    // constraints), report no change. Otherwise a full numeric range that still carries a symbolic
    // relation would look "changed" on every join and the analysis would give the block up.
    auto joined = SIntDomain::Unions(domain, rhsRange.domain);
    if (domain.IsSame(joined) && SameSymbolicBounds(domain, joined)) {
        return std::nullopt;
    }
    return std::make_unique<SIntRange>(SIntRange{std::move(joined)});
}

std::string SIntRange::ToString() const
{
    std::stringstream ss;
    ss << domain;
    return ss.str();
}

std::unique_ptr<ValueRange> SIntRange::Clone() const
{
    return std::make_unique<SIntRange>(domain);
}

const SIntDomain& SIntRange::GetVal() const
{
    return domain;
}

template <> bool IsTrackedGV<RangeValueDomain>(const GlobalVar& gv)
{
    auto baseTyKind = StaticCast<RefType*>(gv.GetType())->GetBaseType()->GetTypeKind();
    return (baseTyKind >= Type::TYPE_INT8 && baseTyKind <= Type::TYPE_UINT_NATIVE) || baseTyKind == Type::TYPE_ENUM ||
        baseTyKind == Type::TYPE_BOOLEAN;
}

template <> RangeValueDomain HandleNonNullLiteralValue<RangeValueDomain>(const LiteralValue* literal)
{
    if (literal->IsBoolLiteral()) {
        return RangeValueDomain(
            std::make_unique<BoolRange>(BoolDomain::FromBool(StaticCast<BoolLiteral*>(literal)->GetVal())));
    } else if (literal->IsIntLiteral()) {
        return RangeValueDomain(std::make_unique<SIntRange>(SIntDomain::From(*literal)));
    } else {
        return RangeValueDomain(true);
    }
}

RangeAnalysis::RangeAnalysis(const Function* func, CHIRBuilder& builder, bool, DiagnosticEngine& diag)
    : ValueAnalysis(func, builder), diag(diag)
{
}

RangeAnalysis::~RangeAnalysis()
{
}

const int MAX_INQUEUE_TIMES = 4;

bool CanAnalyse(const Ptr<Type>& type)
{
    if (type->IsInteger() || type->IsBoolean()) {
        return true;
    }
    return false;
}

const SIntDomain& GetDefaultIntCache(const Ptr<Type>& ty)
{
    constexpr int integerSize{4};
    static SIntDomain signedRange[integerSize]{
        {ConstantRange::Full(IntWidth::I8), false},
        {ConstantRange::Full(IntWidth::I16), false},
        {ConstantRange::Full(IntWidth::I32), false},
        {ConstantRange::Full(IntWidth::I64), false},
    };
    static SIntDomain unsignedRange[integerSize]{
        {ConstantRange::Full(IntWidth::I8), true},
        {ConstantRange::Full(IntWidth::I16), true},
        {ConstantRange::Full(IntWidth::I32), true},
        {ConstantRange::Full(IntWidth::I64), true},
    };
    auto width{Ctz(static_cast<unsigned>(ToWidth(*ty)) / static_cast<unsigned>(CHAR_BIT))};
    return ty->IsUnsignedInteger() ? unsignedRange[width] : signedRange[width];
}

inline bool IsBasicBinaryExpr(const Expression& expr)
{
    auto binary = DynamicCast<const BinaryExpressionBase*>(&expr);
    return binary != nullptr && binary->IsMathematicalOperator() && binary->GetOpKind() != BinaryExprKind::EXP;
}

template <> const std::string Analysis<RangeDomain>::name = "range-analysis";
template <> const std::optional<unsigned> Analysis<RangeDomain>::blockLimit = 80;
template <> RangeDomain::ChildrenMap ValueAnalysis<RangeValueDomain>::globalChildrenMap{};
template <> RangeDomain::AllocatedRefMap ValueAnalysis<RangeValueDomain>::globalAllocatedRefMap{};
template <> RangeDomain::AllocatedObjMap ValueAnalysis<RangeValueDomain>::globalAllocatedObjMap{};
template <> std::vector<std::unique_ptr<Ref>> ValueAnalysis<RangeValueDomain>::globalRefPool{};
template <> std::vector<std::unique_ptr<AbstractObject>> ValueAnalysis<RangeValueDomain>::globalAbsObjPool{};
template <>
RangeDomain ValueAnalysis<RangeValueDomain>::globalState{&globalChildrenMap, &globalAllocatedRefMap,
    nullptr, &globalAllocatedObjMap, &globalRefPool, &globalAbsObjPool};

BoolDomain RangeAnalysis::GetBoolDomainFromState(const RangeDomain& state, const Ptr<Value>& value)
{
    if (!value->GetType()->IsBoolean()) {
        return BoolDomain::Top();
    }
    auto domain = state.CheckAbstractValueWithTopBottom(value);
    if (domain == nullptr || domain->IsTop()) {
        return BoolDomain::Top();
    }
    auto absVal = domain->CheckAbsVal();
    if (absVal == nullptr || absVal->GetRangeKind() != ValueRange::RangeKind::BOOL) {
        return BoolDomain::Top();
    }
    return StaticCast<const BoolRange*>(absVal)->GetVal();
}

const SIntDomain& RangeAnalysis::GetSIntDomainFromState(const RangeDomain& state, const Ptr<Value>& value)
{
    CJC_ASSERT(value->GetType()->IsInteger());
    auto domain = state.CheckAbstractValueWithTopBottom(value);
    if (domain == nullptr || domain->IsTop()) {
        return GetDefaultIntCache(value->GetType());
    }
    auto absVal = domain->CheckAbsVal();
    if (absVal == nullptr || absVal->GetRangeKind() != ValueRange::RangeKind::SINT) {
        return GetDefaultIntCache(value->GetType());
    }
    return StaticCast<const SIntRange*>(absVal)->GetVal();
}

void RangeAnalysis::HandleNormalExpressionEffect(RangeDomain& state, const Expression* expression)
{
    switch (expression->GetExprMajorKind()) {
        case ExprMajorKind::MEMORY_EXPR:
            return;
        case ExprMajorKind::UNARY_EXPR:
            HandleUnaryExpr(state, StaticCast<const UnaryExpressionBase*>(expression));
            break;
        case ExprMajorKind::BINARY_EXPR:
            HandleBinaryExpr(state, StaticCast<const BinaryExpressionBase*>(expression));
            break;
        case ExprMajorKind::OTHERS:
            HandleOthersExpr(state, expression);
            break;
        case ExprMajorKind::STRUCTURED_CTRL_FLOW_EXPR:
        default: {
#ifndef NDEBUG
            CJC_ABORT();
#else
            return;
#endif
        }
    }
}

BoolDomain RangeAnalysis::GenerateBoolRangeFromBinaryOp(
    RangeDomain& state, const Ptr<const BinaryExpressionBase>& binaryExpr) const
{
    auto lhs = binaryExpr->GetLHSOperand();
    auto rhs = binaryExpr->GetRHSOperand();
    auto opKind = binaryExpr->GetOpKind();
    if (lhs->GetType()->IsInteger()) {
        const auto& lRange = GetSIntDomainFromState(state, lhs);
        const auto& rRange = GetSIntDomainFromState(state, rhs);
        return ComputeRelIntBinop(CHIRRelIntBinopArgs{
            lRange, rRange, lhs, rhs, opKind, IsUnsignedArithmetic(*binaryExpr)});
    }
    const auto& lRange = GetBoolDomainFromState(state, lhs);
    const auto& rRange = GetBoolDomainFromState(state, rhs);
    return ComputeEqualityBoolBinop(lRange, rRange, opKind);
}

bool RangeAnalysis::CheckInQueueTimes(const Block* block, RangeDomain& curState)
{
    if (inqueueTimes.count(block) == 0) {
        inqueueTimes[block] = 1;
        return false;
    }
    inqueueTimes[block]++;
    if (inqueueTimes.at(block) >= MAX_INQUEUE_TIMES) {
        curState.ClearState();
        return true;
    }
    return false;
}

void RangeAnalysis::HandleUnaryExpr(RangeDomain& state, const UnaryExpressionBase* unaryExpr) const
{
    auto dest = unaryExpr->GetResult();
    return state.SetToBound(dest, true);
}

std::string GenerateTypeRangePrompt(const Ptr<Type>& type)
{
    const static std::unordered_map<Type::TypeKind, std::pair<int64_t, uint64_t>> TYPE_TO_RANGE = {
        {Type::TypeKind::TYPE_INT8, {std::numeric_limits<int8_t>::min(), std::numeric_limits<int8_t>::max()}},
        {Type::TypeKind::TYPE_INT16, {std::numeric_limits<int16_t>::min(), std::numeric_limits<int16_t>::max()}},
        {Type::TypeKind::TYPE_INT32, {std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()}},
        {Type::TypeKind::TYPE_INT64, {std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::max()}},
        {Type::TypeKind::TYPE_INT_NATIVE, {std::numeric_limits<ssize_t>::min(), std::numeric_limits<ssize_t>::max()}},
        {Type::TypeKind::TYPE_UINT8, {std::numeric_limits<uint8_t>::min(), std::numeric_limits<uint8_t>::max()}},
        {Type::TypeKind::TYPE_UINT16, {std::numeric_limits<uint16_t>::min(), std::numeric_limits<uint16_t>::max()}},
        {Type::TypeKind::TYPE_UINT32, {std::numeric_limits<uint32_t>::min(), std::numeric_limits<uint32_t>::max()}},
        {Type::TypeKind::TYPE_UINT64, {std::numeric_limits<uint64_t>::min(), std::numeric_limits<uint64_t>::max()}},
        {Type::TypeKind::TYPE_UINT_NATIVE, {std::numeric_limits<size_t>::min(), std::numeric_limits<size_t>::max()}}};

    auto [min, max] = TYPE_TO_RANGE.at(type->GetTypeKind());
    return "range of " + type->ToString() + " is " + std::to_string(min) + " ~ " + std::to_string(max);
}

template <typename TBinary, typename T>
void RaiseArithmeticOverflowError(
    const TBinary* expr, BinaryExprKind kind, T leftVal, T rightVal, DiagnosticEngine& diag)
{
    auto& loc = expr->GetDebugLocation();
    auto ty = expr->GetResult()->GetType();
    auto token = GetBinaryExprKindLiteral(kind);
    auto builder =
        diag.DiagnoseRefactor(DiagKindRefactor::chir_arithmetic_operator_overflow, ToRange(loc), token);
    std::string hint = ty->ToString() + "(" + std::to_string(leftVal) + ") " + token + " " +
        expr->GetRHSOperand()->GetType()->ToString() + "(" + std::to_string(rightVal) + ")";
    builder.AddMainHintArguments(hint);
    builder.AddNote(GenerateTypeRangePrompt(expr->GetResult()->GetType()));
}

template <typename T>
bool CheckDivZero(BinaryExprKind kind, const Ptr<const BinaryExpressionBase>& binary, T rVal, DiagnosticEngine* diag)
{
    if (rVal == 0 && (kind == BinaryExprKind::DIV || kind == BinaryExprKind::MOD)) {
        if (diag != nullptr) {
            auto& loc = binary->GetDebugLocation();
            auto prompt = kind == BinaryExprKind::DIV ? "divide" : "modulo";
            auto builder = diag->DiagnoseRefactor(DiagKindRefactor::chir_divisor_is_zero, ToRange(loc), prompt);
            builder.AddMainHintArguments(prompt);
        }
        return true;
    }
    return false;
}

SIntDomain CheckSingleValueOverflow(
    const CHIRArithmeticBinopArgs& args, const Ptr<const BinaryExpressionBase>& expr, DiagnosticEngine* diag)
{
    auto exprKind = ExprKindMgr::ToExprKind(args.op);
    bool isOv = false;
    if (args.uns) {
        uint64_t a = args.ld.NumericBound().Lower().UVal();
        uint64_t b = args.rd.NumericBound().Lower().UVal();
        uint64_t res = 0;
        if (CheckDivZero(args.op, expr, b, diag)) {
            return SIntDomain::Top(args.ld.Width(), true);
        }
        isOv = OverflowChecker::IsUIntOverflow(args.l->GetType()->GetTypeKind(), exprKind, a, b, args.ov, &res);
        if (isOv && args.ov == OverflowStrategy::THROWING) {
            if (diag != nullptr) {
                RaiseArithmeticOverflowError(expr.get(), expr->GetOpKind(), a, b, *diag);
            }
            return SIntDomain::Top(args.ld.Width(), true);
        }
        return {ConstantRange{{args.ld.Width(), res}}, true};
    } else {
        int64_t a = args.ld.NumericBound().Lower().SVal();
        int64_t b = args.rd.NumericBound().Lower().SVal();
        int64_t res = 0;
        if (CheckDivZero(args.op, expr, b, diag)) {
            return SIntDomain::Top(args.ld.Width(), false);
        }
        isOv = OverflowChecker::IsIntOverflow(args.l->GetType()->GetTypeKind(), exprKind, a, b, args.ov, &res);
        if (isOv && args.ov == OverflowStrategy::THROWING) {
            if (diag != nullptr) {
                RaiseArithmeticOverflowError(expr.get(), expr->GetOpKind(), a, b, *diag);
            }
            return SIntDomain::Top(args.ld.Width(), false);
        }
        return {ConstantRange{{args.ld.Width(), static_cast<uint64_t>(res)}}, false};
    }
}

void RangeAnalysis::HandleBinaryExpr(RangeDomain& state, const BinaryExpressionBase* binaryExpr)
{
    auto dest = binaryExpr->GetResult();
    auto lhs = binaryExpr->GetLHSOperand();
    auto rhs = binaryExpr->GetRHSOperand();
    if (!CanAnalyse(dest->GetType()) || !CanAnalyse(lhs->GetType()) || !CanAnalyse(rhs->GetType())) {
        return state.SetToBound(binaryExpr->GetResult(), true);
    }
    auto opKind = binaryExpr->GetOpKind();
    if (dest->GetType()->IsInteger()) {
        if (!IsBasicBinaryExpr(*binaryExpr)) {
            return state.SetToBound(binaryExpr->GetResult(), true);
        }
        const auto& lRange = GetSIntDomainFromState(state, lhs);
        const auto& rRange = GetSIntDomainFromState(state, rhs);
        auto ov = binaryExpr->GetOverflowStrategy();
        auto isUnsigned = IsUnsignedArithmetic(*binaryExpr);
        if (lRange.IsSingleValue() && rRange.IsSingleValue()) {
            // Loop iterations can invalidate singleton operands. Only diagnose when replaying
            // the stable analysis results, while still computing conservative ranges here.
            auto domain = CheckSingleValueOverflow(
                CHIRArithmeticBinopArgs{lRange, rRange, lhs, rhs, opKind, ov, isUnsigned}, binaryExpr,
                isStable ? &diag : nullptr);
            state.Update(dest, std::make_unique<SIntRange>(domain));
            return;
        }
        auto res = ComputeArithmeticBinop(
            CHIRArithmeticBinopArgs{lRange, rRange, lhs, rhs, opKind, ov, isUnsigned});
        if (res.IsNonTrivial()) {
            return state.Update(dest, std::make_unique<SIntRange>(res));
        }
    }
    if (dest->GetType()->IsBoolean()) {
        auto res = GenerateBoolRangeFromBinaryOp(state, binaryExpr);
        if (res.IsNonTrivial()) {
            return state.Update(dest, std::make_unique<BoolRange>(res));
        }
    }
    state.SetToBound(binaryExpr->GetResult(), true);
}

SIntDomain RangeAnalysis::ComputeTypeCast(RangeDomain& state, PtrSymbol oldSymbol, const SIntDomain& v,
    IntWidth dstSize, bool dstUnsigned, OverflowStrategy ov) const
{
    auto numericRange{ComputeTypeCastNumericBound(v, dstSize, dstUnsigned, ov)};
    if (dstSize < v.Width() || v.IsUnsigned() || !dstUnsigned || ov == OverflowStrategy::SATURATING ||
        numericRange.SMinValue().Slt({dstSize, 0u})) {
        return {numericRange, dstUnsigned};
    }
    // signed to unsigned, same width or larger width
    // in this special case, if we have a symbolic range a<b and we know definitely b>=0 && a>=0,
    // this range can be preserved
    SIntDomain::SymbolicBoundsMap mp{};
    for (auto it = v.SymbolicBounds().Begin(); it != v.SymbolicBounds().End(); it++) {
        auto absVal = state.CheckAbstractValue(it->first);
        if (absVal != nullptr && absVal->GetRangeKind() == ValueRange::RangeKind::SINT) {
            auto range = StaticCast<SIntRange>(absVal)->GetVal();
            if (range.NumericBound().SMinValue().Sge(SInt{range.Width(), 0u})) {
                mp.emplace(it->first,
                    NumericConversion(it->second, dstSize, false, false,
                        OverflowStrategy::THROWING)); // this typecast can never wrap, pass THROWING for better
                                                      // performance
            }
        }
    }
    mp.emplace(oldSymbol, ConstantRange{{dstSize, 0u}});
    return SIntDomain{numericRange, std::move(mp), dstUnsigned};
}

void RangeAnalysis::HandleOthersExpr(RangeDomain& state, const Expression* expression)
{
    switch (expression->GetExprKind()) {
        case ExprKind::NUMERIC_CAST: {
            HandleTypeCast(state, StaticCast<const NumericCast*>(expression));
            break;
        }
        case ExprKind::CONSTANT:
        case ExprKind::APPLY:
        case ExprKind::FIELD:
            return;
        case ExprKind::TUPLE:
        default: {
            auto dest = expression->GetResult();
            return state.SetToTopOrTopRef(dest, dest->GetType()->IsRef());
        }
    }
}

std::optional<Block*> RangeAnalysis::HandleTerminatorEffect(RangeDomain& state, const Expression* terminator)
{
    RangeAnalysis::ExceptionKind res = ExceptionKind::NA;
    switch (terminator->GetExprKind()) {
        case ExprKind::GOTO:
        case ExprKind::EXIT:
            break;
        case ExprKind::BRANCH:
            return HandleBranchTerminator(state, StaticCast<const Branch*>(terminator));
        case ExprKind::MULTIBRANCH:
            return HandleMultiBranchTerminator(state, StaticCast<const MultiBranch*>(terminator));
        case ExprKind::TRY_NUMERIC_CAST:
            res = HandleTypeCast(state, StaticCast<const TryNumericCast*>(terminator));
            break;
        case ExprKind::TRY_NEG:
        case ExprKind::TRY_ADD:
        case ExprKind::TRY_SUB:
        case ExprKind::TRY_MUL:
        case ExprKind::TRY_DIV:
        case ExprKind::TRY_MOD:
        case ExprKind::TRY_EXP:
        case ExprKind::TRY_LSHIFT:
        case ExprKind::TRY_RSHIFT:
        case ExprKind::TRY_INTRINSIC:
        default: {
            auto dest = terminator->GetResult();
            if (dest) {
                state.SetToTopOrTopRef(dest, dest->GetType()->IsRef());
            }
            break;
        }
    }
    if (res == ExceptionKind::SUCCESS) {
        return terminator->GetSuccessor(0);
    } else if (res == ExceptionKind::FAIL) {
        return terminator->GetSuccessor(1);
    }

    return std::nullopt;
}

std::optional<Block*> RangeAnalysis::HandleBranchTerminator(const RangeDomain& state, const Branch* branch) const
{
    auto cond = branch->GetCondition();
    const auto& condVal = GetBoolDomainFromState(state, cond);
    if (!condVal.IsSingleValue()) {
        return std::nullopt;
    }
    return condVal.IsTrue() ? branch->GetTrueBlock() : branch->GetFalseBlock();
}

std::optional<Block*> RangeAnalysis::HandleMultiBranchTerminator(
    const RangeDomain& state, const MultiBranch* multi) const
{
    auto cond = multi->GetCondition();
    const auto& condVal = GetSIntDomainFromState(state, cond);
    if (!condVal.IsSingleValue()) {
        return std::nullopt;
    }
    auto val = condVal.NumericBound().Lower().UVal();
    auto cases = multi->GetCaseVals();
    for (size_t i = 0; i < cases.size(); ++i) {
        if (val == cases[i]) {
            return multi->GetSuccessor(i + 1);
        }
    }
    return multi->GetDefaultBlock();
}
} // namespace Cangjie::CHIR
