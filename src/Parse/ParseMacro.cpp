// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 * This file implements macro parse apis.
 */

#include "ParserImpl.h"

#include <vector>

#include "cangjie/AST/Match.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Basic/StringConvertor.h"

using namespace Cangjie;
using namespace Cangjie::AST;

namespace {
// Macro decl's param limit num.
const static int8_t G_LIMITED_PARAM_NUM = 2;
// Valid decl list after macro expand decl.
const static std::vector<ASTKind> G_VALID_DECL_LIST = {ASTKind::FUNC_DECL, ASTKind::STRUCT_DECL, ASTKind::CLASS_DECL,
    ASTKind::MAIN_DECL, ASTKind::VAR_DECL, ASTKind::ENUM_DECL, ASTKind::INTERFACE_DECL, ASTKind::EXTEND_DECL,
    ASTKind::PROP_DECL, ASTKind::PRIMARY_CTOR_DECL, ASTKind::VAR_WITH_PATTERN_DECL, ASTKind::MACRO_EXPAND_DECL,
    ASTKind::FUNC_PARAM, ASTKind::MACRO_EXPAND_PARAM};
// Info when parse macro invocation, included matched paren, diag info.
const static std::unordered_map<TokenKind, TokenKind> G_PARSE_MACRO_INFO = {
    {TokenKind::LPAREN, TokenKind::RPAREN},
    {TokenKind::LSQUARE, TokenKind::RSQUARE},
};
// Exprs that connected By Comma.
const static std::vector<ASTKind> G_EXPRS_CONNECTED_BY_COMMA_LIST = {
    ASTKind::ARRAY_LIT, ASTKind::TUPLE_LIT, ASTKind::FUNC_ARG};
} // namespace

bool ParserImpl::SeeingParamInMacroCallExpr()
{
    if (SeeingContextualKeyword() || Seeing(TokenKind::IDENTIFIER) || Seeing(TokenKind::WILDCARD)) {
        auto tokens = lexer->LookAheadSkipNL(1);
        if (tokens.begin()->kind == TokenKind::COLON || tokens.begin()->kind == TokenKind::NOT) {
            return true;
        }
    }
    return false;
}

std::vector<OwnedPtr<Node>> ParserImpl::ParseNodes(std::variant<ScopeKind, ExprKind> scope, Node& currentMacroCall,
    const std::set<Modifier>& modifiers, PtrVector<Annotation> annos)
{
    this->currentFile = currentMacroCall.curFile;
    this->curMacroCall = &currentMacroCall;
    auto isConnectedByComma = false;
    auto isParamMacro = false;
    auto pInvocation = currentMacroCall.GetConstInvocation();
    Ptr<FuncParam> namedParameter{nullptr};
    Ptr<FuncParam> memberParam{nullptr};
    CJC_NULLPTR_CHECK(pInvocation);
    if (pInvocation->parent &&
        Utils::In(pInvocation->parent->astKind, G_EXPRS_CONNECTED_BY_COMMA_LIST)) {
        isConnectedByComma = true;
    }
    if (pInvocation && pInvocation->parent && pInvocation->parent->astKind == ASTKind::FUNC_PARAM_LIST) {
        isParamMacro = true;
    }
    auto scopeKind = std::get_if<ScopeKind>(&scope);
    std::vector<OwnedPtr<Node>> nodes;
    while (!Seeing(TokenKind::END) && !DetectPrematureEnd()) {
        auto hasNode = !nodes.empty();
        if (hasNode && isConnectedByComma && !Skip(TokenKind::COMMA)) {
            DiagExpectCharacter("','");
            break;
        }
        if (isParamMacro) {
            isConnectedByComma = Skip(TokenKind::COMMA);
        }
        if (scopeKind || hasNode) {
            // The scopekind indicates that decls or nodes are parsed,
            // The first token can be a semicolon, which can be ignored.
            // But the first Expr starting with a semicolon (;) should not be entered here.
            auto hasNLorSEMI = SkipNLOrSemi();
            if (hasNode && !isConnectedByComma && !hasNLorSEMI) {
                DiagExpectSemiOrNewline();
                break;
            }
        }
        if (Seeing(TokenKind::END)) {
            break;
        }
        OwnedPtr<Node> node;
        if (scopeKind) {
            if (isParamMacro) {
                node = ParseParamInParamList(*scopeKind, namedParameter, memberParam);
            } else if (CheckIfSeeingDecl(*scopeKind)) {
                node = nodes.empty() ? ParseDecl(*scopeKind, modifiers, std::move(annos)) : ParseDecl(*scopeKind);
            } else {
                node = ParseExpr();
            }
        } else if (auto scp = std::get_if<ExprKind>(&scope)) {
            if (auto fa = DynamicCast<FuncArg*>(pInvocation->parent); fa && Seeing(TokenKind::INOUT)) {
                fa->withInout = true;
                Next();
            }
            node = ParseExpr(*scp);
        }
        node->EnableAttr(Attribute::MACRO_EXPANDED_NODE);
        node->curFile = this->currentFile;
        node->curMacroCall = this->curMacroCall;
        AddMacroAttr(*node);
        nodes.emplace_back(std::move(node));
    }
    AttachCommentToNodes(nodes);
    return nodes;
}

