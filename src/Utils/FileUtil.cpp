// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements file util related apis.
 */

#include "cangjie/Utils/FileUtil.h"

#include <fstream>
#include <istream>
#include <optional>
#include <queue>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "cangjie/Basic/Print.h"
#include "cangjie/Basic/Utils.h"
#include "cangjie/Driver/StdlibMap.h"
#include "cangjie/Modules/ImportManager.h"
#include "cangjie/Utils/Unicode.h"
#include "cangjie/Lex/Lexer.h"

#ifdef _WIN32
#include "cangjie/Basic/StringConvertor.h"
#include <direct.h>
#include <fileapi.h>
#include <io.h>
#include <windows.h>
#include <winerror.h>
#ifndef PATH_MAX
#define PATH_MAX MAX_PATH
#endif
#elif defined(__linux__) || defined(__APPLE__)
#include <dirent.h>
#endif

namespace Cangjie::FileUtil {
namespace {
constexpr int SHORT_HASH_NUM = 8;
constexpr int BOM_ENCODING_LEN = 3; // EF BB BF
constexpr char SLASH = '/';
constexpr char BACKSLASH = '\\';
constexpr char BACKTICK = '`';
constexpr std::string_view TEST_FILE_NAME{"_test.cj"};

#ifdef _WIN32
inline int Mkdir(const std::string& path)
{
    return _mkdir(path.c_str());
}
#else
inline int Mkdir(const std::string& path)
{
    return mkdir(path.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
}
#endif

/**
 * Find last of dir split, if found return true and the pos.
 */
inline std::optional<size_t> FindLastOfDirSplit(const std::string& str)
{
    auto lastDirSepPos = str.find_last_of(DIR_SEPARATOR);
    if (lastDirSepPos != std::string::npos) {
        return {lastDirSepPos};
    }
    return std::nullopt;
}

/**
 * Whether the last char is '/\\'.
 */
inline bool IsLastCharDirSplit(const std::string& str)
{
    if (str.empty()) {
        return false;
    }
    return str.find_last_of(DIR_SEPARATOR) == str.size() - 1;
}

inline std::string AppendPath(const std::string& p1, const std::string& p2)
{
    if (IsLastCharDirSplit(p1)) {
        return p1 + p2;
    }
    return p1 + DIR_SEPARATOR[0] + p2;
}

/**
 * Find last of '.', if found returns the pos, otherwise returns nullopt.
 */
inline std::optional<size_t> FindLastOfDot(const std::string& str)
{
    auto pos = str.find_last_of('.');
    if (pos != std::string::npos) {
        return {pos};
    }
    return std::nullopt;
}

/**
 * Whether the @p str contain '/\\'.
 */
inline bool HasDirSplit(const std::string& str)
{
    return str.find_first_of(DIR_SEPARATOR) != std::string::npos;
}

inline bool CanExecute(const std::string& path)
{
    if (FileExist(path)) {
        return Access(path, FM_EXE);
    }
    return false;
}
} // namespace

bool IsSlash(char c)
{
#ifdef _WIN32
    return c == BACKSLASH || c == SLASH;
#else
    return c == SLASH;
#endif
}

bool CheckCommandLineInjection(const std::string& cmd)
{
    if (cmd.find_first_of(INJECTION_STRING) != std::string::npos) {
        return true;
    }

    // Allow ` appears in cmd in pairs.
    // Backticks are allowed becasue module names of Cangjie accept backticks. A user may wrap a
    // module name with a pair of backticks to ensure Compiler doesn't treat the name as reserved
    // keywords. However, it seems unnecessary to ask for wrapping such module names with backticks
    // in the command. We know the argument of --module-name IS a module name not a reserved
    // keyword.
    bool isEven = true;
    size_t pos = 0;
    while ((pos = cmd.find(BACKTICK, pos)) != std::string::npos) {
        isEven = !isEven;
        pos++;
    }
    return !isEven;
}

std::string TransferEscapeBacktick(const std::string& s)
{
#ifdef _WIN32
    return s;
#else
    std::string ret = s;
    auto cnt = std::count(s.begin(), s.end(), BACKTICK);
    ret.reserve(s.size() + static_cast<size_t>(cnt));
    size_t pos = s.size() - 1;
    while ((pos = s.rfind(BACKTICK, pos)) != std::string::npos) {
        ret.insert(pos, 1, BACKSLASH);
        if (pos == 0) {
            break;
        }
        pos--;
    }
    return ret;
#endif
}

std::string GetFileName(const std::string& filePath)
{
    std::string path = filePath;
    path = Normalize(path);
    auto posOpt = FindLastOfDirSplit(path);
    if (posOpt) {
        return path.substr(posOpt.value() + 1);
    }
    return path;
}

std::string GetQuoted(const std::string& str)
{
    std::stringstream ss;
    ss << std::quoted(str);
    return ss.str();
}

std::string GetDirName(const std::string& filePath)
{
    std::string path = filePath;
    if (IsLastCharDirSplit(path)) {
        path.pop_back();
    }
    if (path == "." || path == "..") {
        auto absolutePath = GetAbsPath(path);
        path = absolutePath.has_value() ? absolutePath.value() : path;
    }
    return GetFileName(path);
}

std::string GetDirPath(const std::string& filePath)
{
    auto posOpt = FindLastOfDirSplit(filePath);
    if (posOpt) {
        return Normalize(filePath.substr(0, posOpt.value()));
    }
    return ".";
}

std::string GetFileExtension(const std::string& filePath)
{
    auto dotPos = GetFileExtensionSeparatorPos(filePath).value_or(filePath.size());
    if (dotPos < filePath.size()) {
        return filePath.substr(dotPos + 1);
    } else {
        return "";
    }
}

bool HasExtension(const std::string& filePath, const std::string& extension)
{
    return GetFileExtension(filePath) == extension;
}

bool HasCJDExtension(std::string_view path)
{
    auto fileIt = path.rbegin();
    auto fileEnd = path.rend();
    auto extensionIt = CJ_D_FILE_EXTENSION.rbegin();
    auto extensionEnd = CJ_D_FILE_EXTENSION.rend();
    while (fileIt != fileEnd && extensionIt != extensionEnd) {
        if (*fileIt != *extensionIt) {
            return false;
        }
        ++fileIt;
        ++extensionIt;
    }
    // exclude .cj.d file with no filename
    return extensionIt == extensionEnd && fileIt != fileEnd;
}

std::optional<size_t> GetFileExtensionSeparatorPos(const std::string& filePath)
{
    auto dotOpt = FindLastOfDot(filePath);
    // e.g. 'out' or '.out'
    if (!dotOpt.has_value() || dotOpt.value() == 0) {
        return std::nullopt;
    }
    size_t dotPos = dotOpt.value();

    auto dirSplitOpt = FindLastOfDirSplit(filePath);
    if (dirSplitOpt.has_value()) {
        auto dirSplitPos = dirSplitOpt.value();
        // e.g. '/path/.out' or '.vscode/out'
        if ((dotPos == dirSplitPos + 1) || (dirSplitPos > dotPos)) {
            return std::nullopt;
        }
    }
    return {dotPos};
}

std::string GetFileNameWithoutExtension(const std::string& filePath)
{
    auto posOpt = FindLastOfDot(filePath);
    size_t dotPos = posOpt.value_or(filePath.size());

    posOpt = FindLastOfDirSplit(filePath);
    size_t dirSplitPos = posOpt ? posOpt.value() + 1 : 0;
    // consider the filename starting with a dot and without extension, e.g. /path/.out or .out
    if (dotPos == dirSplitPos) {
        dotPos = filePath.size();
    }

    return filePath.substr(dirSplitPos, dotPos - dirSplitPos);
}

std::string GetFileBase(const std::string& filePath)
{
    auto dotPos = GetFileExtensionSeparatorPos(filePath).value_or(filePath.size());
    return filePath.substr(0, dotPos);
}

std::string Normalize(const std::string& input, const bool toUnix)
{
    std::string output = "";
    output.reserve(input.size());
#ifdef _WIN32
    const char slash = toUnix ? SLASH : BACKSLASH;
#else
    (void)toUnix;
    const char slash = SLASH;
#endif
    bool previousIsSlash = false;
    for (auto c : input) {
        if (previousIsSlash && IsSlash(c)) {
            continue;
        }
        if (c == '\0') {
            break;
        }
        if (IsSlash(c)) {
            output.push_back(slash);
            previousIsSlash = true;
        } else {
            output.push_back(c);
            previousIsSlash = false;
        }
    }
    return output;
}

bool IsDir(const std::string& filePath)
{
#ifdef _WIN32
    DWORD dwAttr = GetFileAttributesA(filePath.c_str());
    if (dwAttr == INVALID_FILE_ATTRIBUTES) {
        return false;
    }
    if ((dwAttr & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return true;
    }
#else
    // If we have no read permission to the file path, we will not be able to open it for
    // checking whether it is a directory. Thus in such case we use stat instead of opendir.
    // Otherwise we will get `false` falsely on directories which have no read permissions.
    if (AccessWithResult(filePath, FM_READ) == AccessResultType::NO_PERMISSION) {
        struct stat statbuf;
        if (stat(filePath.c_str(), &statbuf) != 0) {
            return false;
        }
        return S_ISDIR(statbuf.st_mode);
    }
    DIR* dp = opendir(filePath.c_str());
    if (dp != nullptr) {
        closedir(dp);
        return true;
    }
#endif
    return false;
}

bool IsAbsolutePath(const std::string& filePath)
{
    if (filePath.empty()) {
        return false;
    }
#ifdef _WIN32
    // 3 is the length of an absolute path, such as: `c:\\`.
    if (filePath.size() < 3) {
        return false;
    }
    // In an absolute path on windows:
    //   position 0 means volume or drive letter
    //   position 1 means volume separator `:`
    //   Position 2 means a directory separator character `\\` or `/`.
    return std::isalpha(filePath[0]) && filePath[1] == ':' && filePath.find_first_of(DIR_SEPARATOR) == 2;
#else
    return filePath.find_first_of(DIR_SEPARATOR) == 0;
#endif
}

bool Remove(const std::string& itemPath)
{
#ifdef _WIN32
    DWORD dwAttr = GetFileAttributesA(itemPath.c_str());
    if (dwAttr == INVALID_FILE_ATTRIBUTES) {
        return false;
    }
    if ((dwAttr & FILE_ATTRIBUTE_READONLY) == 1 || (dwAttr & FILE_ATTRIBUTE_SYSTEM) == 1 ||
        (dwAttr & FILE_ATTRIBUTE_DEVICE) == 1 || (dwAttr & FILE_ATTRIBUTE_VIRTUAL) == 1) {
        return false;
    }
    return (IsDir(itemPath) ? _rmdir(itemPath.c_str()) : remove(itemPath.c_str())) == 0;
#else
    struct stat statBuf;
    if (lstat(itemPath.c_str(), &statBuf) != 0) {
        return false;
    }
    if (!S_ISREG(statBuf.st_mode) && !S_ISDIR(statBuf.st_mode) && !S_ISLNK(statBuf.st_mode)) {
        return false;
    }
    return remove(itemPath.c_str()) == 0;
#endif
}

bool ReadBinaryFileToBuffer(
    const std::string& filePath, std::vector<uint8_t>& buffer, std::string& failedReason)
{
    failedReason.clear();
    std::ifstream is(filePath, std::ifstream::in | std::ifstream::binary);
    if (!is.is_open()) {
        failedReason = "open file failed";
        return false;
    }
    size_t fileLength = GetFileSize(filePath);
    if (fileLength == 0) {
        failedReason = "empty binary file";
    }
    if (fileLength >= FILE_LEN_LIMIT) {
        failedReason = "exceed the max file length: 4 GB";
    }
    if (!failedReason.empty()) {
        is.close();
        return false;
    }
    buffer.resize(fileLength);
    is.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(fileLength));
    size_t realLen = static_cast<size_t>(is.gcount());
    if (is.bad()) {
        failedReason = "only " + std::to_string(realLen) + " could be read";
    } else if (is.eof() || realLen < fileLength) {
        buffer.resize(realLen);
    }
    is.close();
    return failedReason.empty();
}

bool WriteToFile(const std::string& filePath, const std::string& data)
{
    if (!FileExist(filePath)) {
        if (CreateDirs(filePath) != 0) {
            Errorln("Failed to create file " + filePath);
            return false;
        }
    }
    std::string normalizedPath = Normalize(filePath);
    std::ofstream file(normalizedPath);
    if (!file.is_open()) {
        Errorln("Failed to open file " + normalizedPath);
        return false;
    }

    file << data;

    bool didSucceed = !file.fail();
    if (!didSucceed) {
        Errorln("Failed to write to file " + normalizedPath);
    }
    file.close();

    return didSucceed;
}

bool WriteBufferToASTFile(const std::string& filePath, const std::vector<uint8_t>& buffer)
{
    std::string baseDir = GetDirPath(filePath);
    if (!FileExist(baseDir)) {
        if (CreateDirs(baseDir + "/") != 0) {
            return false;
        }
    }
    std::ofstream outStream(filePath, std::ofstream::out | std::ofstream::binary);
    if (!outStream.is_open()) {
        return false;
    }
    auto length = buffer.size();
    outStream.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(length));
    bool success = !outStream.fail();
    outStream.close();
    return success;
}

std::optional<std::string> ReadFileContent(const std::string& filePath, std::string& failedReason)
{
    failedReason.clear();
    if (!FileExist(filePath)) {
        failedReason = "file not exist";
        return std::nullopt;
    }
    std::ifstream is(filePath, std::ios::in | std::ios::binary);
    if (!is) {
        failedReason = "open file failed";
        return std::nullopt;
    }
    size_t len = GetFileSize(filePath);
    if (len >= FILE_LEN_LIMIT) {
        failedReason = "exceed the max file length: 4 GB";
        is.close();
        return std::nullopt;
    }
    std::string content;
    // Check BOM UTF-8 EF BB BF.
    if (len >= BOM_ENCODING_LEN) {
        content.resize(BOM_ENCODING_LEN);
        is.read(content.data(), BOM_ENCODING_LEN);
        // 0, 1, and 2 respectively indicate the three bytes of the byte order mark (BOM) in UTF-8.
        if (content[0] == '\xEF' && content[1] == '\xBB' && content[2] == '\xBF') {
            is.seekg(BOM_ENCODING_LEN, std::ifstream::beg);
            len -= BOM_ENCODING_LEN;
        } else {
            is.seekg(0, std::ifstream::beg);
        }
    }
    content.resize(len);
    is.read(content.data(), static_cast<std::streamsize>(len));
    size_t realLen = static_cast<size_t>(is.gcount());
    if (is.bad()) {
        failedReason = "only " + std::to_string(realLen) + " could be read";
    } else if (is.eof() || realLen < len) {
        content.resize(realLen);
    }
    is.close();
    if (!failedReason.empty()) {
        return std::nullopt;
    }
    return content;
}

std::string AppendExtension(const std::string& file, const std::string& backup, const std::string& extension)
{
    std::string result = GetFileBase(file);
    return (result.empty() ? (backup + extension) : (result + extension));
}

bool IsAbsolutePathAboveLengthLimit(const std::string& path)
{
    if (path.length() > PATH_MAX) {
        return true;
    }
#ifdef _WIN32
    char resolvedPath[PATH_MAX] = {0};
    int retval = GetFullPathNameA(path.c_str(), PATH_MAX, resolvedPath, NULL);
    // Error code 206 (ERROR_FILENAME_EXCED_RANGE) indicates that the filename or extension is too long.
    if (retval == 0 && GetLastError() == ERROR_FILENAME_EXCED_RANGE) {
        return true;
    }
#endif
    return false;
}

std::optional<std::string> GetAbsPath(const std::string& path)
{
    if (path.length() > PATH_MAX) {
        return {};
    }
    char resolvedPath[PATH_MAX] = {0};
#ifdef _WIN32
    int retval = GetFullPathNameA(path.c_str(), PATH_MAX, resolvedPath, NULL);
    if (retval == 0) {
        return {};
    }
    if (!FileExist(resolvedPath)) {
        return {};
    }
#else
    if (!realpath(path.c_str(), resolvedPath)) {
        return {};
    }
#endif
    return Normalize(std::string(resolvedPath));
}

std::optional<std::string> GetRelativePath(const std::string& basePath, const std::string& path)
{
    if (basePath.empty() || path.empty()) {
        return {};
    }
    std::string::size_type lastDelimPos = 0;
    std::string::size_type pos = 0;
    for (; pos != basePath.length() && pos != path.length() && basePath[pos] == path[pos]; ++pos) {
        if (IsSlash(basePath[pos])) {
            lastDelimPos = pos;
        }
    }

#ifdef _WIN32
    // The different volume case. The behavior is consistent with Windows' `PathRelativePathTo` API.
    if (lastDelimPos == 0 && !IsSlash(path[0])) {
        return {};
    }
#endif

    size_t parentCount = static_cast<size_t>(
        std::count_if(basePath.begin() + static_cast<std::ptrdiff_t>(lastDelimPos + 1), basePath.end(), IsSlash));
    size_t suffixLength = (path.length() - lastDelimPos) - 1;

    // Special handling for pure parent directory
    if (pos == path.length() && !IsSlash(path.back())) {
        // To avoid integer overflow when parentCount == 0.
        parentCount = (parentCount == 0) ? 0 : (parentCount - 1);
        suffixLength = 0;
    }

    std::string result;
    if (parentCount == 0) {
        // 2 is the length of `"./"`
        result.reserve(suffixLength + 2);
        result += "./";
    } else {
        // 3 is the length of `"../"`
        result.reserve(parentCount * 3 + suffixLength);
        for (size_t i = 0; i < parentCount; ++i) {
            result += "../";
        }
    }
    result.append(path, lastDelimPos + 1, suffixLength);
    return Normalize(result);
}

std::vector<std::string> GetAllDirsUnderCurrentPath(const std::string& path)
{
    std::string root = Normalize(path);
    std::vector<std::string> allDirs;
#ifdef _WIN32
    GetAllDirsUnderCurrentPath(root, allDirs);
#else
    std::queue<std::string> pathQueue;
    pathQueue.push(root);
    while (!pathQueue.empty()) {
        auto tmpPath = pathQueue.front();
        pathQueue.pop();
        DIR* dir = opendir(tmpPath.c_str());
        if (!dir) {
            return allDirs;
        }
        for (auto entry = readdir(dir); entry != nullptr; entry = readdir(dir)) {
            if (entry->d_type != DT_DIR) {
                continue;
            }
            auto dirName = std::string(entry->d_name);
            // Not include "." and "..".
            if (dirName != "." && dirName != "..") {
                allDirs.push_back(JoinPath(tmpPath, dirName));
                pathQueue.push(JoinPath(tmpPath, dirName));
            }
        }
        closedir(dir);
    }
#endif
    return allDirs;
}

size_t GetFileSize(const std::string& filePath)
{
    // `tellg` does not report the size of the file.
    // Though it usually works, it may fail.
#ifdef _WIN32
    struct __stat64 statBuf;
    int rc = _stat64(filePath.c_str(), &statBuf);
    return rc == 0 ? static_cast<size_t>(statBuf.st_size) : 0;
#else
    struct stat statBuf;
    int rc = stat(filePath.c_str(), &statBuf);
    return rc == 0 ? static_cast<size_t>(statBuf.st_size) : 0;
#endif
}

inline bool AddFileIfNeeded(
    const std::string& fileName, std::vector<std::string>& allFiles,
    bool shouldSkipTestFiles, bool shouldSkipRegularFiles)
{
    auto findTestSuffix = fileName.rfind(TEST_FILE_NAME);
    // Not compile test, and file is ending with '_test.cj'.
    if (shouldSkipTestFiles && findTestSuffix != std::string::npos &&
        fileName.size() - findTestSuffix == TEST_FILE_NAME.size()) {
        return false;
    } else if (shouldSkipRegularFiles && findTestSuffix == std::string::npos) {
        return false;
    }
    allFiles.emplace_back(fileName);
    return true;
}

#ifdef _WIN32
void GetAllDirsUnderCurrentPath(const std::string& dir, std::vector<std::string>& allFiles)
{
    HANDLE hFind;
    WIN32_FIND_DATA findData;
    std::string dirNew = JoinPath(dir, "*.*");
    hFind = FindFirstFileA(dirNew.c_str(), &findData);
    do {
        if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && strcmp(findData.cFileName, "") != 0 &&
            strcmp(findData.cFileName, ".") != 0 && strcmp(findData.cFileName, "..") != 0) {
            dirNew = JoinPath(dir, std::string(findData.cFileName));
            allFiles.push_back(dirNew);
            GetAllDirsUnderCurrentPath(dirNew, allFiles);
        }
    } while (FindNextFile(hFind, &findData));
    FindClose(hFind);
}

