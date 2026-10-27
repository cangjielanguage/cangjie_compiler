// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include <gtest/gtest.h>

#include "cangjie/AST/Node.h"

#include "cangjie/AST/Match.h"

#include "cangjie/Parse/Parser.h"
#include "cangjie/Basic/DiagnosticEngine.h"

using namespace Cangjie;
using namespace AST;

namespace {
OwnedPtr<Expr> ParseExprFromSrc(const std::string& src)
{
    static DiagnosticEngine diag;
    static SourceManager sm;
    sm.AddSource("./", src);
    diag.SetSourceManager(&sm);
    Parser parser(src, diag, diag.GetSourceManager(), {});
    return parser.ParseExpr();
}

OwnedPtr<Type> ParseTypeFromSrc(const std::string& src)
{
    static DiagnosticEngine diag;
    static SourceManager sm;
    sm.AddSource("./", src);
    diag.SetSourceManager(&sm);
    Parser parser(src, diag, diag.GetSourceManager(), {});
    return parser.ParseType();
}

OwnedPtr<Decl> ParseDeclFromSrc(const std::string& src)
{
    static DiagnosticEngine diag;
    static SourceManager sm;
    sm.AddSource("./", src);
    diag.SetSourceManager(&sm);
    Parser parser(src, diag, diag.GetSourceManager(), {});
    return parser.ParseDecl(ScopeKind::TOPLEVEL);
}

OwnedPtr<Decl> ParseDeclFromSrcExperimental(const std::string& src)
{
    static DiagnosticEngine diag;
    static SourceManager sm;
    sm.AddSource("./", src);
    diag.SetSourceManager(&sm);
    GlobalOptions opts;
    opts.enableEH = true;
    Parser parser(src, diag, diag.GetSourceManager(), {}, false, &opts);
    return parser.ParseDecl(ScopeKind::TOPLEVEL);
}

OwnedPtr<File> ParseFileFromSrc(const std::string& src)
{
    static DiagnosticEngine diag;
    static SourceManager sm;
    sm.AddSource("./", src);
    diag.SetSourceManager(&sm);
    Parser parser(src, diag, diag.GetSourceManager(), {});
    return parser.ParseTopLevel();
}

template <typename TypeNode>
void ExpectTypeToStringAndKind(const std::string& src)
{
    auto type = ParseTypeFromSrc(src);
    ASSERT_NE(type, nullptr) << "Failed to parse type: " << src;
    EXPECT_TRUE(Is<TypeNode>(type.get())) << src;
    EXPECT_EQ(src, type->ToString());
}

template <typename ExprNode>
void ExpectExprToStringAndKind(const std::string& src)
{
    auto expr = ParseExprFromSrc(src);
    ASSERT_NE(expr, nullptr) << "Failed to parse expr: " << src;
    EXPECT_TRUE(Is<ExprNode>(expr.get())) << src;
    EXPECT_EQ(src, expr->ToString());
}
} // namespace

TEST(ToStringTest, BasicNodeSourceRestore)
{
    auto expectExprToString = [](const std::string& src) {
        auto expr = ParseExprFromSrc(src);
        ASSERT_NE(expr, nullptr) << "Failed to parse: " << src;
        EXPECT_EQ(src, expr->ToString());
    };

    expectExprToString("a.b");
    expectExprToString("foo()");
    expectExprToString("foo(1)");
    expectExprToString("foo(1, 2)");
    expectExprToString("[1]");
    expectExprToString("[1, 2]");
}

TEST(ToStringTest, BasicNodeNullSafety)
{
    auto callExpr = ParseExprFromSrc("foo()");
    ASSERT_NE(callExpr, nullptr) << "Failed to parse expr: foo()";
    ASSERT_TRUE(Is<CallExpr>(callExpr.get()));
    auto parsedCallExpr = As<ASTKind::CALL_EXPR>(callExpr.get());
    EXPECT_TRUE(parsedCallExpr->args.empty());
    EXPECT_EQ("foo()", parsedCallExpr->ToString());

    auto namedArgCallExpr = ParseExprFromSrc("foo(name: 1)");
    ASSERT_NE(namedArgCallExpr, nullptr) << "Failed to parse expr: foo(name: 1)";
    ASSERT_TRUE(Is<CallExpr>(namedArgCallExpr.get()));
    auto parsedNamedArgCallExpr = As<ASTKind::CALL_EXPR>(namedArgCallExpr.get());
    ASSERT_EQ(parsedNamedArgCallExpr->args.size(), 1);
    ASSERT_NE(parsedNamedArgCallExpr->args.front(), nullptr);
    EXPECT_EQ("name: 1", parsedNamedArgCallExpr->args.front()->ToString());
    EXPECT_EQ("foo(name: 1)", parsedNamedArgCallExpr->ToString());

    auto memberAccessExpr = ParseExprFromSrc("obj.field");
    ASSERT_NE(memberAccessExpr, nullptr) << "Failed to parse expr: obj.field";
    ASSERT_TRUE(Is<MemberAccess>(memberAccessExpr.get()));
    auto parsedMemberAccessExpr = As<ASTKind::MEMBER_ACCESS>(memberAccessExpr.get());
    ASSERT_NE(parsedMemberAccessExpr->baseExpr, nullptr);
    EXPECT_EQ("obj.field", parsedMemberAccessExpr->ToString());

    (void)parsedCallExpr->ToString();
    (void)parsedNamedArgCallExpr->args.front()->ToString();
    (void)parsedNamedArgCallExpr->ToString();
    (void)parsedMemberAccessExpr->ToString();
}

