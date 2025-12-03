// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.#include <cstdio>

#include <fstream>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <sstream>
#include <string>
#ifdef _WIN32
#include <process.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "gtest/gtest.h"

#define private public
#include "cangjie/AST/Utils.h"
#include "cangjie/Driver/Toolchains/GCCPathScanner.h"
#include "cangjie/Utils/FileUtil.h"
#include "cangjie/Utils/FloatFormat.h"
#include "cangjie/Utils/ProfileRecorder.h"
#include "cangjie/Utils/SipHash.h"
#include "cangjie/Utils/Utils.h"

using namespace Cangjie;
using namespace Cangjie::Utils;
using namespace Cangjie::FileUtil;
using namespace Cangjie::FloatFormat;
using namespace AST;

namespace {
std::string GetSystemTempDir()
{
#ifdef _WIN32
    char buffer[MAX_PATH] = {0};
    DWORD len = GetTempPathA(MAX_PATH, buffer);
    if (len == 0 || len > MAX_PATH) {
        return ".";
    }
    return std::string(buffer, len);
#else
    const char* tmp = std::getenv("TMPDIR");
    if (tmp == nullptr || *tmp == '\0') {
        return "/tmp";
    }
    return tmp;
#endif
}

std::string MakeTempPath(const std::string& prefix)
{
    static std::atomic<uint64_t> counter {0};
    uint64_t value = ++counter;
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
#ifdef _WIN32
    int pid = _getpid();
#else
    int pid = getpid();
#endif
    std::ostringstream oss;
    oss << prefix << "_" << static_cast<unsigned long long>(now) << "_" << pid << "_" << value;
    return FileUtil::JoinPath(GetSystemTempDir(), oss.str());
}

bool CreateSymlinkCrossPlatform(const std::string& target, const std::string& linkPath, bool isDir)
{
#ifdef _WIN32
#ifndef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
#define SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE 0x2
#endif
    DWORD flags = isDir ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0;
    if (CreateSymbolicLinkA(linkPath.c_str(), target.c_str(), flags | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) != 0) {
        return true;
    }
    DWORD err = GetLastError();
    if (err == ERROR_INVALID_PARAMETER) {
        if (CreateSymbolicLinkA(linkPath.c_str(), target.c_str(), flags) != 0) {
            return true;
        }
        err = GetLastError();
    }
    if (err == ERROR_PRIVILEGE_NOT_HELD) {
        return false;
    }
    return false;
#else
    return symlink(target.c_str(), linkPath.c_str()) == 0;
#endif
}
} // namespace

#ifdef PROJECT_SOURCE_DIR
// Gets the absolute path of the project from the compile parameter.
static std::string GetProjectPath()
{
    return PROJECT_SOURCE_DIR;
}
#else
// Just in case, give it a default value.
// Assume the initial is in the build directory.
static std::string GetProjectPath()
{
    return "..";
}
#endif

TEST(UtilsTest, Access)
{
    const std::string path = "./no_such_file.txt";
    bool exist = Access(path, FM_EXIST);

    EXPECT_EQ(false, exist);
}

TEST(UtilsTest, FileExist)
{
    std::string path;
#ifdef _WIN32
    path = "C:/Windows/System32/cmd.exe";
#else
    path = "/bin/bash";
#endif
    bool res = FileExist(path);
    EXPECT_TRUE(res);
}

TEST(UtilsTest, FileExist_CaseSencetive)
{
#ifdef _WIN32
    std::string path = "C:/Windows/System32/Cmd.exe";
    bool res = FileExist(path, true);
    EXPECT_FALSE(res);
    res = FileExist(path, false);
    EXPECT_TRUE(res);
#elif defined(__APPLE__)
    std::string path = "/bin/Bash";
    bool res = FileExist(path, true);
    EXPECT_FALSE(res);
    res = FileExist(path, false);
    EXPECT_TRUE(res);
#else
    std::string path = "/bin/Bash";
    bool res = FileExist(path, true);
    EXPECT_FALSE(res);
    res = FileExist(path, false);
    EXPECT_FALSE(res);
#endif

#ifdef _WIN32
    path = "utilstest";
    res = FileExist(path, true);
    EXPECT_FALSE(res);
    res = FileExist(path, false);
    EXPECT_TRUE(res);
#elif defined(__APPLE__)
    path = "utilstest";
    res = FileExist(path, true);
    EXPECT_FALSE(res);
    res = FileExist(path, false);
    EXPECT_TRUE(res);
#else
    path = "utilstest";
    res = FileExist(path, true);
    EXPECT_FALSE(res);
    res = FileExist(path, false);
    EXPECT_FALSE(res);
#endif
}

TEST(UtilsTest, FindProgramByName)
{
    std::string name = "./good";
    std::string res = FindProgramByName(name, {});
    EXPECT_EQ(res, name);

    name = "bash";
    res = FindProgramByName(name, {"test/test"});
    EXPECT_EQ(res, "");
}

TEST(UtilsTest, RemoveDirectoryNonExist)
{
    std::string path = MakeTempPath("cj_remove_dir_not_exist");
    EXPECT_FALSE(RemoveDirectoryRecursively(path));
}

TEST(UtilsTest, RemoveDirectoryRemoveFile)
{
    std::string tempFile = MakeTempPath("cj_remove_dir_file") + ".txt";
    std::ofstream ofs(tempFile);
    ofs << "test";
    ofs.close();

    EXPECT_FALSE(RemoveDirectoryRecursively(tempFile));
    (void)FileUtil::Remove(tempFile);
}

