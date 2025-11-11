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
            auto& cl = StaticCast<ClassDecl>(*node);
            ASSERT_EQ(cl.body->comments.innerComments.size(), 1);
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
    foreign func bar(): Unit

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
            auto d = StaticAs<ASTKind::MACRO_EXPAND_DECL>(node);
            ASSERT_TRUE(d);
            ASSERT_EQ(d->comments.leadingComments.size(), 1);
            EXPECT_TRUE(d->comments.leadingComments[0].cms.front().info.Value().find("c1") != std::string::npos);
        } else if (node->astKind == ASTKind::VAR_DECL) {
            ASSERT_EQ(nodeLeadCms.size(), 2);
            EXPECT_TRUE(nodeLeadCms[0].cms.front().info.Value().find("c2") != std::string::npos);
            EXPECT_TRUE(nodeLeadCms[1].cms.front().info.Value().find("c3") != std::string::npos);
            ASSERT_EQ(nodeTrailCms.size(), 1);
            EXPECT_TRUE(nodeTrailCms[0].cms.front().info.Value().find("c4") != std::string::npos);
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

TEST_F(ParseCommentTest, ParseError1)
{
    code = R"(// c9 lead funcDecl of bar
    foreign func bar(){ }

    main () {}
    )";

    Parser parser(code, diag, sm, {0, 1, 1}, true);
    OwnedPtr<File> file = parser.ParseTopLevel();
    EXPECT_EQ(file->comments.leadingComments[0].cms[0].info.Value(), "// c9 lead funcDecl of bar");
}

TEST_F(ParseCommentTest, Rule2)
{
    code = R"(
    /** file leading comments
     */
    
    var a = 1
    
    /** file trailing comments
     */
    )";
    Parser parser{code, diag, sm, {0, 1, 1}, true};
    OwnedPtr<File> file = parser.ParseTopLevel();
    ASSERT_EQ(file->comments.leadingComments.size(), 1);
    EXPECT_EQ(file->comments.leadingComments[0].cms.front().info.Value(), "/** file leading comments\n     */");
    ASSERT_EQ(file->comments.trailingComments.size(), 1);
    EXPECT_EQ(file->comments.trailingComments[0].cms.front().info.Value(), "/** file trailing comments\n     */");
}

TEST_F(ParseCommentTest, Rule3)
{
    code = R"(class A {
        var b = 1 // 1 trailing comment
        + 2
    }
    protected  /* comment belong to protected*/ unsafe func foo() {}
    protected
    // comment belong to unsafe
    unsafe
    func bar() {}
    unsafe
    // comment belong to unsafe
    protected
    func baz() {}
    )";
    Parser parser{code, diag, sm, {0, 1, 1}, true};
    OwnedPtr<File> file = parser.ParseTopLevel();
    ASSERT_EQ(file->decls.size(), 4);
    auto& clA = StaticCast<ClassDecl>(*file->decls[0]);
    auto& binit = StaticCast<BinaryExpr>(*StaticCast<VarDecl>(*clA.body->decls[0]).initializer);
    EXPECT_EQ(binit.leftExpr->comments.trailingComments[0].cms[0].info.Value(), "// 1 trailing comment");
    auto& foo = StaticCast<FuncDecl>(*file->decls[1]);
    ASSERT_EQ(foo.modifiers.begin()->comments.trailingComments.size(), 1);
    EXPECT_EQ(foo.modifiers.begin()->comments.trailingComments[0].cms[0].style, CommentStyle::TRAIL_CODE);
    EXPECT_EQ(foo.modifiers.begin()->comments.trailingComments[0].cms[0].info.Value(),
        "/* comment belong to protected*/");
    auto& bar = StaticCast<FuncDecl>(*file->decls[2]);
    auto unsafeBar = --bar.modifiers.end();
    ASSERT_EQ(unsafeBar->comments.leadingComments.size(), 1);
    EXPECT_EQ(unsafeBar->comments.leadingComments[0].cms[0].style, CommentStyle::LEAD_LINE);
    EXPECT_EQ(unsafeBar->comments.leadingComments[0].cms[0].info.Value(),
        "// comment belong to unsafe");
    auto& baz = StaticCast<FuncDecl>(*file->decls[3]);
    auto unsafeBaz = baz.modifiers.begin();
    ASSERT_EQ(unsafeBaz->comments.leadingComments.size(), 1);
    EXPECT_EQ(unsafeBaz->comments.leadingComments[0].cms[0].style, CommentStyle::LEAD_LINE);
    EXPECT_EQ(unsafeBaz->comments.leadingComments[0].cms[0].info.Value(),
        "// comment belong to unsafe");
    
    code = R"(class A {}
    // trailing comment of class A

    func b() {})";
    Parser parser2{code, diag, sm, {0, 1, 1}, true};
    OwnedPtr<File> file2 = parser2.ParseTopLevel();
    ASSERT_EQ(file2->decls.size(), 2);
    auto& clA2 = StaticCast<ClassDecl>(*file2->decls[0]);
    ASSERT_EQ(clA2.comments.trailingComments.size(), 1);
    EXPECT_EQ(clA2.comments.trailingComments[0].cms[0].info.Value(), "// trailing comment of class A");
}