bool ParserImpl::CheckMacroExprRules(const Token& pre, const Token& tok, AST::Expr& expr)
{
    if (expr.astKind != ASTKind::MACRO_EXPAND_EXPR) {
        return true;
    }
    Ptr<Node> node = &expr;
    while (node->IsMacroCallNode()) {
        auto pInvocation = node->GetInvocation();
        if (!pInvocation->decl) {
            return true;
        }
        node = pInvocation->decl.get();
    }
    auto me = StaticAs<ASTKind::MACRO_EXPAND_EXPR>(&expr);
    if (!pre.Begin().IsZero() && !expr.TestAttr(Attribute::HAS_BROKEN)) {
        DiagInvalidMacroExpandExpr(pre, *me);
        return false;
    }
    if (!expr.TestAttr(Attribute::HAS_BROKEN)) {
        DiagUnrecognizedNodeAfterMacro(tok, *me);
    }
    return false;
}

static bool IsBuiltinMacro(const std::string& identifier)
{
    const std::vector<std::string> buildInMacros = {"sourcePackage", "sourceFile", "sourceLine"};
    return std::find(buildInMacros.begin(), buildInMacros.end(), identifier) != buildInMacros.end();
}

OwnedPtr<MacroDecl> ParserImpl::ParseMacroDecl(
    ScopeKind scopeKind, const std::set<Modifier>& modifiers, std::vector<OwnedPtr<AST::Annotation>> annos)
{
    OwnedPtr<MacroDecl> ret = MakeOwned<MacroDecl>();
    ChainScope cs(*this, ret.get());
    if (currentFile != nullptr) {
        ret->curFile = currentFile;
        ret->curFile->hasMacro = true;
    }
    ret->begin = lookahead.Begin();
    ret->keywordPos = lookahead.Begin();
    Next();
    CheckDeclarationInScope(scopeKind, DefKind::MACRO);
    auto attrs = CheckDeclModifiers(modifiers, scopeKind, DefKind::MACRO);
    for (auto& it : attrs) {
        ret->EnableAttr(it);
    }
    ret->EnableAttr(Attribute::MACRO_FUNC);
    ret->identifier = ExpectIdentifierWithPos(*ret);
    if (IsBuiltinAnnotation(moduleName, ret->identifier) ||
        IsBuiltinMacro(ret->identifier)) {
        auto builtinKind = IsBuiltinMacro(ret->identifier) ? "macro" : "annotation";
        ParseDiagnoseRefactor(DiagKindRefactor::parse_macro_define_conflicted_with_builtin, MakeRange(ret->identifier),
            ret->identifier, builtinKind);
        chainedAST.back()->EnableAttr(Attribute::HAS_BROKEN);
    }
    ret->modifiers.insert(modifiers.begin(), modifiers.end());
    ret->funcBody = ParseMacroBody(*ret);
    ret->end = ret->funcBody->end;
    ret->annotations = std::move(annos);
    if (!ret->TestAttr(Attribute::PUBLIC) && !chainedAST.back()->TestAttr(Attribute::IS_BROKEN)) {
        DiagExpectPublicBeforeMacroCall(ret);
        chainedAST.back()->EnableAttr(Attribute::HAS_BROKEN);
    }
    // default package does not contain package node or currentFile package does not have a macro
    if (currentFile != nullptr && (currentFile->package == nullptr || !currentFile->package->hasMacro)) {
        if (ret->identifier.Begin().IsZero()) {
            ParseDiagnoseRefactor(
                DiagKindRefactor::parse_expected_macro_decl_define_in_macro_package,
                MakeRange(ret->begin, lastToken.End()));
        } else {
            ParseDiagnoseRefactor(
                DiagKindRefactor::parse_expected_macro_decl_define_in_macro_package, MakeRange(ret->identifier));
        }
    }
    return ret;
}

