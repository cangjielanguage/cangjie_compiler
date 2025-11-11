// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "ParserImpl.h"

#include "cangjie/AST/Match.h"
#include "cangjie/Basic/Print.h"

using namespace Cangjie;
using namespace AST;

namespace {
const std::string OVERFLOW_STRATEGY = "overflow";
} // namespace

void ParserImpl::ParseAttributeAnnotation(Annotation& anno)
{
    if (!Seeing(TokenKind::LSQUARE)) {
        return;
    }
    auto pos = lookahead.Begin();
    anno.lsquarePos = pos;
    Next();
    while (true) {
        if (SeeingAny({TokenKind::IDENTIFIER, TokenKind::STRING_LITERAL}) || SeeingContextualKeyword()) {
            anno.attrs.push_back(lookahead);
            Next();
        }
        if (Skip(TokenKind::COMMA)) {
            anno.attrCommas.push_back(lastToken.Begin());
            continue;
        }
        break;
    }
    if (!Skip(TokenKind::RSQUARE)) {
        DiagExpectedRightDelimiter("[", pos);
        ConsumeUntil(TokenKind::NL);
    } else {
        anno.rsquarePos = lastToken.Begin();
    }
}

void ParserImpl::ParseOverflowAnnotation(Annotation& anno)
{
    if (Seeing(TokenKind::LSQUARE)) {
        auto pos = lookahead.Begin();
        anno.lsquarePos = pos;
        Next();
        if (Seeing(TokenKind::IDENTIFIER)) {
            anno.overflowStrategy = Utils::StringToOverflowStrategy(Utils::StrToLower(lookahead.Value()));
            Next();
        }
        if (!Skip(TokenKind::RSQUARE)) {
            DiagExpectedRightDelimiter("[", pos);
            ConsumeUntil(TokenKind::NL);
        } else {
            anno.rsquarePos = lastToken.Begin();
        }
    } else {
        anno.overflowStrategy =
            Utils::StringToOverflowStrategy(Utils::StrToLower(anno.identifier.Val().substr(OVERFLOW_STRATEGY.size())));
    }
}

void ParserImpl::ParseWhenAnnotation(Annotation& anno)
{
    if (!Seeing(TokenKind::LSQUARE)) {
        anno.EnableAttr(Attribute::IS_BROKEN);
        DiagExpectedLsquareAfter(anno, "@When", "when annotation must have condition");
        return;
    }
    auto pos = lookahead.Begin();
    anno.lsquarePos = pos;
    Next();
    anno.condExpr = ParseExpr();
    const static std::vector<ASTKind> astKinds = {
        ASTKind::UNARY_EXPR,
        ASTKind::BINARY_EXPR,
        ASTKind::REF_EXPR,
    };
    if (Utils::NotIn(anno.condExpr->astKind, astKinds)) {
        if (!anno.condExpr->TestAttr(Attribute::IS_BROKEN)) {
            DiagUnrecognizedExprInWhen(*anno.condExpr, anno);
        }
    }
    if (!Skip(TokenKind::RSQUARE)) {
        DiagExpectedRightDelimiter("[", pos);
        ConsumeUntil(TokenKind::NL);
    } else {
        anno.rsquarePos = lastToken.Begin();
    }
}

OwnedPtr<FuncArg> ParserImpl::ParseAnnotationArgument()
{
    OwnedPtr<FuncArg> ret = MakeOwned<FuncArg>();
    ret->begin = lookahead.Begin();

    if (SeeingNamedFuncArgs()) {
        ret->name = ExpectIdentifierWithPos(*ret);
        Next();
        ret->colonPos = lookahead.Begin();
    }
    auto tmpExpr = ParseExpr(ExprKind::EXPR_IN_ANNOTATION);
    ret->expr = std::move(tmpExpr);
    if (ret->expr) {
        ret->end = ret->expr->end;
    }
    return ret;
}

void ParserImpl::ParseAnnotationArguments(Annotation& anno)
{
    if (!Skip(TokenKind::LSQUARE)) {
        return;
    }
    auto pos = lastToken.Begin();
    anno.lsquarePos = pos;
    bool backArgsIsInvalid{false};
    for (;;) {
        if (DetectPrematureEnd()) {
            DiagExpectedRightDelimiter("[", pos);
            break;
        }
        if (Skip(TokenKind::RSQUARE)) {
            anno.rsquarePos = lastToken.Begin();
            break;
        }
        (void)anno.args.emplace_back(ParseAnnotationArgument());
        backArgsIsInvalid = anno.args.back()->expr->astKind == ASTKind::INVALID_EXPR;
        if (Skip(TokenKind::COMMA)) {
            if (Seeing(TokenKind::RSQUARE)) {
                DiagExpectCharacter("identifier");
            }
            anno.args.back()->commaPos = lastToken.Begin();
            continue;
        }
        if (!Seeing(TokenKind::RSQUARE)) {
            if (backArgsIsInvalid) {
                break;
            }
            DiagExpectedRightDelimiter("[", pos);
        }
    }
    anno.end = lastToken.End();
}

