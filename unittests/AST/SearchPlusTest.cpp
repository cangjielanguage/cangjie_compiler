// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/AST/Searcher.h"

#include <string>
#include <vector>

#include "Collector.h"
#include "TestCompilerInstance.h"

#include "cangjie/Parse/Parser.h"

#include "gtest/gtest.h"

using namespace Cangjie;
using namespace AST;

class SearchPlusTest : public testing::Test {
protected:
    void SetUp() override
    {
#ifdef _WIN32
        srcPath = projectPath + "\\unittests\\AST\\CangjieFiles\\";
        packagePath = packagePath + "\\";
#else
        std::string command;
        int err = 0;
        if (FileUtil::FileExist("testTempFiles")) {
            command = "rm -rf testTempFiles";
            err = system(command.c_str());
            ASSERT_EQ(0, err);
        }
        command = "mkdir -p testTempFiles";
        err = system(command.c_str());
        ASSERT_EQ(0, err);
        srcPath = projectPath + "/unittests/AST/CangjieFiles/";
        packagePath = packagePath + "/";
#endif
    }

#ifdef PROJECT_SOURCE_DIR
    // Gets the absolute path of the project from the compile parameter.
    const std::string projectPath = FileUtil::JoinPath(PROJECT_SOURCE_DIR, "");
#else
    // Just in case, give it a default value.
    // Assume the initial is in the build directory.
    std::string projectPath = FileUtil::JoinPath(FileUtil::JoinPath("..", ".."), "..");
#endif

    DiagnosticEngine diag;
    CompilerInvocation invocation;
    std::unique_ptr<CompilerInstance> instance;
    std::string packagePath = "testTempFiles";
    std::string srcPath;
};

