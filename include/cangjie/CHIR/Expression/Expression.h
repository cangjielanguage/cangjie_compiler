// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares expressions of CHIR, but not including terminators
 */

#ifndef CANGJIE_CHIR_EXPRESSION_H
#define CANGJIE_CHIR_EXPRESSION_H

#include "cangjie/CHIR/IntrinsicKind.h"
#include "cangjie/CHIR/LiteralValue.h"
#include "cangjie/Utils/SafePointer.h"

#include <cstdarg>
#include <list>
#include <memory>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <vector>

namespace Cangjie::CHIR {
class CHIRBuilder;
class ExprTypeConverter;

/**
 * @brief Expression major kind.
 */
enum class ExprMajorKind : uint8_t {
#define EXPRKIND(...)
#define MAJORKIND(KIND, ...) KIND,
#include "cangjie/CHIR/ExprKind.inc"
#undef EXPRKIND
#undef MAJORKIND
};

/**
 * @brief Expression minor kind.
 */
enum class ExprKind : uint8_t {
    INVALID = 0,
#define EXPRKIND(KIND, ...) KIND,
#define MAJORKIND(_1, ...) __VA_ARGS__
#include "cangjie/CHIR/ExprKind.inc"
#undef EXPRKIND
#undef MAJORKIND
    MAX_EXPR_KINDS
};

class ExprKindMgr {
public:
    static const ExprKindMgr* Instance()
    {
        static ExprKindMgr ins;
        return &ins;
    }
    ExprMajorKind GetMajorKind(ExprKind exprKind) const
    {
        return expr2MajorExprMap.at(exprKind);
    }
    std::string GetKindName(size_t exprKind) const
    {
        return exprKindNames[exprKind];
    }

private:
    void InitMap(ExprMajorKind majorKind, ...)
    {
        va_list vaList;
        va_start(vaList, majorKind);
        ExprKind arg = va_arg(vaList, ExprKind);
        while (static_cast<size_t>(arg) != 0) {
            expr2MajorExprMap[arg] = majorKind;
            arg = va_arg(vaList, ExprKind);
        }
        va_end(vaList);
    }
    ExprKindMgr()
    {
#define EXPRKIND(KIND, ...) ExprKind::KIND,
#define EXPRKINDS(...) __VA_ARGS__
#define MAJORKIND(KIND, ...) InitMap(ExprMajorKind::KIND, EXPRKINDS(__VA_ARGS__) ExprKind::INVALID);
#include "cangjie/CHIR/ExprKind.inc"
#undef EXPRKIND
#undef EXPRKINDS
#undef MAJORKIND
    }

    const char* exprKindNames[static_cast<size_t>(Cangjie::CHIR::ExprKind::MAX_EXPR_KINDS)] = {
        "INVALID",
#define EXPRKIND(KIND, NAME, ...) NAME,
#define MAJORKIND(_1, ...) __VA_ARGS__
#include "cangjie/CHIR/ExprKind.inc"
#undef EXPRKIND
#undef MAJORKIND
    };
    std::unordered_map<ExprKind, ExprMajorKind> expr2MajorExprMap;
};

/**
 * @brief class `Expression` is a base class for all expression in CHIR
 */
class Expression : public Base {
    friend class CHIRContext;
    friend class CHIRBuilder;
    friend class Value;
    friend class LocalVar;
    friend class Block;
    friend class BlockGroup;
    friend class ExprTypeConverter;

public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    ExprKind GetExprKind() const;
    std::string GetExprKindName() const;
    ExprMajorKind GetExprMajorKind() const;

    bool IsApply() const;
    bool IsApplyWithException() const;
    bool IsBinaryExpr() const;
    bool IsConstant() const;
    bool IsConstantNull() const;
    bool IsConstantBool() const;
    bool IsConstantInt() const;
    bool IsConstantFloat() const;
    bool IsConstantString() const;
    bool IsDebug() const;
    bool IsDynamicDispatch() const;
    bool IsField() const;
    bool IsFuncCall() const;
    bool IsIntOpWithException() const;
    bool IsInvoke() const;
    bool IsInvokeStaticBase() const;
    bool IsLambda() const;
    bool IsLoad() const;
    bool IsTerminator() const;
    bool IsTypeCast() const;
    bool IsUnaryExpr() const;

    /**
     * @brief Retrieves all block groups.
     *
     * @return A vector of pointers to all block groups.
     */
    const std::vector<BlockGroup*>& GetBlockGroups() const;

    /**
     * @brief Retrieves the result of the expression.
     *
     * @return The result of the expression.
     */
    LocalVar* GetResult() const;

    /**
     * @brief Retrieves the result type of the expression,
     * if this expression is a terminator and not have result, then return nullptr.
     *
     * @return The result type or nullptr.
     */
    Type* GetResultType() const;

    // ===--------------------------------------------------------------------===//
    // Parent
    // ===--------------------------------------------------------------------===//
    /**
     * @brief Retrieves the parent block of the expression.
     *
     * @return The parent block of the expression.
     */
    Block* GetParentBlock() const;

    /**
     * @brief Retrieves the parent block group of the expression.
     *
     * @return The parent block group of the expression.
     */
    BlockGroup* GetParentBlockGroup() const;

    /**
     * @brief Retrieves the top-level function which this expression belongs to.
     *
     * @return The top-level func.
     */
    Func* GetTopLevelFunc() const;

    // ===--------------------------------------------------------------------===//
    // Operand
    // ===--------------------------------------------------------------------===//
    /**
     * @brief Retrieves the number of operands.
     *
     * @return The number of operands.
     */
    virtual size_t GetNumOfOperands() const;

    /**
     * @brief Retrieves an operand by index.
     *
     * @param idx The index of the operand to retrieve.
     * @return The operand at the specified index.
     */
    virtual Value* GetOperand(size_t idx) const;

    /**
     * @brief Retrieves all operands.
     *
     * @return A vector of pointers to all operands.
     */
    virtual std::vector<Value*> GetOperands() const;

    /**
     * @brief Replace old operand in specified position.
     *
     * @param idx The sepcified position.
     * @param newOperand The destination value.
     */
    virtual void ReplaceOperand(size_t idx, Value* newOperand);

    /**
     * @brief Replace old operand with the new one, if there are many old operands in same expression,
     * then replace them all
     *
     * @param oldOperand The source value.
     * @param newOperand The destination value.
     */
    virtual void ReplaceOperand(Value* oldOperand, Value* newOperand);

    // ===--------------------------------------------------------------------===//
    // Modify Self
    // ===--------------------------------------------------------------------===//
    /**
     * @brief Remove this expression from its parent block, and insert it into after the @expr expression.
     *
     * @param expr The destination position which is after this reference expression.
     */
    void MoveAfter(Expression* expr);

    /**
     * @brief Remove this expression from its parent block, and insert it into before the `expr` expression.
     *
     * @param expr The destination position which is before this reference expression.
     */
    void MoveBefore(Expression* expr);

    /**
     * @brief Move this expression from current parent block to the end of another one.
     *
     * @param block the new block which the expression belongs to
     */
    void MoveTo(Block& block);

    /**
     * @brief Break all connection with its parent block and operands
     *
     * that means you can not get its parent block and operands by this expression any more,
     * and you can not get this expression by its parent block and its operands, too.
     * But we don't free this expression's memory.
     */
    virtual void RemoveSelfFromBlock();

