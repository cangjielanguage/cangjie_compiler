// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "gtest/gtest.h"
#include "cangjie/Basic/StringConvertor.h"
#include "cangjie/Basic/Version.h"
#include "cangjie/Frontend/CompilerInstance.h"
#include "cangjie/Utils/FileUtil.h"
#include "cangjie/Utils/ICEUtil.h"
#include "cangjie/Utils/Signal.h"

#ifdef __unix__
#include <cstdlib>
#include <unistd.h>
#elif _WIN32
#include <windows.h>
#endif

#include <fstream>
#include <iostream>
#include <unordered_map>

using namespace Cangjie;

#ifdef PROJECT_SOURCE_DIR
// Gets the absolute path of the project from the compile parameter.
const std::string PRPJECT_PATH = PROJECT_SOURCE_DIR;
#else
// Just in case, give it a default value.
// Assume the initial is in the build directory.
const std::string PRPJECT_PATH = "..";
#endif

#ifdef __unix__
const int STACK_OVERFLOW_RETURN_CODE = SIGSEGV + 128;
const std::unordered_map<std::string, int> signalStringValueMap = {{"SIGABRT", SIGABRT}, {"SIGFPE", SIGFPE},
    {"SIGSEGV", SIGSEGV}, {"SIGILL", SIGILL}, {"SIGTRAP", SIGTRAP}, {"SIGBUS", SIGBUS}};
#elif _WIN32
const DWORD STACK_OVERFLOW_RETURN_CODE = EXCEPTION_STACK_OVERFLOW;
const std::unordered_map<std::string, int> signalStringValueMap = {
    {"SIGABRT", SIGABRT}, {"SIGFPE", SIGFPE}, {"SIGSEGV", SIGSEGV}, {"SIGILL", SIGILL}};
#endif

const std::unordered_map<std::string, int64_t> moduleValueMap = {
    {"main", static_cast<int64_t>(CompileStage::COMPILE_STAGE_NUMBER)},
    {"parser", static_cast<int64_t>(CompileStage::PARSE)},
    {"sema", static_cast<int64_t>(CompileStage::SEMA)},
    {"chir", static_cast<int64_t>(CompileStage::CHIR)},
    {"codegen", static_cast<int64_t>(CompileStage::CODEGEN)},
    {"driver", static_cast<int64_t>(CompileStage::COMPILE_STAGE_NUMBER)}};

const std::string TEMP_CJ_FILE_NAME = PRPJECT_PATH + "/unittests/Utils/SignalTest.cj";
const std::string TEMP_ERROR_OUTPUT_NAME = "./tempError.txt";

class SignalTests : public testing::Test {
protected:
    void SetUp() override
    {
#ifdef _WIN32
        char* path = getcwd(NULL, 0);
        if (path == nullptr) {
            std::cerr << "Failed to get PWD!" << std::endl;
            _exit(1);
        }
        std::string tempPath = std::string(path);
        std::optional<std::wstring> tempValue = StringConvertor::StringToWString(tempPath);
        if (!tempValue.has_value()) {
            std::cerr << "Failed to set TMP environment variable!" << std::endl;
            _exit(1);
        }
        _wputenv_s(L"TMP", tempValue.value().c_str());
#else
        char* path = getenv("PWD");
        setenv("TMPDIR", path, 1);
#endif
        std::fstream tempFile;
        tempFile.open(TEMP_ERROR_OUTPUT_NAME, std::fstream::out);
        tempFile.close();
    }

    void TearDown() override
    {
        FileUtil::Remove(TEMP_ERROR_OUTPUT_NAME);
    }
};

std::string GetSignalString(std::string& signalValue, std::string& module)
{
    auto moduleStr = moduleValueMap.find(module);
    if (moduleStr == moduleValueMap.end()) {
        return "";
    }
    std::string result1 = Cangjie::ICE::MSG_PART_ONE + Cangjie::SIGNAL_MSG_PART_ONE;
    std::string result2 =
        Cangjie::SIGNAL_MSG_PART_TWO + Cangjie::ICE::MSG_PART_TWO + std::to_string(moduleStr->second) + "\n";
    if (signalValue == "StackOverflow") {
#ifdef __unix__
        return CANGJIE_COMPILER_VERSION + "\n" + result1 + std::to_string(SIGSEGV) + result2;
#elif _WIN32
        return CANGJIE_COMPILER_VERSION + "\n" + result1 + std::to_string(STACK_OVERFLOW_RETURN_CODE) + result2;
#endif
    }
    auto found = signalStringValueMap.find(signalValue);
    if (found != signalStringValueMap.end()) {
        return CANGJIE_COMPILER_VERSION + "\n" + result1 + std::to_string(found->second) + result2;
    }
    return "";
}

void VerifyDeleteTempFile()
{
#ifdef _WIN32
    FILE* fp = popen("dir", "r");
#else
    FILE* fp = popen("ls", "r");
#endif
    std::string data;
    while (true) {
        int c = fgetc(fp);
        if (c <= 0) {
            break;
        }
        data.push_back(static_cast<char>(c));
    }
    pclose(fp);
    std::string tempFileName = "cangjie-tmp-";
    const char* index = strstr(data.c_str(), tempFileName.c_str());
    EXPECT_TRUE(index == nullptr);
}

