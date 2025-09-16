// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "gtest/gtest.h"
#include "cangjie/Utils/Unicode.h"

using namespace Cangjie;
using namespace Unicode;

TEST(UnicodeWidthTests, Str)
{
    EXPECT_EQ(StrWidth("ｈｅｌｌｏ", false), 10);
    EXPECT_EQ(StrWidth("ｈｅｌｌｏ", true), 10);
    EXPECT_EQ(StrWidth({reinterpret_cast<const UTF8*>("\0\0\0\x01\x01"), 5}, false), 5);
    EXPECT_EQ(StrWidth({reinterpret_cast<const UTF8*>("\0\0\0\x01\x01"), 5}, true), 5);
    EXPECT_EQ(StrWidth("", true), 0);
    EXPECT_EQ(StrWidth("\u2081\u2082\u2083\u2084", false), 4);
    EXPECT_EQ(StrWidth("\u2081\u2082\u2083\u2084", true), 8);
    EXPECT_EQ(StrWidth("👩"), 2);
    EXPECT_EQ(StrWidth("🔬"), 2);
    EXPECT_EQ(StrWidth("👩‍🔬"), 4);
}

TEST(UnicodeWidthTests, SingleChar)
{
    EXPECT_EQ(SingleCharWidth(0xff48), 2);
    EXPECT_EQ(SingleCharWidth(0xff48, true), 2);
    EXPECT_EQ(SingleCharWidth(0), 1);
    EXPECT_EQ(SingleCharWidth(0, true), 1);
    EXPECT_EQ(SingleCharWidth(0x1), 1);
    EXPECT_EQ(SingleCharWidth(0x1, true), 1);
    EXPECT_EQ(SingleCharWidth(0x2081), 1);
    EXPECT_EQ(SingleCharWidth(0x2081, true), 2);
    EXPECT_EQ(SingleCharWidth(0x0A), 1);
    EXPECT_EQ(SingleCharWidth(0xA, true), 1);

    EXPECT_EQ(SingleCharWidth('w'), 1);
    EXPECT_EQ(SingleCharWidth('w', true), 1);
    EXPECT_EQ(SingleCharWidth(0xAD), 0);
    EXPECT_EQ(SingleCharWidth(0xAD, true), 0);
    EXPECT_EQ(SingleCharWidth(0x1160), 0);
    EXPECT_EQ(SingleCharWidth(0x1160, true), 0);
    EXPECT_EQ(SingleCharWidth(0xa1), 1);
    EXPECT_EQ(SingleCharWidth(0xa1, true), 2);
    EXPECT_EQ(SingleCharWidth(0x300), 0);
    EXPECT_EQ(SingleCharWidth(0x300, true), 0);
    EXPECT_EQ(SingleCharWidth(0x1F971), 2);
}

TEST(UnicodeWidthTests, DefaultIgnorable)
{
    EXPECT_EQ(SingleCharWidth(0xE0000), 0);
    EXPECT_EQ(SingleCharWidth(0x1160), 0);
    EXPECT_EQ(SingleCharWidth(0x3164), 0);
    EXPECT_EQ(SingleCharWidth(0xFFA0), 0);
}

TEST(UnicodeWidthTests, Jamo)
{
    EXPECT_EQ(SingleCharWidth(0x1100), 2);
    EXPECT_EQ(SingleCharWidth(0xA97C), 2);
    // Special case: U+115F HANGUL CHOSEONG FILLER
    EXPECT_EQ(SingleCharWidth(0x115F), 2);
    EXPECT_EQ(SingleCharWidth(0x1160), 0);
    EXPECT_EQ(SingleCharWidth(0xD7C6), 0);
    EXPECT_EQ(SingleCharWidth(0x11A8), 0);
    EXPECT_EQ(SingleCharWidth(0xD7FB), 0);
}

TEST(UnicodeWidthTests, PrependedConcatenationMarks)
{
    UTF32 chars[]{0x600, 0x601, 0x602, 0x603, 0x604, 0x60d, 0x110bd, 0x110cd};
    for (UTF32 c : chars) {
        EXPECT_EQ(SingleCharWidth(c), 1);
    }

    UTF32 chars2[]{0x605, 0x70f, 0x890, 0x891, 0x8e2};
    for (UTF32 c : chars2) {
        EXPECT_EQ(SingleCharWidth(c), 0);
    }
}

TEST(UnicodeWidthTests, InterlinearAnnotationChars)
{
    EXPECT_EQ(SingleCharWidth(0xfff9), 1);
    EXPECT_EQ(SingleCharWidth(0xfffa), 1);
    EXPECT_EQ(SingleCharWidth(0xfffb), 1);
}

TEST(UnicodeWidthTests, HieroglyphFormatControls)
{
    EXPECT_EQ(SingleCharWidth(0x13430), 1);
    EXPECT_EQ(SingleCharWidth(0x13436), 1);
    EXPECT_EQ(SingleCharWidth(0x1343c), 1);
}

