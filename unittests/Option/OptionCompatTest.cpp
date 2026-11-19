// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * Unit tests for compatible mode (--compat-version).
 */

#include <memory>
#include <string>
#include <vector>

#include "gtest/gtest.h"

#include "cangjie/Basic/Version.h"
#include "cangjie/Option/Option.h"
#include "cangjie/Option/OptionTable.h"

using namespace Cangjie;

namespace {
OptionArgInstance* FindCompatVersionArg(ArgList& argList)
{
    for (auto& arg : argList.args) {
        if (arg->argInstanceType != ArgInstanceType::Option) {
            continue;
        }
        auto optArg = static_cast<OptionArgInstance*>(arg.get());
        if (optArg->info.GetID() == Options::ID::COMPAT_VERSION) {
            return optArg;
        }
    }
    return nullptr;
}
} // namespace

class OptionCompatTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        optTbl = CreateOptionTable();
        gblOpts = std::make_unique<GlobalOptions>();
    }

    bool ParseArgs(const std::vector<std::string>& argStrs, ArgList& argList)
    {
        return optTbl->ParseArgs(argStrs, argList);
    }

    /**
     * Parse "--compat-version 1.3" with the option table, then replace the value with @p value so that
     * the option action is exercised with values the option table would reject.
     */
    bool ParseCompatValueBypassingTable(const std::string& value)
    {
        ArgList argList;
        std::vector<std::string> argStrs = {"cjc", "--compat-version", "1.3"};
        if (!optTbl->ParseArgs(argStrs, argList)) {
            return false;
        }
        auto arg = FindCompatVersionArg(argList);
        EXPECT_NE(arg, nullptr);
        if (arg == nullptr) {
            return false;
        }
        arg->value = value;
        return gblOpts->ParseFromArgs(argList);
    }

    std::unique_ptr<OptionTable> optTbl;
    std::unique_ptr<GlobalOptions> gblOpts;
};

// ---------------------------------------------------------------------------
// VersionInfo
// ---------------------------------------------------------------------------
TEST(VersionInfoTest, EqualityIgnoresValidFlag)
{
    EXPECT_TRUE((VersionInfo{1, 3, 0, true} == VersionInfo{1, 3, 0, true}));
    EXPECT_TRUE((VersionInfo{1, 3, 0, true} == VersionInfo{1, 3, 0, false}));
    EXPECT_FALSE((VersionInfo{1, 3, 0, true} == VersionInfo{1, 3, 1, true}));
    EXPECT_FALSE((VersionInfo{1, 3, 0, true} == VersionInfo{1, 4, 0, true}));
    EXPECT_FALSE((VersionInfo{1, 3, 0, true} == VersionInfo{2, 3, 0, true}));
}

TEST(VersionInfoTest, LessThanComparesMajorMinorPatch)
{
    // major decides
    EXPECT_TRUE((VersionInfo{0, 9, 9, true} < VersionInfo{1, 0, 0, true}));
    EXPECT_FALSE((VersionInfo{2, 0, 0, true} < VersionInfo{1, 9, 9, true}));
    // minor decides
    EXPECT_TRUE((VersionInfo{1, 2, 9, true} < VersionInfo{1, 3, 0, true}));
    EXPECT_FALSE((VersionInfo{1, 4, 0, true} < VersionInfo{1, 3, 9, true}));
    // patch decides
    EXPECT_TRUE((VersionInfo{1, 3, 0, true} < VersionInfo{1, 3, 1, true}));
    EXPECT_FALSE((VersionInfo{1, 3, 1, true} < VersionInfo{1, 3, 0, true}));
    // equal
    EXPECT_FALSE((VersionInfo{1, 3, 0, true} < VersionInfo{1, 3, 0, true}));
}

TEST(VersionInfoTest, ConstexprComparison)
{
    static_assert(VersionInfo{1, 2, 0, true} < VersionInfo{1, 3, 0, true});
    static_assert(VersionInfo{1, 3, 0, true} == VersionInfo{1, 3, 0, true});
    SUCCEED();
}

