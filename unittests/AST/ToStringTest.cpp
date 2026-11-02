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

// ---------------------------------------------------------------------------
// Directly-constructed nodes: cover the ToString() overloads the Parser cannot
// reach (it never synthesises LambdaExpr / SpawnExpr / SynchronizedExpr / macro
// nodes / Package / MatchCaseOther etc. from these fixtures). Each node is built
// with MakeOwned, its required children are populated to avoid CJC_NULLPTR_CHECK
// aborts, and ToString() is asserted on a characteristic fragment.
// ---------------------------------------------------------------------------

TEST(ToStringTest, ConstructedExprToString)
{
    // LambdaExpr needs a FuncBody (CJC_NULLPTR_CHECK(funcBody)).
    auto lambda = MakeOwned<LambdaExpr>(MakeOwned<FuncBody>());
    EXPECT_NE(lambda->ToString().find("{"), std::string::npos);

    // TrailingClosureExpr needs an expr (CJC_NULLPTR_CHECK(expr)); lambda is
    // optional. Give the inner expr a name so ToString() is non-empty.
    auto trailing = MakeOwned<TrailingClosureExpr>();
    auto trailingBase = MakeOwned<RefExpr>();
    trailingBase->ref = Reference("base");
    trailing->expr = std::move(trailingBase);
    EXPECT_EQ("base", trailing->ToString());

    // ArrayExpr with no type and no args prints "()" (empty-collection branch
    // plus the closing paren).
    EXPECT_EQ("()", MakeOwned<ArrayExpr>()->ToString());

    // PointerExpr with no resolved type and no arg prints "CPointer<>(".
    EXPECT_EQ("CPointer<>()", MakeOwned<PointerExpr>()->ToString());

    // QuoteExpr with no sub-exprs prints "quote()".
    EXPECT_EQ("quote()", MakeOwned<QuoteExpr>()->ToString());

    // TokenPart with no tokens ToString()s to "" (empty concatenation).
    EXPECT_TRUE(MakeOwned<TokenPart>()->ToString().empty());

    // InterpolationExpr / StrInterpolationExpr just echo rawString.
    auto interp = MakeOwned<InterpolationExpr>();
    interp->rawString = "${a}";
    EXPECT_EQ("${a}", interp->ToString());
    auto strInterp = MakeOwned<StrInterpolationExpr>();
    strInterp->rawString = "a ${b}";
    EXPECT_EQ("a ${b}", strInterp->ToString());

    // MacroExpandExpr / MacroExpandDecl prepend "@" to the macro name.
    auto macroExpr = MakeOwned<MacroExpandExpr>();
    macroExpr->identifier = "Moo";
    EXPECT_EQ("@Moo", macroExpr->ToString());
    auto macroDecl = MakeOwned<MacroExpandDecl>();
    macroDecl->identifier = "Moo";
    EXPECT_EQ("@Moo", macroDecl->ToString());

    // PerformExpr needs an inner expr (CJC_NULLPTR_CHECK(expr)).
    auto perform = MakeOwned<PerformExpr>();
    perform->expr = MakeOwned<RefExpr>();
    EXPECT_NE(perform->ToString().find("perform"), std::string::npos);

    // ResumeExpr: with/throwing are optional, so a bare node prints "resume".
    EXPECT_EQ("resume", MakeOwned<ResumeExpr>()->ToString());
    // Exercise the "with" branch by populating withExpr.
    auto resumeWith = MakeOwned<ResumeExpr>();
    resumeWith->withExpr = MakeOwned<RefExpr>();
    EXPECT_NE(resumeWith->ToString().find("resume"), std::string::npos);

    // SpawnExpr needs a task (CJC_NULLPTR_CHECK(task)).
    auto spawn = MakeOwned<SpawnExpr>();
    spawn->task = MakeOwned<RefExpr>();
    EXPECT_NE(spawn->ToString().find("spawn"), std::string::npos);

    // SynchronizedExpr needs mutex and body (both CJC_NULLPTR_CHECK).
    auto sync = MakeOwned<SynchronizedExpr>();
    sync->mutex = MakeOwned<RefExpr>();
    sync->body = MakeOwned<Block>();
    EXPECT_NE(sync->ToString().find("synchronized"), std::string::npos);

    // MatchCaseOther needs matchExpr and exprOrDecls (both CJC_NULLPTR_CHECK).
    auto caseOther = MakeOwned<MatchCaseOther>();
    caseOther->matchExpr = MakeOwned<RefExpr>();
    caseOther->exprOrDecls = MakeOwned<Block>();
    EXPECT_NE(caseOther->ToString().find("case"), std::string::npos);
}

TEST(ToStringTest, ConstructedDeclAndTypeToString)
{
    // MacroDecl prefixes "macro " to the name; funcBody is optional.
    auto macroDecl = MakeOwned<MacroDecl>();
    macroDecl->identifier = "dm";
    EXPECT_EQ("macro dm", macroDecl->ToString());

    // Package with an empty file list ToString()s to "" but runs the loop.
    auto pkg = MakeOwned<Package>("my_pkg");
    EXPECT_TRUE(pkg->ToString().empty());

    // PackageDecl takes a Package&; ToString() returns the package name.
    Package rawPkg("p1");
    auto pkgDecl = MakeOwned<PackageDecl>(rawPkg);
    EXPECT_EQ("p1", pkgDecl->ToString());

    // GenericConstraint needs a type (CJC_NULLPTR_CHECK(type)); empty
    // upperBounds skips the "<: ..." part. Give the type a name so ToString()
    // is non-empty.
    auto constraint = MakeOwned<GenericConstraint>();
    auto constraintTy = MakeOwned<RefType>();
    constraintTy->ref = Reference("T");
    constraint->type = std::move(constraintTy);
    EXPECT_EQ("T", constraint->ToString());
}

// ---------------------------------------------------------------------------
// Coverage-driven cases: directly-constructed nodes targeting the ToString(),
// Clear(), GetTarget()/SetTarget()/GetTargets(), GetInvocation() and related
// helper branches of Node.cpp that the Parser fixtures above do not reach.
// Each node is populated so that CJC_NULLPTR_CHECK / CJC_ASSERT paths do not
// abort; assertions are on a characteristic fragment of the output.
// ---------------------------------------------------------------------------

TEST(ToStringTest, ConstructedEnumPatternGetIdentifierBranches)
{
    // EnumPattern::GetIdentifier() dispatches on constructor's astKind:
    //   REF_EXPR -> ref.identifier, MEMBER_ACCESS -> field, default -> CJC_ABORT.
    // Build an EnumPattern with a RefExpr constructor.
    {
        auto ep = MakeOwned<EnumPattern>();
        auto ctor = MakeOwned<RefExpr>();
        ctor->ref = Reference("Red");
        ep->constructor = std::move(ctor);
        EXPECT_EQ("Red", ep->GetIdentifier());
    }
    // MEMBER_ACCESS branch: constructor is a MemberAccess whose `field` is set.
    {
        auto ep = MakeOwned<EnumPattern>();
        auto ctor = MakeOwned<MemberAccess>();
        ctor->field = "Green";
        ep->constructor = std::move(ctor);
        EXPECT_EQ("Green", ep->GetIdentifier());
    }
    // No constructor -> "".
    EXPECT_EQ("", MakeOwned<EnumPattern>()->GetIdentifier());
}

TEST(ToStringTest, ConstructedFuncArgToStringBranches)
{
    // name set + expr null -> "name:" (early return).
    {
        auto fa = MakeOwned<FuncArg>();
        fa->name = "n";
        fa->name.SetPos(Position{0, 1, 1}, Position{0, 1, 2});
        EXPECT_EQ("n:", fa->ToString());
    }
    // name + expr + withInout -> "inout <expr>" path.
    {
        auto fa = MakeOwned<FuncArg>();
        fa->name = "n";
        fa->name.SetPos(Position{0, 1, 1}, Position{0, 1, 2});
        auto e = MakeOwned<RefExpr>();
        e->ref = Reference("v");
        fa->expr = std::move(e);
        fa->withInout = true;
        EXPECT_NE(fa->ToString().find("inout"), std::string::npos);
    }
    // expr set, name empty -> just expr.
    {
        auto fa = MakeOwned<FuncArg>();
        auto e = MakeOwned<RefExpr>();
        e->ref = Reference("v");
        fa->expr = std::move(e);
        EXPECT_EQ("v", fa->ToString());
    }
}

TEST(ToStringTest, ConstructedLitConstExprGetNumLitTypeKind)
{
    // RUNE_BYTE -> TYPE_UINT8.
    {
        LitConstExpr lce{LitConstKind::RUNE_BYTE, "a"};
        EXPECT_EQ(TypeKind::TYPE_UINT8, lce.GetNumLitTypeKind());
    }
    // INTEGER with no suffix -> TYPE_IDEAL_INT.
    {
        LitConstExpr lce{LitConstKind::INTEGER, "123"};
        EXPECT_EQ(TypeKind::TYPE_IDEAL_INT, lce.GetNumLitTypeKind());
    }
    // INTEGER with valid i-suffix, e.g. i32 -> TYPE_INT32.
    {
        LitConstExpr lce{LitConstKind::INTEGER, "1i32"};
        EXPECT_EQ(TypeKind::TYPE_INT32, lce.GetNumLitTypeKind());
    }
    // INTEGER with valid u-suffix, e.g. u16 -> TYPE_UINT16.
    {
        LitConstExpr lce{LitConstKind::INTEGER, "1u16"};
        EXPECT_EQ(TypeKind::TYPE_UINT16, lce.GetNumLitTypeKind());
    }
    // INTEGER with an unparseable suffix width -> TYPE_INVALID (Stoi returns nullopt).
    {
        LitConstExpr lce{LitConstKind::INTEGER, "1ix"};
        EXPECT_EQ(TypeKind::TYPE_INVALID, lce.GetNumLitTypeKind());
    }
    // INTEGER with no i/u suffix char (e.g. "1f32") -> suffix empty -> TYPE_IDEAL_INT.
    {
        LitConstExpr lce{LitConstKind::INTEGER, "1f32"};
        EXPECT_EQ(TypeKind::TYPE_IDEAL_INT, lce.GetNumLitTypeKind());
    }
    // FLOAT, hex prefix -> TYPE_IDEAL_FLOAT.
    {
        LitConstExpr lce{LitConstKind::FLOAT, "0x1p4"};
        EXPECT_EQ(TypeKind::TYPE_IDEAL_FLOAT, lce.GetNumLitTypeKind());
    }
    // FLOAT negative hex prefix -> TYPE_IDEAL_FLOAT (negative sign skipped).
    {
        LitConstExpr lce{LitConstKind::FLOAT, "-0x1p4"};
        EXPECT_EQ(TypeKind::TYPE_IDEAL_FLOAT, lce.GetNumLitTypeKind());
    }
    // FLOAT, no suffix -> TYPE_IDEAL_FLOAT.
    {
        LitConstExpr lce{LitConstKind::FLOAT, "1.5"};
        EXPECT_EQ(TypeKind::TYPE_IDEAL_FLOAT, lce.GetNumLitTypeKind());
    }
    // FLOAT with valid f-suffix, e.g. f32 -> TYPE_FLOAT32.
    {
        LitConstExpr lce{LitConstKind::FLOAT, "1f32"};
        EXPECT_EQ(TypeKind::TYPE_FLOAT32, lce.GetNumLitTypeKind());
    }
    // FLOAT with unparseable suffix width -> TYPE_INVALID.
    {
        LitConstExpr lce{LitConstKind::FLOAT, "1fxx"};
        EXPECT_EQ(TypeKind::TYPE_INVALID, lce.GetNumLitTypeKind());
    }
    // None kind -> final TYPE_INVALID return.
    {
        LitConstExpr lce{LitConstKind::NONE, ""};
        EXPECT_EQ(TypeKind::TYPE_INVALID, lce.GetNumLitTypeKind());
    }
}

TEST(ToStringTest, ConstructedArrayExprWithTypeInfo)
{
    // ArrayExpr::ToString() type != nullptr branch: prints "type(...)".
    auto ae = MakeOwned<ArrayExpr>();
    auto ty = MakeOwned<PrimitiveType>();
    ty->str = "Int64";
    ae->type = std::move(ty);
    // Add an arg with a valid end position so NextSpan does not abort on span math.
    auto arg = MakeOwned<FuncArg>();
    auto e = MakeOwned<RefExpr>();
    e->ref = Reference("x");
    arg->expr = std::move(e);
    ae->args.push_back(std::move(arg));
    // commaPosVector shorter than args exercises the `i < commaPosVector.size()` false branch.
    EXPECT_NE(ae->ToString().find("Int64"), std::string::npos);
}

TEST(ToStringTest, ConstructedPointerExprArgBranch)
{
    // PointerExpr::ToString() arg != nullptr branch.
    auto pe = MakeOwned<PointerExpr>();
    auto arg = MakeOwned<FuncArg>();
    auto e = MakeOwned<RefExpr>();
    e->ref = Reference("p");
    arg->expr = std::move(e);
    pe->arg = std::move(arg);
    EXPECT_NE(pe->ToString().find("CPointer"), std::string::npos);
}

TEST(ToStringTest, ConstructedBlockGetLastExprOrDecl)
{
    // Empty body -> nullptr.
    auto emptyBlock = MakeOwned<Block>();
    EXPECT_EQ(nullptr, emptyBlock->GetLastExprOrDecl());
    // Non-empty -> last element.
    auto block = MakeOwned<Block>();
    auto e = MakeOwned<RefExpr>();
    e->ref = Reference("a");
    block->body.push_back(std::move(e));
    ASSERT_NE(nullptr, block->GetLastExprOrDecl());
    EXPECT_EQ(ASTKind::REF_EXPR, block->GetLastExprOrDecl()->astKind);
}