    /**
    * @brief Replaces the current expression with a new expression.
    *
    * @param newExpr The new expression to replace the current one.
    */
    virtual void ReplaceWith(Expression& newExpr);

    // ===--------------------------------------------------------------------===//
    // Others
    // ===--------------------------------------------------------------------===//
    /**
    * @brief Converts the expression to a string representation.
    *
    * @param indent The number of spaces to indent the string. Default is 0.
    * @return The string representation of the expression.
    */
    virtual std::string ToString(size_t indent = 0) const;

    /**
    * @brief Converts the expression to a string representation and print out.
    */
    void Dump() const;

    virtual Expression* Clone(CHIRBuilder& builder, Block& parent) const = 0;

protected:
    explicit Expression(
        ExprKind kind, const std::vector<Value*>& operands, const std::vector<BlockGroup*>& blockGroups, Block* parent);
    explicit Expression(ExprKind kind, const std::vector<Value*>& operands, Block* parent)
        : Expression(kind, operands, {}, parent)
    {
    }
    explicit Expression(const Expression& other, Block* parent)
        : Expression(other.kind, other.operands, other.blockGroups, parent)
    {
    }
    virtual ~Expression() = default;
    Expression& operator=(const Expression&) = delete;
    void AppendOperand(Value& op);
    void SetParent(Block* newParent);
    void EraseOperands();
    std::string CommentToString() const;
    std::string AddExtraComment(const std::string& comment) const;

    ExprKind kind;                        // Expression kind.
    std::vector<Value*> operands;         // The operands.
    std::vector<BlockGroup*> blockGroups; // The regions of special expression, such as Func.
    Block* parent;                        // The owner basicblock of this expression.
    LocalVar* result = nullptr;           // The result.
};

/**
 * @brief Unary expression, including Neg, Not and BitNot.
 *
 * Cangjie Code:
 *      var x = 1
 *      var y = -x // kind is Neg
 */
class UnaryExpression : public Expression {
    friend class CHIRContext;
    friend class CHIRBuilder;

public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    using Expression::GetOperand;
    Value* GetOperand() const;

    OverflowStrategy GetOverflowStrategy() const;

    // ===--------------------------------------------------------------------===//
    // Others
    // ===--------------------------------------------------------------------===//
    std::string ToString(size_t indent = 0) const override;

protected:
    ~UnaryExpression() override = default;

private:
    explicit UnaryExpression(ExprKind kind, Value* operand, Cangjie::OverflowStrategy ofs, Block* parent)
        : Expression(kind, {operand}, {}, parent), overflowStrategy(ofs)
    {
    }

    UnaryExpression* Clone(CHIRBuilder& builder, Block& parent) const override;

    Cangjie::OverflowStrategy overflowStrategy;
};

/**
 * @brief Binary expression, including:
 * 1. mathematical operator, such as `add`, `sub`, `mul`, `div`, `mod` and `exp`
 * 2. bit operator, such as `<<`, `>>`, `&`, `|` and `^`
 * 3. comparison operator, such as `<`, `>`, `<=`, `>=`, `==` and `!=`
 * 4. logical operator, such as `&&` and `||`
 *
 * Cangjie Code:
 *      var x = 1 + 2 // kind is Add, `1` is left-hand side op, `2` is right-hand side op
 */
class BinaryExpression : public Expression {
    friend class CHIRContext;
    friend class CHIRBuilder;

public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
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

    OverflowStrategy GetOverflowStrategy() const;

    // ===--------------------------------------------------------------------===//
    // Others
    // ===--------------------------------------------------------------------===//
    std::string ToString(size_t indent = 0) const override;

protected:
    ~BinaryExpression() override = default;

private:
    explicit BinaryExpression(ExprKind kind, Value* lhs, Value* rhs, OverflowStrategy ofs, Block* parent);
    explicit BinaryExpression(ExprKind kind, Value* lhs, Value* rhs, Block* parent);

    BinaryExpression* Clone(CHIRBuilder& builder, Block& parent) const override;

    Cangjie::OverflowStrategy overflowStrategy{Cangjie::OverflowStrategy::NA};
};

/**
 * @brief Constant expression in CHIR.
 * There are Bool, Rune, String, Int, Float, Unit literal in Cangjie
 *
 * Cangjie Code:
 *      var x = 1 // `x` is Int literal
 */
class Constant : public Expression {
    friend class CHIRContext;
    friend class CHIRBuilder;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    LiteralValue* GetValue() const;

    // ===--------------------------------------------------------------------===//
    // All kinds of literal
    // ===--------------------------------------------------------------------===//
    bool GetBoolLitVal() const;
    double GetFloatLitVal() const;
    char32_t GetRuneLitVal() const;
    int64_t GetSignedIntLitVal() const;
    std::string GetStringLitVal() const;
    uint64_t GetUnsignedIntLitVal() const;

    bool IsBoolLit() const;
    bool IsFloatLit() const;
    bool IsIntLit() const;
    bool IsJString() const;
    bool IsNullLit() const;
    bool IsRuneLit() const;
    bool IsSignedIntLit() const;
    bool IsUnSignedIntLit() const;
    bool IsStringLit() const;
    bool IsUnitLit() const;

    // ===--------------------------------------------------------------------===//
    // Others
    // ===--------------------------------------------------------------------===//
    std::string ToString(size_t indent = 0) const override;

protected:
    ~Constant() override = default;

private:
    explicit Constant(LiteralValue* val, Block* parent) : Expression(ExprKind::CONSTANT, {val}, parent)
    {
    }

    Constant* Clone(CHIRBuilder& builder, Block& parent) const override;
};

/**
 * @brief Allocate memory whit specific type
 *
 * Cangjie Code:
 *      var x = 1 // allocate 8 Bytes memory
 */
class Allocate : public Expression {
    friend class CHIRContext;
    friend class CHIRBuilder;
    friend class ExprTypeConverter;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    /**
     * @brief which type this `Allocate` allocates
     */
    Type* GetType() const;

    // ===--------------------------------------------------------------------===//
    // Others
    // ===--------------------------------------------------------------------===//
    std::string ToString(size_t indent = 0) const override;

protected:
    ~Allocate() override = default;

private:
    explicit Allocate(Type* ty, Block* parent) : Expression(ExprKind::ALLOCATE, {}, {}, parent), ty(ty)
    {
    }

    Allocate* Clone(CHIRBuilder& builder, Block& parent) const override;

    Type* ty; // The type to be allocated.
};

/**
 * @brief Load a value from reference
 *
 * Cangjie Code:
 *      var x = 1
 *      println(x)  // Load `x` first
 */
class Load : public Expression {
    friend class CHIRContext;
    friend class CHIRBuilder;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    /**
     * @brief which reference to be load
     *
     * @return a value with reference type
     */
    Value* GetLocation() const;

protected:
    ~Load() override = default;

private:
    explicit Load(Value* location, Block* parent) : Expression(ExprKind::LOAD, {location}, {}, parent)
    {
    }

    Load* Clone(CHIRBuilder& builder, Block& parent) const override;
};

/**
 * @brief Store a value to a reference.
 *
 * Cangjie Code:
 *      var x = 1  // store `1` to `x`, `1` is the source value, `x` is the location
 */
class Store : public Expression {
    friend class CHIRContext;
    friend class CHIRBuilder;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    /**
     * @brief Get source value
     */
    Value* GetValue() const;

