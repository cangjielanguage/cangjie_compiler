// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Expression/ExpressionWrapper.h"

#include "cangjie/CHIR/CHIRCasting.h"
#include "cangjie/Utils/CastingTemplate.h"

using namespace Cangjie::CHIR;

ExpressionBase::ExpressionBase(const Expression* e) : expr(e)
{
    CJC_NULLPTR_CHECK(e);
}

const Expression* ExpressionBase::GetRawExpr() const
{
    return expr;
}

LocalVar* ExpressionBase::GetResult() const
{
    return expr->GetResult();
}

std::vector<Value*> ExpressionBase::GetOperands() const
{
    return expr->GetOperands();
}

FuncCallBase::FuncCallBase(const Expression* e) : ExpressionBase(e)
{
    CJC_NULLPTR_CHECK(e);
    if (auto funcCall = Cangjie::DynamicCast<const FuncCall*>(e)) {
        expr = funcCall;
        exprE = nullptr;
    } else {
        expr = nullptr;
        exprE = Cangjie::StaticCast<const FuncCallWithException*>(e);
    }
}

FuncCallBase::FuncCallBase(const FuncCall* expr) : ExpressionBase(expr), expr(expr), exprE(nullptr)
{
    CJC_NULLPTR_CHECK(expr);
}

FuncCallBase::FuncCallBase(const FuncCallWithException* exprE) : ExpressionBase(exprE), expr(nullptr), exprE(exprE)
{
    CJC_NULLPTR_CHECK(exprE);
}

std::vector<Value*> FuncCallBase::GetArgs() const
{
    return expr ? expr->GetArgs()
                : exprE->GetArgs();
}

Type* FuncCallBase::GetThisType() const
{
    return expr ? expr->GetThisType()
                : exprE->GetThisType();
}

std::vector<Type*> FuncCallBase::GetInstantiatedTypeArgs() const
{
    return expr ? expr->GetInstantiatedTypeArgs()
                : exprE->GetInstantiatedTypeArgs();
}

ApplyBase::ApplyBase(const Expression* e) : FuncCallBase(e)
{
    CJC_NULLPTR_CHECK(e);
    if (e->GetExprKind() == ExprKind::APPLY) {
        expr = Cangjie::StaticCast<const Apply*>(e);
        exprE = nullptr;
    } else {
        expr = nullptr;
        exprE = Cangjie::StaticCast<const ApplyWithException*>(e);
    }
}

ApplyBase::ApplyBase(const Apply* expr) : FuncCallBase(expr), expr(expr), exprE(nullptr)
{
    CJC_NULLPTR_CHECK(expr);
}

ApplyBase::ApplyBase(const ApplyWithException* exprE) : FuncCallBase(exprE), expr(nullptr), exprE(exprE)
{
    CJC_NULLPTR_CHECK(exprE);
}

Value* ApplyBase::GetCallee() const
{
    return expr ? expr->GetCallee()
                : exprE->GetCallee();
}

Type* ApplyBase::GetInstParentCustomTyOfCallee(CHIRBuilder& builder) const
{
    return expr ? expr->GetInstParentCustomTyOfCallee(builder)
                : exprE->GetInstParentCustomTyOfCallee(builder);
}

DynamicDispatchBase::DynamicDispatchBase(const Expression* e) : FuncCallBase(e)
{
    CJC_NULLPTR_CHECK(e);
    if (auto funcCall = Cangjie::DynamicCast<const DynamicDispatch*>(e)) {
        expr = funcCall;
        exprE = nullptr;
    } else {
        expr = nullptr;
        exprE = Cangjie::StaticCast<const DynamicDispatchWithException*>(e);
    }
}

DynamicDispatchBase::DynamicDispatchBase(const DynamicDispatch* expr) : FuncCallBase(expr), expr(expr), exprE(nullptr)
{
    CJC_NULLPTR_CHECK(expr);
}

DynamicDispatchBase::DynamicDispatchBase(const DynamicDispatchWithException* exprE)
    : FuncCallBase(exprE), expr(nullptr), exprE(exprE)
{
    CJC_NULLPTR_CHECK(exprE);
}

std::vector<GenericType*> DynamicDispatchBase::GetGenericTypeParams() const
{
    return expr ? expr->GetGenericTypeParams()
                : exprE->GetGenericTypeParams();
}

std::string DynamicDispatchBase::GetMethodName() const
{
    return expr ? expr->GetMethodName()
                : exprE->GetMethodName();
}

FuncType* DynamicDispatchBase::GetMethodType() const
{
    return expr ? expr->GetMethodType()
                : exprE->GetMethodType();
}

size_t DynamicDispatchBase::GetVirtualMethodOffset() const
{
    return expr ? expr->GetVirtualMethodOffset()
                : exprE->GetVirtualMethodOffset();
}

ClassType* DynamicDispatchBase::GetInstSrcParentCustomTypeOfMethod(CHIRBuilder& builder) const
{
    return expr ? expr->GetInstSrcParentCustomTypeOfMethod(builder)
                : exprE->GetInstSrcParentCustomTypeOfMethod(builder);
}