TEST(ToStringTest, ExtendedNodesParsing)
{
    auto expectTypeToString = [](const std::string& src) {
        auto type = ParseTypeFromSrc(src);
        ASSERT_NE(type, nullptr) << "Failed to parse type: " << src;
        (void)type->ToString();
    };

    auto expectDeclToString = [](const std::string& src) {
        auto decl = ParseDeclFromSrc(src);
        ASSERT_NE(decl, nullptr) << "Failed to parse decl: " << src;
        (void)decl->ToString();
    };

    auto expectFileToString = [](const std::string& src) {
        auto file = ParseFileFromSrc(src);
        ASSERT_NE(file, nullptr) << "Failed to parse file: " << src;
        (void)file->ToString();
    };

    // Test parser coverage over RefType, VarDecl, MultiModifiers, etc.
    expectTypeToString("Int64");
    expectTypeToString("Array<Int64>");
    expectTypeToString("*Int64");

    expectDeclToString("let x = 1");
    expectDeclToString("var myVar: Int64 = 0");
    expectDeclToString("public mut var x: Int64 = 0");

    expectFileToString("package my_pkg\n");
    expectFileToString("import some_pkg.*\n");
    auto foreignFuncFile = ParseFileFromSrc("@C\nfunc c_foo() {}\n");
    ASSERT_NE(foreignFuncFile, nullptr) << "Failed to parse file: @C\\nfunc c_foo() {}\\n";
    (void)foreignFuncFile->ToString();
}

TEST(ToStringTest, TypeAndSpecialTypeRestore)
{
    ExpectTypeToStringAndKind<PrimitiveType>("Int64");
    ExpectTypeToStringAndKind<ParenType>("(Int64)");
    ExpectTypeToStringAndKind<OptionType>("?Int64");
    ExpectTypeToStringAndKind<OptionType>("??Int64");
    ExpectTypeToStringAndKind<TupleType>("(Int64, Int64)");
    ExpectTypeToStringAndKind<FuncType>("() -> Unit");
    ExpectTypeToStringAndKind<FuncType>("(Int64) -> Int64");
    ExpectTypeToStringAndKind<FuncType>("(Int64, Float64) -> Unit");
    ExpectTypeToStringAndKind<QualifiedType>("My.Type<Int64>");
    ExpectTypeToStringAndKind<VArrayType>("VArray<Int64, $3>");

    auto classDecl = ParseDeclFromSrc("class Data { func f(): This { this } }");
    ASSERT_NE(classDecl, nullptr) << "Failed to parse decl: class Data { func f(): This { this } }";
    ASSERT_TRUE(Is<ClassDecl>(classDecl.get()));
    auto parsedClassDecl = As<ASTKind::CLASS_DECL>(classDecl.get());
    ASSERT_NE(parsedClassDecl->body, nullptr);
    ASSERT_FALSE(parsedClassDecl->body->decls.empty());
    ASSERT_TRUE(Is<FuncDecl>(parsedClassDecl->body->decls.front().get()));
    auto funcDecl = As<ASTKind::FUNC_DECL>(parsedClassDecl->body->decls.front().get());
    ASSERT_NE(funcDecl->funcBody, nullptr);
    ASSERT_NE(funcDecl->funcBody->retType, nullptr);
    ASSERT_TRUE(Is<ThisType>(funcDecl->funcBody->retType.get()));
    EXPECT_EQ("This", funcDecl->funcBody->retType->ToString());

    auto varrayType = ParseTypeFromSrc("VArray<Int64, $3>");
    ASSERT_NE(varrayType, nullptr) << "Failed to parse type: VArray<Int64, $3>";
    ASSERT_TRUE(Is<VArrayType>(varrayType.get()));
    auto parsedVArrayType = As<ASTKind::VARRAY_TYPE>(varrayType.get());
    ASSERT_NE(parsedVArrayType->constantType, nullptr);
    ASSERT_TRUE(Is<ConstantType>(parsedVArrayType->constantType.get()));
    EXPECT_EQ("$3", parsedVArrayType->constantType->ToString());
}

TEST(ToStringTest, PrimitiveTypeToStringPrefersSourceSpelling)
{
    auto primitiveType = ParseTypeFromSrc("Int32");
    ASSERT_NE(primitiveType, nullptr) << "Failed to parse type: Int32";
    ASSERT_TRUE(Is<PrimitiveType>(primitiveType.get()));
    EXPECT_EQ("Int32", primitiveType->ToString());
}

