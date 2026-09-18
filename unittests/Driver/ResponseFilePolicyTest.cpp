// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

#include "gtest/gtest.h"

#include "cangjie/Driver/ResponseFilePolicy.h"
#include "cangjie/Driver/TempFileManager.h"
#include "cangjie/Option/Option.h"
#include "cangjie/Utils/FileUtil.h"

using namespace Cangjie;

namespace {
// A minimal GNU-style rsp parser (the same syntax lld/Common/Args.cpp and GNU
// ld implement) used to verify that GnuStyleRspSerialize is its inverse:
// whitespace separated tokens, '\''/'"' quoting, '\' escaping everywhere and
// '#' line comments at token start.
std::vector<std::string> GnuStyleRspParse(const std::string& content)
{
    std::vector<std::string> tokens;
    std::string current;
    bool inToken = false;
    bool inSingle = false;
    bool inDouble = false;
    for (size_t i = 0; i < content.size(); ++i) {
        const char c = content[i];
        if (inDouble) {
            if (c == '\\' && i + 1 < content.size()) {
                current += content[++i];
            } else if (c == '"') {
                inDouble = false;
            } else {
                current += c;
            }
        } else if (inSingle) {
            if (c == '\\' && i + 1 < content.size()) {
                current += content[++i];
            } else if (c == '\'') {
                inSingle = false;
            } else {
                current += c;
            }
        } else if (c == '\\' && i + 1 < content.size()) {
            current += content[++i];
            inToken = true;
        } else if (c == '"') {
            inDouble = true;
            inToken = true;
        } else if (c == '\'') {
            inSingle = true;
            inToken = true;
        } else if (c == '#') {
            while (i < content.size() && content[i] != '\n') {
                ++i;
            }
        } else if (std::isspace(static_cast<unsigned char>(c)) != 0) {
            if (inToken) {
                tokens.push_back(current);
                current.clear();
                inToken = false;
            }
        } else {
            current += c;
            inToken = true;
        }
    }
    if (inToken) {
        tokens.push_back(current);
    }
    return tokens;
}
} // namespace

TEST(ResponseFilePolicyTest, SerializePlainArgsOnePerLine)
{
    const std::string content = GnuStyleRspSerialize({"ld", "-o", "/tmp/main", "main.o"});
    EXPECT_EQ(content, "-o\n/tmp/main\nmain.o\n");
}

TEST(ResponseFilePolicyTest, SerializeSkipsProgramName)
{
    EXPECT_EQ(GnuStyleRspSerialize({}), "");
    EXPECT_EQ(GnuStyleRspSerialize({"only-prog"}), "");
}

TEST(ResponseFilePolicyTest, SerializeWindowsPathsQuotingAndEscaping)
{
    const std::string content = GnuStyleRspSerialize({"ld.lld.exe", "-o", "C:\\Build\\My App\\out.exe",
        "C:\\Program Files\\cangjie\\main.o", "-L\"C:\\Program Files\\lib\"", "-lfoo"});
    EXPECT_EQ(content,
        "-o\n"
        "\"C:\\\\Build\\\\My App\\\\out.exe\"\n"
        "\"C:\\\\Program Files\\\\cangjie\\\\main.o\"\n"
        "\"-L\\\"C:\\\\Program Files\\\\lib\\\"\"\n"
        "-lfoo\n");
}

TEST(ResponseFilePolicyTest, SerializeHashCharQuoted)
{
    EXPECT_EQ(GnuStyleRspSerialize({"ld", "-DVERSION=1.0#dev"}), "\"-DVERSION=1.0#dev\"\n");
}

TEST(ResponseFilePolicyTest, SerializeEmptyArgQuoted)
{
    EXPECT_EQ(GnuStyleRspSerialize({"ld.lld.exe", "", "-o", "app"}), "\"\"\n-o\napp\n");
}