InvokeBase::InvokeBase(const Expression* e) : DynamicDispatchBase(e)
{
    CJC_NULLPTR_CHECK(e);
    if (e->GetExprKind() == ExprKind::INVOKE) {
        expr = Cangjie::StaticCast<const Invoke*>(e);
        exprE = nullptr;
    } else {
        expr = nullptr;
        exprE = Cangjie::StaticCast<const InvokeWithException*>(e);
    }
}

InvokeBase::InvokeBase(const Invoke* expr) : DynamicDispatchBase(expr), expr(expr), exprE(nullptr)
{
    CJC_NULLPTR_CHECK(expr);
}

InvokeBase::InvokeBase(const InvokeWithException* exprE) : DynamicDispatchBase(exprE), expr(nullptr), exprE(exprE)
{
    CJC_NULLPTR_CHECK(exprE);
}

Value* InvokeBase::GetObject() const
{
    return expr ? expr->GetObject()
                : exprE->GetObject();
}

InvokeStaticBase::InvokeStaticBase(const Expression* e) : DynamicDispatchBase(e)
{
    CJC_NULLPTR_CHECK(e);
    if (e->GetExprKind() == ExprKind::INVOKESTATIC) {
        expr = Cangjie::StaticCast<const InvokeStatic*>(e);
        exprE = nullptr;
    } else {
        expr = nullptr;
        exprE = Cangjie::StaticCast<const InvokeStaticWithException*>(e);
    }
}

InvokeStaticBase::InvokeStaticBase(const InvokeStatic* expr) : DynamicDispatchBase(expr), expr(expr), exprE(nullptr)
{
    CJC_NULLPTR_CHECK(expr);
}

InvokeStaticBase::InvokeStaticBase(const InvokeStaticWithException* exprE)
    : DynamicDispatchBase(exprE), expr(nullptr), exprE(exprE)
{
    CJC_NULLPTR_CHECK(exprE);
}

Value* InvokeStaticBase::GetRTTIValue() const
{
    return expr ? expr->GetRTTIValue()
                : exprE->GetRTTIValue();
}

UnaryExprBase::UnaryExprBase(const Expression* e) : ExpressionBase(e)
{
    CJC_NULLPTR_CHECK(e);
    if (e->GetExprMajorKind() == ExprMajorKind::UNARY_EXPR) {
        expr = Cangjie::StaticCast<const UnaryExpression*>(e);
        exprE = nullptr;
    } else {
        expr = nullptr;
        exprE = Cangjie::StaticCast<const IntOpWithException*>(e);
    }
}

UnaryExprBase::UnaryExprBase(const UnaryExpression* expr) : ExpressionBase(expr), expr(expr), exprE(nullptr)
{
    CJC_NULLPTR_CHECK(expr);
}

UnaryExprBase::UnaryExprBase(const IntOpWithException* exprE) : ExpressionBase(exprE), expr(nullptr), exprE(exprE)
{
    CJC_NULLPTR_CHECK(exprE);
}

ExprKind UnaryExprBase::GetOpKind() const
{
    return expr ? expr->GetExprKind()
                : exprE->GetOpKind();
}

std::string UnaryExprBase::GetExprKindName() const
{
    return expr ? expr->GetExprKindName()
                : exprE->GetOpKindName();
}

Value* UnaryExprBase::GetOperand() const
{
    return expr ? expr->GetOperand()
                : exprE->GetLHSOperand();
}

Cangjie::OverflowStrategy UnaryExprBase::GetOverflowStrategy() const
{
    return expr ? expr->GetOverflowStrategy()
                : exprE->GetOverflowStrategy();
}

BinaryExprBase::BinaryExprBase(const Expression* e) : ExpressionBase(e)
{
    CJC_NULLPTR_CHECK(e);
    if (e->GetExprMajorKind() == ExprMajorKind::BINARY_EXPR) {
        expr = Cangjie::StaticCast<const BinaryExpression*>(e);
        exprE = nullptr;
    } else {
        expr = nullptr;
        exprE = Cangjie::StaticCast<const IntOpWithException*>(e);
    }
}

BinaryExprBase::BinaryExprBase(const BinaryExpression* expr) : ExpressionBase(expr), expr(expr), exprE(nullptr)
{
    CJC_NULLPTR_CHECK(expr);
}

BinaryExprBase::BinaryExprBase(const IntOpWithException* exprE) : ExpressionBase(exprE), expr(nullptr), exprE(exprE)
{
    CJC_NULLPTR_CHECK(exprE);
}

ExprKind BinaryExprBase::GetOpKind() const
{
    return expr ? expr->GetExprKind()
                : exprE->GetOpKind();
}

std::string BinaryExprBase::GetExprKindName() const
{
    return expr ? expr->GetExprKindName()
                : exprE->GetOpKindName();
}

Value* BinaryExprBase::GetLHSOperand() const
{
    return expr ? expr->GetLHSOperand()
                : exprE->GetLHSOperand();
}

