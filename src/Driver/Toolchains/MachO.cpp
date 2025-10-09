// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the MachO ToolChain class.
 */

#include "Toolchains/MachO.h"

#include <tuple>

#include "cangjie/Driver/TempFileManager.h"
#include "cangjie/Driver/Toolchains/GCCPathScanner.h"
#include "cangjie/Driver/Toolchains/ToolChain.h"
#include "cangjie/Driver/Utils.h"

namespace {
const std::string LINK_PREFIX = "-l";
const std::string STATIC_LIB_EXTEBSION = ".a";

const std::map<std::string, std::string> LLVM_LTO_CSTD_FFI_OPTION_MAP = {
#define CJNATIVE_LTO_CSTD_FFI_OPTIONS(STATILIB, FFILIB) {STATILIB, FFILIB},
#include "Toolchains/BackendOptions.inc"
#undef CJNATIVE_LTO_CSTD_FFI_OPTIONS
};
}; // namespace

using namespace Cangjie;
using namespace Cangjie::Triple;

bool MachO::PrepareDependencyPath()
{
    if ((arPath = FindUserToolPath(g_toolList.at(ToolID::AR).name)).empty()) {
        return false;
    }
    if ((ldPath = FindCangjieLLVMToolPath(g_toolList.at(ToolID::LD64_LLD).name)).empty()) {
        return false;
    }
    if ((dsymutilPath = FindUserToolPath(g_toolList.at(ToolID::DSYMUTIL).name)).empty()) {
        return false;
    }
    if ((stripPath = FindUserToolPath(g_toolList.at(ToolID::STRIP).name)).empty()) {
        return false;
    }
    return true;
}

void MachO::GenerateArchiveTool(const std::vector<TempFileInfo>& objFiles)
{
    auto tool = std::make_unique<Tool>(arPath, ToolType::BACKEND, driverOptions.environment.allVariables);
    // The c for no warn if the library had to be created
    // The r for replacing existing or insert new file(s) into the archive
    tool->AppendArg("cr");

    // When we reach here, we must be at the final phase of the compilation,
    // which means that it is the final output.
    TempFileInfo fileInfo = TempFileManager::Instance().CreateNewFileInfo(objFiles[0], TempFileKind::O_STATICLIB);
    std::string outputFile = fileInfo.filePath;

    // If archive exists, ar attempts to insert given obj files into the archive.
    // We always try to remove the archive before creating a new one.
    (void)FileUtil::Remove(outputFile.c_str());
    tool->AppendArg(outputFile);

    // Note: We do not use tool->inputs here since it is always placed right after executable
    // the first arg of ar should be the option not input
    for (const auto& objFile : objFiles) {
        tool->AppendArg(objFile.filePath);
    }
    backendCmds.emplace_back(MakeSingleToolBatch({std::move(tool)}));
}

