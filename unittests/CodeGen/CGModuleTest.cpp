// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "CGTest.h"
#include "cangjie/Option/Option.h"
#include "../src/CodeGen/CGModule.h"

using namespace Cangjie;
using namespace CodeGen;

class CGModuleTest : public ::testing::Test {
protected:
    void SetUp() override
    {
    };
};

TEST_F(CGModuleTest, GetTargetTripleString)
{
    EXPECT_EQ("arm64-apple-macosx12.0.0", "arm64-apple-macosx12.0.0");
    Triple::Info target;

    // MacOS
    target.arch = Triple::ArchType::AARCH64;
    target.vendor = Triple::Vendor::APPLE;
    target.os = Triple::OSType::DARWIN;
    EXPECT_EQ(CGModule::GetTargetTripleString(target), "arm64-apple-macosx12.0.0");
    target.arch = Triple::ArchType::ARM64;
    EXPECT_EQ(CGModule::GetTargetTripleString(target), "arm64-apple-macosx12.0.0");
    target.arch = Triple::ArchType::ARM32;
    EXPECT_EQ(CGModule::GetTargetTripleString(target), "arm-apple-macosx12.0.0");
    target.os = Triple::OSType::IOS;
    EXPECT_EQ(CGModule::GetTargetTripleString(target), "arm-apple-macosx12.0.0");

    // OHOS
    target.arch = Triple::ArchType::AARCH64;
    target.vendor = Triple::Vendor::UNKNOWN;
    target.os = Triple::OSType::LINUX;
    target.env = Triple::Environment::OHOS;
    EXPECT_EQ(CGModule::GetTargetTripleString(target), "aarch64-unknown-linux-ohos");
    target.vendor = Triple::Vendor::PC;
    EXPECT_EQ(CGModule::GetTargetTripleString(target), "aarch64-unknown-linux-ohos");

    // Linux ARM32
    target.arch = Triple::ArchType::ARM32;
    target.vendor = Triple::Vendor::UNKNOWN;
    target.os = Triple::OSType::LINUX;
    target.env = Triple::Environment::NOT_AVAILABLE;
    EXPECT_EQ(CGModule::GetTargetTripleString(target), "armv7a-linux-gnu");
}