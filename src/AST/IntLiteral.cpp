// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements int literal apis.
 */

#include "cangjie/AST/IntLiteral.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

#include "cangjie/AST/ASTCasting.h"
#include "cangjie/Utils/StdUtils.h"

using namespace Cangjie;
using namespace AST;

namespace {
const std::map<TypeKind, uint64_t> INTEGER_TO_MAX_VALUE{
    {TypeKind::TYPE_INT8, std::numeric_limits<int8_t>::max()},
    {TypeKind::TYPE_INT16, std::numeric_limits<int16_t>::max()},
    {TypeKind::TYPE_INT32, std::numeric_limits<int32_t>::max()},
    {TypeKind::TYPE_INT64, std::numeric_limits<int64_t>::max()},
    {TypeKind::TYPE_UINT8, std::numeric_limits<uint8_t>::max()},
    {TypeKind::TYPE_UINT16, std::numeric_limits<uint16_t>::max()},
    {TypeKind::TYPE_UINT32, std::numeric_limits<uint32_t>::max()},
    {TypeKind::TYPE_UINT64, std::numeric_limits<uint64_t>::max()},
    {TypeKind::TYPE_IDEAL_INT, std::numeric_limits<uint64_t>::max()},
};

const std::map<TypeKind, int64_t> INTEGER_TO_MIN_VALUE{
    {TypeKind::TYPE_INT8, std::numeric_limits<int8_t>::min()},
    {TypeKind::TYPE_INT16, std::numeric_limits<int16_t>::min()},
    {TypeKind::TYPE_INT32, std::numeric_limits<int32_t>::min()},
    {TypeKind::TYPE_INT64, std::numeric_limits<int64_t>::min()},
    {TypeKind::TYPE_UINT8, std::numeric_limits<uint8_t>::min()},
    {TypeKind::TYPE_UINT16, std::numeric_limits<uint16_t>::min()},
    {TypeKind::TYPE_UINT32, std::numeric_limits<uint32_t>::min()},
    {TypeKind::TYPE_UINT64, std::numeric_limits<uint64_t>::min()},
    {TypeKind::TYPE_IDEAL_INT, std::numeric_limits<int64_t>::min()},
};

const std::map<TypeKind, uint64_t> INTEGER_TO_BIT_LEN{
    {TypeKind::TYPE_INT8, 8},
    {TypeKind::TYPE_INT16, 16},
    {TypeKind::TYPE_INT32, 32},
    {TypeKind::TYPE_INT64, 64},
    {TypeKind::TYPE_UINT8, 8},
    {TypeKind::TYPE_UINT16, 16},
    {TypeKind::TYPE_UINT32, 32},
    {TypeKind::TYPE_UINT64, 64},
    {TypeKind::TYPE_IDEAL_INT, 64},
};
} // namespace

static const size_t I64_WIDTH = 64;
static const size_t UI64_WIDTH = 64;

bool IntLiteral::GreaterThanOrEqualBitLen(const TypeKind kind) const
{
    auto iter = INTEGER_TO_BIT_LEN.find(kind);
    if (iter == INTEGER_TO_BIT_LEN.end()) {
        return false;
    }
    return uint64Val >= iter->second;
}

int IntLiteral::EscapeCharacterToInt(char c)
{
    switch (c) {
        case 't':
            return static_cast<int>('\t');
        case 'b':
            return static_cast<int>('\b');
        case 'r':
            return static_cast<int>('\r');
        case 'n':
            return static_cast<int>('\n');
        case '\'':
            return static_cast<int>('\'');
        case '"':
            return static_cast<int>('\"');
        case '\\':
            return static_cast<int>('\\');
        case 'f':
            return static_cast<int>('\f');
        case 'v':
            return static_cast<int>('\v');
        case '0':
            return static_cast<int>('\0');
        default:
            return -1;
    }
}

