// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the DriverOptions.
 */

#include "cangjie/Driver/DriverOptions.h"
#include  "cangjie/Utils/Semaphore.h"

#include <unordered_set>

#include "cangjie/Basic/DiagnosticEngine.h"

/**
 * OPTION_TRUE_ACTION is a help macro for defining an option action which takes no option
 * arguments and does no error checks. It takes simple expression as argument and expands
 * the expression to an action lambda.
 */
#define OPTION_TRUE_ACTION(EXPR) [](DriverOptions& opts, OptionArgInstance&) { (EXPR); return true; }

namespace {
const std::unordered_set<std::string> X86_64_TARGET_CPUS = {
#define CANGJIE_X86_64_TARGET_CPU(TARGET_CPU_NAME) (TARGET_CPU_NAME),
#include "TargetCPUs.inc"
#undef CANGJIE_X86_64_TARGET_CPU
};
const std::unordered_set<std::string> AARCH64_TARGET_CPUS = {
#define CANGJIE_AARCH64_TARGET_CPU(TARGET_CPU_NAME) (TARGET_CPU_NAME),
#include "TargetCPUs.inc"
#undef CANGJIE_AARCH64_TARGET_CPU
};
}

using namespace Cangjie;

namespace {
std::unordered_map<Options::ID, std::function<bool(DriverOptions&, OptionArgInstance&)>> g_actions = {
    { Options::ID::STATIC, OPTION_TRUE_ACTION(opts.linkStatic = true) },
    { Options::ID::TARGET_CPU, [](DriverOptions& opts, const OptionArgInstance& arg) {
        opts.targetCPU = arg.value;
        return true;
    }},
    { Options::ID::OPT_OPTIONS, [](DriverOptions& opts, const OptionArgInstance& arg) {
        opts.optArg = arg.value;
        return true;
    }},
    { Options::ID::LLC_OPTIONS, [](DriverOptions& opts, const OptionArgInstance& arg) {
        opts.llcArg = arg.value;
        return true;
    }},
    { Options::ID::LIBRARY_PATH, [](DriverOptions& opts, const OptionArgInstance& arg) {
        auto maybePath = opts.CheckDirectoryPath(arg.value);
        if (maybePath.has_value()) {
            opts.librarySearchPaths.emplace_back(maybePath.value());
        }
        return true;
    }},
    { Options::ID::LIBRARY, [](DriverOptions& opts, const OptionArgInstance& arg) {
        opts.libraries.emplace_back(arg.value);
        return true;
    }},
    { Options::ID::TOOLCHAIN, [](DriverOptions& opts, const OptionArgInstance& arg) {
        auto maybePath = opts.CheckDirectoryPath(arg.value);
        if (maybePath.has_value()) {
            opts.toolChainPaths.emplace_back(maybePath.value());
        }
        return true;
    }},
    { Options::ID::SYSROOT, [](DriverOptions& opts, const OptionArgInstance& arg) {
        auto maybePath = opts.CheckDirectoryPath(arg.value);
        if (maybePath.has_value()) {
            opts.sysroot = maybePath.value();
            opts.customizedSysroot = true;
        }
        return true;
    }},
    { Options::ID::LINK_OPTION, [](DriverOptions& opts, const OptionArgInstance& arg) {
        opts.linkOption.emplace_back(arg.value);
        return true;
    }},
    { Options::ID::LINK_OPTIONS, [](DriverOptions& opts, const OptionArgInstance& arg) {
        opts.linkOptions.emplace_back(arg.value);
        return true;
    }},
    { Options::ID::STRIP_ALL, OPTION_TRUE_ACTION(opts.stripSymbolTable = true) },
    { Options::ID::USE_RUNTIME_RPATH, OPTION_TRUE_ACTION(opts.useRuntimeRpath = true) },
    { Options::ID::SANITIZE_SET_RPATH, OPTION_TRUE_ACTION(opts.sanitizerEnableRpath = true) },

    // ---------- CODE OBFUSCATION OPTIONS ----------
    { Options::ID::OBF_STRING, OPTION_TRUE_ACTION(opts.enableStringObfuscation = true) },
    { Options::ID::NO_OBF_STRING, OPTION_TRUE_ACTION(opts.enableStringObfuscation = false) },
    { Options::ID::OBF_CONST, OPTION_TRUE_ACTION(opts.enableConstObfuscation = true) },
    { Options::ID::NO_OBF_CONST, OPTION_TRUE_ACTION(opts.enableConstObfuscation = false) },
    { Options::ID::OBF_LAYOUT, OPTION_TRUE_ACTION(opts.enableLayoutObfuscation = true) },
    { Options::ID::NO_OBF_LAYOUT, OPTION_TRUE_ACTION(opts.enableLayoutObfuscation = false) },
    { Options::ID::OBF_CF_FLATTEN, OPTION_TRUE_ACTION(opts.enableCFflattenObfuscation = true) },
    { Options::ID::NO_OBF_CF_FLATTEN, OPTION_TRUE_ACTION(opts.enableCFflattenObfuscation = false) },
    { Options::ID::OBF_CF_BOGUS, OPTION_TRUE_ACTION(opts.enableCFBogusObfuscation = true) },
    { Options::ID::NO_OBF_CF_BOGUS, OPTION_TRUE_ACTION(opts.enableCFBogusObfuscation = false) },
    { Options::ID::OBF_EXPORT_SYMS, OPTION_TRUE_ACTION(opts.enableObfExportSyms = true) },
    { Options::ID::NO_OBF_EXPORT_SYMS, OPTION_TRUE_ACTION(opts.enableObfExportSyms = false) },
    { Options::ID::OBF_LINE_NUMBER, OPTION_TRUE_ACTION(opts.enableObfLineNumber = true) },
    { Options::ID::NO_OBF_LINE_NUMBER, OPTION_TRUE_ACTION(opts.enableObfLineNumber = false) },
    { Options::ID::OBF_SOURCE_PATH, OPTION_TRUE_ACTION(opts.enableObfSourcePath = true) },
    { Options::ID::NO_OBF_SOURCE_PATH, OPTION_TRUE_ACTION(opts.enableObfSourcePath = false) },
    { Options::ID::OBF_ALL, OPTION_TRUE_ACTION(opts.enableObfAll = true) },
    { Options::ID::OBF_SYM_INPUT_MAPPING, [](DriverOptions& opts, const OptionArgInstance& arg) {
        opts.layoutObfInputSymMappingFiles = arg.value;
        return true;
    }},
    { Options::ID::OBF_SYM_OUTPUT_MAPPING, [](DriverOptions& opts, const OptionArgInstance& arg) {
        opts.layoutObfOutputSymMappingFile = arg.value;
        return true;
    }},
    { Options::ID::OBF_MAP_FILE, [](DriverOptions& opts, const OptionArgInstance& arg) {
        opts.layoutObfUserMappingFile = opts.ValidateInputFilePath(
            arg.value, DiagKindRefactor::no_such_file_or_directory);
        return opts.layoutObfUserMappingFile.has_value();
    }},
    { Options::ID::OBF_SYM_PREFIX, [](DriverOptions& opts, const OptionArgInstance& arg) {
        opts.layoutObfSymPrefix = arg.value;
        return true;
    }},
    { Options::ID::OBF_CONFIG, [](DriverOptions& opts, const OptionArgInstance& arg) {
        opts.obfuscationConfigFile = opts.ValidateInputFilePath(arg.value, DiagKindRefactor::no_such_file_or_directory);
        return opts.obfuscationConfigFile.has_value();
    }},
    { Options::ID::OBF_LEVEL, [](DriverOptions& opts, const OptionArgInstance& arg) {
        auto maybeNumber = DriverOptions::ParseIntOptionValue(arg, DriverOptions::OBFUCATION_LEVEL_MIN,
            DriverOptions::OBFUCATION_LEVEL_MAX);
        if (!maybeNumber.has_value()) {
            return false;
        }
        opts.obfuscationLevel = {maybeNumber.value()};
        return true;
    }},
    { Options::ID::OBF_SEED, [](DriverOptions& opts, const OptionArgInstance& arg) {
        auto maybeNumber = DriverOptions::ParseIntOptionValue(arg);
        if (!maybeNumber.has_value()) {
            return false;
        }
        opts.obfuscationSeed = maybeNumber.value();
        return true;
    }},
};
} // namespace