TEST_F(ParseCommentTest, Rule3_2)
{
    code = R"(let a = 1 // comment to 1

    // comment to 2
    + 2)";
    Parser parser{code, diag, sm, {0, 1, 1}, true};
    OwnedPtr<File> file = parser.ParseTopLevel();
    ASSERT_EQ(file->decls.size(), 1);
    auto& ainit = StaticCast<BinaryExpr>(*StaticCast<VarDecl>(*file->decls[0]).initializer);
    ASSERT_EQ(ainit.leftExpr->comments.trailingComments.size(), 1);
    EXPECT_EQ(ainit.leftExpr->comments.trailingComments[0].cms[0].info.Value(), "// comment to 1");
    ASSERT_EQ(ainit.comments.innerComments.size(), 1);
    EXPECT_EQ(ainit.comments.innerComments[0].cms[0].info.Value(), "// comment to 2");
}

TEST_F(ParseCommentTest, Rule3_3)
{
    code = "public class /* comment to body */ A {}";
    Parser parser{code, diag, sm, {0, 1, 1}, true};
    OwnedPtr<File> file = parser.ParseTopLevel();
    ASSERT_EQ(file->decls.size(), 1);
    auto& clA = StaticCast<ClassDecl>(*file->decls[0]);
    ASSERT_EQ(clA.comments.innerComments.size(), 1);
    EXPECT_EQ(clA.comments.innerComments[0].cms[0].info.Value(), "/* comment to body */");
}

TEST_F(ParseCommentTest, Prop)
{
    code = R"(class A {
    // leading comments of a
    prop a: Unit {
    get() {()}}})";
    Parser parser{code, diag, sm, {0, 1, 1}, true};
    OwnedPtr<File> file = parser.ParseTopLevel();
    ASSERT_EQ(file->decls.size(), 1);
    auto& clA = StaticCast<ClassDecl>(*file->decls[0]);
    ASSERT_EQ(clA.body->decls.size(), 1);
    auto& propA = StaticCast<PropDecl>(*clA.body->decls[0]);
    ASSERT_EQ(propA.comments.leadingComments.size(), 1);
    EXPECT_EQ(propA.comments.leadingComments[0].cms[0].style, CommentStyle::LEAD_LINE);
    EXPECT_EQ(propA.comments.leadingComments[0].cms[0].info.Value(), "// leading comments of a");
}

TEST_F(ParseCommentTest, Rule4)
{
    code = R"(class A {
    // c lead var decl of var a
    var a = 1
    
    // lead var b = 2
    
    var b = 2
    }
    )";
    Parser parser{code, diag, sm, {0, 1, 1}, true};
    OwnedPtr<File> file = parser.ParseTopLevel();
    ASSERT_EQ(file->decls.size(), 1);
    auto& clA = StaticCast<ClassDecl>(*file->decls[0]);
    ASSERT_EQ(clA.body->decls.size(), 2);
    auto& varA = StaticCast<VarDecl>(*clA.body->decls[0]);
    ASSERT_EQ(varA.comments.leadingComments.size(), 1);
    EXPECT_EQ(varA.comments.leadingComments[0].cms[0].style, CommentStyle::LEAD_LINE);
    EXPECT_EQ(varA.comments.leadingComments[0].cms[0].info.Value(), "// c lead var decl of var a");
    auto& varB = StaticCast<VarDecl>(*clA.body->decls[1]);
    ASSERT_EQ(varB.comments.leadingComments.size(), 1);
    EXPECT_EQ(varB.comments.leadingComments[0].cms[0].style, CommentStyle::LEAD_LINE);
    EXPECT_EQ(varB.comments.leadingComments[0].cms[0].info.Value(), "// lead var b = 2");
}

