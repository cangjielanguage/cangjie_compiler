// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements Tool class and its functions.
 */

#include "cangjie/Driver/Tool.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <spawn.h>
#include <cstring>
#include <sys/wait.h>
#endif
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <type_traits>

#include "cangjie/Utils/FileUtil.h"
#include "cangjie/Utils/Semaphore.h"
#include "cangjie/Driver/TempFileManager.h"
#include "cangjie/Driver/Utils.h"
#include "cangjie/Driver/ResponseFilePolicy.h"
#ifdef _WIN32
#include "cangjie/Basic/StringConvertor.h"
#endif

using namespace Cangjie;
namespace {
#ifdef _WIN32
std::string GetSystemErrorMessage(DWORD errCode)
{
    if (errCode == 0) {
        return "";
    }

    LPWSTR wMsgBuf = nullptr;

    DWORD size =
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL, errCode, MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT), (LPWSTR)&wMsgBuf, 0, NULL);
    if (size == 0 || wMsgBuf == nullptr) {
        return "Unknown system error code: " + std::to_string(errCode);
    }

    std::wstring wmessage(wMsgBuf, size);

    LocalFree(wMsgBuf);

    while (!wmessage.empty() && (wmessage.back() == L'\r' || wmessage.back() == L'\n')) {
        wmessage.pop_back();
    }

    if (wmessage.empty()) {
        return "";
    }
    int utf8_size = WideCharToMultiByte(CP_UTF8, 0, wmessage.c_str(), (int)wmessage.length(), NULL, 0, NULL, NULL);
    std::string result(utf8_size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wmessage.c_str(), (int)wmessage.length(), &result[0], utf8_size, NULL, NULL);

    return result;
}
#else
std::string GetSystemErrorMessage(int error)
{
    constexpr size_t buffSize = 512;
    char buf[buffSize] = {0};
    auto handleError = [&buf](auto res) -> std::string {
        using T = decltype(res);

        if constexpr (std::is_integral_v<T>) {
            // POSIX
            return res != 0 ? "" : std::string(buf);
        } else {
            // GNU
            return res ? std::string(res) : "";
        }
    };
    // Generic lambda defers semantic checks until instantiation.
    return handleError(strerror_r(error, buf, buffSize));
}
#endif
} // namespace
#ifdef __APPLE__
const static std::string LD_LIBRARY_PATH = "DYLD_LIBRARY_PATH";
const static std::string LD_PRELOAD = "DYLD_INSERT_LIBRARIES";
#elif !defined(_WIN32)
const static std::string LD_LIBRARY_PATH = "LD_LIBRARY_PATH";
const static std::string LD_PRELOAD = "LD_PRELOAD";
#endif

void Tool::AppendMultiArgs(const std::string& argStr)
{
    auto splitArgs = Utils::SplitString(argStr, " ");
    for (const auto& a : splitArgs) {
        if (!a.empty()) {
            args.emplace_back(a);
            argKinds.push_back(ArgKind::Literal);
        }
    }
}

void Tool::AppendArgsFromDirAtExecution(const std::string& dir, const std::string& extension)
{
    size_t deferredIdx = deferredDirArgs.size();
    deferredDirArgs.push_back({dir, extension});
    deferredArgMap[args.size()] = deferredIdx;
    args.emplace_back("<expand_dir:" + extension + ">");
    argKinds.push_back(ArgKind::DeferredDir);
}

std::vector<std::string> Tool::ExpandArgs() const
{
    if (deferredArgMap.empty())
        return args;

    std::vector<std::string> expandedArgs;
    for (size_t pos = 0; pos < args.size(); ++pos) {
        if (argKinds[pos] != ArgKind::DeferredDir) {
            expandedArgs.emplace_back(args[pos]);
            continue;
        }
        const auto& deferredArg = deferredDirArgs[deferredArgMap.at(pos)];
        // GetFileExtension finds the last dot, so "lto.o" → "o".
        auto files = FileUtil::GetAllFilesUnderCurrentPath(
            deferredArg.dir,
            FileUtil::GetFileExtension(deferredArg.extension));
        std::sort(files.begin(), files.end());
        for (const auto& file : files) {
            expandedArgs.emplace_back(FileUtil::JoinPath(deferredArg.dir, file));
        }
    }
    return expandedArgs;
}

