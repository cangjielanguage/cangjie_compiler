// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#define main cjc
#include "../../src/main.cpp"
#undef main

#include <fcntl.h>
#include <cstdio>
#include <unordered_map>
#include <vector>

namespace {
#define SIGCBF(sig)                                                                                                    \
    void sig##Callback()                                                                                               \
    {                                                                                                                  \
        if (raise(sig) == 0) {                                                                                         \
            while (1) {                                                                                                \
            }                                                                                                          \
        }                                                                                                              \
    }

SIGCBF(SIGABRT)
SIGCBF(SIGFPE)
SIGCBF(SIGSEGV)
SIGCBF(SIGILL)

#ifdef __unix__
SIGCBF(SIGTRAP)
SIGCBF(SIGBUS)
#endif
SIGCBF(SIGINT)

void RecursiveFunction(int* arr, int size)
{
    if (size == 0) {
        return;
    }
    int r = rand() % size;
    int* a = (int*)alloca(size * sizeof(int));
    for (int i = 0; i < size; i++) {
        a[i] = arr[r] * arr[i];
    }
    RecursiveFunction(a, size - 1);
}

void StackOverflowCallback()
{
    int num = rand();
    int arraySize = 102400;
    int* a = (int*)alloca(arraySize * sizeof(int));
    for (int i = 0; i < arraySize; i++) {
        a[i] = num * i + a[i];
    }
    RecursiveFunction(a, arraySize);
}

#ifdef __unix__
const std::unordered_map<std::string, Cangjie::SignalTest::SignalTestCallbackFuncType> signalStringCallbackFuncMap = {
    {"SIGABRT", SIGABRTCallback}, {"SIGFPE", SIGFPECallback}, {"SIGSEGV", SIGSEGVCallback}, {"SIGILL", SIGILLCallback},
    {"SIGTRAP", SIGTRAPCallback}, {"SIGBUS", SIGBUSCallback}, {"StackOverflow", StackOverflowCallback},
    {"SIGINT", SIGINTCallback}};
#elif _WIN32
const std::unordered_map<std::string, Cangjie::SignalTest::SignalTestCallbackFuncType> signalStringCallbackFuncMap = {
    {"SIGABRT", SIGABRTCallback}, {"SIGFPE", SIGFPECallback}, {"SIGSEGV", SIGSEGVCallback}, {"SIGILL", SIGILLCallback},
    {"StackOverflow", StackOverflowCallback}, {"SIGINT", SIGINTCallback}};
#endif

const std::unordered_map<std::string, Cangjie::SignalTest::TriggerPointer> stringTriggerPointerMap = {
    {"main", Cangjie::SignalTest::TriggerPointer::MAIN_POINTER},
    {"driver", Cangjie::SignalTest::TriggerPointer::DRIVER_POINTER},
    {"parser", Cangjie::SignalTest::TriggerPointer::PARSER_POINTER},
    {"sema", Cangjie::SignalTest::TriggerPointer::SEMA_POINTER},
    {"chir", Cangjie::SignalTest::TriggerPointer::CHIR_POINTER},
    {"codegen", Cangjie::SignalTest::TriggerPointer::CODEGEN_POINTER}};

// Linux:   SIGABRT, SIGFPE, SIGSEGV, SIGILL, SIGTRAP, SIGBUS
// Windows: SIGABRT, SIGFPE, SIGSEGV, SIGILL
void SetCallBackFunc(std::string arg)
{
    Cangjie::SignalTest::SignalTestCallbackFuncType fp = nullptr;
    Cangjie::SignalTest::TriggerPointer tp = Cangjie::SignalTest::TriggerPointer::NON_POINTER;
    auto pos = arg.find('_');
    if (pos == std::string::npos) {
        return;
    }
    std::string fpStr = arg.substr(0, pos);
    std::string tempStr = arg.substr(pos + 1, arg.size() - pos - 1);

    pos = tempStr.find('_');
    if (pos == std::string::npos) {
        return;
    }
    std::string tpStr = tempStr.substr(0, pos);
    int errorFd = STDERR_FILENO;
    if (fpStr != "SIGINT") {
        std::string fdStr = tempStr.substr(pos + 1, tempStr.size() - pos - 1);
        errorFd = open(fdStr.c_str(), O_WRONLY);
    }

    auto callbackFuncFound = signalStringCallbackFuncMap.find(fpStr);
    if (callbackFuncFound != signalStringCallbackFuncMap.end()) {
        fp = callbackFuncFound->second;
    } else {
        return;
    }

    auto triggerPointerFound = stringTriggerPointerMap.find(tpStr);
    if (triggerPointerFound != stringTriggerPointerMap.end()) {
        tp = triggerPointerFound->second;
    } else {
        fp = nullptr;
        return;
    }
    Cangjie::SignalTest::SetSignalTestCallbackFunc(fp, tp, errorFd);
}
} // namespace

int main(int argc, const char** argv, const char** envp)
{
    SetCallBackFunc(std::string(argv[argc - 1]));
    std::vector<char*> tempArgv;
    for (int i = 0; i < argc - 1; i++) {
        tempArgv.emplace_back(const_cast<char*>(argv[i]));
    }
    tempArgv.emplace_back(nullptr);
    return cjc(tempArgv.size() - 1, const_cast<const char**>(tempArgv.data()), envp);
}
