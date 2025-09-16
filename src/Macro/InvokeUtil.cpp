// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/Macro/InvokeUtil.h"
#include "cangjie/Basic/Print.h"
#ifdef _WIN32
#include <windows.h>
#endif

namespace Cangjie {
using namespace Utils;
using namespace InvokeRuntime;

#ifdef _WIN32
#ifdef UNICODE
#define LoadLibrary LoadLibraryW
#else
#define LoadLibrary LoadLibraryA
#endif

HANDLE InvokeRuntime::OpenSymbolTable(const std::string& libPath)
{
    HANDLE handle = LoadLibrary(libPath.c_str());
    // Judge load dynamic lib correctly or not.
    if (!handle) {
        Errorln("could not load the dynamic library: ", libPath);
    }
    return handle;
}
#elif defined(__linux__) || defined(__APPLE__)
HANDLE InvokeRuntime::OpenSymbolTable(const std::string& libPath, int dlopenMode)
{
    HANDLE handle = nullptr;
    auto realpathRes = realpath(libPath.c_str(), nullptr);
    if (!realpathRes) {
        Errorln("could not get realpath of library: ", libPath);
        return handle;
    }
    handle = dlopen(realpathRes, dlopenMode);
    free(realpathRes);
    // Judge load dynamic lib correctly or not.
    if (!handle) {
        Errorln("could not load the dynamic library: ", libPath);
        Errorln("error info is: ", dlerror());
    }
    return handle;
}
#endif

HANDLE InvokeRuntime::GetMethod(HANDLE handle, const char* name)
{
#ifdef _WIN32
    return (HANDLE)GetProcAddress((HMODULE)handle, name);
#elif defined(__linux__) || defined(__APPLE__)
    return dlsym(handle, name);
#else
    CJC_ASSERT(false);
    return nullptr;
#endif
}

int InvokeRuntime::CloseSymbolTable(HANDLE handle)
{
    int retCode = -1;
#ifdef _WIN32
    if (FreeLibrary(reinterpret_cast<HMODULE>(handle))) {
        retCode = 0;
    }
#elif defined(__linux__) || defined(__APPLE__)
    retCode = dlclose(handle);
#endif
    return retCode;
}

RuntimeInit& RuntimeInit::GetInstance()
{
    static RuntimeInit runtimeInit;
    return runtimeInit;
}

bool RuntimeInit::InitRuntime(const std::string& runtimeLibPath, InvokeRuntime::RuntimeInitArg initArgs)
{
    std::unique_lock<std::mutex> lock(mutex);
    if (!initRuntime) {
        handle = InvokeRuntime::OpenSymbolTable(runtimeLibPath);
        if (handle) {
            bool ret = InvokeRuntime::PrepareRuntime(handle, initArgs);
            if (!ret) {
                return false;
            }
        }
        bool ret = InitRuntimeMethod();
        if (!ret) {
            return false;
        }
        initRuntime = true;
    }
    return true;
}

void RuntimeInit::CloseRuntime()
{
    if (handle != nullptr) {
        // if PrepareRuntime failed, initRuntime is false, no need to FinishRuntime.
        if (initRuntime) {
            InvokeRuntime::FinishRuntime(handle);
            initRuntime = false;
        }
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
        InvokeRuntime::CloseSymbolTable(handle);
#endif
        handle = nullptr;
    }
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    // close macro dynamic library
    CloseMacroDynamicLibrary();
#endif
}

MacroProcMsger& MacroProcMsger::GetInstance()
{
    static MacroProcMsger macProcMsger;
    return macProcMsger;
}
}