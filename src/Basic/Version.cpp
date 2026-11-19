// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements compiler's version apis.
 */

#include "cangjie/Basic/Print.h"
#include "cangjie/Basic/Version.h"

#include <exception>
#include <regex>

namespace Cangjie {
namespace {
// Sub-match indexes of the version pattern in ParseVersion, i.e. the major, minor and patch number groups.
constexpr size_t REGEX_MAJOR_IDX = 1;
constexpr size_t REGEX_MINOR_IDX = 2;
constexpr size_t REGEX_PATCH_IDX = 3;
// Each version number is limited to at most two digits, i.e. [0, 99].
constexpr uint32_t VERSION_NUM_MIN = 0;
constexpr uint32_t VERSION_NUM_MAX = 99;

bool IsValidVersionNumber(uint32_t num)
{
    return num >= VERSION_NUM_MIN && num <= VERSION_NUM_MAX;
}
} // namespace

VersionInfo ParseVersion(const std::string& version, bool matchPatch, bool matchSuffix)
{
    VersionInfo info;
    // The patch number and the suffix are optional parts of the accepted syntax, so the pattern has four variants.
    static const std::regex patchWithSuffixPattern("^([0-9]+)[.]([0-9]+)(?:[.]([0-9]+))?");
    static const std::regex patchExactPattern("^([0-9]+)[.]([0-9]+)(?:[.]([0-9]+))?$");
    static const std::regex minorWithSuffixPattern("^([0-9]+)[.]([0-9]+)");
    static const std::regex minorExactPattern("^([0-9]+)[.]([0-9]+)$");
    const std::regex& pattern = matchPatch
        ? (matchSuffix ? patchWithSuffixPattern : patchExactPattern)
        : (matchSuffix ? minorWithSuffixPattern : minorExactPattern);
    std::smatch match;
    if (!std::regex_search(version, match, pattern)) {
        return info;
    }
#ifndef CANGJIE_ENABLE_GCOV
    try {
#endif
        info.major = static_cast<uint32_t>(std::stoul(match[REGEX_MAJOR_IDX].str()));
        info.minor = static_cast<uint32_t>(std::stoul(match[REGEX_MINOR_IDX].str()));
        // When matchPatch is false the pattern has no patch group, so REGEX_PATCH_IDX must not be looked up.
        info.patch = (matchPatch && match[REGEX_PATCH_IDX].matched)
                         ? static_cast<uint32_t>(std::stoul(match[REGEX_PATCH_IDX].str()))
                         : 0;
#ifndef CANGJIE_ENABLE_GCOV
    } catch (const std::exception&) {
        return info;
    }
#endif
    if (!IsValidVersionNumber(info.major) || !IsValidVersionNumber(info.minor) ||
        !IsValidVersionNumber(info.patch)) {
        return info;
    }
    info.isValid = true;
    return info;
}

const std::array<CompatChangeInfo, static_cast<size_t>(CompatChange::COUNT)> COMPAT_CHANGE_INFOS = {{
#define COMPAT_CHANGE(ID, MAJOR, MINOR, EXEMPT, DESCRIPTION) {{MAJOR, MINOR, 0, true}, EXEMPT, DESCRIPTION},
#include "cangjie/Basic/CompatChanges.def"
#undef COMPAT_CHANGE
}};

VersionInfo VersionInfo::Current()
{
    return ParseVersion(CANGJIE_VERSION);
}

const std::string CANGJIE_VERSION = CJ_SDK_VERSION;

#ifndef VERSION_TAIL
const std::string CANGJIE_COMPILER_VERSION = "Cangjie Compiler: " + CANGJIE_VERSION;
#else
const std::string CANGJIE_COMPILER_VERSION = "Cangjie Compiler: " + CANGJIE_VERSION + VERSION_TAIL;
#endif

void PrintVersion()
{
    Cangjie::Println(CANGJIE_COMPILER_VERSION);
}
} // namespace Cangjie
