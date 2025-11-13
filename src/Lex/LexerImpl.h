// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares class LexerImpl, the impl class of class Lexer.
 */

#ifndef CANGJIE_LEX_LEXERIMPL_H
#define CANGJIE_LEX_LEXERIMPL_H

#include "cangjie/Lex/Lexer.h"

#include <unordered_map>

namespace Cangjie {
/// arg pack for creation of LexerImpl
struct LexerConfig {
    unsigned fileID{0};
    const Position& posBase{0, 1, 1};
    bool collectTokenStream{false};
    bool splitAmbiguousToken{true};
};

class LexerImpl final {
public:
    LexerImpl(const std::string& input, DiagnosticEngine& diag, SourceManager& sm, LexerConfig args)
        : LexerImpl{input, diag, sm.GetSource(args.fileID), std::move(args)}
    {
    }
    LexerImpl(const std::string& input, DiagnosticEngine& diag, Source& s, LexerConfig args)
        : diag{diag},
          fileID{args.fileID},
          source{s},
          pInputStart{input.c_str()},
          pNext{input.c_str()},
          pInputEnd{pInputStart + input.length()},
          posBase{args.posBase},
          splitAmbiguousToken{args.splitAmbiguousToken},
          enableCollectTokenStream{args.collectTokenStream}
    {
        Init();
    }
    LexerImpl(const std::vector<Token>& inputTokens, DiagnosticEngine& diag, SourceManager& sm, LexerConfig args)
        : diag{diag},
          fileID{args.fileID},
          source{sm.GetSource(fileID)},
          posBase{args.posBase},
          splitAmbiguousToken{args.splitAmbiguousToken},
          enableScan{false},
          enableCollectTokenStream{args.collectTokenStream}
    {
        lineResetOffsetsFromBase = 0;
        for (auto& tk : inputTokens) {
            if (tk.kind == TokenKind::IDENTIFIER && LookupKeyword(tk.Value()) != TokenKind::IDENTIFIER) {
                // In macro definition, the user can create an identifier token
                // or obtain an identifier token from getIdentifier() of libast,
                // which is actually a keyword token. Change token from identifier to keyword here.
                Lexer tempLexer(tk.Value(), diag, sm, tk.Begin());
                auto tks = tempLexer.GetTokens();
                (void)tokens.insert(tokens.end(), tks.begin(), tks.end());
            } else {
                (void)tokens.emplace_back(tk);
            }
        }
        // Initialize pos, which is in the next column to the last token.
        if (!tokens.empty()) {
            auto lastToken = tokens.back();
            pos = Position{lastToken.Begin().fileID, lastToken.Begin().line, lastToken.Begin().column + 1};
        } else {
            pos = Position{0, 0, 0};
        }
    }

    // FrontendTool/SourceManager/Parse/Macro/stdlib/unittests.
    Token Next(); // read and return next token

    void ReserveToken(size_t num, bool skipNewline);

    // Parse/unittests.
    const std::list<Token>& LookAhead(size_t num);
    bool Seeing(const std::vector<TokenKind>::const_iterator& begin, const std::vector<TokenKind>::const_iterator& end,
        bool skipNewline = false);
    bool Seeing(const std::vector<TokenKind>& kinds, bool skipNewline = false);

    // Parse.
    std::list<Token> LookAheadSkipNL(size_t num);
    std::vector<Token> comments; // All Comments.
    void EnableCollectTokenStream()
    {
        enableCollectTokenStream = true;
    }
    const std::set<Token>& GetTokenStream() const
    {
        return tokenStream;
    }

    void SetResetPoint();
    void Reset();
    Position GetPosBase() const
    {
        return posBase;
    }

    unsigned int GetFileID() const
    {
        return fileID;
    }

    void ClearStringParts(const Token& t)
    {
        stringPartsMap.erase(t);
    }

