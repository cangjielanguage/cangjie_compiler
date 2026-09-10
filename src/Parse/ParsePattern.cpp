// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements parse pattern.
 */

#include "ParserImpl.h"

#include "cangjie/AST/Match.h"
#include "cangjie/Utils/Utils.h"

using namespace Cangjie;
using namespace AST;

// Pattern
OwnedPtr<Pattern> ParserImpl::ParsePattern(const std::set<Attribute>& attributes, bool isVar, bool inDecl)
{
    if (SeeingAny({TokenKind::BOOL_LITERAL, TokenKind::INTEGER_LITERAL, TokenKind::RUNE_BYTE_LITERAL,
            TokenKind::STRING_LITERAL, TokenKind::MULTILINE_STRING, TokenKind::MULTILINE_RAW_STRING, TokenKind::SUB,
            TokenKind::RUNE_LITERAL, TokenKind::FLOAT_LITERAL}) ||
        Seeing({TokenKind::LPAREN, TokenKind::RPAREN})) {
        return ParseConstPattern();
    }
    if (Skip(TokenKind::WILDCARD)) {
        Position begin = lookahead.Begin();
        if (Skip(TokenKind::COLON)) {
            return ParseTypePattern(begin);
        } else {
            OwnedPtr<WildcardPattern> ret = MakeOwned<WildcardPattern>();
            ret->begin = begin;
            ret->end = begin;
            ret->end.column += 1;
            return ret;
        }
    }
    if (Seeing(TokenKind::LPAREN)) {
        return ParseTuplePattern(false, attributes, isVar, inDecl);
    }
    if (SeeingIdentifierAndTargetOp({TokenKind::LPAREN, TokenKind::LT, TokenKind::DOT})) {
        return ParseEnumPattern(attributes, isVar, inDecl);
    }
    if (Seeing(TokenKind::IDENTIFIER) || SeeingContextualKeyword()) {
        return ParseTypePatternOrVarOrEnumPattern(attributes, isVar, inDecl);
    }
    ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_pattern, lookahead, ConvertToken(lookahead));
    return MakeInvalid<InvalidPattern>(lookahead.Begin());
}

OwnedPtr<VarPattern> ParserImpl::ParseVarPattern(
    const std::set<Attribute>& attributes, const SrcIdentifier& identifier, const Position& begin, bool isVar) const
{
    OwnedPtr<VarPattern> ret = MakeOwned<VarPattern>(identifier, begin);
    ret->varDecl->isVar = isVar;
    for (auto& it : attributes) {
        ret->EnableAttr(it);
        ret->varDecl->EnableAttr(it);
    }
    ret->end = ret->varDecl->identifier.GetRawEndPos();
    return ret;
}

OwnedPtr<VarOrEnumPattern> ParserImpl::ParseVarOrEnumPattern(
    const std::string& identifier, const Position& begin, size_t len, bool isRawId) const
{
    Position idPos = isRawId ? begin + 1 : begin;
    SrcIdentifier id{identifier, idPos, idPos + len, isRawId};
    OwnedPtr<VarOrEnumPattern> ret = MakeOwned<VarOrEnumPattern>(std::move(id));
    ret->begin = begin;
    ret->end = id.GetRawEndPos();
    return ret;
}

OwnedPtr<Pattern> ParserImpl::ParseTypePatternOrVarOrEnumPattern(
    const std::set<Attribute>& attributes, bool isVar, bool inDecl)
{
    auto identifier = ParseIdentifierFromToken(lookahead);
    Position begin = lookahead.Begin();
    Next();
    if (Skip(TokenKind::COLON)) {
        OwnedPtr<TypePattern> typePattern = MakeOwned<TypePattern>();
        typePattern->colonPos = lastToken.Begin();
        typePattern->pattern = MakeOwned<VarPattern>(identifier, begin);
        typePattern->type = ParseType();
        if (typePattern->type->modal.Local() != ASTMode::NONE) {
            DiagUnexpectedModal(MakeRange(typePattern->type->modal.LocalBegin(), typePattern->type->modal.LocalEnd()));
            typePattern->EnableAttr(Attribute::HAS_BROKEN);
        }
        typePattern->begin = begin;
        typePattern->end = typePattern->type->end;
        return typePattern;
    }
    if (inDecl) {
        return ParseVarPattern(attributes, identifier, begin, isVar);
    }
    return ParseVarOrEnumPattern(identifier, begin, identifier.Length(), identifier.IsRaw());
}

OwnedPtr<ConstPattern> ParserImpl::ParseConstPattern()
{
    OwnedPtr<ConstPattern> constPattern = MakeOwned<ConstPattern>();
    constPattern->begin = lookahead.Begin();
    constPattern->literal = ParseLitConst();
    constPattern->end = constPattern->literal->end;
    return constPattern;
}

OwnedPtr<TypePattern> ParserImpl::ParseTypePattern(const Position& begin)
{
    OwnedPtr<TypePattern> typePattern = MakeOwned<TypePattern>();
    typePattern->colonPos = lastToken.Begin();
    typePattern->pattern = MakeOwned<WildcardPattern>(begin);

    typePattern->type = ParseType();
    if (typePattern->type->modal.Local() != ASTMode::NONE) {
        DiagUnexpectedModal(MakeRange(typePattern->type->modal.LocalBegin(), typePattern->type->modal.LocalEnd()));
        typePattern->EnableAttr(Attribute::HAS_BROKEN);
    }
    typePattern->begin = begin;
    typePattern->end = typePattern->type->end;
    return typePattern;
}

