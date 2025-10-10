// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the GNU ToolChain class.
 */

#include "Toolchains/Gnu.h"

#include <fstream>
#include <tuple>

#include "cangjie/Driver/TempFileManager.h"
#include "cangjie/Driver/Toolchains/GCCPathScanner.h"
#include "cangjie/Driver/Toolchains/ToolChain.h"
#include "cangjie/Driver/Utils.h"
#include "cangjie/Utils/FileUtil.h"

using namespace Cangjie;
using namespace Cangjie::Triple;

namespace {
inline void LinkStaticLibrary(Tool& tool, const std::string& lib)
{
    tool.AppendArg("--whole-archive");
    tool.AppendArg(lib);
    tool.AppendArg("--no-whole-archive");
}
}


std::string Gnu::GenerateGCCLibPath(const std::pair<std::string, std::string>& gccCrtFilePair) const
{
    std::string gccLibPath;
    // Scan some possible directories where gcc may be installed, possible directories vary in different
    // triple targets. If we cannot find any available gcc lib path, stop proceeding to linking stage.
    // Selected gcc lib path must contain crt files we need for linking.
    std::vector<std::string> forFiles{gccCrtFilePair.first, gccCrtFilePair.second};
    auto gccPathScanner = GCCPathScanner(driverOptions.target, forFiles, GetCRuntimeLibraryPath());
    auto maybeGccPath = gccPathScanner.Scan();
    if (maybeGccPath.has_value()) {
        gccLibPath = maybeGccPath.value().libPath;
    }
    return gccLibPath;
}

std::string Gnu::GetGccLibFile(const std::string& filename, const std::string& gccLibPath) const
{
    auto maybeCrtFile = FileUtil::FindFileByName(filename, GetCRuntimeLibraryPath());
    if (maybeCrtFile.has_value()) {
        return maybeCrtFile.value();
    } else if (!gccLibPath.empty()) {
        return FileUtil::JoinPath(gccLibPath, filename);
    }
    return filename;
}

std::pair<std::string, std::string> Gnu::GetGccCrtFilePair() const
{
    // Mini FAQ about the misc libc/gcc crt files.
    // Some definitions:
    // PIC - position independent code (-fPIC)
    // PIE - position independent executable (-fPIE -pie)
    // crt - C runtime
    // crtbegin.o
    //   GCC uses this to find the start of the constructors.
    // crtbeginS.o
    //   Used in place of crtbegin.o when generating shared objects/PIEs.
    // crtbeginT.o
    //   Used in place of crtbegin.o when generating static executables.
    // crtend.o
    //   GCC uses this to find the start of the destructors.
    // crtendS.o
    //   Used in place of crtend.o when generating shared objects/PIEs.
    switch (driverOptions.outputMode) {
        case GlobalOptions::OutputMode::EXECUTABLE:
            return gccExecCrtFilePair;
        case GlobalOptions::OutputMode::SHARED_LIB:
            return gccSharedCrtFilePair;
        case GlobalOptions::OutputMode::STATIC_LIB:
        default:
            break;
    }
    return {};
}

bool Gnu::PrepareDependencyPath()
{
    auto toolPrefix = driverOptions.IsCrossCompiling() ? driverOptions.target.GetEffectiveTripleString() + "-" : "";
    if ((arPath = FindUserToolPath(toolPrefix + g_toolList.at(ToolID::AR).name)).empty()) {
        return false;
    }

    if ((objcopyPath = FindCangjieLLVMToolPath(g_toolList.at(ToolID::LLVM_OBJCOPY).name)).empty()) {
        return false;
    }

    if (driverOptions.IsLTOEnabled() || driverOptions.EnableHwAsan()) {
        if ((ldPath = FindCangjieLLVMToolPath(g_toolList.at(ToolID::LLD).name)).empty()) {
            return false;
        }
    } else {
        if ((ldPath = FindUserToolPath(toolPrefix + g_toolList.at(ToolID::LD).name)).empty()) {
            return false;
        }
    }

    return true;
}

