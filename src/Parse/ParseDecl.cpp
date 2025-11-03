// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 * This file implements pattern decl parse apis.
 */

#include "ParserImpl.h"

#include "cangjie/AST/Match.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Parse/ParseModifiersRules.h"

using namespace Cangjie;
using namespace AST;

namespace {
constexpr std::string_view ELLIPSIS("...");
} // namespace

OwnedPtr<Decl> ParserImpl::ParseDecl(ScopeKind scopeKind, std::set<Modifier> modifiers, PtrVector<Annotation> annos)
{
    if (SeeingBuiltinAnnotation() && !modifiers.empty() && annos.empty()) {
        DiagExpectNoModifierBefore(*SortModifierByPos(modifiers)[0], "annotation");
    }
    if ((newlineSkipped && Seeing(TokenKind::NL)) || Seeing(TokenKind::SEMI)) {
        SkipNLOrSemi();
    }
    ParseAnnotations(annos);
    ParseModifiers(modifiers);
    CheckOverflowAnno(annos, scopeKind);
    ffiParser->CheckAnnotations(annos, scopeKind);
    CheckAnnotationAnno(annos, modifiers);

    // Enum construtor.
    if (SeeingEnumConstructor(scopeKind)) {
        return ParseEnumConstructor(modifiers, annos);
    }
    // Macro expression.
    if (SeeingMacroCallDecl()) {
        return ParseMacroCall<MacroExpandDecl>(scopeKind, modifiers, std::move(annos));
    }

    if (auto tokenKind = Peek().kind; declHandlerMap.count(tokenKind) != 0) {
        auto ret = declHandlerMap[tokenKind](this, scopeKind, modifiers, std::move(annos));
        if (scopeKind == ScopeKind::TOPLEVEL) {
            ret->EnableAttr(Attribute::GLOBAL);
        }
        if (HasModifier(modifiers, TokenKind::CONST) && ret->astKind == ASTKind::FUNC_DECL) {
            StaticCast<FuncDecl&>(*ret).isConst = true;
        }
        SetDeclBeginPos(*ret);

        mpImpl->CheckCJMPDecl(*ret);
        return ret;
    }

    if (SeeingPropMember(scopeKind)) {
        return ParsePropMemberDecl(modifiers);
    }
    // Primary constructor decl.
    if (SeeingPrimaryConstructor(scopeKind)) {
        return ParsePrimaryConstructor(scopeKind, modifiers, std::move(annos));
    }
    // Const Variable.
    if (HasModifier(modifiers, TokenKind::CONST) && lastToken == "const") {
        auto ret = ParseConstVariable(scopeKind, modifiers, std::move(annos));
        SetDeclBeginPos(*ret);
        return ret;
    }
    // Finalizer
    if (SeeingFinalizer()) {
        return ParseFinalizer(scopeKind, modifiers, std::move(annos));
    }
    auto ret = MakeOwned<InvalidDecl>(lookahead.Begin());
    DiagExpectedDeclaration(scopeKind);

    ret->EnableAttr(Attribute::IS_BROKEN);
    ImplementConsumeStrategy(scopeKind);
    ret->end = lastToken.End();
    return ret;
}

OwnedPtr<Pattern> ParserImpl::ParseIrrefutablePattern(const std::set<Attribute>& attributes, bool isVar, bool inDecl)
{
    if (Skip(TokenKind::WILDCARD)) {
        Position begin = lookahead.Begin();
        OwnedPtr<WildcardPattern> ret = MakeOwned<WildcardPattern>();
        ret->begin = begin;
        ret->end = lookahead.End();
        return ret;
    }
    if (Seeing(TokenKind::LPAREN)) {
        return ParseTuplePattern(false, attributes, isVar, inDecl);
    }
    if (SeeingIdentifierAndTargetOp({TokenKind::LPAREN, TokenKind::DOT, TokenKind::LT})) {
        return ParseEnumPattern(attributes, isVar, inDecl);
    }
    if (Seeing(TokenKind::IDENTIFIER) || SeeingContextualKeyword()) {
        Position begin = lookahead.Begin();
        auto identifier = ParseIdentifierFromToken(lookahead);
        return ParseVarPattern(attributes, std::move(identifier), begin, isVar);
    }
    // It is internal bug if reach here.
    CJC_ABORT();
    return MakeOwned<InvalidPattern>(lookahead.Begin());
}

OwnedPtr<Decl> ParserImpl::ParseVarWithPatternDecl(
    ScopeKind scopeKind, const std::set<Modifier>& modifiers, const Token& keyToken)
{
    OwnedPtr<VarWithPatternDecl> ret = MakeOwned<VarWithPatternDecl>();
    ChainScope cs(*this, ret.get());
    bool isVar = keyToken == "var";
    ret->isVar = isVar;
    ret->isConst = keyToken == "const";
    ret->keywordPos = keyToken.Begin();
    ret->begin = keyToken.Begin();
    auto attrs = CheckDeclModifiers(modifiers, scopeKind, DefKind::VARIABLE);
    for (auto& it : attrs) {
        ret->EnableAttr(it);
    }
    ret->modifiers.insert(modifiers.begin(), modifiers.end());
    ret->irrefutablePattern = ParseIrrefutablePattern(attrs, isVar, true);
    ret->end = lookahead.End();
    if ((ret->irrefutablePattern->astKind == ASTKind::TUPLE_PATTERN ||
            ret->irrefutablePattern->astKind == ASTKind::ENUM_PATTERN ||
            ret->irrefutablePattern->astKind == ASTKind::WILDCARD_PATTERN) &&
        (scopeKind == ScopeKind::CLASS_BODY || scopeKind == ScopeKind::STRUCT_BODY)) {
        DiagParseIllegalDeclarationPattern(ret, scopeKind);
    }
    ret->end = ret->irrefutablePattern->end;
    ParseTypeAndExpr(ret.get());

    // Set `GLOBAL` attribute for toplevel declares in patterns.
    if (scopeKind == ScopeKind::TOPLEVEL) {
        auto setGlobalAttr = [](Ptr<Node> n) {
            CJC_NULLPTR_CHECK(n);
            n->EnableAttr(Attribute::GLOBAL);
            return VisitAction::WALK_CHILDREN;
        };
        Walker(ret->irrefutablePattern.get(), setGlobalAttr).Walk();
    }
    if (!ret->isConst && HasModifier(modifiers, TokenKind::CONST)) {
        // diag modifier
        DiagUnExpectedModifierOnDeclaration(*ret);
        return ret;
    }
    if (ret->initializer == nullptr) {
        if (ret->isConst) {
            DiagConstVariableExpectedInitializer(*ret);
        } else if (scopeKind != ScopeKind::TOPLEVEL && ret->type == nullptr) {
            DiagExpectedTypeOrInitializerInPattern(*ret);
        } else if (scopeKind == ScopeKind::TOPLEVEL && !diag.ignoreScopeCheck) {
            DiagExpectedInitializerForToplevelVar(*ret);
        }
    }
    return ret;
}

OwnedPtr<Decl> ParserImpl::ParseVarOrLetOrConst(ScopeKind scopeKind,
    const std::set<Modifier>& modifiers, std::vector<OwnedPtr<AST::Annotation>> annos, const Token& keyToken)
{
    OwnedPtr<Decl> ret;
    if (Seeing(TokenKind::WILDCARD) || Seeing(TokenKind::LPAREN) ||
        (SeeingIdentifierAndTargetOp({TokenKind::LPAREN, TokenKind::DOT, TokenKind::LT}))) {
        ret = ParseVarWithPatternDecl(scopeKind, modifiers, keyToken);
    } else if (Seeing(TokenKind::IDENTIFIER) || SeeingContextualKeyword()) {
        ret = ParseVarDecl(scopeKind, modifiers, keyToken);
    } else {
        DiagExpectedIdentifierOrPattern(
            keyToken == "var", keyToken.Begin(), keyToken == "const");
        ret = MakeOwned<InvalidDecl>(lookahead.Begin());
        ConsumeUntil(TokenKind::NL);
    }
    ret->annotations = std::move(annos);
    if (scopeKind == ScopeKind::TOPLEVEL) {
        ret->EnableAttr(Attribute::GLOBAL);
    }
    return ret;
}

OwnedPtr<Decl> ParserImpl::ParseVarOrLet(
    ScopeKind scopeKind, const std::set<Modifier>& modifiers, std::vector<OwnedPtr<AST::Annotation>> annos)
{
    Next();
    Token keyToken = lookahead; // Var or let.
    return ParseVarOrLetOrConst(scopeKind, modifiers, std::move(annos), keyToken);
}

OwnedPtr<Decl> ParserImpl::ParseConstVariable(
    ScopeKind scopeKind, const std::set<Modifier>& modifiers, std::vector<OwnedPtr<AST::Annotation>> annos)
{
    Token keyToken = lastToken; // const.
    return ParseVarOrLetOrConst(scopeKind, modifiers, std::move(annos), keyToken);
}

void ParserImpl::CheckVarDeclModifiers(
    std::set<Modifier> modifiers, Ptr<VarDecl> varDecl, ScopeKind scopeKind, const Token& keyToken)
{
    if (HasModifier(modifiers, TokenKind::UNSAFE) || HasModifier(modifiers, TokenKind::FOREIGN)) {
        SetUnsafe(varDecl, modifiers);
    }
    if (!varDecl->isConst && HasModifier(modifiers, TokenKind::CONST)) {
        // Unexpected modifier: var or let variable has const.
        DiagUnExpectedModifierOnDeclaration(*varDecl);
    } else if (varDecl->isConst && (scopeKind == ScopeKind::CLASS_BODY || scopeKind == ScopeKind::STRUCT_BODY) &&
        !HasModifier(modifiers, TokenKind::STATIC)) {
        DiagConstVariableExpectedStatic(keyToken);
    } else if (varDecl->initializer == nullptr && !varDecl->TestAttr(Attribute::HAS_BROKEN)) {
        if (varDecl->isConst) {
            DiagConstVariableExpectedInitializer(*varDecl);
        } else if (scopeKind != ScopeKind::TOPLEVEL && varDecl->type == nullptr &&
            !HasModifier(modifiers, TokenKind::COMMON)) {
            DiagExpectedOneOfTypeOrInitializer(*varDecl, varDecl->identifier);
        } else if (scopeKind == ScopeKind::TOPLEVEL && !HasModifier(modifiers, TokenKind::FOREIGN) &&
            (varDecl->type == nullptr || !diag.ignoreScopeCheck) && !HasModifier(modifiers, TokenKind::COMMON)) {
            DiagExpectedInitializerForToplevelVar(*varDecl);
        }
    }
}

OwnedPtr<Decl> ParserImpl::ParseVarDecl(
    const ScopeKind& scopeKind, const std::set<Modifier>& modifiers, const Token& keyToken)
{
    OwnedPtr<VarDecl> ret = MakeOwned<VarDecl>();
    ChainScope cs(*this, ret.get());
    ret->begin = keyToken.Begin();
    ret->isVar = keyToken == "var";
    ret->isConst = keyToken == "const";
    ret->keywordPos = keyToken.Begin();
    CheckDeclarationInScope(scopeKind, DefKind::VARIABLE);
    ret->identifier = ExpectIdentifierWithPos(*ret);
    ret->end = lookahead.End();
    ParseTypeAndExpr(ret.get());
    auto attrs = CheckDeclModifiers(modifiers, scopeKind, DefKind::VARIABLE);
    for (auto& it : attrs) {
        ret->EnableAttr(it);
    }
    ret->modifiers.insert(modifiers.begin(), modifiers.end());
    CheckVarDeclModifiers(modifiers, ret.get(), scopeKind, keyToken);
    bool hasNoType = ret->type == nullptr;
    bool isCommonPlatform = ret->IsCommonOrPlatform();
    if (hasNoType && isCommonPlatform) {
        auto kind = ret->TestAttr(Attribute::COMMON) ? "common" : "platform";
        auto keyword = ret->isVar ? "var" : "let";
        ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_type_with_cjmp_var, *ret, kind, keyword);
    }
    return ret;
}

void ParserImpl::ParseTypeAndExpr(Ptr<VarDeclAbstract> const ret)
{
    if (Skip(TokenKind::COLON)) {
        ret->colonPos = lookahead.Begin();
        ret->type = ParseType();
        ret->end = ret->type->end;
        if (ret->type->TestAttr(Attribute::IS_BROKEN) || ret->type->TestAttr(Attribute::HAS_BROKEN)) {
            ret->EnableAttr(Attribute::HAS_BROKEN);
        }
    }
    if (Skip(TokenKind::ASSIGN)) {
        ret->assignPos = lookahead.Begin();
        ret->initializer = ParseExpr(ExprKind::VAR_INIT);
        if (ret->initializer) {
            ret->end = ret->initializer->end;
        }
    }
}

void ParserImpl::CheckGetterAnnotations(std::vector<OwnedPtr<Annotation>>& annos, const Ptr<Node> getter)
{
    for (auto& anno : std::as_const(annos)) {
        DiagUnexpectedAnnoOn(*anno, getter->begin, anno->identifier, "get");
    }
}

void ParserImpl::CheckSetterAnnotations(std::vector<OwnedPtr<Annotation>>& annos, const Ptr<Node> setter)
{
    for (auto& anno : std::as_const(annos)) {
        if (anno->kind != AnnotationKind::DEPRECATED) {
            DiagUnexpectedAnnoOn(*anno, setter->begin, anno->identifier, "set");
        }
    }
}

void ParserImpl::ParsePropBody(const std::set<Modifier>& modifiers, PropDecl& propDecl)
{
    propDecl.leftCurlPos = lastToken.Begin();
    std::vector<OwnedPtr<FuncDecl>> getPropMemberDecls;
    std::vector<OwnedPtr<FuncDecl>> setPropMemberDecls;
    Ptr<Node> setter{nullptr};
    Ptr<Node> getter{nullptr};
    while (true) {
        if (Skip(TokenKind::RCURL)) {
            propDecl.rightCurlPos = lastToken.Begin();
            break;
        }
        if (DetectPrematureEnd()) {
            DiagExpectedRightDelimiter("{", propDecl.leftCurlPos);
            break;
        }
        std::vector<OwnedPtr<Annotation>> annos;
        ParseAnnotations(annos);
        std::set<Modifier> modis;
        ParseModifiers(modis);
        if (SeeingPropMember()) {
            if (lookahead == "get" && getter) {
                DiagDuplicatedGetOrSet(*getter, propDecl);
            }
            if (lookahead == "set" && setter) {
                DiagDuplicatedGetOrSet(*setter, propDecl);
            }
            if (lookahead == "get") {
                auto res = ParsePropMemberDecl(modis);
                CheckGetterAnnotations(annos, res);
                res->annotations = std::move(annos);
                res->outerDecl = &propDecl;
                getPropMemberDecls.emplace_back(std::move(res));
                getter = getPropMemberDecls.back().get();
                SkipSemi();
            } else {
                auto res = ParsePropMemberDecl(modis);
                CheckSetterAnnotations(annos, res);
                res->annotations = std::move(annos);
                res->outerDecl = &propDecl;
                setPropMemberDecls.emplace_back(std::move(res));
                setter = setPropMemberDecls.back().get();
                SkipSemi();
            }
        } else {
            DiagExpectedGetOrSetInProp(propDecl.begin);
            ConsumeUntilAny({TokenKind::NL, TokenKind::SEMI});
        }
    }
    propDecl.getters = std::move(getPropMemberDecls);
    propDecl.setters = std::move(setPropMemberDecls);
    propDecl.rightCurlPos = lastToken.Begin();
    propDecl.modifiers.insert(modifiers.begin(), modifiers.end());
    propDecl.end = lookahead.End();
}