TEST(ToStringTest, ConstructedClearNoexceptBranches)
{
    // CallExpr::Clear(): baseFunc == nullptr -> early return (callKind/resolvedFunction
    // untouched); baseFunc != nullptr -> callKind reset to CALL_INVALID and
    // resolvedFunction nulled. Node::Clear() always clears IS_CHECK_VISITED, which is
    // the observable hook for the early-return branch. NOTE: callKind's default member
    // initializer is already CALL_INVALID (Node.h), so it must be set to a non-default
    // value before Clear() or the post-condition would be tautologically true.
    {
        auto ce = MakeOwned<CallExpr>();
        ce->EnableAttr(Attribute::IS_CHECK_VISITED);
        ASSERT_TRUE(ce->TestAttr(Attribute::IS_CHECK_VISITED));
        ce->Clear(); // baseFunc null -> early return path.
        EXPECT_FALSE(ce->TestAttr(Attribute::IS_CHECK_VISITED));
    }
    {
        auto ce = MakeOwned<CallExpr>();
        auto bf = MakeOwned<RefExpr>();
        bf->ref = Reference("f");
        ce->baseFunc = std::move(bf);
        ce->callKind = CallKind::CALL_DECLARED_FUNCTION; // non-default, proves reset
        auto resolvedFunc = MakeOwned<FuncDecl>(); // keep ownership across Clear()
        ce->resolvedFunction = resolvedFunc.get();
        ASSERT_NE(ce->resolvedFunction, nullptr);
        ASSERT_NE(ce->callKind, CallKind::CALL_INVALID);
        ce->Clear(); // baseFunc != nullptr path.
        EXPECT_EQ(CallKind::CALL_INVALID, ce->callKind);
        EXPECT_EQ(nullptr, ce->resolvedFunction);
    }
    // SubscriptExpr::Clear(): commaPos vector is cleared by the body; with a baseExpr
    // the recursive Clear() also runs on it.
    {
        auto se = MakeOwned<SubscriptExpr>();
        se->commaPos.push_back(Position(1, 2));
        se->commaPos.push_back(Position(3, 4));
        ASSERT_EQ(se->commaPos.size(), 2u);
        se->Clear(); // no baseExpr.
        EXPECT_TRUE(se->commaPos.empty());
    }
    {
        auto se = MakeOwned<SubscriptExpr>();
        auto be = MakeOwned<RefExpr>();
        be->ref = Reference("a");
        be->EnableAttr(Attribute::IS_CHECK_VISITED);
        se->baseExpr = std::move(be);
        se->Clear();
        EXPECT_FALSE(se->baseExpr->TestAttr(Attribute::IS_CHECK_VISITED));
    }
    // AssignExpr::Clear(): no own field is reset (RecoverToAssignExpr returns when
    // desugarExpr is null), so the observable effect is the common Node::Clear() reset.
    {
        auto ae = MakeOwned<AssignExpr>();
        ae->EnableAttr(Attribute::IS_CHECK_VISITED);
        ASSERT_TRUE(ae->TestAttr(Attribute::IS_CHECK_VISITED));
        ae->Clear();
        EXPECT_FALSE(ae->TestAttr(Attribute::IS_CHECK_VISITED));
    }
    // UnaryExpr::Clear(): with expr -> recursive Clear() on it; without -> Node::Clear only.
    {
        auto ue = MakeOwned<UnaryExpr>();
        ue->EnableAttr(Attribute::IS_CHECK_VISITED);
        ASSERT_TRUE(ue->TestAttr(Attribute::IS_CHECK_VISITED));
        ue->Clear();
        EXPECT_FALSE(ue->TestAttr(Attribute::IS_CHECK_VISITED));
    }
    {
        auto ue = MakeOwned<UnaryExpr>();
        auto e = MakeOwned<RefExpr>();
        e->ref = Reference("a");
        e->EnableAttr(Attribute::IS_CHECK_VISITED);
        ue->expr = std::move(e);
        ue->Clear();
        EXPECT_FALSE(ue->expr->TestAttr(Attribute::IS_CHECK_VISITED));
    }
    // BinaryExpr::Clear(): with left/right -> recursive Clear() on both; without -> Node::Clear only.
    {
        auto be = MakeOwned<BinaryExpr>();
        be->EnableAttr(Attribute::IS_CHECK_VISITED);
        ASSERT_TRUE(be->TestAttr(Attribute::IS_CHECK_VISITED));
        be->Clear();
        EXPECT_FALSE(be->TestAttr(Attribute::IS_CHECK_VISITED));
    }
    {
        auto be = MakeOwned<BinaryExpr>();
        auto l = MakeOwned<RefExpr>();
        l->ref = Reference("a");
        l->EnableAttr(Attribute::IS_CHECK_VISITED);
        be->leftExpr = std::move(l);
        auto r = MakeOwned<RefExpr>();
        r->ref = Reference("b");
        r->EnableAttr(Attribute::IS_CHECK_VISITED);
        be->rightExpr = std::move(r);
        be->Clear();
        EXPECT_FALSE(be->leftExpr->TestAttr(Attribute::IS_CHECK_VISITED));
        EXPECT_FALSE(be->rightExpr->TestAttr(Attribute::IS_CHECK_VISITED));
    }
    // ParenExpr::Clear(): with expr -> recursive Clear() on it; without -> Node::Clear only.
    {
        auto pe = MakeOwned<ParenExpr>();
        pe->EnableAttr(Attribute::IS_CHECK_VISITED);
        ASSERT_TRUE(pe->TestAttr(Attribute::IS_CHECK_VISITED));
        pe->Clear();
        EXPECT_FALSE(pe->TestAttr(Attribute::IS_CHECK_VISITED));
    }
    {
        auto pe = MakeOwned<ParenExpr>();
        auto e = MakeOwned<RefExpr>();
        e->ref = Reference("a");
        e->EnableAttr(Attribute::IS_CHECK_VISITED);
        pe->expr = std::move(e);
        pe->Clear();
        EXPECT_FALSE(pe->expr->TestAttr(Attribute::IS_CHECK_VISITED));
    }
}

TEST(ToStringTest, ConstructedSetGetTargetDispatch)
{
    // Node::SetTarget/GetTarget switch over astKind, covering the macro /
    // qualified / member-access / ref branches.
    auto target = MakeOwned<VarDecl>();
    target->identifier = "tgt";

    {
        auto rt = MakeOwned<RefType>();
        rt->SetTarget(target.get());
        EXPECT_EQ(target.get(), rt->GetTarget());
    }
    {
        auto re = MakeOwned<RefExpr>();
        re->SetTarget(target.get());
        EXPECT_EQ(target.get(), re->GetTarget());
    }
    {
        auto qt = MakeOwned<QualifiedType>();
        qt->SetTarget(target.get());
        EXPECT_EQ(target.get(), qt->GetTarget());
    }
    {
        auto ma = MakeOwned<MemberAccess>();
        ma->SetTarget(target.get());
        EXPECT_EQ(target.get(), ma->GetTarget());
    }
    {
        auto med = MakeOwned<MacroExpandDecl>();
        med->SetTarget(target.get());
        EXPECT_EQ(target.get(), med->GetTarget());
    }
    {
        auto mee = MakeOwned<MacroExpandExpr>();
        mee->SetTarget(target.get());
        EXPECT_EQ(target.get(), mee->GetTarget());
    }
    {
        auto mep = MakeOwned<MacroExpandParam>();
        mep->SetTarget(target.get());
        EXPECT_EQ(target.get(), mep->GetTarget());
    }
    // GetTargets() for REF_EXPR and MEMBER_ACCESS.
    {
        auto re = MakeOwned<RefExpr>();
        re->ref.targets.push_back(target.get());
        EXPECT_EQ(1u, re->GetTargets().size());
    }
    {
        auto ma = MakeOwned<MemberAccess>();
        auto funcTarget = MakeOwned<FuncDecl>();
        ma->targets.push_back(funcTarget.get());
        EXPECT_EQ(1u, ma->GetTargets().size());
    }
    // A node whose kind is not in the switch returns nullptr / empty.
    auto vd = MakeOwned<VarDecl>();
    EXPECT_EQ(nullptr, vd->GetTarget());
    EXPECT_TRUE(vd->GetTargets().empty());
}

TEST(ToStringTest, ConstructedMacroInvocationHelpers)
{
    // MacroInvocation::IsIfAvailable() + GetInvocation()/GetConstInvocation()
    // for the three MacroExpand node kinds, plus GetNamedArg/GetLambda/Decompose.
    auto mee = MakeOwned<MacroExpandExpr>();

    // Not IfAvailable by default; but GetConstInvocation() is non-null for a
    // MacroExpandExpr (it returns &invocation).
    EXPECT_FALSE(mee->GetInvocation()->IsIfAvailable());
    EXPECT_NE(nullptr, mee->GetConstInvocation());

    // Mark IfAvailable by setting fullName, and provide nodes[0] as a FuncArg.
    mee->invocation.macroCallDiagInfo.fullName = std::string(IF_AVAILABLE);
    EXPECT_TRUE(mee->GetInvocation()->IsIfAvailable());

    // GetNamedArg requires IsIfAvailable && !nodes.empty(); nodes[0] must be FuncArg.
    auto fa = MakeOwned<FuncArg>();
    fa->name = "p";
    mee->invocation.nodes.push_back(std::move(fa));
    ASSERT_NE(nullptr, mee->GetNamedArg());
    // GetLambda with index beyond nodes size -> nullptr (size() > i false).
    EXPECT_EQ(nullptr, mee->GetLambda(1));

    // Populate nodes[1] and nodes[2] with LambdaExpr for GetLambda / Decompose.
    mee->invocation.nodes.push_back(MakeOwned<LambdaExpr>(MakeOwned<FuncBody>()));
    mee->invocation.nodes.push_back(MakeOwned<LambdaExpr>(MakeOwned<FuncBody>()));
    ASSERT_NE(nullptr, mee->GetLambda(0));
    auto [a, l1, l2] = mee->Decompose();
    EXPECT_NE(a, nullptr);
    EXPECT_NE(l1, nullptr);
    EXPECT_NE(l2, nullptr);

    // MacroExpandDecl / MacroExpandParam GetInvocation paths.
    auto med = MakeOwned<MacroExpandDecl>();
    EXPECT_NE(nullptr, med->GetInvocation());
    EXPECT_NE(nullptr, med->GetConstInvocation());
    auto mep = MakeOwned<MacroExpandParam>();
    EXPECT_NE(nullptr, mep->GetInvocation());
    EXPECT_NE(nullptr, mep->GetConstInvocation());

    // A non-macro node returns nullptr from GetInvocation / GetConstInvocation.
    auto vd = MakeOwned<VarDecl>();
    EXPECT_EQ(nullptr, vd->GetInvocation());
    EXPECT_EQ(nullptr, vd->GetConstInvocation());
}

TEST(ToStringTest, ConstructedNameReferenceOuterArgSize)
{
    // OuterArgSize dispatches on callOrPattern: CallExpr -> args.size, EnumPattern -> patterns.size, else 0.
    // Keep the owned pointee alive for the duration of the call.
    {
        auto re = MakeOwned<RefExpr>();
        auto ce = MakeOwned<CallExpr>();
        re->callOrPattern = ce.get();
        EXPECT_EQ(0u, re->OuterArgSize());
    }
    {
        auto re = MakeOwned<RefExpr>();
        auto ce = MakeOwned<CallExpr>();
        ce->args.push_back(MakeOwned<FuncArg>());
        ce->args.push_back(MakeOwned<FuncArg>());
        re->callOrPattern = ce.get();
        EXPECT_EQ(2u, re->OuterArgSize());
    }
    {
        auto re = MakeOwned<RefExpr>();
        auto ep = MakeOwned<EnumPattern>();
        ep->patterns.push_back(MakeOwned<VarPattern>());
        re->callOrPattern = ep.get();
        EXPECT_EQ(1u, re->OuterArgSize());
    }
    {
        auto re = MakeOwned<RefExpr>();
        EXPECT_EQ(0u, re->OuterArgSize());
    }
}

TEST(ToStringTest, ConstructedDeclGetDesugarDecl)
{
    // GetDesugarDecl dispatches on MacroDecl / MainDecl / FuncParam.
    auto fd = MakeOwned<FuncDecl>();
    fd->identifier = "f";

    {
        auto md = MakeOwned<MacroDecl>();
        md->desugarDecl = MakeOwned<FuncDecl>();
        EXPECT_NE(nullptr, md->GetDesugarDecl());
    }
    {
        auto md = MakeOwned<MainDecl>();
        md->desugarDecl = MakeOwned<FuncDecl>();
        EXPECT_NE(nullptr, md->GetDesugarDecl());
    }
    {
        auto fp = MakeOwned<FuncParam>();
        fp->desugarDecl = MakeOwned<FuncDecl>();
        EXPECT_NE(nullptr, fp->GetDesugarDecl());
    }
    // A plain VarDecl returns nullptr.
    EXPECT_EQ(nullptr, MakeOwned<VarDecl>()->GetDesugarDecl());
}

TEST(ToStringTest, ConstructedDeclGetGenericsCount)
{
    // Fast path: no GENERIC attr -> 0.
    auto vd = MakeOwned<VarDecl>();
    EXPECT_EQ(0u, vd->GetGenericsCount());

    // GENERIC attr set but no generic node -> 0.
    auto fd = MakeOwned<FuncDecl>();
    fd->EnableAttr(Attribute::GENERIC);
    EXPECT_EQ(0u, fd->GetGenericsCount());

    // GENERIC attr set with a generic containing one type parameter -> 1.
    fd->funcBody = MakeOwned<FuncBody>();
    fd->funcBody->generic = MakeOwned<Generic>();
    fd->funcBody->generic->typeParameters.push_back(MakeOwned<GenericParamDecl>());
    EXPECT_EQ(1u, fd->GetGenericsCount());
}