TEST(UtilsTest, RemoveDirectorySuccess)
{
    std::string base = MakeTempPath("cj_remove_dir_success");
    std::string nested = FileUtil::JoinPath(base, "nested");
    std::string nestedFile = FileUtil::JoinPath(nested, "file.txt");
    std::string anotherFile = FileUtil::JoinPath(base, "root.txt");
    EXPECT_EQ(0, CreateDirs(nested));
    std::ofstream(nestedFile) << "nested";
    std::ofstream(anotherFile) << "root";

    EXPECT_TRUE(RemoveDirectoryRecursively(base));
    EXPECT_FALSE(FileExist(base));

    if (FileExist(base)) {
        RemoveDirectoryRecursively(base);
    }
}

TEST(UtilsTest, RemoveDirectoryWithSymlinkChild)
{
    std::string outside = MakeTempPath("cj_remove_symlink_outside") + ".txt";
    std::string base = MakeTempPath("cj_remove_dir_symlink_child");
    EXPECT_EQ(0, CreateDirs(base + DIR_SEPARATOR));
    std::ofstream(outside) << "outside";
    std::string linkPath = FileUtil::JoinPath(base, "link");
    if (!CreateSymlinkCrossPlatform(outside, linkPath, false)) {
        RemoveDirectoryRecursively(base);
        (void)FileUtil::Remove(outside);
        GTEST_SKIP() << "Skip symlink child test: cannot create symbolic link on this platform.";
        return;
    }

    EXPECT_TRUE(RemoveDirectoryRecursively(base));
    EXPECT_TRUE(FileExist(outside));

    (void)FileUtil::Remove(outside);
    if (FileExist(base)) {
        RemoveDirectoryRecursively(base);
    }
}

TEST(UtilsTest, RemoveDirectoryTargetIsSymlink)
{
    std::string base = MakeTempPath("cj_remove_dir_real");
    std::string baseFile = FileUtil::JoinPath(base, "file.txt");
    EXPECT_EQ(0, CreateDirs(base + DIR_SEPARATOR));
    std::ofstream(baseFile) << "data";
    std::string linkPath = MakeTempPath("cj_remove_dir_symlink_root");
    if (!CreateSymlinkCrossPlatform(base, linkPath, true)) {
        RemoveDirectoryRecursively(base);
        GTEST_SKIP() << "Skip symlink directory test: cannot create symbolic link on this platform.";
        return;
    }

    EXPECT_TRUE(RemoveDirectoryRecursively(linkPath));
    EXPECT_TRUE(FileExist(base));

    RemoveDirectoryRecursively(base);
    (void)FileUtil::Remove(linkPath);
}

TEST(UtilsTest, GetFilePath)
{
    std::string path = GetDirPath("lalala.cj");
    EXPECT_EQ(path, ".");
    path = GetDirPath("/usr/local/bin/lalala.cj");
#ifdef _WIN32
    EXPECT_EQ(path, "\\usr\\local\\bin");
#else
    EXPECT_EQ(path, "/usr/local/bin");
#endif
}

TEST(UtilsTest, GetFileExtensionTest)
{
    std::string ext = GetFileExtension("lalala.cj");
    EXPECT_EQ(ext, "cj");
    ext = GetFileExtension("/usr/local/bin/lalala.cj");
    EXPECT_EQ(ext, "cj");
    ext = GetFileExtension("");
    EXPECT_EQ(ext, "");
    ext = GetFileExtension("/");
    EXPECT_EQ(ext, "");
    ext = GetFileExtension("./");
    EXPECT_EQ(ext, "");
    ext = GetFileExtension("out");
    EXPECT_EQ(ext, "");
    ext = GetFileExtension("/out");
    EXPECT_EQ(ext, "");
    ext = GetFileExtension("/.out");
    EXPECT_EQ(ext, "");
    ext = GetFileExtension("./.out");
    EXPECT_EQ(ext, "");
    ext = GetFileExtension("test/.out");
    EXPECT_EQ(ext, "");
    ext = GetFileExtension("test/.test/.out");
    EXPECT_EQ(ext, "");
    ext = GetFileExtension("test/.test/out");
    EXPECT_EQ(ext, "");
    ext = GetFileExtension("test/.test/out/");
    EXPECT_EQ(ext, "");
    ext = GetFileExtension("test/out.ext");
    EXPECT_EQ(ext, "ext");
    ext = GetFileExtension("test/.test/out.ext");
    EXPECT_EQ(ext, "ext");
    ext = GetFileExtension("test/.test/.out.ext");
    EXPECT_EQ(ext, "ext");
}

TEST(UtilsTest, GetFileNameWithoutExtension)
{
    std::string name = GetFileNameWithoutExtension("main.cj");
    EXPECT_EQ(name, "main");

    name = GetFileNameWithoutExtension("testfile");
    EXPECT_EQ(name, "testfile");

    std::string case3 = "/hahaha/yeyeye/test1.cj";

    name = GetFileNameWithoutExtension(case3);
    EXPECT_EQ(name, "test1");

    name = GetFileNameWithoutExtension(".test2");
    EXPECT_EQ(name, ".test2");

    name = GetFileNameWithoutExtension("/some/path/.test3.cj");
    EXPECT_EQ(name, ".test3");
}

TEST(UtilsTest, GetFileBase)
{
    std::string name = GetFileBase("../output/main.cj");
    EXPECT_EQ(name, "../output/main");

    name = GetFileBase("main");
    EXPECT_EQ(name, "main");

    name = GetFileBase("./main");
    EXPECT_EQ(name, "./main");

    name = GetFileBase("./path/main");
    EXPECT_EQ(name, "./path/main");

    name = GetFileBase("../.cjo");
    EXPECT_EQ(name, "../.cjo");

    name = GetFileBase(".cjo");
    EXPECT_EQ(name, ".cjo");

    name = GetFileBase("../.test.cjo");
    EXPECT_EQ(name, "../.test");

    name = GetFileBase("main.cj");
    EXPECT_EQ(name, "main");

    name = GetFileBase("/path/file.extension");
    EXPECT_EQ(name, "/path/file");

    name = GetFileBase("/path/.out");
    EXPECT_EQ(name, "/path/.out");
}