OwnedPtr<EnumPattern> ParserImpl::ParseEnumPattern(const std::set<Attribute>& attributes, bool isVar, bool inDecl)
{
    OwnedPtr<EnumPattern> enumPattern = MakeOwned<EnumPattern>();
    enumPattern->constructor = ParseAtom();
    while (Skip(TokenKind::DOT)) {
        OwnedPtr<MemberAccess> ctor =
            ParseMemberAccess(std::move(enumPattern->constructor));
        if (ctor) {
            ctor->isPattern = true;
        }
        enumPattern->constructor = std::move(ctor);
    }
    CheckTypeArgumentsInEnumPattern(enumPattern.get());
    enumPattern->begin = enumPattern->constructor->begin;
    enumPattern->end = enumPattern->constructor->end;
    for (auto& it : attributes) {
        enumPattern->EnableAttr(it);
    }
    if (Seeing(TokenKind::LPAREN)) {
        OwnedPtr<TuplePattern> tuplePattern = ParseTuplePattern(true, attributes, isVar, inDecl);
        enumPattern->leftParenPos = tuplePattern->begin;
        Position rightParenPos = tuplePattern->end;
        rightParenPos.column -= 1;
        enumPattern->rightParenPos = rightParenPos;
        enumPattern->patterns = std::move(tuplePattern->patterns);
        enumPattern->commaPosVector = tuplePattern->commaPosVector;
        enumPattern->end = tuplePattern->end;
    }
    return enumPattern;
}

void ParserImpl::CheckTypeArgumentsInEnumPattern(Ptr<const EnumPattern> enumPattern)
{
    if (enumPattern->constructor->IsInvalid()) {
        return;
    }
    if (enumPattern->constructor->astKind == ASTKind::REF_EXPR) {
        Ptr<RefExpr> refExpr = StaticAs<ASTKind::REF_EXPR>(enumPattern->constructor.get());
        if (!refExpr->typeArguments.empty()) {
            auto builder = ParseDiagnoseRefactor(
                DiagKindRefactor::parse_unexpected_declaration_in_scope, refExpr->leftAnglePos, "'<'", "enum pattern");
            builder.AddMainHintArguments("'<'");
        }
    } else {
        CJC_ASSERT(enumPattern->constructor->astKind == ASTKind::MEMBER_ACCESS);
        Ptr<Expr> expr = enumPattern->constructor.get();
        for (; expr->astKind == ASTKind::MEMBER_ACCESS; expr = StaticAs<ASTKind::MEMBER_ACCESS>(expr)->baseExpr.get()) {
            Ptr<MemberAccess> memberAccess = StaticAs<ASTKind::MEMBER_ACCESS>(expr);
            if (!memberAccess->typeArguments.empty() &&
                expr != StaticAs<ASTKind::MEMBER_ACCESS>(enumPattern->constructor.get())->baseExpr.get()) {
                auto builder = ParseDiagnoseRefactor(DiagKindRefactor::parse_unexpected_declaration_in_scope,
                    memberAccess->leftAnglePos, "'<'", "enum pattern");
                builder.AddMainHintArguments("'<'");
            }
        }
        CJC_ASSERT(expr->astKind == ASTKind::REF_EXPR);
        if (expr != StaticAs<ASTKind::MEMBER_ACCESS>(enumPattern->constructor.get())->baseExpr.get()) {
            Ptr<RefExpr> refExpr = StaticAs<ASTKind::REF_EXPR>(expr);
            if (!refExpr->typeArguments.empty()) {
                auto builder = ParseDiagnoseRefactor(DiagKindRefactor::parse_unexpected_declaration_in_scope,
                    refExpr->leftAnglePos, "'<'", "enum pattern");
                builder.AddMainHintArguments("'<'");
            }
        }
    }
}

// HACK: isEnumPatterParams is used to indicate whether this tuple pattern belongs to a EnumPattern, however
// EnumPattern's params are slightly different with TuplePattern.
OwnedPtr<TuplePattern> ParserImpl::ParseTuplePattern(
    bool isEnumPatternParams, const std::set<Attribute>& attributes, bool isVar, bool inDecl)
{
    Next(); // skip (
    OwnedPtr<TuplePattern> tuplePattern = MakeOwned<TuplePattern>();
    ChainScope cs(*this, tuplePattern.get());
    for (auto& it : attributes) {
        tuplePattern->EnableAttr(it);
    }
    tuplePattern->begin = lookahead.Begin();
    tuplePattern->leftBracePos = lookahead.Begin();
    std::vector<Position> commaPosVector;
    do {
        tuplePattern->patterns.emplace_back(ParsePattern(attributes, isVar, inDecl));
        if (Seeing(TokenKind::BITOR)) {
            DiagOrPattern();
            ConsumeUntilAny({TokenKind::COMMA, TokenKind::RPAREN}, false);
        }
        if (Seeing(TokenKind::COMMA)) {
            commaPosVector.push_back(lookahead.Begin());
        }
    } while (Skip(TokenKind::COMMA));
    if (!Skip(TokenKind::RPAREN)) {
        if (!std::any_of(tuplePattern->patterns.begin(), tuplePattern->patterns.end(),
                         [](auto& pattern) { return pattern->TestAttr(Attribute::IS_BROKEN); })) {
            DiagExpectedRightDelimiter("(", tuplePattern->begin);
        }
    }
    tuplePattern->commaPosVector = std::move(commaPosVector);
    tuplePattern->rightBracePos = lastToken.Begin();
    if (!isEnumPatternParams && tuplePattern->patterns.size() == 1) {
        DiagExpectedMoreFieldInTuplePattern();
    }

    tuplePattern->end = lastToken.End();
    return tuplePattern;
}
