// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the Lexer, which performs lexical analysis of the source code.
 */

#ifndef CANGJIE_LEX_LEXER_H
#define CANGJIE_LEX_LEXER_H

#include <cstdint>
#include <list>
#include <string>

#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Lex/Token.h"

namespace Cangjie {
const uint8_t BYTE_X_FLAG = 0b10000000; // 0x80
const uint8_t BYTE_2_FLAG = 0b11000000; // 0xc0
const uint8_t BYTE_3_FLAG = 0b11100000; // 0xe0
const uint8_t BYTE_4_FLAG = 0b11110000; // 0xf0
const uint8_t BYTE_5_FLAG = 0b11111000; // 0xf8
const uint8_t BYTE_6_FLAG = 0b11111100; // 0xfc
const uint8_t LOW_6_BIT_MASK = 0b00111111;
const uint8_t LOW_5_BIT_MASK = 0b00011111;
const uint8_t LOW_4_BIT_MASK = 0b00001111;
const uint8_t LOW_3_BIT_MASK = 0b00000111;
const uint8_t LOW_2_BIT_MASK = 0b00000011;
const uint8_t LOW_1_BIT_MASK = 0b00000001;
const uint32_t UTF8_1_MAX = 0x7F;
const uint32_t UTF8_2_MAX = 0x07FF;
const uint32_t UTF8_3_MAX = 0xFFFF;
const uint32_t UTF8_4_MAX = 0x10FFFF;
const uint32_t UTF8_5_MAX = 0x3FFFFFF;
const uint32_t UTF8_6_MAX = 0x7FFFFFFF;
const uint8_t LEFT_SHIFT_30 = 30;
const uint8_t LEFT_SHIFT_24 = 24;
const uint8_t LEFT_SHIFT_18 = 18;
const uint8_t LEFT_SHIFT_12 = 12;
const uint8_t LEFT_SHIFT_6 = 6;
const int32_t BYTE_4_BASE = 65536;
const int32_t BYTE_3_BASE = 2048;
const uint8_t BYTE_2_BASE = 128;
const uint8_t BYTE_1_STEP = 1;
const uint8_t BYTE_2_STEP = 2;
const uint8_t BYTE_3_STEP = 3;
const uint8_t BYTE_4_STEP = 4;
const uint8_t BYTE_5_STEP = 5;
const uint8_t BYTE_6_STEP = 6;
const int32_t ERROR_UTF8 = -2; // The illegal ascii char stands for error unicode.
const uint8_t BYTE_3_INDEX = 2;
const uint8_t BYTE_4_INDEX = 3;
const uint8_t BYTE_5_INDEX = 4;
const uint8_t BYTE_6_INDEX = 5;
const uint8_t UNICODE_MAX_NUM = 8;
const uint8_t BIN_BASE = 2;
const uint8_t OCT_BASE = 8;
const uint8_t DEC_BASE = 10;
const uint8_t HEX_BASE = 16;

/// Get a list of contextual keyword.
const std::vector<TokenKind>& GetContextualKeyword();
bool IsContextualKeyword(std::string_view s);

class Lexer {
public:
    /// Create Lexer.
    /// \param cts whether to enableCollectTokenStream
    Lexer(unsigned int fileID, const std::string& input, DiagnosticEngine& diag, SourceManager& sm,
        bool cts = false);
    /// \param pos the location of the first character of input. This is typically called in parsing interpolation
    /// string.
    Lexer(unsigned int fileID, const std::string& input, DiagnosticEngine& diag, SourceManager& sm,
        const Position& pos);
    /// \param splitAmbi
    /// This is called by libast
    Lexer(const std::string& input, DiagnosticEngine& diag, SourceManager& sm, bool cts = false, bool splitAmbi = true);
    Lexer(const std::string& input, DiagnosticEngine& diag, SourceManager& sm, const Position& pos, bool cts = false);
    Lexer(const std::string& input, DiagnosticEngine& diag, Source& s, const Position& pos, bool cts = false);
    /// Create Lexer with \ref inputTokens from successful macro func call.
    Lexer(const std::vector<Token>& inputTokens, DiagnosticEngine& diag, SourceManager& sm, bool cts = false);
    ~Lexer();

    // FrontendTool/SourceManager/Parse/Macro/stdlib/unittests.
    /// read and return next token
    Token Next();

    // Parse/unittests.
    /// Read and return the next \ref num tokens. If there are less than \ref num tokens left, all are returned.
    /// otherwise, comments are omitted.
    const std::list<Token>& LookAhead(size_t num);
    /// Returns true if the next token is any of the TokenKind's described by range \ref begin and \ref end.
    /// \param skipNewline whether to ignore NL
    bool Seeing(const std::vector<TokenKind>::const_iterator& begin, const std::vector<TokenKind>::const_iterator& end,
        bool skipNewline = false);
    /// Returns true if the next token is any of \ref kinds.
    /// \param skipNewline whether to ignore NL
    bool Seeing(const std::vector<TokenKind>& kinds, bool skipNewline = false);

    // Parse.
    // Parse/unittests.
    /// Read and return the next \ref num tokens. If there are less than \ref num tokens left, all are returned.
    /// otherwise, comments and NL's are omitted.
    std::list<Token> LookAheadSkipNL(size_t num);
    /// Return all comments collected, and clear the comment cache
    std::vector<Token> GetComments();
    const std::set<Token>& GetTokenStream() const;

    /// Cache the current lookahead and read pointer.
    void SetResetPoint();
    /// Restore to the previous state when \ref SetResetPoint is called.
    void Reset();
    /// get the first position of all the tokens of this Lexer.
    const Position& GetPosBase() const;

    unsigned GetFileID() const;

    void ClearStringParts(const Token& t);

    /**
     * @brief Enable lexing of EH keywords
     *
     * @param enabled Whether to enable or disable EH tokens lexing
    */
    void SetEHEnabled(bool enabled) const;

    // Parse/Macro.
    const std::vector<StringPart>& GetStrParts(const Token& t);
    /// Read all tokens until the END token, excluded. After this, the Lexer cannot yield any more token.
    std::vector<Token> GetTokens();
    std::vector<Token> GetCollectTokens() const;
    bool StartCollectTokens();
    void StopCollectTokens(bool bStart);
    std::size_t GetCurrentToken() const;

private:
    class LexerImpl* impl;
};

/// Escape a Cangjie multiline interpolation string by prefixing each double quote with a backslash. Returns a valid
/// cangjie source that can be lexed into a multiline interpolation string.
/// \param value string literal value of a cangjie multinline interpolation string.
std::string ProcessQuotaMarks(const std::string& value, bool processSingle = false);
}; // namespace Cangjie

#endif // CANGJIE_LEX_LEXER_H
