// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 * This file implements type parse apis.
 */

#include "ParserImpl.h"

#include "cangjie/AST/Match.h"
#include "cangjie/Basic/StringConvertor.h"
#include "cangjie/Utils/Utils.h"

using namespace Cangjie;
using namespace AST;

// BaseType is atomicType in BNF.
OwnedPtr<AST::Type> ParserImpl::ParseBaseType()
{
    if (Seeing(TokenKind::IDENTIFIER) || SeeingContextualKeyword()) {
        return ParseQualifiedType();
    }
    // Paren Type, Tuple Type, or Function Type
    if (Skip(TokenKind::LPAREN)) {
        return ParseTypeWithParen();
    }
    if (SeeingPrimTypes()) {
        OwnedPtr<PrimitiveType> primType = MakeOwned<PrimitiveType>();
        primType->begin = lookahead.Begin();
        primType->end = lookahead.End();
        primType->str = lookahead.Value();
        primType->kind = TOKENKIND_TO_PRIMITIVE_TYPEKIND_MAP.at(lookahead.kind);
        Next();
        return primType;
    }
    if (Skip(TokenKind::THISTYPE)) {
        if (enableThis) {
            return MakeOwned<ThisType>(lookahead.Begin());
        } else {
            DiagThisTypeNotAllow();
            // When 'This' appearred in invalid position, should return invalid type for correct semantic.
            return MakeOwned<InvalidType>(lookahead.Begin());
        }
    }
    if (Skip(TokenKind::VARRAY)) {
        return ParseVarrayType();
    }
    auto type = MakeOwned<InvalidType>(lookahead.Begin());
    DiagExpectedTypeName();
    type->EnableAttr(Attribute::IS_BROKEN);
    return type;
}

OwnedPtr<AST::Type> ParserImpl::ParseVarrayType()
{
    OwnedPtr<VArrayType> ret = MakeOwned<VArrayType>();
    ret->varrayPos = lastToken.Begin();
    ret->begin = lastToken.Begin();
    ChainScope cs(*this, ret.get());
    if (!Skip(TokenKind::LT)) {
        ParseDiagnoseRefactor(DiagKindRefactor::parse_varray_type_parameter, lastToken.End());
        return MakeOwned<InvalidType>(lookahead.Begin());
    }
    ret->leftAnglePos = lastToken.Begin();
    // <T, $N>
    //  ^ Parse the type argument of VArray.
    ret->typeArgument = ParseType();
    if (ret->typeArgument->IsInvalid()) {
        return MakeOwned<InvalidType>(lookahead.Begin());
    }
    // <T, $N>
    //   ^ Parse the comma between the type argument and constant type.
    if (!Skip(TokenKind::COMMA)) {
        DiagVArrayTypeArgMismatch(MakeRange(ret->leftAnglePos, lookahead.End()), "a type argument and size literal");
        return MakeOwned<InvalidType>(lookahead.Begin());
    }
    ret->typeArgument->commaPos = lastToken.Begin();
    // <T, $N>
    //     ^ Parse the constant type prefix.
    skipNL = false;
    if (!Skip(TokenKind::DOLLAR)) {
        DiagVArrayTypeArgMismatch(MakeRange(lookahead.Begin(), lookahead.End()),
            "a '$' follows an integer literal as the second generic argument");
        return MakeOwned<InvalidType>(lookahead.Begin());
    }
    OwnedPtr<ConstantType> constType = MakeOwned<ConstantType>();
    constType->dollarPos = lastToken.Begin();
    constType->begin = lastToken.Begin();
    // <T, $N>
    //      ^ Parse the constant value.
    if (!Seeing(TokenKind::INTEGER_LITERAL)) {
        ParseDiagnoseRefactor(DiagKindRefactor::parse_expect_integer_literal_varray, constType->dollarPos);
        ConsumeUntil(TokenKind::NL);
        return MakeOwned<InvalidType>(lookahead.Begin());
    }
    skipNL = true;
    constType->constantExpr = ParseLitConst();
    constType->end = lookahead.End();
    ret->constantType = std::move(constType);
    if (!Skip(TokenKind::GT)) {
        DiagExpectedRightDelimiter("<", ret->leftAnglePos);
        return MakeOwned<InvalidType>(lookahead.Begin());
    }
    ret->rightAnglePos = lookahead.Begin();
    ret->end = lookahead.End();
    return ret;
}

