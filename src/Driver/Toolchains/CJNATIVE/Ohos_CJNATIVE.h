// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the Ohos_CJNATIVE ToolChain class.
 */

#ifndef CANGJIE_DRIVER_TOOLCHAIN_Ohos_CJNATIVE_H
#define CANGJIE_DRIVER_TOOLCHAIN_Ohos_CJNATIVE_H

#include "cangjie/Driver/Backend/Backend.h"
#include "cangjie/Driver/Driver.h"
#include "cangjie/Driver/Toolchains/ToolChain.h"
#include "Toolchains/CJNATIVE/Linux_CJNATIVE.h"

namespace Cangjie {
class Ohos_CJNATIVE : public Linux_CJNATIVE {
public:
    Ohos_CJNATIVE(const Cangjie::Driver& driver, const DriverOptions& driverOptions,
        std::vector<ToolBatch>& backendCmds)
        : Linux_CJNATIVE(driver, driverOptions, backendCmds) {};
    ~Ohos_CJNATIVE() override = default;

protected:
    void AddCRuntimeLibraryPaths() override;
    bool PrepareDependencyPath() override;

    bool GenerateLinking(const std::vector<TempFileInfo>& objFiles) override;
    void GenerateLinkingTool(const std::vector<TempFileInfo>& objFiles, const std::string& gccLibPath,
        const std::pair<std::string, std::string>& gccCrtFilePair) override;

    void GenerateLinkOptions(Tool& tool) override;

    void HandleSanitizerDependencies(Tool& tool) override;

    std::string GetClangRTProfileLibraryName() const override
    {
        return "libclang_rt.profile.a";
    }
};
} // namespace Cangjie
#endif // CANGJIE_DRIVER_TOOLCHAIN_Ohos_CJNATIVE_H