// return -1 if encounter errors
// parse byte such as b'x', b'\n', b'\u{ff}'
static int ParseByteIntLitString(const std::string& val)
{
    size_t startQuote = val.find_first_of('\'', 0);
    size_t endQuote = val.find_last_of('\'', val.size() - 1);
    // 2 is minimum value. It must start with 'b' and '\'' since this is a lexer rule.
    if (endQuote - startQuote < 2) {
        return -1;
    }
    std::string lit = val.substr(startQuote + 1, (endQuote - startQuote) - 1);
    constexpr size_t esCharLen = 2; // 2 is the length of escape characters.
    if (lit.size() == 1) {
        return static_cast<int>(lit[0]);
    } else if (lit.size() == esCharLen && lit[0] == '\\') {
        return IntLiteral::EscapeCharacterToInt(lit[1]);
    } else if (lit.size() > esCharLen && lit[1] == 'u') {
        size_t lCurlPos = val.find_first_of('{', 0);
        size_t rCurlPos = val.find_first_of('}', 0);
        if (lCurlPos == std::string::npos || rCurlPos == std::string::npos || rCurlPos - lCurlPos <= 1) {
            return -1;
        }
        std::string digits = val.substr(lCurlPos + 1, (rCurlPos - lCurlPos) - 1);
        const int hexBase = 16;
        return Stoi(digits, hexBase).value_or(-1);
    }
    return -1;
}

static std::pair<int, std::string> GetBaseAndPureStringValue(const std::string& stringVal)
{
    const int baseLen = 2;
    const int binBase = 2;
    const int octBase = 8;
    const int hexBase = 16;
    int base = 10;
    bool isNegNum = false;
    std::string prefix;
    std::string stringValue = stringVal;

    if (stringValue.at(0) == '-') {
        stringValue.erase(0, 1);
        isNegNum = true;
    }

    if (stringValue.size() > baseLen) {
        prefix = stringValue.substr(0, baseLen);
        std::transform(prefix.begin(), prefix.end(), prefix.begin(), ::tolower);
    }
    // Erase underline. like 1_2_3 ==> 123
    stringValue.erase(std::remove(stringValue.begin(), stringValue.end(), '_'), stringValue.end());
    // Get base.
    if (prefix == "0b") {
        stringValue = stringValue.erase(0, baseLen);
        base = binBase;
    } else if (prefix == "0o") {
        stringValue = stringValue.erase(0, baseLen);
        base = octBase;
    } else if (prefix == "0x") {
        stringValue = stringValue.erase(0, baseLen);
        base = hexBase;
    }
    if (isNegNum) {
        stringValue = "-" + stringValue;
    }
    return std::make_pair(base, stringValue);
}

void IntLiteral::InitIntLiteral(const std::string& stringVal, const TypeKind kind)
{
    if (stringVal.empty()) {
        return;
    }
    type = kind;

    if (stringVal.at(0) == '-') {
        sign = -1;
    }

    auto [base, stringValue] = GetBaseAndPureStringValue(stringVal);

    bool outOfUintRange = false;
    bool outOfIntRange = false;

    if (stringVal[0] == 'b') {
        int64Val = ParseByteIntLitString(stringVal);
        if (int64Val == -1) {
            int64Val = 0;
            outOfIntRange = true;
            outOfUintRange = true;
        }
        uint64Val = static_cast<uint64_t>(int64Val);
    } else {
        if (auto ul = Stoull(stringValue, base)) {
            uint64Val = *ul;
        } else {
            uint64Val = 0;
            outOfUintRange = true;
        }

        if (auto il = Stoll(stringValue, base)) {
            int64Val = *il;
        } else {
            int64Val = 0;
            outOfIntRange = true;
        }
    }
    outOfRange = sign == -1 ? outOfIntRange : outOfUintRange;
}

static TypeKind GetRealTypeKindOfNative(Ptr<const Ty> ty)
{
    CJC_ASSERT(ty);
    TypeKind k = ty->kind;
    if (k == TypeKind::TYPE_INT_NATIVE) {
        k = StaticCast<const PrimitiveTy*>(ty)->bitness == I64_WIDTH ? TypeKind::TYPE_INT64 : TypeKind::TYPE_INT32;
    } else if (k == TypeKind::TYPE_UINT_NATIVE) {
        k = StaticCast<const PrimitiveTy*>(ty)->bitness == UI64_WIDTH ? TypeKind::TYPE_UINT64 : TypeKind::TYPE_UINT32;
    }
    return k;
}

