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
        diag.SetSourceManager(&sm);
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

TEST_F(ParseCommentTest, MacroExpandDecl)
{
    code = R"(@M1/* block comment1 */ [/* block comment2 */expr] var a = 1)";
    Parser parser(code, diag, sm, {0, 1, 1}, true);
    OwnedPtr<File> file = parser.ParseTopLevel();
    auto& foo = StaticCast<MacroExpandDecl>(*file->decls[0]);
    ASSERT_EQ(foo.comments.innerComments.size(), 2);
    EXPECT_EQ(foo.comments.innerComments[0].cms[0].info.Value(), "/* block comment1 */");
    EXPECT_EQ(foo.comments.innerComments[1].cms[0].info.Value(), "/* block comment2 */");
}

TEST_F(ParseCommentTest, EnumDecl)
{
    code = R"(enum E1<T, R> where T <: ToString // line comment2
{
    | A1
})";
    Parser parser(code, diag, sm, {0, 1, 1}, true);
    OwnedPtr<File> file = parser.ParseTopLevel();
    auto& foo = StaticCast<EnumDecl>(*file->decls[0]);
    ASSERT_EQ(foo.generic->genericConstraints[0]->comments.trailingComments.size(), 1);
    EXPECT_EQ(foo.generic->genericConstraints[0]->comments.trailingComments[0].cms[0].info.Value(), "// line comment2");
}

TEST_F(ParseCommentTest, Annotation)
{
    code = R"(@Depre["1234", /* block comment1 */ strict: false] // line comment1
// line comment2
@!Anno1["1234"] // line comment3
class A {})";
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm, {0, 1, 1}, true);
    OwnedPtr<File> file = parser.ParseTopLevel();
    auto& depre = StaticCast<MacroExpandDecl>(*file->decls[0]);
    ASSERT_EQ(depre.comments.innerComments.size(), 1);
    EXPECT_EQ(depre.comments.innerComments[0].cms[0].info.Value(), "/* block comment1 */");
    auto& anno1 = StaticCast<MacroExpandDecl>(*depre.invocation.decl);
    ASSERT_EQ(anno1.comments.leadingComments.size(), 2);
    EXPECT_EQ(anno1.comments.leadingComments[0].cms[0].info.Value(), "// line comment1");
    EXPECT_EQ(anno1.comments.leadingComments[1].cms[0].info.Value(), "// line comment2");
    auto& clA = StaticCast<ClassDecl>(*anno1.invocation.decl);
    ASSERT_EQ(clA.comments.leadingComments.size(), 1);
    EXPECT_EQ(clA.comments.leadingComments[0].cms[0].info.Value(), "// line comment3");
}

TEST_F(ParseCommentTest, AnnotationDeprecated)
{
    code = R"(@Deprecated["1234", /* block comment1 */ strict: false] // line comment1
// line comment2
@!Anno1["1234"] // line comment3
class A {})";
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm, {0, 1, 1}, true);
    OwnedPtr<File> file = parser.ParseTopLevel();
    auto& deprecated = StaticCast<MacroExpandDecl>(*file->decls[0]);
    ASSERT_EQ(deprecated.comments.innerComments.size(), 1);
    EXPECT_EQ(deprecated.comments.innerComments[0].cms[0].info.Value(), "// line comment2");
    auto& anno = *deprecated.annotations[0];
    ASSERT_EQ(anno.comments.innerComments.size(), 1);
    EXPECT_EQ(anno.comments.innerComments[0].cms[0].info.Value(), "/* block comment1 */");
    ASSERT_EQ(anno.comments.trailingComments.size(), 1);
    EXPECT_EQ(anno.comments.trailingComments[0].cms[0].info.Value(), "// line comment1");
    auto& clA = StaticCast<ClassDecl>(*deprecated.invocation.decl);
    ASSERT_EQ(clA.comments.leadingComments.size(), 1);
    EXPECT_EQ(clA.comments.leadingComments[0].cms[0].info.Value(), "// line comment3");
}

