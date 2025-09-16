// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the cjnative backend class.
 */

#include "cangjie/Driver/Backend/CJNATIVEBackend.h"

#include "Job.h"
#include "cangjie/Driver/TempFileManager.h"
#include "cangjie/Driver/ToolOptions.h"
#include "Toolchains/CJNATIVE/Linux_CJNATIVE.h"
#include "Toolchains/CJNATIVE/Darwin_CJNATIVE.h"
#include "Toolchains/CJNATIVE/Linux_CJNATIVE.h"
#include "Toolchains/CJNATIVE/MinGW_CJNATIVE.h"
#include "Toolchains/CJNATIVE/Ohos_CJNATIVE.h"

using namespace Cangjie;
using namespace Cangjie::Triple;

bool CJNATIVEBackend::GenerateToolChain()
{
    std::string errFieldStr;
    switch (driverOptions.target.os) {
        case OSType::LINUX:
            if (driverOptions.target.env == Environment::OHOS) {
                TC = std::make_unique<Ohos_CJNATIVE>(driver, driverOptions, backendCmds);
                return true;
            }
            if (driverOptions.target.env == Environment::GNU ||
                driverOptions.target.env == Environment::NOT_AVAILABLE) {
                TC = std::make_unique<Linux_CJNATIVE>(driver, driverOptions, backendCmds);
                return true;
            }
            errFieldStr = driverOptions.target.EnvironmentToString();
            Errorf("Unsupported Environment Type: %s\n", errFieldStr.c_str());
            break;
        case OSType::WINDOWS:
            if (driverOptions.target.env == Triple::Environment::GNU) {
                TC = std::make_unique<MinGW_CJNATIVE>(driver, driverOptions, backendCmds);
                return true;
            }
            errFieldStr = driverOptions.target.EnvironmentToString();
            Errorf("Unsupported Environment Type: %s\n", errFieldStr.c_str());
            break;
        case OSType::DARWIN:
            TC = std::make_unique<Darwin_CJNATIVE>(driver, driverOptions, backendCmds);
            return true;
        case OSType::UNKNOWN:
        default:
            errFieldStr = driverOptions.target.OSToString();
            Errorf("Unsupported OS Type: %s\n", errFieldStr.c_str());
            break;
    }
    return false;
}

bool CJNATIVEBackend::PrepareDependencyPath()
{
    std::vector<std::string> cjnativeBinSearchPaths;
    // search in CANGJIE_HOME if it is available
    if (driverOptions.environment.cangjieHome.has_value()) {
        cjnativeBinSearchPaths.emplace_back(
            FileUtil::JoinPath(driverOptions.environment.cangjieHome.value(), "third_party/llvm/bin"));
    }
    // relative path to the current executing cjc
    cjnativeBinSearchPaths.emplace_back(FileUtil::JoinPath(driver.cangjieHome, "third_party/llvm/bin"));
    // They don't have to be in the same location.
    optPath = FileUtil::FindProgramByName(g_toolList.at(ToolID::OPT).name, cjnativeBinSearchPaths);
    if (optPath.empty()) {
        Errorln("not found `opt` in the Cangjie installation, " + CANGJIE_HOME);
        return false;
    }
    llcPath = FileUtil::FindProgramByName(g_toolList.at(ToolID::LLC).name, cjnativeBinSearchPaths);
    if (llcPath.empty()) {
        Errorln("not found `llc` in the Cangjie installation, " + CANGJIE_HOME);
        return false;
    }
    return TC->PrepareDependencyPath();
}

std::vector<TempFileInfo> CJNATIVEBackend::GetFrontendOutputs()
{
    // Treating input .bc files as input makes passing .bc files possible. The following two commands
    // should be identical, except that the first one requires two extra jobs for backend compilation.
    // 1) cjc main.cj pkg1.bc
    // 2) cjc main.cj pkg1.o
    std::vector<TempFileInfo> bitCodeFiles;
    for (const auto& frontendOutput : driverOptions.frontendOutputFiles) {
        bitCodeFiles.emplace_back(frontendOutput);
    }
    for (const auto& bcFile : driverOptions.bcInputFiles) {
        auto filename = FileUtil::GetFileNameWithoutExtension(bcFile);
        bitCodeFiles.emplace_back(TempFileInfo{filename, bcFile, bcFile, false, true});
    }
    return bitCodeFiles;
}

