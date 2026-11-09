// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * Test cases for type parser.
 */
#include <utility>
#include "gtest/gtest.h"

#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/PrintNode.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Basic/Match.h"
#include "cangjie/Parse/Parser.h"

using namespace Cangjie;
using namespace AST;
using namespace Meta;

TEST(ParseModalType, OptionalTypeCarriesItsOwnLocality)
{
    for (const auto& entry : std::vector<std::pair<std::string, ASTMode>>{
             {"?C@local!", ASTMode::FULL}, {"?Int64@~local", ASTMode::NOT}}) {
        const auto& code = entry.first;
        SCOPED_TRACE(code);
        SourceManager sm;
        DiagnosticEngine diag;
        diag.SetSourceManager(&sm);
        Parser parser(code, diag, sm);
        auto type = parser.ParseType();
        ASSERT_NE(type, nullptr);
        EXPECT_EQ(diag.GetErrorCount(), 0u);
        ASSERT_TRUE(Is<OptionType>(type.get()));
        EXPECT_EQ(type->modal.Local(), entry.second);
        auto option = StaticCast<OptionType>(type.get());
        ASSERT_NE(option->componentType, nullptr);
        EXPECT_FALSE(option->componentType->modal.HasLocal());
    }
}

TEST(ParseModalType, NonAdjacentOrIncompleteSpecifierDoesNotBecomeAModal)
{
    for (const std::string code : {"C@local !", "C@local ?", "C@~ local", "C@local", "C@~other", "C@"}) {
        SCOPED_TRACE(code);
        SourceManager sm;
        DiagnosticEngine diag;
        diag.SetSourceManager(&sm);
        Parser parser(code, diag, sm);
        auto type = parser.ParseType();
        ASSERT_NE(type, nullptr);
        EXPECT_FALSE(type->modal.HasLocal());
    }
}

TEST(ParseModalType, AdjacentInitializerKeepsItsAssignmentToken)
{
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    const std::string code = "let value: C@local!=C()";
    Parser parser(code, diag, sm);
    auto decl = parser.ParseDecl(ScopeKind::TOPLEVEL);
    ASSERT_EQ(diag.GetErrorCount(), 0u);
    ASSERT_TRUE(Is<VarDecl>(decl.get()));
    auto var = StaticCast<VarDecl>(decl.get());
    ASSERT_NE(var->type, nullptr);
    EXPECT_EQ(var->type->modal.Local(), ASTMode::FULL);
    ASSERT_NE(var->initializer, nullptr);
    EXPECT_TRUE(Is<CallExpr>(var->initializer.get()));
}

TEST(ParseType, ParseTypePosition)
{
    std::string code = R"(
    let a : (A, B) -> (C)
    let b : () -> D
    let c : CFunc<(A)->E>
)";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);
    OwnedPtr<File> file = parser.ParseTopLevel();
    std::vector<std::pair<int, int>> expectPosition;
    expectPosition.push_back(std::make_pair(2, 13));
    expectPosition.push_back(std::make_pair(2, 26));
    expectPosition.push_back(std::make_pair(2, 23));
    expectPosition.push_back(std::make_pair(2, 26));
    expectPosition.push_back(std::make_pair(3, 13));
    expectPosition.push_back(std::make_pair(3, 20));
    expectPosition.push_back(std::make_pair(4, 19));
    expectPosition.push_back(std::make_pair(4, 25));
    std::vector<std::pair<int, int>> position;
    Walker walker(file.get(), [&](Ptr<Node> node) -> VisitAction {
        return match(*node)(
            [&](const ParenType& pt) {
                position.push_back(std::make_pair(pt.begin.line, pt.begin.column));
                position.push_back(std::make_pair(pt.end.line, pt.end.column));
                return VisitAction::WALK_CHILDREN;
            },
            [&](const FuncType& ft) {
                position.push_back(std::make_pair(ft.begin.line, ft.begin.column));
                position.push_back(std::make_pair(ft.end.line, ft.end.column));
                return VisitAction::WALK_CHILDREN;
            },
            [&](const Node& /* node */) { return VisitAction::WALK_CHILDREN; },
            []() { return VisitAction::WALK_CHILDREN; });
    });
    walker.Walk();
    EXPECT_TRUE(position.size() == expectPosition.size());
    EXPECT_TRUE(std::equal(position.begin(), position.end(), expectPosition.begin()));
}