TEST(ToStringTest, ConstructedImportContentHelpers)
{
    // GetPrefixPath: multiple prefix paths joined with DOT separators; the last
    // path is included too (the loop only skips the separator after the last).
    {
        ImportContent c;
        c.prefixPaths = {"a", "b", "c"};
        EXPECT_EQ("a.b.c", c.GetPrefixPath());
    }
    // GetPrefixPath with hasDoubleColon -> first sep is "::".
    {
        ImportContent c;
        c.prefixPaths = {"a", "b"};
        c.hasDoubleColon = true;
        EXPECT_EQ("a::b", c.GetPrefixPath());
    }
    // GetImportedPackageName: IMPORT_ALL skips trailing dot; IMPORT_SINGLE appends identifier.
    {
        ImportContent c;
        c.prefixPaths = {"a", "b"};
        c.kind = ImportKind::IMPORT_ALL;
        EXPECT_EQ("a.b", c.GetImportedPackageName());
    }
    {
        ImportContent c;
        c.prefixPaths = {"a"};
        c.identifier = "x";
        c.kind = ImportKind::IMPORT_SINGLE;
        EXPECT_EQ("a.x", c.GetImportedPackageName());
    }
    // GetImportedPackageNameWithIsDecl: isDecl true skips trailing ".identifier".
    {
        ImportContent c;
        c.prefixPaths = {"a"};
        c.identifier = "x";
        c.kind = ImportKind::IMPORT_SINGLE;
        c.isDecl = true;
        EXPECT_EQ("a", c.GetImportedPackageNameWithIsDecl());
    }
    // GetImportedPackageNameWithIsDecl IMPORT_ALL last-prefix skip.
    {
        ImportContent c;
        c.prefixPaths = {"a", "b"};
        c.kind = ImportKind::IMPORT_ALL;
        EXPECT_EQ("a.b", c.GetImportedPackageNameWithIsDecl());
    }
    // GetPossiblePackageNames: empty prefix -> {identifier}.
    {
        ImportContent c;
        c.identifier = "x";
        c.kind = ImportKind::IMPORT_SINGLE;
        auto names = c.GetPossiblePackageNames();
        ASSERT_EQ(1u, names.size());
        EXPECT_EQ("x", names[0]);
    }
    // GetPossiblePackageNames: IMPORT_ALL with prefix -> {prefix}.
    {
        ImportContent c;
        c.prefixPaths = {"a", "b"};
        c.kind = ImportKind::IMPORT_ALL;
        auto names = c.GetPossiblePackageNames();
        ASSERT_EQ(1u, names.size());
        EXPECT_EQ("a.b", names[0]);
    }
    // GetPossiblePackageNames: hasDoubleColon + single prefix -> {"::ident"}.
    {
        ImportContent c;
        c.prefixPaths = {"a"};
        c.identifier = "x";
        c.hasDoubleColon = true;
        c.kind = ImportKind::IMPORT_SINGLE;
        auto names = c.GetPossiblePackageNames();
        ASSERT_EQ(1u, names.size());
        EXPECT_EQ("a::x", names[0]);
    }
    // GetPossiblePackageNames: IMPORT_SINGLE with prefix -> two candidates.
    {
        ImportContent c;
        c.prefixPaths = {"a", "b"};
        c.identifier = "x";
        c.kind = ImportKind::IMPORT_SINGLE;
        auto names = c.GetPossiblePackageNames();
        ASSERT_EQ(2u, names.size());
    }
    // ToString: IMPORT_MULTI prints "{" ... "}" with item list.
    {
        ImportContent c;
        c.prefixPaths = {"a"};
        c.hasDoubleColon = true;
        c.kind = ImportKind::IMPORT_MULTI;
        ImportContent item;
        item.identifier = "x";
        item.kind = ImportKind::IMPORT_SINGLE;
        c.items.push_back(std::move(item));
        EXPECT_NE(c.ToString().find("{"), std::string::npos);
        EXPECT_NE(c.ToString().find("}"), std::string::npos);
    }
    // ToString: IMPORT_ALIAS appends " as alias".
    {
        ImportContent c;
        c.prefixPaths = {"a"};
        c.identifier = "x";
        c.aliasName = "y";
        c.kind = ImportKind::IMPORT_ALIAS;
        EXPECT_EQ("a.x as y", c.ToString());
    }
}

TEST(ToStringTest, ConstructedFeatureIdAndTokenPart)
{
    // FeatureId::ToString joins identifiers with "." per dotPos.
    auto fid = MakeOwned<FeatureId>();
    Identifier id1;
    id1 = "a";
    Identifier id2;
    id2 = "b";
    fid->identifiers.push_back(id1);
    fid->identifiers.push_back(id2);
    fid->dotPoses.push_back(Position{0, 1, 2});
    EXPECT_EQ("a.b", fid->ToString());

    // TokenPart::ToString concatenates token values.
    auto tp = MakeOwned<TokenPart>();
    tp->tokens.emplace_back(TokenKind::IDENTIFIER, "foo");
    tp->tokens.emplace_back(TokenKind::IDENTIFIER, "bar");
    EXPECT_EQ("foobar", tp->ToString());
}

TEST(ToStringTest, ConstructedResumeThrowingBranch)
{
    // ResumeExpr::ToString "throwing" branch (withExpr optional).
    auto re = MakeOwned<ResumeExpr>();
    re->throwingExpr = MakeOwned<RefExpr>();
    EXPECT_NE(re->ToString().find("throwing"), std::string::npos);
    // Both with and throwing set.
    re->withExpr = MakeOwned<RefExpr>();
    EXPECT_NE(re->ToString().find("resume"), std::string::npos);
}

TEST(ToStringTest, ConstructedMatchExprWithCaseOther)
{
    // MatchExpr::ToString with non-empty matchCases / matchCaseOthers to cover
    // the trailing-newline branch.
    auto me = MakeOwned<MatchExpr>();
    me->selector = MakeOwned<RefExpr>();
    auto mc = MakeOwned<MatchCase>();
    mc->exprOrDecls = MakeOwned<Block>();
    me->matchCases.push_back(std::move(mc));
    auto mco = MakeOwned<MatchCaseOther>();
    mco->matchExpr = MakeOwned<RefExpr>();
    mco->exprOrDecls = MakeOwned<Block>();
    me->matchCaseOthers.push_back(std::move(mco));
    EXPECT_NE(me->ToString().find("match"), std::string::npos);
    EXPECT_NE(me->ToString().find("}"), std::string::npos);
}

TEST(ToStringTest, ConstructedTryExprResourceAndHandler)
{
    // TryExpr::ToString resourceSpec + catch + handler + finally branches.
    auto te = MakeOwned<TryExpr>();
    te->tryBlock = MakeOwned<Block>();
    // resourceSpec non-empty -> "(...)" prefix.
    te->resourceSpec.push_back(MakeOwned<VarDecl>());
    // catch block with pattern.
    auto catchBlock = MakeOwned<Block>();
    te->catchBlocks.push_back(std::move(catchBlock));
    te->catchPatterns.push_back(MakeOwned<WildcardPattern>());
    // handler with commandPattern and block.
    Handler h;
    h.commandPattern = MakeOwned<WildcardPattern>();
    h.block = MakeOwned<Block>();
    te->handlers.push_back(std::move(h));
    // finally block.
    te->finallyBlock = MakeOwned<Block>();
    EXPECT_NE(te->ToString().find("try"), std::string::npos);
    EXPECT_NE(te->ToString().find("catch"), std::string::npos);
    EXPECT_NE(te->ToString().find("handle"), std::string::npos);
    EXPECT_NE(te->ToString().find("finally"), std::string::npos);
}

TEST(ToStringTest, ConstructedSpawnExprArgBranch)
{
    // SpawnExpr::ToString arg != nullptr branch.
    auto se = MakeOwned<SpawnExpr>();
    se->task = MakeOwned<RefExpr>();
    auto arg = MakeOwned<RefExpr>();
    arg->ref = Reference("ctx");
    se->arg = std::move(arg);
    EXPECT_NE(se->ToString().find("spawn"), std::string::npos);
}

TEST(ToStringTest, ConstructedDeclToStringModifierBranches)
{
    // Modifier::ToString isExplicit false -> "".
    {
        Modifier mod{TokenKind::PUBLIC, Position{0, 1, 1}};
        EXPECT_EQ("public", mod.ToString());
    }
    // FuncDecl::ToString with modifiers and funcBody (modifier non-empty branch).
    {
        auto fd = MakeOwned<FuncDecl>();
        fd->identifier = "f";
        fd->modifiers.insert(Modifier{TokenKind::PUBLIC, Position{0, 1, 1}});
        fd->funcBody = MakeOwned<FuncBody>();
        EXPECT_NE(fd->ToString().find("func f"), std::string::npos);
        EXPECT_NE(fd->ToString().find("public"), std::string::npos);
    }
    // MacroDecl::ToString with modifiers + funcBody.
    {
        auto md = MakeOwned<MacroDecl>();
        md->identifier = "dm";
        md->modifiers.insert(Modifier{TokenKind::PUBLIC, Position{0, 1, 1}});
        md->funcBody = MakeOwned<FuncBody>();
        EXPECT_NE(md->ToString().find("macro dm"), std::string::npos);
    }
    // MainDecl::ToString with modifiers + funcBody.
    {
        auto md = MakeOwned<MainDecl>();
        md->modifiers.insert(Modifier{TokenKind::PUBLIC, Position{0, 1, 1}});
        md->funcBody = MakeOwned<FuncBody>();
        EXPECT_NE(md->ToString().find("main"), std::string::npos);
    }
    // TypeAliasDecl::ToString with modifiers + generic.
    {
        auto ta = MakeOwned<TypeAliasDecl>();
        ta->identifier = "T";
        ta->modifiers.insert(Modifier{TokenKind::PUBLIC, Position{0, 1, 1}});
        ta->generic = MakeOwned<Generic>();
        ta->generic->typeParameters.push_back(MakeOwned<GenericParamDecl>());
        ta->type = MakeOwned<RefType>();
        EXPECT_NE(ta->ToString().find("type T"), std::string::npos);
    }
    // ImportSpec::ToString with a modifier.
    {
        auto is = MakeOwned<ImportSpec>();
        is->modifier = MakeOwned<Modifier>(TokenKind::PUBLIC, Position{0, 1, 1});
        is->content.prefixPaths = {"a"};
        is->content.identifier = "x";
        is->content.kind = ImportKind::IMPORT_SINGLE;
        EXPECT_NE(is->ToString().find("import"), std::string::npos);
        EXPECT_NE(is->ToString().find("public"), std::string::npos);
    }
}

TEST(ToStringTest, ConstructedGenericAndFuncBodyBranches)
{
    // Generic::ToString empty typeParameters -> "".
    EXPECT_EQ("", MakeOwned<Generic>()->ToString());
    // Generic::ToString non-empty -> "<...>".
    {
        auto g = MakeOwned<Generic>();
        g->typeParameters.push_back(MakeOwned<GenericParamDecl>());
        EXPECT_EQ('<', g->ToString().front());
    }
    // GenericConstraint::ToString with upperBounds -> "<: ..." branch.
    {
        auto c = MakeOwned<GenericConstraint>();
        auto t = MakeOwned<RefType>();
        t->ref = Reference("T");
        c->type = std::move(t);
        c->upperBounds.push_back(MakeOwned<RefType>());
        EXPECT_NE(c->ToString().find("<:"), std::string::npos);
    }
    // FuncParam::ToString isNamedParam -> "!" branch.
    {
        auto fp = MakeOwned<FuncParam>();
        fp->identifier = "p";
        fp->isNamedParam = true;
        fp->type = MakeOwned<RefType>();
        EXPECT_NE(fp->ToString().find("!"), std::string::npos);
    }
    // FuncBody::ToString with generic, paramLists, retType (colonPos), where-clause, body.
    {
        auto fb = MakeOwned<FuncBody>();
        fb->generic = MakeOwned<Generic>();
        fb->generic->typeParameters.push_back(MakeOwned<GenericParamDecl>());
        auto pl = MakeOwned<FuncParamList>();
        pl->params.push_back(MakeOwned<FuncParam>());
        fb->paramLists.push_back(std::move(pl));
        fb->retType = MakeOwned<RefType>();
        fb->colonPos = Position{0, 1, 2};
        fb->body = MakeOwned<Block>();
        // add a generic constraint (with a type) for the "where" branch
        auto gc = MakeOwned<GenericConstraint>();
        gc->type = MakeOwned<RefType>();
        fb->generic->genericConstraints.push_back(std::move(gc));
        EXPECT_NE(fb->ToString().find("where"), std::string::npos);
    }
    // FuncBody::ToString retType via doubleArrowPos (=> ) branch.
    {
        auto fb = MakeOwned<FuncBody>();
        fb->retType = MakeOwned<RefType>();
        fb->doubleArrowPos = Position{0, 1, 2};
        EXPECT_NE(fb->ToString().find("=>"), std::string::npos);
    }
}

TEST(ToStringTest, ConstructedLambdaAndTrailingClosure)
{
    // LambdaExpr::ToString with a real funcBody containing a param list + body.
    auto lambda = MakeOwned<LambdaExpr>(MakeOwned<FuncBody>());
    lambda->funcBody->paramLists.push_back(MakeOwned<FuncParamList>());
    lambda->funcBody->body = MakeOwned<Block>();
    EXPECT_NE(lambda->ToString().find("{"), std::string::npos);

    // TrailingClosureExpr::ToString lambda branch (expr + lambda).
    auto tc = MakeOwned<TrailingClosureExpr>();
    auto base = MakeOwned<RefExpr>();
    base->ref = Reference("base");
    tc->expr = std::move(base);
    tc->lambda = MakeOwned<LambdaExpr>(MakeOwned<FuncBody>());
    EXPECT_NE(tc->ToString().find("base"), std::string::npos);
}

