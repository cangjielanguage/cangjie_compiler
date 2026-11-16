// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * Tool-side Response File (rsp) fallback: when the command line cjc would
 * pass to a downstream tool exceeds the platform limit, the arguments are
 * serialized into an rsp file and the tool is invoked with `@<rsp>`. The
 * serializer matches the reader of the host toolchain: CRT rules on Windows
 * for now; POSIX hosts keep the original spawn path until their linkers are
 * registered.
 */

#ifndef CANGJIE_DRIVER_RESPONSE_FILE_POLICY_H
#define CANGJIE_DRIVER_RESPONSE_FILE_POLICY_H

#include <cstddef>
#include <string>
#include <vector>

namespace Cangjie {

/// Policy for a tool: fall back to rsp when command length >= threshold.
/// `supported == false` keeps the original execution path unchanged.
struct ResponseFilePolicy {
    bool supported{false};
    size_t threshold{0}; /**< Trigger rsp when command length >= threshold. */
};

/**
 * @brief Look up the rsp policy for a tool by its (possibly absolute) name.
 *
 * The name is normalized (basename -> lower-case -> strip `.exe`). Unknown
 * tools get `supported == false`. Policies are host-based; only Windows hosts
 * register one for now (`ld.lld`, threshold 32000, CreateProcessA cap 32767).
 * POSIX hosts keep the original spawn path until their linkers are
 * registered.
 */
ResponseFilePolicy LookupResponseFilePolicy(const std::string& toolName);

/**
 * @brief Serialize args (argv[0] included) as Windows-CRT-style rsp.
 *
 * Inverse of the CRT tokenizer used by Windows-hosted ld.lld: every arg is
 * wrapped in `"..."`, inner `"` becomes `\"` with its preceding backslash
 * run doubled (CRT 2n rule).
 */
std::string WindowsStyleRspSerialize(const std::vector<std::string>& args);

/// Serialize an rsp with the style matching the rsp reader of the host
/// toolchain. This is the single extension point for new hosts: register the
/// host's policy in `LookupResponseFilePolicy`, then dispatch its serializer
/// here. Only the Windows host (CRT rules) is registered for now; unregistered
/// hosts return an empty string so the caller falls back to the original args.
std::string SerializeRspForHost(const std::vector<std::string>& args);

/**
 * @brief Decide whether a tool invocation should fall back to an rsp file.
 *
 * True when `cmdLength >= policy.threshold`. Always false for tools without
 * a registered policy.
 */
bool ShouldUseRsp(const ResponseFilePolicy& policy, size_t cmdLength);

/**
 * @brief Compute the command length in bytes for `args` (argv[0] included).
 *
 * Windows: the `lpCommandLine` byte length built by `BuildWindowsCommandLine`.
 * POSIX: `sum(strlen(arg)+1)`, what counts toward `ARG_MAX` for posix_spawn.
 */
size_t ComputeCommandLineLength(const std::vector<std::string>& args);

/// Build the Windows `lpCommandLine` string for `args`, applying CRT quoting
/// (same escaping as `WindowsStyleRspSerialize`).
std::string BuildWindowsCommandLine(const std::vector<std::string>& args);
} // namespace Cangjie

#endif // CANGJIE_DRIVER_RESPONSE_FILE_POLICY_H