OwnedPtr<AST::Type> ParserImpl::ParseQualifiedType()
{
    OwnedPtr<AST::Type> baseType = ParseRefType(false);
    while (Seeing(TokenKind::DOT)) {
        auto dotPos = Peek().Begin();
        Next();
        // Qualified type is userType in BNF.
        auto qualifiedType = MakeOwned<QualifiedType>();
        ChainScope cs(*this, qualifiedType.get());
        qualifiedType->begin = baseType->begin;
        qualifiedType->baseType = std::move(baseType);
        qualifiedType->dotPos = dotPos;
        qualifiedType->field = ExpectIdentifierWithPos(*qualifiedType);
        qualifiedType->end = qualifiedType->field.GetRawEndPos();
        if (Skip(TokenKind::LT)) {
            qualifiedType->leftAnglePos = lastToken.Begin();
            qualifiedType->typeArguments = ParseTypeArguments().second;
            qualifiedType->end = lastToken.End();
            qualifiedType->rightAnglePos = lastToken.Begin();
        }
        baseType = std::move(qualifiedType);
    }
    return baseType;
}

OwnedPtr<AST::Type> ParserImpl::ParseTupleType(
    std::vector<OwnedPtr<Type>> types, const Position lParenPos, const Position rParenPos) const
{
    OwnedPtr<TupleType> tupleType = MakeOwned<TupleType>();
    tupleType->begin = lParenPos;
    tupleType->leftParenPos = lParenPos;
    tupleType->rightParenPos = rParenPos;
    for (auto& type : types) {
        tupleType->commaPosVector.emplace_back(type->commaPos);
        tupleType->fieldTypes.emplace_back(std::move(type));
    }
    tupleType->end = rParenPos;
    tupleType->end.column += 1;
    return tupleType;
}

OwnedPtr<Type> ParserImpl::ParseTypeParameterInTupleType(
    std::unordered_map<std::string, Position>& typeNameMap)
{
    while (Skip(TokenKind::NL)) {
    }
    Position colonPos;
    SrcIdentifier typeParameterName;
    if (Seeing({TokenKind::IDENTIFIER, TokenKind::COLON}) || SeeingPrimaryKeyWordContext(TokenKind::COLON)) {
        Next();
        typeParameterName = ParseIdentifierFromToken(lastToken);
        auto it = typeNameMap.find(typeParameterName);
        if (it != typeNameMap.end()) {
            auto builder = diag.DiagnoseRefactor(
                DiagKindRefactor::parse_duplicate_type_parameter_name, MakeRange(typeParameterName), typeParameterName);
            builder.AddHint(MakeRange(it->second, typeParameterName));
        } else {
            typeNameMap.emplace(typeParameterName, typeParameterName.Begin());
        }
        while (Skip(TokenKind::NL)) {
        }
        Next();
        colonPos = lastToken.Begin();
    }
    auto type = ParseType();
    type->typeParameterName = typeParameterName;
    type->typeParameterNameIsRawId = typeParameterName.IsRaw();
    type->colonPos = colonPos;
    type->typePos = type->begin;
    type->begin = typeParameterName.ZeroPos() ? type->begin : typeParameterName.Begin();
    return type;
}

