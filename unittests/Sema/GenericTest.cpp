// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * Generic instantiation related unit tests.
 */

#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "gtest/gtest.h"

#define private public
#include "TestCompilerInstance.h"
#include "cangjie/AST/Match.h"

using namespace Cangjie;
using namespace AST;

class GenericTest : public testing::Test {
protected:
    void SetUp() override
    {
#ifdef _WIN32
        std::string command;
        int err = 0;
        if (FileUtil::FileExist("testTempFiles")) {
            std::string command = "rmdir /s /q testTempFiles";
            int err = system(command.c_str());
            std::cout << err;
        }
        command = "mkdir testTempFiles";
        err = system(command.c_str());
        ASSERT_EQ(0, err);

        srcPath = projectPath + "\\tests\\LLT\\Sema\\generics\\";
        packagePath = packagePath + "\\";
#else
        std::string command;
        int err = 0;
        if (FileUtil::FileExist("testTempFiles")) {
            command = "rm -rf testTempFiles";
            err = system(command.c_str());
            ASSERT_EQ(0, err);
        }
        command = "mkdir -p testTempFiles";
        err = system(command.c_str());
        ASSERT_EQ(0, err);
        srcPath = projectPath + "/tests/LLT/Sema/generics/";
        packagePath = packagePath + "/";
#endif
#ifdef __x86_64__
        invocation.globalOptions.target.arch = Cangjie::Triple::ArchType::X86_64;
#else
        invocation.globalOptions.target.arch = Cangjie::Triple::ArchType::AARCH64;
#endif
#ifdef _WIN32
        invocation.globalOptions.target.os = Cangjie::Triple::OSType::WINDOWS;
#elif __unix__
        invocation.globalOptions.target.os = Cangjie::Triple::OSType::LINUX;
#endif
    }

#ifdef PROJECT_SOURCE_DIR
    // Gets the absolute path of the project from the compile parameter.
    std::string projectPath = PROJECT_SOURCE_DIR;
#else
    // Just in case, give it a default value.
    // Assume the initial is in the build directory.
    std::string projectPath = "..";
#endif
    std::string packagePath = "testTempFiles";
    std::string srcPath;
    DiagnosticEngine diag;
    CompilerInvocation invocation;
    std::unique_ptr<TestCompilerInstance> instance;
};
