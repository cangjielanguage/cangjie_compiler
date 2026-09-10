// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/IR/Expression/Terminator.h"

#include <iostream>
#include <sstream>
#include <string>

#include "cangjie/CHIR/AST2CHIR/Utils.h"
#include "cangjie/CHIR/IR/Expression/Expression.h"
#include "cangjie/CHIR/Utils/CHIRCasting.h"
#include "cangjie/CHIR/IR/CHIRBuilder.h"
#include "cangjie/CHIR/Utils/ToStringUtils.h"
#include "cangjie/CHIR/IR/Type/Type.h"
#include "cangjie/CHIR/Utils/Utils.h"
#include "cangjie/CHIR/IR/Value/Value.h"
#include "cangjie/Utils/CheckUtils.h"

using namespace Cangjie::CHIR;

GoTo::GoTo(Block* destBlock, Block* parent) : Expression(ExprKind::GOTO, {}, parent)
{
    AppendOperand(*destBlock);
}

Block* GoTo::GetDestination() const
{
    return GetSuccessor(0);
}

// MultiBranch

MultiBranch::MultiBranch(Value* cond, Block* defaultBlock, const std::vector<uint64_t>& vals,
    const std::vector<Block*>& succs, Block* parent)
    : Expression(ExprKind::MULTIBRANCH, {cond}, parent), caseVals(vals)
{
    CJC_NULLPTR_CHECK(cond);
    CJC_NULLPTR_CHECK(defaultBlock);
    /* Note that successors[0] is used to store the default basic block */
    AppendOperand(*defaultBlock);
    for (auto b : succs) {
        AppendOperand(*b);
    }
}

Value* MultiBranch::GetCondition() const
{
    return operands[0];
}

const std::vector<uint64_t>& MultiBranch::GetCaseVals() const
{
    return caseVals;
}

uint64_t MultiBranch::GetCaseValByIndex(size_t index) const
{
    return caseVals[index];
}

Block* MultiBranch::GetCaseBlockByIndex(size_t index) const
{
    return GetSuccessor(index + 1);
}

Block* MultiBranch::GetDefaultBlock() const
{
    return GetSuccessor(0);
}

std::vector<Block*> MultiBranch::GetNormalBlocks() const
{
    auto succs = GetSuccessors();
    return {succs.begin() + 1, succs.end()};
}

std::string MultiBranch::OperandsToString() const
{
    std::vector<std::string> res;
    res.emplace_back(GetCondition()->GetIdentifier());
    res.emplace_back(GetDefaultBlock()->GetIdentifier());
    for (size_t i = 1; i < GetNumOfSuccessor(); ++i) {
        auto caseValue = std::to_string(GetCaseValByIndex(i - 1));
        auto caseId = GetSuccessor(i)->GetIdentifier();
        res.emplace_back("[" + caseValue + ", " + caseId + "]");
    }
    return StringJoin(res, ", ");
}

TryApply::TryApply(Value* callee, const FuncCallContext& callContext, Block* sucBlock, Block* errBlock, Block* parent)
    : ApplyBase(ExprKind::TRY_APPLY, callee, callContext, {sucBlock, errBlock}, parent)
{
    CJC_NULLPTR_CHECK(sucBlock);
    CJC_NULLPTR_CHECK(errBlock);
}

Block* TryApply::GetSuccessBlock() const
{
    return GetSuccessor(0);
}

Block* TryApply::GetErrorBlock() const
{
    return GetSuccessor(1);
}

TryInvoke::TryInvoke(const InvokeCallContext& callContext, Block* sucBlock, Block* errBlock, Block* parent)
    : InvokeBase(ExprKind::TRY_INVOKE, callContext, {sucBlock, errBlock}, parent)
{
}

Block* TryInvoke::GetSuccessBlock() const
{
    return GetSuccessor(0);
}

Block* TryInvoke::GetErrorBlock() const
{
    return GetSuccessor(1);
}

TryInvokeStatic::TryInvokeStatic(const InvokeCallContext& callContext, Block* sucBlock, Block* errBlock, Block* parent)
    : InvokeStaticBase(ExprKind::TRY_INVOKESTATIC, callContext, {sucBlock, errBlock}, parent)
{
}

Block* TryInvokeStatic::GetSuccessBlock() const
{
    return GetSuccessor(0);
}