TEST(UtilsTest, IsIdentifierTest)
{
    std::string s = "`te_s1t`";
    EXPECT_TRUE(IsIdentifier(s));

    s = "te_s1t";
    EXPECT_TRUE(IsIdentifier(s));

    s = "``";
    EXPECT_FALSE(IsIdentifier(s));

    s = "_test";
    EXPECT_TRUE(IsIdentifier(s));

    s = "1_test";
    EXPECT_FALSE(IsIdentifier(s));

    s = "";
    EXPECT_FALSE(IsIdentifier(s));

    s = "public";
    EXPECT_TRUE(IsIdentifier(s));
    EXPECT_FALSE(IsIdentifier(s, false));

    s = "`true`";
    EXPECT_TRUE(IsIdentifier(s, false));

    s = "标识符";
    EXPECT_TRUE(IsIdentifier(s, false));

    s = "_标识符";
    EXPECT_TRUE(IsIdentifier(s));

    s = "__";
    EXPECT_TRUE(IsIdentifier(s));

    s = "_1";
    EXPECT_TRUE(IsIdentifier(s));
}

TEST(UtilsTest, TransferEscapeBacktickTest)
{
#ifdef _WIN32
    EXPECT_EQ(TransferEscapeBacktick("`test`"), "`test`");
#else
    std::string s = "`test`";
    EXPECT_EQ(TransferEscapeBacktick(s), "\\`test\\`");
    std::string s2 = "t`test`a";
    EXPECT_EQ(TransferEscapeBacktick(s2), "t\\`test\\`a");
#endif
}

TEST(UtilsTest, GetFileNameTest)
{
    std::string name = GetFileName("main.cj");
    EXPECT_EQ(name, "main.cj");

    name = GetFileName("testfile");
    EXPECT_EQ(name, "testfile");

    std::string case3 = "/hahaha/yeyeye/test1.cj";

    name = GetFileNameWithoutExtension(case3);
    EXPECT_EQ(name, "test1");
}

TEST(UtilsTest, IsDirTest)
{
    std::string utilsDir = GetProjectPath() + "/unittests/Utils/";
    std::string noExistDir = "no/exist/dir";
    EXPECT_TRUE(IsDir(utilsDir));
    EXPECT_FALSE(IsDir(noExistDir));
#ifndef _WIN32
    std::string command = "mkdir -p ./definitelyDirectory";
    int err = system(command.c_str());
    ASSERT_EQ(0, err);
    command = "chmod -r ./definitelyDirectory";
    err = system(command.c_str());
    ASSERT_EQ(0, err);
    EXPECT_TRUE(IsDir("./definitelyDirectory"));
#endif
}

TEST(UtilsTest, JoinPathTest)
{
#ifdef _WIN32
    // windows case.
    std::string moduleDir1 = "g:\\test\\package\\";
    std::string moduleDir2 = "g:\\test\\package";
    std::string srcPath = "src";
    EXPECT_EQ("g:\\test\\package\\src", JoinPath(moduleDir1, srcPath));
    EXPECT_EQ("g:\\test\\package\\src", JoinPath(moduleDir2, srcPath));
#else
    std::string basePath1 = "a/b/";
    std::string basePath2 = "a/b";
    std::string basePath3 = "a/b/\\t";
    std::string appendPath = "c";
    EXPECT_EQ("a/b/c", JoinPath(basePath1, appendPath));
    EXPECT_EQ("a/b/c", JoinPath(basePath2, appendPath));
    EXPECT_EQ("a/b/\\t/c", JoinPath(basePath3, appendPath));
#endif
}

