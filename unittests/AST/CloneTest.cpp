// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include <sstream>
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

TEST(ModalClone, PrimitiveTypeExpressionPreservesWrittenModal)
{
    PrimitiveTypeExpr original(TypeKind::TYPE_INT64);
    original.modal.SetAt({1, 2, 8});
    original.modal.SetLocal(ASTMode::FULL, {1, 2, 9});
    auto clone = ASTCloner::Clone(Ptr(&original));
    ASSERT_NE(clone, nullptr);
    EXPECT_EQ(clone->typeKind, original.typeKind);
    EXPECT_EQ(clone->modal.Local(), ASTMode::FULL);
    EXPECT_EQ(clone->modal.AtBegin(), original.modal.AtBegin());
    EXPECT_EQ(clone->modal.LocalBegin(), original.modal.LocalBegin());
    clone->modal.SetLocal(ASTMode::HALF, {1, 3, 9});
    EXPECT_EQ(original.modal.Local(), ASTMode::FULL);
    std::ostringstream printed;
    PrintNode(clone.get(), 0, "", printed);
    EXPECT_NE(printed.str().find("@local?"), std::string::npos);
}

TEST(ModalClone, ReceiverParameterPreservesSourcePositionsAndModal)
{
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    const std::string code = "class C { func read(this @local!, x: Int64): Int64 { x } }";
    Parser parser(code, diag, sm);
    auto original = parser.ParseDecl(ScopeKind::TOPLEVEL);
    ASSERT_EQ(diag.GetErrorCount(), 0u);
    ASSERT_TRUE(Is<ClassDecl>(original.get()));
    auto originalClass = StaticCast<ClassDecl>(original.get());
    auto originalMethod = StaticCast<FuncDecl>(originalClass->body->decls[0].get());
    auto clone = ASTCloner::Clone(original.get());
    auto clonedClass = StaticCast<ClassDecl>(clone.get());
    auto clonedMethod = StaticCast<FuncDecl>(clonedClass->body->decls[0].get());
    auto originalReceiver = originalMethod->funcBody->paramLists[0]->thisParam.get();
    auto clonedReceiver = clonedMethod->funcBody->paramLists[0]->thisParam.get();
    ASSERT_NE(originalReceiver, nullptr);
    ASSERT_NE(clonedReceiver, nullptr);
    EXPECT_NE(originalReceiver, clonedReceiver);
    EXPECT_EQ(clonedReceiver->modal.Local(), ASTMode::FULL);
    EXPECT_EQ(clonedReceiver->thisPos, originalReceiver->thisPos);
    EXPECT_EQ(clonedReceiver->commaPos, originalReceiver->commaPos);
    EXPECT_EQ(clonedReceiver->outerDecl, originalReceiver->outerDecl);
    clonedReceiver->modal.SetLocal(ASTMode::HALF, {1, 4, 10});
    EXPECT_EQ(originalReceiver->modal.Local(), ASTMode::FULL);
}

TEST(ModalClone, WrittenModalNamesDistinguishOmittedAndExplicitNonLocal)
{
    EXPECT_EQ(ToString(ASTMode::NONE), "");
    EXPECT_EQ(ToString(ASTMode::NOT), "@~local");
    EXPECT_EQ(ToString(ASTMode::HALF), "@local?");
    EXPECT_EQ(ToString(ASTMode::FULL), "@local!");
}

TEST(ModalClone, AttachingCommentsPreservesModalTokenPositions)
{
    const std::string code = R"(
        // A receiver and several annotated type forms.
        class C {
            func f(this /* receiver */ @local!, a: pkg.C @local?): ?C @local? {
                exclave { a }
            }
        }
        func g(a: (Int64, Bool) @local?, b: (()->Unit) @local?, c: VArray<Int64, $2> @local!) {
            let convert = Int64.Max
        }
    )";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm, {0, 1, 1}, true);
    auto file = parser.ParseTopLevel();
    diag.EmitCategoryDiagnostics(DiagCategory::PARSE);
    ASSERT_EQ(diag.GetErrorCount(), 0u);
    auto receivers = MatchASTByNode<ThisParam>(file.get());
    ASSERT_EQ(receivers.size(), 1u);
    auto receiver = StaticCast<ThisParam>(receivers[0]);
    EXPECT_EQ(receiver->modal.Local(), ASTMode::FULL);
    EXPECT_LT(receiver->thisPos.column, receiver->modal.AtBegin().column);
    EXPECT_EQ(MatchASTByNode<ExclaveExpr>(file.get()).size(), 1u);
    auto primitives = MatchASTByNode<PrimitiveTypeExpr>(file.get());
    ASSERT_EQ(primitives.size(), 1u);
    EXPECT_EQ(StaticCast<PrimitiveTypeExpr>(primitives[0])->modal.Local(), ASTMode::NONE);
}

TEST(ModalClone, CallSourceTextPreservesExplicitLocality)
{
    const std::string code = "C()@local!";
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);
    auto expression = parser.ParseExpr();
    ASSERT_EQ(diag.GetErrorCount(), 0u);
    ASSERT_TRUE(Is<CallExpr>(expression.get()));
    EXPECT_NE(expression->ToString().find("@local!"), std::string::npos);
}
