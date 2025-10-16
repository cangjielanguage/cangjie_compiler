// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Expression/Terminator.h"

#include <iostream>
#include <sstream>

#include "cangjie/CHIR/CHIRCasting.h"
#include "cangjie/CHIR/CHIRBuilder.h"
#include "cangjie/CHIR/ToStringUtils.h"
#include "cangjie/CHIR/Type/Type.h"
#include "cangjie/CHIR/Utils.h"
#include "cangjie/CHIR/Value.h"
#include "cangjie/Utils/CheckUtils.h"

using namespace Cangjie::CHIR;

namespace {
constexpr size_t WITH_EXCEPTION_SUCCESSOR_NUM = 2;
}

// Terminator
Terminator::Terminator(
    ExprKind kind, const std::vector<Value*>& operands, const std::vector<Block*>& successors, Block* parent)
    : Expression(kind, operands, parent)
{
    CJC_NULLPTR_CHECK(parent);
    for (auto succ : successors) {
        AppendSuccessor(*succ);
    }
}

size_t Terminator::GetFirstSuccessorIndex() const
{
    CJC_ASSERT(GetExprMajorKind() == ExprMajorKind::TERMINATOR);
    if (kind == ExprKind::BRANCH || kind == ExprKind::MULTIBRANCH) {
        return 1;
    }
    if (kind == ExprKind::RAISE_EXCEPTION) {
        return 1;
    }
    if (static_cast<uint64_t>(ExprKind::APPLY_WITH_EXCEPTION) <= static_cast<uint64_t>(kind) &&
        static_cast<uint64_t>(kind) <= static_cast<uint64_t>(ExprKind::RAW_ARRAY_ALLOCATE_WITH_EXCEPTION)) {
        CJC_ASSERT(operands.size() >= WITH_EXCEPTION_SUCCESSOR_NUM);
        return operands.size() - WITH_EXCEPTION_SUCCESSOR_NUM;
    }
    return 0;
}

void Terminator::ReplaceWith(Expression& newTerminator)
{
    CJC_ASSERT(newTerminator.IsTerminator());
    if (result != nullptr) {
        CJC_NULLPTR_CHECK(newTerminator.GetResult());
        for (auto user : result->GetUsers()) {
            user->ReplaceOperand(result, newTerminator.GetResult());
        }
    }
    auto tempParent = parent;
    RemoveSelfFromBlock();
    tempParent->AppendTerminator(StaticCast<Terminator*>(&newTerminator));
}

size_t Terminator::GetNumOfSuccessor() const
{
    return operands.size() - GetFirstSuccessorIndex();
}

Block* Terminator::GetSuccessor(size_t index) const
{
    CJC_ASSERT(operands.size() == 0 || index + GetFirstSuccessorIndex() < operands.size());
    return StaticCast<Block*>(operands[GetFirstSuccessorIndex() + index]);
}

size_t Terminator::GetNumOfOperands() const
{
    return GetFirstSuccessorIndex();
}

std::vector<Value*> Terminator::GetOperands() const
{
    CJC_ASSERT(operands.size() >= GetFirstSuccessorIndex());
    return {operands.begin(), operands.begin() + GetFirstSuccessorIndex()};
}

Value* Terminator::GetOperand(size_t idx) const
{
    CJC_ASSERT(idx < GetFirstSuccessorIndex());
    return operands[idx];
}

const std::vector<Block*> Terminator::GetSuccessors() const
{
    std::vector<Block*> succs;
    for (auto i = GetFirstSuccessorIndex(); i < operands.size(); ++i) {
        succs.emplace_back(StaticCast<Block*>(operands[i]));
    }
    return succs;
}

void Terminator::ReplaceSuccessor(Block& oldSuccessor, Block& newSuccessor)
{
    oldSuccessor.RemovePredecessor(*GetParentBlock());
    newSuccessor.AddPredecessor(GetParentBlock());

    auto it = std::find(operands.begin(), operands.end(), &oldSuccessor);
    CJC_ASSERT(it != operands.end());
    while (it != operands.end()) {
        *it = &newSuccessor;
        it = std::find(std::next(it), operands.end(), &oldSuccessor);
    }
}