std::vector<std::string> GetAllFilesUnderCurrentPath(
    const std::string& path, const std::string& extension, bool shouldSkipTestFiles, bool shouldSkipRegularFiles)
{
    std::vector<std::string> allFiles;
    HANDLE hFind;
    WIN32_FIND_DATAA findData;
    std::string dirNew = JoinPath(path, "*.*");
    hFind = FindFirstFileA(dirNew.c_str(), &findData);
    do {
        auto fileName = std::string(findData.cFileName);
        bool isFile = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 &&
            strcmp(findData.cFileName, ".") != 0 && HasExtension(fileName, extension);
        if (!isFile) {
            continue;
        }
        if (!AddFileIfNeeded(fileName, allFiles, shouldSkipTestFiles, shouldSkipRegularFiles)) {
            continue;
        }
    } while (FindNextFileA(hFind, &findData));
    FindClose(hFind);
    return allFiles;
}

std::vector<Directory> GetDirectories(const std::string& path)
{
    std::vector<Directory> directories;
    HANDLE hFind;
    WIN32_FIND_DATAA findData;
    std::string dirNew = JoinPath(path, "*");
    hFind = FindFirstFileA(dirNew.c_str(), &findData);
    do {
        if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && strcmp(findData.cFileName, "") != 0 &&
            strcmp(findData.cFileName, ".") != 0 && strcmp(findData.cFileName, "..") != 0) {
            Directory d;
            d.name = findData.cFileName;
            d.path = JoinPath(path, std::string(findData.cFileName));
            directories.emplace_back(d);
        }
    } while (FindNextFileA(hFind, &findData));
    FindClose(hFind);
    return directories;
}
#else
std::vector<std::string> GetAllFilesUnderCurrentPath(
    const std::string& path, const std::string& extension,
    bool shouldSkipTestFiles, bool shouldSkipRegularFiles)
{
    std::vector<std::string> allFiles;
    DIR* dir = opendir(path.c_str());
    if (!dir) {
        return allFiles;
    }
    for (auto entry = readdir(dir); entry != nullptr; entry = readdir(dir)) {
        if (entry->d_type != DT_REG) {
            continue;
        }
        // File name cannot start with '.'.
        std::string fileName(entry->d_name);
        if (fileName[0] == '.' || !HasExtension(fileName, extension)) {
            continue;
        }
        if (!AddFileIfNeeded(fileName, allFiles, shouldSkipTestFiles, shouldSkipRegularFiles)) {
            continue;
        }
    }
    closedir(dir);
    return allFiles;
}

