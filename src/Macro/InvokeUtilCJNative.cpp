// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/Macro/InvokeUtil.h"
#include "cangjie/Basic/Print.h"
#include "cangjie/Macro/InvokeConfig.h"
#include "cangjie/Utils/Utils.h"
#include "cangjie/Utils/StdUtils.h"

namespace Cangjie {
using namespace Utils;
using namespace InvokeRuntime;

namespace {
const static std::string G_CJ_RUNTIME_INIT = "InitCJRuntime";
const static std::string G_CJ_RUNTIME_FINI = "FiniCJRuntime";
const static std::string G_CJ_NEW_TASK_FROM_C = "RunCJTask";
const static std::string G_RELEASE_HANDLE_FROM_C = "ReleaseHandle";
using CangjieInitFromC = int64_t (*)(ConfigParam*);
using CangjieFiniFromC = int64_t (*)();
std::vector<HANDLE> g_openedLibHandles;
std::mutex g_openedLibMutex;

#ifdef _WIN32
// environment variable names in windows is case-insensitive, so they all uppercase in frontend global option
const static std::string G_CJHEAPSIZE = "CJHEAPSIZE";
const static std::string G_CJSTACKSIZE = "CJSTACKSIZE";
#else
const static std::string G_CJHEAPSIZE = "cjHeapSize";
const static std::string G_CJSTACKSIZE = "cjStackSize";
#endif

const static size_t UNIT_LEN = 2; // supports "kb", "mb", "gb".
const static size_t KB = 1024;
const static size_t MB = KB * KB;
const static size_t G_MIN_HEAP_SIZE = 4UL * KB;  // min: 4MB.
const static size_t G_MIN_STACK_SIZE = 64;       // min: 64KB.
const static size_t G_MAX_STACK_SIZE = 1UL * MB; // max: 1GB.

size_t GetSizeFromEnv(std::string& str)
{
    (void)str.erase(std::remove(str.begin(), str.end(), ' '), str.end());
    size_t len = str.length();
    // The last two characters are units, such as "kb".
    if (len <= UNIT_LEN) {
        return SIZE_MAX;
    }
    // Split size and unit.
    auto unit = str.substr(len - UNIT_LEN, UNIT_LEN);
    (void)std::transform(unit.begin(), unit.end(), unit.begin(), ::tolower);
    // unit must be kb mb or gb
    if (unit != "kb" && unit != "mb" && unit != "gb") {
        return SIZE_MAX;
    }
    int32_t iRes = 0;
    auto num = str.substr(0, len - UNIT_LEN);
    if (auto r = Stoi(num)) {
        // str must be a number
        iRes = *r;
        // number must > 0
        if (iRes <= 0) {
            return 0;
        }
    } else {
        return SIZE_MAX;
    }

    size_t tempSize = static_cast<size_t>(iRes);
    if (unit == "kb") {
        return tempSize;
    } else if (unit == "mb") {
        // unit: 1024 * 1KB = 1MB
        return tempSize * KB;
    } else if (unit == "gb") {
        // unit: 1024 * 1024 * 1KB = 1GB
        return tempSize * MB;
    }
    return SIZE_MAX;
}

/**
 * Get heap size from environment variable.
 * The unit must be added when configuring "cjHeapSize", it supports "kb", "mb", "gb".
 * Valid heap size must >= 4MB.
 * for example:
 *     export cjHeapSize = 16GB
 */
size_t GetHeapSizeFromEnv(std::unordered_map<std::string, std::string>& envs)
{
    if (envs.find(G_CJHEAPSIZE) == envs.end()) {
        return HEAP_SIZE;
    }
    auto heapSize = GetSizeFromEnv(envs.at(G_CJHEAPSIZE));
    if (heapSize == SIZE_MAX) {
        Warningln("unsupported cjHeapSize for macro, using 1GB as default size");
        return HEAP_SIZE;
    }
    if (heapSize < G_MIN_HEAP_SIZE) {
        Warningln("unsupported cjHeapSize for macro, must >= 4MB, using 1GB as default size");
        return HEAP_SIZE;
    }
    return heapSize;
}

/**
 * Get stack size from environment variable.
 * The unit must be added when configuring "cjStackSize", it supports "kb", "mb", "gb".
 * Valid stack size range is [64KB, 1GB].
 * for example:
 *     export cjStackSize = 128kb
 */
size_t GetStackSizeFromEnv(std::unordered_map<std::string, std::string>& envs)
{
    if (envs.find(G_CJSTACKSIZE) == envs.end()) {
        return CO_STACK_SIZE;
    }
    auto stackSize = GetSizeFromEnv(envs.at(G_CJSTACKSIZE));
    if (stackSize == SIZE_MAX) {
        Warningln("unsupported cjStackSize for macro, using 4MB as default size");
        return CO_STACK_SIZE;
    }
    if (stackSize < G_MIN_STACK_SIZE || stackSize > G_MAX_STACK_SIZE) {
        Warningln("unsupported cjStackSize for macro, the valid range is [64KB, 1GB], using 4MB as default size");
        return CO_STACK_SIZE;
    }
    return stackSize;
}
} // namespace

void InvokeRuntime::SetOpenedLibHandles(HANDLE handle)
{
    std::unique_lock<std::mutex> lock(g_openedLibMutex);
    (void)g_openedLibHandles.emplace_back(handle);
}

std::vector<HANDLE> InvokeRuntime::GetOpenedLibHandles()
{
    std::unique_lock<std::mutex> lock(g_openedLibMutex);
    return g_openedLibHandles;
}

void InvokeRuntime::ClearOpenedLibHandles()
{
    std::unique_lock<std::mutex> lock(g_openedLibMutex);
    g_openedLibHandles.clear();
}

int64_t InvokeRuntime::CallRuntime(
    const HANDLE handle, const std::string& method, std::unordered_map<std::string, std::string>& envs)
{
    auto runtimeFunc = reinterpret_cast<CangjieInitFromC>(GetMethod(handle, method.c_str()));
    if (runtimeFunc == nullptr) {
        Errorln("could not find runtime method: ", method);
        return 1;
    }
    auto heapSize = GetHeapSizeFromEnv(envs);
    HeapParam hParam = HeapParam(REGION_SIZE, heapSize, EXEMPTION_THRESHOLD, HEAP_UTILIZATION,
        HEAP_GROWTH, ALLOCATION_RATE, ALLOCATION_WAIT_TIME);
    GCParam gcParam = GCParam(GC_THRESHOLD, GARBAGE_THRESHOLD, GC_INTERVAL, BACKUP_GC_INTERNAL, GC_THREADS);
    auto logParam = LogParam(RTLogLevel::RTLOG_FATAL);
    ConcurrencyParam cParam =
        ConcurrencyParam(STACK_SIZE, GetStackSizeFromEnv(envs), PROCESSOR_NUM);

    ConfigParam param = {hParam, gcParam, logParam, cParam};
    return runtimeFunc(&param);
}

bool InvokeRuntime::PrepareRuntime(const HANDLE handle, std::unordered_map<std::string, std::string>& initArgs)
{
    auto callInit = CallRuntime(handle, G_CJ_RUNTIME_INIT, initArgs);
    if (callInit != 0) {
        Errorln("macro expansion failed because of runtime initiate failed. ");
        return false;
    }
    return true;
}

void InvokeRuntime::FinishRuntime(const HANDLE handle)
{
    auto method = GetMethod(handle, G_CJ_RUNTIME_FINI.c_str());
    auto runtimeFunc = reinterpret_cast<CangjieFiniFromC>(method);
    auto finiInit = runtimeFunc();
    if (finiInit != 0) {
        Errorln("runtime finish failed: ");
        return;
    }
}

bool RuntimeInit::InitRuntimeMethod()
{
    runtimeMethodFunc = InvokeRuntime::GetMethod(handle, std::string(G_CJ_NEW_TASK_FROM_C).c_str());
    runtimeReleaseFunc = InvokeRuntime::GetMethod(handle, std::string(G_RELEASE_HANDLE_FROM_C).c_str());
    if (runtimeMethodFunc == nullptr || runtimeReleaseFunc == nullptr) {
        auto errorInfo = runtimeMethodFunc == nullptr ? G_CJ_NEW_TASK_FROM_C : G_RELEASE_HANDLE_FROM_C;
        Errorln("could not find the create task method: ", errorInfo);
        return false;
    }
    return true;
}

void RuntimeInit::CloseMacroDynamicLibrary()
{
    int retCode = -1;
    for (auto& openedLib : InvokeRuntime::GetOpenedLibHandles()) {
        retCode = InvokeRuntime::CloseSymbolTable(openedLib);
        if (retCode != 0) {
            Errorln("close macro related dynamic library failed: ");
            return;
        }
    }
    InvokeRuntime::ClearOpenedLibHandles();
}
} // namespace Cangjie