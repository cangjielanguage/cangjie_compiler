// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include <string>
#include "gtest/gtest.h"
#include "cangjie/Macro/NodeSerialization.h"
#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Frontend/CompilerInstance.h"
#include "cangjie/Parse/Parser.h"

using namespace Cangjie;
using namespace AST;
using namespace NodeSerialization;

class NodeSerializationTest : public testing::Test {
protected:
    void SetUp() override
    {
        binaryExpr = R"(2 * (3 + 4))";
        unaryExpr = R"(!2)";
        varDecl = R"(var a = 2 + 3)";
        funcDecl = R"(
            func MyComponent(aa: Int32, bb: Int32) : Int64 {
                // aa, bb has no meaning at all, just for testing
                var counter = @M(0) // MacroExpandExpr
                MyFoo()
                let b: Int64 = 3 * (2 + 4 - 2)
                MyBar(1, 2) // CallExpr with args
                var c : Int64
                c = b + 2020 // AssignExpr
                let d: VArray<Int64, $5> = VArray<Int64, $5>({i => i}) // VArrayType and ConstantType and ArrayExpr
                var e: (Int32) = 1 // ParenType
                let f = quote(a == b) // QuoteExpr
                return c // ReturnExpr
            }
        )";
        structDecl = R"(
            @Differentiable[except: [in_channels_, out_channels_, has_bias_, activation_]]
            public struct Dense<T> where T <: Evaluable { // GenericConstraint
              var in_channels_: Int32
              var out_channels_: Int32
              var has_bias_: Bool

              var activation_: ActivationType
              var weight_: Tensor
              var bias_: Tensor
            }
        )";

        classDecl = R"(
            class Data <: unittest.TestCases { // QualifiedType
                var a : Int32
                var b : Float32
                var c : Int32 = denseObj.in_channels_ // MemberAccess : RefExpr.field
                public func get(a: () -> Unit) : Int32 { // FuncType
                    synchronized(m) { foo() } // SynchronizedExpr
                    return 1
                }
                @M var d : Float32 // MacroExpandDecl
                type Class1<V> = GenericClassA<Int64, V> // TypeAliasDecl
                func f(): This { // ThisType
                    this
                }
            }
        )";
        interfaceDecl = R"(
            interface MyInterface {
                func foo() {}
            }
        )";
        ifExpr = R"(
            if (a) { // a > 0
                return 1
            } else {
                var a = 2021
                var b = a + 1
                let x = Int64.foo // PrimitiveTypeExpr
                return -1
            }
        )";
        lambdaExpr = R"(
            {a: Int32, b: Int32 => a + b}
        )";
    }

    std::string binaryExpr;
    std::string unaryExpr;
    std::string varDecl;
    std::string funcDecl;
    std::string structDecl;
    std::string classDecl;
    std::string interfaceDecl;
    std::string ifExpr;
    std::string lambdaExpr;
};

TEST_F(NodeSerializationTest, BinaryExpr_Serialization)
{
    SourceManager sm;
    sm.AddSource("./", binaryExpr);
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser binaryExprParser{binaryExpr, diag, sm};
    // binaryExpr: 2 * (3 + 4)
    // litConstExpr and parenExpr are also tested
    // Check specific offset of BinaryExpr using flatc generated methods.
    auto ptr = binaryExprParser.ParseExpr();
    NodeWriter binaryExprWriter(ptr.get());
    uint8_t* buffer = binaryExprWriter.ExportNode();
    buffer = buffer + 4; // move the pointer to the real position of the flatbuffer
    auto fbNode = NodeFormat::GetNode(buffer);
    EXPECT_EQ(fbNode->root_type(), NodeFormat::AnyNode_EXPR);
    auto fbExpr = fbNode->root_as_EXPR();
    EXPECT_EQ(fbExpr->expr_type(), NodeFormat::AnyExpr_BINARY_EXPR);
    auto fbBianryExpr = fbExpr->expr_as_BINARY_EXPR();
    auto leftExpr = fbBianryExpr->left_expr();
    EXPECT_EQ(leftExpr->expr_type(), NodeFormat::AnyExpr_LIT_CONST_EXPR);
    auto leftExprStr = fbBianryExpr->left_expr()->expr_as_LIT_CONST_EXPR()->literal();
    EXPECT_EQ(leftExprStr->str(), "2");
    auto rightExpr = fbBianryExpr->right_expr();
    EXPECT_EQ(rightExpr->expr_type(), NodeFormat::AnyExpr_PAREN_EXPR);
    auto onlyExpr = rightExpr->expr_as_PAREN_EXPR();
    EXPECT_EQ(onlyExpr->expr()->expr_type(), NodeFormat::AnyExpr_BINARY_EXPR);
    auto onlyExprAsBinary = onlyExpr->expr()->expr_as_BINARY_EXPR();
    auto parenLeftExprStr = onlyExprAsBinary->left_expr()->expr_as_LIT_CONST_EXPR()->literal();
    auto parenRightExprStr = onlyExprAsBinary->right_expr()->expr_as_LIT_CONST_EXPR()->literal();
    EXPECT_EQ(parenLeftExprStr->str(), "3");
    EXPECT_EQ(parenRightExprStr->str(), "4");
}

