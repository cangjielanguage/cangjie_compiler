// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements API of class Parser by encapsulating API of ParserImpl.
 */

#include "ParserImpl.h"

namespace Cangjie {
OwnedPtr<AST::File> Parser::ParseTopLevel()
{
    return impl->ParseTopLevel();
}
OwnedPtr<AST::Decl> Parser::ParseDecl(ScopeKind scopeKind)
{
    return impl->ParseDecl(scopeKind, {}, {});
}

OwnedPtr<AST::Expr> Parser::ParseExpr()
{
    return impl->ParseExpr();
}
OwnedPtr<AST::Expr> Parser::ParseExprLibast()
{
    return impl->ParseExpr(ExprKind::UNKNOWN_EXPR);
}
OwnedPtr<AST::Type> Parser::ParseType()
{
    return impl->ParseType();
}
OwnedPtr<AST::Pattern> Parser::ParsePattern()
{
    return impl->ParsePattern();
}
std::vector<OwnedPtr<AST::Node>> Parser::ParseNodes(std::variant<ScopeKind, ExprKind> scope,
    AST::Node& currentMacroCall, const std::set<AST::Modifier>& modifiers,
    std::vector<OwnedPtr<AST::Annotation>> annos)
{
    return impl->ParseNodes(scope, currentMacroCall, modifiers, std::move(annos));
}

void Parser::ParseAnnotationArguments(AST::Annotation& anno) const
{
    return impl->ParseAnnotationArguments(anno);
}

OwnedPtr<AST::Annotation> Parser::ParseCustomAnnotation() const
{
    return impl->ParseCustomAnnotation();
}

DiagnosticEngine& Parser::GetDiagnosticEngine() const
{
    return impl->diag;
}
std::size_t Parser::GetProcessedTokens() const
{
    return impl->GetProcessedTokens();
}
std::string Parser::GetPrimaryDeclIdentRawValue() const
{
    return impl->GetPrimaryDeclIdentRawValue();
}
Parser& Parser::SetPrimaryDecl(const std::string& decl)
{
    impl->SetPrimaryDecl(decl);
    return *this;
}
size_t Parser::GetLineNum() const
{
    return impl->GetLineNum();
}
Parser& Parser::SetModuleName(const std::string& name)
{
    impl->moduleName = name;
    return *this;
}
Parser& Parser::SetForImport(bool isForImport)
{
    impl->forImport = isForImport;
    return *this;
}
Parser& Parser::SetCurFile(Ptr<AST::File> curFile)
{
    impl->currentFile = curFile;
    return *this;
}

Parser& Parser::EnableCustomAnno()
{
    impl->enableCustomAnno = true;
    return *this;
}

Parser& Parser::SetEHEnabled(bool enabled)
{
    impl->enableEH = enabled;
    impl->lexer->SetEHEnabled(enabled);
    return *this;
}

bool Parser::IsEHEnabled() const
{
    return impl->enableEH;
}

TokenVecMap Parser::GetCommentsMap() const
{
    return impl->commentsMap;
}

void Parser::SetCompileOptions(const GlobalOptions& opts)
{
    impl->backend = opts.backend;
    impl->scanDepPkg = opts.scanDepPkg;
    impl->calculateLineNum = opts.enableTimer || opts.enableMemoryCollect;
    // Effect handlers break backwards compatibility by introducing new
    // keywords, so we disable them from the parser unless the user
    // explicitly asks to compile with effect handler support
    SetEHEnabled(opts.enableEH);
}

bool Parser::Skip(TokenKind kind)
{
    return impl->Skip(kind);
}

const Token& Parser::Peek()
{
    return impl->Peek();
}
void Parser::Next()
{
    return impl->Next();
}
bool Parser::Seeing(TokenKind kind)
{
    return impl->Seeing(kind);
}
bool Parser::Seeing(TokenKind rangeLeft, TokenKind rangeRight)
{
    return impl->Seeing(rangeLeft, rangeRight);
}
bool Parser::SeeingAny(const std::vector<TokenKind>& kinds)
{
    return impl->SeeingAny(kinds);
}
bool Parser::Seeing(const std::vector<TokenKind>& kinds, bool skipNewline)
{
    return impl->Seeing(kinds, skipNewline);
}

bool Parser::SeeingCombinator(const std::vector<TokenKind>& kinds)
{
    return impl->SeeingCombinator(kinds);
}

bool Parser::SeeingTokenAndCombinator(TokenKind kind, const std::vector<TokenKind>& cmb)
{
    return impl->SeeingTokenAndCombinator(kind, cmb);
}

void Parser::SkipCombinator(const std::vector<TokenKind>& kinds)
{
    return impl->SkipCombinator(kinds);
}

const Token& Parser::LookAhead() const
{
    return impl->lookahead;
}
const Token& Parser::LastToken() const
{
    return impl->lastToken;
}

Ptr<Node> Parser::CurMacroCall() const
{
    return impl->curMacroCall;
}

Parser::~Parser()
{
    delete impl;
}
}