void Gnu::GenerateArchiveTool(const std::vector<TempFileInfo>& objFiles)
{
    auto tool = std::make_unique<Tool>(arPath, ToolType::BACKEND, driverOptions.environment.allVariables);
    // The c for no warn if the library had to be created
    // The r for replacing existing or insert new file(s) into the archive
    // The D for deterministic mode
    tool->AppendArg("crD");

    // When we reach here, we must be at the final phase of the compilation,
    // which means that is the final output.
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

std::optional<std::string> Gnu::SearchClangLibrary(const std::string libName, const std::string libSuffix)
{
    // Clang lib format:
    // libclang_rt.<module name>[-<arch>].<suffix>
    std::vector<std::string> searchPaths = {};
    auto libraryPaths = GetLibraryPaths();
    auto runtimeLibPath = GetCRuntimeLibraryPath();
    (void)searchPaths.insert(searchPaths.end(),
        driverOptions.librarySearchPaths.begin(), driverOptions.librarySearchPaths.end());
    (void)searchPaths.insert(searchPaths.end(), libraryPaths.begin(), libraryPaths.end());
    (void)searchPaths.insert(searchPaths.end(), runtimeLibPath.begin(), runtimeLibPath.end());

    std::string prefix = "libclang_rt.";
    auto clangLib = FileUtil::FindFileByName(prefix + libName + libSuffix, searchPaths);
    if (clangLib.has_value()) {
        return clangLib;
    }

    // Clang asan lib with arch
    return FileUtil::FindFileByName(
        prefix + libName + "-" + driverOptions.target.ArchToString() + libSuffix, searchPaths);
}

void Gnu::HandleSanitizerDependencies(Tool& tool)
{
    for (const auto& arg : {"-lpthread", "-lrt", "-lm", "-ldl", "-lresolv", "-lgcc_s"}) {
        tool.AppendArg(arg);
    }
}

void Gnu::HandleAsanDependencies(Tool& tool, const std::string& cangjieLibPath, const std::string& gccLibPath)
{
    if (driverOptions.outputMode != Cangjie::GlobalOptions::OutputMode::EXECUTABLE) {
        return;
    }

    // General args
    HandleSanitizerDependencies(tool);
    tool.AppendArg("--export-dynamic");

    // Static library
    auto asanLib = FileUtil::FindFileByName("libclang_rt-asan.a", {cangjieLibPath});
    if (!asanLib.has_value()) {
        asanLib = SearchClangLibrary("asan", ".a");
    }

    if (asanLib.has_value()) {
        LinkStaticLibrary(tool, asanLib.value());
        return;
    }

    // Dynamic library
    // Clang
    asanLib = SearchClangLibrary("asan-preinit", ".a");
    if (asanLib.has_value()) {
        // preinit static library
        LinkStaticLibrary(tool, asanLib.value());
        tool.AppendArg("-lclang_rt.asan");
        return;
    }

    // The gnu lib
    tool.AppendArg(FileUtil::JoinPath(gccLibPath, "libasan_preinit.o"));
    tool.AppendArg("-lasan");
}

void Gnu::HandleHwasanDependencies(Tool& tool, const std::string& cangjieLibPath)
{
    if (driverOptions.outputMode != Cangjie::GlobalOptions::OutputMode::EXECUTABLE) {
        return;
    }

    // General args
    HandleSanitizerDependencies(tool);
    tool.AppendArg("--export-dynamic");

    // Static library
    auto asanLib = FileUtil::FindFileByName("libclang_rt-hwasan.a", {cangjieLibPath});
    if (!asanLib.has_value()) {
        asanLib = SearchClangLibrary("hwasan", ".a");
    }

    if (asanLib.has_value()) {
        LinkStaticLibrary(tool, asanLib.value());
        return;
    }

    // Dynamic library
    asanLib = SearchClangLibrary("hwasan-preinit", ".a");
    LinkStaticLibrary(tool, asanLib.value_or("libclang_rt.hwasan-preinit.a"));
    tool.AppendArg("-lclang_rt.hwasan");
}

void Gnu::HandleSanitizer(Tool& tool, const std::string& cangjieLibPath, const std::string& gccLibPath)
{
    if (!driverOptions.EnableSanitizer()) {
        return;
    }

    auto sanitizerPath = FileUtil::JoinPath(cangjieLibPath, driverOptions.SanitizerTypeToShortString());
    if (driverOptions.EnableAsan()) {
        HandleAsanDependencies(tool, sanitizerPath, gccLibPath);
    } else if (driverOptions.EnableTsan()) {
        if (driverOptions.outputMode == Cangjie::GlobalOptions::OutputMode::EXECUTABLE) {
            tool.AppendArg("--export-dynamic");
            LinkStaticLibrary(tool, FileUtil::JoinPath(sanitizerPath, "libclang_rt-tsan.a"));
            HandleSanitizerDependencies(tool);
        }
    } else if (driverOptions.EnableHwAsan()) {
        HandleHwasanDependencies(tool, sanitizerPath);
    }

    // The eh frame header is needed for all sanitizer
    tool.AppendArg("--eh-frame-hdr");
}

void Gnu::HandleLLVMLinkOptions(
    const std::vector<TempFileInfo>& objFiles, const std::string& gccLibPath, Tool& tool, const std::string& cjldScript)
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
    // Where to search for gcc & backend libraries
    if (!gccLibPath.empty()) {
        tool.AppendArg("-L" + gccLibPath);
    }

    if (driverOptions.discardEhFrame && driverOptions.target.os != Triple::OSType::WINDOWS) {
        tool.AppendArg("-T");
        tool.AppendArg(FileUtil::JoinPath(cangjieLibPath, "discard_eh_frame.lds"));
    }
    // 2. The cjld.lds and cjstart.o
    if (driverOptions.target.os == Triple::OSType::WINDOWS) {
        tool.AppendArg(FileUtil::JoinPath(cangjieLibPath, "section.o"));
    } else {
        tool.AppendArg("-T");
        tool.AppendArg(FileUtil::JoinPath(cangjieLibPath, cjldScript));
    }
    tool.AppendArg(FileUtil::JoinPath(cangjieLibPath, "cjstart.o"));

    // 3. Frontend output or Input files
    // 3.1 Frontend output files (.o)
    // 3.2 Pass to the linker in the order of input files (.o, .a, -l)
    SortInputlibraryFileAndAppend(tool, objFiles);
    // Note that the pgo options must be inserted after those from SortInputlibraryFileAndAppend
    tool.AppendArgIf(driverOptions.enablePgoInstrGen, "-u", "__llvm_profile_runtime");
    tool.AppendArgIf(driverOptions.enablePgoInstrGen || driverOptions.enableCoverage,
        FileUtil::JoinPath(cangjieLibPath, GetClangRTProfileLibraryName()));
    HandleSanitizer(tool, cangjieLibPath, gccLibPath);
    // 4. built-in library dependencies
    GenerateLinkOptionsOfBuiltinLibs(tool);
    // 5. system library dependencies required by the backend
    GenerateLinkOptions(tool);
}

