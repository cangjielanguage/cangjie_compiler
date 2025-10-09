// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the Ohos_CJNATIVE ToolChain base class.
 */

#include "Toolchains/CJNATIVE/Ohos_CJNATIVE.h"

using namespace Cangjie;
using namespace Cangjie::Triple;

void Ohos_CJNATIVE::AddCRuntimeLibraryPaths()
{
    for (const auto& path : driverOptions.toolChainPaths) {
        AddCRuntimeLibraryPath(path);
    }
}

bool Ohos_CJNATIVE::PrepareDependencyPath()
{
    if ((objcopyPath = FindCangjieLLVMToolPath(g_toolList.at(ToolID::LLVM_OBJCOPY).name)).empty()) {
        return false;
    }
    if ((arPath = FindCangjieLLVMToolPath(g_toolList.at(ToolID::LLVM_AR).name)).empty()) {
        return false;
    }
    if ((ldPath = FindCangjieLLVMToolPath(g_toolList.at(ToolID::LLD).name)).empty()) {
        return false;
    }
    return true;
}

bool Ohos_CJNATIVE::GenerateLinking(const std::vector<TempFileInfo>& objFiles)
{
    // Different linking mode requires different gcc crt files
    GenerateLinkingTool(objFiles, "", {});
    return true;
}

void Ohos_CJNATIVE::GenerateLinkingTool(const std::vector<TempFileInfo>& objFiles, const std::string& gccLibPath,
    const std::pair<std::string, std::string>& /* gccCrtFilePair */)
{
    auto tool = std::make_unique<Tool>(ldPath, ToolType::BACKEND, driverOptions.environment.allVariables);
    tool->SetLdLibraryPath(FileUtil::JoinPath(FileUtil::GetDirPath(ldPath.c_str()), "../lib"));
    std::string outputFile = GetOutputFileInfo(objFiles).filePath;
    tool->AppendArg("-o", outputFile);
    if (driverOptions.IsLTOEnabled()) {
        GenerateLinkOptionsForLTO(*tool.get());
    } else if (driverOptions.EnableHwAsan()) {
        // Same args as lto except GenerateLinkOptionsForLTO
        tool->AppendArg("-z", "notext");
    } else {
        tool->AppendArg("-z", "noexecstack");
    }
    tool->AppendArg("-z", "max-page-size=4096");
    tool->AppendArgIf(driverOptions.stripSymbolTable, "-s");

    // Hot reload relies on .gnu.hash section.
    tool->AppendArg("--hash-style=both");

    tool->AppendArg("-m", GetEmulation());

    std::string cjldScript = GetCjldScript(tool);
    // Link order: Scrt1 -> crti -> crtbegin -> other input files -> crtend -> crtn.
    if (driverOptions.outputMode == GlobalOptions::OutputMode::EXECUTABLE) {
        tool->AppendArg("-pie");
        auto maybeCrt1 = FileUtil::FindFileByName("Scrt1.o", GetCRuntimeLibraryPath());
        // If we found Scrt1.o, we use the absolute path of system Scrt1.o, otherwise we let it simply be Scrt1.o,
        // we don't expect such cases though. Same as crti.o and crtend.o.
        tool->AppendArg(maybeCrt1.value_or("Scrt1.o"));
    }
    auto maybeCrti = FileUtil::FindFileByName("crti.o", GetCRuntimeLibraryPath());
    tool->AppendArg(maybeCrti.value_or("crti.o"));
    HandleLLVMLinkOptions(objFiles, gccLibPath, *tool, cjldScript);
    GenerateRuntimePath(*tool);
    auto maybeCrtn = FileUtil::FindFileByName("crtn.o", GetCRuntimeLibraryPath());
    tool->AppendArg(maybeCrtn.value_or("crtn.o"));
    backendCmds.emplace_back(MakeSingleToolBatch({std::move(tool)}));
}

void Ohos_CJNATIVE::GenerateLinkOptions(Tool& tool)
{
    tool.AppendArg("-l:libcangjie-runtime.so");
    for (auto& option : LINUX_CJNATIVE_LINK_OPTIONS) {
        // No libgcc_s.so in hm toolchain
        if (option.compare("-l gcc_s") != 0) {
            tool.AppendArg(option);
        }
    }
    tool.AppendArg("-lclang_rt.builtins");
    // Remind runtime to remove dependency of unwind_s in hm
    tool.AppendArg("-lunwind");
}

void Ohos_CJNATIVE::HandleSanitizerDependencies(Tool& tool)
{
    for (const auto& arg : {"-lpthread", "-lrt", "-lm", "-ldl", "-lresolv"}) {
        tool.AppendArg(arg);
    }
    // The ohos has no gcc_s, unwind (has same function as gcc_s) is
    // always linked in GenerateLinkOptions
}