TEST(ToStringTest, ConstructedMacroExpandExprToStringFull)
{
    // MacroExpandExpr::ToString with attrs, parenthesis args, and nodes.
    auto mee = MakeOwned<MacroExpandExpr>();
    mee->identifier = "Moo";
    mee->invocation.hasParenthesis = true;
    mee->invocation.args.emplace_back(TokenKind::IDENTIFIER, "arg1");
    mee->invocation.args.emplace_back(TokenKind::IDENTIFIER, "arg2");
    mee->invocation.attrs.emplace_back(TokenKind::IDENTIFIER, "attr1");
    mee->invocation.nodes.push_back(MakeOwned<RefExpr>());
    std::string out = mee->ToString();
    EXPECT_NE(out.find("@Moo"), std::string::npos);
    EXPECT_NE(out.find("["), std::string::npos); // attrs
    EXPECT_NE(out.find("("), std::string::npos); // args
    EXPECT_NE(out.find("{"), std::string::npos); // nodes
}

TEST(ToStringTest, ConstructedVarPatternAndTypePatternBranches)
{
    // VarPattern::ToString varDecl null -> "".
    EXPECT_EQ("", MakeOwned<VarPattern>()->ToString());
    // VarPattern::ToString varDecl set -> identifier.
    {
        auto vp = MakeOwned<VarPattern>();
        vp->varDecl = MakeOwned<VarDecl>();
        vp->varDecl->identifier = "x";
        EXPECT_EQ("x", vp->ToString());
    }
    // VarOrEnumPattern::ToString pattern set -> pattern.ToString().
    {
        SrcIdentifier id;
        id = "v";
        auto voe = MakeOwned<VarOrEnumPattern>(id);
        voe->pattern = MakeOwned<VarPattern>();
        EXPECT_TRUE(voe->ToString().empty()); // empty VarPattern -> ""
    }
    // TypePattern::ToString with a named pattern -> "name: type".
    {
        auto tp = MakeOwned<TypePattern>();
        auto vp = MakeOwned<VarPattern>();
        vp->varDecl = MakeOwned<VarDecl>();
        vp->varDecl->identifier = "x";
        tp->pattern = std::move(vp);
        tp->type = MakeOwned<RefType>();
        EXPECT_NE(tp->ToString().find(":"), std::string::npos);
    }
    // ExceptTypePattern / CommandTypePattern: empty ret with types -> " | ".
    {
        auto etp = MakeOwned<ExceptTypePattern>();
        etp->pattern = MakeOwned<VarPattern>();
        etp->types.push_back(MakeOwned<RefType>());
        etp->types.push_back(MakeOwned<RefType>());
        EXPECT_NE(etp->ToString().find("|"), std::string::npos);
    }
}

TEST(ToStringTest, ConstructedPackageFileToString)
{
    // Package::ToString with a file that has a package + decls.
    auto pkg = MakeOwned<Package>("my_pkg");
    auto file = MakeOwned<File>();
    file->package = MakeOwned<PackageSpec>();
    file->package->prefixPaths = {"my_pkg"};
    file->package->packageName = "my_pkg";
    auto vd = MakeOwned<VarDecl>();
    vd->identifier = "x";
    file->decls.push_back(std::move(vd));
    pkg->files.push_back(std::move(file));
    EXPECT_NE(pkg->ToString().find("my_pkg"), std::string::npos);
}

TEST(ToStringTest, ConstructedPrimitiveTypeAndQualifiedTypeBranches)
{
    // PrimitiveType::ToString str empty -> Ty::KindName(kind).
    {
        auto pt = MakeOwned<PrimitiveType>();
        pt->kind = TypeKind::TYPE_INT64;
        EXPECT_FALSE(pt->ToString().empty());
    }
    // QualifiedType::ToString with typeArguments -> "<...>" branch.
    {
        auto qt = MakeOwned<QualifiedType>();
        qt->baseType = MakeOwned<RefType>();
        qt->field = "field";
        qt->typeArguments.push_back(MakeOwned<RefType>());
        qt->typeArguments.push_back(MakeOwned<RefType>());
        EXPECT_NE(qt->ToString().find("<"), std::string::npos);
    }
    // OptionType::ToString questNum > 0 path (vs questVector).
    {
        auto ot = MakeOwned<OptionType>();
        ot->questNum = 2;
        ot->componentType = MakeOwned<RefType>();
        EXPECT_EQ("??", ot->ToString().substr(0, 2));
    }
    // VArrayType::ToString constantType set branch (constantExpr required).
    {
        auto va = MakeOwned<VArrayType>();
        va->typeArgument = MakeOwned<RefType>();
        auto ct = MakeOwned<ConstantType>();
        ct->constantExpr = MakeOwned<LitConstExpr>(LitConstKind::INTEGER, "3");
        va->constantType = std::move(ct);
        EXPECT_NE(va->ToString().find("VArray"), std::string::npos);
    }
    // FuncType::ToString retType set branch.
    {
        auto ft = MakeOwned<FuncType>();
        ft->paramTypes.push_back(MakeOwned<RefType>());
        ft->retType = MakeOwned<RefType>();
        EXPECT_NE(ft->ToString().find("->"), std::string::npos);
    }
}

// ---------------------------------------------------------------------------
// Second batch: cover the Decl predicate helpers (IsExportedDecl / IsOpen /
// IsConst / IsBuiltIn / IsCommonOrSpecific / ...), Node::ShouldDiagnose,
// Node::IsSamePackage, ClassLikeDecl java mirrors, Decl::GetMemberDeclPtrs,
// Decl::GetGeneric, Node::GetFullPackageName, and the position helpers
// (GetFieldPos / GetIdentifierPos / GetIdentifierPos) that delegate to
// GetMacroCallPos on a plain (non-macro) node.
// ---------------------------------------------------------------------------

TEST(ToStringTest, ConstructedShouldDiagnoseBranches)
{
    // Default node -> true.
    EXPECT_TRUE(MakeOwned<VarDecl>()->ShouldDiagnose());

    // !allowCompilerAdd + COMPILER_ADD attr + not cloned -> false.
    {
        auto vd = MakeOwned<VarDecl>();
        vd->EnableAttr(Attribute::COMPILER_ADD);
        EXPECT_FALSE(vd->ShouldDiagnose());
        // IS_CLONED_SOURCE_CODE re-enables diagnosis.
        vd->EnableAttr(Attribute::IS_CLONED_SOURCE_CODE);
        EXPECT_TRUE(vd->ShouldDiagnose());
    }
    // allowCompilerAdd=true + COMPILER_ADD + begin.IsZero() -> false.
    {
        auto vd = MakeOwned<VarDecl>();
        vd->EnableAttr(Attribute::COMPILER_ADD);
        // begin is zero by default.
        EXPECT_FALSE(vd->ShouldDiagnose(true));
    }
    // MACRO_INVOKE_FUNC / MACRO_INVOKE_BODY attrs -> false.
    {
        auto vd = MakeOwned<VarDecl>();
        vd->EnableAttr(Attribute::MACRO_INVOKE_FUNC);
        EXPECT_FALSE(vd->ShouldDiagnose());
    }
    {
        auto vd = MakeOwned<VarDecl>();
        vd->EnableAttr(Attribute::MACRO_INVOKE_BODY);
        EXPECT_FALSE(vd->ShouldDiagnose());
    }
    // Expr with sourceExpr set -> false.
    {
        auto re = MakeOwned<RefExpr>();
        re->sourceExpr = MakeOwned<RefExpr>();
        EXPECT_FALSE(re->ShouldDiagnose());
    }
}

TEST(ToStringTest, ConstructedIsSamePackage)
{
    // No curFile on either -> true.
    {
        auto a = MakeOwned<VarDecl>();
        auto b = MakeOwned<VarDecl>();
        EXPECT_TRUE(a->IsSamePackage(*b));
    }
    // curFile set but no curPackage -> true.
    {
        auto a = MakeOwned<VarDecl>();
        a->curFile = MakeOwned<File>().get();
        auto fa = MakeOwned<File>(); // keep alive
        a->curFile = fa.get();
        auto b = MakeOwned<VarDecl>();
        EXPECT_TRUE(a->IsSamePackage(*b));
    }
    // Both curFile with same curPackage pointer -> true.
    {
        auto pkg = MakeOwned<Package>("p");
        auto fa = MakeOwned<File>();
        fa->curPackage = pkg.get();
        auto fb = MakeOwned<File>();
        fb->curPackage = pkg.get();
        auto a = MakeOwned<VarDecl>();
        a->curFile = fa.get();
        auto b = MakeOwned<VarDecl>();
        b->curFile = fb.get();
        EXPECT_TRUE(a->IsSamePackage(*b));
    }
    // Different curPackage pointers -> false.
    {
        auto pkg1 = MakeOwned<Package>("p1");
        auto pkg2 = MakeOwned<Package>("p2");
        auto fa = MakeOwned<File>();
        fa->curPackage = pkg1.get();
        auto fb = MakeOwned<File>();
        fb->curPackage = pkg2.get();
        auto a = MakeOwned<VarDecl>();
        a->curFile = fa.get();
        auto b = MakeOwned<VarDecl>();
        b->curFile = fb.get();
        EXPECT_FALSE(a->IsSamePackage(*b));
    }
}

TEST(ToStringTest, ConstructedDeclPredicates)
{
    // IsBuiltIn: only BUILTIN_DECL is true.
    EXPECT_TRUE(MakeOwned<BuiltInDecl>(BuiltInType::ARRAY)->IsBuiltIn());
    EXPECT_FALSE(MakeOwned<VarDecl>()->IsBuiltIn());

    // IsConst dispatches on VarDeclAbstract / FuncDecl / PrimaryCtorDecl.
    {
        auto vd = MakeOwned<VarDecl>();
        vd->isConst = true;
        EXPECT_TRUE(vd->IsConst());
    }
    {
        auto fd = MakeOwned<FuncDecl>();
        fd->isConst = true;
        EXPECT_TRUE(fd->IsConst());
    }
    {
        auto pcd = MakeOwned<PrimaryCtorDecl>();
        pcd->isConst = true;
        EXPECT_TRUE(pcd->IsConst());
    }
    // A decl that is none of those -> false.
    EXPECT_FALSE(MakeOwned<TypeAliasDecl>()->IsConst());

    // IsCommonOrSpecific / IsCommonMatchedWithSpecific.
    {
        auto vd = MakeOwned<VarDecl>();
        vd->EnableAttr(Attribute::COMMON);
        EXPECT_TRUE(vd->IsCommonOrSpecific());
        EXPECT_FALSE(vd->IsCommonMatchedWithSpecific()); // no specificImplementation
    }
    {
        auto vd = MakeOwned<VarDecl>();
        vd->EnableAttr(Attribute::SPECIFIC);
        EXPECT_TRUE(vd->IsCommonOrSpecific());
    }
    {
        auto vd = MakeOwned<VarDecl>();
        vd->EnableAttr(Attribute::COMMON);
        vd->specificImplementation = MakeOwned<VarDecl>();
        EXPECT_TRUE(vd->IsCommonMatchedWithSpecific());
    }
}

TEST(ToStringTest, ConstructedIsExportedDeclBranches)
{
    // PUBLIC/PROTECTED -> true.
    EXPECT_TRUE([]() {
        auto vd = MakeOwned<VarDecl>();
        vd->EnableAttr(Attribute::PUBLIC);
        return vd->IsExportedDecl();
    }());
    EXPECT_TRUE([]() {
        auto vd = MakeOwned<VarDecl>();
        vd->EnableAttr(Attribute::PROTECTED);
        return vd->IsExportedDecl();
    }());
    // PRIVATE -> false.
    EXPECT_FALSE([]() {
        auto vd = MakeOwned<VarDecl>();
        vd->EnableAttr(Attribute::PRIVATE);
        return vd->IsExportedDecl();
    }());
    // INTERNAL with curFile+curPackage+noSubPkg=false -> true.
    {
        auto vd = MakeOwned<VarDecl>();
        vd->EnableAttr(Attribute::INTERNAL);
        auto file = MakeOwned<File>();
        auto pkg = MakeOwned<Package>("p");
        pkg->noSubPkg = false;
        file->curPackage = pkg.get();
        vd->curFile = file.get();
        EXPECT_TRUE(vd->IsExportedDecl());
    }
    // INTERNAL with noSubPkg=true -> false.
    {
        auto vd = MakeOwned<VarDecl>();
        vd->EnableAttr(Attribute::INTERNAL);
        auto file = MakeOwned<File>();
        auto pkg = MakeOwned<Package>("p");
        pkg->noSubPkg = true;
        file->curPackage = pkg.get();
        vd->curFile = file.get();
        EXPECT_FALSE(vd->IsExportedDecl());
    }
    // INTERNAL with no curFile -> true.
    {
        auto vd = MakeOwned<VarDecl>();
        vd->EnableAttr(Attribute::INTERNAL);
        EXPECT_TRUE(vd->IsExportedDecl());
    }
    // No access modifier -> true (default exported).
    EXPECT_TRUE(MakeOwned<VarDecl>()->IsExportedDecl());
}

TEST(ToStringTest, ConstructedClassLikeJavaMirrors)
{
    auto cd = MakeOwned<ClassDecl>();
    cd->MarkAsJavaForeign();
    EXPECT_TRUE(cd->TestAttr(Attribute::JAVA_FOREIGN));
    cd->MarkAsJavaExport();
    EXPECT_TRUE(cd->TestAttr(Attribute::JAVA_EXPORT));
}

