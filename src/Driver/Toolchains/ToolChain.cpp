// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the ToolChain base class.
 */

#include "cangjie/Driver/Toolchains/ToolChain.h"
#include "cangjie/Driver/Utils.h"
#include "cangjie/Driver/TempFileManager.h"

namespace {
const std::string LINK_PREFIX = "-l:";
const std::string STATIC_LIB_EXTEBSION = ".a";

const std::map<std::string, std::string> LLVM_LTO_CSTD_FFI_OPTION_MAP = {
#define CJNATIVE_LTO_CSTD_FFI_OPTIONS(STATILIB, FFILIB) {STATILIB, FFILIB},
#include "Toolchains/BackendOptions.inc"
#undef CJNATIVE_LTO_CSTD_FFI_OPTIONS
};

std::optional<std::tuple<std::string, uint64_t>> GetElementFromOrderVector(
    std::vector<std::tuple<std::string, uint64_t>>& orderVector, const std::string& name)
{
    std::optional<std::tuple<std::string, uint64_t>> result = {};
    for (auto it = orderVector.begin(); it < orderVector.end(); ++it) {
        std::tuple<std::string, uint64_t> tuple = *it;
        if (std::get<0>(tuple) == name) {
            (void)orderVector.erase(it);
            result = tuple;
            break;
        }
    }
    return result;
}
}; // namespace

using namespace Cangjie;

void ToolChain::GenerateLinkOptionsOfBuiltinLibs(Tool& tool) const
{
    bool notOutputDylib = driverOptions.outputMode != GlobalOptions::OutputMode::SHARED_LIB;
    // When user compiling to a shared library, standard library must be dynamic linked or there might be multiple
    // versions of standard library exists in different dynamic libraries. A user could still specify --static-std
    // to reproduct the problem. Here we only ensure the default behavior of cjc is correct.
    bool isStaticLink = driverOptions.linkStaticStd.value_or(notOutputDylib);
    if (isStaticLink) {
        return GenerateLinkOptionsOfBuiltinLibsForStaticLink(tool);
    } else {
        return GenerateLinkOptionsOfBuiltinLibsForDyLink(tool);
    }
}

void ToolChain::GenerateLinkOptionsOfBuiltinLibsForStaticLink(Tool& tool) const
{
    std::set<std::string> dynamicLibraries;
    std::set<std::string> staticLibraries;
    std::set<std::string> ltoBuiltInDependencies;
    std::unordered_set<std::string> dyDependencies;
    const std::function<void(const std::unordered_set<std::string>&)> getDyDependencies =
        [this, &dyDependencies](const std::unordered_set<std::string>& dependencies) {
            for (auto& cjoFileName : dependencies) {
                auto staticLib = FileUtil::ConvertFilenameToLibCangjieFormat(cjoFileName, STATIC_LIB_EXTEBSION);
                if (ALWAYS_DYNAMIC_LINK_STD_LIBRARIES.find(staticLib) !=
                    ALWAYS_DYNAMIC_LINK_STD_LIBRARIES.end()) {
                    dyDependencies.emplace(staticLib);
                    if (driverOptions.target.os != Triple::OSType::WINDOWS) {
                        dyDependencies.insert(ALWAYS_DYNAMIC_LINK_STD_LIBRARIES.at(staticLib).begin(),
                            ALWAYS_DYNAMIC_LINK_STD_LIBRARIES.at(staticLib).end());
                    }
                }
            }
        };
    getDyDependencies(driverOptions.directBuiltinDependencies);
    getDyDependencies(driverOptions.indirectBuiltinDependencies);
    const std::function<void(std::string)> appendStaticLibsToTool =
        [this, &dynamicLibraries, &staticLibraries, &ltoBuiltInDependencies, &dyDependencies]
            (const std::string& cjoFileName) {
            auto staticLib = FileUtil::ConvertFilenameToLibCangjieFormat(cjoFileName, STATIC_LIB_EXTEBSION);
            if (dyDependencies.find(staticLib) != dyDependencies.end() && !driverOptions.linkStatic) {
                dynamicLibraries.emplace(LINK_PREFIX +
                    FileUtil::ConvertFilenameToLibCangjieFormat(cjoFileName, GetSharedLibraryExtension()));
                return;
            }
            if (driverOptions.IsLTOEnabled()) {
                auto found = LLVM_LTO_CSTD_FFI_OPTION_MAP.find(staticLib);
                if (found != LLVM_LTO_CSTD_FFI_OPTION_MAP.end()) {
                    staticLibraries.emplace(LINK_PREFIX + found->second);
                }
                ltoBuiltInDependencies.emplace(FileUtil::ConvertFilenameToLtoLibCangjieFormat(cjoFileName));
            } else {
                // search sanitizer path
                if (driverOptions.sanitizerType != GlobalOptions::SanitizerType::NONE) {
                    auto cangjieLibPath = FileUtil::JoinPath(
                        FileUtil::JoinPath(FileUtil::JoinPath(driver.cangjieHome, "lib"),
                        driverOptions.GetCangjieLibTargetPathName()), driverOptions.SanitizerTypeToShortString());
                    staticLibraries.emplace(FileUtil::JoinPath(cangjieLibPath, staticLib));
                    return;
                }
                staticLibraries.emplace(LINK_PREFIX + staticLib);
            }
            CheckOtherDependeniesOfStaticLib(staticLib, dynamicLibraries, staticLibraries);
        };

    ForEachBuiltinDependencies(driverOptions.directBuiltinDependencies, appendStaticLibsToTool);
    ForEachBuiltinDependencies(driverOptions.indirectBuiltinDependencies, appendStaticLibsToTool);
    if (driverOptions.IsLTOEnabled()) {
        for (const auto& bcFile : ltoBuiltInDependencies) {
            tool.AppendArg(bcFile);
        }
    }
    // Static libraries are not sorted, thus we need to group them or symbols may be discarded by the linker.
    tool.AppendArg("--start-group");
    for (const auto& other : staticLibraries) {
        tool.AppendArg(other);
    }
    tool.AppendArg("--end-group");
    for (const auto& lib : dynamicLibraries) {
        tool.AppendArg(lib);
    }
}

