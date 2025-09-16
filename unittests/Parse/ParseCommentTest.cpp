// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * Test cases for comment groups attach.
 */
#include "gtest/gtest.h"

#include "TestCompilerInstance.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Parse/Parser.h"
#include "cangjie/Utils/CastingTemplate.h"

using namespace Cangjie;
using namespace AST;

class ParseCommentTest : public testing::Test {
protected:
    void SetUp() override
    {
#ifdef _WIN32
        srcPath = projectPath + "\\unittests\\Parse\\ParseCangjieFiles\\";
#else
        srcPath = projectPath + "/unittests/Parse/ParseCangjieFiles/";
#endif
#ifdef __x86_64__
        invocation.globalOptions.target.arch = Cangjie::Triple::ArchType::X86_64;
#else
        invocation.globalOptions.target.arch = Cangjie::Triple::ArchType::AARCH64;
#endif
#ifdef _WIN32
        invocation.globalOptions.target.os = Cangjie::Triple::OSType::WINDOWS;
        invocation.globalOptions.executablePath = projectPath + "\\output\\bin\\";
#elif __unix__
        invocation.globalOptions.target.os = Cangjie::Triple::OSType::LINUX;
        invocation.globalOptions.executablePath = projectPath + "/output/bin/";
#endif
        invocation.globalOptions.importPaths = {definePath};
    }

#ifdef PROJECT_SOURCE_DIR
    // Gets the absolute path of the project from the compile parameter.
    std::string projectPath = PROJECT_SOURCE_DIR;
#else
    // Just in case, give it a default value.
    // Assume the initial is in the build directory.
    std::string projectPath = "..";
#endif
    std::string srcPath;
    std::string definePath;
    DiagnosticEngine diag;
    SourceManager sm;
    std::string code;
    CompilerInvocation invocation;
    std::unique_ptr<TestCompilerInstance> instance;
};

TEST_F(ParseCommentTest, ParseMacroNodes)
{
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->compileOnePackageFromSrcFiles = true;
    instance->srcFilePaths = {srcPath + "Test.cj"};
    invocation.globalOptions.outputMode = GlobalOptions::OutputMode::STATIC_LIB;
    instance->Compile(CompileStage::MACRO_EXPAND);
    std::vector<Ptr<Node>> ptrs;
    size_t cmgNum{0};
    auto collectNodes = [&ptrs, &cmgNum](Ptr<Node> curNode) -> VisitAction {
        const auto& nodeCms = curNode->comments;
        cmgNum += nodeCms.leadingComments.size() + nodeCms.trailingComments.size() + nodeCms.innerComments.size();
        if (curNode->astKind == ASTKind::ANNOTATION || curNode->astKind == ASTKind::MODIFIER) {
            return VisitAction::SKIP_CHILDREN;
        }
        if (curNode->astKind == ASTKind::FILE) {
            return VisitAction::WALK_CHILDREN;
        }
        ptrs.push_back(curNode);
        return VisitAction::WALK_CHILDREN;
    };
    Walker macWalker(instance->GetSourcePackages()[0]->files[0], collectNodes);
    macWalker.Walk();
    EXPECT_EQ(cmgNum, 0);
    std::cout << std::endl;
}

TEST_F(ParseCommentTest, TrailComments)
{
    code = R"(
class A {
    let m1 = 1

    // c0 rule 3
} // c1 rule 1
// c2 rule 1

main() {
}
    )";
    Parser parser(code, diag, sm, {0, 1, 1}, true);
    OwnedPtr<File> file = parser.ParseTopLevel();
    size_t attchNum = 0;
    const size_t trlCgNumOfClassA = 2;
    std::vector<Ptr<Node>> testNodes;
    Walker walker(file.get(), [&attchNum, &testNodes](Ptr<Node> node) -> VisitAction {
        const auto& nodeCms = node->comments;
        if (node->astKind == ASTKind::CLASS_DECL || node->astKind == ASTKind::VAR_DECL) {
            testNodes.emplace_back(node);
        }
        attchNum += nodeCms.leadingComments.size() + nodeCms.trailingComments.size() + nodeCms.innerComments.size();
        return VisitAction::WALK_CHILDREN;
    });
    walker.Walk();
    EXPECT_EQ(testNodes.size(), 2);
    for (auto node : testNodes) {
        const auto& nodeTrailCms = node->comments.trailingComments;
        if (node->astKind == ASTKind::CLASS_DECL) {
            ASSERT_EQ(nodeTrailCms.size(), trlCgNumOfClassA);
            EXPECT_TRUE(nodeTrailCms[0].cms.front().info.Value().find("c1") != std::string::npos);
            EXPECT_TRUE(nodeTrailCms[1].cms.front().info.Value().find("c2") != std::string::npos);
        } else if (node->astKind == ASTKind::VAR_DECL) {
            ASSERT_EQ(nodeTrailCms.size(), 1);
            EXPECT_TRUE(nodeTrailCms[0].cms.front().info.Value().find("c0") != std::string::npos);
        }
    }
    EXPECT_EQ(attchNum, 3);
}

