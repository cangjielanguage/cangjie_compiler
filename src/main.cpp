// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file is the main entry of compiler.
 */

#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Driver/Driver.h"
#include "cangjie/Driver/TempFileManager.h"
#include "cangjie/FrontendTool/FrontendTool.h"
#include "cangjie/Utils/FileUtil.h"

#include <memory>
#include <exception>
#include <string>
#ifdef _WIN32
#include <algorithm>
#include <libloaderapi.h>
#endif

#include "cangjie/Basic/Print.h"
#include "cangjie/Utils/Signal.h"

namespace {

#if (defined RELEASE)
void RegisterSignalHandler()
{
#if (defined __unix__)
    Cangjie::CreateAltSignalStack();
#elif (defined _WIN32)
    Cangjie::RegisterCrashExceptionHandler();
#endif
    Cangjie::RegisterCrashSignalHandler();
}
#endif

const int EXIT_CODE_SUCCESS = 0;
const int EXIT_CODE_ERROR = 1; // Normal compiler error

} // namespace

using namespace Cangjie;

int main(int argc, const char** argv, const char** envp)
{
#ifndef CANGJIE_ENABLE_GCOV
    try {
#endif
#if (defined RELEASE)
        RegisterSignalHandler();
#endif
        RegisterCrtlCSignalHandler();
        // Convert all arguments to string list.
        std::vector<std::string> args = Utils::StringifyArgumentVector(argc, argv);
        std::unordered_map<std::string, std::string> environmentVars = Utils::StringifyEnvironmentPointer(envp);
        SourceManager sm;
        DiagnosticEngine diag;
        diag.SetSourceManager(&sm);
#ifdef _WIN32
        auto maybeExePath = Utils::GetApplicationPath();
#else
        auto maybeExePath = Utils::GetApplicationPath(args[0], environmentVars);
#endif
        if (!maybeExePath.has_value()) {
            return EXIT_CODE_ERROR;
        }
        std::string exePath = maybeExePath.value();
        std::string exeName = FileUtil::GetFileName(args[0]);
        // The program is executed by the symbolic link `cjc-frontend`. Run in Frontend mode instead of Driver mode.
        if (exeName == "cjc-frontend" || exeName == "cjc-frontend.exe") {
            auto ret = ExecuteFrontend(exePath, args, environmentVars);
            RuntimeInit::GetInstance().CloseRuntime();
            TempFileManager::Instance().DeleteTempFiles();
            return ret;
        }
#ifdef SIGNAL_TEST
        // The interrupt signal triggers the function. In normal cases, this function does not take effect.
        Cangjie::SignalTest::ExecuteSignalTestCallbackFunc(Cangjie::SignalTest::TriggerPointer::MAIN_POINTER);
#endif
        std::unique_ptr<Driver> driver = std::make_unique<Driver>(args, diag, exePath);
        driver->EnvironmentSetup(environmentVars);
        if (!driver->ParseArgs()) {
            // Driver should have printed error messages,
            // but if driver didn't, users may be confused since cjc did neither compilation
            // nor error reporting. Therefore, we add an error message (and also a help message) here.
            WriteError("Invalid options. Try: 'cjc --help' for more information.\n");
            return EXIT_CODE_ERROR;
        }
        auto res = driver->ExecuteCompilation();
        TempFileManager::Instance().DeleteTempFiles();
        if (!res) {
            RuntimeInit::GetInstance().CloseRuntime();
            return EXIT_CODE_ERROR;
        }
        RuntimeInit::GetInstance().CloseRuntime();
#ifndef CANGJIE_ENABLE_GCOV
    } catch (const NullPointerException& nullPointerException) {
        Cangjie::ICE::TriggerPointSetter iceSetter(nullPointerException.GetTriggerPoint());
        InternalError("null pointer");
    }
#endif
    return EXIT_CODE_SUCCESS;
}
