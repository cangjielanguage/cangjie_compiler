// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * Many Expressions have their xxxWithException version, we wrap them with a new class instead of using a base class
 */

#ifndef CANGJIE_CHIR_EXPRESSION_WRAPPER_H
#define CANGJIE_CHIR_EXPRESSION_WRAPPER_H

#include "cangjie/CHIR/IR/Expression/Terminator.h"

namespace Cangjie {
namespace CHIR {

class ExpressionBase {
public:
    const Expression* GetRawExpr() const;
    LocalVar* GetResult() const;
    std::vector<Value*> GetOperands() const;

protected:
    explicit ExpressionBase(const Expression* e);

private:
    const Expression* expr;
};

class FuncCallBase : public ExpressionBase {
public:
    explicit FuncCallBase(const Expression* e);
    explicit FuncCallBase(const FuncCall* expr);
    explicit FuncCallBase(const FuncCallWithException* exprE);

    std::vector<Value*> GetArgs() const;
    Type* GetThisType() const;
    std::vector<Type*> GetInstantiatedTypeArgs() const;

private:
    const FuncCall* expr;
    const FuncCallWithException* exprE;
};

class ApplyBase : public FuncCallBase {
public:
    explicit ApplyBase(const Expression* e);
    explicit ApplyBase(const Apply* expr);
    explicit ApplyBase(const ApplyWithException* exprE);

    Value* GetCallee() const;
    Type* GetInstParentCustomTyOfCallee(CHIRBuilder& builder) const;

private:
    const Apply* expr;
    const ApplyWithException* exprE;
};

class DynamicDispatchBase : public FuncCallBase {
public:
    explicit DynamicDispatchBase(const Expression* e);
    explicit DynamicDispatchBase(const DynamicDispatch* expr);
    explicit DynamicDispatchBase(const DynamicDispatchWithException* exprE);

    std::vector<GenericType*> GetGenericTypeParams() const;
    std::string GetMethodName() const;
    FuncType* GetMethodType() const;
    size_t GetVirtualMethodOffset() const;
    ClassType* GetInstSrcParentCustomTypeOfMethod(CHIRBuilder& builder) const;

private:
    const DynamicDispatch* expr;
    const DynamicDispatchWithException* exprE;
};

class InvokeBase : public DynamicDispatchBase {
public:
    explicit InvokeBase(const Expression* e);
    explicit InvokeBase(const Invoke* expr);
    explicit InvokeBase(const InvokeWithException* exprE);

    Value* GetObject() const;

private:
    const Invoke* expr;
    const InvokeWithException* exprE;
};

class InvokeStaticBase : public DynamicDispatchBase {
public:
    explicit InvokeStaticBase(const Expression* e);
    explicit InvokeStaticBase(const InvokeStatic* expr);
    explicit InvokeStaticBase(const InvokeStaticWithException* exprE);

    Value* GetRTTIValue() const;

private:
    const InvokeStatic* expr;
    const InvokeStaticWithException* exprE;
};

class UnaryExprBase : public ExpressionBase {
public:
    explicit UnaryExprBase(const Expression* e);
    explicit UnaryExprBase(const UnaryExpression* expr);
    explicit UnaryExprBase(const IntOpWithException* exprE);

    ExprKind GetOpKind() const;
    std::string GetExprKindName() const;
    Value* GetOperand() const;
    OverflowStrategy GetOverflowStrategy() const;

private:
    const UnaryExpression* expr;
    const IntOpWithException* exprE;
};

class BinaryExprBase : public ExpressionBase {
public:
    explicit BinaryExprBase(const Expression* e);
    explicit BinaryExprBase(const BinaryExpression* expr);
    explicit BinaryExprBase(const IntOpWithException* exprE);

    ExprKind GetOpKind() const;
    std::string GetExprKindName() const;
    Value* GetLHSOperand() const;
    Value* GetRHSOperand() const;
    OverflowStrategy GetOverflowStrategy() const;
    
private:
    const BinaryExpression* expr;
    const IntOpWithException* exprE;
};

class SpawnBase : public ExpressionBase {
public:
    explicit SpawnBase(const Expression* e);
    explicit SpawnBase(const Spawn* expr);
    explicit SpawnBase(const SpawnWithException* exprE);

    Value* GetObject() const;
    bool IsExecuteClosure() const;
private:
    const Spawn* expr;
    const SpawnWithException* exprE;
};

class IntrinsicBase : public ExpressionBase {
public:
    explicit IntrinsicBase(const Expression* e);
    explicit IntrinsicBase(const Intrinsic* expr);
    explicit IntrinsicBase(const IntrinsicWithException* exprE);

    IntrinsicKind GetIntrinsicKind() const;
    std::vector<Type*> GetInstantiatedTypeArgs() const;
    
private:
    const Intrinsic* expr;
    const IntrinsicWithException* exprE;
};

class AllocateBase : public ExpressionBase {
public:
    explicit AllocateBase(const Expression* e);
    explicit AllocateBase(const Allocate* expr);
    explicit AllocateBase(const AllocateWithException* exprE);

    Type* GetType() const;
private:
    const Allocate* expr;
    const AllocateWithException* exprE;
};

class RawArrayAllocateBase : public ExpressionBase {
public:
    explicit RawArrayAllocateBase(const Expression* e);
    explicit RawArrayAllocateBase(const RawArrayAllocate* expr);
    explicit RawArrayAllocateBase(const RawArrayAllocateWithException* exprE);

    Type* GetElementType() const;
    Value* GetSize() const;
private:
    const RawArrayAllocate* expr;
    const RawArrayAllocateWithException* exprE;
};
}
}

#endif