// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include <string>
#include <vector>

#include "gtest/gtest.h"

#include "cangjie/Driver/ResponseFilePolicy.h"
#include "cangjie/Driver/TempFileManager.h"
#include "cangjie/Option/Option.h"
#include "cangjie/Utils/FileUtil.h"

using namespace Cangjie;

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
    // Host = macOS: no linker registered yet; keep the original spawn path.
    EXPECT_FALSE(LookupResponseFilePolicy("/usr/bin/ld64.lld").supported);
    EXPECT_FALSE(LookupResponseFilePolicy("/usr/bin/ld.lld").supported);
}
#else
TEST(ResponseFilePolicyTest, RegisteredLinkersMatchHostToolchain)
{
    // Host = Linux: no linker registered yet; keep the original spawn path.
    EXPECT_FALSE(LookupResponseFilePolicy("/usr/bin/ld.lld").supported);
    EXPECT_FALSE(LookupResponseFilePolicy("/usr/bin/ld").supported);
    EXPECT_FALSE(LookupResponseFilePolicy("/usr/bin/ld64.lld").supported);
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
    // argv[0] (program name) is supplied by CreateProcessA and never written.
    EXPECT_EQ(WindowsStyleRspSerialize({}), "");
    EXPECT_EQ(WindowsStyleRspSerialize({"only-prog"}), "");

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

TEST(ResponseFilePolicyTest, SerializeRspForHostMatchesHostStyle)
{
    // The entry Tool::Execute calls; the host dispatch lives inside
    // SerializeRspForHost. To support a new host, register its policy in
    // LookupResponseFilePolicy and dispatch its serializer there.
    const std::vector<std::string> args = {"ld.lld.exe", "-o", "C:\\Build\\My App\\out.exe", "a b"};
#ifdef _WIN32
    EXPECT_EQ(SerializeRspForHost(args), WindowsStyleRspSerialize(args));
#else
    // No POSIX host registered yet: sentinel content, the caller falls back
    // to the original args.
    EXPECT_EQ(SerializeRspForHost(args), "");
#endif
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