void CJNATIVEBackend::GenerateCacheCopyTool(const std::vector<TempFileInfo>& files, const std::string& extension)
{
    ToolBatch batch{};
    for (auto& file : files) {
        if (file.isForeignInput) {
            continue;
        }
        std::string srcFile = file.filePath;
        std::string destFile =
            driverOptions.GetHashedObjFileName(FileUtil::GetFileNameWithoutExtension(srcFile)) + extension;
        auto tool =
            std::make_unique<Tool>("CacheCopy", ToolType::INTERNAL_IMPLEMENTED, driverOptions.environment.allVariables);
        tool->AppendArg(srcFile, destFile);
        batch.emplace_back(std::move(tool));
    }
    backendCmds.emplace_back(std::move(batch));
}

/**
 * Generate backend tool and linker.
 */
bool CJNATIVEBackend::ProcessGeneration()
{
    CJC_ASSERT(!driverOptions.frontendOutputFiles.empty() && "non-compiled file found!");
    std::vector<TempFileInfo> bitCodeFiles = GetFrontendOutputs();
    return driverOptions.incrementalCompileNoChange ? ProcessGenerationOfIncrementalNoChangeCompile(bitCodeFiles)
                                                    : ProcessGenerationOfNormalCompile(bitCodeFiles);
}

bool CJNATIVEBackend::ProcessGenerationOfNormalCompile(const std::vector<TempFileInfo>& bitCodeFiles)
{
    if (driverOptions.IsLTOEnabled() && driverOptions.enIncrementalCompilation) {
        GenerateCacheCopyTool(bitCodeFiles, ".bc");
    }

    if (driverOptions.IsLTOEnabled()) {
        std::vector<TempFileInfo> frontendOutputFiles;
        std::vector<TempFileInfo> preprocessedFiles;
        for (auto& fileInfo : bitCodeFiles) {
            if (fileInfo.isFrontendOutput) {
                frontendOutputFiles.emplace_back(fileInfo);
            } else {
                preprocessedFiles.emplace_back(fileInfo);
            }
        }
        auto processedFrontendFiles = GeneratePreprocessTools(frontendOutputFiles);
        preprocessedFiles.insert(preprocessedFiles.end(), processedFrontendFiles.begin(), processedFrontendFiles.end());
        // When compiling a static library in LTO mode, the compilation process stops at the opt stage.
        if (driverOptions.outputMode == GlobalOptions::OutputMode::STATIC_LIB) {
            return true;
        }
        // In LTO mode, compilation is not performed using llc.
        return TC->ProcessGeneration(preprocessedFiles);
    }

    auto preprocessedFiles = GeneratePreprocessTools(bitCodeFiles);
    if (driverOptions.saveTemps) {
        (void)GenerateCompileTool(preprocessedFiles, true);
    }
    auto objFiles = GenerateCompileTool(preprocessedFiles);
    // copy each obj file from temporary directory to cache directory in normal compile case
    ToolBatch batch{};
    for (auto& objFile : objFiles) {
        std::string srcFile = objFile.filePath;
        std::string destFile =
            driverOptions.GetHashedObjFileName(FileUtil::GetFileNameWithoutExtension(srcFile)) + ".o";
        if (driverOptions.aggressiveParallelCompile.value_or(1) > 1) {
            // the format of parallel compile objFile is: number-pkgName.o
            auto isParallelCompileObjFile = [srcFile] {
                auto fileName = FileUtil::GetFileNameWithoutExtension(srcFile);
                auto extension = FileUtil::GetFileExtension(srcFile);
                auto posOfHyphen = fileName.find("-");
                if (posOfHyphen == std::string::npos) {
                    return false;
                }
                auto parallelId = fileName.substr(0, posOfHyphen);
                // if the parallelId is number and extension is 'o', then it is a parallelCompileObjFile.
                return extension == "o" && parallelId.find_first_not_of("0123456789") == std::string::npos;
            };
            if (isParallelCompileObjFile()) {
                continue;
            }
        }
        auto tool =
            std::make_unique<Tool>("CacheCopy", ToolType::INTERNAL_IMPLEMENTED, driverOptions.environment.allVariables);
        tool->AppendArg(srcFile);
        tool->AppendArg(destFile);
        batch.emplace_back(std::move(tool));
    }
    backendCmds.emplace_back(std::move(batch));
    return TC->ProcessGeneration(objFiles);
}