TEST(ResponseFilePolicyTest, SerializeSingleQuoteArgQuoted)
{
    EXPECT_EQ(GnuStyleRspSerialize({"ld.lld.exe", "-DNAME='foo'", "main.o"}), "\"-DNAME='foo'\"\nmain.o\n");
}

TEST(ResponseFilePolicyTest, RoundTripThroughGnuStyleParser)
{
    const std::vector<std::string> args = {
        "ld.lld",
        "-o",
        "C:\\Build\\My App\\out.exe",
        "C:\\Program Files\\cangjie\\main.o",
        "-L\"C:\\Program Files\\lib\"",
        "-DVERSION=1.0#dev",
        "-DNAME='foo'",
        "a b\tc",
        "back\\slash",
        "'single'",
        "\"double\"",
        "",
        "mixed'\"both",
        "中文路径.o",
        "-lfoo",
    };
    const std::string content = GnuStyleRspSerialize(args);
    const std::vector<std::string> parsed = GnuStyleRspParse(content);
    const std::vector<std::string> expected(args.begin() + 1, args.end());
    EXPECT_EQ(parsed, expected);
}

TEST(ResponseFilePolicyTest, UnknownToolsKeepOriginalPath)
{
    for (const std::string& tool : {"cjc-frontend", "CacheCopy", "RemoveFile", "opt.exe", "clang", "llvm-ar", "gold"}) {
        const ResponseFilePolicy policy = LookupResponseFilePolicy(tool);
        EXPECT_FALSE(policy.supported) << tool;
        EXPECT_EQ(policy.threshold, size_t{0}) << tool;
    }
}

#if defined(_WIN32)
TEST(ResponseFilePolicyTest, RegisteredLinkersMatchHostToolchain)
{
    // Host = Windows: MinGW linker ld.lld, CreateProcessA cap 32767.
    const ResponseFilePolicy policy = LookupResponseFilePolicy("C:\\cangjie\\bin\\ld.lld.exe");
    ASSERT_TRUE(policy.supported);
    EXPECT_EQ(policy.threshold, size_t{32000});
    EXPECT_TRUE(LookupResponseFilePolicy("C:\\Bin\\LD.LLD.EXE").supported);
    EXPECT_FALSE(LookupResponseFilePolicy("C:\\bin\\ld.exe").supported);
    EXPECT_FALSE(LookupResponseFilePolicy("C:\\bin\\ld64.lld.exe").supported);
}
#elif defined(__APPLE__)
TEST(ResponseFilePolicyTest, RegisteredLinkersMatchHostToolchain)
{
    // Host = macOS: Mach-O linker ld64.lld, posix_spawn (ARG_MAX).
    const ResponseFilePolicy policy = LookupResponseFilePolicy("/usr/bin/ld64.lld");
    ASSERT_TRUE(policy.supported);
    EXPECT_GT(policy.threshold, size_t{0});
    EXPECT_LE(policy.threshold, size_t{128 * 1024});
    EXPECT_FALSE(LookupResponseFilePolicy("/usr/bin/ld.lld").supported);
    EXPECT_FALSE(LookupResponseFilePolicy("/usr/bin/ld").supported);
}
#else
TEST(ResponseFilePolicyTest, RegisteredLinkersMatchHostToolchain)
{
    // Host = Linux: ld.lld (LTO/HwAsan) + system ld, posix_spawn (ARG_MAX).
    const ResponseFilePolicy policy = LookupResponseFilePolicy("/usr/bin/ld.lld");
    ASSERT_TRUE(policy.supported);
    EXPECT_GT(policy.threshold, size_t{0});
    EXPECT_LE(policy.threshold, size_t{128 * 1024});
    EXPECT_TRUE(LookupResponseFilePolicy("/usr/bin/ld").supported);
    EXPECT_FALSE(LookupResponseFilePolicy("/usr/bin/ld64.lld").supported);
    // Cross-compilation prefixed ld is not matched (design doc §4.6, plan A).
    EXPECT_FALSE(LookupResponseFilePolicy("/usr/bin/x86_64-linux-gnu-ld").supported);
}

