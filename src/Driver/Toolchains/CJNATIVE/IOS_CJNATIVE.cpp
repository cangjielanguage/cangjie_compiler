// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the IOS_CJNATIVE ToolChain base class.
 */

#include "Toolchains/CJNATIVE/IOS_CJNATIVE.h"

#include "cangjie/Driver/TempFileManager.h"
#include "cangjie/Driver/ToolOptions.h"
#include "cangjie/Driver/Utils.h"
#include "cangjie/Utils/FileUtil.h"

using namespace Cangjie;
using namespace Cangjie::Triple;

void IOS_CJNATIVE::AddSystemLibraryPaths()
{
    if (driverOptions.IsCrossCompiling() && driverOptions.customizedSysroot) {
        // user-specified sysroot is only considered in cross-compilation
        AddLibraryPaths(ComputeLibPaths());
    }
    MachO::AddSystemLibraryPaths();
}

// Shares most of its logic with Darwin_CJNATIVE::GenerateStaticLibObjects; the only
// difference is the -platform_version args (platform name, version, default SDK).
TempFileInfo IOS_CJNATIVE::GenerateStaticLibObjects(const std::vector<TempFileInfo>& objFiles)
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
    auto ltoObjectPath = FileUtil::JoinPath(ltoObjectDir, "0." + GetTargetArchString() + ".lto.o");
    GenerateLinkOptionsForLTO(*tool);
    tool->AppendArg("-object_path_lto", ltoObjectDir);
    tool->AppendArg("-staticlib");
    tool->AppendArg("-arch", GetTargetArchString());

    tool->AppendArg("-platform_version");
    if (driverOptions.target.env == Triple::Environment::SIMULATOR) {
        tool->AppendArg("ios-simulator");
        tool->AppendArg("17.5.0");
    } else {
        tool->AppendArg("ios");
        tool->AppendArg("17.5.0");
    }
    tool->AppendArg(darwinSDKVersion.value_or("17.5"));

    tool->AppendArg("-syslibroot");
    tool->AppendArg(driverOptions.sysroot.empty() ? "/" : driverOptions.sysroot);
    AppendLTOBcInputs(objFiles, *tool);

    backendCmds.emplace_back(MakeSingleToolBatch({std::move(tool)}));
    outputFileInfo.filePath = ltoObjectPath;
    outputFileInfo.rawPath = ltoObjectDir;
    return outputFileInfo;
}

TempFileInfo IOS_CJNATIVE::GenerateLinkingTool(
    const std::vector<TempFileInfo>& objFiles, const std::string& darwinSDKVersion)
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
    if (driverOptions.target.env == Triple::Environment::SIMULATOR) {
        tool->AppendArg("ios-simulator");
        tool->AppendArg("17.5.0");
    } else {
        tool->AppendArg("ios");
        tool->AppendArg("17.5.0");
    }
    tool->AppendArg(darwinSDKVersion);

    tool->AppendArg("-syslibroot");
    tool->AppendArg(driverOptions.sysroot.empty() ? "/" : driverOptions.sysroot);

    if (driverOptions.outputMode == GlobalOptions::OutputMode::EXECUTABLE) {
        tool->AppendArg("-pie");
    }
    if (driverOptions.stripSymbolTable) {
        tool->AppendArg("-install_name", GetOutputFileInfo(objFiles).filePath);
    }

    HandleLLVMLinkOptions(objFiles, *tool);
    GenerateRuntimePath(*tool);
    backendCmds.emplace_back(MakeSingleToolBatch({std::move(tool)}));
    return outputFileInfo;
}

void IOS_CJNATIVE::GenerateLinkOptions(Tool& tool)
{
    for (auto& option : IOS_CJNATIVE_LINK_OPTIONS) {
        tool.AppendArg(option);
    }
    auto cangjieLibPath =
        FileUtil::JoinPath(FileUtil::JoinPath(driver.cangjieHome, "lib"),
                           driverOptions.GetCangjieLibTargetPathName());
    if (driverOptions.target.env == Triple::Environment::SIMULATOR) {
        tool.AppendArg(FileUtil::JoinPath(cangjieLibPath, "libclang_rt.iossim.a"));
    } else {
        tool.AppendArg(FileUtil::JoinPath(cangjieLibPath, "libclang_rt.ios.a"));
    }
}

std::string IOS_CJNATIVE::GetClangRTProfileLibraryName() const
{
    if (driverOptions.target.env == Triple::Environment::SIMULATOR) {
        return "libclang_rt.profile_iossim.a";
    } else {
        return "libclang_rt.profile_ios.a";
    }
}
