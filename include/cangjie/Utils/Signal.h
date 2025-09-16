// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares crash signal handler related functions.
 */

#ifndef CANGJIE_UTILS_SIGNAL_H
#define CANGJIE_UTILS_SIGNAL_H

#if (defined RELEASE)
#include "cangjie/Utils/ICEUtil.h"
#include "cangjie/Utils/FileUtil.h"


#ifdef __unix__
#include <csignal>
#include <functional>
#elif __APPLE__
#include <signal.h>
#elif _WIN32
#include <signal.h>
#include <windows.h>
#endif

namespace Cangjie {

#if defined(__unix__) || defined(__APPLE__)
const std::string SIGNAL_MSG_PART_ONE = "Interrupt signal (";
/* Create alternate signal stack. */
void CreateAltSignalStack();

#elif _WIN32
const std::string SIGNAL_MSG_PART_ONE = "Windows unexpected exception code (";
void RegisterCrashExceptionHandler();
#endif
const std::string SIGNAL_MSG_PART_TWO = ") received.";

/* Register signal handler for crash signals. */
void RegisterCrashSignalHandler();

#ifdef CANGJIE_BUILD_TESTS
#define SIGNAL_TEST
namespace SignalTest {
using SignalTestCallbackFuncType = void (*)(void);

enum TriggerPointer {
    NON_POINTER,    // The test callback function is not executed.
    MAIN_POINTER,   // Execute the test callback function inserted in the main func.
    DRIVER_POINTER, // Execute the test callback function inserted in the Driver module.
    PARSER_POINTER, // Execute the test callback function inserted in the Parser module.
    SEMA_POINTER,   // Execute the test callback function inserted in the Sema module.
    CHIR_POINTER,   // Execute the test callback function inserted in the CHIR module.
    CODEGEN_POINTER // Execute the test callback function inserted in the CodeGen module.
};
void SetSignalTestCallbackFunc(SignalTestCallbackFuncType fp, TriggerPointer pointerType, int errorCodeOffset);
void ExecuteSignalTestCallbackFunc(TriggerPointer executionPoint);
} // namespace SignalTest
#endif

} // namespace Cangjie
#endif
namespace Cangjie {
/* Register signal handler for Crtl C signal. */
void RegisterCrtlCSignalHandler();
} // namespace Cangjie

#endif // CANGJIE_UTILS_SIGNAL_H