void ParserImpl::CheckMacroParamType(AST::Type& type, bool isReturnTy)
{
    if (auto ref = AST::As<ASTKind::REF_TYPE>(&type); ref && ref->ref.identifier == "Tokens") {
        return;
    }
    // Macro Parameter Type Must be Tokens.
    if (isReturnTy) {
        DiagExpectMacroParamType(type);
    } else {
        ParseDiagnoseRefactor(DiagKindRefactor::parse_macro_illegal_param_type, type);
    }
    chainedAST.back()->EnableAttr(Attribute::HAS_BROKEN);
}

OwnedPtr<FuncBody> ParserImpl::ParseMacroBody(AST::MacroDecl& macro)
{
    OwnedPtr<FuncBody> ret = MakeOwned<FuncBody>();
    ret->begin = lookahead.Begin();
    ret->paramLists.emplace_back(ParseMacroParameterList());
    if (Skip(TokenKind::COLON)) {
        ret->colonPos = lastToken.Begin();
        ret->retType = ParseType();
        if (ret->retType) {
            CheckMacroParamType(*ret->retType, true);
            ret->end = ret->retType->end;
        }
    }
    // If macro do not have return type, we should add Tokens as its return type.
    if (!ret->retType) {
        auto tokensType = MakeOwned<RefType>();
        auto pos = lastToken.Begin() == INVALID_POSITION ? lookahead.Begin() : lastToken.Begin();
        tokensType->begin = pos;
        tokensType->end = pos;
        tokensType->ref.identifier = "Tokens";
        tokensType->ref.identifier.SetPos(pos, pos);
        ret->retType = std::move(tokensType);
        ret->retType->EnableAttr(Attribute::IN_MACRO);
        ret->retType->EnableAttr(Attribute::COMPILER_ADD);
    }
    if (Seeing(TokenKind::LCURL)) {
        ret->body = ParseBlock(ScopeKind::MACRO_BODY);
    } else {
        ret->body = MakeInvalid<Block>(lastToken.End());
        // Macro decl must have a body, unless in declaration mode.
        DiagMissingBody("macro", !macro.identifier.Valid() ? "" : " '" + macro.identifier + "'", lookahead.End());
    }
    ret->end = ret->body->end;
    ret->EnableAttr(Attribute::MACRO_FUNC);
    return ret;
}

OwnedPtr<AST::FuncParamList> ParserImpl::ParseMacroParameterList()
{
    auto paramList = ParseParameterList();
    if ((paramList->params.empty() || paramList->params.size() > G_LIMITED_PARAM_NUM) &&
        !paramList->TestAttr(Attribute::IS_BROKEN)) {
        auto pos = paramList->params.empty() ? lookahead.Begin() : paramList->params[0].get()->begin;
        auto diagKind = paramList->params.empty() ? DiagKindRefactor::parse_macro_unexpected_empty_parameter
                                                  : DiagKindRefactor::parse_macro_expected_right_parameter_nums;
        ParseDiagnoseRefactor(diagKind, pos);
    } else {
        for (auto& param : paramList->params) {
            if (param->type == nullptr) {
                ParseDiagnoseRefactor(DiagKindRefactor::parse_macro_illegal_param_type, lookahead);
                chainedAST.back()->EnableAttr(Attribute::HAS_BROKEN);
            } else {
                CheckMacroParamType(*param->type);
            }
            // NamedParams are not allowed in macro: (a !: Int32 = 1)
            if (param->isNamedParam) {
                DiagMacroUnexpectNamedParam(param);
                chainedAST.back()->EnableAttr(Attribute::HAS_BROKEN);
            }
        }
    }
    return paramList;
}

