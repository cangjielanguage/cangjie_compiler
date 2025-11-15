// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the Option related classes.
 */

#include "cangjie/Option/Option.h"

#include <numeric>
#include <regex>
#include <string>
#include <unordered_map>

#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Basic/Print.h"
#include "cangjie/Basic/Utils.h"
#include "cangjie/Utils/FileUtil.h"
#include "cangjie/Utils/Semaphore.h"
#include "cangjie/Utils/SipHash.h"
#include "cangjie/Utils/Unicode.h"
#include "cangjie/Utils/Utils.h"

#ifdef _WIN32
#include <windows.h>
#endif

using namespace Cangjie;
using namespace Cangjie::Triple;
using namespace Cangjie::Utils;
using namespace Cangjie::FileUtil;

namespace {
const std::string CJO_EXTENSION = "cjo";
const std::string BC_EXTENSION = "bc";
const std::string CJ_EXTENSION = "cj";
const std::string ARCHIVE_EXTENSION = "a";
const std::string OBJECT_EXTENSION = "o";
#ifdef _WIN32
const std::string DL_EXTENSION = "dll";
#elif defined(__APPLE__)
const std::string DL_EXTENSION = "dylib";
#else
const std::string DL_EXTENSION = "so";
#endif

std::string VectorStrToSerializedString(const std::vector<std::string>& vec, const std::string& sep)
{
    std::string result;
    for (auto& v : vec) {
        result += v + sep;
    }
    return result;
}

std::string BoolToSerializedString(bool val)
{
    return val ? "t" : "f";
}

const std::unordered_map<ArchType, std::string> ARCH_STRING_MAP = {
    {ArchType::X86_64, "x86_64"},
    {ArchType::AARCH64, "aarch64"},
    {ArchType::UNKNOWN, "unknown"},
};

const std::unordered_map<OSType, std::string> OS_STRING_MAP = {
    {OSType::WINDOWS, "windows"},
    {OSType::WINDOWS, "w64"},
    {OSType::LINUX, "linux"},
    {OSType::DARWIN, "darwin"},
    {OSType::UNKNOWN, "unknown"},
};

const std::unordered_map<Vendor, std::string> VENDOR_STRING_MAP = {
    {Vendor::PC, "pc"},
    {Vendor::APPLE, "apple"},
    {Vendor::UNKNOWN, "unknown"},
};

template <typename... Args>
inline void RaiseArgumentUnusedMessage(DiagnosticEngine& diag, DiagKindRefactor diagKind, const Args... args)
{
    (void)diag.DiagnoseRefactor(diagKind, DEFAULT_POSITION, args..., " The argument is ignored.");
}
} // namespace

namespace Cangjie {
const std::unordered_map<GlobalOptions::OptimizationLevel, std::string> OPTIMIZATION_LEVEL_TO_BACKEND_OPTION = {
    {GlobalOptions::OptimizationLevel::O0, "-O0"}, {GlobalOptions::OptimizationLevel::O1, "-O1"},
    {GlobalOptions::OptimizationLevel::O2, "-O2"}, {GlobalOptions::OptimizationLevel::O3, "-O3"},
    {GlobalOptions::OptimizationLevel::Os, "-Os"}, {GlobalOptions::OptimizationLevel::Oz, "-Oz"}};

bool IsUnsafeBackend(Triple::BackendType backendType)
{
    return backendType == Triple::BackendType::CJNATIVE;
}
} // namespace Cangjie

std::string Triple::BackendToString(const BackendType& backend)
{
    switch (backend) {
        case BackendType::CJNATIVE:
            return "cjnative";
        case BackendType::UNKNOWN:
        default:
            break;
    }
    return "unknown";
}

std::string Triple::Info::ArchToString() const
{
    if (auto search = ARCH_STRING_MAP.find(arch); search != ARCH_STRING_MAP.end()) {
        return search->second;
    } else {
        return "unknown";
    }
}

std::string Triple::Info::OSToString() const
{
    if (auto search = OS_STRING_MAP.find(os); search != OS_STRING_MAP.end()) {
        return search->second;
    } else {
        return "unknown";
    }
}

std::string Triple::Info::VendorToString() const
{
    if (auto search = VENDOR_STRING_MAP.find(vendor); search != VENDOR_STRING_MAP.end()) {
        return search->second;
    } else {
        return "unknown";
    }
}

std::string Triple::Info::ToTripleString() const
{
    return ArchToString() + "-" + OSToString() + "-" + EnvironmentToString();
}

std::string Triple::Info::ToFullTripleString() const
{
    std::string systemStr = OSToString();
    std::string envStr = EnvironmentToString();
    if (systemStr != "unknown" && envStr != "unknown") {
        systemStr = systemStr + "-" + envStr;
    }
    return ArchToString() + "-" + VendorToString() + "-" + systemStr;
}

std::string Triple::Info::GetEffectiveTripleString() const
{
    auto tripleString = ToTripleString();
    if (tripleString == "aarch64-linux-ohos") {
        return "aarch64-linux-gnu";
    }
    if (tripleString == "x86_64-windows-gnu") {
        return "x86_64-w64-mingw32";
    }
    return tripleString;
}

bool Triple::IsPossibleMatchingTripleName(const Triple::Info& info, const std::string& name)
{
    // General triple has format {ARCH}-{VENDOR}-{SYSTEM}-{ENV}, {VENDOR} sometimes is unknown and is omitted,
    // thus both {ARCH}-{VENDOR}-{SYSTEM}-{ENV} and {ARCH}-{SYSTEM}-{ENV} are possible valid triple names.
    auto parts = Utils::SplitString(name, "-");
    // Size of 3 for format {ARCH}-{SYSTEM}-{ENV}, and size of 4 for format {ARCH}-{VENDOR}-{SYSTEM}-{ENV}.
    if (parts.size() != 3 && parts.size() != 4) {
        // In other cases, the name must not be a valid triple name.
        return false;
    }
    if (parts[0] != info.ArchToString()) {
        return false;
    }
    // Size - 2 is {SYSTEM}
    if (parts[parts.size() - 2] != info.OSToString()) {
        return false;
    }
    // Size - 1 is {ENV}
    if (parts[parts.size() - 1] != info.EnvironmentToString()) {
        return false;
    }
    // We ignore vendor information for now
    return true;
}