TEST(VersionInfoTest, CurrentVersion)
{
    auto current = VersionInfo::Current();
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    EXPECT_TRUE(current.isValid);
    // Current() must be consistent with CANGJIE_VERSION string.
    std::string expectedPrefix = std::to_string(current.major) + "." + std::to_string(current.minor);
    EXPECT_EQ(CANGJIE_VERSION.rfind(expectedPrefix, 0), 0u) << CANGJIE_VERSION;
#else
    EXPECT_FALSE(current.isValid);
#endif
}

TEST(VersionInfoTest, CompatChangeTableMatchesEnum)
{
    EXPECT_EQ(COMPAT_CHANGE_INFOS.size(), static_cast<size_t>(CompatChange::COUNT));
    for (const auto& info : COMPAT_CHANGE_INFOS) {
        EXPECT_TRUE(info.since.isValid);
        EXPECT_EQ(info.since.patch, 0u);
        EXPECT_NE(info.description, nullptr);
    }
}

// ---------------------------------------------------------------------------
// GlobalOptions compat helpers
// ---------------------------------------------------------------------------
TEST_F(OptionCompatTest, SupportedCompatVersions)
{
    const auto& versions = GlobalOptions::GetSupportedCompatVersions();
    ASSERT_FALSE(versions.empty());
    for (const auto& v : versions) {
        EXPECT_TRUE(v.isValid);
        EXPECT_TRUE(GlobalOptions::IsSupportedCompatVersion(v.major, v.minor));
    }
    EXPECT_TRUE(GlobalOptions::IsSupportedCompatVersion(1, 3));
    EXPECT_FALSE(GlobalOptions::IsSupportedCompatVersion(1, 2));
    EXPECT_FALSE(GlobalOptions::IsSupportedCompatVersion(2, 3));
    EXPECT_FALSE(GlobalOptions::IsSupportedCompatVersion(0, 0));
    // The static list must be the same object every time.
    EXPECT_EQ(&GlobalOptions::GetSupportedCompatVersions(), &versions);
}

TEST_F(OptionCompatTest, SupportedVersionsMatchOptionTableWhitelist)
{
    // GetSupportedCompatVersions derives from the compat_version_value whitelist declared in
    // Options.inc, so every supported version must be accepted by the option table as well.
    auto optTable = CreateOptionTable();
    for (const auto& v : GlobalOptions::GetSupportedCompatVersions()) {
        ArgList argList;
        std::vector<std::string> argStrs = {
            "cjc", "--compat-version", std::to_string(v.major) + "." + std::to_string(v.minor)};
        EXPECT_TRUE(optTable->ParseArgs(argStrs, argList)) << v.major << "." << v.minor;
    }
}

TEST_F(OptionCompatTest, DefaultIsNotCompatMode)
{
    EXPECT_FALSE(gblOpts->IsCompatModeEnabled());
    EXPECT_EQ(gblOpts->GetCompatVersion(), VersionInfo::Current());
}

TEST_F(OptionCompatTest, CompatVersionQueries)
{
    gblOpts->SetCompatVersion(VersionInfo{1, 3, 0, true});
    EXPECT_TRUE(gblOpts->IsCompatModeEnabled());
    EXPECT_EQ(gblOpts->GetCompatVersion(), (VersionInfo{1, 3, 0, true}));
}