OwnedPtr<AST::Type> ParserImpl::ParseTypeWithParen()
{
    Position lParenPos = lastToken.Begin();
    std::vector<OwnedPtr<Type>> types;
    std::unordered_map<std::string, Position> typeNameMap;
    ParseZeroOrMoreSepTrailing([&types](const Position commaPos) { types.back()->commaPos = commaPos; },
        [this, &types, &typeNameMap]() {
            if (Seeing(TokenKind::RPAREN) && types.size() > 1) {
                return;
            }
            types.emplace_back(ParseTypeParameterInTupleType(typeNameMap));
        }, TokenKind::RPAREN);
    // in a parameter type list, either all parameters must be named, or none of them; mixed is not allowed
    if (std::any_of(types.begin(), types.end(),
        [](const OwnedPtr<Type>& type) { return type->typeParameterName.empty(); }) &&
        std::any_of(types.begin(), types.end(),
        [](const OwnedPtr<Type>& type) { return !type->typeParameterName.empty(); })) {
        ParseDiagnoseRefactor(
            DiagKindRefactor::parse_all_parameters_must_be_named, MakeRange(types.front()->begin, types.back()->end));
    }
    if (!Skip(TokenKind::RPAREN)) {
        DiagExpectedRightDelimiter("(", lParenPos);
        return MakeOwned<InvalidType>(lookahead.Begin());
    }
    Position rParenPos = lastToken.Begin();
    if (Skip(TokenKind::ARROW)) {
        return ParseFuncType(std::move(types), lParenPos, rParenPos);
    }
    // This is a paren type.
    if (types.size() == 1) {
        return ParseParenType(lParenPos, rParenPos, std::move(types[0]));
    }
    // This is a tuple type. 2 is the minimum dimension allowed by tuple.
    if (types.size() >= 2) {
        return ParseTupleType(std::move(types), lParenPos, rParenPos);
    }
    // This is treated as a broken function, which has empty types and no arrow.
    ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_arrow_in_func_type, lookahead.Begin());
    return ParseFuncType(std::move(types), lParenPos, rParenPos);
}

OwnedPtr<ParenType> ParserImpl::ParseParenType(
    const Position& lParenPos, const Position& rParenPos, OwnedPtr<Type> type)
{
    if (!type->typeParameterName.empty()) {
        auto builder =
            ParseDiagnoseRefactor(DiagKindRefactor::parse_only_tuple_and_func_type_allow_type_parameter_name,
                MakeRange(type->begin, type->begin + type->typeParameterName.size()), type->typeParameterName);
        builder.AddNote("only tuple type and function type support type parameter name");
    }
    OwnedPtr<ParenType> pt = MakeOwned<ParenType>();
    pt->type = std::move(type);
    pt->leftParenPos = lParenPos;
    pt->rightParenPos = rParenPos;
    pt->begin = lParenPos;
    pt->end = rParenPos;
    pt->end.column += 1;
    return pt;
}

OwnedPtr<FuncType> ParserImpl::ParseFuncType(
    std::vector<OwnedPtr<Type>> types, const Position& lParenPos, const Position& rParenPos)
{
    OwnedPtr<FuncType> ft = MakeOwned<FuncType>();
    ft->arrowPos = lastToken.Begin();
    ft->begin = lParenPos;
    ft->leftParenPos = lParenPos;
    ft->rightParenPos = rParenPos;
    ft->retType = ParseType();
    ft->end = ft->retType->end;
    ft->paramTypes = std::move(types);
    return ft;
}

