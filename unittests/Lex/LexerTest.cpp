// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include <iostream>
#include <string>

#include "gtest/gtest.h"

#include "cangjie/Basic/DiagnosticEmitter.h"
#include "cangjie/Basic/Print.h"
#include "cangjie/Basic/StringConvertor.h"
#include "cangjie/Basic/Utils.h"
#include "cangjie/Lex/Lexer.h"

using namespace Cangjie;

class LexerTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        lexer = std::make_unique<Lexer>(code, diag, sm);
    }
    std::string code = R"(
?:
    true false None
    main(argc:Int=1,argv:string) {
    let a:Int=-40
    let pi:Float=3.14
    let alpha=0x1.1p1
    let c:Rune = r'\''
    // grh
    /*/**/*/
    let d:string = "asdqwe"
    let d:string = J"asdqwe"
    let b = 2 ** -a
    print( (a+3*b, (a+3) *b) )
    @abc
    };
)";
    std::unique_ptr<Lexer> lexer;
    DiagnosticEngine diag{};
    SourceManager sm;
};

TEST_F(LexerTest, IntegerTokens)
{
    // shared test
    unsigned int i = 0;
    std::vector<std::string> expectShare = {"1", "40", "2", "3", "3"};
    for (;;) {
        Token tok = lexer->Next();
        if (tok.kind == TokenKind::INTEGER_LITERAL) {
            if (i >= expectShare.size()) {
                break;
            }
            EXPECT_EQ(tok.Value(), expectShare[i]);
            i++;
        }
        if (tok.kind == TokenKind::END) {
            break;
        }
    }

    // private test
    i = 0;
    std::string strInteger = R"(  00b
                                  12k3
                                  0b3
                                  0x%
                                  0xp3
)";
    Lexer lexerstring(strInteger, diag, sm);
    for (;;) {
        Token tok = lexerstring.Next();
        if (tok.kind == TokenKind::END) {
            break;
        }
    }
    // private test
    i = 0;
    std::string integerString = R"(
                007
                0_3
                00_3
                0b00101
                0x3_2_
                0xABCDEFabcdef
)";
    std::vector<std::string> expect = {"007", "0_3", "00_3", "0b00101", "0x3_2_", "0xABCDEFabcdef"};
    Lexer lexerInteger(integerString, diag, sm);
    for (;;) {
        Token tok = lexerInteger.Next();
        if (tok.kind == TokenKind::INTEGER_LITERAL) {
            if (i >= expect.size()) {
                break;
            }
            EXPECT_EQ(tok.Value(), expect[i]);
            i++;
        }
        if (tok.kind == TokenKind::END) {
            break;
        }
    }
}

TEST_F(LexerTest, FloatTokens)
{
    // shared test
    unsigned int i = 0;
    std::vector<std::string> expectShare = {"3.14", "0x1.1p1"};
    for (;;) {
        Token tok = lexer->Next();
        if (tok.kind == TokenKind::FLOAT_LITERAL) {
            if (i >= expectShare.size()) {
                break;
            }
            EXPECT_EQ(tok.Value(), expectShare[i]);
            i++;
        }
        if (tok.kind == TokenKind::END) {
            break;
        }
    }

    // private test
    i = 0;
    std::string strFloat = R"(  0x._3
                                3e++
                                0x3.
                                0x3.3p_
                                0x2_p
                                3.3.3
                                0x3.3pp4
                                0x3.3p4.3
                                0x._3
                                3.4e
)";
    Lexer lexerstring(strFloat, diag, sm);
    for (;;) {
        Token tok = lexerstring.Next();
        if (tok.kind == TokenKind::END) {
            break;
        }
    }

    // private test
    i = 0;
    std::string floatString = R"(
                3.4e3
                3.4
                3.4e-3
                .3e4
                .3
                0x.3p3
                0x.3_p3_
                0x3.3p3
)";
    std::vector<std::string> expect = {"3.4e3", "3.4", "3.4e-3", ".3e4", ".3", "0x.3p3", "0x.3_p3_", "0x3.3p3"};
    Lexer lexerFloat(floatString, diag, sm);
    for (;;) {
        Token tok = lexerFloat.Next();
        if (tok.kind == TokenKind::FLOAT_LITERAL) {
            if (i >= expect.size()) {
                break;
            }
            EXPECT_EQ(tok.Value(), expect[i]);
            i++;
        }
        if (tok.kind == TokenKind::END) {
            break;
        }
    }
}

