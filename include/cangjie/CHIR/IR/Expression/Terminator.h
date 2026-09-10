// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares CHIR terminator expressions (block-ending expressions).
 */

#ifndef CANGJIE_CHIR_TERMINATOR_H
#define CANGJIE_CHIR_TERMINATOR_H

#include "cangjie/CHIR/IR/Expression/Expression.h"

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
 * @brief Jump from current block to another one
 */
class GoTo : public Expression {
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
class Branch : public Expression {
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

protected:
    std::string AddExtraComment() const override;

private:
    explicit Branch(Value* cond, Block* trueBlock, Block* falseBlock, Block* parent);
    ~Branch() override = default;

    SourceExpr sourceExpr{SourceExpr::OTHER};

    Branch* Clone(CHIRBuilder& builder, Block& parent) const override;
};

/**
 * @brief `match` can be translated to `MultiBranch` in O2
 */
class MultiBranch : public Expression {
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

protected:
    std::string OperandsToString() const override;

private:
    explicit MultiBranch(Value* cond, Block* defaultBlock, const std::vector<uint64_t>& vals,
        const std::vector<Block*>& succs, Block* parent);
    ~MultiBranch() override = default;

    MultiBranch* Clone(CHIRBuilder& builder, Block& parent) const override;

    /**
     * @brief The specific case values used to match.
     * Note that default Block does not have the case val.
     */
    std::vector<uint64_t> caseVals;
};

/**
 * @brief A nested local memory scope carried as a terminator on its parent block.
 *
 * The body is a separate BlockGroup. During FlattenExclave the sub-blocks are inlined into the
 * parent control flow (the Exclave is replaced by a GoTo into its entry block) and a
 * StartRegion/EndRegion pair is inserted to mark the scope at runtime. It does not exit the
 * surrounding function.
 */
class Exclave : public Expression {
    friend class CHIRContext;
    friend class CHIRBuilder;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    /** @brief Get the body of this Exclave Expression */
    BlockGroup* GetBody() const;

    void InitBody(BlockGroup& newBody);

private:
    explicit Exclave(Block* parent);
    ~Exclave() override = default;

    Exclave* Clone(CHIRBuilder& builder, Block& parent) const override;
};

/**
 * @brief Exit current function.
 */
class Exit : public Expression {
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
class RaiseException : public Expression {
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

/**
 * @brief `Apply` expression written in `try` block
 */
class TryApply : public ApplyBase {
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

private:
    explicit TryApply(
        Value* callee, const FuncCallContext& callContext, Block* sucBlock, Block* errBlock, Block* parent);
    ~TryApply() override = default;

    TryApply* Clone(CHIRBuilder& builder, Block& parent) const override;
};

/**
 * @brief `Invoke` expression written in `try` block
 */
class TryInvoke : public InvokeBase {
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

private:
    explicit TryInvoke(const InvokeCallContext& callContext, Block* sucBlock, Block* errBlock, Block* parent);

    ~TryInvoke() override = default;

    TryInvoke* Clone(CHIRBuilder& builder, Block& parent) const override;
};

/**
 * @brief `InvokeStatic` expression written in `try` block
 */
class TryInvokeStatic : public InvokeStaticBase {
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

private:
    explicit TryInvokeStatic(const InvokeCallContext& callContext, Block* sucBlock, Block* errBlock, Block* parent);

    ~TryInvokeStatic() override = default;

    TryInvokeStatic* Clone(CHIRBuilder& builder, Block& parent) const override;
};

/**
 * @brief `Unary` expression written in `try` block (Neg only).
 */
class TryUnaryExpression : public UnaryExpressionBase {
    friend class CHIRContext;
    friend class CHIRBuilder;
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

private:
    explicit TryUnaryExpression(
        UnaryExprKind unaryKind, Value* operand, Block* normal, Block* exception, Block* parent);
    ~TryUnaryExpression() override = default;

    TryUnaryExpression* Clone(CHIRBuilder& builder, Block& parent) const override;
};

/**
 * @brief `Binary` expression written in `try` block.
 */
class TryBinaryExpression : public BinaryExpressionBase {
    friend class CHIRContext;
    friend class CHIRBuilder;
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

private:
    explicit TryBinaryExpression(BinaryExprKind binaryKind, Value* lhs, Value* rhs, OverflowStrategy ofs,
        Block* normal, Block* exception, Block* parent);
    ~TryBinaryExpression() override = default;

    TryBinaryExpression* Clone(CHIRBuilder& builder, Block& parent) const override;
};

/**
 * @brief `NumericCast` expression written in `try` block
 */
class TryNumericCast : public NumericCastBase {
    friend class CHIRContext;
    friend class CHIRBuilder;
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

private:
    explicit TryNumericCast(Value* operand, Block* normal, Block* exception, Block* parent);
    ~TryNumericCast() override = default;

    TryNumericCast* Clone(CHIRBuilder& builder, Block& parent) const override;
};

/**
 * @brief `Intrinsic` expression wroten in `try` block
 */
class TryIntrinsic : public IntrinsicBase {
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

private:
    explicit TryIntrinsic(const IntrisicCallContext& callContext, Block* normal, Block* exception, Block* parent);
    ~TryIntrinsic() override = default;

    TryIntrinsic* Clone(CHIRBuilder& builder, Block& parent) const override;
};

/**
 * @brief `Allocate` expression written in `try` block
 */
class TryAllocate : public AllocateBase {
    friend class CHIRContext;
    friend class CHIRBuilder;
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

private:
    explicit TryAllocate(Type* ty, Block* normal, Block* exception, Block* parent);
    ~TryAllocate() override = default;

    TryAllocate* Clone(CHIRBuilder& builder, Block& parent) const override;
};

/**
 * @brief `RawArrayAllocate` expression written in `try` block
 */
class TryRawArrayAllocate : public RawArrayAllocateBase {
    friend class CHIRContext;
    friend class CHIRBuilder;
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

private:
    explicit TryRawArrayAllocate(Type* eleTy, Value* size, Block* normal, Block* exception, Block* parent);
    ~TryRawArrayAllocate() override = default;

    TryRawArrayAllocate* Clone(CHIRBuilder& builder, Block& parent) const override;
};

/**
 * @brief `Spawn` expression written in `try` block
 */
class TrySpawn : public SpawnBase {
    friend class CHIRContext;
    friend class CHIRBuilder;
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

private:
    explicit TrySpawn(Value* val, Value* arg, Block* normal, Block* exception, Block* parent);
    explicit TrySpawn(Value* val, Block* normal, Block* exception, Block* parent);
    ~TrySpawn() override = default;

    TrySpawn* Clone(CHIRBuilder& builder, Block& parent) const override;
};
} // namespace Cangjie::CHIR
#endif // CANGJIE_CHIR_EXPRESSION_H
