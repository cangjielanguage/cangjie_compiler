// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include <string>

#include "gtest/gtest.h"
#include "cangjie/Basic/Version.h"

using namespace Cangjie;

TEST(VersionTest, ParseVersionFull)
{
    VersionInfo info = ParseVersion("1.2.3");
    EXPECT_TRUE(info.isValid);
    EXPECT_EQ(info.major, 1u);
    EXPECT_EQ(info.minor, 2u);
    EXPECT_EQ(info.patch, 3u);
}

TEST(VersionTest, ParseVersionWithoutPatch)
{
    // The patch number is optional and defaults to 0.
    VersionInfo info = ParseVersion("1.2");
    EXPECT_TRUE(info.isValid);
    EXPECT_EQ(info.major, 1u);
    EXPECT_EQ(info.minor, 2u);
    EXPECT_EQ(info.patch, 0u);
}

TEST(VersionTest, ParseVersionIgnoresSuffix)
{
    // SDK versions may carry a build suffix, e.g. "0.0.1-git...", and only the leading numbers are parsed.
    VersionInfo info = ParseVersion("0.0.1-git");
    EXPECT_TRUE(info.isValid);
    EXPECT_EQ(info.major, 0u);
    EXPECT_EQ(info.minor, 0u);
    EXPECT_EQ(info.patch, 1u);
}

TEST(VersionTest, ParseVersionRequiresLeadingNumber)
{
    EXPECT_FALSE(ParseVersion("").isValid);
    EXPECT_FALSE(ParseVersion("x.y.z").isValid);
    EXPECT_FALSE(ParseVersion("v1.2.3").isValid);
}

TEST(VersionTest, ParseVersionLimitsEachNumberTo99)
{
    EXPECT_TRUE(ParseVersion("0.0.0").isValid);
    EXPECT_TRUE(ParseVersion("99.99.99").isValid);
    EXPECT_FALSE(ParseVersion("100.2.3").isValid);
    EXPECT_FALSE(ParseVersion("1.100.3").isValid);
    EXPECT_FALSE(ParseVersion("1.2.100").isValid);
}

TEST(VersionTest, ParseVersionWithMinorOnly)
{
    // matchPatch == false: exactly major.minor, a patch number is not accepted.
    VersionInfo info = ParseVersion("1.2", /*matchPatch=*/false, /*matchSuffix=*/false);
    EXPECT_TRUE(info.isValid);
    EXPECT_EQ(info.major, 1u);
    EXPECT_EQ(info.minor, 2u);
    EXPECT_EQ(info.patch, 0u);

    EXPECT_FALSE(ParseVersion("1.2.3", false, false).isValid);
    EXPECT_FALSE(ParseVersion("1", false, false).isValid);
    EXPECT_FALSE(ParseVersion("1.2-local", false, false).isValid);
}

TEST(VersionTest, ParseVersionWithoutSuffix)
{
    // matchSuffix == false: the whole string must be consumed, the patch number is still optional.
    VersionInfo info = ParseVersion("1.2.3", true, false);
    EXPECT_TRUE(info.isValid);
    EXPECT_EQ(info.patch, 3u);
    EXPECT_TRUE(ParseVersion("1.2", true, false).isValid);
    EXPECT_FALSE(ParseVersion("1.2.3-local", true, false).isValid);
    EXPECT_FALSE(ParseVersion("1.2x", true, false).isValid);
}
