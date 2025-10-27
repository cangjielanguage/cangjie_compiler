// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares all terminators of CHIR, including Terminator itself and its sub class
 */

#ifndef CANGJIE_CHIR_TERMINATOR_H
#define CANGJIE_CHIR_TERMINATOR_H

#include "cangjie/CHIR/Expression/Expression.h"

namespace Cangjie::CHIR {
class CHIRBuilder;
class ExprTypeConverter;

enum class SourceExpr : uint8_t {
    IF_EXPR,
    WHILE_EXPR,
    DO_WHILE_EXPR,
    MATCH_EXPR,
    IF_LET_OR_WHILE_LET,
    QUEST,
    BINARY,
    FOR_IN_EXPR,
    OTHER
};

/**
 * @brief Terminator class in CHIR.
 * Terminator is also an expression, but it can only be at the end of one block.
 */
class Terminator : public Expression {
    friend class Block;

public:
    // ===--------------------------------------------------------------------===//
    // Operand
    // ===--------------------------------------------------------------------===//
    std::vector<Value*> GetOperands() const override;

    Value* GetOperand(size_t idx) const override;

    size_t GetNumOfOperands() const override;

    // ===--------------------------------------------------------------------===//
    // Successor
    // ===--------------------------------------------------------------------===//
    size_t GetNumOfSuccessor() const;

    Block* GetSuccessor(size_t index) const;

    /**
     * @brief Replace `oldSuccessor` with `newSuccessor`
     *
     * @param oldSuccessor: one of the current successors
     * @param newSuccessor: new successor
     */
    void ReplaceSuccessor(Block& oldSuccessor, Block& newSuccessor);

    /**
     * @brief Replaced the successor of this terminator
     *
     * @param index: the index-th successor need to be replaced
     * @param newSuccessor: new successor
     */
    void ReplaceSuccessor(size_t index, Block& newSuccessor);

    const std::vector<Block*> GetSuccessors() const;

    // ===--------------------------------------------------------------------===//
    // Modify Self
    // ===--------------------------------------------------------------------===//
    void ReplaceWith(Expression& newTerminator) override;

    /**
     * @brief Break all connection with its parent and operands
     * that means you can not get its parent and operands any more, and you can not get this terminator by its parent
     * and its operands, too. But we don't free this terminator's memory.
     */
    void RemoveSelfFromBlock() override;

protected:
    explicit Terminator(
        ExprKind kind, const std::vector<Value*>& operands, const std::vector<Block*>& successors, Block* parent);
    ~Terminator() override = default;

    void AppendSuccessor(Block& block);

    /**
     * @brief Get first successor's index in operands.
     *
     * in another sense, return the num of operand(does not include successors).
     *
     * NOTE: because returned value is constant,
     * call this function only after Terminator's full initialization(operand and successor are properly set).
     */
    size_t GetFirstSuccessorIndex() const;

private:
    void LetSuccessorsRemoveCurBlock() const;
};

/**
 * @brief Jump from current block to another one
 */
class GoTo : public Terminator {
    friend class CHIRContext;
    friend class CHIRBuilder;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    Block* GetDestination() const;

private:
    explicit GoTo(Block* destBlock, Block* parent);
    ~GoTo() override = default;

    GoTo* Clone(CHIRBuilder& builder, Block& parent) const override;
};

/**
 * @brief `if`, `for-in`, `while`, `do-while` and `match` can be translated to `Branch`
 */
class Branch : public Terminator {
    friend class CHIRContext;
    friend class CHIRBuilder;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    /** @brief Get the condition of this Branch Expression */
    Value* GetCondition() const;

    /** @brief Get the true block of this Branch Expression */
    Block* GetTrueBlock() const;

    /** @brief Get the false block of this Branch Expression */
    Block* GetFalseBlock() const;

    /** @brief Set the source expr, mark where this branch is from */
    void SetSourceExpr(SourceExpr srcExpr);

    /** @brief Get the source expr */
    SourceExpr GetSourceExpr() const;

    // ===--------------------------------------------------------------------===//
    // Others
    // ===--------------------------------------------------------------------===//
    std::string ToString(size_t indent = 0) const override;

private:
    explicit Branch(Value* cond, Block* trueBlock, Block* falseBlock, Block* parent);
    ~Branch() override = default;

    SourceExpr sourceExpr{SourceExpr::OTHER};

