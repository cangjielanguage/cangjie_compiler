// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file defines the implementation of parser of literals, including integer literal, float literal, bool literal,
 * char literal, list literal, set literal and map literal.
 */

#include "ParserImpl.h"

#include "cangjie/AST/Create.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Basic/StringConvertor.h"
#include "cangjie/Utils/FileUtil.h"

using namespace Cangjie;
using namespace Cangjie::AST;

OwnedPtr<Expr> ParserImpl::ParseAtom(ExprKind ek)
{
    if (IsConditionExpr(ek) && Skip(TokenKind::LPAREN)) {
        return ParseLeftParenExprInKind(ek);
    }
    if (const TokenKind peekKind = Peek().kind; exprHandlerMap.count(peekKind) != 0) {
        lastToken = lookahead;
        Next();
        return exprHandlerMap[peekKind](this);
    }
    if (Seeing(TokenKind::IDENTIFIER) || SeeingContextualKeyword()) {
        return ParseRefExpr(ek);
    }
    if (SeeingMacroCall() || SeeingBuiltinAnnotation()) {
        return ParseMacroExprOrLambdaExpr();
    }
    if (SeeingLiteral()) {
        return ParseLitConst();
    }
    // Type convert expression.
    if (SeeingPrimitiveTypeAndLParen()) {
        return ParseTypeConvExpr();
    }
    // Optimize those Seeing().
    // If seeing a primitive type + dot, should be a static function call like Int64.foo().
    if (SeeingPrimitiveTypeAndDot()) {
        auto ret = MakeOwned<PrimitiveTypeExpr>(TOKENKIND_TO_PRIMITIVE_TYPEKIND_MAP.at(lookahead.kind));
        ret->begin = lookahead.Begin();
        Next(); // Consume the TYPE token but keep the DOT token.
        ret->end = lastToken.End();
        return ret;
    }
    if (Seeing(TokenKind::WILDCARD)) {
        return ParseWildcardExpr();
    }
    return GetInvalidExprInAtom(lookahead.Begin());
}

OwnedPtr<Expr> ParserImpl::GetInvalidExprInAtom(Position pos)
{
    auto ret = MakeOwned<InvalidExpr>(pos); // Better to handle the invalid expr upper.
    ret->EnableAttr(Attribute::IS_BROKEN);
    if (chainedAST.empty()) {
        DiagExpectedExpression();
    }
    if (!chainedAST.empty() && !chainedAST.back()->TestAttr(Attribute::HAS_BROKEN)) {
        DiagExpectedExpression();
        chainedAST.back()->EnableAttr(Attribute::HAS_BROKEN);
    }
    ret->value = lookahead.Value();
    if (ret->end.column == 1) {
        // The end position of the diag cannot be 1.
        ret->end = lookahead.End();
    }
    return ret;
}

OwnedPtr<Expr> ParserImpl::ParseUnsafeBlock()
{
    if (!Seeing(TokenKind::LCURL)) {
        return GetInvalidExprInAtom(lookahead.Begin());
    }
    auto unsafePos = lastToken.Begin();
    auto expr = ParseBlock(ScopeKind::FUNC_BODY);
    // unsafe block add UNSAFE Attribute to Special treatment
    expr->EnableAttr(Attribute::UNSAFE);
    SetUnsafe(expr.get());

    expr->unsafePos = unsafePos;
    expr->begin = expr->unsafePos;
    return expr;
}

OwnedPtr<Expr> ParserImpl::ParseLitConst()
{
    if (Seeing({TokenKind::LPAREN, TokenKind::RPAREN})) {
        // We enter this branch only from ParsePattern
        Skip(TokenKind::LPAREN);
        return ParseLeftParenExpr();
    }
    OwnedPtr<LitConstExpr> ret = MakeOwned<LitConstExpr>();
    ret->begin = lookahead.Begin();

    if (Skip(TokenKind::STRING_LITERAL) || Skip(TokenKind::JSTRING_LITERAL) || Skip(TokenKind::MULTILINE_STRING)) {
        return ProcessStringInterpolation(lastToken);
    } else if (Skip(TokenKind::SUB)) {
        return ParseNegativeLiteral();
    }

    ret->stringValue = lookahead.Value();
    ret->isSingleQuote = lookahead.isSingleQuote;

    if (Skip(TokenKind::BOOL_LITERAL)) {
        ret->kind = LitConstKind::BOOL;
    } else if (Skip(TokenKind::INTEGER_LITERAL)) {
        ret->kind = LitConstKind::INTEGER;
    } else if (Skip(TokenKind::RUNE_BYTE_LITERAL)) {
        ret->kind = LitConstKind::RUNE_BYTE;
    } else if (Skip(TokenKind::MULTILINE_RAW_STRING)) {
        ret->kind = LitConstKind::STRING;
        ret->stringKind = StringKind::MULTILINE_RAW;
        ret->stringValue = StringConvertor::Normalize(lookahead.Value(), true);
        ret->codepoint = StringConvertor::UTF8ToCodepoint(ret->stringValue);
        ret->delimiterNum = lookahead.delimiterNum;
    } else if (Skip(TokenKind::RUNE_LITERAL)) {
        ret->kind = LitConstKind::RUNE;
        auto notNormalize = diag.ignoreScopeCheck && lastToken.Value().find("\\u") == 0;
        ret->stringValue = notNormalize ? lastToken.Value() : StringConvertor::Normalize(lastToken.Value());
        ret->codepoint = StringConvertor::UTF8ToCodepoint(ret->stringValue);
        if (!lookahead.Value().empty() && lookahead.Value().find("\\u{") != std::string::npos &&
            ret->codepoint.empty()) {
            DiagInvalidUnicodeScalar(lastToken.Begin(), lastToken.Value());
        }
    } else if (Skip(TokenKind::FLOAT_LITERAL)) {
        ret->kind = LitConstKind::FLOAT;
    } else if (Skip(TokenKind::UNIT_LITERAL)) {
        // Current lexer is unlikely to produce TokenKind.UNIT_LITERAL, and it sees '()' as
        // TWO Tokens: LPAREN, RPAREN. But user provided TokenStream(macro-generated) can have
        // TokenKind.UNIT_LITERAL. Whether to remove UNIT_LITERAL is open to discuss.
        // This is very likely to be removed and make '()' a zero-length tuple in the future.
        ret->kind = LitConstKind::UNIT;
    } else {
        CJC_ABORT();
        return MakeInvalid<InvalidExpr>(lookahead.Begin());
    }
    ret->rawString = lookahead.Value();
    ret->end = lastToken.End();
    return ret;
}

OwnedPtr<Expr> ParserImpl::ParseNegativeLiteral()
{
    OwnedPtr<LitConstExpr> ret = MakeOwned<LitConstExpr>();
    auto subToken = lastToken;
    ret->begin = lookahead.Begin();
    if (Skip(TokenKind::FLOAT_LITERAL)) {
        ret->kind = LitConstKind::FLOAT;
    } else if (Skip(TokenKind::INTEGER_LITERAL)) {
        ret->kind = LitConstKind::INTEGER;
    } else if (Skip(TokenKind::RUNE_BYTE_LITERAL)) {
        ret->kind = LitConstKind::RUNE_BYTE;
    } else {
        DiagExpectedLiteral(subToken.Begin());
        return MakeInvalid<InvalidExpr>(lookahead.Begin());
    }
    ret->stringValue = "-" + lookahead.Value();
    ret->rawString = ret->stringValue;
    ret->end = lastToken.End();
    return ret;
}

OwnedPtr<LitConstExpr> ParserImpl::GetLitConstExprFromStr(
    const std::string& value, const Token& token, const Position& pos) const
{
    OwnedPtr<LitConstExpr> ret = MakeOwned<LitConstExpr>();
    ret->begin = pos;
    ret->rawString = value;
    if (token.kind == TokenKind::MULTILINE_STRING) {
        ret->kind = LitConstKind::STRING;
        ret->stringKind = StringKind::MULTILINE;
    } else if (token.kind == TokenKind::STRING_LITERAL) {
        ret->kind = LitConstKind::STRING;
        ret->stringKind = StringKind::NORMAL;
    } else if (token.kind == TokenKind::JSTRING_LITERAL) {
        ret->kind = LitConstKind::JSTRING;
        ret->stringKind = StringKind::JSTRING;
    }
    ret->stringValue = StringConvertor::Normalize(value);
    ret->codepoint = StringConvertor::UTF8ToCodepoint(ret->stringValue);
    ret->isSingleQuote = token.isSingleQuote;
    ret->end = token.End();
    return ret;
}