void IntLiteral::SetOutOfRange(Ptr<const Ty> ty)
{
    type = GetRealTypeKindOfNative(ty);
    outOfRange = outOfRange || CheckOverflow(); // check overflow if convert not out of range
}

bool IntLiteral::CheckOverflow()
{
    if (INTEGER_TO_MAX_VALUE.find(type) == INTEGER_TO_MAX_VALUE.end()) {
        return false;
    }
    if (sign == 1) {
        return uint64Val > INTEGER_TO_MAX_VALUE.at(type);
    } else {
        return int64Val < INTEGER_TO_MIN_VALUE.at(type);
    }
}

namespace {
uint64_t CalcWrappingValue(uint64_t value, TypeKind type)
{
    if (type == TypeKind::TYPE_UINT8) {
        return static_cast<uint64_t>(static_cast<uint8_t>(value));
    }
    if (type == TypeKind::TYPE_UINT16) {
        return static_cast<uint64_t>(static_cast<uint16_t>(value));
    }
    if (type == TypeKind::TYPE_UINT32) {
        return static_cast<uint64_t>(static_cast<uint32_t>(value));
    }
    return value;
}

int64_t CalcWrappingValue(int64_t value, TypeKind type)
{
    if (type == TypeKind::TYPE_INT8) {
        return static_cast<int64_t>(static_cast<int8_t>(value));
    }
    if (type == TypeKind::TYPE_INT16) {
        return static_cast<int64_t>(static_cast<int16_t>(value));
    }
    if (type == TypeKind::TYPE_INT32) {
        return static_cast<int64_t>(static_cast<int32_t>(value));
    }
    return value;
}
} // namespace

void IntLiteral::CalcWrappingAndSaturatingVal()
{
    if (!outOfRange) {
        return;
    }
    if (INTEGER_TO_MAX_VALUE.find(type) == INTEGER_TO_MAX_VALUE.end()) {
        return;
    }
    if (IsUnsigned()) {
        wuint64Val = CalcWrappingValue(uint64Val, type);
        if (outOfMax) {
            suint64Val = INTEGER_TO_MAX_VALUE.at(type);
        } else {
            suint64Val = static_cast<uint64_t>(INTEGER_TO_MIN_VALUE.at(type));
        }
    } else {
        wint64Val = CalcWrappingValue(int64Val, type);
        if (outOfMax) {
            sint64Val = static_cast<int64_t>(INTEGER_TO_MAX_VALUE.at(type));
        } else {
            sint64Val = INTEGER_TO_MIN_VALUE.at(type);
        }
    }
}

void IntLiteral::SetWrappingValue()
{
    if (!outOfRange) {
        return;
    }
    if (IsUnsigned()) {
        SetUint64(wuint64Val);
        return;
    }
    SetInt64(wint64Val);
}

void IntLiteral::SetSaturatingValue()
{
    if (!outOfRange) {
        return;
    }
    if (IsUnsigned()) {
        SetUint64(suint64Val);
        return;
    }
    SetInt64(sint64Val);
}

std::string IntLiteral::GetValue() const
{
    if (IsUnsigned()) {
        return std::to_string(uint64Val);
    }
    return std::to_string(int64Val);
}

IntLiteral IntLiteral::operator-() const
{
    int64_t min = INTEGER_TO_MIN_VALUE.at(type);
    bool overflow = IsUnsigned() || (sign < 0 && int64Val == min);
    return IntLiteral(-int64Val, type, overflow);
}

IntLiteral IntLiteral::operator~() const
{
    // Cast to signed type to keep sign value.
    return IntLiteral(static_cast<int64_t>(~uint64Val), type, false);
}

IntLiteral IntLiteral::operator+(const IntLiteral& rhs) const
{
    if (sign + rhs.sign == 0) {
        return IntLiteral(int64Val + rhs.int64Val, type, false);
    }
    if (sign > 0) {
        bool overflow = (INTEGER_TO_MAX_VALUE.at(type) - uint64Val) < rhs.uint64Val;
        return IntLiteral(uint64Val + rhs.uint64Val, type, overflow, overflow);
    } else {
        bool overflow = (INTEGER_TO_MIN_VALUE.at(type) - int64Val) > rhs.int64Val;
        return IntLiteral(int64Val + rhs.int64Val, type, overflow);
    }
}