    Branch* Clone(CHIRBuilder& builder, Block& parent) const override;
};

/**
 * @brief `match` can be translated to `MultiBranch` in O2
 */
class MultiBranch : public Terminator {
    friend class CHIRContext;
    friend class CHIRBuilder;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    /** @brief Get the condition of this MultiBranch Expression */
    Value* GetCondition() const;

    /** @brief Get the case values of this MultiBranch Expression */
    const std::vector<uint64_t>& GetCaseVals() const;

    /** @brief Get the case value by index of this MultiBranch Expression */
    uint64_t GetCaseValByIndex(size_t index) const;

    /** @brief Get the case block by index of this MultiBranch Expression */
    Block* GetCaseBlockByIndex(size_t index) const;

    /** @brief Get the default block of this MultiBranch Expression */
    Block* GetDefaultBlock() const;

    std::vector<Block*> GetNormalBlocks() const;

private:
    explicit MultiBranch(Value* cond, Block* defaultBlock, const std::vector<uint64_t>& vals,
        const std::vector<Block*>& succs, Block* parent);
    ~MultiBranch() override = default;

    std::string ToString(size_t indent = 0) const override;

    MultiBranch* Clone(CHIRBuilder& builder, Block& parent) const override;

    /**
     * @brief The specific case values used to match.
     * Note that default Block does not have the case val.
     */
    std::vector<uint64_t> caseVals;
};

/**
 * @brief Exit current function.
 */
class Exit : public Terminator {
    friend class CHIRContext;
    friend class CHIRBuilder;
private:
    explicit Exit(Block* parent);
    ~Exit() override = default;

    Exit* Clone(CHIRBuilder& builder, Block& parent) const override;
};

/**
 * @brief Throw an exception.
 */
class RaiseException : public Terminator {
    friend class CHIRContext;
    friend class CHIRBuilder;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    /** @brief Get the exception value of this RaiseException Expression */
    Value* GetExceptionValue() const;

    /**
     * @brief Get the exception block of this RaiseException Expression.
     *
     *  Return exception block if exist,
     *  nullptr,  otherwise.
     */
    Block* GetExceptionBlock() const;

private:
    explicit RaiseException(Value* value, Block* parent);
    explicit RaiseException(Value* value, Block* successor, Block* parent);
    ~RaiseException() override = default;

    RaiseException* Clone(CHIRBuilder& builder, Block& parent) const override;
};

class ExpressionWithException : public Terminator {
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    /**
     * @brief Retrieves the success block.
     *
     * @return The success block.
     */
    Block* GetSuccessBlock() const;

    /**
     * @brief Retrieves the error block.
     *
     * @return The error block.
     */
    Block* GetErrorBlock() const;

protected:
    explicit ExpressionWithException(ExprKind kind, Block* parent);
    explicit ExpressionWithException(
        ExprKind kind, const std::vector<Value*>& operands, const std::vector<Block*>& successors, Block* parent);
};

/**
 * @brief An expression for function call, including
 * ApplyWithException, InvokeWithException and InvokeStaticWithException
 */
class FuncCallWithException : public ExpressionWithException {
    friend class ExprTypeConverter;
    friend class CHIRSerializer;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    /**
     * @brief Retrieves a list of the application argument nodes.
     *
     * @return A vector of pointers to the application argument nodes.
     */
    virtual std::vector<Value*> GetArgs() const = 0;

    /**
     * @brief Retrieves the type of 'this'.
     *
     * @return The type of 'this'.
     */
    Type* GetThisType() const;

    /**
     * @brief Sets the type of 'this'.
     *
     * @param type The type of 'this'.
     */
    void SetThisType(Type* type);

    /**
     * @brief Retrieves the instantiated argument types.
     *
     * @return A vector of pointers to the instantiated argument types.
     */
    const std::vector<Type*>& GetInstantiatedTypeArgs() const;

protected:
    explicit FuncCallWithException(ExprKind kind, const FuncCallContext& funcCallCtx, Block* parent);

    /**
     * @brief Record instantiated type args.
     * Cangjie Code:
     *      func foo<T1, T2>() {}
     *      foo<Bool, Int32>()  // {Bool, Int32} is current Apply's instantiated type args
     */
    std::vector<Type*> instantiatedTypeArgs;