TEST_F(ParseCommentTest, Rule5)
{
    code = R"(
    class A { // b lead var decl of var a
    var a = 1
    
    // e inner of body
}
    
func foo(/* h inner funcParamList of foo */) {})";
    Parser parser{code, diag, sm, {0, 1, 1}, true};
    OwnedPtr<File> file = parser.ParseTopLevel();
    ASSERT_EQ(file->decls.size(), 2);
    auto& clA = StaticCast<ClassDecl>(*file->decls[0]);
    ASSERT_EQ(clA.body->decls[0]->comments.leadingComments.size(), 1);
    EXPECT_EQ(clA.body->decls[0]->comments.leadingComments[0].cms[0].style, CommentStyle::TRAIL_CODE);
    EXPECT_EQ(clA.body->decls[0]->comments.leadingComments[0].cms[0].info.Value(),
        "// b lead var decl of var a");
    EXPECT_EQ(clA.body->comments.innerComments[0].cms[0].info.Value(), "// e inner of body");
    auto& foo = StaticCast<FuncDecl>(*file->decls[1]);
    ASSERT_EQ(foo.funcBody->paramLists[0]->comments.innerComments.size(), 1);
    EXPECT_EQ(foo.funcBody->paramLists[0]->comments.innerComments[0].cms[0].style, CommentStyle::TRAIL_CODE);
    EXPECT_EQ(foo.funcBody->paramLists[0]->comments.innerComments[0].cms[0].info.Value(),
        "/* h inner funcParamList of foo */");
}

TEST_F(ParseCommentTest, Example1)
{
    code = R"(// EXEC: %compiler %cmp_opt %f
    
main() {
}

// ASSERT: scan expected declaration, found 'x'
// ASSERT: regex-begin .*
/*
 * Copyright xxxxx
 */
)";
    Parser parser{code, diag, sm, {0, 1, 1}, true};
    OwnedPtr<File> file = parser.ParseTopLevel();
    ASSERT_EQ(file->comments.leadingComments.size(), 1);
    EXPECT_EQ(file->comments.leadingComments[0].cms[0].info.Value(), "// EXEC: %compiler %cmp_opt %f");
    ASSERT_EQ(file->comments.trailingComments.size(), 1);
    ASSERT_EQ(file->comments.trailingComments[0].cms.size(), 3);
    EXPECT_EQ(file->comments.trailingComments[0].cms[0].info.Value(),
        "// ASSERT: scan expected declaration, found 'x'");
    EXPECT_EQ(file->comments.trailingComments[0].cms[1].info.Value(), "// ASSERT: regex-begin .*");
    EXPECT_EQ(file->comments.trailingComments[0].cms[2].info.Value(), "/*\n * Copyright xxxxx\n */");
}

TEST_F(ParseCommentTest, Example3)
{
    code = R"(import std.ast.*

// comment one
// framework

// comment two
func foo() {})";
    Parser parser{code, diag, sm, {0, 1, 1}, true};
    OwnedPtr<File> file = parser.ParseTopLevel();
    auto& foo = StaticCast<FuncDecl>(*file->decls[0]);
    ASSERT_EQ(foo.comments.leadingComments.size(), 2);
    EXPECT_EQ(foo.comments.leadingComments[0].cms[0].info.Value(), "// comment one");
    EXPECT_EQ(foo.comments.leadingComments[0].cms[1].info.Value(), "// framework");
}

TEST_F(ParseCommentTest, Example4)
{
    code = R"(func foo(
    name: string, // username
    age: int // userage
): Unit // nothing to return
{})";
    Parser parser{code, diag, sm, {0, 1, 1}, true};
    OwnedPtr<File> file = parser.ParseTopLevel();
    auto& foo = StaticCast<FuncDecl>(*file->decls[0]);
    ASSERT_EQ(foo.funcBody->paramLists[0]->params.size(), 2);
    auto& nameParam = StaticCast<VarDecl>(*foo.funcBody->paramLists[0]->params[0]);
    ASSERT_EQ(nameParam.comments.trailingComments.size(), 0);
    auto& ageParam = StaticCast<VarDecl>(*foo.funcBody->paramLists[0]->params[1]);
    ASSERT_EQ(ageParam.comments.leadingComments.size(), 1);
    EXPECT_EQ(ageParam.comments.leadingComments[0].cms[0].info.Value(), "// username");
    ASSERT_EQ(ageParam.comments.trailingComments.size(), 1);
    EXPECT_EQ(ageParam.comments.trailingComments[0].cms[0].info.Value(), "// userage");
    ASSERT_EQ(foo.funcBody->retType->comments.trailingComments.size(), 1);
    EXPECT_EQ(foo.funcBody->retType->comments.trailingComments[0].cms[0].info.Value(), "// nothing to return");
}