bool CJNATIVEBackend::ProcessGenerationOfIncrementalNoChangeCompile(const std::vector<TempFileInfo>& bitCodeFiles)
{
    std::vector<TempFileInfo> tempBitCodeFiles = bitCodeFiles;

    if (driverOptions.IsLTOEnabled()) {
        std::vector<TempFileInfo> preprocessorInputs;
        for (auto& file : tempBitCodeFiles) {
            auto tempFile = file;
            if (!file.isForeignInput) {
                std::string filePath =
                    driverOptions.GetHashedObjFileName(FileUtil::GetFileNameWithoutExtension(file.fileName)) + ".bc";
                tempFile.filePath = filePath;
                tempFile.rawPath = filePath;
            }
            preprocessorInputs.emplace_back(tempFile);
        }
        auto preprocessedFiles = GeneratePreprocessTools(preprocessorInputs);
        if (driverOptions.outputMode == GlobalOptions::OutputMode::STATIC_LIB) {
            return true;
        }
        return TC->ProcessGeneration(preprocessedFiles);
    }

    // 1. check if all `.o` file exist, if any of them does not exist, then run back to
    // `ProcessGenerationOfNormalCompile`.
    // 2. if all the `.o` files exist, Update the file path in the `bitCodeFiles` from referencing `.bc` file to the
    // corresponding `.o` file, and then trigger the ld.
    for (auto& file : tempBitCodeFiles) {
        auto objFile = driverOptions.GetHashedObjFileName(file.fileName) + ".o";
        if (!FileUtil::FileExist(objFile)) {
            Errorf("The cache directory is incomplete.\n");
            return false;
        }
        file.filePath = driverOptions.GetHashedObjFileName(file.fileName) + ".o";
    }
    return TC->ProcessGeneration(tempBitCodeFiles);
}

void CJNATIVEBackend::PreprocessOfNewPassManager(Tool& tool)
{
    std::string passesCollector = "-passes=";
    std::vector<std::string> passItems;
    {
        using namespace ToolOptions;
        SetFuncType setOptimizationLevelHandler = [&passItems](
                                                      const std::string& option) { passItems.emplace_back(option); };
        SetOptions(setOptimizationLevelHandler, driverOptions, OPT::SetOptimizationLevelOptions);
        // remove the initial hyphen in the options.
        SetFuncType setOptionHandler = [&passItems](
                                           const std::string& option) { passItems.emplace_back(option.substr(1)); };
        SetOptions(setOptionHandler, driverOptions, OPT::SetNewPassManagerOptions);
    }

    for (auto& it : passItems) {
        passesCollector += it;
        if (&it != &passItems.back()) {
            passesCollector += ",";
        }
    }
    tool.AppendArg(passesCollector);
}