TEST(UtilsTest, GetRelativePath)
{
    std::string basePathOne = "/base/dir1";
    std::string pathOne = "/base/dir2";

    std::string basePathTwo = "/base/dir1/lib";
    std::string pathTwo = "/base/dir2";
    std::string basePathThree = "/base/dir1/lib";
    std::string pathThree = "/base/dir2/lib";
#ifdef _WIN32
    EXPECT_EQ(GetRelativePath(basePathOne, pathOne).value(), ".\\dir2");
    EXPECT_EQ(GetRelativePath(basePathTwo, pathTwo).value(), "..\\dir2");
    EXPECT_EQ(GetRelativePath(basePathThree, pathThree).value(), "..\\dir2\\lib");

    EXPECT_EQ(GetRelativePath("/base", "/base").value(), ".\\");
    EXPECT_EQ(GetRelativePath("/base/a", "/base/a").value(), ".\\");

    EXPECT_EQ(GetRelativePath("/base/a", "/base/a/b/c/d").value(), ".\\a\\b\\c\\d");
    EXPECT_EQ(GetRelativePath("/base/a", "/base/a/b/c/d/").value(), ".\\a\\b\\c\\d\\");
    EXPECT_EQ(GetRelativePath("/base/a/", "/base/a/b/c/d").value(), ".\\b\\c\\d");
    EXPECT_EQ(GetRelativePath("/base/a/", "/base/a/b/c/d/").value(), ".\\b\\c\\d\\");

    EXPECT_EQ(GetRelativePath("/base/a/b/c/d", "/base/a").value(), "..\\..\\");
    EXPECT_EQ(GetRelativePath("/base/a/b/c/d/", "/base/a").value(), "..\\..\\..\\");
    EXPECT_EQ(GetRelativePath("/base/a/b/c/d", "/base/a/").value(), "..\\..\\");
    EXPECT_EQ(GetRelativePath("/base/a/b/c/d/", "/base/a/").value(), "..\\..\\..\\");

    EXPECT_EQ(GetRelativePath("\\base", "\\base").value(), ".\\");
    EXPECT_EQ(GetRelativePath("\\base\\a", "\\base\\a").value(), ".\\");

    EXPECT_EQ(GetRelativePath("\\base\\a", "\\base\\a\\b\\c\\d").value(), ".\\a\\b\\c\\d");
    EXPECT_EQ(GetRelativePath("\\base\\a", "\\base\\a\\b\\c\\d\\").value(), ".\\a\\b\\c\\d\\");
    EXPECT_EQ(GetRelativePath("\\base\\a\\", "\\base\\a\\b\\c\\d").value(), ".\\b\\c\\d");
    EXPECT_EQ(GetRelativePath("\\base\\a\\", "\\base\\a\\b\\c\\d\\").value(), ".\\b\\c\\d\\");

    EXPECT_EQ(GetRelativePath("\\base\\a\\b\\c\\d", "\\base\\a").value(), "..\\..\\");
    EXPECT_EQ(GetRelativePath("\\base\\a\\b\\c\\d\\", "\\base\\a").value(), "..\\..\\..\\");
    EXPECT_EQ(GetRelativePath("\\base\\a\\b\\c\\d", "\\base\\a\\").value(), "..\\..\\");
    EXPECT_EQ(GetRelativePath("\\base\\a\\b\\c\\d\\", "\\base\\a\\").value(), "..\\..\\..\\");
#else
    EXPECT_EQ(GetRelativePath(basePathOne, pathOne).value(), "./dir2");
    EXPECT_EQ(GetRelativePath(basePathTwo, pathTwo).value(), "../dir2");
    EXPECT_EQ(GetRelativePath(basePathThree, pathThree).value(), "../dir2/lib");

    EXPECT_EQ(GetRelativePath("/base", "/base").value(), "./");
    EXPECT_EQ(GetRelativePath("/base/a", "/base/a").value(), "./");

    EXPECT_EQ(GetRelativePath("/base/a", "/base/a/b/c/d").value(), "./a/b/c/d");
    EXPECT_EQ(GetRelativePath("/base/a", "/base/a/b/c/d/").value(), "./a/b/c/d/");
    EXPECT_EQ(GetRelativePath("/base/a/", "/base/a/b/c/d").value(), "./b/c/d");
    EXPECT_EQ(GetRelativePath("/base/a/", "/base/a/b/c/d/").value(), "./b/c/d/");

    EXPECT_EQ(GetRelativePath("/base/a/b/c/d", "/base/a").value(), "../../");
    EXPECT_EQ(GetRelativePath("/base/a/b/c/d/", "/base/a").value(), "../../../");
    EXPECT_EQ(GetRelativePath("/base/a/b/c/d", "/base/a/").value(), "../../");
    EXPECT_EQ(GetRelativePath("/base/a/b/c/d/", "/base/a/").value(), "../../../");
#endif
    EXPECT_FALSE(GetRelativePath("", "").has_value());
    EXPECT_FALSE(GetRelativePath("", "/base/a/b/c/d/").has_value());
    EXPECT_FALSE(GetRelativePath("/base/a/b/c/d/", "").has_value());
}

TEST(UtilsTest, SplitStrTest)
{
    std::vector<std::string> res = {"a", "ab", "abc"};
    std::string case1 = "a\nab\nabc\n";
    std::vector<std::string> case1Ret = SplitStr(case1, '\n');
    EXPECT_EQ(res.size(), case1Ret.size());
    for (size_t i = 0; i < res.size(); ++i) {
        EXPECT_EQ(res[i], case1Ret[i]);
    }

    std::vector<std::string> resTwo = {"a", "ab", "abc"};
    std::string caseTwo = "a;ab;abc";
    std::vector<std::string> caseTwoRet = SplitStr(caseTwo, ';');
    EXPECT_EQ(resTwo.size(), caseTwoRet.size());
    for (size_t i = 0; i < resTwo.size(); ++i) {
        EXPECT_EQ(resTwo[i], caseTwoRet[i]);
    }

    std::vector<std::string> resThree = {"a", "ab", "abc"};
    std::string caseThree = " a   ab   abc  ";
    std::vector<std::string> caseThreeRet = SplitStr(caseThree, ' ');
    EXPECT_EQ(resThree.size(), caseThreeRet.size());
    for (size_t i = 0; i < resThree.size(); ++i) {
        EXPECT_EQ(resThree[i], caseThreeRet[i]);
    }
}

TEST(UtilsTest, ReadUtf8WithSignature)
{
    std::string filePath = GetProjectPath() + "/unittests/Utils/CangjieFiles/testpkg01.cj";
    std::string failedReason;
    auto content = ReadFileContent(filePath, failedReason);
    EXPECT_TRUE(content.has_value() && failedReason.empty());
    EXPECT_EQ(content->at(0), '/');
}

TEST(UtilsTest, ReadUtf8)
{
    std::string filePath = GetProjectPath() + "/unittests/Utils/CangjieFiles/pkg01.cj";
    std::string failedReason;
    auto content = ReadFileContent(filePath, failedReason);
    EXPECT_TRUE(content.has_value() && failedReason.empty());
    EXPECT_EQ(content->at(0), '/');
}

TEST(UtilsTest, ReadEmptyFile)
{
    std::string filePath = GetProjectPath() + "/unittests/Utils/CangjieFiles/emptyfile.cj";
    std::string failedReason;
    auto content = ReadFileContent(filePath, failedReason);
    EXPECT_TRUE(content.has_value());
}

