// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the Parser utils function.
 */

#include "ParserImpl.h"

#include "cangjie/AST/Match.h"
#include "cangjie/Utils/Unicode.h"

namespace Cangjie {
const static std::unordered_map<TokenKind, TokenKind> MATCHING_OPENING_BRACKET_OF{{TokenKind::RCURL, TokenKind::LCURL},
    {TokenKind::RSQUARE, TokenKind::LSQUARE}, {TokenKind::RPAREN, TokenKind::LPAREN}};

const std::vector<TokenKind>& GetContextualKeyword()
{
    static const std::vector<TokenKind> CONTEXTUAL_KEYWORD_TOKEN = {
        TokenKind::PUBLIC, TokenKind::PRIVATE, TokenKind::PROTECTED, TokenKind::OVERRIDE, TokenKind::ABSTRACT,
        TokenKind::SEALED, TokenKind::OPEN,    TokenKind::REDEF,     TokenKind::INTERNAL
    };
    return CONTEXTUAL_KEYWORD_TOKEN;
}
const std::vector<TokenKind>& GetTypeFirst()
{
    static const std::vector<TokenKind> TYPE_FIRST_TOKEN = {
        TokenKind::INT8,     TokenKind::INT16,   TokenKind::INT32,   TokenKind::INT64,  TokenKind::INTNATIVE,
        TokenKind::UINT8,    TokenKind::UINT16,  TokenKind::UINT32,  TokenKind::UINT64, TokenKind::UINTNATIVE,
        TokenKind::FLOAT16,  TokenKind::FLOAT32, TokenKind::FLOAT64, TokenKind::RUNE,   TokenKind::BOOLEAN,
        TokenKind::NOTHING,  TokenKind::UNIT,    TokenKind::QUEST,   TokenKind::LPAREN, TokenKind::IDENTIFIER,
        TokenKind::THISTYPE, TokenKind::VARRAY,
    };
    return TYPE_FIRST_TOKEN;
}
const Token& ParserImpl::Peek()
{
    if (lookahead.kind != TokenKind::SENTINEL) {
        return lookahead;
    }
    deadlocked = false;
    bool firstNLFlag = true;
    // skip comments, newlines (conditionally)
    // as part of this, also keep `bracketsStack` up-to-date
    do {
        lookahead = lexer->Next();
        if (calculateLineNum) {
            allTokensInOneFile.emplace_back(lookahead.Begin().line, lookahead.End().line);
        }
        if (lookahead.kind == TokenKind::LCURL || lookahead.kind == TokenKind::LPAREN ||
            lookahead.kind == TokenKind::LSQUARE) {
            bracketsStack.push_back(lookahead.kind);
        }
        auto matchingBracket = MATCHING_OPENING_BRACKET_OF.find(lookahead.kind);
        if (matchingBracket != MATCHING_OPENING_BRACKET_OF.end()) {
            if (!bracketsStack.empty() && matchingBracket->second == bracketsStack.back()) {
                bracketsStack.pop_back();
            }
        }
        if (lookahead.kind == TokenKind::NL) {
            newlineSkipped = true;
            if (firstNLFlag) {
                firstNLPosition = lookahead.Begin();
                firstNLFlag = false;
            }
        }
    } while (lookahead.kind == TokenKind::COMMENT || (lookahead.kind == TokenKind::NL && skipNL));
    // If reach end of file, the end pos should be adjacent with last non-comment token.
    if (lookahead.kind == TokenKind::END && !lastToken.Begin().IsZero()) {
        lookahead.SetValuePos(lookahead.Value(), lastToken.End(), lastToken.End());
    }
    return lookahead;
}

void ParserImpl::Next()
{
    // Ignore all comment.
    if (lookahead.kind != TokenKind::COMMENT && lookahead.kind != TokenKind::END) {
        lastToken = lookahead;
        if (lookahead.kind != TokenKind::NL) {
            lastNoneNLToken = lookahead;
        }
    }
    newlineSkipped = false;
    Peek();
    bool firstNLFlag = true;
    if (lookahead.kind == TokenKind::NL) {
        firstNLPosition = lookahead.Begin();
        firstNLFlag = false;
        newlineSkipped = true;
    }

    while ((skipNL && lexer->Seeing({TokenKind::NL})) || lexer->Seeing({TokenKind::COMMENT})) {
        if (lexer->Seeing({TokenKind::NL})) {
            if (firstNLFlag) {
                firstNLPosition = lexer->LookAhead(1).begin()->Begin();
                firstNLFlag = false;
            }
            newlineSkipped = true;
        }
        lexer->Next();
    }
    lookahead.kind = TokenKind::SENTINEL;
}

bool ParserImpl::Seeing(const std::vector<TokenKind>& kinds, bool skipNewline)
{
    if (lookahead.kind != TokenKind::SENTINEL) {
        if (lookahead.kind == kinds[0]) {
            return lexer->Seeing(kinds.begin() + 1, kinds.end(), skipNewline);
        }
        return false;
    } else {
        return lexer->Seeing(kinds, skipNewline);
    }
}

bool ParserImpl::SeeingExpr()
{
    static const std::vector<TokenKind> exprFirstToken = {
        TokenKind::SUB,
        TokenKind::NOT,
        TokenKind::IF,
        TokenKind::MATCH,
        TokenKind::QUOTE,
        TokenKind::TRY,
        TokenKind::THROW,
        TokenKind::PERFORM,
        TokenKind::RESUME,
        TokenKind::RETURN,
        TokenKind::CONTINUE,
        TokenKind::BREAK,
        TokenKind::FOR,
        TokenKind::WHILE,
        TokenKind::DO,
        TokenKind::SPAWN,
        TokenKind::SYNCHRONIZED,
        TokenKind::LPAREN,
        TokenKind::LCURL,
        TokenKind::LSQUARE,
        TokenKind::THIS,
        TokenKind::SUPER,
        TokenKind::IDENTIFIER,
        TokenKind::UNSAFE,
        TokenKind::WILDCARD,
        TokenKind::VARRAY,
    };
    if (SeeingAny(exprFirstToken)) {
        return true;
    }
    return SeeingLiteral() || SeeingPrimitiveTypeAndLParen() || SeeingPrimitiveTypeAndDot() || SeeingMacroCall() ||
        SeeingBuiltinAnnotation();
}

bool ParserImpl::SeeingCombinator(const std::vector<TokenKind>& kinds)
{
    if (Seeing(kinds, false)) {
        std::vector<Token> toks = {lookahead};
        auto ahead = lexer->LookAheadSkipNL(kinds.size() - 1);
        toks.insert(toks.end(), ahead.begin(), ahead.end());
        for (size_t i = 1; i < kinds.size(); ++i) {
            if (toks[i].Begin().line != toks[i - 1].Begin().line ||
                toks[i].Begin().column != toks[i - 1].Begin().column + 1) {
                return false;
            }
        }
        return true;
    }
    return false;
}

bool ParserImpl::SeeingTokenAndCombinator(TokenKind kind, const std::vector<TokenKind>& cmb)
{
    if (!Seeing(kind)) {
        return false;
    }
    auto ahead = lexer->LookAheadSkipNL(cmb.size());
    std::vector<Token> toks{ahead.begin(), ahead.end()};
    if (cmb.size() != toks.size()) {
        return false;
    }
    CJC_ASSERT(cmb.size() > 1);
    for (size_t i = 0; i < cmb.size(); ++i) {
        if (toks[i].kind != cmb[i]) {
            return false;
        }
    }
    for (size_t i = 1; i < toks.size(); ++i) {
        if (toks[i].Begin().line != toks[i - 1].Begin().line ||
            toks[i].Begin().column != toks[i - 1].Begin().column + 1) {
            return false;
        }
    }
    return true;
}

void ParserImpl::SkipCombinator(const std::vector<TokenKind>& kinds)
{
    if (SeeingCombinator(kinds)) {
        auto i = kinds.size();
        while (i-- > 0) {
            Next();
        }
    }
}

bool ParserImpl::SkipAmbiguousToken()
{
    for (const auto &[tar, comb, val] : ambiguoussCombinedTokens) {
        if (SeeingCombinator(comb)) {
            size_t i = comb.size();
            while (i-- != 0) {
                Next();
            }
            lastToken = Token(tar, val, lookahead.Begin() - (val.size() - 1), lookahead.Begin() - 1);
            return true;
        }
    }
    return false;
}

bool ParserImpl::SkipCombinedDoubleArrow()
{
    if (SeeingCombinator(combinedDoubleArrow)) {
        auto i = combinedDoubleArrow.size();
        while (i-- > 0) {
            Next();
        }
        lastToken = Token(TokenKind::DOUBLE_ARROW, "=>", lastToken.Begin(),
            lastToken.Begin() + std::string_view{"=>"}.size());
        return true;
    }
    return false;
}

bool ParserImpl::SkipCombinedBackarrow()
{
    if (SeeingCombinator(combinedBackarrow)) {
        auto i = combinedBackarrow.size();
        while (i-- > 0) {
            Next();
        }
        lastToken = Token{TokenKind::BACKARROW, "<-", lastToken.Begin(),
            lastToken.Begin() + std::string_view{"<-"}.size()};
        return true;
    }
    return false;
}

bool ParserImpl::Skip(TokenKind kind)
{
    if (Peek().kind == kind) {
        Next();
        return true;
    } else {
        return false;
    }
}

void ParserImpl::ParseOneOrMoreWithSeparator(
    TokenKind separator, std::vector<Position>& positions, const std::function<void()>& parseElement)
{
    parseElement();
    while (Skip(separator)) {
        positions.push_back(lastToken.Begin());
        parseElement();
    }
}

void ParserImpl::ParseOneOrMoreWithSeparator(
    TokenKind separator, const std::function<void(const Position)>& storeSeparator,
    const std::function<void()>& parseElement)
{
    parseElement();
    while (Skip(separator)) {
        storeSeparator(lastToken.Begin());
        parseElement();
    }
}

void ParserImpl::ParseOneOrMoreSepTrailing(std::function<void(const Position&)>&& storeSeparator,
    std::function<void()>&& parseElement, TokenKind end, TokenKind separator)
{
    do {
        parseElement();
        if (Skip(separator)) {
            storeSeparator(lastToken.Begin());
        } else {
            break;
        }
    } while (!Seeing(end));
}

void ParserImpl::ParseZeroOrMoreSepTrailing(std::function<void(const Position&)>&& storeSeparator,
    std::function<void()>&& parseElement, TokenKind end, TokenKind separator)
{
    while (!Seeing(end)) {
        parseElement();
        if (Skip(separator)) {
            storeSeparator(lastToken.Begin());
        } else {
            break;
        }
    }
}

void ParserImpl::ParseZeroOrMoreWithSeparator(TokenKind separator, std::vector<Position>& positions,
    const std::function<void()>& parseElement, TokenKind terminator)
{
    if (!Seeing(terminator)) {
        ParseOneOrMoreWithSeparator(separator, positions, parseElement);
    }
}

void ParserImpl::ParseZeroOrMoreWithSeparator(TokenKind separator,
    const std::function<void(const Position)>& storeSeparator, const std::function<void()>& parseElement,
    TokenKind terminator)
{
    if (!Seeing(terminator)) {
        ParseOneOrMoreWithSeparator(separator, storeSeparator, parseElement);
    }
}

bool ParserImpl::CanMatchBracketInStack()
{
    auto matchingBracket = MATCHING_OPENING_BRACKET_OF.find(lookahead.kind);
    if (matchingBracket != MATCHING_OPENING_BRACKET_OF.end()) {
        auto it = std::find(bracketsStack.begin(), bracketsStack.end(), matchingBracket->second);
        if (it == bracketsStack.end()) {
            return false;
        } else {
            bracketsStack.erase(it, bracketsStack.end());
        }
        return true;
    }
    return false;
}

void ParserImpl::SpreadAttrAndConsume(Ptr<const AST::Node> source, Ptr<AST::Node> target, std::vector<TokenKind>&& kind)
{
    if (source->TestAttr(AST::Attribute::IS_BROKEN)) {
        target->EnableAttr(AST::Attribute::IS_BROKEN);
        TryConsumeUntilAny(std::move(kind));
    }
}

void ParserImpl::SkipPairedBrackets()
{
    if (Seeing(TokenKind::LPAREN)) {
        Next();
        ConsumeUntil(TokenKind::RPAREN, false);
    } else if (Seeing(TokenKind::LCURL)) {
        Next();
        ConsumeUntil(TokenKind::RCURL, false);
    } else if (Seeing(TokenKind::LSQUARE)) {
        Next();
        ConsumeUntil(TokenKind::RSQUARE, false);
    }
}
/*
 * Consume tokens until start of declaration.
 */
void ParserImpl::ConsumeUntilDecl(TokenKind kind)
{
    while (!SeeingDecl() && !SeeingMacroCallDecl() && !Seeing(kind)) {
        if (Seeing(TokenKind::END)) {
            break;
        }
        SkipPairedBrackets();
        Next();
    }
}

bool ParserImpl::ConsumeCommon(bool& flag)
{
    if (Seeing(TokenKind::NL)) {
        flag = true;
    }
    if (Seeing(TokenKind::END)) {
        return false;
    }
    SkipPairedBrackets();
    skipNL = false;
    Next();
    return true;
}

/*
 * Consume tokens until start of declaration.
 */
void ParserImpl::ConsumeUntilDeclOrNL(TokenKind kind)
{
    skipNL = false;
    bool flag = false;
    while (!Seeing(TokenKind::NL) && !SeeingDecl() && !SeeingMacroCallDecl() && !Seeing(kind)) {
        if (!ConsumeCommon(flag)) {
            break;
        }
    }
    if (flag) {
        newlineSkipped = true;
    }
    skipNL = true;
}
/*
 * Consume tokens until target token, and if newline consumed the skipNL will be true.
 */
void ParserImpl::ConsumeUntil(TokenKind kind, bool targetTokenConsumed)
{
    skipNL = false;
    bool flag = false;
    while (!Seeing(kind)) {
        if (!ConsumeCommon(flag)) {
            break;
        }
    }
    if (targetTokenConsumed) {
        if (Seeing(TokenKind::NL)) {
            flag = true;
        }
        Next();
        Peek();
    }
    if (flag) {
        newlineSkipped = true;
    }
    skipNL = true;
}

// Try to consume target tokens, will only lookahead 3 tokens (ignore NL) by default.
// If found target tokens, consume until it, otherwise do nothing.
bool ParserImpl::TryConsumeUntilAny(std::vector<TokenKind> tokens)
{
    const size_t len = 3;
    Peek();
    std::vector<Token> predictToken = {lookahead};
    auto ahead = lexer->LookAheadSkipNL(len - 1);
    predictToken.insert(predictToken.end(), ahead.begin(), ahead.end());

    auto iter = std::find_if(predictToken.begin(), predictToken.end(), [&tokens](auto& token) {
        return std::any_of(tokens.begin(), tokens.end(), [&token](TokenKind kind) { return token.kind == kind; });
    });
    if (iter == predictToken.end()) {
        return false;
    }

    ptrdiff_t loc = iter - predictToken.begin();

    while (loc-- > 0) {
        Next();
        Peek();
    }
    return true;
}

void ParserImpl::TargetTokenConsumedControl(bool& flag, bool targetTokenConsumed)
{
    if (Seeing(TokenKind::NL)) {
        flag = true;
        Next();
    } else if (targetTokenConsumed) {
        Next();
    }
    if (flag) {
        newlineSkipped = true;
    }
    skipNL = true;
}

/*
 * Consume tokens until target tokens, and if newline consumed the skipNL will be true.
 * If token NL is among target tokens, it will be always consumed.
 */
void ParserImpl::ConsumeUntilAny(std::vector<TokenKind>&& tokens, bool targetTokenConsumed)
{
    skipNL = false;
    bool flag = false;
    while (std::none_of(tokens.begin(), tokens.end(), [this](TokenKind kind) { return kind == Peek().kind; })) {
        if (!ConsumeCommon(flag)) {
            break;
        }
    }
    TargetTokenConsumedControl(flag, targetTokenConsumed);
}

void ParserImpl::ConsumeUntilAny(const std::function<bool()>& functor, bool targetTokenConsumed)
{
    skipNL = false;
    bool flag = false;
    while (!functor()) {
        if (!ConsumeCommon(flag)) {
            break;
        }
    }
    TargetTokenConsumedControl(flag, targetTokenConsumed);
}

/*
case1: LevenshteinDistance("lassB", "classB") => 1
case2: LevenshteinDistance("main", "intmian") => 5
*/
unsigned LevenshteinDistance(const std::string& source, const std::string& target)
{
    unsigned m = static_cast<unsigned>(source.length());
    unsigned n = static_cast<unsigned>(target.length());
    if (n == 0) {
        return n;
    }
    unsigned* dp = new unsigned[n + 1];
    unsigned result;
    // although using std::min on unsigned is effectively noexcept, string::operator[] may throws an exception when
    // index out of range, so a try block is required
#ifndef CANGJIE_ENABLE_GCOV
    try {
#endif
        for (unsigned j = 0; j < n + 1; j++) {
            dp[j] = j;
        }

        for (unsigned x = 1; x <= m; x++) {
            dp[0] = x;
            unsigned upperLeft = x - 1;
            for (unsigned y = 1; y <= n; y++) {
                unsigned previousRowDpY = dp[y];
                dp[y] = std::min(upperLeft + (source[x - 1] == target[y - 1] ? 0 : 1), std::min(dp[y], dp[y - 1]) + 1);
                upperLeft = previousRowDpY;
            }
        }

        result = dp[n];
#ifndef CANGJIE_ENABLE_GCOV
    } catch (...) {
        delete[] dp;
        throw;
    }
#endif
    delete[] dp;
    return result;
}

void ParserImpl::ImplementConsumeStrategy(ScopeKind sc)
{
    auto normalDeclConsume = [this]() -> bool { return SeeingDecl() || Seeing(TokenKind::RCURL); };
    auto normalFuncBodyConsume = [this]() -> bool { return SeeingDecl() || SeeingExpr() || Seeing(TokenKind::RCURL); };
    auto normalEnumBodyConsume = [this]() -> bool {
        return SeeingDecl() || Seeing(TokenKind::BITOR) || Seeing(TokenKind::IDENTIFIER) || Seeing(TokenKind::RCURL);
    };
    switch (sc) {
        case ScopeKind::TOPLEVEL: {
            ConsumeUntilDecl();
            ConsumeUntilAny(normalFuncBodyConsume, false);
            break;
        }
        case ScopeKind::EXTEND_BODY:
        case ScopeKind::STRUCT_BODY:
        case ScopeKind::INTERFACE_BODY:
        case ScopeKind::CLASS_BODY: {
            ConsumeUntilAny(normalDeclConsume, false);
            break;
        }
        case ScopeKind::FUNC_BODY:
        case ScopeKind::MACRO_BODY:
        case ScopeKind::PRIMARY_CONSTRUCTOR_BODY_FOR_CLASS:
        case ScopeKind::PRIMARY_CONSTRUCTOR_BODY_FOR_STRUCT:
        case ScopeKind::PROP_MEMBER_GETTER_BODY:
        case ScopeKind::PROP_MEMBER_SETTER_BODY: {
            ConsumeUntilAny(normalFuncBodyConsume, false);
            break;
        }
        case ScopeKind::ENUM_BODY: {
            ConsumeUntilAny(normalEnumBodyConsume, false);
            break;
        }
        default:
            break;
    }
}

bool ParserImpl::SeeingPrimaryIdentifer()
{
    return SeeingIdentifierAndTargetOp({TokenKind::LT, TokenKind::LPAREN, TokenKind::LSQUARE, TokenKind::LCURL}) &&
        LevenshteinDistance(lookahead.Value(), GetPrimaryDeclIdentRawValue()) <= 1;
}

bool ParserImpl::IsRawIdentifier(const std::string& identifier) const
{
    if (identifier.size() <= std::string("``").size()) {
        return false;
    }
    return identifier.at(0) == '`';
}

std::string ParserImpl::ParseNameFromRawIdentifier(const std::string& rawIdentifier) const
{
    return rawIdentifier.substr(1, rawIdentifier.size() - std::string("``").size());
}

SrcIdentifier ParserImpl::ParseIdentifierFromToken(const Token& token) const
{
    size_t len{static_cast<size_t>(static_cast<ssize_t>((token.End() - token.Begin()).column))};
    return ParseIdentifierFromName(token.Value(), token.Begin(), token.End(), len);
}

SrcIdentifier ParserImpl::ParseIdentifierFromName(
    const std::string& identifier, const Position& tkPos, const Position& end, size_t len) const
{
    bool isRaw = IsRawIdentifier(identifier);
    const std::string& name = isRaw ? ParseNameFromRawIdentifier(identifier) : identifier;
    Position pos = isRaw ? (tkPos + 1) : tkPos;
    Position endPos = isRaw ? (end - 1) : end;
    (void)len;
    return {name, pos, endPos, isRaw};
}

SrcIdentifier ParserImpl::ExpectIdentifierWithPos(AST::Node& node)
{
    Position tkPos{INVALID_POSITION};
    if (node.astKind == AST::ASTKind::FUNC_DECL && Skip(TokenKind::MAIN)) {
        return ParseIdentifierFromToken(lastToken);
    }
    if (Skip(TokenKind::IDENTIFIER) || SkipKeyWordIdentifier()) {
        return ParseIdentifierFromToken(lastToken);
    }
    if (!node.TestAttr(AST::Attribute::HAS_BROKEN)) {
        tkPos = lastToken.Begin();
        DiagExpectedIdentifierWithNode(&node);
    }
    return {INVALID_IDENTIFIER, tkPos, tkPos, false};
}

SrcIdentifier ParserImpl::ExpectPackageIdentWithPos(AST::Node& node)
{
    Position tkPos{INVALID_POSITION};
    if (Skip(TokenKind::IDENTIFIER) || Skip(TokenKind::PACKAGE_IDENTIFIER) || SkipKeyWordIdentifier()) {
        return ParseIdentifierFromToken(lastToken);
    }
    if (!node.TestAttr(AST::Attribute::HAS_BROKEN)) {
        tkPos = lastToken.End();
        DiagExpectedIdentifierWithNode(&node);
    }
    return {INVALID_IDENTIFIER, tkPos, tkPos, false};
}

void ParserImpl::SkipBlank(TokenKind blank0, TokenKind blank1)
{
    while (Seeing(blank0) || Seeing(blank1)) {
        Next();
    }
}

bool ParserImpl::DetectPrematureEnd()
{
    if (deadlocked || Seeing(TokenKind::END)) {
        Next();
        return true;
    } else {
        deadlocked = true;
        return false;
    }
}
bool ParserImpl::SeeingKeywordAndOperater()
{
    if (SeeingContextualKeyword()) {
        auto tokens = lexer->LookAheadSkipNL(1);
        // Destructor function and MacroCall function will not be identified as a keyword identifier.
        if (tokens.begin()->kind == TokenKind::BITNOT || tokens.begin()->kind == TokenKind::AT) {
            return false;
        }
        if (tokens.begin()->kind < TokenKind::WILDCARD) {
            return true;
        }
    }
    return false;
}

bool ParserImpl::SeeingKeywordWithDecl()
{
    std::vector<TokenKind> tokenKindVec = {TokenKind::STRUCT, TokenKind::ENUM, TokenKind::PACKAGE,
        TokenKind::IMPORT, TokenKind::CLASS, TokenKind::INTERFACE, TokenKind::FUNC, TokenKind::MACRO, TokenKind::TYPE,
        TokenKind::LET, TokenKind::VAR, TokenKind::EXTEND, TokenKind::MAIN};
    if (SeeingContextualKeyword()) {
        auto tokens = lexer->LookAheadSkipNL(1);
        if (std::find(tokenKindVec.begin(), tokenKindVec.end(), tokens.begin()->kind) != tokenKindVec.end()) {
            return true;
        }
    }
    return false;
}

bool ParserImpl::SeeingNamedFuncArgs()
{
    if (SeeingContextualKeyword() || Seeing(TokenKind::IDENTIFIER)) {
        return lexer->Seeing({TokenKind::COLON}, true);
    }
    return false;
}

bool ParserImpl::SeeingIdentifierAndTargetOp(const std::vector<TokenKind>& tokenKinds)
{
    if (SeeingContextualKeyword() || Seeing(TokenKind::IDENTIFIER)) {
        auto tokens = lexer->LookAheadSkipNL(1);
        for (auto& kind : tokenKinds) {
            if (tokens.begin()->kind == kind) {
                return true;
            }
        }
    }
    return false;
}

bool ParserImpl::SeeingInvaildParamListInLambdaExpr()
{
    const std::vector<TokenKind> vaildKinds{TokenKind::COMMA, TokenKind::COLON, TokenKind::DOUBLE_ARROW};
    if (SeeingContextualKeyword() || Seeing(TokenKind::IDENTIFIER) || Seeing(TokenKind::WILDCARD)) {
        const size_t combinedDoubleArrowSize = 2;
        auto tokens = lexer->LookAheadSkipNL(combinedDoubleArrowSize);
        CJC_ASSERT(!tokens.empty());
        for (auto& kinds : vaildKinds) {
            if (tokens.begin()->kind == kinds) {
                return false;
            }
        }
        CJC_ASSERT(combinedDoubleArrow.size() == combinedDoubleArrowSize);
        if (tokens.size() == combinedDoubleArrow.size()) {
            auto first = tokens.begin();
            if (first->kind != combinedDoubleArrow[0]) {
                return true;
            }
            auto second = ++tokens.begin();
            if (second->kind != combinedDoubleArrow[1]) {
                return true;
            }
            if (second->Begin().line == first->Begin().line && second->Begin().column == first->Begin().column + 1) {
                return false;
            }
        }
    }
    return true;
}

bool ParserImpl::SeeingInvaildOperaterInLambdaExpr()
{
    const std::vector<TokenKind> tokenKinds{TokenKind::COMMA, TokenKind::COLON, TokenKind::DOUBLE_ARROW};
    if (SeeingContextualKeyword() || Seeing(TokenKind::IDENTIFIER)) {
        const size_t combinedDoubleArrowSize = 2;
        const size_t lookNum = combinedDoubleArrowSize + 1;
        auto tokens = lexer->LookAheadSkipNL(lookNum);
        CJC_ASSERT(!tokens.empty());
        if (tokens.front().kind != TokenKind::NOT) {
            return false;
        }
        auto secondTk = ++tokens.begin();
        if (secondTk == tokens.end()) {
            return false;
        }
        if (std::find(tokenKinds.begin(), tokenKinds.end(), secondTk->kind) != tokenKinds.end()) {
            return true;
        }
        if (tokens.size() < lookNum) {
            return false;
        }
        auto thirdTk = tokens.back();
        CJC_ASSERT(combinedDoubleArrow.size() == combinedDoubleArrowSize);
        if (secondTk->kind != combinedDoubleArrow[0] || thirdTk.kind != combinedDoubleArrow[1]) {
            return false;
        }
        if (thirdTk.Begin().line == secondTk->Begin().line && thirdTk.Begin().column == secondTk->Begin().column + 1) {
            return true;
        }
    }
    return false;
}

bool ParserImpl::SeeingAnnotationTrailingClosure(const std::vector<TokenKind>& tokenKinds)
{
    if (!SeeingAny({TokenKind::AT, TokenKind::AT_EXCL})) {
        return false;
    }
    auto tokens = lexer->LookAheadSkipNL(tokenKinds.size() + 1);
    if (tokens.front().kind == TokenKind::IDENTIFIER ||
        (tokens.front().kind >= TokenKind::PUBLIC && tokens.front().kind <= TokenKind::OPEN)) {
        return tokens.back().kind == TokenKind::LSQUARE;
    }
    return false;
}

std::size_t ParserImpl::GetProcessedTokens() const
{
    auto lexerTokensConsumed = this->lexer->GetCurrentToken();
    auto lookAheadKind = this->lookahead.kind;
    if (lookAheadKind != TokenKind::SENTINEL && lookAheadKind != TokenKind::END) {
        lexerTokensConsumed--;
    }
    return lexerTokensConsumed;
}
}; // namespace Cangjie
