// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares class ParserImpl.
 */

#ifndef CANGJIE_PARSE_PARSERIMPL_H
#define CANGJIE_PARSE_PARSERIMPL_H

#include "cangjie/Parse/Parser.h"
#include "MPParserImpl.h"
#include "NativeFFI/FFIParserImpl.h"

namespace Cangjie {
using namespace AST;

const size_t PACKAGE_NAME_LEN_LIMIT = 200U;

enum class DefKind : uint8_t {
    FUNC = 0,
    MAIN,
    MACRO,
    CLASS,
    INTERFACE,
    VARIABLE,
    ENUM,
    STRUCT,
    EXTEND,
    TYPE,
    CONSTRUCTOR,
    PRIMARY_CONSTRUCTOR,
    PROPERTY,
    FINALIZER,
    PACKAGE
};

enum class ExprKind : uint8_t {
    ALL,
    VAR_INIT,
    INDEX_EXPR,
    IF_COND_EXPR,    // if condition is actually not an expression, but this is kept for code compatibility
    WHILE_COND_EXPR, // so does while condition
    LET_PATTERN,
    LEFT_EXPR,
    EXPR_IN_TUPLE,
    EXPR_IN_IF_COND_TUPLE,
    EXPR_IN_WHILE_COND_TUPLE,
    EXPR_IN_ARRAY,
    EXPR_IN_CALLSUFFIX,
    EXPR_IN_ANNOTATION,
    UNKNOWN_EXPR,
};

const std::vector<TokenKind>& GetTypeFirst();

class ParserImpl final {
public:
    ParserImpl(unsigned int fileID, const std::string& input, DiagnosticEngine& diag, SourceManager& sm,
        bool attachComment, bool parsingDeclFiles);
    ParserImpl(const std::string& input, DiagnosticEngine& diag, SourceManager& sm, const Position& pos,
        bool attachComment, bool parsingDeclFiles);
    ParserImpl(const std::vector<Token>& inputTokens, DiagnosticEngine& diag, SourceManager& sm, bool attachComment,
        bool parsingDeclFiles);
    ~ParserImpl();

    size_t GetLineNum() const;

    OwnedPtr<AST::File> ParseTopLevel();
    OwnedPtr<AST::Decl> ParseDecl(
        ScopeKind scopeKind, std::set<AST::Modifier> modifiers = {}, std::vector<OwnedPtr<AST::Annotation>> annos = {});
    OwnedPtr<AST::Expr> ParseExpr(ExprKind ek = ExprKind::ALL);
    OwnedPtr<AST::Type> ParseType();
    OwnedPtr<AST::Pattern> ParsePattern(
        const std::set<AST::Attribute>& attributes = {}, bool isVar = false, bool inDecl = false);
    std::vector<OwnedPtr<AST::Node>> ParseNodes(std::variant<ScopeKind, ExprKind> scope, AST::Node& currentMacroCall,
        const std::set<AST::Modifier>& modifiers = {}, std::vector<OwnedPtr<AST::Annotation>> annos = {});

    Parser& EnableCustomAnno();
    std::size_t GetProcessedTokens() const;
    std::string GetPrimaryDeclIdentRawValue() const;

    Parser& SetModuleName(const std::string& name);
    void SetPrimaryDecl(const std::string& decl, bool isRawId = false);
    Parser& SetForImport(const bool isForImport);
    Parser& SetCurFile(Ptr<AST::File> curFile);

    void SetCompileOptions(const GlobalOptions& opts);

    DiagnosticEngine& GetDiagnosticEngine() const;

    // Skip next token if token kind satisfied
    bool Skip(TokenKind kind);

    // Peek next token, save it to lookAhead
    const Token& Peek();
    // Consume next token, put a sentinel symbol on it
    void Next();
    // Whether the next token is the given token kind.
    bool Seeing(TokenKind kind)
    {
        return Peek().kind == kind;
    }
    inline bool Seeing(TokenKind rangeLeft, TokenKind rangeRight)
    {
        return Peek().kind >= rangeLeft && Peek().kind <= rangeRight;
    }
    inline bool SeeingAny(const std::vector<TokenKind>& kinds)
    {
        return Utils::In(Peek().kind, kinds);
    }
    // Whether the following tokens are the given token vector. 'skipNL=true' means NL token should be skipped when scan
    // following tokens.
    bool Seeing(const std::vector<TokenKind>& kinds, bool skipNewline = true);

    /** See several tokens without space between them, skip all the tokens except the last token. */
    bool SeeingCombinator(const std::vector<TokenKind>& kinds);

    /** Whether the next token is of the given token type and follows the given combined token*/
    bool SeeingTokenAndCombinator(TokenKind kind, const std::vector<TokenKind>& cmb);

    void SkipCombinator(const std::vector<TokenKind>& kinds);

private:
    DiagnosticEngine& diag;
    Token lookahead{TokenKind::SENTINEL, "", Position(0, 1, 1), Position(0, 1, 1)}; /**< Current scanned token. */
    Token lastToken{TokenKind::SENTINEL, "", Position(0, 1, 1), Position(0, 1, 1)}; /**< Last scanned token. */
    /**< Last scanned token which is not newline. */
    Token lastNoneNLToken{TokenKind::SENTINEL, "", Position(0, 1, 1), Position(0, 1, 1)};
    bool newlineSkipped{true};                  /**< Mark that a new line token has been skipped. */
    bool skipNL{true};                          /**< Skip new line token by default. */
    Position firstNLPosition{DEFAULT_POSITION}; /**< Record first nl position. */
    SourceManager& sourceManager;

    // This type can only appear in trait or class
    // member function return type. when parsing trait
    // or class member function return type, enableThis
    // should be set true, and set false while leaving.
    bool enableThis{false};
    bool deadlocked{false};
    bool inForeignBlock{false};
    bool enableCustomAnno{false}; // parse macrocall as custom annotation.

    std::unique_ptr<Lexer> lexer;
    std::string inputString{};
    bool forImport{false};
    std::string moduleName;

    inline bool SeeingTrailingClosure(OwnedPtr<AST::Expr>& baseExpr)
    {
        if (Seeing(TokenKind::LCURL)) {
            return true;
        }
        if (baseExpr->astKind != AST::ASTKind::CALL_EXPR && baseExpr->astKind != AST::ASTKind::REF_EXPR &&
            baseExpr->astKind != AST::ASTKind::ARRAY_EXPR) {
            return false;
        }
        // SeeingAnnotationTrailClosure.
        return SeeingAnnotationLambdaExpr() || SeeingAnnotationTrailingClosure({TokenKind::LSQUARE});
    }

    bool SkipAmbiguousToken();

    bool SkipCombinedDoubleArrow();

    inline bool SeeingImport()
    {
        return Seeing(TokenKind::IMPORT) || Seeing({TokenKind::PUBLIC, TokenKind::IMPORT}) ||
            Seeing({TokenKind::PROTECTED, TokenKind::IMPORT}) || Seeing({TokenKind::INTERNAL, TokenKind::IMPORT}) ||
            Seeing({TokenKind::PRIVATE, TokenKind::IMPORT});
    }
    inline bool SeeingPackage()
    {
        return Seeing(TokenKind::PACKAGE);
    }
    inline bool SeeingFeatures()
    {
        return Seeing(TokenKind::FEATURES);
    }
    inline bool SeeingMacroPackage()
    {
        return Seeing({TokenKind::MACRO, TokenKind::PACKAGE});
    }

    inline bool SeeingContextualKeyword()
    {
        return Utils::In(GetContextualKeyword(), [this](const TokenKind& kind) { return Seeing(kind); });
    }
    inline bool SeeingPrimaryKeyWordContext(TokenKind tk)
    {
        return Utils::In(GetContextualKeyword(), [this, &tk](const TokenKind& kind) { return Seeing({kind, tk}); });
    }
    // Avoid infinite loop when parsing parentheses and braces
    bool DetectPrematureEnd();
    // Continues skip when blank tokens appear
    void SkipBlank(TokenKind blank0, TokenKind blank1 = TokenKind::SENTINEL);
    std::string ParseForImportToGetContentBetween(
        unsigned int fileID, int beginLine, int beginColumn, int endLine, int endColumn);

    std::unordered_map<TokenKind, std::function<OwnedPtr<AST::Expr>(ParserImpl*)>> exprHandlerMap{
        {TokenKind::IF, &ParserImpl::ParseIfExpr},
        {TokenKind::MATCH, &ParserImpl::ParseMatchExpr},
        {TokenKind::QUOTE, &ParserImpl::ParseQuoteExpr},
        {TokenKind::TRY, &ParserImpl::ParseTryExpr},
        {TokenKind::THROW, &ParserImpl::ParseThrowExpr},
        {TokenKind::PERFORM, &ParserImpl::ParsePerformExpr},
        {TokenKind::RESUME, &ParserImpl::ParseResumeExpr},
        {TokenKind::RETURN, &ParserImpl::ParseReturnExpr},
        {TokenKind::FOR, &ParserImpl::ParseForInExpr},
        {TokenKind::WHILE, &ParserImpl::ParseWhileExpr},
        {TokenKind::DO, &ParserImpl::ParseDoWhileExpr},
        {TokenKind::SPAWN, &ParserImpl::ParseSpawnExpr},
        {TokenKind::SYNCHRONIZED, &ParserImpl::ParseSynchronizedExpr},
        {TokenKind::LPAREN, &ParserImpl::ParseLeftParenExpr},
        {TokenKind::CONTINUE, &ParserImpl::ParseContinueJumpExpr},
        {TokenKind::BREAK, &ParserImpl::ParseBreakJumpExpr},
        {TokenKind::LSQUARE, &ParserImpl::ParseArrayLitExpr},
        {TokenKind::THIS, &ParserImpl::ParseThisOrSuper},
        {TokenKind::SUPER, &ParserImpl::ParseThisOrSuper},
        {TokenKind::LCURL, &ParserImpl::ParseLambdaExpr},
        {TokenKind::UNSAFE, &ParserImpl::ParseUnsafeBlock},
        {TokenKind::VARRAY, &ParserImpl::ParseVArrayExpr},
    };

