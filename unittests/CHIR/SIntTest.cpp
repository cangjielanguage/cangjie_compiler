// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include <gtest/gtest.h>

#include "cangjie/CHIR/Analysis/SInt.h"

using namespace Cangjie::CHIR;

TEST(SIntTest, AddSubMul)
{
    // positive operands
    SInt a(IntWidth::I64, 10);
    SInt b(IntWidth::I64, 3);
    SInt sum = a + b;
    SInt diff = a - b;
    SInt prod = a * b;
    EXPECT_EQ(sum.UVal(), 13ull);
    EXPECT_EQ(diff.UVal(), 7ull);
    EXPECT_EQ(prod.UVal(), 30ull);

    // addition with negative operand: 10 + (-5) == 5
    SInt neg5(IntWidth::I64, static_cast<uint64_t>(static_cast<int64_t>(-5)));
    SInt sum2 = a + neg5;
    EXPECT_EQ(sum2.SVal(), 5ll);
    // multiplication by zero
    SInt zero(IntWidth::I64, 0);
    EXPECT_EQ((a * zero).UVal(), 0ull);
}

TEST(SIntTest, OverflowAndSaturate)
{
    // signed overflow check for addition
    SInt smax = SInt::SMaxValue(IntWidth::I64);
    bool ovf = false;
    SInt one(IntWidth::I64, 1);
    SInt sum = smax.SAddOvf(one, ovf);
    EXPECT_TRUE(ovf);
    (void)sum; // result is undefined on overflow; just ensure variable is used

    // signed saturating add should clamp to SMax
    SInt sat = smax.SAddSat(one);
    EXPECT_EQ(sat, SInt::SMaxValue(IntWidth::I64));

    // unsigned saturating add
    SInt umax = SInt::UMaxValue(IntWidth::I64);
    SInt usat = umax.UAddSat(one);
    EXPECT_EQ(usat, SInt::UMaxValue(IntWidth::I64));
}

TEST(SIntTest, MulOverflow)
{
    // signed multiply overflow detection
    SInt large(IntWidth::I64, (uint64_t)1 << 62); // big value
    bool ovf = false;
    SInt prod = large.SMulOvf(SInt(IntWidth::I64, 4), ovf);
    EXPECT_TRUE(ovf);
    (void)prod;

    // unsigned multiply overflow detection
    SInt ularge(IntWidth::I64, (uint64_t)1 << 62);
    bool uovf = false;
    SInt uprod = ularge.UMulOvf(SInt(IntWidth::I64, 4), uovf);
    EXPECT_TRUE(uovf);
    (void)uprod;
}

TEST(SIntTest, DivRemAndCompare)
{
    SInt a(IntWidth::I64, 10);
    SInt b(IntWidth::I64, 3);
    EXPECT_EQ(a.UDiv(b), SInt(IntWidth::I64, 3));
    EXPECT_EQ(a.SDiv(b), SInt(IntWidth::I64, 3));
    EXPECT_EQ(a.URem(b), SInt(IntWidth::I64, 1));
    EXPECT_EQ(a.SRem(b), SInt(IntWidth::I64, 1));

    // comparisons unsigned and signed
    EXPECT_TRUE(SInt(IntWidth::I64, 2).Ult(SInt(IntWidth::I64, 3)));
    EXPECT_TRUE(SInt(IntWidth::I64, -2).Slt(SInt(IntWidth::I64, -1)));
}