TEST_F(LexerTest, KeywordTokens)
{
    // shared test
    unsigned int i = 0;
    std::vector<std::string> expectKeyword = {"main", "let", "let", "let", "let", "let", "let"};
    for (;;) {
        Token tok = lexer->Next();
        if (tok.kind <= TokenKind::MAIN && tok.kind >= TokenKind::STRUCT) {
            if (i >= expectKeyword.size()) {
                break;
            }
            EXPECT_EQ(tok.Value(), expectKeyword[i]);
            i++;
        }
        if (tok.kind == TokenKind::END) {
            break;
        }
    }
    // private test
    i = 0;
    std::string keyword = R"(func public let var class init !in )";
    std::vector<std::string> expect = {"func", "public", "let", "var", "class", "init", "!in"};
    Lexer lexerKeyword(keyword, diag, sm);
    for (;;) {
        Token tok = lexerKeyword.Next();
        if (tok.kind <= TokenKind::MAIN && tok.kind >= TokenKind::STRUCT) {
            if (i >= expect.size()) {
                break;
            }
            EXPECT_EQ(tok.Value(), expect[i]);
            i++;
        }
        if (tok.kind == TokenKind::END) {
            break;
        }
    }
}

TEST_F(LexerTest, CharTokens)
{
    // shared test
    unsigned int i = 0;
    std::vector<std::string> expectKeyword = {"\\\'"};
    for (;;) {
        Token tok = lexer->Next();
        if (tok.kind == TokenKind::RUNE_LITERAL) {
            EXPECT_EQ(tok.Value(), expectKeyword[i]);
            i++;
            if (expectKeyword.size()) {
                break;
            }
        }
        if (tok.kind == TokenKind::END) {
            break;
        }
    }
    // private test
    i = 0;
    std::string strChar = R"(' ' '\\' '\ueeee' '\u2345' 's' '2' '*')";
    std::vector<std::string> expectPrivate = {" ", R"(\\)", "\\ueeee", "\\u2345", "s", "2", "*"};
    Lexer lexerchar(strChar, diag, sm);
    for (;;) {
        Token tok = lexerchar.Next();
        if (tok.kind == TokenKind::RUNE_LITERAL) {
            if (i >= expectPrivate.size()) {
                break;
            }
            EXPECT_EQ(tok.Value(), expectPrivate[i]);
            i++;
        }
        if (tok.kind == TokenKind::END) {
            break;
        }
    }
}

TEST_F(LexerTest, StringTokens)
{
    // shared test
    unsigned int i = 0;
    std::vector<std::string> expectShare = {"asdqwe"};
    for (;;) {
        Token tok = lexer->Next();
        if (tok.kind == TokenKind::STRING_LITERAL) {
            if (i >= expectShare.size()) {
                break;
            }
            EXPECT_EQ(tok.Value(), expectShare[i]);
            i++;
        }
        if (tok.kind == TokenKind::END) {
            break;
        }
    }
    // private test
    i = 0;
    std::string strChar = R"("buasd" "\"" "12\u2341", 'xyz')";
    std::vector<std::string> expectPrivate = {"buasd", "\\\"", "12\\u2341", "xyz"};
    std::vector<bool> isSingleQuotes = {false, false, false, true};
    Lexer lexerstring(strChar, diag, sm);
    for (;;) {
        Token tok = lexerstring.Next();
        if (tok.kind == TokenKind::STRING_LITERAL) {
            if (i >= expectPrivate.size()) {
                break;
            }
            EXPECT_EQ(tok.Value(), expectPrivate[i]);
            EXPECT_EQ(tok.isSingleQuote, isSingleQuotes[i]);
            i++;
        }
        if (tok.kind == TokenKind::END) {
            break;
        }
    }
}

