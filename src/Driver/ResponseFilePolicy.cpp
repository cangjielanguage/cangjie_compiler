// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the tool-side Response File policy framework.
 */

#include "cangjie/Driver/ResponseFilePolicy.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_map>

#include "cangjie/Utils/CheckUtils.h"
#include "cangjie/Utils/FileUtil.h"

namespace Cangjie {
namespace {
std::string NormalizeToolName(const std::string& toolName)
{
    // 1. basename
    std::string base = FileUtil::GetFileName(toolName);
    // 2. lower-case (must run before stripping ".exe", which is case-sensitive)
    std::transform(base.begin(), base.end(), base.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    // 3. strip ".exe" suffix (also covers cross-built Windows tools)
    const std::string exeSuffix = ".exe";
    if (base.size() > exeSuffix.size()) {
        auto pos = base.size() - exeSuffix.size();
        if (base.compare(pos, exeSuffix.size(), exeSuffix) == 0) {
            base.erase(pos, exeSuffix.size());
        }
    }
    return base;
}

// Quotes a single argument with CRT (CommandLineToArgvW) rules — the inverse
// of the tokenizer Windows-hosted ld.lld applies to command lines and rsp
// files. Shared by BuildWindowsCommandLine and WindowsStyleRspSerialize so
// both paths escape args identically and round-trip them exactly. Every arg
// is unconditionally wrapped in "..." (legal for the CRT tokenizer, preserves
// the empty string) and only two escapes remain:
// - a '"' is emitted as \" with its preceding backslash run doubled (2n+1
//   backslashes + quote -> n backslashes + literal quote);
// - the trailing backslash run is doubled before the closing quote (2n rule:
//   2n backslashes + quote -> n backslashes + quote toggle).
// Backslashes elsewhere, '\'', '#' and whitespace inside quotes are literal.
std::string WindowsQuoteArg(const std::string& a)
{
    std::string quoted;
    quoted += '"';
    size_t backslashes = 0;
    for (char c : a) {
        if (c == '\\') {
            ++backslashes;
            continue;
        }
        if (c == '"') {
            quoted.append(backslashes * 2, '\\');
            quoted += "\\\"";
        } else {
            quoted.append(backslashes, '\\');
            quoted += c;
        }
        backslashes = 0;
    }
    // Trailing backslash run sits right before the closing quote: double it.
    quoted.append(backslashes * 2, '\\');
    quoted += '"';
    return quoted;
}
} // namespace

std::string WindowsStyleRspSerialize(const std::vector<std::string>& args)
{
    std::string out;
    for (size_t i = 1; i < args.size(); ++i) {
        // Every arg is quoted; CRT tokenizes inside quotes and keeps '\',
        // '\'' and '#' literal unless a backslash run precedes a quote.
        out += WindowsQuoteArg(args[i]);
        out += '\n';
    }
    return out;
}

std::string SerializeRspForHost(const std::vector<std::string>& args)
{
#ifdef _WIN32
    // CRT rules match the Windows-hosted ld.lld rsp reader.
    return WindowsStyleRspSerialize(args);
#else
    // Extension point: POSIX hosts register no rsp policy yet (see
    // LookupResponseFilePolicy), so Tool::Execute never reaches here. To
    // support a POSIX host, write its serializer (GNU rules; see git
    // history: GnuStyleRspSerialize) and dispatch it in this branch.
    CJC_ASSERT_WITH_MSG(false, "no rsp serializer registered for this host yet");
    return "";
#endif
}

bool ShouldUseRsp(const ResponseFilePolicy& policy, size_t cmdLength)
{
    return policy.supported && cmdLength >= policy.threshold;
}

std::string BuildWindowsCommandLine(const std::vector<std::string>& args)
{
    // CRT (CommandLineToArgvW) quoting per arg — every arg wrapped in "...",
    // shared with WindowsStyleRspSerialize, so the string passed to
    // CreateProcessA round-trips args exactly and the measured length equals
    // the actual command line.
    std::ostringstream oss;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i != 0) {
            oss << ' ';
        }
        oss << WindowsQuoteArg(args[i]);
    }
    return oss.str();
}

size_t ComputeCommandLineLength(const std::vector<std::string>& args)
{
#ifdef _WIN32
    // Length of the lpCommandLine string CreateProcessA will receive.
    return BuildWindowsCommandLine(args).size();
#else
    // POSIX: argv bytes (each arg's strlen + 1 NUL byte) count toward ARG_MAX.
    size_t total = 0;
    for (const auto& a : args) {
        total += a.size() + 1;
    }
    return total;
#endif
}

ResponseFilePolicy LookupResponseFilePolicy(const std::string& toolName)
{
    // Strategies are HOST-platform based: what limits the command line is how
    // cjc itself spawns the child (CreateProcessA vs posix_spawn), not the
    // target being built, so cross compilation picks the right strategy
    // automatically.
    //
    // Scope: only Windows hosts register a policy for now (MinGW linker
    // ld.lld; CreateProcessA caps lpCommandLine at 32767 chars, threshold
    // 32000 keeps a margin). POSIX hosts keep the original spawn path; to
    // extend support, register the host linkers here (Linux: ld.lld/ld,
    // macOS: ld64.lld) with a posix_spawn-based threshold.
    static const std::unordered_map<std::string, ResponseFilePolicy> policyTable = []() {
        std::unordered_map<std::string, ResponseFilePolicy> table;
#if defined(_WIN32)
        table["ld.lld"] = {true, 32000};
#endif
        return table;
    }();

    const std::string key = NormalizeToolName(toolName);
    auto it = policyTable.find(key);
    if (it == policyTable.end()) {
        return ResponseFilePolicy{};
    }
    return it->second;
}
} // namespace Cangjie