IntLiteral IntLiteral::operator-(const IntLiteral& rhs) const
{
    // For a - b, where a,b >= 0 or a,b < 0.
    if (sign + rhs.sign != 0) {
        if (sign == 1 && IsUnsigned()) {
            // allowed overflow calculation
            return IntLiteral(uint64Val - rhs.uint64Val, type, uint64Val < rhs.uint64Val);
        }
        return IntLiteral(int64Val - rhs.int64Val, type, false);
    }
    // For a - b, where a >= 0, b < 0 or a < 0, b >= 0. must be signed type.
    if (sign > 0) {
        uint64_t rhsPos = static_cast<uint64_t>(-rhs.int64Val);
        // `uint64Val` is guaranteed to be smaller than or equal to `MAX`, but `rhsPos` is not.
        // We cannot subtract `MAX` by `rhsPos`, because the subtraction may overflow.
        bool overflow = (INTEGER_TO_MAX_VALUE.at(type) - uint64Val) < rhsPos;
        return IntLiteral(int64Val - rhs.int64Val, type, overflow, overflow);
    } else {
        bool overflow = (int64Val - INTEGER_TO_MIN_VALUE.at(type)) < rhs.int64Val;
        return IntLiteral(int64Val - rhs.int64Val, type, overflow);
    }
}

IntLiteral IntLiteral::operator*(const IntLiteral& rhs) const
{
    if ((int64Val == 0 && uint64Val == 0) || (rhs.int64Val == 0 && rhs.uint64Val == 0)) {
        return IntLiteral(int64_t(0), type, false);
    }
    // Integer division will around to lower number except sign
    if (sign + rhs.sign == 0) {
        int64_t minVal = INTEGER_TO_MIN_VALUE.at(type);
        // if one of operand is -1, multiplication will not overflow
        bool inverse = (int64Val == -1) || (rhs.int64Val == -1);
        if (inverse) {
            return IntLiteral(int64Val * rhs.int64Val, type, false);
        } else if (outOfRange || rhs.outOfRange) {
            return IntLiteral(int64Val * rhs.int64Val, type, true);
        }
        // Eg a * -b > min or -a * b > min => overflow when min / -b < a or min / -a < b.
        if (rhs.int64Val == 0 || int64Val == 0) {
            return IntLiteral(int64_t(0), type, false);
        }
        bool overflow = (sign > 0) ? (minVal / rhs.int64Val < int64Val) : (minVal / int64Val < rhs.int64Val);
        return IntLiteral(int64Val * rhs.int64Val, type, overflow);
    }
    uint64_t maxVal = INTEGER_TO_MAX_VALUE.at(type);
    if (sign > 0) {
        // Eg a * b < max => overflow when max / b < a.
        bool overflow = maxVal / uint64Val < rhs.uint64Val;
        return IntLiteral(uint64Val * rhs.uint64Val, type, overflow, overflow);
    }
    // Eg a * -b < max => overflow when max / |-a| < |-b|.
    auto absValue = GetAbsValue();
    if (absValue == 0) {
        return IntLiteral(int64_t(0), type, false);
    }
    bool overflow = maxVal / absValue < rhs.GetAbsValue();
    return IntLiteral(int64Val * rhs.int64Val, type, overflow, overflow);
}

IntLiteral IntLiteral::operator/(const IntLiteral& rhs) const
{
    if (sign + rhs.sign > 0) {
        return IntLiteral(uint64Val / rhs.uint64Val, type, false);
    }
    int64_t minVal = INTEGER_TO_MIN_VALUE.at(type);
    bool overflow = (sign + rhs.sign) < 0 && int64Val == minVal && rhs.int64Val == -1;
    if (overflow && int64Val == std::numeric_limits<int64_t>::min()) {
        return IntLiteral(static_cast<uint64_t>(-static_cast<double>(int64Val)), type, overflow, overflow);
    }
    if (rhs.int64Val != 0) {
        return IntLiteral(int64Val / rhs.int64Val, type, overflow, overflow);
    }
    if (rhs.outOfRange) {
        return IntLiteral(int64Val, type, false);
    }
    auto rhsAbsValue = rhs.GetAbsValue();
    if (rhsAbsValue != 0) {
        int64_t res = static_cast<int64_t>(GetAbsValue() / rhsAbsValue) * sign * rhs.sign;
        return IntLiteral(res, type, overflow, overflow);
    }
    return IntLiteral(int64Val, type, false);
}