bool ParserImpl::ParseMacroCallEscapeTokens(const TokenKind& left, std::vector<Token>& tokens, bool isAttr)
{
    Skip(TokenKind::ILLEGAL);
    if ((Seeing(TokenKind::LSQUARE, TokenKind::RSQUARE) && (left == TokenKind::LSQUARE)) ||
        (Seeing(TokenKind::LPAREN, TokenKind::RPAREN) && (left == TokenKind::LPAREN))) {
        auto tok = Peek();
        (void)tokens.emplace_back(tok.kind, tok.Value(), tok.Begin(), tok.End());
    } else if ((Seeing(TokenKind::AT) && (left == TokenKind::LPAREN)) ||
        (Seeing(TokenKind::AT) && (left == TokenKind::LSQUARE))) {
        // \@ will both be saved into tokens for further analysis(nested macro).
        auto skipIll = lastToken;
        auto tok = Peek();
        (void)tokens.emplace_back(skipIll.kind, skipIll.Value(), skipIll.Begin(), skipIll.End());
        (void)tokens.emplace_back(tok.kind, tok.Value(), tok.Begin(), tok.End());
    } else {
        auto tokenPos = lookahead;
        // Consume tokens to prevent being further parsed into array member access.
        while (SeeingAny({TokenKind::LSQUARE, TokenKind::LPAREN, TokenKind::DOT})) {
            Next();
        }
        auto diagKind = isAttr ? DiagKindRefactor::parse_illegal_macro_expand_attr_args
                               : DiagKindRefactor::parse_illegal_macro_expand_input_args;
        ParseDiagnoseRefactor(diagKind, tokenPos);
        return false;
    }
    return true;
}

bool ParserImpl::ParseMacroCallTokens(
    const TokenKind& left, const TokenKind& right, std::vector<Token>& tokens, bool isAttr)
{
    skipNL = false;
    int lParenOrSquare = 1;
    while (!Seeing(TokenKind::END)) {
        if (Seeing(right)) {
            if (--lParenOrSquare == 0) {
                break;
            }
        }
        auto token = Peek();
        if (token.kind == left) {
            lParenOrSquare++;
        }
        // Support (1) \[ \] in attr @Foo[\[](123)  (2) \( \) in paren @Foo(\() (3) \@ in paren @Foo(\@Bar(123))
        if (Seeing(TokenKind::ILLEGAL) && lookahead == "\\") {
            if (!ParseMacroCallEscapeTokens(left, tokens, isAttr)) {
                return false;
            }
        } else {
            (void)tokens.emplace_back(token);
        }
        Next();
    }
    skipNL = true;
    return true;
}

bool ParserImpl::ParseMacroCallTokens(TokenKind left, std::vector<Token>& tokens)
{
    auto info = G_PARSE_MACRO_INFO.find(left);
    auto lSquarePos = lookahead.Begin();
    if (info == G_PARSE_MACRO_INFO.end()) {
        return false;
    }
    auto right = info->second;
    auto isAttr = right == TokenKind::RSQUARE;

    if (!ParseMacroCallTokens(left, right, tokens, isAttr)) {
        return false;
    }

    if (!Skip(right)) {
        if (right == TokenKind::RSQUARE) {
            DiagExpectedRightDelimiter("[", lSquarePos);
        } else {
            DiagExpectedRightDelimiter("(", lSquarePos);
        }
        tokens.clear(), tokens.shrink_to_fit();
        return false;
    }
    return true;
}