TEST(UnicodeWidthTests, Marks)
{
    EXPECT_EQ(SingleCharWidth(0x0301), 0);
    EXPECT_EQ(SingleCharWidth(0x20dd), 0);
    EXPECT_EQ(SingleCharWidth(0x9cb), 1);
    EXPECT_EQ(SingleCharWidth(0x9be), 0);
}

TEST(UnicodeWidthTests, DevanagariCaret)
{
    EXPECT_EQ(SingleCharWidth(0xa8fa), 0);
}

TEST(UnicodeWidthTests, EmojiPresentation)
{
    EXPECT_EQ(SingleCharWidth(0x23), 1);
    EXPECT_EQ(SingleCharWidth(0xfe0f), 0);
    EXPECT_EQ(StrWidth("\u0023\uFE0F"), 2);
    EXPECT_EQ(StrWidth("a\u0023\uFE0Fa"), 4);
    EXPECT_EQ(StrWidth("\u0023a\uFE0F"), 2);
    EXPECT_EQ(StrWidth("a\uFE0F"), 1);
    EXPECT_EQ(StrWidth("\u0023\u0023\uFE0Fa"), 4);
    EXPECT_EQ(StrWidth("\u002A\uFE0F"), 2);
    EXPECT_EQ(StrWidth("\u23F9\uFE0F"), 2);
    EXPECT_EQ(StrWidth("\u24C2\uFE0F"), 2);
    EXPECT_EQ(StrWidth("\U0001F6F3\uFE0F"), 2);
    EXPECT_EQ(StrWidth("\U0001F700\uFE0F"), 1);
}

TEST(UnicodeWidthTests, TextPresentation)
{
    EXPECT_EQ(SingleCharWidth(0xFE0E), 0);

    EXPECT_EQ(SingleCharWidth(0x2648), 2);
    EXPECT_EQ(StrWidth("\u2648\uFE0E"), 1);
    EXPECT_EQ(StrWidth("\u2648\uFE0E", true), 2);

    EXPECT_EQ(StrWidth("\U0001F21A\uFE0E"), 2);
    EXPECT_EQ(StrWidth("\U0001F21A\uFE0E", true), 2);

    EXPECT_EQ(StrWidth("\u0301\uFE0E"), 0);
    EXPECT_EQ(StrWidth("\u0301\uFE0E", true), 0);

    EXPECT_EQ(StrWidth("a\uFE0E"), 1);
    EXPECT_EQ(StrWidth("a\uFE0E", true), 1);

    EXPECT_EQ(StrWidth("𘀀\uFE0E"), 2);
    EXPECT_EQ(StrWidth("𘀀\uFE0E", true), 2);
}

TEST(UnicodeWidthTests, ControlLineBreak)
{
    EXPECT_EQ(SingleCharWidth(0x2028), 1);
    EXPECT_EQ(SingleCharWidth(0x2029), 1);
    EXPECT_EQ(StrWidth("\r"), 1);
    EXPECT_EQ(StrWidth("\n"), 1);
    EXPECT_EQ(StrWidth("\r\n"), 1);
    EXPECT_EQ(StrWidth({reinterpret_cast<const UTF8*>("\0"), 1}), 1);
    EXPECT_EQ(StrWidth("1\t2\r\n3\u00854"), 7);
}

TEST(UnicodeWidthTests, CharStringConsistent)
{
    char data[4];
    for (UTF32 a = 0; a <= 0x10ffff; ++a) {
        if (a >= 0xd800 && a <= 0xdfff) {
            // surrogate not allowed in UTF-8
            continue;
        }
        int cw = SingleCharWidth(a);
        char* dam = data;
        ConvertCodepointToUTF8(a, dam);
        int sw = StrWidth(std::string_view{data, static_cast<size_t>(dam - data)});
        EXPECT_EQ(cw, sw);
    }
}

TEST(UnicodeWidthTests, LisuTones)
{
    char data[8];
    constexpr UTF32 lisuBegin{0xa4f8};
    constexpr UTF32 lisuEnd{0xa4fd};
    for (auto c1{lisuBegin}; c1 <= lisuEnd; ++c1) {
        for (auto c2{lisuBegin}; c2 <= lisuEnd; ++c2) {
            char* dam{data};
            ConvertCodepointToUTF8(c1, dam);
            ConvertCodepointToUTF8(c2, dam);
            int sw = StrWidth(std::string_view{data, static_cast<size_t>(dam - data)});
            if (c1 <= 0xa4fb && c2 >= 0xa4fc) {
                EXPECT_EQ(sw, 1);
            } else {
                EXPECT_EQ(sw, 2);
            }
        }
    }
    EXPECT_EQ(StrWidth("ꓪꓹꓼ"), 2);
    EXPECT_EQ(StrWidth("ꓪꓹꓹ"), 3);
    EXPECT_EQ(StrWidth("ꓪꓼꓼ"), 3);
}