    /**
     * @brief Get destination value
     */
    Value* GetLocation() const;

protected:
    ~Store() override = default;

private:
    explicit Store(Value* val, Value* location, Block* parent)
        : Expression(ExprKind::STORE, {val, location}, {}, parent)
    {
    }

    Store* Clone(CHIRBuilder& builder, Block& parent) const override;
};

/**
 * @brief Compute the reference to the corresponding child element data
 * from the data specified by `Location` base on `Path`.
 *
 * Cangjie Code:
 *      struct S { var a = 1 }
 *      var x = S().a  // `S()` is the location, the path is { 0 }
 *
 * we only get the reference, not take up the memory, unless this reference is loaded
 */
class GetElementRef : public Expression {
    friend class CHIRContext;
    friend class CHIRBuilder;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    /**
     * @brief which reference to be compute
     *
     * @return a value with reference type
     */
    Value* GetLocation() const;

    const std::vector<uint64_t>& GetPath() const;

    // ===--------------------------------------------------------------------===//
    // Others
    // ===--------------------------------------------------------------------===//
    std::string ToString(size_t indent = 0) const override;

protected:
    ~GetElementRef() override = default;

private:
    explicit GetElementRef(Value* location, const std::vector<uint64_t>& path, Block* parent)
        : Expression(ExprKind::GET_ELEMENT_REF, {location}, {}, parent), path(path)
    {
    }
    GetElementRef* Clone(CHIRBuilder& builder, Block& parent) const override;

    std::vector<uint64_t> path;
};

/**
 * @brief Compute the reference to the corresponding child element data
 * from the data specified by `Location` base on `Name`.
 *
 * Cangjie Code:
 *      struct S { var a = 1 }
 *      var x = S().a  // `S()` is the location, the Name is { "a" }
 *
 * we only get the reference, not take up the memory, unless this reference is loaded
 */
class GetElementByName : public Expression {
    friend class CHIRContext;
    friend class CHIRBuilder;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    /**
     * @brief which reference to be compute
     *
     * @return a value with reference type
     */
    Value* GetLocation() const;

    const std::vector<std::string>& GetNames() const;

    // ===--------------------------------------------------------------------===//
    // Others
    // ===--------------------------------------------------------------------===//
    std::string ToString(size_t indent = 0) const override;

protected:
    ~GetElementByName() override = default;

private:
    explicit GetElementByName(Value* location, const std::vector<std::string>& names, Block* parent)
        : Expression(ExprKind::GET_ELEMENT_BY_NAME, {location}, {}, parent), names(names)
    {
    }
    GetElementByName* Clone(CHIRBuilder& builder, Block& parent) const override;

    std::vector<std::string> names;
};

/**
 * @brief Store a value to the corresponding child element
 *
 * Cangjie Code:
 *      struct S { var a = 1 }
 *      var x = S()
 *      x.a = 2  // `2` is the source value, `x` is the location, the path is { 0 }
 */
class StoreElementRef : public Expression {
    friend class CHIRContext;
    friend class CHIRBuilder;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    /**
     * @brief Retrieves the source value.
     *
     * @return The value.
     */
    Value* GetValue() const;
    
    /**
     * @brief Retrieves the location.
     *
     * @return The location.
     */
    Value* GetLocation() const;

    /**
     * @brief Retrieves the path.
     *
     * @return A vector containing the path.
     */
    const std::vector<uint64_t>& GetPath() const;

    // ===--------------------------------------------------------------------===//
    // Others
    // ===--------------------------------------------------------------------===//
    std::string ToString(size_t indent = 0) const override;

protected:
    ~StoreElementRef() override = default;

private:
    explicit StoreElementRef(Value* value, Value* location, const std::vector<uint64_t>& path, Block* parent)
        : Expression(ExprKind::STORE_ELEMENT_REF, {value, location}, {}, parent), path(path)
    {
    }

    StoreElementRef* Clone(CHIRBuilder& builder, Block& parent) const override;
    std::vector<uint64_t> path;
};

/**
 * @brief Store a value to the corresponding child element
 *
 * Cangjie Code:
 *      struct S { var a = 1 }
 *      var x = S()
 *      x.a = 2  // `2` is the source value, `x` is the location, the name is { "a" }
 */
class StoreElementByName : public Expression {
    friend class CHIRContext;
    friend class CHIRBuilder;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    /**
     * @brief Retrieves the source value.
     *
     * @return The value.
     */
    Value* GetValue() const;
    
    /**
     * @brief Retrieves the location.
     *
     * @return The location.
     */
    Value* GetLocation() const;

    const std::vector<std::string>& GetNames() const;

    // ===--------------------------------------------------------------------===//
    // Others
    // ===--------------------------------------------------------------------===//
    std::string ToString(size_t indent = 0) const override;

protected:
    ~StoreElementByName() override = default;

private:
    explicit StoreElementByName(Value* value, Value* location, const std::vector<std::string>& names, Block* parent)
        : Expression(ExprKind::STORE_ELEMENT_BY_NAME, {value, location}, {}, parent), names(names)
    {
    }

    StoreElementByName* Clone(CHIRBuilder& builder, Block& parent) const override;
    std::vector<std::string> names;
};

/**
 * @brief Context for a function calling
 */
struct FuncCallContext {
    std::vector<Value*> args;         // function arguments
    std::vector<Type*> instTypeArgs;  // See `instantiatedTypeArgs` in class `FuncCall`
    Type* thisType{nullptr};          // See `thisType` in class `FuncCall`
};

/**
 * @brief Context for a virtual function
 */
struct VirMethodContext {
    std::string srcCodeIdentifier;       // function name
    FuncType* originalFuncType{nullptr}; // method signature type, from virtual method type in parent CustomTypeDef
    std::vector<GenericType*> genericTypeParams;
};

/**
 * @brief Context for a virtual function calling
 */
struct InvokeCallContext {
    Value* caller{nullptr};  // the object in Invoke, or the rtti in InvokeStatic
    FuncCallContext funcCallCtx;
    VirMethodContext virMethodCtx;
};

/**
 * @brief An expression for function call, including Apply, Invoke and InvokeStatic
 */
class FuncCall : public Expression {
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
    explicit FuncCall(ExprKind kind, const FuncCallContext& funcCallCtx, Block* parent);

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
 * @brief A non-virtual function call
 *
 * Cangjie Code:
 *      func foo() {}
 *      foo()  // `foo` is global function, not virtual function
 */
class Apply : public FuncCall {
    friend class ExprTypeConverter;
    friend class CHIRSerializer;
    friend class CHIRContext;
    friend class CHIRBuilder;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    /**
     * @brief Retrieves the callee of the application.
     *
     * @return The callee of the application.
     */
    Value* GetCallee() const;

    /**
     * @brief Retrieves a list of the application argument nodes.
     *
     * @return A vector of pointers to the application argument nodes.
     */
    std::vector<Value*> GetArgs() const override;

    /**
     * @brief Checks if the call is a super call.
     *
     * @return True if the call is a super call, false otherwise.
     */
    bool IsSuperCall() const;

    /**
     * @brief Mark this Apply is `super()`.
     */
    void SetSuperCall();

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

protected:
    ~Apply() override = default;

private:
    explicit Apply(Value* callee, const FuncCallContext& callContext, Block* parent);

    Apply* Clone(CHIRBuilder& builder, Block& parent) const override;

