// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "gtest/gtest.h"
#include "cangjie/Utils/Unicode.h"

#include "NormalisationTestData.generated.inc"

using namespace Cangjie;
using namespace Unicode;

TEST(UnicodeTest, GeneratedNFCTests)
{
    int testDataLen{sizeof(NORMALISATION_TEST_DATA) / sizeof(NormalisationTest)};
    for (int i{0}; i < testDataLen; ++i) {
        auto f = CanonicalRecompose(reinterpret_cast<const UTF8*>(NORMALISATION_TEST_DATA[i].source.begin()),
            reinterpret_cast<const UTF8*>(NORMALISATION_TEST_DATA[i].source.end()));
        EXPECT_EQ(f, NORMALISATION_TEST_DATA[i].nfc);
    }
}