TEST(ParseType, ParseTypeExceptionTest)
{
    {
        // suppress stderr, only test errorcount
        testing::internal::CaptureStderr();
        std::string code = R"(
        Int32->Int64
        )";
        SourceManager sm;
        DiagnosticEngine diag;
        diag.SetSourceManager(&sm);
        Parser p(code, diag, sm);
        OwnedPtr<Node> node = p.ParseType();
        diag.EmitCategoryDiagnostics(DiagCategory::PARSE);
        EXPECT_TRUE(diag.GetErrorCount() > 0);
        (void)testing::internal::GetCapturedStderr();
    }
}

TEST(ParseType, ParseFuncTypeTest)
{
    { // This case contain a 'try-parse'
        std::string code = R"(
        var a = delimiterArr.size * (value.size - 1)
        )";
        SourceManager sm;
        DiagnosticEngine diag;
        diag.SetSourceManager(&sm);
        Parser p(code, diag, sm);
        std::set<Modifier> modifiers;
        OwnedPtr<File> node = p.ParseTopLevel();
        EXPECT_TRUE(diag.GetErrorCount() == 0);
    }
    {
        std::string code = R"(
        ((Int32, Int32))->Int32
        )";
        SourceManager sm;
        DiagnosticEngine diag;
        diag.SetSourceManager(&sm);
        Parser p(code, diag, sm);
        OwnedPtr<Node> node = p.ParseType();
        EXPECT_TRUE(diag.GetErrorCount() == 0);
        EXPECT_TRUE(Is<FuncType>(node.get()));
    }
    {
        std::string code = R"(
        ((Int32, Int32), ((Int8, Int16), Int64))->Int32
        )";
        SourceManager sm;
        DiagnosticEngine diag;
        diag.SetSourceManager(&sm);
        Parser p(code, diag, sm);
        OwnedPtr<Node> node = p.ParseType();
        EXPECT_TRUE(diag.GetErrorCount() == 0);
        EXPECT_TRUE(Is<FuncType>(node.get()));
    }
    { // positive case
        std::string code = R"(
        (Int32)->Int32
        )";

        SourceManager sm;
        DiagnosticEngine diag;
        diag.SetSourceManager(&sm);
        Parser p(code, diag, sm);
        OwnedPtr<Node> node = p.ParseType();
        EXPECT_TRUE(diag.GetErrorCount() == 0);
        EXPECT_TRUE(Is<FuncType>(node.get()));
    }
    { // positive case
        std::string code = R"(
        (Int32, unit)->Int32
        )";

        SourceManager sm;
        DiagnosticEngine diag;
        diag.SetSourceManager(&sm);
        Parser p(code, diag, sm);
        OwnedPtr<Node> node = p.ParseType();
        EXPECT_TRUE(diag.GetErrorCount() == 0);
        EXPECT_TRUE(Is<FuncType>(node.get()));
    }
    { // positive case
        std::string code = R"(
        ()->Int32
        )";

        SourceManager sm;
        DiagnosticEngine diag;
        diag.SetSourceManager(&sm);
        Parser p(code, diag, sm);
        OwnedPtr<Node> node = p.ParseType();
        EXPECT_TRUE(diag.GetErrorCount() == 0);
        EXPECT_TRUE(Is<FuncType>(node.get()));
    }
}

TEST(ParseType, OthreType)
{
    { // positive case
        std::string code = R"(
        inta
        )";

        SourceManager sm;
        DiagnosticEngine diag;
        diag.SetSourceManager(&sm);
        Parser p(code, diag, sm);
        OwnedPtr<Node> node = p.ParseType();

        EXPECT_TRUE(diag.GetErrorCount() == 0);
        EXPECT_TRUE(Is<RefType>(node.get()));
    }
    {
        std::string code = R"(
        (inta, intb, intc)
        )";

        SourceManager sm;
        DiagnosticEngine diag;
        diag.SetSourceManager(&sm);
        Parser p(code, diag, sm);
        OwnedPtr<Node> node = p.ParseType();
        EXPECT_TRUE(diag.GetErrorCount() == 0);
        EXPECT_TRUE(Is<TupleType>(node.get()));
    }
    {
        std::string code = R"(
        ((inta.intb), intc)
        )";

        SourceManager sm;
        DiagnosticEngine diag;
        diag.SetSourceManager(&sm);
        Parser p(code, diag, sm);
        OwnedPtr<Node> node = p.ParseType();
        EXPECT_TRUE(diag.GetErrorCount() == 0);
        EXPECT_TRUE(Is<TupleType>(node.get()));
    }
    {
        std::string code = R"(
        a.b.c
        )";

        SourceManager sm;
        DiagnosticEngine diag;
        diag.SetSourceManager(&sm);
        Parser p(code, diag, sm);
        OwnedPtr<Node> node = p.ParseType();

        EXPECT_TRUE(diag.GetErrorCount() == 0);
        EXPECT_TRUE(Is<QualifiedType>(node.get()));
    }
}