namespace Cangjie {
// Annotations that will only be parsed in the standard library.
static const std::set<std::string> STD_ONLY_ANNO = {
    // Disable const evaluation checker for items annotated with `@ConstSafe`.
    "ConstSafe"};

bool IsBuiltinAnnotation(const std::string& moduleName, const std::string& identifier)
{
    if (STD_ONLY_ANNO.find(identifier) != STD_ONLY_ANNO.end()) {
        return moduleName == "std";
    }
    return NAME_TO_ANNO_KIND.find(identifier) != NAME_TO_ANNO_KIND.end();
}
} // namespace Cangjie

bool ParserImpl::SeeingBuiltinAnnotation()
{
    if (!Seeing(TokenKind::AT)) {
        return false;
    }
    // Get annotation identifier.
    auto tokens = lexer->LookAheadSkipNL(1);
    if (tokens.begin()->kind != TokenKind::IDENTIFIER) {
        return false;
    }
    return IsBuiltinAnnotation(moduleName, tokens.begin()->Value());
}

bool ParserImpl::SeeingAtWhen()
{
    if (!SeeingCombinator({TokenKind::AT, TokenKind::IDENTIFIER})) {
        return false;
    }
    auto tokens = lexer->LookAheadSkipNL(1);
    tokens.begin()->SetValue("When");
    return true;
}

bool ParserImpl::SeeingMacroCall()
{
    if (!Seeing(TokenKind::AT)) {
        return false;
    }
    // Get annotation identifier.
    auto tokens = lexer->LookAheadSkipNL(1);
    if (tokens.begin()->kind != TokenKind::IDENTIFIER &&
        (tokens.begin()->kind < TokenKind::PUBLIC || tokens.begin()->kind > TokenKind::OPEN)) {
        return false;
    }
    return !IsBuiltinAnnotation(moduleName, tokens.begin()->Value());
}

bool ParserImpl::SeeingMacroCallDecl()
{
    if (!SeeingAny({TokenKind::AT, TokenKind::AT_EXCL})) {
        return false;
    }
    // Get annotation identifier.
    auto tokens = lexer->LookAheadSkipNL(1);
    if (tokens.begin()->kind != TokenKind::IDENTIFIER &&
        (tokens.begin()->kind < TokenKind::PUBLIC || tokens.begin()->kind > TokenKind::OPEN)) {
        return false;
    }
    return !IsBuiltinAnnotation(moduleName, tokens.begin()->Value());
}

void ParserImpl::ParseAnnotations(PtrVector<Annotation>& annos)
{
    while (SeeingBuiltinAnnotation() || (this->enableCustomAnno && SeeingMacroCallDecl())) {
        auto annotation = ParseAnnotation();
        auto anno = std::find_if(annos.begin(), annos.end(), [&annotation](const auto& anno) {
            return anno->kind != AnnotationKind::CUSTOM && anno->identifier == annotation->identifier;
        });
        if (anno != annos.end()) {
            DiagDuplicatedAnno(*annotation, **anno);
        }
        annos.emplace_back(std::move(annotation));
    }
}

void ParserImpl::SetBeginToAnnotationsBegin(Node& node, const PtrVector<Annotation>& annos)
{
    if (!annos.empty()) {
        node.begin = annos.front()->begin;
    }
}

OwnedPtr<Annotation> ParserImpl::ParseCustomAnnotation()
{
    bool isCompileTimeVisible{false};
    if (!Skip(TokenKind::AT)) {
        Skip(TokenKind::AT_EXCL);
        isCompileTimeVisible = true;
    }
    auto atPos = lastToken.Begin();
    (void)Peek();
    OwnedPtr<AST::Expr> expr = ParseRefExpr();
    while (Skip(TokenKind::DOT)) {
        auto ret = ParseMemberAccess(std::move(expr));
        expr = std::move(ret);
    }
    auto ident = expr->ToString();
    auto annotation = MakeOwned<Annotation>(ident, AnnotationKind::CUSTOM, atPos);
    annotation->identifier.SetPos(expr->begin, expr->begin + annotation->identifier.Val().size());
    annotation->baseExpr = std::move(expr);
    annotation->end = lastToken.End();
    annotation->isCompileTimeVisible = isCompileTimeVisible;
    ParseAnnotationArguments(*annotation);
    return annotation;
}