    std::unordered_map<TokenKind,
        std::function<OwnedPtr<AST::Decl>(
            ParserImpl*, ScopeKind, const std::set<AST::Modifier>&, std::vector<OwnedPtr<AST::Annotation>>)>>
        declHandlerMap{
            {TokenKind::CLASS, &ParserImpl::ParseClassDecl},
            {TokenKind::INTERFACE, &ParserImpl::ParseInterfaceDecl},
            {TokenKind::MAIN, &ParserImpl::ParseMainDecl},
            {TokenKind::MACRO, &ParserImpl::ParseMacroDecl},
            {TokenKind::STRUCT, &ParserImpl::ParseStructDecl},
            {TokenKind::ENUM, &ParserImpl::ParseEnumDecl},
            {TokenKind::TYPE, &ParserImpl::ParseTypeAlias},
            {TokenKind::EXTEND, &ParserImpl::ParseExtendDecl},
            {TokenKind::PROP, &ParserImpl::ParsePropDecl},
            {TokenKind::FUNC, &ParserImpl::ParseFuncDecl},
            {TokenKind::INIT, &ParserImpl::ParseConstructor},
            {TokenKind::VAR, &ParserImpl::ParseVarOrLet},
            {TokenKind::LET, &ParserImpl::ParseVarOrLet},
        };

    const std::map<ExprKind, std::pair<TokenKind, TokenKind>> exprsFollowedCommas{
        {ExprKind::EXPR_IN_TUPLE, {TokenKind::LPAREN, TokenKind::RPAREN}},
        {ExprKind::EXPR_IN_ARRAY, {TokenKind::LSQUARE, TokenKind::RSQUARE}},
        {ExprKind::EXPR_IN_CALLSUFFIX, {TokenKind::LPAREN, TokenKind::RPAREN}},
        {ExprKind::EXPR_IN_ANNOTATION, {TokenKind::LSQUARE, TokenKind::RSQUARE}},
    };

    const std::map<std::vector<TokenKind>, std::pair<TokenKind, std::string_view>> combinator{
        {{TokenKind::GT, TokenKind::GT, TokenKind::ASSIGN}, {TokenKind::RSHIFT_ASSIGN, ">>="}},
        {{TokenKind::GT, TokenKind::ASSIGN}, {TokenKind::GE, ">="}},
        {{TokenKind::GT, TokenKind::GT}, {TokenKind::RSHIFT, ">>"}},
        {{TokenKind::QUEST, TokenKind::QUEST}, {TokenKind::COALESCING, "??"}}};

    // Note that RSHIFT_ASSIGN should come before RSHIFT, because RSHIFT_ASSIGN contains RSHIFT.
    const std::vector<std::tuple<TokenKind, std::vector<TokenKind>, std::string>> ambiguoussCombinedTokens{
        {TokenKind::COALESCING, {TokenKind::QUEST, TokenKind::QUEST}, "??"},
        {TokenKind::RSHIFT_ASSIGN, {TokenKind::GT, TokenKind::GT, TokenKind::ASSIGN}, ">>="},
        {TokenKind::RSHIFT, {TokenKind::GT, TokenKind::GT}, ">>"},
        {TokenKind::GE, {TokenKind::GT, TokenKind::ASSIGN}, ">="},
    };

    const std::vector<TokenKind> combinedDoubleArrow{TokenKind::ASSIGN, TokenKind::GT};
    static const std::vector<TokenKind> combinedBackarrow;

    friend class ParserScope;
    friend class ChainScope;
    Ptr<AST::File> currentFile{nullptr};
    Ptr<AST::Node> curMacroCall{nullptr};
    std::string lastPrimaryDeclIdent;
    bool lastPrimaryDeclIdentIsRaw{false};
    std::string curPrimaryDeclIdent;
    bool curPrimaryDeclIdentIsRaw{false};
    std::vector<TokenKind> bracketsStack; /* A stack to help match brackets. */
    bool scanDepPkg{false};
    bool enableAttachComment{false};
    bool parseDeclFile{false};

    bool enableEH{false};
    bool enableInteropCJMapping{false};
    GlobalOptions::InteropLanguage targetInteropLanguage{GlobalOptions::InteropLanguage::NA};
    Triple::BackendType backend{Triple::BackendType::CJNATIVE};
    bool calculateLineNum{false};
    // we store line number info from all tokens
    // pair<line num of the begin, line num of the end>
    std::list<std::pair<unsigned, unsigned>> allTokensInOneFile;
    TokenVecMap commentsMap;
    uint8_t Precedence(TokenKind kind) const;

    // cjmp parser implementation class
    friend class MPParserImpl;
    class MPParserImpl* mpImpl;

    friend class FFIParserImpl;
    friend class JFFIParserImpl;
    friend class OCFFIParserImpl;
    class FFIParserImpl* ffiParser;

    /// Common initializer shared among constructors.
    void Init();

    // This is parsing chain currently, only used for diagnostic.
    std::vector<Ptr<AST::Node>> chainedAST;
    std::unordered_set<Ptr<AST::Decl>> intrinsics{}; /* A set to record intrinsic function. */
    /**
     * Attach comment group to the ast nodes in File
     */
    void AttachCommentToFile(Ptr<AST::File> node);

    void AttachCommentToNodes(std::vector<OwnedPtr<Node>>& nodes);
    bool SeeingExprOperator();
    Token GetExprOperator();
    void SkipExprOperator();
    bool SeeingNamedFuncArgs();
    bool SeeingKeywordAndOperater();
    bool SeeingKeywordWithDecl();
    bool SeeingParamInMacroCallExpr();
    bool SeeingIdentifierAndTargetOp(const std::vector<TokenKind>& tokenKinds);
    bool SeeingInvaildParamListInLambdaExpr();
    bool SeeingInvaildOperaterInLambdaExpr();
    bool SeeingAnnotationTrailingClosure(const std::vector<TokenKind>& tokenKinds);
    bool IsRawIdentifier(const std::string& identifier) const;
    std::string ParseNameFromRawIdentifier(const std::string& rawIdentifier) const;
    SrcIdentifier ParseIdentifierFromToken(const Token& token) const;
    SrcIdentifier ParseIdentifierFromName(
        const std::string& identifier, const Position& tkPos, const Position& end, size_t len) const;
    SrcIdentifier ExpectIdentifierWithPos(AST::Node& node);
    SrcIdentifier ExpectPackageIdentWithPos(AST::Node& node);
    std::string ExpectOperatorIdentifier(AST::FuncDecl& fd);
    inline bool IsExprFollowedComma(ExprKind ek) const
    {
        return exprsFollowedCommas.find(ek) != exprsFollowedCommas.end();
    }
    inline bool SeeingAnnotationLambdaExpr()
    {
        return Seeing({TokenKind::AT, TokenKind::IDENTIFIER, TokenKind::LCURL}) && SeeingBuiltinAnnotation();
    }
    bool SeeingPrimTypes()
    {
        return (Peek().kind >= TokenKind::INT8) && (Peek().kind <= TokenKind::UNIT);
    }
    bool SeeingKeyword()
    {
        return (Peek().kind >= TokenKind::INT8 && Peek().kind < TokenKind::IDENTIFIER) || Peek().kind == TokenKind::AS;
    }
    bool SeeingOperator()
    {
        return (Peek().kind >= TokenKind::DOT) && (Peek().kind <= TokenKind::WILDCARD);
    }
    bool SeeingSoftKeyword()
    {
        return Peek().kind == TokenKind::FEATURES;
    }
    bool SeeingExpr();
    // modifiers
    bool SeeingModifier();
    bool SeeingLiteral()
    {
        return SeeingAny({TokenKind::INTEGER_LITERAL, TokenKind::RUNE_BYTE_LITERAL, TokenKind::BOOL_LITERAL,
            TokenKind::STRING_LITERAL, TokenKind::JSTRING_LITERAL, TokenKind::MULTILINE_STRING,
            TokenKind::MULTILINE_RAW_STRING, TokenKind::RUNE_LITERAL, TokenKind::FLOAT_LITERAL,
            TokenKind::UNIT_LITERAL});
    }
    bool SeeingPrimitiveTypeAndLParen()
    {
        return Seeing({TokenKind::INT8, TokenKind::LPAREN}) || Seeing({TokenKind::INT16, TokenKind::LPAREN}) ||
            Seeing({TokenKind::INT32, TokenKind::LPAREN}) || Seeing({TokenKind::INT64, TokenKind::LPAREN}) ||
            Seeing({TokenKind::INTNATIVE, TokenKind::LPAREN}) || Seeing({TokenKind::UINT8, TokenKind::LPAREN}) ||
            Seeing({TokenKind::UINT16, TokenKind::LPAREN}) || Seeing({TokenKind::UINT32, TokenKind::LPAREN}) ||
            Seeing({TokenKind::UINT64, TokenKind::LPAREN}) || Seeing({TokenKind::UINTNATIVE, TokenKind::LPAREN}) ||
            Seeing({TokenKind::FLOAT16, TokenKind::LPAREN}) || Seeing({TokenKind::FLOAT32, TokenKind::LPAREN}) ||
            Seeing({TokenKind::FLOAT64, TokenKind::LPAREN}) || Seeing({TokenKind::RUNE, TokenKind::LPAREN});
    }
    bool SeeingPrimitiveTypeAndDot()
    {
        return Seeing({TokenKind::INT8, TokenKind::DOT}) || Seeing({TokenKind::INT16, TokenKind::DOT}) ||
            Seeing({TokenKind::INT32, TokenKind::DOT}) || Seeing({TokenKind::INT64, TokenKind::DOT}) ||
            Seeing({TokenKind::INTNATIVE, TokenKind::DOT}) || Seeing({TokenKind::UINT8, TokenKind::DOT}) ||
            Seeing({TokenKind::UINT16, TokenKind::DOT}) || Seeing({TokenKind::UINT32, TokenKind::DOT}) ||
            Seeing({TokenKind::UINT64, TokenKind::DOT}) || Seeing({TokenKind::UINTNATIVE, TokenKind::DOT}) ||
            Seeing({TokenKind::FLOAT16, TokenKind::DOT}) || Seeing({TokenKind::FLOAT32, TokenKind::DOT}) ||
            Seeing({TokenKind::FLOAT64, TokenKind::DOT}) || Seeing({TokenKind::RUNE, TokenKind::DOT}) ||
            Seeing({TokenKind::BOOLEAN, TokenKind::DOT}) || Seeing({TokenKind::UNIT, TokenKind::DOT}) ||
            Seeing({TokenKind::NOTHING, TokenKind::DOT});
    }