OwnedPtr<Expr> ParserImpl::ParseInterpolationExpr(const std::string& value, const Position& pos)
{
    auto ret = MakeOwned<InterpolationExpr>();
    ChainScope cs(*this, ret.get());
    ret->begin = pos;
    ret->end = pos;
    auto vals = Utils::SplitLines(value);
    ret->end.line += static_cast<int>(vals.size() - 1);
    if (vals.size() == 1) {
        ret->end.column += static_cast<int>(value.size());
    } else {
        ret->end.column = static_cast<int>(vals.back().size() + 1);
    }
    ret->rawString = value;
    ret->dollarPos = pos;
    // "{xxx}"
    auto wrapInBracket = value.substr(1, value.length() - 1);
    Position basePos = pos;
    basePos.column += 1;
    auto curLookahead = lookahead;
    auto curLastToken = lastToken;
    auto curlastNoneNLToken = lastNoneNLToken;
    auto hasSkipNewline = newlineSkipped;
    std::unique_ptr<Lexer> curlexer = std::move(lexer);
    lexer = std::make_unique<Lexer>(wrapInBracket, diag, diag.GetSourceManager(), basePos);
    if (enableEH) {
        lexer->SetEHEnabled(true);
    }
    ret->block = ParseBlock(ScopeKind::FUNC_BODY);
    if (!ret->block || ret->block->body.empty() || Peek().kind != TokenKind::END) {
        lexer = std::move(curlexer);
        newlineSkipped = hasSkipNewline;
        lastNoneNLToken = curlastNoneNLToken;
        lastToken = curLastToken;
        lookahead = curLookahead;
        ParseDiagnoseRefactor(DiagKindRefactor::parse_empty_string_interpolation, MakeRange(pos, ret->end));
        return MakeInvalid<InvalidExpr>(pos);
    }
    lexer = std::move(curlexer);
    newlineSkipped = hasSkipNewline;
    lastNoneNLToken = curlastNoneNLToken;
    lastToken = curLastToken;
    lookahead = curLookahead;
    // Need set IS_CLONED_SOURCE_CODE For lsp.
    auto block = ret->block.get();
    Walker setAttrWalker(block, [block](Ptr<Node> node) -> VisitAction {
        if (node != block) {
            node->EnableAttr(Attribute::IS_CLONED_SOURCE_CODE);
        }
        return VisitAction::WALK_CHILDREN;
    });
    setAttrWalker.Walk();
    return ret;
}

OwnedPtr<Expr> ParserImpl::ProcessStringInterpolation(const Token& token)
{
    if (token.kind == TokenKind::JSTRING_LITERAL) {
        return GetLitConstExprFromStr(token.Value(), token, token.Begin());
    }
    std::vector<StringPart> strParts = lexer->GetStrParts(token);
    auto tokenValue = token.Value();
    if (strParts.size() == 1) {
        lexer->ClearStringParts(token);
        return GetLitConstExprFromStr(tokenValue, token, token.Begin());
    }
    auto ret = MakeOwned<StrInterpolationExpr>();
    ret->begin = token.Begin();
    ret->rawString = tokenValue;
    ret->strParts = strParts;
    for (size_t i = 0; i < strParts.size(); i++) {
        auto strPart = strParts[i];
        if (strPart.strKind == StringPart::STR) {
            if (i == 0) {
                // First string pos contains '"' or '"""'.
                auto beginPos = token.Begin();
                strPart.begin.column = beginPos.column;
                strPart.begin.line = beginPos.line;
            }
            ret->strPartExprs.emplace_back(GetLitConstExprFromStr(strPart.value, token, strPart.begin));
        } else {
            ret->strPartExprs.emplace_back(ParseInterpolationExpr(strPart.value, strPart.begin));
        }
    }
    ret->end = token.End();
    lexer->ClearStringParts(token);
    auto litConstExpr = GetLitConstExprFromStr(tokenValue, token, token.Begin());
    litConstExpr->siExpr = std::move(ret);
    return litConstExpr;
}

OwnedPtr<Expr> ParserImpl::ParseArrayLitExpr()
{
    OwnedPtr<ArrayLit> ret = MakeOwned<ArrayLit>();
    ret->begin = lastToken.Begin();
    ret->leftSquarePos = lastToken.Begin();
    ParseZeroOrMoreSepTrailing([&ret](const Position& pos) { ret->commaPosVector.push_back(pos); },
        [this, &ret]() { ret->children.emplace_back(ParseExpr(ExprKind::EXPR_IN_ARRAY)); }, TokenKind::RSQUARE);
    if (!Skip(TokenKind::RSQUARE)) {
        DiagExpectedRightDelimiter("[", ret->leftSquarePos);
        ConsumeUntilAny({TokenKind::NL, TokenKind::SEMI, TokenKind::RSQUARE});
    }
    ret->rightSquarePos = lastToken.Begin();
    ret->end = lastToken.End();
    return ret;
}

static bool IsCondition(const Expr& e)
{
    if (auto paren = DynamicCast<ParenExpr>(&e)) {
        return IsCondition(*paren->expr);
    }
    if (Is<LetPatternDestructor>(&e)) {
        return true;
    }
    return false;
}
bool ParserImpl::CheckCondition(Expr* e)
{
    if (!e) {
        return false;
    }
    if (auto bin = DynamicCast<BinaryExpr>(e)) {
        bool hasSubCondition = CheckCondition(bin->leftExpr.get());
        hasSubCondition = CheckCondition(bin->rightExpr.get()) || hasSubCondition;
        if (hasSubCondition) {
            if (bin->op != TokenKind::AND && bin->op != TokenKind::OR) {
                diag.DiagnoseRefactor(DiagKindRefactor::parse_query_expected_logic_symbol, bin->operatorPos,
                    TOKENS[static_cast<int>(bin->op)]);
                bin->EnableAttr(Attribute::HAS_BROKEN);
            }
        }
        return hasSubCondition;
    }
    return IsCondition(*e);
}

void ParserImpl::ConsumeUntilIfExprEnd()
{
    ConsumeUntil(TokenKind::LCURL);
    ConsumeUntil(TokenKind::RCURL);
    SkipBlank(TokenKind::NL, TokenKind::SEMI);
}

OwnedPtr<Expr> ParserImpl::ParseIfExpr()
{
    OwnedPtr<IfExpr> ret = MakeOwned<IfExpr>();
    ret->begin = lookahead.Begin();
    ret->ifPos = lookahead.Begin();
    if (!Skip(TokenKind::LPAREN)) {
        DiagExpectedLeftParenAfter(ret->begin, "if");
        ConsumeUntilIfExprEnd();
        ret->EnableAttr(Attribute::IS_BROKEN);
        return MakeInvalid<InvalidExpr>(ret->begin);
    }
    ret->leftParenPos = lastToken.Begin();
    ret->condExpr = ParseExpr(ExprKind::IF_COND_EXPR);
    if (!ret->condExpr->TestAttr(Attribute::IS_BROKEN)) {
        (void)CheckCondition(ret->condExpr.get());
    }

    if (!Skip(TokenKind::RPAREN)) {
        if (Is<LetPatternDestructor>(ret->condExpr) && Skip(TokenKind::WHERE)) {
            diag.DiagnoseRefactor(DiagKindRefactor::parse_expected_if_let_andand, lastToken);
        } else {
            DiagExpectedRightDelimiter("(", ret->leftParenPos);
        }
        ret->EnableAttr(Attribute::IS_BROKEN);
        ret->thenBody = MakeInvalid<Block>(lookahead.Begin());
        ret->end = lookahead.Begin();
        ConsumeUntilIfExprEnd();
        if (lastToken.kind == TokenKind::RCURL && Skip(TokenKind::ELSE)) {
            ParseElse(*ret);
        }
        return ret;
    }
    ret->rightParenPos = lastToken.Begin();
    // The rule of block is alike with func body.
    ret->thenBody = ParseBlock(ScopeKind::FUNC_BODY);
    ret->end = ret->thenBody->end;
    SkipBlank(TokenKind::NL);
    if (Skip(TokenKind::ELSE)) {
        ParseElse(*ret);
    }
    return ret;
}

void ParserImpl::ParseElse(IfExpr& ret)
{
    ret.hasElse = true;
    ret.elsePos = lastToken.Begin();
    if (Skip(TokenKind::IF)) {
        ret.isElseIf = true;
        ret.elseBody = ParseIfExpr();
    } else {
        // The rule of block is alike with func body.
        ret.elseBody = ParseBlock(ScopeKind::FUNC_BODY);
    }
    ret.end = lastToken.End();
}

OwnedPtr<Block> ParserImpl::ParseExprOrDeclsInMatchCase()
{
    OwnedPtr<Block> exprOrDecls = MakeOwned<Block>();
    exprOrDecls->begin = lastToken.Begin();
    auto flag = false;
    auto hasSemi = false;
    while (!SeeingAny({TokenKind::CASE, TokenKind::RCURL})) {
        if (flag && !newlineSkipped && !hasSemi && !exprOrDecls->TestAttr(Attribute::HAS_BROKEN)) {
            DiagExpectSemiOrNewline();
        }
        flag = true;
        SkipBlank(TokenKind::SEMI);
        if (DetectPrematureEnd()) {
            break;
        }
        if (SeeingMacroCallDecl() || SeeingDecl() || SeeingExpr()) {
            auto node = ParseExprOrDecl(ScopeKind::FUNC_BODY);
            exprOrDecls->body.emplace_back(std::move(node));
        } else {
            DiagMatchCaseExpectedExprOrDecl();
            exprOrDecls->EnableAttr(Attribute::HAS_BROKEN);
            ConsumeUntilAny({TokenKind::CASE, TokenKind::RCURL, TokenKind::NL}, false);
        }
        hasSemi = Seeing(TokenKind::SEMI);
        SkipBlank(TokenKind::SEMI);
    }
    exprOrDecls->end = lastToken.End();
    return exprOrDecls;
}

