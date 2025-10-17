// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the Token related classes, which is set of lexcial tokens of Cangjie.
 */

#ifndef CANGJIE_LEX_TOKEN_H
#define CANGJIE_LEX_TOKEN_H

#include <cstring>
#include <stdint.h>
#include <string>
#include <ostream>
#include <unordered_map>
#include <vector>

#include "cangjie/Basic/Position.h"
#include "cangjie/Utils/CheckUtils.h"

namespace Cangjie {
constexpr uint8_t NUM_TOKENS = 200;

enum class TokenKind : unsigned char {
#define TOKEN(ID, VALUE, LITERAL, PRECEDENCE) ID,
#include "cangjie/Lex/Tokens.inc"
#undef TOKEN
};

inline const char* TOKEN_KIND_VALUES[] = {
#define TOKEN(ID, VALUE, LITERAL, PRECEDENCE) VALUE,
#include "cangjie/Lex/Tokens.inc"
#undef TOKEN
};

inline const char* TOKENS[] = {
#define TOKEN(ID, VALUE, LITERAL, PRECEDENCE) LITERAL,
#include "cangjie/Lex/Tokens.inc"
#undef TOKEN
};

/**
 * @brief Check if a given token is experimental
 *
 * @param token The `const char*` representation of the token to check
 * @return bool Whether `token` is experimental
 */
inline bool IsExperimental(const char* token)
{
#define TOKEN(ID, VALUE, LITERAL, PRECEDENCE)
#define EXPERIMENTAL_TOKEN(ID, VALUE, LITERAL, PRECEDENCE)  \
    if (strcmp(token, LITERAL) == 0) {                      \
        return true;                                        \
    }
#include "cangjie/Lex/Tokens.inc"
#undef TOKEN
#undef EXPERIMENTAL_TOKEN
    return false;
}

inline const uint8_t TOKEN_TO_OPERATOR_PRECEDENCE[NUM_TOKENS] = {
#define TOKEN(ID, VALUE, LITERAL, PRECEDENCE) PRECEDENCE,
#include "cangjie/Lex/Tokens.inc"
#undef TOKEN
};

/**
 * Get the length of some TokenKind.
 */
inline int Len(TokenKind tokenKind)
{
    return static_cast<int>(strlen(TOKENS[static_cast<uint32_t>(tokenKind)]));
}

/**
 * A larger value ("tighter precedence") means an operator is grouped more closely with
 * its operands; for example `a + b * c` means `a + (b * c)` because multiplication
 * has precedence 15, which is larger/tighter than addition's 14.
 *
 * The `INVALID_PRECEDENCE` constant is a special case; 0 means "this is not an
 * operator whose precedence you asked about" -- it does not mean "loosest possible
 * precedence". Tokens such as `break` and `;` have this precedence.
 */
static const uint8_t INVALID_PRECEDENCE = 0;

struct Token {
    TokenKind kind;
    // read-only accessor
    unsigned int delimiterNum = 1; // Delimiter '#' number for raw string.
    bool isSingleQuote{false};     // Quotations of string-related literals can be single or double.
    bool commentForMacroDebug{false}; // added in compiler macro process
    const Position& Begin() const { return begin; }
    const Position& End() const;

    explicit Token(TokenKind kind) : kind(kind) {}
    Token(TokenKind kind, std::string value) : kind(kind)
    {
        SetValue(std::move(value));
    }

    /// \param be begin position
    /// \param en end position
    /// \param value value of Token. for identifiers, this value is after canonical recompose
    Token(TokenKind kind, std::string value, const Position& be, const Position& en, bool cmtForMacDebug = false)
        : kind(kind), commentForMacroDebug(cmtForMacDebug)
    {
        SetValuePos(std::move(value), be, en);
    }

    Token(const Token& other) = default;
    Token(Token&& other) = default;
    Token& operator=(const Token& other) = default;
    Token& operator=(Token&& other) = default;

    bool operator<(const Token& ct) const
    {
        return Begin() < ct.Begin();
    }
    bool operator==(const Token& ct) const
    {
        return Begin() == ct.Begin();
    }

    bool IsBlockComment()
    {
        return kind == TokenKind::COMMENT && v.rfind("/*", 0) != std::string::npos;
    }

    const std::string& Value() const { return v; }

    /// Sets the string value of the token.
    /// WARNING: Typically the begin and end position need to be set with the string value simultaneously. Only call
    /// this when you shall set the position somewhere later or the position does not matter.
    void SetValue(std::string s)
    {
        v = std::move(s);
    }
    void SetValuePos(std::string s, const Position& be, const Position& en)
    {
        SetValue(std::move(s));
        begin = be;
        end = en;
    }

    size_t Length() const
    {
        CJC_ASSERT(begin.fileID == end.fileID);
        CJC_ASSERT(begin.line == end.line);
        return static_cast<size_t>(static_cast<ssize_t>(end.column - begin.column));
    }

    void SetCurFile(bool isCurFile) { begin.isCurFile = end.isCurFile = isCurFile; }

    bool operator==(std::string_view other) const { return v == other; }
    bool operator!=(std::string_view other) const { return !(*this == other); }

private:
    std::string v;
    Position begin{INVALID_POSITION};
    Position end{INVALID_POSITION};
};

/**
 * Split string literal into string content and string expression.
 */
struct StringPart {
    enum StrKind { STR, EXPR } strKind;
    std::string value;
    Position begin;
    Position end;

    StringPart(StrKind strKind, const std::string& value, const Position& begin, const Position& end)
    {
        this->strKind = strKind;
        this->value = value;
        this->begin = begin;
        this->end = end;
    }
};
using TokenVecMap = std::unordered_map<size_t, std::vector<Token>>;
const std::vector<TokenKind>& GetEscapeTokenKinds();
} // namespace Cangjie

#endif // CANGJIE_LEX_TOKEN_H