TEST_F(NodeSerializationTest, UnaryExpr_Serialization)
{
    SourceManager sm;
    sm.AddSource("./", unaryExpr);
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser unaryExprParser{unaryExpr, diag, sm};
    // unaryExpr: !2
    auto ptr = unaryExprParser.ParseExpr();
    NodeWriter unaryExprWriter(ptr.get());
    uint8_t* buffer = unaryExprWriter.ExportNode();
    buffer = buffer + 4; // move the pointer to the real position of the flatbuffer
    auto fbNode = NodeFormat::GetNode(buffer);
    EXPECT_EQ(fbNode->root_type(), NodeFormat::AnyNode_EXPR);
    EXPECT_EQ(fbNode->root_as_EXPR()->expr_type(), NodeFormat::AnyExpr_UNARY_EXPR);
    auto fbUnaryExpr = fbNode->root_as_EXPR()->expr_as_UNARY_EXPR();
    auto onlyExpr = fbUnaryExpr->expr()->expr_as_LIT_CONST_EXPR()->literal();
    EXPECT_EQ(onlyExpr->str(), "2");
}

TEST_F(NodeSerializationTest, VarDecl_Serialization)
{
    SourceManager sm;
    sm.AddSource("./", varDecl);
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser varDeclParser{varDecl, diag, sm};
    // varDecl: var a = 2 + 3
    auto ptr = varDeclParser.ParseDecl(ScopeKind::FUNC_BODY);
    NodeWriter varDeclWriter(ptr.get());
    uint8_t* buffer = varDeclWriter.ExportNode();
    buffer = buffer + 4; // move the pointer to the real position of the flatbuffer
    auto fbNode = NodeFormat::GetNode(buffer);
    EXPECT_EQ(fbNode->root_type(), NodeFormat::AnyNode_DECL);
    EXPECT_EQ(fbNode->root_as_DECL()->decl_type(), NodeFormat::AnyDecl_VAR_DECL);
    auto fbVarDecl = fbNode->root_as_DECL()->decl_as_VAR_DECL();
    EXPECT_EQ(fbVarDecl->is_var(), true);
    EXPECT_EQ(fbVarDecl->base()->identifier()->str(), "a");
    EXPECT_EQ(fbVarDecl->initializer()->expr_type(), NodeFormat::AnyExpr_BINARY_EXPR);
}

TEST_F(NodeSerializationTest, FuncDecl_Serialization)
{
    SourceManager sm;
    sm.AddSource("./", funcDecl);
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser funcDeclParser{funcDecl, diag, sm};
    auto ptr = funcDeclParser.ParseDecl(ScopeKind::TOPLEVEL);
    NodeWriter funcDeclWriter(ptr.get());
    uint8_t* buffer = funcDeclWriter.ExportNode();
    buffer = buffer + 4; // move the pointer to the real position of the flatbuffer
    auto fbNode = NodeFormat::GetNode(buffer);
    EXPECT_EQ(fbNode->root_type(), NodeFormat::AnyNode_DECL);
    EXPECT_EQ(fbNode->root_as_DECL()->decl_type(), NodeFormat::AnyDecl_FUNC_DECL);
    auto fbFuncDecl = fbNode->root_as_DECL()->decl_as_FUNC_DECL();
    EXPECT_EQ(fbFuncDecl->base()->identifier()->str(), "MyComponent");
    auto fbFuncBody = fbFuncDecl->func_body();
    std::vector<std::string> expectIdVec{"aa", "bb"};
    std::vector<std::string> realIdVec;
    auto fbParams = fbFuncBody->param_list()->params(); // flatbuffers::Vector
    for (size_t i = 0; i < fbParams->size(); ++i) {
        // Test parameters
        auto fbParam = fbParams->Get(i);
        auto paramId = fbParam->base()->base()->identifier()->str();
        realIdVec.push_back(paramId);
    }
    EXPECT_EQ(realIdVec, expectIdVec);
    auto fbFuncBlock = fbFuncBody->body()->body();
    std::vector<NodeFormat::AnyNode> expectEnumVec{NodeFormat::AnyNode_DECL, NodeFormat::AnyNode_EXPR,
        NodeFormat::AnyNode_DECL, NodeFormat::AnyNode_EXPR, NodeFormat::AnyNode_DECL, NodeFormat::AnyNode_EXPR,
        NodeFormat::AnyNode_DECL, NodeFormat::AnyNode_DECL, NodeFormat::AnyNode_DECL, NodeFormat::AnyNode_EXPR};
    std::vector<NodeFormat::AnyNode> realEnumVec;
    for (size_t i = 0; i < fbFuncBlock->size(); ++i) {
        auto fbBlockNode = fbFuncBlock->Get(i);
        auto fbNodeType = fbBlockNode->root_type();
        realEnumVec.push_back(fbNodeType);
    }
    EXPECT_EQ(expectEnumVec, realEnumVec);
}