    bool SkipOperator()
    {
        return Skip(TokenKind::NOT) || Skip(TokenKind::ADD) || Skip(TokenKind::SUB) || Skip(TokenKind::EXP) ||
            Skip(TokenKind::MUL) || Skip(TokenKind::DIV) || Skip(TokenKind::MOD) || Skip(TokenKind::LSHIFT) ||
            Skip(TokenKind::LT) || Skip(TokenKind::LE) || Skip(TokenKind::GT) || Skip(TokenKind::EQUAL) ||
            Skip(TokenKind::NOTEQ) || Skip(TokenKind::BITAND) || Skip(TokenKind::BITXOR) || Skip(TokenKind::BITOR);
    }

    bool SkipKeyWordIdentifier()
    {
        return Utils::In(GetContextualKeyword(), [this](const TokenKind& kind) { return Skip(kind); });
    }

    bool SkipNLOrSemi()
    {
        bool hasNLOrSemi = newlineSkipped;
        auto preSkip = skipNL;
        skipNL = false;
        while (SeeingAny({TokenKind::SEMI, TokenKind::NL})) {
            hasNLOrSemi = true;
            Next();
        }
        skipNL = preSkip;
        return hasNLOrSemi;
    }
    /** Skip all overloading operator and return the operator's token. */
    Token SkipAndReturnOverloadingOperator();
    /// Seeing IfAvailable Expr
    bool SeeingIfAvailable()
    {
        if (!Seeing(TokenKind::AT)) {
            return false;
        }
        // Get annotation identifier.
        auto tokens = lexer->LookAheadSkipNL(1);
        return !tokens.empty() && tokens.begin()->kind == TokenKind::IDENTIFIER &&
            tokens.begin()->Value() == IF_AVAILABLE;
    }
    /// A valid ifAvailable expression has the following form.
    /// @IfAvailable(paramName: arg, lambda1, lambda2)
    OwnedPtr<AST::IfAvailableExpr> ParseIfAvailable();
    bool SeeingDecl()
    {
        if (SeeingIfAvailable()) {
            return false;
        }
        return (SeeingModifier() && !Seeing({TokenKind::UNSAFE, TokenKind::LCURL})) ||
            SeeingAny(
                {TokenKind::FUNC, TokenKind::MACRO, TokenKind::LET, TokenKind::VAR, TokenKind::ENUM, TokenKind::TYPE,
                    TokenKind::STRUCT, TokenKind::CLASS, TokenKind::INTERFACE, TokenKind::MAIN, TokenKind::EXTEND}) ||
            (SeeingBuiltinAnnotation() && !SeeingAnnotationLambdaExpr());
    }
    bool SeeingPrimaryIdentifer();
    bool SeeingPrimaryCtorDecl(const ScopeKind& scopeKind)
    {
        if (Utils::NotIn(scopeKind, {ScopeKind::STRUCT_BODY, ScopeKind::CLASS_BODY})) {
            return false;
        }
        return Seeing(TokenKind::INIT) || SeeingPrimaryIdentifer();
    }
    bool SeeingPrimaryConstructor(const ScopeKind& scopeKind)
    {
        return (scopeKind == ScopeKind::STRUCT_BODY || scopeKind == ScopeKind::CLASS_BODY) && SeeingPrimaryIdentifer();
    }
    bool SeeingPropMember(const ScopeKind& scopeKind)
    {
        return scopeKind == ScopeKind::PROP_MEMBER_SETTER_BODY || scopeKind == ScopeKind::PROP_MEMBER_GETTER_BODY;
    }

    bool SeeingPropMember()
    {
        return Seeing(TokenKind::IDENTIFIER) && (lookahead == "get" || lookahead == "set");
    }

    bool SeeingEnumConstructor(const ScopeKind& scopeKind)
    {
        return (Seeing(TokenKind::IDENTIFIER) || SeeingContextualKeyword()) &&
            (scopeKind == ScopeKind::ENUM_BODY || scopeKind == ScopeKind::ENUM_CONSTRUCTOR);
    }
    bool SeeingInitializer()
    {
        return Seeing(TokenKind::INIT);
    }
    bool SeeingFinalizer()
    {
        if (Seeing({TokenKind::BITNOT, TokenKind::INIT}, false)) {
            return true;
        }
        return false;
    }
    bool SeeingIllegalDeclInBlock()
    {
        // Other Decls that illegal in FuncBody.
        return SeeingAny({TokenKind::CLASS, TokenKind::INIT, TokenKind::INTERFACE, TokenKind::EXTEND, TokenKind::STRUCT,
                   TokenKind::PROP}) ||
            SeeingImport();
    }
    // When a children ast is broken, the father ast will be broken. Only used on diagnostic.
    void SpreadAttrAndConsume(Ptr<const AST::Node> source, Ptr<AST::Node> target, std::vector<TokenKind>&& kind);
    bool CanMatchBracketInStack();
    void SkipPairedBrackets();
    /*
     * Common part of all consume strategies.
     */
    bool ConsumeCommon(bool& flag);

    /*
     * Control if consume target token.
     */
    void TargetTokenConsumedControl(bool& flag, bool targetTokenConsumed);
    /*
     * Consume tokens until start of declaration.
     */
    void ConsumeUntilDecl(TokenKind kind = TokenKind::SENTINEL);
    /*
     * Consume tokens until start of declaration.
     */
    void ConsumeUntilDeclOrNL(TokenKind kind = TokenKind::SENTINEL);
    /*
     * Consume tokens until target token, and if newline consumed the skipNL will be true.
     */
    void ConsumeUntil(TokenKind kind, bool targetTokenConsumed = true);

    /*
     * Consume tokens until target tokens, and if newline consumed the skipNL will be true.
     * If token NL is among target tokens, it will be always consumed.
     */
    void ConsumeUntilAny(std::vector<TokenKind>&& tokens, bool targetTokenConsumed = true);

    void ConsumeUntilAny(const std::function<bool()>& functor, bool targetTokenConsumed = true);

    bool TryConsumeUntilAny(std::vector<TokenKind> tokens);

    template <class type> static OwnedPtr<type> MakeInvalid(Position pos)
    {
        static_assert(std::is_convertible_v<type, AST::Node>, "can only make invalid node inherited from AST::Node");
        auto n = MakeOwned<type>();
        n->EnableAttr(AST::Attribute::IS_BROKEN);
        n->begin = pos;
        n->end = pos;
        return n;
    }
    std::vector<OwnedPtr<AST::Decl>> ParseForeignDecls(
        const std::set<AST::Modifier>& modifiers, PtrVector<AST::Annotation>& annos);
    void AssignCurFile(const OwnedPtr<AST::File>& file) const;
    void ParseWhenModifierHandler(std::vector<OwnedPtr<AST::Decl>>& ret, std::vector<OwnedPtr<AST::Annotation>>& annos);