OwnedPtr<FuncDecl> ParserImpl::ParsePropMemberDecl(const std::set<AST::Modifier>& modifiers)
{
    OwnedPtr<FuncDecl> ret = MakeOwned<FuncDecl>();
    ChainScope cs(*this, ret.get());
    ret->identifier.SetPos(lookahead.Begin(), lookahead.End());
    ret->keywordPos = lookahead.Begin();
    ret->begin = lookahead.Begin();
    ret->identifier = lookahead.Value();
    bool isGetter = lookahead == "get";
    isGetter ? (ret->isGetter = true) : (ret->isSetter = true);
    Next();
    ret->begin = lookahead.Begin();
    if (!modifiers.empty()) {
        DiagExpectNoModifier(*modifiers.begin());
        ret->begin = SortModifierByPos(modifiers)[0]->begin;
    }
    if (!isGetter) {
        ret->funcBody = ParseFuncBody(ScopeKind::PROP_MEMBER_SETTER_BODY);
    } else {
        ret->funcBody = ParseFuncBody(ScopeKind::PROP_MEMBER_GETTER_BODY);
    }
    if (!ret->funcBody->body) {
        DiagMissingBody(!ret->identifier.Valid() ? "" : "function", " '" + ret->identifier + "'", lastToken.End());
    }

    ret->end = ret->funcBody->end;

    mpImpl->CheckCJMPDecl(*ret);
    return ret;
}

void ParserImpl::CheckAnnotationAnno(PtrVector<Annotation>& annos, const OwnedPtr<Decl>& decl)
{
    for (auto& anno : annos) {
        if (anno->kind != AnnotationKind::ANNOTATION) {
            continue;
        }
        if (decl->astKind == ASTKind::MACRO_EXPAND_DECL) {
            auto pInvocation = decl->GetInvocation();
            if (!pInvocation || !pInvocation->decl) {
                return;
            }
            CheckAnnotationAnno(annos, pInvocation->decl);
            return;
        }
        // @Annotation can only modify a class, and not a class modified by abstract or open or sealed.
        if (decl->astKind != ASTKind::CLASS_DECL && decl->astKind != ASTKind::INVALID_DECL) {
            DiagUnexpectedAnnoOnKind(*anno, decl->keywordPos, anno->identifier, decl->astKind);
            return;
        }
        for (auto mod : decl->modifiers) {
            if (Utils::In(mod.modifier, {TokenKind::ABSTRACT, TokenKind::OPEN, TokenKind::SEALED})) {
                DiagUnexpectedAnnoOn(*anno, mod.begin, anno->identifier, mod.ToString());
                return;
            }
        }
    }
}

void ParserImpl::CheckAnnotationAnno(PtrVector<Annotation>& annos, std::set<Modifier> modifiers)
{
    for (auto& anno : annos) {
        if (anno->kind != AnnotationKind::ANNOTATION) {
            continue;
        }
        // @Annotation can only modify a class, and not a class modified by abstract or open or sealed.
        if (!Seeing(TokenKind::CLASS) && !Seeing(TokenKind::AT)) {
            DiagUnexpectedAnnoOn(*anno, lookahead.Begin(), anno->identifier, lookahead.Value());
            return;
        }
        for (auto mod : modifiers) {
            if (Utils::In(mod.modifier, {TokenKind::ABSTRACT, TokenKind::OPEN, TokenKind::SEALED})) {
                DiagUnexpectedAnnoOn(*anno, mod.begin, anno->identifier, mod.ToString());
                return;
            }
        }
    }
}

void ParserImpl::CheckOverflowAnno(PtrVector<Annotation>& annos, ScopeKind scopeKind)
{
    if (SeeingAny({TokenKind::FUNC, TokenKind::MAIN, TokenKind::INIT, TokenKind::AT_EXCL}) || SeeingMacroCall() ||
        annos.empty()) {
        return;
    }
    // Primary Constructor
    if (SeeingPrimaryConstructor(scopeKind)) {
        return;
    }
    for (auto& it : annos) {
        if (it->kind == AnnotationKind::NUMERIC_OVERFLOW) {
            DiagUnexpectedAnnoOn(*it, lookahead.Begin(), it->identifier, lookahead.Value());
            break;
        }
    }
}

void ParserImpl::CheckPropDeclJavaMirror(PropDecl& decl)
{
    if (decl.outerDecl && decl.outerDecl->TestAttr(Attribute::JAVA_MIRROR)) {
        decl.EnableAttr(Attribute::JAVA_MIRROR);
        ParseDiagnoseRefactor(DiagKindRefactor::parse_java_mirror_prop_is_deprecated, decl);
        if (decl.outerDecl && !decl.outerDecl->TestAttr(Attribute::ABSTRACT)) {
            decl.DisableAttr(Attribute::ABSTRACT);
        }

        if (decl.TestAttr(Attribute::PRIVATE)) {
            ParseDiagnoseRefactor(DiagKindRefactor::parse_java_mirror_cannot_have_private_member, decl);
            decl.EnableAttr(Attribute::IS_BROKEN);
            decl.outerDecl->EnableAttr(Attribute::HAS_BROKEN, Attribute::IS_BROKEN);
        }

        if (decl.TestAttr(Attribute::OPEN)) {
            ParseDiagnoseRefactor(DiagKindRefactor::parse_java_mirror_cannot_have_open_prop, decl);
            decl.EnableAttr(Attribute::IS_BROKEN);
            decl.outerDecl->EnableAttr(Attribute::HAS_BROKEN, Attribute::IS_BROKEN);
        }

        if (!decl.getters.empty()) {
            ParseDiagnoseRefactor(DiagKindRefactor::parse_java_mirror_prop_cannot_have_getter, decl);
            decl.EnableAttr(Attribute::IS_BROKEN);
        } else {
            InsertPropGetterSignature(decl, Attribute::JAVA_MIRROR);
        }

        if (!decl.setters.empty()) {
            ParseDiagnoseRefactor(DiagKindRefactor::parse_java_mirror_prop_cannot_have_setter, decl);
            decl.EnableAttr(Attribute::IS_BROKEN);
        } else if (decl.isVar) {
            InsertPropSetterSignature(decl, Attribute::JAVA_MIRROR);
        }
    }
}

void ParserImpl::CheckPrimaryCtorDeclJavaMirror(PrimaryCtorDecl& ctor)
{
    if (ctor.outerDecl && ctor.outerDecl->TestAttr(Attribute::JAVA_MIRROR)) {
        ctor.EnableAttr(Attribute::JAVA_MIRROR);
        ctor.EnableAttr(Attribute::IS_BROKEN);
        ParseDiagnoseRefactor(DiagKindRefactor::parse_java_mirror_decl_cannot_have_primary_ctor, ctor);
    }
}

void ParserImpl::CheckInitCtorDeclBody(FuncDecl& ctor)
{
    auto& fb = ctor.funcBody;
    if ((!fb || !fb->body) && !ctor.TestAnyAttr(Attribute::COMMON, Attribute::JAVA_MIRROR, Attribute::OBJ_C_MIRROR)) {
        DiagMissingBody("constructor", "", ctor.end);
        if (!parseDeclFile) {
            ctor.EnableAttr(Attribute::HAS_BROKEN);
        }
    }
}

void ParserImpl::CheckJavaInteropMember(Decl& decl)
{
    if (decl.outerDecl->TestAttr(Attribute::JAVA_MIRROR_SUBTYPE) && !decl.outerDecl->TestAttr(Attribute::JAVA_MIRROR)) {
        if (decl.GetGeneric() != nullptr) {
            ffiParser->Java().DiagJavaImplCannotBeGeneric(decl);
            return;
        }
        if (decl.astKind == ASTKind::FUNC_DECL && decl.TestAttr(Attribute::CONSTRUCTOR, Attribute::STATIC)) {
            ffiParser->Java().DiagJavaImplCannotHaveStaticInit(decl);
            return;
        }
    }
    auto pdecl = Ptr(&decl);
    switch (decl.astKind) {
        case ASTKind::FUNC_DECL: {
            auto& fd = *StaticAs<ASTKind::FUNC_DECL>(pdecl);
            if (fd.TestAttr(Attribute::CONSTRUCTOR)) {
                CheckInitCtorDeclJavaMirror(fd);
            } else if (fd.TestAttr(Attribute::FINALIZER) && fd.outerDecl->TestAttr(Attribute::JAVA_MIRROR)) {
                ffiParser->Java().DiagJavaMirrorCannotHaveFinalizer(fd);
            } else {
                // method branch
                CheckMemberFuncJavaMirror(fd);
                ffiParser->CheckForeignNameAnnotation(fd);
            }
            break;
        }
        case ASTKind::PROP_DECL: {
            auto& propDecl = *StaticAs<ASTKind::PROP_DECL>(pdecl);
            CheckPropDeclJavaMirror(propDecl);
            ffiParser->CheckForeignNameAnnotation(propDecl);
            break;
        }
        case ASTKind::PRIMARY_CTOR_DECL: {
            auto& ctorDecl = *StaticAs<ASTKind::PRIMARY_CTOR_DECL>(pdecl);
            CheckPrimaryCtorDeclJavaMirror(ctorDecl);
            break;
        }

        default: break;
    }
}

void ParserImpl::CheckObjCInteropMember(Decl& member)
{
    if (member.outerDecl->TestAttr(Attribute::OBJ_C_MIRROR_SUBTYPE)) {
        if (member.GetGeneric() != nullptr) {
            member.outerDecl->EnableAttr(Attribute::IS_BROKEN);
            member.EnableAttr(Attribute::IS_BROKEN);
            ffiParser->ObjC().DiagObjCImplCannotBeGeneric(member);
            return;
        }
        if (member.astKind == ASTKind::FUNC_DECL && member.TestAttr(Attribute::CONSTRUCTOR, Attribute::STATIC)) {      
            member.outerDecl->EnableAttr(Attribute::IS_BROKEN);
            member.EnableAttr(Attribute::IS_BROKEN);
            ffiParser->ObjC().DiagObjCImplCannotHaveStaticInit(member);
            return;
        }
    }
    auto pMember = Ptr(&member);
    switch (member.astKind) {
        case ASTKind::FUNC_DECL: {
            auto& fd = *StaticAs<ASTKind::FUNC_DECL>(pMember);
            if (fd.TestAttr(Attribute::CONSTRUCTOR)) {
                CheckInitCtorDeclObjCMirror(fd);
                ffiParser->CheckForeignNameAnnotation(fd);
            } else if (fd.TestAttr(Attribute::FINALIZER) && fd.outerDecl->TestAttr(Attribute::OBJ_C_MIRROR)) {
                fd.outerDecl->EnableAttr(Attribute::IS_BROKEN);
                fd.EnableAttr(Attribute::IS_BROKEN);
                ffiParser->ObjC().DiagObjCMirrorCannotHaveFinalizer(fd);
            }  else {
                // method branch
                CheckMemberFuncObjCMirror(fd);
                ffiParser->CheckForeignNameAnnotation(fd);
                ffiParser->ObjC().CheckInitAnnotation(fd);
            }
            break;
        }
        case ASTKind::VAR_DECL: {
            auto& varDecl = *StaticAs<ASTKind::VAR_DECL>(pMember);
            CheckVarDeclObjCMirror(varDecl);
            break;
        }
        case ASTKind::PROP_DECL: {
            auto& propDecl = *StaticAs<ASTKind::PROP_DECL>(pMember);
            CheckPropDeclObjCMirror(propDecl);
            ffiParser->CheckForeignNameAnnotation(propDecl);
            break;
        }
        case ASTKind::PRIMARY_CTOR_DECL: {
            auto& ctorDecl = *StaticAs<ASTKind::PRIMARY_CTOR_DECL>(pMember);
            CheckPrimaryCtorDeclObjCMirror(ctorDecl);
            break;
        }

        default: break;
    }
}

void ParserImpl::CheckMemberFuncJavaMirror(FuncDecl& decl)
{
    if (!decl.outerDecl || !decl.outerDecl->TestAttr(Attribute::JAVA_MIRROR)) {
        return;
    }

    decl.EnableAttr(Attribute::JAVA_MIRROR);

    if (decl.TestAttr(Attribute::PRIVATE)) {
        ffiParser->Java().DiagJavaMirrorCannotHavePrivateMember(decl);
        decl.EnableAttr(Attribute::IS_BROKEN);
        decl.outerDecl->EnableAttr(Attribute::HAS_BROKEN, Attribute::IS_BROKEN);
    }

    if (HasModifier(decl.modifiers, TokenKind::CONST)) {
        ffiParser->Java().DiagJavaMirrorCannotHaveConstMember(decl);
        decl.EnableAttr(Attribute::IS_BROKEN);
        decl.outerDecl->EnableAttr(Attribute::HAS_BROKEN, Attribute::IS_BROKEN);
    }

    FFICheckClassLikeFuncBody(decl,
        DiagKindRefactor::parse_java_mirror_function_must_have_return_type,
        DiagKindRefactor::parse_java_mirror_function_cannot_have_body);

    if (decl.outerDecl->astKind == ASTKind::INTERFACE_DECL &&
        HasAnnotation(decl.annotations, AnnotationKind::JAVA_HAS_DEFAULT)) {
        decl.EnableAttr(Attribute::JAVA_HAS_DEFAULT);
    }
}

void ParserImpl::CheckMemberFuncObjCMirror(FuncDecl& func)
{
    if (!func.outerDecl || !func.outerDecl->TestAttr(Attribute::OBJ_C_MIRROR)) {
        return;
    }

    func.EnableAttr(Attribute::OBJ_C_MIRROR);

    if (func.TestAttr(Attribute::PRIVATE)) {
        ffiParser->ObjC().DiagObjCMirrorCannotHavePrivateMember(func);
        func.EnableAttr(Attribute::IS_BROKEN);
        func.outerDecl->EnableAttr(Attribute::HAS_BROKEN, Attribute::IS_BROKEN);
    }

    if (HasModifier(func.modifiers, TokenKind::CONST)) {
        ffiParser->ObjC().DiagObjCMirrorCannotHaveConstMember(func);
        func.EnableAttr(Attribute::IS_BROKEN);
        func.outerDecl->EnableAttr(Attribute::HAS_BROKEN, Attribute::IS_BROKEN);
    }

    FFICheckClassLikeFuncBody(func,
        DiagKindRefactor::parse_objc_mirror_method_must_have_return_type,
        DiagKindRefactor::parse_objc_mirror_method_cannot_have_body);
}