bool GlobalOptions::SetOptimizationLevel(OptimizationLevel level)
{
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    this->optimizationLevel = level;
    if (level >= OptimizationLevel::O2) {
        selectedCHIROpts.insert(OptimizationFlag::CONST_PROPAGATION);
        selectedCHIROpts.insert(OptimizationFlag::LETC_FOLDING);
        selectedCHIROpts.insert(OptimizationFlag::REF_FOLDING);
        selectedCHIROpts.insert(OptimizationFlag::REF_TO_LETC);
        selectedCHIROpts.insert(OptimizationFlag::REDUNDANT_RETURNS);
        selectedCHIROpts.insert(OptimizationFlag::REDUNDANT_FUTURE);
        selectedCHIROpts.insert(OptimizationFlag::FUNC_INLINING);
        selectedCHIROpts.insert(OptimizationFlag::DEVIRTUALIZATION);
        selectedCHIROpts.insert(OptimizationFlag::ARRAY_LAMBDA_OPT);
        selectedCHIROpts.insert(OptimizationFlag::SROA_OPT);
        selectedCHIROpts.insert(OptimizationFlag::SWITCH_OPT);
        selectedCHIROpts.insert(OptimizationFlag::VALUE_RANGE_ANALYSIS);
        selectedCHIROpts.insert(OptimizationFlag::REDUNDANT_LOAD);
        // Enabling CHIR Escape Analysis by default in O2
        chirEA = true;
        chirLICM = true;
    } else {
        selectedCHIROpts.clear();
    }
    return true;
#endif
}

bool GlobalOptions::PerformPostActions()
{
    SetupChirOptions();
    bool success = SetupConditionalCompilationCfg();
    // ReprocessInputs depends on the normalized output path which is processed in ReprocessOutputs,
    // so ReprocessOutputs must be run before ReprocessInputs.
    success = success && ReprocessOutputs();
    success = success && ReprocessInputs();
    success = success && ReprocessReflectionOption();
    success = success && CheckCompileMacro();
    success = success && ReprocessCoverageOptions();
    success = success && CheckScanDependencyOptions();
    success = success && CheckSanitizerOptions();
    success = success && CheckLtoOptions();
    success = success && CheckCompileAsExeOptions();
    success = success && CheckPgoOptions();
    success = success && ReprocessObfuseOption();
    RefactJobs();
    RefactAggressiveParallelCompileOption();
    DisableStaticStdForOhos();
    return success;
}

void GlobalOptions::SetupChirOptions()
{
    // Why there is a marco here? In case of LSP, CANGJIE_CODEGEN_CJNATIVE_BACKEND
    // is not set. `chirLLVM` is expected to be false.
    // Since `backend` is always set to `BackendType::CJNATIVE` by default.
    // We need a macro to prevent `chirLLVM` from setting to true.
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    chirLLVM = backend == BackendType::CJNATIVE;
#endif
    chirCC = chirCC || chirLLVM; // cjnative backend should enable chir-cc.

    // If using interpreter we need CHIR graph to be closure converted
    if (interpreter) {
        chirCC = true;
    }
}

bool GlobalOptions::ReprocessReflectionOption()
{
    if (target.IsMacOS()) {
        disableReflection = true;
    }
    return true;
}

bool GlobalOptions::ReprocessOutputs()
{
    DiagnosticEngine diag;

    if (outputDir.has_value()) {
        if (IsAbsolutePath(output)) {
            Errorf("'-o' does not accept absolute path when '--output-dir' is specified\n");
            return false;
        }
        // The outputDir also affects the destination of the final product, thus we set output as well.
        output = JoinPath(outputDir.value(), output);
        if (auto maybePath = FileUtil::GetAbsPath(outputDir.value()); maybePath.has_value()) {
            outputDir = maybePath;
        } else {
            (void)diag.DiagnoseRefactor(DiagKindRefactor::invalid_path, DEFAULT_POSITION, outputDir.value());
            return false;
        }
    }

    bool outputIsDir = IsDir(output);
    auto dirPath = GetDirPath(output);
    // If `output` is not a directory and `dirPath` is empty, we use root directory `/` as default.
    std::string saveDir = outputIsDir ? output : (dirPath.empty() ? "/" : dirPath);
    if (!ValidateDirectoryPath(saveDir, FileUtil::FileMode::FM_WRITE).has_value()) {
        return false;
    }
    if (!outputIsDir && !IsDir(saveDir)) {
        (void)diag.DiagnoseRefactor(DiagKindRefactor::not_a_directory, DEFAULT_POSITION, saveDir);
        return false;
    }
    if (auto maybePath = FileUtil::GetAbsPath(saveDir); maybePath.has_value()) {
        output = outputIsDir ? maybePath.value() : JoinPath(maybePath.value(), FileUtil::GetFileName(output));
    } else {
        (void)diag.DiagnoseRefactor(DiagKindRefactor::invalid_path, DEFAULT_POSITION, output);
        return false;
    }
    if (!CheckOutputPathLength()) {
        return false;
    }
    auto fileName = FileUtil::GetFileName(output);
    if (!compileMacroPackage && fileName.find("lib-macro_") == 0) {
        Errorf("Cannot support specify file prefix 'lib-macro_' without '--compile-macro' option.\n");
        return false;
    }
    return true;
}

bool GlobalOptions::CheckOutputPathLength() const
{
    DiagnosticEngine diag;
#ifdef _WIN32
    // Length of "main.exe" is 8
    size_t defaultOutputNameLen = 8;
#else
    // Length of "main" is 4
    size_t defaultOutputNameLen = 4;
#endif
    auto fullLength = IsDir(output) ? output.size() + defaultOutputNameLen : output.size();
    if (fullLength > FILE_PATH_MAX_LENGTH) {
        (void)diag.DiagnoseRefactor(DiagKindRefactor::driver_path_exceeds_length_limit, DEFAULT_POSITION,
            "the output file", std::to_string(FILE_PATH_MAX_LENGTH));
        return false;
        // If the path length of the output is close to the upper limit, the compilation still has a possibility to
        // fail, because the ld linker on Windows will escape backslashs by doubling it ("\" -> "\\"), which may make
        // the resultant path exceed the limit. Therefore, a warning is generated when the output path is longer than
        // FILE_PATH_MAX_LENGTH - 15.
    } else if (fullLength > FILE_PATH_MAX_LENGTH - 15) {
        (void)diag.DiagnoseRefactor(DiagKindRefactor::driver_warning_path_close_to_length_limit, DEFAULT_POSITION,
            "the output file", std::to_string(FILE_PATH_MAX_LENGTH));
    }
    return true;
}

bool GlobalOptions::ReprocessInputs()
{
    if (FileUtil::IsDir(output)) {
        return true;
    }
    // Any inputs member must obey the following rules:
    // 1. input can not be empty;
    // 2. input can not be the same with output, otherwise input will be overwritten by output(executable).
    DiagnosticEngine diag;
    std::vector<std::string> inputs;
    inputs = argList->GetInputs();
    if (compilePackage) {
        for (auto& path : packagePaths) {
            std::vector<std::string> files = GetAllFilesUnderCurrentPath(path, CJ_EXTENSION);
            std::for_each(files.begin(), files.end(), [&path, &inputs](std::string& file) {
                file = FileUtil::JoinPath(path, file);
                if (GetFileExtension(file) == CJ_EXTENSION) {
                    inputs.emplace_back(file);
                }
            });
        }
    }
    for (auto& inputFile : inputs) {
        auto absInputFile = FileUtil::GetAbsPath(inputFile);
        if (!absInputFile.has_value()) {
            (void)diag.DiagnoseRefactor(DiagKindRefactor::no_such_file_or_directory, DEFAULT_POSITION, inputFile);
            return false;
        }
        if (absInputFile.value() == output) {
            (void)diag.DiagnoseRefactor(
                DiagKindRefactor::input_file_overwritten_by_generated_output, DEFAULT_POSITION, inputFile);
            return false;
        }
    }
    return true;
}