void Terminator::ReplaceSuccessor(size_t index, Block& newSuccessor)
{
    CJC_ASSERT(operands.size() == 0 || index + GetFirstSuccessorIndex() < operands.size());
    GetSuccessor(index)->RemovePredecessor(*GetParentBlock());
    newSuccessor.AddPredecessor(GetParentBlock());
    operands[GetFirstSuccessorIndex() + index] = &newSuccessor;
}

void Terminator::LetSuccessorsRemoveCurBlock() const
{
    auto succs = GetSuccessors();
    for (auto suc : succs) {
        suc->RemovePredecessor(*parent);
    }
}

void Terminator::RemoveSelfFromBlock()
{
    LetSuccessorsRemoveCurBlock();
    Expression::RemoveSelfFromBlock();
}

void Terminator::AppendSuccessor(Block& block)
{
    operands.emplace_back(&block);
}

GoTo::GoTo(Block* destBlock, Block* parent) : Terminator(ExprKind::GOTO, {}, {destBlock}, parent)
{
}

Block* GoTo::GetDestination() const
{
    return GetSuccessor(0);
}

// MultiBranch

MultiBranch::MultiBranch(Value* cond, Block* defaultBlock, const std::vector<uint64_t>& vals,
    const std::vector<Block*>& succs, Block* parent)
    : Terminator(ExprKind::MULTIBRANCH, {cond}, {}, parent), caseVals(vals)
{
    CJC_NULLPTR_CHECK(cond);
    CJC_NULLPTR_CHECK(defaultBlock);
    CJC_NULLPTR_CHECK(parent);
    /* Note that successors[0] is used to store the default basic block */
    AppendSuccessor(*defaultBlock);
    for (auto b : succs) {
        AppendSuccessor(*b);
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

std::string MultiBranch::ToString([[maybe_unused]] size_t indent) const
{
    std::stringstream ss;
    ss << GetExprKindName();
    ss << "(";
    ss << GetCondition()->GetIdentifier();
    ss << ", ";
    ss << GetSuccessor(0)->GetIdentifier();
    for (size_t i = 1; i < GetNumOfSuccessor(); i++) {
        ss << ", [";
        ss << GetCaseValByIndex(i - 1) << ", ";
        ss << GetSuccessor(i)->GetIdentifier() << "]";
    }
    ss << ")";
    ss << CommentToString();

    return ss.str();
}

ExpressionWithException::ExpressionWithException(ExprKind kind, Block* parent)
    : Terminator(kind, {}, {}, parent)
{
}

ExpressionWithException::ExpressionWithException(
    ExprKind kind, const std::vector<Value*>& operands, const std::vector<Block*>& successors, Block* parent)
    : Terminator(kind, operands, successors, parent)
{
}

Block* ExpressionWithException::GetSuccessBlock() const
{
    return GetSuccessor(0);
}

Block* ExpressionWithException::GetErrorBlock() const
{
    return GetSuccessor(1);
}

FuncCallWithException::FuncCallWithException(ExprKind kind, const FuncCallContext& funcCallCtx, Block* parent)
    : ExpressionWithException(kind, parent),
      instantiatedTypeArgs(funcCallCtx.instTypeArgs),
      thisType(funcCallCtx.thisType)
{
}

Type* FuncCallWithException::GetThisType() const
{
    return thisType;
}

void FuncCallWithException::SetThisType(Type* type)
{
    thisType = type;
}

const std::vector<Type*>& FuncCallWithException::GetInstantiatedTypeArgs() const
{
    return instantiatedTypeArgs;
}

ApplyWithException::ApplyWithException(
    Value* callee, const FuncCallContext& callContext, Block* sucBlock, Block* errBlock, Block* parent)
    : FuncCallWithException(ExprKind::APPLY_WITH_EXCEPTION, callContext, parent)
{
    CJC_NULLPTR_CHECK(callee);
    CJC_NULLPTR_CHECK(sucBlock);
    CJC_NULLPTR_CHECK(errBlock);
    CJC_NULLPTR_CHECK(parent);
    AppendOperand(*callee);
    for (auto op : callContext.args) {
        AppendOperand(*op);
    }

    AppendSuccessor(*sucBlock);
    AppendSuccessor(*errBlock);
}

Value* ApplyWithException::GetCallee() const
{
    return operands[0];
}

/** @brief Get a list of the ApplyWithException operation argument nodes */
std::vector<Value*> ApplyWithException::GetArgs() const
{
    if (GetFirstSuccessorIndex() <= 1) {
        return {};
    } else {
        return {operands.begin() + 1, operands.begin() + GetFirstSuccessorIndex()};
    }
}

Type* ApplyWithException::GetInstParentCustomTyOfCallee(CHIRBuilder& builder) const
{
    return GetInstParentCustomTypeForAweCallee(*this, builder);
}

std::string ApplyWithException::ToString([[maybe_unused]] size_t indent) const
{
    std::stringstream ss;
    ss << GetExprKindName();
    ss << "(";
    ss << ThisTypeToString(thisType).AddDelimiterOrNot(", ").Str();
    ss << GetCallee()->GetIdentifier();
    ss << InstTypeArgsToString(instantiatedTypeArgs);
    ss << StringWrapper(", ").AppendOrClear(ExprWithExceptionOperandsToString(GetArgs(), GetSuccessors())).Str();
    ss << ")";
    ss << CommentToString();
    return ss.str();
}

inline static void CheckVirFuncInvokeInfo(const InvokeCallContext& callContext)
{
    CJC_NULLPTR_CHECK(callContext.caller);
    CJC_NULLPTR_CHECK(callContext.funcCallCtx.thisType);
    CJC_ASSERT(!callContext.virMethodCtx.srcCodeIdentifier.empty());
    CJC_NULLPTR_CHECK(callContext.virMethodCtx.originalFuncType);
}

DynamicDispatchWithException::DynamicDispatchWithException(
    ExprKind kind, const InvokeCallContext& callContext, Block* sucBlock, Block* errBlock, Block* parent)
    : FuncCallWithException(kind, callContext.funcCallCtx, parent),
      virMethodCtx(callContext.virMethodCtx)
{
    CJC_NULLPTR_CHECK(sucBlock);
    CJC_NULLPTR_CHECK(errBlock);
    CJC_NULLPTR_CHECK(parent);
    CheckVirFuncInvokeInfo(callContext);
    AppendOperand(*callContext.caller);
    for (auto op : callContext.funcCallCtx.args) {
        AppendOperand(*op);
    }

    AppendSuccessor(*sucBlock);
    AppendSuccessor(*errBlock);
}

const std::string& DynamicDispatchWithException::GetMethodName() const
{
    return virMethodCtx.srcCodeIdentifier;
}

FuncType* DynamicDispatchWithException::GetMethodType() const
{
    return virMethodCtx.originalFuncType;
}

const std::vector<GenericType*>& DynamicDispatchWithException::GetGenericTypeParams() const
{
    return virMethodCtx.genericTypeParams;
}

std::vector<VTableSearchRes> DynamicDispatchWithException::GetVirtualMethodInfo(CHIRBuilder& builder) const
{
    auto thisTypeDeref = thisType->StripAllRefs();
    if (thisTypeDeref->IsThis()) {
        thisTypeDeref = GetTopLevelFunc()->GetParentCustomTypeDef()->GetType();
    }
    std::vector<Type*> instParamTypes;
    for (auto arg : GetArgs()) {
        instParamTypes.emplace_back(arg->GetType());
    }
    auto instFuncType = builder.GetType<FuncType>(instParamTypes, builder.GetUnitTy());
    FuncCallType funcCallType{virMethodCtx.srcCodeIdentifier, instFuncType, instantiatedTypeArgs};
    auto res = GetFuncIndexInVTable(*thisTypeDeref, funcCallType, IsInvokeStaticBase(), builder);
    CJC_ASSERT(!res.empty());
    return res;
}

size_t DynamicDispatchWithException::GetVirtualMethodOffset(CHIRBuilder* builder) const
{
    auto offset = Get<VirMethodOffset>();
    if (offset.has_value()) {
        return offset.value();
    } else {
        CJC_NULLPTR_CHECK(builder);
        return GetVirtualMethodInfo(*builder)[0].offset;
    }
}

ClassType* DynamicDispatchWithException::GetInstSrcParentCustomTypeOfMethod(CHIRBuilder& builder) const
{
    for (auto& r : GetVirtualMethodInfo(builder)) {
        if (r.offset == GetVirtualMethodOffset()) {
            CJC_NULLPTR_CHECK(r.instSrcParentType);
            return r.instSrcParentType;
        }
    }
    CJC_ABORT();
    return nullptr;
}

AttributeInfo DynamicDispatchWithException::GetVirtualMethodAttr(CHIRBuilder& builder) const
{
    for (auto& r : GetVirtualMethodInfo(builder)) {
        if (r.offset == GetVirtualMethodOffset()) {
            CJC_NULLPTR_CHECK(r.instSrcParentType);
            return r.attr;
        }
    }
    CJC_ABORT();
    return AttributeInfo{};
}

InvokeWithException::InvokeWithException(
    const InvokeCallContext& callContext, Block* sucBlock, Block* errBlock, Block* parent)
    : DynamicDispatchWithException(ExprKind::INVOKE_WITH_EXCEPTION, callContext, sucBlock, errBlock, parent)
{
}

Value* InvokeWithException::GetObject() const
{
    return operands[0];
}

/** @brief Get the call args of this InvokeWithException operation */
std::vector<Value*> InvokeWithException::GetArgs() const
{
    return {operands.begin(), operands.begin() + GetFirstSuccessorIndex()};
}

std::string InvokeWithException::ToString([[maybe_unused]] size_t indent) const
{
    std::stringstream ss;
    ss << GetExprKindName();
    ss << "(";
    ss << ThisTypeToString(thisType).AddDelimiterOrNot(", ").Str();
    ss << GetMethodName();
    ss << InstTypeArgsToString(instantiatedTypeArgs) << ": ";
    ss << GetMethodType()->ToString() << ", ";
    ss << ExprWithExceptionOperandsToString(GetArgs(), GetSuccessors());
    ss << ")";
    ss << CommentToString();
    return ss.str();
}

InvokeStaticWithException::InvokeStaticWithException(
    const InvokeCallContext& callContext, Block* sucBlock, Block* errBlock, Block* parent)
    : DynamicDispatchWithException(ExprKind::INVOKESTATIC_WITH_EXCEPTION, callContext, sucBlock, errBlock, parent)
{
}

Value* InvokeStaticWithException::GetRTTIValue() const
{
    return operands[0];
}

std::vector<Value*> InvokeStaticWithException::GetArgs() const
{
    return {operands.begin() + 1, operands.begin() + GetFirstSuccessorIndex()};
}

std::string InvokeStaticWithException::ToString([[maybe_unused]] size_t indent) const
{
    std::stringstream ss;
    ss << GetExprKindName();
    ss << "(";
    ss << ThisTypeToString(thisType).AddDelimiterOrNot(", ").Str();
    ss << GetMethodName();
    ss << InstTypeArgsToString(instantiatedTypeArgs) << ": ";
    ss << GetMethodType()->ToString() << ", ";
    ss << GetRTTIValue()->GetIdentifier();
    ss << StringWrapper(", ").AppendOrClear(ExprWithExceptionOperandsToString(GetArgs(), GetSuccessors())).Str();
    ss << ")";
    ss << CommentToString();
    return ss.str();
}

// IntOpWithException
IntOpWithException::IntOpWithException(
    ExprKind unaryKind, Value* operand, Block* normal, Block* exception, Block* parent)
    : ExpressionWithException(ExprKind::INT_OP_WITH_EXCEPTION, {operand}, {normal, exception}, parent),
    opKind(unaryKind)
{
}

IntOpWithException::IntOpWithException(
    ExprKind binaryKind, Value* lhs, Value* rhs, Block* normal, Block* exception, Block* parent)
    : ExpressionWithException(ExprKind::INT_OP_WITH_EXCEPTION, {lhs, rhs}, {normal, exception}, parent),
    opKind(binaryKind)
{
}

ExprKind IntOpWithException::GetOpKind() const
{
    return opKind;
}

std::string IntOpWithException::GetOpKindName() const
{
    return ExprKindMgr::Instance()->GetKindName(static_cast<size_t>(opKind)) + "WithException";
}

Value* IntOpWithException::GetLHSOperand() const
{
    return GetOperand(0);
}

Value* IntOpWithException::GetRHSOperand() const
{
    return GetOperand(1);
}

Cangjie::OverflowStrategy IntOpWithException::GetOverflowStrategy() const
{
    if (opKind == ExprKind::DIV) {
        return overflowStrategy;
    }
    return Cangjie::OverflowStrategy::THROWING;
}

std::string IntOpWithException::ToString([[maybe_unused]] size_t indent) const
{
    std::stringstream ss;
    ss << GetOpKindName();
    ss << "(";
    ss << ExprWithExceptionOperandsToString(GetOperands(), GetSuccessors());
    ss << ")";
    ss << CommentToString();
    return ss.str();
}

// TypeCastWithException
TypeCastWithException::TypeCastWithException(Value* operand, Block* normal, Block* exception, Block* parent)
    : ExpressionWithException(ExprKind::TYPECAST_WITH_EXCEPTION, {operand}, {normal, exception}, parent)
{
}

Cangjie::OverflowStrategy TypeCastWithException::GetOverflowStrategy() const
{
    return Cangjie::OverflowStrategy::THROWING;
}

/** @brief Get the source value of this cast operation */
Value* TypeCastWithException::GetSourceValue() const
{
    return operands[0];
}

Type* TypeCastWithException::GetSourceTy() const
{
    return operands[0]->GetType();
}

Type* TypeCastWithException::GetTargetTy() const
{
    return result->GetType();
}

std::string TypeCastWithException::ToString([[maybe_unused]] size_t indent) const
{
    std::stringstream ss;
    ss << GetExprKindName();
    ss << "(";
    auto args = std::vector<Value*>{GetSourceValue()};
    ss << ExprWithExceptionOperandsToString(args, GetSuccessors()) << ", ";
    ss << ")";
    ss << CommentToString();
    return ss.str();
}

// IntrinsicWithException
IntrinsicWithException::IntrinsicWithException(
    const IntrisicCallContext& callContext, Block* normal, Block* exception, Block* parent)
    : ExpressionWithException(ExprKind::INTRINSIC_WITH_EXCEPTION, callContext.args, {normal, exception}, parent),
    intrinsicKind(callContext.kind),
    instantiatedTypeArgs(callContext.instTypeArgs)
{
}

IntrinsicKind IntrinsicWithException::GetIntrinsicKind() const
{
    return intrinsicKind;
}

const std::vector<Type*>& IntrinsicWithException::GetInstantiatedTypeArgs() const
{
    return instantiatedTypeArgs;
}

const std::vector<Value*> IntrinsicWithException::GetArgs() const
{
    return GetOperands();
}

std::string IntrinsicWithException::ToString([[maybe_unused]] size_t indent) const
{
    std::stringstream ss;
    ss << GetExprKindName() << "/" << INTRINSIC_KIND_TO_STRING_MAP.at(intrinsicKind);
    ss << InstTypeArgsToString(instantiatedTypeArgs);
    ss << "(";
    ss << ExprWithExceptionOperandsToString(GetOperands(), GetSuccessors());
    ss << ")";
    ss << CommentToString();
    return ss.str();
}

// AllocateWithException
AllocateWithException::AllocateWithException(Type* ty, Block* normal, Block* exception, Block* parent)
    : ExpressionWithException(ExprKind::ALLOCATE_WITH_EXCEPTION, {}, {normal, exception}, parent), ty(ty)
{
}

Type* AllocateWithException::GetType() const
{
    return ty;
}

std::string AllocateWithException::ToString([[maybe_unused]] size_t indent) const
{
    std::stringstream ss;
    ss << GetExprKindName();
    ss << "(";
    ss << ty->ToString() << ", ";
    ss << ExprWithExceptionOperandsToString(std::vector<Value*>{}, GetSuccessors());
    ss << ")";
    ss << CommentToString();
    return ss.str();
}

// RawArrayAllocateWithException
RawArrayAllocateWithException::RawArrayAllocateWithException(
    Type* eleTy, Value* size, Block* normal, Block* exception, Block* parent)
    : ExpressionWithException(ExprKind::RAW_ARRAY_ALLOCATE_WITH_EXCEPTION, {size}, {normal, exception}, parent),
    elementType(eleTy)
{
}

Value* RawArrayAllocateWithException::GetSize() const
{
    return operands[0];
}

Type* RawArrayAllocateWithException::GetElementType() const
{
    return elementType;
}

std::string RawArrayAllocateWithException::ToString([[maybe_unused]] size_t indent) const
{
    std::stringstream ss;
    ss << GetExprKindName();
    ss << "(";
    ss << elementType->ToString() << ", ";
    auto args = std::vector<Value*>{GetSize()};
    ss << ExprWithExceptionOperandsToString(args, GetSuccessors());
    ss << ")";
    ss << CommentToString();
    return ss.str();
}

SpawnWithException::SpawnWithException(
    Value* val, Value* arg, Block* normal, Block* exception, Block* parent)
    : ExpressionWithException(ExprKind::SPAWN_WITH_EXCEPTION, {val, arg}, {normal, exception}, parent)
{
}

SpawnWithException::SpawnWithException(
    Value* val, Block* normal, Block* exception, Block* parent)
    : ExpressionWithException(ExprKind::SPAWN_WITH_EXCEPTION, {val}, {normal, exception}, parent)
{
}

Value* SpawnWithException::GetFuture() const
{
    CJC_ASSERT(!IsExecuteClosure());
    return operands[0];
}

void SpawnWithException::SetExecuteClosure(FuncBase& func)
{
    executeClosure = &func;
}

Value* SpawnWithException::GetSpawnArg() const
{
    if (GetFirstSuccessorIndex() > 1) {
        return operands[1];
    }
    return nullptr;
}

Value* SpawnWithException::GetClosure() const
{
    CJC_ASSERT(IsExecuteClosure());
    return operands[0];
}

FuncBase* SpawnWithException::GetExecuteClosure() const
{
    return executeClosure;
}

bool SpawnWithException::IsExecuteClosure() const
{
    return executeClosure != nullptr;
}

std::string SpawnWithException::ToString([[maybe_unused]] size_t indent) const
{
    std::stringstream ss;
    ss << GetExprKindName();
    ss << "(";
    auto args = std::vector<Value*>{GetFuture()};
    if (auto arg = GetSpawnArg()) {
        args.emplace_back(arg);
    }
    ss << ExprWithExceptionOperandsToString(args, GetSuccessors());
    ss << ")";
    ss << CommentToString();
    if (IsExecuteClosure()) {
        ss << AddExtraComment("executeClosure: " + GetExecuteClosure()->GetIdentifier());
    }
    return ss.str();
}

SpawnWithException* SpawnWithException::Clone(CHIRBuilder& builder, Block& parent) const
{
    SpawnWithException* newNode = nullptr;
    auto arg = GetSpawnArg();
    if (arg != nullptr) {
        newNode = builder.CreateExpression<SpawnWithException>(
            result->GetType(), GetFuture(), arg, GetSuccessBlock(), GetErrorBlock(), &parent);
    } else {
        newNode = builder.CreateExpression<SpawnWithException>(
            result->GetType(), GetFuture(), GetSuccessBlock(), GetErrorBlock(), &parent);
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
    : Terminator(ExprKind::BRANCH, {cond}, {trueBlock, falseBlock}, parent)
{
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

std::string Branch::ToString([[maybe_unused]] size_t indent) const
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
    std::stringstream ss;
    ss << Expression::ToString(indent);
    ss << CommentToString();
    ss << AddExtraComment("sourceExpr: " + SOURCE_EXPR_MAP.at(sourceExpr));
    return ss.str();
}

Exit::Exit(Block* parent) : Terminator(ExprKind::EXIT, {}, {}, parent)
{
}

RaiseException::RaiseException(Value* value, Block* parent)
    : Terminator(ExprKind::RAISE_EXCEPTION, {value}, {}, parent)
{
}

RaiseException::RaiseException(Value* value, Block* successor, Block* parent)
    : Terminator(ExprKind::RAISE_EXCEPTION, {value}, {successor}, parent)
{
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

ApplyWithException* ApplyWithException::Clone(CHIRBuilder& builder, Block& parent) const
{
    CJC_NULLPTR_CHECK(result);
    ApplyWithException* newNode = builder.CreateExpression<ApplyWithException>(
        result->GetType(), GetCallee(), FuncCallContext{
        .args = GetArgs(),
        .instTypeArgs = GetInstantiatedTypeArgs(),
        .thisType = GetThisType()}, GetSuccessBlock(), GetErrorBlock(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

InvokeWithException* InvokeWithException::Clone(CHIRBuilder& builder, Block& parent) const
{
    CJC_NULLPTR_CHECK(result);
    auto args = GetArgs();
    args.erase(args.begin());
    auto invokeInfo = InvokeCallContext {
        .caller = GetObject(),
        .funcCallCtx = FuncCallContext {
            .args = args,
            .instTypeArgs = instantiatedTypeArgs,
            .thisType = thisType
        },
        .virMethodCtx = virMethodCtx
    };
    InvokeWithException* newNode = builder.CreateExpression<InvokeWithException>(
        result->GetType(), invokeInfo, GetSuccessBlock(), GetErrorBlock(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

InvokeStaticWithException* InvokeStaticWithException::Clone(CHIRBuilder& builder, Block& parent) const
{
    CJC_NULLPTR_CHECK(result);
    auto invokeInfo = InvokeCallContext {
        .caller = GetRTTIValue(),
        .funcCallCtx = FuncCallContext {
            .args = GetArgs(),
            .instTypeArgs = instantiatedTypeArgs,
            .thisType = thisType
        },
        .virMethodCtx = virMethodCtx
    };
    InvokeStaticWithException* newNode = builder.CreateExpression<InvokeStaticWithException>(
        result->GetType(), invokeInfo, GetSuccessBlock(), GetErrorBlock(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

IntOpWithException* IntOpWithException::Clone(CHIRBuilder& builder, Block& parent) const
{
    CJC_NULLPTR_CHECK(result);
    IntOpWithException* newNode = nullptr;
    if (GetFirstSuccessorIndex() == 1) {
        newNode = builder.CreateExpression<IntOpWithException>(
            result->GetType(), GetOpKind(), GetLHSOperand(), GetSuccessBlock(), GetErrorBlock(), &parent);
    } else {
        newNode = builder.CreateExpression<IntOpWithException>(result->GetType(), GetOpKind(), GetLHSOperand(),
            GetRHSOperand(), GetSuccessBlock(), GetErrorBlock(), &parent);
    }
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

TypeCastWithException* TypeCastWithException::Clone(CHIRBuilder& builder, Block& parent) const
{
    CJC_NULLPTR_CHECK(result);
    auto newNode = builder.CreateExpression<TypeCastWithException>(
        result->GetType(), GetSourceValue(), GetSuccessBlock(), GetErrorBlock(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

IntrinsicWithException* IntrinsicWithException::Clone(CHIRBuilder& builder, Block& parent) const
{
    CJC_NULLPTR_CHECK(result);
    auto callContext = IntrisicCallContext {
        .kind = GetIntrinsicKind(),
        .args = GetArgs(),
        .instTypeArgs = GetInstantiatedTypeArgs()
    };
    auto newNode = builder.CreateExpression<IntrinsicWithException>(
        result->GetType(), callContext, GetSuccessBlock(), GetErrorBlock(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

AllocateWithException* AllocateWithException::Clone(CHIRBuilder& builder, Block& parent) const
{
    CJC_NULLPTR_CHECK(result);
    auto newNode = builder.CreateExpression<AllocateWithException>(
        result->GetType(), GetType(), GetSuccessBlock(), GetErrorBlock(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

RawArrayAllocateWithException* RawArrayAllocateWithException::Clone(CHIRBuilder& builder, Block& parent) const
{
    CJC_NULLPTR_CHECK(result);
    auto newNode = builder.CreateExpression<RawArrayAllocateWithException>(
        result->GetType(), GetElementType(), GetSize(), GetSuccessBlock(), GetErrorBlock(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}