static OwnedPtr<LambdaExpr> CreateLambdaFromBlock(
    OwnedPtr<Block> block, const Expr& expr, OwnedPtr<FuncParamList> paramList)
{
    std::vector<OwnedPtr<FuncParamList>> paramLists;
    paramList->begin = expr.begin;
    paramList->end = expr.end;
    paramLists.emplace_back(std::move(paramList));
    auto fb = CreateFuncBody(std::move(paramLists), nullptr, std::move(block));

    // create the lambda expression
    auto le = CreateLambdaExpr(std::move(fb));
    le->begin = expr.begin;
    le->end = expr.end;

    return le;
}

static void DesugarTry(const OwnedPtr<TryExpr>& expr)
{
    if (expr->handlers.empty()) {
        return;
    }

    // To: {=> try{...} catch{...}}
    // create block for function body
    OwnedPtr<Block> lambdaBlock;
    if (expr->catchBlocks.empty()) {
        // Careful: CHIR2 cannot compile a Try block without catch or finally.
        // Even if the catch block has a finally clause, we will remove it and turn
        // it into a lambda
        lambdaBlock = ASTCloner::Clone(expr->tryBlock.get(), SetIsClonedSourceCode);
    } else {
        auto teWithoutHandle = ASTCloner::Clone(expr.get(), SetIsClonedSourceCode);
        teWithoutHandle->handlers.clear();
        teWithoutHandle->finallyBlock = nullptr;
        teWithoutHandle->ty = expr->ty;
        std::vector<OwnedPtr<Node>> lambdaBodyExprs;
        lambdaBodyExprs.emplace_back(std::move(teWithoutHandle));
        lambdaBlock = CreateBlock(std::move(lambdaBodyExprs));
    }
    expr->tryLambda = CreateLambdaFromBlock(std::move(lambdaBlock), *expr->tryBlock, MakeOwned<FuncParamList>());

    // handler lambdas
    for (auto& handler : expr->handlers) {
        auto cmdPat = RawStaticCast<CommandTypePattern*>(handler.commandPattern.get());
        // no multiple command type patterns, it will be forbidden in typechecking
        if (expr->TestAttr(Attribute::HAS_BROKEN) || cmdPat->TestAttr(Attribute::IS_BROKEN) ||
            cmdPat->types[0]->TestAttr(Attribute::IS_BROKEN)) {
            expr->EnableAttr(Attribute::HAS_BROKEN);
            return;
        }
        auto paramList = MakeOwned<FuncParamList>();

        auto originalCommandPatternType = cmdPat->types[0].get();
        auto commandTy = originalCommandPatternType->ty;
        auto commandPattern = ASTCloner::Clone(originalCommandPatternType);
        OwnedPtr<FuncParam> command;
        if (auto varPattern = DynamicCast<VarPattern*>(cmdPat->pattern.get()); varPattern) {
            command = CreateFuncParam(varPattern->varDecl->identifier, std::move(commandPattern), nullptr, commandTy);
        } else {
            command = CreateFuncParam("_", std::move(commandPattern), nullptr, commandTy);
        }
        CopyBasicInfo(cmdPat->pattern, command);
        paramList->params.emplace_back(std::move(command));

        handler.desugaredLambda = CreateLambdaFromBlock(
            ASTCloner::Clone(handler.block.get(), SetIsClonedSourceCode), *handler.block, std::move(paramList));
    }

    if (expr->finallyBlock) {
        expr->finallyLambda = CreateLambdaFromBlock(ASTCloner::Clone(expr->finallyBlock.get(), SetIsClonedSourceCode),
            *expr->finallyBlock, MakeOwned<FuncParamList>());
    }
}

void ParserImpl::ParseHandleBlock(TryExpr& tryExpr)
{
    auto handler = Handler();
    handler.pos = lastToken.Begin();
    Position leftParenPos{0, 0, 0};
    if (Skip(TokenKind::LPAREN)) {
        leftParenPos = lastToken.Begin();
    } else if (!tryExpr.TestAttr(Attribute::HAS_BROKEN)) {
        DiagExpectedLeftParenAfter(handler.pos, "handle");
        tryExpr.EnableAttr(Attribute::HAS_BROKEN);
    }
    // handle clause will have one or two arguments
    handler.commandPattern = ParseCommandTypePattern();
    if (!Skip(TokenKind::RPAREN) && leftParenPos.line != 0 && !tryExpr.TestAttr(Attribute::HAS_BROKEN)) {
        DiagExpectedRightDelimiter("(", leftParenPos);
        tryExpr.EnableAttr(Attribute::HAS_BROKEN);
    }
    handler.block = ParseBlock(ScopeKind::FUNC_BODY);
    tryExpr.handlers.emplace_back(std::move(handler));
}

void ParserImpl::ParseCatchBlock(TryExpr& tryExpr)
{
    tryExpr.catchPosVector.push_back(lastToken.Begin());
    Position leftParenPos{0, 0, 0};
    if (Skip(TokenKind::LPAREN)) {
        leftParenPos = lastToken.Begin();
        tryExpr.catchLParenPosVector.push_back(leftParenPos);
    } else if (!tryExpr.TestAttr(Attribute::HAS_BROKEN)) {
        DiagExpectedLeftParenAfter(tryExpr.catchPosVector.back(), "catch");
        tryExpr.EnableAttr(Attribute::HAS_BROKEN);
    }
    tryExpr.catchPatterns.emplace_back(ParseExceptTypePattern());
    if (!Skip(TokenKind::RPAREN) && leftParenPos.line != 0 && !tryExpr.TestAttr(Attribute::HAS_BROKEN)) {
        DiagExpectedRightDelimiter("(", leftParenPos);
        tryExpr.EnableAttr(Attribute::HAS_BROKEN);
    }
    if (lastToken.kind == TokenKind::RPAREN) {
        tryExpr.catchRParenPosVector.push_back(lastToken.Begin());
    }
    tryExpr.catchBlocks.emplace_back(ParseBlock(ScopeKind::FUNC_BODY));
}

OwnedPtr<TryExpr> ParserImpl::ParseTryExpr()
{
    OwnedPtr<TryExpr> tryExpr = MakeOwned<TryExpr>();
    tryExpr->tryPos = lastToken.Begin();
    tryExpr->begin = lookahead.Begin();
    if (Seeing(TokenKind::LCURL)) {
        tryExpr->tryBlock = ParseBlock(ScopeKind::FUNC_BODY);
    } else if (Seeing(TokenKind::LPAREN)) { // try with resources
        ParseTryWithResource(ScopeKind::FUNC_BODY, *tryExpr);
    } else {
        DiagExpectCharacter("'{' or '('", "try expression must have block or resources");
        tryExpr->EnableAttr(Attribute::HAS_BROKEN);
        tryExpr->EnableAttr(Attribute::IS_BROKEN);
    }
    while (Seeing(TokenKind::HANDLE) || Seeing(TokenKind::CATCH)) {
        if (Skip(TokenKind::HANDLE)) {
            ParseHandleBlock(*tryExpr);
        } else if (Skip(TokenKind::CATCH)) {
            ParseCatchBlock(*tryExpr);
        }
    }
    if (Skip(TokenKind::FINALLY)) {
        tryExpr->finallyPos = lastToken.Begin();
        tryExpr->finallyBlock = ParseBlock(ScopeKind::FUNC_BODY);
    } else {
        if (tryExpr->catchBlocks.empty() && tryExpr->handlers.empty() && tryExpr->resourceSpec.empty() &&
            !tryExpr->TestAttr(Attribute::HAS_BROKEN)) {
            DiagExpectedCatchOrHandleOrFinallyAfterTry(*tryExpr);
            tryExpr->EnableAttr(Attribute::HAS_BROKEN);
        }
    }
    tryExpr->end = lastToken.End();
    if (!tryExpr->TestAttr(Attribute::HAS_BROKEN)) {
        DesugarTry(tryExpr);
    }
    return tryExpr;
}

OwnedPtr<Pattern> ParserImpl::ParseExceptTypePattern()
{
    auto parsePatterns = [this](ExceptTypePattern& exceptTypePattern) {
        do {
            if (lastToken.kind == TokenKind::BITOR) {
                exceptTypePattern.bitOrPosVector.emplace_back(lastToken.Begin());
            }
            exceptTypePattern.types.emplace_back(ParseType());
        } while (Skip(TokenKind::BITOR));
    };
    if (Skip(TokenKind::WILDCARD)) {
        auto wildCardPos = lastToken.Begin();
        auto wildCardPattern = MakeOwned<WildcardPattern>(wildCardPos);
        if (Skip(TokenKind::COLON)) {
            OwnedPtr<ExceptTypePattern> exceptTypePattern = MakeOwned<ExceptTypePattern>();
            exceptTypePattern->colonPos = lastToken.Begin();
            exceptTypePattern->begin = wildCardPos;
            exceptTypePattern->pattern = std::move(wildCardPattern);
            parsePatterns(*exceptTypePattern);
            if (!exceptTypePattern->types.empty()) {
                exceptTypePattern->end = exceptTypePattern->types.back()->end;
            }
            return exceptTypePattern;
        } else {
            return wildCardPattern;
        }
    } else if (Seeing(TokenKind::IDENTIFIER) || SeeingContextualKeyword()) {
        OwnedPtr<ExceptTypePattern> exceptTypePattern = MakeOwned<ExceptTypePattern>();
        auto ident = ParseIdentifierFromToken(lookahead);
        Position begin = lookahead.Begin();
        exceptTypePattern->begin = begin;
        exceptTypePattern->pattern = MakeOwned<VarPattern>(std::move(ident), begin);
        exceptTypePattern->patternPos = lookahead.Begin();
        Next();
        if (!Skip(TokenKind::COLON)) {
            ParseDiagnoseRefactor(
                DiagKindRefactor::parse_expected_colon_in_catch_pattern, lookahead, ConvertToken(lookahead));
        }
        exceptTypePattern->colonPos = lastToken.Begin();
        parsePatterns(*exceptTypePattern);
        if (!exceptTypePattern->types.empty()) {
            exceptTypePattern->end = exceptTypePattern->types.back()->end;
        }
        return exceptTypePattern;
    } else {
        ParseDiagnoseRefactor(
            DiagKindRefactor::parse_expected_wildcard_or_exception_pattern, lookahead, ConvertToken(lookahead));
        return MakeInvalid<InvalidPattern>(lookahead.Begin());
    }
}

