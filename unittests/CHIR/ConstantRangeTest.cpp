// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include <gtest/gtest.h>
#include <climits>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "cangjie/CHIR/Analysis/ConstantRange.h"
#include "cangjie/CHIR/Analysis/SInt.h"

using namespace Cangjie::CHIR;

class ConstRangeTest : public ::testing::Test {
protected:
    static ConstantRange full;
    static ConstantRange empty;
    static ConstantRange one;
    static ConstantRange some;
    static ConstantRange wrap;
};

ConstantRange ConstRangeTest::full{IntWidth::I16, true};
ConstantRange ConstRangeTest::empty{IntWidth::I16, false};
ConstantRange ConstRangeTest::one{SInt{I16, 0xa}};
ConstantRange ConstRangeTest::some{SInt{I16, 0xa}, SInt{I16, 0xaaa}};
ConstantRange ConstRangeTest::wrap{SInt{I16, 0xaaa}, SInt{I16, 0xa}};

TEST_F(ConstRangeTest, Basics)
{
    // Full set
    EXPECT_TRUE(full.IsFullSet());
    EXPECT_FALSE(full.IsEmptySet());
    EXPECT_FALSE(full.IsSingleElement());

    // Empty set
    EXPECT_TRUE(empty.IsEmptySet());
    EXPECT_FALSE(empty.IsFullSet());
    EXPECT_FALSE(empty.IsSingleElement());

    // Single element
    EXPECT_TRUE(one.IsSingleElement());
    EXPECT_TRUE(one.Contains(SInt{I16, 0xa}));
    EXPECT_FALSE(one.IsEmptySet());
    EXPECT_FALSE(one.IsFullSet());

    // Some elements (non-full, non-empty, non-single)
    EXPECT_FALSE(some.IsFullSet());
    EXPECT_FALSE(some.IsEmptySet());
    EXPECT_FALSE(some.IsSingleElement());
    EXPECT_TRUE(some.Contains(SInt{I16, 0xa}));
    EXPECT_TRUE(some.Contains(SInt{I16, 0xaaa - 1}));
    EXPECT_FALSE(some.Contains(SInt{I16, 0xaaa}));

    // Wrapped range
    EXPECT_TRUE(wrap.IsWrappedSet());
    EXPECT_FALSE(wrap.IsEmptySet());
    EXPECT_FALSE(wrap.IsFullSet());
    EXPECT_TRUE(wrap.Contains(SInt{I16, 0xaaa}));
    EXPECT_TRUE(wrap.Contains(SInt{I16, 0x0}));
    EXPECT_TRUE(wrap.Contains(SInt{I16, 0xa - 1}));
    EXPECT_FALSE(wrap.Contains(SInt{I16, 0xa}));

    // IsNonTrivial API
    EXPECT_FALSE(full.IsNonTrivial());
    EXPECT_TRUE(empty.IsNonTrivial());
    EXPECT_TRUE(one.IsNonTrivial());
    EXPECT_TRUE(some.IsNonTrivial());
    EXPECT_TRUE(wrap.IsNonTrivial());

    // IsUpperWrapped API
    EXPECT_FALSE(full.IsUpperWrapped());
    EXPECT_FALSE(empty.IsUpperWrapped());
    EXPECT_FALSE(one.IsUpperWrapped());
    EXPECT_FALSE(some.IsUpperWrapped());
    EXPECT_TRUE(wrap.IsUpperWrapped());

    // Inverse API
    ConstantRange inv = wrap.Inverse();
    EXPECT_FALSE(inv.Contains(SInt{I16, 0xaaa}));
    EXPECT_FALSE(inv.Contains(SInt{I16, 0x0}));
    EXPECT_FALSE(inv.Contains(SInt{I16, 0xa - 1}));
    EXPECT_TRUE(inv.Contains(SInt{I16, 0xa}));
    EXPECT_TRUE(inv.Contains(SInt{I16, 0xa + 1}));

    // Negate API
    auto neg_val = [](int16_t v) { return SInt{I16, static_cast<uint16_t>(v)}; };
    ConstantRange neg_one = one.Negate();
    EXPECT_TRUE(neg_one.Contains(neg_val(-0xa)));
    EXPECT_TRUE(neg_one.IsSingleElement());

    ConstantRange neg_some = some.Negate();
    EXPECT_TRUE(neg_some.Contains(neg_val(-0xa)));
    EXPECT_TRUE(neg_some.Contains(neg_val(-(0xaaa - 1))));
    EXPECT_FALSE(neg_some.Contains(neg_val(-0xaaa)));

    ConstantRange neg_wrap = wrap.Negate();
    EXPECT_TRUE(neg_wrap.IsWrappedSet());
    EXPECT_TRUE(neg_wrap.Contains(neg_val(-0xaaa)));
    EXPECT_TRUE(neg_wrap.Contains(neg_val(-0xa + 1)));
    EXPECT_FALSE(neg_wrap.Contains(neg_val(-0xa)));

    // Check lower, upper, maxvalue, minvalue for 'one'
    EXPECT_EQ(one.Lower(), SInt(IntWidth::I16, 0xa));
    EXPECT_EQ(one.Upper(), SInt(IntWidth::I16, 0xb)); // single element range: [a, a+1)
    EXPECT_EQ(one.MaxValue(true), SInt(IntWidth::I16, 0xa)); // unsigned max
    EXPECT_EQ(one.MinValue(true), SInt(IntWidth::I16, 0xa)); // unsigned min
    EXPECT_EQ(one.MaxValue(false), SInt(IntWidth::I16, 0xa)); // signed max
    EXPECT_EQ(one.MinValue(false), SInt(IntWidth::I16, 0xa)); // signed min
}

