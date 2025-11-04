// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include <string>
#include <vector>
#include "gtest/gtest.h"
#include "cangjie/AST/Clone.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/PrintNode.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Parse/Parser.h"

using namespace Cangjie;
using namespace AST;

class CloneTest : public testing::Test {
protected:
    void SetUp() override
    {
        Parser parser(code, diag, sm);
        file = parser.ParseTopLevel();
    }
    std::string code = R"(
        let clockPort   = 12
        let dataPort    = 5
        let ledNum      = 64         // led number
        let lightColor  = 0xffff0000 // led light color -> b: 255, g: 0, r: 0

        // for LED show
        var pos : int   = 0          // LED position
        var leds : int[]

        // c libary api ======= fake FFI
        func print() : unit {}
        func print(str : String) : unit {}
        func sleep(inv : int) : unit {}
        func OpenGPIO(pin : int) : unit {}
        func WriteGPIO(pin : int, val : int) : unit {}
        func SetWord(clkPort : int, dataPort : int, val : int) : unit {}

        // Util function
        func CDW(val : int) : unit {
            SetWord(clockPort, dataPort, val)
        }

        // Set the global array
        func SetChaserPattern() : unit {
            leds[pos] = lightColor
            pos = (pos + 1) % ledNum;
        }

        // Show LED: Right Shift Zero
        func ShowLED(leds : int, lightPWM : int) : unit {
            CDW(0)
            lightPWM = 0xFF000000
            CDW(0xffffffff)
        }

        func StartChaserMode() {
            while (true) {
                SetChaserPattern()
                ShowLED(leds, 0xFF000000)
                sleep(50) // fake sleep
            }
        }

        main() : int {
            print("hello world")

            // Initialize GPIO
            OpenGPIO(clockPort)
            WriteGPIO(clockPort, 1)

            OpenGPIO(dataPort)

            // Show LED
            print("Start Marquee...")
            StartChaserMode()
            return 0
        }
)";
    DiagnosticEngine diag;
    SourceManager sm;
    OwnedPtr<File> file;
};

namespace {
/// Match AST nodes according to Node Type, and return matched nodes.
template <typename T> std::vector<Ptr<Node>> MatchASTByNode(Ptr<Node> node)
{
    std::vector<Ptr<Node>> ret{};
    if (!node) {
        return ret;
    }
    Walker walker(node, [&ret](Ptr<Node> node) -> VisitAction {
        if (dynamic_cast<T*>(node.get())) {
            ret.push_back(node);
        }
        return VisitAction::WALK_CHILDREN;
    });
    walker.Walk();
    return ret;
}
} // namespace

TEST_F(CloneTest, CloneExpr)
{
    std::vector<Ptr<Node>> binaryExprs = MatchASTByNode<BinaryExpr>(file.get());
    for (auto& it : binaryExprs) {
        PrintNode(ASTCloner::Clone(Ptr(As<ASTKind::EXPR>(it))).get());
        EXPECT_TRUE(Is<BinaryExpr>(ASTCloner::Clone(Ptr(As<ASTKind::EXPR>(it))).get()));
    }
    std::vector<Ptr<Node>> callExprs = MatchASTByNode<CallExpr>(file.get());
    for (auto& it : callExprs) {
        PrintNode(ASTCloner::Clone(Ptr(As<ASTKind::EXPR>(it))).get());
        EXPECT_TRUE(Is<CallExpr>(ASTCloner::Clone(Ptr(As<ASTKind::EXPR>(it))).get()));
    }
}

TEST_F(CloneTest, CloneDecl)
{
    std::vector<Ptr<Node>> varDecls = MatchASTByNode<VarDecl>(file.get());
    for (auto& it : varDecls) {
        PrintNode(ASTCloner::Clone(Ptr(As<ASTKind::DECL>(it))).get());
        EXPECT_TRUE(Is<VarDecl>(ASTCloner::Clone(Ptr(As<ASTKind::DECL>(it))).get()));
    }
    std::vector<Ptr<Node>> funcDecls = MatchASTByNode<FuncDecl>(file.get());
    for (auto& it : funcDecls) {
        PrintNode(ASTCloner::Clone(Ptr(As<ASTKind::DECL>(it))).get());
        EXPECT_TRUE(Is<FuncDecl>(ASTCloner::Clone(Ptr(As<ASTKind::DECL>(it))).get()));
    }
}

TEST_F(CloneTest, CloneBlock)
{
    std::vector<Ptr<Node>> blocks = MatchASTByNode<Block>(file.get());
    for (auto& it : blocks) {
        PrintNode(it);
        PrintNode(ASTCloner::Clone(Ptr(As<ASTKind::BLOCK>(it))).get());
        EXPECT_TRUE(Is<Block>(ASTCloner::Clone(Ptr(As<ASTKind::BLOCK>(it))).get()));
    }
}