OwnedPtr<Pattern> ParserImpl::ParseCommandTypePattern()
{
    auto parsePatterns = [this](CommandTypePattern& commandPattern) {
        do {
            if (lastToken.kind == TokenKind::BITOR) {
                commandPattern.bitOrPosVector.emplace_back(lastToken.Begin());
            }
            commandPattern.types.emplace_back(ParseType());
        } while (Skip(TokenKind::BITOR));
    };
    if (Skip(TokenKind::WILDCARD)) {
        auto wildCardPos = lastToken.Begin();
        auto wildCardPattern = MakeOwned<WildcardPattern>(wildCardPos);
        if (Skip(TokenKind::COLON)) {
            OwnedPtr<CommandTypePattern> commandPattern = MakeOwned<CommandTypePattern>();
            commandPattern->colonPos = lastToken.Begin();
            commandPattern->begin = wildCardPos;
            commandPattern->pattern = std::move(wildCardPattern);
            parsePatterns(*commandPattern);
            if (!commandPattern->types.empty()) {
                commandPattern->end = commandPattern->types.back()->end;
            }
            return commandPattern;
        } else {
            ParseDiagnoseRefactor(
                DiagKindRefactor::parse_expected_colon_in_effect_pattern, lookahead, ConvertToken(lookahead));
            return MakeInvalid<InvalidPattern>(lookahead.Begin());
        }
    } else if (Seeing(TokenKind::IDENTIFIER) || SeeingContextualKeyword()) {
        OwnedPtr<CommandTypePattern> commandPattern = MakeOwned<CommandTypePattern>();
        auto ident = ParseIdentifierFromToken(lookahead);
        Position begin = lookahead.Begin();
        commandPattern->begin = begin;
        commandPattern->pattern = MakeOwned<VarPattern>(std::move(ident), begin);
        commandPattern->patternPos = lookahead.Begin();
        Next();
        if (!Skip(TokenKind::COLON)) {
            ParseDiagnoseRefactor(
                DiagKindRefactor::parse_expected_colon_in_effect_pattern, lookahead, ConvertToken(lookahead));
        }
        commandPattern->colonPos = lastToken.Begin();
        parsePatterns(*commandPattern);
        if (!commandPattern->types.empty()) {
            commandPattern->end = commandPattern->types.back()->end;
        }
        return commandPattern;
    } else {
        ParseDiagnoseRefactor(
            DiagKindRefactor::parse_expected_wildcard_or_effect_pattern, lookahead, ConvertToken(lookahead));
        return MakeInvalid<InvalidPattern>(lookahead.Begin());
    }
}

void ParserImpl::ParsePatternsInCase(const OwnedPtr<MatchCase>& matchCase, const MatchExpr& matchExpr)
{
    auto first = ParsePattern();
    first->ctxExpr = matchExpr.selector.get();
    matchCase->patterns.emplace_back(std::move(first));
    while (Skip(TokenKind::BITOR)) {
        matchCase->bitOrPosVector.push_back(lastToken.Begin());
        auto pattern = ParsePattern();
        pattern->ctxExpr = matchExpr.selector.get();
        matchCase->patterns.emplace_back(std::move(pattern));
    }
}

void ParserImpl::ParseMatchCases(AST::MatchExpr& matchExpr)
{
    auto consumeTarget = [this]() {
        return SeeingAny({TokenKind::CASE, TokenKind::RCURL, TokenKind::DOUBLE_ARROW}) ||
            SeeingCombinator(combinedDoubleArrow);
    };
    while (Skip(TokenKind::CASE)) {
        OwnedPtr<MatchCase> matchCase = MakeOwned<MatchCase>();
        ChainScope cs(*this, matchCase.get());
        matchCase->begin = lookahead.Begin();
        ParsePatternsInCase(matchCase, matchExpr);
        if (Skip(TokenKind::WHERE)) {
            matchCase->wherePos = lookahead.Begin();
            matchCase->patternGuard = ParseExpr();
        } else {
            matchCase->patternGuard = OwnedPtr<Expr>();
        }
        if (!Skip(TokenKind::DOUBLE_ARROW) && !SkipCombinedDoubleArrow()) {
            auto builder = ParseDiagnoseRefactor(
                DiagKindRefactor::parse_expected_double_arrow_in_case, lookahead, ConvertToken(lookahead));
            builder.AddHint(MakeRange(matchCase->begin, "case"));
            ConsumeUntilAny(consumeTarget, false);
            matchCase->exprOrDecls = MakeInvalid<Block>(lookahead.Begin());
            matchCase->end = matchCase->exprOrDecls->end;
            matchCase->EnableAttr(Attribute::HAS_BROKEN);
            if (!Skip(TokenKind::DOUBLE_ARROW) && !SkipCombinedDoubleArrow()) {
                matchExpr.matchCases.emplace_back(std::move(matchCase));
                continue;
            }
        }
        matchCase->arrowPos = lastToken.Begin();
        matchCase->exprOrDecls = ParseExprOrDeclsInMatchCase();
        matchCase->end = matchCase->exprOrDecls->end;
        if (matchCase->exprOrDecls->body.empty() && !matchCase->exprOrDecls->TestAttr(Attribute::HAS_BROKEN)) {
            DiagMatchCaseBodyCannotBeEmpty(matchCase->arrowPos + std::string("=>").size());
            matchCase->EnableAttr(Attribute::HAS_BROKEN);
        }
        matchExpr.matchCases.emplace_back(std::move(matchCase));
        SkipBlank(TokenKind::SEMI);
    }
}

void ParserImpl::ParseMatchNoSelector(AST::MatchExpr& matchExpr)
{
    matchExpr.matchMode = false;
    matchExpr.leftCurlPos = Peek().Begin();
    Next();
    auto consumeTarget = [this]() {
        return SeeingAny({TokenKind::CASE, TokenKind::RCURL, TokenKind::DOUBLE_ARROW}) ||
            SeeingCombinator(combinedDoubleArrow);
    };
    while (Skip(TokenKind::CASE)) {
        OwnedPtr<MatchCaseOther> matchCaseOther = MakeOwned<MatchCaseOther>();
        matchCaseOther->begin = lookahead.Begin();
        if (Skip(TokenKind::WILDCARD)) {
            auto wildcardExpr = MakeOwned<WildcardExpr>(lastToken.Begin());
            matchCaseOther->matchExpr = std::move(wildcardExpr);
        } else {
            matchCaseOther->matchExpr = ParseExpr();
        }
        if (!Skip(TokenKind::DOUBLE_ARROW) && !SkipCombinedDoubleArrow()) {
            auto builder = ParseDiagnoseRefactor(
                DiagKindRefactor::parse_expected_double_arrow_in_case, lookahead, ConvertToken(lookahead));
            builder.AddHint(MakeRange(matchCaseOther->begin, "case"));
            ConsumeUntilAny(consumeTarget, false);
            matchCaseOther->exprOrDecls = MakeInvalid<Block>(lookahead.Begin());
            matchCaseOther->EnableAttr(Attribute::HAS_BROKEN);
            if (!Skip(TokenKind::DOUBLE_ARROW) && !SkipCombinedDoubleArrow()) {
                matchCaseOther->end = lastToken.End();
                matchExpr.matchCaseOthers.emplace_back(std::move(matchCaseOther));
                continue;
            }
        }
        matchCaseOther->arrowPos = lookahead.Begin();
        matchCaseOther->exprOrDecls = ParseExprOrDeclsInMatchCase();
        matchCaseOther->end = lastToken.Begin();
        if (matchCaseOther->exprOrDecls->body.empty() && !matchCaseOther->TestAttr(Attribute::HAS_BROKEN)) {
            DiagMatchCaseBodyCannotBeEmpty(matchCaseOther->arrowPos + std::string("=>").size());
            matchCaseOther->EnableAttr(Attribute::HAS_BROKEN);
        }
        matchExpr.matchCaseOthers.emplace_back(std::move(matchCaseOther));
    }
}