void ToolChain::GenerateLinkOptionsOfBuiltinLibsForDyLink(Tool& tool) const
{
    const std::function<void(std::string)> appendDyLibsToTool = [this, &tool](const std::string& cjoFileName) {
        tool.AppendArg(LINK_PREFIX +
            FileUtil::ConvertFilenameToLibCangjieFormat(cjoFileName, GetSharedLibraryExtension()));
    };

    ForEachBuiltinDependencies(driverOptions.directBuiltinDependencies, appendDyLibsToTool);
    // Link indirect dependent dynamic libraries surrounded by `--as-needed` and `--no-as-needed`.
    // For the current implementation of generic types of cangjie, some symbols may be shared across
    // libraries, which means that an indirect dependency may be an direct dependency. Thus we must link
    // indirect dependencies here. Indirect dependencies are passed after `--as-needed` options
    // so unnecessary dependencies will be discarded by the linker.
    tool.AppendArgIf(!driverOptions.target.IsMinGW(), "--as-needed");
    ForEachBuiltinDependencies(driverOptions.indirectBuiltinDependencies, appendDyLibsToTool);
    tool.AppendArgIf(!driverOptions.target.IsMinGW(), "--no-as-needed");
}

void ToolChain::CheckOtherDependeniesOfStaticLib(
    const std::string& libName, std::set<std::string>& dynamicLibraries, std::set<std::string>& otherLibs) const
{
    if (libName == "libcangjie-std-net.a") {
        if (driverOptions.target.IsMinGW()) {
            otherLibs.emplace("-l:libws2_32.a");
        }
    } else if (libName == "cangjie-dynamicLoader-openssl.a") {
        if (!driverOptions.target.IsMinGW()) {
            dynamicLibraries.emplace("-ldl");
        }
        if (driverOptions.target.IsMinGW()) {
            dynamicLibraries.emplace("-lcrypt32");
        }
    } else if (libName == "libcangjie-std-ffi.python.a") {
        // From Glibc 2.34 libdl is provided as a part of libc and libdl.so is no longer provided as a
        // separated library. However, a dummy libdl.a is still provided for backwards compatibility.
        // Here we have to also link against `libdl` instead of `libdl.so` for compatibility.
        dynamicLibraries.emplace("-ldl");
    } else if (libName == "libcangjie-std-ast.a") {
        otherLibs.emplace("-l:libcangjie-std-astFFI.a");
        // std-ast relies on unwind symbols, which supplied by clang or gcc runtime library. Linking some versions of
        // gcc.a before clang_rt-builtins causes multiple definitions, thus we need to link clang_rt-builtins first.
        if (driverOptions.target.arch != Triple::ArchType::AARCH64 &&
            driverOptions.target.os == Triple::OSType::LINUX && driverOptions.target.env != Triple::Environment::OHOS) {
            otherLibs.emplace("-lclang_rt-builtins");
        }
        if (driverOptions.target.os == Triple::OSType::LINUX &&
            driverOptions.target.env == Triple::Environment::ANDROID) {
            otherLibs.emplace("-lc++");
            otherLibs.emplace("-lunwind");
        } else {
            dynamicLibraries.emplace("-lgcc_s");
        }
    }
}
void ToolChain::AppendObjectsFromCompiled(Tool& tool, const std::vector<TempFileInfo>& objFiles,
    std::vector<std::tuple<std::string, uint64_t>>& inputOrderTuples)
{
    std::vector<std::tuple<std::string, uint64_t>> itemTuples;
    itemTuples.insert(itemTuples.begin(), driverOptions.inputFileOrder.begin(), driverOptions.inputFileOrder.end());
    for (const auto& objFile : objFiles) {
        if (objFile.isFrontendOutput) {
            tool.AppendArg(objFile.filePath);
        } else {
            auto found = GetElementFromOrderVector(itemTuples, objFile.rawPath);
            if (found.has_value()) {
                inputOrderTuples.emplace_back(std::make_tuple(objFile.filePath, std::get<1>(found.value())));
            } else {
                InternalError("Input file not in inputFileOrder.");
            }
        }
    }
}

