// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the tool-side Response File (rsp) policy framework.
 *
 * When the command line that `cjc` would pass to a downstream tool (linker,
 * LLVM tool, ...) exceeds the platform's safe limit, `cjc` serializes the
 * arguments into an rsp file and invokes the tool with `@<rsp>`.
 *
 * The serializer style follows the rsp reader: GNU ld / lld / LLVM CommandLine
 * readers use GNU-style syntax, while Windows-hosted ld.lld (MinGW) tokenizes
 * response files and command lines with CRT rules — hence two serializers
 * selected by host OS (the rsp reader runs on the same host as cjc).
 */

#ifndef CANGJIE_DRIVER_RESPONSE_FILE_POLICY_H
#define CANGJIE_DRIVER_RESPONSE_FILE_POLICY_H

#include <cstddef>
#include <string>
#include <vector>

namespace Cangjie {

/**
 * @brief Policy describing whether and when a tool should fall back to rsp.
 *
 * `supported == false` means the tool keeps the original execution path with
 * zero behavior change.
 */
struct ResponseFilePolicy {
    bool supported{false};
    size_t threshold{0}; /**< Trigger rsp when command length >= threshold. */
};

/**
 * @brief Look up the rsp policy for a tool by its (possibly absolute) name.
 *
 * The name is normalized (basename → lower-case → strip `.exe`) before
 * matching. Returns a default `supported=false` policy for unknown tools,
 * so callers can treat the result uniformly.
 *
 * Strategies are host-platform based: Windows (CreateProcessA, 32767 cap)
 * registers `ld.lld` with a fixed 32000 threshold; Linux/macOS (posix_spawn,
 * ARG_MAX) register their linkers (`ld.lld`+`ld` / `ld64.lld`) with
 * `min(ARG_MAX/2, 128KB)`. The rsp serializer matches the reader style: GNU
 * rules on POSIX hosts, CRT rules on Windows (the linker runs on the same
 * host as cjc). The environment variable
 * `CJC_FORCE_RSP=1` bypasses the threshold check for any tool whose policy
 * is `supported=true`, which is useful for testing on POSIX where the real
 * threshold is rarely reached.
 */
ResponseFilePolicy LookupResponseFilePolicy(const std::string& toolName);

/**
 * @brief Serialize a full argument vector (argv[0] included) as GNU-style rsp.
 *
 * Implements the three rules from the design doc:
 *   1. argv[0] is NOT written to rsp (the program name is supplied separately
 *      by `CreateProcessA`/`posix_spawn`).
 *   2. Each remaining arg occupies one line (`\n` separated).
 *   3. Args containing any of space/tab/`\r`/`\n`/`"`/`'`/`\`/`#`, or empty,
 *      are wrapped in `"..."` with internal `\` -> `\\` and `"` -> `\"`.
 *
 * @param args Full argument vector, args[0] is the program name.
 * @return std::string The rsp file content.
 */
std::string GnuStyleRspSerialize(const std::vector<std::string>& args);

/**
 * @brief Serialize a full argument vector (argv[0] included) as a
 * Windows-CRT-style rsp.
 *
 * The inverse of the MSVC CRT command-line tokenizer that Windows-hosted
 * ld.lld (MinGW driver) applies to response files: every arg is wrapped in
 * `"..."`, inner `"` becomes `\"` with its preceding backslash run doubled,
 * and the backslash run immediately before the closing quote is doubled
 * (CRT 2n rule). Backslashes elsewhere, `'` and `#` are ordinary characters
 * to the CRT and pass through literally.
 */
std::string WindowsStyleRspSerialize(const std::vector<std::string>& args);

/**
 * @brief Serialize an rsp with the style matching the host OS.
 *
 * Windows host → WindowsStyleRspSerialize (CRT rules); other hosts →
 * GnuStyleRspSerialize (GNU rules). The branch criterion is the host OS,
 * the same one lld itself uses, because the rsp reader runs on the same
 * host as cjc even when cross-compiling.
 */
std::string SerializeRspForHost(const std::vector<std::string>& args);

/**
 * @brief Decide whether a tool invocation should fall back to an rsp file.
 *
 * True when `cmdLength` reaches the tool's registered threshold, or when the
 * environment variable `CJC_FORCE_RSP` is set to a non-empty value other than
 * "0" (a testing escape hatch for hosts where the real threshold is rarely
 * reached). Always false for tools without a registered policy.
 */
bool ShouldUseRsp(const ResponseFilePolicy& policy, size_t cmdLength);

/**
 * @brief Compute the platform-specific command length for `args`.
 *
 * On Windows this is the byte length of the `lpCommandLine` string that
 * `CreateProcessA` will receive (mirrors the quoting logic in
 * `BuildWindowsCommandLine`). On POSIX this is `sum(strlen(arg)+1)` which is
 * what counts toward `ARG_MAX` for `posix_spawn`.
 *
 * @param args Full argument vector, args[0] is the program name.
 * @return size_t The command length in bytes.
 */
size_t ComputeCommandLineLength(const std::vector<std::string>& args);

/**
 * @brief Build the Windows `lpCommandLine` string for `args`.
 *
 * Applies CRT (`CommandLineToArgvW`) quoting per arg — the same escaping as
 * `WindowsStyleRspSerialize` — so the command line round-trips args exactly
 * and the measured length equals what `CreateProcessA` actually receives.
 *
 * @param args Full argument vector including program name at index 0.
 * @return std::string The escaped command line string.
 */
std::string BuildWindowsCommandLine(const std::vector<std::string>& args);
} // namespace Cangjie

#endif // CANGJIE_DRIVER_RESPONSE_FILE_POLICY_H


// TODO: 去掉\` `` 转义仅为兼容 bash 书写习惯 这个方式不合理glo