    OwnedPtr<AST::PackageSpec> ParsePackageHeader(std::set<AST::Modifier>&& modifiers);
    // return: seeing end of file.
    bool ParsePackageHeaderEnd();
    void CheckAndHandleUnexpectedTopLevelDeclAfterFeatures();
    void CheckExpectedTopLevelDeclWhenNoPackage(
        const PtrVector<AST::Annotation>& annos, const std::set<AST::Modifier>& modifiers);
    bool IsExpectedTokenAfterFeaturesOrPackage(bool allowPackageKeyword);
    void ParseTopLevelDecls(AST::File& file, std::vector<OwnedPtr<AST::Annotation>>& annos);
    void ParseTopLevelDecl(AST::File& file, std::vector<OwnedPtr<AST::Annotation>>& annos);
    void ParseAnnotations(PtrVector<AST::Annotation>& annos);
    void SetBeginToAnnotationsBegin(Node& node, const PtrVector<Annotation>& annos);
    OwnedPtr<AST::Annotation> ParseCustomAnnotation();
    OwnedPtr<AST::Annotation> ParseAnnotation();
    void ParseModifiers(std::set<AST::Modifier>& modifiers);
    OwnedPtr<AST::MainDecl> ParseMainDecl(
        ScopeKind scopeKind, const std::set<AST::Modifier>& modifiers, std::vector<OwnedPtr<AST::Annotation>> annos);
    OwnedPtr<AST::Decl> ParseVarWithPatternDecl(
        ScopeKind scopeKind, const std::set<AST::Modifier>& modifiers, const Token& keyToken);
    OwnedPtr<AST::Pattern> ParseIrrefutablePattern(
        const std::set<AST::Attribute>& attributes, bool isVar = false, bool inDecl = false);
    OwnedPtr<AST::Decl> ParseVarOrLetOrConst(ScopeKind scopeKind, const std::set<AST::Modifier>& modifiers,
        std::vector<OwnedPtr<AST::Annotation>> annos, const Token& keyToken);
    OwnedPtr<AST::Decl> ParseVarOrLet(ScopeKind scopeKind, const std::set<AST::Modifier>& modifiers,
        std::vector<OwnedPtr<AST::Annotation>> annos = {});
    OwnedPtr<AST::Decl> ParseConstVariable(
        ScopeKind scopeKind, const std::set<AST::Modifier>& modifiers, std::vector<OwnedPtr<AST::Annotation>> annos);
    void ImplementConsumeStrategy(ScopeKind sc);
    OwnedPtr<AST::Decl> ParsePropDecl(ScopeKind scopeKind, const std::set<AST::Modifier>& modifiers,
        std::vector<OwnedPtr<AST::Annotation>> annos = {});
    void DiagMissingPropertyBody(AST::PropDecl& prop);
    void ParsePropBody(const std::set<AST::Modifier>& modifiers, AST::PropDecl& propDecl);
    OwnedPtr<AST::FuncDecl> ParsePropMemberDecl(const std::set<AST::Modifier>& modifiers);
    void ParseFuncGenericConstraints(const AST::FuncBody& fb);
    void ParsePropMemberBody(const ScopeKind& scopeKind, AST::FuncBody& fb);
    void ParseFuncParameters(const ScopeKind& scopeKind, AST::FuncBody& fb);
    OwnedPtr<AST::MacroDecl> ParseMacroDecl(ScopeKind scopeKind, const std::set<AST::Modifier>& modifiers,
        std::vector<OwnedPtr<AST::Annotation>> annos = {});
    bool ParseMacroCallTokens(TokenKind left, std::vector<Token>& tokens);
    bool ParseMacroCallTokens(const TokenKind& left, const TokenKind& right, std::vector<Token>& tokens, bool isAttr);
    bool ParseMacroCallEscapeTokens(const TokenKind& left, std::vector<Token>& tokens, bool isAttr);
    void ParseNodeToTokens(const AST::Node& node, const Token& begin, std::vector<Token>& tokens);
    void ParseMacroCallAttr(AST::MacroInvocation& invocation);
    void ParseMacroCallArg(
        ScopeKind scopeKind, const std::set<AST::Modifier>& modifiers, AST::MacroInvocation& invocation);
    void ParseMacroCallArgsWithParen(AST::MacroInvocation& invocation);
    void ParseMacroCallArgsWithoutParen(
        ScopeKind scopeKind, const std::set<AST::Modifier>& modifiers, AST::MacroInvocation& invocation);
    void ParseMacroCallDeclInput(
        ScopeKind scopeKind, const std::set<AST::Modifier>& modifiers, AST::MacroInvocation& invocation);
    void ParseMacroInvocation(
        ScopeKind scopeKind, const std::set<AST::Modifier>& modifiers, AST::MacroInvocation& invocation);
    bool ParseMacroCallIdentifier(AST::MacroInvocation& invocation, AST::Node& node);
    template <typename T>
    OwnedPtr<T> ParseMacroCall(ScopeKind scopeKind = ScopeKind::UNKNOWN_SCOPE,
        const std::set<AST::Modifier>& modifiers = {}, std::vector<OwnedPtr<AST::Annotation>> annos = {});
    OwnedPtr<AST::FuncBody> ParseFuncBody(ScopeKind scopeKind);
    void CheckMacroParamType(AST::Type& type, bool isReturnTy = false);
    OwnedPtr<AST::FuncBody> ParseMacroBody(AST::MacroDecl& macro);
    OwnedPtr<AST::FuncParamList> ParseMacroParameterList();
    void ParseEllipsisParameter(AST::FuncParam& funcParam);
    void ParseParameter(ScopeKind scopeKind, AST::FuncParam& fp);
    void ParseAssignInParam(AST::FuncParam& fp);
    OwnedPtr<AST::FuncParamList> ParseParameterList(ScopeKind scopeKind = ScopeKind::UNKNOWN_SCOPE);
    void CheckSetterAnnotations(std::vector<OwnedPtr<AST::Annotation>>& annos, const Ptr<AST::Node> setter);
    void CheckGetterAnnotations(std::vector<OwnedPtr<AST::Annotation>>& annos, const Ptr<AST::Node> getter);
    void CheckModifierInParamList(
        const ScopeKind& scopeKind, const std::set<AST::Modifier>& modifiers, bool isMember, AST::FuncParam& fp);
    void ParseLetOrVarInParamList(const ScopeKind& scopeKind, AST::FuncParam& fp, Position& memberStartPos);
    template <typename T> void CheckIntrinsicFunc(T& fd);
    OwnedPtr<AST::FuncParam> ParseParamInParamList(
        const ScopeKind& scopeKind, Ptr<AST::FuncParam>& meetNamedParameter, Ptr<AST::FuncParam>& meetMemberParams);
    void ParseInterfaceDeclOrClassDeclGeneric(AST::InheritableDecl& ret);
    void SetDefaultFunc(ScopeKind scopeKind, AST::Decl& decl) const;
    void ParseCaseBody(AST::EnumDecl& enumDecl);
    void ParseEnumBody(AST::EnumDecl& enumDecl);
    OwnedPtr<AST::Decl> ParseEnumConstructorWithArgs(const Token& id, PtrVector<AST::Annotation>& annos);
    OwnedPtr<AST::Decl> ParseNoArgsEnumConstructor(const Token& id, PtrVector<AST::Annotation>& annos);
    OwnedPtr<AST::StructBody> ParseStructBody(AST::StructDecl& sd);
    void ParseStructInheritedTypes(AST::StructDecl& structDecl);
    void ParseExtendedType(AST::ExtendDecl& extendDecl);
    void ParseExtendBody(AST::ExtendDecl& ed);
    OwnedPtr<AST::RefType> ParseRefType(bool onlyRef = false);
    OwnedPtr<AST::Type> ParseBaseType();
    OwnedPtr<AST::Type> ParseQualifiedType();
    OwnedPtr<AST::Type> ParseVarrayType();
    // Processes paren-related types, such as ParentType, TupleType, FuncType, and CFuncType.
    OwnedPtr<AST::Type> ParseTypeWithParen();
    OwnedPtr<AST::Type> ParseTupleType(
        std::vector<OwnedPtr<AST::Type>> types, const Position lParenPos, const Position rParenPos) const;
    OwnedPtr<AST::ParenType> ParseParenType(
        const Position& lParenPos, const Position& rParenPos, OwnedPtr<AST::Type> type);
    OwnedPtr<AST::FuncType> ParseFuncType(
        std::vector<OwnedPtr<AST::Type>> types, const Position& lParenPos, const Position& rParenPos);
    OwnedPtr<AST::Type> ParsePrefixType();
    OwnedPtr<AST::GenericParamDecl> ParseGenericParamDecl();
    OwnedPtr<AST::Generic> ParseGeneric();
    /* return type is `<bool, vector<unique_ptr<Type>>>`
     * 1. `bool` indicates whether source input `<...>` is (recognized as) a `generic arg list`
     * 2. vector contains parsed `Type`
     */
    std::pair<bool, std::vector<OwnedPtr<AST::Type>>> ParseTypeArguments(ExprKind ek = ExprKind::UNKNOWN_EXPR);
    bool TypeArgsMaybeConfusedWithExprWithComma(const std::vector<OwnedPtr<AST::Type>>& typeArgs) const;
    bool TypesMaybeConfusedWithExprWithComma(const std::vector<Ptr<AST::Type>>& types) const;

    bool IsLegFollowForGenArgInExprWithComma(ExprKind ek = ExprKind::ALL);
    std::vector<OwnedPtr<AST::GenericConstraint>> ParseGenericConstraints();
    bool ParseGenericUpperBound(const OwnedPtr<AST::GenericConstraint>& genericConstraint);
    static bool HasModifier(const std::set<AST::Modifier>& modifiers, TokenKind tk);
    static bool HasAnnotation(const std::vector<OwnedPtr<AST::Annotation>>& annos, AST::AnnotationKind ak);

    bool IsLeftValueExpression(const OwnedPtr<AST::Expr>& expr, const Token& tok);
    bool IsLeftAuxExpression(const OwnedPtr<AST::Expr>& expr, const Token& tok);
    bool CheckMacroExprRules(const Token& pre, const Token& tok, AST::Expr& expr);
    OwnedPtr<AST::Expr> MakeOperatorExpr(OwnedPtr<AST::Expr>& lExpr, const Token& oTok);
    bool IsNoneAssociative(const Token& tok) const;
    /// Register \param rExpr as part of \param expr, taking its memory and set necessary values.
    /// This should be called on right expr of a binary expr, or initializer of an initialization/assignment expr,
    /// for example.
    void RegisterRightExpr(const OwnedPtr<AST::Expr>& expr, OwnedPtr<AST::Expr>&& rExpr);
    void SetUnsafe(Ptr<AST::Node> node, const std::set<AST::Modifier>& modifiers = {});
    void ParseIncOrDec(OwnedPtr<AST::Expr>& baseExpr);
    OwnedPtr<AST::Expr> ParseUnaryExpr(ExprKind ek = ExprKind::ALL);
    /// Parse UnaryExpr, PrefixExpr, PostfixExpr, or AtomExpr
    OwnedPtr<AST::Expr> ParseBaseExpr(OwnedPtr<AST::Expr> expr = nullptr, ExprKind ek = ExprKind::ALL);
    // Expr means base. Some ast need to parse base outside and register to this api.
    OwnedPtr<AST::Expr> ParseExpr(const Token& preT, OwnedPtr<AST::Expr> expr = nullptr, ExprKind ek = ExprKind::ALL);
    void ParseExprWithRightExprOrType(OwnedPtr<AST::Expr>& base, const Token& tok, ExprKind ek);
    static bool IsConditionExpr(ExprKind ek);