bool GlobalOptions::CheckCompileMacro() const
{
    if (compileMacroPackage && enableOutputType) {
        Errorf("using both '--compile-macro' and '--output-type' is not allowed.\n");
        return false;
    }
    if (compileMacroPackage) {
        if (!output.empty() && !IsDir(output)) {
            Errorf("Cannot support specify output file name with '--compile-macro' option.\n");
            return false;
        }
    }
    return true;
}

bool GlobalOptions::ReprocessCoverageOptions()
{
    if (enableCoverage) {
        if (optimizationLevel != OptimizationLevel::O0) {
            Warningf("'--coverage' should be used without optimizations.\n");
            (void)SetOptimizationLevel(OptimizationLevel::O0);
        }
        if (!removedPathPrefix.empty()) {
            Warningf("'--coverage' should be used without option '--trimpath'.\n ");
            removedPathPrefix.clear();
        }
    }
    return true;
}

bool GlobalOptions::CheckScanDependencyOptions() const
{
    if (!scanDepPkg) {
        return true;
    }
    DiagnosticEngine diag;
    if (compilePackage) {
        // Report error for `cjc--scan-dependency -p pkg.cjo ...`. When `-p` is specified .cjo file
        // inputs are not accepted.
        if (!inputCjoFile.empty()) {
            (void)diag.DiagnoseRefactor(
                DiagKindRefactor::driver_not_accept_cjo_inputs_when, DEFAULT_POSITION, "'-p' or '--package'");
            return false;
        }
        // Report error for `cjc --scan-dependency -p`. A path input is required.
        if (packagePaths.empty()) {
            (void)diag.DiagnoseRefactor(
                DiagKindRefactor::driver_require_package_directory_scan_dependency, DEFAULT_POSITION);
            return false;
        }
    } else {
        // Report error for `cjc --scan-dependency`. A .cjo file input is required.
        if (inputCjoFile.empty()) {
            (void)diag.DiagnoseRefactor(DiagKindRefactor::driver_source_cjo_empty, DEFAULT_POSITION);
            return false;
        }
    }
    return true;
}

bool GlobalOptions::CheckSanitizerOptions() const
{
    if (!EnableSanitizer()) {
        return true;
    }

    auto runtimeDirPath = FileUtil::JoinPath(cangjieHome, "runtime");
    auto libraryDirPath = FileUtil::JoinPath(FileUtil::JoinPath(runtimeDirPath, "lib"), GetCangjieLibTargetPathName());
    auto ext = GetSharedLibraryExtension(target.os);
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    auto sanitizerPath = FileUtil::JoinPath(libraryDirPath, SanitizerTypeToShortString());
    if (!FileUtil::FileExist(FileUtil::JoinPath(sanitizerPath, "libcangjie-runtime" + ext))) {
        Errorf("%s sanitizer feature is not supported.\n", SanitizerTypeToSerializedString().c_str());
        return false;
    }
#endif

    if (IsLTOEnabled()) {
        Errorln("Option '--sanitize' and '--lto' cannot be enabled together.");
        return false;
    }

    if (discardEhFrame) {
        Errorln("Option '--sanitize' and '--discard-eh-frame' cannot be enabled together.");
        return false;
    }

    if (compileMacroPackage) {
        Errorln("Option '--sanitize' and '--compile-macro' cannot be enabled together.");
        return false;
    }

    auto osType = target.GetOSFamily();
    if (EnableAsan() && osType != OSType::LINUX) {
        Errorln("Address Sanitizer is only supported on Linux");
        return false;
    }

    if (EnableHwAsan() && osType != OSType::LINUX) {
        Errorln("Hardware-Assisted Address Sanitizer is only supported on Linux");
        return false;
    }

    auto arch = target.arch;
    if (EnableHwAsan() && (arch != ArchType::X86_64 && arch != ArchType::AARCH64)) {
        Errorln("Hardware-Assisted Address Sanitizer is only supported on X86_64 or aarch64");
        return false;
    }

    if (EnableTsan() && (osType != OSType::LINUX && osType != OSType::WINDOWS)) {
        Errorln("Thread Sanitizer is only supported on Linux/Windows");
        return false;
    }

    return true;
}

bool GlobalOptions::CheckLtoOptions() const
{
    if (!IsLTOEnabled()) {
        return true;
    }
    auto osType = target.GetOSFamily();
    if (osType == OSType::WINDOWS) {
        Errorln("Windows does not support LTO optimization.");
        return false;
    }
    if (outputMode == OutputMode::STATIC_LIB && !bcInputFiles.empty()) {
        Errorln("The input file cannot be bc files When generating a static library in LTO mode.");
        return false;
    }
    if (optimizationLevel == OptimizationLevel::Os || optimizationLevel == OptimizationLevel::Oz) {
        Errorln("-Os and -Oz optimize options are not supported in LTO mode.");
        return false;
    }
    return true;
}

bool GlobalOptions::CheckCompileAsExeOptions() const 
{
    if (!IsCompileAsExeEnabled()) {
        return true;
    }
    if (IsCompileAsExeEnabled() && !IsLTOEnabled()){
        DiagnosticEngine diag;
        diag.DiagnoseRefactor(DiagKindRefactor::driver_invalid_compile_as_exe, DEFAULT_POSITION);
        return false;
    }
    auto osType = target.GetOSFamily();
    if(osType == OSType::WINDOWS || osType == OSType::DARWIN) {
        DiagnosticEngine diag;
        diag.DiagnoseRefactor(DiagKindRefactor::driver_invalid_compile_as_exe_platform, DEFAULT_POSITION);
        return false;
    }
    return true;
}

bool GlobalOptions::CheckPgoOptions() const
{
    DiagnosticEngine diag;
    // Using both '--pgo-instr-gen' and '--pgo-instr-use' is not allowed.
    if (enablePgoInstrGen && enablePgoInstrUse) {
        (void)diag.DiagnoseRefactor(DiagKindRefactor::driver_pgo_both_gen_and_use, DEFAULT_POSITION);
        return false;
    }
    // Check if the pgo-profile-file is valid.
    if (enablePgoInstrUse) {
        if (!FileExist(pgoProfileFile)) {
            (void)diag.DiagnoseRefactor(DiagKindRefactor::no_such_file_or_directory, DEFAULT_POSITION, pgoProfileFile);
            return false;
        } else {
            const std::string PROFILE_EXT = "profdata";
            if (FileUtil::GetFileExtension(pgoProfileFile) != PROFILE_EXT) {
                (void)diag.DiagnoseRefactor(
                    DiagKindRefactor::driver_pgo_invalid_profile_extension, DEFAULT_POSITION, pgoProfileFile);
                return false;
            }
        }
    }
    return true;
}

