// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements some utility functions.
 */

#include "cangjie/Basic/Utils.h"
#include "cangjie/Lex/Token.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include "cangjie/Utils/CheckUtils.h"

using namespace Cangjie;

namespace Cangjie {
uint64_t Utils::GetHash(const std::string& content)
{
    std::size_t ret = std::hash<std::string>{}(content);
    return static_cast<uint64_t>(ret);
}

std::vector<std::string> Utils::SplitLines(const std::string& str)
{
    std::vector<std::string> res;
    size_t length = str.size();
    if (length == 0) {
        return res;
    }
    size_t lineTerminatorPos;
    size_t windowsTerminatorLength = std::string("\r\n").size();
    for (size_t curIndex = 0, newLineStartPos = 0; curIndex < length;) {
        while (curIndex < length && !(str[curIndex] == '\r' && curIndex + 1 < length && str[curIndex + 1] == '\n') &&
            str[curIndex] != '\n') {
            curIndex++;
        }
        lineTerminatorPos = curIndex;
        if (curIndex < length) {
            if (str[curIndex] == '\r' && curIndex + 1 < length && str[curIndex + 1] == '\n') {
                curIndex += windowsTerminatorLength;
            } else {
                curIndex++;
            }
        }
        res.emplace_back(str.substr(newLineStartPos, lineTerminatorPos - newLineStartPos));
        newLineStartPos = curIndex;
    }
    if (str[length - 1] == '\n') {
        res.emplace_back("");
    }
    return res;
}

std::vector<std::string> Utils::SplitString(const std::string& str, const std::string& delimiter)
{
    std::vector<std::string> res;
    if (!str.empty()) {
        size_t pos = 0;
        size_t foundPos = str.find(delimiter);
        while (foundPos != std::string::npos) {
            res.emplace_back(str.substr(pos, foundPos - pos));
            pos = foundPos + 1;
            foundPos = str.find(delimiter, pos);
        }
        if (pos < str.size()) {
            res.emplace_back(str.substr(pos));
        } else if (pos == str.size()) {
            res.emplace_back("");
        }
    }
    return res;
}

std::vector<std::string> Utils::SplitQualifiedName(const std::string& qualifiedName, bool splitDc)
{
    std::vector<std::string> names;
    std::string::size_type idx = 0;
    std::string_view dc = TOKENS[static_cast<int>(TokenKind::DOUBLE_COLON)];
    if (auto next = qualifiedName.find(dc); splitDc && next != std::string::npos) {
        names.push_back(qualifiedName.substr(0, next));
        idx = next + dc.size();
    }
    while (idx < qualifiedName.size()) {
        if (qualifiedName[idx] == '.') {
            ++idx;
        } else if (qualifiedName[idx] == '`') {
            auto next = qualifiedName.find('`', idx + 1);
            names.emplace_back(qualifiedName.substr(idx + 1, next - (idx + 1)));
            idx = next == std::string::npos ? next : (next + 1);
        } else {
            auto next = qualifiedName.find('.', idx + 1);
            names.emplace_back(qualifiedName.substr(idx, next - idx));
            idx = next;
        }
    }
    CJC_ASSERT(!names.empty());
    return names;
}

std::string Utils::JoinStrings(const std::vector<std::string>& strs, const std::string& delimiter)
{
    std::string s;
    for (unsigned int i = 0; i < strs.size(); ++i) {
        s += strs[i];
        if (i != strs.size() - 1) {
            s += delimiter;
        }
    }
    return s;
}

uint8_t Utils::GetLineTerminatorLength(const char* pStr, const char* pEnd)
{
    if (pStr == nullptr || pEnd == nullptr) {
        return 0;
    }
    if (*pStr == '\n') {
        return LINUX_LINE_TERMINATOR_LENGTH;
    } else if (*pStr == '\r' && pStr + 1 <= pEnd && *(pStr + 1) == '\n') {
        return WINDOWS_LINE_TERMINATOR_LENGTH;
    } else {
        return 0;
    }
}

bool Utils::StartsWith(std::string_view str , std::string_view prefix)
{
    return str.size() >= prefix.size() && str.compare(0, prefix.size(), prefix) == 0;
}

#ifdef _WIN32
typedef NTSTATUS(WINAPI* RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);

Utils::WindowsOsVersion Utils::GetOSVersion()
{
    HMODULE hMod = GetModuleHandleA("ntdll.dll");
    RTL_OSVERSIONINFOW osVersionInfo = {0};
    if (hMod) {
        auto RtlGetVersion = (RtlGetVersionPtr)GetProcAddress(hMod, "RtlGetVersion");
        if (RtlGetVersion != nullptr) {
            osVersionInfo.dwOSVersionInfoSize = sizeof(osVersionInfo);
            RtlGetVersion(&osVersionInfo);
        }
    }

    return {osVersionInfo.dwMajorVersion, osVersionInfo.dwMinorVersion, osVersionInfo.dwBuildNumber,
        osVersionInfo.dwPlatformId};
}
#endif
} // namespace Cangjie