OwnedPtr<MatchExpr> ParserImpl::ParseMatchExpr()
{
    OwnedPtr<MatchExpr> matchExpr = MakeOwned<MatchExpr>();
    ChainScope cs(*this, matchExpr.get());
    matchExpr->begin = lookahead.Begin();
    // If the next two valid tokens are '{' followed by a 'case', then this match expression does not have a
    // selector.
    skipNL = true;
    if (!Seeing({TokenKind::LCURL, TokenKind::CASE})) { // If this match expression has a selector.
        matchExpr->matchMode = true;
        if (!Skip(TokenKind::LPAREN)) {
            DiagExpectedSelectorOrMatchExprBody(matchExpr->begin);
            matchExpr->selector = MakeInvalid<InvalidExpr>(lookahead.Begin());
            matchExpr->EnableAttr(Attribute::HAS_BROKEN);
            ConsumeUntilAny({TokenKind::LCURL, TokenKind::NL}, false);
        } else {
            matchExpr->leftParenPos = lastToken.Begin();
            matchExpr->selector = ParseExpr();
            if (!Skip(TokenKind::RPAREN)) {
                DiagExpectedRightDelimiter("(", matchExpr->leftParenPos);
            }
            matchExpr->rightParenPos = lastToken.Begin();
        }
        if (!Skip(TokenKind::LCURL)) {
            if (!matchExpr->TestAttr(Attribute::HAS_BROKEN)) {
                ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_left_brace, lookahead, ConvertToken(lookahead));
                matchExpr->EnableAttr(Attribute::HAS_BROKEN);
            }
        } else {
            matchExpr->leftCurlPos = lookahead.Begin();
        }
        SkipSemi();
        if (!Seeing(TokenKind::CASE)) {
            ConsumeUntil(TokenKind::RCURL, false);
        }
        ParseMatchCases(*matchExpr);
        if (matchExpr->matchCases.empty()) {
            auto builder =
                ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_case, lookahead, ConvertToken(lookahead));
            builder.AddHint(MakeRange(matchExpr->begin, "match"));
        }
    } else { // If this match expression does not have a selector.
        ParseMatchNoSelector(*matchExpr);
        if (matchExpr->matchCaseOthers.empty()) {
            ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_case, lookahead, ConvertToken(lookahead));
        }
    }
    if (!Skip(TokenKind::RCURL) && !matchExpr->leftCurlPos.IsZero()) {
        DiagExpectedRightDelimiter("{", matchExpr->leftCurlPos);
    }
    matchExpr->rightCurlPos = lookahead.Begin();
    matchExpr->end = lastToken.End();
    return matchExpr;
}

OwnedPtr<QuoteExpr> ParserImpl::ParseQuoteExpr()
{
    OwnedPtr<QuoteExpr> ret = MakeOwned<QuoteExpr>();
    ret->begin = lookahead.Begin();
    ret->quotePos = lookahead.Begin();
    skipNL = false;
    if (Skip(TokenKind::NL) || newlineSkipped) {
        ParseDiagnoseRefactor(DiagKindRefactor::parse_unexpected_line_break, lastToken);
        ret->EnableAttr(Attribute::HAS_BROKEN);
    }
    SkipBlank(TokenKind::NL);
    if (Skip(TokenKind::LPAREN)) {
        ret->leftParenPos = lookahead.Begin();
        ParseQuoteTokens(*ret);
        if (!Skip(TokenKind::RPAREN)) {
            DiagExpectedRightDelimiter("(", ret->leftParenPos);
        }
        ret->rightParenPos = lookahead.Begin();
    } else {
        if (!ret->TestAttr(Attribute::HAS_BROKEN)) {
            ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_left_paren, lookahead, ConvertToken(lookahead));
        }
    }
    skipNL = true;
    ret->end = lastToken.End();
    return ret;
}

OwnedPtr<ReturnExpr> ParserImpl::ParseReturnExpr()
{
    OwnedPtr<ReturnExpr> ret = MakeOwned<ReturnExpr>();
    ret->begin = lookahead.Begin();
    ret->returnPos = lookahead.Begin();
    if ((SeeingDecl() && !SeeingContextualKeyword()) ||
        SeeingAny({TokenKind::SEMI, TokenKind::COMMA, TokenKind::RPAREN, TokenKind::RSQUARE, TokenKind::RCURL,
            TokenKind::CASE, TokenKind::END, TokenKind::DOUBLE_ARROW}) ||
        SeeingCombinator(combinedDoubleArrow)) {
        OwnedPtr<LitConstExpr> unitLit = MakeOwned<LitConstExpr>();
        unitLit->kind = LitConstKind::UNIT;
        unitLit->stringValue = "()";
        ret->expr = std::move(unitLit);
        // Be aware to always add Position information when construct AST.
        ret->expr->begin = lastToken.End();
        ret->expr->end = ret->expr->begin;
        if (Seeing(TokenKind::SEMI)) {
            ret->hasSemi = true;
            ret->semiPos = lookahead.Begin();
        }
        ret->expr->EnableAttr(Attribute::COMPILER_ADD);
    } else {
        if (newlineSkipped) {
            auto builder = ParseDiagnoseRefactor(DiagKindRefactor::parse_nl_warning, firstNLPosition);
            auto iter = std::find_if(
                combinator.rbegin(), combinator.rend(), [this](auto& k) { return SeeingCombinator(k.first); });
            std::string tkValue = (iter != combinator.rend()) ? std::string(iter->second.second) : lookahead.Value();
            builder.AddHint(
                MakeRange(lastNoneNLToken.Begin(), lookahead.Begin() + 1), lastNoneNLToken.Value(), tkValue);
        }
        ret->expr = ParseExpr();
        if (ret->expr->TestAttr(Attribute::IS_BROKEN)) {
            ConsumeUntilAny({TokenKind::NL, TokenKind::SEMI});
        }
    }
    ret->end = ret->expr->end;
    return ret;
}

OwnedPtr<ThrowExpr> ParserImpl::ParseThrowExpr()
{
    OwnedPtr<ThrowExpr> ret = MakeOwned<ThrowExpr>();
    ret->throwPos = lastToken.Begin();
    ret->begin = lookahead.Begin();
    ret->expr = ParseExpr();
    if (ret->expr) {
        ret->end = ret->expr->end;
    }
    return ret;
}

OwnedPtr<PerformExpr> ParserImpl::ParsePerformExpr()
{
    OwnedPtr<PerformExpr> ret = MakeOwned<PerformExpr>();
    ret->performPos = lastToken.Begin();
    ret->begin = lookahead.Begin();
    ret->expr = ParseExpr();
    if (ret->expr) {
        ret->end = ret->expr->end;
    }
    return ret;
}

OwnedPtr<ResumeExpr> ParserImpl::ParseResumeExpr()
{
    OwnedPtr<ResumeExpr> ret = MakeOwned<ResumeExpr>();
    ret->resumePos = lastToken.Begin();
    ret->begin = lookahead.Begin();
    ret->end = lookahead.End();
    if (Skip(TokenKind::WITH)) {
        ret->withPos = lastToken.Begin();
        ret->withExpr = ParseExpr();
        if (ret->withExpr) {
            ret->end = ret->withExpr->end;
        }
    } else if (Skip(TokenKind::THROWING)) {
        ret->throwingPos = lastToken.Begin();
        ret->throwingExpr = ParseExpr();
        if (ret->throwingExpr) {
            ret->end = ret->throwingExpr->end;
        }
    }

    return ret;
}

OwnedPtr<RefExpr> ParserImpl::ParseThisOrSuper() const
{
    const std::string& str = TOKENS[static_cast<int>(lastToken.kind)];
    auto ret = MakeOwned<RefExpr>();
    ret->isThis = lastToken.kind == TokenKind::THIS;
    ret->isSuper = lastToken.kind == TokenKind::SUPER;
    ret->ref.identifier = str;
    ret->ref.identifier.SetPos(lastToken.Begin(), lastToken.End());
    ret->begin = lastToken.Begin();
    ret->end = ret->begin;
    ret->end.column += static_cast<int>(str.length());
    return ret;
}

OwnedPtr<Expr> ParserImpl::ParseBreakJumpExpr()
{
    OwnedPtr<JumpExpr> ret = MakeOwned<JumpExpr>();
    ret->isBreak = true;
    ret->begin = lookahead.Begin();
    ret->end = lastToken.End();
    return ret;
}

OwnedPtr<Expr> ParserImpl::ParseContinueJumpExpr()
{
    OwnedPtr<JumpExpr> ret = MakeOwned<JumpExpr>();
    ret->isBreak = false;
    ret->begin = lookahead.Begin();
    ret->end = lastToken.End();
    return ret;
}

