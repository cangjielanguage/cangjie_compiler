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
#ifndef _WIN32
#include <unistd.h>
#endif

#include "cangjie/Utils/FileUtil.h"

namespace Cangjie {
namespace {
#ifndef _WIN32
// POSIX fallback used only when sysconf fails or returns a nonsensical value.
constexpr size_t POSIX_FALLBACK_THRESHOLD = 128 * 1024;

// Cap the POSIX threshold at 128 KB so we trigger earlier than ARG_MAX/2 in
// practice (a typical ARG_MAX is 2 MB; 128 KB is well below it and avoids
// surprising E2BIG on weird systems).
constexpr size_t POSIX_THRESHOLD_CAP = 128 * 1024;
#endif

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

#ifndef _WIN32
size_t PosixThreshold()
{
    long argMax = sysconf(_SC_ARG_MAX);
    size_t threshold = (argMax > 0) ? static_cast<size_t>(argMax) / 2 : POSIX_FALLBACK_THRESHOLD;
    return std::min(threshold, POSIX_THRESHOLD_CAP);
}
#endif

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

std::string GnuStyleRspSerialize(const std::vector<std::string>& args)
{
    std::string out;
    // Rule 1: argv[0] (program name) is supplied by CreateProcessA/posix_spawn
    // and is NOT written to rsp.
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& a = args[i];
        // Rule 3: wrap in quotes if arg is empty or contains any of the
        // whitespace/quote/backslash/comment characters that the GNU rsp
        // parser treats specially.
        const bool needQuote = a.empty() ||
            a.find_first_of(" \t\r\n\f\v\"'\\#") != std::string::npos;
        if (!needQuote) {
            out += a;       // rule 2 + rule 3 (false branch): raw arg, one line
            out += '\n';
            continue;
        }
        out += '"';
        for (char c : a) {
            if (c == '\\' || c == '"') {
                out += '\\';   // rule 3 (true branch): escape \ and "
            }
            out += c;
        }
        out += "\"\n";
    }
    return out;
}

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
    return WindowsStyleRspSerialize(args);
#else
    return GnuStyleRspSerialize(args);
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
    static const std::unordered_map<std::string, ResponseFilePolicy> policyTable = []() {
        std::unordered_map<std::string, ResponseFilePolicy> table;
#if defined(_WIN32)
        // TEMP gate test (revert before commit): threshold shrunk to force
        // the rsp path. Real value: 32000 (CreateProcessA cap 32767, margin).
        table["ld.lld"] = {true, 128};
#elif defined(__APPLE__)
        // TEMP gate test (revert before commit). Real value: PosixThreshold().
        table["ld64.lld"] = {true, std::min(PosixThreshold(), size_t{128})};
#else
        // TEMP gate test (revert before commit). Real value: PosixThreshold().
        const size_t threshold = std::min(PosixThreshold(), size_t{128});
        table["ld.lld"] = {true, threshold};
        table["ld"]     = {true, threshold};
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
