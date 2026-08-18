// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the MachO class.
 */

#ifndef CANGJIE_DRIVER_TOOLCHAIN_MACHO_H
#define CANGJIE_DRIVER_TOOLCHAIN_MACHO_H

#include "cangjie/Driver/Backend/Backend.h"
#include "cangjie/Driver/Driver.h"

namespace Cangjie {
class MachO : public ToolChain {
public:
    MachO(const Cangjie::Driver& driver, const DriverOptions& driverOptions, std::vector<ToolBatch>& backendCmds)
        : ToolChain(driver, driverOptions, backendCmds) {};

    ~MachO() override = default;

    bool ProcessGeneration(std::vector<TempFileInfo>& objFiles) override;

    bool PrepareDependencyPath() override;

    void InitializeLibraryPaths() override;

protected:
    std::string ldPath;
    std::string arPath;
    std::string dsymutilPath;
    std::string stripPath;
    std::string GetSharedLibraryExtension() const override
    {
        return ".dylib";
    }

    // Get the target architecture string. It is passed to the linker.
    virtual std::string GetTargetArchString() const;

    virtual void GenerateArchiveTool(const std::vector<TempFileInfo>& objFiles);
    void HandleLLVMLinkOptions(const std::vector<TempFileInfo>& objFiles, Tool& tool);
    // For LTO staticlib production with -staticlib: only pass .bc files (user code + stdlib),
    // skip runtime .a/.dylib, section.o, cjstart.o, and -L paths.
    void AppendLTOBcInputs(const std::vector<TempFileInfo>& objFiles, Tool& tool);
    virtual void HandleLibrarySearchPaths(Tool& tool, const std::string& cangjieLibPath);

    virtual void AddCRuntimeLibraryPaths();
    // Gather library paths from LIBRARY_PATH and compiler guesses.
    virtual void AddSystemLibraryPaths();
    virtual void GenerateLinkOptions([[maybe_unused]] Tool& tool) {};
    virtual TempFileInfo GenerateLinkingTool([[maybe_unused]] const std::vector<TempFileInfo>& objFiles,
        [[maybe_unused]] const std::string& darwinSDKVersion)
    {
        return TempFileInfo{};
    };
    virtual TempFileInfo GenerateLinking(const std::vector<TempFileInfo>& objFiles);
    virtual void GenerateDebugSymbolFile(const TempFileInfo& binaryFile);
    void GenerateStripSymbolFile(const TempFileInfo& binaryFile);
    // Generate the static link options of built-in libraries except 'std-ast'.
    // The 'std-ast' library is dynamically linked by default.
    void GenerateLinkOptionsOfBuiltinLibsForStaticLink(Tool& tool) const override;
    // Generate the dynamic link options of built-in libraries.
    void GenerateLinkOptionsOfBuiltinLibsForDyLink(Tool& tool) const override;
};
} // namespace Cangjie
#endif // CANGJIE_DRIVER_TOOLCHAIN_MACHO_H