TEST_F(ConstRangeTest, ZeroExtend)
{
    // single-element 8-bit value zero-extended to 16-bit
    ConstantRange r8(SInt(IntWidth::I8, 0xAB));
    ConstantRange r16 = r8.ZeroExtend(IntWidth::I16);
    EXPECT_TRUE(r16.IsSingleElement());
    EXPECT_EQ(r16.GetSingleElement(), SInt(IntWidth::I16, 0x00AB));

    // multi-element 8-bit range [1,3) -> zero-extend preserves bounds
    ConstantRange r8m(SInt(IntWidth::I8, 1), SInt(IntWidth::I8, 3));
    ConstantRange r16m = r8m.ZeroExtend(IntWidth::I16);
    EXPECT_FALSE(r16m.IsSingleElement());
    EXPECT_EQ(r16m.Lower(), SInt(IntWidth::I16, 1));
    EXPECT_EQ(r16m.Upper(), SInt(IntWidth::I16, 3));
}

TEST_F(ConstRangeTest, SignExtend)
{
    // single-element negative in I8 (-5 -> 0xFB). Sign extend to I16 -> 0xFFFB
    ConstantRange r8(SInt(IntWidth::I8, static_cast<uint8_t>(-5)));
    ConstantRange r16 = r8.SignExtend(IntWidth::I16);
    EXPECT_TRUE(r16.IsSingleElement());
    EXPECT_EQ(r16.GetSingleElement(), SInt(IntWidth::I16, static_cast<uint16_t>(static_cast<int16_t>(-5))));

    // positive value sign-extend: 0x7F -> 0x007F
    ConstantRange rp(SInt(IntWidth::I8, 0x7F));
    ConstantRange rps = rp.SignExtend(IntWidth::I16);
    EXPECT_EQ(rps.GetSingleElement(), SInt(IntWidth::I16, 0x007F));
}

TEST_F(ConstRangeTest, Truncate)
{
    // single element 0x0123 truncated to 8-bit -> 0x23
    ConstantRange r16(SInt(IntWidth::I16, 0x0123));
    ConstantRange r8 = r16.Truncate(IntWidth::I8);
    EXPECT_TRUE(r8.IsSingleElement());
    EXPECT_EQ(r8.GetSingleElement(), SInt(IntWidth::I8, 0x23));

    // another value: 0x00FF -> trunc to 8-bit 0xFF
    ConstantRange r16b(SInt(IntWidth::I16, 0x00FF));
    ConstantRange r8b = r16b.Truncate(IntWidth::I8);
    EXPECT_EQ(r8b.GetSingleElement(), SInt(IntWidth::I8, 0xFF));
}