    OwnedPtr<AST::Expr> ParseIndexAccess();
    OwnedPtr<AST::LetPatternDestructor> ParseLetPattern(ExprKind ek);
    OwnedPtr<AST::Expr> ParseSubscriptExpr(OwnedPtr<AST::Expr> baseExpr);
    OwnedPtr<AST::MemberAccess> ParseMemberAccess(OwnedPtr<AST::Expr> baseExpr, ExprKind ek = ExprKind::ALL);
    OwnedPtr<AST::Expr> ParseCallExpr(OwnedPtr<AST::Expr> baseExpr);
    AST::SuffixKind ParseSuffix(OwnedPtr<AST::Expr>& baseExpr);
    void ParseBaseExprPostfix(OwnedPtr<AST::Expr>& baseExpr, ExprKind ek = ExprKind::ALL);
    OwnedPtr<AST::Expr> ParseOptionalExpr(
        const Position questPos, OwnedPtr<AST::Expr> baseExpr, AST::SuffixKind suffix) const;
    void ParseQuestSuffixExpr(OwnedPtr<AST::Expr>& expr);
    OwnedPtr<AST::Expr> ParseAtom(ExprKind ek = ExprKind::ALL);
    bool IsNeedToCreateOptionalChain(TokenKind token, AST::Expr& expr) const;
    /**
     * Parses expressions starting with '('
     *
     * - ParenExpr    -- parenthesized expressions
     * - UnitExpr     -- the 0-tuple '()'
     * - TupleLiteral -- a tuple
     */
    OwnedPtr<AST::Expr> ParseLeftParenExpr();
    OwnedPtr<AST::Expr> ParseLeftParenExprInKind(ExprKind ek);
    OwnedPtr<AST::TupleLit> ParseTupleLitForParenExpr(const Position& leftParenPos);
    OwnedPtr<AST::TupleLit> ParseTupleLitForParenExprComma(const Position& leftParenPos, OwnedPtr<AST::Expr> expr);
    /**
     * Parses expressions starting with '[': array literals. like: [1, 2, 3]
     */
    OwnedPtr<AST::Expr> ParseArrayLitExpr();
    OwnedPtr<AST::Expr> ParseTypeConvExpr();
    OwnedPtr<AST::Expr> ParseBreakJumpExpr();
    OwnedPtr<AST::Expr> ParseContinueJumpExpr();
    OwnedPtr<AST::Expr> ParseLitConst();
    OwnedPtr<AST::Expr> ParseNegativeLiteral();
    OwnedPtr<AST::Expr> ParseInterpolationExpr(const std::string& value, const Position& pos);
    OwnedPtr<AST::LitConstExpr> GetLitConstExprFromStr(
        const std::string& value, const Token& token, const Position& pos) const;
    /**
     * Combines zero or more string fragments into a concatenation of these fragments. String fragments are
     * either literal strings, or string interpolations, arbitrary expressions wrapped in code that converts
     * that expression's result to a string.
     */
    OwnedPtr<AST::Expr> ProcessStringInterpolation(const Token& token);
    OwnedPtr<AST::TrailingClosureExpr> ParseTrailingClosureExpr(OwnedPtr<AST::Expr> baseExpr);
    OwnedPtr<AST::Expr> ParseMacroExprOrLambdaExpr();
    OwnedPtr<AST::Expr> ParseAnnotationLambdaExpr(bool isTailClosure = false);
    OwnedPtr<AST::FuncParamList> ParseFuncParamListInLambdaExpr();
    OwnedPtr<FuncParam> ParseFuncParam();
    OwnedPtr<AST::FuncBody> ParseFuncBodyInLambdaExpr(bool isTailClosure = false);
    OwnedPtr<AST::LambdaExpr> ParseLambdaExpr();
    OwnedPtr<AST::Expr> ParseVArrayExpr();
    OwnedPtr<AST::LambdaExpr> ParseLambdaExprWithTrailingClosure();
    OwnedPtr<AST::Expr> ParseIfExpr();
    void ParseElse(AST::IfExpr& ret);
    void ConsumeUntilIfExprEnd();
    /// Check if the given condition is has a condition subclause.
    /// Side Effects: if a non &&, || is used to connect to conditions, emit a diagnostic.
    /// \returns whether this condition contains a condition subclause. The return value is only meant to be used
    /// recursively, and it DOES NOT mean whether the condition is good or not.
    bool CheckCondition(AST::Expr* e);
    OwnedPtr<AST::ThrowExpr> ParseThrowExpr();
    OwnedPtr<AST::PerformExpr> ParsePerformExpr();
    OwnedPtr<AST::ResumeExpr> ParseResumeExpr();
    // Parse 'this' or 'super'.
    OwnedPtr<AST::RefExpr> ParseThisOrSuper() const;
    OwnedPtr<AST::ReturnExpr> ParseReturnExpr();
    OwnedPtr<AST::QuoteExpr> ParseQuoteExpr();
    void ParseQuoteTokens(AST::QuoteExpr& qe);
    void ParseQuoteDollarInterpolationWithParen(AST::QuoteExpr& qe);
    void ParseQuoteDollarInterpolation(AST::QuoteExpr& qe);
    void ParseQuoteEscapeToken(std::vector<Token>& tokens);
    void ParsePatternsInCase(const OwnedPtr<AST::MatchCase>& matchCase, const AST::MatchExpr& matchExpr);
    void ParseMatchCases(AST::MatchExpr& matchExpr);
    void ParseMatchNoSelector(AST::MatchExpr& matchExpr);
    OwnedPtr<AST::MatchExpr> ParseMatchExpr();
    /**
     * Parses zero or more expressions or declarations in a match expression. This is what can occur to the
     * right of a '=>' in a match expression. This function parses but ignores any semicolons occurring before
     * each expression or declaration. It stops before either a 'case' token (indicating the next case), or
     * a '}' right curly brace (indicating the end of the match expression); it doesn't consume either of
     * those tokens. If this function finds that there were no expressions or declarations before the
     * terminator, it registers a non-fatal compiler diagnostic.
     */
    OwnedPtr<AST::Block> ParseExprOrDeclsInMatchCase();

    /// \param modifiers already parsed modifiers, to be attached to the returned VarDecl
    OwnedPtr<AST::Decl> ParseVarDecl(
        const ScopeKind& scopeKind, const std::set<AST::Modifier>& modifiers, const Token& keyToken);
    OwnedPtr<AST::TryExpr> ParseTryExpr();
    void ParseHandleBlock(AST::TryExpr& tryExpr);
    void ParseCatchBlock(AST::TryExpr& tryExpr);
    void ParseTryWithResource(const ScopeKind& scopeKind, AST::TryExpr& tryExpr);
    OwnedPtr<AST::Pattern> ParseExceptTypePattern();
    OwnedPtr<AST::Pattern> ParseCommandTypePattern();
    OwnedPtr<AST::ForInExpr> ParseForInExpr();
    OwnedPtr<AST::RefExpr> ParseRefExpr(ExprKind ek = ExprKind::ALL);
    OwnedPtr<AST::Expr> ParseWildcardExpr();
    OwnedPtr<AST::WhileExpr> ParseWhileExpr();
    OwnedPtr<AST::DoWhileExpr> ParseDoWhileExpr();
    OwnedPtr<AST::Node> ParseExprOrDecl(ScopeKind sk);
    OwnedPtr<AST::Block> ParseExpressionOrDeclarations(ScopeKind sk);
    OwnedPtr<AST::Block> ParseBlock(ScopeKind scopeKind = ScopeKind::UNKNOWN_SCOPE);
    OwnedPtr<AST::Expr> ParseUnsafeBlock();
    bool DiagForBlock(const AST::Block& block);
    template <typename T> bool CheckSkipRcurOrPrematureEnd(T& ret);
    OwnedPtr<AST::ClassBody> ParseClassBody(AST::ClassDecl& cd);
    OwnedPtr<AST::InterfaceBody> ParseInterfaceBody(AST::InterfaceDecl& id);
    OwnedPtr<AST::Decl> ParseEnumConstructor(
        const std::set<AST::Modifier>& modifiers, PtrVector<AST::Annotation>&& annos);
    OwnedPtr<AST::TypePattern> ParseTypePattern(const Position& begin);
    OwnedPtr<AST::ConstPattern> ParseConstPattern();

