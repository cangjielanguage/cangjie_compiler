// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/IR/Expression/Expression.h"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <tuple>

#include "cangjie/CHIR/AST2CHIR/Utils.h"
#include "cangjie/CHIR/Utils/CHIRCasting.h"
#include "cangjie/CHIR/IR/CHIRBuilder.h"
#include "cangjie/CHIR/IR/Expression/Terminator.h"
#include "cangjie/CHIR/Utils/ToStringUtils.h"
#include "cangjie/CHIR/IR/Type/ClassDef.h"
#include "cangjie/CHIR/IR/Type/Type.h"
#include "cangjie/CHIR/Utils/Utils.h"
#include "cangjie/CHIR/IR/Value/Value.h"
#include "cangjie/CHIR/Utils/Visitor/Visitor.h"
#include "cangjie/Utils/CheckUtils.h"

using namespace Cangjie::CHIR;

namespace {
uint64_t g_lambdaClonedIdx = 0;

std::pair<size_t, size_t> GetAllocExprOfRetVal(const BlockGroup& body)
{
    auto blocks = body.GetBlocks();
    for (size_t i = 0; i < blocks.size(); ++i) {
        auto exprs = blocks[i]->GetExpressions();
        for (size_t j = 0; j < exprs.size(); ++j) {
            auto res = exprs[j]->GetResult();
            if (res != nullptr && res->IsRetValue()) {
                return {i, j};
            }
        }
    }
    CJC_ABORT();
    return {0, 0};
}
}

Expression::Expression(
    ExprKind kind, const std::vector<Value*>& operands, const std::vector<BlockGroup*>& blockGroups, Block* parent)
    : kind(kind), operands(operands), blockGroups(blockGroups), parent(parent)
{
    CJC_NULLPTR_CHECK(parent);
    for (auto op : operands) {
        // note: a hack here, remove later
        if (op == nullptr) {
            continue;
        }
        op->AddUserOnly(this);
    }
    for (auto blockGroup : blockGroups) {
        blockGroup->AddUserOnly(this);
    }
}

bool Expression::IsConstantNull() const
{
    if (kind == ExprKind::CONSTANT) {
        auto constant = StaticCast<const Constant*>(this);
        return constant->IsNullLit();
    }
    return false;
}

bool Expression::IsConstantInt() const
{
    if (kind == ExprKind::CONSTANT) {
        auto constant = StaticCast<const Constant*>(this);
        return constant->IsIntLit();
    }
    return false;
}

bool Expression::IsConstantBool() const
{
    if (kind == ExprKind::CONSTANT) {
        auto constant = StaticCast<const Constant*>(this);
        return constant->IsBoolLit();
    }
    return false;
}

bool Expression::IsConstantString() const
{
    if (kind == ExprKind::CONSTANT) {
        auto constant = StaticCast<const Constant*>(this);
        return constant->IsStringLit();
    }
    return false;
}

ExprMajorKind Expression::GetExprMajorKind() const
{
    return ExprKindMgr::Instance()->GetMajorKind(kind);
}

ExprKind Expression::GetExprKind() const
{
    return kind;
}


bool Expression::IsTerminator() const
{
    return ExprKindMgr::Instance()->GetMajorKind(kind) == ExprMajorKind::TERMINATOR;
}

std::string Expression::GetExprKindName() const
{
    return ExprKindMgr::Instance()->GetKindName(static_cast<size_t>(kind));
}

Block* Expression::GetParentBlock() const
{
    return parent;
}

BlockGroup* Expression::GetFuncOrLambdaBody() const
{
    if (this->GetExprKind() == ExprKind::LAMBDA) {
        return StaticCast<Lambda*>(this)->GetBody();
    }
    auto blockGroup = GetParentBlockGroup();
    if (auto ownerFunc = blockGroup->GetOwnerFunc()) {
        return ownerFunc->GetBody();
    }
    auto ownerExpr = blockGroup->GetOwnerExpression();
    CJC_NULLPTR_CHECK(ownerExpr);
    if (auto ownerLambda = DynamicCast<Lambda*>(ownerExpr)) {
        return ownerLambda->GetBody();
    }
    return ownerExpr->GetFuncOrLambdaBody();
}

const std::vector<BlockGroup*>& Expression::GetBlockGroups() const
{
    return blockGroups;
}

size_t Expression::GetNumOfOperands() const
{
    return operands.size();
}

size_t Expression::GetNumOfNonSuccessorOperands() const
{
    return GetSuccessorIndex(0);
}

std::vector<Value*> Expression::GetOperands() const
{
    return operands;
}

Value* Expression::GetOperand(size_t idx) const
{
    CJC_ASSERT(idx < GetNumOfNonSuccessorOperands());
    return operands[idx];
}

std::vector<Value*> Expression::GetNonSuccessorOperands() const
{
    auto num = GetNumOfNonSuccessorOperands();
    CJC_ASSERT(operands.size() >= num);
    return {operands.begin(), operands.begin() + static_cast<long>(num)};
}

std::vector<Block*> Expression::GetSuccessors() const
{
    std::vector<Block*> succs;
    for (auto op : operands) {
        if (op->IsBlock()) {
            succs.emplace_back(StaticCast<Block*>(op));
        }
    }
    return succs;
}

size_t Expression::GetSuccessorIndex(size_t index) const
{
    size_t firstSuccIdx = 0;
    while (firstSuccIdx < operands.size() && !operands[firstSuccIdx]->IsBlock()) {
        ++firstSuccIdx;
    }
    return firstSuccIdx + index;
}

size_t Expression::GetNumOfSuccessor() const
{
    return operands.size() - GetSuccessorIndex(0);
}

Block* Expression::GetSuccessor(size_t index) const
{
    return StaticCast<Block*>(operands[GetSuccessorIndex(index)]);
}

LocalVar* Expression::GetResult() const
{
    return result;
}

Type* Expression::GetResultType() const
{
    return result ? result->GetType() : nullptr;
}

/**
 * @brief Get the block group to which the expression belongs.
 */
BlockGroup* Expression::GetParentBlockGroup() const
{
    CJC_NULLPTR_CHECK(parent);
    return parent->GetParentBlockGroup();
}

Function* Expression::GetTopLevelFunc() const
{
    if (auto blockGroup = GetParentBlockGroup(); blockGroup != nullptr) {
        return blockGroup->GetTopLevelFunc();
    }
    return nullptr;
}

/**
 * @brief Remove this expression from its parent basicblock.
 */
void Expression::RemoveSelfFromBlock()
{
    for (auto suc : GetSuccessors()) {
        suc->RemovePredecessor(*parent);
    }
    if (parent != nullptr) {
        parent->RemoveExprOnly(*this);
        parent = nullptr;
    }
    for (auto blockGroup : blockGroups) {
        blockGroup->ClearBlockGroup();
    }
    EraseOperands();
}

void Expression::ReplaceWith(Expression& newExpr)
{
    // 1. replace result
    if (result != nullptr) {
        CJC_NULLPTR_CHECK(newExpr.GetResult());
        for (auto user : result->GetUsers()) {
            user->ReplaceOperand(result, newExpr.GetResult());
        }
    }

    // 2. store current parent and the position of this expression in the parent block
    CJC_NULLPTR_CHECK(parent);
    auto tempParent = parent;
    auto pos = std::find(tempParent->exprs.begin(), tempParent->exprs.end(), this);
    CJC_ASSERT(pos != tempParent->exprs.end());
    auto idx = static_cast<size_t>(pos - tempParent->exprs.begin());

    // 3. remove this expression from its parent block
    RemoveSelfFromBlock();

    // 4. remove the new expression from its parent block
    if (newExpr.parent != nullptr) {
        newExpr.parent->RemoveExprOnly(newExpr);
    }

    // 5. set new parent
    newExpr.SetParent(tempParent);

    // 6. insert the new expression into the parent block at the same position
    tempParent->exprs.insert(tempParent->exprs.begin() + static_cast<std::ptrdiff_t>(idx), &newExpr);

    // 7. add the successors of the new expression to the parent block
    for (auto suc : newExpr.GetSuccessors()) {
        suc->AddPredecessor(tempParent);
    }
}

/**
 * @brief Remove this expression from its parent basicblock, and insert it into before the @expr expression.
 *
 * @param expr The destination position which is before this reference expression.
 *
 */
void Expression::MoveBefore(Expression* expr)
{
    CJC_ASSERT(!this->IsTerminator());
    CJC_NULLPTR_CHECK(expr);
    if (this == expr) {
        return;
    }
    // 1. remove current expr from parent block
    if (parent != nullptr) {
        parent->RemoveExprOnly(*this);
    }

    // 2. insert current expr before `expr`
    CJC_NULLPTR_CHECK(expr->parent);
    CJC_ASSERT(this->GetParentBlockGroup() == expr->GetParentBlockGroup());
    auto pos = std::find(expr->parent->exprs.begin(), expr->parent->exprs.end(), expr);
    CJC_ASSERT(pos != expr->parent->exprs.end());
    expr->parent->exprs.insert(pos, this);

    // 3. change current expr's parent
    parent = expr->parent;
}