TEST_F(ConstRangeTest, Add)
{
    ConstantRange a(SInt(IntWidth::I16, 10));
    ConstantRange b(SInt(IntWidth::I16, 3));
    ConstantRange res = a.Add(b);
    EXPECT_TRUE(res.IsSingleElement());
    EXPECT_EQ(res.GetSingleElement(), SInt(IntWidth::I16, 13));

    // range add: [10,12) + 3 -> [13,15)
    ConstantRange ar(SInt(IntWidth::I16, 10), SInt(IntWidth::I16, 12));
    ConstantRange br(SInt(IntWidth::I16, 3));
    ConstantRange rr = ar.Add(br);
    EXPECT_FALSE(rr.IsSingleElement());
    EXPECT_EQ(rr.Lower(), SInt(IntWidth::I16, 13));
    EXPECT_EQ(rr.Upper(), SInt(IntWidth::I16, 15));
}

TEST_F(ConstRangeTest, Sub)
{
    ConstantRange a(SInt(IntWidth::I16, 10));
    ConstantRange b(SInt(IntWidth::I16, 3));
    ConstantRange res = a.Sub(b);
    EXPECT_TRUE(res.IsSingleElement());
    EXPECT_EQ(res.GetSingleElement(), SInt(IntWidth::I16, 7));

    // single minus range: 10 - [1,3) -> [8,10)
    ConstantRange br(SInt(IntWidth::I16, 1), SInt(IntWidth::I16, 3));
    ConstantRange rr = a.Sub(br);
    EXPECT_EQ(rr.Lower(), SInt(IntWidth::I16, 8));
    EXPECT_EQ(rr.Upper(), SInt(IntWidth::I16, 10));
}

TEST_F(ConstRangeTest, SDiv_UDiv)
{
    // signed and unsigned division with non-zero divisor
    ConstantRange num(SInt(IntWidth::I16, 10));
    ConstantRange den(SInt(IntWidth::I16, 3));
    ConstantRange ures = num.UDiv(den);
    ConstantRange sres = num.SDiv(den);
    EXPECT_TRUE(ures.IsSingleElement());
    EXPECT_TRUE(sres.IsSingleElement());
    EXPECT_EQ(ures.GetSingleElement(), SInt(IntWidth::I16, 3));
    EXPECT_EQ(sres.GetSingleElement(), SInt(IntWidth::I16, 3));

    // numerator smaller than denominator -> result 0
    ConstantRange num2(SInt(IntWidth::I16, 2));
    ConstantRange den2(SInt(IntWidth::I16, 3));
    EXPECT_EQ(num2.UDiv(den2).GetSingleElement(), SInt(IntWidth::I16, 0));
    EXPECT_EQ(num2.SDiv(den2).GetSingleElement(), SInt(IntWidth::I16, 0));
}

TEST_F(ConstRangeTest, URem_SRem)
{
    ConstantRange num(SInt(IntWidth::I16, 10));
    ConstantRange den(SInt(IntWidth::I16, 3));
    ConstantRange urem = num.URem(den);
    ConstantRange srem = num.SRem(den);
    EXPECT_TRUE(urem.IsSingleElement());
    EXPECT_TRUE(srem.IsSingleElement());
    EXPECT_EQ(urem.GetSingleElement(), SInt(IntWidth::I16, 1));
    EXPECT_EQ(srem.GetSingleElement(), SInt(IntWidth::I16, 1));

    // divisible case: 6 % 3 == 0
    ConstantRange n2(SInt(IntWidth::I16, 6));
    ConstantRange d2(SInt(IntWidth::I16, 3));
    EXPECT_EQ(n2.URem(d2).GetSingleElement(), SInt(IntWidth::I16, 0));
    EXPECT_EQ(n2.SRem(d2).GetSingleElement(), SInt(IntWidth::I16, 0));
}