TEST_F(SearchPlusTest, IllegalParametersTest)
{
    auto srcFile = srcPath + "testfile_search_01n.cj";
    std::string failedReason;
    auto content = FileUtil::ReadFileContent(srcFile, failedReason);
    if (!content.has_value()) {
        diag.DiagnoseRefactor(
            DiagKindRefactor::module_read_file_to_buffer_failed, DEFAULT_POSITION, srcFile, failedReason);
    }

    std::unique_ptr<CompilerInstance> instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    Parser parser(0, content.value(), diag, instance->GetSourceManager());
    OwnedPtr<Package> pkg = MakeOwned<Package>();
    pkg->files.emplace_back(parser.ParseTopLevel());
    ASTContext ctx(diag, *pkg);
    ScopeManager scopeManager;
    Collector collector(scopeManager);
    collector.BuildSymbolTable(ctx, pkg.get());

    std::vector<std::string> srcFiles;
    std::vector<std::string> srcs = {srcFile};
    for (auto& src : srcs) {
        srcFiles.push_back(src);
    }
    instance->srcFilePaths = srcFiles;
    instance->PerformParse();

    Searcher searcher;
    std::vector<Symbol*> res = searcher.Search(ctx, "");
    EXPECT_EQ(res.size(), 0);
    res = searcher.Search(ctx, " ");
    EXPECT_EQ(res.size(), 0);
    res = searcher.Search(ctx, "name:Day");
    EXPECT_EQ(res.size(), 13);
    res = searcher.Search(ctx, "name:Day!name:Day");
    EXPECT_EQ(res.size(), 0);
    res = searcher.Search(ctx, "name:Day&&name:Day");
    EXPECT_EQ(res.size(), 13);
    res = searcher.Search(ctx, "name:Day||name:Day");
    EXPECT_EQ(res.size(), 13);
    res = searcher.Search(ctx, "name:name");
    EXPECT_EQ(res.size(), 0);
    res = searcher.Search(ctx, "name:Day,name:Time");
    EXPECT_EQ(res.size(), 0);
    res = searcher.Search(ctx, "name:中"); // EXPECTED:illegal symbol '中'
    EXPECT_TRUE(res.empty());
    res = searcher.Search(ctx, "key:value"); // EXPECTED:Unknow query term: key!
    EXPECT_TRUE(res.empty());
    res = searcher.Search(ctx,
        "name:abcdefghijklmnopqrstuvwxyabcdefghijklmnopqrstuvwxyabcdefghijklmnopqrstuvwxyabcdefghijklmnopqrstuvwxy");
    EXPECT_EQ(res.size(), 1);
    res = searcher.Search(ctx, "name:**"); // EXPECTED :1:6: should be identifer, positive integer, 'foo*' or '*foo'
    EXPECT_TRUE(res.empty());
    res = searcher.Search(ctx, "name:D*y");
    EXPECT_EQ(res.size(), 0);
    res = searcher.Search(ctx, "name:*i*"); // EXPECTED :1:6: should be identifer, positive integer, 'foo*' or '*foo'
    EXPECT_TRUE(res.empty());
    res = searcher.Search(ctx, "name:Da?");
    EXPECT_EQ(res.size(), 0);

    res = searcher.Search(
        ctx, "id:1000"); // EXPECTED:Searcher error: id number '128' past the end of array and it would be ignored.
    EXPECT_TRUE(res.empty());
    res = searcher.Search(
        ctx, "id:205 && name:n && scope_name:a0j0k0i && ast_kind:ref_expr && scope_level:3 && _<=(0, 84, 32)");
    EXPECT_EQ(res.size(), 0);

    res = searcher.Search(ctx, "_=(0, 87, 32) ");
    EXPECT_EQ(res.size(), 5);
    res = searcher.Search(ctx, "_>(-1, -86, -32) ");
    EXPECT_TRUE(res.empty());

    res = searcher.Search(ctx, "scope_name:a0j0*");
    EXPECT_EQ(res.size(), 49);
    res = searcher.Search(ctx, "scope_name:*0i"); // scope_name not support suffix
    EXPECT_TRUE(res.empty());
    res = searcher.Search(ctx, "scope_name:xxxxxxxx");
    EXPECT_TRUE(res.empty());

    res = searcher.Search(ctx, "ast_kind:generic_param_decl");
    EXPECT_EQ(res.size(), 2);
    res = searcher.Search(ctx, "ast_kind:struct_decl");
    EXPECT_EQ(res.size(), 2);
    res = searcher.Search(ctx, "ast_kind:func_decl");
    EXPECT_EQ(res.size(), 13);
    res = searcher.Search(ctx, "ast_kind:var_decl");
    EXPECT_EQ(res.size(), 22);
    res = searcher.Search(ctx, "ast_kind:class_decl");
    EXPECT_EQ(res.size(), 6);
    res = searcher.Search(ctx, "ast_kind:interface_decl");
    EXPECT_EQ(res.size(), 1);
    res = searcher.Search(ctx, "ast_kind:enum_decl");
    EXPECT_EQ(res.size(), 2);
    res = searcher.Search(ctx, "ast_kind:type_alias_decl");
    EXPECT_EQ(res.size(), 0);
    res = searcher.Search(ctx, "ast_kind:class_like_decl");
    EXPECT_EQ(res.size(), 0);
    res = searcher.Search(ctx, "ast_kind:*decl");
    EXPECT_EQ(res.size(), 49);
    res = searcher.Search(ctx, "ast_kind:c*"); // ast_kind not support prefix
    EXPECT_TRUE(res.empty());
    res = searcher.Search(ctx, "ast_kind:*_decl");
    EXPECT_EQ(res.size(), 49);
    res = searcher.Search(ctx, "ast_kind:xxxxxxxx");
    EXPECT_TRUE(res.empty());
    // AST must be released before ASTContext for correct symbol detaching.
    pkg.reset();
    instance.reset();
}

