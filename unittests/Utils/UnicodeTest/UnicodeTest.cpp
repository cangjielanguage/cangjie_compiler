// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "gtest/gtest.h"
#include "cangjie/Utils/Unicode.h"

#include <optional>
#include <string>

using namespace Cangjie;
using namespace Cangjie::Unicode;

TEST(UnicodeTest, NfcQuickCheck)
{
    std::string okay{"ok\u00e0\u031b\u0316\u0317\u0318\u0319\u031c\u031d\u0301\u0302"
                     "\u0303\u0304\u0305\u0306\u0307\u0308\u0309\u030a\u030b\u030c\u030d\u030e\u030f"
                     "\u0310\u0311\u0312\u0313\u0314\u0315\u031ay"};
    EXPECT_EQ(NfcQuickCheck(okay), NfcQcResult::MAYBE);

    // this string is itself MAYBE in respect to nfc quick check, but is stream unsafe, which is not implemented yet
    std::string tooMuch{"not ok\u00e0\u031b\u0316\u0317\u0318\u0319\u031c\u031d\u031e\u0301\u0302"
                        "\u0303\u0304\u0305\u0306\u0307\u0308\u0309\u030a\u030b\u030c\u030d\u030e\u030f"
                        "\u0310\u0311\u0312\u0313\u0314\u0315\u031ay"};
    EXPECT_EQ(NfcQuickCheck(tooMuch), NfcQcResult::MAYBE);
}

TEST(UnicodeTest, ComposeHangul)
{
    EXPECT_EQ(ComposeHangul(0xcea0, 0x11a7), std::nullopt);
}

TEST(UnicodeTest, XID)
{
    EXPECT_FALSE(IsXIDStart(static_cast<unsigned>(static_cast<unsigned char>('_'))));
    EXPECT_FALSE(IsXIDStart(static_cast<unsigned>(static_cast<unsigned char>('$'))));
    EXPECT_TRUE(IsXIDContinue(static_cast<unsigned>(static_cast<unsigned char>('_'))));
    EXPECT_FALSE(IsXIDContinue(static_cast<unsigned>(static_cast<unsigned char>('$'))));
    EXPECT_FALSE(IsXIDContinue(0x200c));
    EXPECT_FALSE(IsXIDContinue(0x200d));

    // Unicode tr31 5.1.1
    constexpr UTF32 XIDCONTINUE_NOT_XIDSTART[]{0xe33, 0xeb3, 0xff9e, 0xff9f};
    for (auto c : XIDCONTINUE_NOT_XIDSTART) {
        EXPECT_TRUE(IsXIDContinue(c));
        EXPECT_FALSE(IsXIDStart(c));
    }

    // Unicode tr31 5.1.2
    EXPECT_FALSE(IsXIDStart(0x037a));
}

std::string GetNfc(const std::string& s)
{
    auto p = s;
    NFC(p);
    return p;
}

TEST(UnicodeTest, NFD)
{
    EXPECT_EQ(CanonicalDecompose("abc"), "abc");
    EXPECT_EQ(CanonicalDecompose("\u1e0b\u01c4"), "d\u0307\u01c4");
    EXPECT_EQ(CanonicalDecompose("\u2026"), "\u2026");
    EXPECT_EQ(CanonicalDecompose("\u2126"), "\u03a9");
    EXPECT_EQ(CanonicalDecompose("\u1e0b\u0323"), "d\u0323\u0307");
    EXPECT_EQ(CanonicalDecompose("\u1e0d\u0307"), "d\u0323\u0307");
    EXPECT_EQ(CanonicalDecompose("a\u0301"), "a\u0301");
    EXPECT_EQ(CanonicalDecompose("\u301a"), "\u301a");
    EXPECT_EQ(CanonicalDecompose("\ud4db"), "\u1111\u1171\u11b6");
    EXPECT_EQ(CanonicalDecompose("\uac1c"), "\u1100\u1162");
}

TEST(UnicodeTest, NFC)
{
    EXPECT_EQ(GetNfc("abc"), "abc");
    EXPECT_EQ(GetNfc("\u1e0b\u01c4"), "\u1e0b\u01c4");
    EXPECT_EQ(GetNfc("\u2026"), "\u2026");
    EXPECT_EQ(GetNfc("\u2126"), "\u03a9");
    EXPECT_EQ(GetNfc("\u1e0b\u0323"), "\u1e0d\u0307");
    EXPECT_EQ(GetNfc("\u1e0d\u0307"), "\u1e0d\u0307");
    EXPECT_EQ(GetNfc("a\u0301"), "\u00e1");
    EXPECT_EQ(GetNfc("\u0301a"), "\u0301a");
    EXPECT_EQ(GetNfc("\ud4db"), "\ud4db");
    EXPECT_EQ(GetNfc("\uac1c"), "\uac1c");
    EXPECT_EQ(GetNfc("a\u0300\u0305\u0315\u05aeb"), "\u00e0\u05ae\u0305\u0315b");
}

TEST(UnicodeTest, InfiniteLoop)
{
    // no infinite loop here
    GetNfc("a\u0300\u0305\u0315\u5aeb");
}
