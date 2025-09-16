// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Analysis/SInt.h"

#include <iostream>
#include "cangjie/CHIR/Analysis/Arithmetic.h"

namespace Cangjie::CHIR {
IntWidth ToWidth(const Type& ty)
{
    switch (ty.GetTypeKind()) {
        case Type::TypeKind::TYPE_INT8:
        case Type::TypeKind::TYPE_UINT8:
            return IntWidth::I8;
        case Type::TypeKind::TYPE_INT16:
        case Type::TypeKind::TYPE_UINT16:
            return IntWidth::I16;
        case Type::TypeKind::TYPE_INT32:
        case Type::TypeKind::TYPE_UINT32:
            return IntWidth::I32;
        default:
            return IntWidth::I64;
    }
}

static unsigned GetDigit(char d, Radix radix)
{
    unsigned ans;
    constexpr unsigned radixDiff = 11u;
    constexpr unsigned raidxDiffAddition = static_cast<unsigned>(Radix::R10);
    if (radix == Radix::R16) {
        ans = static_cast<unsigned int>(d) - static_cast<unsigned int>('0');
        if (ans < Radix::R10) {
            return ans;
        }
        ans = static_cast<unsigned int>(d) - static_cast<unsigned int>('A');
        if (ans <= radix - radixDiff) {
            return ans + raidxDiffAddition;
        }
        ans = static_cast<unsigned int>(d) - static_cast<unsigned int>('a');
        if (ans <= radix - radixDiff) {
            return ans + raidxDiffAddition;
        }
        radix = Radix::R10;
    }
    ans = static_cast<unsigned int>(d) - static_cast<unsigned int>('0');
    if (ans < static_cast<unsigned>(radix)) {
        return ans;
    }
    return UINT_MAX;
}

SInt::SInt(IntWidth width, WordType val) : width{width}, val{val}
{
    ClearUnusedBits();
}

SInt::SInt(const SInt& other) : width{other.width}, val{other.val}
{
}

SInt::SInt(SInt&& other) : width{other.width}, val{other.val}
{
}

SInt::SInt(unsigned val) : SInt{FromUnsigned<unsigned>(), val}
{
}

SInt::SInt(unsigned long val) : SInt{FromUnsigned<unsigned long>(), val}
{
}

SInt::SInt(unsigned long long val) : SInt{FromUnsigned<unsigned long long>(), val}
{
}

SInt::SInt(IntWidth width, const std::string& str, Radix radix) : width(width)
{
    FromString(str, radix);
}

SInt::WordType SInt::UVal() const
{
    return val;
}

int64_t SInt::SVal() const
{
    return SignExtend64(val, width);
}

IntWidth SInt::Width() const
{
    return width;
}

SInt SInt::Zero(IntWidth w)
{
    return SInt(w, 0);
}

SInt SInt::UMaxValue(IntWidth w)
{
    return AllOnes(w);
}

SInt SInt::SMaxValue(IntWidth w)
{
    SInt ans = AllOnes(w);
    CJC_ASSERT(w > 0);
    ans.ClearBit(w - 1);
    return ans;
}

SInt SInt::UMinValue(IntWidth w)
{
    return Zero(w);
}

SInt SInt::SMinValue(IntWidth w)
{
    SInt ans = Zero(w);
    CJC_ASSERT(w > 0);
    ans.SetBit(w - 1);
    return ans;
}

SInt SInt::AllOnes(IntWidth w)
{
    return {w, WORD_TYPE_MAX};
}

SInt SInt::BitMask(IntWidth w, unsigned no)
{
    SInt ans = Zero(w);
    ans.SetBit(no);
    return ans;
}

SInt SInt::GetHighBitsSet(IntWidth w, unsigned highBits)
{
    SInt res{w, 0};
    res.SetHighBits(highBits);
    return res;
}

SInt SInt::GetLowBitsSet(IntWidth w, unsigned lowBits)
{
    SInt res{w, 0};
    res.SetLowBits(lowBits);
    return res;
}

SInt SInt::GetOneBitSet(IntWidth w, unsigned pos)
{
    SInt ans{w, 0};
    ans.SetBit(pos);
    return ans;
}

bool SInt::IsNeg() const
{
    return IsSignBitSet();
}

bool SInt::IsNonNeg() const
{
    return IsSignBitClear();
}

bool SInt::IsPositive() const
{
    return IsNonNeg() && !IsZero();
}

// Determine if this SInt is non negative
bool SInt::IsSignBitSet() const
{
    CJC_ASSERT(width > 0);
    return At(width - 1);
}

bool SInt::IsSignBitClear() const
{
    return !IsSignBitSet();
}

// Determine if this SInt has only the specified bit set.
bool SInt::IsOneBitSet(unsigned no) const
{
    return At(no) && Popcnt() == 1;
}

// Determine if all bits are set
bool SInt::IsAllOnes() const
{
    CJC_ASSERT(width > 0 && width <= I64);
    return val == (WORD_TYPE_MAX >> (BITS_PER_WORD - width));
}

bool SInt::IsZero() const
{
    return val == 0;
}

bool SInt::IsOne() const
{
    return val == 1;
}

bool SInt::IsUMaxValue() const
{
    return IsAllOnes();
}

bool SInt::IsSMaxValue() const
{
    CJC_ASSERT(width > 0 && width <= I64);
    return val == ((static_cast<WordType>(1) << (width - 1)) - 1);
}
bool SInt::IsUMinValue() const
{
    return IsZero();
}

bool SInt::IsSMinValue() const
{
    CJC_ASSERT(width > 0 && width <= I64);
    return val == (static_cast<WordType>(1) << (width - 1));
}

// Check if this SInt has an \p n-bits unsigned value
bool SInt::IsUIntN(unsigned n) const
{
    return ActiveBits() <= n;
}

// Check if this SInt has an \p n-bits signed value
bool SInt::IsSIntN(unsigned n) const
{
    return SignificantBits() <= n;
}

bool SInt::IsPowerOf2() const
{
    return ::Cangjie::CHIR::IsPowerOf2(val);
}

bool SInt::IsNegatedPowerOf2() const
{
    if (IsNonNeg()) {
        return false;
    }
    return Clo() + Ctz() == width;
}

// Check if this SInt is returned by SignMask
bool SInt::IsSignMask() const
{
    return IsSMinValue();
}

bool SInt::ToBool() const
{
    return !IsZero();
}

// Returns the value of this SInt saturate to \p maxv
uint64_t SInt::GetULimitedValue(uint64_t maxv) const
{
    return Ugt(maxv) ? maxv : GetZExtValue();
}

bool SInt::IsMask(unsigned bits) const
{
    return val == (WORD_TYPE_MAX >> (BITS_PER_WORD - bits));
}

/// format SInt to beauty format.
SInt::Formatter SInt::ToString(bool asUnsigned, Radix radix) const
{
    return {{asUnsigned, radix}, *this};
}

/// Region: unary operators
SInt SInt::operator++(int)
{
    SInt ans{*this};
    ++*this;
    return ans;
}

SInt& SInt::operator++()
{
    ++val;
    return ClearUnusedBits();
}

SInt SInt::operator--(int)
{
    SInt ans{*this};
    --*this;
    return ans;
}

SInt& SInt::operator--()
{
    --val;
    return ClearUnusedBits();
}

bool SInt::operator!() const
{
    return IsZero();
}

SInt& SInt::operator=(const SInt& other)
{
    if (this != &other) {
        val = other.val;
        width = other.width;
    }
    return *this;
}

SInt& SInt::operator=(SInt&& other)
{
    val = other.val;
    width = other.width;
    return *this;
}

SInt& SInt::operator=(uint64_t v)
{
    val = v;
    return ClearUnusedBits();
}

// compound assignment operators
SInt& SInt::operator&=(const SInt& other)
{
    return *this &= other.val;
}

SInt& SInt::operator&=(uint64_t v)
{
    val &= v;
    return *this;
}

SInt& SInt::operator|=(const SInt& other)
{
    return *this |= other.val;
}

SInt& SInt::operator|=(uint64_t v)
{
    val |= v;
    return *this;
}

SInt& SInt::operator^=(const SInt& other)
{
    return *this ^= other.val;
}

SInt& SInt::operator^=(uint64_t v)
{
    val ^= v;
    return *this;
}

SInt& SInt::operator*=(const SInt& other)
{
    return *this = *this * other;
}

SInt& SInt::operator*=(uint64_t v)
{
    val *= v;
    return ClearUnusedBits();
}

SInt SInt::operator*(const SInt& other) const
{
    CJC_ASSERT(width == other.width);
    return {width, val * other.val};
}

SInt& SInt::operator+=(const SInt& other)
{
    CJC_ASSERT(width == other.width);
    val += other.val;
    return ClearUnusedBits();
}

SInt& SInt::operator+=(uint64_t v)
{
    val += v;
    return ClearUnusedBits();
}

SInt& SInt::operator-=(const SInt& other)
{
    CJC_ASSERT(width == other.width);
    val -= other.val;
    return ClearUnusedBits();
}

SInt& SInt::operator-=(uint64_t v)
{
    val -= v;
    return ClearUnusedBits();
}

SInt& SInt::operator<<=(unsigned count)
{
    CJC_ASSERT(count <= width);
    if (count == width) {
        val = 0;
    } else {
        val <<= count;
    }
    return ClearUnusedBits();
}

SInt& SInt::operator<<=(const SInt& count)
{
    *this <<= static_cast<unsigned>(count.GetULimitedValue(width));
    return *this;
}

SInt SInt::operator<<(const SInt& count) const
{
    return Shl(count);
}

SInt SInt::operator<<(unsigned count) const
{
    return Shl(count);
}

SInt SInt::AShr(unsigned count) const
{
    SInt ans{*this};
    ans.AShrInPlace(count);
    return ans;
}

void SInt::AShrInPlace(unsigned count)
{
    CJC_ASSERT(count <= width);
    int64_t sextVal = SignExtend64(val, width);
    if (count == width) {
        val = static_cast<uint64_t>(sextVal >> (BITS_PER_WORD - 1)); // fill with sign bit
    } else {
        val = static_cast<uint64_t>(sextVal >> count);
    }
    ClearUnusedBits();
}

SInt SInt::LShr(unsigned count) const
{
    SInt ans{*this};
    ans.LShrInPlace(count);
    return ans;
}

void SInt::LShrInPlace(unsigned count)
{
    CJC_ASSERT(count <= width);
    val = count == width ? 0 : val >> count;
}

SInt SInt::Ashr(const SInt& count) const
{
    SInt ans{*this};
    ans.AShrInPlace(static_cast<unsigned>(count.val));
    return ans;
}

SInt SInt::LShr(const SInt& count) const
{
    SInt ans{*this};
    ans.LShrInPlace(count);
    return ans;
}

void SInt::LShrInPlace(const SInt& count)
{
    return LShrInPlace(static_cast<unsigned>(count.GetULimitedValue(width)));
}

SInt SInt::Shl(unsigned count) const
{
    SInt ans{*this};
    ans <<= count;
    return ans;
}

SInt SInt::Shl(const SInt& count) const
{
    SInt ans{*this};
    ans <<= count;
    return ans;
}

SInt SInt::Concat(const SInt& v) const
{
    unsigned nw = width + v.width;
    switch (nw) {
        case IntWidth::I8:
        case IntWidth::I16:
        case IntWidth::I32:
        case IntWidth::I64:
            break;
        default:
            CJC_ABORT();
            break;
    }
    return {FromUnsigned(nw), (val << v.width) | v.val};
}

bool SInt::At(unsigned bit) const
{
    CJC_ASSERT(bit < width);
    return this->operator[](bit);
}

bool SInt::operator[](unsigned bit) const
{
    return (MaskBit(bit) & val) != 0;
}

bool SInt::operator==(const SInt& other) const
{
    CJC_ASSERT(width == other.width);
    return val == other.val;
}

bool SInt::operator==(uint64_t rhs) const
{
    return GetZExtValue() == rhs;
}

bool SInt::operator!=(const SInt& rhs) const
{
    return !(*this == rhs);
}

bool SInt::operator!=(uint64_t rhs) const
{
    return GetZExtValue() != rhs;
}

bool SInt::Ult(const SInt& rhs) const
{
    return UCmp(rhs) < 0;
}

bool SInt::Ult(uint64_t rhs) const
{
    return GetZExtValue() < rhs;
}

bool SInt::Slt(const SInt& rhs) const
{
    return SCmp(rhs) < 0;
}

bool SInt::Slt(int64_t rhs) const
{
    return GetSExtValue() < rhs;
}

bool SInt::Ule(const SInt& rhs) const
{
    return UCmp(rhs) <= 0;
}

bool SInt::Ule(uint64_t rhs) const
{
    return !Ugt(rhs);
}

bool SInt::Sle(const SInt& rhs) const
{
    return SCmp(rhs) <= 0;
}

bool SInt::Sle(int64_t rhs) const
{
    return !Sgt(rhs);
}

bool SInt::Ugt(const SInt& rhs) const
{
    return !Ule(rhs);
}

bool SInt::Ugt(uint64_t rhs) const
{
    return GetZExtValue() > rhs;
}

bool SInt::Sgt(const SInt& rhs) const
{
    return !Sle(rhs);
}

bool SInt::Sgt(int64_t rhs) const
{
    return GetSExtValue() > rhs;
}

bool SInt::Uge(const SInt& rhs) const
{
    return !Ult(rhs);
}

bool SInt::Uge(uint64_t rhs) const
{
    return !Ult(rhs);
}

bool SInt::Sge(const SInt& rhs) const
{
    return !Slt(rhs);
}

bool SInt::Sge(int64_t rhs) const
{
    return !Slt(rhs);
}

SInt SInt::UMin(const SInt& lhs, const SInt& rhs)
{
    return lhs.Ule(rhs) ? lhs : rhs;
}

SInt SInt::SMin(const SInt& lhs, const SInt& rhs)
{
    return lhs.Sle(rhs) ? lhs : rhs;
}

SInt SInt::UMax(const SInt& lhs, const SInt& rhs)
{
    return lhs.Ule(rhs) ? rhs : lhs;
}

SInt SInt::SMax(const SInt& lhs, const SInt& rhs)
{
    return lhs.Sle(rhs) ? rhs : lhs;
}

bool SInt::Intersects(const SInt& other) const
{
    CJC_ASSERT(width == other.width);
    return (val & other.val) != 0;
}

bool SInt::IsSubsetOf(const SInt& other) const
{
    CJC_ASSERT(width == other.width);
    return (val & ~other.val) != 0;
}

void SInt::SetAllBits()
{
    val = WORD_TYPE_MAX;
    ClearUnusedBits();
}

// Set the selected bit \p pos to one
void SInt::SetBit(unsigned pos)
{
    CJC_ASSERT(pos < width);
    val |= MaskBit(pos);
}

// Set from \p lo to \p hi bits to one
void SInt::SetBits(unsigned lo, unsigned hi)
{
    CJC_ASSERT(lo <= hi);
    CJC_ASSERT(hi <= width);
    if (lo == hi) {
        return;
    }
    WordType mask = WORD_TYPE_MAX >> (BITS_PER_WORD - hi + lo);
    mask <<= lo;
    val |= mask;
}

void SInt::SetLowBits(unsigned lo)
{
    SetBits(0, lo);
}

void SInt::SetHighBits(unsigned hi)
{
    SetBits(width - hi, width);
}

void SInt::ClearBit(unsigned pos)
{
    CJC_ASSERT(pos < width);
    val &= ~MaskBit(pos);
}

void SInt::FlipAllBits()
{
    val ^= WORD_TYPE_MAX;
    ClearUnusedBits();
}

// Negate this value in place
void SInt::Neg()
{
    FlipAllBits();
    ++*this;
}

unsigned SInt::ActiveBits() const
{
    return width - Clz();
}

unsigned SInt::SignificantBits() const
{
    return width - GetNumSignBits() + 1;
}

uint64_t SInt::GetZExtValue() const
{
    return val;
}

int64_t SInt::GetSExtValue() const
{
    return SignExtend64(val, width);
}

// Count leading zeroes
unsigned SInt::Clz() const
{
    return static_cast<unsigned>(::Cangjie::CHIR::Clz(val)) - (BITS_PER_WORD - width);
}

// Count leading ones
unsigned SInt::Clo() const
{
    CJC_ASSERT(width > 0);
    return static_cast<unsigned>(::Cangjie::CHIR::Clo(val << (BITS_PER_WORD - width)));
}

// Get the number of leading bits of this SInt that are equal to its sign bit
unsigned SInt::GetNumSignBits() const
{
    return IsNeg() ? Clo() : Clz();
}

// Count trailing zeroes
unsigned SInt::Ctz() const
{
    return std::min(static_cast<unsigned>(::Cangjie::CHIR::Ctz(val)), static_cast<unsigned>(width));
}

// Count trailing ones
unsigned SInt::Cto() const
{
    return static_cast<unsigned>(::Cangjie::CHIR::Cto(val));
}

// Count population
unsigned SInt::Popcnt() const
{
    return ::Cangjie::CHIR::Popcnt(val);
}

SInt SInt::GetBitsSetFrom(IntWidth w, unsigned lo)
{
    SInt res{w, 0u};
    res.SetBits(lo, w);
    return res;
}

uint64_t SInt::MaskBit(unsigned pos)
{
    return 1ULL << pos;
}

SInt& SInt::ClearUnusedBits()
{
    CJC_ASSERT(width > 0);
    unsigned wordBits = (width - 1) % BITS_PER_WORD + 1;
    uint64_t mask = WORD_TYPE_MAX >> (BITS_PER_WORD - wordBits);
    val &= mask;
    return *this;
}

void SInt::FromString(const std::string& str, Radix radix)
{
    size_t slen = str.size();
    auto p = str.begin();
    bool isNeg = *p == '-';
    if (*p == '-' || *p == '+') {
        ++p;
        --slen;
        CJC_ASSERT(slen != 0);
    }
    val = 0;
    unsigned shift = radix == Radix::R16 ? 4 : radix == Radix::R2 ? 1 : 0;
    for (auto e = str.end(); p != e; ++p) {
        unsigned digit = GetDigit(*p, radix);
        CJC_ASSERT(digit < static_cast<unsigned>(radix));
        if (slen > 1) {
            if (shift != 0) {
                val <<= shift;
            } else {
                val *= radix;
            }
        }
        val += digit;
    }
    if (isNeg) {
        Neg();
    }
}

decltype(std::setbase(0)) SIntFormatterBase::GetBaseManip() const
{
    if (radix == Radix::R2) {
        CJC_ABORT();
    }
    return std::setbase(static_cast<int>(radix));
}

std::ostream& operator<<(std::ostream& out, const SInt::Formatter& fmt)
{
    out << fmt.GetBaseManip();
    if ((out.flags() & std::ios_base::basefield) == std::ios_base::hex) {
        out << std::showbase;
    }
    if (fmt.asUnsigned) {
        return out << fmt.value.UVal();
    }
    return out << fmt.value.SVal();
}

SInt SInt::UDiv(const SInt& rhs) const
{
    CJC_ASSERT(width == rhs.width);
    return UDiv(rhs.val);
}

SInt SInt::UDiv(uint64_t rhs) const
{
    CJC_ASSERT(rhs != 0);
    return {width, val / rhs};
}

SInt SInt::SDiv(const SInt& rhs) const
{
    CJC_ASSERT(width == rhs.width);
    if (IsNeg()) {
        if (rhs.IsNeg()) {
            return (-*this).UDiv(-rhs);
        }
        return -(-*this).UDiv(rhs);
    }
    if (rhs.IsNeg()) {
        return -UDiv(-rhs);
    }
    return UDiv(rhs);
}

SInt SInt::SDiv(int64_t rhs) const
{
    CJC_ASSERT(rhs != 0);
    if (IsNeg()) {
        if (rhs < 0) {
            return (-*this).UDiv(static_cast<uint64_t>(-rhs));
        }
        return -(-*this).UDiv(static_cast<uint64_t>(rhs));
    }
    if (rhs < 0) {
        return -UDiv(static_cast<uint64_t>(-rhs));
    }
    return UDiv(static_cast<uint64_t>(rhs));
}

SInt SInt::URem(const SInt& rhs) const
{
    CJC_ASSERT(width == rhs.width);
    return URem(rhs.val);
}

SInt SInt::URem(uint64_t rhs) const
{
    CJC_ASSERT(rhs != 0);
    return {width, val % rhs};
}

SInt SInt::SRem(const SInt& rhs) const
{
    CJC_ASSERT(width == rhs.width);
    if (IsNeg()) {
        if (rhs.IsNeg()) {
            return -(-*this).URem(-rhs);
        }
        return -(-*this).URem(rhs);
    }
    if (rhs.IsNeg()) {
        return URem(-rhs);
    }
    return URem(rhs);
}

SInt SInt::SRem(int64_t rhs) const
{
    CJC_ASSERT(rhs != 0);
    if (IsNeg()) {
        if (rhs < 0) {
            return -(-*this).URem(static_cast<uint64_t>(-rhs));
        }
        return -(-*this).URem(static_cast<uint64_t>(rhs));
    }
    if (rhs < 0) {
        return URem(static_cast<uint64_t>(-rhs));
    }
    return URem(static_cast<uint64_t>(rhs));
}

int SInt::UCmp(const SInt& rhs) const
{
    CJC_ASSERT(width == rhs.width);
    return val < rhs.val ? -1 : val > rhs.val;
}

int SInt::SCmp(const SInt& rhs) const
{
    CJC_ASSERT(width == rhs.width);
    int64_t lext = GetSExtValue();
    int64_t rext = rhs.GetSExtValue();
    return lext < rext ? -1 : lext > rext;
}

SInt SInt::Trunc(IntWidth w) const
{
    CJC_ASSERT(w <= this->width);
    return {w, val};
}

SInt SInt::TruncUSat(IntWidth w) const
{
    CJC_ASSERT(w <= this->width);
    if (IsUIntN(w)) {
        return Trunc(w);
    }
    return UMaxValue(w);
}

SInt SInt::TruncSSat(IntWidth w) const
{
    if (IsSIntN(w)) {
        return Trunc(w);
    }
    return IsNeg() ? SMinValue(w) : SMaxValue(w);
}

SInt SInt::ZExt(IntWidth w) const
{
    CJC_ASSERT(this->width <= w);
    return {w, val};
}

SInt SInt::SExt(IntWidth w) const
{
    CJC_ASSERT(this->width <= w);
    return {w, static_cast<WordType>(SignExtend64(val, static_cast<unsigned>(this->width)))};
}

SInt SInt::SAddOvf(const SInt& rhs, bool& overflow) const
{
    SInt res{*this + rhs};
    overflow = IsNonNeg() == rhs.IsNonNeg() && res.IsNonNeg() != IsNonNeg();
    return res;
}

SInt SInt::UAddOvf(const SInt& rhs, bool& overflow) const
{
    SInt res{*this + rhs};
    overflow = res.Ult(rhs);
    return res;
}

SInt SInt::SSubOvf(const SInt& rhs, bool& overflow) const
{
    SInt res{*this - rhs};
    overflow = IsNonNeg() != rhs.IsNonNeg() && res.IsNonNeg() != IsNonNeg();
    return res;
}

SInt SInt::USubOvf(const SInt& rhs, bool& overflow) const
{
    SInt res{*this - rhs};
    overflow = res.Ugt(*this);
    return res;
}

SInt SInt::SMulOvf(const SInt& rhs, bool& overflow) const
{
    SInt res{*this * rhs};
    if (rhs != 0) {
        overflow = res.SDiv(rhs) != *this || (IsSMinValue() && rhs.IsAllOnes());
    } else {
        overflow = false;
    }
    return res;
}

SInt SInt::UMulOvf(const SInt& rhs, bool& overflow) const
{
    constexpr unsigned int k = 2;
    if (Clz() + rhs.Clz() + k <= width) {
        overflow = true;
        return *this * rhs;
    }
    SInt res = LShr(1) * rhs;
    overflow = res.IsNeg();
    res <<= 1;
    if ((*this)[0]) {
        res += rhs;
        if (res.Ult(rhs)) {
            overflow = true;
        }
    }
    return res;
}

SInt SInt::SShlOvf(const SInt& count, bool& overflow) const
{
    overflow = count.Uge(width);
    if (overflow) {
        return {width, 0};
    }
    if (IsNonNeg()) {
        overflow = count.Uge(Clz());
    } else {
        overflow = count.Uge(Clo());
    }
    return *this << count;
}

SInt SInt::UShlOvf(const SInt& count, bool& overflow) const
{
    overflow = count.Uge(width);
    if (overflow) {
        return {width, 0};
    }
    overflow = count.Ugt(Clz());
    return *this << count;
}

SInt SInt::SAddSat(const SInt& rhs) const
{
    bool overflow;
    SInt res = SAddOvf(rhs, overflow);
    if (!overflow) {
        return res;
    }
    return IsNeg() ? SMinValue(width) : SMaxValue(width);
}

SInt SInt::UAddSat(const SInt& rhs) const
{
    bool overflow;
    SInt res = UAddOvf(rhs, overflow);
    if (!overflow) {
        return res;
    }
    return UMaxValue(width);
}

SInt SInt::SSubSat(const SInt& rhs) const
{
    bool overflow;
    SInt res = SSubOvf(rhs, overflow);
    if (!overflow) {
        return res;
    }
    return IsNeg() ? SMinValue(width) : SMaxValue(width);
}

SInt SInt::USubSat(const SInt& rhs) const
{
    bool overflow;
    SInt res = USubOvf(rhs, overflow);
    return overflow ? UMinValue(width) : res;
}

SInt SInt::SMulSat(const SInt& rhs) const
{
    bool overflow;
    SInt res = SMulOvf(rhs, overflow);
    if (!overflow) {
        return res;
    }
    return IsNeg() != rhs.IsNeg() ? SMinValue(width) : SMaxValue(width);
}

SInt SInt::UMulSat(const SInt& rhs) const
{
    bool overflow;
    SInt res = UMulOvf(rhs, overflow);
    return overflow ? UMaxValue(width) : res;
}

SInt SInt::SDivSat(const SInt& rhs) const
{
    if (rhs.SVal() == -1 && this->IsSMinValue()) {
        return SMaxValue(width);
    }
    return SDiv(rhs);
}

SInt SInt::SShlSat(const SInt& rhs) const
{
    bool overflow;
    SInt res = SShlOvf(rhs, overflow);
    if (!overflow) {
        return res;
    }
    return IsNeg() ? SMinValue(width) : SMaxValue(width);
}

SInt SInt::UShlSat(const SInt& rhs) const
{
    bool overflow;
    SInt res = UShlOvf(rhs, overflow);
    return overflow ? UMaxValue(width) : res;
}

bool operator==(uint64_t v1, const SInt& v2)
{
    return v2 == v1;
}

bool operator!=(uint64_t v1, const SInt& v2)
{
    return v2 != v1;
}

SInt operator-(SInt v)
{
    v.Neg();
    return v;
}

SInt operator+(SInt a, const SInt& b)
{
    a += b;
    return a;
}

SInt operator+(const SInt& a, SInt&& b)
{
    b += a;
    return std::move(b);
}

SInt operator+(SInt a, uint64_t rhs)
{
    a += rhs;
    return a;
}

SInt operator+(uint64_t lhs, SInt b)
{
    b += lhs;
    return b;
}

SInt operator&(SInt a, const SInt& b)
{
    a &= b;
    return a;
}

SInt operator&(const SInt& a, SInt&& b)
{
    b &= a;
    return std::move(b);
}

SInt operator&(SInt a, uint64_t rhs)
{
    a &= rhs;
    return a;
}

SInt operator&(uint64_t lhs, SInt b)
{
    b &= lhs;
    return b;
}

SInt operator|(SInt a, const SInt& b)
{
    a |= b;
    return a;
}

SInt operator|(const SInt& a, SInt&& b)
{
    b |= a;
    return std::move(b);
}

SInt operator|(SInt a, uint64_t rhs)
{
    a |= rhs;
    return a;
}

SInt operator|(uint64_t lhs, SInt b)
{
    b |= lhs;
    return b;
}

SInt operator^(SInt a, const SInt& b)
{
    a ^= b;
    return a;
}

SInt operator^(const SInt& a, SInt&& b)
{
    b ^= a;
    return std::move(b);
}

SInt operator^(SInt a, uint64_t rhs)
{
    a ^= rhs;
    return a;
}

SInt operator^(uint64_t lhs, SInt b)
{
    b ^= lhs;
    return b;
}

SInt operator-(SInt a, const SInt& b)
{
    a -= b;
    return a;
}

SInt operator-(const SInt& a, SInt&& b)
{
    b.Neg();
    b += a;
    return std::move(b);
}

SInt operator-(SInt a, uint64_t rhs)
{
    a -= rhs;
    return a;
}

SInt operator-(uint64_t lhs, SInt b)
{
    b.Neg();
    b += lhs;
    return b;
}

SInt operator*(SInt a, uint64_t rhs)
{
    a *= rhs;
    return a;
}

SInt operator*(uint64_t lhs, SInt b)
{
    b *= lhs;
    return b;
}
} // namespace Cangjie::CHIR
