// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the literal value related class in CHIR.
 */
#include "cangjie/CHIR/LiteralValue.h"
#include <iomanip>
#include <iostream>
#include <sstream>

#include "cangjie/Basic/StringConvertor.h"

using namespace Cangjie::CHIR;

LiteralValue::LiteralValue(Type* ty, ConstantValueKind literalKind)
    : Value(ty, "", ValueKind::KIND_LITERAL), literalKind(literalKind)
{
    CJC_ASSERT(literalKind != ConstantValueKind::KIND_FUNC);
}

bool LiteralValue::IsNullLiteral() const
{
    return literalKind == ConstantValueKind::KIND_NULL;
}

bool LiteralValue::IsBoolLiteral() const
{
    return literalKind == ConstantValueKind::KIND_BOOL;
}

bool LiteralValue::IsRuneLiteral() const
{
    return literalKind == ConstantValueKind::KIND_RUNE;
}

bool LiteralValue::IsStringLiteral() const
{
    return literalKind == ConstantValueKind::KIND_STRING;
}

bool LiteralValue::IsIntLiteral() const
{
    return literalKind == ConstantValueKind::KIND_INT;
}

bool LiteralValue::IsFloatLiteral() const
{
    return literalKind == ConstantValueKind::KIND_FLOAT;
}

bool LiteralValue::IsUnitLiteral() const
{
    return literalKind == ConstantValueKind::KIND_UNIT;
}

ConstantValueKind LiteralValue::GetConstantValueKind() const
{
    return literalKind;
}

BoolLiteral::BoolLiteral(Type* ty, bool val)
    : LiteralValue(ty, ConstantValueKind::KIND_BOOL), val(val)
{
    CJC_ASSERT(ty->IsBoolean());
}

bool BoolLiteral::GetVal() const
{
    return val;
}

std::string BoolLiteral::ToString() const
{
    std::stringstream ss;
    ss << std::boolalpha << val;
    return ss.str();
}

RuneLiteral::RuneLiteral(Type* ty, char32_t val)
    : LiteralValue(ty, ConstantValueKind::KIND_RUNE), val(val)
{
    CJC_ASSERT(ty->IsRune());
}

char32_t RuneLiteral::GetVal() const
{
    return val;
}

std::string RuneLiteral::ToString() const
{
    std::stringstream ss;
    ss << '\'' << val << '\'';
    return ss.str();
}

StringLiteral::StringLiteral(Type* ty, std::string val)
    : LiteralValue(ty, ConstantValueKind::KIND_STRING), val(val)
{
    CJC_ASSERT(ty->IsString());
}

std::string StringLiteral::GetVal() const&
{
    return val;
}
std::string StringLiteral::GetVal() &&
{
    return std::move(val);
}

std::string StringLiteral::ToString() const
{
    std::stringstream ss;
    ss << '"' << StringConvertor::Normalize(val) << '"';
    return ss.str();
}

IntLiteral::IntLiteral(Type* ty, uint64_t val)
    : LiteralValue(ty, ConstantValueKind::KIND_INT), val(val)
{
    CJC_ASSERT(ty->IsInteger());
}

int64_t IntLiteral::GetSignedVal() const
{
    return static_cast<int64_t>(val);
}

uint64_t IntLiteral::GetUnsignedVal() const
{
    return val;
}

bool IntLiteral::IsSigned() const
{
    return static_cast<IntType*>(ty)->IsSigned();
}

std::string IntLiteral::ToString() const
{
    std::stringstream ss;
    if (IsSigned()) {
        ss << GetSignedVal();
        ss << 'i';
    } else {
        ss << GetUnsignedVal();
        ss << 'u';
    }
    return ss.str();
}

FloatLiteral::FloatLiteral(Type* ty, double val)
    : LiteralValue(ty, ConstantValueKind::KIND_FLOAT), val(val)
{
}

double FloatLiteral::GetVal() const
{
    return val;
}

std::string FloatLiteral::ToString() const
{
    std::stringstream ss;
    ss << std::fixed << val << 'f';
    return ss.str();
}

UnitLiteral::UnitLiteral(Type* ty)
    : LiteralValue(ty, ConstantValueKind::KIND_UNIT)
{
}

std::string UnitLiteral::ToString() const
{
    std::stringstream ss;
    ss << "unit";
    return ss.str();
}

NullLiteral::NullLiteral(Type* ty)
    : LiteralValue(ty, ConstantValueKind::KIND_NULL)
{
}

std::string NullLiteral::ToString() const
{
    std::stringstream ss;
    ss << "null";
    return ss.str();
}
