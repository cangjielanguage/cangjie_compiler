// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the Linux_CJNATIVE class.
 */

#ifndef CANGJIE_DRIVER_TOOLCHAIN_Darwin_CJNATIVE_H
#define CANGJIE_DRIVER_TOOLCHAIN_Darwin_CJNATIVE_H

#include "cangjie/Driver/Backend/Backend.h"
#include "cangjie/Driver/Driver.h"
#include "Toolchains/MachO.h"
#include "cangjie/Driver/Toolchains/ToolChain.h"

namespace Cangjie {
class Darwin_CJNATIVE : public MachO {
public:
    Darwin_CJNATIVE(const Cangjie::Driver& driver, const DriverOptions& driverOptions,
        std::vector<ToolBatch>& backendCmds)
        : MachO(driver, driverOptions, backendCmds) {};
    ~Darwin_CJNATIVE() override = default;

protected:
    const std::vector<std::string> DARWIN_CJNATIVE_LINK_OPTIONS = {
#define CJNATIVE_STD_OPTIONS(OPTION) (OPTION),
#include "Toolchains/BackendOptions.inc"
#undef CJNATIVE_STD_OPTIONS
#define CJNATIVE_DARWIN_BASIC_OPTIONS(OPTION) (OPTION),
#include "Toolchains/BackendOptions.inc"
#undef CJNATIVE_DARWIN_BASIC_OPTIONS
    };
    // Gather library paths from LIBRARY_PATH and compiler guesses.
    void AddSystemLibraryPaths() override;
    void GenerateLinkOptionsForLTO(Tool& tool) const;
    void GenerateArchiveTool(const std::vector<TempFileInfo>& objFiles) override;
    TempFileInfo GenerateLinkingTool(
        const std::vector<TempFileInfo>& objFiles, const std::string& darwinSDKVersion) override;
    void GenerateLinkOptions(Tool& tool) override;
    virtual TempFileInfo GenerateStaticLibObjects(const std::vector<TempFileInfo>& objFiles);
};
} // namespace Cangjie
#endif // CANGJIE_DRIVER_TOOLCHAIN_Darwin_CJNATIVE_H
