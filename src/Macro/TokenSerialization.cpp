// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/*
 * @file
 *
 * This file implements the Cangjie Token Serialization.
 */

#include "cangjie/Macro/TokenSerialization.h"

#include <limits>

#include "cangjie/Basic/Print.h"
#include "cangjie/Basic/SourceManager.h"
#include "cangjie/Macro/MacroCommon.h"

using namespace TokenSerialization;

namespace {
std::string GetStringFromBytes(const uint8_t* pBuffer, uint32_t strLen)
{
    if (pBuffer == nullptr) {
        return "";
    }
    std::string value;
    for (uint32_t i = 0; i < strLen; i++) {
        if (*pBuffer == '\0') {
            value += "\\0";
            pBuffer += 1;
            continue;
        }
        value.push_back(static_cast<char>(*pBuffer));
        pBuffer += 1;
    }
    return value;
}
} // namespace

/**
 * Encoding tokens in memory like this.
 *
 * -> uint32_t   [uint16_t   uint32_t   char+   uint32_t   int32_t   int32_t]+
 *    ~~~~~~~~    ~~~~~~~~   ~~~~~~~~   ~~~~~   ~~~~~~~~   ~~~~~~~   ~~~~~~~ ~
 *    |           |          |          |       |          |         |       |
 *    a           b          c          d       e          f         g       h
 *
 * a: size of tokens
 * b: token kind as number
 * c: size of token value
 * d: token value as char stream
 * e: fileID as number
 * f: line number
 * g: column number
 * h: iterate each token in tokens
 *
 * @param expr : QuoteExpr desugared.
 */
std::vector<uint8_t> TokenSerialization::GetTokensBytes(const std::vector<Token>& tokens)
{
    if (tokens.empty()) {
        return {};
    }
    std::vector<uint8_t> tokensBytes;
    auto pushBytes = [&tokensBytes](
                         const uint8_t* v, const size_t l) { (void)tokensBytes.insert(tokensBytes.end(), v, v + l); };
    uint32_t numberOfTokens = static_cast<uint32_t>(tokens.size());
    pushBytes(reinterpret_cast<uint8_t*>(&numberOfTokens), sizeof(uint32_t));
    for (const auto& tk : tokens) {
        uint16_t kind = static_cast<uint16_t>(tk.kind);
        pushBytes(reinterpret_cast<uint8_t*>(&kind), sizeof(uint16_t));
        // use uint32_t(4 Bytes) to encode the length of string.
        size_t strLen = tk.Value().size();
        pushBytes(reinterpret_cast<const uint8_t*>(&strLen), sizeof(uint32_t));
        (void)tokensBytes.insert(tokensBytes.end(), tk.Value().begin(), tk.Value().end());
        auto begin = tk.Begin();
        pushBytes(reinterpret_cast<const uint8_t*>(&(begin.fileID)), sizeof(uint32_t));
        pushBytes(reinterpret_cast<const uint8_t*>(&(begin.line)), sizeof(int32_t));
        auto& escapes = GetEscapeTokenKinds();
        int32_t column = begin.column;
        if (std::find(escapes.begin(), escapes.end(), tk.kind) != escapes.end() &&
            column + 1 + static_cast<int>(strLen) == tk.End().column) {
            ++column;
        }
        pushBytes(reinterpret_cast<const uint8_t*>(&column), sizeof(int32_t));
        pushBytes(reinterpret_cast<const uint8_t*>(&(tk.isSingleQuote)), sizeof(uint16_t));
        if (tk.kind == TokenKind::MULTILINE_RAW_STRING) {
            pushBytes(reinterpret_cast<const uint8_t*>(&(tk.delimiterNum)), sizeof(uint16_t));
        }
    }
    return tokensBytes;
}

std::vector<Token> TokenSerialization::GetTokensFromBytes(const uint8_t* pBuffer)
{
    if (pBuffer == nullptr) {
        return {};
    }
    std::vector<Token> tokens{};
    uint32_t numberOfTokens = 0;
    (void)std::copy(pBuffer, pBuffer + sizeof(uint32_t), reinterpret_cast<uint8_t*>(&numberOfTokens));
    pBuffer += sizeof(uint32_t);
    for (uint32_t i = 0; i < numberOfTokens; ++i) {
        uint16_t kind = 0;
        (void)std::copy(pBuffer, pBuffer + sizeof(uint16_t), reinterpret_cast<uint8_t*>(&kind));
        pBuffer += sizeof(uint16_t);
        uint32_t strLen = 0;
        (void)std::copy(pBuffer, pBuffer + sizeof(uint32_t), reinterpret_cast<uint8_t*>(&strLen));
        pBuffer += sizeof(uint32_t);
        std::string value = GetStringFromBytes(pBuffer, strLen);
        pBuffer += strLen;
        uint32_t fileID = 0;
        constexpr auto i4 = sizeof(int32_t);
        (void)std::copy(pBuffer, pBuffer + i4, reinterpret_cast<uint8_t*>(&fileID));
        pBuffer += i4;
        int32_t line = 0;
        int32_t column = 0;
        (void)std::copy(pBuffer, pBuffer + i4, reinterpret_cast<uint8_t*>(&line));
        pBuffer += i4;
        (void)std::copy(pBuffer, pBuffer + i4, reinterpret_cast<uint8_t*>(&column));
        pBuffer += i4;
        Position begin{fileID, line, column};

        uint16_t isSingle = 0;
        (void)std::copy(pBuffer, pBuffer + sizeof(uint16_t), reinterpret_cast<uint8_t*>(&isSingle));
        pBuffer += sizeof(uint16_t);
        unsigned delimiterNum{1};
        if (static_cast<TokenKind>(kind) == TokenKind::MULTILINE_RAW_STRING) {
            (void)std::copy(pBuffer, pBuffer + sizeof(uint16_t), reinterpret_cast<uint8_t*>(&delimiterNum));
            pBuffer += sizeof(uint16_t);
        }
        Position end{begin == INVALID_POSITION ? INVALID_POSITION
            : begin + GetTokenLength(value.size(), static_cast<TokenKind>(kind), delimiterNum)};
        Token token{static_cast<TokenKind>(kind), std::move(value), begin, end};
        token.delimiterNum = delimiterNum;
        token.isSingleQuote = (isSingle == 1) ? true : false;
        (void)tokens.emplace_back(std::move(token));
    }
    return tokens;
}

uint8_t* TokenSerialization::GetTokensBytesWithHead(const std::vector<Token>& tokens)
{
    if (tokens.empty()) {
        return nullptr;
    }
    std::vector<uint8_t> tokensBytes = TokenSerialization::GetTokensBytes(tokens);
    size_t bufferSize = tokensBytes.size() + sizeof(uint32_t);
    if (bufferSize == 0 || bufferSize > std::numeric_limits<uint32_t>::max()) {
        Errorln("Memory Allocated Size is Not Valid.");
        return nullptr;
    }
    uint8_t* rawPtr = (uint8_t*)malloc(bufferSize);
    if (rawPtr == nullptr) {
        Errorln("Memory Allocation Failed.");
        return rawPtr;
    }
    uint32_t head = static_cast<uint32_t>(bufferSize);
    auto pHead = reinterpret_cast<uint8_t*>(&head);
    (void)tokensBytes.insert(tokensBytes.begin(), pHead, pHead + sizeof(uint32_t));
    (void)std::copy(tokensBytes.begin(), tokensBytes.end(), rawPtr);
    return rawPtr;
}