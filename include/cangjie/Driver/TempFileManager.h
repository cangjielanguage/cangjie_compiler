// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the TempFileManager class.
 */

#ifndef CANGJIE_DRIVER_TEMP_FILES_UTIL_H
#define CANGJIE_DRIVER_TEMP_FILES_UTIL_H

#include <atomic>
#include <string>
#include <unordered_map>
#include <vector>

#include "cangjie/Driver/TempFileInfo.h"
#include "cangjie/Option/Option.h"

namespace Cangjie {
class TempFileManager {
public:
    /**
     * @brief Disable the copy constructor of class TempFileManager.
     */
    TempFileManager(TempFileManager const&) = delete;

    /**
     * @brief Disable the copy assignment operator of class TempFileManager.
     */
    TempFileManager& operator=(TempFileManager const&) = delete;

    /**
     * @brief Obtains the globally unique TempFileManager instance.
     *
     * @return TempFileManager The globally unique TempFileManager instance.
     */
    static TempFileManager& Instance()
    {
        static TempFileManager manager{};
        return manager;
    }

    /**
     * @brief Initialize the constructed TempFileManager.
     *
     * @param options GlobalOptions Instance.
     * @param isFrontend It is cjc-frontend or cjc being executed.
     * @return bool Return true if Initialization succeeded.
     */
    bool Init(const GlobalOptions& options, bool isFrontend);

    /**
     * @brief Create a new TempFileInfo whose type is kind.
     *
     * @param info Old TempFileInfo, It may only have the fileName field.
     * @param kind Type of TempFileInfo to be created.
     * @return TempFileInfo The new TempFileInfo.
     */
    TempFileInfo CreateNewFileInfo(const TempFileInfo& info, TempFileKind kind);

    /**
     * @brief Get the path of the temporary folder.
     * It may be a generated temporary directory or a user specified location.
     *
     * @return std::string The path of the temporary folder.
     */
    std::string GetTempFolder();

    /**
     * @brief Write a tool-side response file (rsp) under the temp folder.
     *
     * The returned path is unique to support concurrent tool execution. The
     * file is registered for deletion by `DeleteTempFiles()` unless
     * `--save-temps` is in effect, so callers must NOT delete it themselves
     * (the spawned child may not have finished reading it when this call
     * returns).
     *
     * @param content The rsp file content (GNU-style serialized args).
     * @return std::string The absolute path of the written rsp file. Empty
     *                     string on write failure (an error is reported via
     *                     `Errorln`).
     */
    std::string CreateRspFile(const std::string& content);

    /**
     * @brief Delete all temporary files.
     */
    void DeleteTempFiles();

    /**
     * @brief Delete all temporary files asynchronously in a signal-safe manner.
     */
    void DeleteTempFilesSignalSafe();

    /**
     * @brief Check whether temporary files are deleted.
     *
     * @return bool Return true If temporary files are deleted.
     */
    bool IsDeleted() const;

private:
    bool isCjcFrontend{false};
    GlobalOptions opts{};
    std::string tempDir{};
    std::string outputDir{};
    std::string outputName{};
    std::vector<std::string> deletedFiles{};
    std::atomic<uint8_t> isDeleted{0}; // 0: not deleted, 1: deleting, 2: deleted
    TempFileManager(){};
    bool InitOutPutDir();
    bool InitTempDir();
    TempFileInfo CreateIntermediateFileInfo(const TempFileInfo& info, TempFileKind kind);
    TempFileInfo CreateTempBcFileInfo(const TempFileInfo& info, TempFileKind kind);
    TempFileInfo CreateOutputFileInfo(const TempFileInfo& info, TempFileKind kind);
    std::unordered_map<TempFileKind, std::function<std::string()>> fileSuffixMap;
    TempFileInfo CreateTempFileInfo(const TempFileInfo& info, TempFileKind kind);
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    TempFileInfo CreateLinuxLLVMOptOutputBcFileInfo(const TempFileInfo& info, TempFileKind kind);
#endif
    std::string GetDylibSuffix() const;
    std::string GetObjSuffix() const;
};

} // namespace Cangjie

#endif // CANGJIE_DRIVER_TEMP_FILES_UTIL_H