TEST(UtilsTest, GCCVersionCompare)
{
    // Different major.
    struct GCCVersion a = {7, 5, 5}; // Indicate version 7.5.5
    struct GCCVersion b = {6, 5, 5}; // Indicate version 6.5.5
    EXPECT_FALSE(a < b);
    b.major = a.major + 1;
    EXPECT_TRUE(a < b);

    // Different minor.
    b.major = a.major;
    b.minor = a.minor - 1;
    EXPECT_FALSE(a < b);
    b.minor = a.minor + 1;
    EXPECT_TRUE(a < b);

    // Different build.
    b.minor = a.minor;
    b.build = a.build - 1;
    EXPECT_FALSE(a < b);
    b.build = a.build + 1;
    EXPECT_TRUE(a < b);
}

TEST(UtilsTest, CheckCommandLineInjection)
{
    EXPECT_TRUE(CheckCommandLineInjection("11|"));
    EXPECT_TRUE(CheckCommandLineInjection(";22"));
    EXPECT_TRUE(CheckCommandLineInjection("3&3"));

#ifndef _WIN32
    EXPECT_TRUE(CheckCommandLineInjection("44$"));
    EXPECT_TRUE(CheckCommandLineInjection(">55"));
    EXPECT_TRUE(CheckCommandLineInjection("6<6"));
    EXPECT_TRUE(CheckCommandLineInjection("77\\"));
    EXPECT_TRUE(CheckCommandLineInjection("!88"));
    EXPECT_TRUE(CheckCommandLineInjection("9\n9"));
#endif

    EXPECT_FALSE(CheckCommandLineInjection("1234567"));
    EXPECT_FALSE(CheckCommandLineInjection("abcdefg"));
    EXPECT_FALSE(CheckCommandLineInjection("abc\t"));

    // Allow ` appears in cmd in pairs.
    EXPECT_TRUE(CheckCommandLineInjection("`"));
    EXPECT_TRUE(CheckCommandLineInjection("`a"));
    EXPECT_FALSE(CheckCommandLineInjection("`a`"));
    EXPECT_TRUE(CheckCommandLineInjection("`a`b`"));
    EXPECT_FALSE(CheckCommandLineInjection("a`b`c"));
}

TEST(UtilsTest, IsAbsolutePath)
{
    EXPECT_FALSE(IsAbsolutePath(""));
    EXPECT_FALSE(IsAbsolutePath("abc"));

#ifdef _WIN32
    EXPECT_FALSE(IsAbsolutePath("/"));
    EXPECT_FALSE(IsAbsolutePath("/abc"));
    EXPECT_TRUE(IsAbsolutePath("C:\\"));
    EXPECT_TRUE(IsAbsolutePath("D:\\abc"));
#else
    EXPECT_TRUE(IsAbsolutePath("/"));
    EXPECT_TRUE(IsAbsolutePath("/abc"));
#endif
}

TEST(UtilsTest, ReadBinaryFileToBuffer)
{
    std::string filePath = "./no_such_file.txt";
    std::string failedReason;
    std::vector<uint8_t> buffer;

    bool ret = ReadBinaryFileToBuffer(filePath, buffer, failedReason);
    EXPECT_FALSE(ret);
    EXPECT_EQ("open file failed", failedReason);

#ifdef _WIN32
    filePath = "C:/Windows/System32/cmd.exe";
#else
    filePath = "/bin/bash";
#endif
    ret = ReadBinaryFileToBuffer(filePath, buffer, failedReason);
    EXPECT_TRUE(ret);
    EXPECT_NE("empty binary file", failedReason);

    filePath = "./4GB+.bin";
    std::ofstream os(filePath, std::ios::binary | std::ios::out);
    EXPECT_TRUE(os);
    os.seekp(4ULL * 1024 * 1024 * 1024 + 1);
    os.write("", 1);
    os.close();
    ret = ReadBinaryFileToBuffer(filePath, buffer, failedReason);
    EXPECT_FALSE(ret);
    EXPECT_EQ("exceed the max file length: 4 GB", failedReason);
    std::remove(filePath.c_str());
}

TEST(UtilsTest, ReadFileContent)
{
    std::string filePath = "./no_such_file.txt";
    std::string failedReason = "";
    auto content = ReadFileContent(filePath, failedReason);
    EXPECT_FALSE(content.has_value() && failedReason.empty());
}

TEST(UtilsTest, AppendExtension)
{
    std::string result1 = AppendExtension("", "backup", ".so");
    EXPECT_EQ("backup.so", result1);

    std::string result2 = AppendExtension("main.exe", "backup", ".so");
    EXPECT_EQ("main.so", result2);
}

TEST(UtilsTest, GetAbsPath)
{
    std::string filePath = "a";
    for (auto i = 0; i < PATH_MAX; i++) {
        filePath.append("b");
    }

    auto absPath = GetAbsPath(filePath);

    EXPECT_FALSE(absPath.has_value());
}

TEST(UtilsTest, Float32ToFloat16)
{
    // inf: 0111110000000000
    EXPECT_EQ(0b0111110000000000, Float32ToFloat16(6.5536e4f));
    // max normal: 0111101111111111 (1.1111111111 × 2^15)
    EXPECT_EQ(0b0111101111111111, Float32ToFloat16(6.5504e4f));
    // normal: 0011000100000000 ((1 + 2^-2) × 2^-3)
    EXPECT_EQ(0b0011000100000000, Float32ToFloat16(0.15625f));
    // min normal: 0000010000000000 (2^-14)
    EXPECT_EQ(0b0000010000000000, Float32ToFloat16(6.104e-5f));
    // max subnormal: 0000001111111111 (0.1111111111)
    EXPECT_EQ(0b0000001111111111, Float32ToFloat16(6.102e-5f));
    // min subnormal: 0000000000000001 (2^-10 × 2^-14)
    EXPECT_EQ(0b0000000000000001, Float32ToFloat16(5.9605e-8f));
    // zero: 0000000000000000
    EXPECT_EQ(0b0000000000000000, Float32ToFloat16(5.9604e-8f));
}