Block* TryInvokeStatic::GetErrorBlock() const
{
    return GetSuccessor(1);
}

// TryUnaryExpression
TryUnaryExpression::TryUnaryExpression(
    UnaryExprKind unaryKind, Value* operand, Block* normal, Block* exception, Block* parent)
    : UnaryExpressionBase(unaryKind, operand, Cangjie::OverflowStrategy::THROWING, true, {normal, exception}, parent)
{
}

Block* TryUnaryExpression::GetSuccessBlock() const
{
    return GetSuccessor(0);
}

Block* TryUnaryExpression::GetErrorBlock() const
{
    return GetSuccessor(1);
}

// TryBinaryExpression
TryBinaryExpression::TryBinaryExpression(BinaryExprKind binaryKind, Value* lhs, Value* rhs,
    OverflowStrategy ofs, Block* normal, Block* exception, Block* parent)
    : BinaryExpressionBase(binaryKind, lhs, rhs, ofs, true, {normal, exception}, parent)
{
    // Only DIV may overflow under a non-THROWING strategy (e.g. MIN / -1).
    // Other ops reach here for exceptions like div-by-zero or overshift, so force THROWING.
    if (binaryKind != BinaryExprKind::DIV) {
        overflowStrategy = Cangjie::OverflowStrategy::THROWING;
    }
}

Block* TryBinaryExpression::GetSuccessBlock() const
{
    return GetSuccessor(0);
}

Block* TryBinaryExpression::GetErrorBlock() const
{
    return GetSuccessor(1);
}

// TryNumericCast
TryNumericCast::TryNumericCast(Value* operand, Block* normal, Block* exception, Block* parent)
    : NumericCastBase(
          ExprKind::TRY_NUMERIC_CAST, operand, Cangjie::OverflowStrategy::THROWING, {normal, exception},
          parent)
{
}

Block* TryNumericCast::GetSuccessBlock() const
{
    return GetSuccessor(0);
}

Block* TryNumericCast::GetErrorBlock() const
{
    return GetSuccessor(1);
}

// TryIntrinsic
TryIntrinsic::TryIntrinsic(const IntrisicCallContext& callContext, Block* normal, Block* exception, Block* parent)
    : IntrinsicBase(ExprKind::TRY_INTRINSIC, callContext, {normal, exception}, parent)
{
    CJC_NULLPTR_CHECK(normal);
    CJC_NULLPTR_CHECK(exception);
}

Block* TryIntrinsic::GetSuccessBlock() const
{
    return GetSuccessor(0);
}

Block* TryIntrinsic::GetErrorBlock() const
{
    return GetSuccessor(1);
}

// TryAllocate
TryAllocate::TryAllocate(Type* ty, Block* normal, Block* exception, Block* parent)
    : AllocateBase(ExprKind::TRY_ALLOCATE, ty, {normal, exception}, parent)
{
}

Block* TryAllocate::GetSuccessBlock() const
{
    return GetSuccessor(0);
}

Block* TryAllocate::GetErrorBlock() const
{
    return GetSuccessor(1);
}

// TryRawArrayAllocate
TryRawArrayAllocate::TryRawArrayAllocate(Type* eleTy, Value* size, Block* normal, Block* exception, Block* parent)
    : RawArrayAllocateBase(ExprKind::TRY_RAW_ARRAY_ALLOCATE, eleTy, size, {normal, exception}, parent)
{
}

Block* TryRawArrayAllocate::GetSuccessBlock() const
{
    return GetSuccessor(0);
}

Block* TryRawArrayAllocate::GetErrorBlock() const
{
    return GetSuccessor(1);
}

TrySpawn::TrySpawn(Value* val, Value* arg, Block* normal, Block* exception, Block* parent)
    : SpawnBase(ExprKind::TRY_SPAWN, {val, arg}, {normal, exception}, parent)
{
}

TrySpawn::TrySpawn(Value* val, Block* normal, Block* exception, Block* parent)
    : SpawnBase(ExprKind::TRY_SPAWN, {val}, {normal, exception}, parent)
{
}

Block* TrySpawn::GetSuccessBlock() const
{
    return GetSuccessor(0);
}

Block* TrySpawn::GetErrorBlock() const
{
    return GetSuccessor(1);
}