TEST_F(ParseCommentTest, FuncParam)
{
    code = R"(func foo<T>(a: T, /** document comment2 */ b!: Int64 = 0) where T <: I1 /** document comment3 */ & I2 {
    func goo(a0: Int64) : (Int64 /* block comment3 */, /* block comment4 */ Int64) { (a0, a0)}
    // line comment2

    return (0, a)
} // line comment3)";
    Parser parser(code, diag, sm, {0, 1, 1}, true);
    OwnedPtr<File> file = parser.ParseTopLevel();
    auto& foo = StaticCast<FuncDecl>(*file->decls[0]);
    ASSERT_EQ(foo.funcBody->paramLists[0]->params.size(), 2);
    auto& aParam = StaticCast<VarDecl>(*foo.funcBody->paramLists[0]->params[0]);
    ASSERT_EQ(aParam.comments.trailingComments.size(), 0);
    auto& bParam = StaticCast<VarDecl>(*foo.funcBody->paramLists[0]->params[1]);
    ASSERT_EQ(bParam.comments.leadingComments.size(), 1);
    EXPECT_EQ(bParam.comments.leadingComments[0].cms[0].info.Value(), "/** document comment2 */");
    auto& cons = *foo.funcBody->generic->genericConstraints[0];
    auto& i1 = *cons.upperBounds[0];
    ASSERT_EQ(i1.comments.trailingComments.size(), 1);
    EXPECT_EQ(i1.comments.trailingComments[0].cms[0].info.Value(), "/** document comment3 */");

    auto& goo = StaticCast<FuncDecl>(*foo.funcBody->body->body[0]);
    auto& retType = StaticCast<TupleType>(*goo.funcBody->retType);
    ASSERT_EQ(retType.fieldTypes[0]->comments.trailingComments.size(), 1);
    EXPECT_EQ(retType.fieldTypes[0]->comments.trailingComments[0].cms[0].info.Value(), "/* block comment3 */");
    ASSERT_EQ(retType.fieldTypes[1]->comments.leadingComments.size(), 1);
    EXPECT_EQ(retType.fieldTypes[1]->comments.leadingComments[0].cms[0].info.Value(), "/* block comment4 */");
    ASSERT_EQ(goo.comments.trailingComments.size(), 1);
    EXPECT_EQ(goo.comments.trailingComments[0].cms[0].info.Value(), "// line comment2");
    ASSERT_EQ(foo.comments.trailingComments.size(), 1);
    EXPECT_EQ(foo.comments.trailingComments[0].cms[0].info.Value(), "// line comment3");
}

TEST_F(ParseCommentTest, Lambda)
{
    code = R"(func foo() {
    let lamb1 = { /* block comment2 */ => }

    let lamb3 = { /* block comment3 */ a: Int64, /* block comment4 */ b: Int64 => // line comment10
        a + b + 1
    }
})";
    Parser parser(code, diag, sm, {0, 1, 1}, true);
    OwnedPtr<File> file = parser.ParseTopLevel();
    auto& foo = StaticCast<FuncDecl>(*file->decls[0]);
    auto& block = *foo.funcBody->body;
    auto& lambda1 = StaticCast<LambdaExpr>(*StaticCast<VarDecl>(*block.body[0]).initializer);
    ASSERT_EQ(lambda1.funcBody->paramLists[0]->comments.leadingComments.size(), 1);
    EXPECT_EQ(lambda1.funcBody->paramLists[0]->comments.leadingComments[0].cms[0].info.Value(), "/* block comment2 */");

    auto& lambda2 = StaticCast<LambdaExpr>(*StaticCast<VarDecl>(*block.body[1]).initializer);
    ASSERT_EQ(lambda2.funcBody->paramLists[0]->comments.leadingComments.size(), 1);
    EXPECT_EQ(lambda2.funcBody->paramLists[0]->comments.leadingComments[0].cms[0].info.Value(), "/* block comment3 */");
    auto& bParam = StaticCast<VarDecl>(*lambda2.funcBody->paramLists[0]->params[1]);
    ASSERT_EQ(bParam.comments.leadingComments.size(), 1);
    ASSERT_EQ(bParam.comments.trailingComments.size(), 0);
    EXPECT_EQ(bParam.comments.leadingComments[0].cms[0].info.Value(), "/* block comment4 */");
}

TEST_F(ParseCommentTest, TryCatch)
{
    code = R"(func foo() {
    try {
    } catch(e: Exception1 /** document comment1 */ | /** document comment2 */ Exception2) {
    }
})";
    Parser parser(code, diag, sm, {0, 1, 1}, true);
    OwnedPtr<File> file = parser.ParseTopLevel();
    ASSERT_EQ(diag.GetErrorCount(), 0);
    auto& foo = StaticCast<FuncDecl>(*file->decls[0]);
    auto& block = *foo.funcBody->body;
    auto& tryCatch = StaticCast<TryExpr>(*block.body[0]);
    auto& catchPattern = StaticCast<ExceptTypePattern>(*tryCatch.catchPatterns[0]);
    ASSERT_EQ(catchPattern.types[0]->comments.trailingComments.size(), 1);
    EXPECT_EQ(catchPattern.types[0]->comments.trailingComments[0].cms[0].info.Value(), "/** document comment1 */");
    ASSERT_EQ(catchPattern.types[1]->comments.leadingComments.size(), 1);
    EXPECT_EQ(catchPattern.types[1]->comments.leadingComments[0].cms[0].info.Value(), "/** document comment2 */");
}