// Have no idea how to use linker script on MacOS currently.
void MachO::HandleLLVMLinkOptions(const std::vector<TempFileInfo>& objFiles, Tool& tool)
{
    // The order of linkage of object files & libraries is as follows:
    //
    // 1. object file(s) we are compiling, which is the current package
    // 2. object file(s) specified in the cjc command
    // 3. libraries (-l) specified in the cjc command
    // 4. built-in static library dependencies
    // 5. built-in dynamic library direct dependencies
    // 6. built-in dynamic library indirect dependencies
    // 7. system library dependencies
    //
    // Since object files are main inputs, and they are always linked into the final product,
    // they are linked first.
    //
    // Libraries specified by -l in the cjc command could be cangjie packages
    // with built-in library dependencies. They must be linked before built-in libraries are linked
    // or built-in libraries it depends may be discarded by the linker.
    //
    // For built-in libraries, static libraries have to be linked before dynamic libraries. To be specific,
    // core.a must be linked before runtime.so get linked. Runtime requires some symbols defined in
    // core package, therefore these symbols need to be linked firstly.
    //
    // System libraries are linked at the end. First, user libraries may contain symbols whose names are
    // the same as some hidden symbols in system libraries. Linking system libraries too early may cause
    // symbol overriding on user symbols. Second, built-in static libraries may use symbols of system
    // libraries, thus they need to be linked after built-in libraries.
    //

    auto cangjieLibPath =
        FileUtil::JoinPath(FileUtil::JoinPath(driver.cangjieHome, "lib"), driverOptions.GetCangjieLibTargetPathName());
    // 1. The -L library path
    HandleLibrarySearchPaths(tool, cangjieLibPath);

    // 2. cjstart.o
    tool.AppendArg(FileUtil::JoinPath(cangjieLibPath, "section.o"));
    tool.AppendArg(FileUtil::JoinPath(cangjieLibPath, "cjstart.o"));

    // 3. Frontend output or input files
    // 3.1 Frontend output files (.o)
    // 3.2 Pass to the linker in the order of input files (.o, .a, -l)
    SortInputlibraryFileAndAppend(tool, objFiles);
    // Note that the pgo options must be inserted after those from SortInputlibraryFileAndAppend
    tool.AppendArgIf(driverOptions.enablePgoInstrGen, "-u", "___llvm_profile_runtime");
    tool.AppendArgIf(driverOptions.enablePgoInstrGen || driverOptions.enableCoverage,
        FileUtil::JoinPath(cangjieLibPath, GetClangRTProfileLibraryName()));
    // 4. The built-in library dependencies
    GenerateLinkOptionsOfBuiltinLibs(tool);
    // 5. System library dependencies required by the backend
    GenerateLinkOptions(tool);
}

void MachO::HandleLibrarySearchPaths(Tool& tool, const std::string& cangjieLibPath)
{
    // Append -L (those specified in command) as search paths
    tool.AppendArg(PrependToPaths("-L", driverOptions.librarySearchPaths));
    // path to built-in libraries is placed between -L search paths and LIBRARY_PATH so that
    // 1) user can explicitly specify a library path which contains libcangjie-<sth> to force Linker
    //    link against such libraries.
    // 2) Linker will not be confused if user's LIBRARY_PATH is polluted, for example, LIBRARY_PATH
    //    contains the path to the library folder of a different version of cjc.
    auto cangjieRuntimeLibPath = FileUtil::JoinPath(
        FileUtil::JoinPath(driver.cangjieHome, "runtime/lib"), driverOptions.GetCangjieLibTargetPathName());
    // Different from Gnu toolchain, we pass runtime library path first here. ld64 does not support -l:<filename>
    // passing style, thus we use -l<libname> for dynamic linkage and pass full path for static libraries (.a files).
    // Passing runtime library path first ensures -l<libname> always find dynamic libraries before the static one.
    tool.AppendArg("-L" + cangjieRuntimeLibPath);
    tool.AppendArg("-L" + cangjieLibPath);
    // Append LIBRARY_PATH as search paths
    tool.AppendArg(PrependToPaths("-L", GetLibraryPaths()));
    if (driverOptions.IsCrossCompiling()) {
        tool.AppendArg("-L");
        tool.AppendArg(cangjieRuntimeLibPath);
    }
}

std::string MachO::GetTargetArchString() const
{
    switch (driverOptions.target.arch) {
        case ArchType::AARCH64:
            return "arm64";
        case ArchType::X86_64:
            return "x86_64";
        case ArchType::UNKNOWN:
        default:
            return "";
    }
}

void MachO::InitializeLibraryPaths()
{
    // The paths from --toolchain/-B can be used to search system libs, such as Scrt1.o, crti.o, etc.
    for (auto& libPath : driverOptions.toolChainPaths) {
        AddCRuntimeLibraryPath(libPath);
    }
    AddCRuntimeLibraryPaths();
    AddSystemLibraryPaths();
}