TEST_F(ConstRangeTest, UAddSat_SAddSat_USubSat_SSubSat)
{
    // Unsigned saturating add: max + 1 -> max
    ConstantRange umax(SInt(IntWidth::I16, 0xFFFF));
    ConstantRange one(SInt(IntWidth::I16, 1));
    ConstantRange ua = umax.UAddSat(one);
    EXPECT_TRUE(ua.IsSingleElement());
    EXPECT_EQ(ua.GetSingleElement(), SInt(IntWidth::I16, 0xFFFF));

    // Signed saturating add: max_signed + 1 -> max_signed (0x7FFF)
    ConstantRange smax(SInt(IntWidth::I16, 0x7FFF));
    ConstantRange sa = smax.SAddSat(one);
    EXPECT_TRUE(sa.IsSingleElement());
    EXPECT_EQ(sa.GetSingleElement(), SInt(IntWidth::I16, 0x7FFF));

    // Unsigned saturating sub: 0 - 1 -> 0
    ConstantRange zero(SInt(IntWidth::I16, 0));
    ConstantRange us = zero.USubSat(one);
    EXPECT_TRUE(us.IsSingleElement());
    EXPECT_EQ(us.GetSingleElement(), SInt(IntWidth::I16, 0));

    // Signed saturating sub: min_signed - 1 -> min_signed (0x8000)
    ConstantRange smin(SInt(IntWidth::I16, 0x8000));
    ConstantRange ss = smin.SSubSat(one);
    EXPECT_TRUE(ss.IsSingleElement());
    EXPECT_EQ(ss.GetSingleElement(), SInt(IntWidth::I16, 0x8000));

    // small non-saturating examples
    ConstantRange usmall(SInt(IntWidth::I16, 1));
    ConstantRange utwo(SInt(IntWidth::I16, 2));
    EXPECT_EQ(usmall.UAddSat(utwo).GetSingleElement(), SInt(IntWidth::I16, 3));

    ConstantRange ssmall(SInt(IntWidth::I16, 5));
    ConstantRange stwo(SInt(IntWidth::I16, 2));
    EXPECT_EQ(ssmall.SAddSat(stwo).GetSingleElement(), SInt(IntWidth::I16, 7));
}

TEST_F(ConstRangeTest, UMul_SMulSat)
{
    // Unsigned multiply (non-saturating) using single elements
    ConstantRange a(SInt(IntWidth::I16, 2));
    ConstantRange b(SInt(IntWidth::I16, 3));
    ConstantRange um = a.UMul(b);
    EXPECT_TRUE(um.IsSingleElement());
    EXPECT_EQ(um.GetSingleElement(), SInt(IntWidth::I16, 6));

    // Signed non-saturating multiply: 1000 * 2 = 2000
    ConstantRange ssmall2(SInt(IntWidth::I16, 1000));
    ConstantRange sms2 = ssmall2.SMulSat(ConstantRange(SInt(IntWidth::I16, 2)));
    EXPECT_EQ(sms2.GetSingleElement(), SInt(IntWidth::I16, 2000));

    // Signed multiply saturating: use large values that would saturate if overflowed
    ConstantRange sml(SInt(IntWidth::I16, 0x4000)); // 16384
    ConstantRange two(SInt(IntWidth::I16, 2));
    ConstantRange sms = sml.SMulSat(two);
    // 16384*2 = 32768 which overflows signed 16-bit; saturates to 0x7FFF
    EXPECT_TRUE(sms.IsSingleElement());
    EXPECT_EQ(sms.GetSingleElement(), SInt(IntWidth::I16, 0x7FFF));
}

