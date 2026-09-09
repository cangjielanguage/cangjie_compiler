// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the Darwin_CJNATIVE ToolChain base class.
 */

#include "Toolchains/CJNATIVE/Darwin_CJNATIVE.h"

#include <unordered_set>

#include "cangjie/Basic/Utils.h"
#include "cangjie/Driver/TempFileManager.h"
#include "cangjie/Driver/ToolOptions.h"
#include "cangjie/Driver/Utils.h"
#include "cangjie/Utils/FileUtil.h"

using namespace Cangjie;
using namespace Cangjie::Triple;

namespace {
GlobalOptions::OptimizationLevel GetEffectiveOptimizationLevel(const DriverOptions& driverOptions)
{
    return driverOptions.disableBackendOpt ? GlobalOptions::OptimizationLevel::O0 : driverOptions.optimizationLevel;
}
} // namespace

void Darwin_CJNATIVE::AddSystemLibraryPaths()
{
    if (driverOptions.IsCrossCompiling() && driverOptions.customizedSysroot) {
        // user-specified sysroot is only considered in cross-compilation
        AddLibraryPaths(ComputeLibPaths());
    }
    MachO::AddSystemLibraryPaths();
}

void Darwin_CJNATIVE::GenerateLinkOptionsForLTO(Tool& tool) const
{
    using namespace ToolOptions;

    // Set LTO linker options supported by ld64.lld.
    SetFuncType setOptionHandler = [&tool](const std::string& option) { tool.AppendArg(option); };
    SetOptions(setOptionHandler, driverOptions, LLD::SetLTOOptimizationLevelOptions);
    if (driverOptions.IsFullLTOEnabled()) {
        tool.AppendArg("-mllvm");
        tool.AppendArg("-cangjie-full-lto");
    }
    if (GetEffectiveOptimizationLevel(driverOptions) == GlobalOptions::OptimizationLevel::O2) {
        tool.AppendArg("-mllvm");
        tool.AppendArg("--cj-safepoint-outline=false");
    }
    tool.AppendArg("-mllvm");
    tool.AppendArg("--cj-lto-opt");

    // Set --visible-pkgs for symbol visibility control in LTO mode.
    // This is also needed when building static libraries with LTO, so we use
    // IsLTOPkgVisibilityEnabled / IsCompileAsExeEnabled instead of checking outputMode.
    if (driverOptions.IsLTOPkgVisibilityEnabled() || driverOptions.IsCompileAsExeEnabled()) {
        if (!driverOptions.GetLtoVisiblePkgs().empty()) {
            tool.AppendArg("--visible-pkgs=" + Utils::JoinStrings(driverOptions.GetLtoVisiblePkgs(), ","));
        } else {
            tool.AppendArg("--visible-pkgs=");
        }
    }

    std::unordered_set<std::string> optionSet = {};
    SetFuncType setCompositeOption = [&tool, &optionSet](const std::string& option) {
        if (optionSet.find(option) == optionSet.end()) {
            optionSet.emplace(option);
            tool.AppendArg("-mllvm");
            tool.AppendArg(option);
        }
    };
    std::vector<ToolOptionType> setCompositeOptionsPass = {
        OPT::SetOptions,                // Comment ensure vector members are arranged vertically.
        OPT::SetCodeObfuscationOptions, //
        OPT::SetTransparentOptions,     // The transparent OPT options must after other OPT options.
        LLD::SetPgoOptions,             //
        LLC::SetOptions,                //
        LLC::SetTransparentOptions,     // The transparent LLC options must after other LLC options.
    };
    SetOptions(setCompositeOption, driverOptions, setCompositeOptionsPass);
}

void Darwin_CJNATIVE::GenerateArchiveTool(const std::vector<TempFileInfo>& objFiles)
{
    if (!driverOptions.ShouldEmitStaticLibInLTO()) {
        MachO::GenerateArchiveTool(objFiles);
        return;
    }
    auto ltoObject = GenerateStaticLibObjects(objFiles);
    auto archiveTool = std::make_unique<Tool>(arPath, ToolType::BACKEND, driverOptions.environment.allVariables);
    archiveTool->AppendArg("cr");
    auto archiveOutput = CreateNewFileInfoWrapper(objFiles, TempFileKind::O_STATICLIB);
    auto removeArchive = std::make_unique<Tool>("RemoveFile", ToolType::INTERNAL_IMPLEMENTED,
        driverOptions.environment.allVariables);
    removeArchive->AppendArg(archiveOutput.filePath);
    backendCmds.emplace_back(MakeSingleToolBatch({std::move(removeArchive)}));
    archiveTool->AppendArg(archiveOutput.filePath);
    archiveTool->AppendArgsFromDirAtExecution(ltoObject.rawPath, "lto.o");
    backendCmds.emplace_back(MakeSingleToolBatch({std::move(archiveTool)}));
}