void ParserImpl::CheckInitCtorDeclJavaMirror(FuncDecl& ctor)
{
    if (!ctor.TestAttr(Attribute::JAVA_MIRROR)) {
        return;
    }
    ctor.EnableAttr(Attribute::JAVA_MIRROR);
    ctor.DisableAttr(Attribute::ABSTRACT);
    ctor.constructorCall = ConstructorCall::OTHER_INIT;

    if (ctor.TestAttr(Attribute::STATIC)) {
        ffiParser->Java().DiagJavaMirrorCannotHaveStaticInit(ctor);
        ctor.EnableAttr(Attribute::IS_BROKEN);
        ctor.outerDecl->EnableAttr(Attribute::HAS_BROKEN, Attribute::IS_BROKEN);
    }

    if (HasModifier(ctor.modifiers, TokenKind::CONST)) {
        ffiParser->Java().DiagJavaMirrorCannotHaveConstMember(ctor);
        ctor.EnableAttr(Attribute::IS_BROKEN);
        ctor.outerDecl->EnableAttr(Attribute::HAS_BROKEN, Attribute::IS_BROKEN);
    }

    if (ctor.TestAttr(Attribute::PRIVATE)) {
        ffiParser->Java().DiagJavaMirrorCannotHavePrivateMember(ctor);
        ctor.EnableAttr(Attribute::IS_BROKEN);
        ctor.outerDecl->EnableAttr(Attribute::HAS_BROKEN, Attribute::IS_BROKEN);
    }

    if (ctor.funcBody && ctor.funcBody->body) {
        ctor.EnableAttr(Attribute::IS_BROKEN);
        ParseDiagnoseRefactor(
            DiagKindRefactor::parse_java_mirror_constructor_cannot_have_body, ctor.funcBody->body->begin);
    } else if (ctor.funcBody) {
        auto body = MakeOwned<Block>();
        body->EnableAttr(Attribute::COMPILER_ADD, Attribute::IMPLICIT_ADD);
        body->begin = ctor.end;
        body->curFile = ctor.curFile;
        body->end = ctor.end;
        body->ty = ctor.ty;
        ctor.funcBody->body = std::move(body);
    }
}

void ParserImpl::CheckInitCtorDeclObjCMirror(FuncDecl& ctor)
{
    if (!ctor.outerDecl || !ctor.outerDecl->TestAttr(Attribute::OBJ_C_MIRROR)) {
        return;
    }
    ctor.constructorCall = ConstructorCall::OTHER_INIT;
    ctor.EnableAttr(Attribute::OBJ_C_MIRROR);

    if (ctor.TestAttr(Attribute::STATIC)) {
        ffiParser->ObjC().DiagObjCMirrorCannotHaveStaticInit(ctor);
        ctor.EnableAttr(Attribute::IS_BROKEN);
        ctor.outerDecl->EnableAttr(Attribute::HAS_BROKEN, Attribute::IS_BROKEN);
    }

    if (HasModifier(ctor.modifiers, TokenKind::CONST)) {
        ffiParser->ObjC().DiagObjCMirrorCannotHaveConstMember(ctor);
        ctor.EnableAttr(Attribute::IS_BROKEN);
        ctor.outerDecl->EnableAttr(Attribute::HAS_BROKEN, Attribute::IS_BROKEN);
    }

    if (ctor.TestAttr(Attribute::PRIVATE)) {
        ffiParser->ObjC().DiagObjCMirrorCannotHavePrivateMember(ctor);
        ctor.EnableAttr(Attribute::IS_BROKEN);
        ctor.outerDecl->EnableAttr(Attribute::HAS_BROKEN, Attribute::IS_BROKEN);
    }

    if (ctor.funcBody && ctor.funcBody->body) {
        ctor.EnableAttr(Attribute::IS_BROKEN);
        ParseDiagnoseRefactor(
            DiagKindRefactor::parse_objc_mirror_ctor_cannot_have_body, ctor.funcBody->body->begin);
    } else if (ctor.funcBody) {
        auto body = MakeOwned<Block>();
        body->EnableAttr(Attribute::COMPILER_ADD, Attribute::IMPLICIT_ADD);
        body->begin = ctor.end;
        body->curFile = ctor.curFile;
        body->end = ctor.end;
        body->ty = ctor.ty;
        ctor.funcBody->body = std::move(body);
    }
}

void ParserImpl::CheckVarDeclObjCMirror(VarDecl& field) const
{
    if (!field.outerDecl || !field.outerDecl->TestAnyAttr(Attribute::OBJ_C_MIRROR)) {
        return;
    }

    field.EnableAttr(Attribute::OBJ_C_MIRROR);

    if (field.TestAttr(Attribute::PRIVATE)) {
        ffiParser->ObjC().DiagObjCMirrorCannotHavePrivateMember(field);
        field.EnableAttr(Attribute::IS_BROKEN);
        field.outerDecl->EnableAttr(Attribute::HAS_BROKEN, Attribute::IS_BROKEN);
    }

    if (field.TestAttr(Attribute::STATIC)) {
        ffiParser->ObjC().DiagObjCMirrorFieldCannotBeStatic(field);
        field.EnableAttr(Attribute::IS_BROKEN);
        field.outerDecl->EnableAttr(Attribute::HAS_BROKEN, Attribute::IS_BROKEN);
    }

    if (field.initializer) {
        ffiParser->ObjC().DiagObjCMirrorFieldCannotHaveInitializer(field);
        field.EnableAttr(Attribute::IS_BROKEN);
        field.outerDecl->EnableAttr(Attribute::HAS_BROKEN, Attribute::IS_BROKEN);
    }
}

void ParserImpl::CheckPropDeclObjCMirror(PropDecl& prop)
{
    if (!prop.outerDecl || !prop.outerDecl->TestAttr(Attribute::OBJ_C_MIRROR)) {
        return;
    }
    prop.EnableAttr(Attribute::OBJ_C_MIRROR);

    if (prop.outerDecl && !prop.outerDecl->TestAttr(Attribute::ABSTRACT)) {
        prop.DisableAttr(Attribute::ABSTRACT);
    }

    if (prop.TestAttr(Attribute::PRIVATE)) {
        ffiParser->ObjC().DiagObjCMirrorCannotHavePrivateMember(prop);
        prop.EnableAttr(Attribute::IS_BROKEN);
        prop.outerDecl->EnableAttr(Attribute::HAS_BROKEN, Attribute::IS_BROKEN);
    }

    if (!prop.getters.empty()) {
        ParseDiagnoseRefactor(DiagKindRefactor::parse_objc_mirror_prop_cannot_have_getter, prop, prop.identifier);
        prop.EnableAttr(Attribute::IS_BROKEN);
    } else {
        InsertPropGetterSignature(prop, Attribute::OBJ_C_MIRROR);
    }

    if (!prop.setters.empty()) {
        ParseDiagnoseRefactor(DiagKindRefactor::parse_objc_mirror_prop_cannot_have_setter, prop, prop.identifier);
        prop.EnableAttr(Attribute::IS_BROKEN);
    } else if (prop.isVar) {
        InsertPropSetterSignature(prop, Attribute::OBJ_C_MIRROR);
    }
}

void ParserImpl::CheckPrimaryCtorDeclObjCMirror(PrimaryCtorDecl& ctor)
{
    if (!ctor.outerDecl || !ctor.outerDecl->TestAttr(Attribute::OBJ_C_MIRROR)) {
        return;
    }

    ctor.EnableAttr(Attribute::OBJ_C_MIRROR);
    ctor.EnableAttr(Attribute::IS_BROKEN);
    ParseDiagnoseRefactor(DiagKindRefactor::parse_objc_mirror_cannot_have_primary_ctor, ctor);
}

void ParserImpl::CheckCJMappingAttr(Decl& decl) const
{
    if (!enableInteropCJMapping) {
        return;
    }
    if (decl.TestAnyAttr(Attribute::JAVA_MIRROR, Attribute::JAVA_MIRROR_SUBTYPE)) {
        return;
    }
    // currently only support struct decl, enum decl, class decl interface decl, extend decl.
    bool isCJMappingClass = decl.astKind == ASTKind::CLASS_DECL && !decl.TestAttr(Attribute::ABSTRACT) &&
        !decl.TestAttr(Attribute::JAVA_MIRROR) && !decl.TestAttr(Attribute::JAVA_MIRROR_SUBTYPE);
    // support java type
    if (decl.astKind == ASTKind::STRUCT_DECL || decl.astKind == ASTKind::ENUM_DECL || isCJMappingClass ||
        decl.astKind == ASTKind::INTERFACE_DECL) {
        if (decl.TestAttr(Attribute::PUBLIC) && targetInteropLanguage == GlobalOptions::InteropLanguage::Java) {
            decl.EnableAttr(Attribute::JAVA_CJ_MAPPING);
        }
    }
    if (decl.astKind == ASTKind::EXTEND_DECL) {
        if (targetInteropLanguage == GlobalOptions::InteropLanguage::Java) {
            decl.EnableAttr(Attribute::JAVA_CJ_MAPPING);
        }
    }
    // support objc type
    if (decl.astKind == ASTKind::STRUCT_DECL) {
        if (decl.TestAttr(Attribute::PUBLIC) && targetInteropLanguage == GlobalOptions::InteropLanguage::ObjC) {
            decl.EnableAttr(Attribute::OBJ_C_CJ_MAPPING);
        } 
    }
}

template <typename T>
void ParserImpl::ParseFuncDeclAnnos(std::vector<OwnedPtr<AST::Annotation>>& annos, T& funcDecl)
{
    funcDecl.annotations = std::move(annos);
    for (auto& it : funcDecl.annotations) {
        if (it->kind == AnnotationKind::JAVA) {
            // We enable attribute JAVA_APP by default here to facilitate subsequent processing.
            // It will be checked and reset to the final correct attribute in the Sema phase.
            funcDecl.EnableAttr(Attribute::JAVA_APP);
        }
        if (it->kind == AnnotationKind::INTRINSIC) {
            CheckIntrinsicFunc(funcDecl);
            funcDecl.EnableAttr(Attribute::INTRINSIC);
        }
        if (it->kind == AnnotationKind::NUMERIC_OVERFLOW) {
            funcDecl.EnableAttr(Attribute::NUMERIC_OVERFLOW);
            funcDecl.overflowStrategy = it->overflowStrategy;
        }
        if (it->kind == AnnotationKind::FASTNATIVE) {
            funcDecl.isFastNative = true;
        }
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
        if (auto fd = dynamic_cast<FuncDecl*>(&funcDecl); fd && it->kind == AnnotationKind::FROZEN) {
            fd->isFrozen = true;
        }
#endif
    }
}

Token ParserImpl::SkipAndReturnOverloadingOperator()
{
    constexpr size_t pairLen{2};
    if (SeeingCombinator({TokenKind::GT, TokenKind::GT})) {
        SkipCombinator({TokenKind::GT, TokenKind::GT});
        lastToken = Token(TokenKind::RSHIFT, ">>", lastToken.Begin(), lastToken.Begin() + pairLen);
        return lastToken;
    }
    if (SeeingCombinator({TokenKind::GT, TokenKind::ASSIGN})) {
        SkipCombinator({TokenKind::GT, TokenKind::ASSIGN});
        lastToken = Token(TokenKind::GE, ">=", lastToken.Begin(), lastToken.Begin() + pairLen);
        return lastToken;
    }
    if (Seeing({TokenKind::LSQUARE, TokenKind::RSQUARE}, false)) {
        (void)Skip(TokenKind::LSQUARE);
        auto lparenPos{lastToken.Begin()};
        (void)Skip(TokenKind::RSQUARE);
        lastToken = Token(TokenKind::LSQUARE, "[]", lparenPos, lastToken.End());
        return lastToken;
    }
    if (Seeing({TokenKind::LPAREN, TokenKind::RPAREN}, false)) {
        (void)Skip(TokenKind::LPAREN);
        auto lparenPos{lastToken.Begin()};
        (void)Skip(TokenKind::RPAREN);
        lastToken = Token(TokenKind::LPAREN, "()", lparenPos, lastToken.End());
        return lastToken;
    }
    if (SkipOperator()) {
        return lastToken;
    }
    return Token{TokenKind::ILLEGAL};
}

// Trustlist checks the name of the operator overload function.
std::string ParserImpl::ExpectOperatorIdentifier(FuncDecl& fd)
{
    Peek();
    auto operatorTok = SkipAndReturnOverloadingOperator();
    if (operatorTok.kind == TokenKind::ILLEGAL) {
        DiagInvalidOverloadedOperator();
        Next();
        return INVALID_IDENTIFIER;
    }
    // operator function do not support generic, but we do error check in sema.
    if (!Seeing(TokenKind::LPAREN) && !Seeing(TokenKind::LT)) {
        DiagExpectCharacter("'('");
        Next();
        return INVALID_IDENTIFIER;
    }
    fd.identifier.SetPos(operatorTok.Begin(), operatorTok.End());
    fd.op = operatorTok.kind;
    return operatorTok.Value();
}

OwnedPtr<FuncDecl> ParserImpl::ParseFinalizer(
    ScopeKind scopeKind, const std::set<Modifier> modifiers, PtrVector<Annotation> annos)
{
    Next(); // skip ~
    auto tildeBegin{lastToken.Begin()};
    OwnedPtr<FuncDecl> funcDecl = MakeOwned<FuncDecl>();
    [[maybe_unused]]ChainScope cs(*this, funcDecl.get());
    funcDecl->begin = tildeBegin;
    Skip(TokenKind::INIT); // skip init
    funcDecl->identifier = SrcIdentifier{"~init", tildeBegin, lastToken.End(), false};
    funcDecl->keywordPos = lastToken.Begin();
    funcDecl->EnableAttr(Attribute::FINALIZER);
    CheckDeclarationInScope(scopeKind, DefKind::FINALIZER);
    funcDecl->funcBody = ParseFuncBody(scopeKind);
    auto initAttrs = CheckDeclModifiers(modifiers, scopeKind, DefKind::FINALIZER);
    for (auto& it : initAttrs) {
        funcDecl->EnableAttr(it);
    }
    if (!funcDecl->funcBody->paramLists.empty() && !funcDecl->funcBody->paramLists[0]->params.empty()) {
        ParseDiagnoseRefactor(
            DiagKindRefactor::parse_finalizer_can_not_accept_any_parameter, *funcDecl->funcBody->paramLists[0]);
    }
    funcDecl->modifiers.insert(modifiers.begin(), modifiers.end());
    if (funcDecl->funcBody) {
        funcDecl->end = funcDecl->funcBody->end;
    }
    CheckNoDeprecatedAnno(annos, "~init");
    ParseFuncDeclAnnos(annos, *funcDecl);
    if (!funcDecl->funcBody || !funcDecl->funcBody->body) {
        DiagMissingBody("finalizer", "", funcDecl->end);
        if (!parseDeclFile) {
            funcDecl->EnableAttr(Attribute::HAS_BROKEN);
        }
    }
    if (funcDecl->funcBody && funcDecl->funcBody->retType) {
        ParseDiagnoseRefactor(
            DiagKindRefactor::parse_invalid_return_type, *funcDecl->funcBody->retType, "finalizer");
        funcDecl->EnableAttr(Attribute::HAS_BROKEN);
    }
    return funcDecl;
}