TEST_F(ParseCommentTest, Example4_2)
{
    code = R"(func bar(
    name: sting, age: int, // user age
) // nothing to return{})";
    Parser parser2{code, diag, sm, {0, 1, 1}, true};
    OwnedPtr<File> file2 = parser2.ParseTopLevel();
    auto& bar = StaticCast<FuncDecl>(*file2->decls[0]);
    ASSERT_EQ(bar.funcBody->paramLists[0]->params.size(), 2);
    ASSERT_EQ(bar.funcBody->paramLists[0]->comments.innerComments.size(), 1);
    EXPECT_EQ(bar.funcBody->paramLists[0]->comments.innerComments[0].cms[0].info.Value(), "// user age");
    ASSERT_EQ(bar.comments.trailingComments.size(), 1);
    EXPECT_EQ(bar.comments.trailingComments[0].cms[0].info.Value(), "// nothing to return{}");
}

TEST_F(ParseCommentTest, Example5)
{
    code = R"(func calculate(): int {
    let result = 10 +
        // to 20 * 30
        20 *
        30 /* to let decl*/
    return result
}
func bar() {
    let nums = ArrayList(
        1, // second element
        2,
        // third element
        3,
    )
}
func f5() {
    return 0 + /* belongs to 5 */ 5
})";
    Parser parser{code, diag, sm, {0, 1, 1}, true};
    OwnedPtr<File> file = parser.ParseTopLevel();
    auto& calculate = StaticCast<FuncDecl>(*file->decls[0]);
    auto& result = StaticCast<VarDecl>(*calculate.funcBody->body->body[0]);
    EXPECT_EQ(StaticCast<BinaryExpr>(*result.initializer).rightExpr->comments.leadingComments[0].cms[0].info.Value(),
        "// to 20 * 30");
    EXPECT_EQ(result.comments.trailingComments[0].cms[0].info.Value(), "/* to let decl*/");
    auto& bar = StaticCast<FuncDecl>(*file->decls[1]);
    auto& nums = StaticCast<VarDecl>(*bar.funcBody->body->body[0]);
    auto& arrayList = StaticCast<CallExpr>(*nums.initializer);
    EXPECT_EQ(arrayList.args[1]->comments.leadingComments[0].cms[0].info.Value(), "// second element");
    EXPECT_EQ(arrayList.args[2]->comments.leadingComments[0].cms[0].info.Value(), "// third element");
    auto& f5 = StaticCast<FuncDecl>(*file->decls[2]);
    auto& ret = StaticCast<BinaryExpr>(*StaticCast<ReturnExpr>(*f5.funcBody->body->body[0]).expr);
    EXPECT_EQ(ret.rightExpr->comments.leadingComments[0].cms[0].info.Value(), "/* belongs to 5 */");
}


TEST_F(ParseCommentTest, Example6)
{
    code = R"(func foo() {
    0
    
    // comment belongs to body
    
    }
    
    func bar() {
    0
    
    // comment belongs to 2

    2
    })";
    Parser parser{code, diag, sm, {0, 1, 1}, true};
    OwnedPtr<File> file = parser.ParseTopLevel();
    auto& foo = StaticCast<FuncDecl>(*file->decls[0]);
    ASSERT_EQ(foo.funcBody->body->comments.innerComments.size(), 1);
    EXPECT_EQ(foo.funcBody->body->comments.innerComments[0].cms[0].info.Value(), "// comment belongs to body");
    auto& bar = StaticCast<FuncDecl>(*file->decls[1]);
    ASSERT_EQ(bar.funcBody->body->body[1]->comments.leadingComments.size(), 1);
    EXPECT_EQ(bar.funcBody->body->body[1]->comments.leadingComments[0].cms[0].info.Value(), "// comment belongs to 2");
}

TEST_F(ParseCommentTest, Macro1)
{
    code = R"(// leading comment belong to macroExpandDecl
    @testMacro
    class A {}
    )";
    Parser parser(code, diag, sm, {0, 1, 1}, true);
    OwnedPtr<File> file = parser.ParseTopLevel();
    auto& foo = StaticCast<MacroExpandDecl>(*file->decls[0]);
    EXPECT_EQ(foo.comments.leadingComments.size(), 1);
}