TempFileInfo Darwin_CJNATIVE::GenerateStaticLibObjects(const std::vector<TempFileInfo>& objFiles)
{
    std::optional<std::string> darwinSDKVersion = GetDarwinSDKVersion(driverOptions.sysroot);
    if (driverOptions.enableVerbose) {
        Infoln("selected Darwin SDK path: " + driverOptions.sysroot +
            " (SDK Version: " + darwinSDKVersion.value_or("N/A") + ")");
    }
    auto tool = std::make_unique<Tool>(ldPath, ToolType::BACKEND, driverOptions.environment.allVariables);
    tool->SetLdLibraryPath(FileUtil::JoinPath(FileUtil::GetDirPath(ldPath), "../lib"));
    auto outputFileInfo = CreateNewFileInfoWrapper(objFiles, TempFileKind::T_OBJ);
    auto ltoObjectDir = outputFileInfo.filePath + ".lto";
    // Linker always produces nums.<arch>.lto.o inside -object_path_lto, so the name must match.
    auto ltoObjectPath = FileUtil::JoinPath(ltoObjectDir, "0." + GetTargetArchString() + ".lto.o");
    GenerateLinkOptionsForLTO(*tool);
    tool->AppendArg("-object_path_lto", ltoObjectDir);
    tool->AppendArg("-staticlib");
    tool->AppendArg("-arch", GetTargetArchString());

    tool->AppendArg("-platform_version");
    tool->AppendArg("macos");
    tool->AppendArg("12.0.0");
    tool->AppendArg(darwinSDKVersion.value_or("12"));

    tool->AppendArg("-syslibroot");
    tool->AppendArg(driverOptions.sysroot.empty() ? "/" : driverOptions.sysroot);
    AppendLTOBcInputs(objFiles, *tool);

    backendCmds.emplace_back(MakeSingleToolBatch({std::move(tool)}));
    outputFileInfo.filePath = ltoObjectPath;
    outputFileInfo.rawPath = ltoObjectDir;
    return outputFileInfo;
}

TempFileInfo Darwin_CJNATIVE::GenerateLinkingTool(const std::vector<TempFileInfo>& objFiles,
    const std::string& darwinSDKVersion)
{
    auto tool = std::make_unique<Tool>(ldPath, ToolType::BACKEND, driverOptions.environment.allVariables);
    auto outputFileInfo = TempFileInfo{};
    if (driverOptions.stripSymbolTable) {
        TempFileKind kind = driverOptions.outputMode == GlobalOptions::OutputMode::SHARED_LIB
            ? TempFileKind::T_DYLIB_MAC
            : TempFileKind::T_EXE_MAC;
        outputFileInfo = CreateNewFileInfoWrapper(objFiles, kind);
    } else {
        outputFileInfo = GetOutputFileInfo(objFiles);
    }
    std::string outputFile = outputFileInfo.filePath;
    tool->AppendArg("-o", outputFile);
    if (driverOptions.IsLTOEnabled()) {
        tool->SetLdLibraryPath(FileUtil::JoinPath(FileUtil::GetDirPath(ldPath), "../lib"));
        GenerateLinkOptionsForLTO(*tool);
        if (!driverOptions.IsFullLTOEnabled()) {
            tool->AppendArg("-cache_path_lto");
            tool->AppendArg(driverOptions.compilationCachedPath);
        }
    }
    tool->AppendArgIf(driverOptions.outputMode == GlobalOptions::OutputMode::SHARED_LIB, "-dylib");
    tool->AppendArg("-arch", GetTargetArchString());

    tool->AppendArg("-platform_version");
    tool->AppendArg("macos");
    tool->AppendArg("12.0.0");
    tool->AppendArg(darwinSDKVersion);

    tool->AppendArg("-syslibroot");
    tool->AppendArg(driverOptions.sysroot.empty() ? "/" : driverOptions.sysroot);

    if (driverOptions.stripSymbolTable) {
        tool->AppendArg("-install_name", GetOutputFileInfo(objFiles).filePath);
    }

    if (driverOptions.outputMode == GlobalOptions::OutputMode::EXECUTABLE) {
        tool->AppendArg("-pie");
    }
    HandleLLVMLinkOptions(objFiles, *tool);
    GenerateRuntimePath(*tool);
    backendCmds.emplace_back(MakeSingleToolBatch({std::move(tool)}));
    return outputFileInfo;
}

void Darwin_CJNATIVE::GenerateLinkOptions(Tool& tool)
{
    auto cangjieLibPath =
        FileUtil::JoinPath(FileUtil::JoinPath(driver.cangjieHome, "lib"), driverOptions.GetCangjieLibTargetPathName());
    if (driverOptions.linkStatic) {
        tool.AppendArg(FileUtil::JoinPath(cangjieLibPath, "libcangjie-runtime.a"));
        tool.AppendArg(FileUtil::JoinPath(cangjieLibPath, "libcangjie-thread.a"));
        tool.AppendArg(FileUtil::JoinPath(cangjieLibPath, "libboundscheck-static.a"));
        tool.AppendArg("-lc++");
    } else {
        tool.AppendArg("-lcangjie-runtime");
        tool.AppendArg("-lboundscheck");
    }
    tool.AppendArg("-lSystem");
    tool.AppendArg(FileUtil::JoinPath(cangjieLibPath, "libclang_rt.osx.a"));
}
