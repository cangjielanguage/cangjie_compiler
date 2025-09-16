// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include <iostream>
#include <string>

#include "gtest/gtest.h"

#include "cangjie/IncrementalCompilation/IncrementalCompilationLogger.h"

using namespace Cangjie;

class IncrementalCompilationLoggerTest : public ::testing::Test {
protected:
    void SetUp() override
    {
    }
};

TEST_F(IncrementalCompilationLoggerTest, InvidPath)
{
    auto& logger = IncrementalCompilationLogger::GetInstance();
    logger.InitLogFile("");
    EXPECT_EQ(logger.IsEnable(), false);
    logger.InitLogFile(".cache");
    EXPECT_EQ(logger.IsEnable(), false);
    logger.InitLogFile(".cjo");
    EXPECT_EQ(logger.IsEnable(), false);
    logger.InitLogFile("xxx");
    EXPECT_EQ(logger.IsEnable(), false);
    logger.InitLogFile("xxx");
    EXPECT_EQ(logger.IsEnable(), false);
    logger.InitLogFile(".log");
    EXPECT_EQ(logger.IsEnable(), false);
    Cangjie::FileUtil::CreateDirs("log/");
    logger.InitLogFile("log");
    EXPECT_EQ(logger.IsEnable(), false);
    logger.InitLogFile("log/");
    EXPECT_EQ(logger.IsEnable(), false);
    std::string nomalIncrLogPath = ".cached/2343242355.log";
    if (Cangjie::FileUtil::FileExist(nomalIncrLogPath)) {
        logger.InitLogFile(nomalIncrLogPath);
        EXPECT_EQ(logger.IsEnable(), true);
    } else {
        logger.InitLogFile(nomalIncrLogPath);
        EXPECT_EQ(logger.IsEnable(), false);
        Cangjie::FileUtil::CreateDirs(".cached/");
        logger.InitLogFile(nomalIncrLogPath);
        EXPECT_EQ(logger.IsEnable(), true);
    }
}