TEST(UtilsTest, GetSizeDecl)
{
    InvalidTy ty = InvalidTy();
    EXPECT_EQ(nullptr, GetSizeDecl(ty));
}

TEST(UtilsTest, FindFileByName)
{
    std::vector<std::string> paths;

    auto res = FindFileByName("", paths);
    EXPECT_FALSE(res.has_value());

    res = FindFileByName("hello", paths);
    EXPECT_FALSE(res.has_value());

    paths.push_back("/bin/");
    paths.push_back("/etc/");

    res = FindFileByName("non-exist-file", paths);
    EXPECT_FALSE(res.has_value());

#ifndef _WIN32
    res = FindFileByName("bash", paths);
    EXPECT_EQ(res.value(), "/bin/bash");
#endif
}

TEST(UtilsTest, NormalizePath)
{
#ifdef _WIN32
    EXPECT_EQ(NormalizePath("main"), ".\\main");
    EXPECT_EQ(NormalizePath("./main"), ".\\main");
    EXPECT_EQ(NormalizePath("/path/to/file"), "\\path\\to\\file");
#else
    EXPECT_EQ(NormalizePath("main"), "./main");
    EXPECT_EQ(NormalizePath("./main"), "./main");
    EXPECT_EQ(NormalizePath("/path/to/file"), "/path/to/file");
#endif
}

TEST(UtilsTest, GetAllDirsUnderCurrentPath)
{
#ifndef _WIN32
    std::string command = "rm -rf fileUtilsMkdir";
    auto err = system(command.c_str());
    ASSERT_EQ(0, err);
    command = "mkdir -p fileUtilsMkdir/tmp1";
    err = system(command.c_str());
    ASSERT_EQ(0, err);

    std::vector<std::string> dirs = GetAllDirsUnderCurrentPath("fileUtilsMkdir");
    EXPECT_EQ(dirs.size(), 1);
    EXPECT_EQ(dirs[0], "fileUtilsMkdir/tmp1");

    command = "mkdir fileUtilsMkdir/tmp2";
    err = system(command.c_str());
    ASSERT_EQ(0, err);

    dirs = GetAllDirsUnderCurrentPath("fileUtilsMkdir");
    EXPECT_EQ(dirs.size(), 2);
    EXPECT_EQ(std::count(dirs.begin(), dirs.end(), "fileUtilsMkdir/tmp2"), 1);

    auto absPath = GetAbsPath("fileUtilsMkdir");
    ASSERT_TRUE(absPath.has_value());
    dirs = GetAllDirsUnderCurrentPath(absPath.value());
    EXPECT_EQ(dirs.size(), 2);
#endif
}

TEST(UtilsTest, GetPkgNameFromRelativePath)
{
    // Run both linux and windows.
    EXPECT_EQ(GetPkgNameFromRelativePath("./a"), "a");
    EXPECT_EQ(GetPkgNameFromRelativePath("./a/b"), "a.b");
    EXPECT_EQ(GetPkgNameFromRelativePath("a/b"), "a.b");
    EXPECT_EQ(GetPkgNameFromRelativePath("a/./b"), "a.b");
    EXPECT_EQ(GetPkgNameFromRelativePath("a/b/."), "a.b");

    EXPECT_EQ(GetPkgNameFromRelativePath(""), "default");
    EXPECT_EQ(GetPkgNameFromRelativePath("./"), "default");
    EXPECT_EQ(GetPkgNameFromRelativePath("../"), "default");
    EXPECT_EQ(GetPkgNameFromRelativePath("xx/../"), "default");
#ifdef _WIN32
    EXPECT_EQ(GetPkgNameFromRelativePath(".\\a"), "a");
    EXPECT_EQ(GetPkgNameFromRelativePath(".\\a\\b"), "a.b");
    EXPECT_EQ(GetPkgNameFromRelativePath("a\\b"), "a.b");
    EXPECT_EQ(GetPkgNameFromRelativePath("a\\.\\b"), "a.b");
    EXPECT_EQ(GetPkgNameFromRelativePath("a\\b\\."), "a.b");

    EXPECT_EQ(GetPkgNameFromRelativePath(""), "default");
    EXPECT_EQ(GetPkgNameFromRelativePath(".\\"), "default");
    EXPECT_EQ(GetPkgNameFromRelativePath("..\\"), "default");
    EXPECT_EQ(GetPkgNameFromRelativePath("xx\\..\\"), "default");
#endif
}

TEST(UtilsTest, IsAbsolutePathAboveLengthLimit)
{
#ifdef _WIN32
    // 27 characters in the path
    EXPECT_FALSE(IsAbsolutePathAboveLengthLimit("C:/Windows/System32/cmd.exe"));
    // 265 characters in the path
    EXPECT_TRUE(IsAbsolutePathAboveLengthLimit(
        "./"
        "12345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890"
        "12345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890"
        "1234567890123456789012345678901234567890.cj"));
#else
    EXPECT_FALSE(IsAbsolutePathAboveLengthLimit("/bin/bash"));
#endif
}

