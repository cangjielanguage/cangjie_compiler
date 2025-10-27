// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares some common utility functions.
 */

#ifndef CANGJIE_BASIC_UTILS_H
#define CANGJIE_BASIC_UTILS_H

#include <stdint.h>
#include <string>
#include <vector>
#include <cstdint>

namespace Cangjie::Utils {

const uint8_t LINUX_LINE_TERMINATOR_LENGTH = 1;
const uint8_t WINDOWS_LINE_TERMINATOR_LENGTH = 2;

/// Get hash value by std::hash<std::string>.
uint64_t GetHash(const std::string& content);
/// Split a string by '\r\n' and '\n' to form a vector of strings.
std::vector<std::string> SplitLines(const std::string& str);
/// Split a string by customised \ref delimiter. Typically used in command line arguments parsing.
std::vector<std::string> SplitString(const std::string& str, const std::string& delimiter);
/// \param splitDc whether to split by '::' as well.
std::vector<std::string> SplitQualifiedName(const std::string& qualifiedName, bool splitDc = false);
/// Join strings \ref strs with \ref delimiter.
std::string JoinStrings(const std::vector<std::string>& strs, const std::string& delimiter);

/**
 * check whether character(s) start from pStr is `LineTerminator`
 * if it is a Windows LineTerminator '\r\n', return 2
 * if it is a Linux LineTerminator '\n', return 1
 * otherwise, 0 is returned.
 */
uint8_t GetLineTerminatorLength(const char* pStr, const char* pEnd);

#ifdef _WIN32
struct WindowsOsVersion {
    unsigned long dwMajorVersion;
    unsigned long dwMinorVersion;
    unsigned long dwBuildNumber;
    unsigned long dwPlatformId;
};

WindowsOsVersion GetOSVersion();
#endif

inline std::string GetLineTerminator()
{
#ifdef _WIN32
    return "\r\n";
#else
    return "\n";
#endif
}
} // namespace Cangjie::Utils

#endif