    // Parse/Macro.
    const std::vector<StringPart>& GetStrParts(const Token& t);
    std::vector<Token> GetTokens();
    std::vector<Token> GetCollectTokens() const;
    bool StartCollectTokens();
    void StopCollectTokens(bool bStart);
    std::size_t GetCurrentToken() const;

private:
    friend class Lexer;
    std::vector<int> resetLineOffsets{0};
    std::map<Token, std::vector<StringPart>> stringPartsMap;
    bool IsCurrentCharLineTerminator() const;
    bool ScanInterpolationString(const char* pStart, bool allowNewLine);
    bool ScanInterpolationStringLiteralHoleBalancedTextString();
    bool ScanInterpolationStringLiteralHoleBalancedText(const char* pStart, char endingChar, bool allowNewline = false);
    Position GetPos(const char* current);
    std::pair<const char*, bool> ProcessStringInterpolation(
        const char* pStart, Position& beginPos, std::vector<StringPart>& stringParts, bool allowNewLine = false);
    bool ScanInterpolationStringLiteralHoleComment(bool allowNewline);
    bool ProcessInterpolationStringLiteralLineBreak(bool allowNewline);
    size_t CountTerminator(const std::string& str);

    DiagnosticEngine& diag; // diag for lexer
    unsigned int fileID{0};
    Source& source;
    const char* pInputStart{nullptr}; // point to begin of input file
    const char* pNext{nullptr};       // point to the next character, traversing source code
    const char* pResetNext{nullptr};
    const char* pInputEnd{nullptr}; // point to end of input file
    const char* pCurrent{nullptr};  // point to the current character
    const char* pResetCurrent{nullptr};
    int32_t currentChar{-1}; // currently processing character
    std::unordered_map<std::string_view, TokenKind> tokenMap{};
    unsigned lineResetOffsetsFromBase{0};
    TokenKind tokenKind{TokenKind::ILLEGAL};
    Position pos;
    Position posBase{0, 1, 1};
    // ambiguous tokens like <=, <<, <<=, ??, will be splited for parser case. e.g.(a < b, c >= d) and let a: ??T = 1
    bool splitAmbiguousToken = true;
    const std::unordered_map<TokenKind, std::tuple<TokenKind, std::string_view, TokenKind, std::string_view>>
        ambiCombinedTokensDegTable{
            {TokenKind::COALESCING, {TokenKind::QUEST, "?", TokenKind::QUEST, "?"}},
            {TokenKind::RSHIFT_ASSIGN, {TokenKind::GT, ">", TokenKind::GE, ">="}},
            {TokenKind::RSHIFT, {TokenKind::GT, ">", TokenKind::GT, ">"}},
            {TokenKind::GE, {TokenKind::GT, ">", TokenKind::ASSIGN, "="}},
        };
    /*
     * Record the offset of the start position of each line in the source code relative to pInputStart.
     * every two adjacent elements in this vector can be seen as a left closed right open interval pair,
     * one pair corresponds to one line
     *
     * e.g.
     * `lineOffsetsFromBase == {0, 5}` means the source code have two lines,
     * and the first line has five chars (including line terminator), the pair can be abbreviated as [0, 5),
     * 0 is the offset of start of the first line relative to pInputStart,
     * 5 is the offset of start of the second line relative to pInputStart.
     *
     */
    std::vector<size_t> lineOffsetsFromBase{0};
    std::list<Token> lookAheadCache;
    std::list<Token> resetLookAheadCache;
    bool enableScan{true};
    std::vector<Token> tokens;
    size_t curToken{0};
    size_t resetToken{0};
    bool enableCollect{false};
    bool enableCollectTokenStream{false};
    std::vector<Token> collectTokens;
    std::set<Token> tokenStream; // use set because ParseModifiers before import may cause the tokens of the first decl
        // to be Scan()'ed multiple times.
    mutable bool success{true};                                     /// Used for diagnostic.
    mutable std::vector<std::pair<const char*, bool>> stringStarts; /// Used for diagnostic, string start position.
    mutable std::vector<const char*> interpolations; /// Used for diagnostic, string interpolation position.
    /// Get an UTF-8 character, skipping spaces.
    /// \param diagUnsafe when true, issue a diagnostic error on unsafe Unicode values
    void ReadUTF8Char();
    void ReadUTF8CharFromMultiBytes(int32_t& ch);
    bool CheckUnicodeSecurity(const int32_t& c) const;
    bool ProcessXdigit(const int& base, bool& hasDigit, const char* reasonPoint);
    bool ProcessDigits(const int& base, bool& hasDigit, const char* reasonPoint);
    std::string GetSuffix(const char* pSuffixStart);
    void ProcessIntegerSuffix();
    TokenKind LookupKeyword(const std::string& literal);
    void ProcessEscape(const char* pStart, bool isInString, bool isByteLiteral);
    void ProcessUnicodeEscape();
    Token ProcessIllegalToken(bool needStringParts, bool multiLine, const char* pStart, bool isJString = false);
    std::pair<Token, bool> ScanStringOrJString(const char* pStart, bool needStringParts = true, bool isJString = false);
    std::pair<Token, bool> ScanMultiLineString(const char* pStart, bool needStringParts = true);
    void ConsumeNChar(uint32_t n);
    std::pair<Token, bool> ScanMultiLineRawString(const char* pStart);