OwnedPtr<TupleLit> ParserImpl::ParseTupleLitForParenExpr(const Position& leftParenPos)
{
    std::vector<OwnedPtr<RefExpr>> refExprs;
    std::vector<Position> commaPosVector;
    do {
        if (Seeing(TokenKind::IDENTIFIER) || SeeingContextualKeyword()) {
            refExprs.emplace_back(ParseRefExpr(ExprKind::EXPR_IN_TUPLE));
        }
        Next();
        commaPosVector.emplace_back(lookahead.Begin());
    } while (SeeingIdentifierAndTargetOp({TokenKind::COMMA}));
    // It's actually a tuple literal.
    OwnedPtr<TupleLit> ret = MakeOwned<TupleLit>();
    ret->begin = leftParenPos;
    ret->leftParenPos = leftParenPos;
    for (auto& it : refExprs) {
        ret->children.emplace_back(std::move(it));
    }
    ret->commaPosVector.assign(commaPosVector.begin(), commaPosVector.end());
    ParseOneOrMoreWithSeparator(TokenKind::COMMA, ret->commaPosVector, [this, &ret]() {
        if (Seeing(TokenKind::RPAREN) && ret->children.size() > 1) {
            return;
        }
        auto expr = ParseExpr(ExprKind::EXPR_IN_TUPLE);
        ret->children.emplace_back(std::move(expr));
    });

    if (!Skip(TokenKind::RPAREN)) {
        DiagExpectedRightDelimiter("(", ret->leftParenPos);
        ConsumeUntilAny({TokenKind::NL, TokenKind::SEMI, TokenKind::RPAREN});
    }
    ret->rightParenPos = lastToken.Begin();
    ret->end = lastToken.End();
    return ret;
}

OwnedPtr<TupleLit> ParserImpl::ParseTupleLitForParenExprComma(const Position& leftParenPos, OwnedPtr<Expr> expr)
{
    OwnedPtr<TupleLit> ret = MakeOwned<TupleLit>();
    ret->children.emplace_back(std::move(expr));
    while (Skip(TokenKind::COMMA)) {
        ret->commaPosVector.push_back(lastToken.Begin());
        if (Seeing(TokenKind::RPAREN) && ret->children.size() > 1) {
            break;
        }
        auto tmpExpr = ParseExpr(ExprKind::EXPR_IN_TUPLE);
        ret->children.emplace_back(std::move(tmpExpr));
    }
    if (!Skip(TokenKind::RPAREN)) {
        DiagExpectedRightDelimiter("(", leftParenPos);
        ConsumeUntilAny({TokenKind::NL, TokenKind::SEMI, TokenKind::RPAREN});
    }
    ret->leftParenPos = leftParenPos;
    ret->rightParenPos = lookahead.Begin();
    ret->begin = leftParenPos;
    ret->end = lastToken.End();
    return ret;
}

OwnedPtr<Expr> ParserImpl::ParseLeftParenExpr()
{
    return ParseLeftParenExprInKind(ExprKind::ALL);
}

OwnedPtr<AST::Expr> ParserImpl::ParseLeftParenExprInKind(ExprKind ek)
{
    // Parse conventional ParenExpr
    Position leftParenPos = lookahead.Begin();
    if (Skip(TokenKind::RPAREN)) {
        // LitConstExpr(unit_literal) is parsed from TWO Tokens.
        OwnedPtr<LitConstExpr> ret = MakeOwned<LitConstExpr>();
        ret->begin = leftParenPos;
        ret->kind = LitConstKind::UNIT;
        ret->stringValue = sourceManager.GetContentBetween(leftParenPos.fileID, leftParenPos, lastToken.Begin() + 1);
        ret->end = lastToken.End();
        return ret;
    }
    if (SeeingIdentifierAndTargetOp({TokenKind::COMMA})) {
        if (auto ret = ParseTupleLitForParenExpr(leftParenPos); ret) {
            return ret;
        }
    }
    // Not identifier.
    OwnedPtr<Expr> expr = ParseExpr(ek == ExprKind::IF_COND_EXPR ? ExprKind::EXPR_IN_IF_COND_TUPLE
            : ek == ExprKind::WHILE_COND_EXPR                    ? ExprKind::EXPR_IN_WHILE_COND_TUPLE
                                                                 : ExprKind::EXPR_IN_TUPLE);
    if (Seeing(TokenKind::COMMA)) {
        // It's actually a tuple literal.
        auto ret = ParseTupleLitForParenExprComma(leftParenPos, std::move(expr));
        if (ret) {
            return ret;
        }
    }

    if (!Skip(TokenKind::RPAREN)) {
        DiagExpectedRightDelimiter("(", leftParenPos);
        ConsumeUntilAny({TokenKind::NL, TokenKind::SEMI, TokenKind::RPAREN});
    }
    Position rightParenPos = lastToken.Begin();
    OwnedPtr<ParenExpr> ret = MakeOwned<ParenExpr>();
    ret->leftParenPos = leftParenPos;
    ret->rightParenPos = rightParenPos;
    ret->expr = std::move(expr);
    ret->begin = leftParenPos;
    ret->end = lastToken.End();
    return ret;
}

OwnedPtr<FuncArg> ParserImpl::ParseFuncArg()
{
    OwnedPtr<FuncArg> ret = MakeOwned<FuncArg>();
    ret->begin = lookahead.Begin();

    if (SeeingNamedFuncArgs()) {
        ret->name = ExpectIdentifierWithPos(*ret);
        Next();
        ret->colonPos = lookahead.Begin();
    } else if (Skip(TokenKind::INOUT)) {
        ret->withInout = true;
        ret->inoutPos = lastToken.Begin();
    }
    auto tmpExpr = ParseExpr(ExprKind::EXPR_IN_CALLSUFFIX);
    ret->expr = std::move(tmpExpr);
    if (ret->expr) {
        ret->end = ret->expr->end;
    }
    return ret;
}

void ParserImpl::ParseTryWithResource(const ScopeKind& scopeKind, TryExpr& tryExpr)
{
    Next();
    tryExpr.lParen = lookahead.Begin();
    std::vector<std::pair<std::string, Position>> varDeclName;
    do {
        if (lastToken.kind == TokenKind::COMMA) {
            tryExpr.resourceSpecCommaPos.push_back(lastToken.Begin());
        }
        OwnedPtr<VarDecl> decl = MakeOwned<VarDecl>();
        ChainScope cs(*this, decl.get());
        decl->isVar = false;
        decl->isResourceVar = true;
        decl->identifier = ExpectIdentifierWithPos(*decl);
        decl->begin = lookahead.Begin();
        auto iter = std::find_if(
            varDeclName.begin(), varDeclName.end(), [&decl](auto& p) { return p.first == decl->identifier; });
        if (iter != varDeclName.end()) {
            DiagRedefinedResourceName({decl->identifier, decl->identifier.Begin()}, *iter);
        } else {
            varDeclName.emplace_back(decl->identifier, decl->identifier.Begin());
        }
        if (Skip(TokenKind::COLON)) {
            decl->colonPos = lastToken.Begin();
            decl->type = ParseRefType();
        }
        if (!Skip(TokenKind::ASSIGN)) {
            ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_assignment, lookahead, ConvertToken(lookahead));
        } else {
            decl->assignPos = lastToken.Begin();
        }
        decl->initializer = ParseExpr(ExprKind::VAR_INIT);
        decl->end = lastToken.End();
        tryExpr.resourceSpec.emplace_back(std::move(decl));
    } while (Skip(TokenKind::COMMA));
    if (!Skip(TokenKind::RPAREN)) {
        DiagExpectedRightDelimiter("(", tryExpr.lParen);
    } else {
        tryExpr.rParen = lastToken.Begin();
    }
    tryExpr.isDesugaredFromTryWithResources = true;
    tryExpr.tryBlock = ParseBlock(scopeKind);
}

OwnedPtr<WhileExpr> ParserImpl::ParseWhileExpr()
{
    OwnedPtr<WhileExpr> ret = MakeOwned<WhileExpr>();
    ret->begin = lookahead.Begin();
    ret->whilePos = lookahead.Begin();
    if (!Skip(TokenKind::LPAREN)) {
        ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_left_paren, lookahead, ConvertToken(lookahead));
    } else {
        ret->leftParenPos = lastToken.Begin();
    }
    ret->condExpr = ParseExpr(ExprKind::WHILE_COND_EXPR);
    if (!ret->condExpr->TestAttr(Attribute::HAS_BROKEN)) {
        (void)CheckCondition(ret->condExpr.get());
    }

    if (!Skip(TokenKind::RPAREN) && !ret->leftParenPos.IsZero()) {
        if (Is<LetPatternDestructor>(ret->condExpr) && Skip(TokenKind::WHERE)) {
            diag.DiagnoseRefactor(DiagKindRefactor::parse_expected_if_let_andand, lastToken);
        } else {
            DiagExpectedRightDelimiter("(", ret->leftParenPos);
        }
        ret->body = MakeInvalid<Block>(lookahead.Begin());
        ret->end = lookahead.Begin();
        ConsumeUntilIfExprEnd();
        return ret;
    }
    ret->rightParenPos = lastToken.Begin();
    ret->body = ParseBlock(ScopeKind::FUNC_BODY);
    if (ret->body->TestAttr(Attribute::IS_BROKEN)) {
        ret->EnableAttr(Attribute::IS_BROKEN);
    }
    ret->end = ret->body->end;
    return ret;
}

