// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the Android_CJNATIVE ToolChain base class.
 */

#include "Toolchains/CJNATIVE/Android_CJNATIVE.h"

#include <string>
#include <vector>

#include "cangjie/Driver/Toolchains/GCCPathScanner.h"
#include "cangjie/Utils/FileUtil.h"

using namespace Cangjie;
using namespace Cangjie::Triple;

void Android_CJNATIVE::InitializeLibraryPaths()
{
    auto deduceToolchainLibPath = [this](const std::string& toolchainPath) {
        auto toolChainRoot = FileUtil::GetDirPath(toolchainPath);
        auto clangrtLibRoot = FileUtil::JoinPath(toolChainRoot, "lib64/clang");
        if (!FileUtil::FileExist(clangrtLibRoot)) {
            clangrtLibRoot = FileUtil::JoinPath(toolChainRoot, "lib/clang");
        }
        std::vector<FileUtil::Directory> subDirs = FileUtil::GetDirectories(clangrtLibRoot);
        GCCVersion selectedClangVersion{0, 0, 0};
        std::string selectedClangPath{};
        for (const auto& dir : subDirs) {
            std::optional<GCCVersion> gccVersion = GCCPathScanner::StrToGCCVersion(dir.name);
            if (gccVersion && selectedClangVersion < *gccVersion) {
                selectedClangPath = dir.path;
                selectedClangVersion = *gccVersion;
            }
        }
        if (!selectedClangPath.empty()) {
            AddLibraryPath(FileUtil::JoinPath(selectedClangPath, "lib/linux/aarch64"));
            AddLibraryPath(FileUtil::JoinPath(selectedClangPath, "lib/linux"));
        }
    };
    // 1. Deduce libs path from toolchain.
    // If the toolchain path is not specified by -B/--toolchain, deduce libs path from the environment path.
    auto clangPath = FileUtil::FindProgramByName(driverOptions.target.GetEffectiveTripleString() + "-clang",
        driverOptions.toolChainPaths.empty() ? driverOptions.environment.paths : driverOptions.toolChainPaths);
    if (!clangPath.empty()) {
        auto clangDir = FileUtil::GetDirPath(clangPath);
        deduceToolchainLibPath(clangDir);
        sysroot = FileUtil::JoinPath(FileUtil::GetDirPath(clangDir), "sysroot");
    }
    if (driverOptions.customizedSysroot) {
        sysroot = driverOptions.sysroot;
    }
    // 2. Deduce libs path from sysroot.
    if (!sysroot.empty()) {
        std::string tripleDirectory = "usr/lib/" + driverOptions.target.ArchToString() + "-linux-android/";
        AddLibraryPath(FileUtil::JoinPath(sysroot, tripleDirectory + driverOptions.target.apiLevel));
        AddLibraryPath(FileUtil::JoinPath(sysroot, tripleDirectory));
        AddLibraryPath(sysroot);
    }
    AddCRuntimeLibraryPaths();
}

void Android_CJNATIVE::AddCRuntimeLibraryPaths()
{
    std::string tripleDirectory = "usr/lib/" + driverOptions.target.ArchToString() + "-linux-android/";
    AddCRuntimeLibraryPath(FileUtil::JoinPath(sysroot, tripleDirectory + driverOptions.target.apiLevel));
}

bool Android_CJNATIVE::PrepareDependencyPath()
{
    if ((objcopyPath = FindCangjieLLVMToolPath(g_toolList.at(ToolID::LLVM_OBJCOPY).name)).empty()) {
        return false;
    }
    if ((arPath = FindUserToolPath(g_toolList.at(ToolID::LLVM_AR).name)).empty()) {
        return false;
    }
    if ((ldPath = FindCangjieLLVMToolPath(g_toolList.at(ToolID::LLD).name)).empty()) {
        return false;
    }
    return true;
}

bool Android_CJNATIVE::GenerateLinking(const std::vector<TempFileInfo>& objFiles)
{
    // Different linking mode requires different gcc crt files
    GenerateLinkingTool(objFiles, "", {});
    return true;
}

void Android_CJNATIVE::GenerateLinkingTool(const std::vector<TempFileInfo>& objFiles, const std::string& gccLibPath,
    const std::pair<std::string, std::string>& /* gccCrtFilePair */)
{
    auto tool = std::make_unique<Tool>(ldPath, ToolType::BACKEND, driverOptions.environment.allVariables);
    tool->SetLdLibraryPath(FileUtil::JoinPath(FileUtil::GetDirPath(ldPath.c_str()), "../lib"));
    std::string outputFile = GetOutputFileInfo(objFiles).filePath;
    tool->AppendArg("-o", outputFile);
    if (driverOptions.IsLTOEnabled()) {
        GenerateLinkOptionsForLTO(*tool.get());
        // The -z notext option is the default for ld, while the -z noexecstack option is the default for lld.
        // Therefore, ld needs to explicitly pass -z noexecstack, and lld needs to explicitly pass -z notext.
        tool->AppendArg("-z", "notext");
    } else {
        tool->AppendArg("-z", "noexecstack");
    }
    tool->AppendArgIf(driverOptions.stripSymbolTable, "-s");

    // Hot reload relies on .gnu.hash section.
    tool->AppendArg("--hash-style=both");
    tool->AppendArg("-EL");
    tool->AppendArg("-m", GetEmulation());

    std::string cjldScript = GetCjldScript(tool);
    if (driverOptions.outputMode == GlobalOptions::OutputMode::EXECUTABLE) {
        tool->AppendArg("-pie");
    }
    std::string crtBeginName =
        driverOptions.outputMode == GlobalOptions::OutputMode::EXECUTABLE ? "crtbegin_dynamic.o" : "crtbegin_so.o";
    auto maybeCrtBegin = FileUtil::FindFileByName(crtBeginName, GetCRuntimeLibraryPath());

    tool->AppendArg(maybeCrtBegin.value_or(crtBeginName));
    HandleLLVMLinkOptions(objFiles, gccLibPath, *tool, cjldScript);
    std::string crtEndName =
        driverOptions.outputMode == GlobalOptions::OutputMode::EXECUTABLE ? "crtend_android.o" : "crtend_so.o";
    auto maybeCrtEnd = FileUtil::FindFileByName(crtEndName, GetCRuntimeLibraryPath());
    tool->AppendArg(maybeCrtEnd.value_or(crtEndName));
    backendCmds.emplace_back(MakeSingleToolBatch({std::move(tool)}));
}

void Android_CJNATIVE::GenerateLinkOptions(Tool& tool)
{
    tool.AppendArg("-lclang_rt.builtins-" + driverOptions.target.ArchToString() + "-android");
    tool.AppendArg("-l:libcangjie-runtime.so");
    tool.AppendArg(LINUX_CJNATIVE_LINK_OPTIONS);
    tool.AppendArg("-lclang_rt.builtins-" + driverOptions.target.ArchToString() + "-android");
    tool.AppendArg("-ldl");
    tool.AppendArg("-z", "max-page-size=4096");
}

void Android_CJNATIVE::HandleSanitizerDependencies(Tool& tool)
{
    for (const auto& arg : {"-lpthread", "-lrt", "-lm", "-ldl", "-lresolv"}) {
        tool.AppendArg(arg);
    }
}