void GlobalOptions::RefactJobs()
{
    if (jobs.has_value()) {
        return;
    }
    jobs = std::optional<size_t>{std::thread::hardware_concurrency()};
}

void GlobalOptions::RefactAggressiveParallelCompileOption()
{
    if (target.os == Triple::OSType::WINDOWS || target.os == Triple::OSType::DARWIN) {
        // When the user's input compile options contain `--apc` and the os is windows or mac,
        // aggressiveParallelCompile will be disabled because it is not supported yet.
        if (aggressiveParallelCompile.has_value() || aggressiveParallelCompileWithoutArg) {
            Warningf("'--apc' is not supported when targeting %s.\n", target.OSToString().c_str());
        }
        aggressiveParallelCompile = 1;
        return;
    }

    // When the compile case enables Obfuscation/LTO/Coverage, we disable the parallel compile
    if (IsObfuscationEnabled() || IsLTOEnabled() || enableCoverage) {
        aggressiveParallelCompile = 1;
        return;
    }

    if (aggressiveParallelCompile.has_value()) {
        return;
    } else if (optimizationLevel == OptimizationLevel::O0 || enableCompileDebug ||
        aggressiveParallelCompileWithoutArg) {
        // When the compile options contain `-O0`\'-g'\`--apc`, aggressiveParallelCompile will be enabled,
        // and the degree of parallelism is the same as that of `-j`.
        CJC_ASSERT(jobs.has_value());
        constexpr std::size_t allowance = 2;
        Utils::Semaphore::Get().SetCount(jobs.value() + allowance);
        aggressiveParallelCompile = jobs;
    } else {
        aggressiveParallelCompile = 1;
    }
}

void GlobalOptions::DisableStaticStdForOhos()
{
    if (target.env == Triple::Environment::OHOS) {
        if (linkStaticStd.has_value() && linkStaticStd.value()) {
            DiagnosticEngine diag;
            (void) diag.DiagnoseRefactor(DiagKindRefactor::driver_static_std_for_ohos, DEFAULT_POSITION);
        }
        linkStaticStd = false;
    }
}

bool GlobalOptions::HandleArchiveExtension(DiagnosticEngine& diag, const std::string& value)
{
    auto maybePath = ValidateInputFilePath(value, DiagKindRefactor::driver_invalid_binary_file);
    if (!maybePath.has_value()) {
        return false;
    }
    auto ext = GetFileExtension(value);
    if (ext == ARCHIVE_EXTENSION && GetFileExtension(maybePath.value()) != ARCHIVE_EXTENSION) {
        RaiseArgumentUnusedMessage(diag, DiagKindRefactor::driver_warning_not_archive_file, value, maybePath.value());
        return true;
    }
    if (ext == OBJECT_EXTENSION && GetFileExtension(maybePath.value()) != OBJECT_EXTENSION) {
        RaiseArgumentUnusedMessage(diag, DiagKindRefactor::driver_warning_not_object_file, value, maybePath.value());
        return true;
    }
    inputObjs.emplace_back(maybePath.value());
    // The .o and .a file is replaced with the absolute path
    ReplaceInputFileName(value, maybePath.value());
    return true;
}

bool GlobalOptions::HandleCJOExtension(DiagnosticEngine& diag, const std::string& value)
{
    auto maybePath = ValidateInputFilePath(value, DiagKindRefactor::driver_invalid_binary_file);
    if (!maybePath.has_value()) {
        return false;
    }
    if (GetFileExtension(maybePath.value()) != CJO_EXTENSION) {
        RaiseArgumentUnusedMessage(diag, DiagKindRefactor::driver_warning_not_cjo_file, value, maybePath.value());
        return true;
    }
    if (!inputCjoFile.empty()) {
        (void)diag.DiagnoseRefactor(DiagKindRefactor::driver_require_one_package_directory_scan_dependency,
            DEFAULT_POSITION, "--scan-dependency");
        return false;
    }
    inputCjoFile = value;
    return true;
}

bool GlobalOptions::HandleCJExtension(DiagnosticEngine& diag, const std::string& value)
{
    if (compilePackage) {
        (void)diag.DiagnoseRefactor(
            DiagKindRefactor::driver_unsupport_compile_package_with_source_file, DEFAULT_POSITION, value);
        return false;
    }
    auto maybePath = ValidateInputFilePath(value, DiagKindRefactor::driver_invalid_source_file);
    if (!maybePath.has_value()) {
        return false;
    }
    if (GetFileExtension(maybePath.value()) != CJ_EXTENSION) {
        RaiseArgumentUnusedMessage(diag, DiagKindRefactor::driver_warning_not_cj_file, value, maybePath.value());
        return true;
    }
    srcFiles.push_back(value);
    return true;
}

bool GlobalOptions::HandleCJDExtension(DiagnosticEngine& diag, const std::string& value)
{
    if (compilePackage) {
        (void)diag.DiagnoseRefactor(
            DiagKindRefactor::driver_unsupport_compile_package_with_source_file, DEFAULT_POSITION, value);
        return false;
    }
    auto maybePath = ValidateInputFilePath(value, DiagKindRefactor::driver_invalid_source_file);
    if (!maybePath.has_value()) {
        return false;
    }
    srcFiles.push_back(value);
    return true;
}

bool GlobalOptions::HandleBCExtension(DiagnosticEngine& diag, const std::string& value)
{
    auto maybePath = ValidateInputFilePath(value, DiagKindRefactor::driver_invalid_source_file);
    if (!maybePath.has_value()) {
        return false;
    }
    if (GetFileExtension(maybePath.value()) != BC_EXTENSION) {
        RaiseArgumentUnusedMessage(diag, DiagKindRefactor::driver_warning_not_bc_file, value, maybePath.value());
        return true;
    }
    bcInputFiles.push_back(maybePath.value());
    // The .bc file is replaced with the absolute path
    ReplaceInputFileName(value, maybePath.value());
    return true;
}

bool GlobalOptions::HandleNoExtension(DiagnosticEngine& diag, const std::string& value)
{
    if (IsAbsolutePathAboveLengthLimit(value)) {
        (void)diag.DiagnoseRefactor(DiagKindRefactor::driver_path_exceeds_length_limit, DEFAULT_POSITION, value,
            std::to_string(FILE_PATH_MAX_LENGTH));
        return false;
    }
    if (!FileExist(value)) {
        (void)diag.DiagnoseRefactor(DiagKindRefactor::driver_invalid_file_or_directory, DEFAULT_POSITION, value);
        return false;
    }

    if (IsDir(value)) {
        if (!compilePackage) {
            (void)diag.DiagnoseRefactor(
                DiagKindRefactor::driver_unsupport_compile_source_file_with_path, DEFAULT_POSITION, value);
        }
        if (!ValidateDirectoryPath(value, FileUtil::FileMode::FM_EXIST).has_value()) {
            return false;
        }
        packagePaths.emplace_back(value);
    } else {
        (void)diag.DiagnoseRefactor(DiagKindRefactor::driver_warning_argument_unused, DEFAULT_POSITION, value);
    }
    return true;
}

