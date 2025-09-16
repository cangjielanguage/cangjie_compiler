// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "gtest/gtest.h"
#include "cangjie/Driver/TempFileManager.h"
#include "cangjie/Driver/TempFileInfo.h"
#include "cangjie/Option/Option.h"
#include "cangjie/Utils/FileUtil.h"

using namespace Cangjie;

class TempFileManagerTest : public ::testing::Test {
protected:
    void SetUp() override
    {
    }
};

TEST_F(TempFileManagerTest, WindowsOutputSuffixTest)
{
    GlobalOptions options;
    options.target.os = Triple::OSType::WINDOWS;
    TempFileManager::Instance().Init(options, false);
    TempFileInfo info;
    info.fileName = "test";
    auto newInfo = TempFileManager::Instance().CreateNewFileInfo(info, TempFileKind::O_EXE);
    ASSERT_EQ(FileUtil::GetFileName(newInfo.filePath), "main.exe");
    newInfo = TempFileManager::Instance().CreateNewFileInfo(info, TempFileKind::O_DYLIB);
    ASSERT_EQ(FileUtil::GetFileName(newInfo.filePath), "libtest.dll");
    newInfo = TempFileManager::Instance().CreateNewFileInfo(info, TempFileKind::O_STATICLIB);
    ASSERT_EQ(FileUtil::GetFileName(newInfo.filePath), "libtest.a");
}

TEST_F(TempFileManagerTest, LinuxOutputSuffixTest)
{
    GlobalOptions options;
    options.target.os = Triple::OSType::LINUX;
    TempFileManager::Instance().Init(options, false);
    TempFileInfo info;
    info.fileName = "test";
    auto newInfo = TempFileManager::Instance().CreateNewFileInfo(info, TempFileKind::O_EXE);
    ASSERT_EQ(FileUtil::GetFileName(newInfo.filePath), "main");
    newInfo = TempFileManager::Instance().CreateNewFileInfo(info, TempFileKind::O_DYLIB);
    ASSERT_EQ(FileUtil::GetFileName(newInfo.filePath), "libtest.so");
    newInfo = TempFileManager::Instance().CreateNewFileInfo(info, TempFileKind::O_STATICLIB);
    ASSERT_EQ(FileUtil::GetFileName(newInfo.filePath), "libtest.a");
}