TEST_F(ConstRangeTest, Negate_Simple)
{
    ConstantRange one(SInt(IntWidth::I16, 10));
    ConstantRange neg = one.Negate();
    EXPECT_TRUE(neg.IsSingleElement());
    // -10 in 16-bit two's complement
    EXPECT_EQ(neg.GetSingleElement(), SInt(IntWidth::I16, static_cast<uint16_t>(static_cast<int16_t>(-10))));

    // negate zero -> zero
    ConstantRange zero(SInt(IntWidth::I16, 0));
    ConstantRange neg0 = zero.Negate();
    EXPECT_TRUE(neg0.IsSingleElement());
    EXPECT_EQ(neg0.GetSingleElement(), SInt(IntWidth::I16, 0));
}

TEST_F(ConstRangeTest, Equality)
{
    EXPECT_EQ(full, full);
    EXPECT_EQ(empty, empty);
    EXPECT_EQ(one, one);
    EXPECT_EQ(some, some);
    EXPECT_EQ(wrap, wrap);
    EXPECT_NE(full, empty);
    EXPECT_NE(full, one);
    EXPECT_NE(empty, one);
    EXPECT_NE(one, some);
    EXPECT_NE(some, wrap);
}

TEST_F(ConstRangeTest, From)
{
    // Create ranges from relational operations on value 0xa (I16)
    SInt v{I16, 0xa};
    auto eq = ConstantRange::From(RelationalOperation::EQ, v, /*isSigned=*/false);
    auto ne = ConstantRange::From(RelationalOperation::NE, v, /*isSigned=*/false);
    auto ge = ConstantRange::From(RelationalOperation::GE, v, /*isSigned=*/false);
    auto gt = ConstantRange::From(RelationalOperation::GT, v, /*isSigned=*/false);
    auto le = ConstantRange::From(RelationalOperation::LE, v, /*isSigned=*/false);
    auto lt = ConstantRange::From(RelationalOperation::LT, v, /*isSigned=*/false);

    // EQ should be single-element containing v
    EXPECT_TRUE(eq.IsSingleElement());
    EXPECT_TRUE(eq.Contains(v));
    EXPECT_FALSE(eq.Contains(SInt{I16, 0xb}));

    // NE should not contain v but contain other values
    EXPECT_FALSE(ne.Contains(v));
    EXPECT_TRUE(ne.Contains(SInt{I16, 0xb}));

    // GE contains v and larger
    EXPECT_TRUE(ge.Contains(v));
    EXPECT_TRUE(ge.Contains(SInt{I16, 0xb}));
    EXPECT_FALSE(ge.Contains(SInt{I16, 0x9}));

    // GT does not contain v but contains greater values
    EXPECT_FALSE(gt.Contains(v));
    EXPECT_TRUE(gt.Contains(SInt{I16, 0xb}));

    // LE contains v and smaller
    EXPECT_TRUE(le.Contains(v));
    EXPECT_TRUE(le.Contains(SInt{I16, 0x9}));
    EXPECT_FALSE(le.Contains(SInt{I16, 0xb}));

    // LT contains strictly smaller
    EXPECT_TRUE(lt.Contains(SInt{I16, 0x9}));
    EXPECT_FALSE(lt.Contains(v));
}

TEST_F(ConstRangeTest, SignWrapped)
{
    // Existing fixtures: full, empty, one, some, wrap
    // Most of these should not be sign-wrapped
    EXPECT_FALSE(full.IsSignWrappedSet());
    EXPECT_FALSE(empty.IsSignWrappedSet());
    EXPECT_FALSE(one.IsSignWrappedSet());
    EXPECT_FALSE(some.IsSignWrappedSet());
    EXPECT_TRUE(wrap.IsSignWrappedSet());

    // Construct a range that is sign-wrapped: e.g. [-1, -2) in signed terms.
    // Use two's complement unsigned representations for -1 and -2 in 16-bit.
    ConstantRange signWrap(SInt{IntWidth::I16, 0xFFFF}, SInt{IntWidth::I16, 0xFFFE});
    EXPECT_TRUE(signWrap.IsSignWrappedSet());

    // Single small ranges should not be sign-wrapped
    ConstantRange smallSingle(SInt{IntWidth::I16, 0xFFFE}, SInt{IntWidth::I16, 0xFFFF});
    EXPECT_FALSE(smallSingle.IsSignWrappedSet());

    // Construct a range that crosses signed max -> signed min boundary,
    // e.g. [0x7FFF, 0x8001) contains {0x7FFF, 0x8000} which is {32767, -32768}
    // and should be sign-wrapped.
    ConstantRange crossSign(SInt{IntWidth::I16, 0x7FFF}, SInt{IntWidth::I16, 0x8001});
    EXPECT_TRUE(crossSign.IsSignWrappedSet());
}