    /**
     * @brief Mark this Apply is `super()`.
     */
    bool isSuperCall{false};
};

/**
 * @brief An expression for virtual function call, including Invoke and InvokeStatic
 */
class DynamicDispatch : public FuncCall {
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

protected:
    explicit DynamicDispatch(ExprKind kind, const InvokeCallContext& callContext, Block* parent);

    VirMethodContext virMethodCtx;

private:
    std::vector<VTableSearchRes> GetVirtualMethodInfo(CHIRBuilder& builder) const;
};

/**
 * @brief An expression for virtual instance member method call
 *
 * Cangjie Code:
 *      interface I { func foo() {} }
 *      class A <: I { public func foo() {} }
 *      var x: I = A()
 *      x.foo() // `foo` is a virtual instance member method, which means `foo` can be overrided by class A
 */
class Invoke : public DynamicDispatch {
    friend class ExprTypeConverter;
    friend class PrivateTypeConverterNoInvokeOriginal;
    friend class CHIRSerializer;
    friend class CHIRContext;
    friend class CHIRBuilder;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    /**
     * @brief Replace old operand in specified position.
     *
     * @param idx The sepcified position.
     * @param newOperand The destination value.
     */
    void ReplaceOperand(size_t idx, Value* newOperand) final;

    /**
     * @brief Replace old operand with the new one, if there are many old operands in same expression,
     * then replace them all
     *
     * @param oldOperand The source value.
     * @param newOperand The destination value.
     */
    void ReplaceOperand(Value* oldOperand, Value* newOperand) final;
    
    /**
     * @brief Retrieves the object of this Invoke operation.
     * Object's type doesn't always equal to ThisType, maybe we use an object from sub type to call
     * a virtual func from super type, then in order to func param type matched,
     * this object need to be casted to super type, but ThisType is still the sub type.
     *
     * @return The object of this Invoke operation.
     */
    Value* GetObject() const;

    /**
     * @brief Retrieves the call arguments of this Invoke operation.
     *
     * @return A vector of pointers to the call arguments.
     */
    std::vector<Value*> GetArgs() const override;

    // ===--------------------------------------------------------------------===//
    // Others
    // ===--------------------------------------------------------------------===//
    std::string ToString(size_t indent = 0) const override;

protected:
    ~Invoke() override = default;

private:
    explicit Invoke(const InvokeCallContext& callContext, Block* parent);

    Invoke* Clone(CHIRBuilder& builder, Block& parent) const override;

    void UpdateThisType();
};

/**
 * @brief Get a value's runtime type info, operand's type is class& or This&
 *
 * Cangjie Code:
 *      interface I {
 *          func foo() {
 *              goo()  // `goo` may be overrided by sub class, we need to know `this`'s runtime type info
 *                     // so operand is `this`, added in func foo's param list by CHIR
 *          }
 *          static func goo() {}
 *      }
 */
class GetRTTI : public Expression {
    friend class CHIRContext;
    friend class CHIRBuilder;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    Value* GetOperand() const;
    using Expression::GetOperand;

    // ===--------------------------------------------------------------------===//
    // Others
    // ===--------------------------------------------------------------------===//
    std::string ToString(size_t indent = 0) const override;

private:
    GetRTTI(Value* val, Block* parent);
    ~GetRTTI() override = default;
    GetRTTI* Clone(CHIRBuilder& builder, Block& parent) const override;
};

/**
 * @brief Get a type's runtime type info
 *
 * Cangjie Code:
 *      interface I { static func goo() {} }
 *      class A <: I { static func goo() {} }
 *      func foo<T>() where T <: I {
 *          T.goo()  // Get `T`'s runtime type info
 *      }
 */
class GetRTTIStatic : public Expression {
    friend class ExprTypeConverter;
    friend class CHIRContext;
    friend class CHIRBuilder;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    Type* GetRTTIType() const;

    // ===--------------------------------------------------------------------===//
    // Others
    // ===--------------------------------------------------------------------===//
    std::string ToString(size_t indent = 0) const override;

private:
    GetRTTIStatic(Type* type, Block* parent);
    ~GetRTTIStatic() override = default;
    GetRTTIStatic* Clone(CHIRBuilder& builder, Block& parent) const override;

    Type* ty;
};

/**
 * @brief If a static member method calling can trigger dynamic dispatch, we will use `InvokeStatic` to describe
 *
 * Cangjie Code:
 *      interface I { static func goo() {} }
 *      class A <: I { static func goo() {} }
 *      func foo<T>() where T <: I {
 *          T.goo()  // we don't know which `goo` is called in runtime, I.goo or A.goo
 *      }
 */
class InvokeStatic : public DynamicDispatch {
    friend class ExprTypeConverter;
    friend class PrivateTypeConverterNoInvokeOriginal;
    friend class CHIRSerializer;
    friend class CHIRContext;
    friend class CHIRBuilder;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    /**
     * @brief Retrieves the call arguments of this Invoke operation.
     *
     * @return A vector of pointers to the call arguments.
     */
    std::vector<Value*> GetArgs() const override;

    /**
     * @brief Retrieves the RTTI value.
     *
     * @return The RTTI value.
     */
    Value* GetRTTIValue() const;

    // ===--------------------------------------------------------------------===//
    // Others
    // ===--------------------------------------------------------------------===//
    std::string ToString(size_t indent = 0) const override;

protected:
    ~InvokeStatic() override = default;

private:
    explicit InvokeStatic(const InvokeCallContext& callContext, Block* parent);

    InvokeStatic* Clone(CHIRBuilder& builder, Block& parent) const override;
};

/**
 * @brief Cast a src type to target type.
 *
 * Cangjie Code:
 *      var x: Int32 = 1
 *      var y: Int64 = Int64(x)  // x is Int32, cast to Int64
 */
class TypeCast : public Expression {
    friend class CHIRContext;
    friend class CHIRBuilder;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    /** @brief Get the source value of this operation */
    Value* GetSourceValue() const;

    /** @brief Get the source type of this operation */
    Type* GetSourceTy() const;

    /** @brief Get the target type of this operation */
    Type* GetTargetTy() const;

    /** @brief Get the overflow strategy of this cast operation */
    OverflowStrategy GetOverflowStrategy() const;

    // ===--------------------------------------------------------------------===//
    // Others
    // ===--------------------------------------------------------------------===//
    std::string ToString(size_t indent = 0) const override;

protected:
    ~TypeCast() override = default;

private:
    explicit TypeCast(Value* operand, Block* parent);
    explicit TypeCast(Value* operand, Cangjie::OverflowStrategy overflow, Block* parent);

    TypeCast* Clone(CHIRBuilder& builder, Block& parent) const override;

    Cangjie::OverflowStrategy overflowStrategy{Cangjie::OverflowStrategy::NA};
};

/**
 * @brief Judge whether an object is specific type, we will get a result in runtime.
 *
 * Cangjie Code:
 *      interface I {}
 *      class A <: I {}
 *      var x: I = A()
 *      println(x is A)  // judge whether `x`'s type is class `A` in runtime
 */
class InstanceOf : public Expression {
    friend class ExprTypeConverter;
    friend class CHIRContext;
    friend class CHIRBuilder;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    /** @brief Get the object to test against */
    Value* GetObject() const;

    /** @brief Get the tested type */
    Type* GetType() const;