std::string Tool::GenerateCommand() const
{
    std::string tempCommand;
    std::string argStr;
    auto argList = ExpandArgs();
    std::for_each(argList.begin(), argList.end(), [&argStr](const std::string& s) {
#ifdef _WIN32
        argStr = argStr + FileUtil::GetQuoted(s) + " ";
#else
        argStr = argStr + GetSingleQuoted(s) + " ";
#endif
    });
#ifndef _WIN32
    if (!GetLdLibraryPath().empty()) {
        tempCommand = "LD_LIBRARY_PATH=" + GetSingleQuoted(GetLdLibraryPath()) + ":$" + LD_LIBRARY_PATH + " ";
    }
#endif

#ifdef _WIN32
    tempCommand += FileUtil::GetQuoted(name);
#else
    tempCommand += GetSingleQuoted(name);
#endif
    tempCommand += " " + argStr;
#ifdef _WIN32
    // Command run as `cmd /C`. The leading double quote will be removed by `cmd` causing the semantic of command
    // changes. Extra pair of double quotes is added to make the command works when there is leading double quote.
    return "\"" + tempCommand + "\"";
#else
    return tempCommand;
#endif
}

std::string Tool::GenerateCommandFromExecArgs(const std::vector<std::string>& execArgs) const
{
    // Like GenerateCommand(), but formats the already-resolved execArgs (with
    // the program name at index 0, possibly a single `@<rsp>` entry) so the
    // verbose output reflects what is actually spawned.
    std::string tempCommand;
    std::string argStr;
    for (size_t i = 1; i < execArgs.size(); ++i) {
#ifdef _WIN32
        argStr = argStr + FileUtil::GetQuoted(execArgs[i]) + " ";
#else
        argStr = argStr + GetSingleQuoted(execArgs[i]) + " ";
#endif
    }
#ifndef _WIN32
    if (!GetLdLibraryPath().empty()) {
        tempCommand = "LD_LIBRARY_PATH=" + GetSingleQuoted(GetLdLibraryPath()) + ":$" + LD_LIBRARY_PATH + " ";
    }
#endif
    const std::string& progName = execArgs.empty() ? name : execArgs[0];
#ifdef _WIN32
    tempCommand += FileUtil::GetQuoted(progName);
#else
    tempCommand += GetSingleQuoted(progName);
#endif
    tempCommand += " " + argStr;
#ifdef _WIN32
    return "\"" + tempCommand + "\"";
#else
    return tempCommand;
#endif
}

bool Tool::InternalImplementedCommandExec() const
{
    bool res = false;
    if (type == ToolType::INTERNAL_IMPLEMENTED && name == "RemoveFile") {
        auto& arguments = GetFullArgs();
        // arguments[1] - absolute name of file to remove
        return !FileUtil::FileExist(arguments[1]) || FileUtil::Remove(arguments[1]);
    }
    if (type == ToolType::INTERNAL_IMPLEMENTED && name == "CacheCopy") {
        auto& arguments = GetFullArgs();
        // arguments[0] - name of command
        // arguments[1] - absolute name of srcFile
        std::ifstream srcStream(arguments[1], std::ios::binary);
        // arguments[2] - absolute name of destFile
        std::ofstream destStream(arguments[2], std::ios::binary);
        destStream << srcStream.rdbuf();
        destStream.close();
#ifdef _WIN32
        FileUtil::HideFile(FileUtil::GetDirPath(arguments[2]));
#endif
        res = true;
    }
    return res;
}