std::vector<Directory> GetDirectories(const std::string& path)
{
    std::vector<Directory> directories;
    struct dirent* entry;
    DIR* dir = opendir(path.c_str());
    if (dir) {
        while ((entry = readdir(dir)) != nullptr) {
            // get directories and symbolic links (they may refer to directories) only
            // In the future: we may need to check if symbolic links are referring to dir or not
            if (entry->d_type != DT_DIR && entry->d_type != DT_LNK) {
                continue;
            }
            // "." and ".." are also directories, but we get directories in 'path' only
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            Directory d;
            d.name = entry->d_name;
            d.path = JoinPath(path, entry->d_name);
            (void)directories.emplace_back(d);
        }
        closedir(dir);
    }
    return directories;
}
#endif

std::string JoinPath(const std::string& base, const std::string& append)
{
    std::string joinedPath = base;
    if (base.find_last_of(DIR_SEPARATOR) != base.size() - 1) {
        joinedPath += DIR_SEPARATOR[0];
    }
    joinedPath += append;
    return Normalize(joinedPath);
}

std::string GetShortHash(uint64_t fullHash)
{
    std::string hash = std::to_string(fullHash);
    return hash.size() <= SHORT_HASH_NUM ? hash : hash.substr(0, SHORT_HASH_NUM);
}