TEST(ToStringTest, ConstructedGetMemberDeclPtrs)
{
    // ClassDecl with body.
    {
        auto cd = MakeOwned<ClassDecl>();
        cd->body = MakeOwned<ClassBody>();
        cd->body->decls.push_back(MakeOwned<VarDecl>());
        cd->body->decls.push_back(MakeOwned<VarDecl>());
        EXPECT_EQ(2u, cd->GetMemberDeclPtrs().size());
    }
    // InterfaceDecl with body.
    {
        auto id = MakeOwned<InterfaceDecl>();
        id->body = MakeOwned<InterfaceBody>();
        id->body->decls.push_back(MakeOwned<FuncDecl>());
        EXPECT_EQ(1u, id->GetMemberDeclPtrs().size());
    }
    // StructDecl with body.
    {
        auto sd = MakeOwned<StructDecl>();
        sd->body = MakeOwned<StructBody>();
        sd->body->decls.push_back(MakeOwned<VarDecl>());
        EXPECT_EQ(1u, sd->GetMemberDeclPtrs().size());
    }
    // EnumDecl with constructors + members.
    {
        auto ed = MakeOwned<EnumDecl>();
        ed->constructors.push_back(MakeOwned<VarDecl>());
        ed->members.push_back(MakeOwned<FuncDecl>());
        EXPECT_EQ(2u, ed->GetMemberDeclPtrs().size());
    }
    // ExtendDecl with members.
    {
        auto exd = MakeOwned<ExtendDecl>();
        exd->members.push_back(MakeOwned<FuncDecl>());
        EXPECT_EQ(1u, exd->GetMemberDeclPtrs().size());
    }
    // A decl with no body case (e.g. TypeAliasDecl) -> empty.
    EXPECT_TRUE(MakeOwned<TypeAliasDecl>()->GetMemberDeclPtrs().empty());
}

TEST(ToStringTest, ConstructedGetGenericDispatch)
{
    // FuncDecl with funcBody->generic.
    {
        auto fd = MakeOwned<FuncDecl>();
        fd->funcBody = MakeOwned<FuncBody>();
        fd->funcBody->generic = MakeOwned<Generic>();
        fd->funcBody->generic->typeParameters.push_back(MakeOwned<GenericParamDecl>());
        EXPECT_NE(nullptr, fd->GetGeneric());
    }
    // FuncDecl funcBody no generic, parentEnum + ENUM_CONSTRUCTOR -> parentEnum generic.
    {
        auto fd = MakeOwned<FuncDecl>();
        fd->funcBody = MakeOwned<FuncBody>();
        fd->EnableAttr(Attribute::ENUM_CONSTRUCTOR);
        auto ed = MakeOwned<EnumDecl>();
        ed->generic = MakeOwned<Generic>();
        fd->funcBody->parentEnum = ed.get();
        EXPECT_NE(nullptr, fd->GetGeneric());
    }
    // FuncDecl funcBody no generic, no parentEnum -> nullptr.
    {
        auto fd = MakeOwned<FuncDecl>();
        fd->funcBody = MakeOwned<FuncBody>();
        EXPECT_EQ(nullptr, fd->GetGeneric());
    }
    // FuncDecl no funcBody -> generic.get() (none).
    {
        auto fd = MakeOwned<FuncDecl>();
        EXPECT_EQ(nullptr, fd->GetGeneric());
    }
    // VarDecl with outerDecl being EnumDecl -> outerDecl generic.
    {
        auto vd = MakeOwned<VarDecl>();
        auto ed = MakeOwned<EnumDecl>();
        ed->generic = MakeOwned<Generic>();
        vd->outerDecl = ed.get();
        EXPECT_NE(nullptr, vd->GetGeneric());
    }
    // VarDecl no outerDecl -> own generic.get().
    {
        auto vd = MakeOwned<VarDecl>();
        vd->generic = MakeOwned<Generic>();
        EXPECT_NE(nullptr, vd->GetGeneric());
    }
}

TEST(ToStringTest, ConstructedGetFullPackageName)
{
    // Decl with fullPackageName set -> that name.
    {
        auto vd = MakeOwned<VarDecl>();
        vd->fullPackageName = "my.pkg";
        EXPECT_EQ("my.pkg", vd->GetFullPackageName());
    }
    // No fullPackageName, curFile+curPackage -> package full name.
    {
        auto vd = MakeOwned<VarDecl>();
        auto file = MakeOwned<File>();
        auto pkg = MakeOwned<Package>("file.pkg");
        file->curPackage = pkg.get();
        vd->curFile = file.get();
        EXPECT_EQ("file.pkg", vd->GetFullPackageName());
    }
    // No decl name, no curFile -> empty package name.
    EXPECT_EQ("", MakeOwned<RefExpr>()->GetFullPackageName());
}

TEST(ToStringTest, ConstructedPositionHelpers)
{
    // On a plain node (no curMacroCall, not a macro node), GetMacroCallPos
    // returns the origin position; the position helpers delegate to it.
    {
        auto ma = MakeOwned<MemberAccess>();
        ma->field = "fld";
        ma->field.SetPos(Position{0, 1, 5}, Position{0, 1, 8});
        EXPECT_EQ(ma->field.Begin(), ma->GetFieldPos());
    }
    {
        auto re = MakeOwned<RefExpr>();
        re->ref = Reference("x");
        re->ref.identifier.SetPos(Position{0, 1, 1}, Position{0, 1, 2});
        EXPECT_EQ(re->ref.identifier.Begin(), re->GetIdentifierPos());
    }
    {
        auto qt = MakeOwned<QualifiedType>();
        qt->field = "fld";
        qt->field.SetPos(Position{0, 1, 3}, Position{0, 1, 6});
        EXPECT_EQ(qt->field.Begin(), qt->GetFieldPos());
    }
    {
        auto vd = MakeOwned<VarDecl>();
        vd->identifier = "x";
        vd->identifier.SetPos(Position{0, 1, 1}, Position{0, 1, 2});
        EXPECT_EQ(vd->identifier.Begin(), vd->GetIdentifierPos());
    }
    // GetBegin / GetEnd on a plain node return begin / end.
    {
        auto vd = MakeOwned<VarDecl>();
        vd->begin = Position{0, 1, 1};
        vd->end = Position{0, 1, 5};
        EXPECT_EQ((Position{0, 1, 1}), vd->GetBegin());
        EXPECT_EQ((Position{0, 1, 5}), vd->GetEnd());
    }
}

TEST(ToStringTest, ConstructedRefTypeIsGenericThisType)
{
    // ref.target is a ClassDecl with generic + typeParameters, identifier "This" -> true.
    {
        auto rt = MakeOwned<RefType>();
        rt->ref = Reference("This");
        auto cd = MakeOwned<ClassDecl>();
        cd->generic = MakeOwned<Generic>();
        cd->generic->typeParameters.push_back(MakeOwned<GenericParamDecl>());
        rt->ref.target = cd.get();
        EXPECT_TRUE(rt->IsGenericThisType());
    }
    // identifier not "This" -> false.
    {
        auto rt = MakeOwned<RefType>();
        rt->ref = Reference("Other");
        auto cd = MakeOwned<ClassDecl>();
        cd->generic = MakeOwned<Generic>();
        cd->generic->typeParameters.push_back(MakeOwned<GenericParamDecl>());
        rt->ref.target = cd.get();
        EXPECT_FALSE(rt->IsGenericThisType());
    }
    // No target -> false.
    EXPECT_FALSE(MakeOwned<RefType>()->IsGenericThisType());
}

TEST(ToStringTest, ConstructedImportContentDoubleColonBranches)
{
    // GetImportedPackageName with hasDoubleColon and single prefix -> "::ident".
    {
        ImportContent c;
        c.prefixPaths = {"a"};
        c.hasDoubleColon = true;
        c.identifier = "x";
        c.kind = ImportKind::IMPORT_SINGLE;
        EXPECT_EQ("a::x", c.GetImportedPackageName());
    }
    // GetImportedPackageNameWithIsDecl hasDoubleColon single prefix, isDecl=false.
    {
        ImportContent c;
        c.prefixPaths = {"a"};
        c.hasDoubleColon = true;
        c.identifier = "x";
        c.kind = ImportKind::IMPORT_SINGLE;
        c.isDecl = false;
        EXPECT_EQ("a::.x", c.GetImportedPackageNameWithIsDecl());
    }
    // GetPrefixPath single prefix (no separator added).
    {
        ImportContent c;
        c.prefixPaths = {"a"};
        EXPECT_EQ("a", c.GetPrefixPath());
    }
    // ToString single import with hasDoubleColon.
    {
        ImportContent c;
        c.prefixPaths = {"a"};
        c.hasDoubleColon = true;
        c.identifier = "x";
        c.kind = ImportKind::IMPORT_SINGLE;
        EXPECT_EQ("a::x", c.ToString());
    }
    // PackageSpec::GetPackageName with hasDoubleColon.
    {
        PackageSpec ps;
        ps.prefixPaths = {"a", "b"};
        ps.packageName = "c";
        ps.hasDoubleColon = true;
        EXPECT_EQ("a::b.c", ps.GetPackageName());
    }
}

// Batch 3: cover additional reachable branches that batch 1/2 left open.

TEST(ToStringTest, ConstructedCallExprBaseFuncNullBranch)
{
    // CallExpr::ToString() baseFunc == nullptr branch: prints "(" ... ")".
    auto ce = MakeOwned<CallExpr>();
    auto arg = MakeOwned<FuncArg>();
    auto e = MakeOwned<RefExpr>();
    e->ref = Reference("x");
    arg->expr = std::move(e);
    ce->args.push_back(std::move(arg));
    // baseFunc stays null -> the else branch emits the bare "(".
    EXPECT_NE(ce->ToString().find("("), std::string::npos);
}

TEST(ToStringTest, ConstructedArrayExprCommaBranch)
{
    // ArrayExpr::ToString() with two args and matching commaPosVector exercises
    // the `i + 1 < args.size() && i < commaPosVector.size()` true branch.
    auto ae = MakeOwned<ArrayExpr>();
    auto arg1 = MakeOwned<FuncArg>();
    auto e1 = MakeOwned<RefExpr>();
    e1->ref = Reference("a");
    arg1->expr = std::move(e1);
    arg1->end = Position{0, 1, 2};
    auto arg2 = MakeOwned<FuncArg>();
    auto e2 = MakeOwned<RefExpr>();
    e2->ref = Reference("b");
    arg2->expr = std::move(e2);
    arg2->end = Position{0, 1, 4};
    ae->args.push_back(std::move(arg1));
    ae->args.push_back(std::move(arg2));
    ae->commaPosVector.push_back(Position{0, 1, 3});
    auto out = ae->ToString();
    EXPECT_NE(out.find("a"), std::string::npos);
    EXPECT_NE(out.find("b"), std::string::npos);
}

TEST(ToStringTest, ConstructedPointerExprPointeeBranch)
{
    // PointerExpr::ToString() pointeeTy != nullptr && !typeArgs.empty() branch:
    // emits "CPointer<typeArgs[0]>(".
    auto pe = MakeOwned<PointerExpr>();
    auto ty = MakeOwned<PrimitiveTy>(TypeKind::TYPE_INT64);
    auto pointee = MakeOwned<PrimitiveTy>(TypeKind::TYPE_FLOAT64);
    ty->typeArgs.push_back(DataTy{pointee});
    pe->SetTy(DataTy{ty});
    auto out = pe->ToString();
    EXPECT_NE(out.find("CPointer"), std::string::npos);
}

TEST(ToStringTest, ConstructedReturnExprNullExpr)
{
    // ReturnExpr::ToString() expr == nullptr branch -> "return".
    EXPECT_EQ("return", MakeOwned<ReturnExpr>()->ToString());
}

TEST(ToStringTest, ConstructedGetMacroCallNewPosBranches)
{
    // Path 1: a plain node (not in macro call) returns INVALID_POSITION.
    {
        auto vd = MakeOwned<VarDecl>();
        EXPECT_EQ(INVALID_POSITION, vd->GetMacroCallNewPos(Position{0, 1, 1}));
    }
    // Path 2: a node in a macro-call chain whose invocation has empty maps
    // returns INVALID_POSITION (early-return at the empty-map guard).
    {
        auto vd = MakeOwned<VarDecl>();
        vd->isInMacroCall = true;
        auto outer = MakeOwned<MacroExpandExpr>();
        outer->identifier = "Moo";
        vd->curMacroCall = outer.get();
        EXPECT_EQ(INVALID_POSITION, vd->GetMacroCallNewPos(Position{0, 1, 1}));
    }
    // Path 3: a MacroExpandExpr node with empty maps returns INVALID_POSITION
    // via the IsMacroCallNode() branch.
    {
        auto mee = MakeOwned<MacroExpandExpr>();
        mee->identifier = "Moo";
        EXPECT_EQ(INVALID_POSITION, mee->GetMacroCallNewPos(Position{0, 1, 1}));
    }
    // Path 4: a MacroExpandExpr with a populated originPosMap whose key is not
    // present returns INVALID_POSITION at the lower_bound/end check.
    {
        auto mee = MakeOwned<MacroExpandExpr>();
        mee->identifier = "Moo";
        // Insert an entry for a *different* hash so the lookup misses.
        Position dummyOrigin{0, 9, 9};
        mee->invocation.macroCallDiagInfo.originPosMap[dummyOrigin.Hash32()] = dummyOrigin;
        mee->invocation.origin2newPosMap[dummyOrigin.Hash64()] = Position{0, 2, 2};
        EXPECT_EQ(INVALID_POSITION, mee->GetMacroCallNewPos(Position{0, 1, 1}));
    }
    // Path 5: a MacroExpandExpr with matching maps returns the mapped position.
    {
        auto mee = MakeOwned<MacroExpandExpr>();
        mee->identifier = "Moo";
        Position originPos{0, 1, 1};
        Position originToken{0, 1, 5};
        Position newPos{0, 2, 3};
        mee->invocation.macroCallDiagInfo.originPosMap[originPos.Hash32()] = originToken;
        mee->invocation.origin2newPosMap[originToken.Hash64()] = newPos;
        EXPECT_EQ(newPos, mee->GetMacroCallNewPos(originPos));
    }
}