void Gnu::HandleLibrarySearchPaths(Tool& tool, const std::string& cangjieLibPath)
{
    // Append -L (those specified in command) as search paths
    tool.AppendArg(PrependToPaths("-L", driverOptions.librarySearchPaths));
    // Path to built-in libraries is placed between -L search paths and LIBRARY_PATH so that
    // 1) user can explicitly specify a library path which contains libcangjie-<sth> to force Linker
    //    link against such libraries.
    // 2) Linker will not be confused if user's LIBRARY_PATH is polluted, for example, LIBRARY_PATH
    //    contains the path to the library folder of a different version of cjc.
    tool.AppendArgIf(driverOptions.EnableSanitizer(),
        "-L" + FileUtil::JoinPath(cangjieLibPath, driverOptions.SanitizerTypeToShortString()));
    tool.AppendArg("-L" + cangjieLibPath);
    auto cangjieRuntimeLibPath = FileUtil::JoinPath(
        FileUtil::JoinPath(driver.cangjieHome, "runtime/lib"), driverOptions.GetCangjieLibTargetPathName());
    tool.AppendArgIf(driverOptions.EnableSanitizer(),
        "-L" + FileUtil::JoinPath(cangjieRuntimeLibPath, driverOptions.SanitizerTypeToShortString()));
    tool.AppendArg("-L" + cangjieRuntimeLibPath);
    // Append LIBRARY_PATH as search paths
    tool.AppendArg(PrependToPaths("-L", GetLibraryPaths()));
    if (driverOptions.IsCrossCompiling() && !driverOptions.target.IsMinGW()) {
        tool.AppendArg("-rpath-link");
        tool.AppendArg(cangjieRuntimeLibPath);
    }
}

