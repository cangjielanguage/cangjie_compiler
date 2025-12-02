// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_LITERAL_VALUE_H
#define CANGJIE_CHIR_LITERAL_VALUE_H

#include "cangjie/CHIR/IR/Value/Value.h"

namespace Cangjie::CHIR {
enum ConstantValueKind : uint8_t {
    KIND_BOOL,
    KIND_RUNE,
    KIND_INT,
    KIND_FLOAT,
    KIND_STRING,
    KIND_UNIT,
    KIND_NULL,
    // Specially, Func is also a kind of constant value in CHIR thus can be bound to a variable by `Constant` expression
    KIND_FUNC
};

/*
 * @brief Base class for literals in CHIR.
 *
 */
class LiteralValue : public Value {
    friend class CHIRSerializer;
public:
    bool IsBoolLiteral() const;
    bool IsFloatLiteral() const;
    bool IsIntLiteral() const;
    bool IsNullLiteral() const;
    bool IsRuneLiteral() const;
    bool IsStringLiteral() const;
    bool IsUnitLiteral() const;

protected:
    explicit LiteralValue(Type* ty, ConstantValueKind literalKind);
    ~LiteralValue() = default;

private:
    ConstantValueKind GetConstantValueKind() const;

    ConstantValueKind literalKind;
};

/*
 * @brief Bool Literal in CHIR.
 *
 * Define bool literal value.
 */
class BoolLiteral : public LiteralValue {
    friend class CHIRBuilder;

public:
    bool GetVal() const;
    std::string ToString() const override;

private:
    BoolLiteral(Type* ty, bool val);
    ~BoolLiteral() = default;

    bool val; /* The value of contant boolean */
};

/*
 * @brief Rune Literal in CHIR.
 *
 * Define char literal value (with char32_t unicode character).
 */
class RuneLiteral : public LiteralValue {
    friend class CHIRBuilder;

public:
    char32_t GetVal() const;
    std::string ToString() const override;

private:
    explicit RuneLiteral(Type* ty, char32_t val);
    ~RuneLiteral() override = default;
    
    char32_t val; /* The value of constant character */
};

/*
 * @brief String Literal in CHIR.
 *
 * Define string literal value.
 */
class StringLiteral : public LiteralValue {
    friend class CHIRBuilder;

public:
    std::string GetVal() const&;
    std::string GetVal() &&;

    std::string ToString() const override;

private:
    explicit StringLiteral(Type* ty, std::string val);
    ~StringLiteral() override = default;

    std::string val;
};

/*
 * @brief IntLiteral in CHIR.
 *
 * Define signed or unsigned integer literal value.
 * The integer literal value can be Int8, Int16, Int32, Int64, IntNative, UInt8, UInt16,
 * UInt32, UInt64, UIntNative type.
 */
class IntLiteral : public LiteralValue {
    friend class CHIRBuilder;

public:
    int64_t GetSignedVal() const;

    uint64_t GetUnsignedVal() const;

    bool IsSigned() const;

    std::string ToString() const override;

private:
    explicit IntLiteral(Type* ty, uint64_t val);
    ~IntLiteral() override = default;
    
    /** @brief The value of this constant.
     *
     * It is stored in 64 bits unsigned integer, but the actual kind is determined by
     * the type of this literal
     */
    uint64_t val;
};

/*
 * @brief FloatLiteral in CHIR.
 *
 * Define IEEE64 standard float value.
 * The float literal value can be Float16, Float32, Float64 type.
 */
class FloatLiteral : public LiteralValue {
    friend class CHIRBuilder;

public:
    double GetVal() const;

    std::string ToString() const override;

private:
    explicit FloatLiteral(Type* ty, double val);
    ~FloatLiteral() override = default;

    /** @brief The value of this constant.
     *
     * It is stored in 64 bits float, but the actual kind is determined by
     * the type of this literal
     */
    double val;
};

/*
 * @brief UnitLiteral in CHIR.
 *
 * Define unit literal value.
 */
class UnitLiteral : public LiteralValue {
    friend class CHIRBuilder;

public:
    std::string ToString() const override;

private:
    explicit UnitLiteral(Type* ty);
    ~UnitLiteral() override = default;
};

/*
 * @brief NullLiteral in CHIR.
 *
 */
class NullLiteral : public LiteralValue {
    friend class CHIRBuilder;
public:
    std::string ToString() const override;

private:
    explicit NullLiteral(Type* ty);
    ~NullLiteral() override = default;
};
} // namespace Cangjie::CHIR

#endif
