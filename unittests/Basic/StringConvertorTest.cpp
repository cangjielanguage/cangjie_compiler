// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include <string>
#include <utility>
#include <vector>
#include "cangjie/Basic/StringConvertor.h"

#include "gtest/gtest.h"
using namespace Cangjie;

namespace {
unsigned char g_gbkCharArray[] = {176, 162, 203, 174};            // 阿水 gbk 编码
unsigned char g_utf8CharArray[] = {233, 152, 191, 230, 176, 180}; // 阿水 utf8 编码
unsigned char g_errorCharArray[] = {129};                         // unknow 编码
unsigned char g_error2CharArray[] = {240, 169, 184};              // 𩸽 utf8 编码 丢失最后一位: 189
} // namespace

TEST(StringConvertorTest, GetStringEncoding)
{
    std::string gbkString(reinterpret_cast<const char*>(g_gbkCharArray), sizeof(g_gbkCharArray));
    EXPECT_EQ(StringConvertor::GetStringEncoding(gbkString), Cangjie::StringConvertor::GBK);
    std::string utf8String(reinterpret_cast<const char*>(g_utf8CharArray), sizeof(g_utf8CharArray));
    EXPECT_EQ(StringConvertor::GetStringEncoding(utf8String), Cangjie::StringConvertor::UTF8);
    std::string errorString(reinterpret_cast<const char*>(g_errorCharArray), sizeof(g_errorCharArray));
    EXPECT_EQ(StringConvertor::GetStringEncoding(errorString), Cangjie::StringConvertor::UNKNOWN);
    std::string error2String(reinterpret_cast<const char*>(g_error2CharArray), sizeof(g_error2CharArray));
    EXPECT_EQ(StringConvertor::GetStringEncoding(error2String), Cangjie::StringConvertor::UNKNOWN);
}

TEST(StringConvertorTest, GBKToUTF8)
{
    std::string gbkString(reinterpret_cast<const char*>(g_gbkCharArray), sizeof(g_gbkCharArray));
    std::string utf8String(reinterpret_cast<const char*>(g_utf8CharArray), sizeof(g_utf8CharArray));
    std::optional<std::string> str = StringConvertor::GBKToUTF8(gbkString);
    EXPECT_EQ(str.has_value(), true);
    EXPECT_EQ(str.value(), utf8String);
}

TEST(StringConvertorTest, UTF8ToGBK)
{
    std::string gbkString(reinterpret_cast<const char*>(g_gbkCharArray), sizeof(g_gbkCharArray));
    std::string utf8String(reinterpret_cast<const char*>(g_utf8CharArray), sizeof(g_utf8CharArray));
    std::optional<std::string> str = StringConvertor::UTF8ToGBK(utf8String);
    EXPECT_EQ(str.has_value(), true);
    EXPECT_EQ(str.value(), gbkString);
}

TEST(StringConvertorTest, NormalizeStringToUTF8)
{
    std::string gbkString(reinterpret_cast<const char*>(g_gbkCharArray), sizeof(g_gbkCharArray));
    std::string utf8String(reinterpret_cast<const char*>(g_utf8CharArray), sizeof(g_utf8CharArray));
    std::optional<std::string> str = StringConvertor::NormalizeStringToUTF8(gbkString);
    EXPECT_EQ(str.has_value(), true);
    EXPECT_EQ(str.value(), utf8String);

    str = StringConvertor::NormalizeStringToUTF8(utf8String);
    EXPECT_EQ(str.has_value(), true);
    EXPECT_EQ(str.value(), utf8String);
}

TEST(StringConvertorTest, NormalizeStringToGBK)
{
    std::string gbkString(reinterpret_cast<const char*>(g_gbkCharArray), sizeof(g_gbkCharArray));
    std::string utf8String(reinterpret_cast<const char*>(g_utf8CharArray), sizeof(g_utf8CharArray));
    std::optional<std::string> str = StringConvertor::NormalizeStringToGBK(gbkString);
    EXPECT_EQ(str.has_value(), true);
    EXPECT_EQ(str.value(), gbkString);

    str = StringConvertor::NormalizeStringToGBK(utf8String);
    EXPECT_EQ(str.has_value(), true);
    EXPECT_EQ(str.value(), gbkString);
}