TEST(ToStringTest, ConstructedGetDebugPosBranches)
{
    // Path 1: a plain non-macro node has no const invocation -> returns curPos.
    {
        auto vd = MakeOwned<VarDecl>();
        Position curPos{0, 1, 1};
        EXPECT_EQ(curPos, vd->GetDebugPos(curPos));
    }
    // Path 2: a MacroExpandExpr with empty macroDebugMap returns curPos.
    {
        auto mee = MakeOwned<MacroExpandExpr>();
        mee->identifier = "Moo";
        Position curPos{0, 1, 1};
        EXPECT_EQ(curPos, mee->GetDebugPos(curPos));
    }
    // Path 3: a MacroExpandExpr with a non-matching column returns curPos.
    {
        auto mee = MakeOwned<MacroExpandExpr>();
        mee->identifier = "Moo";
        Position curPos{0, 1, 1};
        // Map a different column so the lookup misses.
        mee->invocation.macroDebugMap[5u] = Position{0, 2, 2};
        EXPECT_EQ(curPos, mee->GetDebugPos(curPos));
    }
    // Path 4: a MacroExpandExpr with a matching column returns the mapped position.
    {
        auto mee = MakeOwned<MacroExpandExpr>();
        mee->identifier = "Moo";
        Position curPos{0, 1, 1};
        Position mapped{0, 3, 7};
        mee->invocation.macroDebugMap[static_cast<unsigned int>(curPos.column)] = mapped;
        EXPECT_EQ(mapped, mee->GetDebugPos(curPos));
    }
}

TEST(ToStringTest, ConstructedGetEndFileIdMismatchBranch)
{
    // GetEnd(): when begin and end map to positions in different fileIDs,
    // endPos is reset to beginPos + 1.
    auto vd = MakeOwned<VarDecl>();
    vd->begin = Position{0, 1, 1};
    vd->end = Position{1, 2, 5};
    // No curMacroCall -> GetMacroCallPos returns the positions unchanged,
    // begin.fileID(0) != end.fileID(1) -> end = begin + 1.
    EXPECT_EQ((Position{0, 1, 2}), vd->GetEnd());
}

TEST(ToStringTest, ConstructedSetTargetAndGetTargetDefaults)
{
    // SetTarget default case: a node whose astKind is not one of the handled
    // kinds returns without writing.
    {
        auto vd = MakeOwned<VarDecl>();
        auto target = MakeOwned<VarDecl>();
        vd->SetTarget(target.get()); // default -> no-op, no crash.
        EXPECT_EQ(nullptr, vd->GetTarget()); // default GetTarget -> nullptr.
    }
    // GetTargets default case: a non-RefType/RefExpr/MemberAccess node returns {}.
    {
        auto vd = MakeOwned<VarDecl>();
        EXPECT_TRUE(vd->GetTargets().empty());
    }
}

// Batch 4: cover IsExportedDecl/IsOpen overrides, IsSamePackage null-package,
// SubscriptExpr::Clear indexExprs branch, and GetTargets REF_TYPE branch.

TEST(ToStringTest, ConstructedExtendDeclIsExportedDeclBranches)
{
    // Rule 3: a direct extension (empty inheritedTypes) whose extended type's
    // package differs from the extension's package is never exported.
    // A bare ExtendDecl has GetTy() == InitialTy -> extendedDecl nullptr ->
    // isInSamePkg false -> not std.core -> Rule 3 -> false.
    {
        auto ed = MakeOwned<ExtendDecl>();
        ed->fullPackageName = "my.pkg";
        EXPECT_FALSE(ed->IsExportedDecl());
    }
    // Rule 1: in package std.core, direct extensions are exported.
    {
        auto ed = MakeOwned<ExtendDecl>();
        ed->fullPackageName = "std.core";
        EXPECT_TRUE(ed->IsExportedDecl());
    }
    // Interface extension (non-empty inheritedTypes), not same pkg, empty
    // inheritedTypes' targets -> isInterfaceAllExported stays false.
    {
        auto ed = MakeOwned<ExtendDecl>();
        ed->fullPackageName = "other.pkg";
        // Add an inherited type with no resolved target so the loop body skips it.
        ed->inheritedTypes.push_back(MakeOwned<RefType>());
        EXPECT_FALSE(ed->IsExportedDecl());
    }
}

TEST(ToStringTest, ConstructedPropDeclAndFuncDeclIsExportedDeclNullOuter)
{
    // PropDecl with no outerDecl falls through to Decl::IsExportedDecl.
    {
        auto pd = MakeOwned<PropDecl>();
        EXPECT_TRUE(pd->IsExportedDecl()); // default exported (no modifier).
    }
    // PropDecl with a PUBLIC modifier and no outerDecl.
    {
        auto pd = MakeOwned<PropDecl>();
        pd->EnableAttr(Attribute::PUBLIC);
        EXPECT_TRUE(pd->IsExportedDecl());
    }
    // FuncDecl with no outerDecl falls through to Decl::IsExportedDecl.
    {
        auto fd = MakeOwned<FuncDecl>();
        fd->identifier = "f";
        EXPECT_TRUE(fd->IsExportedDecl());
    }
    // FuncDecl with PRIVATE and no outerDecl -> false.
    {
        auto fd = MakeOwned<FuncDecl>();
        fd->identifier = "g";
        fd->EnableAttr(Attribute::PRIVATE);
        EXPECT_FALSE(fd->IsExportedDecl());
    }
}

TEST(ToStringTest, ConstructedFuncDeclAndPropDeclIsOpenNullOuter)
{
    // FuncDecl::IsOpen() with no outerDecl -> first clause true -> false.
    {
        auto fd = MakeOwned<FuncDecl>();
        fd->identifier = "f";
        EXPECT_FALSE(fd->IsOpen());
    }
    // PropDecl::IsOpen() with no outerDecl -> first clause true -> false.
    {
        auto pd = MakeOwned<PropDecl>();
        EXPECT_FALSE(pd->IsOpen());
    }
    // FuncDecl::IsOpen() with a STATIC attribute but no outerDecl -> false.
    {
        auto fd = MakeOwned<FuncDecl>();
        fd->identifier = "h";
        fd->EnableAttr(Attribute::STATIC);
        EXPECT_FALSE(fd->IsOpen());
    }
}

TEST(ToStringTest, ConstructedIsSamePackageNullPackageBranch)
{
    // Both nodes have a curFile but the file's curPackage is null -> returns true.
    auto vd = MakeOwned<VarDecl>();
    auto other = MakeOwned<VarDecl>();
    auto file = MakeOwned<File>(); // curPackage stays null.
    vd->curFile = file.get();
    other->curFile = file.get();
    EXPECT_TRUE(vd->IsSamePackage(*other));
}

TEST(ToStringTest, ConstructedSubscriptExprClearIndexExprsBranch)
{
    // SubscriptExpr::Clear() with indexExprs exercises the indexExpr->Clear() loop.
    auto se = MakeOwned<SubscriptExpr>();
    auto idx = MakeOwned<RefExpr>();
    idx->ref = Reference("i");
    se->indexExprs.push_back(std::move(idx));
    se->Clear(); // walks indexExprs and clears each.
    SUCCEED();
}

TEST(ToStringTest, ConstructedGetTargetsRefTypeBranch)
{
    // GetTargets() for REF_TYPE returns ref.targets.
    auto rt = MakeOwned<RefType>();
    auto target = MakeOwned<VarDecl>();
    rt->ref.targets.push_back(target.get());
    auto decls = rt->GetTargets();
    EXPECT_EQ(1u, decls.size());
    EXPECT_EQ(target.get(), decls[0]);
}

// Batch 5: deeper macro-call chains, GetDebugPos via curMacroCall,
// GetPossiblePackageNames double-colon multi-prefix, ExtendDecl extendedType loop,
// and GetNamedArg/GetMacroCallNewPos nested-loop bodies.

TEST(ToStringTest, ConstructedGetNamedArgNotIfAvailable)
{
    // GetNamedArg() returns {} when invocation is not IfAvailable.
    auto mee = MakeOwned<MacroExpandExpr>();
    mee->identifier = "Moo"; // fullName defaults to "" != IF_AVAILABLE.
    // Add a node so !invocation.nodes.empty() is true but IsIfAvailable() is false.
    mee->invocation.nodes.push_back(MakeOwned<FuncArg>());
    EXPECT_EQ(nullptr, mee->GetNamedArg());
}

TEST(ToStringTest, ConstructedGetMacroCallNewPosNestedChain)
{
    // A node whose curMacroCall itself has a curMacroCall exercises the
    // while-loop body (tempNode = tempNode->curMacroCall) at the outermost walk.
    auto mee = MakeOwned<MacroExpandExpr>();
    mee->identifier = "Outer";
    auto inner = MakeOwned<MacroExpandExpr>();
    inner->identifier = "Inner";
    inner->curMacroCall = mee.get(); // inner->curMacroCall = outer (non-null).
    // Now calling GetMacroCallNewPos on `inner` (a macro node) walks the chain:
    // tempNode = inner; while(tempNode->curMacroCall) -> mee; loop body runs.
    EXPECT_EQ(INVALID_POSITION, inner->GetMacroCallNewPos(Position{0, 1, 1}));
}

TEST(ToStringTest, ConstructedGetMacroCallNewPosIsInMacroCallNestedChain)
{
    // The isInMacroCall branch with a two-deep curMacroCall chain exercises the
    // first while-loop body (line 779).
    auto vd = MakeOwned<VarDecl>();
    vd->isInMacroCall = true;
    auto outer = MakeOwned<MacroExpandExpr>();
    outer->identifier = "Outer";
    auto middle = MakeOwned<MacroExpandExpr>();
    middle->identifier = "Middle";
    middle->curMacroCall = outer.get(); // middle->curMacroCall non-null.
    vd->curMacroCall = middle.get();
    // Walk: tempNode=middle; while(middle->curMacroCall) -> body runs -> tempNode=outer.
    EXPECT_EQ(INVALID_POSITION, vd->GetMacroCallNewPos(Position{0, 1, 1}));
}

TEST(ToStringTest, ConstructedGetDebugPosViaCurMacroCall)
{
    // A non-macro node whose curMacroCall is a MacroExpandExpr (with a non-empty
    // macroDebugMap) exercises the 1015-1017 deep branch.
    auto vd = MakeOwned<VarDecl>();
    auto mee = MakeOwned<MacroExpandExpr>();
    mee->identifier = "Moo";
    mee->invocation.macroDebugMap[1u] = Position{0, 2, 2};
    vd->curMacroCall = mee.get();
    Position curPos{0, 1, 1};
    // curPos != INVALID, curMacroCall set -> pInvocation = curMacroCall->GetConstInvocation()
    // which is non-null with a non-empty macroDebugMap -> falls through to lookup.
    // column 1 is in the map -> returns the mapped position.
    EXPECT_EQ((Position{0, 2, 2}), vd->GetDebugPos(curPos));
}

TEST(ToStringTest, ConstructedGetDebugPosViaCurMacroCallEmptyMap)
{
    // Same as above but macroDebugMap empty -> returns curPos at the 1017 guard.
    auto vd = MakeOwned<VarDecl>();
    auto mee = MakeOwned<MacroExpandExpr>();
    mee->identifier = "Moo";
    vd->curMacroCall = mee.get();
    Position curPos{0, 1, 1};
    EXPECT_EQ(curPos, vd->GetDebugPos(curPos));
}

TEST(ToStringTest, ConstructedGetDebugPosViaCurMacroCallNoInvocation)
{
    // curMacroCall points at a non-macro node -> GetConstInvocation returns null
    // -> the !pInvocation branch returns curPos.
    auto vd = MakeOwned<VarDecl>();
    auto outer = MakeOwned<VarDecl>(); // not a macro node -> GetConstInvocation null.
    vd->curMacroCall = outer.get();
    Position curPos{0, 1, 1};
    EXPECT_EQ(curPos, vd->GetDebugPos(curPos));
}

TEST(ToStringTest, ConstructedGetPossiblePackageNamesDoubleColonMultiPrefix)
{
    // hasDoubleColon with multiple prefix paths exercises the i==0 double-colon
    // branch (line 1128) and subsequent dot branches.
    ImportContent c;
    c.prefixPaths = {"a", "b"};
    c.identifier = "x";
    c.hasDoubleColon = true;
    c.kind = ImportKind::IMPORT_SINGLE;
    auto names = c.GetPossiblePackageNames();
    ASSERT_EQ(2u, names.size());
    EXPECT_EQ("a::b.x", names[0]);
    EXPECT_EQ("a::b", names[1]);
}

TEST(ToStringTest, ConstructedExtendDeclExtendedTypeLoop)
{
    // extendedType != nullptr with a non-empty GetTypeArgs() exercises the
    // extendedType check loop (lines 1200-1204). The type arg's GetTarget() is
    // null (RefType not in GetTarget's handled kinds) so it does not return false.
    auto ed = MakeOwned<ExtendDecl>();
    ed->fullPackageName = "my.pkg";
    auto ext = MakeOwned<RefType>();
    ext->typeArguments.push_back(MakeOwned<RefType>()); // non-empty type args.
    ed->extendedType = std::move(ext);
    // Falls through to Rule 3 (different pkg, empty inheritedTypes) -> false.
    EXPECT_FALSE(ed->IsExportedDecl());
}

