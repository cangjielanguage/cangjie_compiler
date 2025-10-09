// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the IOS_CJNATIVE class.
 */

#ifndef CANGJIE_DRIVER_TOOLCHAIN_IOS_CJNATIVE_H
#define CANGJIE_DRIVER_TOOLCHAIN_IOS_CJNATIVE_H

#include "Toolchains/CJNATIVE/Darwin_CJNATIVE.h"
#include "cangjie/Driver/Backend/Backend.h"
#include "cangjie/Driver/Driver.h"
#include "cangjie/Driver/Toolchains/ToolChain.h"

using namespace Cangjie;

class IOS_CJNATIVE : public Darwin_CJNATIVE {
public:
    IOS_CJNATIVE(const Cangjie::Driver& driver, const DriverOptions& driverOptions,
        std::vector<ToolBatch>& backendCmds)
        : Darwin_CJNATIVE(driver, driverOptions, backendCmds) {};
    ~IOS_CJNATIVE() override {};

protected:
    const std::vector<std::string> LINUX_CJNATIVE_LINK_OPTIONS = {
#define CJNATIVE_STD_OPTIONS(OPTION) (OPTION),
#include "Toolchains/BackendOptions.inc"
#undef CJNATIVE_STD_OPTIONS
#define CJNATIVE_DARWIN_BASIC_OPTIONS(OPTION) (OPTION),
#include "Toolchains/BackendOptions.inc"
#undef CJNATIVE_DARWIN_BASIC_OPTIONS
    };
    // Gather library paths from LIBRARY_PATH and compiler guesses.
    void AddSystemLibraryPaths() override;
    TempFileInfo GenerateLinkingTool(
        const std::vector<TempFileInfo>& objFiles, const std::string& darwinSDKVersion) override;
    void GenerateLinkOptions(Tool& tool) override;
};

#endif // CANGJIE_DRIVER_TOOLCHAIN_IOS_CJNATIVE_H