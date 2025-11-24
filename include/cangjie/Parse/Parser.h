// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the Parser related classes, which parses the source code to AST.
 */

#ifndef CANGJIE_PARSE_PARSER_H
#define CANGJIE_PARSE_PARSER_H

#include "cangjie/AST/Node.h"
#include "cangjie/AST/NodeX.h"
#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Lex/Lexer.h"
#include "cangjie/Option/Option.h"

namespace Cangjie {

enum class ScopeKind : uint8_t {
    TOPLEVEL = 0,
    CLASS_BODY,
    INTERFACE_BODY,
    STRUCT_BODY,
    EXTEND_BODY,
    FUNC_BODY,
    MACRO_BODY,
    PRIMARY_CONSTRUCTOR_BODY_FOR_CLASS,
    PRIMARY_CONSTRUCTOR_BODY_FOR_STRUCT,
    PROP_MEMBER_SETTER_BODY,
    PROP_MEMBER_GETTER_BODY,
    ENUM_BODY,
    ENUM_CONSTRUCTOR,
    PRIMARY_CONSTRUCTOR_FUNC_PARAM,
    MAIN_BODY,
    UNKNOWN_SCOPE
};

// ExprKind means the expected expr in parseExpr.
// e.g. VAR_INIT stands for all those expr who can be var initializer.
enum class ExprKind : uint8_t;

std::string ConvertToken(const Token& t);

/// Record the correspondence between AnnotationKinds and identifiers.
const std::unordered_map<std::string, AST::AnnotationKind> NAME_TO_ANNO_KIND = {
    {"JavaMirror", AST::AnnotationKind::JAVA_MIRROR}, {"JavaImpl", AST::AnnotationKind::JAVA_IMPL},
    {"JavaHasDefault", AST::AnnotationKind::JAVA_HAS_DEFAULT},
    {"ObjCMirror", AST::AnnotationKind::OBJ_C_MIRROR}, {"ObjCImpl", AST::AnnotationKind::OBJ_C_IMPL},
    {"ForeignGetterName", AST::AnnotationKind::FOREIGN_GETTER_NAME}, {"ForeignSetterName", AST::AnnotationKind::FOREIGN_SETTER_NAME},
    {"ObjCInit", AST::AnnotationKind::OBJ_C_INIT}, {"ObjCOptional", AST::AnnotationKind::OBJ_C_OPTIONAL},
    {"ForeignName", AST::AnnotationKind::FOREIGN_NAME},
    {"CallingConv", AST::AnnotationKind::CALLING_CONV}, {"C", AST::AnnotationKind::C},
    {"Attribute", AST::AnnotationKind::ATTRIBUTE}, {"Intrinsic", AST::AnnotationKind::INTRINSIC},
    {"OverflowThrowing", AST::AnnotationKind::NUMERIC_OVERFLOW},
    {"OverflowWrapping", AST::AnnotationKind::NUMERIC_OVERFLOW},
    {"OverflowSaturating", AST::AnnotationKind::NUMERIC_OVERFLOW}, {"When", AST::AnnotationKind::WHEN},
    {"FastNative", AST::AnnotationKind::FASTNATIVE}, {"Annotation", AST::AnnotationKind::ANNOTATION},
    {"ConstSafe", AST::AnnotationKind::CONSTSAFE}, {"Deprecated", AST::AnnotationKind::DEPRECATED},
    {"Frozen", AST::AnnotationKind::FROZEN}, {"EnsurePreparedToMock", AST::AnnotationKind::ENSURE_PREPARED_TO_MOCK}};

bool IsBuiltinAnnotation(const std::string& moduleName, const std::string& identifier);

inline bool IsIdentifierOrContextualKeyword(const TokenKind& kind)
{
    return kind == TokenKind::IDENTIFIER ||
        Utils::In(GetContextualKeyword(), [&kind](const TokenKind& tokenKind) { return kind == tokenKind; });
}

template <typename T> using PtrVector = std::vector<OwnedPtr<T>>;

/// The main class used for parsing.
class Parser {
public:
    /// Create Parser with \ref fileID and string \ref input.
    /// \param parseDeclFile true if this parser is used to parse .cj.d file. When parsing these files, do not report
    /// an error if the func is missing its body.
    Parser(unsigned int fileID, const std::string& input, DiagnosticEngine& diag, SourceManager& sm,
        bool attachComment = false, bool parseDeclFile = false);
    /// Create Parser with string \ref input and the position of the first token. Only used in macro reparse.
    Parser(const std::string& input, DiagnosticEngine& diag, SourceManager& sm, const Position& pos = {0, 1, 1},
        bool attachComment = false, bool parseDeclFile = false);
    /// Create Parser with tokens \ref inputTokens returned by macro func. Only used in macro reparse.
    Parser(const std::vector<Token>& inputTokens, DiagnosticEngine& diag, SourceManager& sm, bool attachComment = false,
        bool parseDeclFile = false);
    ~Parser();

    size_t GetLineNum() const;