TEST(ToStringTest, TypeAndOptionalExprRestore)
{
    ExpectExprToStringAndKind<ParenExpr>("(1)");
    ExpectExprToStringAndKind<AsExpr>("1 as Int64");
    ExpectExprToStringAndKind<IsExpr>("1 is Int64");
    ExpectExprToStringAndKind<TypeConvExpr>("Int64(1)");
    ExpectExprToStringAndKind<OptionalChainExpr>("a?.b");

    auto optionalExprChain = ParseExprFromSrc("x?.b?[0]");
    ASSERT_NE(optionalExprChain, nullptr) << "Failed to parse expr: x?.b?[0]";
    ASSERT_TRUE(Is<OptionalChainExpr>(optionalExprChain.get()));
    auto optionalChainExpr = As<ASTKind::OPTIONAL_CHAIN_EXPR>(optionalExprChain.get());
    ASSERT_NE(optionalChainExpr->expr, nullptr);
    ASSERT_TRUE(Is<SubscriptExpr>(optionalChainExpr->expr.get()));
    auto subscriptExpr = As<ASTKind::SUBSCRIPT_EXPR>(optionalChainExpr->expr.get());
    ASSERT_NE(subscriptExpr->baseExpr, nullptr);
    ASSERT_TRUE(Is<OptionalExpr>(subscriptExpr->baseExpr.get()));
    EXPECT_EQ("x?.b?", subscriptExpr->baseExpr->ToString());

    auto primitiveTypeExprCall = ParseExprFromSrc("Int64.foo()");
    ASSERT_NE(primitiveTypeExprCall, nullptr) << "Failed to parse expr: Int64.foo()";
    ASSERT_TRUE(Is<CallExpr>(primitiveTypeExprCall.get()));
    auto callExpr = As<ASTKind::CALL_EXPR>(primitiveTypeExprCall.get());
    ASSERT_NE(callExpr->baseFunc, nullptr);
    ASSERT_TRUE(Is<MemberAccess>(callExpr->baseFunc.get()));
    auto memberAccess = As<ASTKind::MEMBER_ACCESS>(callExpr->baseFunc.get());
    ASSERT_NE(memberAccess->baseExpr, nullptr);
    ASSERT_TRUE(Is<PrimitiveTypeExpr>(memberAccess->baseExpr.get()));
    EXPECT_EQ("Int64", memberAccess->baseExpr->ToString());
    EXPECT_EQ("Int64.foo()", primitiveTypeExprCall->ToString());
}

TEST(ToStringTest, PatternLeafAndLetPatternRestore)
{
    auto wildcardExpr = ParseExprFromSrc("if(let _ <- x) {}");
    ASSERT_NE(wildcardExpr, nullptr) << "Failed to parse expr: if(let _ <- x) {}";
    ASSERT_TRUE(Is<IfExpr>(wildcardExpr.get()));
    auto wildcardIfExpr = As<ASTKind::IF_EXPR>(wildcardExpr.get());
    ASSERT_NE(wildcardIfExpr->condExpr, nullptr);
    ASSERT_TRUE(Is<LetPatternDestructor>(wildcardIfExpr->condExpr.get()));
    auto wildcardLetPattern = As<ASTKind::LET_PATTERN_DESTRUCTOR>(wildcardIfExpr->condExpr.get());
    ASSERT_EQ(wildcardLetPattern->patterns.size(), 1);
    ASSERT_NE(wildcardLetPattern->patterns[0], nullptr);
    ASSERT_TRUE(Is<WildcardPattern>(wildcardLetPattern->patterns[0].get()));
    EXPECT_EQ("_", wildcardLetPattern->patterns[0]->ToString());
    EXPECT_EQ("let _ <- x", wildcardLetPattern->ToString());

    auto constExpr = ParseExprFromSrc("match (x) { case 1 => 1 }");
    ASSERT_NE(constExpr, nullptr) << "Failed to parse expr: match (x) { case 1 => 1 }";
    ASSERT_TRUE(Is<MatchExpr>(constExpr.get()));
    auto constMatchExpr = As<ASTKind::MATCH_EXPR>(constExpr.get());
    ASSERT_FALSE(constMatchExpr->matchCases.empty());
    ASSERT_EQ(constMatchExpr->matchCases[0]->patterns.size(), 1);
    ASSERT_TRUE(Is<ConstPattern>(constMatchExpr->matchCases[0]->patterns[0].get()));
    auto constPattern = As<ASTKind::CONST_PATTERN>(constMatchExpr->matchCases[0]->patterns[0].get());
    EXPECT_EQ("1", constPattern->ToString());

    auto ifLetExpr = ParseExprFromSrc("if(let value <- x) {}");
    ASSERT_NE(ifLetExpr, nullptr) << "Failed to parse expr: if(let value <- x) {}";
    ASSERT_TRUE(Is<IfExpr>(ifLetExpr.get()));
    auto ifExpr = As<ASTKind::IF_EXPR>(ifLetExpr.get());
    ASSERT_NE(ifExpr->condExpr, nullptr);
    ASSERT_TRUE(Is<LetPatternDestructor>(ifExpr->condExpr.get()));
    auto letPattern = As<ASTKind::LET_PATTERN_DESTRUCTOR>(ifExpr->condExpr.get());
    ASSERT_EQ(letPattern->patterns.size(), 1);
    ASSERT_NE(letPattern->patterns[0], nullptr);
    ASSERT_TRUE(Is<VarPattern>(letPattern->patterns[0].get()) || Is<VarOrEnumPattern>(letPattern->patterns[0].get()));
    EXPECT_EQ("value", letPattern->patterns[0]->ToString());
    EXPECT_EQ("let value <- x", letPattern->ToString());
}

