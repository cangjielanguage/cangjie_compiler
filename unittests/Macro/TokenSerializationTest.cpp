// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include <string>
#include "gtest/gtest.h"
#include "cangjie/Macro/TokenSerialization.h"
#include "cangjie/Lex/Lexer.h"
using namespace Cangjie;

class TokenSerializationTest : public testing::Test {
protected:
    void SetUp() override
    {
    }
    std::string code = R"(
?:
    true false
    main(argc:Int64=1, argv:String) {
    let a:Int64=-40
    let pi:float64=3.14
    let alpha=0x1.1p1
    let c:char = '\''
    // grh
    /*/**/*/
    let d:String = "asdqwe"
    let b = 2 ** -a
    print((a+3*b, (a+3) *b))
    @abc
    };
)";
    DiagnosticEngine diag{};
    SourceManager sm;
};

TEST_F(TokenSerializationTest, BufferCase)
{
    Lexer lexer(code, diag, sm);
    std::vector<Token> tokens{};
    for (;;) {
        Token tok = lexer.Next();
        tokens.emplace_back(tok);
        if (tok.kind == TokenKind::END) {
            break;
        }
    }
    std::vector<uint8_t> buf = TokenSerialization::GetTokensBytes(tokens);
    std::vector<Token> backTokens = TokenSerialization::GetTokensFromBytes(buf.data());
    EXPECT_EQ(tokens.size(), 93);
    ASSERT_TRUE(tokens.size() == backTokens.size());
    for (int i = 0; i < 93; ++i) {
        EXPECT_EQ(tokens[i].kind, backTokens[i].kind);
        EXPECT_EQ(tokens[i].Value(), backTokens[i].Value());
        EXPECT_EQ(tokens[i].Begin(), backTokens[i].Begin());
    }
}