OwnedPtr<FuncDecl> ParserImpl::ParseConstructor(
    ScopeKind scopeKind, const std::set<Modifier>& modifiers, PtrVector<Annotation> annos)
{
    OwnedPtr<FuncDecl> funcDecl = MakeOwned<FuncDecl>();
    ChainScope cs(*this, funcDecl.get());
    funcDecl->begin = lookahead.Begin();
    funcDecl->identifier = "init";
    funcDecl->identifier.SetPos(lookahead.Begin(), lookahead.End());
    funcDecl->keywordPos = lookahead.Begin();
    funcDecl->EnableAttr(Attribute::CONSTRUCTOR);
    if (HasModifier(modifiers, TokenKind::STATIC)) {
        CheckNoDeprecatedAnno(annos, "static constructor");
    }
    if (HasModifier(modifiers, TokenKind::COMMON)) {
        funcDecl->EnableAttr(Attribute::COMMON);
    }
    Next();
    CheckDeclarationInScope(scopeKind, DefKind::CONSTRUCTOR);
    funcDecl->funcBody = ParseFuncBody(scopeKind);

    auto initAttrs = CheckDeclModifiers(modifiers, scopeKind, DefKind::CONSTRUCTOR);
    for (auto& it : initAttrs) {
        funcDecl->EnableAttr(it);
    }
    funcDecl->modifiers.insert(modifiers.begin(), modifiers.end());
    CheckVariableParams(*funcDecl);
    if (funcDecl->TestAttr(Attribute::STATIC)) {
        // Static init is always 'private'.
        funcDecl->EnableAttr(Attribute::PRIVATE);
        if (!funcDecl->funcBody->paramLists.empty() && !funcDecl->funcBody->paramLists[0]->params.empty()) {
            ParseDiagnoseRefactor(
                DiagKindRefactor::parse_static_init_can_not_accept_any_parameter, *funcDecl->funcBody->paramLists[0]);
        }
    }
    if (funcDecl->funcBody) {
        funcDecl->end = funcDecl->funcBody->end;
    }
    ParseFuncDeclAnnos(annos, *funcDecl);
    CheckConstructorBody(*funcDecl, scopeKind);

    return funcDecl;
}

OwnedPtr<PrimaryCtorDecl> ParserImpl::ParsePrimaryConstructor(
    ScopeKind scopeKind, const std::set<Modifier>& modifiers, PtrVector<Annotation> annos)
{
    if (HasModifier(modifiers, TokenKind::STATIC)) {
        CheckNoDeprecatedAnno(annos, "static constructor");
    }
    OwnedPtr<PrimaryCtorDecl> primaryCtorDecl = MakeOwned<PrimaryCtorDecl>();
    ChainScope cs(*this, primaryCtorDecl.get());
    if (!modifiers.empty()) {
        primaryCtorDecl->begin = SortModifierByPos(modifiers)[0]->begin;
    } else {
        primaryCtorDecl->begin = lookahead.Begin();
    }
    primaryCtorDecl->keywordPos = lookahead.Begin();
    auto identifier = ParseIdentifierFromToken(lookahead);
    if (identifier == INVALID_IDENTIFIER) {
        return primaryCtorDecl;
    }
    if (identifier != curPrimaryDeclIdent && !curPrimaryDeclIdent.empty()) {
        DiagUnknownPrimaryConstructor(curPrimaryDeclIdent);
    }
    primaryCtorDecl->identifier = std::move(identifier);
    Next();
    if (scopeKind == ScopeKind::CLASS_BODY) {
        primaryCtorDecl->funcBody = ParseFuncBody(ScopeKind::PRIMARY_CONSTRUCTOR_BODY_FOR_CLASS);
        CheckVariableParams(*primaryCtorDecl);
        primaryCtorDecl->EnableAttr(Attribute::IN_CLASSLIKE);
    }
    if (scopeKind == ScopeKind::STRUCT_BODY) {
        primaryCtorDecl->funcBody = ParseFuncBody(ScopeKind::PRIMARY_CONSTRUCTOR_BODY_FOR_STRUCT);
        CheckVariableParams(*primaryCtorDecl);
        primaryCtorDecl->EnableAttr(Attribute::IN_STRUCT);
    }
    primaryCtorDecl->EnableAttr(Attribute::CONSTRUCTOR);
    auto initAttrs = CheckDeclModifiers(modifiers, scopeKind, DefKind::PRIMARY_CONSTRUCTOR);
    for (auto& it : initAttrs) {
        primaryCtorDecl->EnableAttr(it);
    }
    if (HasModifier(modifiers, TokenKind::CONST)) {
        primaryCtorDecl->isConst = true;
    }
    primaryCtorDecl->modifiers.insert(modifiers.begin(), modifiers.end());
    if (HasModifier(modifiers, TokenKind::UNSAFE)) {
        SetUnsafe(primaryCtorDecl.get(), modifiers);
    }
    if ((!primaryCtorDecl->funcBody || !primaryCtorDecl->funcBody->body) &&
        !primaryCtorDecl->TestAttr(Attribute::COMMON)) {
        DiagMissingBody("main constructor",
            !primaryCtorDecl->identifier.Valid() ? "" : " '" + primaryCtorDecl->identifier + "'", lastToken.End());
    }
    ParseFuncDeclAnnos(annos, *primaryCtorDecl);
    if (primaryCtorDecl->funcBody) {
        primaryCtorDecl->end = primaryCtorDecl->funcBody->end;
    }
    mpImpl->CheckCJMPDecl(*primaryCtorDecl);
    if (primaryCtorDecl->funcBody && primaryCtorDecl->funcBody->retType) {
        ParseDiagnoseRefactor(
            DiagKindRefactor::parse_invalid_return_type, *primaryCtorDecl->funcBody->retType, "primary constructor");
    }
    return primaryCtorDecl;
}

template <typename T> bool ParserImpl::CheckSkipRcurOrPrematureEnd(T& ret)
{
    if (Skip(TokenKind::RCURL)) {
        ret->rightCurlPos = lastToken.Begin();
        return true;
    }
    if (DetectPrematureEnd() && !ret->TestAttr(Attribute::HAS_BROKEN)) {
        ret->EnableAttr(Attribute::IS_BROKEN);
        DiagExpectedRightDelimiter("{", ret->leftCurlPos);
        return true;
    }
    return false;
}

OwnedPtr<ClassBody> ParserImpl::ParseClassBody(ClassDecl& cd)
{
    OwnedPtr<ClassBody> ret = MakeOwned<ClassBody>();
    if (!Skip(TokenKind::LCURL)) {
        ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_left_brace, lookahead, ConvertToken(lookahead));
        ConsumeUntilDecl(TokenKind::LCURL);
        Skip(TokenKind::LCURL);
        ret->EnableAttr(Attribute::HAS_BROKEN);
        ret->EnableAttr(Attribute::IS_BROKEN);
        return ret;
    } else {
        ret->leftCurlPos = lastToken.Begin();
    }
    ret->begin = lookahead.Begin();
    while (true) {
        bool hasNLorSEMI = SkipNLOrSemi();
        if (CheckSkipRcurOrPrematureEnd(ret)) {
            break;
        }
        if (!ret->decls.empty() && !hasNLorSEMI) {
            DiagExpectSemiOrNewline();
        }
        auto decl = ParseDecl(ScopeKind::CLASS_BODY);
        if (auto ctor = As<ASTKind::FUNC_DECL>(decl); ctor && ctor->TestAttr(Attribute::CONSTRUCTOR)) {
            if (cd.TestAttr(Attribute::JAVA_MIRROR)) {
                ctor->EnableAttr(Attribute::JAVA_MIRROR);
            }
            if (cd.TestAttr(Attribute::OBJ_C_MIRROR)) {
                ctor->EnableAttr(Attribute::OBJ_C_MIRROR);
            }
            CheckInitCtorDeclBody(*ctor);
        }

        if (decl->IsInvalid()) {
            continue;
        }
        SetMemberParentInheritableDecl(cd, decl);

        if (auto fd = As<ASTKind::FUNC_DECL>(decl.get())) {
            CheckClassLikeFuncBodyAbstractness(*fd);
        } else if (auto pd = As<ASTKind::PROP_DECL>(decl.get())) {
            CheckClassLikePropAbstractness(*pd);
        }

        CheckJavaInteropMember(*decl);
        CheckObjCInteropMember(*decl);

        if (decl->astKind == ASTKind::PRIMARY_CTOR_DECL && decl->identifier != cd.identifier) {
            ret->decls.emplace_back(MakeOwned<InvalidDecl>(decl->begin));
            continue;
        }
        ret->decls.emplace_back(std::move(decl));
    }
    ret->end = lastToken.End();
    return ret;
}

OwnedPtr<InterfaceBody> ParserImpl::ParseInterfaceBody(InterfaceDecl& id)
{
    if (!Skip(TokenKind::LCURL)) {
        ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_left_brace, lookahead, ConvertToken(lookahead));
        return MakeInvalid<InterfaceBody>(lastToken.End());
    }
    OwnedPtr<InterfaceBody> ret = MakeOwned<InterfaceBody>();

    ret->leftCurlPos = lastToken.Begin();
    ret->begin = ret->leftCurlPos;
    while (true) {
        SkipBlank(TokenKind::SEMI);
        if (CheckSkipRcurOrPrematureEnd(ret)) {
            break;
        }
        auto decl = ParseDecl(ScopeKind::INTERFACE_BODY);
        if (decl->IsInvalid()) {
            continue;
        }
        SetMemberParentInheritableDecl(id, decl);

        if (auto fd = As<ASTKind::FUNC_DECL>(decl.get())) {
            CheckClassLikeFuncBodyAbstractness(*fd);
        } else if (auto pd = As<ASTKind::PROP_DECL>(decl.get())) {
            CheckClassLikePropAbstractness(*pd);
        }

        CheckJavaInteropMember(*decl);
        CheckObjCInteropMember(*decl);

        ret->decls.emplace_back(std::move(decl));
    }
    ret->end = ret->rightCurlPos;
    ret->end.column += 1;
    return ret;
}

OwnedPtr<Decl> ParserImpl::ParseEnumConstructor(
    const std::set<AST::Modifier>& modifiers, PtrVector<AST::Annotation>& annos)
{
    OwnedPtr<Decl> ret;
    std::string caseIdent = lookahead.Value();
    auto caseIdentPos(lookahead.Begin());
    Next();
    if (caseIdent == INVALID_IDENTIFIER) {
        return MakeOwned<InvalidDecl>(lookahead.Begin());
    }
    auto lkCopy(lookahead);
    ret = Skip(TokenKind::LPAREN)
        ? ParseEnumConstructorWithArgs(lkCopy, annos)
        : ParseNoArgsEnumConstructor(lkCopy, annos);
    [[maybe_unused]] ChainScope cs(*this, ret.get());
    if (!modifiers.empty()) {
        auto firstMod = *SortModifierByPos(modifiers)[0];
        DiagExpectNoModifier(firstMod);
    }
    for (auto& anno : annos) {
        if (!(anno->kind == AnnotationKind::WHEN || anno->kind == AnnotationKind::DEPRECATED)) {
            DiagUnexpectedAnnoOn(*annos[0], caseIdentPos, annos[0]->identifier, caseIdent);
        }
    }
    return ret;
}

OwnedPtr<Decl> ParserImpl::ParseEnumConstructorWithArgs(const Token& id, PtrVector<Annotation>& annos)
{
    OwnedPtr<FuncParamList> funcParamList = MakeOwned<FuncParamList>();
    funcParamList->leftParenPos = lastToken.Begin();
    int nameIndex = 1;
    ParseOneOrMoreWithSeparator(
        TokenKind::COMMA,
        [&funcParamList](
            const Position commaPos) { funcParamList->params.back()->commaPos = commaPos; },
        [this, &nameIndex, &funcParamList]() {
            OwnedPtr<FuncParam> funcParam = MakeOwned<FuncParam>();
            funcParam->identifier = "p" + std::to_string(nameIndex);
            funcParam->isIdentifierCompilerAdd = true;
            funcParam->type = ParseType();
            funcParam->begin = funcParam->type->begin;
            funcParam->identifier.SetPos(funcParam->type->begin, funcParam->type->begin);
            funcParam->isVar = false;
            funcParam->end = funcParam->type->end;
            (void)funcParamList->params.emplace_back(std::move(funcParam));
            nameIndex++;
        });
    if (!Skip(TokenKind::RPAREN)) {
        DiagExpectedRightDelimiter("(", funcParamList->leftParenPos);
    }
    funcParamList->rightParenPos = lastToken.Begin();
    funcParamList->begin = funcParamList->leftParenPos;
    funcParamList->end = funcParamList->rightParenPos;

    OwnedPtr<FuncBody> funcBody = MakeOwned<FuncBody>();
    funcBody->begin = lastToken.Begin();
    funcBody->paramLists.emplace_back(std::move(funcParamList));
    funcBody->end = lastToken.End();

    OwnedPtr<FuncDecl> ret = MakeOwned<FuncDecl>();
    ret->identifier = ParseIdentifierFromName(id.Value(), id.Begin(), id.End(), id.Length());
    ret->funcBody = std::move(funcBody);
    ret->begin = id.Begin();
    ret->end = lastToken.End();
    ret->EnableAttr(Attribute::ENUM_CONSTRUCTOR);
    ret->annotations = std::move(annos);
    return ret;
}

OwnedPtr<Decl> ParserImpl::ParseNoArgsEnumConstructor(const Token& id, PtrVector<Annotation>& annos)
{
    OwnedPtr<VarDecl> ret = MakeOwned<VarDecl>();
    ret->identifier = ParseIdentifierFromName(id.Value(), id.Begin(), id.End(), id.Length());
    ret->begin = id.Begin();
    ret->end = lastToken.End();
    ret->isVar = false;
    ret->EnableAttr(Attribute::ENUM_CONSTRUCTOR);
    ret->annotations = std::move(annos);
    return ret;
}