std::vector<TempFileInfo> CJNATIVEBackend::GeneratePreprocessTools(const std::vector<TempFileInfo>& bitCodeFiles)
{
    std::vector<TempFileInfo> outputFiles;
    ToolBatch batch{};
    for (const auto& bitCodeFile : bitCodeFiles) {
        // 'opt' can only process one file in one execution, for each bitCodeFile, generate one 'opt' command for it.
        std::unique_ptr<Tool> tool = GenerateCJNativeBaseTool(optPath);
        // set input
        tool->AppendArg(bitCodeFile.filePath);

        // set options
        // handle the new pass manager of 'opt'
        PreprocessOfNewPassManager(*tool);
        {
            using namespace ToolOptions;
            SetFuncType setOptionHandler = [&tool](const std::string& option) { tool->AppendArg(option); };
            std::vector<ToolOptionType> setOptionsPass = {
                OPT::SetOptions,                // Comment ensure vector members are arranged vertically.
                OPT::SetVerifyOptions,          //
                OPT::SetTripleOptions,          //
                OPT::SetCodeObfuscationOptions, //
                OPT::SetLTOOptions,             //
                OPT::SetPgoOptions,             //
                OPT::SetTransparentOptions      // The transparent options must after other options.
            };
            SetOptions(setOptionHandler, driverOptions, setOptionsPass);
        }

        // set output
        // When compiling a static library in LTO mode
        // the optimized bc file generated by the opt phase is used as the output file.
        TempFileKind kind =
            driverOptions.IsLTOEnabled() && driverOptions.outputMode == GlobalOptions::OutputMode::STATIC_LIB
            ? TempFileKind::O_OPT_BC
            : TempFileKind::T_OPT_BC;
        TempFileInfo optBcFileInfo = TempFileManager::Instance().CreateNewFileInfo(bitCodeFile, kind);
        tool->AppendArg("-o", optBcFileInfo.filePath);
        outputFiles.emplace_back(optBcFileInfo);
        batch.emplace_back(std::move(tool));
    }
    backendCmds.emplace_back(std::move(batch));
    return outputFiles;
}

std::vector<TempFileInfo> CJNATIVEBackend::GenerateCompileTool(
    const std::vector<TempFileInfo>& bitCodeFiles, bool emitAssembly)
{
    std::vector<TempFileInfo> outputFiles;
    ToolBatch batch{};
    for (const auto& bitCodeFile : bitCodeFiles) {
        // 'llc' can only process one file in one execution, for each bitCodeFile,
        // generate one 'llc' command for it, just like 'opt'.
        std::unique_ptr<Tool> tool = GenerateCJNativeBaseTool(llcPath);

        // set input
        tool->AppendArg(bitCodeFile.filePath);

        // set options
        {
            using namespace ToolOptions;
            SetFuncType setOptionHandler = [&tool](const std::string& option) { tool->AppendArg(option); };
            std::vector<ToolOptionType> setOptionsPass = {
                LLC::SetOptions,                  // Comment ensure vector members are arranged vertically.
                LLC::SetTripleOptions,             //
                LLC::SetOptimizationLevelOptions, //
                LLC::SetTransparentOptions,       // The transparent options must after other options.
            };
            SetOptions(setOptionHandler, driverOptions, setOptionsPass);
        }

        // set output
        tool->AppendArg(emitAssembly ? "--filetype=asm" : "--filetype=obj");
        auto fileKind = emitAssembly ? TempFileKind::T_ASM : TempFileKind::T_OBJ;
        TempFileInfo fileInfo = TempFileManager::Instance().CreateNewFileInfo(bitCodeFile, fileKind);
        tool->AppendArg("-o", fileInfo.filePath);
        outputFiles.emplace_back(fileInfo);
        batch.emplace_back(std::move(tool));
    }
    backendCmds.emplace_back(std::move(batch));
    return outputFiles;
}

std::unique_ptr<Tool> CJNATIVEBackend::GenerateCJNativeBaseTool(const std::string& toolPath)
{
    auto tool = std::make_unique<Tool>(toolPath, ToolType::BACKEND, driverOptions.environment.allVariables);
    tool->SetLdLibraryPath(FileUtil::JoinPath(FileUtil::GetDirPath(toolPath), "../lib"));
    return tool;
}