// Batch 6: cover GetMacroCallNewPos final-miss branch, and the GetMacroCallPos
// branches that walk the curMacroCall / direct-invocation paths.

TEST(ToStringTest, ConstructedGetMacroCallNewPosMapKeyMiss)
{
    // originPosMap contains the origin hash, but origin2newPosMap does not hold
    // the corresponding newkey -> falls through to the final return INVALID_POSITION.
    auto mee = MakeOwned<MacroExpandExpr>();
    mee->identifier = "Moo";
    Position originPos{0, 1, 1};
    Position originToken{0, 1, 5};
    mee->invocation.macroCallDiagInfo.originPosMap[originPos.Hash32()] = originToken;
    // origin2newPosMap is non-empty (passes the empty-map guard) but has no entry
    // for originToken.Hash64().
    mee->invocation.origin2newPosMap[0u] = Position{0, 9, 9};
    EXPECT_EQ(INVALID_POSITION, mee->GetMacroCallNewPos(originPos));
}

TEST(ToStringTest, ConstructedGetMacroCallPosLineMismatch)
{
    // curMacroCall set, originPos.line != curMacroCall->begin.line -> return originPos.
    auto vd = MakeOwned<VarDecl>();
    auto mee = MakeOwned<MacroExpandExpr>();
    mee->identifier = "Moo";
    mee->begin = Position{0, 5, 1}; // macro call begins on line 5.
    vd->curMacroCall = mee.get();
    Position originPos{0, 1, 1}; // line 1 != 5.
    EXPECT_EQ(originPos, vd->GetMacroCallPos(originPos));
}

TEST(ToStringTest, ConstructedGetMacroCallPosNonPureAnnotation)
{
    // curMacroCall set, same line, pInvocation non-null, not a pure annotation
    // -> delegates to GetMacroSourcePos (lines 818-820).
    auto vd = MakeOwned<VarDecl>();
    auto mee = MakeOwned<MacroExpandExpr>();
    mee->identifier = "Moo";
    mee->begin = Position{0, 5, 1};
    // Ensure not pure annotation (isCustom stays false).
    vd->curMacroCall = mee.get();
    Position originPos{0, 5, 3}; // same line as macrocall begin.
    // GetMacroSourcePos delegates to MapPos; with empty maps it returns the pos.
    EXPECT_EQ(originPos, vd->GetMacroCallPos(originPos));
}

TEST(ToStringTest, ConstructedGetMacroCallPosPureAnnotationFallsThrough)
{
    // curMacroCall set, same line, but the invocation is a pure annotation ->
    // the inner if is skipped, falls through to the GetConstInvocation() block,
    // which on a non-macro node is null -> returns originPos.
    auto vd = MakeOwned<VarDecl>();
    auto mee = MakeOwned<MacroExpandExpr>();
    mee->identifier = "Moo";
    mee->begin = Position{0, 5, 1};
    mee->invocation.macroCallDiagInfo.isCustom = true;
    mee->invocation.macroCallDiagInfo.isCurFile = true; // pure annotation.
    vd->curMacroCall = mee.get();
    Position originPos{0, 5, 3};
    EXPECT_EQ(originPos, vd->GetMacroCallPos(originPos));
}

TEST(ToStringTest, ConstructedGetMacroCallPosMacroNodeFileIdMismatch)
{
    // A macro node (MacroExpandExpr) with begin.fileID != originPos.fileID
    // -> returns originPos (lines 825-827).
    auto mee = MakeOwned<MacroExpandExpr>();
    mee->identifier = "Moo";
    mee->begin = Position{0, 1, 1}; // fileID 0.
    Position originPos{1, 1, 1};      // fileID 1 != 0.
    EXPECT_EQ(originPos, mee->GetMacroCallPos(originPos));
}

TEST(ToStringTest, ConstructedGetMacroCallPosMacroNodeSameFile)
{
    // A macro node with begin.fileID == originPos.fileID -> delegates to
    // GetMacroSourcePos (line 830).
    auto mee = MakeOwned<MacroExpandExpr>();
    mee->identifier = "Moo";
    mee->begin = Position{0, 1, 1};
    Position originPos{0, 1, 1}; // same fileID.
    // MapPos with empty maps returns the pos unchanged.
    EXPECT_EQ(originPos, mee->GetMacroCallPos(originPos));
}

// Batch 7: cover ExtendDecl same-package direct-extension and interface-extension
// paths, plus FuncDecl/PropDecl outerDecl=ExtendDecl paths, by constructing real
// ClassTy/ClassDecl tys so GetDeclPtrOfTy returns a non-null extended decl.

TEST(ToStringTest, ConstructedExtendDeclSamePkgDirectExtension)
{
    // Same-package direct extension (empty inheritedTypes) with a real extended
    // ClassDecl that has no access modifier (exported). generic is null ->
    // returns extendedDecl->IsExportedDecl() (lines 1228-1233).
    auto ed = MakeOwned<ExtendDecl>();
    auto cd = MakeOwned<ClassDecl>();
    cd->identifier = "Foo";
    ed->fullPackageName = "my.pkg";
    cd->fullPackageName = "my.pkg"; // same package -> isInSamePkg true.
    auto cty = MakeOwned<ClassTy>("Foo", *cd, std::vector<Ptr<Ty>>{});
    ed->SetTy(DataTy{cty});
    EXPECT_TRUE(ed->IsExportedDecl());
}

TEST(ToStringTest, ConstructedExtendDeclSamePkgDirectExtensionWithGeneric)
{
    // Same-package direct extension with a generic -> calls isUpperBoundExport()
    // (lines 1235, 1208-1220). With an empty genericConstraints list, the loop
    // body never runs and isUpperboundAllExported stays true.
    auto ed = MakeOwned<ExtendDecl>();
    auto cd = MakeOwned<ClassDecl>();
    cd->identifier = "Foo";
    ed->fullPackageName = "my.pkg";
    cd->fullPackageName = "my.pkg";
    ed->generic = MakeOwned<Generic>(); // generic non-null, constraints empty.
    auto cty = MakeOwned<ClassTy>("Foo", *cd, std::vector<Ptr<Ty>>{});
    ed->SetTy(DataTy{cty});
    EXPECT_TRUE(ed->IsExportedDecl());
}

TEST(ToStringTest, ConstructedExtendDeclInterfaceExtensionSamePkg)
{
    // Same-package interface extension (non-empty inheritedTypes) -> returns
    // extendedDecl->IsExportedDecl() (line 1246).
    auto ed = MakeOwned<ExtendDecl>();
    auto cd = MakeOwned<ClassDecl>();
    cd->identifier = "Foo";
    ed->fullPackageName = "my.pkg";
    cd->fullPackageName = "my.pkg";
    auto cty = MakeOwned<ClassTy>("Foo", *cd, std::vector<Ptr<Ty>>{});
    ed->SetTy(DataTy{cty});
    ed->inheritedTypes.push_back(MakeOwned<RefType>()); // non-empty -> interface ext.
    EXPECT_TRUE(ed->IsExportedDecl());
}

TEST(ToStringTest, ConstructedExtendDeclInterfaceExtensionDiffPkgNoExported)
{
    // Different-package interface extension with no exported inherited types
    // -> isInterfaceAllExported stays false (lines 1251-1264).
    auto ed = MakeOwned<ExtendDecl>();
    auto cd = MakeOwned<ClassDecl>();
    cd->identifier = "Foo";
    ed->fullPackageName = "my.pkg";
    cd->fullPackageName = "other.pkg"; // different package.
    auto cty = MakeOwned<ClassTy>("Foo", *cd, std::vector<Ptr<Ty>>{});
    ed->SetTy(DataTy{cty});
    // inheritedType with no resolved target -> loop skips it -> false.
    ed->inheritedTypes.push_back(MakeOwned<RefType>());
    EXPECT_FALSE(ed->IsExportedDecl());
}

TEST(ToStringTest, ConstructedPropDeclIsExportedDeclWithExtendOuter)
{
    // PropDecl whose outerDecl is an ExtendDecl: not same package (extended decl
    // in a different package), direct extension (empty inheritedTypes) -> false.
    auto pd = MakeOwned<PropDecl>();
    auto ed = MakeOwned<ExtendDecl>();
    auto cd = MakeOwned<ClassDecl>();
    cd->identifier = "Foo";
    ed->fullPackageName = "my.pkg";
    cd->fullPackageName = "other.pkg";
    auto cty = MakeOwned<ClassTy>("Foo", *cd, std::vector<Ptr<Ty>>{});
    ed->SetTy(DataTy{cty});
    pd->outerDecl = ed.get();
    EXPECT_FALSE(pd->IsExportedDecl());
}

TEST(ToStringTest, ConstructedPropDeclIsExportedDeclWithExtendOuterSamePkg)
{
    // PropDecl whose outerDecl is an ExtendDecl in the same package as the
    // extended type -> delegates to Decl::IsExportedDecl (line 1276).
    auto pd = MakeOwned<PropDecl>();
    auto ed = MakeOwned<ExtendDecl>();
    auto cd = MakeOwned<ClassDecl>();
    cd->identifier = "Foo";
    ed->fullPackageName = "my.pkg";
    cd->fullPackageName = "my.pkg";
    auto cty = MakeOwned<ClassTy>("Foo", *cd, std::vector<Ptr<Ty>>{});
    ed->SetTy(DataTy{cty});
    pd->outerDecl = ed.get();
    pd->EnableAttr(Attribute::PUBLIC);
    EXPECT_TRUE(pd->IsExportedDecl());
}

TEST(ToStringTest, ConstructedFuncDeclIsExportedDeclWithExtendOuter)
{
    // FuncDecl whose outerDecl is an ExtendDecl: different package, direct
    // extension -> false (lines 1297-1298).
    auto fd = MakeOwned<FuncDecl>();
    fd->identifier = "f";
    auto ed = MakeOwned<ExtendDecl>();
    auto cd = MakeOwned<ClassDecl>();
    cd->identifier = "Foo";
    ed->fullPackageName = "my.pkg";
    cd->fullPackageName = "other.pkg";
    auto cty = MakeOwned<ClassTy>("Foo", *cd, std::vector<Ptr<Ty>>{});
    ed->SetTy(DataTy{cty});
    fd->outerDecl = ed.get();
    EXPECT_FALSE(fd->IsExportedDecl());
}

// Batch 8: cover GetSuperInterfaceTys / GetStableSuperInterfaceTys /
// GetAllSuperDecls (via real InterfaceTy/ClassTy in inheritedTypes), FuncBody
// multi-paramList spacing, and MacroExpandExpr multi-attr formatting.

TEST(ToStringTest, ConstructedInheritableDeclGetSuperInterfaceTys)
{
    // A ClassDecl with an inherited InterfaceTy -> GetSuperInterfaceTys collects it.
    auto cd = MakeOwned<ClassDecl>();
    cd->identifier = "C";
    auto id = MakeOwned<InterfaceDecl>();
    id->identifier = "I";
    auto ity = MakeOwned<InterfaceTy>("I", *id, std::vector<Ptr<Ty>>{});
    auto refType = MakeOwned<RefType>();
    refType->SetTy(DataTy{ity});
    cd->inheritedTypes.push_back(std::move(refType));
    auto supers = cd->GetSuperInterfaceTys();
    ASSERT_EQ(1u, supers.size());
    // The set holds the InterfaceTy pointer.
    EXPECT_EQ(ity.get(), *supers.begin());
}

TEST(ToStringTest, ConstructedInheritableDeclGetStableSuperInterfaceTys)
{
    // Same setup but via GetStableSuperInterfaceTys (sorted vector).
    auto cd = MakeOwned<ClassDecl>();
    cd->identifier = "C";
    auto id = MakeOwned<InterfaceDecl>();
    id->identifier = "I";
    auto ity = MakeOwned<InterfaceTy>("I", *id, std::vector<Ptr<Ty>>{});
    auto refType = MakeOwned<RefType>();
    refType->SetTy(DataTy{ity});
    cd->inheritedTypes.push_back(std::move(refType));
    auto supers = cd->GetStableSuperInterfaceTys();
    ASSERT_EQ(1u, supers.size());
    EXPECT_EQ(ity.get(), supers[0]);
}

TEST(ToStringTest, ConstructedInheritableDeclGetAllSuperDecls)
{
    // A ClassDecl (ClassLikeDecl) with an inherited ClassTy -> GetAllSuperDecls
    // walks the worklist and collects both the start decl and the super class.
    auto cd = MakeOwned<ClassDecl>();
    cd->identifier = "C";
    auto superCd = MakeOwned<ClassDecl>();
    superCd->identifier = "Super";
    auto cty = MakeOwned<ClassTy>("Super", *superCd, std::vector<Ptr<Ty>>{});
    auto refType = MakeOwned<RefType>();
    refType->SetTy(DataTy{cty});
    cd->inheritedTypes.push_back(std::move(refType));
    auto supers = cd->GetAllSuperDecls();
    // Result should contain at least the start ClassDecl and the super ClassDecl.
    ASSERT_GE(supers.size(), 2u);
    EXPECT_EQ(cd.get(), supers[0]);
    EXPECT_EQ(superCd.get(), supers[1]);
}

TEST(ToStringTest, ConstructedInheritableDeclGetAllSuperDeclsInterface)
{
    // A ClassDecl with an inherited InterfaceTy -> GetAllSuperDecls interface branch.
    auto cd = MakeOwned<ClassDecl>();
    cd->identifier = "C";
    auto id = MakeOwned<InterfaceDecl>();
    id->identifier = "I";
    auto ity = MakeOwned<InterfaceTy>("I", *id, std::vector<Ptr<Ty>>{});
    auto refType = MakeOwned<RefType>();
    refType->SetTy(DataTy{ity});
    cd->inheritedTypes.push_back(std::move(refType));
    auto supers = cd->GetAllSuperDecls();
    ASSERT_GE(supers.size(), 2u);
    EXPECT_EQ(cd.get(), supers[0]);
    EXPECT_EQ(id.get(), supers[1]);
}

