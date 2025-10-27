// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements frontend execute apis.
 */

#include "cangjie/FrontendTool/FrontendTool.h"

#ifdef __linux__
#include <malloc.h>
#endif
#include <memory>
#include <string>
#include <vector>

#include "cangjie/AST/PrintNode.h"
#include "cangjie/Basic/Version.h"
#include "cangjie/CHIR/Serializer/CHIRDeserializer.h"
#include "cangjie/Driver/Driver.h"
#include "cangjie/Driver/TempFileManager.h"
#include "cangjie/Frontend/CompilerInstance.h"
#include "cangjie/FrontendTool/CjdCompilerInstance.h"
#include "cangjie/FrontendTool/DefaultCompilerInstance.h"
#include "cangjie/FrontendTool/IncrementalCompilerInstance.h"
#include "cangjie/Macro/InvokeUtil.h"
#include "cangjie/Modules/PackageManager.h"
#include "cangjie/Utils/FileUtil.h"
#include "cangjie/Utils/ProfileRecorder.h"

#if (defined RELEASE)
#include "cangjie/Utils/Signal.h"
#endif

using namespace Cangjie;

static bool InitializeCompilerInvocation(CompilerInvocation& ci, const std::vector<std::string>& args)
{
    if (!ci.ParseArgs(args)) {
        WriteError("Invalid options. Try: 'cjc-frontend --help' for more information.\n");
        return false;
    }
    return true;
}

static bool PerformDumpAction(DefaultCompilerInstance& instance, const FrontendOptions::DumpAction dumpAction)
{
    bool ret = true;
    switch (dumpAction) {
        case FrontendOptions::DumpAction::DUMP_TOKENS:
            return instance.DumpTokens();
        case FrontendOptions::DumpAction::DUMP_SYMBOLS:
            ret = instance.Compile(CompileStage::SEMA);
            instance.DumpSymbols();
            break;
        case FrontendOptions::DumpAction::TYPE_CHECK:
            ret = instance.Compile(CompileStage::CHIR);
            break;
        case FrontendOptions::DumpAction::DUMP_DEP_PKG:
            ret = instance.Compile(CompileStage::IMPORT_PACKAGE);
            instance.DumpDepPackage();
            break;
        case FrontendOptions::DumpAction::DESERIALIZE_CHIR:
            ret = instance.DeserializeCHIR();
            break;
        default:
            break;
    }
    ret = ret && instance.diag.GetErrorCount() == 0;
    return ret;
}

static bool IsEmptyInputFile(const DefaultCompilerInstance& instance)
{
    auto& globalOptions = instance.invocation.globalOptions;
    if (globalOptions.scanDepPkg) {
        // In scan dependency mode, .cjo file input is required or `-p` must be specified (with package path input).
        return !globalOptions.compilePackage && globalOptions.inputCjoFile.empty();
    } else {
        // In code compilation mode, .cj file input is required or `-p` must be specified (with package path input).
        return !globalOptions.compilePackage && globalOptions.srcFiles.empty() && globalOptions.inputChirFiles.empty();
    }
}

static bool HandleEmptyInputFileSituation(const DefaultCompilerInstance& instance)
{
    auto& globalOptions = instance.invocation.globalOptions;
    if (!globalOptions.scanDepPkg && globalOptions.srcFiles.empty() && globalOptions.inputChirFiles.empty()) {
        instance.diag.DiagnoseRefactor(DiagKindRefactor::driver_source_file_empty, DEFAULT_POSITION);
        return false;
    }
    if (globalOptions.scanDepPkg && globalOptions.inputCjoFile.empty()) {
        instance.diag.DiagnoseRefactor(DiagKindRefactor::driver_source_cjo_empty, DEFAULT_POSITION);
        return false;
    }
    return true;
}

static bool ExecuteCompile(DefaultCompilerInstance& instance)
{
    FrontendOptions& opts = instance.invocation.frontendOptions;
    bool isEmitCHIR = instance.invocation.globalOptions.IsEmitCHIREnable();
    if (IsEmptyInputFile(instance) && opts.dumpAction != FrontendOptions::DumpAction::DESERIALIZE_CHIR) {
        return HandleEmptyInputFileSituation(instance);
    }

    if (opts.dumpAction != FrontendOptions::DumpAction::NO_ACTION) {
        return PerformDumpAction(instance, opts.dumpAction);
    }

    // Process compilation of frontend.
    if (!instance.Compile(CompileStage::CHIR)) {
        return false;
    }
    bool res = true;
    if (!isEmitCHIR && instance.invocation.globalOptions.outputMode != GlobalOptions::OutputMode::CHIR) {
        Cangjie::ICE::TriggerPointSetter iceSetter(CompileStage::CODEGEN);
        res = instance.PerformCodeGen() && res;
    }
    if (!isEmitCHIR) {
        Cangjie::ICE::TriggerPointSetter iceSetter(CompileStage::SAVE_RESULTS);
        res = instance.PerformCjoAndBchirSaving() && res;
    }
    return res;
}