std::vector<std::string> SplitStr(const std::string& str, const char del)
{
    std::vector<std::string> res;
    size_t slow = 0;
    size_t fast = 0;
    size_t len = str.size();
    while (slow < len && fast < len) {
        if (str[fast] == del && str[slow] == del) {
            slow = ++fast;
        } else if (str[fast] != del && str[slow] != del) {
            fast++;
        } else if (str[fast] == del && str[slow] != del) {
            res.emplace_back(str.substr(slow, fast - slow));
            slow = fast;
        }
    }
    if (slow != fast) {
        res.emplace_back(str.substr(slow, fast));
    }
    return res;
}

std::string IdenticalFunc(const std::string& str)
{
    return str;
}

std::string GetPkgNameFromRelativePath(const std::string& path)
{
    std::string packageName = Normalize(path);
    size_t pos = 0;
    // The path containing '../' is not supported, and return "default".
    if (packageName.find(UPPERLEVEL_PATH) != std::string::npos) {
        return DEFAULT_PACKAGE_NAME;
    }
    while ((pos = packageName.find(CURRENT_PATH, pos)) != std::string::npos) {
        packageName.replace(pos, CURRENT_PATH.size(), "");
    }
    pos = 0;
    while ((pos = packageName.find_first_of(DIR_SEPARATOR, pos)) != std::string::npos) {
        // 1 is size of "\\" or "/".
        packageName.replace(pos, 1, ".");
        pos += 1;
    }
    while (!packageName.empty() && packageName.back() == '.') {
        packageName.pop_back();
    }
    return packageName.empty() ? DEFAULT_PACKAGE_NAME : packageName;
}