std::unique_ptr<ToolFuture> Tool::Execute(bool verbose) const
{
    // Resolve the final exec args: name + expanded args, or name + @rsp when
    // the policy says the command line is too long (or CJC_FORCE_RSP=1
    // forces it for testing on platforms where the real threshold is high).
    std::vector<std::string> execArgs;
    execArgs.push_back(name);
    std::vector<std::string> expandedArgs = ExpandArgs();

    const ResponseFilePolicy policy = LookupResponseFilePolicy(name);
    bool useRsp = false;
    if (policy.supported) {
        std::vector<std::string> fullArgs = execArgs;
        fullArgs.insert(fullArgs.end(), expandedArgs.begin(), expandedArgs.end());
        const size_t cmdLen = ComputeCommandLineLength(fullArgs);
        const char* forceEnv = std::getenv("CJC_FORCE_RSP");
        const bool forced = (forceEnv != nullptr && forceEnv[0] != '\0' && std::string(forceEnv) != "0");
        if (cmdLen >= policy.threshold || forced) {
            useRsp = true;
        }
    }

    if (useRsp) {
        // Build the full args vector (with name at index 0) and let the
        // serializer skip argv[0]. Style follows the host OS: Windows-hosted
        // ld.lld reads rsp with CRT rules, the rest with GNU rules.
        std::vector<std::string> fullArgs = execArgs;
        fullArgs.insert(fullArgs.end(), expandedArgs.begin(), expandedArgs.end());
        std::string content = SerializeRspForHost(fullArgs);
        std::string rspPath = TempFileManager::Instance().CreateRspFile(content);
        if (rspPath.empty()) {
            // Write failed; fall back to original args so the tool still runs
            // (and reports its own failure mode if the command is genuinely
            // too long).
            useRsp = false;
        } else {
            execArgs.push_back("@" + rspPath);
        }
    }
    if (!useRsp) {
        execArgs.insert(execArgs.end(), expandedArgs.begin(), expandedArgs.end());
    }

    if (verbose) {
        Println(FileUtil::Normalize(GenerateCommandFromExecArgs(execArgs)));
    }
    // If the file is deleted, the subsequent commands are not executed.
    if (TempFileManager::Instance().IsDeleted()) {
        return nullptr;
    }
    return Run(execArgs);
}

#ifdef _WIN32
std::unique_ptr<ToolFuture> Tool::Run(const std::vector<std::string>& execArgs) const
{
    if (type == ToolType::INTERNAL_IMPLEMENTED) {
        Utils::Semaphore::Get().Acquire();
        return std::make_unique<ThreadFuture>(std::async(&Tool::InternalImplementedCommandExec, this));
    }
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    // Use the rsp-aware execArgs passed by Execute() (which already contains
    // the program name at index 0). This replaces the historical
    // `GetFullArgs()` + ostringstream+std::quoted block; the same quoting is
    // now shared via BuildWindowsCommandLine so the measured length equals
    // the actual string passed to CreateProcessA.
    std::string commandLine = BuildWindowsCommandLine(execArgs);

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));
    Utils::Semaphore::Get().Acquire();
    if (!CreateProcessA(
        name.c_str(), const_cast<char*>(commandLine.c_str()), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        Utils::Semaphore::Get().Release();
        return std::make_unique<ErrorFuture>(GetSystemErrorMessage(GetLastError()));
    }

    return std::make_unique<WindowsProcessFuture>(pi);
}
#else
std::unique_ptr<ToolFuture> Tool::Run(const std::vector<std::string>& execArgs) const
{
    if (type == ToolType::INTERNAL_IMPLEMENTED) {
        Utils::Semaphore::Get().Acquire();
        return std::make_unique<ThreadFuture>(std::async(&Tool::InternalImplementedCommandExec, this));
    }
    // execArgs already contains the program name at index 0; either the
    // original expanded args or {name, "@<rsp>"} when rsp fallback was used.
    std::vector<char*> rawArgumentArray = {};
    rawArgumentArray.resize(execArgs.size() + 1);
    size_t index = 0;
    while (index < execArgs.size()) {
        rawArgumentArray[index] = const_cast<char*>(execArgs[index].c_str());
        index++;
    }
    rawArgumentArray[index] = nullptr;

    // Note: `childEnvironment` must be alive until the call of `posix_spawn` finishes. The c pointer
    // of environment we get will be invalid if `childEnvironment` is destroyed too early.
    std::list<std::string> childEnvironment = BuildEnvironmentVector();

    // Make an array for envp argument.
    std::vector<char*> rawEnvironmentArray;
    for (const std::string& s : std::as_const(childEnvironment)) {
        rawEnvironmentArray.emplace_back(const_cast<char*>(s.c_str()));
    }
    rawEnvironmentArray.push_back(nullptr);

    pid_t pid;
    int status = 0;
    Utils::Semaphore::Get().Acquire();
    // If name has no directory separator, then it should be a program name that needs to be
    // searched in PATH. `posix_spawnp` does program searches for us.
    if (name.find_first_of(DIR_SEPARATOR) == std::string::npos) {
        status =
            posix_spawnp(&pid, name.c_str(), nullptr, nullptr, rawArgumentArray.data(), rawEnvironmentArray.data());
    } else {
        status = posix_spawn(&pid, name.c_str(), nullptr, nullptr, rawArgumentArray.data(), rawEnvironmentArray.data());
    }

    // Failed to start the child process
    if (status != 0) {
        Utils::Semaphore::Get().Release();
        return std::make_unique<ErrorFuture>(GetSystemErrorMessage(status));
    }

    return std::make_unique<LinuxProcessFuture>(pid);
}