TEST(UtilsTest, SipHash)
{
    uint64_t hashVal = Utils::SipHash::GetHashValue(std::string("123abc"));
    EXPECT_EQ(hashVal, 0x4F03C64A98DDE71E);
    hashVal = Utils::SipHash::GetHashValue(uint8_t(12));
    EXPECT_EQ(hashVal, 0xD854D2BDE319255);
    hashVal = Utils::SipHash::GetHashValue(int8_t(-23));
    EXPECT_EQ(hashVal, 0xAEC08D713104780D);
    hashVal = Utils::SipHash::GetHashValue(uint16_t(120));
    EXPECT_EQ(hashVal, 0xBF50EFAA5E3EC47F);
    hashVal = Utils::SipHash::GetHashValue(int16_t(-230));
    EXPECT_EQ(hashVal, 0xF59AAFE3408C3246);
    hashVal = Utils::SipHash::GetHashValue(uint32_t(4500));
    EXPECT_EQ(hashVal, 0xDE95412C896457D8);
    hashVal = Utils::SipHash::GetHashValue(int32_t(-5600));
    EXPECT_EQ(hashVal, 0x92071CC5CB5A1C0B);
    hashVal = Utils::SipHash::GetHashValue(uint64_t(7800000));
    EXPECT_EQ(hashVal, 0xDA9374B66FE40DAF);
    hashVal = Utils::SipHash::GetHashValue(int64_t(-89456113));
    EXPECT_EQ(hashVal, 0x456D9FCA9BDD4D37);
    hashVal = Utils::SipHash::GetHashValue(double(-89456113.123789));
    EXPECT_EQ(hashVal, 0xB72124ADB2EABF59);
    hashVal = Utils::SipHash::GetHashValue(float(789456.222666));
    EXPECT_EQ(hashVal, 0xC413B71DA4335E1B);
    hashVal = Utils::SipHash::GetHashValue(std::bitset<64>{789});
    EXPECT_EQ(hashVal, 0xD43173C1E336768B);
    hashVal = Utils::SipHash::GetHashValue(std::bitset<32>{789});
    EXPECT_EQ(hashVal, 0xD43173C1E336768B);
    hashVal = Utils::SipHash::GetHashValue(std::bitset<16>{789});
    EXPECT_EQ(hashVal, 0xD43173C1E336768B);
    hashVal = Utils::SipHash::GetHashValue(std::bitset<5>{9});
    EXPECT_EQ(hashVal, 0x4F5CD87E0CD8CAAC);
    hashVal = Utils::SipHash::GetHashValue(double(-89456113.123789));
    EXPECT_EQ(hashVal, 0xB72124ADB2EABF59);
    hashVal = Utils::SipHash::GetHashValue(double(-89456113.123789));
    EXPECT_EQ(hashVal, 0xB72124ADB2EABF59);
    hashVal = Utils::SipHash::GetHashValue(std::string("123abc"));
    EXPECT_EQ(hashVal, 0x4F03C64A98DDE71E);
}

