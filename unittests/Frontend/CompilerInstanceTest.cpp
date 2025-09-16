// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "gtest/gtest.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"
#include "cangjie/Basic/SourceManager.h"
#include "cangjie/FrontendTool/DefaultCompilerInstance.h"
#include "cangjie/Utils/FileUtil.h"

#include <memory>
#include <string>

using namespace Cangjie;
using namespace AST;

class CompilerInstanceTest : public ::testing::Test {
public:
    CompilerInvocation invocation;
    DiagnosticEngine diag;
    std::string projectPath;
    std::string cangjieHome;

protected:
    void SetUp() override
    {
#ifdef PROJECT_SOURCE_DIR
        // Gets the absolute path of the project from the compile parameter.
        projectPath = PROJECT_SOURCE_DIR;
        cangjieHome = FileUtil::JoinPath(FileUtil::JoinPath(PROJECT_SOURCE_DIR, "build"), "build");
#else
        // Just in case, give it a default value. Assume the initial is in the build directory.
        projectPath = "..";
        cangjieHome = FileUtil::JoinPath(FileUtil::JoinPath(".", "build"), "build");
#endif
#ifdef __x86_64__
        invocation.globalOptions.target.arch = Cangjie::Triple::ArchType::X86_64;
#else
        invocation.globalOptions.target.arch = Cangjie::Triple::ArchType::AARCH64;
#endif
#ifdef _WIN32
        invocation.globalOptions.target.os = Cangjie::Triple::OSType::WINDOWS;
#elif __unix__
        invocation.globalOptions.target.os = Cangjie::Triple::OSType::LINUX;
#endif
        invocation.globalOptions.compilePackage = true;
        invocation.globalOptions.compilationCachedPath = ".";
    };
    std::string code = R"(
        package pkg1
        class C{}
    )";
};

TEST_F(CompilerInstanceTest, DISABLED_FullCompile)
{
    std::unique_ptr<DefaultCompilerInstance> instance = std::make_unique<DefaultCompilerInstance>(invocation, diag);
    instance->srcDirs.emplace(FileUtil::JoinPath(projectPath, "unittests/Frontend/FullCompile/src"));
    instance->compileOnePackageFromSrcFiles = false;
    instance->cangjieHome = cangjieHome;
    instance->Compile();
    auto pkgs = instance->GetSourcePackages();
    ASSERT_EQ(pkgs.size(), 1);
    auto pkg = pkgs[0];
    ASTContext* ctx = instance->GetASTContextByPackage(pkg);
    ASSERT_TRUE(ctx != nullptr);
    EXPECT_EQ(ctx->curPackage, pkg);
}

TEST_F(CompilerInstanceTest, DISABLED_GetAllVisibleExtendMembers01)
{
    std::unique_ptr<DefaultCompilerInstance> instance = std::make_unique<DefaultCompilerInstance>(invocation, diag);
    instance->srcDirs.emplace(FileUtil::JoinPath(projectPath, "unittests/Frontend/FullCompile/src"));
    instance->compileOnePackageFromSrcFiles = false;
    instance->cangjieHome = cangjieHome;
    instance->Compile();
    auto pkgs = instance->GetSourcePackages();
    ASSERT_EQ(pkgs.size(), 1);
    ASTContext* ctx = instance->GetASTContextByPackage(pkgs[0]);
    ASSERT_NE(ctx, nullptr);
    Searcher searcher;
    auto extendSyms = searcher.Search(*ctx, "ast_kind:extend_decl", Sort::posAsc);
    // Int64 -> #{Eqq}
    auto memberSet1 = instance->GetAllVisibleExtendMembers(
        StaticAs<ASTKind::EXTEND_DECL>(extendSyms[0]->node)->extendedType->ty, *extendSyms[0]->node->curFile);
    bool containExtendMember = false;
    for (auto member : memberSet1) {
        EXPECT_TRUE(member->astKind == ASTKind::FUNC_DECL || member->astKind == ASTKind::PROP_DECL);
        if (member->identifier.Val() == "g") {
            containExtendMember = true;
            break;
        }
    }
    EXPECT_TRUE(containExtendMember);
    auto classSyms = searcher.Search(*ctx, "(ast_kind:class_decl && name:A)");
    containExtendMember = false;
    // class A -> #{Eqq}
    auto memberSet2 = instance->GetAllVisibleExtendMembers(
        RawStaticCast<InheritableDecl*>(classSyms[0]->node), *extendSyms[0]->node->curFile);
    for (auto member : memberSet2) {
        EXPECT_TRUE(member->astKind == ASTKind::FUNC_DECL || member->astKind == ASTKind::PROP_DECL);
        if (member->identifier.Val() == "g") {
            containExtendMember = true;
            break;
        }
    }
    EXPECT_TRUE(containExtendMember);
}

TEST_F(CompilerInstanceTest, Comments)
{
    std::unique_ptr<DefaultCompilerInstance> instance = std::make_unique<DefaultCompilerInstance>(invocation, diag);
    instance->srcDirs.emplace(FileUtil::JoinPath(projectPath, "unittests/Frontend/FullCompile/src"));
    instance->compileOnePackageFromSrcFiles = false;
    instance->cangjieHome = cangjieHome;
    instance->Compile();
    auto pkgs = instance->GetSourcePackages();
    ASSERT_EQ(pkgs.size(), 1);
    bool oneComments{false};
    for (auto& file : pkgs[0]->files) {
        auto comments = instance->GetSourceManager().GetSource(file->begin.fileID).offsetCommentsMap;
        if (comments.size() == 5) {
            oneComments = true;
            break;
        }
    }
    EXPECT_TRUE(oneComments);
}

TEST_F(CompilerInstanceTest, DISABLED_TrailingClosure)
{
    std::unique_ptr<DefaultCompilerInstance> instance = std::make_unique<DefaultCompilerInstance>(invocation, diag);
    instance->srcDirs.emplace(FileUtil::JoinPath(projectPath, "unittests/Frontend/TrailingClosure/src"));
    instance->compileOnePackageFromSrcFiles = false;
    instance->cangjieHome = cangjieHome;
    instance->Compile();
    auto pkgs = instance->GetSourcePackages();
    ASSERT_EQ(pkgs.size(), 1);
    ASTContext* ctx = instance->GetASTContextByPackage(pkgs[0]);
    ASSERT_NE(ctx, nullptr);
    Searcher searcher;
    auto syms = searcher.Search(*ctx, "name:i && ast_kind: ref_expr");
    // 3 * 2 of i.
    size_t n = 6;
    size_t isClonedNode = 0;
    for (size_t i = 0; i < n; i++) {
        ASSERT_TRUE(i < syms.size() && syms[i] && syms[i]->node);
        if (syms[i]->node->TestAttr(Attribute::IS_CLONED_SOURCE_CODE)) {
            ++isClonedNode;
        }
    }
    // Trailing Closure will be desugared without any cloned node.
    EXPECT_TRUE(isClonedNode == 0);
    syms = searcher.Search(*ctx, "name:f2 && ast_kind: ref_expr");
    EXPECT_TRUE(syms.size() == 1);
}
