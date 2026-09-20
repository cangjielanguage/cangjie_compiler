// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
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
 * serializer style matches the reader on the same host: GNU rules on POSIX,
 * CRT rules on Windows.
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
 * tools get `supported == false`. Registered tools are host-based: Windows
 * `ld.lld` with threshold 32000 (CreateProcessA cap 32767); Linux/macOS
 * linkers with `min(ARG_MAX/2, 128KB)`.
 */
ResponseFilePolicy LookupResponseFilePolicy(const std::string& toolName);

/**
 * @brief Serialize args (argv[0] included) as GNU-style rsp.
 *
 * argv[0] is not written; each remaining arg occupies one line; args
 * containing space/tab/`\r`/`\n`/`"`/`'`/`\`/`#`, or empty, are wrapped in
 * `"..."` with `\` -> `\\` and `"` -> `\"`.
 *
 * @param args Full argument vector, args[0] is the program name.
 * @return std::string The rsp file content.
 */
std::string GnuStyleRspSerialize(const std::vector<std::string>& args);

/**
 * @brief Serialize args (argv[0] included) as Windows-CRT-style rsp.
 *
 * Inverse of the CRT tokenizer used by Windows-hosted ld.lld: every arg is
 * wrapped in `"..."`, inner `"` becomes `\"` with its preceding backslash
 * run doubled (CRT 2n rule).
 */
std::string WindowsStyleRspSerialize(const std::vector<std::string>& args);

/// Serialize an rsp with the style matching the host OS: Windows host uses
/// CRT rules, other hosts GNU rules (the rsp reader runs on the same host).
std::string SerializeRspForHost(const std::vector<std::string>& args);

/**
 * @brief Decide whether a tool invocation should fall back to an rsp file.
 *
 * True when `cmdLength >= policy.threshold`, or when `CJC_FORCE_RSP` is set
 * to a non-empty value other than "0" (testing escape hatch). Always false
 * for tools without a registered policy.
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