TEST(SIntTest, ShiftsAndBits)
{
    SInt v(IntWidth::I64, 4);
    EXPECT_EQ(v.Shl(1), SInt(IntWidth::I64, 8));

    // logical right shift
    EXPECT_EQ(v.LShr(1), SInt(IntWidth::I64, 2));

    // compound left-shift operator: <<= with unsigned
    SInt s(IntWidth::I64, 3);
    s <<= 2u;
    EXPECT_EQ(s, SInt(IntWidth::I64, 12));

    // compound left-shift operator: <<= with SInt count
    SInt s2(IntWidth::I64, 5);
    SInt cnt(IntWidth::I64, 1);
    s2 <<= cnt;
    EXPECT_EQ(s2, SInt(IntWidth::I64, 10));

    SInt neg(IntWidth::I64, static_cast<uint64_t>(static_cast<int64_t>(-4)));
    EXPECT_EQ(neg.AShr(1).SVal(), static_cast<int64_t>(-2));

    // logical shift of all-ones should clear top bit
    SInt allones(IntWidth::I64, static_cast<uint64_t>(static_cast<int64_t>(-1)));
    SInt lshr = allones.LShr(1);
    EXPECT_EQ(lshr.UVal(), (static_cast<uint64_t>(~0ull) >> 1));

    // bitwise ops
    SInt x(IntWidth::I64, 0xF0F0);
    SInt y(IntWidth::I64, 0x0FF0);
    EXPECT_EQ((x & y).UVal(), (0xF0F0 & 0x0FF0));
    EXPECT_EQ((x | y).UVal(), (0xF0F0 | 0x0FF0));
    EXPECT_EQ((x ^ y).UVal(), (0xF0F0 ^ 0x0FF0));

    // non-member operator<< (shift) with unsigned and SInt
    EXPECT_EQ((SInt(IntWidth::I64, 3) << 2u), SInt(IntWidth::I64, 12));
    EXPECT_EQ((SInt(IntWidth::I64, 3) << SInt(IntWidth::I64, 2)), SInt(IntWidth::I64, 12));

    // compound bitwise assignment operators
    SInt zb(IntWidth::I64, 0xF0F0);
    zb &= SInt(IntWidth::I64, 0x0FF0);
    EXPECT_EQ(zb.UVal(), (0xF0F0 & 0x0FF0));

    SInt zb2(IntWidth::I64, 0x100);
    zb2 |= 0x3u;
    EXPECT_EQ(zb2.UVal(), 0x103u);

    SInt zx(IntWidth::I64, 0xFF);
    zx ^= SInt(IntWidth::I64, 0xF0);
    EXPECT_EQ(zx.UVal(), (0xFF ^ 0xF0));

    // non-member multiply with uint64_t
    EXPECT_EQ((SInt(IntWidth::I64, 4) * 3u).UVal(), 12ull);
}

TEST(SIntTest, IsPredicates)
{
    // zero/one
    SInt z(IntWidth::I64, 0);
    SInt one(IntWidth::I64, 1);
    EXPECT_TRUE(z.IsZero());
    EXPECT_FALSE(z.IsOne());
    EXPECT_TRUE(one.IsOne());
    EXPECT_FALSE(one.IsZero());
    EXPECT_TRUE(one.ToBool());

    // sign tests
    SInt neg(IntWidth::I64, static_cast<uint64_t>(static_cast<int64_t>(-5)));
    EXPECT_TRUE(neg.IsNeg());
    EXPECT_FALSE(neg.IsNonNeg());
    EXPECT_FALSE(neg.IsPositive());
    EXPECT_TRUE(neg.IsSignBitSet());
    EXPECT_FALSE(neg.IsSignBitClear());

    SInt pos(IntWidth::I64, 5);
    EXPECT_FALSE(pos.IsNeg());
    EXPECT_TRUE(pos.IsNonNeg());
    EXPECT_TRUE(pos.IsPositive());
    EXPECT_FALSE(pos.IsSignBitSet());
    EXPECT_TRUE(pos.IsSignBitClear());

    // all ones / max/min checks
    SInt umax = SInt::UMaxValue(IntWidth::I64);
    SInt smax = SInt::SMaxValue(IntWidth::I64);
    SInt smin = SInt::SMinValue(IntWidth::I64);
    EXPECT_TRUE(umax.IsUMaxValue());
    EXPECT_FALSE(umax.IsSMaxValue());
    EXPECT_TRUE(smax.IsSMaxValue());
    EXPECT_TRUE(smin.IsSMinValue());
    EXPECT_TRUE(umax.IsAllOnes());

    // IsOneBitSet
    SInt eight(IntWidth::I64, 8);
    EXPECT_TRUE(eight.IsOneBitSet(3));
    EXPECT_FALSE(eight.IsOneBitSet(2));

    // IsMask (lower n bits set)
    SInt mask4(IntWidth::I64, 0xF);
    EXPECT_TRUE(mask4.IsMask(4));
    EXPECT_FALSE(mask4.IsMask(3));

    // IsUIntN / IsSIntN
    EXPECT_TRUE(SInt(IntWidth::I64, 0xFF).IsUIntN(8));
    EXPECT_FALSE(SInt(IntWidth::I64, 0x1FF).IsUIntN(8));
    EXPECT_TRUE(SInt(IntWidth::I64, static_cast<uint64_t>(static_cast<int64_t>(-2))).IsSIntN(8));

    // power-of-two checks
    EXPECT_TRUE(SInt(IntWidth::I64, 8).IsPowerOf2());
    EXPECT_FALSE(SInt(IntWidth::I64, 12).IsPowerOf2());
    EXPECT_TRUE(SInt(IntWidth::I64, static_cast<uint64_t>(static_cast<int64_t>(-8))).IsNegatedPowerOf2());

    // sign mask detection
    SInt signmask = SInt::GetOneBitSet(IntWidth::I64, 63);
    EXPECT_TRUE(signmask.IsSignMask());
}