    // ===--------------------------------------------------------------------===//
    // Others
    // ===--------------------------------------------------------------------===//
    std::string ToString(size_t indent = 0) const override;

private:
    explicit InstanceOf(Value* operand, Type* ty, Block* parent);
    ~InstanceOf() override = default;
    InstanceOf* Clone(CHIRBuilder& builder, Block& parent) const override;

    Type* ty;
};

/**
 * @brief Cast a value type to reference type.
 * value type includes:
 *      1. Int, UInt, Float, Rune, Bool, Unit, Nothing
 *      2. Enum, Struct
 *      3. Tuple, VArray, Func, CPointer, CString
 * reference type includes:
 *      1. Class&, Array&
 *      2. BoxType&
 *      3. This&
 */
class Box : public Expression {
    friend class CHIRContext;
    friend class CHIRBuilder;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    /** @brief Get the source value of this operation */
    Value* GetSourceValue() const;

    /** @brief Get the source type of this operation */
    Type* GetSourceTy() const;

    /** @brief Get the target type of this operation */
    Type* GetTargetTy() const;

private:
    explicit Box(Value* operand, Block* parent);
    ~Box() override = default;

    Box* Clone(CHIRBuilder& builder, Block& parent) const override;
};

/**
 * @brief Cast a reference type to value type.
 * value type includes:
 *      1. Int, UInt, Float, Rune, Bool, Unit, Nothing
 *      2. Enum, Struct
 *      3. Tuple, VArray, Func, CPointer, CString
 * reference type includes:
 *      1. Class&, Array&
 *      2. BoxType&
 *      3. This&
 */
class UnBox : public Expression {
    friend class CHIRContext;
    friend class CHIRBuilder;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    /** @brief Get the source value of this operation */
    Value* GetSourceValue() const;

    /** @brief Get the source type of this operation */
    Type* GetSourceTy() const;

    /** @brief Get the target type of this operation */
    Type* GetTargetTy() const;

private:
    explicit UnBox(Value* operand, Block* parent);
    ~UnBox() override = default;

    UnBox* Clone(CHIRBuilder& builder, Block& parent) const override;
};

/**
 * @brief Cast a concrete type to a generic related type.
 * 1. Bool -> T
 * 2. Class A<Bool>& -> Class A<T>&
 */
class TransformToGeneric : public Expression {
    friend class CHIRContext;
    friend class CHIRBuilder;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    /** @brief Get the source value of this operation */
    Value* GetSourceValue() const;

    /** @brief Get the source type of this operation */
    Type* GetSourceTy() const;

    /** @brief Get the target type of this operation */
    Type* GetTargetTy() const;

private:
    explicit TransformToGeneric(Value* operand, Block* parent);
    ~TransformToGeneric() override = default;

    TransformToGeneric* Clone(CHIRBuilder& builder, Block& parent) const override;
};

/**
 * @brief Cast a generic related type to a concrete type.
 * 1. T -> Bool
 * 2. Class A<T>& -> Class A<Bool>&
 */
class TransformToConcrete : public Expression {
    friend class CHIRContext;
    friend class CHIRBuilder;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    /** @brief Get the source value of this operation */
    Value* GetSourceValue() const;

    /** @brief Get the source type of this operation */
    Type* GetSourceTy() const;

    /** @brief Get the target type of this operation */
    Type* GetTargetTy() const;

private:
    explicit TransformToConcrete(Value* operand, Block* parent);
    ~TransformToConcrete() override = default;

    TransformToConcrete* Clone(CHIRBuilder& builder, Block& parent) const override;
};

/**
 * @brief Cast a reference type to a value ref type.
 * 1. Class I& -> Struct S&
 */
class UnBoxToRef : public Expression {
    friend class CHIRContext;
    friend class CHIRBuilder;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    /** @brief Get the source value of this operation */
    Value* GetSourceValue() const;

    /** @brief Get the source type of this operation */
    Type* GetSourceTy() const;

    /** @brief Get the target type of this operation */
    Type* GetTargetTy() const;

private:
    explicit UnBoxToRef(Value* operand, Block* parent);
    ~UnBoxToRef() override = default;

    UnBoxToRef* Clone(CHIRBuilder& builder, Block& parent) const override;
};

/**
 * @brief Build up some values to a tuple.
 *
 * Cangjie Code:
 *      var x = (1, 2, 3)  // generate a tuple to describe
 */
class Tuple : public Expression {
    friend class CHIRContext;
    friend class CHIRBuilder;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    /**
     * @brief Retrieves all element values in tuple.
     */
    std::vector<Value*> GetElementValues() const;

    /**
     * @brief Retrieves all element types in tuple.
     */
    std::vector<Type*> GetElementTypes() const;

private:
    explicit Tuple(const std::vector<Value*>& values, Block* parent);
    ~Tuple() override = default;

    Tuple* Clone(CHIRBuilder& builder, Block& parent) const override;
};

/**
 * @brief Compute child element value from specific `Base` and `Indices`.
 * `Field` is different from `GetElementRef`, `Base` in `Field` is value type,
 * but `Location` in `GetElementRef` is reference type
 *
 * Cangjie Code:
 *      var x = (1, 2, 3)
 *      println(x[0])  // use `Field` to get first element value, `Base` is `x`, `Indices` is {0}
 */
class Field : public Expression {
    friend class CHIRContext;
    friend class CHIRBuilder;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    Value* GetBase() const;

    std::vector<uint64_t> GetPath() const;

    // ===--------------------------------------------------------------------===//
    // Others
    // ===--------------------------------------------------------------------===//
    std::string ToString(size_t indent = 0) const override;

private:
    explicit Field(Value* val, const std::vector<uint64_t>& path, Block* parent);
    ~Field() override = default;

    Field* Clone(CHIRBuilder& builder, Block& parent) const override;

    std::vector<uint64_t> path;
};

/**
 * @brief Compute child element value from specific `Base` and `Name`.
 * `FieldByName` is different from `GetElementByName`, `Base` in `FieldByName` is value type,
 * but `Location` in `GetElementRef` is reference type
 *
 * Cangjie Code:
 *      struct S {
 *          let a = 1
 *      }
 *      let x = S()
 *      println(x.a)  // use `FieldByName` to get first element value, `Base` is `x`, `Name` is {"a"}
 */
class FieldByName : public Expression {
    friend class CHIRContext;
    friend class CHIRBuilder;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    Value* GetBase() const;

    const std::vector<std::string>& GetNames() const;

    // ===--------------------------------------------------------------------===//
    // Others
    // ===--------------------------------------------------------------------===//
    std::string ToString(size_t indent = 0) const override;

private:
    explicit FieldByName(Value* val, const std::vector<std::string>& names, Block* parent);
    ~FieldByName() override = default;

    FieldByName* Clone(CHIRBuilder& builder, Block& parent) const override;

    std::vector<std::string> names;
};

// ===--------------------------------------------------------------------===//
// RawArray is an internal type used in struct Array of std.core
// Its implementation of constructor and memory allocating is from CodeGen
// So CHIR need to use specific expressions to describe
// ===--------------------------------------------------------------------===//
/**
 * @brief Allocate a memory to store array element value,
 * this expression is an internal implementation to generate Array
 *
 *  Cangjie Code:
 *      var x: Array<Int32> = [1, 2, 3]  // use `RawArrayAllocate` to get a memory to store `1, 2, 3`
 *  CHIR:
 *      %0: Int64 = Constant(3)
 *      %1: RawArray<Int32>& = RawArrayAllocate(Int32, %0)
 *      %2: Int32 = Constant(1)
 *      %3: Int32 = Constant(2)
 *      %4: Int32 = Constant(3)
 *      %5: Unit = RawArrayLiteralInit(%1, %2, %3, %4)
 */
class RawArrayAllocate : public Expression {
    friend class ExprTypeConverter;
    friend class TypeConverterForCC;
    friend class CHIRContext;
    friend class CHIRBuilder;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    /**
     * @brief Retrieves element quantity in Array.
     */
    Value* GetSize() const;