bool GlobalOptions::ProcessInputs(const std::vector<std::string>& inputs)
{
    DiagnosticEngine diag;
    bool ret = true;
    std::for_each(inputs.begin(), inputs.end(), [this, &ret, &diag](const std::string& value) {
        if (!ret) {
            return;
        }
        std::string ext = GetFileExtension(value);
        if (ext == OBJECT_EXTENSION || ext == ARCHIVE_EXTENSION) {
            ret = HandleArchiveExtension(diag, value);
        } else if (ext == CJ_EXTENSION && !compileCjd) {
            ret = HandleCJExtension(diag, value);
        } else if (ext == BC_EXTENSION) {
            ret = HandleBCExtension(diag, value);
        } else if (ext == CJO_EXTENSION) {
            ret = HandleCJOExtension(diag, value);
        } else if (HasCJDExtension(value) && compileCjd) {
            ret = HandleCJDExtension(diag, value);
        } else {
            ret = HandleNoExtension(diag, value);
        }
    });
    // Check inputs.
    if (compilePackage && packagePaths.empty()) {
        (void)diag.DiagnoseRefactor(DiagKindRefactor::driver_require_package_directory, DEFAULT_POSITION);
        return false;
    }
    if (compilePackage && packagePaths.size() > 1) {
        (void)diag.DiagnoseRefactor(DiagKindRefactor::driver_require_one_package_directory, DEFAULT_POSITION);
        return false;
    }
    return ret;
}

void GlobalOptions::CollectOrderedInputFiles(ArgInstance& arg, uint64_t idx)
{
    switch (arg.argInstanceType) {
        case ArgInstanceType::Input:
            // The input files should be passed to the linker in the order they were inputted.
            inputFileOrder.push_back(std::make_tuple(static_cast<InputArgInstance*>(&arg)->value, idx));
            break;
        case ArgInstanceType::Option: {
            // The library files entered with `--library` or `-l` should also be passed to the linker in the order
            // they were entered.
            OptionArgInstance& optArg = *static_cast<OptionArgInstance*>(&arg);
            if (optArg.info.GetID() == Options::ID::LIBRARY) {
                inputLibraryOrder.push_back(std::make_tuple(optArg.value, idx));
            } else if (optArg.info.GetID() == Options::ID::LINK_OPTIONS) {
                inputLinkOptionOrder.push_back(std::make_tuple(optArg.value, idx));
            }
            break;
        }
    }
}

bool GlobalOptions::ParseFromArgs(ArgList& argList)
{
    this->argList = &argList;
    std::vector<OptionArgInstance> optArgs;
    uint64_t inputIdx = 0;

    // First, filter out inputs and handle pre-process arguments. Pre-process arguments are a set of
    // arguments that have higher priority than other arguments. For example, the existence of '-h' would
    // suppress all other argument errors in a command.
    bool skipParsing = false;
    for (auto& arg : argList.args) {
        switch (arg->argInstanceType) {
            case ArgInstanceType::Input:
                argList.AddInput(static_cast<InputArgInstance*>(arg.get())->value);
                break;
            case ArgInstanceType::Option: {
                // If parse success, then the argument is consumed. Otherwise, the argument
                // is not a pre-process argument, keep for the second round of processing.
                OptionArgInstance& optArg = *static_cast<OptionArgInstance*>(arg.get());
                bool skip = false;
                if (!TryParsePreOption(optArg, argList, skip)) {
                    optArgs.emplace_back(optArg);
                }
                skipParsing = skipParsing || skip;
                break;
            }
        }
        CollectOrderedInputFiles(*arg.get(), inputIdx);
        inputIdx++;
    }

    if (skipParsing) {
        return true;
    }

    // Process all remaining option arguments
    for (auto& optArg : optArgs) {
        // Failure on parsing implies that the argument is invalid. Stop parsing and exit with false.
        if (!TryParseOption(optArg, argList)) {
            return false;
        }
    }

    if (!ProcessInputs(argList.GetInputs())) {
        return false;
    }

    if (!PerformPostActions()) {
        return false;
    }

    return true;
}

std::vector<std::string> GlobalOptions::GenerateFrontendOptions() const
{
    std::vector<std::string> optionList;
    optionList.reserve(argList->args.size());
    for (auto& arg : argList->args) {
        switch (arg->argInstanceType) {
            case ArgInstanceType::Input:
                optionList.emplace_back(static_cast<InputArgInstance*>(arg.get())->value);
                break;
            case ArgInstanceType::Option: {
                OptionArgInstance& optArg = *static_cast<OptionArgInstance*>(arg.get());
                if (optArg.info.GetID() == Options::ID::OUTPUT_FILE) {
                    break;
                }
                if (optArg.info.BelongsGroup(Options::Group::DRIVER)) {
                    break;
                }
                optionList.emplace_back(optArg.name);
                if (optArg.info.GetKind() == Options::Kind::SEPARATED) {
                    optionList.emplace_back(optArg.value);
                }
                break;
                // All constructors of ArgInstanceType are handled here. Without `default` case,
                // exhaustive type checking could help detect potential errors.
            }
        }
    }
    bool multiBC = aggressiveParallelCompile.value_or(1) > 1;
    if (!frontendOutputFiles.empty() && !multiBC) {
        optionList.emplace_back("-o");
        optionList.emplace_back(frontendOutputFiles[0].filePath);
    }
    return optionList;
}

void GlobalOptions::DeprecatedOptionCheck(const OptionArgInstance& arg) const
{
    // Check if the option is deprecated which will be removed in the future release
    Options::ID id = arg.info.GetID();
    const std::unordered_set<Options::ID> deprecatedOptions{Options::ID::STATIC_LIBS, Options::ID::DY_LIBS};
    DiagnosticEngine diag;
    if (deprecatedOptions.find(id) != deprecatedOptions.end()) {
        std::string substitutableOption = "";
        (void)diag.DiagnoseRefactor(
            DiagKindRefactor::driver_deprecated_option, DEFAULT_POSITION, arg.str, substitutableOption);
    }
}