// Parse the syntactic sugar of option types.
OwnedPtr<AST::Type> ParserImpl::ParsePrefixType()
{
    // See the symbol '?'.
    if (Seeing(TokenKind::QUEST)) {
        OwnedPtr<AST::OptionType> optionType = MakeOwned<AST::OptionType>();
        optionType->begin = lookahead.Begin();
        // Handle all '?'
        bool prevSkipNL = skipNL;
        skipNL = false;
        while (Skip(TokenKind::QUEST)) {
            optionType->questNum++;
            if (lastToken.kind == TokenKind::QUEST) {
                optionType->questVector.emplace_back(lastToken.Begin());
            }
            optionType->end = lastToken.End();
            if (Seeing(TokenKind::NL)) {
                ParseDiagnoseRefactor(
                    DiagKindRefactor::parse_newline_not_allowed_between_quest_and_type, lookahead.End());
                SkipBlank(TokenKind::NL);
            }
        }
        // Parse the type after the last '?'.
        OwnedPtr<AST::Type> baseType = ParseBaseType();
        skipNL = true;
        // Skip tailing newline, if previous skip newline enabled.
        while (prevSkipNL && Skip(TokenKind::NL)) {
        }
        optionType->end = baseType->end;
        optionType->componentType = std::move(baseType);
        return optionType;
    } else {
        OwnedPtr<AST::Type> baseType = ParseBaseType();
        return baseType;
    }
}

OwnedPtr<AST::Type> ParserImpl::ParseType()
{
    auto postType = ParsePrefixType();
    if (Seeing(TokenKind::ARROW)) {
            if (postType->astKind == ASTKind::FUNC_TYPE) {
                DiagRedundantArrowAfterFunc(*postType);
                ConsumeUntilAny({TokenKind::RCURL, TokenKind::NL}, false);
            } else {
                DiagParseExpectedParenthis(postType);
            }
    }
    return postType;
}

OwnedPtr<AST::RefType> ParserImpl::ParseRefType(bool onlyRef)
{
    OwnedPtr<RefType> ret = MakeOwned<RefType>();
    ChainScope cs(*this, ret.get());
    ret->ref.identifier = ExpectIdentifierWithPos(*ret);
    if (ret->ref.identifier == INVALID_IDENTIFIER) {
        ret->EnableAttr(Attribute::IS_BROKEN);
        TryConsumeUntilAny({TokenKind::LT});
    }
    ret->begin = lookahead.Begin();
    ret->end = lookahead.End();
    if (!onlyRef && Skip(TokenKind::LT)) {
        ret->leftAnglePos = lastToken.Begin();
        ret->typeArguments = ParseTypeArguments().second;
        ret->rightAnglePos = lastToken.Begin();
        ret->end = lastToken.End();
    }
    return ret;
}

std::pair<bool, std::vector<OwnedPtr<AST::Type>>> ParserImpl::ParseTypeArguments(ExprKind ek)
{
    Position leftAnglePos = lastToken.Begin();
    std::vector<OwnedPtr<AST::Type>> ret;
    if (Skip(TokenKind::GT)) {
        ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_type_argument, lastToken.Begin());
        return {true, std::move(ret)};
    }
    ParseOneOrMoreSepTrailing(
        [&ret](const Position commaPos) {
            if (!ret.empty()) {
                ret.back()->commaPos = commaPos;
            }
        },
        [this, &ret]() {
            while (Skip(TokenKind::NL)) {
            }
            auto type = ParseType();
            if (type && !type->TestAttr(Attribute::IS_BROKEN)) {
                ret.emplace_back(std::move(type));
            }
        }, TokenKind::GT);
    if (!Skip(TokenKind::GT) && !ret.empty()) {
        DiagExpectedRightDelimiter("<", leftAnglePos);
        ret.clear();
        return {false, std::move(ret)};
    }

    if (ret.empty() ||
        std::any_of(ret.begin(), ret.end(), [](auto& type) { return type->astKind == ASTKind::INVALID_TYPE; })) {
        ret.clear();
        return {false, std::move(ret)};
    }
    if (IsExprFollowedComma(ek) && TypeArgsMaybeConfusedWithExprWithComma(ret) &&
        !IsLegFollowForGenArgInExprWithComma(ek)) {
        // it may be part of an expr with comma. e.g. <b,c> in (a<b,c>=d)
        ret.clear();
        return {false, std::move(ret)};
    }
    return {true, std::move(ret)};
}
