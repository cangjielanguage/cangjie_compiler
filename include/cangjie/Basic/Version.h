// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the cangjie compiler's version.
 */

#ifndef CANGJIE_DRIVER_VERSION_H
#define CANGJIE_DRIVER_VERSION_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace Cangjie {
struct VersionInfo {
    uint32_t major = 0;
    uint32_t minor = 0;
    uint32_t patch = 0;
    bool isValid = false;

    constexpr bool operator==(const VersionInfo& other) const
    {
        return major == other.major && minor == other.minor && patch == other.patch;
    }

    constexpr bool operator<(const VersionInfo& other) const
    {
        if (major != other.major) {
            return major < other.major;
        }
        if (minor != other.minor) {
            return minor < other.minor;
        }
        return patch < other.patch;
    }

    static VersionInfo Current();
};

/**
 * @brief Parse the leading major[.minor[.patch]] numbers of a version string.
 * @param version      Version string, e.g. "1.2.3", "1.2" or "1.2.3-local".
 * @param matchPatch   Whether the patch number takes part in matching. When false the version is major.minor only
 *                     and a following ".patch" is rejected.
 * @param matchSuffix  Whether characters after the matched numbers are allowed, e.g. the "-local" in
 *                     "1.2.3-local". When false the whole string must be consumed.
 * @return Parsed version; isValid is false when the string does not match, a number is out of the [0, 99] range,
 *         or a number cannot be represented.
 */
VersionInfo ParseVersion(const std::string& version, bool matchPatch = true, bool matchSuffix = true);

enum class CompatChange : uint16_t {
#define COMPAT_CHANGE(ID, ...) ID,
#include "cangjie/Basic/CompatChanges.def"
#undef COMPAT_CHANGE
    COUNT
};

struct CompatChangeInfo {
    VersionInfo since;
    bool exempt;
    const char* description;
};

/**
 * @brief Description of every compat change, indexed by CompatChange.
 *
 * Defined in Version.cpp rather than inline here so that the table exists exactly once in the binary and the
 * entries are only compiled into the translation unit that owns them.
 */
extern const std::array<CompatChangeInfo, static_cast<size_t>(CompatChange::COUNT)> COMPAT_CHANGE_INFOS;

extern const std::string CANGJIE_VERSION;
extern const std::string CANGJIE_COMPILER_VERSION;

void PrintVersion();
} // namespace Cangjie
#endif // CANGJIE_DRIVER_VERSION_H