void ParserImpl::ParseConstraints(const Decl& decl)
{
    if (!decl.generic) {
        DiagUnexpectedWhere(lastToken);
    } else {
        decl.generic->genericConstraints = ParseGenericConstraints();
    }
}

void ParserImpl::ParseInheritedTypes(InheritableDecl& decl)
{
    decl.upperBoundPos = lastToken.Begin();
    while (true) {
        auto interfaceType = ParseType();
        if (auto ref = AST::As<ASTKind::REF_TYPE>(interfaceType.get()); ref && ref->ref.identifier == decl.identifier) {
            DiagDeclCannotInheritTheirSelf(decl, *ref);
        } else if (Is<RefType>(interfaceType.get()) || Is<QualifiedType>(interfaceType.get())) {
            decl.inheritedTypes.emplace_back(std::move(interfaceType));
        } else {
            if (!interfaceType->TestAttr(Attribute::IS_BROKEN)) {
                DiagInvalidInheritType(*interfaceType);
            }
        }
        if (!Skip(TokenKind::BITAND)) {
            break;
        }
        if (!decl.inheritedTypes.empty()) {
            decl.inheritedTypes.back()->bitAndPos = lastToken.Begin();
        }
    }
}

void ParserImpl::ParseInterfaceDeclOrClassDeclGeneric(InheritableDecl& ret)
{
    if (Skip(TokenKind::LT)) {
        ret.EnableAttr(Attribute::GENERIC);
        ret.generic = ParseGeneric();
    }
    if (SeeingAny({TokenKind::IDENTIFIER, TokenKind::COLON}) || SeeingContextualKeyword()) {
        ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_lt_brace, lookahead, ConvertToken(lookahead));
        ConsumeUntilDecl(TokenKind::LCURL);
    }
    if (Skip(TokenKind::UPPERBOUND)) {
        ParseInheritedTypes(ret);
    }
    if (Skip(TokenKind::WHERE)) {
        ParseConstraints(ret);
    }
}

void ParserImpl::SetMemberParentInheritableDecl(InheritableDecl& ret, const OwnedPtr<AST::Decl>& decl) const
{
    decl->outerDecl = &ret;
    if (auto macroExpandDecl = DynamicCast<MacroExpandDecl*>(decl.get());
        macroExpandDecl && macroExpandDecl->invocation.decl) {
        SetMemberParentInheritableDecl(ret, macroExpandDecl->invocation.decl);
    } else if (auto pd = DynamicCast<AST::PrimaryCtorDecl*>(decl.get());
               pd && pd->funcBody && !pd->funcBody->paramLists.empty()) {
        for (auto& param : pd->funcBody->paramLists[0]->params) {
            if (param->isMemberParam) {
                param->outerDecl = &ret;
            }
        }
    } else if (auto func = DynamicCast<FuncDecl*>(decl.get()); func && func->funcBody) {
        if (ret.IsClassLikeDecl()) {
            func->funcBody->parentClassLike = StaticCast<ClassLikeDecl*>(&ret);
        } else if (ret.astKind == ASTKind::ENUM_DECL) {
            func->funcBody->parentEnum = StaticCast<EnumDecl*>(&ret);
        }
    }
}

void ParserImpl::SetDefaultFunc(ScopeKind scopeKind, AST::Decl& decl) const
{
    if (scopeKind != ScopeKind::INTERFACE_BODY) {
        return;
    }
    if (decl.astKind == ASTKind::FUNC_DECL) {
        bool defaultFunc = !decl.TestAttr(Attribute::FOREIGN) && !decl.TestAttr(Attribute::ABSTRACT);
        if (defaultFunc) {
            decl.EnableAttr(Attribute::DEFAULT);
        }
    } else if (decl.astKind == ASTKind::PROP_DECL) {
        if (decl.TestAttr(Attribute::ABSTRACT)) {
            return;
        }
        decl.EnableAttr(Attribute::DEFAULT);
        auto pd = RawStaticCast<PropDecl*>(&decl);
        for (auto& setter : pd->setters) {
            setter->EnableAttr(Attribute::DEFAULT);
        }
        for (auto& getter : pd->getters) {
            getter->EnableAttr(Attribute::DEFAULT);
        }
    }
}

void ParserImpl::CheckDeclarationInScope(ScopeKind sk, DefKind dk)
{
    // Unknown scope is with parser called by macro mostly.
    if (sk == ScopeKind::UNKNOWN_SCOPE) {
        return;
    }
    if (GetModifierRulesByDefKind(dk).count(sk) == 0) {
        if (!chainedAST.back()->TestAttr(Attribute::HAS_BROKEN)) {
            DiagUnexpectedDeclInScope(sk);
            chainedAST.back()->EnableAttr(Attribute::HAS_BROKEN);
        }
    }
}

OwnedPtr<ClassDecl> ParserImpl::ParseClassDecl(
    ScopeKind scopeKind, const std::set<Modifier>& modifiers, std::vector<OwnedPtr<AST::Annotation>> annos)
{
    Next();
    OwnedPtr<ClassDecl> ret = MakeOwned<ClassDecl>();
    ChainScope cs(*this, ret.get());
    ret->begin = lookahead.Begin();
    ret->keywordPos = lookahead.Begin();
    ret->identifier = ExpectIdentifierWithPos(*ret);
    ParseInterfaceDeclOrClassDeclGeneric(*ret);
    CheckDeclarationInScope(scopeKind, DefKind::CLASS);
    auto attrs = CheckDeclModifiers(modifiers, scopeKind, DefKind::CLASS);
    for (auto& it : attrs) {
        ret->EnableAttr(it);
    }
    ffiParser->CheckClassLikeSignature(*ret, annos);
    ret->modifiers.insert(modifiers.begin(), modifiers.end());
    // Used to parse nested classDecl or primary ctor decl.
    SetPrimaryDecl(ret->identifier, ret->identifier.IsRaw());
    ret->body = ParseClassBody(*ret);
    RevertPrimaryDecl();
    ret->end = lastToken.End();
    ret->annotations = std::move(annos);

    if (Interop::Java::IsDeclAppropriateForSyntheticClassGeneration(*ret)) {
        Interop::Java::InsertSyntheticClassDecl(*ret, *currentFile);
    }

    CheckCJMappingAttr(*ret);

    return ret;
}

OwnedPtr<InterfaceDecl> ParserImpl::ParseInterfaceDecl(
    ScopeKind scopeKind, const std::set<Modifier>& modifiers, std::vector<OwnedPtr<AST::Annotation>> annos)
{
    Next();
    OwnedPtr<InterfaceDecl> ret = MakeOwned<InterfaceDecl>();
    ret->keywordPos = lookahead.Begin();
    ChainScope cs(*this, ret.get());
    ret->begin = lookahead.Begin();
    ret->identifier = ExpectIdentifierWithPos(*ret);
    ParseInterfaceDeclOrClassDeclGeneric(*ret);
    CheckDeclarationInScope(scopeKind, DefKind::INTERFACE);
    auto attrs = CheckDeclModifiers(modifiers, scopeKind, DefKind::INTERFACE);
    for (auto& it : attrs) {
        ret->EnableAttr(it);
    }
    ffiParser->CheckClassLikeSignature(*ret, annos);
    ret->modifiers.insert(modifiers.begin(), modifiers.end());
    ret->body = ParseInterfaceBody(*ret);
    ret->end = lastToken.End();
    ret->annotations = std::move(annos);

    if (Interop::Java::IsDeclAppropriateForSyntheticClassGeneration(*ret)) {
        Interop::Java::InsertSyntheticClassDecl(*ret, *currentFile);
    }

    CheckCJMappingAttr(*ret);
    if (Interop::ObjC::IsDeclAppropriateForSyntheticClassGeneration(*ret)) {
        Interop::ObjC::InsertSyntheticClassDecl(*ret, *currentFile);
    }

    return ret;
}

void ParserImpl::ParseCaseBody(EnumDecl& enumDecl)
{
    auto caseBody = ParseDecl(ScopeKind::ENUM_CONSTRUCTOR);
    if (caseBody->astKind == AST::ASTKind::FUNC_DECL) {
        enumDecl.hasArguments = true;
    }
    if (!caseBody->TestAttr(Attribute::ENUM_CONSTRUCTOR)) {
        ParseDiagnoseRefactor(DiagKindRefactor::parse_unknown_enum_constructor, *caseBody);
    }
    SetMemberParentInheritableDecl(enumDecl, caseBody);
    // common/platform enum attributes propagate to it's constructors to be used in further resolve
    if (enumDecl.TestAttr(Attribute::PLATFORM)) {
        caseBody->EnableAttr(Attribute::PLATFORM);
    } else if (enumDecl.TestAttr(Attribute::COMMON)) {
        caseBody->EnableAttr(Attribute::COMMON);
    }

    enumDecl.constructors.emplace_back(std::move(caseBody));
}

void ParserImpl::ParseEnumBody(EnumDecl& enumDecl)
{
    enumDecl.bodyScope = MakeOwned<DummyBody>();
    enumDecl.bodyScope->begin = lastToken.Begin();

    if (Seeing(TokenKind::BITOR)) { // Skip the first (optional) 'BITOR' in the caseBody.
        Next();
    }
    do {
        if (lastToken.kind == TokenKind::BITOR) {
            enumDecl.bitOrPosVector.emplace_back(lastToken.Begin());
        }
        bool seeingEllipse = Seeing(TokenKind::ELLIPSIS);
        if ((!seeingEllipse && !Seeing(TokenKind::IDENTIFIER) && !SeeingMacroCallDecl() &&
            !SeeingContextualKeyword() && !SeeingAtWhen()) || SeeingKeywordWithDecl()) {
            if (!chainedAST.back()->TestAttr(Attribute::IS_BROKEN)) {
                DiagExpectedIdentifierEnumDecl(&enumDecl);
            }
            TryConsumeUntilAny({TokenKind::BITOR});
            chainedAST.back()->EnableAttr(Attribute::IS_BROKEN);
        } else {
            if (seeingEllipse) {
                Next();
                if (enumDecl.constructors.empty()) {
                    DiagExpectedIdentifierEnumDecl(&enumDecl);
                }
                enumDecl.hasEllipsis = true;
                enumDecl.ellipsisPos = lastToken.Begin();
                // ellipse must be the last constructor
                break;
            }
            ParseCaseBody(enumDecl);
        }
        SkipBlank(TokenKind::SEMI);
    } while (Skip(TokenKind::BITOR));
    // Parse functions.
    while (true) {
        SkipBlank(TokenKind::SEMI);
        if (Skip(TokenKind::RCURL)) {
            enumDecl.rightCurlPos = lastToken.Begin();
            break;
        }
        if (DetectPrematureEnd()) {
            DiagExpectedRightDelimiter("{", enumDecl.leftCurlPos);
            break;
        }
        auto decl = ParseDecl(ScopeKind::ENUM_BODY);
        if (decl->IsInvalid()) {
            continue;
        }
        if (decl->TestAttr(Attribute::ENUM_CONSTRUCTOR)) {
            ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_decl, MakeRange(decl->identifier), decl->identifier);
        }
        SetMemberParentInheritableDecl(enumDecl, decl);
        enumDecl.members.emplace_back(std::move(decl));
    }
}

OwnedPtr<EnumDecl> ParserImpl::ParseEnumDecl(
    ScopeKind scopeKind, const std::set<Modifier>& modifiers, std::vector<OwnedPtr<AST::Annotation>> annos)
{
    Next();
    OwnedPtr<EnumDecl> ret = MakeOwned<EnumDecl>();
    ChainScope cs(*this, ret.get());
    ret->annotations = std::move(annos);
    ret->begin = lookahead.Begin();
    ret->keywordPos = lookahead.Begin();
    CheckDeclarationInScope(scopeKind, DefKind::ENUM);
    auto attrs = CheckDeclModifiers(modifiers, scopeKind, DefKind::ENUM);
    for (auto& it : attrs) {
        ret->EnableAttr(it);
    }
    ret->modifiers.insert(modifiers.begin(), modifiers.end());
    ret->identifier = ExpectIdentifierWithPos(*ret);
    if (Skip(TokenKind::LT)) {
        ret->EnableAttr(Attribute::GENERIC);
        ret->generic = ParseGeneric();
    }
    if (Skip(TokenKind::UPPERBOUND)) {
        ParseInheritedTypes(*ret);
    }
    if (Skip(TokenKind::WHERE)) {
        ParseConstraints(*ret);
    }
    if (!Skip(TokenKind::LCURL)) {
        ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_left_brace, lookahead, ConvertToken(lookahead));
        ConsumeUntilDeclOrNL(TokenKind::LCURL);
        if (!Skip(TokenKind::LCURL)) {
            ret->EnableAttr(Attribute::IS_BROKEN);
            ret->bodyScope = MakeOwned<DummyBody>(); // The 'bodyScope' must exist.
            ret->end = lastToken.End();
            return ret;
        }
    }
    ret->leftCurlPos = lastToken.Begin();
    ParseEnumBody(*ret);
    ret->end = lastToken.End();
    ret->bodyScope->end = ret->end;

    CheckCJMappingAttr(*ret);

    return ret;
}

OwnedPtr<StructBody> ParserImpl::ParseStructBody(StructDecl& sd)
{
    if (!Skip(TokenKind::LCURL)) {
        ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_left_brace, lookahead, ConvertToken(lookahead));
        return MakeInvalid<StructBody>(lastToken.End());
    }
    OwnedPtr<StructBody> ret = MakeOwned<StructBody>();

    ret->leftCurlPos = lastToken.Begin();
    ret->begin = lookahead.Begin();
    while (true) {
        SkipBlank(TokenKind::SEMI);
        if (CheckSkipRcurOrPrematureEnd(ret)) {
            break;
        }

        auto decl = ParseDecl(ScopeKind::STRUCT_BODY);
        if (auto ctor = As<ASTKind::FUNC_DECL>(decl); ctor && ctor->TestAttr(Attribute::CONSTRUCTOR)) {
            if (sd.TestAttr(Attribute::JAVA_MIRROR)) {
                ctor->EnableAttr(Attribute::JAVA_MIRROR);
            }
            CheckInitCtorDeclBody(*ctor);
        }
        if (decl->IsInvalid()) {
            continue;
        }
        SetMemberParentInheritableDecl(sd, decl);
        if (decl->astKind == ASTKind::PRIMARY_CTOR_DECL && decl->identifier != sd.identifier) {
            ret->decls.emplace_back(MakeOwned<InvalidDecl>(decl->begin));
            continue;
        }
        ret->decls.emplace_back(std::move(decl));
    }
    ret->rightCurlPos = lastToken.Begin();
    ret->end = lastToken.End();
    return ret;
}

