// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_ANALYSIS_SINT_H
#define CANGJIE_CHIR_ANALYSIS_SINT_H

#include <climits>
#include <cstdint>
#include <iomanip>

#include "cangjie/CHIR/IR/Type/Type.h"

namespace Cangjie::CHIR {
/**
 * @brief int width enum
 */
enum IntWidth : unsigned { I8 = 8, I16 = 16, I32 = 32, I64 = 64 };

/// get width from CHIR type.
IntWidth ToWidth(const Type& ty);

/// Build an IntWidth from bitness \p v
constexpr IntWidth FromUnsigned(unsigned v)
{
    switch (v) {
        case 8u:
            return IntWidth::I8;
        case 16u:
            return IntWidth::I16;
        case 32u:
            return IntWidth::I32;
        case 64u:
            return IntWidth::I64;
        default:
            CJC_ABORT();
            return IntWidth::I64;
    }
}

/// get width of template type t.
template <class T> constexpr IntWidth FromUnsigned()
{
    return FromUnsigned(sizeof(T) * CHAR_BIT);
}

/// radix enum replacing number.
enum Radix : unsigned { R2 = 2, R10 = 10, R16 = 16 };

/**
 * @brief base formatter struct, to print SInt with selected signess and radix.
 */
struct SIntFormatterBase {
    /// whether is unsigned.
    bool asUnsigned;
    /// number radix to print.
    Radix radix;
    /// beauty print delimiter.
    static constexpr char DIVIDOR = '|';
    /// set radix base.
    decltype(std::setbase(0)) GetBaseManip() const;
    /// stream output with beauty print.
    friend std::ostream& operator<<(std::ostream& out, const SIntFormatterBase& range);
};

/**
 * @brief SInt number to unify unsigned and signed number and its operator.
 */
struct SInt final {
    /// 64 word width standard.
    using WordType = uint64_t;
    /// minimum unit with 8 width size.
    static constexpr unsigned WORD_SIZE = 8;
    /// count of bits that is used to store SInt.
    static constexpr unsigned BITS_PER_WORD = 64;
    /// maximum number uint64.
    static constexpr WordType WORD_TYPE_MAX = ~0ull;

    /// Region: constructors & getters
    SInt(IntWidth width, WordType val);

    /// copy constructor with width and value.
    SInt(const SInt& other);

    /// move constructor with width and value.
    SInt(SInt&& other);

    /// implicit constructors from unsigned values.
    SInt(unsigned val);

    /// implicit constructors from unsigned long values.
    SInt(unsigned long val);

    /// implicit constructors from unsigned long long values.
    SInt(unsigned long long val);

    /// Prevent unexpected sign extension
    SInt(int) = delete;
    SInt(long) = delete;
    SInt(long long) = delete;

    /// Construct an SInt with given width from string, which represents the SInt by radix.
    /// Should any error occurs during the construction (i.e. width not enough, incorrect character in radix), that
    /// error is NOT handled.
    SInt(IntWidth width, const std::string& str, Radix radix);

    /// return unsigned value
    WordType UVal() const;

    /// return signed value
    int64_t SVal() const;

    /// output width of SInt
    IntWidth Width() const;

    /// Region: get static constants
    static SInt Zero(IntWidth w);

    /// Gets the maximum unsigned value of SInt of width \p width
    static SInt UMaxValue(IntWidth w);

    /// Gets the maximum signed value of SInt of width \p width
    static SInt SMaxValue(IntWidth w);

    /// Gets the minimum unsigned value of SInt of width \p width
    static SInt UMinValue(IntWidth w);

    /// Gets the minimum signed value of SInt of width \p width
    static SInt SMinValue(IntWidth w);

    /// Get all zero SInt of input width
    static SInt AllOnes(IntWidth w);

    /// Get a bit mask integer with exactly \p no bit set
    static SInt BitMask(IntWidth w, unsigned no);

    /// Get a bit mask integer with bits from \p loBit to \p hiBit set.
    static SInt BitMask(IntWidth w, unsigned loBit, unsigned hiBit);

