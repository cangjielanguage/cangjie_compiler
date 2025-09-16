// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include <string>
#include <vector>

#include "ScopeManager.h"

#include "gtest/gtest.h"
#include "cangjie/Lex/Lexer.h"
#include "cangjie/Utils/FileUtil.h"

using namespace Cangjie;

class SourceManagerTest : public testing::Test {
protected:
    void SetUp() override
    {
#ifdef PROJECT_SOURCE_DIR
        // Gets the absolute path of the project from the compile parameter.
        projectPath = PROJECT_SOURCE_DIR;
#else
        // Just in case, give it a default value. Assume the initial is in the build directory.
        projectPath = "..";
#endif
        std::string command;
        int err = 0;
        if (FileUtil::FileExist("testTempFiles")) {
            command = "rmdir testTempFiles";
            err = system(command.c_str());
            ASSERT_EQ(0, err);
        }
        command = "mkdir testTempFiles";
        err = system(command.c_str());
        ASSERT_EQ(0, err);
        srcPath = projectPath + "/unittests/Basic/CangjieFiles/";
    }
    std::string projectPath;
    std::string srcPath;
    SourceManager sm;
};

TEST_F(SourceManagerTest, AddSourceTest)
{
    std::string srcFile = srcPath + "lsp" + ".cj";
    std::string absName = FileUtil::GetAbsPath(srcFile) | FileUtil::IdenticalFunc;
    std::string failedReason;
    auto content = FileUtil::ReadFileContent(srcFile, failedReason);
    ASSERT_TRUE(content.has_value() && failedReason.empty());
    auto fileID1 = sm.AddSource(absName, content.value());
    auto fileID2 = sm.AddSource(absName, content.value());
    auto fileID3 = sm.AddSource(absName, content.value());
    EXPECT_EQ(fileID1, fileID2);
    EXPECT_EQ(fileID2, fileID3);
    auto expectSourceSize = 2; // There is a source {0, "", ""} in sources.
    EXPECT_EQ(sm.GetNumberOfFiles(), expectSourceSize);
    EXPECT_EQ(sm.GetFileID(absName), fileID3);
}

TEST_F(SourceManagerTest, GetContentBetweenTest)
{
    std::string srcFile = srcPath + "lsp" + ".cj";
    std::string absName = FileUtil::GetAbsPath(srcFile) | FileUtil::IdenticalFunc;
    std::string failedReason;
    auto content = FileUtil::ReadFileContent(srcFile, failedReason);
    ASSERT_TRUE(content.has_value() && failedReason.empty());
    auto fileID1 = sm.AddSource(absName, content.value());

    DiagnosticEngine diag;
    Lexer lexer(fileID1, content.value(), diag, sm);
    for (auto tok = lexer.Next(); tok.kind != TokenKind::END; tok = lexer.Next()) {
    }
#ifdef _WIN32
    auto code = sm.GetContentBetween(fileID1, Position(14, 9), Position(14, 14));
    EXPECT_EQ(code, "let a");

    code = sm.GetContentBetween(fileID1, Position(14, 9), Position(14, 18));
    EXPECT_EQ(code, "let a = 1");

    code = sm.GetContentBetween(fileID1, Position(14, 9), Position(14, 19));
    EXPECT_EQ(code, "let a = 1\r\n");

    code = sm.GetContentBetween(fileID1, Position(14, 9), Position(14, std::numeric_limits<int>::max()));
    EXPECT_EQ(code, "let a = 1\r\n");

    code = sm.GetContentBetween(fileID1, Position(14, 9), Position(15, 14));
    EXPECT_EQ(code, "let a = 1\r\n        print");

    code = sm.GetContentBetween(fileID1, Position(14, 9), Position(15, 37));
    EXPECT_EQ(code, "let a = 1\r\n        print(\"PageRankList${a}\\n\");");

    code = sm.GetContentBetween(fileID1, Position(14, 9), Position(15, 38));
    EXPECT_EQ(code, "let a = 1\r\n        print(\"PageRankList${a}\\n\");\r\n");

    code = sm.GetContentBetween(fileID1, Position(14, 9), Position(15, std::numeric_limits<int>::max()));
    EXPECT_EQ(code, "let a = 1\r\n        print(\"PageRankList${a}\\n\");\r\n");

#elif __unix__
    auto code = sm.GetContentBetween(fileID1, Position(16, 9), Position(16, 14));
    EXPECT_EQ(code, "let a");

    code = sm.GetContentBetween(fileID1, Position(16, 9), Position(16, 18));
    EXPECT_EQ(code, "let a = 1");

    code = sm.GetContentBetween(fileID1, Position(16, 9), Position(16, 19));
    EXPECT_EQ(code, "let a = 1\n");

    code = sm.GetContentBetween(fileID1, Position(16, 9), Position(16, std::numeric_limits<int>::max()));
    EXPECT_EQ(code, "let a = 1\n");

    code = sm.GetContentBetween(fileID1, Position(16, 9), Position(17, 14));
    EXPECT_EQ(code, "let a = 1\n        print");

    code = sm.GetContentBetween(fileID1, Position(16, 9), Position(17, 37));
    EXPECT_EQ(code, "let a = 1\n        print(\"PageRankList${a}\\n\");");

    code = sm.GetContentBetween(fileID1, Position(16, 9), Position(17, 38));
    EXPECT_EQ(code, "let a = 1\n        print(\"PageRankList${a}\\n\");\n");

    code = sm.GetContentBetween(fileID1, Position(16, 9), Position(17, std::numeric_limits<int>::max()));
    EXPECT_EQ(code, "let a = 1\n        print(\"PageRankList${a}\\n\");\n");
#endif
}