TEST(ToStringTest, ConstructedFuncBodyMultipleParamListsSpacing)
{
    // FuncBody::ToString with two paramLists exercises the `i > 0` space branch.
    auto fb = MakeOwned<FuncBody>();
    auto pl1 = MakeOwned<FuncParamList>();
    pl1->params.push_back(MakeOwned<FuncParam>());
    auto pl2 = MakeOwned<FuncParamList>();
    pl2->params.push_back(MakeOwned<FuncParam>());
    fb->paramLists.push_back(std::move(pl1));
    fb->paramLists.push_back(std::move(pl2));
    auto out = fb->ToString();
    // The second list is preceded by a space separator.
    EXPECT_NE(out.find(" "), std::string::npos);
}

TEST(ToStringTest, ConstructedMacroExpandExprMultipleAttrs)
{
    // MacroExpandExpr::ToString with two attrs exercises the `i > 0` ", " branch.
    auto mee = MakeOwned<MacroExpandExpr>();
    mee->identifier = "Moo";
    mee->invocation.attrs.emplace_back(TokenKind::IDENTIFIER, std::string("a"));
    mee->invocation.attrs.emplace_back(TokenKind::IDENTIFIER, std::string("b"));
    auto out = mee->ToString();
    EXPECT_NE(out.find(", "), std::string::npos);
}

// Batch 9: cover the remaining ExtendDecl/PropDecl/FuncDecl IsExportedDecl deep
// branches (extendedType type-arg check, upper-bound export lambda, interface-
// extension exported-target, INTERFACE_IMPL), FuncDecl/PropDecl IsOpen deep
// branches (open outer decl), and GetStableSuperInterfaceTys comparator.

TEST(ToStringTest, ConstructedExtendDeclExtendedTypeUnexportedTypeArg)
{
    // extendedType with a type arg whose target is unexported (PRIVATE) ->
    // the loop body returns false (line 1202).
    auto ed = MakeOwned<ExtendDecl>();
    ed->fullPackageName = "my.pkg";
    auto ext = MakeOwned<RefType>();
    auto typeArg = MakeOwned<RefType>();
    auto unexp = MakeOwned<VarDecl>();
    unexp->EnableAttr(Attribute::PRIVATE); // unexported.
    typeArg->ref.target = unexp.get();
    ext->typeArguments.push_back(std::move(typeArg));
    ed->extendedType = std::move(ext);
    EXPECT_FALSE(ed->IsExportedDecl());
}

TEST(ToStringTest, ConstructedExtendDeclSamePkgGenericUpperBound)
{
    // Same-package direct extension with a generic whose genericConstraints
    // have an upper bound with a null target -> the inner continue branch
    // (lines 1211-1215). The lambda runs; result stays exported.
    auto ed = MakeOwned<ExtendDecl>();
    auto cd = MakeOwned<ClassDecl>();
    cd->identifier = "Foo";
    ed->fullPackageName = "my.pkg";
    cd->fullPackageName = "my.pkg";
    ed->generic = MakeOwned<Generic>();
    auto gc = MakeOwned<GenericConstraint>();
    auto ub = MakeOwned<RefType>(); // upper bound with no resolved target.
    gc->upperBounds.push_back(std::move(ub));
    ed->generic->genericConstraints.push_back(std::move(gc));
    auto cty = MakeOwned<ClassTy>("Foo", *cd, std::vector<Ptr<Ty>>{});
    ed->SetTy(DataTy{cty});
    // extendedDecl exported (no modifier) and upperBound has null target (skipped).
    EXPECT_TRUE(ed->IsExportedDecl());
}

TEST(ToStringTest, ConstructedExtendDeclSamePkgGenericUpperBoundExported)
{
    // Same-package direct extension with a generic whose upper bound has a
    // resolved, exported target -> the isUpperboundAllExported assignment runs
    // (line 1217) and stays true.
    auto ed = MakeOwned<ExtendDecl>();
    auto cd = MakeOwned<ClassDecl>();
    cd->identifier = "Foo";
    ed->fullPackageName = "my.pkg";
    cd->fullPackageName = "my.pkg";
    ed->generic = MakeOwned<Generic>();
    auto gc = MakeOwned<GenericConstraint>();
    auto ub = MakeOwned<RefType>();
    auto exp = MakeOwned<VarDecl>();
    exp->EnableAttr(Attribute::PUBLIC); // exported target.
    ub->ref.target = exp.get();
    gc->upperBounds.push_back(std::move(ub));
    ed->generic->genericConstraints.push_back(std::move(gc));
    auto cty = MakeOwned<ClassTy>("Foo", *cd, std::vector<Ptr<Ty>>{});
    ed->SetTy(DataTy{cty});
    EXPECT_TRUE(ed->IsExportedDecl());
}

TEST(ToStringTest, ConstructedExtendDeclInterfaceExtDiffPkgExportedTarget)
{
    // Different-package interface extension where an inherited type's target is
    // exported -> isInterfaceAllExported becomes true (lines 1256-1258).
    auto ed = MakeOwned<ExtendDecl>();
    auto cd = MakeOwned<ClassDecl>();
    cd->identifier = "Foo";
    ed->fullPackageName = "my.pkg";
    cd->fullPackageName = "other.pkg";
    auto cty = MakeOwned<ClassTy>("Foo", *cd, std::vector<Ptr<Ty>>{});
    ed->SetTy(DataTy{cty});
    // An inherited RefType whose ref.target is an exported decl.
    auto inh = MakeOwned<RefType>();
    auto exp = MakeOwned<VarDecl>();
    exp->EnableAttr(Attribute::PUBLIC); // exported.
    inh->ref.target = exp.get();
    ed->inheritedTypes.push_back(std::move(inh));
    EXPECT_TRUE(ed->IsExportedDecl());
}

TEST(ToStringTest, ConstructedExtendDeclInterfaceExtDiffPkgWithGeneric)
{
    // Different-package interface extension with a generic and an exported
    // inherited target -> isInterfaceAllExported && isUpperBoundExport() (1264).
    auto ed = MakeOwned<ExtendDecl>();
    auto cd = MakeOwned<ClassDecl>();
    cd->identifier = "Foo";
    ed->fullPackageName = "my.pkg";
    cd->fullPackageName = "other.pkg";
    auto cty = MakeOwned<ClassTy>("Foo", *cd, std::vector<Ptr<Ty>>{});
    ed->SetTy(DataTy{cty});
    ed->generic = MakeOwned<Generic>(); // empty constraints -> lambda returns true.
    auto inh = MakeOwned<RefType>();
    auto exp = MakeOwned<VarDecl>();
    exp->EnableAttr(Attribute::PUBLIC);
    inh->ref.target = exp.get();
    ed->inheritedTypes.push_back(std::move(inh));
    EXPECT_TRUE(ed->IsExportedDecl());
}

TEST(ToStringTest, ConstructedPropDeclIsExportedDeclInterfaceImpl)
{
    // PropDecl with outerDecl=ExtendDecl, different package, non-empty
    // inheritedTypes, and INTERFACE_IMPL set -> Decl::IsExportedDecl() && true.
    auto pd = MakeOwned<PropDecl>();
    pd->EnableAttr(Attribute::PUBLIC);
    pd->EnableAttr(Attribute::INTERFACE_IMPL);
    auto ed = MakeOwned<ExtendDecl>();
    auto cd = MakeOwned<ClassDecl>();
    cd->identifier = "Foo";
    ed->fullPackageName = "my.pkg";
    cd->fullPackageName = "other.pkg";
    auto cty = MakeOwned<ClassTy>("Foo", *cd, std::vector<Ptr<Ty>>{});
    ed->SetTy(DataTy{cty});
    ed->inheritedTypes.push_back(MakeOwned<RefType>()); // non-empty -> not direct ext.
    pd->outerDecl = ed.get();
    EXPECT_TRUE(pd->IsExportedDecl());
}

TEST(ToStringTest, ConstructedFuncDeclIsExportedDeclSamePkgWithExtend)
{
    // FuncDecl with outerDecl=ExtendDecl, same package -> Decl::IsExportedDecl (1294).
    auto fd = MakeOwned<FuncDecl>();
    fd->identifier = "f";
    fd->EnableAttr(Attribute::PUBLIC);
    auto ed = MakeOwned<ExtendDecl>();
    auto cd = MakeOwned<ClassDecl>();
    cd->identifier = "Foo";
    ed->fullPackageName = "my.pkg";
    cd->fullPackageName = "my.pkg";
    auto cty = MakeOwned<ClassTy>("Foo", *cd, std::vector<Ptr<Ty>>{});
    ed->SetTy(DataTy{cty});
    fd->outerDecl = ed.get();
    EXPECT_TRUE(fd->IsExportedDecl());
}

TEST(ToStringTest, ConstructedFuncDeclIsExportedDeclInterfaceImpl)
{
    // FuncDecl with outerDecl=ExtendDecl, different package, non-empty
    // inheritedTypes, INTERFACE_IMPL set -> Decl::IsExportedDecl() && true (1300).
    auto fd = MakeOwned<FuncDecl>();
    fd->identifier = "f";
    fd->EnableAttr(Attribute::PUBLIC);
    fd->EnableAttr(Attribute::INTERFACE_IMPL);
    auto ed = MakeOwned<ExtendDecl>();
    auto cd = MakeOwned<ClassDecl>();
    cd->identifier = "Foo";
    ed->fullPackageName = "my.pkg";
    cd->fullPackageName = "other.pkg";
    auto cty = MakeOwned<ClassTy>("Foo", *cd, std::vector<Ptr<Ty>>{});
    ed->SetTy(DataTy{cty});
    ed->inheritedTypes.push_back(MakeOwned<RefType>()); // non-empty.
    fd->outerDecl = ed.get();
    EXPECT_TRUE(fd->IsExportedDecl());
}

TEST(ToStringTest, ConstructedFuncDeclIsOpenDeepBranches)
{
    // outerDecl is an InterfaceDecl (IsOpen() true), not STATIC, with OPEN attr
    // -> returns true (lines 1308-1309).
    {
        auto fd = MakeOwned<FuncDecl>();
        fd->identifier = "f";
        auto id = MakeOwned<InterfaceDecl>();
        fd->outerDecl = id.get();
        fd->EnableAttr(Attribute::OPEN);
        EXPECT_TRUE(fd->IsOpen());
    }
    // Same but without OPEN/ABSTRACT and funcBody->body empty -> returns true
    // (line 1311).
    {
        auto fd = MakeOwned<FuncDecl>();
        fd->identifier = "g";
        auto id = MakeOwned<InterfaceDecl>();
        fd->outerDecl = id.get();
        fd->funcBody = MakeOwned<FuncBody>(); // body stays null.
        EXPECT_TRUE(fd->IsOpen());
    }
    // With IMPORTED attr -> false (the !TestAttr(IMPORTED) clause).
    {
        auto fd = MakeOwned<FuncDecl>();
        fd->identifier = "h";
        auto id = MakeOwned<InterfaceDecl>();
        fd->outerDecl = id.get();
        fd->funcBody = MakeOwned<FuncBody>();
        fd->EnableAttr(Attribute::IMPORTED);
        EXPECT_FALSE(fd->IsOpen());
    }
}

TEST(ToStringTest, ConstructedPropDeclIsOpenDeepBranches)
{
    // outerDecl is an InterfaceDecl, not STATIC, with OPEN attr -> true (1319-1320).
    {
        auto pd = MakeOwned<PropDecl>();
        auto id = MakeOwned<InterfaceDecl>();
        pd->outerDecl = id.get();
        pd->EnableAttr(Attribute::OPEN);
        EXPECT_TRUE(pd->IsOpen());
    }
    // Without OPEN/ABSTRACT and empty getters/setters -> true (1322).
    {
        auto pd = MakeOwned<PropDecl>();
        auto id = MakeOwned<InterfaceDecl>();
        pd->outerDecl = id.get();
        EXPECT_TRUE(pd->IsOpen());
    }
    // With IMPORTED attr -> false.
    {
        auto pd = MakeOwned<PropDecl>();
        auto id = MakeOwned<InterfaceDecl>();
        pd->outerDecl = id.get();
        pd->EnableAttr(Attribute::IMPORTED);
        EXPECT_FALSE(pd->IsOpen());
    }
}

TEST(ToStringTest, ConstructedGetStableSuperInterfaceTysComparator)
{
    // Two interface tys in inheritedTypes forces the set comparator to run,
    // exercising the cmp lambda body (line 530).
    auto cd = MakeOwned<ClassDecl>();
    cd->identifier = "C";
    auto id1 = MakeOwned<InterfaceDecl>();
    id1->identifier = "I";
    auto id2 = MakeOwned<InterfaceDecl>();
    id2->identifier = "J";
    auto ity1 = MakeOwned<InterfaceTy>("I", *id1, std::vector<Ptr<Ty>>{});
    auto ity2 = MakeOwned<InterfaceTy>("J", *id2, std::vector<Ptr<Ty>>{});
    auto ref1 = MakeOwned<RefType>();
    ref1->SetTy(DataTy{ity1});
    auto ref2 = MakeOwned<RefType>();
    ref2->SetTy(DataTy{ity2});
    cd->inheritedTypes.push_back(std::move(ref1));
    cd->inheritedTypes.push_back(std::move(ref2));
    auto supers = cd->GetStableSuperInterfaceTys();
    ASSERT_EQ(2u, supers.size());
}
