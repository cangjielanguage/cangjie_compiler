// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include <iostream>
#include <string>

#include "gtest/gtest.h"

#include "cangjie/Basic/Print.h"
#include "cangjie/Basic/StringConvertor.h"
#include "cangjie/Lex/Lexer.h"

using namespace Cangjie;

class LexerTerminatorTest : public ::testing::Test {
protected:
    void SetUp() override
    {
    }

    std::unique_ptr<Lexer> lexer;
    DiagnosticEngine diag{};
};

TEST_F(LexerTerminatorTest, End)
{
    SourceManager sm2;
    std::vector<Position> expectPos = {{0, 2, 6}, {0, 2, 5}, {0, 2, 4}};
    std::vector<std::string> escape_n = {"\"\"\"\nab\"\"\"", "\"\"\"a\nb\"\"\"", "\"\"\"ab\n\"\"\""};
    for (size_t i = 0; i < expectPos.size(); i++) {
        Lexer lexer = Lexer(escape_n[i], diag, sm2);
        Token tok = lexer.Next();
        Position endPos = tok.End();
        EXPECT_EQ(endPos, expectPos[i]);
    }

    std::vector<std::string> escape_r = {"\"\"\"\rab\"\"\"", "\"\"\"a\rb\"\"\"", "\"\"\"ab\r\"\"\""};
    for (size_t i = 0; i < expectPos.size(); i++) {
        Lexer lexer = Lexer(escape_r[i], diag, sm2);
        Token tok = lexer.Next();
        Position endPos = tok.End();
        EXPECT_EQ(endPos, Position(0, 1, 10));
    }

    std::vector<std::string> escape_r_n = {"\"\"\"\r\nab\"\"\"", "\"\"\"a\r\nb\"\"\"", "\"\"\"ab\r\n\"\"\""};
    for (size_t i = 0; i < expectPos.size(); i++) {
        Lexer lexer = Lexer(escape_r_n[i], diag, sm2);
        Token tok = lexer.Next();
        Position endPos = tok.End();
        EXPECT_EQ(endPos, expectPos[i]);
    }
}

TEST_F(LexerTerminatorTest, ScanComment)
{
    SourceManager sm2;
    std::vector<std::string> terminator = {"//abc\n", "//abc\r\n"};
    Position expect_pos = {0, 1, 6};
    for (size_t i = 0; i < terminator.size(); i++) {
        Lexer lexer(terminator[i], diag, sm2);
        Token tok = lexer.Next();
        Position endPos = tok.End();
        EXPECT_EQ(endPos, expect_pos);
    }
    std::string nonTerminator = "//abc\r";
    Lexer lexer = Lexer(nonTerminator, diag, sm2);
    Token tok = lexer.Next();
    Position endPos = tok.End();
    EXPECT_EQ(endPos, Position(0, 1, 7));
}

TEST_F(LexerTerminatorTest, identify_terminator)
{
    SourceManager sm2;
    std::vector<std::string> terminator = {"\n", "\r\n"};
    for (size_t i = 0; i < terminator.size(); i++) {
        Lexer lexer = Lexer(terminator[i], diag, sm2);
        Token term = lexer.Next();
        EXPECT_EQ(term.kind, TokenKind::NL);
        Token end = lexer.Next();
        EXPECT_EQ(end.kind, TokenKind::END);
    }

    std::string nonTerminator = "\r";
    Lexer lexer = Lexer(nonTerminator, diag, sm2);
    Token term = lexer.Next();
    EXPECT_EQ(term.kind, TokenKind::ILLEGAL);
    Token end = lexer.Next();
    EXPECT_EQ(end.kind, TokenKind::END);
}