bool GlobalOptions::TryParseOption(OptionArgInstance& arg, ArgList& argList)
{
#ifdef CANGJIE_VISIBLE_OPTIONS_ONLY
    if (std::count(arg.info.groups.begin(), arg.info.groups.end(), Options::Group::VISIBLE) == 0) {
        return false;
    };
#endif
    if (!experimentalMode && std::count(arg.info.groups.begin(), arg.info.groups.end(), Options::Group::STABLE) == 0) {
        ErrorExperimentalOption(arg.str);
        return false;
    }
    if (arg.info.values.size() != 0) {
        auto result = std::find_if(arg.info.values.begin(), arg.info.values.end(),
            [value = arg.value, this](const Options::OptionValue& optionValue) {
                std::set<Options::Backend> backends{GetOptionsBackend()};
                return value == optionValue.value &&
                    OptionTable::BelongsTo(backends, optionValue.backends, Options::Backend::ALL) != 0;
            });
#ifdef CANGJIE_VISIBLE_OPTIONS_ONLY
        if (result != arg.info.values.end() &&
            std::count(result->groups.begin(), result->groups.end(), Options::Group::VISIBLE) == 0) {
            Errorf("invalid value: '%s'\n", result->value.c_str());
            return false;
        };
#endif
        if (result != arg.info.values.end() && !experimentalMode &&
            std::count(result->groups.begin(), result->groups.end(), Options::Group::STABLE) == 0) {
            ErrorExperimentalOption(arg.str);
            return false;
        }
    }
    // Though the deprecated option will be removed in future release, it still works in current release.
    DeprecatedOptionCheck(arg);
    auto maybeResult = ParseOption(arg);
    CJC_ASSERT(maybeResult.has_value());
    // We expect ParseOption always returns an option with a value. If it doesn't, it is an ICE.
    // Release version will fail on getting value and raise an exception, which triggers ICE fail process.
    auto success = maybeResult.value();

    OccurrenceCheck(arg, argList);
    return success;
}

void GlobalOptions::OccurrenceCheck(const OptionArgInstance& arg, ArgList& argList) const
{
    if (argList.IsSpecified(arg.info.GetID()) &&
        arg.info.GetOccurrenceType() == Options::Occurrence::SINGLE_OCCURRENCE && !argList.IsWarned(arg.info.GetID())) {
        std::string name = arg.info.GetName();
        name += arg.info.GetAlias().empty() ? "" : "' or '" + arg.info.GetAlias();
        Warningf("'%s' is specified multiple times. The last one is chosen.\n", name.c_str());
        argList.MarkWarned(arg.info.GetID());
    }
    argList.MarkSpecified(arg);
}

std::optional<std::string> GlobalOptions::CheckDirectoryPath(const std::string& path) const
{
    DiagnosticEngine diag;
    if (!FileUtil::FileExist(path)) {
        RaiseArgumentUnusedMessage(diag, DiagKindRefactor::driver_warning_no_such_directory, path);
        return std::nullopt;
    }
    if (!FileUtil::IsDir(path)) {
        RaiseArgumentUnusedMessage(diag, DiagKindRefactor::driver_warning_not_a_directory, path);
        return std::nullopt;
    }
    auto maybePath = FileUtil::GetAbsPath(path);
    if (!maybePath.has_value()) {
        RaiseArgumentUnusedMessage(diag, DiagKindRefactor::driver_warning_invalid_path, path);
    }
    return maybePath;
}

std::optional<std::string> GlobalOptions::ValidateDirectoryPath(const std::string& path, FileUtil::FileMode mode) const
{
    DiagnosticEngine diag;
    auto printErrorMessage = [&path, &diag](AccessResultType resultType) {
        if (resultType == AccessResultType::NO_PERMISSION) {
            diag.DiagnoseRefactor(DiagKindRefactor::permission_denied, DEFAULT_POSITION, path);
        } else {
            diag.DiagnoseRefactor(DiagKindRefactor::no_such_directory, DEFAULT_POSITION, path);
        }
    };
    auto res = FileUtil::AccessWithResult(path, mode);
    if (res != AccessResultType::OK) {
        printErrorMessage(res);
        return std::nullopt;
    }
    if (!FileUtil::IsDir(path)) {
        (void)diag.DiagnoseRefactor(DiagKindRefactor::not_a_directory, DEFAULT_POSITION, path);
        return std::nullopt;
    }
    if (mode == FileUtil::FileMode::FM_WRITE) {
        // EXE permission is also requires if we need to write to the directory
        res = FileUtil::AccessWithResult(path, FileUtil::FileMode::FM_EXE);
        if (res != AccessResultType::OK) {
            printErrorMessage(res);
            return std::nullopt;
        }
    }
    auto maybePath = FileUtil::GetAbsPath(path);
    if (!maybePath.has_value()) {
        (void)diag.DiagnoseRefactor(DiagKindRefactor::invalid_path, DEFAULT_POSITION, path);
    }
    return maybePath;
}

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
std::optional<std::string> GlobalOptions::CheckInputFilePath(const std::string& path) const
{
    DiagnosticEngine diag;
    if (!FileUtil::FileExist(path)) {
        RaiseArgumentUnusedMessage(diag, DiagKindRefactor::driver_warning_no_such_file_or_directory, path);
        return std::nullopt;
    }
    if (!FileUtil::Access(path, FileUtil::FileMode::FM_READ)) {
        RaiseArgumentUnusedMessage(diag, DiagKindRefactor::driver_warning_permission_denied, path);
        return std::nullopt;
    }
    if (FileUtil::IsDir(path)) {
        RaiseArgumentUnusedMessage(diag, DiagKindRefactor::driver_warning_not_a_file, path);
        return std::nullopt;
    }
    auto maybePath = FileUtil::GetAbsPath(path);
    if (!maybePath.has_value()) {
        RaiseArgumentUnusedMessage(diag, DiagKindRefactor::driver_warning_invalid_path, path);
    }
    return maybePath;
}
#endif

std::optional<std::string> GlobalOptions::ValidateInputFilePath(
    const std::string& path, const DiagKindRefactor notFoundError) const
{
    DiagnosticEngine diag;
    if (IsAbsolutePathAboveLengthLimit(path)) {
        (void)diag.DiagnoseRefactor(DiagKindRefactor::driver_path_exceeds_length_limit, DEFAULT_POSITION, path,
            std::to_string(FILE_PATH_MAX_LENGTH));
        return std::nullopt;
    }
    if (!FileExist(path)) {
        (void)diag.DiagnoseRefactor(notFoundError, DEFAULT_POSITION, path);
        return std::nullopt;
    }
    if (!FileUtil::Access(path, FileUtil::FileMode::FM_READ)) {
        (void)diag.DiagnoseRefactor(DiagKindRefactor::permission_denied, DEFAULT_POSITION, path);
        return std::nullopt;
    }
    if (FileUtil::IsDir(path)) {
        (void)diag.DiagnoseRefactor(DiagKindRefactor::not_a_file, DEFAULT_POSITION, path);
        return std::nullopt;
    }
    auto maybePath = FileUtil::GetAbsPath(path);
    if (!maybePath.has_value()) {
        (void)diag.DiagnoseRefactor(DiagKindRefactor::invalid_path, DEFAULT_POSITION, path);
    }
    return maybePath;
}