    OwnedPtr<AST::Pattern> ParseTypePatternOrVarOrEnumPattern(
        const std::set<AST::Attribute>& attributes, bool isVar, bool inDecl);
    OwnedPtr<AST::VarPattern> ParseVarPattern(const std::set<AST::Attribute>& attributes,
        const SrcIdentifier& identifier, const Position& begin, bool isVar) const;
    OwnedPtr<AST::VarOrEnumPattern> ParseVarOrEnumPattern(
        const std::string& identifier, const Position& begin, size_t len, bool isRawId) const;
    OwnedPtr<AST::EnumPattern> ParseEnumPattern(const std::set<AST::Attribute>& attributes, bool isVar, bool inDecl);
    OwnedPtr<AST::TuplePattern> ParseTuplePattern(bool isEnumPatternParams = false,
        const std::set<AST::Attribute>& attributes = {}, bool isVar = false, bool inDecl = false);
    OwnedPtr<AST::SpawnExpr> ParseSpawnExpr();
    OwnedPtr<AST::SynchronizedExpr> ParseSynchronizedExpr();
    void ParseFeatureDirective(OwnedPtr<FeaturesDirective>& features);
    bool ParseFeatureId(OwnedPtr<FeaturesDirective>& features);
    void ParseCommonImportSpec(PtrVector<AST::ImportSpec>& imports, PtrVector<AST::Annotation>& annos);
    void CheckImportSpec(PtrVector<AST::ImportSpec>& imports);
    void CheckTypeArgumentsInEnumPattern(Ptr<const AST::EnumPattern> enumPattern);
    void ParseImportSpec(PtrVector<AST::ImportSpec>& imports, const PtrVector<AST::Annotation>& annos);
    void DesugarImportMulti(PtrVector<AST::ImportSpec>& imports, AST::ImportSpec& import) const;
    void ParseImportContent(AST::ImportContent& content);
    /**
     * Return true if this is a complete import-single or import-all (considerless the possible alias),
     * return false if skipped a '{'.
     */
    bool ParseImportSingle(AST::ImportContent& content, bool inMultiImport = false);
    void ParseImportMulti(AST::ImportContent& content);
    void ParseImportAliasPart(AST::ImportContent& content);
    void ParseImportSpecInTop(PtrVector<AST::ImportSpec>& imports, PtrVector<AST::Annotation>& annos);
    std::vector<OwnedPtr<AST::RefExpr>> TryParseIdentifierList(
        AST::Expr& expr, const std::string& attrName, const Position& pos);
    bool SeeingBuiltinAnnotation();
    bool SeeingAtWhen();
    void ParseAttributeAnnotation(AST::Annotation& anno);
    void ParseOverflowAnnotation(AST::Annotation& anno);
    void ParseWhenAnnotation(AST::Annotation& anno);
    void ParseAnnotationArguments(AST::Annotation& anno);
    void ValidateDeprecatedAnnotationArgument(const Ptr<AST::LitConstExpr> lce, const std::string& name,
        const AST::LitConstKind& expectedKind, bool& isArgumentFound);
    void CheckDeprecatedAnnotation(const AST::Annotation& anno);
    void CheckDeclarationInScope(ScopeKind sk, DefKind dk);
    bool SeeingMacroCall();
    /// Returns true when seeing macro call before a decl.
    /// It can be either @Foo[...] class/func ...
    /// Or @!Foo[...] class/func ...
    /// Note that call to \ref ParserImpl::SeeingMacroCall() would always return true if this function returns true at
    /// some point, so no point in writing `SeeingMacroCall() || SeeingMacroCallDecl()`
    bool SeeingMacroCallDecl();
    void ParseOneOrMoreWithSeparator(
        TokenKind separator, std::vector<Position>& positions, const std::function<void()>& parseElement);
    void ParseOneOrMoreWithSeparator(TokenKind separator, const std::function<void(const Position)>& storeSeparator,
        const std::function<void()>& parseElement);
    /// Parse one or more via \ref parse, separated by \ref separator, and allows trailing separator.
    void ParseOneOrMoreSepTrailing(std::function<void(const Position&)>&& storeSeparator,
        std::function<void()>&& parseElement, TokenKind end, TokenKind separator = TokenKind::COMMA);
    /// Parse zero or more via \ref parse, separated by \ref separator, and allows trailing separator.
    /// Note that trailing comma is invalid if there are zero elements.
    void ParseZeroOrMoreSepTrailing(std::function<void(const Position&)>&& storeSeparator,
        std::function<void()>&& parseElement, TokenKind end, TokenKind separator = TokenKind::COMMA);
    void ParseZeroOrMoreWithSeparator(TokenKind separator, const std::function<void(const Position)>& storeSeparator,
        const std::function<void()>& parseElement, TokenKind terminator);
    void CheckOverflowAnno(
        std::vector<OwnedPtr<AST::Annotation>>& annos, ScopeKind scopeKind = ScopeKind::UNKNOWN_SCOPE);
    void CheckAnnotationAnno(PtrVector<AST::Annotation>& annos, std::set<AST::Modifier> modifiers);
    void CheckAnnotationAnno(PtrVector<AST::Annotation>& annos, const OwnedPtr<AST::Decl>& decl);
    std::set<AST::Attribute> CheckDeclModifiers(
        const std::set<AST::Modifier>& modifiers, ScopeKind scopeKind, DefKind defKind);
    AST::Attribute GetModifierAttr(const OwnedPtr<AST::Modifier>& modifier, AST::ASTKind kind) const;
    void SetDeclBeginPos(AST::Decl& decl) const;
    void CheckFuncBody(ScopeKind scopeKind, AST::FuncDecl& decl);
    void CheckVariableParams(AST::FuncDecl& decl);
    void CheckVariableParams(AST::PrimaryCtorDecl& decl);
    void CheckBaseOfTrailingClosureExpr(const OwnedPtr<AST::Expr>& baseExpr);
    template <typename T> void ParseFuncDeclAnnos(std::vector<OwnedPtr<AST::Annotation>>& annos, T& funcDecl);
    // Move this to sema.
    void CheckNoDeprecatedAnno(const PtrVector<AST::Annotation>& annos, const std::string& invalidTarget);
    void CheckDeprecationOfFuncParam(const AST::FuncParam& param);
    std::string GetSingleLineContent(const Position& begin, const Position& end) const;
    void DiagExpectedIdentifier(const Range& range, const std::string& expectedName = "a name",
        const std::string& afterName = "after this", const bool callHelp = true);
    void DiagExpectedIdentifierWithNode(Ptr<AST::Node> node);
    void DiagImportingByPackageNameIsNotSupported(const Position& expectPos);
    void DiagNameLengthOverflow(const Range& r, const std::string& tar, size_t maxLength, size_t realLength);
    void DiagIllegalModifierInScope(const AST::Modifier& mod);
    void DiagConflictedModifier(const AST::Modifier& resMod, const AST::Modifier& tarMod);
    void DiagRedundantModifiers(const AST::Modifier& modifier);
    void DiagRedundantModifiers(const AST::Modifier& lowerModifier, const AST::Modifier& higherModifier);
    void DiagExpectNoModifier(const AST::Modifier& mod);
    void DiagExpectNoModifierBefore(const AST::Modifier& mod, const std::string& str);
    void DiagUnexpectedAnnoOn(
        const AST::Annotation& anno, const Position& onPos, const std::string& annoStr, const std::string& onStr);
    void DiagUnexpectedAnnoOnKind(
        const AST::Annotation& anno, const Position& onPos, const std::string& annoStr, AST::ASTKind kind);
    void DiagUnexpectedWhenON(PtrVector<AST::Annotation>& annos);
    void DiagUnexpectedDeclInScope(ScopeKind sk);
    void DiagExpectedMoreFieldInTuplePattern();
    void DiagExpectedTypeNameAfterAs(const Token& tok);
    void DiagExpectedTypeName();
    void DiagInvalidLeftHandExpr(const AST::Expr& expr, const Token& tok);
    void DiagNoneAssociativeOp(const Token& preT, const Token& tok);
    void DiagInvalidInheritType(const AST::Type& type);
    void DiagExpectedExpression();
    /**
     * Working with Left and Right Paired Symbols, eg. '(' and ')'
     * @param del left symbol
     * @param pos position of left symbol
     */
    void DiagExpectedRightDelimiter(const std::string& del, const Position& pos);
    void DiagInvalidIncreExpr(const AST::Expr& expr);
    void DiagInvalidMacroExpandExpr(const Token& tok, const AST::MacroExpandExpr& expr);
    void DiagUnrecognizedNodeAfterMacro(const Token& tok, const AST::MacroExpandExpr& expr);
    void DiagChainedAsExpr(AST::Expr& expr, const Token& tok);
    void DiagExpecetedOpeOrEnd();
    void DiagExpectedNoNewLine();
    void DiagCannotHaveAssignmentInInit(const AST::Expr& expr);
    void DiagOrPattern();
    void DiagDeclarationInMacroPackage(const OwnedPtr<AST::Decl>& decl);
    void DiagIllegalFunc(const OwnedPtr<AST::FuncDecl>& funcDecl);
    void DiagParseExpectedParenthis(const OwnedPtr<AST::Type>& postType);
    void DiagParseIllegalDeclarationPattern(const OwnedPtr<AST::VarWithPatternDecl>& decl, ScopeKind scopeKind);
    void DiagThisTypeNotAllow();
    void DiagInvalidUnicodeScalar(const Position& startPos, const std::string& str);
    void DiagExpectedLiteral(const Position& subPos);
    void DiagTypePatternInLetCondExpr(const AST::Pattern& pat, ExprKind ek);
    void DiagExpectedLeftParenAfter(const Position& pos, const std::string& str);
    void DiagMatchCaseExpectedExprOrDecl();
    void DiagMatchCaseBodyCannotBeEmpty(const Position& pos);
    void DiagExpectedCatchOrHandleOrFinallyAfterTry(const AST::TryExpr& te);
    void DiagExpectedSelectorOrMatchExprBody(const Position& pos);
    void DiagRedefinedResourceName(
        const std::pair<std::string, Position>& cur, const std::pair<std::string, Position>& pre);
    void DiagExpectedNoArgumentsInSpawn(const std::vector<OwnedPtr<AST::FuncParam>>& params, const Position& pos);
    void DiagExpectCharacter(const std::string& expectStr, const std::string& noteStr = "");
    void DiagExpectCharacter(const Position& pos, const std::string& expectStr, const std::string& noteStr = "");
    void DiagExpectSemiOrNewline();
    void DiagExpectPublicBeforeMacroCall(const OwnedPtr<AST::MacroDecl>& md);
    void DiagExpectMacroParamType(const AST::Type& type);
    void DiagMacroUnexpectNamedParam(const OwnedPtr<AST::FuncParam>& param);
    void DiagInvalidOverloadedOperator();
    void DiagRedundantArrowAfterFunc(const AST::Type& type);
    void DiagExpectedDeclaration(ScopeKind scopeKind);
    void DiagExpectedDeclaration(const Position& pos, const std::string& str);
    /**
        * Suggest keywords for expected declaration.
        * @param keywords keywords to suggest
        * @param minLevDis minimum Levenshtein distance to suggest
        * @param scopeKind scope kind to determine which keywords to suggest
    */
    void DiagAndSuggestKeywordForExpectedDeclaration(
        const std::vector<std::string>& keywords, size_t minLevDis = 1, ScopeKind scopeKind = ScopeKind::TOPLEVEL);
    void DiagUnExpectedModifierOnDeclaration(const AST::Decl& vd);
    void DiagConstVariableExpectedStatic(const Token& key);
    void DiagConstVariableExpectedInitializer(AST::Decl& vd);
    void DiagExpectedOneOfTypeOrInitializer(const AST::Decl& vd, const std::string& str);
    void DiagExpectedTypeOrInitializerInPattern(const AST::Decl& vd);
    void DiagExpectedInitializerForToplevelVar(const AST::Decl& vd);
    void DiagExpectedIdentifierOrPattern(bool isVar, const Position& pos, bool isConst = false);
    void DiagExpectedGetOrSetInProp(const Position& pos);
    void DiagDuplicatedGetOrSet(const AST::Node& node, const AST::PropDecl& pd);
    void DiagUnknownPrimaryConstructor(const std::string& str);
    void DiagExpectedName(const std::string& str, const std::string& afterName);
    void DiagGetOrSetCannotBeGeneric(const std::string& str, const AST::Generic& ge);
    void DiagUnexpectedWhere(const Token& token);
    void DiagDuplicatedIntrinsicFunc(const AST::Decl& decl, const AST::Decl& prev);
    void DiagMissingBody(const std::string& str, const std::string& name, const Position& pos);
    void DiagDeclCannotInheritTheirSelf(const AST::InheritableDecl& decl, const AST::RefType& rt);
    void DiagUnsafeWillBeIgnored(const AST::Modifier& mod);
    void DiagDuplicatedModifier(const AST::Modifier& mod, const AST::Modifier& pre);
    void DiagNamedParameterAfterUnnamed(const AST::FuncParam& param, const AST::FuncParam& pre);
    void DiagMemberParameterAfterRegular(const AST::FuncParam& param, const AST::FuncParam& pre);
    void DiagUnexpectedColonInRange(const AST::RangeExpr& re);
    void DiagDuplicatedAttiValue(const std::string& attr, const Position& pos, const Position& pre);
    void DiagDuplicatedItem(const std::string& name, const std::string& sourceName, const Position& errPos,
        const Position& prePos, const std::string& attachment = "");
    void DiagUnrecognizedAttrInAnno(const AST::Annotation& anno, const std::string& attrName, const Position& pos,
        const std::set<std::string>& acs);
    void DiagDuplicatedAnno(const AST::Annotation& anno, const AST::Annotation& pre);
    void DiagExpectedLsquareAfter(const AST::Node& node, const std::string& aName, const std::string& note);
    // @Deprecated
    void DiagDeprecatedArgumentNotLitConst(const AST::Node& node);
    void DiagDeprecatedArgumentDuplicated(const AST::Node& node, const std::string& parameterName);
    void DiagDeprecatedWrongArgumentType(
        const AST::Node& node, const std::string& paramName, const std::string& expectedType);
    void DiagDeprecatedUnknownArgument(const AST::Node& arg, const std::string& name);
    void DiagDeprecatedEmptyStringArgument(const AST::Node& node, const std::string& paramName);
    void DiagDeprecatedInvalidTarget(const AST::Node& node, const std::string& invalidTarget);