    Token ScanFromTokens();
    Token ScanIllegalSymbol(const char* pStart);
    Token ScanDivOrComment(const char* pStart);
    void ScanCharDiagUnterminated(const char* pStart);
    std::pair<Token, bool> ScanChar(const char* pStart, bool isByteLiteral = false);
    void ScanSymbolPlus();
    void ScanSymbolHyphen();
    void ScanSymbolAsterisk();
    void ScanSymbolAmpersand();
    void ScanSymbolExclamation();
    void ScanSymbolLessThan();
    void ScanSymbolGreaterThan();
    void ScanSymbolAt();
    void ScanSymbolEqual();
    void ScanSymbolPercentSign();
    void ScanSymbolVerticalBar();
    void ScanSymbolTilde();
    void ScanSymbolCaret();
    void ScanSymbolQuest();
    void ScanSymbolColon();
    Token GetSymbolToken(const char* pStart);
    Token ScanSymbol(const char* pStart);
    std::pair<Token, bool> ScanComment(const char* pStart, bool allowNewLine = true);
    std::pair<Token, bool> ScanMultiLineComment(const char* pStart, bool allowNewLine);
    /// Check if there are f32, f64 after a float literal
    void ProcessFloatSuffix(const char prefix);
    void ProcessNumberExponentPart(const char prefix, const char* reasonPoint, bool& isFloat);
    /// Check if there a f32, f64 after a float literal. After that, this float is completed; if there are excessive
    /// '.' and chars following it without space between, check whether the following chars are possibly valid.
    /// The valid cases are
    /// 1.3f32.. -> .. is range operator
    /// 1.3f32.什么 -> .什么 is a member prop/func access (in this case Float32/Float64 is extended)
    void ProcessNumberFloatSuffix(const char& prefix, bool isFloat);
    Token ScanNumber(const char* pStart);
    Token ScanNumberOrDotPrefixSymbol(const char* pStart);
    bool ScanNumberIntegerPart(const char* pStart, int& base, char& prefix, bool& hasDigit, const char*& reasonPoint);
    void ScanNumberDecimalPart(const int& base, const char& prefix, bool& hasDigit, const char* reasonPoint);
    void ScanNumberExponentPart(const char* reasonPoint);
    Token ScanDotPrefixSymbol();
    Token ScanBackquotedIdentifier(const char* pStart);
    void ScanIdentifierOrKeyword(Token& res, const char* pStart);
    Token ScanSingleOrMultiLineString(const char* pStart);
    const char* GetIllegalStartDecimalPart(const char* pStart, const char* pEnd) const;
    void DiagUnexpectedDecimalPoint(const char* reasonPoint);
    void DiagExpectedDigit(const char base);
    inline void DiagSmallExpectedDigit(bool hasDigit, char base);
    inline void CheckIllegalUTF8InStringLiteral(uint32_t byte);
    void CheckUnsecureUnicodeValue(const int32_t& ch);
    void DiagUnexpectedDigit(const int& base, const char* reasonPoint);
    void DiagUnexpectedExponentPart(const char exp, const char prefix, const char* reasonPoint);
    void DiagExpectedExponentPart(const char* reasonPoint);
    void DiagUnexpectedDollarIdentifier(const Token& t);
    void DiagIllegalSymbol(const char* pStart);
    void DiagUnterminatedSingleLineString(const char* pStart, bool isMatchEnd, bool isJString = false);
    void DiagUnterminatedMultiLineString(const char* pStart);
    void DiagUnterminatedRawString(const char* pStart);
    void DiagUnrecognizedEscape(const char* pStart, bool isInString, bool isByteLiteral);
    void DiagUnexpectedIntegerLiteralTypeSuffix(
        const char* pSuffixStart, const std::string& signednessType, const std::string& suffix);
    void DiagUnexpectedFloatLiteralTypeSuffix(const char* pSuffixStart, const std::string& suffix);
    void DiagExpectedRightBracket(const char* pStart);
    void DiagExpectedRightBracketOrHexadecimal(const char* pStart);
    void DiagCharactersOverflow(const char* pStart);
    void DiagUnterminatedInterpolation();
    void DiagUnknownStartOfToken(const Position curPos);
    void DiagUnrecognizedCharInByte(
        const int32_t& c, const std::string& str, const char* pStart, const std::pair<Position, Position>& range);
    std::string ConvertCurrentChar();
    inline bool IsAdjacent(const char* first, const char* second) const
    {
        if ((*first == '\r') && (first + 1 < pInputEnd) && *(first + 1) == '\n') {
            return second - first == BYTE_2_STEP;
        }
        return second - first == 1;
    }