void ParserImpl::ParseNodeToTokens(const Node& node, const Token& begin, std::vector<Token>& tokens)
{
    // Get tokens from the token vector according to the begin position and the end position of the node.
    tokens.push_back(begin);
    auto collectTokens = lexer->GetCollectTokens();
    if (collectTokens.empty() || begin.End() == node.end) {
        // The current node has only one token and the token has been pushed back into the vector tokens.
        return;
    }
    auto startIter = collectTokens.begin();
    auto endIter = collectTokens.end();
    for (auto iter = collectTokens.begin(); iter != collectTokens.end(); ++iter) {
        // Get startIter according to the begin position of the node.
        if (iter->Begin() == begin.Begin()) {
            startIter = iter;
        }
        // Get endIter according to the end position of the node.
        if (iter->Begin() == node.end) {
            endIter = iter;
            break;
        }
        // Get endIter according to the end position of the node.
        if (iter->End() == node.end) {
            endIter = iter + 1;
            break;
        }
    }
    // If the content of startIter is the begin token.
    if (startIter->Begin() == begin.Begin()) {
        (void)tokens.insert(tokens.end(), startIter + 1, endIter);
    } else {
        (void)tokens.insert(tokens.end(), startIter, endIter);
    }
    // Delete last token if it is NL or END.
    if (tokens.back().kind == TokenKind::NL || tokens.back().kind == TokenKind::END) {
        tokens.pop_back();
    }
}

void ParserImpl::ParseMacroCallDeclInput(
    ScopeKind scopeKind, const std::set<Modifier>& modifiers, MacroInvocation& invocation)
{
    // Node Begin Pos must include modifier.
    auto nodeBegin = lookahead;
    auto bStart = lexer->StartCollectTokens();
    auto decl = MakeInvalid<Decl>(lookahead.Begin());
    if (invocation.isFuncParam || SeeingParamInMacroCallExpr() ||
        scopeKind == ScopeKind::PRIMARY_CONSTRUCTOR_FUNC_PARAM) {
        // For macrocall in parameter of primary constructor,
        // use ScopeKind::PRIMARY_CONSTRUCTOR_BODY_FOR_CLASS to avoid reparse error.
        auto scope = (scopeKind == ScopeKind::PRIMARY_CONSTRUCTOR_FUNC_PARAM) ?
            ScopeKind::PRIMARY_CONSTRUCTOR_BODY_FOR_CLASS : scopeKind;
        Ptr<FuncParam> namedParameter{nullptr};
        Ptr<FuncParam> memberParam{nullptr};
        decl = ParseParamInParamList(scope, namedParameter, memberParam);
    } else {
        decl = ParseDecl(scopeKind, modifiers);
    }
    if (decl && decl->TestAttr(Attribute::IS_BROKEN)) {
        chainedAST.back()->EnableAttr(Attribute::HAS_BROKEN);
    }
    lexer->StopCollectTokens(bStart);

    if (Utils::In(decl->astKind, G_VALID_DECL_LIST) && decl->astKind != ASTKind::MACRO_DECL) {
        ParseNodeToTokens(*decl, nodeBegin, invocation.args);
    } else {
        ParseDiagnoseRefactor(DiagKindRefactor::parse_illegal_macro_expand_input_args_without_paren, decl->begin);
    }

    invocation.decl = std::move(decl);
}

void ParserImpl::ParseMacroCallArgsWithoutParen(
    ScopeKind scopeKind, const std::set<Modifier>& modifiers, MacroInvocation& invocation)
{
    ParseMacroCallDeclInput(scopeKind, modifiers, invocation);
    invocation.hasParenthesis = false;
}

void ParserImpl::ParseMacroCallArgsWithParen(MacroInvocation& invocation)
{
    // macro call: @macroCall(any Tokens)
    invocation.leftParenPos = lookahead.Begin();
    if (!ParseMacroCallTokens(TokenKind::LPAREN, invocation.args)) {
        return;
    }
    invocation.rightParenPos = lookahead.Begin();
    invocation.hasParenthesis = true;
}

void ParserImpl::ParseMacroCallArg(
    ScopeKind scopeKind, const std::set<Modifier>& modifiers, MacroInvocation& invocation)
{
    // Parse macro call args.
    if (Skip(TokenKind::LPAREN)) {
        ParseMacroCallArgsWithParen(invocation);
    } else {
        ParseMacroCallArgsWithoutParen(scopeKind, modifiers, invocation);
    }
}

