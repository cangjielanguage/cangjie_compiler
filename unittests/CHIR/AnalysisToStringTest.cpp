// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include <gtest/gtest.h>
#include <memory>

#include "cangjie/CHIR/Analysis/ConstAnalysis.h"
#include "cangjie/CHIR/Analysis/TypeAnalysis.h"
#include "cangjie/CHIR/Analysis/ValueRangeAnalysis.h"

using namespace Cangjie::CHIR;

TEST(CHIRAnalysisToStringTest, ValueTest)
{
    // abstract value
    AbstractObject abs("test");
    EXPECT_EQ(abs.ToString(), "test");
    Ref ref("1", false);
    EXPECT_EQ(ref.GetUniqueID(), "1");

    // const value
    ConstBoolVal a(true);
    EXPECT_EQ(a.ToString(), "true");
    ConstRuneVal b('c');
    EXPECT_EQ(b.ToString(), "99");
    ConstIntVal c(123);
    EXPECT_EQ(c.ToString(), "123");
    ConstFloatVal d(123);
    EXPECT_EQ(d.ToString(), "123.000000");
    ConstStrVal e("test");
    EXPECT_EQ(e.ToString(), "test");
    ConstUIntVal f(456);
    EXPECT_EQ(f.ToString(), "456");

    // type value
    std::unordered_map<unsigned int, std::string> NameMap;
    CHIRContext cctx(&NameMap);
    CHIRBuilder builder(cctx);
    TypeValue typeVal(DevirtualTyKind::SUBTYPE_OF, builder.GetInt32Ty());
    EXPECT_EQ(typeVal.ToString(), "{ SUBTYPE_OF, Int32 }");

    // range value
    BoolDomain tr{BoolDomain::True()};
    BoolRange br(tr);
    EXPECT_EQ(br.ToString(), "true");

    auto range = ConstantRange::From(RelationalOperation::EQ, {IntWidth::I32, 0u}, false);
    SIntDomain dom{range, false};
    SIntRange sintRange(dom);
    EXPECT_EQ(sintRange.ToString(), "0");
}

TEST(CHIRAnalysisToStringTest, SIntDomainFromRelations)
{
    // LT (unsigned) : values < 5 -> printed as "<=4"
    auto cr_lt = ConstantRange::From(RelationalOperation::LT, SInt(IntWidth::I32, 5), false);
    SIntDomain dlt{cr_lt, false};
    SIntRange rlt(dlt);
    EXPECT_EQ(rlt.ToString(), "|>=0,<=4|");

    // GT (signed) : values > 2 -> printed as ">=3"
    auto cr_gt = ConstantRange::From(RelationalOperation::GT, SInt(IntWidth::I32, 2), true);
    SIntRange rgt(SIntDomain{cr_gt, false});
    EXPECT_EQ(rgt.ToString(), "|>=3|");

    // NE (unsigned, small width) : not equal 1 -> wrapped format
    auto cr_ne = ConstantRange::From(RelationalOperation::NE, SInt(IntWidth::I8, 1), false);
    SIntRange rne(SIntDomain{cr_ne, false});
    EXPECT_EQ(rne.ToString(), "|<=0&>=2|");

    // Also test construction via FromNumeric factory yields same printed form
    auto d2 = SIntDomain::FromNumeric(RelationalOperation::LT, SInt(IntWidth::I32, 5), false);
    SIntRange r2(d2);
    EXPECT_EQ(r2.ToString(), "|<=4|");

    // Single value (signed): EQ -3 -> printed as "-3"
    auto cr_eq_signed = ConstantRange::From(
        RelationalOperation::EQ, SInt(IntWidth::I32, static_cast<uint64_t>(static_cast<int32_t>(-3))), true);
    SIntRange reqs(SIntDomain{cr_eq_signed, false});
    EXPECT_EQ(reqs.ToString(), "-3");

    // Single value (unsigned): EQ 7 -> printed as "7"
    auto cr_eq_unsigned = ConstantRange::From(RelationalOperation::EQ, SInt(IntWidth::I32, 7), false);
    SIntRange requ(SIntDomain{cr_eq_unsigned, true});
    EXPECT_EQ(requ.ToString(), "7");

    // Same bit-pattern printed signed vs unsigned (I8: 250 -> -6 when signed)
    auto cr_250 = ConstantRange::From(RelationalOperation::EQ, SInt(IntWidth::I8, 250u), false);
    SIntRange r250_signed(SIntDomain{cr_250, false});
    SIntRange r250_unsigned(SIntDomain{cr_250, true});
    EXPECT_EQ(r250_signed.ToString(), "-6");
    EXPECT_EQ(r250_unsigned.ToString(), "250");

    // any (full set) and empty set
    auto cr_any = ConstantRange::Full(IntWidth::I32);
    SIntRange rany(SIntDomain{cr_any, false});
    EXPECT_EQ(rany.ToString(), "|any|");

    auto cr_empty = ConstantRange::Empty(IntWidth::I32);
    SIntRange rempty(SIntDomain{cr_empty, false});
    EXPECT_EQ(rempty.ToString(), "||");
}