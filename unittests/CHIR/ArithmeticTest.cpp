// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include <gtest/gtest.h>

#include "cangjie/CHIR/Analysis/Arithmetic.h"

using namespace Cangjie::CHIR;

TEST(ArithTest, Log2)
{
    EXPECT_EQ(IsPowerOf2(0xffffffffffffffffull), false);
    EXPECT_EQ(IsPowerOf2(0ull), false);
    EXPECT_EQ(IsPowerOf2(0x0000000080000000ull), true);

    EXPECT_EQ(IsPowerOf2(0xffffu), false);
    EXPECT_EQ(IsPowerOf2(0x80000001ul), false);
    EXPECT_EQ(IsPowerOf2(0ul), false);
    EXPECT_EQ(IsPowerOf2(0x00010000ul), true);
}

TEST(ArithTest, Clz)
{
    EXPECT_EQ(Clz(0xffffffffffffffffull), 0);
    EXPECT_EQ(Clz(0x00075320ull), 45);
    EXPECT_EQ(Clz(0x0007532000000000ull), 13);
    EXPECT_EQ(Clz(0ull), 64);
    EXPECT_EQ(Clz(1ull), 63);
    EXPECT_EQ(Clz(1u), 31);
    EXPECT_EQ(Clz(0x00075320u), 13);
    EXPECT_EQ(Clz<uint16_t>(0x7fu), 9);
    EXPECT_EQ(Clz<uint16_t>(0xffffu), 0);
    EXPECT_EQ(Clz<uint16_t>(0u), 16);
    EXPECT_EQ(Clz(0u), 32);
}

TEST(ArithTest, Ctz)
{
    EXPECT_EQ(Ctz(0xffffffffffffffffull), 0);
    EXPECT_EQ(Ctz(0x00075320ull), 5);
    EXPECT_EQ(Ctz(0x0007532000000000ull), 37);
    EXPECT_EQ(Ctz(0ull), 64);
    EXPECT_EQ(Ctz(1ull), 0);
    EXPECT_EQ(Ctz(1u), 0);
    EXPECT_EQ(Ctz(0x00075320u), 5);
    EXPECT_EQ(Ctz<uint16_t>(0x7fu), 0);
    EXPECT_EQ(Ctz<uint16_t>(0x8000u), 15);
    EXPECT_EQ(Ctz<uint16_t>(0u), 16);
    EXPECT_EQ(Ctz(0u), 32);
}

TEST(ArithTest, Clo)
{
    EXPECT_EQ(Clo(0xffffffffffffffffull), 64);
    EXPECT_EQ(Clo(0x00075320ull), 0);
    EXPECT_EQ(Clo(0ull), 0);
    EXPECT_EQ(Clo(0x8000000000000000ull), 1);
    EXPECT_EQ(Clo(0x80000000u), 1);
    EXPECT_EQ(Clo(0xffff332011111111ull), 16);
    EXPECT_EQ(Clo<uint16_t>(0xf300u), 4);
    EXPECT_EQ(Clo<uint16_t>(0xffffu), 16);
    EXPECT_EQ(Clo<uint16_t>(0u), 0);
    EXPECT_EQ(Clo(0u), 0);
}

TEST(ArithTest, Cto)
{
    EXPECT_EQ(Cto(0xffffffffffffffffull), 64);
    EXPECT_EQ(Cto(0x00075320ull), 0);
    EXPECT_EQ(Cto(0x0007532000000000ull), 0);
    EXPECT_EQ(Cto(0ull), 0);
    EXPECT_EQ(Cto(1ull), 1);
    EXPECT_EQ(Cto(1u), 1);
    EXPECT_EQ(Cto(0x00075320u), 0);
    EXPECT_EQ(Cto<uint16_t>(0x7fu), 7);
    EXPECT_EQ(Cto<uint16_t>(0xffffu), 16);
    EXPECT_EQ(Cto<uint16_t>(0u), 0);
    EXPECT_EQ(Cto(0u), 0);
}

TEST(ArithTest, Popcnt)
{
    EXPECT_EQ(Popcnt(0xffffffffffffffffull), 64);
    EXPECT_EQ(Popcnt(0x00075320ull), 8);
    EXPECT_EQ(Popcnt(0x0007532000000000ull), 8);
    EXPECT_EQ(Popcnt(0ull), 0);
    EXPECT_EQ(Popcnt(1ull), 1);
    EXPECT_EQ(Popcnt(1u), 1);
    EXPECT_EQ(Popcnt(0x00075320u), 8);
    EXPECT_EQ(Popcnt<uint16_t>(0x7fu), 7);
    EXPECT_EQ(Popcnt<uint16_t>(0xffffu), 16);
    EXPECT_EQ(Popcnt<uint16_t>(0u), 0);
    EXPECT_EQ(Popcnt(0u), 0);
}