TEST_F(LexerTest, MultilineStringTokens)
{
    unsigned int i = 0;
    std::string strMulString = R"(
"""
buasd
""" """
\ """
"""

12\u2341""")";
    std::vector<std::string> expectPrivate = {"buasd\n", "\\ ", "\n12\\u2341"};
    Lexer lexerstring(strMulString, diag, sm);
    for (;;) {
        Token tok = lexerstring.Next();
        if (tok.kind == TokenKind::MULTILINE_STRING) {
            if (i >= expectPrivate.size()) {
                break;
            }
            EXPECT_EQ(tok.Value(), expectPrivate[i]);
            i++;
        }
        if (tok.kind == TokenKind::END) {
            break;
        }
    }
}

TEST_F(LexerTest, MultilineStringTokenKind)
{
    std::string strMulString = R"(
"""
${a+"_abc"}""")";
    Lexer lexerstring(strMulString, diag, sm);
    auto tok1 = lexerstring.Next();
    EXPECT_EQ(tok1.kind, TokenKind::NL);
    auto tok2 = lexerstring.Next();
    EXPECT_EQ(tok2.kind, TokenKind::MULTILINE_STRING);
}

TEST_F(LexerTest, MultilineiRawStringTokens)
{
    unsigned int i = 0;
    std::string strMulRawString = R"(
#"
buasd
"# #"\ "#
#"


"#
    )";
    std::vector<std::string> expectPrivate = {"\nbuasd\n", "\\ ", "\n\n\n"};
    Lexer lexerstring(strMulRawString, diag, sm);
    for (;;) {
        Token tok = lexerstring.Next();
        if (tok.kind == TokenKind::MULTILINE_RAW_STRING) {
            if (i >= expectPrivate.size()) {
                break;
            }
            EXPECT_EQ(tok.Value(), expectPrivate[i]);
            i++;
        }
        if (tok.kind == TokenKind::END) {
            break;
        }
    }
}

TEST_F(LexerTest, MultilineStringPositions)
{
    unsigned int i = 0;
    std::string strMulString = R"(
"""
buasd
""" """
\ """
"""

12\u2341""" """
 """)";
    std::vector<std::string> expectPrivate = {"buasd\n", "\\ ", "\n12\\u2341", " "};
    std::vector<int> expectLine = {2, 4, 6, 8};
    std::vector<int> expectColumn = {1, 5, 1, 13};
    Lexer lexerstring(strMulString, diag, sm);
    for (;;) {
        Token tok = lexerstring.Next();
        if (tok.kind == TokenKind::MULTILINE_STRING) {
            if (i >= expectPrivate.size()) {
                break;
            }
            EXPECT_EQ(tok.Value(), expectPrivate[i]);
            EXPECT_EQ(tok.Begin().line, expectLine[i]);
            EXPECT_EQ(tok.Begin().column, expectColumn[i]);
            i++;
        }
        if (tok.kind == TokenKind::END) {
            break;
        }
    }
}

TEST_F(LexerTest, MultilineiRawStringPositions)
{
    unsigned int i = 0;
    std::string strMulRawString = R"(
#"
buasd
"# #"\ "#
#"


"# #" "#
    )";
    std::vector<std::string> expectPrivate = {"\nbuasd\n", "\\ ", "\n\n\n", " "};
    std::vector<int> expectLine = {2, 4, 5, 8};
    std::vector<int> expectColumn = {1, 4, 1, 4};
    Lexer lexerstring(strMulRawString, diag, sm);
    for (;;) {
        Token tok = lexerstring.Next();
        if (tok.kind == TokenKind::MULTILINE_RAW_STRING) {
            if (i >= expectPrivate.size()) {
                break;
            }
            EXPECT_EQ(tok.Value(), expectPrivate[i]);
            EXPECT_EQ(tok.Begin().line, expectLine[i]);
            EXPECT_EQ(tok.Begin().column, expectColumn[i]);
            i++;
        }
        if (tok.kind == TokenKind::END) {
            break;
        }
    }
}