// ---------------------------------------------------------------------------
// Cache key
// ---------------------------------------------------------------------------
TEST_F(OptionCompatTest, CacheNameDiffersInCompatMode)
{
    gblOpts->compilePackage = true;
    auto [normalDir, normalName] = gblOpts->GenerateNamesOfCachedDirAndFile("pkg");

    gblOpts->SetCompatVersion(VersionInfo{1, 3, 0, true});
    auto [compatDir, compatName] = gblOpts->GenerateNamesOfCachedDirAndFile("pkg");
    EXPECT_EQ(normalDir, compatDir);
    EXPECT_NE(normalName, compatName);

    // Deterministic for the same compat version.
    auto [compatDir2, compatName2] = gblOpts->GenerateNamesOfCachedDirAndFile("pkg");
    EXPECT_EQ(compatName, compatName2);

    // Different compat version leads to different cache name.
    gblOpts->SetCompatVersion(VersionInfo{1, 4, 0, true});
    auto [compatDir3, compatName3] = gblOpts->GenerateNamesOfCachedDirAndFile("pkg");
    EXPECT_NE(compatName, compatName3);
}

// ---------------------------------------------------------------------------
// Command-line parsing
// ---------------------------------------------------------------------------
TEST_F(OptionCompatTest, ParseValidCompatVersionSeparated)
{
    ArgList argList;
    std::vector<std::string> argStrs = {"cjc", "--compat-version", "1.3"};
    ASSERT_TRUE(ParseArgs(argStrs, argList));
    ASSERT_TRUE(gblOpts->ParseFromArgs(argList));
    ASSERT_TRUE(gblOpts->IsCompatModeEnabled());
    EXPECT_EQ(gblOpts->GetCompatVersion(), (VersionInfo{1, 3, 0, true}));
    EXPECT_TRUE(gblOpts->GetCompatVersion().isValid);
}

TEST_F(OptionCompatTest, ParseValidCompatVersionJoined)
{
    ArgList argList;
    std::vector<std::string> argStrs = {"cjc", "--compat-version=1.3"};
    ASSERT_TRUE(ParseArgs(argStrs, argList));
    ASSERT_TRUE(gblOpts->ParseFromArgs(argList));
    ASSERT_TRUE(gblOpts->IsCompatModeEnabled());
    EXPECT_EQ(gblOpts->GetCompatVersion(), (VersionInfo{1, 3, 0, true}));
}

TEST_F(OptionCompatTest, ParseSameCompatVersionTwice)
{
    ArgList argList;
    std::vector<std::string> argStrs = {"cjc", "--compat-version", "1.3", "--compat-version=1.3"};
    ASSERT_TRUE(ParseArgs(argStrs, argList));
    testing::internal::CaptureStderr();
    bool succ = gblOpts->ParseFromArgs(argList);
    std::string err = testing::internal::GetCapturedStderr();
    EXPECT_TRUE(succ);
    // MULTIPLE_OCCURRENCE: no "specified multiple times" warning.
    EXPECT_EQ(err.find("specified multiple times"), std::string::npos) << err;
    EXPECT_EQ(gblOpts->GetCompatVersion(), (VersionInfo{1, 3, 0, true}));
}

TEST_F(OptionCompatTest, ParseConflictingCompatVersions)
{
    // Only one version is currently supported, so simulate a previously set different version.
    gblOpts->SetCompatVersion(VersionInfo{1, 4, 0, true});
    ArgList argList;
    std::vector<std::string> argStrs = {"cjc", "--compat-version", "1.3"};
    ASSERT_TRUE(ParseArgs(argStrs, argList));
    testing::internal::CaptureStderr();
    bool succ = gblOpts->ParseFromArgs(argList);
    std::string err = testing::internal::GetCapturedStderr();
    EXPECT_FALSE(succ);
    EXPECT_NE(err.find("different compatibility versions cannot be used together"), std::string::npos) << err;
    EXPECT_NE(err.find("'1.3' and '1.4'"), std::string::npos) << err;
    // The previously set value is kept.
    EXPECT_EQ(gblOpts->GetCompatVersion(), (VersionInfo{1, 4, 0, true}));
}

TEST_F(OptionCompatTest, OptionActionRejectsUnsupportedVersion)
{
    EXPECT_FALSE(ParseCompatValueBypassingTable("1.2"));
    EXPECT_FALSE(gblOpts->IsCompatModeEnabled());
}