std::string Gnu::GetEmulation() const
{
    switch (driverOptions.target.arch) {
        case Triple::ArchType::ARM32:
            return "armelf_linux_eabi";
        case Triple::ArchType::X86_64:
            if (driverOptions.target.os == Triple::OSType::LINUX) {
                return "elf_x86_64";
            } else if (driverOptions.target.os == Triple::OSType::WINDOWS) {
                return "i386pep";
            }
            break;
        case Triple::ArchType::AARCH64:
            if (driverOptions.target.os == Triple::OSType::LINUX) {
                return "aarch64linux";
            }
            break;
        case Triple::ArchType::UNKNOWN:
        default:
            break;
    }
    return "";
}

void Gnu::InitializeLibraryPaths()
{
    // The paths from --toolchain/-B can be used to search system libs, such as Scrt1.o, crti.o, etc.
    for (auto& libPath : driverOptions.toolChainPaths) {
        AddCRuntimeLibraryPath(libPath);
    }
    AddCRuntimeLibraryPaths();
    AddSystemLibraryPaths();
}

void Gnu::AddCRuntimeLibraryPaths()
{
    // The order that library paths are added to the list does matter. Libraries are searched in order
    // and the first appearance of a matching library is chosen.
    // The rule how default library paths are added is defined as follows:
    // 1. one of lib64 and lib32 folder that matches the target arch.
    // 2. root folder, and then '/usr' folder.
    // For example, in the case of x86_64, the search list may be ["/lib64", "/usr/lib64", "/lib", "/usr/lib"]
    // Search for system object files.
    auto archFolderName = GetArchFolderName(driverOptions.target.arch);
    AddCRuntimeLibraryPath(FileUtil::JoinPath(driverOptions.sysroot, archFolderName));
    auto usrPath = FileUtil::JoinPath(driverOptions.sysroot, "usr");
    AddCRuntimeLibraryPath(FileUtil::JoinPath(usrPath, archFolderName));
    auto libPath = FileUtil::JoinPath(driverOptions.sysroot, "lib");
    AddCRuntimeLibraryPath(libPath);
    auto usrLibPath = FileUtil::JoinPath(usrPath, "lib");
    AddCRuntimeLibraryPath(usrLibPath);
    // Some libraries can be found in the compatible toolchain folder
    auto directories = FileUtil::GetDirectories(libPath);
    for (const auto& dir : directories) {
        if (Triple::IsPossibleMatchingTripleName(driverOptions.target, dir.name)) {
            AddCRuntimeLibraryPath(dir.path);
        }
    }
    auto usrDirectories = FileUtil::GetDirectories(usrLibPath);
    for (const auto& dir : usrDirectories) {
        if (Triple::IsPossibleMatchingTripleName(driverOptions.target, dir.name)) {
            AddCRuntimeLibraryPath(dir.path);
        }
    }
}

void Gnu::AddSystemLibraryPaths()
{
    AddLibraryPaths(driverOptions.environment.libraryPaths);
}