TEST_F(LexerTest, IdentifierStartWithUnderscore)
{
    std::vector<std::string> identifierCases = {"_x", "__x", "x", "x_", "x__", "x_x", "x__x"};
    for (auto& identifierCase : identifierCases) {
        Lexer lexer = Lexer(identifierCase, diag, sm);
        EXPECT_EQ(lexer.Next().kind, TokenKind::IDENTIFIER);
        EXPECT_EQ(lexer.Next().kind, TokenKind::END);
    }

    std::vector<std::string> backquoteIdentifierCases = {"`_x`", "`__x`", "`x`", "`x_`", "`x__`", "`x_x`", "`x__x`"};
    for (auto& backquoteIdentifierCase : backquoteIdentifierCases) {
        Lexer lexer = Lexer(backquoteIdentifierCase, diag, sm);
        EXPECT_EQ(lexer.Next().kind, TokenKind::IDENTIFIER);
        EXPECT_EQ(lexer.Next().kind, TokenKind::END);
    }

    // expected several wildcard
    DiagnosticEngine multiUnderscoreDiag{};
    SourceManager multiSm;
    std::string multiUnderscore = "__)";
    Lexer multiUnderscoreLexer = Lexer(multiUnderscore, multiUnderscoreDiag, multiSm);
    EXPECT_EQ(multiUnderscoreLexer.Next().kind, TokenKind::IDENTIFIER);
    EXPECT_EQ(multiUnderscoreLexer.Next().kind, TokenKind::RPAREN);
    EXPECT_EQ(multiUnderscoreLexer.Next().kind, TokenKind::END);
    EXPECT_EQ(multiUnderscoreDiag.GetErrorCount(), 0);

    // expected wild card and div
    std::string mixedIdentifier = "_/_";
    Lexer mixedLexer = Lexer(mixedIdentifier, diag, sm);
    EXPECT_EQ(mixedLexer.Next().kind, TokenKind::WILDCARD);
    EXPECT_EQ(mixedLexer.Next().kind, TokenKind::DIV);
    EXPECT_EQ(mixedLexer.Next().kind, TokenKind::WILDCARD);
    EXPECT_EQ(mixedLexer.Next().kind, TokenKind::END);
}

TEST_F(LexerTest, Identifier)
{
    unsigned int i = 0;
    std::string identifier = R"(
    abc `abc` `int`
    )";
    std::vector<std::string> expectPrivate = {"abc", "`abc`", "`int`"};
    Lexer lexer(identifier, diag, sm);
    for (;;) {
        Token tok = lexer.Next();
        if (tok.kind == TokenKind::IDENTIFIER) {
            if (i >= expectPrivate.size()) {
                break;
            }
            EXPECT_EQ(tok.Value(), expectPrivate[i]);
            i++;
        }
        if (tok.kind == TokenKind::END) {
            break;
        }
    }
}