void MachO::AddCRuntimeLibraryPaths()
{
    auto archFolderName = GetArchFolderName(driverOptions.target.arch);
    AddCRuntimeLibraryPath(FileUtil::JoinPath(driverOptions.sysroot, archFolderName));
    auto usrPath = FileUtil::JoinPath(driverOptions.sysroot, "usr");
    AddCRuntimeLibraryPath(FileUtil::JoinPath(usrPath, archFolderName));
    auto usrLibPath = FileUtil::JoinPath(usrPath, "lib");
    AddCRuntimeLibraryPath(usrLibPath);
    auto usrDirectories = FileUtil::GetDirectories(usrLibPath);
    for (const auto& dir : usrDirectories) {
        if (Triple::IsPossibleMatchingTripleName(driverOptions.target, dir.name)) {
            AddCRuntimeLibraryPath(dir.path);
        }
    }
}

void MachO::AddSystemLibraryPaths()
{
    AddLibraryPaths(driverOptions.environment.libraryPaths);
}

bool MachO::ProcessGeneration(std::vector<TempFileInfo>& objFiles)
{
    // The '--output-type=staticlib', one more step to go, create an archive file consisting of all generated object files
    if (driverOptions.outputMode == GlobalOptions::OutputMode::STATIC_LIB) {
        GenerateArchiveTool(objFiles);
        return true;
    }

    auto outputFile = GenerateLinking(objFiles);
    if (driverOptions.enableCompileDebug) {
        GenerateDebugSymbolFile(outputFile);
    }
    if (driverOptions.stripSymbolTable) {
        GenerateStripSymbolFile(outputFile);
    }

    return true;
}

TempFileInfo MachO::GenerateLinking(const std::vector<TempFileInfo>& objFiles)
{
    std::optional<std::string> darwinSDKVersion = GetDarwinSDKVersion(driverOptions.sysroot);
    if (driverOptions.enableVerbose) {
        Infoln("selected Darwin SDK path: " + driverOptions.sysroot +
            " (SDK Version: " + darwinSDKVersion.value_or("N/A") + ")");
    }
    // If SDK version cannot be detected, default version 12 is used here.
    return GenerateLinkingTool(objFiles, darwinSDKVersion.value_or("12"));
}

void MachO::GenerateDebugSymbolFile(const TempFileInfo& binaryFile)
{
    auto tool = std::make_unique<Tool>(dsymutilPath, ToolType::OTHER, driverOptions.environment.allVariables);
    tool->AppendArg(binaryFile.filePath);
    tool->AppendArg("-o", binaryFile.filePath + ".dSYM");
    backendCmds.emplace_back(MakeSingleToolBatch({std::move(tool)}));

    auto codesignTool = std::make_unique<Tool>(g_toolList.at(ToolID::CODESIGN).name,
        ToolType::OTHER, driverOptions.environment.allVariables);
    codesignTool->AppendArg("-s", "-", "-f");
    codesignTool->AppendArg("--entitlements", FileUtil::JoinPath(driver.cangjieHome, "lib/entitlement.plist"));
    codesignTool->AppendArg(binaryFile.filePath);
    backendCmds.emplace_back(MakeSingleToolBatch({std::move(codesignTool)}));
}


void MachO::GenerateStripSymbolFile(const TempFileInfo& binaryFile)
{
    auto tool = std::make_unique<Tool>(stripPath, ToolType::BACKEND, driverOptions.environment.allVariables);
    switch (driverOptions.outputMode) {
        case GlobalOptions::OutputMode::EXECUTABLE:
            tool->AppendArg("-u", "-r");
            break;
        case GlobalOptions::OutputMode::SHARED_LIB:
            tool->AppendArg("-x");
            break;
        case GlobalOptions::OutputMode::STATIC_LIB:
        default:
            break;
    }
    std::vector<TempFileInfo> binaryFiles;
    binaryFiles.emplace_back(binaryFile);
    auto newBinaryFile = GetOutputFileInfo(binaryFiles);
    tool->AppendArg(binaryFile.filePath);
    tool->AppendArg("-o");
    tool->AppendArg(newBinaryFile.filePath);
    backendCmds.emplace_back(MakeSingleToolBatch({std::move(tool)}));
}

