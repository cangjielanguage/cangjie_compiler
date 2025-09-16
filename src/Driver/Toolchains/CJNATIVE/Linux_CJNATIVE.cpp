// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the Linux_CJNATIVE ToolChain base class.
 */

#include "Toolchains/CJNATIVE/Linux_CJNATIVE.h"

#include "cangjie/Driver/ToolOptions.h"
#include "cangjie/Driver/Utils.h"
#include "cangjie/Utils/FileUtil.h"

using namespace Cangjie;
using namespace Cangjie::Triple;

void Linux_CJNATIVE::AddSystemLibraryPaths()
{
    if (driverOptions.IsCrossCompiling() && driverOptions.customizedSysroot) {
        // user-specified sysroot is only considered in cross-compilation
        AddLibraryPaths(ComputeLibPaths());
    }
    Gnu::AddSystemLibraryPaths();
}

std::pair<std::string, std::string> Linux_CJNATIVE::GetGccCrtFilePair() const
{
    switch (driverOptions.outputMode) {
        case GlobalOptions::OutputMode::EXECUTABLE:
        case GlobalOptions::OutputMode::SHARED_LIB:
            return gccSharedCrtFilePair;
            // WARNING: OutputMode::STATIC_LIB has no relation with gcc static crt files.
            // OutputMode::STATIC_LIB indicates that we should produce an archive(.a) file, which is similar to '-c'
            // Static crt files are for 'ld -static' but it is not supported currently.
        case GlobalOptions::OutputMode::STATIC_LIB:
        default:
            break;
    }
    return {};
}

std::string Linux_CJNATIVE::GetCjldScript(const std::unique_ptr<Tool>& tool) const
{
    std::string cjldScript = "cjld.lds";
    switch (driverOptions.outputMode) {
        case GlobalOptions::OutputMode::SHARED_LIB:
            tool->AppendArg("-shared");
            cjldScript = "cjld.shared.lds";
            [[fallthrough]];
        case GlobalOptions::OutputMode::EXECUTABLE:
            tool->AppendArg("-dynamic-linker");
            tool->AppendArg(GetDynamicLinkerPath(driverOptions.target));
            break;
        case GlobalOptions::OutputMode::STATIC_LIB:
        default:
            break;
    }
    return cjldScript;
}

void Linux_CJNATIVE::GenerateLinkingTool(const std::vector<TempFileInfo>& objFiles, const std::string& gccLibPath,
    const std::pair<std::string, std::string>& gccCrtFilePair)
{
    auto tool = std::make_unique<Tool>(ldPath, ToolType::BACKEND, driverOptions.environment.allVariables);
    std::string outputFile = GetOutputFileInfo(objFiles).filePath;
    tool->AppendArg("-o", outputFile);
    if (driverOptions.IsLTOEnabled()) {
        tool->SetLdLibraryPath(FileUtil::JoinPath(FileUtil::GetDirPath(ldPath), "../lib"));
        GenerateLinkOptionsForLTO(*tool.get());
    } else if (driverOptions.EnableHwAsan()) {
        // same args as lto except GenerateLinkOptionsForLTO
        tool->SetLdLibraryPath(FileUtil::JoinPath(FileUtil::GetDirPath(ldPath), "../lib"));
        tool->AppendArg("-z", "notext");
    } else {
        tool->AppendArg("-z", "noexecstack");
    }

    tool->AppendArgIf(driverOptions.stripSymbolTable, "-s");
    tool->AppendArg("-m", GetEmulation());

    // Hot reload relies on .gnu.hash section.
    tool->AppendArg("--hash-style=both");
    tool->AppendArgIf(driverOptions.enableGcSections, "-gc-sections");

    std::string cjldScript = GetCjldScript(tool);
    // Link order: crt1 -> crti -> crtbegin -> other input files -> crtend -> crtn.
    if (driverOptions.outputMode == GlobalOptions::OutputMode::EXECUTABLE) {
        tool->AppendArg("-pie");
        auto maybeCrt1 = FileUtil::FindFileByName("Scrt1.o", GetCRuntimeLibraryPath());
        // If we found Scrt1.o, we use the absolute path of system Scrt1.o, otherwise we let it simply be Scrt1.o,
        // we don't expect such cases though. Same as crti.o and crtend.o.
        tool->AppendArg(maybeCrt1 ? maybeCrt1.value() : "Scrt1.o");
    }
    auto maybeCrti = FileUtil::FindFileByName("crti.o", GetCRuntimeLibraryPath());
    tool->AppendArg(maybeCrti ? maybeCrti.value() : "crti.o");
    tool->AppendArg(GetGccLibFile(gccCrtFilePair.first, gccLibPath));
    // Add crtfastmath.o if fast math is enabled and crtfastmath.o is found.
    if (driverOptions.fastMathMode) {
        auto crtfastmathFilepath = GetGccLibFile("crtfastmath.o", gccLibPath);
        tool->AppendArgIf(FileUtil::FileExist(crtfastmathFilepath), crtfastmathFilepath);
    }
    HandleLLVMLinkOptions(objFiles, gccLibPath, *tool, cjldScript);
    GenerateRuntimePath(*tool);
    tool->AppendArg(GetGccLibFile(gccCrtFilePair.second, gccLibPath));
    auto maybeCrtn = FileUtil::FindFileByName("crtn.o", GetCRuntimeLibraryPath());
    tool->AppendArg(maybeCrtn ? maybeCrtn.value() : "crtn.o");
    backendCmds.emplace_back(MakeSingleToolBatch({std::move(tool)}));
}