TEST_F(LexerTest, AnnotationToken)
{
    unsigned int i = 0;
    std::vector<std::string> expectShare = {"@abc"};
    for (;;) {
        Token tok = lexer->Next();
        if (tok.kind == TokenKind::ANNOTATION) {
            if (i >= expectShare.size()) {
                break;
            }
            EXPECT_EQ(tok.Value(), expectShare[i]);
            i++;
        }
        if (tok.kind == TokenKind::END) {
            break;
        }
    }
    i = 0;
    std::string test = R"(@int @`a123` @abc @ abc @123 @_ @__ )"; // @123 is @ and 123
    std::vector<std::string> expectPrivate = {"@int", "@`a123`", "@abc", "@_", "@__"};
    Lexer lexerchar(test, diag, sm);
    for (;;) {
        Token tok = lexerchar.Next();
        if (tok.kind == TokenKind::ANNOTATION) {
            if (i >= expectPrivate.size()) {
                break;
            }
            EXPECT_EQ(tok.Value(), expectPrivate[i]);
            i++;
        }
        if (tok.kind == TokenKind::END) {
            break;
        }
    }
}
TEST_F(LexerTest, CommentTokens)
{
    // shared test
    unsigned int i = 0;
    std::vector<std::string> expectShare = {"// grh", "/*/**/*/"};
    for (;;) {
        Token tok = lexer->Next();
        if (tok.kind == TokenKind::COMMENT) {
            if (i >= expectShare.size()) {
                break;
            }
            EXPECT_EQ(tok.Value(), expectShare[i]);
            i++;
        }
        if (tok.kind == TokenKind::END) {
            break;
        }
    }
    // private test
    i = 0;
    std::string strComment = R"(
                      // &*
  /* **
  /* /&
  */
  */
)";
    std::vector<std::string> expectPrivate = {"// &*", "/* **\n  /* /&\n  */\n  */"};
    Lexer lexerstring(strComment, diag, sm);
    for (;;) {
        Token tok = lexerstring.Next();
        if (tok.kind == TokenKind::COMMENT) {
            if (i >= expectPrivate.size()) {
                break;
            }
            EXPECT_EQ(tok.Value(), expectPrivate[i]);
            i++;
        }
        if (tok.kind == TokenKind::END) {
            break;
        }
    }
}

TEST_F(LexerTest, LookAhead)
{
    // shared test
    std::vector<std::string> expectShare = {"main", "(", "argc"};
    std::vector<std::string> share;
    for (;;) {
        Token tok = lexer->Next();
        if (tok.kind == TokenKind::MAIN) {
            share.push_back(tok.Value());
            auto cache = lexer->LookAhead(2);
            for (const auto& i : cache) {
                share.push_back(i.Value());
            }
        }
        if (tok.kind == TokenKind::END) {
            break;
        }
    }
    EXPECT_EQ(expectShare.size(), share.size());
    if (expectShare.size() == share.size()) {
        EXPECT_TRUE(std::equal(expectShare.begin(), expectShare.end(), share.begin()));
    }
}

TEST_F(LexerTest, AllTokens)
{
    for (;;) {
        Token tok = lexer->Next();
        if (tok.kind == TokenKind::END) {
            break;
        }
    }
}