OwnedPtr<AST::ForInExpr> ParserImpl::ParseForInExpr()
{
    Position begin = lookahead.Begin();
    if (!Skip(TokenKind::LPAREN)) {
        ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_left_paren, lookahead, ConvertToken(lookahead));
        ConsumeUntilIfExprEnd();
        return MakeInvalid<ForInExpr>(lastToken.End());
    }

    OwnedPtr<ForInExpr> ret = MakeOwned<ForInExpr>();
    ret->begin = begin;
    ret->leftParenPos = lastToken.Begin();
    ret->pattern = ParsePattern({}, false, true);
    ret->pattern->ctxExpr = ret->inExpression.get();
    if (!Skip(TokenKind::IN)) {
        ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_in_forin_expression, lookahead, ConvertToken(lookahead));
        ConsumeUntilIfExprEnd();
        ret->EnableAttr(Attribute::IS_BROKEN);
        ret->end = lastToken.End();
        return ret;
    }
    ret->inPos = lookahead.Begin();
    ret->inExpression = ParseExpr();
    if (Skip(TokenKind::WHERE)) {
        ret->wherePos = lookahead.Begin();
        ret->patternGuard = ParseExpr();
    }
    if (!Skip(TokenKind::RPAREN)) {
        DiagExpectedRightDelimiter("(", ret->leftParenPos);
        ConsumeUntilIfExprEnd();
        ret->EnableAttr(Attribute::IS_BROKEN);
        ret->end = lastToken.End();
        return ret;
    }
    ret->rightParenPos = lastToken.Begin();
    SkipBlank(TokenKind::NL);
    ret->body = ParseBlock(ScopeKind::FUNC_BODY);
    ret->end = ret->body->end;
    return ret;
}

OwnedPtr<DoWhileExpr> ParserImpl::ParseDoWhileExpr()
{
    OwnedPtr<DoWhileExpr> doWhileExpr = MakeOwned<DoWhileExpr>();
    doWhileExpr->begin = lookahead.Begin();
    doWhileExpr->doPos = lookahead.Begin();
    doWhileExpr->body = ParseBlock(ScopeKind::FUNC_BODY);
    if (!Skip(TokenKind::WHILE)) {
        ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_while_in_do_while, lookahead, ConvertToken(lookahead));
        ConsumeUntilAny({TokenKind::NL, TokenKind::WHILE});
        if (!Skip(TokenKind::WHILE)) {
            doWhileExpr->EnableAttr(Attribute::IS_BROKEN);
            doWhileExpr->end = lastToken.End();
            return doWhileExpr;
        }
    }
    doWhileExpr->whilePos = lookahead.Begin();
    if (!Skip(TokenKind::LPAREN)) {
        ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_left_paren, lookahead, ConvertToken(lookahead));
    } else {
        doWhileExpr->leftParenPos = lastToken.Begin();
    }
    doWhileExpr->condExpr = ParseExpr();
    if (!Skip(TokenKind::RPAREN)) {
        if (!doWhileExpr->leftParenPos.IsZero()) {
            DiagExpectedRightDelimiter("(", doWhileExpr->leftParenPos);
        }
        ConsumeUntil(TokenKind::NL);
    }
    doWhileExpr->rightParenPos = lastToken.Begin();
    doWhileExpr->end = lastToken.End();
    return doWhileExpr;
}

OwnedPtr<Expr> ParserImpl::ParseWildcardExpr()
{
    Next();
    auto expr = MakeOwned<WildcardExpr>(lastToken.Begin());
    return expr;
}

OwnedPtr<RefExpr> ParserImpl::ParseRefExpr(ExprKind ek)
{
    OwnedPtr<RefExpr> ret = MakeOwned<RefExpr>();
    ret->isThis = lookahead.Value() == "this";
    ret->isSuper = lookahead.Value() == "super";
    ret->begin = lookahead.Begin();
    ret->ref.identifier = ExpectIdentifierWithPos(*ret);
    ret->end = ret->begin + lookahead.Value().size();
    if (!Seeing(TokenKind::LT)) {
        return ret;
    }
    ParserScope scope(*this);
    ret->leftAnglePos = lookahead.Begin();
    Next();
    // collecting diagnoses in `ParseTypeArguments` and storing these diagnoses to a cache
    diag.Prepare();
    auto [isGenericArgList, typeArguments] = ParseTypeArguments(ek);
    if (isGenericArgList) {
        // parse type success, handle those diagnoses which were stored in the cache
        ret->typeArguments = std::move(typeArguments);
        ret->rightAnglePos = lastToken.Begin();
        diag.Commit();
    } else {
        diag.ClearTransaction();
        // if it is like: if a<b {} or (a < b, c >= d), reset parser.
        scope.ResetParserScope();
        ret->leftAnglePos = INVALID_POSITION;
        ret->rightAnglePos = INVALID_POSITION;
    }
    if (ret->rightAnglePos != INVALID_POSITION) {
        ret->end = ret->rightAnglePos;
        ret->end.column += 1;
    }
    return ret;
}

OwnedPtr<SpawnExpr> ParserImpl::ParseSpawnExpr()
{
    OwnedPtr<SpawnExpr> ret = MakeOwned<SpawnExpr>();
    ret->spawnPos = lastToken.Begin();
    ret->begin = lookahead.Begin();
    auto tok = lookahead;
    if (Seeing(TokenKind::LPAREN)) {
        if (newlineSkipped) {
            (void)ParseDiagnoseRefactor(
                DiagKindRefactor::parse_newline_not_allowed_between_spawn_and_argument, firstNLPosition);
        }
        if (Skip(TokenKind::LPAREN)) {
            ret->leftParenPos = lastToken.Begin();
            ret->arg = ParseExpr();
        }
        if (!Skip(TokenKind::RPAREN) && !ret->TestAttr(Attribute::HAS_BROKEN)) {
            ret->EnableAttr(Attribute::HAS_BROKEN);
            DiagExpectedRightDelimiter("(", ret->leftParenPos);
        }
        ret->rightParenPos = lastToken.Begin();
    }
    if (SeeingBuiltinAnnotation()) {
        ret->task = ParseAnnotationLambdaExpr(true);
    } else if (Skip(TokenKind::LCURL)) {
        ret->task = ParseLambdaExprWithTrailingClosure();
    } else {
        DiagExpectedExpression();
        ret->task = MakeInvalid<InvalidExpr>(lookahead.Begin());
    }
    if (!ret->task || ret->task->astKind != ASTKind::LAMBDA_EXPR) {
        ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_left_brace, tok, ConvertToken(lookahead));
        ret->EnableAttr(Attribute::IS_BROKEN);
    }
    if (ret->task && ret->task->astKind == ASTKind::LAMBDA_EXPR) {
        auto task = As<ASTKind::LAMBDA_EXPR>(ret->task.get());
        CJC_NULLPTR_CHECK(task);
        if (!task->funcBody->paramLists[0].get()->params.empty()) {
            DiagExpectedNoArgumentsInSpawn(task->funcBody->paramLists[0].get()->params, ret->begin);
            ret->EnableAttr(Attribute::IS_BROKEN);
        }
    }
    ret->end = lastToken.End();
    return ret;
}

OwnedPtr<SynchronizedExpr> ParserImpl::ParseSynchronizedExpr()
{
    OwnedPtr<SynchronizedExpr> ret = MakeOwned<SynchronizedExpr>();
    ret->syncPos = lastToken.Begin();
    ret->begin = lookahead.Begin();
    if (!Skip(TokenKind::LPAREN)) {
        ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_left_paren, lookahead, ConvertToken(lookahead));
        ret->EnableAttr(Attribute::HAS_BROKEN);
    }
    ret->leftParenPos = lastToken.Begin();
    ret->mutex = ParseExpr();
    if (!Skip(TokenKind::RPAREN) && !ret->TestAttr(Attribute::HAS_BROKEN)) {
        DiagExpectedRightDelimiter("(", ret->leftParenPos);
        ret->EnableAttr(Attribute::HAS_BROKEN);
    }
    ret->rightParenPos = lastToken.Begin();
    ret->body = ParseBlock();
    ret->end = ret->body->end;
    return ret;
}

OwnedPtr<Expr> ParserImpl::ParseAnnotationLambdaExpr(bool isTailClosure)
{
    std::vector<OwnedPtr<Annotation>> annos;
    ParseAnnotations(annos);
    Next();
    auto ret = isTailClosure ? ParseLambdaExprWithTrailingClosure() : ParseLambdaExpr();
    for (auto& it : annos) {
        if (it->kind == AnnotationKind::NUMERIC_OVERFLOW) {
            ret->EnableAttr(Attribute::NUMERIC_OVERFLOW);
            ret->overflowStrategy = it->overflowStrategy;
        } else if (it->kind == AnnotationKind::ENSURE_PREPARED_TO_MOCK) {
            ret->EnableAttr(Attribute::MOCK_SUPPORTED);
        } else {
            DiagExpectedDeclaration(ret->begin, "lambda expression");
        }
    }
    return ret;
}

OwnedPtr<Expr> ParserImpl::ParseMacroExprOrLambdaExpr()
{
    if (SeeingAnnotationLambdaExpr()) {
        return ParseAnnotationLambdaExpr();
    }
    if (SeeingBuiltinAnnotation()) {
        DiagExpectedExpression();
        return MakeInvalid<Expr>(lastToken.End());
    }
    return ParseMacroCall<MacroExpandExpr>();
}