    /**
     * @brief Record object type if callee is member method.
     * 1. foo(), global func, `thisType` is nullptr
     * 2. obj.foo(), instance member method, `thisType` is obj's type
     * 3. A<Bool>.foo(), static member method, `thisType` is A<Bool>
     * 4. method(), a member method with implicit `this`, `thisType` is parent custom type
     *
     * As for global func and instance member method, we can compute `thisType` from callee's args,
     * but for static member method, we can't.
     */
    Type* thisType{nullptr};
};

/**
 * @brief `Apply` expression wroten in `try` block
 */
class ApplyWithException : public FuncCallWithException {
    friend class ExprTypeConverter;
    friend class TypeConverterForCC;
    friend class CHIRSerializer;
    friend class CHIRContext;
    friend class CHIRBuilder;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    /**
     * @brief Retrieves the callee of this ApplyWithException operation.
     *
     * @return The callee of this ApplyWithException operation.
     */
    Value* GetCallee() const;

    /**
     * @brief Retrieves a list of the ApplyWithException operation argument nodes.
     *
     * @return A vector of pointers to the argument nodes.
     */
    std::vector<Value*> GetArgs() const override;

    // ===--------------------------------------------------------------------===//
    // Instantiated Types
    // ===--------------------------------------------------------------------===//
    /**
     * @brief Retrieves the instantiated parent custom type of the callee.
     *
     * @return The instantiated parent custom type of the callee.
     */
    Type* GetInstParentCustomTyOfCallee(CHIRBuilder& builder) const;

    // ===--------------------------------------------------------------------===//
    // Others
    // ===--------------------------------------------------------------------===//
    std::string ToString(size_t indent = 0) const override;

private:
    explicit ApplyWithException(
        Value* callee, const FuncCallContext& callContext, Block* sucBlock, Block* errBlock, Block* parent);
    ~ApplyWithException() override = default;

    ApplyWithException* Clone(CHIRBuilder& builder, Block& parent) const override;
};

/**
 * @brief An expression for virtual function call, including Invoke and InvokeStatic
 */
class DynamicDispatchWithException : public FuncCallWithException {
    friend class ExprTypeConverter;
    friend class PrivateTypeConverterNoInvokeOriginal;
    friend class CHIRSerializer;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    /**
     * @brief Retrieves the method name of this Invoke operation.
     *
     * @return The method name of this Invoke operation.
     */
    const std::string& GetMethodName() const;

    /**
     * @brief Retrieves the method type of this Invoke operation.
     *
     * @return The method type of this Invoke operation.
     */
    FuncType* GetMethodType() const;

    const std::vector<GenericType*>& GetGenericTypeParams() const;
    /**
     * @brief Retrieves the method's offset in vtable.
     *
     * @return The offset, greater than or equal to zero.
     */
    size_t GetVirtualMethodOffset(CHIRBuilder* builder = nullptr) const;

    // ===--------------------------------------------------------------------===//
    // Instantiated Types
    // ===--------------------------------------------------------------------===//
    /**
     * @brief Retrieves base class's instantiated type, base class is the one in which method first appears.
     *
     * @return Base class's instantiated type.
     */
    ClassType* GetInstSrcParentCustomTypeOfMethod(CHIRBuilder& builder) const;

    // ===--------------------------------------------------------------------===//
    // Others
    // ===--------------------------------------------------------------------===//
    /*
     * @brief Retrieves virtual method's attribute.
     *
     * @return Virtual method's attribute.
     */
    AttributeInfo GetVirtualMethodAttr(CHIRBuilder& builder) const;

protected:
    explicit DynamicDispatchWithException(
        ExprKind kind, const InvokeCallContext& callContext, Block* sucBlock, Block* errBlock, Block* parent);

    VirMethodContext virMethodCtx;

private:
    std::vector<VTableSearchRes> GetVirtualMethodInfo(CHIRBuilder& builder) const;
};

/**
 * @brief `Invoke` expression wroten in `try` block
 */
class InvokeWithException : public DynamicDispatchWithException {
    friend class ExprTypeConverter;
    friend class TypeConverterForCC;
    friend class PrivateTypeConverterNoInvokeOriginal;
    friend class CHIRSerializer;
    friend class CHIRContext;
    friend class CHIRBuilder;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    /**
     * @brief Retrieves the object of this InvokeWithException operation.
     *
     * @return The object of this InvokeWithException operation.
     */
    Value* GetObject() const;

    /**
     * @brief Retrieves the call arguments of this InvokeWithException operation.
     *
     * @return A vector of pointers to the call arguments.
     */
    std::vector<Value*> GetArgs() const override;

    // ===--------------------------------------------------------------------===//
    // Others
    // ===--------------------------------------------------------------------===//
    std::string ToString(size_t indent = 0) const override;

private:
    explicit InvokeWithException(
        const InvokeCallContext& callContext, Block* sucBlock, Block* errBlock, Block* parent);