void ToolChain::AppendObjectsFromInput(std::vector<std::tuple<std::string, uint64_t>>& inputOrderTuples)
{
    std::vector<std::tuple<std::string, uint64_t>> itemTuples;
    itemTuples.insert(itemTuples.begin(), driverOptions.inputFileOrder.begin(), driverOptions.inputFileOrder.end());
    for (const std::string& obj : driverOptions.inputObjs) {
        auto found = GetElementFromOrderVector(itemTuples, obj);
        if (found.has_value()) {
            inputOrderTuples.emplace_back(found.value());
        } else {
            InternalError("Input object file not in inputFileOrder.");
        }
    }
}

void ToolChain::AppendLibrariesFromInput(std::vector<std::tuple<std::string, uint64_t>>& inputOrderTuples)
{
    for (const std::tuple<std::string, uint64_t>& library : driverOptions.inputLibraryOrder) {
        inputOrderTuples.emplace_back(std::make_tuple("-l" + std::get<0>(library), std::get<1>(library)));
    }
}

void ToolChain::AppendLinkOptionFromInput(std::vector<std::tuple<std::string, uint64_t>>& inputOrderTuples)
{
    for (const std::tuple<std::string, uint64_t>& optionTuple : driverOptions.inputLinkOptionOrder) {
        std::string option = std::get<0>(optionTuple);
        if (!option.empty()) {
            inputOrderTuples.emplace_back(std::make_tuple(option, std::get<1>(optionTuple)));
        }
    }
}

void ToolChain::AppendLinkOptionsFromInput(std::vector<std::tuple<std::string, uint64_t>>& inputOrderTuples)
{
    for (const std::tuple<std::string, uint64_t>& optionTuple : driverOptions.inputLinkOptionsOrder) {
        std::string option = std::get<0>(optionTuple);
        auto splitArgs = Utils::SplitString(option, " ");
        for (const auto& arg : splitArgs) {
            if (!arg.empty()) {
                inputOrderTuples.emplace_back(std::make_tuple(arg, std::get<1>(optionTuple)));
            }
        }
    }
}