TEST_F(ConstRangeTest, UpperWrapped)
{
    // Use the same example ranges as SignWrapped but test IsUpperWrapped
    EXPECT_FALSE(full.IsUpperWrapped());
    EXPECT_FALSE(empty.IsUpperWrapped());
    EXPECT_FALSE(one.IsUpperWrapped());
    EXPECT_FALSE(some.IsUpperWrapped());

    // the unsigned-wrapped fixture
    EXPECT_TRUE(wrap.IsUpperWrapped());

    // signWrap and cross-sign ranges should also report upper-wrapped when they wrap
    ConstantRange signWrap(SInt{IntWidth::I16, 0xFFFF}, SInt{IntWidth::I16, 0xFFFE});
    EXPECT_TRUE(signWrap.IsUpperWrapped());

    ConstantRange smallSingle(SInt{IntWidth::I16, 0xFFFE}, SInt{IntWidth::I16, 0xFFFF});
    EXPECT_FALSE(smallSingle.IsUpperWrapped());

    ConstantRange crossSign(SInt{IntWidth::I16, 0x7FFF}, SInt{IntWidth::I16, 0x8001});
    EXPECT_FALSE(crossSign.IsUpperWrapped());
}

TEST_F(ConstRangeTest, SingleElement)
{
    // existing fixtures
    EXPECT_FALSE(full.IsSingleElement());
    EXPECT_FALSE(empty.IsSingleElement());
    EXPECT_TRUE(one.IsSingleElement());
    EXPECT_FALSE(some.IsSingleElement());
    EXPECT_FALSE(wrap.IsSingleElement());

    // small single element near unsigned top
    ConstantRange smallSingle(SInt(IntWidth::I16, 0xFFFE), SInt(IntWidth::I16, 0xFFFF));
    EXPECT_TRUE(smallSingle.IsSingleElement());

    // sign-wrapped example should not be single element
    ConstantRange signWrap(SInt(IntWidth::I16, 0xFFFF), SInt(IntWidth::I16, 0xFFFE));
    EXPECT_FALSE(signWrap.IsSingleElement());

    // cross sign example spanning signed max/min is not single
    ConstantRange crossSign(SInt(IntWidth::I16, 0x7FFF), SInt(IntWidth::I16, 0x8001));
    EXPECT_FALSE(crossSign.IsSingleElement());
}

TEST_F(ConstRangeTest, SizeCompare)
{
    // empty < one
    EXPECT_TRUE(empty.IsSizeStrictlySmallerThan(one));
    EXPECT_FALSE(one.IsSizeStrictlySmallerThan(empty));

    // one < some
    EXPECT_TRUE(one.IsSizeStrictlySmallerThan(some));
    EXPECT_FALSE(some.IsSizeStrictlySmallerThan(one));

    // some < full
    EXPECT_TRUE(some.IsSizeStrictlySmallerThan(full));
    EXPECT_FALSE(full.IsSizeStrictlySmallerThan(some));

    // equal sizes -> false
    ConstantRange a(SInt{IntWidth::I16, 10}, SInt{IntWidth::I16, 12}); // {10,11}
    ConstantRange b(SInt{IntWidth::I16, 20}, SInt{IntWidth::I16, 22}); // {20,21}
    EXPECT_FALSE(a.IsSizeStrictlySmallerThan(b));
    EXPECT_FALSE(b.IsSizeStrictlySmallerThan(a));

    // small vs one
    ConstantRange small(SInt{IntWidth::I16, 5}, SInt{IntWidth::I16, 7}); // size 2
    EXPECT_TRUE(one.IsSizeStrictlySmallerThan(small));
    EXPECT_FALSE(small.IsSizeStrictlySmallerThan(one));
}