OwnedPtr<FuncParam> ParserImpl::ParseFuncParam()
{
    auto consumeTarget = [this]() {
        return SeeingAny({TokenKind::DOUBLE_ARROW, TokenKind::COLON, TokenKind::COMMA, TokenKind::NL, TokenKind::SEMI,
            TokenKind::RCURL}) || SeeingCombinator(combinedDoubleArrow);
    };
    auto param = MakeOwned<FuncParam>();
    ChainScope c(*this, param.get());
    // add process of '_'
    if (Skip(TokenKind::WILDCARD)) {
        param->identifier = "_";
        param->identifier.SetPos(lookahead.Begin(), lookahead.End());
    } else {
        param->identifier = ExpectIdentifierWithPos(*param);
    }
    if (param->identifier == INVALID_IDENTIFIER) {
        chainedAST.back()->EnableAttr(Attribute::IS_BROKEN);
        ConsumeUntilAny(consumeTarget, false);
    }
    param->isVar = false;
    param->begin = lookahead.Begin();
    if (Skip(TokenKind::COLON)) {
        param->colonPos = lastToken.Begin();
        param->type = ParseType();
        if (param->type) {
            param->end = param->type->end;
        }
    } else {
        param->end = param->identifier.GetRawEndPos();
    }
    return param;
}

OwnedPtr<FuncParamList> ParserImpl::ParseFuncParamListInLambdaExpr()
{
    auto paramList = MakeOwned<FuncParamList>();
    ChainScope cs(*this, paramList.get());
    if (SeeingInvaildParamListInLambdaExpr()) {
        paramList->begin = lookahead.Begin();
        paramList->end = lookahead.Begin();
        return paramList;
    }
    // Parse paramList.
    ParseZeroOrMoreSepTrailing(
        [&paramList](const Position& pos) {
            paramList->params.back()->commaPos = pos;
        },
        [this, &paramList]() {
            auto param = ParseFuncParam();
            if (param->TestAttr(Attribute::IS_BROKEN)) {
                paramList->EnableAttr(Attribute::IS_BROKEN);
            }
            paramList->params.emplace_back(std::move(param));
        }, TokenKind::DOUBLE_ARROW);
    if (!paramList->params.empty()) {
        paramList->begin = paramList->params.front()->begin;
        paramList->end = paramList->params.back()->end;
    }
    return paramList;
}

OwnedPtr<FuncBody> ParserImpl::ParseFuncBodyInLambdaExpr(bool isTailClosure)
{
    auto funcBody = MakeOwned<FuncBody>();
    ChainScope cs(*this, funcBody.get());
    auto paramList = ParseFuncParamListInLambdaExpr();
    if (paramList->TestAttr(Attribute::IS_BROKEN)) {
        chainedAST.back()->EnableAttr(Attribute::HAS_BROKEN);
    }
    if (!paramList->params.empty() && !Skip(TokenKind::DOUBLE_ARROW) && !SkipCombinedDoubleArrow()) {
        if (!chainedAST.back()->TestAttr(Attribute::HAS_BROKEN)) {
            ParseDiagnoseRefactor(
                DiagKindRefactor::parse_expected_double_arrow_in_lambda, lookahead, ConvertToken(lookahead));
        }
        ConsumeUntilAny(
            [this]() {
                return SeeingAny({TokenKind::NL, TokenKind::SEMI, TokenKind::DOUBLE_ARROW, TokenKind::RCURL}) ||
                    SeeingCombinator(combinedDoubleArrow);
            },
            false);
        if (!Skip(TokenKind::DOUBLE_ARROW)) {
            (void)SkipCombinedDoubleArrow();
        }
    } else {
        if (paramList->params.empty() && !isTailClosure) {
            // TailClosure can omit => if param is empty ,and other lambdaExpr is not allowed.
            bool hasDoubleArrow =
                lastToken.kind == TokenKind::DOUBLE_ARROW || Skip(TokenKind::DOUBLE_ARROW) || SkipCombinedDoubleArrow();
            bool hasWildard = Seeing({TokenKind::WILDCARD, TokenKind::DOUBLE_ARROW}) ||
                Seeing({TokenKind::WILDCARD, TokenKind::COMMA}) ||
                SeeingTokenAndCombinator(TokenKind::WILDCARD, combinedDoubleArrow);
            if (!hasDoubleArrow && !hasWildard) {
                ParseDiagnoseRefactor(
                    DiagKindRefactor::parse_expected_double_arrow_in_lambda, lookahead, ConvertToken(lookahead));
            }
        }
        if (lastToken.kind == TokenKind::DOUBLE_ARROW || Skip(TokenKind::DOUBLE_ARROW) || SkipCombinedDoubleArrow()) {
            funcBody->doubleArrowPos = lastToken.Begin();
        }
    }
    funcBody->begin = lastToken.Begin();
    funcBody->paramLists.emplace_back(std::move(paramList));
    funcBody->body = ParseExpressionOrDeclarations(ScopeKind::FUNC_BODY);
    funcBody->end = funcBody->body->end;
    return funcBody;
}

OwnedPtr<AST::LambdaExpr> ParserImpl::ParseLambdaExpr()
{
    OwnedPtr<LambdaExpr> ret = ParseBaseLambdaExpr();
    ChainScope cs(*this, ret.get());
    ret->funcBody = ParseFuncBodyInLambdaExpr(false);
    ret->end = ret->funcBody->end;
    ret->funcBody->body->leftCurlPos = ret->begin;
    return ret;
}

OwnedPtr<AST::Expr> ParserImpl::ParseVArrayExpr()
{
    OwnedPtr<ArrayExpr> ret = MakeOwned<ArrayExpr>();
    ChainScope cs(*this, ret.get());
    ret->begin = lookahead.Begin();
    if (!Seeing(TokenKind::LT)) {
        ParseDiagnoseRefactor(DiagKindRefactor::parse_varray_type_parameter, lastToken.End());
        return MakeOwned<InvalidExpr>(lookahead.Begin());
    }
    ret->type = ParseVarrayType();
    ret->isValueArray = true;
    if (!Skip(TokenKind::LPAREN)) {
        // Only 'VArray <type, $n>(expr)' or 'VArray <type, $n>{ i => expr }' is valid grammar.
        if (!ret->type->TestAttr(Attribute::IS_BROKEN) && !Seeing(TokenKind::LCURL)) {
            (void)ParseDiagnoseRefactor(DiagKindRefactor::parse_varray_with_paren, ret->type->end);
            ret->EnableAttr(Attribute::IS_BROKEN);
        }
        ret->end = ret->type->end;
        return ret;
    }
    ret->leftParenPos = lookahead.Begin();
    ParseZeroOrMoreSepTrailing(
        [&ret](const Position& pos) {
            ret->args.back()->commaPos = pos;
        },
        [this, &ret]() {
            ret->args.emplace_back(ParseFuncArg());
        }, TokenKind::RPAREN);
    if (!Skip(TokenKind::RPAREN)) {
        ret->EnableAttr(Attribute::HAS_BROKEN);
        DiagExpectedRightDelimiter("(", ret->leftParenPos);
    }
    ret->rightParenPos = lookahead.Begin();
    ret->end = lookahead.End();
    return ret;
}

OwnedPtr<AST::LambdaExpr> ParserImpl::ParseLambdaExprWithTrailingClosure()
{
    OwnedPtr<LambdaExpr> ret = ParseBaseLambdaExpr();
    ChainScope cs(*this, ret.get());
    ret->funcBody = ParseFuncBodyInLambdaExpr(true);
    ret->end = ret->funcBody->end;
    ret->funcBody->body->leftCurlPos = ret->begin;
    return ret;
}

OwnedPtr<LambdaExpr> ParserImpl::ParseBaseLambdaExpr()
{
    auto ret = MakeOwned<LambdaExpr>();
    ret->begin = lastToken.Begin();
    if (SeeingInvaildOperaterInLambdaExpr()) {
        Skip(TokenKind::IDENTIFIER);
        SkipKeyWordIdentifier();
        Peek();
        ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_ccd_in_lambda, lookahead, ConvertToken(lookahead));
        ConsumeUntilAny(
            [this]() {
                return SeeingAny({TokenKind::NL, TokenKind::SEMI, TokenKind::DOUBLE_ARROW, TokenKind::RCURL}) ||
                    SeeingCombinator(combinedDoubleArrow);
            },
            false);
    }
    return ret;
}

OwnedPtr<AST::Expr> ParserImpl::ParseTypeConvExpr()
{
    OwnedPtr<TypeConvExpr> ret = MakeOwned<TypeConvExpr>();
    ret->begin = lookahead.Begin();
    OwnedPtr<PrimitiveType> type = MakeOwned<PrimitiveType>();
    type->begin = lookahead.Begin();
    type->end = lookahead.End();
    type->str = lookahead.Value();
    type->kind = TOKENKIND_TO_PRIMITIVE_TYPEKIND_MAP.at(lookahead.kind);
    Next();
    ret->type = std::move(type);
    if (!Skip(TokenKind::LPAREN)) {
        DiagExpectedLeftParenAfter(ret->begin, lastToken.Value());
        return MakeOwned<InvalidExpr>(lookahead.Begin());
    }
    ret->leftParenPos = lastToken.Begin();
    ret->expr = ParseExpr();
    if (!Skip(TokenKind::RPAREN)) {
        DiagExpectedRightDelimiter("(", ret->leftParenPos);
        ConsumeUntilAny({TokenKind::NL, TokenKind::SEMI, TokenKind::RPAREN});
        return MakeOwned<InvalidExpr>(lookahead.Begin());
    }
    ret->rightParenPos = lastToken.Begin();
    ret->end = lastToken.End();
    return ret;
}