void Linux_CJNATIVE::GenerateLinkOptions(Tool& tool)
{
    if (driverOptions.linkStatic) {
        if (driverOptions.EnableSanitizer()) {
            auto cangjieLibPath = FileUtil::JoinPath(FileUtil::JoinPath(
                FileUtil::JoinPath(driver.cangjieHome, "lib"),
                driverOptions.GetCangjieLibTargetPathName()),
                driverOptions.SanitizerTypeToShortString());
            tool.AppendArg(FileUtil::JoinPath(cangjieLibPath, "libcangjie-runtime.a"));
            tool.AppendArg(FileUtil::JoinPath(cangjieLibPath, "libcangjie-thread.a"));
            tool.AppendArg(FileUtil::JoinPath(cangjieLibPath, "libboundscheck-static.a"));
        } else {
            tool.AppendArg("-l:libcangjie-runtime.a");
            tool.AppendArg("-l:libcangjie-thread.a");
            tool.AppendArg("-l:libboundscheck-static.a");
        }
        tool.AppendArg(LINUX_STATIC_LINK_OPTIONS);
    } else {
        tool.AppendArg("-l:libcangjie-runtime.so");
        tool.AppendArg(LINUX_CJNATIVE_LINK_OPTIONS);
    }
    // `__gnu_h2f_ieee` `__gnu_f2h_ieee` two symbols are needed to float16 on non-arm64 platform.
    // Because std-core package is always imported, we put it here.
    if (driverOptions.target.arch != Triple::ArchType::AARCH64) {
        tool.AppendArg("-lclang_rt-builtins");
    } else {
        if (driverOptions.target.os == Triple::OSType::LINUX && driverOptions.target.env == Triple::Environment::GNU) {
            tool.AppendArg("-lgcc");
        }
    }
}

void Linux_CJNATIVE::GenerateLinkOptionsForLTO(Tool& tool) const
{
    using namespace ToolOptions;

    // Set LTO lld options
    SetFuncType setOptionHandler = [&tool](const std::string& option) { tool.AppendArg(option); };
    std::vector<ToolOptionType> setOptionsPass = {
        LLD::SetLTOOptimizationLevelOptions, // Comment ensure vector members are arranged vertically.
        LLD::SetLTOOptions,                  //
    };
    SetOptions(setOptionHandler, driverOptions, setOptionsPass);

    // Set opt passes
    std::string passesCollector = "--lto-newpm-passes=";
    std::vector<std::string> passItems;
    {
        using namespace ToolOptions;
        // remove the initial hyphen in the options.
        SetFuncType handler = [&passItems](const std::string& option) { passItems.emplace_back(option.substr(1)); };
        SetOptions(handler, driverOptions, OPT::SetNewPassManagerOptions);
    }
    for (auto& it : passItems) {
        passesCollector += it;
        if (&it != &passItems.back()) {
            passesCollector += ",";
        }
    }
    tool.AppendArg(passesCollector);

    // use set to avoid duplicate options
    std::unordered_set<std::string> optionSet = {};
    // set composite option
    SetFuncType setCompositeOption = [&tool, &optionSet](const std::string& option) {
        if (optionSet.find(option) == optionSet.end()) {
            optionSet.emplace(option);
            tool.AppendArg("--mllvm");
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