void VerifyErrorOutput(std::string signalValue, std::string module)
{
    char c;
    std::string errorStr;
    std::fstream tempFile;
    tempFile.open(TEMP_ERROR_OUTPUT_NAME);
    while (tempFile.get(c)) {
        errorStr.push_back(c);
    }
    tempFile.close();
    std::string sigStr = GetSignalString(signalValue, module);
    EXPECT_EQ(errorStr, sigStr);
    VerifyDeleteTempFile();
}

#ifdef __unix__

#define MAX_PATH 4096

int ExecuteProcess(std::string signalValue, std::string triggerPoint)
{
    std::stringstream ss;
    ss << signalValue << "_" << triggerPoint << "_" << TEMP_ERROR_OUTPUT_NAME;
    std::string commandLine = ss.str();
    char buffer[MAX_PATH] = {0};
    if (readlink("/proc/self/exe", buffer, MAX_PATH) == -1) {
        return -1;
    }
    std::string exePath = FileUtil::GetDirPath(std::string(buffer)) + "/SignalTestCJC";
    pid_t pid;
    pid_t wpid;
    int status;
    pid = fork();
    if (pid == 0) {
        if (execl(exePath.c_str(), exePath.c_str(), TEMP_CJ_FILE_NAME.c_str(), commandLine.c_str(), nullptr) == -1) {
            _exit(1);
        }
    } else if (pid > 0) {
        wpid = wait(&status);
        if (wpid == -1) {
            return -1;
        }
        if (WIFEXITED(status)) {
            return WEXITSTATUS(status); // return coce
        } else if (WIFSIGNALED(status)) {
            WTERMSIG(status); // signal code
        }
    } else {
        return -1;
    }
    return -1;
}

#elif _WIN32
DWORD ExecuteProcess(std::string signalValue, std::string triggerPoint)
{
    char buffer[MAX_PATH];
    GetModuleFileName(NULL, buffer, MAX_PATH);
    std::string exePath = FileUtil::GetDirPath(std::string(buffer)) + "\\SignalTestCJC.exe";

    std::stringstream ss;
    ss << exePath << " " << TEMP_CJ_FILE_NAME << " " << signalValue << "_" << triggerPoint << "_" << TEMP_ERROR_OUTPUT_NAME;
    std::string commandLine = ss.str();

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));
    if (!CreateProcessA(exePath.c_str(), commandLine.data(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        return 1;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code;
    if (FALSE == GetExitCodeProcess(pi.hProcess, &exit_code)) {
        return 1;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return exit_code;
}
#endif

#define CT(sig, module)                                                                                                \
    TEST_F(SignalTests, module##Signal##sig)                                                                           \
    {                                                                                                                  \
        EXPECT_EQ(ExecuteProcess(#sig, #module), sig + 128);                                                           \
        VerifyErrorOutput(#sig, #module);                                                                              \
    }

#define CTSO(module)                                                                                                   \
    TEST_F(SignalTests, module##StackOverflow)                                                                         \
    {                                                                                                                  \
        EXPECT_EQ(ExecuteProcess("StackOverflow", #module), STACK_OVERFLOW_RETURN_CODE);                                  \
        VerifyErrorOutput("StackOverflow", #module);                                                                   \
    }
CT(SIGABRT, main)
CT(SIGFPE, main)
CT(SIGSEGV, main)
CT(SIGILL, main)
#if __unix__
CT(SIGTRAP, main)
CT(SIGBUS, main)
#endif
CTSO(main)

CT(SIGABRT, parser)
CT(SIGFPE, parser)
CT(SIGSEGV, parser)
CT(SIGILL, parser)
#if __unix__
CT(SIGTRAP, parser)
CT(SIGBUS, parser)
#endif
CTSO(parser)

// CT(SIGABRT, sema)
// CT(SIGFPE, sema)
// CT(SIGSEGV, sema)
// CT(SIGILL, sema)
// #if __unix__
// CT(SIGTRAP, sema)
// CT(SIGBUS, sema)
// #endif
// CTSO(sema)

// CT(SIGABRT, chir)
// CT(SIGFPE, chir)
// CT(SIGSEGV, chir)
// CT(SIGILL, chir)
// #if __unix__
// CT(SIGTRAP, chir)
// CT(SIGBUS, chir)
// #endif
// CTSO(chir)

// CT(SIGABRT, codegen)
// CT(SIGFPE, codegen)
// CT(SIGSEGV, codegen)
// CT(SIGILL, codegen)
// #if __unix__
// CT(SIGTRAP, codegen)
// CT(SIGBUS, codegen)
// #endif
// CTSO(codegen)

// CT(SIGABRT, driver)
// CT(SIGFPE, driver)
// CT(SIGSEGV, driver)
// CT(SIGILL, driver)
// #if __unix__
// CT(SIGTRAP, driver)
// CT(SIGBUS, driver)
// #endif
// CTSO(driver)