TEST_F(NodeSerializationTest, ClassDecl_Serialization)
{
    SourceManager sm;
    sm.AddSource("./", classDecl);
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser classDeclParser{classDecl, diag, sm};
    auto ptr = classDeclParser.ParseDecl(ScopeKind::TOPLEVEL);
    NodeWriter classDeclWriter(ptr.get());
    uint8_t* buffer = classDeclWriter.ExportNode();
    buffer = buffer + 4; // point to the real pos of buffer
    auto fbNode = NodeFormat::GetNode(buffer);
    EXPECT_EQ(fbNode->root_type(), NodeFormat::AnyNode_DECL);
    EXPECT_EQ(fbNode->root_as_DECL()->decl_type(), NodeFormat::AnyDecl_CLASS_DECL);
    // test class name
    auto fbClassDecl = fbNode->root_as_DECL()->decl_as_CLASS_DECL();
    EXPECT_EQ(fbClassDecl->base()->identifier()->str(), "Data");
    // test class body
    auto fbClassBody = fbClassDecl->body()->decls();
    std::vector<NodeFormat::AnyDecl> expectDeclType{NodeFormat::AnyDecl_VAR_DECL, NodeFormat::AnyDecl_VAR_DECL,
        NodeFormat::AnyDecl_VAR_DECL, NodeFormat::AnyDecl_FUNC_DECL, NodeFormat::AnyDecl_MACRO_EXPAND_DECL,
        NodeFormat::AnyDecl_TYPE_ALIAS_DECL, NodeFormat::AnyDecl_FUNC_DECL};
    std::vector<NodeFormat::AnyDecl> realDeclType;
    std::vector<std::string> expectDeclId{"a", "b", "c", "get", "M", "", "f"};
    std::vector<std::string> realDeclId;
    for (size_t i = 0; i < fbClassBody->size(); i++) {
        auto fbDecl = fbClassBody->Get(i);
        auto declType = fbDecl->decl_type();
        realDeclType.push_back(declType);
        std::string id;
        if (declType == NodeFormat::AnyDecl_VAR_DECL) {
            auto fbVarDecl = fbDecl->decl_as_VAR_DECL();
            id = fbVarDecl->base()->identifier()->str();
        }
        if (declType == NodeFormat::AnyDecl_FUNC_DECL) {
            auto fbFuncDecl = fbDecl->decl_as_FUNC_DECL();
            id = fbFuncDecl->base()->identifier()->str();
        }
        if (declType == NodeFormat::AnyDecl_MACRO_EXPAND_DECL) {
            auto fbFuncDecl = fbDecl->decl_as_MACRO_EXPAND_DECL();
            id = fbFuncDecl->base()->identifier()->str();
        }
        realDeclId.push_back(id);
    }
    EXPECT_EQ(realDeclType, expectDeclType);
    EXPECT_EQ(realDeclId, expectDeclId);
    auto VarMemAcc = fbClassBody->Get(2)->decl_as_VAR_DECL();
    auto initExpr = VarMemAcc->initializer();
    EXPECT_EQ(initExpr->expr_type(), NodeFormat::AnyExpr_MEMBER_ACCESS);
    auto memAccExpr = initExpr->expr_as_MEMBER_ACCESS();
    auto firstPart = memAccExpr->base_expr()->expr_as_REF_EXPR()->ref()->identifier()->str();
    auto secondPart = memAccExpr->field()->str();
    EXPECT_EQ(firstPart, "denseObj");
    EXPECT_EQ(secondPart, "in_channels_");
    // test superClassType, interfaceType, sub_decls
    // test generic
}