    /// Get a bit mask integer with wrapping.
    /// If \p loBit > \p hiBit, the behaviour is same as the non-wrapped version;
    /// If \p loBit <= \p hiBit, bits from \p hiBit to width - 1 and bits from 0 to \p loBit are all set.
    /// Specifically if \p loBit == \p hiBit, the result has all bits within \p width set.
    static SInt WrappedBitMask(IntWidth w, unsigned loBit, unsigned hiBit);

    /// Set an SInt of width \p width, with exactly high \p highBits set
    static SInt GetHighBitsSet(IntWidth w, unsigned highBits);

    /// Set an SInt of width \p width, with exactly low \p lowBits set
    static SInt GetLowBitsSet(IntWidth w, unsigned lowBits);

    /// Set the selected bit \p pos to one with width length.
    static SInt GetOneBitSet(IntWidth w, unsigned pos);

    /// Region: value tests
    /// Determine if this SInt is negative
    bool IsNeg() const;
    bool IsNonNeg() const;
    bool IsPositive() const;

    /// Determine if this SInt is non negative
    bool IsSignBitSet() const;

    /// Determine if this SInt is negative
    bool IsSignBitClear() const;

    /// Determine if this SInt has only the specified bit set.
    bool IsOneBitSet(unsigned no) const;

    /// Determine if all bits are set
    bool IsAllOnes() const;

    /// Is certain number.
    bool IsZero() const;
    bool IsOne() const;
    bool IsUMaxValue() const;
    bool IsSMaxValue() const;
    bool IsUMinValue() const;
    bool IsSMinValue() const;

    // /Check if this SInt has an \p n-bits unsigned value
    bool IsUIntN(unsigned n) const;

    /// Check if this SInt has an \p n-bits signed value
    bool IsSIntN(unsigned n) const;

    /// Check if value is power of 2.
    bool IsPowerOf2() const;

    /// Check if value is negated power of 2.
    bool IsNegatedPowerOf2() const;

    /// Check if this SInt is returned by SignMask
    bool IsSignMask() const;

    /// change Sint to bool.
    bool ToBool() const;

    /// Returns the value of this SInt saturate to \p maxv
    uint64_t GetULimitedValue(uint64_t maxv = UINT64_MAX) const;

    /// Check if this SInt consists of a repeated bit pattern
    /// e.g. 0x01010101 is a splat of 8.
    /// \p splatSizeInBits must divide \f width without remainder
    bool IsSplat(unsigned splatSizeInBits) const;

    /// Check if val is mask with input bits, bits < BITS_PER_WORD
    bool IsMask(unsigned bits) const;

    /// Returns a SInt with the same width as this, and with low bits zero-masked and high bits right shift to the
    /// least significant bits
    SInt HighBits(unsigned num) const;

    /// Returns a SInt with the same width as this, and with high bits zero-masked
    SInt LowBits(unsigned num) const;

    /// Check if the two SInt has the same value after ZExt one of them if needed.
    static bool IsSameValue(const SInt& a, const SInt& b);

    /**
     * @brief beauty printer for SInt
     */
    struct Formatter final : public SIntFormatterBase {
        /// SInt value to print
        const SInt& value;
        /// output stream function
        friend std::ostream& operator<<(std::ostream& out, const Formatter& fmt);
    };

    /// format SInt to beauty format.
    Formatter ToString(bool asUnsigned, Radix radix = Radix::R10) const;

    /// Region: unary operators
    SInt operator++(int);
    SInt& operator++();
    SInt operator--(int);
    SInt& operator--();
    bool operator!() const;

    /// assignment operator
    SInt& operator=(const SInt& other);
    SInt& operator=(SInt&& other);
    SInt& operator=(uint64_t v);

    // compound assignment operators
    SInt& operator&=(const SInt& other);
    SInt& operator&=(uint64_t v);
    SInt& operator|=(const SInt& other);
    SInt& operator|=(uint64_t v);
    SInt& operator^=(const SInt& other);
    SInt& operator^=(uint64_t v);
    SInt& operator*=(const SInt& other);
    SInt& operator*=(uint64_t v);
    SInt operator*(const SInt& other) const;
    SInt& operator+=(const SInt& other);
    SInt& operator+=(uint64_t v);
    SInt& operator-=(const SInt& other);
    SInt& operator-=(uint64_t v);
    SInt& operator<<=(unsigned count);
    SInt& operator<<=(const SInt& count);
    SInt operator<<(const SInt& count) const;
    SInt operator<<(unsigned count) const;