OwnedPtr<StructDecl> ParserImpl::ParseStructDecl(
    ScopeKind scopeKind, const std::set<Modifier>& modifiers, PtrVector<Annotation> annos)
{
    Next();
    OwnedPtr<StructDecl> ret = MakeOwned<StructDecl>();
    ChainScope cs(*this, ret.get());
    ret->begin = lookahead.Begin();
    ret->keywordPos = lookahead.Begin();
    CheckDeclarationInScope(scopeKind, DefKind::STRUCT);
    auto attrs = CheckDeclModifiers(modifiers, scopeKind, DefKind::STRUCT);
    for (auto& attr : attrs) {
        ret->EnableAttr(attr);
    }
    ret->modifiers.insert(modifiers.begin(), modifiers.end());
    ret->identifier = ExpectIdentifierWithPos(*ret);
    if (Skip(TokenKind::LT)) {
        ret->generic = ParseGeneric();
        ret->EnableAttr(Attribute::GENERIC);
    }
    if (Skip(TokenKind::UPPERBOUND)) {
        ret->upperBoundPos = lastToken.Begin();
        ParseStructInheritedTypes(*ret);
    }
    if (Skip(TokenKind::WHERE)) {
        ParseConstraints(*ret);
    }
    // Used to parse nested structDecl or primary ctor decl.
    SetPrimaryDecl(ret->identifier, ret->identifier.IsRaw());
    ret->body = ParseStructBody(*ret);
    RevertPrimaryDecl();
    ret->end = lastToken.End();
    ret->annotations = std::move(annos);

    CheckCJMappingAttr(*ret);

    return ret;
}

void ParserImpl::ParseStructInheritedTypes(StructDecl& structDecl)
{
    while (true) {
        auto interfaceType = ParseType();
        if (Is<RefType>(interfaceType.get()) || Is<QualifiedType>(interfaceType.get())) {
            structDecl.inheritedTypes.emplace_back(std::move(interfaceType));
        } else {
            if (!interfaceType->TestAttr(Attribute::IS_BROKEN)) {
                DiagInvalidInheritType(*interfaceType);
            }
        }
        if (!Skip(TokenKind::BITAND)) {
            break;
        }
        if (!structDecl.inheritedTypes.empty()) {
            structDecl.inheritedTypes.back()->bitAndPos = lastToken.Begin();
        }
    }
}

OwnedPtr<TypeAliasDecl> ParserImpl::ParseTypeAlias(
    ScopeKind scopeKind, const std::set<Modifier>& modifiers, PtrVector<Annotation> annos)
{
    Next();
    OwnedPtr<TypeAliasDecl> ret = MakeOwned<TypeAliasDecl>();
    ChainScope cs(*this, ret.get());
    ret->annotations = std::move(annos);
    ret->begin = lookahead.Begin();
    ret->keywordPos = lookahead.Begin();
    CheckDeclarationInScope(scopeKind, DefKind::TYPE);
    auto attrs = CheckDeclModifiers(modifiers, scopeKind, DefKind::TYPE);
    for (auto& it : attrs) {
        ret->EnableAttr(it);
    }
    while (SeeingOperator()) {
        DiagExpectedName("type", "after 'type'");
        Next();
    }
    ret->modifiers.insert(modifiers.begin(), modifiers.end());
    ret->identifier = ExpectIdentifierWithPos(*ret);
    if (Skip(TokenKind::LT)) {
        ret->EnableAttr(Attribute::GENERIC);
        ret->generic = ParseGeneric();
    }
    if (Skip(TokenKind::ASSIGN)) {
        ret->assignPos = lookahead.Begin();
    } else {
        ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_assignment, lookahead, ConvertToken(lookahead));
        ConsumeUntilDecl();
    }
    // Subtype or trait constraints are not allowed in typealias decl. If the type of RHS of = is a type that has
    // constraints, related constraints need to be applied to type parameters on the LHS of =.
    // Example: In typealias A<T> = C<T,Int32>, if T is constrained in type C's decl, the constraint should be
    // added to T for type A in a later compiler phase.
    ret->type = ParseType();
    ret->end = lastToken.End();
    return ret;
}

OwnedPtr<ExtendDecl> ParserImpl::ParseExtendDecl(
    ScopeKind scopeKind, const std::set<AST::Modifier>& modifiers, PtrVector<Annotation> annos)
{
    Next(); // Consume the extend token.
    auto ret = MakeOwned<ExtendDecl>();
    ChainScope cs(*this, ret.get());
    CheckNoDeprecatedAnno(annos, "extend");
    ret->annotations = std::move(annos);

    ret->begin = lookahead.Begin();
    ret->keywordPos = lookahead.Begin();
    ret->identifier.SetPos(lookahead.Begin(), lookahead.Begin());
    CheckDeclarationInScope(scopeKind, DefKind::EXTEND);
    if (modifiers.size() == 1 && mpImpl->HasCJMPModifiers(*modifiers.begin())) {
        mpImpl->CheckCJMPModifiers(modifiers);
        auto attr = GetAttributeByModifier(modifiers.begin()->modifier);
        ret->EnableAttr(attr.value());
    } else if (!modifiers.empty() && !chainedAST.back()->TestAttr(Attribute::IS_BROKEN)) {
        DiagExpectNoModifier(*modifiers.begin());
    }
    CheckCJMappingAttr(*ret);
    ParseExtendedType(*ret);
    if (Skip(TokenKind::UPPERBOUND)) { // Interface extension.
        ret->upperBoundPos = lastToken.Begin();
        do {
            if (lastToken.kind == TokenKind::BITAND) {
                ret->inheritedTypes.back()->bitAndPos = lastToken.Begin();
            }
            ret->inheritedTypes.push_back(ParseType());
        } while (Skip(TokenKind::BITAND));
    }
    if (Skip(TokenKind::WHERE)) { // Generic constraints.
        ParseConstraints(*ret);
    }
    ParseExtendBody(*ret);
    ret->end = lastToken.End();
    ret->bodyScope->end = ret->end;
    return ret;
}

void ParserImpl::SetUnsafe(Ptr<Node> node, const std::set<Modifier>& modifiers)
{
    auto iter = std::find_if(modifiers.begin(), modifiers.end(), [](auto& mod) {
        return mod.modifier == TokenKind::UNSAFE;
    });
    if (!IsUnsafeBackend(backend) && iter != modifiers.end()) {
        DiagUnsafeWillBeIgnored(*iter);
        return;
    }
    auto walker = Walker(node, [](Ptr<Node> node) {
        if (node == nullptr) {
            return VisitAction::SKIP_CHILDREN;
        }
        if (node->astKind == ASTKind::CALL_EXPR) {
            node->EnableAttr(Attribute::UNSAFE);
        }
        if (auto expr = DynamicCast<OverloadableExpr*>(node); expr) {
            expr->EnableAttr(Attribute::UNSAFE);
        }
        return VisitAction::WALK_CHILDREN;
    });
    walker.Walk();
}

OwnedPtr<GenericParamDecl> ParserImpl::ParseGenericParamDecl()
{
    OwnedPtr<GenericParamDecl> ret = MakeOwned<GenericParamDecl>();
    ChainScope cs(*this, ret.get());
    ret->identifier = ParseIdentifierFromToken(lookahead);
    ret->begin = lookahead.Begin();
    ret->end = lookahead.End();
    Next();
    return ret;
}

OwnedPtr<Generic> ParserImpl::ParseGeneric()
{
    CJC_ASSERT(lastToken.kind == TokenKind::LT);
    auto ret = MakeOwned<Generic>();
    ret->leftAnglePos = lastToken.Begin();
    ret->begin = lastToken.Begin();
    ChainScope cs(*this, ret.get());
    ParseOneOrMoreSepTrailing(
        [&ret](const Position commaPos) {
            if (!ret->typeParameters.empty()) {
                ret->typeParameters.back()->commaPos = commaPos;
            }
        },
        [&ret, this]() {
            OwnedPtr<GenericParamDecl> gpd;
            if (Seeing(TokenKind::IDENTIFIER) || SeeingContextualKeyword()) {
                gpd = ParseGenericParamDecl();
            } else {
                if (!ret->TestAttr(Attribute::IS_BROKEN)) {
                    DiagExpectedIdentifierGeneric(ret.get());
                }
                TryConsumeUntilAny({TokenKind::COMMA, TokenKind::GT});
                ret->EnableAttr(Attribute::IS_BROKEN);
            }
            if (lookahead.kind != TokenKind::GT && gpd) {
                ret->typeParameters.emplace_back(std::move(gpd));
            }
        }, TokenKind::GT);
    if (!Skip(TokenKind::GT)) {
        if (!ret->TestAttr(Attribute::IS_BROKEN)) {
            ret->end = lastToken.End();
            DiagExpectedRightDelimiter("<", ret->leftAnglePos);
        }
    } else {
        ret->rightAnglePos = lastToken.Begin();
        ret->end = lastToken.End();
    }
    return ret;
}

bool ParserImpl::ParseGenericUpperBound(const OwnedPtr<GenericConstraint>& genericConstraint)
{
    bool illegalConstraint{false};
    if (!Skip(TokenKind::UPPERBOUND)) {
        if (!genericConstraint->TestAttr(Attribute::HAS_BROKEN)) {
            DiagExpectCharacter("'<:'");
            genericConstraint->EnableAttr(Attribute::HAS_BROKEN);
        }
        TryConsumeUntilAny(GetTypeFirst());
        illegalConstraint = true;
    } else {
        genericConstraint->operatorPos = lastToken.Begin();
    }
    do {
        if (lastToken.kind == TokenKind::BITAND) {
            genericConstraint->bitAndPos.push_back(lastToken.Begin());
        }
        auto upperBound = ParseType();
        if (!upperBound->TestAttr(Attribute::IS_BROKEN) && upperBound->astKind != ASTKind::THIS_TYPE) {
            genericConstraint->upperBounds.push_back(std::move(upperBound));
        } else {
            if (!genericConstraint->TestAttr(Attribute::HAS_BROKEN)) {
                DiagExpectedIdentifierGenericConstraint(genericConstraint.get());
                genericConstraint->EnableAttr(Attribute::HAS_BROKEN);
            }
            auto match = TryConsumeUntilAny({TokenKind::BITAND});
            illegalConstraint = true;
            if (!match) {
                break;
            }
        }
    } while (Skip(TokenKind::BITAND));
    return illegalConstraint;
}

std::vector<OwnedPtr<GenericConstraint>> ParserImpl::ParseGenericConstraints()
{
    std::vector<OwnedPtr<GenericConstraint>> ret;
    do {
        auto genericConstraint = MakeOwned<GenericConstraint>();
        ChainScope cs(*this, genericConstraint.get());
        if (lastToken.kind == TokenKind::COMMA && !ret.empty()) {
            ret.back()->commaPos = lastToken.Begin();
        } else if (lastToken.kind == TokenKind::WHERE) {
            genericConstraint->wherePos = lastToken.Begin();
        }
        genericConstraint->type = ParseRefType(true);
        SpreadAttrAndConsume(genericConstraint->type.get(), genericConstraint.get(), {TokenKind::UPPERBOUND});
        auto illegalConstraint = ParseGenericUpperBound(genericConstraint);
        if (!illegalConstraint) {
            genericConstraint->begin = genericConstraint->wherePos.IsZero() ?
                genericConstraint->type->begin : genericConstraint->wherePos;
            genericConstraint->end = lastToken.End();
            ret.push_back(std::move(genericConstraint));
        }
    } while (Skip(TokenKind::COMMA));
    std::unordered_map<std::string_view, Position> posRecord;
    for (auto& gc : ret) {
        if (gc && gc->type->astKind == AST::ASTKind::REF_TYPE) {
            auto rt = StaticAs<AST::ASTKind::REF_TYPE>(gc->type.get());
            if (posRecord.count(rt->ref.identifier.Val()) > 0) {
                DiagDuplicatedItem("type name", rt->ref.identifier, rt->begin, posRecord[rt->ref.identifier.Val()],
                    " in generic constrain");
            } else {
                posRecord[rt->ref.identifier.Val()] = rt->begin;
            }
        }
    }
    return ret;
}

void ParserImpl::ParseModifiers(std::set<Modifier>& modifiers)
{
    while (SeeingModifier() && !SeeingKeywordAndOperater()) {
        auto modifier = Modifier(lookahead.kind, lookahead.Begin());
        modifier.curFile = currentFile;
        if (!modifiers.emplace(modifier).second) {
            DiagDuplicatedModifier(modifier, *modifiers.find(modifier));
        }
        Next();
    }
}

bool ParserImpl::HasModifier(const std::set<Modifier>& modifiers, TokenKind tk)
{
    return Utils::In(modifiers, [&](const auto& mod) { return mod.modifier == tk; });
}

bool ParserImpl::HasAnnotation(const PtrVector<Annotation>& annos, AnnotationKind ak)
{
    return Utils::In(annos, [&](const auto& anno) { return anno->kind == ak; });
}

std::string ParserImpl::ParseForImportToGetContentBetween(
    unsigned int fileID, int beginLine, int beginColumn, int endLine, int endColumn)
{
    Position posBase = lexer->GetPosBase();
    int newBeginLine;
    int newBeginColumn;
    int newEndLine;
    int newEndColumn;
    if (beginLine == posBase.line && beginColumn == posBase.column) {
        newBeginLine = 1;
        newBeginColumn = 1;
        newEndLine = (endLine - beginLine) + 1;
        newEndColumn = endColumn;
    } else {
        newBeginLine = (beginLine - posBase.line) + 1;
        newBeginColumn = beginColumn;
        newEndLine = (endLine - posBase.line) + 1;
        newEndColumn = endColumn;
    }
    return sourceManager.GetContentBetween(
        fileID, Position(newBeginLine, newBeginColumn), Position(newEndLine, newEndColumn), inputString);
}