TEST(ResponseFilePolicyTest, CommandLineLengthCountsArgvBytes)
{
    EXPECT_EQ(ComputeCommandLineLength({"a", "bb"}), size_t{5});
    EXPECT_EQ(ComputeCommandLineLength({"prog", "-o", "/tmp/a.o"}), size_t{17});
}
#endif

TEST(ResponseFilePolicyTest, WindowsStyleRspSerializeCrtEscaping)
{
    // Every arg is unconditionally wrapped in "..."; mid-string backslashes
    // stay single (literal to the CRT tokenizer).
    EXPECT_EQ(WindowsStyleRspSerialize({"ld.lld.exe", "-LC:\\lib\\sub"}),
        "\"-LC:\\lib\\sub\"\n");
    // Trailing backslash run is doubled before the closing quote (CRT 2n rule).
    EXPECT_EQ(WindowsStyleRspSerialize({"ld.lld.exe", "-LC:\\lib\\"}), "\"-LC:\\lib\\\\\"\n");
    // Three trailing backslashes are written as six and read back as three.
    EXPECT_EQ(WindowsStyleRspSerialize({"ld.lld.exe", "trail\\\\\\"}), "\"trail\\\\\\\\\\\\\"\n");
    // Inner quote is escaped (\" with the preceding run doubled: 2*0+1 = 1).
    EXPECT_EQ(WindowsStyleRspSerialize({"ld.lld.exe", "-L\"C:\\lib\""}), "\"-L\\\"C:\\lib\\\"\"\n");
    // The empty string survives as "" (an empty line would be dropped).
    EXPECT_EQ(WindowsStyleRspSerialize({"ld.lld.exe", ""}), "\"\"\n");
}

// A minimal MSVC CRT command-line/rsp tokenizer (the syntax Windows-hosted
// ld.lld applies) used to verify that WindowsStyleRspSerialize is its
// inverse: 2n backslashes + quote -> n backslashes + quote toggle, 2n+1
// backslashes + quote -> n backslashes + literal quote; one arg per line.
std::vector<std::string> WindowsStyleRspParse(const std::string& content)
{
    std::vector<std::string> tokens;
    std::string current;
    bool inQuotes = false;
    size_t backslashes = 0;
    for (const char c : content) {
        if (c == '\\') {
            ++backslashes;
        } else if (c == '"') {
            current.append(backslashes / 2, '\\');
            if (backslashes % 2 == 0) {
                inQuotes = !inQuotes;
            } else {
                current += '"';
            }
            backslashes = 0;
        } else {
            current.append(backslashes, '\\');
            backslashes = 0;
            if (!inQuotes && c == '\n') {
                tokens.push_back(current);
                current.clear();
            } else {
                current += c;
            }
        }
    }
    return tokens;
}

TEST(ResponseFilePolicyTest, RoundTripThroughWindowsStyleParser)
{
    const std::vector<std::string> args = {
        "ld.lld.exe",
        "-o",
        "C:\\Build\\My App\\out.exe",
        "-LC:\\hlt\\cangjie\\third_party\\mingw\\lib\\",
        "C:\\Program Files\\cangjie\\main.o",
        "-L\"C:\\Program Files\\lib\"",
        "trail\\\\\\",
        "a b\tc",
        "'single'",
        "",
        "-lfoo",
    };
    const std::string content = WindowsStyleRspSerialize(args);
    const std::vector<std::string> parsed = WindowsStyleRspParse(content);
    const std::vector<std::string> expected(args.begin() + 1, args.end());
    EXPECT_EQ(parsed, expected);
}