namespace {
inline bool IsKeyword(const std::string& str)
{
    static std::unordered_set<std::string> keywordsSet{};
    if (keywordsSet.empty()) {
        for (unsigned char i = 0; i < static_cast<unsigned char>(TokenKind::IDENTIFIER); i++) {
            keywordsSet.emplace(std::string(TOKENS[i]));
        }
        keywordsSet.emplace("true");
        keywordsSet.emplace("false");
    }
    return keywordsSet.count(str) != 0;
}
} // namespace

using namespace Unicode;
bool IsIdentifier(const std::string& str, bool useContextualKeyWords)
{
    if (str.empty()) {
        return false;
    }
    auto start{reinterpret_cast<const UTF8*>(str.data())};
    auto end{reinterpret_cast<const UTF8*>(str.data() + str.size())};

    if (str[0] == BACKTICK) {
        ++start;
        --end;
        // '3' means original id can't be empty.
        if ((str.size() < 3 || *(str.end() - 1) != BACKTICK)) {
            return false;
        }
    }

    bool isFirst{true};
    UTF32 codepoint;
    UTF32* targetEnd{&codepoint + 1};
    while (start < end) {
        UTF32* targetStart{&codepoint};
        auto cr = ConvertUTF8toUTF32(&start, end, &targetStart, targetEnd);
        if (cr != ConversionResult::OK) {
            return false;
        }
        if (isFirst) {
            if (!IsASCIIIdStart(codepoint) && !IsCJXIDStart(codepoint)) {
                return false;
            }
        } else if (!IsASCIIIdContinue(codepoint) && !IsXIDContinue(codepoint)) {
            return false;
        }
        isFirst = false;
    }
    if (IsKeyword(str)) {
        if (useContextualKeyWords && IsContextualKeyword(str)) {
            return true;
        }
        return false;
    }
    return true;
}