void MachO::GenerateLinkOptionsOfBuiltinLibsForStaticLink(Tool& tool) const
{
    std::set<std::string> dynamicLibraries;
    std::set<std::string> staticLibraries;
    std::set<std::string> ltoBuiltInDependencies;

    const std::function<void(std::string)> appendStaticLibsToTool =
        [this, &dynamicLibraries, &staticLibraries, &ltoBuiltInDependencies](const std::string& cjoFileName) {
            auto staticLib = FileUtil::ConvertFilenameToLibCangjieFormat(cjoFileName, STATIC_LIB_EXTEBSION);
            if (ALWAYS_DYNAMIC_LINK_STD_LIBRARIES.find(staticLib) !=
                ALWAYS_DYNAMIC_LINK_STD_LIBRARIES.end()) {
                dynamicLibraries.emplace(LINK_PREFIX +
                    FileUtil::ConvertFilenameToLibCangjieBaseFormat(cjoFileName));
                return;
            }
            if (driverOptions.IsLTOEnabled()) {
                auto found = LLVM_LTO_CSTD_FFI_OPTION_MAP.find(staticLib);
                if (found != LLVM_LTO_CSTD_FFI_OPTION_MAP.end()) {
                    staticLibraries.emplace(LINK_PREFIX + found->second);
                }
                ltoBuiltInDependencies.emplace(FileUtil::ConvertFilenameToLtoLibCangjieFormat(cjoFileName));
            } else {
                // In the case of static linkage, we pass full path of static library to the linker, otherwise
                // we use -l<libname> passing style.
                auto cangjieLibPath = FileUtil::JoinPath(FileUtil::JoinPath(driver.cangjieHome, "lib"),
                    driverOptions.GetCangjieLibTargetPathName());
                // Search sanitizer path
                if (driverOptions.sanitizerType != GlobalOptions::SanitizerType::NONE) {
                    cangjieLibPath = FileUtil::JoinPath(cangjieLibPath, driverOptions.SanitizerTypeToShortString());
                }
                std::string libLinkOpt = driverOptions.linkStaticStd.value_or(true)
                    ? FileUtil::JoinPath(cangjieLibPath, staticLib)
                    : LINK_PREFIX + FileUtil::ConvertFilenameToLibCangjieBaseFormat(cjoFileName);
                staticLibraries.emplace(libLinkOpt);
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
    for (const auto& other : staticLibraries) {
        tool.AppendArg(other);
    }
    for (const auto& lib : dynamicLibraries) {
        tool.AppendArg(lib);
    }
}

void MachO::GenerateLinkOptionsOfBuiltinLibsForDyLink(Tool& tool) const
{
    const std::function<void(std::string)> appendDyLibsToTool = [&tool](const std::string& cjoFileName) {
        tool.AppendArg(LINK_PREFIX + FileUtil::ConvertFilenameToLibCangjieBaseFormat(cjoFileName));
    };

    ForEachBuiltinDependencies(driverOptions.directBuiltinDependencies, appendDyLibsToTool);
    // Link indirect dependent dynamic libraries surrounded by `--as-needed` and `--no-as-needed`.
    // For the current implementation of generic types of cangjie, some symbols may be shared across
    // libraries, which means that an indirect dependency may be a direct dependency. Thus, we must link
    // indirect dependencies here. Indirect dependencies are passed after `--as-needed` options
    // so unnecessary dependencies will be discarded by the linker.
    ForEachBuiltinDependencies(driverOptions.indirectBuiltinDependencies, appendDyLibsToTool);
}