int Cangjie::ExecuteFrontend(const std::string& exePath, const std::vector<std::string>& args,
    const std::unordered_map<std::string, std::string>& environmentVars)
{
    DiagnosticEngine diag;
    CompilerInvocation invocation;
    // Get absolute path of `cjc` executable program.
    auto cangjieHome =
        FileUtil::GetDirPath(FileUtil::GetDirPath(FileUtil::GetAbsPath(exePath) | FileUtil::IdenticalFunc));
    invocation.frontendOptions.executablePath = exePath;
    invocation.frontendOptions.ReadPathsFromEnvironmentVars(environmentVars);
    invocation.frontendOptions.cangjieHome = invocation.frontendOptions.environment.cangjieHome.value_or(cangjieHome);

    invocation.globalOptions.executablePath = invocation.frontendOptions.executablePath;
    invocation.globalOptions.environment = invocation.frontendOptions.environment;
    invocation.globalOptions.cangjieHome = invocation.frontendOptions.cangjieHome;
    if (!InitializeCompilerInvocation(invocation, args)) {
        return 1;
    }

    GlobalOptions& globalOptions = invocation.globalOptions;
    if (globalOptions.showUsage) {
        std::set<Options::Group> groups{Options::Group::GLOBAL, Options::Group::FRONTEND};
        invocation.optionTable->Usage(globalOptions.GetOptionsBackend(), groups);
        return 0;
    }

    if (globalOptions.enableVerbose || globalOptions.printVersionOnly) {
        Cangjie::PrintVersion();
        if (globalOptions.printVersionOnly) {
            return 0;
        }
    }
    if (globalOptions.enableTimer) {
        Utils::ProfileRecorder::Enable(true, Utils::ProfileRecorder::Type::TIMER);
    }
    if (globalOptions.enableMemoryCollect) {
        Utils::ProfileRecorder::Enable(true, Utils::ProfileRecorder::Type::MEMORY);
    }
    if (!TempFileManager::Instance().Init(globalOptions, true)) {
        return 1;
    }
    diag.SetErrorCountLimit(globalOptions.errorCountLimit);
    diag.RegisterHandler(globalOptions.diagFormat);
    std::unique_ptr<DefaultCompilerInstance> instance;
    if (NeedCreateIncrementalCompilerInstance(globalOptions)) {
        instance = std::make_unique<IncrementalCompilerInstance>(invocation, diag);
    } else if (globalOptions.compileCjd) {
        instance = std::make_unique<CjdCompilerInstance>(invocation, diag);
    } else {
        instance = std::make_unique<DefaultCompilerInstance>(invocation, diag);
    }

    if (ExecuteCompile(*instance)) {
        {
            Cangjie::ICE::TriggerPointSetter iceSetter(Cangjie::ICE::TriggerPointSetter::writeCahedTP);
            instance->UpdateAndWriteCachedInfoToDisk();
        }
        return 0;
    }
    return 1;
}

bool Cangjie::ExecuteFrontendByDriver(DefaultCompilerInstance& instance, const Driver& driver)
{
    if (ExecuteCompile(instance)) {
        driver.driverOptions->frontendOutputFiles = instance.invocation.globalOptions.frontendOutputFiles;
        driver.driverOptions->directBuiltinDependencies = instance.invocation.globalOptions.directBuiltinDependencies;
        driver.driverOptions->indirectBuiltinDependencies =
            instance.invocation.globalOptions.indirectBuiltinDependencies;
        driver.driverOptions->compilationCachedPath = instance.invocation.globalOptions.compilationCachedPath;
        driver.driverOptions->compilationCachedDir = instance.invocation.globalOptions.compilationCachedDir;
        driver.driverOptions->compilationCachedFileName = instance.invocation.globalOptions.compilationCachedFileName;
        driver.driverOptions->incrementalCompileNoChange =
            (instance.invocation.globalOptions.enIncrementalCompilation && instance.kind == IncreKind::NO_CHANGE);
        driver.driverOptions->symbolsNeedLocalized = instance.invocation.globalOptions.symbolsNeedLocalized;
        {
            Cangjie::ICE::TriggerPointSetter iceSetter(Cangjie::ICE::TriggerPointSetter::writeCahedTP);
            instance.UpdateAndWriteCachedInfoToDisk();
        }
        return true;
    }
    RuntimeInit::GetInstance().CloseRuntime();
    return false;
}

bool Cangjie::NeedCreateIncrementalCompilerInstance(const GlobalOptions& opts)
{
    auto& logger = IncrementalCompilationLogger::GetInstance();
    logger.SetDebugPrint(opts.printIncrementalInfo);
    if (opts.enIncrementalCompilation) {
        if (opts.mock == MockMode::ON) {
            logger.LogLn("enable mock, roll back to full compilation");
        } else if (opts.enableCoverage) {
            logger.LogLn("enable coverage, roll back to full compilation");
        } else if (opts.outputMode == GlobalOptions::OutputMode::CHIR) {
            logger.LogLn("enable compile common part mode, roll back to full compilation");
        } else if (opts.commonPartCjo) {
            logger.LogLn("enable compile platform part mode, roll back to full compilation");
        }
    }
    return opts.enIncrementalCompilation && opts.mock != MockMode::ON && !opts.enableCoverage &&
        !(opts.outputMode == GlobalOptions::OutputMode::CHIR) && !opts.commonPartCjo;
}