    void DiagUnrecognizedExprInWhen(const AST::Expr& expr, const AST::Annotation& when);
    void DiagUnexpectedTypeIn(
        const AST::Type& type, const Position& inPos, const std::string& inStr, const std::string& noteStr);
    void DiagVArrayTypeArgMismatch(const Range& range, const std::string& note);
    bool CheckIfSeeingDecl(const ScopeKind& scopeKind)
    {
        if (SeeingIfAvailable()) {
            return false;
        }
        return SeeingDecl() || SeeingMacroCallDecl() || SeeingIllegalDeclInBlock() ||
            SeeingPrimaryCtorDecl(scopeKind) || SeeingEnumConstructor(scopeKind);
    }

    void SkipSemi()
    {
        SkipBlank(TokenKind::SEMI);
    }
    void RevertPrimaryDecl()
    {
        curPrimaryDeclIdent = lastPrimaryDeclIdent;
        curPrimaryDeclIdentIsRaw = lastPrimaryDeclIdentIsRaw;
    };
    void ParseConstraints(const AST::Decl& decl);

    void ParseInheritedTypes(AST::InheritableDecl& decl);

    // ASTLoader.
    OwnedPtr<AST::FuncDecl> ParseFuncDecl(
        ScopeKind scopeKind, const std::set<AST::Modifier>& modifiers, std::vector<OwnedPtr<AST::Annotation>> annos);
    OwnedPtr<AST::EnumDecl> ParseEnumDecl(ScopeKind scopeKind, const std::set<AST::Modifier>& modifiers,
        std::vector<OwnedPtr<AST::Annotation>> annos = {});
    OwnedPtr<AST::StructDecl> ParseStructDecl(ScopeKind scopeKind, const std::set<AST::Modifier>& modifiers,
        std::vector<OwnedPtr<AST::Annotation>> annos = {});
    OwnedPtr<AST::ExtendDecl> ParseExtendDecl(ScopeKind scopeKind, const std::set<AST::Modifier>& modifiers,
        std::vector<OwnedPtr<AST::Annotation>> annos = {});
    OwnedPtr<AST::TypeAliasDecl> ParseTypeAlias(ScopeKind scopeKind, const std::set<AST::Modifier>& modifiers,
        std::vector<OwnedPtr<AST::Annotation>> annos = {});
    OwnedPtr<AST::ClassDecl> ParseClassDecl(ScopeKind scopeKind, const std::set<AST::Modifier>& modifiers,
        std::vector<OwnedPtr<AST::Annotation>> annos = {});
    OwnedPtr<AST::InterfaceDecl> ParseInterfaceDecl(ScopeKind scopeKind, const std::set<AST::Modifier>& modifiers,
        std::vector<OwnedPtr<AST::Annotation>> annos = {});
    OwnedPtr<AST::PrimaryCtorDecl> ParsePrimaryConstructor(ScopeKind scopeKind,
        const std::set<AST::Modifier>& modifiers, std::vector<OwnedPtr<AST::Annotation>> annos = {});
    OwnedPtr<AST::FuncDecl> ParseFinalizer(
        ScopeKind scopeKind, const std::set<AST::Modifier> modifiers, PtrVector<AST::Annotation> annos = {});
    OwnedPtr<AST::FuncDecl> ParseConstructor(ScopeKind scopeKind, const std::set<AST::Modifier>& modifiers,
        std::vector<OwnedPtr<AST::Annotation>> annos = {});
    OwnedPtr<AST::FuncArg> ParseFuncArg();
    OwnedPtr<AST::FuncArg> ParseAnnotationArgument();
    std::set<AST::Attribute> GetModifierAttrs(const ScopeKind& scopeKind,
        std::unordered_map<TokenKind, std::vector<TokenKind>>& scopeRules,
        const std::unordered_map<TokenKind, std::vector<TokenKind>>& scopeWarningRules,
        const std::vector<Ptr<const AST::Modifier>>& modifiersVec);
    void ReportModifierWarning(ScopeKind scopeKind,
        const std::unordered_map<TokenKind, std::vector<TokenKind>>& scopeWarningRules,
        const std::vector<Ptr<const AST::Modifier>>& modifiers);
    void ParseTypeAndExpr(Ptr<AST::VarDeclAbstract> const ret);
    void DiagMatchCase(DiagnosticBuilder& builder);
    OwnedPtr<AST::Expr> GetInvalidExprInAtom(Position pos);
    OwnedPtr<AST::Type> ParseTypeParameterInTupleType(std::unordered_map<std::string, Position>& typeNameMap);
    OwnedPtr<AST::LambdaExpr> ParseBaseLambdaExpr();
    std::unordered_map<AST::ASTKind, std::function<void(ParserImpl*, Ptr<AST::Node>)>> diagExpectedIdentifierMap{
        {AST::ASTKind::ENUM_DECL, &ParserImpl::DiagExpectedIdentifierEnumDecl},
        {AST::ASTKind::GENERIC, &ParserImpl::DiagExpectedIdentifierGeneric},
        {AST::ASTKind::GENERIC_CONSTRAINT, &ParserImpl::DiagExpectedIdentifierGenericConstraint},
        {AST::ASTKind::IMPORT_CONTENT, &ParserImpl::DiagExpectedIdentifierImportContent},
        {AST::ASTKind::IMPORT_SPEC, &ParserImpl::DiagExpectedIdentifierImportSpec},
        {AST::ASTKind::FEATURES_DIRECTIVE, &ParserImpl::DiagExpectedIdentifierFeatureDirective},
        {AST::ASTKind::PACKAGE_SPEC, &ParserImpl::DiagExpectedIdentifierPackageSpec},
        {AST::ASTKind::PROP_DECL, &ParserImpl::DiagExpectedIdentifierPropDecl},
        {AST::ASTKind::REF_TYPE, &ParserImpl::DiagExpectedIdentifierRefType},
        {AST::ASTKind::FUNC_PARAM, &ParserImpl::DiagExpectedIdentifierFuncParam},
        {AST::ASTKind::QUALIFIED_TYPE, &ParserImpl::DiagExpectedIdentifierQualifiedType},
        {AST::ASTKind::MEMBER_ACCESS, &ParserImpl::DiagExpectedIdentifierMemberAccess},
        {AST::ASTKind::MACRO_DECL, &ParserImpl::DiagExpectedIdentifierMacroDecl},
        {AST::ASTKind::FUNC_BODY, &ParserImpl::DiagExpectedIdentifierFuncBody},
        {AST::ASTKind::FUNC_DECL, &ParserImpl::DiagExpectedIdentifierFuncDecl},
        {AST::ASTKind::CLASS_DECL, &ParserImpl::DiagExpectedIdentifierClassDecl},
        {AST::ASTKind::INTERFACE_DECL, &ParserImpl::DiagExpectedIdentifierInterfaceDecl},
        {AST::ASTKind::STRUCT_DECL, &ParserImpl::DiagExpectedIdentifierStructDecl},
        {AST::ASTKind::TYPE_ALIAS_DECL, &ParserImpl::DiagExpectedIdentifierTypeAliasDecl},
        {AST::ASTKind::MACRO_EXPAND_DECL, &ParserImpl::DiagExpectedIdentifierMacroExpandDecl},
        {AST::ASTKind::MACRO_EXPAND_EXPR, &ParserImpl::DiagExpectedIdentifierMacroExpandExpr},
    };
    void DiagExpectedIdentifierEnumDecl(Ptr<AST::Node> node);
    void DiagExpectedIdentifierGeneric(Ptr<AST::Node> node);
    void DiagExpectedIdentifierGenericConstraint(Ptr<AST::Node> node);
    void DiagExpectedIdentifierImportContent(Ptr<AST::Node> node);
    void DiagExpectedIdentifierImportSpec(Ptr<AST::Node> node);
    void DiagExpectedIdentifierFeatureDirective(Ptr<Node> node);
    void DiagExpectedIdentifierPackageSpec(Ptr<AST::Node> node);
    void DiagExpectedIdentifierPropDecl(Ptr<AST::Node> node);
    void DiagExpectedIdentifierRefType(Ptr<AST::Node>);
    void DiagExpectedIdentifierFuncParam(Ptr<AST::Node>);
    void DiagExpectedIdentifierQualifiedType(Ptr<AST::Node> node);
    void DiagExpectedIdentifierMemberAccess(Ptr<AST::Node> node);
    void DiagExpectedIdentifierMacroDecl(Ptr<AST::Node> node);
    void DiagExpectedIdentifierFuncBody(Ptr<AST::Node> node);
    void DiagExpectedIdentifierFuncDecl(Ptr<AST::Node> node);
    void DiagExpectedIdentifierClassDecl(Ptr<AST::Node> node);
    void DiagExpectedIdentifierInterfaceDecl(Ptr<AST::Node> node);
    void DiagExpectedIdentifierStructDecl(Ptr<AST::Node> node);
    void DiagExpectedIdentifierTypeAliasDecl(Ptr<AST::Node> node);
    void DiagExpectedIdentifierMacroExpandDecl(Ptr<AST::Node> node);
    void DiagExpectedIdentifierMacroExpandExpr(Ptr<AST::Node> node);
    void CheckLeftExpression(const Token& preT, const OwnedPtr<AST::Expr>& base, const Token& tok);
    void CheckWildcardInExpr(const OwnedPtr<AST::Expr> &root);
    void CheckVarDeclModifiers(
        std::set<AST::Modifier> modifiers, Ptr<AST::VarDecl> varDecl, ScopeKind scopeKind, const Token& keyToken);
    void DiagAnnotationExpectsOneArgument(const AST::Annotation& node, const std::string& annotationName);
    void DiagAnnotationExpectsOneArgument(const AST::Annotation& node, const std::string& annotationName,
        const std::string& argInfo);
    void DiagAnnotationMoreThanOneArgs(const AST::Annotation& node, const std::string& annotationName);
    void DiagAnnotationMoreThanOneArgs(const AST::Annotation& node, const std::string& annotationName,
        const std::string& argInfo);
    void DiagAnnotationShouldNotHaveArgs(const Annotation& node, const std::string& annotationName);

