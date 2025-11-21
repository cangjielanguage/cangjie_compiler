// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements class LexerImpl.
 */

#include "LexerImpl.h"

namespace Cangjie {
Token Lexer::Next()
{
    return impl->Next();
}

const std::list<Token>& Lexer::LookAhead(size_t num)
{
    return impl->LookAhead(num);
}
bool Lexer::Seeing(const std::vector<TokenKind>::const_iterator& begin,
    const std::vector<TokenKind>::const_iterator& end, bool skipNewline)
{
    return impl->Seeing(begin, end, skipNewline);
}
bool Lexer::Seeing(const std::vector<TokenKind>& kinds, bool skipNewline)
{
    return impl->Seeing(kinds, skipNewline);
}

std::list<Token> Lexer::LookAheadSkipNL(size_t num)
{
    return impl->LookAheadSkipNL(num);
}
std::vector<Token> Lexer::GetComments()
{
    return std::move(impl->comments);
}
const std::vector<Token>& Lexer::GetTokenStream() const
{
    return impl->tokenStream;
}

void Lexer::SetResetPoint()
{
    impl->SetResetPoint();
}
void Lexer::Reset()
{
    impl->Reset();
}
const Position& Lexer::GetPosBase() const
{
    return impl->posBase;
}

unsigned Lexer::GetFileID() const
{
    return impl->fileID;
}

void Lexer::ClearStringParts(const Token& t)
{
    impl->stringPartsMap.erase(t);
}

const std::vector<StringPart>& Lexer::GetStrParts(const Token& t)
{
    return impl->GetStrParts(t);
}
std::vector<Token> Lexer::GetTokens()
{
    return impl->GetTokens();
}
std::vector<Token> Lexer::GetCollectTokens() const
{
    return impl->GetCollectTokens();
}
bool Lexer::StartCollectTokens()
{
    return impl->StartCollectTokens();
}
void Lexer::StopCollectTokens(bool bStart)
{
    impl->StopCollectTokens(bStart);
}
std::size_t Lexer::GetCurrentToken() const
{
    return impl->GetCurrentToken();
}
} // namespace Cangjie