std::optional<bool> DriverOptions::ParseOption(OptionArgInstance& arg)
{
    if (g_actions.find(arg.info.GetID()) == g_actions.end()) {
        return GlobalOptions::ParseOption(arg);
    }
    return {g_actions[arg.info.GetID()](*this, arg)};
}

bool DriverOptions::ReprocessObfuseOption()
{
    if (enableObfAll) {
        if (!enableStringObfuscation.has_value()) {
            enableStringObfuscation = true;
        }
        if (!enableConstObfuscation.has_value()) {
            enableConstObfuscation = true;
        }
        if (!enableLayoutObfuscation.has_value()) {
            enableLayoutObfuscation = true;
        }
        if (!enableCFflattenObfuscation.has_value()) {
            enableCFflattenObfuscation = true;
        }
        if (!enableCFBogusObfuscation.has_value()) {
            enableCFBogusObfuscation = true;
        }
        if (!enableObfLineNumber.has_value() && enableLayoutObfuscation) {
            enableObfLineNumber = true;
        }
    }
    return true;
}

bool DriverOptions::PerformPostActions()
{
    if (!GlobalOptions::PerformPostActions()) {
        return false;
    }
    bool success = true;
    success = success && CheckTargetCPUOption();
    success = success && CheckObfuscationOptions();
    success = success && SetupSysroot();
    success = success && CheckRuntimeRPath();
    success = success && CheckSanitizerRPath();
    success = success && CheckStaticOption();
    return success;
}