std::string NormalizePath(const std::string& path)
{
    if (path.empty()) {
        return path;
    }
    if (GetDirPath(path) == ".") {
        return JoinPath(".", GetFileName(path));
    }
    return Normalize(path);
}

std::string ConvertPackageNameToLibCangjieBaseFormat(const std::string& fullPackageName)
{
    if (auto found = STANDARD_LIBS.find(fullPackageName); found != STANDARD_LIBS.end()) {
        auto names = Utils::SplitQualifiedName(fullPackageName);
        if (names.size() == 1) {
            return "cangjie-" + found->second;
        }
        auto moduleName = names.front();
        return "cangjie-" + moduleName + "-" + found->second;
    }
    return {};
}

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
std::string ConvertFilenameToLibCangjieBaseFormat(const std::string& objectFile)
{
    auto fullPackageName = GetFileNameWithoutExtension(objectFile);
    return ConvertPackageNameToLibCangjieBaseFormat(fullPackageName);
}

std::string ConvertFilenameToLibCangjieFormat(
    const std::string& objectFile, const std::string& extension)
{
    auto fullPackageName = GetFileNameWithoutExtension(objectFile);
    auto base = ConvertPackageNameToLibCangjieBaseFormat(fullPackageName);
    return base.empty() ? std::string() : "lib" + base + extension;
}
#endif