TEST_F(OptionCompatTest, OptionActionRejectsMalformedVersion)
{
    const std::vector<std::string> malformed = {
        "", "1", "1.", ".3", "1.3.0", "1.3a", "a.b", "v1.3", " 1.3", "1.3 ", "1..3", "-1.3"};
    for (const auto& value : malformed) {
        gblOpts = std::make_unique<GlobalOptions>();
        EXPECT_FALSE(ParseCompatValueBypassingTable(value)) << "value: '" << value << "'";
        EXPECT_FALSE(gblOpts->IsCompatModeEnabled()) << "value: '" << value << "'";
    }
}

TEST_F(OptionCompatTest, OptionActionAcceptsLeadingZeros)
{
    // "01.03" is syntactically valid and numerically equals 1.3.
    EXPECT_TRUE(ParseCompatValueBypassingTable("01.03"));
    EXPECT_EQ(gblOpts->GetCompatVersion(), (VersionInfo{1, 3, 0, true}));
}

TEST_F(OptionCompatTest, InvalidCompatVersionSeparatedReportsWhitelist)
{
    ArgList argList;
    std::vector<std::string> argStrs = {"cjc", "--compat-version", "1.2"};
    testing::internal::CaptureStderr();
    bool succ = ParseArgs(argStrs, argList);
    std::string err = testing::internal::GetCapturedStderr();
    EXPECT_FALSE(succ);
    EXPECT_NE(err.find("invalid compat version, supported compatible versions: [1.3]"), std::string::npos) << err;
    EXPECT_EQ(err.find("invalid value"), std::string::npos) << err;
}

TEST_F(OptionCompatTest, InvalidCompatVersionJoinedReportsWhitelist)
{
    ArgList argList;
    std::vector<std::string> argStrs = {"cjc", "--compat-version=abc"};
    testing::internal::CaptureStderr();
    bool succ = ParseArgs(argStrs, argList);
    std::string err = testing::internal::GetCapturedStderr();
    EXPECT_FALSE(succ);
    EXPECT_NE(err.find("invalid compat version, supported compatible versions: [1.3]"), std::string::npos) << err;
}

TEST_F(OptionCompatTest, InvalidCompatVersionEmptyJoinedValue)
{
    ArgList argList;
    std::vector<std::string> argStrs = {"cjc", "--compat-version="};
    testing::internal::CaptureStderr();
    bool succ = ParseArgs(argStrs, argList);
    (void)testing::internal::GetCapturedStderr();
    EXPECT_FALSE(succ);
}

TEST_F(OptionCompatTest, MissingCompatVersionValue)
{
    ArgList argList;
    std::vector<std::string> argStrs = {"cjc", "--compat-version"};
    testing::internal::CaptureStderr();
    bool succ = ParseArgs(argStrs, argList);
    (void)testing::internal::GetCapturedStderr();
    EXPECT_FALSE(succ);
}

TEST_F(OptionCompatTest, InvalidValueOfOtherOptionKeepsGenericMessage)
{
    {
        ArgList argList;
        std::vector<std::string> argStrs = {"cjc", "--output-type", "unknown_type"};
        testing::internal::CaptureStderr();
        bool succ = ParseArgs(argStrs, argList);
        std::string err = testing::internal::GetCapturedStderr();
        EXPECT_FALSE(succ);
        EXPECT_NE(err.find("invalid value: 'unknown_type'"), std::string::npos) << err;
        EXPECT_EQ(err.find("compat"), std::string::npos) << err;
    }
    {
        ArgList argList;
        std::vector<std::string> argStrs = {"cjc", "--output-type=unknown_type"};
        testing::internal::CaptureStderr();
        bool succ = ParseArgs(argStrs, argList);
        std::string err = testing::internal::GetCapturedStderr();
        EXPECT_FALSE(succ);
        EXPECT_NE(err.find("invalid value: 'unknown_type'"), std::string::npos) << err;
    }
}