void ParserImpl::ValidateDeprecatedAnnotationArgument(
    const Ptr<LitConstExpr> lce, const std::string& name, const LitConstKind& expectedKind, bool& isArgumentFound)
{
    if (isArgumentFound) {
        DiagDeprecatedArgumentDuplicated(*lce, name);
    }
    isArgumentFound = true;

    if (lce->stringValue == "") {
        DiagDeprecatedEmptyStringArgument(*lce, name);
    } else if (lce->kind != expectedKind) {
        const auto kindName = (expectedKind == LitConstKind::STRING) ? "String" : "Bool";
        DiagDeprecatedWrongArgumentType(*lce, name, kindName);
    }
}

void ParserImpl::CheckDeprecatedAnnotation(const Annotation& anno)
{
    bool messageFound = false;
    bool sinceFound = false;
    bool strictFound = false;

    for (auto& arg : anno.args) {
        if (!arg->expr || arg->expr->astKind != ASTKind::LIT_CONST_EXPR) {
            DiagDeprecatedArgumentNotLitConst(*arg);
            return;
        }

        auto lce = DynamicCast<LitConstExpr*>(arg->expr.get());
        if (!lce) {
            DiagDeprecatedArgumentNotLitConst(*arg);
            return;
        }

        if (arg->name == "message" || arg->name == "") {
            ValidateDeprecatedAnnotationArgument(lce, "message", LitConstKind::STRING, messageFound);
        } else if (arg->name == "since") {
            ValidateDeprecatedAnnotationArgument(lce, "since", LitConstKind::STRING, sinceFound);
        } else if (arg->name == "strict") {
            ValidateDeprecatedAnnotationArgument(lce, "strict", LitConstKind::BOOL, strictFound);
        } else {
            DiagDeprecatedUnknownArgument(*arg, arg->name);
        }
    }
}

void ParserImpl::CheckObjCMirrorAnnotation(const Annotation& anno) const
{
    static const std::string OBJ_C_MIRROR_NAME = "@ObjCMirror";
    static const std::string OBJ_C_IMPL_NAME = "@ObjCImpl";

    const auto& annotationName =
        anno.kind == AnnotationKind::OBJ_C_MIRROR ? OBJ_C_MIRROR_NAME : OBJ_C_IMPL_NAME;
    ffiParser->CheckZeroOrSingleStringLitArgAnnotation(anno, annotationName);
}

OwnedPtr<Annotation> ParserImpl::ParseAnnotation()
{
    if (this->enableCustomAnno && SeeingMacroCallDecl()) {
        // Reparse as a custom annotation after macro expansion if one macrocall can't find it's macrodef.
        return ParseCustomAnnotation();
    }
    // parse builtin annotation
    Skip(TokenKind::AT);
    auto beg = lastToken.Begin();
    Skip(TokenKind::IDENTIFIER);
    CJC_ASSERT(NAME_TO_ANNO_KIND.find(lastToken.Value()) != NAME_TO_ANNO_KIND.end());
    auto kind = NAME_TO_ANNO_KIND.at(lastToken.Value());
    auto annotation = MakeOwned<Annotation>(lastToken.Value(), kind, beg);
    annotation->identifier.SetPos(lastToken.Begin(), lastToken.End());
    annotation->end = lastToken.End();
    auto tok = lastToken;
    ChainScope cs(*this, annotation.get());
    switch (annotation->kind) {
        case AnnotationKind::ATTRIBUTE: {
            ParseAttributeAnnotation(*annotation);
            break;
        }
        case AnnotationKind::NUMERIC_OVERFLOW: {
            ParseOverflowAnnotation(*annotation);
            break;
        }
        case AnnotationKind::WHEN: {
            ParseWhenAnnotation(*annotation);
            break;
        }
        case AnnotationKind::DEPRECATED: {
            ParseAnnotationArguments(*annotation);
            CheckDeprecatedAnnotation(*annotation);
            break;
        }
        case AnnotationKind::JAVA_MIRROR:
        case AnnotationKind::JAVA_IMPL: {
            ParseAnnotationArguments(*annotation);
            break;
        }
        case AnnotationKind::FOREIGN_NAME: {
            ParseAnnotationArguments(*annotation);
            break;
        }
        case AnnotationKind::OBJ_C_MIRROR:
        case AnnotationKind::OBJ_C_IMPL: {
            ParseAnnotationArguments(*annotation);
            CheckObjCMirrorAnnotation(*annotation);
            break;
        }
        default: {
            // parse annotation arguments.
            ParseAnnotationArguments(*annotation);
            break;
        }
    }
    annotation->end = lastToken.End();

    return annotation;
}