TEST(ParseType, ParseParenTypeTest)
{
    {
        std::string code = R"(
        ((Int32, Int32), Int32)
        )";
        SourceManager sm;
        DiagnosticEngine diag;
        diag.SetSourceManager(&sm);
        Parser p(code, diag, sm);
        OwnedPtr<Node> node = p.ParseType();
        EXPECT_TRUE(diag.GetErrorCount() == 0);
        EXPECT_TRUE(Is<TupleType>(node.get()));
    }
}

TEST(ParseType, ParseOptionTypeWithNewlines)
{
    const std::vector<std::pair<std::string, ASTKind>> cases = {
        {"?(Bool, Nothing\n)", ASTKind::TUPLE_TYPE},
        {"?(Int64\n, Bool)", ASTKind::TUPLE_TYPE},
        {"?V0\n.V1", ASTKind::QUALIFIED_TYPE},
        {"?V0.\nV1", ASTKind::QUALIFIED_TYPE},
        {"?Box<Int64\n, Bool>", ASTKind::REF_TYPE},
        {"?()\n-> (\n) -> Bool", ASTKind::FUNC_TYPE},
        {"? ?(?Int64\n, Bool\n)", ASTKind::TUPLE_TYPE},
    };
    for (const auto& [type, componentKind] : cases) {
        SCOPED_TRACE(type);
        SourceManager sm;
        DiagnosticEngine diag;
        diag.SetSourceManager(&sm);
        std::string code = "type T = " + type + "\nmain() {}";
        Parser parser(code, diag, sm);
        auto file = parser.ParseTopLevel();
        ASSERT_EQ(diag.GetErrorCount(), 0);
        ASSERT_EQ(file->decls.size(), 2);
        auto alias = DynamicCast<TypeAliasDecl*>(file->decls[0].get());
        ASSERT_NE(alias, nullptr);
        auto option = DynamicCast<OptionType*>(alias->type.get());
        ASSERT_NE(option, nullptr);
        ASSERT_NE(option->componentType, nullptr);
        EXPECT_EQ(option->componentType->astKind, componentKind);
    }
}

TEST(ParseType, RejectNewlineAfterOptionPrefix)
{
    for (const std::string type : {"?\nInt64", "?\n?Int64"}) {
        SCOPED_TRACE(type);
        SourceManager sm;
        DiagnosticEngine diag;
        diag.SetSourceManager(&sm);
        std::string code = "type T = " + type + "\nmain() {}";
        Parser parser(code, diag, sm);
        auto file = parser.ParseTopLevel();
        auto diagnostics = diag.GetCategoryDiagnostic(DiagCategory::PARSE);
        ASSERT_EQ(diagnostics.size(), 1);
        EXPECT_EQ(diagnostics[0].rKind, DiagKindRefactor::parse_newline_not_allowed_between_quest_and_type);
    }
}

TEST(ParseType, UnclosedOptionalTypeArgumentPosition)
{
    for (const std::string type : {"A<O", "?A<O", "? ?A<O", "?A<OtherType"}) {
        for (const std::string suffix : {"", "\n", "\n// trailing comment\n"}) {
            const std::string line = "type C = " + type;
            const std::string code = line + suffix;
            SCOPED_TRACE(code);
            SourceManager sm;
            DiagnosticEngine diag;
            diag.SetSourceManager(&sm);
            Parser parser(code, diag, sm);
            auto file = parser.ParseTopLevel();
            auto diagnostics = diag.GetCategoryDiagnostic(DiagCategory::PARSE);
            ASSERT_EQ(diagnostics.size(), 1);
            const auto& diagnostic = diagnostics[0];
            EXPECT_EQ(diagnostic.rKind, DiagKindRefactor::parse_expected_right_delimiter);
            // Point after the final type argument, even when newlines/comments precede EOF.
            EXPECT_EQ(diagnostic.mainHint.range.begin.line, 1);
            EXPECT_EQ(diagnostic.mainHint.range.begin.column, line.size() + 1);
            ASSERT_EQ(diagnostic.otherHints.size(), 1);
            EXPECT_EQ(diagnostic.otherHints[0].range.begin.column, line.find('<') + 1);
        }
    }
}
