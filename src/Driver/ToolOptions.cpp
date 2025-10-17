// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements Tool Options functions.
 */

#include "cangjie/Driver/ToolOptions.h"

#include <unordered_map>

namespace Cangjie::ToolOptions {

void SetOptionStringArgs(SetFuncType setOptionHandler, const std::string& stringArg)
{
    auto args = Utils::SplitString(stringArg, " ");
    for (const auto& a : args) {
        if (!a.empty()) {
            setOptionHandler(a);
        }
    }
}

void SetOptionIf(SetFuncType setOptionHandler, bool condition, const std::string& stringArg)
{
    if (condition) {
        setOptionHandler(stringArg);
    }
}

void SetMtripleOption(SetFuncType setOptionHandler, const DriverOptions& driverOptions)
{
    bool canSetMtriple = driverOptions.IsCrossCompiling() || driverOptions.target.IsMinGW();
    SetOptionIf(setOptionHandler, canSetMtriple, "--mtriple=" + driverOptions.target.GetEffectiveTripleString());
}

namespace OPT {
void SetOptimizationLevelOptions(SetFuncType setOptionHandler, const DriverOptions& driverOptions)
{
    auto optimizationLevel =
        driverOptions.disableBackendOpt ? GlobalOptions::OptimizationLevel::O0 : driverOptions.optimizationLevel;
    setOptionHandler("default<" + OPTIMIZATION_LEVEL_TO_BACKEND_OPTION.find(optimizationLevel)->second.substr(1) + ">");
}

void SetNewPassManagerOptions(SetFuncType setOptionHandler, const DriverOptions& driverOptions)
{
    // the new pass manager only supports the following pass settings:
    // - optimization (in function SetOptimizationLevelOptions)
    // - coverage
    // - obfuscation (ObfData, ObfLayout, ObfControlFlow)
    // - optPassOptions
    SetOptionIf(setOptionHandler, driverOptions.enableCoverage, "-insert-gcov-profiling");
    SetOptionIf(setOptionHandler, driverOptions.EnableAsan(), "-asan-module");
    SetOptionIf(setOptionHandler, driverOptions.EnableTsan(), "-tsan");
    SetOptionIf(setOptionHandler, driverOptions.EnableHwAsan(), "-hwasan");
    SetOptionIf(setOptionHandler, (!driverOptions.optPassOptions.empty()), "-" + driverOptions.optPassOptions);
}

void SetCodeLayoutObfuscationOptions(SetFuncType setOptionHandler, const DriverOptions& driverOptions)
{
    setOptionHandler("--obf-layout=true");
    if (driverOptions.layoutObfInputSymMappingFiles.has_value()) {
        setOptionHandler(
            std::string("-obf-input-symbol-mapping-files=") + driverOptions.layoutObfInputSymMappingFiles.value());
    }
    if (driverOptions.layoutObfOutputSymMappingFile.has_value()) {
        setOptionHandler(
            std::string("-obf-output-symbol-mapping-file=") + driverOptions.layoutObfOutputSymMappingFile.value());
    }
    if (driverOptions.layoutObfUserMappingFile.has_value()) {
        setOptionHandler(
            std::string("-obf-apply-symbol-mapping=") + driverOptions.layoutObfUserMappingFile.value());
    }
    if (driverOptions.layoutObfSymPrefix.has_value()) {
        setOptionHandler(std::string("-obf-sym-prefix=") + driverOptions.layoutObfSymPrefix.value());
    }
    setOptionHandler(
        std::string("-obf-export-symbol=") + (driverOptions.enableObfExportSyms.value_or(true) ? "true" : "false"));
    setOptionHandler(
        std::string("-obf-line-number=") + (driverOptions.enableObfLineNumber.value_or(false) ? "true" : "false"));
    setOptionHandler(
        std::string("-obf-source-path=") + (driverOptions.enableObfSourcePath.value_or(true) ? "true" : "false"));
}

void SetCodeObfuscationOptions(SetFuncType setOptionHandler, const DriverOptions& driverOptions)
{
    bool obfEnabled = false;
    bool obfString = driverOptions.enableStringObfuscation.value_or(false);
    bool obfConst = driverOptions.enableConstObfuscation.value_or(false);
    bool obfLayout = driverOptions.enableLayoutObfuscation.value_or(false);
    bool obfCFFlatten = driverOptions.enableCFflattenObfuscation.value_or(false);
    bool obfCFBogus = driverOptions.enableCFBogusObfuscation.value_or(false);
    if (obfString || obfConst) {
        setOptionHandler(std::string("-obf-string=") + (obfString ? "true" : "false"));
        setOptionHandler(std::string("-obf-const=") + (obfConst ? "true" : "false"));
        obfEnabled = true;
    }
    if (obfLayout) {
        SetCodeLayoutObfuscationOptions(setOptionHandler, driverOptions);
        obfEnabled = true;
    }
    if (obfCFFlatten || obfCFBogus) {
        setOptionHandler(
            std::string("-control-flow-flatten=") + (obfCFFlatten ? "true" : "false"));
        setOptionHandler(
            std::string("-control-flow-bogus=") + (obfCFBogus ? "true" : "false"));
        obfEnabled = true;
    }
    if (obfEnabled) {
        setOptionHandler(std::string("-obf-level=") + std::to_string(driverOptions.obfuscationLevel));
        if (driverOptions.obfuscationSeed.has_value()) {
            setOptionHandler(std::string("-obf-seed=") + std::to_string(driverOptions.obfuscationSeed.value()));
        }
        if (driverOptions.obfuscationConfigFile.has_value()) {
            setOptionHandler(std::string("-obf-config-file=") + driverOptions.obfuscationConfigFile.value());
        }
    }
}

void SetLTOOptions(SetFuncType setOptionHandler, const DriverOptions& driverOptions)
{
    if (driverOptions.IsLTOEnabled()) {
        setOptionHandler("--cangjie-lto");
        setOptionHandler("--module-summary");
        setOptionHandler("--module-hash");
    }
}

void SetVerifyOptions(SetFuncType setOptionHandler, [[maybe_unused]] const DriverOptions& driverOptions)
{
    setOptionHandler("--only-verify-out");
}

void SetOptions(SetFuncType setOptionHandler, const DriverOptions& driverOptions)
{
    setOptionHandler("--cangjie-pipeline");
    SetOptionIf(setOptionHandler, driverOptions.EnableAsan(), "-cj-asan=true");
    SetOptionIf(setOptionHandler, driverOptions.EnableTsan(), "-cj-tsan=true");
    SetOptionIf(setOptionHandler, driverOptions.EnableTsan(), "-tsan-instrument-atomics=false");
    SetOptionIf(setOptionHandler, driverOptions.EnableTsan(), "--cj-tsan-support-for-mutexopt");
    SetOptionIf(setOptionHandler, driverOptions.EnableHwAsan(), "-cj-hwasan=true");
    SetOptionIf(setOptionHandler, driverOptions.enableFuncSections, "-function-sections");
    SetOptionIf(setOptionHandler, driverOptions.enableDataSections, "-data-sections");
    SetOptionIf(setOptionHandler,
        driverOptions.target.env == Triple::Environment::OHOS && driverOptions.target.arch == Triple::ArchType::AARCH64,
        "--cj-ptrauth-backward-cfi=true");
}

void SetTripleOptions(SetFuncType setOptionHandler, const DriverOptions& driverOptions)
{
    SetMtripleOption(setOptionHandler, driverOptions);
}

void SetTransparentOptions(SetFuncType setOptionHandler, const DriverOptions& driverOptions)
{
    // optArg may contain multiple opt arguments separated by spaces.
    SetOptionStringArgs(setOptionHandler, driverOptions.optArg);
}

void SetPgoOptions(SetFuncType setOptionHandler, const DriverOptions& driverOptions)
{
    if (driverOptions.enablePgoInstrGen) {
        setOptionHandler("-pgo-kind=pgo-instr-gen-pipeline");
        setOptionHandler("-runtime-counter-relocation");
    }

    if (driverOptions.enablePgoInstrUse) {
        setOptionHandler("-pgo-kind=pgo-instr-use-pipeline");
        setOptionHandler("-profile-file=" + driverOptions.pgoProfileFile);
    }
}

} // namespace OPT

namespace LLC {
void SetOptimizationLevelOptions(SetFuncType setOptionHandler, const DriverOptions& driverOptions)
{
    auto optimizationLevel =
        driverOptions.disableBackendOpt ? GlobalOptions::OptimizationLevel::O0 : driverOptions.optimizationLevel;
    // when the optimize option '-Os' or 'Oz' is enabled in opt stage, then it will trigger '-O2' in llc stage.
    if (optimizationLevel == GlobalOptions::OptimizationLevel::Os ||
        optimizationLevel == GlobalOptions::OptimizationLevel::Oz) {
        optimizationLevel = GlobalOptions::OptimizationLevel::O2;
    }
    setOptionHandler(OPTIMIZATION_LEVEL_TO_BACKEND_OPTION.find(optimizationLevel)->second);
}

void SetOptions(SetFuncType setOptionHandler, const DriverOptions& driverOptions)
{
    setOptionHandler("--cangjie-pipeline");
    SetOptionIf(setOptionHandler,
        (!driverOptions.enableCompileDebug && !driverOptions.enableCoverage && driverOptions.displayLineInfo),
        "-disable-debug-info-print");
    setOptionHandler("--relocation-model=pic");
    setOptionHandler("--frame-pointer=non-leaf");
    setOptionHandler(driverOptions.GetStackTraceFormat());
    SetOptionIf(setOptionHandler, driverOptions.fastMathMode, "-fp-contract=fast");
    SetOptionIf(setOptionHandler, driverOptions.enableFuncSections, "-function-sections");
    SetOptionIf(setOptionHandler, driverOptions.enableDataSections, "-data-sections");
	// thread sanitizer pass for atomics instrument
    SetOptionIf(setOptionHandler, driverOptions.EnableTsan(), "-tsan");
    SetOptionIf(setOptionHandler, driverOptions.EnableHwAsan(), "-mattr=+tagged-globals");
#ifdef CANGJIE_DISABLE_STACK_GROW_FEATURE
    setOptionHandler("--cj-stack-grow=false");
#else
    SetOptionIf(setOptionHandler,
        driverOptions.target.env == Triple::Environment::OHOS ||
        driverOptions.target.env == Triple::Environment::ANDROID,
        "--cj-stack-grow=false");
#endif
    if (driverOptions.targetCPU.has_value() && !driverOptions.targetCPU.value().empty()) {
        setOptionHandler("-mcpu=" + driverOptions.targetCPU.value());
        // AVX instruction generation may cause runtime fail on stack tracing. Manually disable here.
        SetOptionIf(setOptionHandler, driverOptions.target.arch == Triple::ArchType::X86_64, "-mattr=-avx");
    }
}

void SetTripleOptions(SetFuncType setOptionHandler, const DriverOptions& driverOptions)
{
    SetMtripleOption(setOptionHandler, driverOptions);
}

void SetTransparentOptions(SetFuncType setOptionHandler, const DriverOptions& driverOptions)
{
    // llcArg may contain multiple opt arguments separated by spaces.
    SetOptionStringArgs(setOptionHandler, driverOptions.llcArg);
}

} // namespace LLC

namespace LLD {
void SetLTOOptimizationLevelOptions(SetFuncType setOptionHandler, const DriverOptions& driverOptions)
{
    auto optimizationLevel =
        driverOptions.disableBackendOpt ? GlobalOptions::OptimizationLevel::O0 : driverOptions.optimizationLevel;
    switch (optimizationLevel) {
        case GlobalOptions::OptimizationLevel::O3:
            setOptionHandler("--lto-O3");
            break;
        // when the optimize option '-Os' or 'Oz' is enabled in opt stage, then it will trigger '-O2' in llc stage.
        case GlobalOptions::OptimizationLevel::Os:
        case GlobalOptions::OptimizationLevel::Oz:
        case GlobalOptions::OptimizationLevel::O2:
            setOptionHandler("--lto-O2");
            break;
        case GlobalOptions::OptimizationLevel::O1:
            setOptionHandler("--lto-O1");
            break;
        case GlobalOptions::OptimizationLevel::O0:
            setOptionHandler("--lto-O0");
            break;
        default:
            setOptionHandler("--lto-O2");
            break;
    }
}

void SetLTOOptions(SetFuncType setOptionHandler, const DriverOptions& driverOptions)
{
    if (driverOptions.IsFullLTOEnabled()) {
        setOptionHandler("--mllvm");
        setOptionHandler("--cangjie-full-lto");
    } else {
        // The backend lld defaults to supporting incremental compilation in thin LTO mode.
        setOptionHandler("--thinlto-cache-dir=" + driverOptions.compilationCachedPath);
    }
    // Differentiate the optimization in the LLD and OPT phases.
    setOptionHandler("--mllvm");
    setOptionHandler("--cj-lto-opt");
    setOptionHandler("--allow-multiple-definition");
    setOptionHandler("--plugin-opt=no-opaque-pointers");
    if (driverOptions.IsCompileAsExeEnabled()) {
        setOptionHandler("--compile-as-exe");
    }
}

void SetPgoOptions(SetFuncType setOptionHandler, const DriverOptions& driverOptions)
{
    if (driverOptions.enablePgoInstrGen) {
        setOptionHandler("-cj-pgo-kind=pgo-instr-gen-pipeline");
    }

    if (driverOptions.enablePgoInstrUse) {
        setOptionHandler("-cj-pgo-kind=pgo-instr-use-pipeline");
        setOptionHandler("-cj-profile-file=" + driverOptions.pgoProfileFile);
    }
}
} // namespace LLD

void SetOptions(SetFuncType setOptionHandler, const DriverOptions& driverOptions, const ToolOptionType typeFunc)
{
    typeFunc(setOptionHandler, driverOptions);
}

void SetOptions(
    SetFuncType setOptionHandler, const DriverOptions& driverOptions, const std::vector<ToolOptionType>& typeFuncs)
{
    for (const auto& typefunc : typeFuncs) {
        typefunc(setOptionHandler, driverOptions);
    }
}

} // namespace Cangjie::ToolOptions
