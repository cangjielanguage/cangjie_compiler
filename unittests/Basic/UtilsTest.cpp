// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include <string>
#include <utility>
#include <vector>
#include "cangjie/Basic/Utils.h"

#include "gtest/gtest.h"
using namespace Cangjie;

TEST(UtilsTest, GetHashTest)
{
    std::string case1 = "hello world";
    uint64_t h1 = Cangjie::Utils::GetHash(case1);
    EXPECT_EQ(h1, std::hash<std::string>{}(case1));
}

TEST(UtilsTest, JoinStringsTest)
{
    std::vector<std::pair<std::vector<std::string>, std::string>> cases = {
        {{"hello"}, ","}, {{"hello", "hello"}, "|"}, {{""}, ""}};
    std::vector<std::string> expect = {"hello", "hello|hello", ""};

    for (size_t i = 0; i < cases.size(); i++) {
        std::string result = Cangjie::Utils::JoinStrings(cases[i].first, cases[i].second);
        EXPECT_EQ(result, expect[i]);
    }
}

TEST(UtilsTest, StringSplitTest)
{
    std::vector<std::string> res = {"a", "ab", "abc", ""};
    std::string case1 = "a\nab\nabc\n";
    std::vector<std::string> case1Ret = Utils::SplitString(case1, "\n");
    EXPECT_EQ(res.size(), case1Ret.size());
    for (size_t i = 0; i < res.size(); ++i) {
        EXPECT_EQ(res[i], case1Ret[i]);
    }

    std::vector<std::string> res2 = {"a", "ab", "abc"};
    std::string case2 = "a;ab;abc";
    std::vector<std::string> case2Ret = Utils::SplitString(case2, ";");
    EXPECT_EQ(res2.size(), case2Ret.size());
    for (size_t i = 0; i < res2.size(); ++i) {
        EXPECT_EQ(res2[i], case2Ret[i]);
    }
}

TEST(UtilsTest, SplitLinesTest0)
{
    std::vector<std::string> res = {"", "", "", ""};
    std::string case1 = "\n\n\n";
    std::string case2 = "\r\r\r";
    std::string case3 = "\r\n\r\n\r\n";
    std::string case4 = "\n\r\r\n";

    std::vector<std::string> case1Ret = Utils::SplitLines(case1);
    std::vector<std::string> case2Ret = Utils::SplitLines(case2);
    std::vector<std::string> case3Ret = Utils::SplitLines(case3);
    std::vector<std::string> case4Ret = Utils::SplitLines(case4);

    EXPECT_EQ(res.size(), case1Ret.size());
    EXPECT_EQ(1, case2Ret.size());
    EXPECT_EQ(res.size(), case3Ret.size());
    EXPECT_EQ(3, case4Ret.size());

    for (size_t i = 0; i < res.size(); ++i) {
        EXPECT_EQ(res[i], case1Ret[i]);
        EXPECT_EQ(res[i], case3Ret[i]);
    }
    EXPECT_EQ("\r\r\r", case2Ret[0]);
    EXPECT_EQ("", case4Ret[0]);
    EXPECT_EQ("\r", case4Ret[1]);
    EXPECT_EQ("", case4Ret[2]);
}

TEST(UtilsTest, SplitLinesTest1)
{
    std::vector<std::string> res = {"a", "ab", "abc", ""};
    std::string case1 = "a\nab\nabc\n";
    std::string case2 = "a\rab\rabc\r";
    std::string case3 = "a\r\nab\r\nabc\r\n";
    std::string case4 = "a\nab\rabc\r\n";

    std::vector<std::string> case1Ret = Utils::SplitLines(case1);
    std::vector<std::string> case2Ret = Utils::SplitLines(case2);
    std::vector<std::string> case3Ret = Utils::SplitLines(case3);
    std::vector<std::string> case4Ret = Utils::SplitLines(case4);

    EXPECT_EQ(res.size(), case1Ret.size());
    EXPECT_EQ(1, case2Ret.size());
    EXPECT_EQ(res.size(), case3Ret.size());
    EXPECT_EQ(3, case4Ret.size());

    for (size_t i = 0; i < res.size(); ++i) {
        EXPECT_EQ(res[i], case1Ret[i]);
        EXPECT_EQ(res[i], case3Ret[i]);
    }
    EXPECT_EQ("a\rab\rabc\r", case2Ret[0]);
    EXPECT_EQ("a", case4Ret[0]);
    EXPECT_EQ("ab\rabc", case4Ret[1]);
    EXPECT_EQ("", case4Ret[2]);
}

TEST(UtilsTest, SplitLinesTest2)
{
    std::vector<std::string> res = {"a", "ab", "abc", "a"};
    std::string case1 = "a\nab\nabc\na";
    std::string case2 = "a\rab\rabc\ra";
    std::string case3 = "a\r\nab\r\nabc\r\na";
    std::string case4 = "a\nab\rabc\r\na";

    std::vector<std::string> case1Ret = Utils::SplitLines(case1);
    std::vector<std::string> case2Ret = Utils::SplitLines(case2);
    std::vector<std::string> case3Ret = Utils::SplitLines(case3);
    std::vector<std::string> case4Ret = Utils::SplitLines(case4);

    EXPECT_EQ(res.size(), case1Ret.size());
    EXPECT_EQ(1, case2Ret.size());
    EXPECT_EQ(res.size(), case3Ret.size());
    EXPECT_EQ(3, case4Ret.size());

    for (size_t i = 0; i < res.size(); ++i) {
        EXPECT_EQ(res[i], case1Ret[i]);
        EXPECT_EQ(res[i], case3Ret[i]);
    }
    EXPECT_EQ("a\rab\rabc\ra", case2Ret[0]);
    EXPECT_EQ("a", case4Ret[0]);
    EXPECT_EQ("ab\rabc", case4Ret[1]);
    EXPECT_EQ("a", case4Ret[2]);
}

TEST(UtilsTest, SplitLinesTest3)
{
    std::vector<std::string> res = {"", "ab", "abc", "a"};
    std::string case1 = "\nab\nabc\na";
    std::string case2 = "\rab\rabc\ra";
    std::string case3 = "\r\nab\r\nabc\r\na";
    std::string case4 = "\nab\rabc\r\na";

    std::vector<std::string> case1Ret = Utils::SplitLines(case1);
    std::vector<std::string> case2Ret = Utils::SplitLines(case2);
    std::vector<std::string> case3Ret = Utils::SplitLines(case3);
    std::vector<std::string> case4Ret = Utils::SplitLines(case4);

    EXPECT_EQ(res.size(), case1Ret.size());
    EXPECT_EQ(1, case2Ret.size());
    EXPECT_EQ(res.size(), case3Ret.size());
    EXPECT_EQ(3, case4Ret.size());

    for (size_t i = 0; i < res.size(); ++i) {
        EXPECT_EQ(res[i], case1Ret[i]);
        EXPECT_EQ(res[i], case3Ret[i]);
    }
    EXPECT_EQ("\rab\rabc\ra", case2Ret[0]);
    EXPECT_EQ("", case4Ret[0]);
    EXPECT_EQ("ab\rabc", case4Ret[1]);
    EXPECT_EQ("a", case4Ret[2]);
}