std::optional<int> GlobalOptions::ParseIntOptionValue(const OptionArgInstance& arg, int from, int to)
{
    auto maybeNumber = Utils::TryParseInt(arg.value);
    if (!maybeNumber.has_value()) {
        Errorf("The value of %s is invalid.\n", arg.name.c_str());
        return std::nullopt;
    }
    auto number = maybeNumber.value();
    if (number < from || number > to) {
        Errorf("The value of %s is invalid.\n", arg.name.c_str());
        return std::nullopt;
    }
    return maybeNumber;
}

std::string GlobalOptions::GetSharedLibraryExtension(Triple::OSType osType)
{
    switch (osType) {
        case Triple::OSType::WINDOWS:
            return ".dll";
        case Triple::OSType::DARWIN:
            return ".dylib";
        case Triple::OSType::LINUX:
        case Triple::OSType::UNKNOWN:
        default:
            return ".so";
    }
}

void GlobalOptions::ReadPathsFromEnvironmentVars(const std::unordered_map<std::string, std::string>& environmentVars)
{
    const std::string cangjiePath = "CANGJIE_PATH";
    if (environmentVars.find(cangjiePath) != environmentVars.end()) {
        environment.cangjiePaths = FileUtil::SplitEnvironmentPaths(environmentVars.at(cangjiePath));
    }
    if (environmentVars.find(CANGJIE_HOME) != environmentVars.end()) {
        environment.cangjieHome = FileUtil::GetAbsPath(environmentVars.at(CANGJIE_HOME));
    }
    const std::string libraryPath = "LIBRARY_PATH";
    if (environmentVars.find(libraryPath) != environmentVars.end()) {
        environment.libraryPaths = FileUtil::SplitEnvironmentPaths(environmentVars.at(libraryPath));
    }
    const std::string path = "PATH";
    if (environmentVars.find(path) != environmentVars.end()) {
        environment.paths = FileUtil::SplitEnvironmentPaths(environmentVars.at(path));
    }
    const std::string sdkROOT = "SDKROOT";
    if (environmentVars.find(sdkROOT) != environmentVars.end()) {
        environment.macOSSDKRoot = FileUtil::GetAbsPath(environmentVars.at(sdkROOT));
    }
    environment.allVariables = environmentVars;
}

std::string GlobalOptions::GetCangjieLibHostPathName() const
{
    return host.OSToString() + "_" + host.ArchToString() + "_" + BackendToString(backend);
}

std::string GlobalOptions::GetCangjieLibTargetPathName() const
{
    std::string name = target.OSToString();
    if (target.env != Triple::Environment::GNU && target.env != Triple::Environment::NOT_AVAILABLE) {
        name += "_" + target.EnvironmentToString();
    }
    name += "_" + target.ArchToString() + "_" + BackendToString(backend);
    return name;
}

void GlobalOptions::SetCompilationCachedPath()
{
    if (outputDir.has_value()) {
        compilationCachedPath = outputDir.value();
    } else if (FileUtil::IsDir(output)) {
        compilationCachedPath = output;
    } else {
        compilationCachedPath = FileUtil::GetDirPath(output);
    }
    CJC_ASSERT(compilationCachedPath != "");
}

static std::string HashString(const std::string& str)
{
    std::stringstream ss;
    ss << std::hex << Utils::SipHash::GetHashValue(str);
    return std::move(*ss.rdbuf()).str();
}

std::string GlobalOptions::GetHashedObjFileName(const std::string& objFileName) const
{
    return FileUtil::JoinPath(compilationCachedDir, HashString(compilationCachedFileName + objFileName));
}

std::pair<std::string, std::string> GlobalOptions::GenerateNamesOfCachedDirAndFile(
    const std::string& fullPackageName) const
{
    std::string fileName;
    if (compilePackage) {
        fileName = fullPackageName;
    } else {
        // Sort all srcFiles' relative path
        std::vector<std::string> sortedSrcFiles;
        for (auto& srcFile : srcFiles) {
            (void)sortedSrcFiles.emplace_back(GetCacheRelativeFilePath(srcFile));
        }
        std::sort(sortedSrcFiles.begin(), sortedSrcFiles.end());

        size_t allFilePathLength = std::accumulate(sortedSrcFiles.begin(), sortedSrcFiles.end(), 0uL,
            [](const size_t length, const std::string& filePath) { return length + filePath.length(); });
        fileName.reserve(allFilePathLength);
        for (auto& filePath : sortedSrcFiles) {
            fileName += filePath;
        }
    }
    auto hashedFileName = HashString(fileName);
    std::string folder = FileUtil::JoinPath(compilationCachedPath, ".cached");
    return std::make_pair(std::move(folder), std::move(hashedFileName));
}

std::string GlobalOptions::GenerateCachedPathName(
    const std::string& fullPackageName, const std::string& extension) const
{
    auto [folder, fileName] = GenerateNamesOfCachedDirAndFile(fullPackageName);
    if (!FileUtil::FileExist(folder)) {
        (void)FileUtil::CreateDirs(folder + "/");
    }
    auto ret = FileUtil::JoinPath(folder, fileName + extension);
    return ret;
}

std::string GlobalOptions::GenerateCachedPathNameForCodeGen(
    const std::string& subModuleName, const std::string& extension) const
{
    auto hashedFileName = codegenDebugMode ? subModuleName : HashString(subModuleName);
    auto folder = FileUtil::JoinPath(compilationCachedPath, ".cached");
    if (!FileUtil::FileExist(folder)) {
        (void)FileUtil::CreateDirs(folder + "/");
    }
    auto ret = FileUtil::JoinPath(folder, hashedFileName + extension);
    return ret;
}

std::string GlobalOptions::GetCacheRelativeFilePath(const std::string& filePath) const
{
    CJC_ASSERT(!compilationCachedPath.empty());
    auto absFilePath = FileUtil::GetAbsPath(filePath);
    if (!absFilePath.has_value()) {
        return FileUtil::GetFileName(filePath);
    }
    auto relativePath = FileUtil::GetRelativePath(FileUtil::JoinPath(compilationCachedPath, ""), absFilePath.value());
    if (!relativePath.has_value()) {
        return FileUtil::GetFileName(filePath);
    }
    return relativePath.value();
}

void GlobalOptions::UpdateCachedDirName(const std::string& fullPackageName)
{
    std::tie(compilationCachedDir, compilationCachedFileName) = GenerateNamesOfCachedDirAndFile(fullPackageName);
}

std::string GlobalOptions::PassedWhenKeyValueToSerializedString() const
{
    std::string result;
    std::map<std::string, std::string> orderedMap;
    for (auto& it : passedWhenKeyValue) {
        orderedMap.emplace(it.first, it.second);
    }
    for (const auto& it : std::as_const(orderedMap)) {
        result += (it.first + it.second);
    }
    return result;
}