TEST_F(LexerTest, ErrorPrint)
{
    std::string strError = R"(
       0x
       123e
       0x1.2
       "qwer\c"
       '\u21p
       '1234'
       "12
       '\u123dfg'
       '\u1233444444'
       'u
       '\u'
       '
       '\'
       '\
       '\2
       '\u
       0b_1
       """ \abc """
       "\u"
       <:
       a._1._2._3
        `0`
        `asdf
        0x_1
        0x123.23p_1
        123._1
        0x213.123p123_2
        )";
    Lexer lexerError(strError, diag, sm);
    for (;;) {
        Token tok = lexerError.Next();
        if (tok.kind == TokenKind::END) {
            break;
        }
    }
}

TEST_F(LexerTest, TokenValues)
{
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::DOT)]) == ".");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::COMMA)]) == ",");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::LPAREN)]) == "(");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::RPAREN)]) == ")");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::LSQUARE)]) == "[");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::RSQUARE)]) == "]");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::LCURL)]) == "{");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::RCURL)]) == "}");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::EXP)]) == "**");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::MUL)]) == "*");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::MOD)]) == "%");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::DIV)]) == "/");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::ADD)]) == "+");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::SUB)]) == "-");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::INCR)]) == "++");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::DECR)]) == "--");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::AND)]) == "&&");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::OR)]) == "||");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::NOT)]) == "!");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::BITAND)]) == "&");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::BITOR)]) == "|");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::BITXOR)]) == "^");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::LSHIFT)]) == "<<");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::RSHIFT)]) == ">>");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::COLON)]) == ":");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::SEMI)]) == ";");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::ASSIGN)]) == "=");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::ADD_ASSIGN)]) == "+=");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::SUB_ASSIGN)]) == "-=");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::MUL_ASSIGN)]) == "*=");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::EXP_ASSIGN)]) == "**=");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::DIV_ASSIGN)]) == "/=");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::MOD_ASSIGN)]) == "%=");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::AND_ASSIGN)]) == "&&=");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::OR_ASSIGN)]) == "||=");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::BITAND_ASSIGN)]) == "&=");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::BITOR_ASSIGN)]) == "|=");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::BITXOR_ASSIGN)]) == "^=");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::LSHIFT_ASSIGN)]) == "<<=");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::RSHIFT_ASSIGN)]) == ">>=");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::ARROW)]) == "->");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::DOUBLE_ARROW)]) == "=>");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::RANGEOP)]) == "..");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::CLOSEDRANGEOP)]) == "..=");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::ELLIPSIS)]) == "...");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::HASH)]) == "#");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::AT)]) == "@");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::QUEST)]) == "?");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::LT)]) == "<");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::GT)]) == ">");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::LE)]) == "<=");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::GE)]) == ">=");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::NOTEQ)]) == "!=");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::EQUAL)]) == "==");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::WILDCARD)]) == "_");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::INT8)]) == "Int8");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::INT16)]) == "Int16");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::INT32)]) == "Int32");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::INT64)]) == "Int64");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::UINT8)]) == "UInt8");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::UINT16)]) == "UInt16");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::UINT32)]) == "UInt32");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::UINT64)]) == "UInt64");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::FLOAT16)]) == "Float16");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::FLOAT32)]) == "Float32");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::FLOAT64)]) == "Float64");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::RUNE)]) == "Rune");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::BOOLEAN)]) == "Bool");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::UNIT)]) == "Unit");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::STRUCT)]) == "struct");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::ENUM)]) == "enum");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::THISTYPE)]) == "This");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::PACKAGE)]) == "package");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::IMPORT)]) == "import");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::CLASS)]) == "class");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::INTERFACE)]) == "interface");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::FUNC)]) == "func");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::LET)]) == "let");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::VAR)]) == "var");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::TYPE)]) == "type");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::INIT)]) == "init");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::THIS)]) == "this");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::SUPER)]) == "super");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::IF)]) == "if");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::ELSE)]) == "else");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::CASE)]) == "case");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::TRY)]) == "try");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::CATCH)]) == "catch");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::HANDLE)]) == "handle");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::FINALLY)]) == "finally");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::FOR)]) == "for");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::DO)]) == "do");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::WHILE)]) == "while");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::THROW)]) == "throw");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::PERFORM)]) == "perform");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::RESUME)]) == "resume");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::RETURN)]) == "return");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::CONTINUE)]) == "continue");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::BREAK)]) == "break");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::AS)]) == "as");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::IN)]) == "in");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::NOT_IN)]) == "!in");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::MATCH)]) == "match");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::WHERE)]) == "where");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::WITH)]) == "with");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::THROWING)]) == "throwing");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::STATIC)]) == "static");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::PUBLIC)]) == "public");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::PRIVATE)]) == "private");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::PROTECTED)]) == "protected");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::ENUM)]) == "enum");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::OVERRIDE)]) == "override");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::ABSTRACT)]) == "abstract");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::OPEN)]) == "open");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::OPERATOR)]) == "operator");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::UPPERBOUND)]) == "<:");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::SPAWN)]) == "spawn");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::DOUBLE_COLON)]) == "::");
    ASSERT_TRUE(std::string(TOKENS[static_cast<int>(TokenKind::FEATURES)]) == "features");
    ASSERT_EQ(static_cast<unsigned int>(TokenKind::FEATURES) + 1, sizeof(TOKENS) / sizeof(*TOKENS));
}

TEST_F(LexerTest, TokenKindValues)
{
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::DOT)]) == "dot");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::COMMA)]) == "comma");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::LPAREN)]) == "l_paren");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::RPAREN)]) == "r_paren");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::LSQUARE)]) == "l_square");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::RSQUARE)]) == "r_square");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::LCURL)]) == "l_curl");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::RCURL)]) == "r_curl");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::EXP)]) == "exp");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::MUL)]) == "mul");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::MOD)]) == "mod");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::DIV)]) == "div");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::ADD)]) == "add");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::SUB)]) == "sub");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::INCR)]) == "incr");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::DECR)]) == "decr");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::AND)]) == "and");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::OR)]) == "or");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::NOT)]) == "not");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::BITAND)]) == "bit_and");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::BITOR)]) == "bit_or");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::BITXOR)]) == "bit_xor");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::LSHIFT)]) == "lshift");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::RSHIFT)]) == "rshift");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::COLON)]) == "colon");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::SEMI)]) == "semi");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::ASSIGN)]) == "assign");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::ADD_ASSIGN)]) == "add_assign");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::SUB_ASSIGN)]) == "sub_assign");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::MUL_ASSIGN)]) == "mul_assign");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::EXP_ASSIGN)]) == "exp_assign");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::DIV_ASSIGN)]) == "div_assign");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::MOD_ASSIGN)]) == "mod_assign");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::AND_ASSIGN)]) == "and_assign");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::OR_ASSIGN)]) == "or_assign");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::BITAND_ASSIGN)]) == "bit_and_assign");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::BITOR_ASSIGN)]) == "bit_or_assign");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::BITXOR_ASSIGN)]) == "bit_xor_assign");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::LSHIFT_ASSIGN)]) == "lshift_assign");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::RSHIFT_ASSIGN)]) == "rshift_assign");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::ARROW)]) == "arrow");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::DOUBLE_ARROW)]) == "double_arrow");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::RANGEOP)]) == "range_op");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::HASH)]) == "hash");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::AT)]) == "at");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::QUEST)]) == "quest");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::LT)]) == "less");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::GT)]) == "greater");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::LE)]) == "less_equal");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::GE)]) == "greater_equal");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::NOTEQ)]) == "not_equal");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::EQUAL)]) == "equal");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::WILDCARD)]) == "wildcard");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::INT8)]) == "Int8");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::INT16)]) == "Int16");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::INT32)]) == "Int32");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::INT64)]) == "Int64");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::UINT8)]) == "UInt8");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::UINT16)]) == "UInt16");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::UINT32)]) == "UInt32");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::UINT64)]) == "UInt64");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::FLOAT16)]) == "Float16");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::FLOAT32)]) == "Float32");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::FLOAT64)]) == "Float64");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::RUNE)]) == "Rune");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::BOOLEAN)]) == "Bool");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::UNIT)]) == "Unit");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::STRUCT)]) == "struct");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::ENUM)]) == "enum");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::THISTYPE)]) == "This");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::PACKAGE)]) == "package");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::IMPORT)]) == "import");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::CLASS)]) == "class");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::INTERFACE)]) == "interface");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::FUNC)]) == "func");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::LET)]) == "let");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::VAR)]) == "var");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::TYPE)]) == "type");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::INIT)]) == "init");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::THIS)]) == "this");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::SUPER)]) == "super");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::IF)]) == "if");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::ELSE)]) == "else");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::CASE)]) == "case");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::TRY)]) == "try");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::CATCH)]) == "catch");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::HANDLE)]) == "handle");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::FINALLY)]) == "finally");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::FOR)]) == "for");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::DO)]) == "do");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::WHILE)]) == "while");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::THROW)]) == "throw");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::RETURN)]) == "return");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::CONTINUE)]) == "continue");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::BREAK)]) == "break");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::AS)]) == "as");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::IN)]) == "in");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::NOT_IN)]) == "not_in");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::MATCH)]) == "match");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::WHERE)]) == "where");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::WITH)]) == "with");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::STATIC)]) == "static");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::PUBLIC)]) == "public");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::PRIVATE)]) == "private");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::PROTECTED)]) == "protected");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::ENUM)]) == "enum");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::OVERRIDE)]) == "override");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::ABSTRACT)]) == "abstract");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::OPEN)]) == "open");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::OPERATOR)]) == "operator");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::UPPERBOUND)]) == "upperbound");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::IDENTIFIER)]) == "identifier");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::INTEGER_LITERAL)]) == "integer_literal");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::FLOAT_LITERAL)]) == "float_literal");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::COMMENT)]) == "comment");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::NL)]) == "newline");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::END)]) == "end");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::SENTINEL)]) == "sentinel");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::RUNE_LITERAL)]) == "char_literal");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::STRING_LITERAL)]) == "string_literal");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::MULTILINE_STRING)]) == "multiline_string");
    ASSERT_TRUE(
        std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::MULTILINE_RAW_STRING)]) == "multiline_raw_string");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::BOOL_LITERAL)]) == "bool_literal");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::DOLLAR_IDENTIFIER)]) == "dollar_identifier");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::ANNOTATION)]) == "annotation");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::ILLEGAL)]) == "illegal");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::DOUBLE_COLON)]) == "double_colon");
    ASSERT_TRUE(std::string(TOKEN_KIND_VALUES[static_cast<int>(TokenKind::FEATURES)]) == "features");
    ASSERT_EQ(
        static_cast<unsigned int>(TokenKind::FEATURES) + 1, sizeof(TOKEN_KIND_VALUES) / sizeof(*TOKEN_KIND_VALUES));
}

TEST_F(LexerTest, UTF8ToCodepoint)
{
    std::string utf8 = R"(中文\u{4e2d}\t)";
    std::vector<uint32_t> codepoint = {20013, 25991, 20013, 9};
    auto vec = StringConvertor::UTF8ToCodepoint(StringConvertor::Normalize(utf8));
    ASSERT_TRUE(std::equal(vec.begin(), vec.end(), codepoint.begin()));
}

TEST_F(LexerTest, Diagnose)
{
    std::string str = R"(abc def)";
    auto fileid = sm.AddSource("test", str);
    Lexer lexer(fileid, str, diag, sm);
    Token tok = lexer.Next();
    EXPECT_EQ(tok.End(), Position(1, 1, 4));
    DiagnosticEngine diag;
    diag.DiagnoseRefactor(DiagKindRefactor::parse_this_type_not_allow, tok.Begin());
}

TEST_F(LexerTest, PrintAsciiControlCode)
{
    std::string str = "\r";
    DiagnosticEngine diag{};
    SourceManager sm;
    auto fileid = sm.AddSource("test.cj", str);
    diag.SetSourceManager(&sm);
    Lexer lexer(fileid, str, diag, sm);
    Token tok = lexer.Next();
    lexer.Next();
    auto diagnostics = diag.GetCategoryDiagnostic(DiagCategory::LEX);
    EXPECT_EQ(diagnostics.size(), 1);

    std::ostringstream output;
    DiagnosticEmitter(diagnostics[0], false, true, output, sm).Emit();
    auto splits = Utils::SplitLines(output.str());
    EXPECT_EQ(splits[3].substr(22, 8), "\x1b[30;47m");
    EXPECT_EQ(splits[3].substr(30, 8), "\\u{000D}");
    EXPECT_EQ(splits[3].substr(38, 4), "\x1b[0m");
}