bool Gnu::ProcessGeneration(std::vector<TempFileInfo>& objFiles)
{
    size_t codegenOutputBCNum = 0;
    for (auto objName : objFiles) {
        codegenOutputBCNum += objName.isForeignInput ? 0 : 1;
    }
    if (codegenOutputBCNum == 1) {
        // The '--output-type=staticlib', one more step to go, create an archive
        // file consisting of all generated object files
        if (driverOptions.outputMode == GlobalOptions::OutputMode::STATIC_LIB) {
            GenerateArchiveTool(objFiles);
            return true;
        } else {
            return GenerateLinking(objFiles);
        }
    }
    auto tool = std::make_unique<Tool>(ldPath, ToolType::BACKEND, driverOptions.environment.allVariables);
    // Recover the 'outputFile' from 'path/0-xx.o' to 'path/xx.o'
    std::string outputFile = objFiles[0].filePath;
    std::string dirPath = FileUtil::GetDirPath(outputFile);
    std::string fileName = FileUtil::GetFileName(outputFile).substr(2); // 2 is the length of the prefix "0-"
    outputFile = FileUtil::JoinPath(dirPath, fileName);

    tool->AppendArg("-o", outputFile);
    tool->AppendArg("-r");
    std::vector<TempFileInfo> processedObjFiles{};
    for (auto objName : objFiles) {
        if (objName.isForeignInput) {
            processedObjFiles.emplace_back(objName);
        } else {
            tool->AppendArg(objName.filePath);
        }
    }
    backendCmds.emplace_back(MakeSingleToolBatch({std::move(tool)}));

    auto outputDir = FileUtil::GetAbsPath(FileUtil::GetDirPath(outputFile));
    CJC_ASSERT(outputDir.has_value());
    auto name = FileUtil::JoinPath(outputDir.value(), FileUtil::GetFileNameWithoutExtension(fileName) + ".__symbols");
    std::ofstream file(name);
    CJC_ASSERT(file.is_open());
    for (const auto& str : driverOptions.symbolsNeedLocalized) {
        file << str << "\n";
    }
    file.close();

    if (!driverOptions.symbolsNeedLocalized.empty()) {
        auto changeSymVis = std::make_unique<Tool>(
            objcopyPath, ToolType::BACKEND, driverOptions.environment.allVariables);
        changeSymVis->AppendArg(outputFile);
        changeSymVis->AppendArg("--localize-symbols=" + name);
        backendCmds.emplace_back(MakeSingleToolBatch({std::move(changeSymVis)}));
    }

    TempFileInfo fileInfo = {FileUtil::GetFileNameWithoutExtension(outputFile), outputFile, outputFile, true, false};
    processedObjFiles.insert(processedObjFiles.begin(), fileInfo);
    objFiles = std::move(processedObjFiles);

    // If aggressiveParallelCompile is enabled, we combined 0-xx.o, 1-xx.o, etc. to xx.o (aka 'outputFile') by using
    // 'ld', we need to copy xx.o to cache.
    if (driverOptions.aggressiveParallelCompile.value_or(1) > 1) {
        std::string destFile =
            driverOptions.GetHashedObjFileName(FileUtil::GetFileNameWithoutExtension(outputFile)) + ".o";
        auto toolOfCacheCopy =
            std::make_unique<Tool>("CacheCopy", ToolType::INTERNAL_IMPLEMENTED, driverOptions.environment.allVariables);
        toolOfCacheCopy->AppendArg(outputFile);
        toolOfCacheCopy->AppendArg(destFile);
        backendCmds.emplace_back(MakeSingleToolBatch({std::move(toolOfCacheCopy)}));
    }

    // The '--output-type=staticlib', one more step to go, create an archive file consisting of all generated object files
    if (driverOptions.outputMode == GlobalOptions::OutputMode::STATIC_LIB) {
        GenerateArchiveTool(objFiles);
        return true;
    } else {
        return GenerateLinking(objFiles);
    }
}

bool Gnu::GenerateLinking(const std::vector<TempFileInfo>& objFiles)
{
    // Different linking mode requires different gcc crt files
    auto gccCrtFilePair = GetGccCrtFilePair();
    std::string gccLibPath = GenerateGCCLibPath(gccCrtFilePair);
    if (driverOptions.enableVerbose) {
        Infoln("selected gcc lib path: " + gccLibPath);
    }
    GenerateLinkingTool(objFiles, gccLibPath, gccCrtFilePair);
    return true;
}