void ParserImpl::ParseMacroCallAttr(MacroInvocation& invocation)
{
    invocation.leftSquarePos = lookahead.Begin();
    if (!ParseMacroCallTokens(TokenKind::LSQUARE, invocation.attrs)) {
        return;
    }
    invocation.rightSquarePos = lookahead.Begin();
}

static void ParseInterplationString2OriginPosMap(
    Token &strToken, MacroInvocation& pMacroInvocation, DiagnosticEngine &diag)
{
    std::vector<Token> tokens{strToken};
    Lexer lexer(tokens, diag, diag.GetSourceManager());
    auto strParts = lexer.GetStrParts(strToken);
    for (auto &strP : strParts) {
        if (strP.strKind != StringPart::EXPR) {
            continue;
        }
        CJC_ASSERT(strP.begin != INVALID_POSITION);
        Lexer lexerStr(strP.begin.fileID, strP.value, diag, diag.GetSourceManager(), strP.begin);
        std::vector<Token> tks = lexerStr.GetTokens();
        // first token must be `$` and second token must be `{`
        CJC_ASSERT(!tks.empty());
        for (size_t i = 2; i < tks.size() - 1; i++) {
            auto& t = tks[i];
            if (t.kind == TokenKind::COMMENT) {
                continue;
            }
            auto be = t.Begin();
            be.isCurFile = strToken.Begin().isCurFile;
            t.SetValuePos(t.Value(), be, t.End());
            pMacroInvocation.originPosMap[static_cast<unsigned int>(t.Begin().Hash32())] = be;
        }
    }
}

void ParserImpl::ParseMacroInvocation(
    ScopeKind scopeKind, const std::set<Modifier>& modifiers, MacroInvocation& invocation)
{
    // Parse macro call attrs.
    if (Skip(TokenKind::LSQUARE)) { // Skip func always lets lastToken = lookAhead
        // macro call: @macroCall[xxx] Decl
        if (IsBuiltinMacro(invocation.identifier)) {
            (void)ParseDiagnoseRefactor(
                DiagKindRefactor::parse_macro_call_illegal_with_builtin, lookahead, invocation.identifier);
        }
        invocation.hasAttr = true;
        ParseMacroCallAttr(invocation);
    }
    invocation.scope = scopeKind;
    ParseMacroCallArg(scopeKind, modifiers, invocation);
    if (!curPrimaryDeclIdent.empty()) {
        invocation.outerDeclIdent = curPrimaryDeclIdent;
    }
    // For lsp.
    for (auto& tk : invocation.attrs) {
        invocation.originPosMap[static_cast<unsigned int>(tk.Begin().Hash32())] = tk.Begin();
    }
    // For lsp.
    for (auto& tk : invocation.args) {
        invocation.originPosMap[static_cast<unsigned int>(tk.Begin().Hash32())] = tk.Begin();
        if (tk.kind == TokenKind::STRING_LITERAL || tk.kind == TokenKind::MULTILINE_STRING) {
            ParseInterplationString2OriginPosMap(tk, invocation, diag);
        }
    }
}

bool ParserImpl::ParseMacroCallIdentifier(MacroInvocation& invocation, Node& node)
{
    auto tokAt = Peek();
    // macro call has the form as followings
    // @M class A{}
    // @p1.p2.p3.M class A{}
    if (!Skip(TokenKind::AT)) {
        Skip(TokenKind::AT_EXCL);
        invocation.isCompileTimeVisible = true;
    }
    // MacroCall identifier.
    invocation.fullName = Peek().Value();
    invocation.atPos = tokAt.Begin();
    if (newlineSkipped) {
        // We forbid this form of macro call:
        // @
        // M class A{}
        ParseDiagnoseRefactor(
            DiagKindRefactor::parse_unexpected_newline_between_at_and_mc, tokAt.Begin(), lookahead.Value());
    }
    auto item = ParseIdentifierFromName(
        invocation.fullName, lookahead.Begin(), lookahead.End(), invocation.fullName.size());
    invocation.identifierPos = item.Begin();
    Next();
    // If we see @p1..M, the error msg is not useful.
    while (Skip(TokenKind::DOT)) {
        invocation.fullName += ".";
        invocation.fullNameDotPos.push_back(lastToken.Begin());
        item = ExpectIdentifierWithPos(node);
        if (item == INVALID_IDENTIFIER) {
            if (node.astKind == ASTKind::MACRO_EXPAND_EXPR) {
                ConsumeUntilAny({TokenKind::NL, TokenKind::SEMI});
            } else {
                ConsumeUntilDecl();
            }
            return false;
        }
        invocation.fullName += item;
    }
    invocation.macroNamePos = lastToken.Begin();
    invocation.identifier = item;
    return true;
}