std::list<std::string> Tool::BuildEnvironmentVector() const
{
    std::list<std::string> environment;
    // Append our ld library path to the front of LD_LIBRARY_PATH
    if (ldLibraryPath.empty()) {
        if (environmentVars.find(LD_LIBRARY_PATH) != environmentVars.end()) {
            environment.emplace_back(LD_LIBRARY_PATH + "=" + environmentVars.at(LD_LIBRARY_PATH));
        }
    } else {
        std::string newLdLibraryPath = ldLibraryPath;
        // If LD_LIBRARY_PATH is empty, we don't append `:` or ld.so will treat the trailing empty path as ".".
        // See documentation of ld.so for details.
        if (environmentVars.find(LD_LIBRARY_PATH) != environmentVars.end() &&
            !environmentVars.at(LD_LIBRARY_PATH).empty()) {
            newLdLibraryPath += ":" + environmentVars.at(LD_LIBRARY_PATH);
        }
        environment.emplace_back(LD_LIBRARY_PATH + "=" + newLdLibraryPath);
    }

    const static std::unordered_set<std::string> blocklist = {
        // Linux specific
        LD_LIBRARY_PATH,
        LD_PRELOAD,          // Preload shared libraries
        "LD_AUDIT",          // Audit libraries
        "LD_DEBUG",          // Debugging
        "LD_PROFILE",        // Profiling
        "LD_TRACE_LOADED_OBJECTS", // Trace loaded objects

        // macOS specific
        "DYLD_INSERT_LIBRARIES",       // Preload libraries on macOS
        "DYLD_FRAMEWORK_PATH",         // Framework search path
        "DYLD_FALLBACK_FRAMEWORK_PATH", // Fallback framework path
        "DYLD_FALLBACK_LIBRARY_PATH",   // Fallback library path
        "DYLD_VERSIONED_FRAMEWORK_PATH", // Versioned framework path
        "DYLD_VERSIONED_LIBRARY_PATH",   // Versioned library path
        "DYLD_PRINT_LIBRARIES",         // Print loaded libraries
        "DYLD_PRINT_TO_FILE",           // Print to file

        // Compiler specific
        "RUSTC_WRAPPER",     // Rust compiler wrapper
        "LLVM_SYS_*",        // LLVM system variables
        "MLIR_SYS_*",        // MLIR system variables
        "TABLEGEN_*",        // TableGen variables
    };

    for (const auto& environmentVar : environmentVars) {
        // Skip blocklisted environment variables
        if (blocklist.find(environmentVar.first) != blocklist.end()) {
            continue;
        }
        // Also skip variables that match patterns like LLVM_SYS_*, MLIR_SYS_*, TABLEGEN_*
        if (environmentVar.first.find("LLVM_SYS_") == 0 ||
            environmentVar.first.find("MLIR_SYS_") == 0 ||
            environmentVar.first.find("TABLEGEN_") == 0) {
            continue;
        }
        environment.emplace_back(environmentVar.first + "=" + environmentVar.second);
    }
    return environment;
}
#endif