TEST(ResponseFilePolicyTest, WindowsCommandLineQuoting)
{
    EXPECT_EQ(BuildWindowsCommandLine({}), "");
    EXPECT_EQ(BuildWindowsCommandLine({"prog"}), "\"prog\"");
    EXPECT_EQ(BuildWindowsCommandLine({"prog", "-o", "out"}), "\"prog\" \"-o\" \"out\"");
    EXPECT_EQ(BuildWindowsCommandLine({"prog", ""}), "\"prog\" \"\"");
    EXPECT_EQ(BuildWindowsCommandLine({"prog", "a b"}), "\"prog\" \"a b\"");
    // CRT rules: backslashes not adjacent to a quote are literal, so plain
    // paths need no doubling.
    EXPECT_EQ(BuildWindowsCommandLine({"C:\\bin\\ld.lld.exe", "@C:\\tmp\\a.rsp"}),
        "\"C:\\bin\\ld.lld.exe\" \"@C:\\tmp\\a.rsp\"");
    // Trailing backslash adjacent to the closing quote is doubled.
    EXPECT_EQ(BuildWindowsCommandLine({"prog", "-LC:\\lib dir\\"}), "\"prog\" \"-LC:\\lib dir\\\\\"");
    EXPECT_EQ(BuildWindowsCommandLine({"prog", "-L\"C:\\lib\""}), "\"prog\" \"-L\\\"C:\\lib\\\"\"");
}

#ifdef _WIN32
TEST(ResponseFilePolicyTest, CommandLineLengthMatchesWindowsQuoting)
{
    const std::vector<std::string> args = {"C:\\bin\\ld.lld.exe", "-o", "C:\\out dir\\a.exe"};
    EXPECT_EQ(ComputeCommandLineLength(args), BuildWindowsCommandLine(args).size());
}
#endif

TEST(ResponseFilePolicyTest, OversizedCommandLineWritesRspFile)
{
    // Make the decision independent of the ambient environment.
#ifdef _WIN32
    _putenv_s("CJC_FORCE_RSP", "");
#else
    unsetenv("CJC_FORCE_RSP");
#endif

    // Trigger boundary: a command line exactly at the threshold falls back,
    // one byte below does not.
    const ResponseFilePolicy windowsPolicy{true, 32000};
    EXPECT_FALSE(ShouldUseRsp(windowsPolicy, size_t{31999}));
    EXPECT_TRUE(ShouldUseRsp(windowsPolicy, size_t{32000}));

    // An argv whose measured length exceeds the Windows CreateProcessA limit
    // under BOTH formulas: lpCommandLine bytes (BuildWindowsCommandLine) and
    // POSIX argv bytes (ComputeCommandLineLength), so the oversized case is
    // meaningful on every host.
    std::vector<std::string> args{"C:\\bin\\ld.lld.exe", "-o", "out.exe"};
    for (int i = 0; i < 50; ++i) {
        args.push_back("C:\\a\\" + std::string(700, 'a') + "\\" + std::to_string(i) + ".o");
    }
    ASSERT_GT(BuildWindowsCommandLine(args).size(), size_t{32000});
    ASSERT_GT(ComputeCommandLineLength(args), size_t{32000});
    EXPECT_TRUE(ShouldUseRsp(windowsPolicy, ComputeCommandLineLength(args)));

    // Unregistered tools never fall back, even when oversized.
    EXPECT_FALSE(ShouldUseRsp(ResponseFilePolicy{}, ComputeCommandLineLength(args)));

    // End to end: the rsp file is really written to disk and its content
    // round-trips through the CRT tokenizer that Windows-hosted ld.lld
    // applies to response files.
    GlobalOptions options;
    ASSERT_TRUE(TempFileManager::Instance().Init(options, false));
    const std::string content = WindowsStyleRspSerialize(args);
    const std::string rspPath = TempFileManager::Instance().CreateRspFile(content);
    ASSERT_FALSE(rspPath.empty());

    std::string failedReason;
    const auto written = FileUtil::ReadFileContent(rspPath, failedReason);
    ASSERT_TRUE(written.has_value()) << failedReason;
    EXPECT_EQ(*written, content);

    const std::vector<std::string> expected(args.begin() + 1, args.end());
    EXPECT_EQ(WindowsStyleRspParse(*written), expected);
}
