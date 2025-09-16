// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 * This file implements quote parse apis.
 */

#include "ParserImpl.h"

#include "cangjie/Basic/SourceManager.h"
#include "cangjie/Basic/StringConvertor.h"
#include "cangjie/AST/Create.h"

using namespace Cangjie;
using namespace Cangjie::AST;

void ParserImpl::ParseQuoteDollarInterpolationWithParen(AST::QuoteExpr& qe)
{
    const static std::vector<ASTKind> INVALID_AST_KIND = {ASTKind::INVALID_EXPR, ASTKind::RETURN_EXPR,
        ASTKind::WHILE_EXPR, ASTKind::DO_WHILE_EXPR, ASTKind::FOR_IN_EXPR, ASTKind::THROW_EXPR, ASTKind::TRY_EXPR,
        ASTKind::JUMP_EXPR};
    auto dol = lookahead;
    Skip(TokenKind::DOLLAR);
    auto dollarPos = lastToken.Begin();
    Skip(TokenKind::LPAREN);
    auto lpPos = lastToken.Begin();
    skipNL = true;
    OwnedPtr<Expr> expr = ParseExpr();
    CheckMacroExprRules(dol, Token{TokenKind::DOT}, *expr);
    skipNL = false;
    if (Utils::In(expr->astKind, INVALID_AST_KIND)) {
        ParseDiagnoseRefactor(DiagKindRefactor::parse_invalid_quote_dollar_expr, dol);
    }
    // add a level of ParenExpr to store the location of '$()', to be used by std.syntax
    // cjc/fmt/lsp does not need it
    auto quoteExprWrapper = MakeOwned<ParenExpr>();
    quoteExprWrapper->expr = std::move(expr);
    quoteExprWrapper->begin = dollarPos;
    quoteExprWrapper->leftParenPos = lpPos;
    quoteExprWrapper->rightParenPos = lookahead.Begin();
    quoteExprWrapper->end = lookahead.End();
    qe.exprs.emplace_back(std::move(quoteExprWrapper));
    if (!Seeing(TokenKind::RPAREN)) {
        DiagExpectedRightDelimiter("(", dol.Begin());
    }
}

void ParserImpl::ParseQuoteDollarInterpolation(AST::QuoteExpr& qe)
{
    // Support '$ast' use case.
    auto token = Peek();
    auto ident = token.Value().substr(1, token.Value().length() - 1);
    bool isRawId = IsRawIdentifier(ident);
    if (isRawId) {
        ident = ParseNameFromRawIdentifier(ident);
    }
    auto ref = CreateRefExpr({ident, token.Begin(), token.End(), isRawId}, nullptr, token.Begin());
    ref->begin.column += 1;
    ref->end = token.End();
    ref->isQuoteDollar = true;
    ref->EnableAttr(Attribute::COMPILER_ADD);
    qe.exprs.emplace_back(std::move(ref));
}

void ParserImpl::ParseQuoteEscapeToken(std::vector<Token>& tokens)
{
    const static std::vector<TokenKind> ESCAPE_TOKEN_KINDS = {
        TokenKind::DOLLAR_IDENTIFIER, TokenKind::AT, TokenKind::DOLLAR, TokenKind::LPAREN, TokenKind::RPAREN};
    // In Lex/Lexer.cpp, IsMacroEscape() function, escaping of [ and ] is used in macro attribute input
    const static std::vector<TokenKind> ESCAPE_FOR_MACRO_NOT_FOR_QUOTE = {
        TokenKind::LSQUARE, TokenKind::RSQUARE};
    Skip(TokenKind::ILLEGAL);
    if (SeeingAny(ESCAPE_TOKEN_KINDS)) {
        auto token = Peek();
        tokens.emplace_back(token.kind, token.Value(), token.Begin(), token.End());
    } else if (SeeingAny(ESCAPE_FOR_MACRO_NOT_FOR_QUOTE)) {
        auto builder = diag.DiagnoseRefactor(
            DiagKindRefactor::lex_unknown_start_of_token, lastToken.Begin(), lastToken.Value());
        builder.AddMainHintArguments(lastToken.Value());
        tokens.emplace_back(lastToken);
    } else {
        tokens.emplace_back(lastToken);
    }
}

static void GenerateTokenPart(QuoteExpr& qe, std::vector<Token>& tokens)
{
    if (tokens.empty()) {
        return;
    }
    qe.exprs.emplace_back(MakeOwned<TokenPart>(tokens));
    tokens.clear();
}

void ParserImpl::ParseQuoteTokens(QuoteExpr& qe)
{
    std::vector<Token> tokens;
    auto lparenCnt = 1 - static_cast<int>(Seeing(TokenKind::RPAREN));
    while (lparenCnt > 0) {
        if (Seeing(TokenKind::END)) {
            break;
        }
        if (Seeing(TokenKind::ILLEGAL) && lookahead == "\\") {
            ParseQuoteEscapeToken(tokens);
        } else if (Seeing({TokenKind::DOLLAR, TokenKind::LPAREN})) {
            GenerateTokenPart(qe, tokens);
            ParseQuoteDollarInterpolationWithParen(qe);
        } else if (Seeing(TokenKind::DOLLAR_IDENTIFIER)) {
            GenerateTokenPart(qe, tokens);
            ParseQuoteDollarInterpolation(qe);
        } else {
            if (Seeing(TokenKind::LPAREN)) {
                lparenCnt++;
            } else if (Seeing(TokenKind::DOLLAR)) {
                ParseDiagnoseRefactor(DiagKindRefactor::parse_expect_escape_dollar_token, lookahead.Begin());
            }
            if (SkipAmbiguousToken() || SkipCombinedDoubleArrow()) {
                tokens.emplace_back(lastToken);
                lparenCnt -= static_cast<int>(Seeing(TokenKind::RPAREN));
                continue;
            }
            auto token = Peek();
            tokens.emplace_back(token);
        }
        Next();
        lparenCnt -= static_cast<int>(Seeing(TokenKind::RPAREN));
    }
    GenerateTokenPart(qe, tokens);
}