    // Frontend/Macro/Sema.
    OwnedPtr<AST::File> ParseTopLevel();
    // Macro/Sema/stdlib.
    OwnedPtr<AST::Decl> ParseDecl(ScopeKind scopeKind);
    /// Parse expression entrance. No context info provided. This function is public only for testing.
    OwnedPtr<AST::Expr> ParseExpr();
    /// ParseExpr entrance from libast. The scope of this expr is unknown as it lacks a parsing context.
    OwnedPtr<AST::Expr> ParseExprLibast();
    // unittests.
    OwnedPtr<AST::Type> ParseType();
    OwnedPtr<AST::Pattern> ParsePattern();
    /// Parse annotation arguments from unprocessed tokens, and put them into \ref anno
    /// Used by fmt.
    void ParseAnnotationArguments(AST::Annotation& anno) const;
    OwnedPtr<AST::Annotation> ParseCustomAnnotation() const;
    // Macro.
    std::vector<OwnedPtr<AST::Node>> ParseNodes(std::variant<ScopeKind, ExprKind> scope, AST::Node& currentMacroCall,
        const std::set<AST::Modifier>& modifiers = {}, std::vector<OwnedPtr<AST::Annotation>> annos = {});

    Parser& EnableCustomAnno();
    std::size_t GetProcessedTokens() const;
    std::string GetPrimaryDeclIdentRawValue() const;

    Parser& SetEHEnabled(bool enabled);
    bool IsEHEnabled() const;

    Parser& SetModuleName(const std::string& name);
    Parser& SetPrimaryDecl(const std::string& decl);
    Parser& SetForImport(bool isForImport);
    Parser& SetCurFile(Ptr<AST::File> curFile);

    void SetCompileOptions(const GlobalOptions& opts);

    DiagnosticEngine& GetDiagnosticEngine() const;
    TokenVecMap GetCommentsMap() const;

    /// Issue a diagnostic. If the parsing context is in a macro call, consider it.
    template <typename... Args>
    DiagnosticBuilder ParseDiagnoseRefactor(DiagKindRefactor kind, const Position pos, Args&&... args)
    {
        auto n = MakeOwned<AST::Node>();
        n->begin = pos;
        n->end = pos + 1;
        if (CurMacroCall()) {
            n->EnableAttr(AST::Attribute::MACRO_EXPANDED_NODE);
            n->curMacroCall = CurMacroCall();
        }
        return GetDiagnosticEngine().DiagnoseRefactor(kind, *n, pos, std::forward<Args>(args)...);
    }

    template <typename... Args>
    DiagnosticBuilder ParseDiagnoseRefactor(DiagKindRefactor kind, const Range range, Args&&... args)
    {
        auto n = MakeOwned<AST::Node>();
        n->begin = range.begin;
        n->end = range.end;
        if (CurMacroCall()) {
            n->EnableAttr(AST::Attribute::MACRO_EXPANDED_NODE);
            n->curMacroCall = CurMacroCall();
        }
        return GetDiagnosticEngine().DiagnoseRefactor(kind, *n, range, std::forward<Args>(args)...);
    }

    template <typename... Args>
    DiagnosticBuilder ParseDiagnoseRefactor(DiagKindRefactor kind, const Token& token, Args&&... args)
    {
        auto n = MakeOwned<AST::Node>();
        n->begin = token.Begin();
        n->end = token.End();
        if (CurMacroCall()) {
            n->EnableAttr(AST::Attribute::MACRO_EXPANDED_NODE);
            n->curMacroCall = CurMacroCall();
        }
        return GetDiagnosticEngine().DiagnoseRefactor(kind, *n, token, std::forward<Args>(args)...);
    }

    template <typename... Args>
    DiagnosticBuilder ParseDiagnoseRefactor(DiagKindRefactor kind, const AST::Node& node, Args&&... args)
    {
        auto n = MakeOwned<AST::Node>();
        n->begin = node.begin;
        n->end = node.end;
        if (CurMacroCall()) {
            n->EnableAttr(AST::Attribute::MACRO_EXPANDED_NODE);
            n->curMacroCall = CurMacroCall();
        }
        return GetDiagnosticEngine().DiagnoseRefactor(kind, *n, std::forward<Args>(args)...);
    }
    /// Skip next token if token kind satisfied
    bool Skip(TokenKind kind);

    /// Attach comment to node (typically by macro expansion)
    void AttachCommentToNodes(std::vector<OwnedPtr<AST::Node>>& nodes);

protected:
    /// used by QueryParser
    /// Peek next token, save it to lookAhead
    const Token& Peek();
    /// Consume next token, put a sentinel symbol on it
    void Next();
    /// Whether the next token is the given token kind.
    bool Seeing(TokenKind kind);
    bool Seeing(TokenKind rangeLeft, TokenKind rangeRight);
    bool SeeingAny(const std::vector<TokenKind>& kinds);
    /// Whether the following tokens are the given token vector. 'skipNL=true' means NL token should be skipped when
    /// scan following tokens.
    bool Seeing(const std::vector<TokenKind>& kinds, bool skipNewline = true);

    /// See several tokens without space between them, skip all the tokens except the last token.
    bool SeeingCombinator(const std::vector<TokenKind>& kinds);

    /// Whether the next token is of the given token type and follows the given combined token
    bool SeeingTokenAndCombinator(TokenKind kind, const std::vector<TokenKind>& cmb);

    /// Skip tokens described by \ref kinds if possible.
    void SkipCombinator(const std::vector<TokenKind>& kinds);

    const Token& LookAhead() const;
    const Token& LastToken() const;
    Ptr<AST::Node> CurMacroCall() const;

private:
    class ParserImpl* impl;
};
} // namespace Cangjie

#endif // CANGJIE_PARSE_PARSER_H