    /**
     * @brief Retrieves element type in Array.
     */
    Type* GetElementType() const;

    // ===--------------------------------------------------------------------===//
    // Others
    // ===--------------------------------------------------------------------===//
    std::string ToString(size_t indent = 0) const override;

private:
    explicit RawArrayAllocate(Type* eleTy, Value* size, Block* parent);
    ~RawArrayAllocate() override = default;
    
    RawArrayAllocate* Clone(CHIRBuilder& builder, Block& parent) const override;

    Type* elementType; // The element type.
};

/**
 * @brief When Array is generated by `[]`, we will use `RawArrayLiteralInit` to initialize Array.
 *
 * Cangjie Code:
 *      var x: Array<Int32> = [1, 2, 3]  // use `RawArrayLiteralInit` to initialize `[1, 2, 3]`
 * CHIR:
 *      %0: Int64 = Constant(3)
 *      %1: RawArray<Int32>& = RawArrayAllocate(Int32, %0)
 *      %2: Int32 = Constant(1)
 *      %3: Int32 = Constant(2)
 *      %4: Int32 = Constant(3)
 *      %5: Unit = RawArrayLiteralInit(%1, %2, %3, %4)
 */
class RawArrayLiteralInit : public Expression {
    friend class CHIRContext;
    friend class CHIRBuilder;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    /**
     * @brief Retrieves address which allocated by `RawArrayAllocate`.
     */
    Value* GetRawArray() const;

    /**
     * @brief Retrieves element quantity in Array.
     */
    size_t GetSize() const;

    /**
     * @brief Retrieves all elements in Array.
     */
    std::vector<Value*> GetElements() const;

private:
    explicit RawArrayLiteralInit(const Ptr<Value> raw, std::vector<Value*> elements, Block* parent);
    ~RawArrayLiteralInit() override = default;

    RawArrayLiteralInit* Clone(CHIRBuilder& builder, Block& parent) const override;
};

/**
 * @brief In struct Array's member method `public init(size: Int64, repeat!: T)`,
 * we use `RawArray<T>(size, repeat: repeat)` to initialize RawArray.
 *
 * Cangjie Code:
 *      public init(size: Int64, repeat!: T) {
 *          // we use `RawArrayInitByValue` to describe this initialization
 *          //  `size` stands for element quantity, `repeat` stands for element init value
 *          this.rawptr = RawArray<T>(size, repeat: repeat)
 *          ...
 *      }
 */
class RawArrayInitByValue : public Expression {
    friend class CHIRContext;
    friend class CHIRBuilder;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    /**
     * @brief Retrieves address which allocated by `RawArrayAllocate`.
     */
    Value* GetRawArray() const;

    /**
     * @brief Retrieves element quantity in Array.
     */
    Value* GetSize() const;

    /**
     * @brief Retrieves element init value in Array.
     */
    Value* GetInitValue() const;

private:
    explicit RawArrayInitByValue(Value* raw, Value* size, Value* initVal, Block* parent);
    ~RawArrayInitByValue() override = default;

    RawArrayInitByValue* Clone(CHIRBuilder& builder, Block& parent) const override;
};

/**
 * @brief Generate a VArray object by `[]`.
 *
 * Cangjie Code:
 *      var x: VArray<Int64 ,$2> = [0, 1]
 */
class VArray : public Expression {
    friend class CHIRContext;
    friend class CHIRBuilder;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    /**
     * @brief Retrieves element quantity in VArray.
     */
    int64_t GetSize() const;

private:
    explicit VArray(std::vector<Value*> elements, Block* parent);
    ~VArray() override = default;

    VArray* Clone(CHIRBuilder& builder, Block& parent) const override;
};

/**
 * @brief Generate a VArray object by item or lambda.
 *
 * Cangjie Code:
 *      // size is 2, item is 3, initFunc is null
 *      var x = VArray<Int64,$2>(repeat: 3)
 *
 *      // size is 2, item is null, initFunc is {i => i}
 *      var y = VArray<Int64,$2>({i => i})
 */
class VArrayBuilder : public Expression {
    friend class CHIRContext;
    friend class CHIRBuilder;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    /**
     * @brief Retrieves element quantity in VArray.
     */
    Value* GetSize() const;

    /**
     * @brief Retrieves element init value in VArray.
     */
    Value* GetItem() const;

    /**
     * @brief Retrieves element init function in VArray.
     */
    Value* GetInitFunc() const;

private:
    explicit VArrayBuilder(Value* size, Value* item, Value* initFunc, Block* parent);
    enum class ElementIdx { SIZE_IDX = 0, ITEM_IDX = 1, INIT_FUNC_IDX = 2 };
    ~VArrayBuilder() override = default;

    VArrayBuilder* Clone(CHIRBuilder& builder, Block& parent) const override;
};

/**
 * @brief Get exception object in runtime, this object is passed to intrinsic `beginCatch`.
 *
 * Cangjie Code:
 *      try {
 *          throw Exception()
 *      } catch(_) {}  // we will capture exception in catch condition
 */
class GetException : public Expression {
    friend class CHIRContext;
    friend class CHIRBuilder;
private:
    explicit GetException(Block* parent);
    ~GetException() override = default;

    GetException* Clone(CHIRBuilder& builder, Block& parent) const override;
};

struct IntrisicCallContext {
    IntrinsicKind kind{IntrinsicKind::NOT_INTRINSIC};
    std::vector<Value*> args;
    std::vector<Type*> instTypeArgs;
};

/**
 * @brief Generate intrinsic function calling.
 * All intrinsic kinds are listd in cangjie/CHIR/IntrinsicKind.h
 */
class Intrinsic : public Expression {
    friend class ExprTypeConverter;
    friend class CHIRContext;
    friend class CHIRBuilder;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    /**
     * @brief Retrieves the intrinsic kind.
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
    const std::vector<Value*>& GetArgs() const;

    // ===--------------------------------------------------------------------===//
    // Others
    // ===--------------------------------------------------------------------===//
    std::string ToString(size_t indent = 0) const override;

private:
    explicit Intrinsic(const IntrisicCallContext& callContext, Block* parent);
    ~Intrinsic() override = default;

    Intrinsic* Clone(CHIRBuilder& builder, Block& parent) const override;

private:
    CHIR::IntrinsicKind intrinsicKind;
    std::vector<Type*> instantiatedTypeArgs;
};

/**
 * @brief A high-level description for `if` in Cangjie.
 * `If` expression will be desugared to `Branch` in the end.
 */
class If : public Expression {
    friend class CHIRContext;
    friend class CHIRBuilder;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    /** @brief Get the condition of this If Expression */
    Value* GetCondition() const;

    /** @brief Get the true branch of this If Expression */
    BlockGroup* GetTrueBranch() const;