OwnedPtr<MainDecl> ParserImpl::ParseMainDecl(
    ScopeKind scopeKind, const std::set<AST::Modifier>& modifiers, PtrVector<Annotation> annos)
{
    OwnedPtr<MainDecl> ret = MakeOwned<MainDecl>();
    ChainScope cs(*this, ret.get());
    CheckNoDeprecatedAnno(annos, "main");

    ret->keywordPos = lookahead.Begin();
    ret->begin = lookahead.Begin();
    ret->identifier = {"main", lookahead.Begin(), lookahead.End()};
    ret->modifiers.insert(modifiers.begin(), modifiers.end());
    if (ret->identifier.Begin() == INVALID_POSITION) {
        ret->identifier.SetPos(lookahead.Begin(), lookahead.End());
    }
    Next();
    CheckDeclarationInScope(scopeKind, DefKind::MAIN);
    ret->funcBody = ParseFuncBody(ScopeKind::MAIN_BODY);
    if (ret->funcBody) {
        ret->end = ret->funcBody->end;
        // The Curring function is not supported in spec 0.22.
        if (ret->funcBody->paramLists.size() > 1) {
            ParseDiagnoseRefactor(
                DiagKindRefactor::parse_expected_left_brace, ret->funcBody->paramLists[1]->begin, std::string("("));
        }
    }
    const std::set<AnnotationKind> annoOnMain = {
        AnnotationKind::WHEN,
        AnnotationKind::NUMERIC_OVERFLOW,
        AnnotationKind::C,
    };
    for (auto& anno : annos) {
        if (std::find(annoOnMain.begin(), annoOnMain.end(), anno->kind) == annoOnMain.end()) {
            DiagUnexpectedAnnoOn(*annos[0], ret->identifier.Begin(), annos[0]->identifier.Val(), ret->identifier.Val());
        }
    }
    if (!ret->funcBody || !ret->funcBody->body) {
        DiagMissingBody("main function", "", lastToken.End());
    }
    ParseFuncDeclAnnos(annos, *ret);
    auto attrs = CheckDeclModifiers(modifiers, scopeKind, DefKind::MAIN);
    if (Utils::In(Attribute::UNSAFE, attrs)) {
        ret->EnableAttr(Attribute::UNSAFE);
        SetUnsafe(ret.get(), modifiers);
    }
    return ret;
}

OwnedPtr<FuncDecl> ParserImpl::ParseFuncDecl(
    ScopeKind scopeKind, const std::set<Modifier>& modifiers, PtrVector<Annotation> annos)
{
    OwnedPtr<FuncDecl> ret = MakeOwned<FuncDecl>();
    ChainScope cs(*this, ret.get());

    ret->keywordPos = lookahead.Begin();
    ret->begin = lookahead.Begin();
    Next();
    if (forImport && scopeKind == ScopeKind::CLASS_BODY && HasModifier(modifiers, TokenKind::MUT)) {
        // For import: funcdecl with Mut modifier only allow in interface.
        scopeKind = ScopeKind::INTERFACE_BODY;
    }
    auto attrs = CheckDeclModifiers(modifiers, scopeKind, DefKind::FUNC);
    for (auto& it : attrs) {
        ret->EnableAttr(it);
    }
    if (ret->TestAttr(Attribute::OPERATOR)) {
        ret->identifier = ExpectOperatorIdentifier(*ret);
    } else {
        ret->identifier = ExpectIdentifierWithPos(*ret);
    }
    if (ret->identifier.Begin() == INVALID_POSITION) {
        ret->identifier.SetPos(lookahead.Begin(), lookahead.End());
    }
    ret->modifiers.insert(modifiers.begin(), modifiers.end());
    ret->funcBody = ParseFuncBody(scopeKind);
    bool hasNoBody = !ret->funcBody->body;
    bool hasNoReturnType = !ret->funcBody->retType;
    if (ret->TestAttr(Attribute::COMMON) && hasNoReturnType && hasNoBody) {
        ParseDiagnoseRefactor(DiagKindRefactor::parse_common_function_must_have_return_type, *ret);
    }
    if (ret->TestAttr(Attribute::PLATFORM) && hasNoReturnType && hasNoBody) {
        ParseDiagnoseRefactor(DiagKindRefactor::parse_platform_function_must_have_return_type, *ret);
    }
    if (HasModifier(modifiers, TokenKind::UNSAFE) || HasModifier(modifiers, TokenKind::FOREIGN)) {
        SetUnsafe(ret.get(), modifiers);
    }
    ffiParser->CheckFuncSignature(*ret, annos);
    ParseFuncDeclAnnos(annos, *ret);
    CheckFuncBody(scopeKind, *ret);
    if (ret->funcBody) {
        ret->end = ret->funcBody->end;
        // The Curring function is not supported in spec 0.22.
        if (ret->funcBody->paramLists.size() > 1) {
            ParseDiagnoseRefactor(
                DiagKindRefactor::parse_expected_left_brace, ret->funcBody->paramLists[1]->begin, std::string("("));
        }
        if (ret->identifier == "main" && !ret->identifier.IsRaw()) {
            DiagIllegalFunc(ret);
        }
    }
    SetDefaultFunc(scopeKind, *ret);
    if (scopeKind != ScopeKind::CLASS_BODY && scopeKind != ScopeKind::INTERFACE_BODY) {
        ffiParser->CheckForeignNameAnnotation(*ret);
    }
    if (scopeKind != ScopeKind::CLASS_BODY) {
        ffiParser->ObjC().CheckInitAnnotation(*ret);
    }
    return ret;
}

void ParserImpl::CheckFuncBody(ScopeKind scopeKind, FuncDecl& decl)
{
    auto& fb = decl.funcBody;
    if (fb && fb->generic) {
        decl.EnableAttr(Attribute::GENERIC);
    }
    if (decl.TestAttr(Attribute::INTRINSIC)) {
        if (scopeKind != ScopeKind::TOPLEVEL && scopeKind != ScopeKind::UNKNOWN_SCOPE) {
            ParseDiagnoseRefactor(
                DiagKindRefactor::parse_intrinsic_function_must_be_toplevel, MakeRange(decl.begin, lastToken.End()));
        }
        if (fb && fb->body) {
            ParseDiagnoseRefactor(DiagKindRefactor::parse_intrinsic_function_cannot_have_body, *fb->body);
        }
    }

    auto const inClassLikeScope = scopeKind == ScopeKind::CLASS_BODY || scopeKind == ScopeKind::INTERFACE_BODY;

    /*
        Check here if a function could be abstract,
        and when the outerDecl will be known - clarify it for mirrors / common declarations
     */
    if (fb && !fb->body && !parseDeclFile && scopeKind != ScopeKind::UNKNOWN_SCOPE
        && !decl.TestAnyAttr(Attribute::FOREIGN, Attribute::INTRINSIC, Attribute::OBJ_C_MIRROR)) {
        if (CanBeAbstract(decl, scopeKind)) {
            decl.EnableAttr(Attribute::ABSTRACT);
        } else if (!fb->TestAttr(Attribute::HAS_BROKEN) && !inClassLikeScope && !decl.TestAttr(Attribute::COMMON)) {
            DiagMissingBody("function",
                !decl.identifier.Valid() ? "" : " '" + decl.identifier + "'", lastToken.End());
        }
    }

    if (decl.TestAnyAttr(Attribute::FOREIGN)) {
        if (decl.TestAttr(Attribute::GENERIC)) {
            ParseDiagnoseRefactor(DiagKindRefactor::parse_foreign_func_should_not_be_generic, *fb->generic);
            decl.EnableAttr(Attribute::IS_BROKEN);
        }
        if (!fb->retType) {
            auto foreignPos = std::find_if(decl.modifiers.begin(), decl.modifiers.end(),
                [](const Modifier& it) { return it.modifier == TokenKind::FOREIGN; });
            CJC_ASSERT(foreignPos != decl.modifiers.end());
            ParseDiagnoseRefactor(DiagKindRefactor::parse_foreign_func_must_declare_return_type,
                MakeRange(foreignPos->begin, decl.keywordPos + 4)); // Keyword func's length is 4.
            decl.EnableAttr(Attribute::IS_BROKEN);
        }
        if (fb->body) {
            ParseDiagnoseRefactor(DiagKindRefactor::parse_foreign_function_with_body, *fb->body);
            decl.EnableAttr(Attribute::IS_BROKEN);
        }
    }
    CheckVariableParams(decl);
}

void ParserImpl::FFICheckClassLikeFuncBody(
    FuncDecl& decl,
    DiagKindRefactor functionMustHaveReturnType,
    DiagKindRefactor functionCanNotHaveBody)
{
    if (!decl.funcBody) {
        return;
    }
    if (!decl.funcBody->retType && !decl.funcBody->TestAttr(Attribute::HAS_BROKEN)) {
        ParseDiagnoseRefactor(functionMustHaveReturnType, decl);
        decl.EnableAttr(Attribute::IS_BROKEN);
    }
    if (decl.funcBody->body) {
        ParseDiagnoseRefactor(functionCanNotHaveBody, decl, decl.identifier);
        decl.EnableAttr(Attribute::IS_BROKEN);
    }
}

void ParserImpl::CheckClassLikeFuncBodyAbstractness(FuncDecl& decl)
{
    bool isCommon = decl.TestAttr(Attribute::COMMON);
    auto outerModifiers = decl.outerDecl->modifiers;
    bool inAbstract = HasModifier(outerModifiers, TokenKind::ABSTRACT);
    bool inCJMP = HasModifier(outerModifiers, TokenKind::PLATFORM) || HasModifier(outerModifiers, TokenKind::COMMON);
    bool inAbstractCJMP = inAbstract && inCJMP;
    bool inObjCMirror = decl.outerDecl->TestAttr(Attribute::OBJ_C_MIRROR);

    bool isJavaMirrorOrJavaMirrorSubtype =
        decl.outerDecl->TestAnyAttr(Attribute::JAVA_MIRROR, Attribute::JAVA_MIRROR_SUBTYPE);
    // explicit ABSTRACT modifier allowed only in COMMON ABSTRACT class or in JFFI
    if (HasModifier(decl.modifiers, TokenKind::ABSTRACT) && !isCommon && !inAbstractCJMP &&
        !isJavaMirrorOrJavaMirrorSubtype) {
        ParseDiagnoseRefactor(DiagKindRefactor::parse_explicitly_abstract_only_for_cjmp_abstract_class,
            decl, "function");
    }

    if (decl.funcBody && decl.funcBody->body) {
        return;
    }

    CJC_NULLPTR_CHECK(decl.outerDecl);
    if (decl.TestAttr(Attribute::CONSTRUCTOR)) {
        return;
    }

    if (inObjCMirror) {
        decl.DisableAttr(Attribute::ABSTRACT);
        return;
    }

    bool inClass = decl.outerDecl->astKind == ASTKind::CLASS_DECL;
    bool inAbstractCJMPClass = inAbstractCJMP && inClass;

    if (inAbstractCJMP && !HasModifier(decl.modifiers, TokenKind::ABSTRACT)) {
        // OPEN func without ABSTRACT or COMMON must have body in CJMP ABSTRACT class
        if (HasModifier(decl.modifiers, TokenKind::OPEN) && inAbstractCJMP) {
            DiagMissingBody("function", !decl.identifier.Valid() ? "" : " '" + decl.identifier + "'", lastToken.End());
        }
        decl.DisableAttr(Attribute::ABSTRACT);
    }

    bool hasAbstractModifier = HasModifier(decl.modifiers, TokenKind::ABSTRACT);
    
    if (isJavaMirrorOrJavaMirrorSubtype) {
        if (ffiParser->Java().IsAbstractFunction(decl, *decl.outerDecl)) {
            decl.EnableAttr(Attribute::ABSTRACT);
            return;
        } else if (Interop::Java::IsMirror(*decl.outerDecl)) {
            decl.DisableAttr(Attribute::ABSTRACT);
        }
    }

    if (hasAbstractModifier && !inAbstractCJMPClass) {
        Ptr<const Modifier> abstractMod = nullptr;
        for (auto& modifier : decl.modifiers) {
            if (modifier.modifier == TokenKind::ABSTRACT) {
                abstractMod = Ptr(&modifier);
            }
        }
        CJC_NULLPTR_CHECK(abstractMod);
        ChainScope cs(*this, Ptr(&decl));
        DiagIllegalModifierInScope(*abstractMod);
        return;
    }

    if (isJavaMirrorOrJavaMirrorSubtype) {
        return;
    }

    if (isCommon && decl.outerDecl->astKind == ASTKind::INTERFACE_DECL) {
        decl.EnableAttr(Attribute::ABSTRACT);
        return;
    }
    if (decl.TestAnyAttr(Attribute::FOREIGN, Attribute::INTRINSIC)) {
        return;
    }

    auto& fb = decl.funcBody;
    if (fb->TestAttr(Attribute::HAS_BROKEN)) {
        return;
    }
    if (decl.TestAttr(Attribute::ABSTRACT) || (!decl.funcBody->body && !decl.TestAttr(Attribute::FINALIZER))) {
        if (!decl.funcBody->retType) {
            ParseDiagnoseRefactor(DiagKindRefactor::parse_abstract_func_must_have_return_type, lastToken.End());
        }
    } else if (!isCommon) {
        decl.DisableAttr(Attribute::ABSTRACT);
        DiagMissingBody("function", !decl.identifier.Valid() ? "" : " '" + decl.identifier + "'", lastToken.End());
    }
}

void ParserImpl::CheckVariableParams(AST::FuncDecl& decl)
{
    if (!decl.funcBody || decl.funcBody->paramLists.size() != 1) {
        return;
    }
    auto& params = decl.funcBody->paramLists[0]->params;
    std::optional<size_t> varargIdx = std::nullopt;
    for (size_t index = 0; index < params.size(); index++) {
        if (params[index]->identifier == ELLIPSIS) {
            decl.hasVariableLenArg = true;
            decl.variadicArgIndex = index;
            // For fmt, variable parameter is erased.
            decl.funcBody->paramLists[0]->hasVariableLenArg = true;
            decl.funcBody->paramLists[0]->variadicArgIndex = index;

            if (!decl.TestAttr(Attribute::FOREIGN)) {
                ParseDiagnoseRefactor(DiagKindRefactor::parse_variable_length_parameter_only_in_the_foreign_function,
                    MakeRange(decl.keywordPos, decl.identifier.End()));
                decl.EnableAttr(Attribute::IS_BROKEN);
                break;
            }

            if (index == 0) {
                ParseDiagnoseRefactor(
                    DiagKindRefactor::parse_variable_length_parameter_can_not_be_first, *params[index]);
                decl.EnableAttr(Attribute::IS_BROKEN);
            } else if (index != params.size() - 1) {
                ParseDiagnoseRefactor(
                    DiagKindRefactor::parse_variable_length_parameter_must_in_the_end, *params[index]);
                decl.EnableAttr(Attribute::IS_BROKEN);
            } else {
                varargIdx = {index};
            }
        }
    }
    if (varargIdx) {
        auto nodeIter = decl.funcBody->paramLists[0]->params.begin() + static_cast<std::ptrdiff_t>(*varargIdx);
        decl.funcBody->paramLists[0]->params.erase(nodeIter);
    }
}