namespace {
constexpr size_t kTuplePatternElementCount = 2;
constexpr size_t kCatchTypeCount = 2;

void VerifyTuplePatternRestore()
{
    auto tupleExpr = ParseExprFromSrc("match (x) { case (Color.Red, _) => 1 }");
    ASSERT_NE(tupleExpr, nullptr) << "Failed to parse expr: match (x) { case (Color.Red, _) => 1 }";
    ASSERT_TRUE(Is<MatchExpr>(tupleExpr.get()));
    auto tupleMatchExpr = As<ASTKind::MATCH_EXPR>(tupleExpr.get());
    ASSERT_FALSE(tupleMatchExpr->matchCases.empty());
    ASSERT_EQ(tupleMatchExpr->matchCases[0]->patterns.size(), 1);
    ASSERT_TRUE(Is<TuplePattern>(tupleMatchExpr->matchCases[0]->patterns[0].get()));
    auto tuplePattern = As<ASTKind::TUPLE_PATTERN>(tupleMatchExpr->matchCases[0]->patterns[0].get());
    ASSERT_EQ(tuplePattern->patterns.size(), kTuplePatternElementCount);
    ASSERT_NE(tuplePattern->patterns[0], nullptr);
    ASSERT_NE(tuplePattern->patterns[1], nullptr);
    ASSERT_TRUE(Is<EnumPattern>(tuplePattern->patterns[0].get()));
    ASSERT_TRUE(Is<WildcardPattern>(tuplePattern->patterns[1].get()));
    EXPECT_EQ("Color.Red", tuplePattern->patterns[0]->ToString());
    EXPECT_EQ("_", tuplePattern->patterns[1]->ToString());
    EXPECT_EQ("(Color.Red, _)", tuplePattern->ToString());
}

void VerifyTypePatternRestore()
{
    auto typeExpr = ParseExprFromSrc("match (x) { case y: Int64 => 1 }");
    ASSERT_NE(typeExpr, nullptr) << "Failed to parse expr: match (x) { case y: Int64 => 1 }";
    ASSERT_TRUE(Is<MatchExpr>(typeExpr.get()));
    auto typeMatchExpr = As<ASTKind::MATCH_EXPR>(typeExpr.get());
    ASSERT_FALSE(typeMatchExpr->matchCases.empty());
    ASSERT_EQ(typeMatchExpr->matchCases[0]->patterns.size(), 1);
    ASSERT_TRUE(Is<TypePattern>(typeMatchExpr->matchCases[0]->patterns[0].get()));
    auto typePattern = As<ASTKind::TYPE_PATTERN>(typeMatchExpr->matchCases[0]->patterns[0].get());
    ASSERT_NE(typePattern->pattern, nullptr);
    ASSERT_NE(typePattern->type, nullptr);
    ASSERT_TRUE(Is<VarPattern>(typePattern->pattern.get()));
    EXPECT_EQ("y", typePattern->pattern->ToString());
    EXPECT_EQ("Int64", typePattern->type->ToString());
    EXPECT_EQ("y: Int64", typePattern->ToString());
}

void VerifyEnumPatternRestore()
{
    auto enumExpr = ParseExprFromSrc("match (x) { case Year(y) => 1 }");
    ASSERT_NE(enumExpr, nullptr) << "Failed to parse expr: match (x) { case Year(y) => 1 }";
    ASSERT_TRUE(Is<MatchExpr>(enumExpr.get()));
    auto enumMatchExpr = As<ASTKind::MATCH_EXPR>(enumExpr.get());
    ASSERT_FALSE(enumMatchExpr->matchCases.empty());
    ASSERT_EQ(enumMatchExpr->matchCases[0]->patterns.size(), 1);
    ASSERT_TRUE(Is<EnumPattern>(enumMatchExpr->matchCases[0]->patterns[0].get()));
    auto enumPattern = As<ASTKind::ENUM_PATTERN>(enumMatchExpr->matchCases[0]->patterns[0].get());
    ASSERT_NE(enumPattern->constructor, nullptr);
    ASSERT_EQ(enumPattern->patterns.size(), 1);
    EXPECT_EQ("Year(y)", enumPattern->ToString());
}

void VerifyVarOrEnumPatternRestore()
{
    auto varOrEnumExpr = ParseExprFromSrc("match (x) { case who => 1 }");
    ASSERT_NE(varOrEnumExpr, nullptr) << "Failed to parse expr: match (x) { case who => 1 }";
    ASSERT_TRUE(Is<MatchExpr>(varOrEnumExpr.get()));
    auto varOrEnumMatchExpr = As<ASTKind::MATCH_EXPR>(varOrEnumExpr.get());
    ASSERT_FALSE(varOrEnumMatchExpr->matchCases.empty());
    ASSERT_EQ(varOrEnumMatchExpr->matchCases[0]->patterns.size(), 1);
    ASSERT_TRUE(Is<VarOrEnumPattern>(varOrEnumMatchExpr->matchCases[0]->patterns[0].get()));
    auto varOrEnumPattern = As<ASTKind::VAR_OR_ENUM_PATTERN>(varOrEnumMatchExpr->matchCases[0]->patterns[0].get());
    EXPECT_EQ("who", varOrEnumPattern->ToString());
}

void VerifyExceptTypePatternRestore()
{
    auto tryExpr = ParseExprFromSrc("try {} catch(e: Exception1 | Exception2) {}");
    ASSERT_NE(tryExpr, nullptr) << "Failed to parse expr: try {} catch(e: Exception1 | Exception2) {}";
    ASSERT_TRUE(Is<TryExpr>(tryExpr.get()));
    auto parsedTryExpr = As<ASTKind::TRY_EXPR>(tryExpr.get());
    ASSERT_EQ(parsedTryExpr->catchPatterns.size(), 1);
    ASSERT_NE(parsedTryExpr->catchPatterns[0], nullptr);
    ASSERT_TRUE(Is<ExceptTypePattern>(parsedTryExpr->catchPatterns[0].get()));
    auto catchPattern = As<ASTKind::EXCEPT_TYPE_PATTERN>(parsedTryExpr->catchPatterns[0].get());
    ASSERT_NE(catchPattern->pattern, nullptr);
    ASSERT_TRUE(Is<VarPattern>(catchPattern->pattern.get()));
    ASSERT_EQ(catchPattern->types.size(), kCatchTypeCount);
    EXPECT_EQ("e: Exception1 | Exception2", catchPattern->ToString());
}

void VerifyCommandTypePatternRestore()
{
    auto funcDecl = ParseDeclFromSrcExperimental("func f() { try {} handle(effect: Effect1 | Effect2) {} }");
    ASSERT_NE(funcDecl, nullptr) << "Failed to parse decl: func f() { try {} handle(effect: Effect1 | Effect2) {} }";
    ASSERT_TRUE(Is<FuncDecl>(funcDecl.get()));
    auto parsedFuncDecl = As<ASTKind::FUNC_DECL>(funcDecl.get());
    ASSERT_NE(parsedFuncDecl->funcBody, nullptr);
    ASSERT_NE(parsedFuncDecl->funcBody->body, nullptr);
    ASSERT_FALSE(parsedFuncDecl->funcBody->body->body.empty());
    ASSERT_TRUE(Is<TryExpr>(parsedFuncDecl->funcBody->body->body[0].get()));
    auto parsedTryExpr = As<ASTKind::TRY_EXPR>(parsedFuncDecl->funcBody->body->body[0].get());
    ASSERT_EQ(parsedTryExpr->handlers.size(), 1);
    ASSERT_NE(parsedTryExpr->handlers[0].commandPattern, nullptr);
    ASSERT_TRUE(Is<CommandTypePattern>(parsedTryExpr->handlers[0].commandPattern.get()));
    auto commandPattern = As<ASTKind::COMMAND_TYPE_PATTERN>(parsedTryExpr->handlers[0].commandPattern.get());
    ASSERT_NE(commandPattern->pattern, nullptr);
    ASSERT_TRUE(Is<VarPattern>(commandPattern->pattern.get()));
    ASSERT_EQ(commandPattern->types.size(), kCatchTypeCount);
    EXPECT_EQ("effect: Effect1 | Effect2", commandPattern->ToString());
}
} // namespace