std::string ConvertFilenameToLtoLibCangjieFormat(const std::string& objectFile)
{
    auto dotOptional = FindLastOfDot(objectFile);
    auto dirSplitOptional = FindLastOfDirSplit(objectFile);
    size_t dotPos = dotOptional.value_or(objectFile.size());
    size_t dirSplitPos = dirSplitOptional.value_or(0);
    std::string path = objectFile.substr(0, dirSplitPos);
    std::string libName = objectFile.substr(dirSplitPos + 1, dotPos - (dirSplitPos + 1));
    std::stringstream ss;
    ss << path << DIR_SEPARATOR << "lib" << libName;
    ss << ".bc";
    return ss.str();
}

AccessResultType AccessWithResult(const std::string& path, FileMode mode)
{
#ifdef _WIN32
    return Access(path, mode) ? AccessResultType::OK : AccessResultType::FAILED_WITH_UNKNOWN_REASON;
#else
    if (access(path.c_str(), static_cast<int>(mode)) == 0) {
        return AccessResultType::OK;
    }
    if (errno == ENOENT) {
        return AccessResultType::NOT_EXIST;
    } else if (errno == EACCES) {
        return AccessResultType::NO_PERMISSION;
    }
    return AccessResultType::FAILED_WITH_UNKNOWN_REASON;
#endif
}

bool Access(const std::string& path, FileMode mode)
{
#ifdef _WIN32
    return _access(path.c_str(), static_cast<int>(mode)) == 0;
#else
    return access(path.c_str(), static_cast<int>(mode)) == 0;
#endif
}

bool FileExist(const std::string& path, [[maybe_unused]] bool caseSensitive)
{
#ifdef _WIN32
    if (!Access(path, FM_EXIST)) {
        return false;
    }
    if (!caseSensitive) {
        return true;
    }
    std::string pathWithoutSep = path;
    if (auto pos = path.find_last_not_of(DIR_SEPARATOR); pos != std::string::npos) {
        pathWithoutSep = path.substr(0, pos + 1);
    }
    const std::string filename = GetFileName(pathWithoutSep);
    HANDLE hFind;
    WIN32_FIND_DATAA findData;
    std::string dirNew = JoinPath(GetDirPath(pathWithoutSep), "*.*");
    hFind = FindFirstFileA(dirNew.c_str(), &findData);
    do {
        if (filename == findData.cFileName) {
            FindClose(hFind);
            return true;
        }
    } while (FindNextFileA(hFind, &findData));
    FindClose(hFind);
    return false;
#else
    return Access(path, FM_EXIST);
#endif
}

std::optional<std::string> FindFileByName(
    const std::string& fileName, const std::vector<std::string>& searchPaths, bool caseSensitive)
{
    if (fileName.empty()) {
        return std::nullopt;
    }
    for (const auto& path : searchPaths) {
        if (path.empty()) {
            continue;
        }
        auto filepath = JoinPath(path, fileName);
        if (FileExist(filepath, caseSensitive)) {
            return {filepath};
        }
    }
    return std::nullopt;
}

std::string FindProgramByName(const std::string& name, const std::vector<std::string>& paths)
{
    if (name.empty()) {
        return "";
    }

    if (HasDirSplit(name)) {
        return name;
    }

    std::string exePath;
    for (const auto& path : paths) {
        if (path.empty()) {
            continue;
        }

        exePath = AppendPath(path, name);
        if (CanExecute(exePath)) {
            return exePath;
        }
    }

    return "";
}

