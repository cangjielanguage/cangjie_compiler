// Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "ParserImpl.h"

#include <iterator>

#include "cangjie/AST/Clone.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"
#include "cangjie/Basic/Print.h"

using namespace Cangjie;
using namespace AST;

namespace {
const std::string OVERFLOW_STRATEGY = "overflow";

// '@HasThis[TypeName]' takes a type reference, which annotation parsing
// represents as a plain identifier or a dotted identifier path. A literal, a
// call, an index or a compound expression is not a type reference and is
// refused at parse time, before any semantic resolution.
bool IsHasThisTypeReference(const Expr* expr)
{
    if (expr == nullptr) {
        return false;
    }
    if (expr->astKind == ASTKind::REF_EXPR) {
        return true;
    }
    if (auto access = DynamicCast<const MemberAccess*>(expr); access != nullptr) {
        return IsHasThisTypeReference(access->baseExpr.get());
    }
    return false;
}
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
            // Position the error at the entire lastToken if encountering a newline,
            // otherwise at the end of lastToken for more precise error location.
            auto builder = lookahead.kind == TokenKind::NL
                ? ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_right_delimiter, lastToken, "[")
                : ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_right_delimiter, lastToken.End(), "[");
            builder.AddMainHintArguments("]");
            builder.AddHint(pos, "[");
            if (anno.kind == AnnotationKind::CUSTOM) {
                builder.AddNote(
                    "Argument syntax error. May be caused by missing macro definitions. "
                    "If not, verify the annotation arguments are valid."
                );
            }
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

std::string AnnotationKindToString(AnnotationKind kind)
{
    auto nameIter = ANNO_KIND_TO_NAME.find(kind);
    CJC_ASSERT_WITH_MSG(
        nameIter != ANNO_KIND_TO_NAME.end(), "Annotation kind doesn't have an entry in NAME_TO_ANNO_KIND");
    auto name = nameIter->second;
    return name;
}
} // namespace Cangjie

bool ParserImpl::SeeingBuiltinAnnotation()
{
    if (!Seeing(TokenKind::AT)) {
        return false;
    }
    if (SeeingModalInfo()) {
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
    if (SeeingModalInfo()) {
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
    if (SeeingModalInfo()) {
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

// Before: a block default @ArkTS[module:"a", namespace:"n"] and a member
// @ArkTS[module:"b"]. After: that member owns two independent Annotation nodes
// ordered [cloned block default, parsed member override]. ArkTSDeclOptions
// later folds them to {module:"b", namespace:"n"}; no shared block node changes.
// The default layer is withheld during member duplicate checks, so a block
// default plus one member annotation is valid, but two explicit member @ArkTS
// annotations still fail. Other annotation kinds retain the normal duplicate check.
void ParserImpl::ParseInheritedAnnotations(const PtrVector<Annotation>& inherited, PtrVector<Annotation>& annotations)
{
    // Inherited option annotations provide defaults, so parse member options
    // before prepending them. All other inherited annotations participate in
    // the normal duplicate check while member annotations are parsed.
    PtrVector<Annotation> defaults;
    for (const auto& annotation : inherited) {
        auto cloned = ASTCloner::Clone(annotation.get());
        if (ffiParser->AllowsInheritedAnnotationOptions(annotation->kind)) {
            defaults.emplace_back(std::move(cloned));
        } else {
            annotations.emplace_back(std::move(cloned));
        }
    }
    ParseAnnotations(annotations);
    annotations.insert(
        annotations.begin(), std::make_move_iterator(defaults.begin()), std::make_move_iterator(defaults.end()));
}

void ParserImpl::ParseAnnotations(PtrVector<Annotation>& annos)
{
    while ((SeeingBuiltinAnnotation() || (this->enableCustomAnno && SeeingMacroCallDecl())) && !SeeingModalInfo()) {
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

void ParserImpl::CheckRawValueAnnotation(const Annotation& anno)
{
    if (!experimentalMode) {
        return;
    }
    // @RawValue accepts exactly one unnamed argument: an integer, string or
    // boolean literal. The argument-count gate lives here so @RawValue[] or
    // @RawValue[1, 2] cannot fall through to the Sema auto-increment branch
    // (args.size() != 1) and be silently ignored.
    if (anno.args.size() != 1) {
        DiagAnnotationExpectsOneArgument(anno, "@RawValue");
        return;
    }
    auto& arg = anno.args[0];
    CJC_NULLPTR_CHECK(arg);
    if (!arg->name.Empty()) {
        ParseDiagnoseRefactor(DiagKindRefactor::parse_raw_value_not_literal, *arg);
        return;
    }
    auto lce = (arg->expr != nullptr && arg->expr->astKind == ASTKind::LIT_CONST_EXPR)
                   ? DynamicCast<LitConstExpr*>(arg->expr.get())
                   : nullptr;
    // A numeric, string or boolean literal is accepted. Numeric includes
    // floating values: the design's EnumValueLiteral is NumericLiteral and
    // ArkTS enum members may be `A = 1.5`. A boolean literal belongs to the
    // §7.2 union-type mapping; the Cangjie side cannot distinguish a union
    // carrier from an ArkTS enum, so it is accepted here and lowered as a
    // boolean member rather than refused.
    if (lce == nullptr ||
        (lce->kind != LitConstKind::INTEGER && lce->kind != LitConstKind::FLOAT &&
            lce->kind != LitConstKind::STRING && lce->kind != LitConstKind::BOOL)) {
        ParseDiagnoseRefactor(DiagKindRefactor::parse_raw_value_not_literal, *arg);
    }
}

void ParserImpl::CheckHasThisAnnotation(Annotation& anno)
{
    // '@HasThis' takes exactly one unnamed type reference, e.g.
    // '@HasThis[Person]'. A zero or multiple argument list, a named argument
    // and a literal or any other value expression are all refused here with
    // the dedicated diagnostic, so the annotation can never be silently
    // accepted with a value that has no type meaning.
    if (anno.args.size() != 1) {
        ParseDiagnoseRefactor(DiagKindRefactor::parse_has_this_invalid_argument, anno);
        return;
    }
    auto& arg = anno.args.front();
    CJC_NULLPTR_CHECK(arg);
    if (!arg->name.Empty() || !IsHasThisTypeReference(arg->expr.get())) {
        ParseDiagnoseRefactor(DiagKindRefactor::parse_has_this_invalid_argument, *arg);
    }
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
        case AnnotationKind::JAVA_IMPL:
        case AnnotationKind::JAVA: {
            ParseAnnotationArguments(*annotation);
            break;
        }
        case AnnotationKind::FOREIGN_GETTER_NAME:
        case AnnotationKind::FOREIGN_SETTER_NAME:
        case AnnotationKind::FOREIGN_NAME: {
            ParseAnnotationArguments(*annotation);
            break;
        }
        case AnnotationKind::OBJ_C_MIRROR:
        case AnnotationKind::OBJ_C_IMPL:
        case AnnotationKind::OBJ_C: {
            ParseAnnotationArguments(*annotation);
            break;
        }
        case AnnotationKind::ARKTS_RAW_VALUE: {
            ParseAnnotationArguments(*annotation);
            CheckRawValueAnnotation(*annotation);
            break;
        }
        case AnnotationKind::HAS_THIS: {
            ParseAnnotationArguments(*annotation);
            CheckHasThisAnnotation(*annotation);
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