void ParserImpl::CheckVariableParams(AST::PrimaryCtorDecl& decl)
{
    if (!decl.funcBody || decl.funcBody->paramLists.size() != 1) {
        return;
    }
    auto& params = decl.funcBody->paramLists[0]->params;
    for (size_t index = 0; index < params.size(); index++) {
        if (params[index]->identifier == ELLIPSIS) {
            decl.hasVariableLenArg = true;
            // For fmt, variable parameter is erased.
            decl.funcBody->paramLists[0]->hasVariableLenArg = true;
            decl.funcBody->paramLists[0]->variadicArgIndex = index;

            // Primary constructor is certainly not a foreign function.
            ParseDiagnoseRefactor(DiagKindRefactor::parse_variable_length_parameter_only_in_the_foreign_function,
                MakeRange(decl.keywordPos, decl.identifier.End()));
            decl.EnableAttr(Attribute::IS_BROKEN);
            break;
        }
    }
}

void ParserImpl::ParsePropMemberBody(const ScopeKind& scopeKind, FuncBody& fb)
{
    if (fb.generic) {
        if (scopeKind == ScopeKind::PROP_MEMBER_SETTER_BODY) {
            DiagGetOrSetCannotBeGeneric("getter", *fb.generic);
        }
        if (scopeKind == ScopeKind::PROP_MEMBER_GETTER_BODY) {
            DiagGetOrSetCannotBeGeneric("setter", *fb.generic);
        }
    }
    if (!Seeing(TokenKind::LCURL)) {
        ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_left_brace, lookahead, ConvertToken(lookahead));
        ConsumeUntilDecl(TokenKind::LCURL);
        if (!Seeing(TokenKind::LCURL)) {
            return;
        }
    }
    fb.body = ParseBlock(ScopeKind::FUNC_BODY);
    fb.end = fb.body->end;
}

OwnedPtr<FuncBody> ParserImpl::ParseFuncBody(ScopeKind scopeKind)
{
    OwnedPtr<FuncBody> ret = MakeOwned<FuncBody>();
    ChainScope cs(*this, ret.get());
    ret->begin = lookahead.Begin();
    // main is never generic
    if (scopeKind != ScopeKind::MAIN_BODY && Skip(TokenKind::LT)) {
        ret->generic = ParseGeneric();
    }
    ParseFuncParameters(scopeKind, *ret);
    // To deal with the endPos of the FuncDecl without return type and funcBody.
    // ex: foreign func test(a: Int64)
    // The end pos of the FuncDecl above should be next to ).
    auto paramsEndToken = lastToken;
    if (scopeKind == ScopeKind::PROP_MEMBER_SETTER_BODY || scopeKind == ScopeKind::PROP_MEMBER_GETTER_BODY) {
        ParsePropMemberBody(scopeKind, *ret);
        return ret;
    }
    if (Skip(TokenKind::COLON)) {
        ret->colonPos = lastToken.Begin();
        // `This` type can only be used as the return type of instance member function
        if (scopeKind == ScopeKind::CLASS_BODY && Seeing(TokenKind::THISTYPE)) {
            enableThis = true;
        }
        if (SeeingAny(GetTypeFirst()) || SeeingContextualKeyword()) {
            ret->retType = ParseType();
        } else {
            ret->EnableAttr(Attribute::IS_BROKEN);
            DiagExpectedIdentifierFuncBody(ret.get());
            TryConsumeUntilAny({TokenKind::LCURL, TokenKind::WHERE});
        }

        if (scopeKind == ScopeKind::CLASS_BODY) {
            enableThis = false;
        }
    }
    ParseFuncGenericConstraints(*ret);
    if (Seeing(TokenKind::LCURL)) {
        ret->body = ParseBlock(ScopeKind::FUNC_BODY);
        ret->end = ret->body->end;
    } else if (ret->generic && !ret->generic->genericConstraints.empty()) {
        auto& lastGC = *ret->generic->genericConstraints.cbegin();
        ret->end = lastGC->end;
    } else {
        ret->end = ret->retType ? ret->retType->end : paramsEndToken.End();
    }
    return ret;
}

void ParserImpl::ParseFuncParameters(const ScopeKind& scopeKind, FuncBody& fb)
{
    if (Seeing(TokenKind::LPAREN)) {
        do {
            if (scopeKind == ScopeKind::PRIMARY_CONSTRUCTOR_BODY_FOR_CLASS ||
                scopeKind == ScopeKind::PRIMARY_CONSTRUCTOR_BODY_FOR_STRUCT ||
                scopeKind == ScopeKind::PROP_MEMBER_SETTER_BODY || scopeKind == ScopeKind::PROP_MEMBER_GETTER_BODY) {
                (void)fb.paramLists.emplace_back(ParseParameterList(scopeKind));
            } else {
                (void)fb.paramLists.emplace_back(ParseParameterList());
            }
        } while (Seeing(TokenKind::LPAREN));
    } else {
        (void)fb.paramLists.emplace_back(MakeInvalid<FuncParamList>(lookahead.Begin()));
        ParseDiagnoseRefactor(scopeKind == ScopeKind::MAIN_BODY ? DiagKindRefactor::parse_expected_left_paren :
            DiagKindRefactor::parse_expected_lt_paren, lookahead, ConvertToken(lookahead));
        fb.EnableAttr(Attribute::HAS_BROKEN);
    }
}

void ParserImpl::ParseFuncGenericConstraints(const FuncBody& fb)
{
    if (Skip(TokenKind::WHERE)) {
        if (!fb.generic) {
            DiagUnexpectedWhere(lastToken);
        } else {
            fb.generic->genericConstraints = ParseGenericConstraints();
        }
    } else if (fb.generic && (Seeing(TokenKind::IDENTIFIER))) {
        ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_where_brace, lookahead, ConvertToken(lookahead));
    }
}

void ParserImpl::ParseEllipsisParameter(FuncParam& funcParam)
{
    funcParam.identifier = std::string{ELLIPSIS};
    Next();
    funcParam.identifier.SetPos(lookahead.Begin(), lookahead.End());
    funcParam.begin = lookahead.Begin();
    funcParam.end = lookahead.End();
    funcParam.isVar = false;
    funcParam.end = lookahead.End();
}

void ParserImpl::ParseParameter(ScopeKind scopeKind, FuncParam& fp)
{
    Position memberStartPos;
    ParseLetOrVarInParamList(scopeKind, fp, memberStartPos);
    if (Seeing(TokenKind::ELLIPSIS)) {
        ParseEllipsisParameter(fp);
        return;
    }
    // process of '_'. And wildcard is not allowed in primary constructor let/var
    if (Skip(TokenKind::WILDCARD) && fp.hasLetOrVar) {
        ParseDiagnoseRefactor(DiagKindRefactor::parse_wildcard_can_not_be_used_as_member_name, lastToken);
    }
    if (lastToken.kind == TokenKind::WILDCARD) {
        fp.identifier = lastToken.Value();
        fp.identifier.SetPos(lastToken.Begin(), lastToken.End());
    } else {
        fp.identifier = ExpectIdentifierWithPos(fp);
    }
    fp.begin = lookahead.Begin();
    if (fp.identifier == INVALID_IDENTIFIER) {
        fp.type = MakeOwned<InvalidType>(lookahead.Begin());
        fp.assignment = MakeOwned<InvalidExpr>(lookahead.Begin());
        fp.end = lookahead.End();
        fp.EnableAttr(Attribute::HAS_BROKEN);
        fp.EnableAttr(Attribute::IS_BROKEN);
        return;
    }
    if (scopeKind == ScopeKind::PROP_MEMBER_SETTER_BODY || scopeKind == ScopeKind::PROP_MEMBER_GETTER_BODY) {
        fp.end = lastToken.End();
    } else {
        if (Skip(TokenKind::NOT)) {
            if (fp.identifier == "_") {
                // add diagnose of '_!'
                auto builder = ParseDiagnoseRefactor(
                    DiagKindRefactor::parse_expected_character, lastToken, "\':\'", lastToken.Value());
                builder.AddMainHintArguments("\':\'");
            }
            fp.notMarkPos = lastToken.Begin();
            fp.isNamedParam = true;
        }
        if (!Skip(TokenKind::COLON) && !fp.TestAttr(Attribute::HAS_BROKEN)) {
            DiagExpectCharacter("':'");
            fp.type = MakeOwned<InvalidType>(lookahead.Begin());
            fp.end = fp.type->end;
            fp.EnableAttr(Attribute::HAS_BROKEN);
            fp.EnableAttr(Attribute::IS_BROKEN);
            return;
        }
        fp.colonPos = lastToken.Begin();
        fp.type = ParseType();
        fp.end = fp.type->end;
        ParseAssignInParam(fp);
    }
    fp.begin = fp.isMemberParam ? memberStartPos : fp.begin;
}

void ParserImpl::ParseAssignInParam(FuncParam& fp)
{
    if (Skip(TokenKind::ASSIGN)) {
        if (!fp.isNamedParam) {
            ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_dot_lparen, lastToken, ConvertToken(lastToken));
        }
        fp.assignPos = lastToken.Begin();
        fp.assignment = ParseExpr();
        fp.end = fp.assignment->end;
    }
}

OwnedPtr<FuncParamList> ParserImpl::ParseParameterList(ScopeKind scopeKind)
{
    if (!Skip(TokenKind::LPAREN)) {
        ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_left_paren, lookahead, ConvertToken(lookahead));
        return MakeInvalid<FuncParamList>(lookahead.Begin());
    }
    OwnedPtr<FuncParamList> ret = MakeOwned<FuncParamList>();
    ChainScope cs(*this, ret.get());
    // Parameter with value, i.e. optional parameter must list behind all non-optional parameters.
    Ptr<FuncParam> namedParameter{nullptr};
    Ptr<FuncParam> memberParam{nullptr};
    ret->begin = lookahead.Begin();
    ret->leftParenPos = lookahead.Begin();
    ParseZeroOrMoreSepTrailing([&ret](const Position& pos) { ret->params.back()->commaPos = pos; },
        [&ret, this, scopeKind, &namedParameter, &memberParam]() {
            ret->params.push_back(ParseParamInParamList(scopeKind, namedParameter, memberParam));
        },
        TokenKind::RPAREN);
    if (!Skip(TokenKind::RPAREN)) {
        DiagExpectedRightDelimiter("(", ret->leftParenPos);
        ret->end = lookahead.End();
        ret->EnableAttr(Attribute::IS_BROKEN);
        return ret;
    }
    if (scopeKind == ScopeKind::PROP_MEMBER_SETTER_BODY) {
        if (ret->params.empty()) {
            ParseDiagnoseRefactor(
                DiagKindRefactor::parse_setter_must_contain_one_parameter, MakeRange(ret->begin, lastToken.End()));
        } else if (ret->params.size() > 1) {
            ParseDiagnoseRefactor(
                DiagKindRefactor::parse_setter_can_only_accept_one_parameter, MakeRange(ret->begin, lastToken.End()));
        }
    }
    ret->rightParenPos = lastToken.Begin();
    ret->end = lastToken.End();
    return ret;
}

OwnedPtr<FuncParam> ParserImpl::ParseParamInParamList(
    const ScopeKind& scopeKind, Ptr<AST::FuncParam>& meetNamedParameter, Ptr<AST::FuncParam>& meetMemberParams)
{
    // For the situation: func f(a: Int64, @M(), @M(), ...)
    // or func f(a: Int64, @!M() b: Int32, ...)
    if (SeeingMacroCallDecl() && !this->enableCustomAnno) {
        return ParseMacroCall<MacroExpandParam>(scopeKind);
    }
    OwnedPtr<FuncParam> param = MakeOwned<FuncParam>();
    ChainScope cs(*this, param.get());
    ParseAnnotations(param->annotations);
    ParseModifiers(param->modifiers);
    ParseParameter(scopeKind, *param);
    // Check named parameter rule.
    if (meetNamedParameter && !param->isNamedParam) {
        DiagNamedParameterAfterUnnamed(*param, *meetNamedParameter);
    }
    // Check member variable parameter rule.
    if (meetMemberParams && !param->isMemberParam) {
        DiagMemberParameterAfterRegular(*param, *meetMemberParams);
    }
    if (param->isNamedParam && !meetNamedParameter) {
        meetNamedParameter = param.get();
    }
    if (param->isMemberParam && !meetMemberParams) {
        meetMemberParams = param.get();
    }
    CheckModifierInParamList(scopeKind, param->modifiers, param->isMemberParam, *param);
    if (Seeing(TokenKind::COMMA)) {
        param->commaPos = lookahead.Begin();
    }
    SetDeclBeginPos(*param);

    CheckDeprecationOfFuncParam(*param);

    return param;
}

void ParserImpl::ParseLetOrVarInParamList(const ScopeKind& scopeKind, FuncParam& fp, Position& memberStartPos)
{
    bool isPrimaryConstr = (scopeKind == ScopeKind::PRIMARY_CONSTRUCTOR_BODY_FOR_CLASS) ||
        (scopeKind == ScopeKind::PRIMARY_CONSTRUCTOR_BODY_FOR_STRUCT);
    if (Skip(TokenKind::LET) || Skip(TokenKind::VAR)) {
        fp.hasLetOrVar = true;
        fp.keywordPos = lastToken.Begin();
        if (!isPrimaryConstr) {
            ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_parameter_rp, lastToken, ConvertToken(lastToken));
        } else {
            // Member variable parameter for class/struct main constructor.
            fp.isMemberParam = true;
            fp.isVar = lastToken == "var";
            fp.begin = memberStartPos = lastToken.Begin();
        }
    } else if (HasModifier(fp.modifiers, TokenKind::CONST) && lastToken == "const") {
        std::string scope = isPrimaryConstr ? "primary constructor paramlist" : "paramlist";
        std::string decl = isPrimaryConstr ? "const member variable declaration" : "const variable declaration";
        auto builder = ParseDiagnoseRefactor(DiagKindRefactor::parse_unexpected_declaration_in_scope,
            MakeRange(lastToken.Begin(), lastToken.Begin() + std::string("const").size()), decl, scope);
        builder.AddMainHintArguments(decl);
    }
}
void ParserImpl::CheckModifierInParamList(
    const ScopeKind& scopeKind, const std::set<Modifier>& modifiers, bool isMember, AST::FuncParam& fp)
{
    if (!modifiers.empty()) {
        if (!isMember) {
            if (!fp.TestAttr(Attribute::HAS_BROKEN)) {
                DiagExpectNoModifier(*modifiers.begin());
            }
        } else {
            if (HasModifier(modifiers, TokenKind::CONST)) {
                DiagUnExpectedModifierOnDeclaration(fp);
            }
            auto attrs = CheckDeclModifiers(modifiers, scopeKind, DefKind::VARIABLE);
            for (auto& it : attrs) {
                fp.EnableAttr(it);
            }
        }
    }
}
template <typename T> void ParserImpl::CheckIntrinsicFunc(T& fd)
{
    auto iter = std::find_if(
        intrinsics.begin(), intrinsics.end(), [&fd](auto fd1) { return fd.identifier == fd1->identifier; });
    if (iter != intrinsics.end()) {
        DiagDuplicatedIntrinsicFunc(fd, **iter);
    } else {
        intrinsics.emplace(&fd);
    }
}
