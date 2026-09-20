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
// A minimal GNU-style rsp parser mirroring the real readers (lld @file
// expansion, GNU ld expandargv); '#' is an ordinary character, not a comment.
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
    const std::string content = GnuStyleRspSerialize({"ld.lld.exe", "-o", R"(C:\Build\My App\out.exe)",
        R"(C:\Program Files\cangjie\main.o)", R"(-L"C:\Program Files\lib")", "-lfoo"});
    EXPECT_EQ(content,
        R"(-o
"C:\\Build\\My App\\out.exe"
"C:\\Program Files\\cangjie\\main.o"
"-L\"C:\\Program Files\\lib\""
-lfoo
)");
}

TEST(ResponseFilePolicyTest, SerializeHashCharQuoted)
{
    EXPECT_EQ(GnuStyleRspSerialize({"ld", "-DVERSION=1.0#dev"}), R"("-DVERSION=1.0#dev"
)");
}

TEST(ResponseFilePolicyTest, SerializeEmptyArgQuoted)
{
    EXPECT_EQ(GnuStyleRspSerialize({"ld.lld.exe", "", "-o", "app"}),
        R"("
-o
app
)");
}

TEST(ResponseFilePolicyTest, SerializeSingleQuoteArgQuoted)
{
    EXPECT_EQ(GnuStyleRspSerialize({"ld.lld.exe", "-DNAME='foo'", "main.o"}),
        R"("-DNAME='foo'")
main.o
)");
}

TEST(ResponseFilePolicyTest, RoundTripThroughGnuStyleParser)
{
    const std::vector<std::string> args = {
        "ld.lld",
        "-o",
        R"(C:\Build\My App\out.exe)",
        R"(C:\Program Files\cangjie\main.o)",
        R"(-L"C:\Program Files\lib")",
        R"(-DVERSION=1.0#dev)",
        R"(-DNAME='foo')",
        "a b\tc",
        R"(back\slash)",
        R"('single')",
        R"("double")",
        "",
        R"(mixed'"both)",
        R"(中文路径.o)",
        "-lfoo",
    };
    const std::string content = GnuStyleRspSerialize(args);
    const std::vector<std::string> parsed = GnuStyleRspParse(content);
    const std::vector<std::string> expected(args.begin() + 1, args.end());
    EXPECT_EQ(parsed, expected);
}

TEST(ResponseFilePolicyTest, SerializeRspForHostMatchesHostStyle)
{
    // The entry Tool::Execute calls; style dispatches on the host OS.
    const std::vector<std::string> args = {"ld.lld", "-o", "C:\\Build\\My App\\out.exe", "-DNAME='foo'", "a b"};
#ifdef _WIN32
    EXPECT_EQ(SerializeRspForHost(args), WindowsStyleRspSerialize(args));
#else
    EXPECT_EQ(SerializeRspForHost(args), GnuStyleRspSerialize(args));
#endif
}

