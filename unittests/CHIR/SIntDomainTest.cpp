// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include <gtest/gtest.h>

#include "cangjie/CHIR/Analysis/SIntDomain.h"
#include "cangjie/CHIR/IR/CHIRBuilder.h"

using namespace Cangjie::CHIR;

TEST(SIntDomainTest, TypeCastOverflowStrategies_BasicUnsignedToSigned)
{
	// unsigned 16-bit values in [0, 300) cast to signed 8-bit
	ConstantRange src{SInt(IntWidth::I16, 0u), SInt(IntWidth::I16, 300u)};
	SIntDomain dom{src, true};

	auto resThrow = ComputeTypeCastNumericBound(dom, IntWidth::I8, false, Cangjie::OverflowStrategy::THROWING);
	auto resSat = ComputeTypeCastNumericBound(dom, IntWidth::I8, false, Cangjie::OverflowStrategy::SATURATING);
	auto resWrap = ComputeTypeCastNumericBound(dom, IntWidth::I8, false, Cangjie::OverflowStrategy::WRAPPING);

	// All results should be non-empty (since src is non-empty)
	EXPECT_TRUE(resThrow.IsNotEmptySet());
	EXPECT_TRUE(resSat.IsNotEmptySet());
	EXPECT_TRUE(resWrap.IsNotEmptySet());

	// At least one strategy should produce a different numeric bound
	bool allEqual = (resThrow == resSat) && (resSat == resWrap);
	EXPECT_FALSE(allEqual);
}

TEST(SIntDomainTest, TypeCastOverflowStrategies_SignedToUnsigned_Negatives)
{
	// signed 16-bit values spanning negatives and positives: [-200, 200)
	SInt lower(IntWidth::I16, static_cast<uint64_t>(static_cast<int16_t>(-200)));
	SInt upper(IntWidth::I16, static_cast<uint64_t>(200));
	ConstantRange src{lower, upper};
	// source is signed
	SIntDomain dom{src, false};

	auto resThrow = ComputeTypeCastNumericBound(dom, IntWidth::I8, true, Cangjie::OverflowStrategy::THROWING);
	auto resSat = ComputeTypeCastNumericBound(dom, IntWidth::I8, true, Cangjie::OverflowStrategy::SATURATING);
	auto resWrap = ComputeTypeCastNumericBound(dom, IntWidth::I8, true, Cangjie::OverflowStrategy::WRAPPING);

	// none should be empty
	EXPECT_TRUE(resThrow.IsNotEmptySet());
	EXPECT_TRUE(resSat.IsNotEmptySet());
	EXPECT_TRUE(resWrap.IsNotEmptySet());

	// Expect at least a difference among strategies
	bool allEqual = (resThrow == resSat) && (resSat == resWrap);
	EXPECT_FALSE(allEqual);
}

TEST(SIntDomainTest, TypeCastOverflowStrategies_NoopWhenFits)
{
	// a range that fits into target width should be identical across strategies
	ConstantRange src{SInt(IntWidth::I16, 10u), SInt(IntWidth::I16, 20u)}; // [10,20)
	SIntDomain dom{src, false};

	auto a = ComputeTypeCastNumericBound(dom, IntWidth::I32, false, Cangjie::OverflowStrategy::THROWING);
	auto b = ComputeTypeCastNumericBound(dom, IntWidth::I32, false, Cangjie::OverflowStrategy::SATURATING);
	auto c = ComputeTypeCastNumericBound(dom, IntWidth::I32, false, Cangjie::OverflowStrategy::WRAPPING);

	EXPECT_EQ(a, b);
	EXPECT_EQ(b, c);
}