TEST(SIntTest, TruncAndExt)
{
    SInt wide(IntWidth::I64, 0x1234567890ull);
    SInt t = wide.Trunc(IntWidth::I32);
    EXPECT_EQ(t.UVal(), static_cast<uint64_t>(0x1234567890ull & 0xFFFFFFFFull));

    SInt small(IntWidth::I8, static_cast<uint64_t>(static_cast<int8_t>(-5)));
    SInt sext = small.SExt(IntWidth::I16);
    SInt zext = small.ZExt(IntWidth::I16);
    EXPECT_EQ(sext.SVal(), static_cast<int64_t>(-5));
    EXPECT_EQ(zext.UVal(), static_cast<uint64_t>(static_cast<uint8_t>(-5)));
}

TEST(SIntTest, MulAndDiv)
{
    // unsigned greater-or-equal
    SInt a(IntWidth::I64, 10);
    SInt b(IntWidth::I64, 3);
    EXPECT_TRUE(a.Uge(b));
    EXPECT_FALSE(b.Uge(a));

    // unsigned division and remainder
    EXPECT_EQ(a.UDiv(b), SInt(IntWidth::I64, 3));
    EXPECT_EQ(a.URem(b), SInt(IntWidth::I64, 1));

    // signed division and remainder with negative numerator
    SInt neg10(IntWidth::I64, static_cast<uint64_t>(static_cast<int64_t>(-10)));
    SInt sdiv = neg10.SDiv(b);
    SInt srem = neg10.SRem(b);
    EXPECT_EQ(sdiv.SVal(), static_cast<int64_t>(-3));
    // remainder: -10 % 3 == -1
    EXPECT_EQ(srem.SVal(), static_cast<int64_t>(-1));

    // unsigned greater-than uses unsigned interpretation: -1 (all ones) > 1
    SInt minus1(IntWidth::I64, static_cast<uint64_t>(static_cast<int64_t>(-1)));
    EXPECT_TRUE(minus1.Ugt(SInt(IntWidth::I64, 1)));
}

TEST(SIntTest, SaturatingOps)
{
    SInt one(IntWidth::I64, 1);

    // Signed add saturating
    SInt smax = SInt::SMaxValue(IntWidth::I64);
    EXPECT_EQ(smax.SAddSat(one), SInt::SMaxValue(IntWidth::I64));
    SInt small1(IntWidth::I64, 5);
    SInt small2(IntWidth::I64, 2);
    EXPECT_EQ(small1.SAddSat(small2), SInt(IntWidth::I64, 7));

    // Unsigned add saturating
    SInt umax = SInt::UMaxValue(IntWidth::I64);
    EXPECT_EQ(umax.UAddSat(one), SInt::UMaxValue(IntWidth::I64));
    EXPECT_EQ(SInt(IntWidth::I64, 1).UAddSat(SInt(IntWidth::I64, 2)), SInt(IntWidth::I64, 3));

    // Signed sub saturating
    SInt smin = SInt::SMinValue(IntWidth::I64);
    EXPECT_EQ(smin.SSubSat(one), SInt::SMinValue(IntWidth::I64));
    EXPECT_EQ(SInt(IntWidth::I64, 5).SSubSat(SInt(IntWidth::I64, 2)), SInt(IntWidth::I64, 3));

    // Unsigned sub saturating
    EXPECT_EQ(SInt(IntWidth::I64, 0).USubSat(one), SInt(IntWidth::I64, 0));
    EXPECT_EQ(SInt(IntWidth::I64, 5).USubSat(SInt(IntWidth::I64, 2)), SInt(IntWidth::I64, 3));

    // Signed multiply saturating
    EXPECT_EQ(SInt(IntWidth::I64, 1000).SMulSat(SInt(IntWidth::I64, 2)), SInt(IntWidth::I64, 2000));
    EXPECT_EQ(SInt(IntWidth::I64, (int64_t)0x4000000000000000ull).SMulSat(SInt(IntWidth::I64, 4)), SInt::SMaxValue(IntWidth::I64));

    // Unsigned multiply saturating
    EXPECT_EQ(SInt(IntWidth::I64, 2).UMulSat(SInt(IntWidth::I64, 3)), SInt(IntWidth::I64, 6));
    EXPECT_EQ(SInt::UMaxValue(IntWidth::I64).UMulSat(SInt(IntWidth::I64, 2)), SInt::UMaxValue(IntWidth::I64));

    // Signed divide saturating: SMin / -1 -> SMax (overflow case)
    SInt neg1(IntWidth::I64, static_cast<uint64_t>(static_cast<int64_t>(-1)));
    EXPECT_EQ(SInt::SMinValue(IntWidth::I64).SDivSat(neg1), SInt::SMaxValue(IntWidth::I64));

    // Shift saturating
    EXPECT_EQ(SInt(IntWidth::I64, 2).SShlSat(SInt(IntWidth::I64, 2)), SInt(IntWidth::I64, 8));
    EXPECT_EQ(SInt::SMaxValue(IntWidth::I64).SShlSat(SInt(IntWidth::I64, 1)), SInt::SMaxValue(IntWidth::I64));
    EXPECT_EQ(SInt(IntWidth::I64, 2).UShlSat(SInt(IntWidth::I64, 2)), SInt(IntWidth::I64, 8));
    EXPECT_EQ(SInt::UMaxValue(IntWidth::I64).UShlSat(SInt(IntWidth::I64, 1)), SInt::UMaxValue(IntWidth::I64));
}

