// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the common macro methods and alias.
 */

#ifndef CANGJIE_MACROCOMMON_H
#define CANGJIE_MACROCOMMON_H

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "cangjie/Basic/Print.h"
#include "cangjie/Macro/MacroCall.h"

namespace Cangjie {

/**
 * Collect macro infos per package.
 */
struct MacroCollector {
    // macro definitions.
    std::vector<Ptr<AST::MacroDecl>> macroDefFuncs;
    // macro invocations.
    std::vector<MacroCall> macCalls;
    // current pacakge who uses macros.
    Ptr<AST::Package> curPkg;
    // imported macro pacakges.
    std::unordered_set<Ptr<AST::Package>> importedMacroPkgs;

    // Clear previous info when we expand macros in a different package.
    void clear()
    {
        macroDefFuncs.clear();
        macCalls.clear();
        importedMacroPkgs.clear();
    }
};

std::vector<Token> GetTokensFromString(
    const std::string& source, DiagnosticEngine& diag, const Position pos = INVALID_POSITION);

bool IsCurFile(const SourceManager& sm, const Token& tk, unsigned int fileID = 0);

// Convert Tokens into string representation in source code.
std::string LineToString(TokenVector& line);

bool MacroExpandFailed(const std::vector<Token>& retTokens);

bool CheckAddSpace(Token curToken, Token nextToken);

/**
 * Helper class to convert Tokens to string.
 */
class MacroFormatter {
public:
    MacroFormatter(
        const TokenVector ts, std::vector<Position> eposVec, int offset)
        : input(ts), escapePosVec(eposVec), offset(offset)
    {
    }
    MacroFormatter(const TokenVector ts) : input(ts)
    {
        PushIntoLines();
    }
    const std::string Produce(bool hasComment = true);

private:
    std::vector<Token> input;
    std::vector<TokenVector> lines;
    std::vector<Position> escapePosVec;
    std::string retStr;
    int offset;

    void PushIntoLines();
    void LinesToString(bool hasComment);

    bool SeeCurlyBracket(const TokenVector& lineOfTk, const TokenKind& tk) const;
};

inline size_t GetTokenLength(size_t originalSize, TokenKind kind, unsigned delimiterNum)
{
    constexpr auto doubleQuotesSize = 2;
    constexpr auto multiQuotesSize = 6;
    switch (kind) {
        // both windows and linux trait NL as 1 size
        case TokenKind::NL:
            return 1;
        case TokenKind::STRING_LITERAL:
            return originalSize + doubleQuotesSize;
        case TokenKind::RUNE_LITERAL:
        case TokenKind::JSTRING_LITERAL:
            return originalSize + doubleQuotesSize + 1;
        case TokenKind::MULTILINE_STRING:
            return originalSize + multiQuotesSize;
        // For ##"abc"##, the offset of the length and value is 3 * 2.
        case TokenKind::MULTILINE_RAW_STRING:
            return originalSize + ((delimiterNum + 1) << 1);
        default:
            return originalSize;
    }
}
} // namespace Cangjie
#endif