TEST(ToStringTest, PatternCompositeRestore)
{
    VerifyTuplePatternRestore();
    VerifyTypePatternRestore();
    VerifyEnumPatternRestore();
    VerifyVarOrEnumPatternRestore();
    VerifyExceptTypePatternRestore();
    VerifyCommandTypePatternRestore();
}

namespace {
void VerifyIfExprToString()
{
    auto ifExpr = ParseExprFromSrc("if (true) {}");
    ASSERT_NE(ifExpr, nullptr);
    ASSERT_TRUE(Is<IfExpr>(ifExpr.get()));
    auto parsed = As<ASTKind::IF_EXPR>(ifExpr.get());
    EXPECT_EQ("if (true) {}", parsed->ToString());

    auto ifElseExpr = ParseExprFromSrc("if (true) {} else {}");
    ASSERT_NE(ifElseExpr, nullptr);
    ASSERT_TRUE(Is<IfExpr>(ifElseExpr.get()));
    auto parsedElse = As<ASTKind::IF_EXPR>(ifElseExpr.get());
    EXPECT_EQ("if (true) {} else {}", parsedElse->ToString());
}

void VerifyWhileExprToString()
{
    auto whileExpr = ParseExprFromSrc("while (true) {}");
    ASSERT_NE(whileExpr, nullptr);
    ASSERT_TRUE(Is<WhileExpr>(whileExpr.get()));
    auto parsed = As<ASTKind::WHILE_EXPR>(whileExpr.get());
    EXPECT_EQ("while (true) {}", parsed->ToString());
}

void VerifyDoWhileExprToString()
{
    auto doWhileExpr = ParseExprFromSrc("do {} while (true)");
    ASSERT_NE(doWhileExpr, nullptr);
    ASSERT_TRUE(Is<DoWhileExpr>(doWhileExpr.get()));
    auto parsed = As<ASTKind::DO_WHILE_EXPR>(doWhileExpr.get());
    EXPECT_EQ("do {} while (true)", parsed->ToString());
}

void VerifyForInExprToString()
{
    auto forInExpr = ParseExprFromSrc("for (i in arr) {}");
    ASSERT_NE(forInExpr, nullptr);
    ASSERT_TRUE(Is<ForInExpr>(forInExpr.get()));
    auto parsed = As<ASTKind::FOR_IN_EXPR>(forInExpr.get());
    EXPECT_EQ("for (i in arr) {}", parsed->ToString());
}

void VerifyForInWhereExprToString()
{
    auto forInWhere = ParseExprFromSrc("for (i in arr where i > 0) {}");
    ASSERT_NE(forInWhere, nullptr);
    ASSERT_TRUE(Is<ForInExpr>(forInWhere.get()));
    auto parsed = As<ASTKind::FOR_IN_EXPR>(forInWhere.get());
    (void)parsed->ToString();
}

void VerifyReturnExprToString()
{
    auto returnExpr = ParseExprFromSrc("return");
    ASSERT_NE(returnExpr, nullptr);
    ASSERT_TRUE(Is<ReturnExpr>(returnExpr.get()));
    auto parsed = As<ASTKind::RETURN_EXPR>(returnExpr.get());
    (void)parsed->ToString();

    auto returnValExpr = ParseExprFromSrc("return 1");
    ASSERT_NE(returnValExpr, nullptr);
    ASSERT_TRUE(Is<ReturnExpr>(returnValExpr.get()));
    auto parsedVal = As<ASTKind::RETURN_EXPR>(returnValExpr.get());
    EXPECT_EQ("return 1", parsedVal->ToString());
}

void VerifyJumpExprToString()
{
    auto breakExpr = ParseExprFromSrc("break");
    ASSERT_NE(breakExpr, nullptr);
    ASSERT_TRUE(Is<JumpExpr>(breakExpr.get()));
    auto parsedBreak = As<ASTKind::JUMP_EXPR>(breakExpr.get());
    EXPECT_EQ("break", parsedBreak->ToString());

    auto continueExpr = ParseExprFromSrc("continue");
    ASSERT_NE(continueExpr, nullptr);
    ASSERT_TRUE(Is<JumpExpr>(continueExpr.get()));
    auto parsedCont = As<ASTKind::JUMP_EXPR>(continueExpr.get());
    EXPECT_EQ("continue", parsedCont->ToString());
}

void VerifyThrowExprToString()
{
    auto throwExpr = ParseExprFromSrc("throw e");
    ASSERT_NE(throwExpr, nullptr);
    ASSERT_TRUE(Is<ThrowExpr>(throwExpr.get()));
    auto parsed = As<ASTKind::THROW_EXPR>(throwExpr.get());
    EXPECT_EQ("throw e", parsed->ToString());
}

void VerifyMatchExprToString()
{
    auto matchExpr = ParseExprFromSrc("match (x) { case 1 => 1 }");
    ASSERT_NE(matchExpr, nullptr);
    ASSERT_TRUE(Is<MatchExpr>(matchExpr.get()));
    auto parsed = As<ASTKind::MATCH_EXPR>(matchExpr.get());
    (void)parsed->ToString();
}

void VerifyMatchMultiCaseToString()
{
    auto matchMulti = ParseExprFromSrc("match (x) { case 1 => 1 case 2 => 2 }");
    ASSERT_NE(matchMulti, nullptr);
    ASSERT_TRUE(Is<MatchExpr>(matchMulti.get()));
    (void)As<ASTKind::MATCH_EXPR>(matchMulti.get())->ToString();

    auto matchGuard = ParseExprFromSrc("match (x) { case 1 where x > 0 => 1 }");
    ASSERT_NE(matchGuard, nullptr);
    ASSERT_TRUE(Is<MatchExpr>(matchGuard.get()));
    (void)As<ASTKind::MATCH_EXPR>(matchGuard.get())->ToString();

    auto matchMultiPattern = ParseExprFromSrc("match (x) { case 1 | 2 => 3 }");
    ASSERT_NE(matchMultiPattern, nullptr);
    ASSERT_TRUE(Is<MatchExpr>(matchMultiPattern.get()));
    (void)As<ASTKind::MATCH_EXPR>(matchMultiPattern.get())->ToString();
}

void VerifyTryCatchToString()
{
    auto tryCatchExpr = ParseExprFromSrc("try {} catch(e: Exception) {}");
    ASSERT_NE(tryCatchExpr, nullptr);
    ASSERT_TRUE(Is<TryExpr>(tryCatchExpr.get()));
    auto parsed = As<ASTKind::TRY_EXPR>(tryCatchExpr.get());
    (void)parsed->ToString();

    auto tryCatchFinally = ParseExprFromSrc("try {} catch(e: Exception) {} finally {}");
    ASSERT_NE(tryCatchFinally, nullptr);
    ASSERT_TRUE(Is<TryExpr>(tryCatchFinally.get()));
    (void)As<ASTKind::TRY_EXPR>(tryCatchFinally.get())->ToString();

    auto tryFinally = ParseExprFromSrc("try {} finally {}");
    ASSERT_NE(tryFinally, nullptr);
    ASSERT_TRUE(Is<TryExpr>(tryFinally.get()));
    (void)As<ASTKind::TRY_EXPR>(tryFinally.get())->ToString();
}

void VerifyTryHandleToString()
{
    auto funcDecl = ParseDeclFromSrcExperimental("func f() { try {} handle(effect: Effect1) {} }");
    ASSERT_NE(funcDecl, nullptr);
    ASSERT_TRUE(Is<FuncDecl>(funcDecl.get()));
    auto parsedFuncDecl = As<ASTKind::FUNC_DECL>(funcDecl.get());
    ASSERT_NE(parsedFuncDecl->funcBody, nullptr);
    ASSERT_NE(parsedFuncDecl->funcBody->body, nullptr);
    ASSERT_FALSE(parsedFuncDecl->funcBody->body->body.empty());
    ASSERT_TRUE(Is<TryExpr>(parsedFuncDecl->funcBody->body->body[0].get()));
    auto parsedTryExpr = As<ASTKind::TRY_EXPR>(parsedFuncDecl->funcBody->body->body[0].get());
    (void)parsedTryExpr->ToString();
}

void VerifyFuncDeclToString()
{
    auto funcDecl = ParseDeclFromSrc("func foo() {}");
    ASSERT_NE(funcDecl, nullptr);
    ASSERT_TRUE(Is<FuncDecl>(funcDecl.get()));
    (void)As<ASTKind::FUNC_DECL>(funcDecl.get())->ToString();

    auto funcWithParam = ParseDeclFromSrc("func foo(a: Int64) {}");
    ASSERT_NE(funcWithParam, nullptr);
    ASSERT_TRUE(Is<FuncDecl>(funcWithParam.get()));
    (void)As<ASTKind::FUNC_DECL>(funcWithParam.get())->ToString();

    auto funcWithRet = ParseDeclFromSrc("func foo(a: Int64): Int64 { return a }");
    ASSERT_NE(funcWithRet, nullptr);
    ASSERT_TRUE(Is<FuncDecl>(funcWithRet.get()));
    (void)As<ASTKind::FUNC_DECL>(funcWithRet.get())->ToString();

    auto funcGeneric = ParseDeclFromSrc("func foo<T>(a: T): T { return a }");
    ASSERT_NE(funcGeneric, nullptr);
    ASSERT_TRUE(Is<FuncDecl>(funcGeneric.get()));
    (void)As<ASTKind::FUNC_DECL>(funcGeneric.get())->ToString();
}

void VerifyClassBodyToString()
{
    auto classDecl = ParseDeclFromSrc("class Data { func f(): This { this } }");
    ASSERT_NE(classDecl, nullptr);
    ASSERT_TRUE(Is<ClassDecl>(classDecl.get()));
    auto parsedClassDecl = As<ASTKind::CLASS_DECL>(classDecl.get());
    ASSERT_NE(parsedClassDecl->body, nullptr);
    (void)parsedClassDecl->body->ToString();
    (void)parsedClassDecl->ToString();
}

void VerifyStructBodyToString()
{
    auto structDecl = ParseDeclFromSrc("struct Point { var x: Int64 }");
    ASSERT_NE(structDecl, nullptr);
    ASSERT_TRUE(Is<StructDecl>(structDecl.get()));
    auto parsedStructDecl = As<ASTKind::STRUCT_DECL>(structDecl.get());
    ASSERT_NE(parsedStructDecl->body, nullptr);
    (void)parsedStructDecl->body->ToString();
    (void)parsedStructDecl->ToString();
}

void VerifyInterfaceBodyToString()
{
    auto interfaceDecl = ParseDeclFromSrc("interface I { func f(): Int64 }");
    ASSERT_NE(interfaceDecl, nullptr);
    ASSERT_TRUE(Is<InterfaceDecl>(interfaceDecl.get()));
    auto parsedInterfaceDecl = As<ASTKind::INTERFACE_DECL>(interfaceDecl.get());
    ASSERT_NE(parsedInterfaceDecl->body, nullptr);
    (void)parsedInterfaceDecl->body->ToString();
    (void)parsedInterfaceDecl->ToString();
}

void VerifyGenericToString()
{
    auto funcDecl = ParseDeclFromSrc("func foo<T>(a: T): T { return a }");
    ASSERT_NE(funcDecl, nullptr);
    ASSERT_TRUE(Is<FuncDecl>(funcDecl.get()));
    auto parsed = As<ASTKind::FUNC_DECL>(funcDecl.get());
    ASSERT_NE(parsed->funcBody, nullptr);
    ASSERT_NE(parsed->funcBody->generic, nullptr);
    EXPECT_FALSE(parsed->funcBody->generic->typeParameters.empty());
    EXPECT_EQ("T", parsed->funcBody->generic->typeParameters[0]->ToString());
}

void VerifyGenericConstraintToString()
{
    auto funcDecl = ParseDeclFromSrc("func foo<T>(a: T): T where T <: Comparable { return a }");
    ASSERT_NE(funcDecl, nullptr);
    ASSERT_TRUE(Is<FuncDecl>(funcDecl.get()));
    auto parsed = As<ASTKind::FUNC_DECL>(funcDecl.get());
    ASSERT_NE(parsed->funcBody, nullptr);
    ASSERT_NE(parsed->funcBody->generic, nullptr);
    (void)parsed->funcBody->generic->ToString();
}

void VerifyTypeAliasDeclToString()
{
    auto typeAliasDecl = ParseDeclFromSrc("type MyInt = Int64");
    ASSERT_NE(typeAliasDecl, nullptr);
    ASSERT_TRUE(Is<TypeAliasDecl>(typeAliasDecl.get()));
    auto parsed = As<ASTKind::TYPE_ALIAS_DECL>(typeAliasDecl.get());
    (void)parsed->ToString();
}
} // namespace