TEST(SIntTest, ConcatAndIntersects)
{
    // concat two I8 into I16: 0x12 << 8 | 0x34 == 0x1234
    SInt a8(IntWidth::I8, 0x12);
    SInt b8(IntWidth::I8, 0x34);
    SInt c16 = a8.Concat(b8);
    EXPECT_EQ(c16, SInt(IntWidth::I16, 0x1234));

    // concat I16 + I16 -> I32
    SInt a16(IntWidth::I16, 0xABCD);
    SInt b16(IntWidth::I16, 0x1357);
    SInt c32 = a16.Concat(b16);
    EXPECT_EQ(c32, SInt(IntWidth::I32, (static_cast<uint32_t>(0xABCD) << 16) | 0x1357));

    // Intersects: check overlapping bits
    SInt m1(IntWidth::I16, 0x0F0F);
    SInt m2(IntWidth::I16, 0x000F);
    SInt m3(IntWidth::I16, 0xF000);
    EXPECT_TRUE(m1.Intersects(m2));
    EXPECT_FALSE(m1.Intersects(m3));

    // Intersects requires same width; test equal-width false case
    SInt m8(IntWidth::I8, 0x0F);
    SInt m8b(IntWidth::I8, 0xF0);
    EXPECT_FALSE(m8.Intersects(m8b));
}

TEST(SIntTest, IncDecCompoundAssignAndAssign)
{
    // basic assignment
    SInt a(IntWidth::I16, 5);
    SInt b(IntWidth::I16, 10);
    a = b;
    EXPECT_EQ(a, SInt(IntWidth::I16, 10));

    // pre-increment
    SInt c(IntWidth::I16, 1);
    SInt pre = ++c;
    EXPECT_EQ(pre, SInt(IntWidth::I16, 2));
    EXPECT_EQ(c, SInt(IntWidth::I16, 2));

    // post-increment
    SInt d(IntWidth::I16, 2);
    SInt post = d++;
    EXPECT_EQ(post, SInt(IntWidth::I16, 2));
    EXPECT_EQ(d, SInt(IntWidth::I16, 3));

    // pre-decrement
    SInt e(IntWidth::I16, 3);
    SInt predec = --e;
    EXPECT_EQ(predec, SInt(IntWidth::I16, 2));
    EXPECT_EQ(e, SInt(IntWidth::I16, 2));

    // post-decrement
    SInt f(IntWidth::I16, 4);
    SInt postdec = f--;
    EXPECT_EQ(postdec, SInt(IntWidth::I16, 4));
    EXPECT_EQ(f, SInt(IntWidth::I16, 3));

    // operator+= with SInt and integer
    SInt g(IntWidth::I16, 5);
    g += SInt(IntWidth::I16, 3);
    EXPECT_EQ(g, SInt(IntWidth::I16, 8));
    g += 2u;
    EXPECT_EQ(g, SInt(IntWidth::I16, 10));

    // operator*= with SInt and integer
    SInt h(IntWidth::I16, 6);
    h *= SInt(IntWidth::I16, 2);
    EXPECT_EQ(h, SInt(IntWidth::I16, 12));
    h *= 2u;
    EXPECT_EQ(h, SInt(IntWidth::I16, 24));

    // chain assignment works (assignment returns reference)
    SInt i(IntWidth::I16, 1);
    SInt j(IntWidth::I16, 2);
    (i = j) = SInt(IntWidth::I16, 7);
    EXPECT_EQ(i, SInt(IntWidth::I16, 7));
    EXPECT_EQ(j, SInt(IntWidth::I16, 2)); // j unchanged
}