    /// Arithmetic right shift, that is, the sign bit is preserved when the left operand is a negative number
    SInt AShr(unsigned count) const;
    void AShrInPlace(unsigned count);

    /// Logical right shift, that is, the sign bit is lost when the left operand is a negative number
    SInt LShr(unsigned count) const;
    void LShrInPlace(unsigned count);
    SInt Ashr(const SInt& count) const;
    SInt LShr(const SInt& count) const;
    void LShrInPlace(const SInt& count);
    SInt Shl(unsigned count) const;
    SInt Shl(const SInt& count) const;

    /// Concatenate the bits from \p v onto the bottom of this
    /// RR: what does this mean?
    SInt Concat(const SInt& v) const;

    /// Divide and remainder operations. Note that division and remainder never overflow.
    SInt UDiv(const SInt& rhs) const;
    SInt UDiv(uint64_t rhs) const;
    SInt SDiv(const SInt& rhs) const;
    SInt SDiv(int64_t rhs) const;
    SInt URem(const SInt& rhs) const;
    SInt URem(uint64_t rhs) const;
    SInt SRem(const SInt& rhs) const;
    SInt SRem(int64_t rhs) const;

    /// Arithmetic operations with an additional flag \p overflow that indicates this operation has overflow or not
    SInt SAddOvf(const SInt& rhs, bool& overflow) const;
    SInt UAddOvf(const SInt& rhs, bool& overflow) const;
    SInt SSubOvf(const SInt& rhs, bool& overflow) const;
    SInt USubOvf(const SInt& rhs, bool& overflow) const;
    SInt SMulOvf(const SInt& rhs, bool& overflow) const;
    SInt UMulOvf(const SInt& rhs, bool& overflow) const;
    SInt SShlOvf(const SInt& count, bool& overflow) const;
    SInt UShlOvf(const SInt& count, bool& overflow) const;

    /// Arithmetic operations with saturates the result
    SInt SAddSat(const SInt& rhs) const;
    SInt UAddSat(const SInt& rhs) const;
    SInt SSubSat(const SInt& rhs) const;
    SInt USubSat(const SInt& rhs) const;
    SInt SMulSat(const SInt& rhs) const;
    SInt UMulSat(const SInt& rhs) const;
    SInt SDivSat(const SInt& rhs) const;
    SInt SShlSat(const SInt& rhs) const;
    SInt UShlSat(const SInt& rhs) const;

    /// at operator to get certain bit.
    bool At(unsigned bit) const;
    bool operator[](unsigned bit) const;

    // Region: Comparison operators
    bool operator==(const SInt& other) const;
    bool operator==(uint64_t rhs) const;
    bool operator!=(const SInt& rhs) const;
    bool operator!=(uint64_t rhs) const;
    bool Ult(const SInt& rhs) const;
    bool Ult(uint64_t rhs) const;
    bool Slt(const SInt& rhs) const;
    bool Slt(int64_t rhs) const;
    bool Ule(const SInt& rhs) const;
    bool Ule(uint64_t rhs) const;
    bool Sle(const SInt& rhs) const;
    bool Sle(int64_t rhs) const;
    bool Ugt(const SInt& rhs) const;
    bool Ugt(uint64_t rhs) const;
    bool Sgt(const SInt& rhs) const;
    bool Sgt(int64_t rhs) const;
    bool Uge(const SInt& rhs) const;
    bool Uge(uint64_t rhs) const;
    bool Sge(const SInt& rhs) const;
    bool Sge(int64_t rhs) const;

    static SInt UMin(const SInt& lhs, const SInt& rhs);
    static SInt SMin(const SInt& lhs, const SInt& rhs);
    static SInt UMax(const SInt& lhs, const SInt& rhs);
    static SInt SMax(const SInt& lhs, const SInt& rhs);