std::string GlobalOptions::BackendTypeToSerializedString() const
{
    switch (backend) {
        case Triple::BackendType::CJNATIVE:
            return "LL";
        case Triple::BackendType::UNKNOWN:
            return "K";
        default:
            CJC_ABORT();
            return "";
    }
}

std::string GlobalOptions::OptimizationLevelToSerializedString() const
{
    switch (optimizationLevel) {
        case OptimizationLevel::O0:
            return "O0";
        case OptimizationLevel::O1:
            return "O1";
        case OptimizationLevel::O2:
            return "O2";
        case OptimizationLevel::O3:
            return "O3";
        case OptimizationLevel::Os:
            return "Os";
        case OptimizationLevel::Oz:
            return "Oz";
        default:
            CJC_ABORT();
            return "";
    }
}

std::string GlobalOptions::OutputModeToSerializedString() const
{
    switch (outputMode) {
        case OutputMode::EXECUTABLE:
            return "E";
        case OutputMode::STATIC_LIB:
            return "C";
        case OutputMode::SHARED_LIB:
            return "S";
        default:
            CJC_ABORT();
            return "";
    }
}

std::string GlobalOptions::SanitizerCoverageOptions::ToSerializedString() const
{
    std::string result;
    result += "coverageType:";
    switch (coverageType) {
        case Type::SCK_UNKNOW:
            result += "K";
            break;
        case Type::SCK_NONE:
            result += "N";
            break;
        case Type::SCK_FUNCTION:
            result += "F";
            break;
        case Type::SCK_BB:
            result += "BB";
            break;
        default:
            break;
    }
    result += ",traceCmp:" + BoolToSerializedString(traceCmp);
    result += ",traceMemCmp" + BoolToSerializedString(traceMemCmp);
    result += ",tracePCGuard:" + BoolToSerializedString(tracePCGuard);
    result += ",inline8bitCounters:" + BoolToSerializedString(inline8bitCounters);
    result += ",inlineBoolFlag:" + BoolToSerializedString(inlineBoolFlag);
    result += ",pcTable:" + BoolToSerializedString(pcTable);
    result += ",stackDepth:" + BoolToSerializedString(stackDepth);
    return result;
}

std::string GlobalOptions::StackTraceFormatToSerializedString() const
{
    std::string result = "stack-trace-format=";
    switch (stackTraceFmt) {
        case StackTraceFormat::ALL:
            result += "all";
            break;
        case StackTraceFormat::SIMPLE:
            result += "simple";
            break;
        case StackTraceFormat::DEFAULT:
        default:
            result += "default";
    }
    return result;
}

std::string GlobalOptions::SelectedCHIROptsToSerializedString() const
{
    Utils::Out64 printer{0, false, false};
    for (auto flag : selectedCHIROpts) {
        printer << (1ull << static_cast<uint_fast64_t>(flag));
    }
    std::stringstream ss;
    ss << printer;
    return std::move(*ss.rdbuf()).str();
}

std::string GlobalOptions::OverflowStrategyToSerializedString() const
{
    switch (overflowStrategy) {
        case OverflowStrategy::NA:
            return "N";
        case OverflowStrategy::CHECKED:
            return "C";
        case OverflowStrategy::WRAPPING:
            return "W";
        case OverflowStrategy::THROWING:
            return "T";
        case OverflowStrategy::SATURATING:
            return "S";
        default:
            CJC_ABORT();
            return "";
    }
}

std::string GlobalOptions::SanitizerTypeToSerializedString() const
{
    switch (sanitizerType) {
        case SanitizerType::NONE:
            return "none";
        case SanitizerType::ADDRESS:
            return "address";
        case SanitizerType::THREAD:
            return "thread";
        case SanitizerType::HWADDRESS:
            return "hardware address";
        default:
            return "";
    }
}

std::string GlobalOptions::SanitizerTypeToShortString() const
{
    switch (sanitizerType) {
        case SanitizerType::ADDRESS:
            return "asan";
        case SanitizerType::THREAD:
            return "tsan";
        case SanitizerType::HWADDRESS:
            return "hwasan";
        case SanitizerType::NONE:
        default:
            return "";
    }
}

std::vector<std::string> GlobalOptions::ToSerialized() const
{
    std::vector<std::string> result;
    constexpr decltype(result.size()) estimatedOptionNum = 70;
    result.reserve(estimatedOptionNum);
    result.emplace_back(BoolToSerializedString(enableCompileDebug));
    result.emplace_back(target.ToFullTripleString());
    result.emplace_back(PassedWhenKeyValueToSerializedString());
    result.emplace_back(SelectedCHIROptsToSerializedString());
    result.emplace_back(BoolToSerializedString(compilePackage));
    result.emplace_back(BoolToSerializedString(noSubPkg));
    result.emplace_back(BoolToSerializedString(displayLineInfo));
    result.emplace_back(moduleName);
    result.emplace_back(std::to_string(static_cast<uint8_t>(ltoMod)));
    result.emplace_back(VectorStrToSerializedString(removedPathPrefix, ":"));
    result.emplace_back(BackendTypeToSerializedString());
    result.emplace_back(BoolToSerializedString(enableMacroInLSP));
    result.emplace_back(BoolToSerializedString(enableCompileTest));
    result.emplace_back(BoolToSerializedString(implicitPrelude));
    result.emplace_back(BoolToSerializedString(chirEA));
    result.emplace_back(BoolToSerializedString(chirLICM));
    result.emplace_back(BoolToSerializedString(chirCC));
    result.emplace_back(OptimizationLevelToSerializedString());
    result.emplace_back(StackTraceFormatToSerializedString());
    result.emplace_back(OutputModeToSerializedString());
    result.emplace_back(BoolToSerializedString(enableHotReload));
    result.emplace_back(BoolToSerializedString(disableChirOpt));
    result.emplace_back(sancovOption.ToSerializedString());
    result.emplace_back(BoolToSerializedString(chirLLVM));
    result.emplace_back(BoolToSerializedString(strictNumberMode));
    result.emplace_back(BoolToSerializedString(fastMathMode));
    result.emplace_back(BoolToSerializedString(enableCoverage));
    result.emplace_back(SanitizerTypeToSerializedString());
    result.emplace_back(BoolToSerializedString(experimentalMode));
    result.emplace_back(OverflowStrategyToSerializedString());
    (void)result.emplace_back(BoolToSerializedString(interpreter));
    (void)result.emplace_back(VectorStrToSerializedString(interpreterSearchPaths, ":"));
    (void)result.emplace_back(VectorStrToSerializedString(interpLoadLib, ":"));
    (void)result.emplace_back(VectorStrToSerializedString(interpreterArgs, "\\"));
    (void)result.emplace_back(BoolToSerializedString(interpreterPrintResult));
    (void)result.emplace_back(BoolToSerializedString(interpMainNoLinkage));
    (void)result.emplace_back(BoolToSerializedString(disableCodeGen));
    (void)result.emplace_back(BoolToSerializedString(disableDeserializer));
    return result;
}