TrySpawn* TrySpawn::Clone(CHIRBuilder& builder, Block& parent) const
{
    TrySpawn* newNode = nullptr;
    auto arg = GetSpawnArg();
    if (arg != nullptr) {
        newNode = builder.CreateExpression<TrySpawn>(
            result->GetType(), GetObject(), arg, GetSuccessBlock(), GetErrorBlock(), &parent);
    } else {
        newNode = builder.CreateExpression<TrySpawn>(
            result->GetType(), GetObject(), GetSuccessBlock(), GetErrorBlock(), &parent);
    }
    if (executeClosure) {
        newNode->SetExecuteClosure(*executeClosure);
    }
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

GoTo* GoTo::Clone(CHIRBuilder& builder, Block& parent) const
{
    CJC_ASSERT(result == nullptr);
    auto newNode = builder.CreateTerminator<GoTo>(GetDestination(), &parent);
    parent.AppendExpression(newNode);
    return newNode;
}

Branch::Branch(Value* cond, Block* trueBlock, Block* falseBlock, Block* parent)
    : Expression(ExprKind::BRANCH, {cond}, parent)
{
    AppendOperand(*trueBlock);
    AppendOperand(*falseBlock);
}

Value* Branch::GetCondition() const
{
    return operands[0];
}

Block* Branch::GetTrueBlock() const
{
    return GetSuccessor(0);
}

Block* Branch::GetFalseBlock() const
{
    return GetSuccessor(1);
}

void Branch::SetSourceExpr(SourceExpr srcExpr)
{
    sourceExpr = srcExpr;
}

SourceExpr Branch::GetSourceExpr() const
{
    return sourceExpr;
}

std::string Branch::AddExtraComment() const
{
    const static std::unordered_map<SourceExpr, std::string> SOURCE_EXPR_MAP = {
        {SourceExpr::IF_EXPR, "IF_EXPR"},
        {SourceExpr::WHILE_EXPR, "WHILE_EXPR"},
        {SourceExpr::DO_WHILE_EXPR, "DO_WHILE_EXPR"},
        {SourceExpr::MATCH_EXPR, "MATCH_EXPR"},
        {SourceExpr::IF_LET_OR_WHILE_LET, "IF_LET_OR_WHILE_LET"},
        {SourceExpr::QUEST, "QUEST"},
        {SourceExpr::BINARY, "BINARY"},
        {SourceExpr::FOR_IN_EXPR, "FOR_IN_EXPR"},
        {SourceExpr::OTHER, "OTHER"},
    };
    return "sourceExpr: " + SOURCE_EXPR_MAP.at(sourceExpr);
}

Exit::Exit(Block* parent) : Expression(ExprKind::EXIT, {}, parent)
{
}

RaiseException::RaiseException(Value* value, Block* parent)
    : Expression(ExprKind::RAISE_EXCEPTION, {value}, parent)
{
}

RaiseException::RaiseException(Value* value, Block* successor, Block* parent)
    : Expression(ExprKind::RAISE_EXCEPTION, {value}, parent)
{
    AppendOperand(*successor);
}

Value* RaiseException::GetExceptionValue() const
{
    return operands[0];
}

Block* RaiseException::GetExceptionBlock() const
{
    auto succs = GetSuccessors();
    if (!succs.empty()) {
        return succs[0];
    }
    return nullptr;
}

Branch* Branch::Clone(CHIRBuilder& builder, Block& parent) const
{
    CJC_ASSERT(result == nullptr);
    auto newNode = builder.CreateTerminator<Branch>(GetCondition(), GetTrueBlock(), GetFalseBlock(), &parent);
    parent.AppendExpression(newNode);
    return newNode;
}

MultiBranch* MultiBranch::Clone(CHIRBuilder& builder, Block& parent) const
{
    CJC_ASSERT(result == nullptr);
    auto newNode = builder.CreateTerminator<MultiBranch>(
        GetCondition(), GetDefaultBlock(), GetCaseVals(), GetNormalBlocks(), &parent);
    parent.AppendExpression(newNode);
    return newNode;
}

Exit* Exit::Clone(CHIRBuilder& builder, Block& parent) const
{
    CJC_ASSERT(result == nullptr);
    auto newNode = builder.CreateTerminator<Exit>(&parent);
    parent.AppendExpression(newNode);
    return newNode;
}

TryApply* TryApply::Clone(CHIRBuilder& builder, Block& parent) const
{
    CJC_NULLPTR_CHECK(result);
    TryApply* newNode = builder.CreateExpression<TryApply>(
        result->GetType(), GetCallee(), FuncCallContext{
        .args = GetArgs(),
        .instTypeArgs = GetInstantiatedTypeArgs(),
        .thisType = GetThisType()}, GetSuccessBlock(), GetErrorBlock(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

TryInvoke* TryInvoke::Clone(CHIRBuilder& builder, Block& parent) const
{
    CJC_NULLPTR_CHECK(result);
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
    TryInvoke* newNode = builder.CreateExpression<TryInvoke>(
        result->GetType(), invokeInfo, GetSuccessBlock(), GetErrorBlock(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

TryInvokeStatic* TryInvokeStatic::Clone(CHIRBuilder& builder, Block& parent) const
{
    CJC_NULLPTR_CHECK(result);
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
    TryInvokeStatic* newNode = builder.CreateExpression<TryInvokeStatic>(
        result->GetType(), invokeInfo, GetSuccessBlock(), GetErrorBlock(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

TryUnaryExpression* TryUnaryExpression::Clone(CHIRBuilder& builder, Block& parent) const
{
    CJC_NULLPTR_CHECK(result);
    auto newNode = builder.CreateExpression<TryUnaryExpression>(result->GetType(), GetOpKind(), GetOperand(),
        GetSuccessBlock(), GetErrorBlock(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

TryBinaryExpression* TryBinaryExpression::Clone(CHIRBuilder& builder, Block& parent) const
{
    CJC_NULLPTR_CHECK(result);
    auto newNode = builder.CreateExpression<TryBinaryExpression>(result->GetType(), GetOpKind(),
        GetLHSOperand(), GetRHSOperand(), overflowStrategy, GetSuccessBlock(), GetErrorBlock(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

TryNumericCast* TryNumericCast::Clone(CHIRBuilder& builder, Block& parent) const
{
    CJC_NULLPTR_CHECK(result);
    auto newNode = builder.CreateExpression<TryNumericCast>(
        result->GetType(), GetSourceValue(), GetSuccessBlock(), GetErrorBlock(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

TryIntrinsic* TryIntrinsic::Clone(CHIRBuilder& builder, Block& parent) const
{
    CJC_NULLPTR_CHECK(result);
    auto callContext = IntrisicCallContext {
        .kind = GetIntrinsicKind(),
        .args = GetArgs(),
        .instTypeArgs = GetInstantiatedTypeArgs()
    };
    auto newNode = builder.CreateExpression<TryIntrinsic>(
        result->GetType(), callContext, GetSuccessBlock(), GetErrorBlock(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

TryAllocate* TryAllocate::Clone(CHIRBuilder& builder, Block& parent) const
{
    CJC_NULLPTR_CHECK(result);
    auto newNode = builder.CreateExpression<TryAllocate>(
        result->GetType(), GetType(), GetSuccessBlock(), GetErrorBlock(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

TryRawArrayAllocate* TryRawArrayAllocate::Clone(CHIRBuilder& builder, Block& parent) const
{
    CJC_NULLPTR_CHECK(result);
    auto newNode = builder.CreateExpression<TryRawArrayAllocate>(
        result->GetType(), GetElementType(), GetSize(), GetSuccessBlock(), GetErrorBlock(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

Exclave::Exclave(Block* parent) : Expression(ExprKind::EXCLAVE, {}, {}, parent)
{
    CJC_NULLPTR_CHECK(parent);
}

void Exclave::InitBody(BlockGroup& newBody)
{
    CJC_ASSERT(blockGroups.empty());
    newBody.SetOwnerExpression(*this);
    blockGroups.emplace_back(&newBody);
}

BlockGroup* Exclave::GetBody() const
{
    return blockGroups[0];
}

Exclave* Exclave::Clone(CHIRBuilder& builder, Block& parent) const
{
    auto newNode = builder.CreateTerminator<Exclave>(&parent);
    parent.AppendExpression(newNode);
    GetBody()->Clone(builder, *newNode);
    return newNode;
}