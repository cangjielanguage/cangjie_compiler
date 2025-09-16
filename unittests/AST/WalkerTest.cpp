// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include <vector>
#include "gtest/gtest.h"

#define private public
#include "cangjie/AST/Match.h"
#include "cangjie/AST/PrintNode.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Parse/Parser.h"

using namespace Cangjie;
using namespace AST;

class WalkerTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        std::string code = "main(argc : Int32, argv : Array<String>) {\n"
                           "	let a : Int = 40\n"
                           "	let b = 2 ** -a\n"
                           "	print((a + 3 * b, (a + 3) * b))\n"
                           "}\n";
        Parser parser(code, diag, sm);
        file = parser.ParseTopLevel();
    }

    DiagnosticEngine diag;
    SourceManager sm;
    OwnedPtr<File> file;
};

TEST_F(WalkerTest, WalkPair)
{
    int count = 0;

    Walker walker(
        file.get(),
        [&count](Ptr<Node> node) -> VisitAction {
            ++count;
            return VisitAction::WALK_CHILDREN;
        },
        [&count](Ptr<Node> node) -> VisitAction {
            --count;
            return VisitAction::WALK_CHILDREN;
        });
    walker.Walk();

    EXPECT_EQ(0, count);
}

TEST_F(WalkerTest, WalkPairSkipChildren)
{
    int count = 0;

    Walker walker(
        file.get(),
        [&count](Ptr<Node> node) -> VisitAction {
            ++count;
            return VisitAction::SKIP_CHILDREN;
        },
        [&count](Ptr<Node> node) -> VisitAction {
            --count;
            return VisitAction::WALK_CHILDREN;
        });
    walker.Walk();

    EXPECT_EQ(0, count);
}

TEST_F(WalkerTest, WalkPairStopNow)
{
    int count = 0;

    Walker walker(
        file.get(),
        [&count](Ptr<Node> node) -> VisitAction {
            ++count;
            return VisitAction::STOP_NOW;
        },
        [&count](Ptr<Node> node) -> VisitAction {
            --count;
            return VisitAction::WALK_CHILDREN;
        });
    walker.Walk();

    EXPECT_EQ(1, count);
}

TEST_F(WalkerTest, WalkShareID)
{
    // Walker and ConstWalker must share same counter.
    Walker::nextWalkerID = 1;
    ConstWalker::nextWalkerID = 1;
    auto id1 = Walker(file.get()).ID;
    auto id2 = ConstWalker(file.get()).ID;
    EXPECT_NE(id1, id2);
}

TEST_F(WalkerTest, GetDecls)
{
    std::vector<std::string> identifiers;

    Walker walker(file.get(), [&identifiers](Ptr<Node> node) -> VisitAction {
        if (auto decl = AST::As<ASTKind::DECL>(node); decl) {
            identifiers.push_back(decl->identifier);
        }
        return VisitAction::WALK_CHILDREN;
    });
    walker.Walk();

    std::string expectedIdentifiers[] = {"main", "argc", "argv", "a", "b"};

    ASSERT_EQ(std::size(expectedIdentifiers), identifiers.size());
    for (size_t i = 0; i < identifiers.size(); i++) {
        EXPECT_EQ(expectedIdentifiers[i], identifiers[i]);
    }
}

TEST_F(WalkerTest, GetDeclsPost)
{
    std::vector<std::string> identifiers;

    Walker walker(file.get(), nullptr, [&identifiers](Ptr<Node> node) -> VisitAction {
        if (auto decl = AST::As<ASTKind::DECL>(node); decl) {
            identifiers.push_back(decl->identifier);
        }
        return VisitAction::WALK_CHILDREN;
    });
    walker.Walk();

    std::string expectedIdentifiers[] = {"argc", "argv", "a", "b", "main"};

    ASSERT_EQ(std::size(expectedIdentifiers), identifiers.size());
    for (size_t i = 0; i < identifiers.size(); i++) {
        EXPECT_EQ(expectedIdentifiers[i], identifiers[i]);
    }
}

TEST_F(WalkerTest, GetCallExprs)
{
    std::vector<std::string> callExprNames;

    Walker walker(file.get(), [&callExprNames](Ptr<Node> node) -> VisitAction {
        if (auto ce = AST::As<ASTKind::CALL_EXPR>(node); ce) {
            if (auto re = AST::As<ASTKind::REF_EXPR>(ce->baseFunc.get()); re) {
                callExprNames.push_back(re->ref.identifier);
            }
        }
        return VisitAction::WALK_CHILDREN;
    });

    walker.Walk();

    std::string expectedCallExprNames[] = {"print"};

    ASSERT_EQ(std::size(expectedCallExprNames), callExprNames.size());
    for (size_t i = 0; i < callExprNames.size(); i++) {
        EXPECT_EQ(expectedCallExprNames[i], callExprNames[i]);
    }
}