    /// Check if any bit is set to true in both operands
    bool Intersects(const SInt& other) const;

    /// Check if all bits set in this SInt are also set in the other
    bool IsSubsetOf(const SInt& other) const;

    /// Truncate to SInt width \p width
    SInt Trunc(IntWidth w) const;
    /// Truncate this SInt as unsigned to another unisgned with a new width. If the truncation cannot be lossless,
    /// return max value.
    SInt TruncUSat(IntWidth w) const;
    /// Truncate this SInt as signed to another signed with a new width. If the truncation cannot be lossless,
    /// return the saturated value.
    SInt TruncSSat(IntWidth w) const;
    /// Extend this SInt as signed to a new width.
    SInt SExt(IntWidth w) const;
    /// Extend this SInt as unsigned to a new width.
    SInt ZExt(IntWidth w) const;

    /// Region: manipulating functions
    /// Set all bits to one
    void SetAllBits();

    /// Set the selected bit \p pos to one
    void SetBit(unsigned pos);

    /// Set from \p lo to \p hi bits to one
    void SetBits(unsigned lo, unsigned hi);

    /// set low bits to one.
    void SetLowBits(unsigned lo);

    /// set high bits to one.
    void SetHighBits(unsigned hi);

    /// clear position to zero.
    void ClearBit(unsigned pos);

    /// flip all bits.
    void FlipAllBits();

    /// flip position bit.
    void FlipBit(unsigned pos);

    /// Negate this value in place
    void Neg();

    /// indicate active bits.
    unsigned ActiveBits() const;

    /// get significant bits.
    unsigned SignificantBits() const;

    // get zero extend value.
    uint64_t GetZExtValue() const;

    // get signed zero extend value.
    int64_t GetSExtValue() const;

    /// Count leading zeroes
    unsigned Clz() const;

    /// Count leading ones
    unsigned Clo() const;

    /// Get the number of leading bits of this SInt that are equal to its sign bit
    unsigned GetNumSignBits() const;

    /// Count trailing zeroes
    unsigned Ctz() const;

    /// Count trailing ones
    unsigned Cto() const;

    /// Count population
    unsigned Popcnt() const;

    /// set bits  and return new value.
    static SInt GetBitsSetFrom(IntWidth w, unsigned lo);

private:
    IntWidth width;
    uint64_t val;

    static uint64_t MaskBit(unsigned pos);

    SInt& ClearUnusedBits();

    void FromString(const std::string& str, Radix radix);

    int UCmp(const SInt& rhs) const;
    int SCmp(const SInt& rhs) const;
};

/// SInt operators.
bool operator==(uint64_t v1, const SInt& v2);
bool operator!=(uint64_t v1, const SInt& v2);
SInt operator-(SInt v);
SInt operator+(SInt a, const SInt& b);
SInt operator+(const SInt& a, SInt&& b);
SInt operator+(SInt a, uint64_t rhs);
SInt operator+(uint64_t lhs, SInt b);
SInt operator&(SInt a, const SInt& b);
SInt operator&(const SInt& a, SInt&& b);
SInt operator&(SInt a, uint64_t rhs);
SInt operator&(uint64_t lhs, SInt b);
SInt operator|(SInt a, const SInt& b);
SInt operator|(const SInt& a, SInt&& b);
SInt operator|(SInt a, uint64_t rhs);
SInt operator|(uint64_t lhs, SInt b);
SInt operator^(SInt a, const SInt& b);
SInt operator^(const SInt& a, SInt&& b);
SInt operator^(SInt a, uint64_t rhs);
SInt operator^(uint64_t lhs, SInt b);
SInt operator-(SInt a, const SInt& b);
SInt operator-(const SInt& a, SInt&& b);
SInt operator-(SInt a, uint64_t rhs);
SInt operator-(uint64_t lhs, SInt b);
SInt operator*(SInt a, uint64_t rhs);
SInt operator*(uint64_t lhs, SInt b);
} // namespace Cangjie::CHIR
#endif