IntLiteral IntLiteral::operator%(const IntLiteral& rhs) const
{
    if (sign + rhs.sign > 0) {
        return IntLiteral(uint64Val % rhs.uint64Val, type, false);
    }
    if (rhs.int64Val != 0) {
        return IntLiteral(int64Val % rhs.int64Val, type, false);
    }
    if (rhs.outOfRange) {
        return IntLiteral(int64Val, type, false);
    }
    auto rhsAbsValue = rhs.GetAbsValue();
    if (rhsAbsValue != 0) {
        // In c++ calculation, sign of mod is decided by left value.
        return IntLiteral(static_cast<int64_t>(GetAbsValue() % rhsAbsValue) * sign, type, false);
    }
    return IntLiteral(int64Val, type, false);
}

IntLiteral IntLiteral::operator>>(const IntLiteral& rhs) const
{
    // Cast to signed type to keep sign value.
    return IntLiteral(static_cast<int64_t>(uint64Val >> rhs.uint64Val), type, false);
}

IntLiteral IntLiteral::operator<<(const IntLiteral& rhs) const
{
    // Cast to signed type to keep sign value.
    return IntLiteral(static_cast<int64_t>(uint64Val << rhs.uint64Val), type, false);
}

IntLiteral IntLiteral::operator&(const IntLiteral& rhs) const
{
    // Cast to signed type to keep sign value.
    return IntLiteral(static_cast<int64_t>(uint64Val & rhs.uint64Val), type, false);
}

IntLiteral IntLiteral::operator^(const IntLiteral& rhs) const
{
    // Cast to signed type to keep sign value.
    return IntLiteral(static_cast<int64_t>(uint64Val ^ rhs.uint64Val), type, false);
}

IntLiteral IntLiteral::operator|(const IntLiteral& rhs) const
{
    // Cast to signed type to keep sign value.
    return IntLiteral(static_cast<int64_t>(uint64Val | rhs.uint64Val), type, false);
}

uint64_t IntLiteral::QuickPow(const uint64_t inBase, const uint64_t inExp, const uint64_t maxVal, bool& overflow) const
{
    uint64_t res = 1;
    uint64_t base = inBase;
    uint64_t exp = inExp;
    bool ov = false;
    while (exp != 0 && !overflow) {
        if ((exp & 1) != 0) {
            if (ov || (base != 0 && (maxVal / base < res))) {
                overflow = true;
            }
            res *= base;
        }
        if (!ov && (base != 0 && (maxVal / base < base))) {
            ov = true;
        }
        base *= base;
        exp >>= 1;
    }
    return res;
}

IntLiteral IntLiteral::PowerOf(const IntLiteral& exponent) const
{
    bool overflow = false;
    if (exponent.sign < 0) { // power of minus number will not overflow
        double result = pow(static_cast<double>(int64Val), static_cast<double>(exponent.int64Val));
        return IntLiteral(static_cast<int64_t>(result), type, overflow);
    }
    // overflow condition: a ** b > max, -a ** b > max where b is even, |-a ** b| > |min| where b is odd
    bool inverse = sign < 0 && (exponent.uint64Val & 1) == 1;
    uint64_t maxVal = inverse ? static_cast<uint64_t>(-INTEGER_TO_MIN_VALUE.at(type)) : INTEGER_TO_MAX_VALUE.at(type);
    uint64_t base = sign > 0 ? uint64Val : static_cast<uint64_t>(-int64Val);
    uint64_t result = QuickPow(base, exponent.uint64Val, maxVal, overflow);
    if (inverse) {
        return IntLiteral(-static_cast<int64_t>(result), type, overflow);
    }
    return IntLiteral(result, type, overflow, overflow);
}

uint64_t IntLiteral::GetAbsValue() const
{
    /* The min value of int64_t cast to uint64_t is exactly its absolute value. */
    if (int64Val == std::numeric_limits<int64_t>::min()) {
        return static_cast<uint64_t>(int64Val);
    }
    return sign == 1 ? uint64Val : static_cast<uint64_t>(-int64Val);
}