TEST_F(ParseCommentTest, File6)
{
    code = R"(// EXEC: %compiler %cmp_opt %f

/*
 * Copyright xxxxx
 */
)";
    Parser parser{code, diag, sm, {0, 1, 1}, true};
    OwnedPtr<File> file = parser.ParseTopLevel();
    ASSERT_EQ(file->comments.leadingComments.size(), 2);
    EXPECT_EQ(file->comments.leadingComments[0].cms[0].info.Value(), "// EXEC: %compiler %cmp_opt %f");
    EXPECT_EQ(file->comments.leadingComments[1].cms[0].info.Value(), "/*\n * Copyright xxxxx\n */");
}

TEST_F(ParseCommentTest, MacroExpand)
{
    code = "@M1/* block comment1 */ [/* block comment2 */expr] /* block comment3 */var a = 1 // line comment1";
    Parser parser(code, diag, sm, {0, 1, 1}, true);
    OwnedPtr<File> file = parser.ParseTopLevel();
    auto& macroExp = StaticCast<MacroExpandDecl>(*file->decls[0]);
    ASSERT_EQ(macroExp.comments.innerComments.size(), 2);
    EXPECT_EQ(macroExp.comments.innerComments[0].cms[0].info.Value(), "/* block comment1 */");
    EXPECT_EQ(macroExp.comments.innerComments[1].cms[0].info.Value(), "/* block comment2 */");
    auto& a = StaticCast<VarDecl>(*macroExp.invocation.decl);
    ASSERT_EQ(a.comments.leadingComments.size(), 1);
    EXPECT_EQ(a.comments.leadingComments[0].cms[0].info.Value(), "/* block comment3 */");
    ASSERT_EQ(macroExp.comments.trailingComments.size(), 1);
    EXPECT_EQ(macroExp.comments.trailingComments[0].cms[0].info.Value(), "// line comment1");
}

TEST_F(ParseCommentTest, Enum2)
{
    code = "enum E1 { | B1(/* comment 1 */T, /* comment 2 */ R) }";
    Parser parser(code, diag, sm, {0, 1, 1}, true);
    OwnedPtr<File> file = parser.ParseTopLevel();
    auto& enumDecl = StaticCast<EnumDecl>(*file->decls[0]);
    auto& b1 = StaticCast<FuncDecl>(*enumDecl.constructors[0]);
    auto& t = StaticCast<VarDecl>(*b1.funcBody->paramLists[0]->params[0]);
    ASSERT_EQ(t.comments.leadingComments.size(), 1);
    EXPECT_EQ(t.comments.leadingComments[0].cms[0].info.Value(), "/* comment 1 */");
    auto& r = StaticCast<VarDecl>(*b1.funcBody->paramLists[0]->params[1]);
    ASSERT_EQ(r.comments.leadingComments.size(), 1);
    EXPECT_EQ(r.comments.leadingComments[0].cms[0].info.Value(), "/* comment 2 */");
}

TEST_F(ParseCommentTest, IntLiteral)
{
    code = R"(func foo() {
    @synmmetry func s() {
        // 等待crdRegistrationMgr初始化完成
        0
    }
    let a = 0
})";
    Parser parser(code, diag, sm, {0, 1, 1}, true);
    OwnedPtr<File> file = parser.ParseTopLevel();
    auto& a = StaticCast<FuncDecl>(*file->decls[0]);
    auto& block = *a.funcBody->body;
    auto& macro = StaticCast<MacroExpandExpr>(*block.body[0]);
    auto& s = StaticCast<FuncDecl>(*macro.invocation.decl);
    auto& zero = StaticCast<LitConstExpr>(*s.funcBody->body->body[0]);
    ASSERT_EQ(zero.comments.leadingComments.size(), 1);
    EXPECT_EQ(zero.comments.leadingComments[0].cms[0].info.Value(), "// 等待crdRegistrationMgr初始化完成");
}