    ~InvokeWithException() override = default;

    InvokeWithException* Clone(CHIRBuilder& builder, Block& parent) const override;
};

/**
 * @brief `InvokeStatic` expression wroten in `try` block
 */
class InvokeStaticWithException : public DynamicDispatchWithException {
    friend class ExprTypeConverter;
    friend class TypeConverterForCC;
    friend class PrivateTypeConverterNoInvokeOriginal;
    friend class CHIRSerializer;
    friend class CHIRContext;
    friend class CHIRBuilder;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    /**
     * @brief Retrieves the RTTI value.
     *
     * @return The RTTI value.
     */
    Value* GetRTTIValue() const;

    /**
     * @brief Retrieves the call arguments of this InvokeStaticWithException operation.
     *
     * @return A vector of pointers to the call arguments.
     */
    std::vector<Value*> GetArgs() const override;

    // ===--------------------------------------------------------------------===//
    // Others
    // ===--------------------------------------------------------------------===//
    std::string ToString(size_t indent = 0) const override;

private:
    explicit InvokeStaticWithException(
        const InvokeCallContext& callContext, Block* sucBlock, Block* errBlock, Block* parent);

    ~InvokeStaticWithException() override = default;

    InvokeStaticWithException* Clone(CHIRBuilder& builder, Block& parent) const override;
};

/**
 * @brief `Unary` or `Binary` expression wroten in `try` block
 */
class IntOpWithException : public ExpressionWithException {
    friend class CHIRDeserializer;
    friend class CHIRContext;
    friend class CHIRBuilder;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    /**
     * @brief Retrieves the operation kind.
     *
     * @return The operation kind.
     */
    ExprKind GetOpKind() const;

    /**
     * @brief Retrieves the operation kind name.
     *
     * @return The operation kind name.
     */
    std::string GetOpKindName() const;

    /**
     * @brief Retrieves the left-hand side operand.
     *
     * @return The left-hand side operand.
     */
    Value* GetLHSOperand() const;

    /**
     * @brief Retrieves the right-hand side operand.
     *
     * @return The right-hand side operand.
     */
    Value* GetRHSOperand() const;

    /**
     * @brief Retrieves the overflow strategy.
     *
     * @return The overflow strategy.
     */
    OverflowStrategy GetOverflowStrategy() const;

    // ===--------------------------------------------------------------------===//
    // Others
    // ===--------------------------------------------------------------------===//
    std::string ToString(size_t indent = 0) const override;

private:
    explicit IntOpWithException(ExprKind unaryKind, Value* operand, Block* normal, Block* exception, Block* parent);
    explicit IntOpWithException(
        ExprKind binaryKind, Value* lhs, Value* rhs, Block* normal, Block* exception, Block* parent);
    ~IntOpWithException() override = default;

    IntOpWithException* Clone(CHIRBuilder& builder, Block& parent) const override;

    ExprKind opKind; // Operator Kind
    Cangjie::OverflowStrategy overflowStrategy{Cangjie::OverflowStrategy::NA};
};

/**
 * @brief `TypeCast` expression wroten in `try` block
 */
class TypeCastWithException : public ExpressionWithException {
    friend class CHIRContext;
    friend class CHIRBuilder;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    /**
     * @brief Retrieves the overflow strategy.
     *
     * @return The overflow strategy.
     */
    OverflowStrategy GetOverflowStrategy() const;

    /**
     * @brief Retrieves the source value of this cast operation.
     *
     * @return The source value of this cast operation.
     */
    Value* GetSourceValue() const;

    /**
     * @brief Retrieves the source type of this cast operation.
     *
     * @return The source type of this cast operation.
     */
    Type* GetSourceTy() const;

    /**
     * @brief Retrieves the target type of this cast operation.
     *
     * @return The target type of this cast operation.
     */
    Type* GetTargetTy() const;

    // ===--------------------------------------------------------------------===//
    // Others
    // ===--------------------------------------------------------------------===//
    std::string ToString(size_t indent = 0) const override;

private:
    explicit TypeCastWithException(Value* operand, Block* normal, Block* exception, Block* parent);
    ~TypeCastWithException() override = default;

    TypeCastWithException* Clone(CHIRBuilder& builder, Block& parent) const override;
};

/**
 * @brief `Intrinsic` expression wroten in `try` block
 */
class IntrinsicWithException : public ExpressionWithException {
    friend class ExprTypeConverter;
    friend class TypeConverterForCC;
    friend class CHIRContext;
    friend class CHIRBuilder;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    /**
     * @brief Retrieves the intrinsic kind.
     *
     * @return The intrinsic kind.
     */
    CHIR::IntrinsicKind GetIntrinsicKind() const;

