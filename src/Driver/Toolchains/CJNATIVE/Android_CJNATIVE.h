// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the Android_CJNATIVE ToolChain class.
 */

#ifndef CANGJIE_DRIVER_TOOLCHAIN_ANDROID_CJNATIVE_H
#define CANGJIE_DRIVER_TOOLCHAIN_ANDROID_CJNATIVE_H

#include <string>

#include "Toolchains/CJNATIVE/Linux_CJNATIVE.h"
#include "cangjie/Driver/Backend/Backend.h"
#include "cangjie/Driver/Driver.h"
#include "cangjie/Driver/Toolchains/ToolChain.h"

namespace Cangjie {
class Android_CJNATIVE : public Linux_CJNATIVE {
public:
    Android_CJNATIVE(
        const Cangjie::Driver& driver, const DriverOptions& driverOptions, std::vector<ToolBatch>& backendCmds)
        : Linux_CJNATIVE(driver, driverOptions, backendCmds) {};
    ~Android_CJNATIVE() override {};

protected:
    void InitializeLibraryPaths() override;
    void AddCRuntimeLibraryPaths() override;
    bool PrepareDependencyPath() override;

    bool GenerateLinking(const std::vector<TempFileInfo>& objFiles) override;
    void GenerateLinkingTool(const std::vector<TempFileInfo>& objFiles, const std::string& gccLibPath,
        const std::pair<std::string, std::string>& gccCrtFilePair) override;

    void GenerateLinkOptions(Tool& tool) override;

    void HandleSanitizerDependencies(Tool& tool) override;

private:
    std::string sysroot;
};
} // namespace Cangjie
#endif // CANGJIE_DRIVER_TOOLCHAIN_ANDROID_CJNATIVE_H