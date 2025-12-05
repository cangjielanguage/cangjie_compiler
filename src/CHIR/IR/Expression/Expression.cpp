// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include <iostream>
#include <sstream>

#include "cangjie/CHIR/Utils/CHIRCasting.h"
#include "cangjie/CHIR/IR/CHIRBuilder.h"
#include "cangjie/CHIR/IR/Expression/Terminator.h"
#include "cangjie/CHIR/Utils/StringWrapper.h"
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

void GetAllSuperClassDefs(const ClassDef& def, std::unordered_set<ClassDef*>& allSuperDefs)
{
    for (auto parent : def.GetImplementedInterfaceDefs()) {
        allSuperDefs.emplace(parent);
        GetAllSuperClassDefs(*parent, allSuperDefs);
    }
    if (auto superClass = def.GetSuperClassDef()) {
        allSuperDefs.emplace(superClass);
        GetAllSuperClassDefs(*superClass, allSuperDefs);
    }
    for (auto extendDef : def.GetExtends()) {
        for (auto parent : extendDef->GetImplementedInterfaceDefs()) {
            allSuperDefs.emplace(parent);
            GetAllSuperClassDefs(*parent, allSuperDefs);
        }
    }
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

bool Expression::IsConstantFloat() const
{
    if (kind == ExprKind::CONSTANT) {
        auto constant = StaticCast<const Constant*>(this);
        return constant->IsFloatLit();
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

bool Expression::IsConstant() const
{
    return kind == ExprKind::CONSTANT;
}

bool Expression::IsTerminator() const
{
    return ExprKindMgr::Instance()->GetMajorKind(kind) == ExprMajorKind::TERMINATOR;
}

bool Expression::IsUnaryExpr() const
{
    return ExprKindMgr::Instance()->GetMajorKind(kind) == ExprMajorKind::UNARY_EXPR;
}

bool Expression::IsLoad() const
{
    return kind == ExprKind::LOAD;
}

bool Expression::IsTypeCast() const
{
    return kind == ExprKind::TYPECAST;
}

bool Expression::IsDynamicDispatch() const
{
    return kind == ExprKind::INVOKE || kind == ExprKind::INVOKESTATIC;
}

bool Expression::IsFuncCall() const
{
    return kind == ExprKind::APPLY || kind == ExprKind::INVOKE || kind == ExprKind::INVOKESTATIC;
}

bool Expression::IsField() const
{
    return kind == ExprKind::FIELD;
}

bool Expression::IsApply() const
{
    return kind == ExprKind::APPLY;
}

bool Expression::IsApplyWithException() const
{
    return kind == ExprKind::APPLY_WITH_EXCEPTION;
}

bool Expression::IsInvoke() const
{
    return kind == ExprKind::INVOKE;
}

bool Expression::IsLambda() const
{
    return kind == ExprKind::LAMBDA;
}

bool Expression::IsBinaryExpr() const
{
    return ExprKindMgr::Instance()->GetMajorKind(kind) == ExprMajorKind::BINARY_EXPR;
}

bool Expression::IsIntOpWithException() const
{
    return kind == ExprKind::INT_OP_WITH_EXCEPTION;
}

bool Expression::IsInvokeStaticBase() const
{
    return kind == ExprKind::INVOKESTATIC || kind == ExprKind::INVOKESTATIC_WITH_EXCEPTION;
}

bool Expression::IsDebug() const
{
    return kind == ExprKind::DEBUGEXPR;
}

std::string Expression::GetExprKindName() const
{
    return ExprKindMgr::Instance()->GetKindName(static_cast<size_t>(kind));
}

Block* Expression::GetParentBlock() const
{
    return parent;
}

const std::vector<BlockGroup*>& Expression::GetBlockGroups() const
{
    return blockGroups;
}

size_t Expression::GetNumOfOperands() const
{
    return operands.size();
}

std::vector<Value*> Expression::GetOperands() const
{
    return operands;
}

Value* Expression::GetOperand(size_t idx) const
{
    CJC_ASSERT(idx < operands.size());
    return operands[idx];
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
    if (parent == nullptr) {
        return nullptr;
    }
    return parent->GetParentBlockGroup();
}

Func* Expression::GetTopLevelFunc() const
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
    if (parent != nullptr) {
        parent->RemoveExprOnly(*this);
        parent = nullptr;
    }
    EraseOperands();
}

void Expression::ReplaceWith(Expression& newExpr)
{
    CJC_ASSERT(!newExpr.IsTerminator());
    // 1. replace result
    for (auto user : result->GetUsers()) {
        user->ReplaceOperand(result, newExpr.GetResult());
    }
    // 2. break double linkage between operands and current expr
    EraseOperands();

    // 3. remove new expr from its parent block
    if (newExpr.parent != nullptr) {
        newExpr.parent->RemoveExprOnly(newExpr);
    }

    // 4. replace to new expr in parent block
    CJC_NULLPTR_CHECK(parent);
    for (size_t i = 0; i < parent->exprs.size(); ++i) {
        if (parent->exprs[i] == this) {
            parent->exprs[i] = &newExpr;
        }
    }
    newExpr.parent = parent;
    parent = nullptr;
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
    // you shouldn't move an expression after a terminator, that's illegal ir
    CJC_ASSERT(!expr->IsTerminator());
    if (parent != nullptr) {
        parent->RemoveExprOnly(*this);
        if (IsTerminator()) {
            for (auto suc : StaticCast<Terminator*>(this)->GetSuccessors()) {
                suc->RemovePredecessor(*parent);
            }
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
    parent->RemoveExprOnly(*this);
    if (IsTerminator()) {
        for (auto suc : StaticCast<Terminator*>(this)->GetSuccessors()) {
            suc->RemovePredecessor(*parent);
        }
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
    if (oldOperand == newOperand) {
        return;
    }
    for (unsigned i = 0; i < operands.size(); i++) {
        if (operands[i] == oldOperand) {
            operands[i] = newOperand;
            operands[i]->AddUserOnly(this);
            oldOperand->RemoveUserOnly(this);
        }
    }
}

void Expression::ReplaceOperand(size_t idx, Value* newOperand)
{
    CJC_ASSERT(idx < operands.size());
    auto oldOperand = operands[idx];
    if (oldOperand == newOperand) {
        return;
    }
    operands[idx] = newOperand;
    newOperand->AddUserOnly(this);
    bool needRemoveOldOpUser = true;
    for (auto op : operands) {
        if (op == oldOperand) {
            needRemoveOldOpUser = false;
        }
    }
    if (needRemoveOldOpUser) {
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

std::string Expression::ToString([[maybe_unused]] size_t indent) const
{
    std::stringstream ss;
    ss << GetExprKindName();
    ss << "(";
    ss << ExprOperandsToString(operands);
    ss << ")";
    ss << CommentToString();
    return ss.str();
}

void Expression::Dump() const
{
    std::cout << ToString() << std::endl;
}

void Expression::AppendOperand(Value& op)
{
    operands.emplace_back(&op);
    op.AddUserOnly(this);
}

std::string Expression::CommentToString() const
{
    auto comment = StringWrapper(this->ToStringAnnotationMap());
    if (result != nullptr) {
        comment.Append(result->ToStringAnnotationMap(), ",");
    }
    if (comment.Str().empty()) {
        return "";
    }
    return " // " + comment.Str();
}

std::string Expression::AddExtraComment(const std::string& comment) const
{
    CJC_ASSERT(!comment.empty());
    if ((result && result->ToStringAnnotationMap().empty()) || this->ToStringAnnotationMap().empty()) {
        return " // " + comment;
    } else {
        return ", " + comment;
    }
}

Value* UnaryExpression::GetOperand() const
{
    return GetOperand(0);
}

Cangjie::OverflowStrategy UnaryExpression::GetOverflowStrategy() const
{
    return overflowStrategy;
}

std::string UnaryExpression::ToString(size_t indent) const
{
    std::stringstream ss;
    ss << Expression::ToString(indent);
    ss << AddExtraComment(OverflowToString(overflowStrategy));
    return ss.str();
}

BinaryExpression::BinaryExpression(ExprKind kind, Value* lhs, Value* rhs, OverflowStrategy ofs, Block* parent)
    : Expression(kind, {lhs, rhs}, {}, parent), overflowStrategy(ofs)
{
}

BinaryExpression::BinaryExpression(ExprKind kind, Value* lhs, Value* rhs, Block* parent)
    : Expression(kind, {lhs, rhs}, {}, parent)
{
    CJC_ASSERT(kind >= ExprKind::BITAND);
}

Value* BinaryExpression::GetLHSOperand() const
{
    return GetOperand(0);
}

Value* BinaryExpression::GetRHSOperand() const
{
    return GetOperand(1);
}

Cangjie::OverflowStrategy BinaryExpression::GetOverflowStrategy() const
{
    return overflowStrategy;
}

std::string BinaryExpression::ToString([[maybe_unused]] size_t indent) const
{
    std::stringstream ss;
    ss << Expression::ToString(indent);
    ss << AddExtraComment(OverflowToString(overflowStrategy));
    return ss.str();
}

std::string Constant::ToString([[maybe_unused]] size_t indent) const
{
    std::stringstream ss;
    ss << GetExprKindName();
    ss << "(";
    ss << GetValue()->ToString();
    ss << ")";
    ss << CommentToString();
    return ss.str();
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
Type* Allocate::GetType() const
{
    return ty;
}

std::string Allocate::ToString([[maybe_unused]] size_t indent) const
{
    std::stringstream ss;
    ss << GetExprKindName();
    ss << "(" << ty->ToString() << ")";
    ss << CommentToString();
    return ss.str();
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
Value* GetElementRef::GetLocation() const
{
    return operands[0];
}

const std::vector<uint64_t>& GetElementRef::GetPath() const
{
    return path;
}

std::string GetElementRef::ToString([[maybe_unused]] size_t indent) const
{
    std::stringstream ss;
    ss << GetExprKindName();
    ss << "(";
    ss << GetLocation()->GetIdentifier();
    for (auto p : GetPath()) {
        ss << ", " << p;
    }
    ss << ")";
    ss << CommentToString();
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

std::string GetElementByName::ToString([[maybe_unused]] size_t indent) const
{
    std::stringstream ss;
    ss << GetExprKindName();
    ss << "(";
    ss << GetLocation()->GetIdentifier();
    for (const auto& p : GetNames()) {
        ss << ", " << p;
    }
    ss << ")";
    ss << CommentToString();
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

Value* StoreElementRef::GetLocation() const
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

std::string StoreElementByName::ToString([[maybe_unused]] size_t indent) const
{
    std::stringstream ss;
    ss << GetExprKindName();
    ss << "(";
    ss << GetValue()->GetIdentifier() << ", ";
    ss << GetLocation()->GetIdentifier();
    for (const auto& p : GetNames()) {
        ss << ", " << p;
    }
    ss << ")";
    ss << CommentToString();
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

// Apply
Apply::Apply(Value* callee, const FuncCallContext& callContext, Block* parent)
    : FuncCall(ExprKind::APPLY, callContext, parent)
{
    CJC_NULLPTR_CHECK(parent);
    AppendOperand(*callee);
    for (auto op : callContext.args) {
        AppendOperand(*op);
    }
}

Value* Apply::GetCallee() const
{
    return operands[0];
}

void Apply::SetSuperCall()
{
    this->isSuperCall = true;
}

bool Apply::IsSuperCall() const
{
    return isSuperCall;
}

Type* Apply::GetInstParentCustomTyOfCallee(CHIRBuilder& builder) const
{
    return GetInstParentCustomTypeForApplyCallee(*this, builder);
}

std::vector<Value*> Apply::GetArgs() const
{
    std::vector<Value*> args;
    if (operands.size() > 1) {
        args.assign(operands.begin() + 1, operands.end());
    }
    return args;
}

std::string Apply::ToString([[maybe_unused]] size_t indent) const
{
    std::stringstream ss;
    ss << GetExprKindName();
    ss << "(";
    ss << ThisTypeToString(thisType).AddDelimiterOrNot(", ").Str();
    ss << GetCallee()->GetIdentifier();
    ss << InstTypeArgsToString(instantiatedTypeArgs);
    ss << StringWrapper(", ").AppendOrClear(ExprOperandsToString(GetArgs())).Str();
    ss << ")";
    ss << CommentToString();
    if (IsSuperCall()) {
        ss << AddExtraComment("isSuperCall");
    }

    return ss.str();
}

DynamicDispatch::DynamicDispatch(ExprKind kind, const InvokeCallContext& callContext, Block* parent)
    : FuncCall(kind, callContext.funcCallCtx, parent),
      virMethodCtx(callContext.virMethodCtx)
{
    CJC_ASSERT(!virMethodCtx.srcCodeIdentifier.empty());
    CJC_NULLPTR_CHECK(virMethodCtx.originalFuncType);
    CJC_NULLPTR_CHECK(callContext.funcCallCtx.thisType);
}

const std::string& DynamicDispatch::GetMethodName() const
{
    return virMethodCtx.srcCodeIdentifier;
}

FuncType* DynamicDispatch::GetMethodType() const
{
    return virMethodCtx.originalFuncType;
}

const std::vector<GenericType*>& DynamicDispatch::GetGenericTypeParams() const
{
    return virMethodCtx.genericTypeParams;
}

std::vector<VTableSearchRes> DynamicDispatch::GetVirtualMethodInfo(CHIRBuilder& builder) const
{
    auto thisTypeDeref = thisType->StripAllRefs();
    if (thisTypeDeref->IsThis()) {
        thisTypeDeref = GetTopLevelFunc()->GetParentCustomTypeDef()->GetType();
    }
    std::vector<Type*> instParamTypes;
    for (auto arg : GetArgs()) {
        instParamTypes.emplace_back(arg->GetType());
    }
    if (!IsInvokeStaticBase()) {
        instParamTypes.erase(instParamTypes.begin());
    }
    auto instFuncType = builder.GetType<FuncType>(instParamTypes, builder.GetUnitTy());
    FuncCallType funcCallType{virMethodCtx.srcCodeIdentifier, instFuncType, instantiatedTypeArgs};
    auto res = GetFuncIndexInVTable(*thisTypeDeref, funcCallType, builder);
    CJC_ASSERT(!res.empty());
    return res;
}

size_t DynamicDispatch::GetVirtualMethodOffset(CHIRBuilder* builder) const
{
    auto offset = Get<VirMethodOffset>();
    if (offset.has_value()) {
        return offset.value();
    } else {
        CJC_NULLPTR_CHECK(builder);
        return GetVirtualMethodInfo(*builder)[0].offset;
    }
}

ClassType* DynamicDispatch::GetInstSrcParentCustomTypeOfMethod(CHIRBuilder& builder) const
{
    for (auto& r : GetVirtualMethodInfo(builder)) {
        if (r.offset == GetVirtualMethodOffset()) {
            auto def = r.instSrcParentType->GetClassDef();
            const auto& parentFuncInfo = def->GetDefVTable().GetExpectedTypeVTable(*def->GetType());
            auto originalType = parentFuncInfo.GetVirtualMethods()[r.offset].GetOriginalFuncType();
            if (VirMethodTypeIsMatched(*originalType, *GetMethodType())) {
                CJC_NULLPTR_CHECK(r.instSrcParentType);
                return r.instSrcParentType;
            }
        }
    }
    CJC_ABORT();
    return nullptr;
}

AttributeInfo DynamicDispatch::GetVirtualMethodAttr(CHIRBuilder& builder) const
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

// Invoke
Invoke::Invoke(const InvokeCallContext& callContext, Block* parent)
    : DynamicDispatch(ExprKind::INVOKE, callContext, parent)
{
    CJC_NULLPTR_CHECK(parent);
    AppendOperand(*callContext.caller);
    for (auto op : callContext.funcCallCtx.args) {
        AppendOperand(*op);
    }
}

Value* Invoke::GetObject() const
{
    return operands[0];
}

void Invoke::UpdateThisType()
{
    auto objType = GetObject()->GetType();
    auto objDerefType = DynamicCast<ClassType*>(objType->StripAllRefs());
    auto thisDerefType = DynamicCast<ClassType*>(thisType->StripAllRefs());
    // for now, we only care about class type, maybe we can handle other types later
    if (objDerefType == thisDerefType || objDerefType == nullptr || thisDerefType == nullptr) {
        return;
    }
    // `ThisType` must be sub type or equal to object's type, in fact, they should be same,
    // but original object may be casted to parent type for func param type matched.
    // after function inlining, object may be changed to sub type of `ThisType`, so we need to update `ThisType`
    auto parentDef = objDerefType->GetClassDef();
    std::unordered_set<ClassDef*> allSuperDefs;
    GetAllSuperClassDefs(*thisDerefType->GetClassDef(), allSuperDefs);
    auto it = allSuperDefs.find(parentDef);
    if (it == allSuperDefs.end()) {
        SetThisType(objType);
    }
}

void Invoke::ReplaceOperand(Value* oldOperand, Value* newOperand)
{
    auto needUpdateThisType = false;
    if (GetObject() == oldOperand) {
        needUpdateThisType = true;
    }
    Expression::ReplaceOperand(oldOperand, newOperand);
    if (needUpdateThisType) {
        UpdateThisType();
    }
}

void Invoke::ReplaceOperand(size_t idx, Value* newOperand)
{
    Expression::ReplaceOperand(idx, newOperand);
    if (idx == 0) {
        UpdateThisType();
    }
}

std::string Invoke::ToString([[maybe_unused]] size_t indent) const
{
    std::stringstream ss;
    ss << GetExprKindName();
    ss << "(";
    ss << ThisTypeToString(thisType).AddDelimiterOrNot(", ").Str();
    ss << GetMethodName();
    ss << InstTypeArgsToString(instantiatedTypeArgs) << ": ";
    ss << GetMethodType()->ToString() << ", ";
    ss << ExprOperandsToString(GetArgs());
    ss << ")";
    ss << CommentToString();
    return ss.str();
}

std::vector<Value*> Invoke::GetArgs() const
{
    return operands;
}

InvokeStatic::InvokeStatic(const InvokeCallContext& callContext, Block* parent)
    : DynamicDispatch(ExprKind::INVOKESTATIC, callContext, parent)
{
    CJC_NULLPTR_CHECK(parent);
    AppendOperand(*callContext.caller);
    for (auto op : callContext.funcCallCtx.args) {
        AppendOperand(*op);
    }
}

Value* InvokeStatic::GetRTTIValue() const
{
    return operands[0];
}

std::string InvokeStatic::ToString([[maybe_unused]] size_t indent) const
{
    std::stringstream ss;
    ss << GetExprKindName();
    ss << "(";
    ss << ThisTypeToString(thisType).AddDelimiterOrNot(", ").Str();
    ss << GetMethodName();
    ss << InstTypeArgsToString(instantiatedTypeArgs) << ": ";
    ss << GetMethodType()->ToString() << ", ";
    ss << GetRTTIValue()->GetIdentifier();
    ss << StringWrapper(", ").AppendOrClear(ExprOperandsToString(GetArgs())).Str();
    ss << ")";
    ss << CommentToString();
    return ss.str();
}

std::vector<Value*> InvokeStatic::GetArgs() const
{
    return {operands.begin() + 1, operands.end()};
}

InvokeStatic* InvokeStatic::Clone(CHIRBuilder& builder, Block& parent) const
{
    auto invokeInfo = InvokeCallContext {
        .caller = GetRTTIValue(),
        .funcCallCtx = FuncCallContext {
            .args = GetArgs(),
            .instTypeArgs = instantiatedTypeArgs,
            .thisType = thisType
        },
        .virMethodCtx = virMethodCtx
    };
    auto newNode = builder.CreateExpression<InvokeStatic>(result->GetType(), invokeInfo, &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

// TypeCast
TypeCast::TypeCast(Value* operand, Block* parent) : Expression(ExprKind::TYPECAST, {operand}, {}, parent)
{
}

TypeCast::TypeCast(Value* operand, Cangjie::OverflowStrategy overflow, Block* parent)
    : Expression(ExprKind::TYPECAST, {operand}, {}, parent), overflowStrategy(overflow)
{
}

Value* TypeCast::GetSourceValue() const
{
    return operands[0];
}

Type* TypeCast::GetSourceTy() const
{
    return operands[0]->GetType();
}

Type* TypeCast::GetTargetTy() const
{
    return result->GetType();
}

Cangjie::OverflowStrategy TypeCast::GetOverflowStrategy() const
{
    return overflowStrategy;
}

std::string TypeCast::ToString(size_t indent) const
{
    std::stringstream ss;
    ss << Expression::ToString(indent);
    ss << AddExtraComment(OverflowToString(overflowStrategy));
    return ss.str();
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

std::string InstanceOf::ToString([[maybe_unused]] size_t indent) const
{
    std::stringstream ss;
    ss << GetExprKindName();
    ss << "(" << GetObject()->GetIdentifier() << ", " << GetType()->ToString() << ")";
    ss << CommentToString();
    return ss.str();
}

// Box
Box::Box(Value* operand, Block* parent)
    : Expression(ExprKind::BOX, {operand}, {}, parent)
{
}

Value* Box::GetSourceValue() const
{
    return operands[0];
}

Type* Box::GetSourceTy() const
{
    return GetSourceValue()->GetType();
}

Type* Box::GetTargetTy() const
{
    return result->GetType();
}

Box* Box::Clone(CHIRBuilder& builder, Block& parent) const
{
    auto newNode = builder.CreateExpression<Box>(result->GetType(), GetSourceValue(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

// UnBox
UnBox::UnBox(Value* operand, Block* parent)
    : Expression(ExprKind::UNBOX, {operand}, {}, parent)
{
}

Value* UnBox::GetSourceValue() const
{
    return operands[0];
}

Type* UnBox::GetSourceTy() const
{
    return GetSourceValue()->GetType();
}

Type* UnBox::GetTargetTy() const
{
    return result->GetType();
}

UnBox* UnBox::Clone(CHIRBuilder& builder, Block& parent) const
{
    auto newNode = builder.CreateExpression<UnBox>(result->GetType(), GetSourceValue(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

// TransformToGeneric
TransformToGeneric::TransformToGeneric(Value* operand, Block* parent)
    : Expression(ExprKind::TRANSFORM_TO_GENERIC, {operand}, {}, parent)
{
}

Value* TransformToGeneric::GetSourceValue() const
{
    return operands[0];
}

Type* TransformToGeneric::GetSourceTy() const
{
    return GetSourceValue()->GetType();
}

Type* TransformToGeneric::GetTargetTy() const
{
    return result->GetType();
}

TransformToGeneric* TransformToGeneric::Clone(CHIRBuilder& builder, Block& parent) const
{
    auto newNode = builder.CreateExpression<TransformToGeneric>(result->GetType(), GetSourceValue(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

// TransformToConcrete
TransformToConcrete::TransformToConcrete(Value* operand, Block* parent)
    : Expression(ExprKind::TRANSFORM_TO_CONCRETE, {operand}, {}, parent)
{
}

Value* TransformToConcrete::GetSourceValue() const
{
    return operands[0];
}

Type* TransformToConcrete::GetSourceTy() const
{
    return GetSourceValue()->GetType();
}

Type* TransformToConcrete::GetTargetTy() const
{
    return result->GetType();
}

TransformToConcrete* TransformToConcrete::Clone(CHIRBuilder& builder, Block& parent) const
{
    auto newNode = builder.CreateExpression<TransformToConcrete>(result->GetType(), GetSourceValue(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

// UnBoxToRef
UnBoxToRef::UnBoxToRef(Value* operand, Block* parent)
    : Expression(ExprKind::UNBOX_TO_REF, {operand}, {}, parent)
{
}

Value* UnBoxToRef::GetSourceValue() const
{
    return operands[0];
}

Type* UnBoxToRef::GetSourceTy() const
{
    return GetSourceValue()->GetType();
}

Type* UnBoxToRef::GetTargetTy() const
{
    return result->GetType();
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

std::string Field::ToString([[maybe_unused]] size_t indent) const
{
    std::stringstream ss;
    ss << GetExprKindName();
    ss << "(";
    ss << GetBase()->GetIdentifier();
    for (auto p : GetPath()) {
        ss << ", " << p;
    }
    ss << ")";
    ss << CommentToString();
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

std::string FieldByName::ToString([[maybe_unused]] size_t indent) const
{
    std::stringstream ss;
    ss << GetExprKindName();
    ss << "(";
    ss << GetBase()->GetIdentifier();
    for (const auto& p : GetNames()) {
        ss << ", " << p;
    }
    ss << ")";
    ss << CommentToString();
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
RawArrayAllocate::RawArrayAllocate(Type* eleTy, Value* size, Block* parent)
    : Expression(ExprKind::RAW_ARRAY_ALLOCATE, {size}, {}, parent), elementType(eleTy)
{
}

Value* RawArrayAllocate::GetSize() const
{
    CJC_ASSERT(!operands.empty());
    return operands[0];
}

Type* RawArrayAllocate::GetElementType() const
{
    return elementType;
}

std::string RawArrayAllocate::ToString([[maybe_unused]] size_t indent) const
{
    std::stringstream ss;
    ss << GetExprKindName();
    ss << "(";
    ss << elementType->ToString() << ", ";
    ss << GetSize()->GetIdentifier();
    ss << ")";
    ss << CommentToString();
    return ss.str();
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

std::vector<Value*> RawArrayLiteralInit::GetElements() const
{
    CJC_ASSERT(operands.size() > 0);
    return {operands.begin() + 1, operands.end()};
}

// Intrinsic
Intrinsic::Intrinsic(const IntrisicCallContext& callContext, Block* parent)
    : Expression(ExprKind::INTRINSIC, callContext.args, {}, parent),
    intrinsicKind(callContext.kind),
    instantiatedTypeArgs(callContext.instTypeArgs)
{
}

IntrinsicKind Intrinsic::GetIntrinsicKind() const
{
    return intrinsicKind;
}

const std::vector<Type*>& Intrinsic::GetInstantiatedTypeArgs() const
{
    return instantiatedTypeArgs;
}

const std::vector<Value*>& Intrinsic::GetArgs() const
{
    return operands;
}

std::string Intrinsic::ToString([[maybe_unused]] size_t indent) const
{
    std::stringstream ss;
    ss << GetExprKindName() << "/" << INTRINSIC_KIND_TO_STRING_MAP.at(intrinsicKind);
    ss << InstTypeArgsToString(instantiatedTypeArgs);
    ss << "(";
    ss << ExprOperandsToString(operands);
    ss << ")";
    ss << CommentToString();
    return ss.str();
}

// If
If::If(Value* cond, BlockGroup* thenBody, BlockGroup* elseBody, Block* parent)
    : Expression(ExprKind::IF, {cond}, {thenBody, elseBody}, parent)
{
}

Value* If::GetCondition() const
{
    return operands[0];
}

BlockGroup* If::GetTrueBranch() const
{
    return blockGroups[0];
}

BlockGroup* If::GetFalseBranch() const
{
    return blockGroups[1];
}

std::string If::ToString([[maybe_unused]] size_t indent) const
{
    std::stringstream ss;
    ss << GetExprKindName();
    ss << "(" << operands[0]->GetIdentifier() << ", " << std::endl;
    ss << GetBlockGroupStr(*blockGroups[0], indent + 1);
    ss << "," << std::endl;
    ss << GetBlockGroupStr(*blockGroups[1], indent + 1);
    ss << ")";
    ss << CommentToString();
    return ss.str();
}

// Loop
Loop::Loop(BlockGroup* loopBody, Block* parent) : Expression(ExprKind::LOOP, {}, {loopBody}, parent)
{
}

BlockGroup* Loop::GetLoopBody() const
{
    return blockGroups[0];
}

std::string Loop::ToString([[maybe_unused]] size_t indent) const
{
    std::stringstream ss;
    ss << GetExprKindName() << "(" << std::endl;
    ss << GetBlockGroupStr(*blockGroups[0], indent + 1);
    ss << ")";
    ss << CommentToString();
    return ss.str();
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

std::string ForIn::ToString([[maybe_unused]] size_t indent) const
{
    std::stringstream ss;
    ss << GetExprKindName();
    ss << "(";
    ss << GetInductionVar()->GetIdentifier();
    ss << ", " << GetLoopCondVar()->GetIdentifier() << "," << std::endl;
    ss << GetBlockGroupStr(*GetBody(), indent + 1);
    ss << "," << std::endl;
    ss << GetBlockGroupStr(*GetLatch(), indent + 1);
    ss << "," << std::endl;
    ss << GetBlockGroupStr(*GetCond(), indent + 1);
    ss << ")";
    ss << CommentToString();
    return ss.str();
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

std::string Lambda::ToString([[maybe_unused]] size_t indent) const
{
    return GetLambdaStr(*this, indent);
}

std::vector<Value*> Lambda::GetCapturedVariables() const
{
    std::vector<Value*> envs;
    auto preVisit = [&envs, this](Expression& e) {
        for (auto op : e.GetOperands()) {
            bool isEnv = false;
            if (op->IsLocalVar()) {
                auto localVar = static_cast<LocalVar*>(op);
                if (localVar != GetResult() && localVar->GetOwnerBlockGroup() != GetBody()) {
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
        return VisitResult::CONTINUE;
    };
    Visitor::Visit(*GetBody(), preVisit, [](Expression&) { return VisitResult::CONTINUE; });
    return envs;
}

// Debug
Debug::Debug(Value* local, std::string srcCodeIdentifier, Block* parent)
    : Expression(ExprKind::DEBUGEXPR, {local}, parent), srcCodeIdentifier(srcCodeIdentifier)
{
    CJC_NULLPTR_CHECK(parent);
}

std::string Debug::ToString([[maybe_unused]] size_t indent) const
{
    std::stringstream ss;
    ss << GetExprKindName();
    ss << "(";
    ss << GetValue()->GetIdentifier() << ", ";
    ss << GetSrcCodeIdentifier();
    ss << ")";
    ss << CommentToString();
    return ss.str();
}

Spawn::Spawn(Value* val, Block* parent)
    : Expression(ExprKind::SPAWN, {val}, {}, parent)
{
}

Spawn::Spawn(Value* val, Value* arg, Block* parent)
    : Expression(ExprKind::SPAWN, {val, arg}, {}, parent)
{
}

Value* Spawn::GetFuture() const
{
    CJC_ASSERT(!IsExecuteClosure());
    return operands[0];
}

Value* Spawn::GetSpawnArg() const
{
    if (operands.size() > 1) {
        return operands[1];
    }
    return nullptr;
}

Value* Spawn::GetClosure() const
{
    CJC_ASSERT(IsExecuteClosure());
    return operands[0];
}

FuncBase* Spawn::GetExecuteClosure() const
{
    return executeClosure;
}

bool Spawn::IsExecuteClosure() const
{
    return executeClosure != nullptr;
}

void Spawn::SetExecuteClosure(FuncBase& func)
{
    executeClosure = &func;
}

std::string Spawn::ToString([[maybe_unused]] size_t indent) const
{
    std::stringstream ss;
    ss << Expression::ToString(indent);
    if (IsExecuteClosure()) {
        ss << AddExtraComment("executeClosure: " + GetExecuteClosure()->GetIdentifier());
    }
    return ss.str();
}

UnaryExpression* UnaryExpression::Clone(CHIRBuilder& builder, Block& parent) const
{
    auto newNode = builder.CreateExpression<UnaryExpression>(
        result->GetType(), GetExprKind(), GetOperand(), GetOverflowStrategy(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

BinaryExpression* BinaryExpression::Clone(CHIRBuilder& builder, Block& parent) const
{
    auto newNode = builder.CreateExpression<BinaryExpression>(
        result->GetType(), GetExprKind(), GetLHSOperand(), GetRHSOperand(), GetOverflowStrategy(), &parent);
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
    auto newNode = builder.CreateExpression<GetElementRef>(result->GetType(), GetLocation(), GetPath(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

std::string StoreElementRef::ToString([[maybe_unused]] size_t indent) const
{
    std::stringstream ss;
    ss << GetExprKindName();
    ss << "(";
    ss << GetValue()->GetIdentifier() << ", ";
    ss << GetLocation()->GetIdentifier();
    for (auto p : GetPath()) {
        ss << ", " << p;
    }
    ss << ")";
    ss << CommentToString();
    return ss.str();
}

StoreElementRef* StoreElementRef::Clone(CHIRBuilder& builder, Block& parent) const
{
    auto newNode =
        builder.CreateExpression<StoreElementRef>(result->GetType(), GetValue(), GetLocation(), GetPath(), &parent);
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
        .caller = GetObject(),
        .funcCallCtx = FuncCallContext {
            .args = args,
            .instTypeArgs = instantiatedTypeArgs,
            .thisType = thisType
        },
        .virMethodCtx = virMethodCtx
    };
    auto newNode = builder.CreateExpression<Invoke>(result->GetType(), invokeInfo, &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

TypeCast* TypeCast::Clone(CHIRBuilder& builder, Block& parent) const
{
    auto newNode =
        builder.CreateExpression<TypeCast>(result->GetType(), GetSourceValue(), GetOverflowStrategy(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    // Need to keep the NeedCheckCast for TypeCast Clone
    newNode->Set<NeedCheckCast>(this->Get<NeedCheckCast>());
    newNode->GetResult()->Set<EnumCaseIndex>(result->Get<EnumCaseIndex>());
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
    auto newNode = builder.CreateExpression<Tuple>(result->GetType(), GetOperands(), &parent);
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
        builder.CreateExpression<RawArrayLiteralInit>(result->GetType(), GetRawArray(), GetElements(), &parent);
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
    auto newNode = builder.CreateExpression<VArray>(result->GetType(), GetOperands(), &parent);
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

If* If::Clone(CHIRBuilder& builder, Block& parent) const
{
    auto newNode =
        builder.CreateExpression<If>(result->GetType(), GetCondition(), GetTrueBranch(), GetFalseBranch(), &parent);
    parent.AppendExpression(newNode);
    newNode->GetResult()->AppendAttributeInfo(result->GetAttributeInfo());
    return newNode;
}

Loop* Loop::Clone(CHIRBuilder& builder, Block& parent) const
{
    BlockGroup* newBody = nullptr;
    if (parent.GetParentBlockGroup()->GetOwnerFunc()) {
        newBody = GetLoopBody()->Clone(builder, *parent.GetParentBlockGroup()->GetOwnerFunc());
    } else {
        auto parentLambda = StaticCast<Lambda*>(parent.GetParentBlockGroup()->GetOwnerExpression());
        newBody = GetLoopBody()->Clone(builder, *parentLambda);
    }
    auto newNode = builder.CreateExpression<Loop>(result->GetType(), newBody, &parent);
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

Spawn* Spawn::Clone(CHIRBuilder& builder, Block& parent) const
{
    Spawn* newNode = nullptr;
    auto arg = GetSpawnArg();
    if (arg != nullptr) {
        newNode = builder.CreateExpression<Spawn>(result->GetType(), GetFuture(), arg, &parent);
    } else {
        newNode = builder.CreateExpression<Spawn>(result->GetType(), GetFuture(), &parent);
    }
    if (executeClosure) {
        newNode->SetExecuteClosure(*executeClosure);
    }
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

std::string GetInstantiateValue::ToString([[maybe_unused]] size_t indent) const
{
    std::stringstream ss;
    ss << GetExprKindName();
    ss << "(";
    ss << GetGenericResult()->GetIdentifier();
    for (auto ty : GetInstantiateTypes()) {
        ss << ", " << ty->ToString();
    }
    ss << ")";
    ss << CommentToString();
    return ss.str();
}

void Lambda::InitBody(BlockGroup& newBody)
{
    CJC_ASSERT(body.body == nullptr);
    CJC_ASSERT(blockGroups.empty());
    // newBody -> lambda
    newBody.SetOwnerExpression(*this);

    // lambda -> newBody
    body.body = &newBody;
    blockGroups.emplace_back(&newBody);
}

FuncType* Lambda::GetFuncType() const
{
    return funcTy;
}

BlockGroup* Lambda::GetBody() const
{
    return body.GetBody();
}

void Lambda::RemoveBody()
{
    blockGroups.clear();
    body.RemoveBody();
}

Block* Lambda::GetEntryBlock() const
{
    return body.GetBody()->GetEntryBlock();
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
    body.AddParam(arg);
    arg.SetOwnerLambda(this);
}

size_t Lambda::GetNumOfParams() const
{
    return body.GetParams().size();
}

Parameter* Lambda::GetParam(size_t index) const
{
    return body.GetParam(index);
}

const std::vector<Parameter*>& Lambda::GetParams() const
{
    return body.GetParams();
}

const std::vector<GenericType*>& Lambda::GetGenericTypeParams() const
{
    return genericTypeParams;
}

void Lambda::SetReturnValue(LocalVar& ret)
{
    ret.SetRetValue(true);
    body.SetReturnValue(ret);
}

LocalVar* Lambda::GetReturnValue() const
{
    return body.GetReturnValue();
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
    if (auto blockGroup = body.GetBody()) {
        blockGroup->ClearBlockGroup();
        body.RemoveBody();
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

std::string GetRTTI::ToString([[maybe_unused]] size_t indent) const
{
    std::stringstream ss;
    ss << Expression::ToString();
    return ss.str();
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

std::string GetRTTIStatic::ToString([[maybe_unused]] size_t indent) const
{
    std::stringstream ss;
    ss << GetExprKindName() << "(" << GetRTTIType()->ToString() << ")";
    ss << CommentToString();
    return ss.str();
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
