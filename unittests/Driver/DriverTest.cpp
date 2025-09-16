// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "gtest/gtest.h"
#include "cangjie/Driver/Driver.h"

#include <algorithm>
#include <string>
#include <vector>

#include "cangjie/Driver/Utils.h"

using namespace Cangjie;

class DriverTest : public ::testing::Test {
public:
    std::unique_ptr<Driver> driver;

protected:
    void SetUp() override
    {
#ifdef PROJECT_SOURCE_DIR
        // Gets the absolute path of the project from the compile parameter.
        std::string projectPath = PROJECT_SOURCE_DIR;
#else
        // Just in case, give it a default value.
        // Assume the initial is in the build directory.
        std::string projectPath = "..";
#endif
#ifdef _WIN32
        sdkPath = FileUtil::JoinPath(projectPath, "unittests\\Driver");
#else
        sdkPath = FileUtil::JoinPath(projectPath, "unittests/Driver");
#endif
    }
    std::string sdkPath;
};

TEST_F(DriverTest, GetSingleQuotedTest)
{
    const std::string singleQuote{"\\'"};
    EXPECT_EQ(GetSingleQuoted("abcde"), "'abcde'");
    EXPECT_EQ(GetSingleQuoted("'; ls"), "''" + singleQuote + "'; ls'");
    EXPECT_EQ(GetSingleQuoted("'wrapped'"), std::string{"''" + singleQuote + "'wrapped'" + singleQuote + "''"});
    EXPECT_EQ(GetSingleQuoted("start'wrapped'end"),
        std::string{"'start'" + singleQuote + "'wrapped'" + singleQuote + "'end'"});
    EXPECT_EQ(GetSingleQuoted("end'"), std::string{"'end'" + singleQuote + "''"});
}

TEST_F(DriverTest, GetDarwinSDKVersion)
{
    std::optional<std::string> sdkVersion = GetDarwinSDKVersion(sdkPath);
    EXPECT_FALSE(sdkVersion.has_value());

    sdkVersion = GetDarwinSDKVersion(sdkPath + "/IncorrectSDKSettings1");
    EXPECT_FALSE(sdkVersion.has_value());

    sdkVersion = GetDarwinSDKVersion(sdkPath + "/IncorrectSDKSettings2");
    EXPECT_FALSE(sdkVersion.has_value());

    sdkVersion = GetDarwinSDKVersion(sdkPath + "/IncorrectSDKSettings3");
    EXPECT_FALSE(sdkVersion.has_value());

    sdkVersion = GetDarwinSDKVersion(sdkPath + "/CorrectSDKSettings");
    EXPECT_TRUE(sdkVersion.has_value());
    EXPECT_EQ(sdkVersion.value(), "14.5");
}