TEST_F(ParseCommentTest, Macro2)
{
    code = R"(func a(/* leading comment belong to macroExpandParam */@testMacro[public func]a: Int64) {}
    )";
    Parser parser(code, diag, sm, {0, 1, 1}, true);
    OwnedPtr<File> file = parser.ParseTopLevel();
    auto& params = StaticCast<FuncDecl>(*file->decls[0]).funcBody->paramLists[0];
    EXPECT_EQ(params->params[0]->comments.leadingComments.size(), 1);
}

TEST_F(ParseCommentTest, Import)
{
    code = "import /* importMulti comment*/ {test1, test2.*, test3 as pkg}";
    Parser parser(code, diag, sm, {0, 1, 1}, true);
    OwnedPtr<File> file = parser.ParseTopLevel();
    ASSERT_EQ(file->imports.size(), 4);
    ASSERT_EQ(file->imports[0]->content.comments.leadingComments.size(), 1);
    EXPECT_EQ(file->imports[0]->content.comments.leadingComments[0].cms[0].info.Value(),
        "/* importMulti comment*/");
}

TEST_F(ParseCommentTest, File1)
{
    code = R"(/** file leading comments
*/

// leading comment belong to classDecl
class a {}
// trailing comment belong to classDecl

func a() {})";
    Parser parser(code, diag, sm, {0, 1, 1}, true);
    OwnedPtr<File> file = parser.ParseTopLevel();
    ASSERT_EQ(file->comments.leadingComments.size(), 1);
    EXPECT_EQ(file->comments.leadingComments[0].cms[0].info.Value(), "/** file leading comments\n*/");
    auto& clA = StaticCast<ClassDecl>(*file->decls[0]);
    ASSERT_EQ(clA.comments.leadingComments.size(), 1);
    EXPECT_EQ(clA.comments.leadingComments[0].cms[0].info.Value(), "// leading comment belong to classDecl");
    ASSERT_EQ(clA.comments.trailingComments.size(), 1);
    EXPECT_EQ(clA.comments.trailingComments[0].cms[0].info.Value(), "// trailing comment belong to classDecl");
}

TEST_F(ParseCommentTest, File2)
{
    code = R"(
// leading comment belong to classDecl
class a {}
// trailing comment belong to classDecl

func a() {})";
    Parser parser(code, diag, sm, {0, 1, 1}, true);
    OwnedPtr<File> file = parser.ParseTopLevel();
    auto& clA = StaticCast<ClassDecl>(*file->decls[0]);
    ASSERT_EQ(clA.comments.leadingComments.size(), 1);
    EXPECT_EQ(clA.comments.leadingComments[0].cms[0].info.Value(), "// leading comment belong to classDecl");
    ASSERT_EQ(clA.comments.trailingComments.size(), 1);
    EXPECT_EQ(clA.comments.trailingComments[0].cms[0].info.Value(), "// trailing comment belong to classDecl");
}

TEST_F(ParseCommentTest, ImportSingle)
{
    code = "import /* importSingle comment*/ test1";
    Parser parser(code, diag, sm, {0, 1, 1}, true);
    OwnedPtr<File> file = parser.ParseTopLevel();
    ASSERT_EQ(file->imports.size(), 1);
    EXPECT_EQ(file->imports[0]->content.comments.leadingComments.size(), 1);
}

TEST_F(ParseCommentTest, File3)
{
    code = R"(/** file leading comments
*/

func f5() {
    return 0 + /*comment to 5*/ 5
}

func foo() // comment to paramList
{}

/** file trailing comments
*/)";
    Parser parser(code, diag, sm, {0, 1, 1}, true);
    OwnedPtr<File> file = parser.ParseTopLevel();
    auto& foo = StaticCast<FuncDecl>(*file->decls[1]);
    ASSERT_EQ(foo.funcBody->paramLists[0]->comments.trailingComments.size(), 1);
    EXPECT_EQ(foo.funcBody->paramLists[0]->comments.trailingComments[0].cms[0].style, CommentStyle::TRAIL_CODE);
    EXPECT_EQ(foo.funcBody->paramLists[0]->comments.trailingComments[0].cms[0].info.Value(),
        "// comment to paramList");
}