    /** @brief Get the false branch of this If Expression */
    BlockGroup* GetFalseBranch() const;

    // ===--------------------------------------------------------------------===//
    // Others
    // ===--------------------------------------------------------------------===//
    std::string ToString(size_t indent = 0) const override;

private:
    explicit If(Value* cond, BlockGroup* thenBody, BlockGroup* elseBody, Block* parent);
    ~If() override = default;

    If* Clone(CHIRBuilder& builder, Block& parent) const override;
};

/**
 * @brief A high-level description for `while` and `do-while` in Cangjie.
 * `Loop` expression will be desugared to `Branch` + `GoTo` in the end.
 */
class Loop : public Expression {
    friend class CHIRContext;
    friend class CHIRBuilder;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    /** @brief Get the loop body of this Loop Expression */
    BlockGroup* GetLoopBody() const;

    // ===--------------------------------------------------------------------===//
    // Others
    // ===--------------------------------------------------------------------===//
    std::string ToString(size_t indent = 0) const override;

private:
    explicit Loop(BlockGroup* loopBody, Block* parent);
    ~Loop() override = default;

    Loop* Clone(CHIRBuilder& builder, Block& parent) const override;
};

/**
 * @brief A high-level description for `for-in` in Cangjie.
 * `ForIn` expression will be desugared to `Branch` + `GoTo` in the end.
 */
class ForIn : public Expression {
    friend class CHIRContext;
    friend class CHIRBuilder;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    /** @brief Get the induction variable of this ForIn Expression */
    Value* GetInductionVar() const;

    /** @brief Get the loop condition variable of this ForIn Expression */
    Value* GetLoopCondVar() const;

    /** @brief Get the body block group of this ForIn Expression */
    BlockGroup* GetBody() const;

    /** @brief Get the latch block group of this ForIn Expression */
    BlockGroup* GetLatch() const;

    /** @brief Get the conditional block group of this ForIn Expression */
    BlockGroup* GetCond() const;

    /** @brief Init the body, latch, and conditional block groups of this ForIn Expression */
    void InitBlockGroups(BlockGroup& body, BlockGroup& latch, BlockGroup& cond);

    // ===--------------------------------------------------------------------===//
    // Others
    // ===--------------------------------------------------------------------===//
    std::string ToString(size_t indent = 0) const override;

    /// Describes the execution order of sub block groups in a forin expression
    struct BGExecutionOrder {
        BGExecutionOrder(std::initializer_list<BlockGroup*> bgs)
        {
            CJC_ASSERT(bgs.size() == BG_NUMBER);
            std::copy(bgs.begin(), bgs.end(), s);
        }

        BlockGroup* const * begin() const
        {
            return &s[0];
        }
        BlockGroup* const * end() const
        {
            return s + BG_NUMBER;
        }
    private:
        constexpr static int BG_NUMBER{3};
        BlockGroup* s[BG_NUMBER];
    };
    /// Get the execution order of a forin expression. The first block is the jump target from outer expressions.
    virtual BGExecutionOrder GetExecutionOrder() const = 0;

protected:
    explicit ForIn(ExprKind kind, Value* inductionVar, Value* loopCondVar, Block* parent);
    ~ForIn() override = default;

    template <class T> T* CloneBase(CHIRBuilder& builder, Block& parent) const;
};

/**
 * @brief An expression to describe `for-in`.
 *
 * Cangjie Code:
 *      for (x in 0..10) {}  // this pattern is translated to `ForInRange`
 */
class ForInRange : public ForIn {
    friend class CHIRContext;
    friend class CHIRBuilder;
public:
    // ===--------------------------------------------------------------------===//
    // Others
    // ===--------------------------------------------------------------------===//
    BGExecutionOrder GetExecutionOrder() const override;

private:
    explicit ForInRange(Value* inductionVar, Value* loopCondVar, Block* parent)
        : ForIn(ExprKind::FORIN_RANGE, inductionVar, loopCondVar, parent) {}
    ~ForInRange() override = default;

    ForInRange* Clone(CHIRBuilder& builder, Block& parent) const override;
};

/**
 * @brief An expression to describe `for-in`.
 *
 * Cangjie Code:
 *      let arr = [1, 2, 3]
 *      for (x in arr) {}  // this pattern is translated to `ForInIter`,
 *                         // `arr`'s `next()` will be called for visiting its element
 */
class ForInIter : public ForIn {
    friend class CHIRContext;
    friend class CHIRBuilder;
public:
    // ===--------------------------------------------------------------------===//
    // Others
    // ===--------------------------------------------------------------------===//
    BGExecutionOrder GetExecutionOrder() const override;

private:
    explicit ForInIter(Value* inductionVar, Value* loopCondVar, Block* parent);
    ~ForInIter() override = default;

    ForInIter* Clone(CHIRBuilder& builder, Block& parent) const override;
};

/**
 * @brief An expression to describe `for-in`.
 *
 * Cangjie Code:
 *      for (x in 0..=10) {}  // this pattern is translated to `ForInClosedRange`
 */
class ForInClosedRange : public ForIn {
    friend class CHIRContext;
    friend class CHIRBuilder;
public:
    // ===--------------------------------------------------------------------===//
    // Others
    // ===--------------------------------------------------------------------===//
    BGExecutionOrder GetExecutionOrder() const override;

private:
    explicit ForInClosedRange(Value* inductionVar, Value* loopCondVar, Block* parent);
    ~ForInClosedRange() override = default;

    ForInClosedRange* Clone(CHIRBuilder& builder, Block& parent) const override;
};

/**
 * @brief An expression to describe lambda and local function in Cangjie.
 *
 * Cangjie Code:
 *      func foo() {
 *          var x = { i => 1 }  // this lambda is translated to `Lambda`
 *          func goo() {}       // this local function is translated to `Lambda`
 *      }
 */
class Lambda : public Expression {
    friend class ExprTypeConverter;
    friend class BlockGroup;
    friend class CHIRContext;
    friend class CHIRBuilder;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    bool IsCompileTimeValue() const;
    void SetCompileTimeValue();

    /**
     * @brief Checks if the lambda expression is local.
     *
     * @return True if the lambda expression is local, false otherwise.
     */
    bool IsLocalFunc() const;

    /**
     * @brief Retrieves the function type of the lambda expression.
     *
     * @return The function type of the lambda expression.
     */
    FuncType* GetFuncType() const;

    /**
     * @brief Retrieves the identifier of the lambda expression.
     *
     * @return The identifier of the lambda expression.
     */
    std::string GetIdentifier() const;

    /**
     * @brief Retrieves the identifier of the lambda expression without the prefix '@'.
     *
     * @return The identifier of the lambda expression without the prefix '@'.
     */
    std::string GetIdentifierWithoutPrefix() const;

    /**
     * @brief Retrieves the source code identifier of the lambda expression.
     *
     * @return The source code identifier of the lambda expression.
     */
    std::string GetSrcCodeIdentifier() const;

    /**
     * @brief Sets the host function for parameter default values.
     *
     * @param hostFunc The host function to set.
     */
    void SetParamDftValHostFunc(Lambda& hostFunc);

    /**
     * @brief Retrieves the host function for parameter default values.
     *
     * @return The host function for parameter default values.
     */
    Lambda* GetParamDftValHostFunc() const;
    