TEST(ToStringTest, ControlFlowRestore)
{
    VerifyIfExprToString();
    VerifyWhileExprToString();
    VerifyDoWhileExprToString();
    VerifyForInExprToString();
    VerifyForInWhereExprToString();
    VerifyReturnExprToString();
    VerifyJumpExprToString();
    VerifyThrowExprToString();
}

TEST(ToStringTest, MatchAndTryRestore)
{
    VerifyMatchExprToString();
    VerifyMatchMultiCaseToString();
    VerifyTryCatchToString();
    VerifyTryHandleToString();
}

TEST(ToStringTest, FunctionAndBodyRestore)
{
    VerifyFuncDeclToString();
    VerifyClassBodyToString();
    VerifyStructBodyToString();
    VerifyInterfaceBodyToString();
}

TEST(ToStringTest, GenericAndDeclRestore)
{
    VerifyGenericToString();
    VerifyGenericConstraintToString();
    VerifyTypeAliasDeclToString();
}

namespace {
void VerifyInvalidNodeToString()
{
    auto invalidType = MakeOwned<InvalidType>(Position{0, 1, 1});
    EXPECT_EQ("<invalid>", invalidType->ToString());

    auto invalidExpr = MakeOwned<InvalidExpr>();
    EXPECT_EQ("<invalid>", invalidExpr->ToString());

    auto invalidDecl = MakeOwned<InvalidDecl>(Position{0, 1, 1});
    EXPECT_EQ("<invalid>", invalidDecl->ToString());

    auto invalidPattern = MakeOwned<InvalidPattern>();
    EXPECT_EQ("<invalid>", invalidPattern->ToString());
}

void VerifyImportSpecToString()
{
    auto file = ParseFileFromSrc("package my_pkg\nimport std.collection.*\n");
    ASSERT_NE(file, nullptr);
    ASSERT_FALSE(file->imports.empty());
    (void)file->imports[0]->ToString();
}

void VerifyPackageSpecToString()
{
    auto file = ParseFileFromSrc("package my_pkg\n");
    ASSERT_NE(file, nullptr);
    ASSERT_NE(file->package, nullptr);
    (void)file->package->ToString();
}

void VerifyFileToString()
{
    auto file = ParseFileFromSrc("package my_pkg\n");
    ASSERT_NE(file, nullptr);
    (void)file->ToString();
}

void VerifyMainDeclToString()
{
    auto file = ParseFileFromSrc("main() {\n}\n");
    ASSERT_NE(file, nullptr);
    ASSERT_FALSE(file->decls.empty());
    ASSERT_TRUE(Is<MainDecl>(file->decls[0].get()));
    auto mainDecl = As<ASTKind::MAIN_DECL>(file->decls[0].get());
    (void)mainDecl->ToString();
}

void VerifyStrInterpolationToString()
{
    auto expr = ParseExprFromSrc("\"hello ${name}\"");
    ASSERT_NE(expr, nullptr);
    (void)expr->ToString();
}
} // namespace

TEST(ToStringTest, PR6InvalidNodeRestore)
{
    VerifyInvalidNodeToString();
}

TEST(ToStringTest, PR6FileHeaderRestore)
{
    VerifyImportSpecToString();
    VerifyPackageSpecToString();
    VerifyFileToString();
    VerifyMainDeclToString();
    VerifyStrInterpolationToString();
}