/*
 * @brief Remove this expression from its parent basicblock, and insert it into after the @expr expression.
 *
 * @param expr The destination position which is after this reference expression.
 *
 */
void Expression::MoveAfter(Expression* expr)
{
    CJC_NULLPTR_CHECK(expr);
    CJC_ASSERT(this->GetParentBlockGroup() == expr->GetParentBlockGroup());
    // you shouldn't move an expression after a terminator, that's illegal ir
    CJC_ASSERT(!expr->IsTerminator());
    if (parent != nullptr) {
        parent->RemoveExprOnly(*this);
        for (auto suc : GetSuccessors()) {
            suc->RemovePredecessor(*parent);
        }
    }
    CJC_NULLPTR_CHECK(expr->parent);
    auto pos = std::find(expr->parent->exprs.begin(), expr->parent->exprs.end(), expr);
    CJC_ASSERT(pos != expr->parent->exprs.end());
    expr->parent->exprs.insert(pos + 1, this);
    parent = expr->parent;
}

void Expression::MoveTo(Block& block)
{
    CJC_NULLPTR_CHECK(parent);
    CJC_ASSERT(this->GetParentBlockGroup() == block.GetParentBlockGroup());
    parent->RemoveExprOnly(*this);
    for (auto suc : GetSuccessors()) {
        suc->RemovePredecessor(*parent);
    }
    block.AppendExpression(this);
}

void Expression::SetParent(Block* newParent)
{
    parent = newParent;
}

void Expression::ReplaceOperand(Value* oldOperand, Value* newOperand)
{
    CJC_NULLPTR_CHECK(oldOperand);
    CJC_NULLPTR_CHECK(newOperand);
    if (oldOperand == newOperand || std::find(operands.begin(), operands.end(), oldOperand) == operands.end()) {
        return;
    }
    std::replace(operands.begin(), operands.end(), oldOperand, newOperand);
    // Block successors update CFG edges (and users) via predecessor APIs.
    if (auto block = DynamicCast<Block*>(oldOperand)) {
        block->RemovePredecessor(*GetParentBlock());
    } else {
        oldOperand->RemoveUserOnly(this);
    }
    if (auto block = DynamicCast<Block*>(newOperand)) {
        block->AddPredecessor(GetParentBlock());
    } else {
        newOperand->AddUserOnly(this);
    }
}

void Expression::ReplaceOperand(size_t idx, Value* newOperand)
{
    CJC_ASSERT(idx < operands.size());
    CJC_NULLPTR_CHECK(newOperand);
    auto oldOperand = operands[idx];
    if (oldOperand == newOperand) {
        return;
    }
    operands[idx] = newOperand;
    if (auto block = DynamicCast<Block*>(newOperand)) {
        block->AddPredecessor(GetParentBlock());
    } else {
        newOperand->AddUserOnly(this);
    }
    if (std::find(operands.begin(), operands.end(), oldOperand) != operands.end()) {
        return;
    }
    if (auto block = DynamicCast<Block*>(oldOperand)) {
        block->RemovePredecessor(*GetParentBlock());
    } else {
        oldOperand->RemoveUserOnly(this);
    }
}

void Expression::EraseOperands()
{
    for (auto op : operands) {
        op->RemoveUserOnly(this);
    }
    operands.clear();
}

std::string Expression::ToString(size_t indent) const
{
    // [ret] %x[name]: type = expression(xxx) // comment
    std::stringstream ss;
    ss << IndentToString(indent);
    if (result != nullptr) {
        if (result->IsRetValue()) {
            ss << "[ret] ";
        }
        ss << result->GetIdentifier();
        if (auto srcName = result->GetSrcCodeIdentifier(); !srcName.empty()) {
            ss << "[" << srcName << "]";
        }
        ss << ": " << result->GetType()->ToString() << " = ";
    }
    ss << GetExprKindName();
    if (kind == ExprKind::LAMBDA) {
        ss << StaticCast<const Lambda*>(this)->LambdaOperandsToString(indent);
    } else {
        ss << "(" << OperandsToString() << ")";
    }
    ss << CommentToString();
    for (auto subGroup : blockGroups) {
        ss << std::endl << subGroup->ToString(indent);
    }
    return ss.str();
}

bool Expression::HasExceptionBranch() const
{
    return kind == ExprKind::TRY_APPLY ||
        kind == ExprKind::TRY_INVOKE ||
        kind == ExprKind::TRY_INVOKESTATIC ||
        Is<TryUnaryExpression>(*this) ||
        Is<TryBinaryExpression>(*this) ||
        kind == ExprKind::TRY_SPAWN ||
        kind == ExprKind::TRY_NUMERIC_CAST ||
        kind == ExprKind::TRY_INTRINSIC ||
        kind == ExprKind::TRY_ALLOCATE ||
        kind == ExprKind::TRY_RAW_ARRAY_ALLOCATE;
}

std::string Expression::OperandsToString() const
{
    return ValueIdVecToString("", operands, "", HasExceptionBranch());
}

void Expression::Dump() const
{
    std::cout << ToString(0) << std::endl;
}

void Expression::AppendOperand(Value& op)
{
    operands.emplace_back(&op);
    op.AddUserOnly(this);
}

std::string Expression::CommentToString() const
{
    std::vector<std::string> resultStr;
    if (auto baseComment = BaseCommentToString(); !baseComment.empty()) {
        resultStr.emplace_back(baseComment);
    }
    if (auto extraStr = AddExtraComment(); !extraStr.empty()) {
        resultStr.emplace_back(extraStr);
    }
    return ::CommentToString(resultStr);
}

std::string Expression::AddExtraComment() const
{
    return "";
}

bool Expression::IsInBlockGroupRecursively(const BlockGroup& target) const
{
    auto blockGroup = GetParentBlockGroup();
    while (blockGroup != nullptr) {
        if (blockGroup == &target) {
            return true;
        }
        if (blockGroup->GetOwnerFunc()) {
            return false;
        } else if (auto ownerExpression = blockGroup->GetOwnerExpression()) {
            blockGroup = ownerExpression->GetParentBlockGroup();
        }
    }
    return false;
}

namespace {
// (opKind, ExprKind, try ExprKind). ExprKind::INVALID means no try form.
constexpr std::tuple<UnaryExprKind, ExprKind, ExprKind> UNARY_EXPR_KIND_TABLE[] = {
    {UnaryExprKind::NEG, ExprKind::NEG, ExprKind::TRY_NEG},
    {UnaryExprKind::NOT, ExprKind::NOT, ExprKind::INVALID},
    {UnaryExprKind::BITNOT, ExprKind::BITNOT, ExprKind::INVALID},
};

constexpr std::tuple<BinaryExprKind, ExprKind, ExprKind> BINARY_EXPR_KIND_TABLE[] = {
    {BinaryExprKind::ADD, ExprKind::ADD, ExprKind::TRY_ADD},
    {BinaryExprKind::SUB, ExprKind::SUB, ExprKind::TRY_SUB},
    {BinaryExprKind::MUL, ExprKind::MUL, ExprKind::TRY_MUL},
    {BinaryExprKind::DIV, ExprKind::DIV, ExprKind::TRY_DIV},
    {BinaryExprKind::MOD, ExprKind::MOD, ExprKind::TRY_MOD},
    {BinaryExprKind::EXP, ExprKind::EXP, ExprKind::TRY_EXP},
    {BinaryExprKind::LSHIFT, ExprKind::LSHIFT, ExprKind::TRY_LSHIFT},
    {BinaryExprKind::RSHIFT, ExprKind::RSHIFT, ExprKind::TRY_RSHIFT},
    {BinaryExprKind::BITAND, ExprKind::BITAND, ExprKind::INVALID},
    {BinaryExprKind::BITOR, ExprKind::BITOR, ExprKind::INVALID},
    {BinaryExprKind::BITXOR, ExprKind::BITXOR, ExprKind::INVALID},
    {BinaryExprKind::LT, ExprKind::LT, ExprKind::INVALID},
    {BinaryExprKind::GT, ExprKind::GT, ExprKind::INVALID},
    {BinaryExprKind::LE, ExprKind::LE, ExprKind::INVALID},
    {BinaryExprKind::GE, ExprKind::GE, ExprKind::INVALID},
    {BinaryExprKind::EQUAL, ExprKind::EQUAL, ExprKind::INVALID},
    {BinaryExprKind::NOTEQUAL, ExprKind::NOTEQUAL, ExprKind::INVALID},
    {BinaryExprKind::AND, ExprKind::AND, ExprKind::INVALID},
    {BinaryExprKind::OR, ExprKind::OR, ExprKind::INVALID},
};

template <typename OpKind, size_t N>
ExprKind LookupExprKind(
    const std::tuple<OpKind, ExprKind, ExprKind> (&table)[N], OpKind kind, bool isTry)
{
    for (const auto& [opKind, exprKind, tryExprKind] : table) {
        if (opKind != kind) {
            continue;
        }
        auto result = isTry ? tryExprKind : exprKind;
        CJC_ASSERT(result != ExprKind::INVALID);
        return result;
    }
    CJC_ABORT();
    return ExprKind::INVALID;
}

template <typename OpKind, size_t N>
OpKind LookupOpKind(const std::tuple<OpKind, ExprKind, ExprKind> (&table)[N], ExprKind kind)
{
    for (const auto& [opKind, exprKind, tryExprKind] : table) {
        if (exprKind == kind || (tryExprKind != ExprKind::INVALID && tryExprKind == kind)) {
            return opKind;
        }
    }
    CJC_ABORT();
    return OpKind{};
}
} // namespace

