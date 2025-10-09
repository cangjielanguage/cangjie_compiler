// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares basic invoke related apis.
 */

#ifndef CANGJIE_UTILS_INVOKE_H
#define CANGJIE_UTILS_INVOKE_H

#include <condition_variable>
#include <list>
#include <mutex>
#include <string>
#include <atomic>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#if defined(__linux__) || defined(__APPLE__)
#include <dlfcn.h>
#endif

namespace Cangjie {
namespace InvokeRuntime {
// Declare 32-byte alignment to ensure c++ generate same function definition as cangjie IR in both x86 and arm64.
    struct UnsafePtrType {
        uint8_t* ptr;
    } __attribute__((aligned(32)));

    const size_t REGION_SIZE = 64;
    const size_t HEAP_SIZE = 1024 * 1024;
    const double EXEMPTION_THRESHOLD = 0.8;
    const double HEAP_UTILIZATION = 0.6;
    const double HEAP_GROWTH = 0.15;
    const double ALLOCATION_RATE = 10240;
    const size_t ALLOCATION_WAIT_TIME = 1000;

    const size_t GC_THRESHOLD = 20;
    const double GARBAGE_THRESHOLD = 0.5;
    const uint64_t GC_INTERVAL = 150;
    const uint64_t BACKUP_GC_INTERNAL = 240;
    const uint32_t GC_THREADS = 8;

    const size_t STACK_SIZE = 64 * 1024;
    const size_t CO_STACK_SIZE = 4 * 1024;
    const uint32_t PROCESSOR_NUM = 24;

    const size_t HEAP_INITIAL_SIZE = 64 * 1024;

    using CommonFuncPtrT = uint8_t* (*)(void*, const int64_t, void*);
    using AttrFuncPtrT = uint8_t* (*)(void*, const int64_t, void*, const int64_t, void*);
    using HANDLE = void*;
    using InitGlobalFuncPtr = void* (*)(void*);
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    using RuntimeInitArg = std::unordered_map<std::string, std::string>;
#endif

#ifdef _WIN32
    /**
     * @brief Open dynamic lib, save it to singleton variable.
     *
     * @param libPath Dynamic library path.
     * @return HANDLE Handle for the dynamic library.
     */
    HANDLE OpenSymbolTable(const std::string& libPath);
#elif defined(__linux__) || defined(__APPLE__)
    /**
     * @brief Open dynamic lib, save it to singleton variable.
     *
     * @param libPath Dynamic library path.
     * @param dlopenMode The MODE argument to `dlopen'
     * @return
     */
    HANDLE OpenSymbolTable(const std::string& libPath, int dlopenMode = RTLD_LAZY | RTLD_GLOBAL);
#endif

    /**
     * @brief Get method's address from handle of dynamic library.
     *
     * @param HANDLE Handle for the dynamic library.
     * @param name Name of method.
     * @return HANDLE Symbol's address for the method.
     */
    HANDLE GetMethod(HANDLE handle, const char* name);

    /**
     * @brief Prepare cj runtime, call runtime init.
     *
     * @param HANDLE Handle for the dynamic library.
     * @param initArgs The arguments for initialize runtime.
     * @return bool Return true if the initialization is successful; otherwise, false.
     */
    bool PrepareRuntime(const HANDLE handle, InvokeRuntime::RuntimeInitArg& initArgs);

    /**
     * @brief Finish cj runtime, call runtime finish.
     *
     * @param HANDLE Handle for the dynamic library.
     */
    void FinishRuntime(const HANDLE handle);

    /**
     * @brief call methods by name and invoke it from dynamic library.
     *
     * @param HANDLE Handle for the dynamic library.
     * @param method name of method.
     * @param envs environment variable.
     * @return int64_t Return 0 if call runtime successed; otherwise, failed.
     */
    int64_t CallRuntime(const HANDLE handle, const std::string& method, std::unordered_map<std::string,
                                                                            std::string>& envs);

    /**
     * @brief Close dynamic lib.
     *
     * @param HANDLE Handle for the dynamic library.
     * @return int Return 0 if close successfully; otherwise, failed.
     */
    int CloseSymbolTable(HANDLE handle);

    /**
     * @brief get libs that has been open.
     *
     * @return std::vector<HANDLE> opened llibrary Handle.
     */
    std::vector<HANDLE> GetOpenedLibHandles();

    /**
     * @brief set libs that has been open.
     *
     * @param HANDLE Handle for the dynamic library.
     */
    void SetOpenedLibHandles(HANDLE handle);

    /**
     * @brief clear libs vector that has been open.
     */
    void ClearOpenedLibHandles();
} // namespace Invoke

class RuntimeInit {
public:
    static RuntimeInit& GetInstance();
    bool InitRuntime(const std::string& runtimeLibPath, InvokeRuntime::RuntimeInitArg initArgs = {});
    void CloseRuntime();
    ~RuntimeInit() = default;

    InvokeRuntime::HANDLE GetHandle()
    {
        return handle;
    }

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    void* runtimeMethodFunc{nullptr};
    void* runtimeReleaseFunc{nullptr};
#endif
private:
    /**
     * Close macro dynamic library.
     */
    void CloseMacroDynamicLibrary();
    bool InitRuntimeMethod();
    bool initRuntime{false};
    InvokeRuntime::HANDLE handle{nullptr};
    std::mutex mutex;
};

class MacroProcMsger {
public:
    static MacroProcMsger& GetInstance();
    ~MacroProcMsger() = default;
    std::atomic_bool macroSrvRun{false};
    std::atomic_bool pipeError{false};
    std::mutex mutex;
#ifdef _WIN32
    typedef void *HANDLE;
    HANDLE hParentRead{};
    HANDLE hParentWrite{};
     // server process info
    HANDLE hChildRead{};
    HANDLE hChildWrite{};
    HANDLE hProcess{};
    HANDLE hThread{};
#else
    int pipefdP2C[2]{-1, -1}; // 0 for srv read from cli, 1 for cli write msg to srv
    int pipefdC2P[2]{-1, -1}; // 0 for cli read from srv, 1 for srv write msg to cli
#endif
    // client
    void CloseMacroSrv();
    bool SendMsgToSrv(const std::vector<uint8_t>& msg);
    bool ReadMsgFromSrv(std::vector<uint8_t>& msg);
    bool ReadAllMsgFromSrv(std::list<std::vector<uint8_t>>& msgVec);
    void CloseClientResource();
    // server
    bool SendMsgToClient(const std::vector<uint8_t>& msg);
    bool ReadMsgFromClient(std::vector<uint8_t>& msg);
#ifdef _WIN32
    void SetSrvPipeHandle(HANDLE hRead, HANDLE hWrite);
#else
    void SetSrvPipeHandle(int hRead, int hWrite);
#endif
private:
    MacroProcMsger(){};
    bool WriteToSrvPipe(const uint8_t* buf, size_t size) const;
    bool ReadFromSrvPipe(uint8_t* buf, size_t size) const;
    bool WriteToClientPipe(const uint8_t* buf, size_t size) const;
    bool ReadFromClientPipe(uint8_t* buf, size_t size) const;
    const size_t msgSliceLen = 4096; // Pipe cache is limited, too long messages cannot be written.
};
} // namespace Cangjie

#endif // CANGJIE_UTILS_INVOKE_H