TEST_F(NodeSerializationTest, InterfaceDecl_Serialization)
{
    SourceManager sm;
    sm.AddSource("./", interfaceDecl);
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser interfaceDeclParser{interfaceDecl, diag, sm};
    auto ptr = interfaceDeclParser.ParseDecl(ScopeKind::TOPLEVEL);
    NodeWriter interfaceDeclWriter(ptr.get());
    uint8_t* buffer = interfaceDeclWriter.ExportNode();
    buffer = buffer + 4; // point to the real pos of buffer
    auto fbNode = NodeFormat::GetNode(buffer);
    EXPECT_EQ(fbNode->root_type(), NodeFormat::AnyNode_DECL);
    EXPECT_EQ(fbNode->root_as_DECL()->decl_type(), NodeFormat::AnyDecl_INTERFACE_DECL);
    // test interface name
    auto fbInterfaceDecl = fbNode->root_as_DECL()->decl_as_INTERFACE_DECL();
    EXPECT_EQ(fbInterfaceDecl->base()->identifier()->str(), "MyInterface");
}

TEST_F(NodeSerializationTest, IfExpr_Serialization)
{
    SourceManager sm;
    sm.AddSource("./", ifExpr);
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser ifExprParser{ifExpr, diag, sm};
    auto ptr = ifExprParser.ParseExpr();
    NodeWriter ifExprWriter(ptr.get());
    uint8_t* buffer = ifExprWriter.ExportNode();
    buffer = buffer + 4; // move the pointer to the real position of the flatbuffer
    auto fbNode = NodeFormat::GetNode(buffer);
    EXPECT_EQ(fbNode->root_type(), NodeFormat::AnyNode_EXPR);
    EXPECT_EQ(fbNode->root_as_EXPR()->expr_type(), NodeFormat::AnyExpr_IF_EXPR);
}

TEST_F(NodeSerializationTest, LambdaExpr_Serialization)
{
    SourceManager sm;
    sm.AddSource("./", lambdaExpr);
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    Parser lambdaExprParser{lambdaExpr, diag, sm};
    // {a: Int32, b: Int32 => a + b}
    // For lambaExpr, it's only part of the funcBody.
    // Some fields of FuncBody are set to default(null) in lambdaExpr
    auto ptr = lambdaExprParser.ParseExpr();
    NodeWriter lambdaWriter(ptr.get());
    uint8_t* buffer = lambdaWriter.ExportNode();
    buffer = buffer + 4;
    auto fbNode = NodeFormat::GetNode(buffer);
    EXPECT_EQ(fbNode->root_type(), NodeFormat::AnyNode_EXPR);
    EXPECT_EQ(fbNode->root_as_EXPR()->expr_type(), NodeFormat::AnyExpr_LAMBDA_EXPR);

    // check parameter
    auto fbLambdaExpr = fbNode->root_as_EXPR()->expr_as_LAMBDA_EXPR();
    auto fbLambdaBody = fbLambdaExpr->body();
    auto lbdParams = fbLambdaBody->param_list()->params();
    std::vector<std::string> expectParamVec{"a", "b"};
    std::vector<std::string> realParamVec{};
    for (size_t i = 0; i < lbdParams->size(); i++) {
        auto fbParam = lbdParams->Get(i);
        auto paramId = fbParam->base()->base()->identifier()->str();
        realParamVec.push_back(paramId);
    }
    EXPECT_EQ(expectParamVec, realParamVec);

    // check => pos
    auto arrowPos = fbLambdaBody->arrow_pos();
    auto line = arrowPos->line();
    auto column = arrowPos->column();
    EXPECT_NE(line, 0);
    EXPECT_NE(column, 0);

    // check lambda body
    auto lbdBlock = fbLambdaBody->body()->body(); // flatbuffers::Vector<flatbuffers::Offset<Node>>
    std::vector<NodeFormat::AnyNode> expectNodeType{NodeFormat::AnyNode_EXPR};
    std::vector<NodeFormat::AnyNode> realNodeType{};
    for (size_t i = 0; i < lbdBlock->size(); i++) {
        auto fbNode = lbdBlock->Get(i);
        realNodeType.push_back(fbNode->root_type());
    }
    EXPECT_EQ(expectNodeType, realNodeType);
}