template <typename T> OwnedPtr<T> ParserImpl::ParseMacroCall(
    ScopeKind scopeKind, const std::set<Modifier>& modifiers, std::vector<OwnedPtr<Annotation>> annos)
{
    CheckOverflowAnno(annos, scopeKind);
    // @! can be used before decl (param included), but not expr
    if constexpr (std::is_same_v<T, MacroExpandDecl> || std::is_same_v<T, MacroExpandParam>) {
        CJC_ASSERT(SeeingMacroCallDecl());
    } else {
        CJC_ASSERT(SeeingMacroCall());
    }

    OwnedPtr<T> macroCall = MakeOwned<T>();
    ChainScope cs(*this, macroCall.get());
    if (currentFile != nullptr) {
        macroCall->curFile = currentFile;
        macroCall->curFile->hasMacro = true;
    }
    macroCall->begin = lookahead.Begin();
    if (!modifiers.empty()) {
        auto firstModifier = *SortModifierByPos(modifiers)[0];
        DiagExpectNoModifierBefore(firstModifier, "macro call");
        macroCall->begin = firstModifier.begin;
    }
    if (!ParseMacroCallIdentifier(macroCall->invocation, *macroCall.get())) {
        macroCall->EnableAttr(Attribute::IS_BROKEN);
        macroCall->end = lastToken.End();
        return macroCall;
    }
    macroCall->identifier = Identifier{macroCall->invocation.identifier, macroCall->invocation.identifierPos,
        macroCall->invocation.identifierPos + macroCall->invocation.identifier.size()};
    // May have multiple annotations include macro or builtin annotation. But Now annos Only have builtin annotations.
    if (!annos.empty()) {
        macroCall->annotations = std::move(annos);
    }
    ParseMacroInvocation(scopeKind, modifiers, macroCall->invocation);
    if (macroCall->invocation.decl) {
        CheckAnnotationAnno(macroCall->annotations, macroCall->invocation.decl);
        macroCall->end = macroCall->invocation.decl->end;
    } else {
        macroCall->end = lastToken.End();
    }
    if (scopeKind == ScopeKind::ENUM_CONSTRUCTOR &&
        (macroCall->invocation.hasParenthesis ||
            (macroCall->invocation.decl && macroCall->invocation.decl->TestAttr(Attribute::ENUM_CONSTRUCTOR)))) {
        macroCall->EnableAttr(Attribute::ENUM_CONSTRUCTOR);
    }
    return macroCall;
}

template OwnedPtr<AST::MacroExpandDecl> ParserImpl::ParseMacroCall<AST::MacroExpandDecl>(
    ScopeKind scopeKind, const std::set<AST::Modifier>& modifiers, std::vector<OwnedPtr<AST::Annotation>> annos);
template OwnedPtr<AST::MacroExpandExpr> ParserImpl::ParseMacroCall<AST::MacroExpandExpr>(
    ScopeKind scopeKind, const std::set<AST::Modifier>& modifiers, std::vector<OwnedPtr<AST::Annotation>> annos);
template OwnedPtr<AST::MacroExpandParam> ParserImpl::ParseMacroCall<AST::MacroExpandParam>(
    ScopeKind scopeKind, const std::set<AST::Modifier>& modifiers, std::vector<OwnedPtr<AST::Annotation>> annos);