TEST_F(ConstRangeTest, MaxMinValues)
{
    // full unsigned bounds
    EXPECT_EQ(full.MaxValue(true), SInt(IntWidth::I16, 0xFFFF));
    EXPECT_EQ(full.MinValue(true), SInt(IntWidth::I16, 0x0));

    // full signed bounds
    EXPECT_EQ(full.MaxValue(false), SInt(IntWidth::I16, 0x7FFF));
    EXPECT_EQ(full.MinValue(false), SInt(IntWidth::I16, 0x8000));

    // one element
    EXPECT_EQ(one.MaxValue(true), SInt(IntWidth::I16, 0xa));
    EXPECT_EQ(one.MinValue(true), SInt(IntWidth::I16, 0xa));
    EXPECT_EQ(one.MaxValue(false), SInt(IntWidth::I16, 0xa));
    EXPECT_EQ(one.MinValue(false), SInt(IntWidth::I16, 0xa));

    // some: [0xa, 0xaaa)
    EXPECT_EQ(some.MaxValue(true), SInt(IntWidth::I16, 0xAAA - 1));
    EXPECT_EQ(some.MinValue(true), SInt(IntWidth::I16, 0xA));

    // wrapped: contains high and low values
    EXPECT_EQ(wrap.MaxValue(true), SInt(IntWidth::I16, 0xFFFF));
    EXPECT_EQ(wrap.MinValue(true), SInt(IntWidth::I16, 0x0));

    // cross sign example: contains 0x7FFF and 0x8000
    ConstantRange crossSign(SInt{IntWidth::I16, 0x7FFF}, SInt{IntWidth::I16, 0x8001});
    EXPECT_EQ(crossSign.MaxValue(false), SInt(IntWidth::I16, 0x7FFF));
    EXPECT_EQ(crossSign.MinValue(false), SInt(IntWidth::I16, 0x8000));
}

TEST_F(ConstRangeTest, Difference)
{
    // simple subtract leading segment: [10,20) - [10,12) => [12,20)
    ConstantRange a(SInt{IntWidth::I16, 10}, SInt{IntWidth::I16, 20});
    ConstantRange b(SInt{IntWidth::I16, 10}, SInt{IntWidth::I16, 12});
    ConstantRange r = a.Difference(b);
    EXPECT_EQ(r.Lower(), SInt(IntWidth::I16, 12));
    EXPECT_EQ(r.Upper(), SInt(IntWidth::I16, 20));
    EXPECT_TRUE(r.Contains(SInt(IntWidth::I16, 12)));
    EXPECT_FALSE(r.Contains(SInt(IntWidth::I16, 10)));

    // subtract trailing segment: [10,20) - [18,25) => [10,18)
    ConstantRange c(SInt{IntWidth::I16, 18}, SInt{IntWidth::I16, 25});
    ConstantRange r2 = a.Difference(c);
    EXPECT_EQ(r2.Lower(), SInt(IntWidth::I16, 10));
    EXPECT_EQ(r2.Upper(), SInt(IntWidth::I16, 18));
    EXPECT_TRUE(r2.Contains(SInt(IntWidth::I16, 10)));
    EXPECT_FALSE(r2.Contains(SInt(IntWidth::I16, 18)));

    // fixture-based: some ([0xa, 0xaaa)) - one([0xa]) => should not contain 0xa but contain 0xa+1
    ConstantRange res = some.Difference(one);
    EXPECT_FALSE(res.Contains(SInt{I16, 0xa}));
    EXPECT_TRUE(res.Contains(SInt{I16, 0xa + 1}));
}