    /**
     * @brief Retrieves the generic type information.
     *
     * @return A vector of pointers to the generic types.
     */
    const std::vector<Type*>& GetInstantiatedTypeArgs() const;

    /**
     * @brief Retrieves the arguments of the intrinsic operation.
     *
     * @return A vector of pointers to the arguments.
     */
    const std::vector<Value*> GetArgs() const;

    // ===--------------------------------------------------------------------===//
    // Others
    // ===--------------------------------------------------------------------===//
    std::string ToString(size_t indent = 0) const override;

private:
    explicit IntrinsicWithException(
        const IntrisicCallContext& callContext, Block* normal, Block* exception, Block* parent);
    ~IntrinsicWithException() override = default;

    IntrinsicWithException* Clone(CHIRBuilder& builder, Block& parent) const override;

    CHIR::IntrinsicKind intrinsicKind;
    std::vector<Type*> instantiatedTypeArgs;
};

/**
 * @brief `Allocate` expression wroten in `try` block
 */
class AllocateWithException : public ExpressionWithException {
    friend class ExprTypeConverter;
    friend class CHIRContext;
    friend class CHIRBuilder;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    Type* GetType() const;

    // ===--------------------------------------------------------------------===//
    // Others
    // ===--------------------------------------------------------------------===//
    std::string ToString(size_t indent = 0) const override;

private:
    explicit AllocateWithException(Type* ty, Block* normal, Block* exception, Block* parent);
    ~AllocateWithException() override = default;

    AllocateWithException* Clone(CHIRBuilder& builder, Block& parent) const override;

    /** @brief The type to be allocated.
     */
    Type* ty;
};

/**
 * @brief `RawArrayAllocate` expression wroten in `try` block
 */
class RawArrayAllocateWithException : public ExpressionWithException {
    friend class ExprTypeConverter;
    friend class TypeConverterForCC;
    friend class CHIRContext;
    friend class CHIRBuilder;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    Value* GetSize() const;

    Type* GetElementType() const;

    // ===--------------------------------------------------------------------===//
    // Others
    // ===--------------------------------------------------------------------===//
    std::string ToString(size_t indent = 0) const override;

private:
    explicit RawArrayAllocateWithException(Type* eleTy, Value* size, Block* normal, Block* exception, Block* parent);
    ~RawArrayAllocateWithException() override = default;

    RawArrayAllocateWithException* Clone(CHIRBuilder& builder, Block& parent) const override;

    Type* elementType; // The element type.
};

/**
 * @brief `Spawn` expression wroten in `try` block
 */
class SpawnWithException : public ExpressionWithException {
    friend class CHIRContext;
    friend class CHIRBuilder;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    /**
     * @brief Get the spawn argument.
     *
     * @return nullptr if no argument.
     */
    Value* GetSpawnArg() const;

    bool IsExecuteClosure() const;
    void SetExecuteClosure(FuncBase& func);

    // ===--------------------------------------------------------------------===//
    // Before Optimization
    // ===--------------------------------------------------------------------===//
    /** @brief Get the future argument for execute.*/
    Value* GetFuture() const;
    
    // ===--------------------------------------------------------------------===//
    // After Optimization
    // ===--------------------------------------------------------------------===//
    /** @brief Get the closure argument for execute closure.*/
    Value* GetClosure() const;

    /**
     * @brief Get the FuncBase* of execute closure.
     *
     * @return nullptr if not exist.
     */
    FuncBase* GetExecuteClosure() const;

    // ===--------------------------------------------------------------------===//
    // Others
    // ===--------------------------------------------------------------------===//
    std::string ToString(size_t indent = 0) const override;

private:
    explicit SpawnWithException(
        Value* val, Value* arg, Block* normal, Block* exception, Block* parent);
    explicit SpawnWithException(
        Value* val, Block* normal, Block* exception, Block* parent);
    ~SpawnWithException() override = default;

    SpawnWithException* Clone(CHIRBuilder& builder, Block& parent) const override;

    /**
     * @brief After optimization, backend will use `executeClosure` to create new thread, not `Future` object.
     * `executeClosure` is member method in class `Future` which is declared in std.core
     */
    FuncBase* executeClosure{nullptr};
};
} // namespace Cangjie::CHIR
#endif // CANGJIE_CHIR_EXPRESSION_H