int32_t CreateDirs(const std::string& directoryPath)
{
    size_t dirPathLen = directoryPath.length();
    if (dirPathLen > DIR_PATH_MAX_LENGTH) {
        std::string msg = "Failed to create directory: " + directoryPath + "\nthe path cannot be longer than " +
            std::to_string(DIR_PATH_MAX_LENGTH) + " characters.";
        Errorln(msg);
        return -1;
    }
    char tmpDirPath[DIR_PATH_MAX_LENGTH + 1] = {0};
    size_t fileNameStartIdx = 0;
    for (size_t i = 0; i < dirPathLen; ++i) {
        tmpDirPath[i] = directoryPath[i];
#ifdef _WIN32
        if (!Utils::In(tmpDirPath[i], {BACKSLASH, SLASH})) {
#elif defined(__linux__) || defined(__APPLE__)
        if (tmpDirPath[i] != SLASH) {
#endif
            continue;
        }
        if (i - fileNameStartIdx > FILE_NAME_MAX_LENGTH) {
            std::string msg = "Failed to create directory: " + std::string(tmpDirPath)
                + "\nthe directory name cannot be longer than "
                + std::to_string(FILE_NAME_MAX_LENGTH) + " characters.";
            Errorln(msg);
            return -1;
        }
        fileNameStartIdx = i + 1; // current 'i' is slash, so file name start index is next char
        if (Access(tmpDirPath, FM_EXIST)) {
            continue;
        }
        // The path may be created by another thread or process after the first access before reaching this line.
        // In that case, the path already exists and cannot be successfully created. As the required folder is created
        // anyway in this case, do not return a failure exit code by checking the existence of the folder again.
        int32_t ret = Mkdir(tmpDirPath);
        if (ret != 0 && !Access(tmpDirPath, FM_EXIST)) {
            return ret;
        }
    }
    return 0;
}

std::string FindSerializationFile(
    const std::string& fullPackageName, const std::string& suffix, const std::vector<std::string>& searchPaths)
{
    auto names = Utils::SplitQualifiedName(fullPackageName);
    auto fileName = ToCjoFileName(names.front()) + DIR_SEPARATOR + fullPackageName;
    auto filePath = FileUtil::FindFileByName(fileName + suffix, searchPaths, true).value_or("");
    if (filePath.empty()) {
        // TEMPORARILY, find file in direct path.
        return FileUtil::FindFileByName(fullPackageName + suffix, searchPaths, true).value_or("");
    }
    return filePath;
}

std::vector<std::string> SplitEnvironmentPaths(const std::string& pathsString)
{
#ifdef _WIN32
    const std::string separator = ";";
#else
    const std::string separator = ":";
#endif
    auto strings = Utils::SplitString(pathsString, separator);
    std::vector<std::string> paths;
    for (const std::string& s : strings) {
        // remove empty strings from paths.
        if (s.empty()) {
            continue;
        }
        auto maybePath = GetAbsPath(s);
        if (maybePath.has_value()) {
            paths.emplace_back(maybePath.value());
        }
    }
    return paths;
}

bool CanWrite(const std::string& path)
{
#ifdef _WIN32
    std::optional<std::wstring> tempPath = StringConvertor::StringToWString(path);
    if (!tempPath.has_value()) {
        return false;
    }
    std::wstring wPath = tempPath.value();
    WIN32_FILE_ATTRIBUTE_DATA wfad;
    BOOL res = GetFileAttributesExW(wPath.c_str(), GetFileExInfoStandard, &wfad);
    if (!res) {
        return false;
    }
    return (wfad.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != FILE_ATTRIBUTE_READONLY;
#else
    return access(path.c_str(), W_OK) == 0;
#endif
}

std::string RemovePathPrefix(const std::string& absolutePath, const std::vector<std::string>& removedPathPrefix)
{
    // for the path: aa/bb/cc.cj, the absolutePath is "aa/bb", and the last "/" is need to be added.
#ifdef _WIN32
    char split = '\\';
    std::string res = absolutePath + split;
    std::string tempRes{};
    tempRes.reserve(res.size());
    std::transform(res.begin(), res.end(), std::back_inserter(tempRes), &tolower);
    for (auto prefix : removedPathPrefix) {
        std::transform(prefix.begin(), prefix.end(), prefix.begin(), &tolower);
        auto pos = tempRes.find(prefix);
#else
    char split = '/';
    std::string res = absolutePath + split;
    for (auto& prefix : removedPathPrefix) {
        auto pos = res.find(prefix);
#endif
        // when pos is 0, it means the path starts with the prefix.
        if (pos == 0) {
            res = res.substr(prefix.length());
            if (res.empty()) {
                return res;
            }
            res = res[0] == split ? res.substr(1) : res;
            break;
        }
    }
    // remove the last symbol.
    return res.empty() ? res : res.substr(0, res.size() - 1);
}

#ifdef _WIN32
void HideFile(const std::string& path)
{
    (void)SetFileAttributesA(path.c_str(), FILE_ATTRIBUTE_HIDDEN | GetFileAttributesA(path.c_str()));
}
#endif

std::string ToCjoFileName(std::string_view fullPackageName)
{
    std::string ret{};
    size_t i{0};
    for (; i < fullPackageName.size(); ++i) {
        // replace first :: with # as organization name separator
        if (fullPackageName[i] == ':' && i + 1 < fullPackageName.size() && fullPackageName[i + 1] == ':') {
            ret += ORG_NAME_SEPARATOR;
            i += strlen(TOKENS[static_cast<int>(TokenKind::DOUBLE_COLON)]);
            break;
        }
        if (fullPackageName[i] == '.') {
            break;
        }
        ret.push_back(fullPackageName[i]);
    }
    if (i < fullPackageName.size()) {
        ret.insert(ret.end(), fullPackageName.begin() + i, fullPackageName.end());
    }
    return ret;
}
 
std::string ToPackageName(std::string_view cjoName)
{
    auto it = cjoName.find(ORG_NAME_SEPARATOR);
    if (it != std::string::npos) {
        return std::string{cjoName.substr(0, it)} + TOKENS[static_cast<int>(TokenKind::DOUBLE_COLON)] +
            std::string{cjoName.substr(it + 1)};
    }
    return std::string{cjoName};
}
} // namespace Cangjie::FileUtil