Value* BinaryExprBase::GetRHSOperand() const
{
    return expr ? expr->GetRHSOperand()
                : exprE->GetRHSOperand();
}

Cangjie::OverflowStrategy BinaryExprBase::GetOverflowStrategy() const
{
    return expr ? expr->GetOverflowStrategy()
                : exprE->GetOverflowStrategy();
}

SpawnBase::SpawnBase(const Expression* e) : ExpressionBase(e)
{
    CJC_NULLPTR_CHECK(e);
    if (e->GetExprKind() == ExprKind::SPAWN) {
        expr = Cangjie::StaticCast<const Spawn*>(e);
        exprE = nullptr;
    } else {
        expr = nullptr;
        exprE = Cangjie::StaticCast<const SpawnWithException*>(e);
    }
}

SpawnBase::SpawnBase(const Spawn* expr) : ExpressionBase(expr), expr(expr), exprE(nullptr)
{
    CJC_NULLPTR_CHECK(expr);
}

SpawnBase::SpawnBase(const SpawnWithException* exprE) : ExpressionBase(exprE), expr(nullptr), exprE(exprE)
{
    CJC_NULLPTR_CHECK(exprE);
}

Value* SpawnBase::GetObject() const
{
    return expr ? expr->GetOperands()[0]
                : exprE->GetOperands()[0];
}

bool SpawnBase::IsExecuteClosure() const
{
    return expr ? expr->IsExecuteClosure()
                : exprE->IsExecuteClosure();
}

IntrinsicBase::IntrinsicBase(const Expression* e) : ExpressionBase(e)
{
    CJC_NULLPTR_CHECK(e);
    if (e->GetExprKind() == ExprKind::INTRINSIC) {
        expr = Cangjie::StaticCast<const Intrinsic*>(e);
        exprE = nullptr;
    } else {
        expr = nullptr;
        exprE = Cangjie::StaticCast<const IntrinsicWithException*>(e);
    }
}

IntrinsicBase::IntrinsicBase(const Intrinsic* expr) : ExpressionBase(expr), expr(expr), exprE(nullptr)
{
    CJC_NULLPTR_CHECK(expr);
}

IntrinsicBase::IntrinsicBase(const IntrinsicWithException* exprE) : ExpressionBase(exprE), expr(nullptr), exprE(exprE)
{
    CJC_NULLPTR_CHECK(exprE);
}

IntrinsicKind IntrinsicBase::GetIntrinsicKind() const
{
    return expr ? expr->GetIntrinsicKind()
                : exprE->GetIntrinsicKind();
}

std::vector<Type*> IntrinsicBase::GetInstantiatedTypeArgs() const
{
    return expr ? expr->GetInstantiatedTypeArgs()
                : exprE->GetInstantiatedTypeArgs();
}

AllocateBase::AllocateBase(const Expression* e) : ExpressionBase(e)
{
    CJC_NULLPTR_CHECK(e);
    if (e->GetExprKind() == ExprKind::ALLOCATE) {
        expr = Cangjie::StaticCast<const Allocate*>(e);
        exprE = nullptr;
    } else {
        expr = nullptr;
        exprE = Cangjie::StaticCast<const AllocateWithException*>(e);
    }
}

AllocateBase::AllocateBase(const Allocate* expr) : ExpressionBase(expr), expr(expr), exprE(nullptr)
{
    CJC_NULLPTR_CHECK(expr);
}

AllocateBase::AllocateBase(const AllocateWithException* exprE) : ExpressionBase(exprE), expr(nullptr), exprE(exprE)
{
    CJC_NULLPTR_CHECK(exprE);
}

Type* AllocateBase::GetType() const
{
    return expr ? expr->GetType()
                : exprE->GetType();
}

RawArrayAllocateBase::RawArrayAllocateBase(const Expression* e) : ExpressionBase(e)
{
    CJC_NULLPTR_CHECK(e);
    if (e->GetExprKind() == ExprKind::RAW_ARRAY_ALLOCATE) {
        expr = Cangjie::StaticCast<const RawArrayAllocate*>(e);
        exprE = nullptr;
    } else {
        expr = nullptr;
        exprE = Cangjie::StaticCast<const RawArrayAllocateWithException*>(e);
    }
}

RawArrayAllocateBase::RawArrayAllocateBase(const RawArrayAllocate* expr)
    : ExpressionBase(expr), expr(expr), exprE(nullptr)
{
    CJC_NULLPTR_CHECK(expr);
}

RawArrayAllocateBase::RawArrayAllocateBase(const RawArrayAllocateWithException* exprE)
    : ExpressionBase(exprE), expr(nullptr), exprE(exprE)
{
    CJC_NULLPTR_CHECK(exprE);
}

Type* RawArrayAllocateBase::GetElementType() const
{
    return expr ? expr->GetElementType()
                : exprE->GetElementType();
}

Value* RawArrayAllocateBase::GetSize() const
{
    return expr ? expr->GetSize()
                : exprE->GetSize();
}