TEST(UtilsTest, SipHash2)
{
    EXPECT_NE(Utils::SipHash::GetHashValue("define_40020"), Utils::SipHash::GetHashValue("Vefine_40020"));
    EXPECT_NE(Utils::SipHash::GetHashValue("define_macro"), Utils::SipHash::GetHashValue("Define_macro"));
    EXPECT_NE(Utils::SipHash::GetHashValue("Define_30031"), Utils::SipHash::GetHashValue("define_30031"));
    EXPECT_NE(Utils::SipHash::GetHashValue("define_20033"), Utils::SipHash::GetHashValue("lefine_20033"));
    EXPECT_NE(Utils::SipHash::GetHashValue("define_20056a"), Utils::SipHash::GetHashValue("tefine_20056a"));

    EXPECT_NE(Utils::SipHash::GetHashValue("define_4002"), Utils::SipHash::GetHashValue("Vefine_4002"));
    EXPECT_NE(Utils::SipHash::GetHashValue("define_macr"), Utils::SipHash::GetHashValue("Define_macr"));
    EXPECT_NE(Utils::SipHash::GetHashValue("Define_3003"), Utils::SipHash::GetHashValue("define_3003"));
    EXPECT_NE(Utils::SipHash::GetHashValue("define_2003"), Utils::SipHash::GetHashValue("lefine_2003"));
    EXPECT_NE(Utils::SipHash::GetHashValue("define20056a"), Utils::SipHash::GetHashValue("tefine20056a"));

    EXPECT_NE(Utils::SipHash::GetHashValue("define_400"), Utils::SipHash::GetHashValue("Vefine_400"));
    EXPECT_NE(Utils::SipHash::GetHashValue("define_mac"), Utils::SipHash::GetHashValue("Define_mac"));
    EXPECT_NE(Utils::SipHash::GetHashValue("Define_300"), Utils::SipHash::GetHashValue("define_300"));
    EXPECT_NE(Utils::SipHash::GetHashValue("define_200"), Utils::SipHash::GetHashValue("lefine_200"));
    EXPECT_NE(Utils::SipHash::GetHashValue("define_200"), Utils::SipHash::GetHashValue("tefine_200"));

    EXPECT_NE(Utils::SipHash::GetHashValue("define_40"), Utils::SipHash::GetHashValue("Vefine_40"));
    EXPECT_NE(Utils::SipHash::GetHashValue("define_ma"), Utils::SipHash::GetHashValue("Define_ma"));
    EXPECT_NE(Utils::SipHash::GetHashValue("Define_30"), Utils::SipHash::GetHashValue("define_30"));
    EXPECT_NE(Utils::SipHash::GetHashValue("define_20"), Utils::SipHash::GetHashValue("lefine_20"));
    EXPECT_NE(Utils::SipHash::GetHashValue("define_20"), Utils::SipHash::GetHashValue("tefine_20"));

    EXPECT_NE(Utils::SipHash::GetHashValue("define_20"), Utils::SipHash::GetHashValue("degine_20"));
    EXPECT_NE(Utils::SipHash::GetHashValue("define_20"), Utils::SipHash::GetHashValue("dEfine_20"));
    EXPECT_NE(Utils::SipHash::GetHashValue("define_20"), Utils::SipHash::GetHashValue("definE_20"));
    EXPECT_NE(Utils::SipHash::GetHashValue("define_20"), Utils::SipHash::GetHashValue("define_21"));
    EXPECT_NE(Utils::SipHash::GetHashValue("define_20"), Utils::SipHash::GetHashValue("define'20"));

    EXPECT_NE(Utils::SipHash::GetHashValue("abcdefghi"), Utils::SipHash::GetHashValue("dbcdefghi"));
    EXPECT_NE(Utils::SipHash::GetHashValue("pleasecom"), Utils::SipHash::GetHashValue("jleasecom"));
    EXPECT_NE(Utils::SipHash::GetHashValue("tutututut"), Utils::SipHash::GetHashValue("dutututut"));
    EXPECT_NE(Utils::SipHash::GetHashValue("dontcome!"), Utils::SipHash::GetHashValue("Vontcome!"));
    EXPECT_NE(Utils::SipHash::GetHashValue("deception"), Utils::SipHash::GetHashValue("teception"));

    EXPECT_NE(Utils::SipHash::GetHashValue("1abcdefghi"), Utils::SipHash::GetHashValue("1dbcdefghi"));
    EXPECT_NE(Utils::SipHash::GetHashValue("dpleasecom"), Utils::SipHash::GetHashValue("djleasecom"));
    EXPECT_NE(Utils::SipHash::GetHashValue("ktutututut"), Utils::SipHash::GetHashValue("kdutututut"));
    EXPECT_NE(Utils::SipHash::GetHashValue("kdontcome!"), Utils::SipHash::GetHashValue("kVontcome!"));
    EXPECT_NE(Utils::SipHash::GetHashValue("kdeception"), Utils::SipHash::GetHashValue("kteception"));

    EXPECT_NE(Utils::SipHash::GetHashValue("define_4"), Utils::SipHash::GetHashValue("Vefine_4"));
    EXPECT_NE(Utils::SipHash::GetHashValue("define_m"), Utils::SipHash::GetHashValue("Define_m"));
    EXPECT_NE(Utils::SipHash::GetHashValue("Define_3"), Utils::SipHash::GetHashValue("define_3"));
    EXPECT_NE(Utils::SipHash::GetHashValue("define_2"), Utils::SipHash::GetHashValue("lefine_2"));
    EXPECT_NE(Utils::SipHash::GetHashValue("define_2"), Utils::SipHash::GetHashValue("tefine_2"));

    EXPECT_NE(Utils::SipHash::GetHashValue("a"), Utils::SipHash::GetHashValue("d"));
    EXPECT_NE(Utils::SipHash::GetHashValue("t"), Utils::SipHash::GetHashValue("d"));
    EXPECT_NE(Utils::SipHash::GetHashValue("D"), Utils::SipHash::GetHashValue("d"));

    EXPECT_NE(Utils::SipHash::GetHashValue("amamamam"), Utils::SipHash::GetHashValue("dmamamam"));
    EXPECT_NE(Utils::SipHash::GetHashValue("amamamam"), Utils::SipHash::GetHashValue("amdmamam"));
    EXPECT_NE(Utils::SipHash::GetHashValue("amamamam"), Utils::SipHash::GetHashValue("amamdmam"));
    EXPECT_NE(Utils::SipHash::GetHashValue("amamamam"), Utils::SipHash::GetHashValue("amamamdm"));
    EXPECT_NE(Utils::SipHash::GetHashValue("amamamama"), Utils::SipHash::GetHashValue("amamamamd"));

    EXPECT_NE(Utils::SipHash::GetHashValue("bambambomba"), Utils::SipHash::GetHashValue("nambambomba"));
    EXPECT_NE(Utils::SipHash::GetHashValue("bambambombak"), Utils::SipHash::GetHashValue("nambambombak"));
    EXPECT_NE(Utils::SipHash::GetHashValue("bambambombalm"), Utils::SipHash::GetHashValue("nambambombalm"));
    EXPECT_NE(Utils::SipHash::GetHashValue("bambambombaese"), Utils::SipHash::GetHashValue("nambambombaese"));
    EXPECT_NE(Utils::SipHash::GetHashValue("bambambombadodo"), Utils::SipHash::GetHashValue("nambambombadodo"));
}

TEST(UtilsTest, IsUnderFlowFloat)
{
    auto underUse = [](const std::string& value) {
        auto stringValue = value;
        stringValue.erase(std::remove(stringValue.begin(), stringValue.end(), '_'), stringValue.end());
        return IsUnderFlowFloat(stringValue);
    };

    EXPECT_EQ(underUse("1_e10000000000000000000000000000000"), false);
    EXPECT_EQ(underUse("1e10000000000000000000000000000000"), false);
    EXPECT_EQ(underUse("-1_e10000000000000000000000000000000"), false);
    EXPECT_EQ(underUse("-1E10000"), false);
    EXPECT_EQ(underUse("1E10000"), false);
    EXPECT_EQ(underUse("-1E-10000"), true);
    EXPECT_EQ(underUse("1E-10000"), true);
    EXPECT_EQ(underUse("0_."
                       "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
                       "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
                       "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
                       "0000000000000000000000000000000000000000000001"),
        true);
    EXPECT_EQ(underUse("."
                       "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
                       "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
                       "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
                       "0000000000000000000000000000000000000000000001"),
        true);
    EXPECT_EQ(underUse(".123e-10000"), true);
    EXPECT_EQ(underUse("0x1p-100000000000000000000000000000000000000000"), true);
    // By default, this function is invoked only when it is beyond the C++ representation range.
    // This should not occur in actual calls.
    EXPECT_EQ(underUse("1.0"), false);
}
