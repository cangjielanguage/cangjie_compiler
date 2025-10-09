// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the TempFileManager Class.
 */

#include "cangjie/Driver/TempFileManager.h"
#include "cangjie/Basic/Print.h"
#include "cangjie/Basic/StringConvertor.h"
#include "cangjie/Option/Option.h"
#include "cangjie/Utils/ConstantsUtils.h"
#include "cangjie/Utils/FileUtil.h"
#include "cangjie/Utils/Utils.h"

#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unordered_map>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <windows.h>
#endif

namespace {
#ifdef _WIN32
const std::string TMP_DIR_ENVIRONMENT_KEY = "TMP";
const std::string DEFAULT_TMP_DIR = "C:\\Windows\\Temp";
const char CODE[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
#else
const std::string TMP_DIR_ENVIRONMENT_KEY = "TMPDIR";
const std::string DEFAULT_TMP_DIR = "/tmp";
const char CODE[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
#endif
const std::string CANGJIE_TMP_DIR_PERFIX = "/cangjie-tmp-";

constexpr size_t MULTIPLE_OF_SECOND_TO_NANOSECOND = 1'000'000'000;
constexpr uint8_t NOT_DELETED = 0;
constexpr uint8_t DELETING = 1;
constexpr uint8_t DELETED = 2;

std::optional<std::string> TempDirLegalityCheck(const std::string& path, bool fromEnvSetting)
{
    std::optional<std::string> absPath = Cangjie::FileUtil::GetAbsPath(path);
    if (!absPath.has_value()) {
        Cangjie::Errorf("%s (%s) does not exist.\n",
            fromEnvSetting ? "Temporary directory path set by environment variable" : "Temporary directory path",
            fromEnvSetting ? TMP_DIR_ENVIRONMENT_KEY.c_str() : DEFAULT_TMP_DIR.c_str());
        return {};
    }
    if (fromEnvSetting) {
        if (!Cangjie::FileUtil::IsDir(path)) {
            Cangjie::Errorf("Temporary directory path set by environment variable(%s) is not a directory.\n",
                TMP_DIR_ENVIRONMENT_KEY.c_str());
            return {};
        }
    }
    std::string temp = absPath.value();
    if (!Cangjie::FileUtil::CanWrite(temp)) {
        Cangjie::Errorf("%s (%s) does not have write permission.\n",
            fromEnvSetting ? "Temporary directory path set by environment variable" : "Temporary directory path",
            fromEnvSetting ? TMP_DIR_ENVIRONMENT_KEY.c_str() : DEFAULT_TMP_DIR.c_str());
        return {};
    };
    return temp;
}

std::string GetErrMessage(int error)
{
    constexpr size_t buffSize = 512;
    char buf[buffSize] = {0};
#ifdef _WIN32
    if (strerror_s(buf, buffSize, error) != 0) {
        return "";
    }
    return std::string(buf);
#elif defined(__ohos__) || defined(__APPLE__)
    if (strerror_r(error, buf, buffSize) != 0) {
        return "";
    }
    return std::string(buf);
#else
    return std::string(strerror_r(error, buf, buffSize));
#endif
}

std::string SetNowTimeEncodedString()
{
    struct timespec wallNow = {0, 0};
    (void)clock_gettime(CLOCK_REALTIME, &wallNow);
    size_t ns =
        static_cast<size_t>(wallNow.tv_sec) * MULTIPLE_OF_SECOND_TO_NANOSECOND + static_cast<size_t>(wallNow.tv_nsec);
    char res[32] = {0}; // 32 is much greater than the length of the result
    size_t index = 0;
    size_t numberOfcode = strlen(CODE);
    while (ns > numberOfcode) {
        size_t i = ns % numberOfcode;
        ns = ns / numberOfcode;
        res[index] = CODE[i];
        index++;
    }
    res[index] = CODE[ns];
    std::string s = std::string(res);
    std::reverse(s.begin(), s.end());
    return s;
}

std::string CreateTempDirName(const std::string& tempDir)
{
    std::stringstream ss;
    ss << CANGJIE_TMP_DIR_PERFIX;
    ss << SetNowTimeEncodedString();
    ss << "-" << Cangjie::Utils::GenerateRandomHexString();
    return Cangjie::FileUtil::JoinPath(tempDir, ss.str());
}

std::optional<std::string> MakeTempDir(const std::string& tempDir, bool fromEnvSetting)
{
    int8_t loopSize = 10; // Loop 10 times to try creating a temporary directory.
    do {
        std::string path = CreateTempDirName(tempDir);
        loopSize--;
#ifdef _WIN32
        std::optional<std::wstring> tempValue = Cangjie::StringConvertor::StringToWString(path);
        if (tempValue.has_value()) {
            if (_wmkdir(tempValue.value().c_str()) == 0) {
                return path;
            }
        }
#else
        // The permission on the new directory is 775(drwxrwxr-x)
        if (mkdir(const_cast<char*>(path.c_str()), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == 0) {
            return path;
        }
#endif
    } while (errno == EEXIST && loopSize > 0);
    std::string errMsg = GetErrMessage(errno);
    Cangjie::Errorf("Failed to create the directory in temporary directory path %s(%s)%s\n",
        fromEnvSetting ? "set by environment variable" : "",
        fromEnvSetting ? TMP_DIR_ENVIRONMENT_KEY.c_str() : DEFAULT_TMP_DIR.c_str(),
        errMsg.empty() ? errMsg.c_str() : (": " + errMsg).c_str());
    return {};
}

#ifdef _WIN32
void RemoveDirRecursively(const std::wstring& dirPath)
{
    HANDLE hFind;
    WIN32_FIND_DATAW findData;
    std::wstring dirNew = dirPath + std::wstring(L"\\*.*");
    hFind = FindFirstFileW(dirNew.c_str(), &findData);
    if (findData.dwFileAttributes == INVALID_FILE_ATTRIBUTES) {
        FindClose(hFind);
        return;
    }
    do {
        std::wstring fileName = std::wstring(findData.cFileName);
        if (fileName == std::wstring(L".") || fileName == std::wstring(L"..") || fileName == std::wstring()) {
            FindNextFileW(hFind, &findData);
            continue;
        }
        std::wstring newPath = dirPath + std::wstring(L"\\") + fileName;
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            RemoveDirRecursively(newPath);
        } else {
            DeleteFileW(newPath.c_str());
        }
    } while (FindNextFileW(hFind, &findData));
    FindClose(hFind);
    RemoveDirectoryW(dirPath.c_str());
}
#else
void RemoveDirRecursively(const std::string& dirPath)
{
    DIR* dir = opendir(dirPath.c_str());
    if (!dir) {
        return;
    }
    for (auto entry = readdir(dir); entry != nullptr; entry = readdir(dir)) {
        std::string fileName = std::string(entry->d_name);
        if (fileName == "." || fileName == "..") {
            continue;
        }
        std::string newPath = Cangjie::FileUtil::JoinPath(dirPath, fileName);
        if (entry->d_type == DT_REG) {
            (void)unlink(newPath.c_str());
        } else if (entry->d_type == DT_DIR) {
            RemoveDirRecursively(newPath);
        }
    }
    (void)closedir(dir);
    (void)rmdir(dirPath.c_str());
}
#endif

} // namespace

using namespace Cangjie;

bool TempFileManager::Init(const GlobalOptions& options, bool isFrontend)
{
    isCjcFrontend = isFrontend;
    opts = options;
    isDeleted.store(NOT_DELETED);
    if (!InitOutPutDir()) {
        return false;
    }
    fileSuffixMap = {
        {TempFileKind::O_CJO, []() { return SERIALIZED_FILE_EXTENSION; }},
        {TempFileKind::O_FULL_BCHIR, []() { return FULL_BCHIR_SERIALIZED_FILE_EXTENSION; }},
        {TempFileKind::O_BCHIR, []() { return BCHIR_SERIALIZED_FILE_EXTENSION; }},
        {TempFileKind::T_BC, []() { return ".bc"; }},
        {TempFileKind::O_BC, []() { return ".bc"; }},
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
        {TempFileKind::O_STATICLIB, []() { return ".a"; }},
        {TempFileKind::O_EXE, [this]() { return opts.target.os == Triple::OSType::WINDOWS ? ".exe" : ""; }},
        {TempFileKind::O_DYLIB, [this]() { return GetDylibSuffix(); }},
        {TempFileKind::O_MACRO, [this]() { return GetDylibSuffix(); }},
        {TempFileKind::T_OPT_BC, []() { return ".opt.bc"; }},
        {TempFileKind::O_OPT_BC, []() { return ".bc"; }},
        {TempFileKind::T_ASM, []() { return ".s"; }},
#endif
        {TempFileKind::O_CHIR, []() { return CHIR_SERIALIZATION_FILE_EXTENSION; }},
        {TempFileKind::T_OBJ, []() { return ".o"; }},
        {TempFileKind::T_EXE_MAC, []() { return  "_temp"; }},
        {TempFileKind::T_DYLIB_MAC, []() { return "_temp.dylib"; }},
    };
    return InitTempDir();
}

bool TempFileManager::InitOutPutDir()
{
    if (FileUtil::IsDir(opts.output)) {
        outputDir = opts.output;
        outputName = "";
    } else {
        outputDir = FileUtil::GetDirPath(opts.output);
        outputName = FileUtil::GetFileName(opts.output);
    }
    std::optional<std::string> absPath = FileUtil::GetAbsPath(outputDir);
    if (absPath.has_value()) {
        outputDir = absPath.value();
    } else {
        Errorln("Output path does not exist.");
        return false;
    }
    if (!opts.ValidateDirectoryPath(outputDir, FileUtil::FileMode::FM_WRITE).has_value()) {
        return false;
    }
    return true;
}

bool TempFileManager::InitTempDir()
{
    if (opts.saveTemps) {
        tempDir = opts.tempFolderPath;
        std::optional<std::string> absPath = FileUtil::GetAbsPath(tempDir);
        if (!absPath.has_value()) {
            return false;
        }
        tempDir = absPath.value();
        return true;
    }
    std::unordered_map<std::string, std::string>& envs = opts.environment.allVariables;
    auto found = envs.find(TMP_DIR_ENVIRONMENT_KEY);
    bool fromEnvSetting = false;
    if (found != envs.end()) {
        tempDir = found->second;
        fromEnvSetting = true;
    } else {
        tempDir = DEFAULT_TMP_DIR;
    }
    std::optional<std::string> ret = TempDirLegalityCheck(tempDir, fromEnvSetting);
    if (!ret.has_value()) {
        return false;
    }
    std::optional<std::string> dir = MakeTempDir(ret.value(), fromEnvSetting);
    if (dir.has_value()) {
        tempDir = dir.value();
        // Record temporary directory to be deleted later.
        deletedFiles.push_back(tempDir);
        return true;
    }
    return false;
}

std::string TempFileManager::GetTempFolder()
{
    return tempDir;
}

TempFileInfo TempFileManager::CreateNewFileInfo(const TempFileInfo& info, TempFileKind kind)
{
    TempFileInfo newInfo;
    switch (kind) {
        case TempFileKind::O_CJO:
        case TempFileKind::O_FULL_BCHIR:
        case TempFileKind::O_BCHIR:
            newInfo = CreateIntermediateFileInfo(info, kind);
            break;
        case TempFileKind::O_CHIR:
            newInfo = CreateTempFileInfo(info, kind);
            break;
        case TempFileKind::T_BC:
            newInfo = CreateTempBcFileInfo(info, kind);
            break;
        case TempFileKind::O_BC:
        case TempFileKind::O_EXE:
        case TempFileKind::O_MACRO:
        case TempFileKind::O_DYLIB:
        case TempFileKind::O_STATICLIB:
            newInfo = CreateOutputFileInfo(info, kind);
            break;
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
        case TempFileKind::T_OPT_BC:
        case TempFileKind::T_ASM:
        case TempFileKind::T_OBJ:
        case TempFileKind::T_DYLIB_MAC:
        case TempFileKind::T_EXE_MAC:
            newInfo = CreateTempFileInfo(info, kind);
            break;
        case TempFileKind::O_OPT_BC:
            newInfo = CreateLinuxLLVMOptOutputBcFileInfo(info, kind);
            break;
#endif
        default:
            CJC_ASSERT(false);
            return {};
    }
    newInfo.rawPath = info.rawPath;
    newInfo.isFrontendOutput = info.isFrontendOutput;
    newInfo.isForeignInput = info.isForeignInput;
    return newInfo;
}

TempFileInfo TempFileManager::CreateIntermediateFileInfo(const TempFileInfo& info, TempFileKind kind)
{
    std::string dir = opts.outputDir.value_or(outputDir);
    std::string outputFilePath = FileUtil::JoinPath(dir, info.fileName) + fileSuffixMap.at(kind)();
    return TempFileInfo{info.fileName, outputFilePath};
}

TempFileInfo TempFileManager::CreateTempBcFileInfo(const TempFileInfo& info, TempFileKind kind)
{
    if (isCjcFrontend) {
        // If compile with cjc-frontend, the temporary bc file is used as the output file.
        return CreateOutputFileInfo(info, kind);
    }
    std::string tempFilePath = FileUtil::JoinPath(tempDir, info.fileName);
    tempFilePath = tempFilePath + fileSuffixMap.at(kind)();
#ifndef COMPILER_EXPLORER_RACE_FIX
    if (!opts.saveTemps) {
        // Record temporary file to be deleted later.
        deletedFiles.push_back(tempFilePath);
    }
#endif
    return TempFileInfo{info.fileName, tempFilePath};
}

TempFileInfo TempFileManager::CreateOutputFileInfo(const TempFileInfo& info, TempFileKind kind)
{
    if (outputName.empty()) {
        std::string outputFileName;
        if (kind == TempFileKind::O_EXE) {
            outputFileName = "main";
        } else if (kind == TempFileKind::O_DYLIB || kind == TempFileKind::O_STATICLIB) {
            outputFileName = "lib" + info.fileName;
        } else if (kind == TempFileKind::O_MACRO) {
            outputFileName = "lib-macro_" + info.fileName;
        } else {
            outputFileName = info.fileName;
        }
        std::string outputFilePath = FileUtil::JoinPath(outputDir, outputFileName);
        outputFilePath = outputFilePath + fileSuffixMap.at(kind)();
        return TempFileInfo{outputFileName, outputFilePath};
    } else {
        std::string outputFilePath = FileUtil::JoinPath(outputDir, outputName);
        return TempFileInfo{outputName, outputFilePath};
    }
}

TempFileInfo TempFileManager::CreateTempFileInfo(const TempFileInfo& info, TempFileKind kind)
{
    std::string tempFilePath = FileUtil::JoinPath(tempDir, info.fileName);
    tempFilePath = tempFilePath + fileSuffixMap.at(kind)();
    if (!opts.saveTemps) {
        // Record temporary file to be deleted later.
        deletedFiles.push_back(tempFilePath);
    }
    return TempFileInfo{info.fileName, tempFilePath};
}

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
TempFileInfo TempFileManager::CreateLinuxLLVMOptOutputBcFileInfo(const TempFileInfo& info, TempFileKind kind)
{
    // The output is in the same directory as the .cjo file
    std::string dir = opts.outputDir.value_or(outputDir);
    if (outputName.empty()) {
        std::string outputFileName = "lib" + info.fileName;
        std::string outputFilePath = FileUtil::JoinPath(dir, outputFileName);
        outputFilePath = outputFilePath + fileSuffixMap.at(kind)();
        return TempFileInfo{outputFileName, outputFilePath};
    } else {
        std::string outputFilePath = FileUtil::JoinPath(dir, outputName);
        return TempFileInfo{outputName, outputFilePath};
    }
}
#endif

void TempFileManager::DeleteTempFiles(bool isSignalSafe)
{
    if (isDeleted.exchange(DELETING) != NOT_DELETED && IsDeleted()) {
        return;
    }
#ifdef _WIN32
    int i = static_cast<int>(deletedFiles.size()) - 1;
    while (i >= 0) {
        std::string filePath = deletedFiles[i];
        std::optional<std::wstring> tempValue = Cangjie::StringConvertor::StringToWString(filePath);
        if (!tempValue.has_value()) {
            i--;
            continue;
        }
        std::wstring wFilePath = tempValue.value();
        DWORD dwAttr = GetFileAttributesW(wFilePath.c_str());
        if (dwAttr == INVALID_FILE_ATTRIBUTES) {
            i--;
            continue;
        }
        if ((dwAttr & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            RemoveDirRecursively(wFilePath);
        } else {
            DeleteFileW(wFilePath.c_str());
        }
        i--;
    }
#else
    for (auto it = deletedFiles.crbegin(); it != deletedFiles.crend(); ++it) {
        const char* filePath = it->c_str();
        struct stat buf;
        if (stat(filePath, &buf) != 0) {
            continue;
        }
        if (S_ISREG(buf.st_mode)) {
            (void)unlink(filePath);
        } else if (S_ISDIR(buf.st_mode)) {
            (void)rmdir(filePath);
            if (!isSignalSafe) {
                RemoveDirRecursively(filePath);
            }
        }
    }
#endif
    isDeleted.store(DELETED);
}

bool TempFileManager::IsDeleted() const
{
    if (isDeleted.load() == DELETED) {
        return true;
    }
    if (isDeleted.load() == NOT_DELETED) {
        return false;
    }
    if (isDeleted.load() == DELETING) {
        size_t n = MULTIPLE_OF_SECOND_TO_NANOSECOND;
        while (n > 0) {
            if (isDeleted.load() == DELETED) {
                return true;
            }
            n--;
        }
    }
    return true;
}

std::string TempFileManager::GetDylibSuffix() const
{
    switch (opts.target.os) {
        case Triple::OSType::WINDOWS:
            return ".dll";
        case Triple::OSType::DARWIN:
        case Triple::OSType::IOS:
            return ".dylib";
        case Triple::OSType::LINUX:
        case Triple::OSType::UNKNOWN:
        default:
            return ".so";
    }
}