TEST(SIntDomainTest, BoolDomain_ApiCoverage)
{
	// basic constructors
	auto t = BoolDomain::True();
	auto f = BoolDomain::False();
	auto top = BoolDomain::Top();
	auto bot = BoolDomain::Bottom();

	EXPECT_TRUE(t.IsTrue());
	EXPECT_FALSE(t.IsFalse());
	EXPECT_FALSE(t.IsTop());
	EXPECT_FALSE(t.IsBottom());
	EXPECT_TRUE(t.IsSingleValue());
	EXPECT_TRUE(t.GetSingleValue());

	EXPECT_TRUE(f.IsFalse());
	EXPECT_FALSE(f.IsTrue());
	EXPECT_FALSE(f.IsTop());
	EXPECT_FALSE(f.IsBottom());
	EXPECT_TRUE(f.IsSingleValue());
	EXPECT_FALSE(f.GetSingleValue());

	EXPECT_TRUE(top.IsTop());
	EXPECT_FALSE(top.IsSingleValue());
	EXPECT_TRUE(bot.IsBottom());

	// FromBool
	auto fromTrue = BoolDomain::FromBool(true);
	auto fromFalse = BoolDomain::FromBool(false);
	EXPECT_TRUE(fromTrue.IsTrue());
	EXPECT_TRUE(fromFalse.IsFalse());

	// Logical operators (table-driven semantics)
	EXPECT_TRUE(LogicalAnd(t, t).IsTrue());
	EXPECT_TRUE(LogicalAnd(t, f).IsFalse());
	EXPECT_TRUE(LogicalOr(t, f).IsTrue());
	EXPECT_TRUE(LogicalOr(f, bot).IsBottom());

	// bitwise-like operators implemented as masks
	// operator& uses bitwise on internal code: True (2) & False (1) -> 0 (Bottom)
	EXPECT_TRUE((t & f).IsBottom());
	// operator| uses bitwise or: True|False -> Top
	EXPECT_TRUE((t | f).IsTop());

	// LogicalNot
	EXPECT_TRUE((!t).IsFalse());
	EXPECT_TRUE((!f).IsTrue());
	EXPECT_TRUE((!top).IsTop());

	// Union is alias to operator|
	EXPECT_TRUE(BoolDomain::Union(t, f).IsTop());

	// IsSame compares internal encoding
	EXPECT_TRUE(t.IsSame(BoolDomain::True()));
	EXPECT_FALSE(t.IsSame(f));

	// ostream output
	std::stringstream ss;
	ss << t << ',' << f << ',' << top << ',' << bot;
	EXPECT_EQ(ss.str(), std::string("true,false,<t,f>,<>"));
}

TEST(SIntDomainTest, NumericConversion_Api)
{
	// widening signed: sign-extend I8 [10,20) -> I32
	ConstantRange a{SInt(IntWidth::I8, 10), SInt(IntWidth::I8, 20)};
	auto ra = NumericConversion(a, IntWidth::I32, /*srcUnsigned=*/false, /*dstUnsigned=*/false,
								Cangjie::OverflowStrategy::THROWING);
	EXPECT_EQ(ra, a.SignExtend(IntWidth::I32));

	// widening unsigned: zero-extend
	ConstantRange au{SInt(IntWidth::I8, 10), SInt(IntWidth::I8, 20)};
	auto rau = NumericConversion(au, IntWidth::I32, /*srcUnsigned=*/true, /*dstUnsigned=*/true,
								 Cangjie::OverflowStrategy::THROWING);
	EXPECT_EQ(rau, au.ZeroExtend(IntWidth::I32));

	// narrowing without wrap: I16 [0,100) -> I8
	ConstantRange b{SInt(IntWidth::I16, 0u), SInt(IntWidth::I16, 100u)};
	auto nb = NumericConversion(b, IntWidth::I8, /*srcUnsigned=*/true, /*dstUnsigned=*/true,
								Cangjie::OverflowStrategy::THROWING);
	EXPECT_EQ(nb.Width(), IntWidth::I8);
	EXPECT_TRUE(nb.IsNotEmptySet());

	// unsigned->signed with different overflow strategies: expect at least one difference
	ConstantRange c{SInt(IntWidth::I16, 0u), SInt(IntWidth::I16, 300u)};
	auto t = NumericConversion(c, IntWidth::I8, /*srcUnsigned=*/true, /*dstUnsigned=*/false,
							  Cangjie::OverflowStrategy::THROWING);
	auto s = NumericConversion(c, IntWidth::I8, /*srcUnsigned=*/true, /*dstUnsigned=*/false,
							  Cangjie::OverflowStrategy::SATURATING);
	auto w = NumericConversion(c, IntWidth::I8, /*srcUnsigned=*/true, /*dstUnsigned=*/false,
							  Cangjie::OverflowStrategy::WRAPPING);
	EXPECT_TRUE(t.IsNotEmptySet());
	EXPECT_TRUE(s.IsNotEmptySet());
	EXPECT_TRUE(w.IsNotEmptySet());
	EXPECT_FALSE((t == s) && (s == w));

	// wrapped source handling: large unsigned I16 wrapped range
	ConstantRange wrap{SInt(IntWidth::I16, 65000u), SInt(IntWidth::I16, 200u)};
	auto wrapRes = NumericConversion(wrap, IntWidth::I8, /*srcUnsigned=*/true, /*dstUnsigned=*/true,
									 Cangjie::OverflowStrategy::THROWING);
	EXPECT_EQ(wrapRes.Width(), IntWidth::I8);
	EXPECT_TRUE(wrapRes.IsNotEmptySet());
}