ExprKind ExprKindMgr::ToExprKind(UnaryExprKind kind, bool isTry)
{
    return LookupExprKind(UNARY_EXPR_KIND_TABLE, kind, isTry);
}

ExprKind ExprKindMgr::ToExprKind(BinaryExprKind kind, bool isTry)
{
    return LookupExprKind(BINARY_EXPR_KIND_TABLE, kind, isTry);
}

UnaryExprKind ExprKindMgr::ToUnaryExprKind(ExprKind kind)
{
    return LookupOpKind(UNARY_EXPR_KIND_TABLE, kind);
}

BinaryExprKind ExprKindMgr::ToBinaryExprKind(ExprKind kind)
{
    return LookupOpKind(BINARY_EXPR_KIND_TABLE, kind);
}

UnaryExpressionBase::UnaryExpressionBase(
    UnaryExprKind kind, Value* operand, Cangjie::OverflowStrategy ofs, bool isTry,
    const std::vector<Block*>& successors, Block* parent)
    : Expression(ExprKindMgr::ToExprKind(kind, isTry), {operand}, parent), overflowStrategy(ofs)
{
    CJC_NULLPTR_CHECK(parent);
    for (auto succ : successors) {
        AppendOperand(*succ);
    }
}

Value* UnaryExpressionBase::GetOperand() const
{
    return GetOperand(0);
}

UnaryExprKind UnaryExpressionBase::GetOpKind() const
{
    return ExprKindMgr::ToUnaryExprKind(GetExprKind());
}

Cangjie::OverflowStrategy UnaryExpressionBase::GetOverflowStrategy() const
{
    return overflowStrategy;
}

std::string UnaryExpressionBase::AddExtraComment() const
{
    if (overflowStrategy != Cangjie::OverflowStrategy::NA) {
        return OverflowToString(overflowStrategy);
    }
    return "";
}

UnaryExpression::UnaryExpression(UnaryExprKind kind, Value* operand, Cangjie::OverflowStrategy ofs, Block* parent)
    : UnaryExpressionBase(kind, operand, ofs, false, {}, parent)
{
}

BinaryExpressionBase::BinaryExpressionBase(
    BinaryExprKind kind, Value* lhs, Value* rhs, OverflowStrategy ofs, bool isTry,
    const std::vector<Block*>& successors, Block* parent)
    : Expression(ExprKindMgr::ToExprKind(kind, isTry), {lhs, rhs}, parent), overflowStrategy(ofs)
{
    CJC_NULLPTR_CHECK(parent);
    for (auto succ : successors) {
        AppendOperand(*succ);
    }
}

BinaryExpressionBase::BinaryExpressionBase(BinaryExprKind kind, Value* lhs, Value* rhs, Block* parent)
    : Expression(ExprKindMgr::ToExprKind(kind, false), {lhs, rhs}, parent)
{
    CJC_ASSERT(kind >= BinaryExprKind::BITAND);
}

Value* BinaryExpressionBase::GetLHSOperand() const
{
    return GetOperand(0);
}

Value* BinaryExpressionBase::GetRHSOperand() const
{
    return GetOperand(1);
}

BinaryExprKind BinaryExpressionBase::GetOpKind() const
{
    return ExprKindMgr::ToBinaryExprKind(GetExprKind());
}

bool BinaryExpressionBase::IsMathematicalOperator() const
{
    auto kind = GetOpKind();
    return kind >= BinaryExprKind::ADD && kind <= BinaryExprKind::EXP;
}

bool BinaryExpressionBase::IsBitwiseOperator() const
{
    auto kind = GetOpKind();
    return kind >= BinaryExprKind::LSHIFT && kind <= BinaryExprKind::BITXOR;
}

bool BinaryExpressionBase::IsComparisonOperator() const
{
    auto kind = GetOpKind();
    return kind >= BinaryExprKind::LT && kind <= BinaryExprKind::NOTEQUAL;
}

bool BinaryExpressionBase::IsLogicalOperator() const
{
    auto kind = GetOpKind();
    return kind >= BinaryExprKind::AND && kind <= BinaryExprKind::OR;
}

Cangjie::OverflowStrategy BinaryExpressionBase::GetOverflowStrategy() const
{
    return overflowStrategy;
}

std::string BinaryExpressionBase::AddExtraComment() const
{
    if (overflowStrategy != Cangjie::OverflowStrategy::NA) {
        return OverflowToString(overflowStrategy);
    }
    return "";
}

BinaryExpression::BinaryExpression(BinaryExprKind kind, Value* lhs, Value* rhs, OverflowStrategy ofs, Block* parent)
    : BinaryExpressionBase(kind, lhs, rhs, ofs, false, {}, parent)
{
}

BinaryExpression::BinaryExpression(BinaryExprKind kind, Value* lhs, Value* rhs, Block* parent)
    : BinaryExpressionBase(kind, lhs, rhs, parent)
{
}

LiteralValue* Constant::GetValue() const
{
    CJC_ASSERT(GetOperand(0)->IsLiteral());
    return StaticCast<LiteralValue*>(GetOperand(0));
}

bool Constant::GetBoolLitVal() const
{
    auto val = GetValue();
    CJC_ASSERT(val->IsBoolLiteral());
    return static_cast<BoolLiteral*>(val)->GetVal();
}

bool Constant::IsBoolLit() const
{
    return GetValue()->IsBoolLiteral();
}

char32_t Constant::GetRuneLitVal() const
{
    auto val = GetValue();
    CJC_ASSERT(val->IsRuneLiteral());
    return static_cast<RuneLiteral*>(val)->GetVal();
}

bool Constant::IsRuneLit() const
{
    return GetValue()->IsRuneLiteral();
}

std::string Constant::GetStringLitVal() const
{
    auto val = GetValue();
    CJC_ASSERT(val->IsStringLiteral());
    return static_cast<StringLiteral*>(val)->GetVal();
}

bool Constant::IsStringLit() const
{
    return GetValue()->IsStringLiteral();
}

int64_t Constant::GetSignedIntLitVal() const
{
    auto val = GetValue();
    CJC_ASSERT(val->IsIntLiteral());
    return static_cast<IntLiteral*>(val)->GetSignedVal();
}

uint64_t Constant::GetUnsignedIntLitVal() const
{
    auto val = GetValue();
    CJC_ASSERT(val->IsIntLiteral());
    return static_cast<IntLiteral*>(val)->GetUnsignedVal();
}

bool Constant::IsIntLit() const
{
    return GetValue()->IsIntLiteral();
}

bool Constant::IsSignedIntLit() const
{
    if (!IsIntLit()) {
        return false;
    }
    return static_cast<IntLiteral*>(GetValue())->IsSigned();
}

bool Constant::IsUnSignedIntLit() const
{
    if (!IsIntLit()) {
        return false;
    }
    return !static_cast<IntLiteral*>(GetValue())->IsSigned();
}

bool Constant::IsFloatLit() const
{
    return GetValue()->IsFloatLiteral();
}

double Constant::GetFloatLitVal() const
{
    auto val = GetValue();
    CJC_ASSERT(val->IsFloatLiteral());
    return static_cast<FloatLiteral*>(val)->GetVal();
}

bool Constant::IsNullLit() const
{
    return GetValue()->IsNullLiteral();
}

bool Constant::IsUnitLit() const
{
    return GetValue()->IsUnitLiteral();
}

// Allocate
AllocateBase::AllocateBase(ExprKind kind, Type* ty, const std::vector<Block*>& successors, Block* parent)
    : Expression(kind, {}, parent), ty(ty)
{
    CJC_NULLPTR_CHECK(parent);
    for (auto succ : successors) {
        AppendOperand(*succ);
    }
}

Type* AllocateBase::GetType() const
{
    return ty;
}

std::string AllocateBase::OperandsToString() const
{
    return ty->ToString();
}

Allocate::Allocate(Type* ty, Block* parent) : AllocateBase(ExprKind::ALLOCATE, ty, {}, parent)
{
}

Value* Load::GetLocation() const
{
    return operands[0];
}

Value* Store::GetValue() const
{
    return operands[0];
}
Value* Store::GetLocation() const
{
    return operands[1];
}

// GetElementRef
Value* GetElementRef::GetBase() const
{
    return operands[0];
}

const std::vector<uint64_t>& GetElementRef::GetPath() const
{
    return path;
}

std::string GetElementRef::OperandsToString() const
{
    std::stringstream ss;
    ss << GetBase()->GetIdentifier();
    for (auto p : GetPath()) {
        ss << ", " << p;
    }
    return ss.str();
}

// GetElementByName
Value* GetElementByName::GetLocation() const
{
    return operands[0];
}