TEST_F(ConstRangeTest, IntersectWith)
{
    // overlapping unwrapped ranges: [10,20) & [15,25) -> [15,20)
    ConstantRange a(SInt(IntWidth::I16, 10), SInt(IntWidth::I16, 20));
    ConstantRange b(SInt(IntWidth::I16, 15), SInt(IntWidth::I16, 25));
    ConstantRange i = a.IntersectWith(b);
    EXPECT_FALSE(i.IsEmptySet());
    EXPECT_EQ(i.Lower(), SInt(IntWidth::I16, 15));
    EXPECT_EQ(i.Upper(), SInt(IntWidth::I16, 20));

    // disjoint unwrapped ranges -> empty
    ConstantRange d1(SInt(IntWidth::I16, 10), SInt(IntWidth::I16, 12));
    ConstantRange d2(SInt(IntWidth::I16, 20), SInt(IntWidth::I16, 22));
    ConstantRange id = d1.IntersectWith(d2);
    EXPECT_TRUE(id.IsEmptySet());

    // wrapped vs small unwrapped: [250,5) intersects [0,3) -> [0,3)
    ConstantRange wrap2(SInt(IntWidth::I16, 250), SInt(IntWidth::I16, 5));
    ConstantRange small(SInt(IntWidth::I16, 0), SInt(IntWidth::I16, 3));
    ConstantRange iw = wrap2.IntersectWith(small);
    EXPECT_FALSE(iw.IsEmptySet());
    EXPECT_EQ(iw.Lower(), SInt(IntWidth::I16, 0));
    EXPECT_EQ(iw.Upper(), SInt(IntWidth::I16, 3));

    // both wrapped and overlapping: [250,5) & [253,2) -> [253,2)
    ConstantRange w1(SInt(IntWidth::I16, 250), SInt(IntWidth::I16, 5));
    ConstantRange w2(SInt(IntWidth::I16, 253), SInt(IntWidth::I16, 2));
    ConstantRange iw2 = w1.IntersectWith(w2);
    EXPECT_FALSE(iw2.IsEmptySet());
    EXPECT_TRUE(iw2.IsWrappedSet());
    // intersection should cover from 253 to 2
    EXPECT_EQ(iw2.Lower(), SInt(IntWidth::I16, 253));
    EXPECT_EQ(iw2.Upper(), SInt(IntWidth::I16, 2));
}

TEST_F(ConstRangeTest, UnionWith)
{
    // overlapping unwrapped ranges: [10,20) & [15,25) -> [10,25)
    ConstantRange a(SInt(IntWidth::I16, 10), SInt(IntWidth::I16, 20));
    ConstantRange b(SInt(IntWidth::I16, 15), SInt(IntWidth::I16, 25));
    ConstantRange u = a.UnionWith(b);
    EXPECT_EQ(u.Lower(), SInt(IntWidth::I16, 10));
    EXPECT_EQ(u.Upper(), SInt(IntWidth::I16, 25));

    // disjoint unwrapped ranges -> covering range [10,22)
    ConstantRange d1(SInt(IntWidth::I16, 10), SInt(IntWidth::I16, 12));
    ConstantRange d2(SInt(IntWidth::I16, 20), SInt(IntWidth::I16, 22));
    ConstantRange ud = d1.UnionWith(d2);
    EXPECT_EQ(ud.Lower(), SInt(IntWidth::I16, 10));
    EXPECT_EQ(ud.Upper(), SInt(IntWidth::I16, 22));

    // wrapped vs small unwrapped: union should equal the wrapped range
    ConstantRange wrap2(SInt(IntWidth::I16, 250), SInt(IntWidth::I16, 5));
    ConstantRange small(SInt(IntWidth::I16, 0), SInt(IntWidth::I16, 3));
    ConstantRange uw = wrap2.UnionWith(small);
    EXPECT_EQ(uw, wrap2);

    // both wrapped and overlapping: union equals the larger wrapped range
    ConstantRange w1(SInt(IntWidth::I16, 250), SInt(IntWidth::I16, 5));
    ConstantRange w2(SInt(IntWidth::I16, 253), SInt(IntWidth::I16, 2));
    ConstantRange uw2 = w1.UnionWith(w2);
    EXPECT_EQ(uw2.Lower(), SInt(IntWidth::I16, 250));
    EXPECT_EQ(uw2.Upper(), SInt(IntWidth::I16, 5));
}

