// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements constant analysis.
 */

#include "cangjie/CHIR/Analysis/ConstAnalysis.h"

#include <cmath>

namespace Cangjie::CHIR {
ConstValue::ConstValue(ConstKind kind) : kind(kind)
{
}

ConstValue::~ConstValue()
{
}

ConstValue::ConstKind ConstValue::GetConstKind() const
{
    return kind;
}

std::optional<std::unique_ptr<ConstValue>> ConstBoolVal::Join(const ConstValue& rhs) const
{
    if (rhs.GetConstKind() == ConstKind::BOOL && this->val == StaticCast<const ConstBoolVal&>(rhs).val) {
        return std::nullopt;
    }
    return nullptr;
}

std::string ConstBoolVal::ToString() const
{
    return val ? "true" : "false";
}

std::unique_ptr<ConstValue> ConstBoolVal::Clone() const
{
    return std::make_unique<ConstBoolVal>(val);
}

bool ConstBoolVal::GetVal() const
{
    return val;
}

std::optional<std::unique_ptr<ConstValue>> ConstRuneVal::Join(const ConstValue& rhs) const
{
    if (rhs.GetConstKind() == ConstKind::RUNE && this->val == StaticCast<const ConstRuneVal&>(rhs).val) {
        return std::nullopt;
    }
    return nullptr;
}

std::string ConstRuneVal::ToString() const
{
    return std::to_string(val);
}

std::unique_ptr<ConstValue> ConstRuneVal::Clone() const
{
    return std::make_unique<ConstRuneVal>(val);
}

char32_t ConstRuneVal::GetVal() const
{
    return val;
}

std::optional<std::unique_ptr<ConstValue>> ConstStrVal::Join(const ConstValue& rhs) const
{
    if (rhs.GetConstKind() == ConstKind::STRING && this->val == StaticCast<const ConstStrVal&>(rhs).val) {
        return std::nullopt;
    }
    return nullptr;
}

std::string ConstStrVal::ToString() const
{
    return val;
}

std::unique_ptr<ConstValue> ConstStrVal::Clone() const
{
    return std::make_unique<ConstStrVal>(val);
}

std::string ConstStrVal::GetVal() const
{
    return val;
}

std::optional<std::unique_ptr<ConstValue>> ConstUIntVal::Join(const ConstValue& rhs) const
{
    if (rhs.GetConstKind() == ConstKind::UINT && this->val == StaticCast<const ConstUIntVal&>(rhs).val) {
        return std::nullopt;
    }
    return nullptr;
}

std::string ConstUIntVal::ToString() const
{
    return std::to_string(val);
}

std::unique_ptr<ConstValue> ConstUIntVal::Clone() const
{
    return std::make_unique<ConstUIntVal>(val);
}

uint64_t ConstUIntVal::GetVal() const
{
    return val;
}

std::optional<std::unique_ptr<ConstValue>> ConstIntVal::Join(const ConstValue& rhs) const
{
    if (rhs.GetConstKind() == ConstKind::INT && this->val == StaticCast<const ConstIntVal&>(rhs).val) {
        return std::nullopt;
    }
    return nullptr;
}

std::string ConstIntVal::ToString() const
{
    return std::to_string(val);
}

std::unique_ptr<ConstValue> ConstIntVal::Clone() const
{
    return std::make_unique<ConstIntVal>(val);
}

int64_t ConstIntVal::GetVal() const
{
    return val;
}

std::optional<std::unique_ptr<ConstValue>> ConstFloatVal::Join(const ConstValue& rhs) const
{
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfloat-equal"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
#endif
    // this float equal is intentional
    if (rhs.GetConstKind() == ConstKind::FLOAT && this->val == StaticCast<const ConstFloatVal&>(rhs).val) {
#if defined(__clang__)
#pragma clang diagnostic pop
#else
#pragma GCC diagnostic pop
#endif
        return std::nullopt;
    }
    return nullptr;
}

std::string ConstFloatVal::ToString() const
{
    return std::to_string(val);
}

std::unique_ptr<ConstValue> ConstFloatVal::Clone() const
{
    return std::make_unique<ConstFloatVal>(val);
}

double ConstFloatVal::GetVal() const
{
    return val;
}

template <> const std::string Analysis<ConstDomain>::name = "const-analysis";
template <> const std::optional<unsigned> Analysis<ConstDomain>::blockLimit = std::nullopt;
template <> ConstDomain::ChildrenMap ValueAnalysis<ConstValueDomain>::globalChildrenMap{};
template <> ConstDomain::AllocatedRefMap ValueAnalysis<ConstValueDomain>::globalAllocatedRefMap{};
template <> ConstDomain::AllocatedObjMap ValueAnalysis<ConstValueDomain>::globalAllocatedObjMap{};
template <> std::vector<std::unique_ptr<Ref>> ValueAnalysis<ConstValueDomain>::globalRefPool{};
template <> std::vector<std::unique_ptr<AbstractObject>> ValueAnalysis<ConstValueDomain>::globalAbsObjPool{};
template <>
ConstDomain ValueAnalysis<ConstValueDomain>::globalState{&globalChildrenMap, &globalAllocatedRefMap,
    nullptr, &globalAllocatedObjMap, &globalRefPool, &globalAbsObjPool};

ConstAnalysis::ConstAnalysis(const Func* func, CHIRBuilder& builder, bool isDebug, DiagAdapter* diag)
    : ValueAnalysis(func, builder, isDebug), diag(diag)
{
}

ConstAnalysis::~ConstAnalysis()
{
}

void ConstAnalysis::PrintDebugMessage(const Expression* expr, const ConstValue* absVal) const
{
    std::string message = "[ConstAnalysis] The const value of " + expr->GetExprKindName() +
        ToPosInfo(expr->GetDebugLocation()) + " has been set to " + absVal->ToString() + "\n";
    std::cout << message;
}

void ConstAnalysis::MarkExpressionAsMustNotOverflow(Expression& expr) const
{
    if (this->isStable) {
        expr.Set<NeverOverflowInfo>(true);
    }
}

void ConstAnalysis::HandleArithmeticOpOfFloat(
    ConstDomain& state, const BinaryExpression* binaryExpr, const ConstValue* lhs, const ConstValue* rhs) const
{
    auto dest = binaryExpr->GetResult();
    if (!lhs || !rhs) {
        return state.SetToBound(dest, /* isTop = */ true);
    }

    auto left = StaticCast<const ConstFloatVal*>(lhs);
    auto right = StaticCast<const ConstFloatVal*>(rhs);

    double res = 0.0;
    switch (binaryExpr->GetExprKind()) {
        case ExprKind::ADD:
            res = left->GetVal() + right->GetVal();
            break;
        case ExprKind::SUB:
            res = left->GetVal() - right->GetVal();
            break;
        case ExprKind::MUL:
            res = left->GetVal() * right->GetVal();
            break;
        case ExprKind::DIV:
            res = left->GetVal() / right->GetVal();
            break;
        default:
            CJC_ABORT();
    }

    if (std::isnan(res) || std::isinf(res)) {
        state.SetToBound(dest, /* isTop = */ true);
    } else {
        res = CutOffHighBits(res, dest->GetType()->GetTypeKind());
        state.Update(dest, std::make_unique<ConstFloatVal>(res));
    }
}

void ConstAnalysis::HandleNormalExpressionEffect(ConstDomain& state, const Expression* expression)
{
    auto exceptionKind = ExceptionKind::NA;
    switch (expression->GetExprMajorKind()) {
        case ExprMajorKind::MEMORY_EXPR:
            return;
        case ExprMajorKind::UNARY_EXPR:
            HandleUnaryExpr(state, StaticCast<const UnaryExpression*>(expression), exceptionKind);
            break;
        case ExprMajorKind::BINARY_EXPR:
            HandleBinaryExpr(state, StaticCast<const BinaryExpression*>(expression), exceptionKind);
            break;
        case ExprMajorKind::OTHERS:
            HandleOthersExpr(state, expression, exceptionKind);
            break;
        case ExprMajorKind::STRUCTURED_CTRL_FLOW_EXPR:
        default: {
            CJC_ABORT();
            return;
        }
    }
    if (exceptionKind == ExceptionKind::SUCCESS) {
        MarkExpressionAsMustNotOverflow(*const_cast<Expression*>(expression));
    }
    if (expression->GetExprMajorKind() != ExprMajorKind::UNARY_EXPR &&
        expression->GetExprMajorKind() != ExprMajorKind::BINARY_EXPR &&
        expression->GetExprKind() != ExprKind::TYPECAST) {
        return;
        }
    auto exprType = expression->GetResult()->GetType();
    if (isDebug && (exprType->IsInteger() || exprType->IsFloat() || exprType->IsRune() || exprType->IsBoolean() ||
        exprType->IsString())) {
        if (auto absVal = state.CheckAbstractValue(expression->GetResult()); absVal) {
            PrintDebugMessage(expression, absVal);
        }
        }
}

void ConstAnalysis::HandleUnaryExpr(ConstDomain& state, const UnaryExpression* unaryExpr, ExceptionKind& exceptionKind)
{
    auto operand = unaryExpr->GetOperand();
    auto dest = unaryExpr->GetResult();
    const ConstValue* absVal = state.CheckAbstractValue(operand);
    if (absVal == nullptr) {
        return state.SetToBound(dest, /* isTop = */ true);
    }
    switch (unaryExpr->GetExprKind()) {
        case ExprKind::NEG: {
            if (absVal->GetConstKind() == ConstValue::ConstKind::UINT) {
                exceptionKind = HandleNegOpOfInt<ConstUIntVal>(state, unaryExpr, absVal);
                return;
            } else if (absVal->GetConstKind() == ConstValue::ConstKind::INT) {
                exceptionKind = HandleNegOpOfInt<ConstIntVal>(state, unaryExpr, absVal);
                return;
            } else if (absVal->GetConstKind() == ConstValue::ConstKind::FLOAT) {
                auto res = -(StaticCast<const ConstFloatVal*>(absVal)->GetVal());
                res = CutOffHighBits(res, dest->GetType()->GetTypeKind());
                if (std::isnan(res) || std::isinf(res)) {
                    return state.SetToBound(dest, /* isTop = */ true);
                } else {
                    return state.Update(dest, std::make_unique<ConstFloatVal>(res));
                }
            }
            CJC_ABORT();
        }
        case ExprKind::NOT: {
            auto boolVal = StaticCast<const ConstBoolVal*>(absVal)->GetVal();
            return state.Update(dest, std::make_unique<ConstBoolVal>(!boolVal));
        }
        case ExprKind::BITNOT: {
            if (absVal->GetConstKind() == ConstValue::ConstKind::UINT) {
                auto uintVal = StaticCast<const ConstUIntVal*>(absVal)->GetVal();
                uintVal = CutOffHighBits(~uintVal, operand->GetType()->GetTypeKind());
                return state.Update(dest, std::make_unique<ConstUIntVal>(uintVal));
            } else {
                auto intVal = StaticCast<const ConstIntVal*>(absVal)->GetVal();
                intVal = CutOffHighBits(~intVal, operand->GetType()->GetTypeKind());
                return state.Update(dest, std::make_unique<ConstIntVal>(intVal));
            }
        }
        default:
            CJC_ABORT();
    }
}

template <> bool IsTrackedGV<ValueDomain<ConstValue>>(const GlobalVar& gv)
{
    auto baseTyKind = StaticCast<RefType*>(gv.GetType())->GetBaseType()->GetTypeKind();
    return (baseTyKind >= Type::TYPE_INT8 && baseTyKind <= Type::TYPE_UNIT) || baseTyKind == Type::TYPE_TUPLE ||
        baseTyKind == Type::TYPE_STRUCT || baseTyKind == Type::TYPE_ENUM;
}

template <>
ValueDomain<ConstValue> HandleNonNullLiteralValue<ValueDomain<ConstValue>>(const LiteralValue* literalValue)
{
    if (literalValue->IsBoolLiteral()) {
        return ValueDomain<ConstValue>(
            std::make_unique<ConstBoolVal>(StaticCast<BoolLiteral*>(literalValue)->GetVal()));
    } else if (literalValue->IsFloatLiteral()) {
        // There is no proper types to represent `Float16` in Cangjie.
        if (literalValue->GetType()->GetTypeKind() != Type::TypeKind::TYPE_FLOAT16) {
            return ValueDomain<ConstValue>(
                std::make_unique<ConstFloatVal>(StaticCast<FloatLiteral*>(literalValue)->GetVal()));
        } else {
            return ValueDomain<ConstValue>(/* isTop = */true);
        }
    } else if (literalValue->IsIntLiteral()) {
        if (auto intTy = StaticCast<IntType*>(literalValue->GetType()); intTy->IsSigned()) {
            return ValueDomain<ConstValue>(
                std::make_unique<ConstIntVal>(StaticCast<IntLiteral*>(literalValue)->GetSignedVal()));
        } else {
            return ValueDomain<ConstValue>(
                std::make_unique<ConstUIntVal>(StaticCast<IntLiteral*>(literalValue)->GetUnsignedVal()));
        }
    } else if (literalValue->IsRuneLiteral()) {
        return ValueDomain<ConstValue>(
            std::make_unique<ConstRuneVal>(StaticCast<RuneLiteral*>(literalValue)->GetVal()));
    } else if (literalValue->IsStringLiteral()) {
        return ValueDomain<ConstValue>(
                std::make_unique<ConstStrVal>(StaticCast<StringLiteral*>(literalValue)->GetVal()));
    } else if (literalValue->IsUnitLiteral()) {
        return ValueDomain<ConstValue>(/* isTop = */true);
    } else {
        InternalError("Unsupported const val kind");
        return ValueDomain<ConstValue>(/* isTop = */true);
    }
}

void ConstAnalysis::HandleBinaryExpr(
    ConstDomain& state, const BinaryExpression* binaryExpr, ExceptionKind& exceptionKind)
{
    auto kind = binaryExpr->GetExprKind();
    switch (kind) {
        case ExprKind::ADD:
        case ExprKind::SUB:
        case ExprKind::MUL:
        case ExprKind::DIV:
        case ExprKind::MOD: {
            exceptionKind = HandleArithmeticOp(state, binaryExpr, kind);
            return;
        }
        case ExprKind::EXP: {
            exceptionKind = HandleExpOp(state, binaryExpr);
            return;
        }
        case ExprKind::LSHIFT:
        case ExprKind::RSHIFT:
        case ExprKind::BITAND:
        case ExprKind::BITXOR:
        case ExprKind::BITOR:
            return (void)HandleBitwiseOp(state, binaryExpr, kind);
        case ExprKind::LT:
        case ExprKind::GT:
        case ExprKind::LE:
        case ExprKind::GE:
        case ExprKind::EQUAL:
        case ExprKind::NOTEQUAL:
            return HandleRelationalOp(state, binaryExpr);
        case ExprKind::AND:
        case ExprKind::OR:
            return HandleLogicalOp(state, binaryExpr);
        default:
            CJC_ABORT();
    }
}

void ConstAnalysis::HandleOthersExpr(ConstDomain& state, const Expression* expression, ExceptionKind& exceptionKind)
{
    switch (expression->GetExprKind()) {
        case ExprKind::TYPECAST: {
            exceptionKind = HandleTypeCast(state, StaticCast<const TypeCast*>(expression));
            return;
        }
        case ExprKind::INTRINSIC: {
            return (void)HandleIntrinsic(state, StaticCast<const Intrinsic*>(expression));
        }
        case ExprKind::CONSTANT:
        case ExprKind::APPLY:
        case ExprKind::FIELD:
            return;
        default: {
            auto dest = expression->GetResult();
            return state.SetToTopOrTopRef(dest, /* isRef = */ dest->GetType()->IsRef());
        }
    }
}

// a<b, a>b, a<=b, a>=b, a!=b, a==b
void ConstAnalysis::HandleRelationalOp(ConstDomain& state, const BinaryExpression* binaryExpr)
{
    auto kind = binaryExpr->GetExprKind();
    auto dest = binaryExpr->GetResult();
    auto lhs = binaryExpr->GetLHSOperand();
    auto rhs = binaryExpr->GetRHSOperand();
    auto lhsTy = lhs->GetType();
    CJC_ASSERT(lhsTy == rhs->GetType());
    if (lhsTy->IsUnit() || (lhs == rhs && !lhsTy->IsFloat())) {
        bool res = true;
        if (kind == ExprKind::LT || kind == ExprKind::GT || kind == ExprKind::NOTEQUAL) {
            res = false;
        }
        return state.Update(dest, std::make_unique<ConstBoolVal>(res));
    }
    const ConstValue* lhsAbsVal = state.CheckAbstractValue(lhs);
    const ConstValue* rhsAbsVal = state.CheckAbstractValue(rhs);
    if (!lhsAbsVal || !rhsAbsVal) {
        return state.SetToBound(dest, /* isTop = */ true);
    }
    switch (lhsAbsVal->GetConstKind()) {
        case ConstValue::ConstKind::UINT:
            return HandleRelationalOpOfType<ConstUIntVal>(state, binaryExpr, lhsAbsVal, rhsAbsVal);
        case ConstValue::ConstKind::INT:
            return HandleRelationalOpOfType<ConstIntVal>(state, binaryExpr, lhsAbsVal, rhsAbsVal);
        case ConstValue::ConstKind::FLOAT:
            return HandleRelationalOpOfType<ConstFloatVal>(state, binaryExpr, lhsAbsVal, rhsAbsVal);
        case ConstValue::ConstKind::BOOL:
            return HandleRelationalOpOfType<ConstBoolVal>(state, binaryExpr, lhsAbsVal, rhsAbsVal);
        case ConstValue::ConstKind::RUNE:
            return HandleRelationalOpOfType<ConstRuneVal>(state, binaryExpr, lhsAbsVal, rhsAbsVal);
        case ConstValue::ConstKind::STRING:
            return HandleRelationalOpOfType<ConstStrVal>(state, binaryExpr, lhsAbsVal, rhsAbsVal);
        default:
            CJC_ABORT();
    }
}

// a&&b, a||b
void ConstAnalysis::HandleLogicalOp(ConstDomain& state, const BinaryExpression* binaryExpr) const
{
    auto lhs = binaryExpr->GetLHSOperand();
    auto rhs = binaryExpr->GetRHSOperand();
    auto dest = binaryExpr->GetResult();
    auto lhsVal = RawStaticCast<const ConstBoolVal*>(state.CheckAbstractValue(lhs));
    auto rhsVal = RawStaticCast<const ConstBoolVal*>(state.CheckAbstractValue(rhs));
    if (binaryExpr->GetExprKind() == ExprKind::AND) {
        if (lhsVal && rhsVal) {
            return state.Update(dest, std::make_unique<ConstBoolVal>(lhsVal->GetVal() && rhsVal->GetVal()));
        } else if ((lhsVal && !lhsVal->GetVal()) || (rhsVal && !rhsVal->GetVal())) {
            return state.Update(dest, std::make_unique<ConstBoolVal>(false));
        }
    } else {
        if (lhsVal && rhsVal) {
            return state.Update(dest, std::make_unique<ConstBoolVal>(lhsVal->GetVal() && rhsVal->GetVal()));
        } else if ((lhsVal && lhsVal->GetVal()) || (rhsVal && rhsVal->GetVal())) {
            return state.Update(dest, std::make_unique<ConstBoolVal>(true));
        }
    }
    return state.SetToBound(dest, /* isTop = */ true);
}

std::optional<Block*> ConstAnalysis::HandleTerminatorEffect(ConstDomain& state, const Terminator* terminator)
{
    ConstAnalysis::ExceptionKind res = ExceptionKind::NA;
    switch (terminator->GetExprKind()) {
        // already handled by the framework
        // case ExprKind::ALLOCATE_WITH_EXCEPTION:
        // case ExprKind::RAW_ARRAY_ALLOCATE_WITH_EXCEPTION:
        // case ExprKind::RAW_ARRAY_LITERAL_ALLOCATE_WITH_EXCEPTION:
        // case ExprKind::APPLY_WITH_EXCEPTION:
        // case ExprKind::INVOKE_WITH_EXCEPTION:
        case ExprKind::GOTO:
        case ExprKind::EXIT:
            break;
        case ExprKind::BRANCH:
            return HandleBranchTerminator(state, StaticCast<const Branch*>(terminator));
        case ExprKind::MULTIBRANCH:
            return HandleMultiBranchTerminator(state, StaticCast<const MultiBranch*>(terminator));
        case ExprKind::TYPECAST_WITH_EXCEPTION:
            res = HandleTypeCast(state, StaticCast<const TypeCastWithException*>(terminator));
            break;
        case ExprKind::INT_OP_WITH_EXCEPTION:
            res = HandleIntOpWithExcepTerminator(state, StaticCast<const IntOpWithException*>(terminator));
            break;
        case ExprKind::INTRINSIC_WITH_EXCEPTION:
            res = HandleIntrinsic(state, StaticCast<const IntrinsicWithException*>(terminator));
            break;
        default: {
            auto dest = terminator->GetResult();
            if (dest) {
                state.SetToTopOrTopRef(dest, dest->GetType()->IsRef());
            }
            break;
        }
    }
    if (res == ExceptionKind::SUCCESS) {
        MarkExpressionAsMustNotOverflow(*const_cast<Terminator*>(terminator));
        return terminator->GetSuccessor(0);
    } else if (res == ExceptionKind::FAIL) {
        return terminator->GetSuccessor(1);
    }

    return std::nullopt;
}

std::optional<Block*> ConstAnalysis::HandleBranchTerminator(const ConstDomain& state, const Branch* branch) const
{
    auto cond = branch->GetCondition();
    if (auto condVal = state.CheckAbstractValue(cond); condVal) {
        auto boolVal = StaticCast<const ConstBoolVal*>(condVal)->GetVal();
        return boolVal ? branch->GetTrueBlock() : branch->GetFalseBlock();
    }
    return std::nullopt;
}

std::optional<Block*> ConstAnalysis::HandleMultiBranchTerminator(
    const ConstDomain& state, const MultiBranch* multi) const
{
    auto cond = multi->GetCondition();
    if (auto condVal = state.CheckAbstractValue(cond); condVal) {
        auto intVal = static_cast<const ConstUIntVal*>(condVal)->GetVal();
        auto cases = multi->GetCaseVals();
        for (size_t i = 0; i < cases.size(); ++i) {
            if (intVal == cases[i]) {
                return multi->GetCaseBlockByIndex(i);
            }
        }
        return multi->GetDefaultBlock();
    }
    return std::nullopt;
}

ConstAnalysis::ExceptionKind ConstAnalysis::HandleIntOpWithExcepTerminator(
    ConstDomain& state, const IntOpWithException* intOp)
{
    auto kind = intOp->GetOpKind();
    if (kind == ExprKind::NEG) {
        auto operand = intOp->GetOperand(0);
        auto absVal = state.CheckAbstractValue(operand);
        if (!absVal) {
            state.SetToBound(intOp->GetResult(), /* isTop = */ true);
            return ExceptionKind::NA;
        }
        if (absVal->GetConstKind() == ConstValue::ConstKind::UINT) {
            return HandleNegOpOfInt<ConstUIntVal>(state, intOp, absVal);
        } else {
            return HandleNegOpOfInt<ConstIntVal>(state, intOp, absVal);
        }
    }
    if (kind >= ExprKind::ADD && kind < ExprKind::EXP) {
        return HandleArithmeticOp(state, intOp, kind);
    }
    if (kind == ExprKind::EXP) {
        return HandleExpOp(state, intOp);
    }
    if (kind >= ExprKind::LSHIFT && kind <= ExprKind::RSHIFT) {
        return HandleBitwiseOp(state, intOp, kind);
    }
    InternalError("Unsupported IntOpWithException terminator");
    return ExceptionKind::NA;
}

void ConstAnalysis::HandleApplyExpr(ConstDomain& state, const Apply* apply, Value* refObj)
{
    HandleApply(state, apply, refObj);
}

std::optional<Block*> ConstAnalysis::HandleApplyWithExceptionTerminator(
    ConstDomain& state, const ApplyWithException* apply, Value* refObj)
{
    auto res = HandleApply(state, apply, refObj);
    if (res == ExceptionKind::SUCCESS) {
        return apply->GetSuccessBlock();
    } else if (res == ExceptionKind::FAIL) {
        return apply->GetErrorBlock();
    } else {
        return std::nullopt;
    }
}
}  // namespace Cangjie::CHIR