const std::vector<std::string>& GetElementByName::GetNames() const
{
    return names;
}

std::string GetElementByName::OperandsToString() const
{
    std::stringstream ss;
    ss << GetLocation()->GetIdentifier();
    for (const auto& p : GetNames()) {
        ss << ", " << p;
    }
    return ss.str();
}

GetElementByName* GetElementByName::Clone(CHIRBuilder& builder, Block& parent) const
{
    auto newNode = builder.CreateExpression<GetElementByName>(result->GetType(), GetLocation(), GetNames(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

// StoreElementRef
Value* StoreElementRef::GetValue() const
{
    return operands[0];
}

Value* StoreElementRef::GetBase() const
{
    return operands[1];
}

const std::vector<uint64_t>& StoreElementRef::GetPath() const
{
    return path;
}

// StoreElementByName
Value* StoreElementByName::GetValue() const
{
    return operands[0];
}

Value* StoreElementByName::GetLocation() const
{
    return operands[1];
}

const std::vector<std::string>& StoreElementByName::GetNames() const
{
    return names;
}

std::string StoreElementByName::OperandsToString() const
{
    std::stringstream ss;
    ss << GetValue()->GetIdentifier() << ", ";
    ss << GetLocation()->GetIdentifier();
    for (const auto& p : GetNames()) {
        ss << ", " << p;
    }
    return ss.str();
}

StoreElementByName* StoreElementByName::Clone(CHIRBuilder& builder, Block& parent) const
{
    auto newNode =
        builder.CreateExpression<StoreElementByName>(result->GetType(), GetValue(), GetLocation(), GetNames(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

FuncCall::FuncCall(ExprKind kind, const FuncCallContext& funcCallCtx, Block* parent)
    : Expression(kind, {}, {}, parent),
      instantiatedTypeArgs(funcCallCtx.instTypeArgs),
      thisType(funcCallCtx.thisType)
{
}

Value* FuncCall::GetArg(size_t index) const
{
    auto args = GetArgs();
    CJC_ASSERT(index < args.size());
    return args[index];
}

Type* FuncCall::GetThisType() const
{
    return thisType;
}

void FuncCall::SetThisType(Type* type)
{
    thisType = type;
}

const std::vector<Type*>& FuncCall::GetInstantiatedTypeArgs() const
{
    return instantiatedTypeArgs;
}

std::string FuncCall::OperandsToString() const
{
    std::vector<std::string> res;
    std::string func;
    if (thisType != nullptr) {
        func += thisType->ToString() + "->";
    }
    func += GetCalleeIdentifier();
    func += TypeVecToString("<", instantiatedTypeArgs, ">");
    res.emplace_back(func);
    std::vector<Value*> ops;
    if (Is<const IntrinsicBase*>(this)) {
        ops = operands;
    } else {
        ops = std::vector<Value*>(operands.begin() + 1, operands.end());
    }
    res.emplace_back(ValueIdVecToString("", ops, "", IsTerminator()));
    return StringJoin(res, ", ");
}

// ApplyBase
ApplyBase::ApplyBase(ExprKind kind, Value* callee, const FuncCallContext& callContext,
    const std::vector<Block*>& successors, Block* parent)
    : FuncCall(kind, callContext, parent)
{
    CJC_NULLPTR_CHECK(callee);
    CJC_NULLPTR_CHECK(parent);
    AppendOperand(*callee);
    for (auto op : callContext.args) {
        AppendOperand(*op);
    }
    for (auto succ : successors) {
        AppendOperand(*succ);
    }
}

Value* ApplyBase::GetCallee() const
{
    return operands[0];
}

std::vector<Value*> ApplyBase::GetArgs() const
{
    auto end = GetSuccessorIndex(0);
    if (end <= 1) {
        return {};
    }
    return {operands.begin() + 1, operands.begin() + static_cast<long>(end)};
}

Type* ApplyBase::GetInstParentCustomTyOfCallee(CHIRBuilder& builder) const
{
    return ::Cangjie::CHIR::GetInstParentCustomTyOfCallee(*GetCallee(), GetArgs(), GetThisType(), builder);
}

std::string ApplyBase::GetCalleeIdentifier() const
{
    return GetCallee()->GetIdentifier();
}

// Apply
Apply::Apply(Value* callee, const FuncCallContext& callContext, Block* parent)
    : ApplyBase(ExprKind::APPLY, callee, callContext, {}, parent)
{
}

void Apply::SetSuperCall()
{
    this->isSuperCall = true;
}

bool Apply::IsSuperCall() const
{
    return isSuperCall;
}

std::string Apply::AddExtraComment() const
{
    if (IsSuperCall()) {
        return "isSuperCall";
    }
    return "";
}

DynamicDispatch::DynamicDispatch(ExprKind kind, const InvokeCallContext& callContext, Block* parent)
    : FuncCall(kind, callContext.funcCallCtx, parent), overflowStrategy(callContext.overflowStrategy)
{
    CJC_NULLPTR_CHECK(callContext.method);
    CJC_NULLPTR_CHECK(callContext.funcCallCtx.thisType);
    CJC_ASSERT(callContext.overflowStrategy != Cangjie::OverflowStrategy::CHECKED);
    AppendOperand(*callContext.method);
}

Function* DynamicDispatch::GetCallee() const
{
    return StaticCast<Function*>(operands[0]);
}

std::string DynamicDispatch::GetMethodName() const
{
    auto name = GetCallee()->GetSrcCodeIdentifier();
    if (overflowStrategy != Cangjie::OverflowStrategy::NA) {
        return OverflowStrategyPrefix(overflowStrategy) + name;
    }
    return name;
}

FuncType* DynamicDispatch::GetMethodType() const
{
    return GetCallee()->GetFuncType();
}

const std::vector<GenericType*>& DynamicDispatch::GetGenericTypeParams() const
{
    return GetCallee()->GetGenericTypeParams();
}

size_t DynamicDispatch::GetVirtualMethodOffset() const
{
    auto offset = Get<VirMethodOffset>();
    if (offset.has_value()) {
        return offset.value();
    }
    auto callee = GetCallee();
    auto def = callee->GetParentCustomTypeDef();
    const auto& virtualMethods =
        def->GetDefVTable().GetExpectedTypeVTable(*StaticCast<ClassType*>(def->GetType())).GetVirtualMethods();
    // Overflow operators (`+` etc.) are split into multiple vtable slots (`&+`/`~+`/`%+`) that may share
    // the same Function*. Matching only by callee would always hit the first slot, so also compare
    // method name (GetMethodName() already includes OverflowStrategyPrefix).
    for (size_t i = 0; i < virtualMethods.size(); i++) {
        if (virtualMethods[i].GetVirtualMethod() == callee &&
            virtualMethods[i].GetMethodName() == GetMethodName()) {
            return i;
        }
    }
    CJC_ABORT();
    return 0;
}

ClassType* DynamicDispatch::GetInstSrcParentCustomTypeOfMethod(CHIRBuilder& builder) const
{
    auto thisTypeDeref = thisType->StripAllRefs();
    if (thisTypeDeref->IsThis()) {
        thisTypeDeref = GetTopLevelFunc()->GetParentCustomTypeDef()->GetType();
    }
    // May inherit the same parent interface more than once with different type args, e.g.
    //   interface A<T> { func foo(a: T) {} }
    //   class B <: A<Bool> & A<Int64> {}
    //   B().foo(1)  // must pick A<Int64>, not A<Bool>
    // GetInstParentCustomTyOfCallee disambiguates by matching callee args when multiple parents match.
    return StaticCast<ClassType*>(
        ::Cangjie::CHIR::GetInstParentCustomTyOfCallee(*GetCallee(), GetArgs(), thisTypeDeref, builder));
}

AttributeInfo DynamicDispatch::GetVirtualMethodAttr() const
{
    return GetCallee()->GetAttributeInfo();
}

std::string DynamicDispatch::GetCalleeIdentifier() const
{
    return GetCallee()->GetIdentifier();
}

std::string DynamicDispatch::AddExtraComment() const
{
    if (overflowStrategy != Cangjie::OverflowStrategy::NA) {
        return "methodName: " + GetMethodName();
    }
    return "";
}

inline static void CheckVirFuncInvokeInfo(const InvokeCallContext& callContext)
{
    CJC_NULLPTR_CHECK(callContext.method);
    CJC_NULLPTR_CHECK(callContext.caller);
    CJC_NULLPTR_CHECK(callContext.funcCallCtx.thisType);
}

// InvokeBase
InvokeBase::InvokeBase(
    ExprKind kind, const InvokeCallContext& callContext, const std::vector<Block*>& successors, Block* parent)
    : DynamicDispatch(kind, callContext, parent)
{
    CJC_NULLPTR_CHECK(parent);
    CheckVirFuncInvokeInfo(callContext);
    AppendOperand(*callContext.caller);
    for (auto op : callContext.funcCallCtx.args) {
        AppendOperand(*op);
    }
    for (auto succ : successors) {
        AppendOperand(*succ);
    }
}

Value* InvokeBase::GetObject() const
{
    return operands[1];
}

std::vector<Value*> InvokeBase::GetArgs() const
{
    return {operands.begin() + 1, operands.begin() + static_cast<long>(GetSuccessorIndex(0))};
}

// Invoke
Invoke::Invoke(const InvokeCallContext& callContext, Block* parent)
    : InvokeBase(ExprKind::INVOKE, callContext, {}, parent)
{
}

// InvokeStaticBase
InvokeStaticBase::InvokeStaticBase(
    ExprKind kind, const InvokeCallContext& callContext, const std::vector<Block*>& successors, Block* parent)
    : DynamicDispatch(kind, callContext, parent)
{
    CJC_NULLPTR_CHECK(parent);
    CheckVirFuncInvokeInfo(callContext);
    AppendOperand(*callContext.caller);
    for (auto op : callContext.funcCallCtx.args) {
        AppendOperand(*op);
    }
    for (auto succ : successors) {
        AppendOperand(*succ);
    }
}

Value* InvokeStaticBase::GetRTTIValue() const
{
    return operands[1];
}

std::vector<Value*> InvokeStaticBase::GetArgs() const
{
    return {operands.begin() + 2, operands.begin() + static_cast<long>(GetSuccessorIndex(0))};
}

InvokeStatic::InvokeStatic(const InvokeCallContext& callContext, Block* parent)
    : InvokeStaticBase(ExprKind::INVOKESTATIC, callContext, {}, parent)
{
}

InvokeStatic* InvokeStatic::Clone(CHIRBuilder& builder, Block& parent) const
{
    auto invokeInfo = InvokeCallContext {
        .method = GetCallee(),
        .caller = GetRTTIValue(),
        .funcCallCtx = FuncCallContext {
            .args = GetArgs(),
            .instTypeArgs = instantiatedTypeArgs,
            .thisType = thisType
        },
        .overflowStrategy = overflowStrategy
    };
    auto newNode = builder.CreateExpression<InvokeStatic>(result->GetType(), invokeInfo, &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

// TypeCast
TypeCast::TypeCast(ExprKind kind, Value* operand, const std::vector<Block*>& successors, Block* parent)
    : Expression(kind, {operand}, parent)
{
    CJC_NULLPTR_CHECK(parent);
    for (auto succ : successors) {
        AppendOperand(*succ);
    }
}

Value* TypeCast::GetSourceValue() const
{
    return operands[0];
}

Type* TypeCast::GetSourceType() const
{
    return operands[0]->GetType();
}

Type* TypeCast::GetTargetType() const
{
    return result->GetType();
}

// NumericCastBase
NumericCastBase::NumericCastBase(
    ExprKind kind, Value* operand, Cangjie::OverflowStrategy overflow, const std::vector<Block*>& successors,
    Block* parent)
    : TypeCast(kind, operand, successors, parent), overflowStrategy(overflow)
{
}

Cangjie::OverflowStrategy NumericCastBase::GetOverflowStrategy() const
{
    return overflowStrategy;
}

std::string NumericCastBase::AddExtraComment() const
{
    if (overflowStrategy != Cangjie::OverflowStrategy::NA) {
        return OverflowToString(overflowStrategy);
    }
    return "";
}

// StaticCast
ClassStaticCast::ClassStaticCast(Value* operand, Block* parent)
    : TypeCast(ExprKind::CLASS_STATIC_CAST, operand, {}, parent)
{
}

ClassStaticCast* ClassStaticCast::Clone(CHIRBuilder& builder, Block& parent) const
{
    auto newNode = builder.CreateExpression<ClassStaticCast>(result->GetType(), GetSourceValue(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    newNode->Set<NeedCheckCast>(this->Get<NeedCheckCast>());
    newNode->GetResult()->Set<EnumCaseIndex>(result->Get<EnumCaseIndex>());
    return newNode;
}

// NumericCast
NumericCast::NumericCast(Value* operand, Cangjie::OverflowStrategy overflow, Block* parent)
    : NumericCastBase(ExprKind::NUMERIC_CAST, operand, overflow, {}, parent)
{
}

NumericCast* NumericCast::Clone(CHIRBuilder& builder, Block& parent) const
{
    auto newNode =
        builder.CreateExpression<NumericCast>(result->GetType(), GetSourceValue(), GetOverflowStrategy(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    newNode->Set<NeedCheckCast>(this->Get<NeedCheckCast>());
    newNode->GetResult()->Set<EnumCaseIndex>(result->Get<EnumCaseIndex>());
    return newNode;
}

// InstanceOf
InstanceOf::InstanceOf(Value* operand, Type* ty, Block* parent)
    : Expression(ExprKind::INSTANCEOF, {operand}, {}, parent), ty(ty)
{
}

Value* InstanceOf::GetObject() const
{
    return operands[0];
}

Type* InstanceOf::GetType() const
{
    return ty;
}

std::string InstanceOf::OperandsToString() const
{
    return GetObject()->GetIdentifier() + ", " + GetType()->ToString();
}

// Box
Box::Box(Value* operand, Block* parent)
    : TypeCast(ExprKind::BOX, operand, {}, parent)
{
}

Box* Box::Clone(CHIRBuilder& builder, Block& parent) const
{
    auto newNode = builder.CreateExpression<Box>(result->GetType(), GetSourceValue(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

// UnBoxToValue
UnBoxToValue::UnBoxToValue(Value* operand, Block* parent)
    : TypeCast(ExprKind::UNBOX_TO_VALUE, operand, {}, parent)
{
}

UnBoxToValue* UnBoxToValue::Clone(CHIRBuilder& builder, Block& parent) const
{
    auto newNode = builder.CreateExpression<UnBoxToValue>(result->GetType(), GetSourceValue(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

// CastToGeneric
CastToGeneric::CastToGeneric(Value* operand, Block* parent)
    : TypeCast(ExprKind::CAST_TO_GENERIC, operand, {}, parent)
{
}

CastToGeneric* CastToGeneric::Clone(CHIRBuilder& builder, Block& parent) const
{
    auto newNode = builder.CreateExpression<CastToGeneric>(result->GetType(), GetSourceValue(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

// CastToConcrete
CastToConcrete::CastToConcrete(Value* operand, Block* parent)
    : TypeCast(ExprKind::CAST_TO_CONCRETE, operand, {}, parent)
{
}

CastToConcrete* CastToConcrete::Clone(CHIRBuilder& builder, Block& parent) const
{
    auto newNode = builder.CreateExpression<CastToConcrete>(result->GetType(), GetSourceValue(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

// UnBoxToRef
UnBoxToRef::UnBoxToRef(Value* operand, Block* parent)
    : TypeCast(ExprKind::UNBOX_TO_REF, operand, {}, parent)
{
}

UnBoxToRef* UnBoxToRef::Clone(CHIRBuilder& builder, Block& parent) const
{
    auto newNode = builder.CreateExpression<UnBoxToRef>(result->GetType(), GetSourceValue(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

// Tuple
Tuple::Tuple(const std::vector<Value*>& values, Block* parent)
    : Expression(ExprKind::TUPLE, values, {}, parent)
{
}

std::vector<Value*> Tuple::GetElementValues() const
{
    return operands;
}

std::vector<Type*> Tuple::GetElementTypes() const
{
    std::vector<Type*> types;
    for (auto op : operands) {
        types.emplace_back(op->GetType());
    }
    return types;
}

// Field
Field::Field(Value* val, const std::vector<uint64_t>& path, Block* parent)
    : Expression(ExprKind::FIELD, {val}, {}, parent), path(path)
{
}

Value* Field::GetBase() const
{
    return operands[0];
}

std::vector<uint64_t> Field::GetPath() const
{
    return path;
}

std::string Field::OperandsToString() const
{
    std::stringstream ss;
    ss << GetBase()->GetIdentifier();
    for (auto p : GetPath()) {
        ss << ", " << p;
    }
    return ss.str();
}

// Field
FieldByName::FieldByName(Value* val, const std::vector<std::string>& names, Block* parent)
    : Expression(ExprKind::FIELD_BY_NAME, {val}, {}, parent), names(names)
{
}

Value* FieldByName::GetBase() const
{
    return operands[0];
}

const std::vector<std::string>& FieldByName::GetNames() const
{
    return names;
}

std::string FieldByName::OperandsToString() const
{
    std::stringstream ss;
    ss << GetBase()->GetIdentifier();
    for (const auto& p : GetNames()) {
        ss << ", " << p;
    }
    return ss.str();
}

FieldByName* FieldByName::Clone(CHIRBuilder& builder, Block& parent) const
{
    auto newNode = builder.CreateExpression<FieldByName>(result->GetType(), GetBase(), GetNames(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

// RawArrayAllocate
RawArrayAllocateBase::RawArrayAllocateBase(
    ExprKind kind, Type* eleTy, Value* size, const std::vector<Block*>& successors, Block* parent)
    : Expression(kind, {size}, parent), elementType(eleTy)
{
    CJC_NULLPTR_CHECK(parent);
    for (auto succ : successors) {
        AppendOperand(*succ);
    }
}

Value* RawArrayAllocateBase::GetSize() const
{
    CJC_ASSERT(!operands.empty());
    return operands[0];
}

Type* RawArrayAllocateBase::GetElementType() const
{
    return elementType;
}

std::string RawArrayAllocateBase::OperandsToString() const
{
    return elementType->ToString() + ", " + GetSize()->GetIdentifier();
}

RawArrayAllocate::RawArrayAllocate(Type* eleTy, Value* size, Block* parent)
    : RawArrayAllocateBase(ExprKind::RAW_ARRAY_ALLOCATE, eleTy, size, {}, parent)
{
}

// RawArrayLiteralInit
RawArrayLiteralInit::RawArrayLiteralInit(const Ptr<Value> raw, std::vector<Value*> elements, Block* parent)
    : Expression(ExprKind::RAW_ARRAY_LITERAL_INIT, {}, {}, parent)
{
    CJC_NULLPTR_CHECK(parent);
    AppendOperand(*raw);
    for (auto op : elements) {
        AppendOperand(*op);
    }
}

Value* RawArrayLiteralInit::GetRawArray() const
{
    return operands[0];
}

size_t RawArrayLiteralInit::GetSize() const
{
    CJC_ASSERT(operands.size() > 0);
    return operands.size() - 1;
}

std::vector<Value*> RawArrayLiteralInit::GetElementValues() const
{
    CJC_ASSERT(operands.size() > 0);
    return {operands.begin() + 1, operands.end()};
}

// IntrinsicBase
IntrinsicBase::IntrinsicBase(ExprKind kind, const IntrisicCallContext& callContext,
    const std::vector<Block*>& successors, Block* parent)
    : FuncCall(kind,
          FuncCallContext{.args = callContext.args, .instTypeArgs = callContext.instTypeArgs, .thisType = nullptr},
          parent),
      intrinsicKind(callContext.kind)
{
    CJC_NULLPTR_CHECK(parent);
    for (auto op : callContext.args) {
        AppendOperand(*op);
    }
    for (auto succ : successors) {
        AppendOperand(*succ);
    }
}

IntrinsicKind IntrinsicBase::GetIntrinsicKind() const
{
    return intrinsicKind;
}

std::vector<Value*> IntrinsicBase::GetArgs() const
{
    auto end = GetSuccessorIndex(0);
    return {operands.begin(), operands.begin() + static_cast<long>(end)};
}

std::string IntrinsicBase::GetCalleeIdentifier() const
{
    return IntrinsicKindToString(intrinsicKind);
}

// Intrinsic
Intrinsic::Intrinsic(const IntrisicCallContext& callContext, Block* parent)
    : IntrinsicBase(ExprKind::INTRINSIC, callContext, {}, parent)
{
}

// ForIn
ForIn::ForIn(ExprKind kind, Value* inductionVar, Value* loopCondVar, Block* parent)
    : Expression(kind, {inductionVar, loopCondVar}, parent)
{
}

Value* ForIn::GetInductionVar() const
{
    return operands[0];
}

Value* ForIn::GetLoopCondVar() const
{
    return operands[1];
}

BlockGroup* ForIn::GetBody() const
{
    return blockGroups[0];
}

BlockGroup* ForIn::GetLatch() const
{
    return blockGroups[1];
}

BlockGroup* ForIn::GetCond() const
{
    return blockGroups[2U];
}

void ForIn::InitBlockGroups(BlockGroup& body, BlockGroup& latch, BlockGroup& cond)
{
    CJC_ASSERT(blockGroups.empty());
    body.SetOwnerExpression(*this);
    blockGroups.emplace_back(&body);

    latch.SetOwnerExpression(*this);
    blockGroups.emplace_back(&latch);

    cond.SetOwnerExpression(*this);
    blockGroups.emplace_back(&cond);
}

// Lambda
Lambda::Lambda(FuncType* ty, Block* parent, bool isLocalFunc, const std::string& identifier,
    const std::string& srcCodeIdentifier, const std::vector<GenericType*>& genericTypeParams)
    : Expression(ExprKind::LAMBDA, /* operands = */ {}, /* block group = */ {}, parent),
      identifier(identifier),
      srcCodeIdentifier(srcCodeIdentifier),
      funcTy(ty),
      isLocalFunc(isLocalFunc),
      genericTypeParams(genericTypeParams)
{
    CJC_NULLPTR_CHECK(ty);
    CJC_NULLPTR_CHECK(parent);
}

bool Lambda::IsCompileTimeValue() const
{
    return isCompileTimeValue;
}

void Lambda::SetCompileTimeValue()
{
    isCompileTimeValue = true;
}

Type* Lambda::GetReturnType() const
{
    return funcTy->GetReturnType();
}

std::string Lambda::LambdaOperandsToString(size_t indent) const
{
    std::stringstream ss;
    ss << "[" << identifier << "]" << TypeVecToString("<", genericTypeParams, ">") << "(";
    for (auto param : params) {
        ss << std::endl << param->ToString(indent + 1);
    }
    if (!params.empty()) {
        ss << std::endl;
        ss << IndentToString(indent);
    }
    ss << "): " << GetReturnType()->ToString();
    return ss.str();
}

std::string Lambda::AddExtraComment() const
{
    std::vector<std::string> res;
    if (isCompileTimeValue) {
        res.emplace_back("compileTimeVal");
    }
    if (isLocalFunc) {
        res.emplace_back("localFunc");
    }
    if (!srcCodeIdentifier.empty()) {
        res.emplace_back("srcCodeIdentifier: " + srcCodeIdentifier);
    }
    if (paramDftValHostFunc != nullptr) {
        res.emplace_back("paramDftValHostFunc: " + paramDftValHostFunc->GetIdentifier());
    }
    if (auto gStr =  GetGenericTypeConstaintsStr(genericTypeParams); !gStr.empty()) {
        res.emplace_back(gStr);
    }
    if (!res.empty()) {
        return StringJoin(res, ", ");
    }
    return "";
}

std::vector<Value*> Lambda::GetCapturedVariables() const
{
    std::vector<Value*> envs;
    auto preVisit = [&envs, this](Expression& e) {
        for (auto op : e.GetNonSuccessorOperands()) {
            bool isEnv = false;
            if (op->IsLocalVar()) {
                auto localVar = static_cast<LocalVar*>(op);
                if (localVar != GetResult() && !localVar->GetExpr()->IsInBlockGroupRecursively(*GetBody())) {
                    isEnv = true;
                }
            } else if (op->IsParameter()) {
                auto arg = static_cast<Parameter*>(op);
                if (arg->GetOwnerLambda() != this) {
                    isEnv = true;
                }
            }
            if (isEnv && std::find(envs.begin(), envs.end(), op) == envs.end()) {
                envs.emplace_back(op);
            }
        }
        if (Is<Lambda>(e)) {
            return VisitResult::SKIP;
        }
        return VisitResult::CONTINUE;
    };
    Visitor::Visit(*GetBody(), preVisit);
    return envs;
}

// Debug
Debug::Debug(Value* local, std::string srcCodeIdentifier, Block* parent)
    : Expression(ExprKind::DEBUGEXPR, {local}, parent), srcCodeIdentifier(srcCodeIdentifier)
{
    CJC_NULLPTR_CHECK(parent);
}

std::string Debug::OperandsToString() const
{
    return GetValue()->GetIdentifier() + ", " + GetSrcCodeIdentifier();
}

SpawnBase::SpawnBase(
    ExprKind kind, const std::vector<Value*>& operands, const std::vector<Block*>& successors, Block* parent)
    : Expression(kind, operands, parent)
{
    CJC_NULLPTR_CHECK(parent);
    for (auto succ : successors) {
        AppendOperand(*succ);
    }
}

Value* SpawnBase::GetFuture() const
{
    CJC_ASSERT(!IsExecuteClosure());
    return operands[0];
}

Value* SpawnBase::GetSpawnArg() const
{
    if (GetNumOfNonSuccessorOperands() > 1) {
        return operands[1];
    }
    return nullptr;
}

Value* SpawnBase::GetObject() const
{
    return operands[0];
}

Value* SpawnBase::GetClosure() const
{
    CJC_ASSERT(IsExecuteClosure());
    return operands[0];
}

Function* SpawnBase::GetExecuteClosure() const
{
    return executeClosure;
}

bool SpawnBase::IsExecuteClosure() const
{
    return executeClosure != nullptr;
}

void SpawnBase::SetExecuteClosure(Function& func)
{
    executeClosure = &func;
}

std::string SpawnBase::AddExtraComment() const
{
    if (IsExecuteClosure()) {
        return "executeClosure: " + GetExecuteClosure()->GetIdentifier();
    }
    return "";
}

Spawn::Spawn(Value* val, Block* parent)
    : SpawnBase(ExprKind::SPAWN, {val}, {}, parent)
{
}

Spawn::Spawn(Value* val, Value* arg, Block* parent)
    : SpawnBase(ExprKind::SPAWN, {val, arg}, {}, parent)
{
}

Spawn* Spawn::Clone(CHIRBuilder& builder, Block& parent) const
{
    Spawn* newNode = nullptr;
    auto arg = GetSpawnArg();
    if (arg != nullptr) {
        newNode = builder.CreateExpression<Spawn>(result->GetType(), GetObject(), arg, &parent);
    } else {
        newNode = builder.CreateExpression<Spawn>(result->GetType(), GetObject(), &parent);
    }
    if (executeClosure) {
        newNode->SetExecuteClosure(*executeClosure);
    }
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

UnaryExpression* UnaryExpression::Clone(CHIRBuilder& builder, Block& parent) const
{
    auto newNode = builder.CreateExpression<UnaryExpression>(
        result->GetType(), GetOpKind(), GetOperand(), GetOverflowStrategy(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

BinaryExpression* BinaryExpression::Clone(CHIRBuilder& builder, Block& parent) const
{
    auto newNode = builder.CreateExpression<BinaryExpression>(
        result->GetType(), GetOpKind(), GetLHSOperand(), GetRHSOperand(), GetOverflowStrategy(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

Constant* Constant::Clone(CHIRBuilder& builder, Block& parent) const
{
    Constant* newNode = nullptr;
    auto litVal = StaticCast<LiteralValue*>(GetValue());
    if (litVal->IsNullLiteral()) {
        litVal = builder.CreateLiteralValue<NullLiteral>(litVal->GetType());
    }
    newNode = builder.CreateExpression<Constant>(result->GetType(), litVal, &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

Allocate* Allocate::Clone(CHIRBuilder& builder, Block& parent) const
{
    auto newNode = builder.CreateExpression<Allocate>(result->GetType(), GetType(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

Load* Load::Clone(CHIRBuilder& builder, Block& parent) const
{
    auto newNode = builder.CreateExpression<Load>(result->GetType(), GetLocation(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

Store* Store::Clone(CHIRBuilder& builder, Block& parent) const
{
    auto newNode = builder.CreateExpression<Store>(result->GetType(), GetValue(), GetLocation(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

GetElementRef* GetElementRef::Clone(CHIRBuilder& builder, Block& parent) const
{
    auto newNode = builder.CreateExpression<GetElementRef>(result->GetType(), GetBase(), GetPath(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

std::string StoreElementRef::OperandsToString() const
{
    std::stringstream ss;
    ss << GetValue()->GetIdentifier() << ", ";
    ss << GetBase()->GetIdentifier();
    for (auto p : GetPath()) {
        ss << ", " << p;
    }
    return ss.str();
}

StoreElementRef* StoreElementRef::Clone(CHIRBuilder& builder, Block& parent) const
{
    auto newNode =
        builder.CreateExpression<StoreElementRef>(result->GetType(), GetValue(), GetBase(), GetPath(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

Apply* Apply::Clone(CHIRBuilder& builder, Block& parent) const
{
    auto newNode = builder.CreateExpression<Apply>(result->GetType(), GetCallee(), FuncCallContext{
        .args = GetArgs(),
        .instTypeArgs = GetInstantiatedTypeArgs(),
        .thisType = GetThisType()}, &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

Invoke* Invoke::Clone(CHIRBuilder& builder, Block& parent) const
{
    auto args = GetArgs();
    args.erase(args.begin());
    auto invokeInfo = InvokeCallContext {
        .method = GetCallee(),
        .caller = GetObject(),
        .funcCallCtx = FuncCallContext {
            .args = args,
            .instTypeArgs = instantiatedTypeArgs,
            .thisType = thisType
        },
        .overflowStrategy = overflowStrategy
    };
    auto newNode = builder.CreateExpression<Invoke>(result->GetType(), invokeInfo, &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

InstanceOf* InstanceOf::Clone(CHIRBuilder& builder, Block& parent) const
{
    auto newNode = builder.CreateExpression<InstanceOf>(result->GetType(), GetObject(), GetType(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

RawArrayInitByValue::RawArrayInitByValue(Value* raw, Value* size, Value* initVal, Block* parent)
    : Expression(ExprKind::RAW_ARRAY_INIT_BY_VALUE, {raw, size, initVal}, {}, parent)
{
}

Value* RawArrayInitByValue::GetRawArray() const
{
    return operands[0];
}

Value* RawArrayInitByValue::GetSize() const
{
    return operands[1];
}

Value* RawArrayInitByValue::GetInitValue() const
{
    constexpr int initIdx = 2;
    return operands[initIdx];
}

VArray::VArray(std::vector<Value*> elements, Block* parent)
    : Expression(ExprKind::VARRAY, elements, {}, parent)
{
}

int64_t VArray::GetSize() const
{
    return static_cast<int64_t>(operands.size());
}

std::vector<Value*> VArray::GetElementValues() const
{
    return operands;
}

VArrayBuilder::VArrayBuilder(Value* size, Value* item, Value* initFunc, Block* parent)
    : Expression(ExprKind::VARRAY_BUILDER, {size, item, initFunc}, {}, parent)
{
}

Value* VArrayBuilder::GetSize() const
{
    return GetOperand(static_cast<size_t>(ElementIdx::SIZE_IDX));
}

Value* VArrayBuilder::GetItem() const
{
    return GetOperand(static_cast<size_t>(ElementIdx::ITEM_IDX));
}

Value* VArrayBuilder::GetInitFunc() const
{
    return GetOperand(static_cast<size_t>(ElementIdx::INIT_FUNC_IDX));
}

GetException::GetException(Block* parent) : Expression(ExprKind::GET_EXCEPTION, {}, {}, parent)
{
}

ForIn::BGExecutionOrder ForInRange::GetExecutionOrder() const
{
    return {GetCond(), GetBody(), GetLatch()};
}

ForInIter::ForInIter(Value* inductionVar, Value* loopCondVar, Block* parent)
    : ForIn(ExprKind::FORIN_ITER, inductionVar, loopCondVar, parent)
{
}

ForIn::BGExecutionOrder ForInIter::GetExecutionOrder() const
{
    return {GetLatch(), GetCond(), GetBody()};
}

ForInClosedRange::ForInClosedRange(Value* inductionVar, Value* loopCondVar, Block* parent)
    : ForIn(ExprKind::FORIN_CLOSED_RANGE, inductionVar, loopCondVar, parent)
{
}

ForIn::BGExecutionOrder ForInClosedRange::GetExecutionOrder() const
{
    return {GetBody(), GetCond(), GetLatch()};
}

RaiseException* RaiseException::Clone(CHIRBuilder& builder, Block& parent) const
{
    CJC_ASSERT(result == nullptr);
    auto excp = GetExceptionBlock();
    auto newNode = (excp ? builder.CreateTerminator<RaiseException>(GetExceptionValue(), excp, &parent)
                         : builder.CreateTerminator<RaiseException>(GetExceptionValue(), &parent));
    parent.AppendExpression(newNode);
    return newNode;
}

Tuple* Tuple::Clone(CHIRBuilder& builder, Block& parent) const
{
    auto newNode = builder.CreateExpression<Tuple>(result->GetType(), GetElementValues(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

Field* Field::Clone(CHIRBuilder& builder, Block& parent) const
{
    auto newNode = builder.CreateExpression<Field>(result->GetType(), GetBase(), GetPath(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

RawArrayAllocate* RawArrayAllocate::Clone(CHIRBuilder& builder, Block& parent) const
{
    auto newNode = builder.CreateExpression<RawArrayAllocate>(result->GetType(), GetElementType(), GetSize(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

RawArrayLiteralInit* RawArrayLiteralInit::Clone(CHIRBuilder& builder, Block& parent) const
{
    auto newNode =
        builder.CreateExpression<RawArrayLiteralInit>(result->GetType(), GetRawArray(), GetElementValues(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

RawArrayInitByValue* RawArrayInitByValue::Clone(CHIRBuilder& builder, Block& parent) const
{
    auto newNode = builder.CreateExpression<RawArrayInitByValue>(
        result->GetType(), GetRawArray(), GetSize(), GetInitValue(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

VArray* VArray::Clone(CHIRBuilder& builder, Block& parent) const
{
    auto newNode = builder.CreateExpression<VArray>(result->GetType(), GetElementValues(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

VArrayBuilder* VArrayBuilder::Clone(CHIRBuilder& builder, Block& parent) const
{
    auto newNode =
        builder.CreateExpression<VArrayBuilder>(result->GetType(), GetSize(), GetItem(), GetInitFunc(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

GetException* GetException::Clone(CHIRBuilder& builder, Block& parent) const
{
    auto newNode = builder.CreateExpression<GetException>(result->GetType(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

Intrinsic* Intrinsic::Clone(CHIRBuilder& builder, Block& parent) const
{
    auto callContext = IntrisicCallContext {
        .kind = GetIntrinsicKind(),
        .args = GetArgs(),
        .instTypeArgs = GetInstantiatedTypeArgs()
    };
    auto newNode = builder.CreateExpression<Intrinsic>(result->GetType(), callContext, &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

template <class T> T* ForIn::CloneBase(CHIRBuilder& builder, Block& parent) const
{
    static_assert(std::is_base_of_v<ForIn, T>);
    BlockGroup* newCond = nullptr;
    BlockGroup* newBody = nullptr;
    BlockGroup* newLatch = nullptr;
    if (parent.GetParentBlockGroup()->GetOwnerFunc()) {
        newCond = GetCond()->Clone(builder, *parent.GetParentBlockGroup()->GetOwnerFunc());
        newBody = GetBody()->Clone(builder, *parent.GetParentBlockGroup()->GetOwnerFunc());
        newLatch = GetLatch()->Clone(builder, *parent.GetParentBlockGroup()->GetOwnerFunc());
    } else {
        auto parentLambda = StaticCast<Lambda*>(parent.GetParentBlockGroup()->GetOwnerExpression());
        newCond = GetCond()->Clone(builder, *parentLambda);
        newBody = GetBody()->Clone(builder, *parentLambda);
        newLatch = GetLatch()->Clone(builder, *parentLambda);
    }
    T* newNode = builder.CreateExpression<T>(result->GetType(), GetInductionVar(), GetLoopCondVar(), &parent);
    newNode->InitBlockGroups(*newBody, *newLatch, *newCond);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

ForInRange* ForInRange::Clone(CHIRBuilder &builder, Block &parent) const
{
    return CloneBase<ForInRange>(builder, parent);
}

ForInIter* ForInIter::Clone(CHIRBuilder &builder, Block &parent) const
{
    return CloneBase<ForInIter>(builder, parent);
}

ForInClosedRange* ForInClosedRange::Clone(CHIRBuilder& builder, Block& parent) const
{
    return CloneBase<ForInClosedRange>(builder, parent);
}

Lambda* Lambda::Clone(CHIRBuilder& builder, Block& parent) const
{
    auto newIdentifier = identifier + "." + std::to_string(g_lambdaClonedIdx++);
    auto newNode = builder.CreateExpression<Lambda>(result->GetType(), funcTy, &parent, IsLocalFunc(), newIdentifier,
        GetSrcCodeIdentifier(), GetGenericTypeParams());
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    auto newBody = GetBody()->Clone(builder, *newNode);
    for (auto p : GetParams()) {
        builder.CreateParameter(p->GetType(), INVALID_LOCATION, *newNode);
    }
    auto [blockIdx, exprIdx] = GetAllocExprOfRetVal(*GetBody());
    auto newRet = newBody->GetBlockByIdx(blockIdx)->GetExpressionByIdx(exprIdx)->GetResult();
    CJC_NULLPTR_CHECK(newRet);
    newNode->SetReturnValue(*newRet);
    return newNode;
}

Debug* Debug::Clone(CHIRBuilder& builder, Block& parent) const
{
    auto newNode = builder.CreateExpression<Debug>(result->GetType(), GetValue(), GetSrcCodeIdentifier(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

GetInstantiateValue* GetInstantiateValue::Clone(CHIRBuilder& builder, Block& parent) const
{
    GetInstantiateValue* newNode = nullptr;
    auto val = GetGenericResult();
    if (val->IsLocalVar()) {
        /* `val` must be lambda, if we step in here, it means lambda's parent func is inlined
            e.g.
            func foo<T1>() {
                func goo<T2>() {}
                var a = goo<Int32>  // GetInstantiateValue(goo, T1, Int32)
            }
            main() {
                foo<Bool>()  // inlined
            }
            ===========> after inline, `main` body should be:
            main() {
                func goo<T2>() {}
                var a = goo<Int32>  // GetInstantiateValue(goo, Int32)
            }
            instantiated types in GetInstantiateValue are changed
        */
        auto oldOutDefTypes = GetOutDefDeclaredTypes(*result);
        auto oldAllTypes = GetInstantiateTypes();
        auto newAllTypes = GetOutDefDeclaredTypes(parent);
        for (size_t i = oldOutDefTypes.size(); i < oldAllTypes.size(); ++i) {
            newAllTypes.emplace_back(oldAllTypes[i]);
        }
        newNode = builder.CreateExpression<GetInstantiateValue>(result->GetType(), val, newAllTypes, &parent);
    } else {
        newNode = builder.CreateExpression<GetInstantiateValue>(result->GetType(), val, instantiateTys, &parent);
    }
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

std::vector<Type*> GetInstantiateValue::GetInstantiateTypes() const
{
    return instantiateTys;
}

GetInstantiateValue::GetInstantiateValue(Value* val, std::vector<Type*> insTypes, Block* parent)
    : Expression(ExprKind::GET_INSTANTIATE_VALUE, {val}, {}, parent), instantiateTys(insTypes)
{
}

Value* GetInstantiateValue::GetGenericResult() const
{
    return operands[0];
}

std::string GetInstantiateValue::OperandsToString() const
{
    std::stringstream ss;
    ss << GetGenericResult()->GetIdentifier();
    for (auto ty : GetInstantiateTypes()) {
        ss << ", " << ty->ToString();
    }
    return ss.str();
}

void Lambda::InitBody(BlockGroup& newBody)
{
    CJC_ASSERT(body == nullptr);
    CJC_ASSERT(blockGroups.empty());
    // newBody -> lambda
    newBody.SetOwnerExpression(*this);

    // lambda -> newBody
    body = &newBody;
    blockGroups.emplace_back(&newBody);
}

FuncType* Lambda::GetFuncType() const
{
    return funcTy;
}

BlockGroup* Lambda::GetBody() const
{
    return body;
}

void Lambda::RemoveBody()
{
    blockGroups.clear();
    body = nullptr;
}

Block* Lambda::GetEntryBlock() const
{
    return body->GetEntryBlock();
}

std::string Lambda::GetIdentifier() const
{
    return identifier;
}

/**
    * Get identifier without prefix '@'
    * '@' stands for global decl, so lambda's identifier doesn't have it
    */
std::string Lambda::GetIdentifierWithoutPrefix() const
{
    return identifier;
}

std::string Lambda::GetSrcCodeIdentifier() const
{
    return srcCodeIdentifier;
}

void Lambda::AddParam(Parameter& arg)
{
    params.emplace_back(&arg);
    arg.SetOwnerLambda(this);
}

size_t Lambda::GetNumOfParams() const
{
    return params.size();
}

Parameter* Lambda::GetParam(size_t index) const
{
    CJC_ASSERT(index < params.size());
    return params[index];
}

const std::vector<Parameter*>& Lambda::GetParams() const
{
    return params;
}

const std::vector<GenericType*>& Lambda::GetGenericTypeParams() const
{
    return genericTypeParams;
}

void Lambda::SetReturnValue(LocalVar& ret)
{
    ret.SetRetValue(true);
    retValue = &ret;
}

LocalVar* Lambda::GetReturnValue() const
{
    return retValue;
}

bool Lambda::IsLocalFunc() const
{
    return isLocalFunc;
}

void Lambda::SetParamDftValHostFunc(Lambda& hostFunc)
{
    paramDftValHostFunc = &hostFunc;
}

Lambda* Lambda::GetParamDftValHostFunc() const
{
    return paramDftValHostFunc;
}

void Lambda::RemoveSelfFromBlock()
{
    if (body != nullptr) {
        body = nullptr;
    }

    Expression::RemoveSelfFromBlock();
}

std::string Debug::GetSrcCodeIdentifier() const
{
    return srcCodeIdentifier;
}

Value* Debug::GetValue() const
{
    return operands[0];
}

GetRTTI::GetRTTI(Value* val, Block* parent) : Expression{ExprKind::GET_RTTI, {val}, {}, parent}
{
    CJC_ASSERT(Is<RefType>(val->GetType()) &&
        (val->GetType()->StripAllRefs()->IsClass() || val->GetType()->StripAllRefs()->IsClass()) &&
        "GetRTTI must be used on class ref type");
}

Value* GetRTTI::GetOperand() const
{
    return operands[0];
}

GetRTTI* GetRTTI::Clone(CHIRBuilder& builder, Block& parent) const
{
    auto newNode = builder.CreateExpression<GetRTTI>(GetDebugLocation(), GetResultType(), GetOperand(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

std::string GetRTTIStatic::OperandsToString() const
{
    return GetRTTIType()->ToString();
}

GetRTTIStatic* GetRTTIStatic::Clone(CHIRBuilder& builder, Block& parent) const
{
    auto newNode = builder.CreateExpression<GetRTTIStatic>(GetDebugLocation(), GetResultType(), GetRTTIType(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

GetRTTIStatic::GetRTTIStatic(Type* type, Block* parent)
    : Expression{ExprKind::GET_RTTI_STATIC, {}, {}, parent}, ty{type}
{
}

Type* GetRTTIStatic::GetRTTIType() const
{
    return ty;
}

StartRegion::StartRegion(Block* parent) : Expression(ExprKind::START_REGION, {}, {}, parent)
{
}

StartRegion* StartRegion::Clone(CHIRBuilder& builder, Block& parent) const
{
    auto newNode = builder.CreateExpression<StartRegion>(result->GetType(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

EndRegion::EndRegion(Block* parent) : Expression(ExprKind::END_REGION, {}, {}, parent)
{
}

EndRegion* EndRegion::Clone(CHIRBuilder& builder, Block& parent) const
{
    auto newNode = builder.CreateExpression<EndRegion>(result->GetType(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}