    // Cangjie Native & Java/ObjC FFI
    void CheckObjCMirrorAnnotation(const AST::Annotation& anno) const;
    // Check whether member decl can be abstract
    bool CanBeAbstract(const AST::Decl& decl, ScopeKind scopeKind) const;
    void CheckConstructorBody(AST::FuncDecl& ctor, ScopeKind scopeKind, bool inMacro = false);
    void SetMemberParentInheritableDecl(AST::InheritableDecl& ret, const OwnedPtr<AST::Decl>& decl) const;
    void FFICheckClassLikeFuncBody(
        FuncDecl& decl,
        DiagKindRefactor functionMustHaveReturnType,
        DiagKindRefactor functionCanNotHaveBody);
    void CheckClassLikePropAbstractness(AST::PropDecl& prop);
    void CheckClassLikeFuncBodyAbstractness(AST::FuncDecl& decl);
    void CheckJavaInteropMember(Decl& decl);
    void CheckObjCInteropMember(Decl& member);
    void CheckPropDeclJavaMirror(AST::PropDecl& decl);
    void CheckPrimaryCtorDeclJavaMirror(AST::PrimaryCtorDecl& ctor);
    void CheckMemberFuncJavaMirror(AST::FuncDecl& decl);
    void CheckMemberFuncObjCMirror(AST::FuncDecl& func);
    void CheckCJMappingAttr(Decl& decl) const;
    void CheckInitCtorDeclBody(AST::FuncDecl& ctor);
    void CheckInitCtorDeclJavaMirror(AST::FuncDecl& ctor);
    void CheckInitCtorDeclObjCMirror(AST::FuncDecl& ctor);
    void CheckVarDeclObjCMirror(AST::VarDecl& field) const;
    void CheckPropDeclObjCMirror(AST::PropDecl& prop);
    void CheckPrimaryCtorDeclObjCMirror(AST::PrimaryCtorDecl& ctor);

    friend class Parser;

    template <typename... Args>
    DiagnosticBuilder ParseDiagnoseRefactor(DiagKindRefactor kind, const Position pos, Args&&... args)
    {
        auto n = MakeOwned<AST::Node>();
        n->begin = pos;
        n->end = pos + 1;
        if (curMacroCall) {
            n->EnableAttr(AST::Attribute::MACRO_EXPANDED_NODE);
            n->curMacroCall = curMacroCall;
        }
        return diag.DiagnoseRefactor(kind, *n, pos, std::forward<Args>(args)...);
    }

    template <typename... Args>
    DiagnosticBuilder ParseDiagnoseRefactor(DiagKindRefactor kind, const Range range, Args&&... args)
    {
        auto n = MakeOwned<AST::Node>();
        n->begin = range.begin;
        n->end = range.end;
        if (curMacroCall) {
            n->EnableAttr(AST::Attribute::MACRO_EXPANDED_NODE);
            n->curMacroCall = curMacroCall;
        }
        return diag.DiagnoseRefactor(kind, *n, range, std::forward<Args>(args)...);
    }

    template <typename... Args>
    DiagnosticBuilder ParseDiagnoseRefactor(DiagKindRefactor kind, const Token& token, Args&&... args)
    {
        auto n = MakeOwned<AST::Node>();
        n->begin = token.Begin();
        n->end = token.End();
        if (curMacroCall) {
            n->EnableAttr(AST::Attribute::MACRO_EXPANDED_NODE);
            n->curMacroCall = curMacroCall;
        }
        return diag.DiagnoseRefactor(kind, *n, token, std::forward<Args>(args)...);
    }

    template <typename... Args>
    DiagnosticBuilder ParseDiagnoseRefactor(DiagKindRefactor kind, const AST::Node& node, Args&&... args)
    {
        auto n = MakeOwned<AST::Node>();
        n->begin = node.begin;
        n->end = node.end;
        if (curMacroCall) {
            n->EnableAttr(AST::Attribute::MACRO_EXPANDED_NODE);
            n->curMacroCall = curMacroCall;
        }
        return diag.DiagnoseRefactor(kind, *n, std::forward<Args>(args)...);
    }
};

extern template OwnedPtr<AST::MacroExpandDecl> ParserImpl::ParseMacroCall<AST::MacroExpandDecl>(
    ScopeKind scopeKind, const std::set<AST::Modifier>& modifiers, std::vector<OwnedPtr<AST::Annotation>> annos);
extern template OwnedPtr<AST::MacroExpandExpr> ParserImpl::ParseMacroCall<AST::MacroExpandExpr>(
    ScopeKind scopeKind, const std::set<AST::Modifier>& modifiers, std::vector<OwnedPtr<AST::Annotation>> annos);
extern template OwnedPtr<AST::MacroExpandParam> ParserImpl::ParseMacroCall<AST::MacroExpandParam>(
    ScopeKind scopeKind, const std::set<AST::Modifier>& modifiers, std::vector<OwnedPtr<AST::Annotation>> annos);

class ChainScope {
public:
    ChainScope(ParserImpl& parser, Ptr<AST::Node> node) : ref(&parser.chainedAST)
    {
        ref->push_back(node);
    }

    ~ChainScope()
    {
        ref->pop_back();
    }

private:
    std::vector<Ptr<AST::Node>>* ref;
};

class ParserScope {
public:
    explicit ParserScope(ParserImpl& parser)
        : ref(&parser),
          oldLookAhead(parser.lookahead),
          oldSkipToken(parser.lastToken),
          oldLastToken(parser.lastToken),
          oldLastNoneNLToken(parser.lastNoneNLToken),
          oldNewlineSkipped(parser.newlineSkipped)
    {
        parser.lexer->SetResetPoint();
    }
    void ResetParserScope()
    {
        ref->lookahead = oldLookAhead;
        ref->lastToken = oldSkipToken;
        ref->lexer->Reset();
        ref->lastToken = oldLastToken;
        ref->lastNoneNLToken = oldLastNoneNLToken;
        ref->newlineSkipped = oldNewlineSkipped;
    }
    ~ParserScope() = default;

private:
    ParserImpl* ref;
    Token oldLookAhead;
    Token oldSkipToken;
    Token oldLastToken;
    Token oldLastNoneNLToken;
    bool oldNewlineSkipped;
};

/**
 * All primitive type kind map.
 */
const std::unordered_map<TokenKind, AST::TypeKind> TOKENKIND_TO_PRIMITIVE_TYPEKIND_MAP{
    {TokenKind::INT8, AST::TypeKind::TYPE_INT8},
    {TokenKind::INT16, AST::TypeKind::TYPE_INT16},
    {TokenKind::INT32, AST::TypeKind::TYPE_INT32},
    {TokenKind::INT64, AST::TypeKind::TYPE_INT64},
    {TokenKind::INTNATIVE, AST::TypeKind::TYPE_INT_NATIVE},
    {TokenKind::UINT8, AST::TypeKind::TYPE_UINT8},
    {TokenKind::UINT16, AST::TypeKind::TYPE_UINT16},
    {TokenKind::UINT32, AST::TypeKind::TYPE_UINT32},
    {TokenKind::UINT64, AST::TypeKind::TYPE_UINT64},
    {TokenKind::UINTNATIVE, AST::TypeKind::TYPE_UINT_NATIVE},
    {TokenKind::FLOAT16, AST::TypeKind::TYPE_FLOAT16},
    {TokenKind::FLOAT32, AST::TypeKind::TYPE_FLOAT32},
    {TokenKind::FLOAT64, AST::TypeKind::TYPE_FLOAT64},
    {TokenKind::RUNE, AST::TypeKind::TYPE_RUNE},
    {TokenKind::BOOLEAN, AST::TypeKind::TYPE_BOOLEAN},
    {TokenKind::NOTHING, AST::TypeKind::TYPE_NOTHING},
    {TokenKind::UNIT, AST::TypeKind::TYPE_UNIT},
};

// Levenshtein distance calculation for suggesting similar names in diagnostics.
unsigned LevenshteinDistance(const std::string& source, const std::string& target);
} // namespace Cangjie
#endif