TEST_F(ParseCommentTest, leadingComments)
{
    code = R"(
class A {
    // c0 rule 3

    // c1 rule 2
    let m1 = 1
}
// c2 rule 3
main() {
}
    )";
    Parser parser(code, diag, sm, {0, 1, 1}, true);
    OwnedPtr<File> file = parser.ParseTopLevel();
    size_t attchNum = 0;
    const size_t trlCgNumOfVarDecl = 2;
    std::vector<Ptr<Node>> testNodes;
    Walker walker(file.get(), [&attchNum, &testNodes](Ptr<Node> node) -> VisitAction {
        const auto& nodeCms = node->comments;
        if (node->astKind == ASTKind::VAR_DECL || node->astKind == ASTKind::MAIN_DECL) {
            testNodes.emplace_back(node);
        }
        attchNum += nodeCms.leadingComments.size() + nodeCms.trailingComments.size() + nodeCms.innerComments.size();
        return VisitAction::WALK_CHILDREN;
    });
    walker.Walk();
    EXPECT_EQ(testNodes.size(), 2);
    for (auto node : testNodes) {
        const auto& nodeLeadCms = node->comments.leadingComments;
        if (node->astKind == ASTKind::VAR_DECL) {
            ASSERT_EQ(nodeLeadCms.size(), trlCgNumOfVarDecl);
            EXPECT_TRUE(nodeLeadCms[0].cms.front().info.Value().find("c0") != std::string::npos);
            EXPECT_TRUE(nodeLeadCms[1].cms.front().info.Value().find("c1") != std::string::npos);
        } else if (node->astKind == ASTKind::MAIN_DECL) {
            ASSERT_EQ(nodeLeadCms.size(), 1);
            EXPECT_TRUE(nodeLeadCms[0].cms.front().info.Value().find("c2") != std::string::npos);
        }
    }
    EXPECT_EQ(attchNum, 3);
}

TEST_F(ParseCommentTest, InnerComents)
{
    code = R"(
    main(/* c0*/) {
        // c1
    }
    )";
    Parser parser(code, diag, sm, {0, 1, 1}, true);
    OwnedPtr<File> file = parser.ParseTopLevel();
    size_t attchNum = 0;
    std::vector<Ptr<Node>> testNodes;
    Walker walker(file.get(), [&attchNum, &testNodes](Ptr<Node> node) -> VisitAction {
        const auto& nodeCms = node->comments;
        if (node->astKind == ASTKind::FUNC_PARAM_LIST || node->astKind == ASTKind::BLOCK) {
            testNodes.emplace_back(node);
        }
        attchNum += nodeCms.leadingComments.size() + nodeCms.trailingComments.size() + nodeCms.innerComments.size();
        return VisitAction::WALK_CHILDREN;
    });
    walker.Walk();
    EXPECT_EQ(testNodes.size(), 2);
    for (auto node : testNodes) {
        const auto& nodeInnerCms = node->comments.innerComments;
        if (node->astKind == ASTKind::FUNC_PARAM_LIST) {
            ASSERT_EQ(nodeInnerCms.size(), 1);
            EXPECT_TRUE(nodeInnerCms[0].cms.front().info.Value().find("c0") != std::string::npos);
        } else if (node->astKind == ASTKind::BLOCK) {
            ASSERT_EQ(nodeInnerCms.size(), 1);
            EXPECT_TRUE(nodeInnerCms[0].cms.front().info.Value().find("c1") != std::string::npos);
        }
    }
    EXPECT_EQ(attchNum, 2);
}