TEST(ResponseFilePolicyTest, GnuStyleRspParserHandlesRawShellSyntax)
{
    // Raw inputs the serializer never emits, covering every tokenizer branch.
    EXPECT_EQ(GnuStyleRspParse(R"(-o
'single quote'
)"), std::vector<std::string>({"-o", "single quote"}));
    EXPECT_EQ(GnuStyleRspParse(R"('it\'s'
)"), std::vector<std::string>({"it's"}));
    EXPECT_EQ(GnuStyleRspParse(R"(a\ b)"), std::vector<std::string>({"a b"}));
    // Trailing backslashes hit the "i + 1 < size" false arms.
    EXPECT_EQ(GnuStyleRspParse(R"('a\)"), std::vector<std::string>({"a\\"}));    EXPECT_EQ(GnuStyleRspParse("a\\"), std::vector<std::string>({"a\\"}));
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
    // Mid-string backslashes stay literal to the CRT tokenizer.
    EXPECT_EQ(WindowsStyleRspSerialize({"ld.lld.exe", R"(-LC:\lib\sub)"}),
        R"("-LC:\lib\sub"
)");
    // Trailing run doubled (CRT 2n rule).
    EXPECT_EQ(WindowsStyleRspSerialize({"ld.lld.exe", R"(-LC:\lib\)"}), R"("-LC:\lib\\"
)");
    // Three trailing backslashes are written as six and read back as three.
    EXPECT_EQ(WindowsStyleRspSerialize({"ld.lld.exe", R"(trail\\\)"}), R"("trail\\\\\\"
)");
    // Inner quote escaped; preceding run doubled.
    EXPECT_EQ(WindowsStyleRspSerialize({"ld.lld.exe", R"(-L"C:\lib")"}), R"("-L\"C:\lib\""
)");
    // Empty string must survive quoted.
    EXPECT_EQ(WindowsStyleRspSerialize({"ld.lld.exe", ""}), R"(""
)");
}

// A minimal MSVC CRT tokenizer mirroring Windows-hosted ld.lld; the inverse
// check for WindowsStyleRspSerialize.
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
        R"(C:\Build\My App\out.exe)",
        R"(-LC:\hlt\cangjie\third_party\mingw\lib\)",
        R"(C:\Program Files\cangjie\main.o)",
        R"(-L"C:\Program Files\lib")",
        R"(trail\\\)",
        "a b\tc",
        R"('single')",
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
    EXPECT_EQ(BuildWindowsCommandLine({"prog"}), R"("prog")");
    EXPECT_EQ(BuildWindowsCommandLine({"prog", "-o", "out"}), R"("prog" "-o" "out")");
    EXPECT_EQ(BuildWindowsCommandLine({"prog", ""}), R"("prog" "")");
    EXPECT_EQ(BuildWindowsCommandLine({"prog", "a b"}), R"("prog" "a b")");
    // Backslashes not adjacent to a quote are literal.
    EXPECT_EQ(BuildWindowsCommandLine({R"(C:\bin\ld.lld.exe)", R"(@C:\tmp\a.rsp)"}),
        R"("C:\bin\ld.lld.exe" "@C:\tmp\a.rsp")");
    // Trailing backslash doubled before the closing quote.
    EXPECT_EQ(BuildWindowsCommandLine({"prog", R"(-LC:\lib dir\)"}), R"("prog" "-LC:\lib dir\\")");
    EXPECT_EQ(BuildWindowsCommandLine({"prog", R"(-L"C:\lib")"}), R"("prog" "-L\"C:\lib\"")");
}

#ifdef _WIN32
TEST(ResponseFilePolicyTest, CommandLineLengthMatchesWindowsQuoting)
{
    const std::vector<std::string> args = {R"(C:\bin\ld.lld.exe)", "-o", R"(C:\out dir\a.exe)"};
    EXPECT_EQ(ComputeCommandLineLength(args), BuildWindowsCommandLine(args).size());
}
#endif

TEST(ResponseFilePolicyTest, OversizedCommandLineWritesRspFile)
{
    // Isolate from the ambient environment.
#ifdef _WIN32
    _putenv_s("CJC_FORCE_RSP", "");
#else
    unsetenv("CJC_FORCE_RSP");
#endif

    // Threshold boundary: at-threshold falls back, one byte below does not.
    const ResponseFilePolicy windowsPolicy{true, 32000};
    EXPECT_FALSE(ShouldUseRsp(windowsPolicy, size_t{31999}));
    EXPECT_TRUE(ShouldUseRsp(windowsPolicy, size_t{32000}));

    // Oversized under both length formulas (Windows lpCommandLine and POSIX
    // argv bytes), so the case is meaningful on every host.
    std::vector<std::string> args{R"(C:\bin\ld.lld.exe)", "-o", "out.exe"};
    for (int i = 0; i < 50; ++i) {
        args.push_back(R"(C:\a\)" + std::string(700, 'a') + R"(\)" + std::to_string(i) + ".o");
    }
    ASSERT_GT(BuildWindowsCommandLine(args).size(), size_t{32000});
    ASSERT_GT(ComputeCommandLineLength(args), size_t{32000});
    EXPECT_TRUE(ShouldUseRsp(windowsPolicy, ComputeCommandLineLength(args)));

    // Unregistered tools never fall back.
    EXPECT_FALSE(ShouldUseRsp(ResponseFilePolicy{}, ComputeCommandLineLength(args)));

    // End to end: rsp file written and round-trips the CRT tokenizer.
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

TEST(ResponseFilePolicyTest, ShouldUseRspForcedByEnv)
{
    // CJC_FORCE_RSP: "1" forces, "0" and "" count as unset; unset at the end.
    const ResponseFilePolicy policy{true, 32000};
#ifdef _WIN32
    _putenv_s("CJC_FORCE_RSP", "1");
#else
    setenv("CJC_FORCE_RSP", "1", 1);
#endif
    EXPECT_TRUE(ShouldUseRsp(policy, size_t{5}));
#ifdef _WIN32
    _putenv_s("CJC_FORCE_RSP", "0");
#else
    setenv("CJC_FORCE_RSP", "0", 1);
#endif
    EXPECT_FALSE(ShouldUseRsp(policy, size_t{5}));
    EXPECT_TRUE(ShouldUseRsp(policy, size_t{32000}));
#ifdef _WIN32
    _putenv_s("CJC_FORCE_RSP", "");
#else
    setenv("CJC_FORCE_RSP", "", 1);
#endif
    EXPECT_FALSE(ShouldUseRsp(policy, size_t{5}));
#ifdef _WIN32
    _putenv_s("CJC_FORCE_RSP", "");
#else
    unsetenv("CJC_FORCE_RSP");
#endif
}