TEST(SIntDomainTest, DomainLevel)
{
	std::unordered_map<unsigned int, std::string> nameMap;
    CHIRContext cctx(&nameMap);
    CHIRBuilder builder(cctx);
    Type* unitTy = builder.GetUnitTy();
    FuncType* funcTy = builder.GetType<FuncType>(std::vector<Type*>{}, unitTy);
    auto func = builder.CreateFunc({"test.cj", 1, {1, 1}, {1, 1}}, funcTy, "aaa", "bbb", "", "ccc");
    auto body = builder.CreateBlockGroup(*func);
    func->InitBody(*body);
    auto block = builder.CreateBlock(body);
    body->SetEntryBlock(block);
    auto ea = builder.CreateConstantExpression<IntLiteral>(builder.GetInt64Ty(), block, 0);
    auto eb = builder.CreateConstantExpression<IntLiteral>(builder.GetInt64Ty(), block, 0);
    auto ec = builder.CreateConstantExpression<IntLiteral>(builder.GetInt64Ty(), block, 0);
    auto a = ea->GetResult();
    auto b = eb->GetResult();
    auto c = ec->GetResult();
    SIntDomain s1{ConstantRange::Full(IntWidth::I32), false};
    SIntDomain s2{ConstantRange{{IntWidth::I32, 0x80000000}}, false};
    auto sub1{ComputeArithmeticBinop({s1, s2, a, b, ExprKind::SUB, Cangjie::OverflowStrategy::THROWING, false})};
    EXPECT_EQ(sub1.SymbolicBounds().Begin(), sub1.SymbolicBounds().End());
    auto sub2{ComputeArithmeticBinop({s2, s1, a, b, ExprKind::SUB, Cangjie::OverflowStrategy::WRAPPING, false})};
    EXPECT_EQ(sub2.SymbolicBounds().Begin(), sub2.SymbolicBounds().End());
    auto sub3{ComputeArithmeticBinop({s2, s2, a, b, ExprKind::SUB, Cangjie::OverflowStrategy::SATURATING, false})};
    EXPECT_EQ(sub3.SymbolicBounds().Begin(), sub3.SymbolicBounds().End());

    SIntDomain s3{ConstantRange::Full(IntWidth::I32), SIntDomain::SymbolicBoundsMap{{c, {{0u}, {10u}}}}, true};
    SIntDomain s4{ConstantRange::Full(IntWidth::I32), SIntDomain::SymbolicBoundsMap{{c, {{15u}, {20u}}}}, true};
    EXPECT_TRUE(ComputeRelIntBinop({s3, s4, a, b, ExprKind::NOTEQUAL, true}).IsTrue());
    EXPECT_TRUE(ComputeRelIntBinop({s3, s4, a, b, ExprKind::EQUAL, true}).IsFalse());
    SIntDomain s5{ConstantRange::Full(IntWidth::I32), SIntDomain::SymbolicBoundsMap{{c, {{5u}, {15u}}}}, true};
    EXPECT_TRUE(ComputeRelIntBinop({s3, s5, a, b, ExprKind::NOTEQUAL, true}).IsTop());
    EXPECT_TRUE(ComputeRelIntBinop({s3, s5, a, b, ExprKind::EQUAL, true}).IsTop());
}