TEST_F(SearchPlusTest, ScopeTest)
{
    auto srcFile = srcPath + "testfile_search_02n.cj";
    std::string failedReason;
    auto content = FileUtil::ReadFileContent(srcFile, failedReason);
    if (!content.has_value()) {
        diag.DiagnoseRefactor(
            DiagKindRefactor::module_read_file_to_buffer_failed, DEFAULT_POSITION, srcFile, failedReason);
    }

    std::unique_ptr<CompilerInstance> instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    Parser parser(0, content.value(), diag, instance->GetSourceManager());
    OwnedPtr<Package> pkg = MakeOwned<Package>();
    pkg->files.emplace_back(parser.ParseTopLevel());
    ASTContext ctx(diag, *pkg);

    ScopeManager scopeManager;
    Collector collector(scopeManager);
    collector.BuildSymbolTable(ctx, pkg.get());
    std::vector<std::string> srcFiles;
    std::vector<std::string> srcs = {srcFile};
    for (auto& src : srcs) {
        srcFiles.push_back(src);
    }
    instance->srcFilePaths = srcFiles;
    instance->PerformParse();

    diag.ClearError();

    Searcher searcher;
    std::vector<Symbol*> res = searcher.Search(ctx, "name:Friday && scope_level:10 && ast_kind:class_decl");
    EXPECT_EQ(res.size(), 1);

    std::string curScopeName;
    res = searcher.Search(ctx, "_ = (0, 43, 28)");
    curScopeName = ScopeManagerApi::GetScopeNameWithoutTail(res[0]->scopeName);
    std::vector<std::string> result;
    while (!curScopeName.empty()) {
        std::string q = "scope_name: " + curScopeName + "&& _ < (0, 43, 28)";
        res = searcher.Search(ctx, q);
        for (auto tmp : res) {
            if (tmp->name.empty() || tmp->scopeName == "a") {
                continue;
            }
            result.push_back(tmp->name);
        }
        curScopeName = ScopeManagerApi::GetParentScopeName(curScopeName);
    }
    EXPECT_EQ(result.size(), 22);
    EXPECT_EQ(result[0], "thurday");
    EXPECT_EQ(result[1], "Int32");
    EXPECT_EQ(result[2], "a");
    EXPECT_EQ(result[3], "wednesday");
    EXPECT_EQ(result[4], "Int32");
    EXPECT_EQ(result[5], "a");
    EXPECT_EQ(result[6], "tuesday");
    EXPECT_EQ(result[7], "Int32");
    EXPECT_EQ(result[8], "a");
    EXPECT_EQ(result[9], "monday");
    EXPECT_EQ(result[10], "Int32");
    EXPECT_EQ(result[11], "a");
    EXPECT_EQ(result[12], "String");
    EXPECT_EQ(result[13], "String");
    EXPECT_EQ(result[14], "c");
    EXPECT_EQ(result[15], "String");
    EXPECT_EQ(result[16], "String");
    EXPECT_EQ(result[17], "b");
    EXPECT_EQ(result[18], "Int32");
    EXPECT_EQ(result[19], "a");
    // AST must be released before ASTContext for correct symbol detaching.
    pkg.reset();
    instance.reset();
}

TEST_F(SearchPlusTest, DISABLED_MultiFileTest)
{
#ifdef _WIN32
    srcPath = srcPath + "\\pkgs";
#elif __unix__
    srcPath = srcPath + "/pkgs";
#endif

    CompilerInvocation invocation;
    DiagnosticEngine diag;
    std::unique_ptr<CompilerInstance> instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->srcDirs = {srcPath};
    instance->invocation.globalOptions.implicitPrelude = true;
#ifdef __x86_64__
    instance->invocation.globalOptions.target.arch = Cangjie::Triple::ArchType::X86_64;
#else
    instance->invocation.globalOptions.target.arch = Cangjie::Triple::ArchType::AARCH64;
#endif
#ifdef _WIN32
    instance->invocation.globalOptions.target.os = Cangjie::Triple::OSType::WINDOWS;
#elif __unix__
    instance->invocation.globalOptions.target.os = Cangjie::Triple::OSType::LINUX;
#endif
    instance->Compile(CompileStage::SEMA);

    Searcher searcher;
    ASTContext* ctx = instance->GetASTContextByPackage(instance->GetSourcePackages()[0]);
    ASSERT_TRUE(ctx != nullptr);
    std::vector<Symbol*> res = searcher.Search(*ctx, "name:Time && ast_kind:class_decl");
    EXPECT_EQ(res.size(), 2);
    EXPECT_EQ(res[0]->hashID.fieldID, 55);
    EXPECT_EQ(res[1]->hashID.fieldID, 55);
}
