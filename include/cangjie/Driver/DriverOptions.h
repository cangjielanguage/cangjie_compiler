// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the DriverOptions, which provides options for Driver.
 */

#ifndef CANGJIE_DRIVER_DRIVEROPTIONS_H
#define CANGJIE_DRIVER_DRIVEROPTIONS_H

#include <string>

#include "cangjie/Option/Option.h"

namespace Cangjie {
class DriverOptions : public GlobalOptions {
public:
    /**
     * @brief The constructor of class DriverOptions.
     *
     * @return DriverOptions The instance of DriverOptions.
     */
    DriverOptions() = default;

    /**
     * @brief The destructor of class DriverOptions.
     */
    ~DriverOptions() override = default;

    std::string optArg;
    std::string llcArg;

    std::optional<std::string> targetCPU = std::nullopt;

    bool linkStatic = false;

    // Strip symbol table for DSO and executable or not.
    // Controlled by --strip-all/-s.
    bool stripSymbolTable = false;

    // User-provided custom arguments to linker.
    // Values is passed by --link-options.
    std::vector<std::string> linkOptions;

    // User-provided paths for linkers to search for libraries,
    // passed by --library-path/-L.
    std::vector<std::string> librarySearchPaths;

    // User-provided library names for linkers,
    // passed by --library/-l.
    std::vector<std::string> libraries;

    // User-provided paths for the driver to search for binaries and object files,
    // passed by --toolchain/-B.
    std::vector<std::string> toolChainPaths;

    // Sysroot is the root directory under which toolchain binaries, libraries and header files can be found.
    // The sysroot is "/" by default.
#ifdef _WIN32
    std::string sysroot = "C:/windows";
#else
    std::string sysroot = "/";
#endif

    bool customizedSysroot = false;

    bool useRuntimeRpath = false;

    // Whether cjc write rpath with sanitizer version cangjie runtime path to binary
    bool sanitizerEnableRpath = false;

    bool incrementalCompileNoChange = false;

    // ---------- CODE OBFUSCATION OPTIONS ----------
    bool enableObfAll = false;

    /**
     * @brief Check whether it is obfuscation enabled.
     *
     * @return bool Return true If it is obfuscation enabled.
     */
    bool IsObfuscationEnabled() const override
    {
        return enableStringObfuscation || enableConstObfuscation || enableLayoutObfuscation ||
            enableCFflattenObfuscation || enableCFBogusObfuscation;
    }

    std::optional<bool> enableObfExportSyms;
    std::optional<bool> enableObfLineNumber;
    std::optional<bool> enableObfSourcePath;
    std::optional<bool> enableLayoutObfuscation;
    std::optional<bool> enableConstObfuscation;
    std::optional<bool> enableStringObfuscation;
    std::optional<bool> enableCFflattenObfuscation;
    std::optional<bool> enableCFBogusObfuscation;
    std::optional<std::string> layoutObfSymPrefix;
    std::optional<std::string> layoutObfInputSymMappingFiles = std::nullopt;
    std::optional<std::string> layoutObfOutputSymMappingFile = std::nullopt;
    std::optional<std::string> layoutObfUserMappingFile = std::nullopt;
    std::optional<std::string> obfuscationConfigFile = std::nullopt;

    static const int OBFUCATION_LEVEL_MIN = 1;
    static const int OBFUCATION_LEVEL_MAX = 9;

    /**
     * Valid range of obfuscation level is [1,9] (see OBFUCATION_LEVEL_MIN and OBFUCATION_LEVEL_MAX).
     * Higher obfuscation level represents that more obfuscation work will be done on the code, which
     * makes the product more difficult to be understood by reverse enginering. Higher obfuscation level
     * also causes larger code size and more runtime overhead. 5 is a balanced choice, for both source
     * code safety and cost.
     */
    int obfuscationLevel = 5;

    std::optional<int> obfuscationSeed = 0;

    /**
     * @brief Reprocess obfuse option.
     *
     * @return bool Return true.
     */
    bool ReprocessObfuseOption() override;

protected:
    virtual std::optional<bool> ParseOption(OptionArgInstance& arg) override;
    virtual bool PerformPostActions() override;

private:
    bool CheckObfuscationOptions() const;
    bool CheckTargetCPUOption();
    bool SetupSysroot();
    bool CheckRuntimeRPath() const;
    bool CheckSanitizerRPath() const;
    bool CheckStaticOption();
};
} // namespace Cangjie
#endif // CANGJIE_DRIVER_DRIVEROPTIONS_H