    // ===--------------------------------------------------------------------===//
    // Lambda Body
    // ===--------------------------------------------------------------------===//
    /**
     * @brief Retrieves the body of the lambda expression.
     *
     * @return The body of the lambda expression.
     */
    BlockGroup* GetBody() const;
    
    /**
     * @brief Initializes the body of the lambda expression.
     *
     * @param newBody The new body to initialize.
     */
    void InitBody(BlockGroup& newBody);

    /**
     * @brief Retrieves the entry block of the lambda expression.
     *
     * @return The entry block of the lambda expression.
     */
    Block* GetEntryBlock() const;

    /**
     * @brief Adds a parameter to the lambda expression.
     *
     * @param arg The parameter to add.
     */
    void AddParam(Parameter& arg);

    /**
     * @brief Retrieves the number of parameters in the lambda expression.
     *
     * @return The number of parameters in the lambda expression.
     */
    size_t GetNumOfParams() const;

    /**
     * @brief Retrieves a parameter at a specific index.
     *
     * @param index The index of the parameter.
     * @return The parameter at the specified index.
     */
    Parameter* GetParam(size_t index) const;

    /**
     * @brief Retrieves all parameters of the lambda expression.
     *
     * @return A vector of pointers to the parameters.
     */
    const std::vector<Parameter*>& GetParams() const;

    /**
     * @brief Retrieves the generic type parameters of the lambda expression.
     *
     * @return A vector of pointers to the generic type parameters.
     */
    const std::vector<GenericType*>& GetGenericTypeParams() const;

    /**
     * @brief Sets the return value of the lambda expression.
     *
     * @param ret The return value to set.
     */
    void SetReturnValue(LocalVar& ret);

    /**
     * @brief Retrieves the return value of the lambda expression.
     *
     * @return A pointer to the return value.
     */
    LocalVar* GetReturnValue() const;

    /**
     * @brief Retrieves the return type of the lambda expression.
     *
     * @return The return type of the lambda expression.
     */
    Type* GetReturnType() const;

    // ===--------------------------------------------------------------------===//
    // Modify Self
    // ===--------------------------------------------------------------------===//
    /**
     * @brief Removes the lambda expression from its block.
     */
    void RemoveSelfFromBlock() override;

    // ===--------------------------------------------------------------------===//
    // Others
    // ===--------------------------------------------------------------------===//
    /**
     * @brief Retrieves captured variables of the lambda expression.
     * These variables include mutable and immutable variables that declared in local scope,
     * so they must be local variables or parameters, not be global variables.
     * Of course, if there is another lambda calling, we will think that lambda is captured in current lambda
     *
     * @return A vector of pointers to the captured variables.
     */
    std::vector<Value*> GetCapturedVariables() const;
    
    std::string ToString(size_t indent = 0) const override;

private:
    explicit Lambda(FuncType* ty, Block* parent, bool isLocalFunc = false, const std::string& identifier = "",
        const std::string& srcCodeIdentifier = "", const std::vector<GenericType*>& genericTypeParams = {});
    ~Lambda() override
    {
        funcTy = nullptr;
    };

    /**
     * @brief Removes the body of the lambda expression.
     */
    void RemoveBody();

    Lambda* Clone(CHIRBuilder& builder, Block& parent) const override;

private:
    std::string identifier;        // the mangledName of nested function or lambda.
    std::string srcCodeIdentifier; // the name of nested function or lambda.
    FuncBody body;
    FuncType* funcTy;
    bool isLocalFunc{false};
    std::vector<GenericType*> genericTypeParams;
    Lambda* paramDftValHostFunc = nullptr;
    bool isCompileTimeValue = false;
};

/**
 * @brief Generate debug information for cjdb.
 */
class Debug : public Expression {
    friend class CHIRSerializer;
    friend class CHIRContext;
    friend class CHIRBuilder;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    /**
     * @brief obtains the identifier of the corresponding Cangjie source code.
     */
    std::string GetSrcCodeIdentifier() const;

    Value* GetValue() const;

    // ===--------------------------------------------------------------------===//
    // Others
    // ===--------------------------------------------------------------------===//
    std::string ToString(size_t indent = 0) const override;

private:
    explicit Debug(Value* local, std::string srcCodeIdentifier, Block* parent);
    ~Debug() override = default;

    Debug* Clone(CHIRBuilder& builder, Block& parent) const override;

    std::string srcCodeIdentifier;
};

/**
 * @brief An expression to describe `spawn` in Cangjie.
 *
 * Cangjie Code:
 *      let x = spawn(arg) {
 *          return 1
 *      }
 *
 * Will be desugared by CHIR:
 *      %0: Class-Future<Int64>& = Allocate(Class-Future<Int64>)
 *      %1: ()->Int64 = Lambda[mangled name]()=> {
 *          return 1
 *      }
 *      %2: Void = Apply(Future.init, %0, %1)
 *      %3: ArgType = xxx
 *      %4: Class-Future<Int64>& = Spawn(%0, %3)
 * Then, %0 is Future object, %3 is spawn argument, spawn only has one argument or not
 *
 * After redundant Future removing optimization, operands in `Spawn` will be changed:
 *      %1: ()->Int64 = Lambda[mangled name]()=> {
 *          return 1
 *      }
 *      %3: ArgType = xxx
 *      %4: Class-Future<Int64>& = Spawn(%1, %3)
 * Then, %1 is closure
 */
class Spawn : public Expression {
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
    /** @brief Get the Future object.*/
    Value* GetFuture() const;

    // ===--------------------------------------------------------------------===//
    // After Optimization
    // ===--------------------------------------------------------------------===//
    /** @brief Get the closure object.*/
    Value* GetClosure() const;

    /**
     * @brief Get the member method `executeClosure` in class Future.
     */
    FuncBase* GetExecuteClosure() const;

    // ===--------------------------------------------------------------------===//
    // Others
    // ===--------------------------------------------------------------------===//
    std::string ToString(size_t indent = 0) const override;

private:
    explicit Spawn(Value* val, Block* parent);
    explicit Spawn(Value* val, Value* arg, Block* parent);
    ~Spawn() override = default;

    Spawn* Clone(CHIRBuilder& builder, Block& parent) const override;

private:
    /**
     * @brief After optimization, backend will use `executeClosure` to create new thread, not `Future` object.
     * `executeClosure` is member method in class `Future` which is declared in std.core
     */
    FuncBase* executeClosure{nullptr};
};

/**
 * @brief Store instantiated types.
 *
 * Cangjie Code:
 *      func foo<T>() {}
 *      let x = foo<Bool>  // we need to store `Bool` for `x`, otherwise,
 *                         // we won't know which instantiated type args are used in next line
 *      x()
 */
class GetInstantiateValue : public Expression {
    friend class ExprTypeConverter;
    friend class CHIRContext;
    friend class CHIRBuilder;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    std::vector<Type*> GetInstantiateTypes() const;

    Value* GetGenericResult() const;

    // ===--------------------------------------------------------------------===//
    // Others
    // ===--------------------------------------------------------------------===//
    std::string ToString(size_t indent = 0) const override;

private:
    explicit GetInstantiateValue(Value* val, std::vector<Type*> insTypes, Block* parent);
    ~GetInstantiateValue() override = default;

    GetInstantiateValue* Clone(CHIRBuilder& builder, Block& parent) const override;

    std::vector<Type*> instantiateTys;
};
} // namespace Cangjie::CHIR
#endif // CANGJIE_CHIR_EXPRESSION_H