bool DriverOptions::CheckObfuscationOptions() const
{
    if (enableCompileDebug) {
        bool obfuscationEnabled = enableStringObfuscation || enableConstObfuscation || enableLayoutObfuscation ||
            enableCFflattenObfuscation || enableCFBogusObfuscation;
        if (obfuscationEnabled) {
            Warningf("Obfuscation options should be used without '-g' or debugging may not work properly.\n");
        }
    }
    if (!enableLayoutObfuscation && layoutObfInputSymMappingFiles.has_value()) {
        Errorf("Input symbol mapping files can be specified only when layout obfuscation is enabled.\n");
        return false;
    }
    if (!enableLayoutObfuscation && layoutObfOutputSymMappingFile.has_value()) {
        Errorf("Output symbol mapping file can be specified only when layout obfuscation is enabled.\n");
        return false;
    }
    if (!enableLayoutObfuscation && layoutObfUserMappingFile.has_value()) {
        Errorf("User-defined symbol mapping file can be specified only when layout obfuscation is enabled.\n");
        return false;
    }
    if (!enableLayoutObfuscation && enableObfExportSyms.has_value()) {
        Errorf("You can set whether to obfuscate export symbols only when layout obfuscation is enabled.\n");
        return false;
    }
    if (!enableLayoutObfuscation && enableObfLineNumber.has_value()) {
        Errorf("You can set whether to obfuscate line number only when layout obfuscation is enabled.\n");
        return false;
    }
    if (!enableLayoutObfuscation && enableObfSourcePath.has_value()) {
        Errorf("You can set whether to obfuscate source path only when layout obfuscation is enabled.\n");
        return false;
    }
    if (!enableLayoutObfuscation && layoutObfSymPrefix.has_value()) {
        Errorf("Symbol prefix can be specified only when layout obfuscation is enabled.\n");
        return false;
    }

    return true;
}

bool DriverOptions::CheckTargetCPUOption()
{
    // If target is MacOS, target CPU can be a null value, no further process is needed.
    if (target.IsMacOS() && !targetCPU.has_value()) {
        return true;
    }
    // If target is not MacOS, we set generic as default target CPU.
    targetCPU = targetCPU.value_or("generic");
    if (targetCPU.value() == "generic" || targetCPU.value() == "native") {
        return true;
    }
    bool isSupportedTargetCPU = (target.arch == Triple::ArchType::AARCH64 &&
                                    AARCH64_TARGET_CPUS.find(targetCPU.value()) != AARCH64_TARGET_CPUS.end()) ||
        (target.arch == Triple::ArchType::X86_64 &&
            X86_64_TARGET_CPUS.find(targetCPU.value()) != X86_64_TARGET_CPUS.end());
    if (isSupportedTargetCPU) {
        return true;
    }
    DiagnosticEngine diag;
    (void)diag.DiagnoseRefactor(DiagKindRefactor::driver_unsupported_target_cpu, DEFAULT_POSITION, targetCPU.value());
    return false;
}

bool DriverOptions::SetupSysroot()
{
    if (!customizedSysroot) {
        if (target.IsMacOS() && environment.macOSSDKRoot.has_value()) {
            sysroot = environment.macOSSDKRoot.value();
        }
    }
    return true;
}

bool DriverOptions::CheckRuntimeRPath() const
{
    if (!useRuntimeRpath) {
        return true;
    }

    if (target.os == Triple::OSType::WINDOWS) {
        Warningln("'--set-runtime-rpath' option has no effect on PE targets.");
    } else if (IsCrossCompiling()) {
        Warningln("'--set-runtime-rpath' option has no effect on cross-compiled targets.");
    }
    return true;
}

bool DriverOptions::CheckSanitizerRPath() const
{
    if (!sanitizerEnableRpath) {
        return true;
    }

    if (!EnableSanitizer()) {
        Errorln("Option '--sanitize-set-rpath' can only be enabled along with '--sanitize'");
        return false;
    }

    if (useRuntimeRpath) {
        Errorln("Option '--sanitize-set-rpath' and '--set-runtime-rpath' cannot be enabled together.");
        return false;
    }

    if (IsCrossCompiling()) {
        Warningln("'--sanitize-set-rpath' option has no effect on cross-compiled targets.");
    }
    return true;
}

bool DriverOptions::CheckStaticOption()
{
    if (linkStatic && (target.os != Triple::OSType::LINUX || target.env == Triple::Environment::OHOS)) {
        linkStatic = false;
        Warningln("'--static' option has only effect on Linux targets.");
    }
    if (linkStatic && (outputMode != OutputMode::EXECUTABLE)) {
        linkStatic = false;
        Warningln("'--static' option is only effective when compiling executables.");
    }
    return true;
}