TEST_F(ParseCommentTest, MultStyComents)
{
    code = R"(
    /**
     * c0 lead package spec
     */
    package comment

    import std.ast.*

    // c1 lead Macro Decl of M0
    @M0
    public class A { // c2 lead var decl of var a
        // c3 lead var decl of var a rule 2
        var a = 1 // c4 trail var decl of var a
        // c5 trail var decl of  var a
    } // c6 trail Macro Decl of M0
    // c7 lead funcDecl of foo rule 2
    public func foo(){/* c8 inner funcBlock*/}

    // c9 lead funcDecl of bar
    foreign func bar(){ }

    main () {
        1 + 2
    }
    // cEnd trail mainDecl rule 1
    )";

    Parser parser(code, diag, sm, {0, 1, 1}, true);
    OwnedPtr<File> file = parser.ParseTopLevel();
    size_t attchNum = 0;
    std::vector<Ptr<Node>> testNodes;
    const std::set<ASTKind> collectKinds{ASTKind::PACKAGE_SPEC, ASTKind::MACRO_EXPAND_DECL, ASTKind::VAR_DECL,
        ASTKind::FUNC_DECL, ASTKind::BLOCK, ASTKind::MAIN_DECL};
    Walker walker(file.get(), [&](Ptr<Node> node) -> VisitAction {
        const auto& nodeCms = node->comments;
        attchNum += nodeCms.leadingComments.size() + nodeCms.trailingComments.size() + nodeCms.innerComments.size();
        if (collectKinds.find(node->astKind) == collectKinds.end()) {
            return VisitAction::WALK_CHILDREN;
        }
        if (node->astKind == ASTKind::BLOCK) {
            if (node->begin.line == 17) {
                testNodes.emplace_back(node);
            }
        } else {
            testNodes.emplace_back(node);
        }
        return VisitAction::WALK_CHILDREN;
    });
    walker.Walk();
    EXPECT_EQ(testNodes.size(), 7);
    for (auto node : testNodes) {
        const auto& nodeLeadCms = node->comments.leadingComments;
        const auto& nodeInnerCms = node->comments.innerComments;
        const auto& nodeTrailCms = node->comments.trailingComments;
        if (node->astKind == ASTKind::PACKAGE_SPEC) {
            ASSERT_EQ(nodeLeadCms.size(), 1);
            EXPECT_TRUE(nodeLeadCms[0].cms.front().info.Value().find("c0") != std::string::npos);
        } else if (node->astKind == ASTKind::MACRO_EXPAND_DECL) {
            auto d = StaticAs<ASTKind::MACRO_EXPAND_DECL>(node)->invocation.decl.get();
            ASSERT_TRUE(d);
            ASSERT_EQ(d->comments.leadingComments.size(), 1);
            EXPECT_TRUE(d->comments.leadingComments[0].cms.front().info.Value().find("c1") != std::string::npos);
            ASSERT_EQ(d->comments.trailingComments.size(), 1);
            EXPECT_TRUE(d->comments.trailingComments[0].cms.front().info.Value().find("c6") != std::string::npos);
        } else if (node->astKind == ASTKind::VAR_DECL) {
            ASSERT_EQ(nodeLeadCms.size(), 2);
            EXPECT_TRUE(nodeLeadCms[0].cms.front().info.Value().find("c2") != std::string::npos);
            EXPECT_TRUE(nodeLeadCms[1].cms.front().info.Value().find("c3") != std::string::npos);
            ASSERT_EQ(nodeTrailCms.size(), 2);
            EXPECT_TRUE(nodeTrailCms[0].cms.front().info.Value().find("c4") != std::string::npos);
            EXPECT_TRUE(nodeTrailCms[1].cms.front().info.Value().find("c5") != std::string::npos);
        } else if (node->astKind == ASTKind::FUNC_DECL) {
            auto fd = StaticCast<FuncDecl>(node);
            if (fd->identifier == "foo") {
                ASSERT_EQ(nodeLeadCms.size(), 1);
                EXPECT_TRUE(nodeLeadCms[0].cms.front().info.Value().find("c7") != std::string::npos);
            } else if (fd->identifier == "bar") {
                ASSERT_EQ(nodeLeadCms.size(), 1);
                EXPECT_TRUE(nodeLeadCms[0].cms.front().info.Value().find("c9") != std::string::npos);
            }
        } else if (node->astKind == ASTKind::BLOCK) {
            ASSERT_EQ(nodeInnerCms.size(), 1);
            EXPECT_TRUE(nodeInnerCms[0].cms.front().info.Value().find("c8") != std::string::npos);
        } else if (node->astKind == ASTKind::MAIN_DECL) {
            ASSERT_EQ(nodeTrailCms.size(), 1);
            EXPECT_TRUE(nodeTrailCms[0].cms.front().info.Value().find("cEnd") != std::string::npos);
        }
    }
    EXPECT_EQ(attchNum, 11);
}
