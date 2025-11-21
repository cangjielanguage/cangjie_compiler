// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares file util related apis.
 */

#ifndef CANGJIE_UTILS_FILEUTIL_H
#define CANGJIE_UTILS_FILEUTIL_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#ifdef _MSC_VER
// Workaround for "combaseapi.h(229):
//     error C2187: syntax error: 'identifier' was unexpected here" when using /permissive-
struct IUnknown;
#include <io.h>
#define R_OK 4
#define W_OK 2
#define X_OK 0
#define F_OK 0
#define access _access
#else
#include <unistd.h>
#endif

#include "cangjie/Basic/Print.h"

namespace Cangjie {
class DiagnosticEngine;
const size_t FILE_NAME_MAX_LENGTH = 255;
const size_t FILE_LEN_LIMIT = 4ULL * 1024 * 1024 * 1024; // 4 GB

#ifdef _WIN32
const static std::string INJECTION_STRING = "^\";|&";
// On Windows, both dos and unix dir separators ('\\' and '/') are supported.
// To search for dir separators from a string, only methods similar to `find_last_of` and `find_first_of` can be used.
// `find` cannot be used.
const std::string DIR_SEPARATOR = "\\/";
const std::string CURRENT_PATH = ".\\";
const std::string UPPERLEVEL_PATH = "..\\";
const size_t FILE_PATH_MAX_LENGTH = 260;
const size_t DIR_PATH_MAX_LENGTH = 248;
#else
const static std::string INJECTION_STRING = "|;&$><\\!\n"; // Check ` otherwhere.
const std::string DIR_SEPARATOR = "/";
const std::string CURRENT_PATH = "./";
const std::string UPPERLEVEL_PATH = "../";
const size_t FILE_PATH_MAX_LENGTH = 4096;
const size_t DIR_PATH_MAX_LENGTH = 4096;
#endif

/**
 * Monad by overloading | as pipe operator.
 * @param F this function return option<value> or value.
 * @param opt option<value>.
 * @return the same as F.
 */
template <typename F>
auto operator|(const std::optional<std::string>& opt, F function) -> decltype(function(opt.value()))
{
    if (opt) {
        return function(*opt);
    } else {
        Errorln("fail to invoke system api");
        return {};
    }
}

namespace FileUtil {
/**
 * Check whether there is commandline injection in command.
 * @param cmd commandline string
 * @return true if found injection, else return false.
 */
bool CheckCommandLineInjection(const std::string& cmd);

/**
 * Escape Backtick by insert a backslash before the backtick
 * @param s commandline string.
 * @return string with escaped backtick.
 */
std::string TransferEscapeBacktick(const std::string& s);

/**
 * Recursively remove directory and its contents.
 * @param dirPath target directory.
 * @return true if directory removed successfully.
 */
bool RemoveDirectoryRecursively(const std::string& dirPath);

/**
 * Get file name from filePath.
 * @param filePath string like "path/file.extension".
 * @return file.extension.
 */
std::string GetFileName(const std::string& filePath);

/**
 * Get file name from filePath.
 * @param filePath string like "path1/path2" or ".".
 * @return path2 or real dir name.
 */
std::string GetDirName(const std::string& filePath);

/**
 * Get dir name from filePath.
 * @param filePath string like "path/file.extension".
 * @return dir path.
 */
std::string GetDirPath(const std::string& filePath);

/**
 * Get extension from filePath.
 * @param filePath string like "path/file.extension".
 * @return extension.
 */
std::string GetFileExtension(const std::string& filePath);

/**
 * Get the position of the dot character used as file extension separator in a file path.
 * @param filePath string like "path/file.extension".
 * @return the position of '.' between 'file' and 'extension'.
 */
std::optional<size_t> GetFileExtensionSeparatorPos(const std::string& filePath);

/**
 * Get name from filePath.
 * @param filePath string like "path/file.extension".
 * @return file.
 */
std::string GetFileNameWithoutExtension(const std::string& filePath);

/**
 * Judge whether file has some extension.
 * @param filePath string like "path/file.extension".
 * @param extension string like "extension".
 * @return true if match.
 */
bool HasExtension(const std::string& filePath, const std::string& extension);
bool HasCJDExtension(std::string_view path);

/**
 * Get file base from filePath.
 * @param filePath string like "path/file.extension".
 * @return path/file.
 */
std::string GetFileBase(const std::string& filePath);

/**
 * Get the input string quoted.
 * Double quotes in the input are escaped by backslash.
 * @param str.
 * @return quoted string.
 */
std::string GetQuoted(const std::string& str);

/**
 * Judge whether file is directory, using system api.
 * @param filePath filePath string can be relative path or absolute path.
 * @return true if filePath is directory.
 */
bool IsDir(const std::string& filePath);

/**
 * Check whether a path is an absolute path.
 * @param filePath
 * @return true if given path is an absoluate path.
 */
bool IsAbsolutePath(const std::string& filePath);

/**
 * Take a path to a file or an empty directory and try to remove the item.
 * @param itemPath path to a file or an empty directory
 * @return true if specified item is removed successfully
 */
bool Remove(const std::string& itemPath);

#ifdef _WIN32
void GetAllDirsUnderCurrentPath(const std::string& path, std::vector<std::string>& allFiles);
#endif

/**
 * Convert from windows '\' to unix '/', or reverse.
 * The conversion is performed only on Windows. Otherwise, the original string is returned.
 * If @p input contains '\0', the characters before it will be returned as the normalized path.
 *
 * @param input string to be converted
 * @param toUnix indicate transition from windows '\' to unix '/', or reverse.
 *
 * @return string after conversion on Windows, or the original string otherwise.
 */
std::string Normalize(const std::string& input, const bool toUnix = false);

/**
 * Read .ast file to buffer.
 * @param[in] filePath String like "path/file.ast".
 * @param[in] buffer Save .cjo file.
 * @param[out] failedReason Why read file to buffer failed.
 * @return whether func invoked successfully.
 */
bool ReadBinaryFileToBuffer(const std::string& filePath, std::vector<uint8_t>& buffer, std::string& failedReason);

/**
 * Get the size of the File.
 * @param Path to the file.
 * @return file sizse.
 */
size_t GetFileSize(const std::string& filePath);

/**
 * Read file and return its content.
 * @param[in] filePath String like "path/file.extension".
 * @param[out] failedReason Why read file to buffer failed.
 * @return content
 */
std::optional<std::string> ReadFileContent(const std::string& filePath, std::string& failedReason);

/**
 * Write data into file.
 * @param filePath string like "path/to/file".
 * @param data string to be written into file.
 * @return whether func invoked successfully.
 */
bool WriteToFile(const std::string& filePath, const std::string& data);

/**
 * Write buffer into .cjo file.
 * @param filePath string like "path/file.cjo".
 * @param buffer save .ast file.
 * @return whether func invoked successfully.
 */
bool WriteBufferToASTFile(const std::string& filePath, const std::vector<uint8_t>& buffer);

/**
 * Get relative path from basePath and normalize it with UNIX flavor delimiter.
 * @param basePath absolute path to be calculated from, like "path1/path3/[file.extension]".
 * Folder path must end with '/'.
 * @param path absolute path like "path1/path2/file.extension".
 * @return calculated relative path like "../path2/file.extension".
 */
std::optional<std::string> GetRelativePath(const std::string& basePath, const std::string& path);

/**
 * Append extension after file.
 * @param file string like "path/file".
 * @param backup used when file is empty.
 * @param extension string for appending.
 * @return string like "path/file.extension".
 */
std::string AppendExtension(const std::string& file, const std::string& backup, const std::string& extension);

/**
 * Get absolute path from relative path.
 * @param path relative path.
 * @return absolute path.
 */
std::optional<std::string> GetAbsPath(const std::string& path);

/**
 * Return whether the absolute path of the given path exceeds the system upper limit.
 * @param path relative or absolute path.
 * @return True if the absolute path exceeds the limit.
 */
bool IsAbsolutePathAboveLengthLimit(const std::string& path);

/**
 * Recursively get all directories under current path.
 * @param path root directory path.
 * @return all directories under current path.
 */
std::vector<std::string> GetAllDirsUnderCurrentPath(const std::string& path);

/**
 * Non-recursively get all files under current path.
 * @param path root directory path.
 * @param extension file extension for collection.
 * @param notSkipTest whether skip source file whose name is ending with '_test', some like 'xxx_test.cj'.
 * @return all files under current path.
 */
std::vector<std::string> GetAllFilesUnderCurrentPath(
    const std::string& path, const std::string& extension,
    bool shouldSkipTestFiles = true, bool shouldSkipRegularFiles = false);

/**
 * Join path and file, strict or no strict path is OK.
 * File separators on different platforms are considered.
 * @param base path at first.
 * @param append file at last.
 * @return path/file.
 */
std::string JoinPath(const std::string& base, const std::string& append);

/**
 * Get the shorter hash value of long hash value.
 * @param fullHash int value which record the
 * @return Shortened hash.
 */
std::string GetShortHash(uint64_t fullHash);

/**
 * Split string by given delimiter.
 * @param str string by split.
 * @param del delimiter.
 * @return strings get from str.
 */
std::vector<std::string> SplitStr(const std::string& str, const char del);

/**
 * Used for ending monad.
 * @param str str can be any string.
 * @return the same as str.
 */
std::string IdenticalFunc(const std::string& str);

/**
 * Get package name through file system mapping relationships.
 */
std::string GetPkgNameFromRelativePath(const std::string& path);

/**
 * Check whether a string is an identifier.
 * Identifier rule as below (not including keywords, but in some case contextual keywords are special strings that can
 * be used as an Identifier).
 * Ident : XID_Start XID_Continue* | '_' XID_Continue+
 *     ;
 * RawIdent
 *     : '`' Ident '`'
 *     ;
 * Identifier
 *     : Ident
 *     | RawIdent
 *     ;
 * @param str str can be any string.
 * @param useContextualKeyWords whether contextual keywords can be used.
 * @return whether str is an identifier
 */
bool IsIdentifier(const std::string& str, bool useContextualKeyWords = true);

std::string NormalizePath(const std::string& path);

/**
 * Convert package name 'std.pkg' to a form of 'cangjie-<std>-<pkg>'
 */
std::string ConvertPackageNameToLibCangjieBaseFormat(const std::string& fullPackageName);

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
/**
 * Convert filename '<file>.<ext>' to a form of 'cangjie-<file>'
 */
std::string ConvertFilenameToLibCangjieBaseFormat(const std::string& objectFile);

/**
 * Convert filename '<file>.<ext>' to a form of 'libcangjie-<file><.extension>'
 */
std::string ConvertFilenameToLibCangjieFormat(const std::string& objectFile, const std::string& extension);
#else
/**
 * Convert filename '<file>.<ext>' to a form of 'cangjie-<file><sanitizerSuffix>'
 */
std::string ConvertFilenameToLibCangjieBaseFormat(
    const std::string& objectFile, const std::string& sanitizerSuffix = "");

/**
 * Convert filename '<file>.<ext>' to a form of 'libcangjie-<file><sanitizerSuffix><.extension>'
 */
std::string ConvertFilenameToLibCangjieFormat(
    const std::string& objectFile, const std::string& extension, const std::string& sanitizerSuffix = "");
#endif

std::string ConvertFilenameToLtoLibCangjieFormat(const std::string& objectFile);

enum FileMode { FM_EXIST = F_OK, FM_EXE = X_OK, FM_WRITE = W_OK, FM_READ = R_OK };

enum class AccessResultType {
    OK,
    NOT_EXIST,
    NO_PERMISSION,
    FAILED_WITH_UNKNOWN_REASON
};

AccessResultType AccessWithResult(const std::string& path, FileMode mode);
bool Access(const std::string& path, FileMode mode);
/**
 * Check if @p path exists.
 * NOTE: @p caseSensitive is meaningful on windows, otherwise it will be ignored.
 */
bool FileExist(const std::string& path, bool caseSensitive = false);
/**
 * Search module by name and path for searching.
 * @param fileName file name
 * @param searchPaths paths for searching
 * @param caseSensitive indicates whether the search process is case sensitive, is meaningful only on windows
 * @return
 */
std::optional<std::string> FindFileByName(
    const std::string& fileName, const std::vector<std::string>& searchPaths, bool caseSensitive = false);

/**
 * Find Program by an executable name, like: ./cjc, cjc, and return the full path with the executable name.
 * Optionally can pass a list of paths, to find the full path.
 */
std::string FindProgramByName(const std::string& name, const std::vector<std::string>& paths);

/**
 * Search specific serialization file by packageName from searchPaths.
 * @param fullPackageName full package name
 * @param suffix file suffix of the serialization file
 * @param searchPaths paths for searching
 * @return
 */
std::string FindSerializationFile(
    const std::string& fullPackageName, const std::string& suffix, const std::vector<std::string>& searchPaths);

/**
 * Judge a directory exist or not, if not exit create it.
 * @param directoryPath string like "/path/directory/".
 * @return 0 : exist or create successful, -1 : create failed.
 */
int32_t CreateDirs(const std::string& directoryPath);
struct Directory {
    std::string path;
    std::string name;
};

std::vector<Directory> GetDirectories(const std::string& path);

std::vector<std::string> SplitEnvironmentPaths(const std::string& pathsString);

bool CanWrite(const std::string& path);

/**
 * Remove prefixes from an absolute path.
 */
std::string RemovePathPrefix(const std::string& absolutePath, const std::vector<std::string>& removedPathPrefix);

#ifdef _WIN32
// make a file or directory hidden
void HideFile(const std::string& path);
#endif
bool IsSlash(char c);
 
/// Get file name for a package when used as default temp/output file.
/// e.g. for package a::b.c, returns a#b.c
std::string ToCjoFileName(std::string_view fullPackageName);
std::string ToPackageName(std::string_view cjoName);
} // namespace FileUtil
} // namespace Cangjie
#endif // CANGJIE_UTILS_FILEUTIL_H