void ToolChain::SortInputlibraryFileAndAppend(Tool& tool, const std::vector<TempFileInfo>& objFiles)
{
    std::vector<std::tuple<std::string, uint64_t>> inputOrderTuples;

    // Object file compiled from the cangjie file
    AppendObjectsFromCompiled(tool, objFiles, inputOrderTuples);

    AppendObjectsFromInput(inputOrderTuples);
    AppendLibrariesFromInput(inputOrderTuples);
    AppendLinkOptionFromInput(inputOrderTuples);
    AppendLinkOptionsFromInput(inputOrderTuples);
    // need to maintain front-to-back relative position
    std::stable_sort(inputOrderTuples.begin(), inputOrderTuples.end(),
        [](auto& a, auto& b) { return std::get<1>(a) < std::get<1>(b); });

    for (auto& tuple : inputOrderTuples) {
        tool.AppendArg(std::get<0>(tuple));
    }
}

TempFileInfo ToolChain::GetOutputFileInfo(const std::vector<TempFileInfo>& objFiles) const
{
    TempFileKind fileKind;
    if (driverOptions.compileMacroPackage) {
        fileKind = TempFileKind::O_MACRO;
    } else {
        fileKind = driverOptions.outputMode == GlobalOptions::OutputMode::SHARED_LIB ?
            TempFileKind::O_DYLIB : TempFileKind::O_EXE;
    }
    return TempFileManager::Instance().CreateNewFileInfo(objFiles[0], fileKind);
}

std::string ToolChain::GetArchFolderName(const Triple::ArchType& arch) const
{
    switch (arch) {
        case Triple::ArchType::X86_64:
        case Triple::ArchType::AARCH64:
            return "lib64";
        case Triple::ArchType::UNKNOWN:
        default:
            return "";
    }
}

std::vector<std::string> ToolChain::ComputeLibPaths() const
{
    return {};
}

std::vector<std::string> ToolChain::ComputeBinPaths() const
{
    std::vector<std::string> result;
    result.emplace_back(FileUtil::JoinPath(driverOptions.sysroot, "bin"));
    result.emplace_back(FileUtil::JoinPath(driverOptions.sysroot, "usr/bin"));
    return result;
}

void ToolChain::GenerateRuntimePath(Tool& tool)
{
    // driver option has already warned the user, so here just ignore it
    // effective for useRuntimeRpath and sanitizerEnableRpath
    if (driverOptions.IsCrossCompiling()) {
        return;
    }

    if (driverOptions.useRuntimeRpath) {
        auto cangjieRuntimeLibPath = FileUtil::JoinPath(
            FileUtil::JoinPath(driver.cangjieHome, "runtime/lib"), driverOptions.GetCangjieLibTargetPathName());
        tool.AppendArg("-rpath", cangjieRuntimeLibPath);
    } else if (driverOptions.sanitizerEnableRpath) {
        auto sanitizerRuntimePath =
            FileUtil::JoinPath(FileUtil::JoinPath(FileUtil::JoinPath(
                driver.cangjieHome, "runtime/lib"), driverOptions.GetCangjieLibTargetPathName()),
                driverOptions.SanitizerTypeToShortString());
        // --sanitize-set-rpath needs rpath, not runpath
        tool.AppendArg("--disable-new-dtags");
        tool.AppendArg("-rpath", sanitizerRuntimePath);
    }
}

std::string ToolChain::FindCangjieLLVMToolPath(const std::string& toolName) const
{
    std::string toolPath = FindToolPath(
        toolName, std::vector<std::string>{FileUtil::JoinPath(driver.cangjieHome, "third_party/llvm/bin")});
    if (toolPath.empty()) {
        Errorf("not found `%s` in search paths. Your Cangjie installation might be broken.\n", toolName.c_str());
    }
    return toolPath;
}

std::string ToolChain::FindUserToolPath(const std::string& toolName) const
{
    // ComputeBinPaths makes some guesses on --sysroot option for protential available search paths. It has lower
    // precedence than toolchain paths so users may always use -B to specifiy which path to search first.
    std::string toolPath = FindToolPath(toolName, driverOptions.toolChainPaths,
        driverOptions.customizedSysroot ? ComputeBinPaths() : std::vector<std::string>{},
        driverOptions.environment.paths);
    if (toolPath.empty()) {
        Errorf("not found `%s` in search paths. You may add search path by `-B` option.\n", toolName.c_str());
    }
    return toolPath;
}