TEST_F(ParseCommentTest, File4)
{
    code = R"(/** file leading comments
*/

func foo(): (
    int /*comment to int*/,
    bool // comment to bool
) {}

/** file trailing comments
*/)";
    Parser parser(code, diag, sm, {0, 1, 1}, true);
    OwnedPtr<File> file = parser.ParseTopLevel();
    auto& foo = StaticCast<FuncDecl>(*file->decls[0]);
    auto& retType = StaticCast<TupleType>(*foo.funcBody->retType);
    ASSERT_EQ(retType.fieldTypes[0]->comments.trailingComments.size(), 1);
    EXPECT_EQ(retType.fieldTypes[0]->comments.trailingComments[0].cms[0].info.Value(), "/*comment to int*/");
    ASSERT_EQ(retType.fieldTypes[1]->comments.trailingComments.size(), 1);
    EXPECT_EQ(retType.fieldTypes[1]->comments.trailingComments[0].cms[0].info.Value(), "// comment to bool");
}

TEST_F(ParseCommentTest, File5)
{
    code = R"(func foo() {}

// file comment 1

// file comment 2)";
    Parser parser(code, diag, sm, {0, 1, 1}, true);
    OwnedPtr<File> file = parser.ParseTopLevel();
    ASSERT_EQ(file->comments.trailingComments.size(), 2);
    EXPECT_EQ(file->comments.trailingComments[0].cms.front().info.Value(), "// file comment 1");
    EXPECT_EQ(file->comments.trailingComments[1].cms.front().info.Value(), "// file comment 2");
}

TEST_F(ParseCommentTest, ClassDecl)
{
    code = R"(class /*comment to classDecl*/ F {})";
    Parser parser(code, diag, sm, {0, 1, 1}, true);
    OwnedPtr<File> file = parser.ParseTopLevel();
    auto& foo = StaticCast<ClassDecl>(*file->decls[0]);
    ASSERT_EQ(foo.comments.innerComments.size(), 1);
    EXPECT_EQ(foo.comments.innerComments[0].cms[0].info.Value(), "/*comment to classDecl*/");
}

TEST_F(ParseCommentTest, FuncDecl)
{
    code = R"(func c<T, S>(param1/*comment to param1*/: /*comment to type*/Int64): Int64 where T <: A<S>, S <: ToString & B {})";
    Parser parser(code, diag, sm, {0, 1, 1}, true);
    OwnedPtr<File> file = parser.ParseTopLevel();
    auto& foo = StaticCast<FuncDecl>(*file->decls[0]);
    ASSERT_EQ(foo.funcBody->paramLists[0]->params.size(), 1);
    auto& funcParam = StaticCast<VarDecl>(*foo.funcBody->paramLists[0]->params[0]);
    ASSERT_EQ(funcParam.comments.innerComments.size(), 1);
    EXPECT_EQ(funcParam.comments.innerComments[0].cms[0].info.Value(), "/*comment to param1*/");
    ASSERT_EQ(funcParam.type->comments.leadingComments.size(), 1);
    EXPECT_EQ(funcParam.type->comments.leadingComments[0].cms[0].info.Value(), "/*comment to type*/");
}

TEST_F(ParseCommentTest, MemberAccess)
{
    code = R"(func foo() {
    A./*comment to memberAccess*/ a

})";
    Parser parser(code, diag, sm, {0, 1, 1}, true);
    OwnedPtr<File> file = parser.ParseTopLevel();
    auto& foo = StaticCast<FuncDecl>(*file->decls[0]);
    auto& block = foo.funcBody->body;
    auto& memberAccess = StaticCast<MemberAccess>(*block->body[0]);
    ASSERT_EQ(memberAccess.comments.innerComments.size(), 1);
    EXPECT_EQ(memberAccess.comments.innerComments[0].cms[0].info.Value(), "/*comment to memberAccess*/");
}

TEST_F(ParseCommentTest, WhileExpr)
{
    code = R"(func foo() {
    while(true /*comment to true*/) {}

})";
    Parser parser(code, diag, sm, {0, 1, 1}, true);
    OwnedPtr<File> file = parser.ParseTopLevel();
    auto& foo = StaticCast<FuncDecl>(*file->decls[0]);
    auto& block = foo.funcBody->body;
    auto& whileExpr = StaticCast<WhileExpr>(*block->body[0]);
    ASSERT_EQ(whileExpr.condExpr->comments.trailingComments.size(), 1);
    EXPECT_EQ(whileExpr.condExpr->comments.trailingComments[0].cms[0].info.Value(), "/*comment to true*/");
}