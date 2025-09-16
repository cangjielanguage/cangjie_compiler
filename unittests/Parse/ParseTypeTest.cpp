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