    void TryRegisterLineOffset();

    const char* PrefixName(const char prefix) const;
    template <typename... T> void Diagnose(DiagKind kind, T... args)
    {
        diag.Diagnose(pos, kind, args...);
    }
    template <typename... T> void Diagnose(const Position diagPos, DiagKind kind, T... args)
    {
        diag.Diagnose(diagPos, kind, args...);
    }
    void Init()
    {
        for (unsigned char i = 0; i < static_cast<unsigned char>(TokenKind::IDENTIFIER); i++) {
            tokenMap[TOKENS[i]] = static_cast<TokenKind>(i);
        }
        // @! is added after TokenKind::IDENTIFIER
        auto atExclIndex = static_cast<unsigned char>(TokenKind::AT_EXCL);
        tokenMap[TOKENS[atExclIndex]] = static_cast<TokenKind>(atExclIndex);
        auto commonIndex = static_cast<unsigned char>(TokenKind::COMMON);
        tokenMap[TOKENS[commonIndex]] = static_cast<TokenKind>(commonIndex);
        auto platformIndex = static_cast<unsigned char>(TokenKind::PLATFORM);
        tokenMap[TOKENS[platformIndex]] = static_cast<TokenKind>(platformIndex);
        auto dcIndex = static_cast<unsigned char>(TokenKind::DOUBLE_COLON);
        tokenMap[TOKENS[dcIndex]] = static_cast<TokenKind>(dcIndex);
        tokenMap["true"] = TokenKind::BOOL_LITERAL;
        tokenMap["false"] = TokenKind::BOOL_LITERAL;
        auto ftrIndex = static_cast<unsigned char>(TokenKind::FEATURES);
        tokenMap[TOKENS[ftrIndex]] = TokenKind::FEATURES;
    }
    void Back();
    bool IsCharOrString() const;
    Token ScanCharOrString(const char* pStart);
    Token ScanBase();
    Token Scan();
    Token ScanByteUInt8(const char* pStart);
    /**
     * Lookahead a character.
     * @param index Start from 0, 0 means lookahead one char.
     * @return Return the lookahead value, if cross the border, return '\0'.
     */
    char GetNextChar(const unsigned char index) const
    {
        if (pNext + index >= pInputEnd) {
            return '\0';
        } else {
            return pNext[index];
        }
    }
    bool CheckArraySize(size_t len, int32_t& ch);
    void CollectToken(const Token& token);
    std::pair<Token, bool> ProcessIllegalCharValue(const char* pStart, size_t beginOffset);
    /**
     * Scan a unicode identifier at \p pStart, with first unicode char \p codePoint. \p codePoint may not be a valid
     * unicode value, issue a diagnostic in that case.
     */
    void ScanUnicodeIdentifierStart(Token& res, unsigned codePoint, const char* pStart);
    /**
     * Continue to scan identifier after scanning one ascii id char. \p pStart points to the next identfier char to
     * scan, which is one unicode character past #pCurrent; #pCurrent points to the start of this identifier.
     */
    void ScanIdentifierContinue(Token& res, const char* pStart);
    /**
     * Diagnose illegal unicode using \p currentChar
     */
    void DiagIllegalUnicode();
    /**
     * Try to consume a unicode character. Returns true if unicode character read succeeded, and advance #pCurrent and
     * #pNext in this case; otherwise, #pCurrent and #pNext is untouched.
     */
    bool TryConsumeIdentifierUTF8Char();
    bool ProcessIllegalRightQuotation(const char* pStart, int32_t quote);
};
} // namespace Cangjie

#endif
