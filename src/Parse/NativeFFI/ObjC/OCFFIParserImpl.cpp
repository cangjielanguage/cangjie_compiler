// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements logics related to parsing in Objective-C FFI in Cangjie Native
 */

#include "OCFFIParserImpl.h"
#include "../../ParserImpl.h"
#include "cangjie/AST/AttributePack.h"
#include "cangjie/AST/Node.h"
#include "cangjie/Utils/CastingTemplate.h"
#include "cangjie/AST/Utils.h"

using namespace Cangjie;
using namespace AST;

void OCFFIParserImpl::CheckAnnotation(Annotation& anno, ScopeKind scopeKind) const
{
    switch (anno.kind) {
        case AnnotationKind::OBJ_C_MIRROR: {
            CheckMirrorAnnoArgs(anno);
            CheckMirrorAnnoTarget(anno, scopeKind);
            break;
        }
        case AnnotationKind::OBJ_C_IMPL: {
            CheckImplAnnoArgs(anno);
            CheckImplAnnoTarget(anno);
            break;
        }
        case AnnotationKind::OBJ_C: {
            CheckObjCAnnoArgs(anno);
            CheckObjCAnnoTarget(anno, scopeKind);
            break;
        }
        case AnnotationKind::OBJ_C_INIT: {
            CheckInitAnnoArgs(anno);
            CheckInitAnnoTarget(anno);
            break;
        }
        case AnnotationKind::OBJ_C_OPTIONAL: {
            CheckOptionalAnnoArgs(anno);
            CheckOptionalAnnoTarget(anno);
            break;
        }
        default: CJC_ABORT(); // Unexpected annotation
    }
}

void OCFFIParserImpl::CheckMirrorSignature(ClassLikeDecl& decl, const PtrVector<Annotation>& annos) const
{
    CJC_ASSERT(p.HasAnnotation(annos, AnnotationKind::OBJ_C_MIRROR) ||
        (p.HasAnnotation(annos, AnnotationKind::OBJ_C) && HasModifier(decl, TokenKind::FOREIGN)));
    decl.EnableAttr(Attribute::OBJ_C_FOREIGN);
    decl.EnableAttr(Attribute::NO_REFLECT_INFO);
    
    /*
     * Disable `Attribute::FOREIGN` here to avoid any possible clashes with `@C`.
     * `@ObjC foreign` declarations are marked with `Attribute::OBJ_C_FOREIGN` attribute.
     */
    decl.DisableAttr(Attribute::FOREIGN);

    if (decl.TestAttr(Attribute::SEALED)) {
        DiagObjCMirrorCannotBeSealed(decl);
        decl.EnableAttr(Attribute::IS_BROKEN);
    }
    if (decl.TestAnyAttr(Attribute::COMMON, Attribute::SPECIFIC)) {
        DiagObjCMirrorCannotBeCommonOrSpecific(decl);
        decl.EnableAttr(Attribute::IS_BROKEN);
    }
}

void OCFFIParserImpl::CheckMirrorSignature(FuncDecl& decl, const PtrVector<Annotation>& annos) const
{
    CJC_ASSERT(p.HasAnnotation(annos, AnnotationKind::OBJ_C_MIRROR) || p.HasAnnotation(annos, AnnotationKind::OBJ_C));
    decl.EnableAttr(Attribute::OBJ_C_FOREIGN);
    decl.EnableAttr(Attribute::NO_REFLECT_INFO);

    if (p.HasAnnotation(annos, AnnotationKind::OBJ_C)) {
        if (!decl.TestAttr(Attribute::FOREIGN)) {
            DiagObjCFuncMustBeForeign(decl);
            decl.EnableAttr(Attribute::IS_BROKEN);
        }
    }
    /*
     * Disable `Attribute::FOREIGN` here to avoid clashes with `@C foreign` functions.
     * `@ObjC foreign` functions are marked with `Attribute::OBJ_C_FOREIGN` attribute.
     */
    decl.DisableAttr(Attribute::FOREIGN);

    if (decl.GetGeneric() != nullptr) {
        DiagObjCMirrorFuncCannotBeGeneric(decl);
        decl.EnableAttr(Attribute::IS_BROKEN);
    }

    if (decl.funcBody->body != nullptr) {
        DiagObjCMirrorFuncCannotHaveBody(decl);
        decl.EnableAttr(Attribute::IS_BROKEN);
    }

    if (decl.funcBody->retType == nullptr) {
        DiagObjCMirrorFuncMustHaveExplicitType(decl);
        decl.EnableAttr(Attribute::IS_BROKEN);
    }

    if (decl.isConst || HasModifier(decl.modifiers, TokenKind::CONST)) {
        DiagObjCMirrorFuncCannotBeConst(decl);
        decl.EnableAttr(Attribute::IS_BROKEN);
    }

    if (decl.TestAnyAttr(Attribute::COMMON, Attribute::SPECIFIC)) {
        DiagObjCMirrorCannotBeCommonOrSpecific(decl);
        decl.EnableAttr(Attribute::IS_BROKEN);
    }

    if (decl.outerDecl != nullptr) {
        DiagObjCMirrorFuncMustBeTopLevel(decl);
        decl.EnableAttr(Attribute::IS_BROKEN);
    }
}

void OCFFIParserImpl::CheckImplSignature(ClassLikeDecl& decl, const PtrVector<Annotation>& annos) const
{
    CJC_ASSERT(p.HasAnnotation(annos, AnnotationKind::OBJ_C_IMPL) ||
        (p.HasAnnotation(annos, AnnotationKind::OBJ_C) && !HasModifier(decl, TokenKind::FOREIGN)));
    decl.EnableAttr(Attribute::OBJ_C_EXPORT);
    decl.EnableAttr(Attribute::NO_REFLECT_INFO);

    if (decl.GetGeneric() != nullptr) {
        DiagObjCImplCannotBeGeneric(decl);
        decl.EnableAttr(Attribute::IS_BROKEN);
    }

    if (decl.TestAttr(Attribute::ABSTRACT)) {
        DiagObjCImplCannotBeAbstract(decl);
        decl.EnableAttr(Attribute::IS_BROKEN);
    }

    if (decl.TestAttr(Attribute::SEALED)) {
        DiagObjCImplCannotBeSealed(decl);
        decl.EnableAttr(Attribute::IS_BROKEN);
    }

    if (decl.astKind == ASTKind::INTERFACE_DECL) {
        DiagObjCImplCannotBeInterface(decl);
        decl.EnableAttr(Attribute::IS_BROKEN);
    }

    if (decl.TestAnyAttr(Attribute::COMMON, Attribute::SPECIFIC)) {
        DiagObjCImplCannotBeCommonOrSpecific(decl);
        decl.EnableAttr(Attribute::IS_BROKEN);
    }
}

const static std::string at = "@";

void OCFFIParserImpl::CheckMirrorAnnoArgs(Annotation& anno) const
{
    static const std::string OBJC_MIRROR_NAME = at + ANNO_KIND_TO_NAME.at(AnnotationKind::OBJ_C_MIRROR);
    p.ffiParser->CheckZeroOrSingleStringLitArgAnnotation(anno, OBJC_MIRROR_NAME);
}

void OCFFIParserImpl::CheckImplAnnoArgs(Annotation& anno) const
{
    static const std::string OBJC_IMPL_NAME = at + ANNO_KIND_TO_NAME.at(AnnotationKind::OBJ_C_IMPL);
    p.ffiParser->CheckZeroOrSingleStringLitArgAnnotation(anno, OBJC_IMPL_NAME);
}

void OCFFIParserImpl::CheckObjCAnnoArgs(Annotation& anno) const
{
    static const std::string OBJC_NAME = at + ANNO_KIND_TO_NAME.at(AnnotationKind::OBJ_C);
    p.ffiParser->CheckZeroOrSingleStringLitArgAnnotation(anno, OBJC_NAME);
}

void OCFFIParserImpl::CheckInitAnnoArgs(Annotation& anno) const
{
    static const std::string OBJC_INIT_NAME = at + ANNO_KIND_TO_NAME.at(AnnotationKind::OBJ_C_INIT);
    p.ffiParser->CheckNoArgAnnotation(anno, OBJC_INIT_NAME);
}

void OCFFIParserImpl::CheckOptionalAnnoArgs(Annotation& anno) const
{
    static const std::string OBJC_OPTIONAL_NAME = at + ANNO_KIND_TO_NAME.at(AnnotationKind::OBJ_C_OPTIONAL);
    p.ffiParser->CheckNoArgAnnotation(anno, OBJC_OPTIONAL_NAME);
}

void OCFFIParserImpl::CheckMirrorAnnoTarget(const Annotation& anno, ScopeKind scopeKind) const
{
    // The same semantics as `@ObjC`
    CheckObjCAnnoTarget(anno, scopeKind);
}

void OCFFIParserImpl::CheckImplAnnoTarget(const Annotation& anno) const
{
    if (p.SeeingAny({TokenKind::CLASS, TokenKind::INTERFACE})) {
        return;
    }

    if (anno.kind == AnnotationKind::OBJ_C_IMPL) {
        auto& lah = p.lookahead;
        p.DiagUnexpectedAnnoOn(anno, lah.Begin(), anno.identifier, lah.Value());
    }
}

void OCFFIParserImpl::CheckObjCAnnoTarget(const Annotation& anno, ScopeKind scopeKind) const
{
    if (p.SeeingAny({TokenKind::CLASS, TokenKind::INTERFACE})) {
        return;
    }
    // We cannot check here if it is `@ObjC func` or `@ObjC foreign func`,
    // so "foreign"-ness check appears at `CheckMirrorSignature` function.
    if (p.Seeing(TokenKind::FUNC) && scopeKind == ScopeKind::TOPLEVEL) {
        return;
    }

    auto& lah = p.lookahead;
    auto qualifier = (scopeKind == ScopeKind::TOPLEVEL)? "": "member or local ";
    p.DiagUnexpectedAnnoOn(anno, lah.Begin(), anno.identifier, qualifier + lah.Value());
}


void OCFFIParserImpl::CheckInitAnnoTarget(const Annotation& anno) const
{
    if (p.Seeing(TokenKind::FUNC)) {
        return;
    }

    if (anno.kind == AnnotationKind::OBJ_C_INIT) {
        auto& lah = p.lookahead;
        p.DiagUnexpectedAnnoOn(anno, lah.Begin(), anno.identifier, lah.Value());
    }
}

void OCFFIParserImpl::CheckOptionalAnnoTarget(const Annotation& anno) const
{
    if (p.Seeing(TokenKind::FUNC)) {
        return;
    }

    if (anno.kind == AnnotationKind::OBJ_C_OPTIONAL) {
        auto& lah = p.lookahead;
        p.DiagUnexpectedAnnoOn(anno, lah.Begin(), anno.identifier, lah.Value());
    }
}

void OCFFIParserImpl::CheckOptionalAnnotation(AST::FuncDecl& fd) const
{
    CJC_ASSERT(!fd.TestAttr(Attribute::CONSTRUCTOR));

    for (auto& anno : fd.annotations) {
        if (anno->kind != AnnotationKind::OBJ_C_OPTIONAL) {
            continue;
        }

        if (!fd.outerDecl || fd.outerDecl->astKind != ASTKind::INTERFACE_DECL
            || !fd.outerDecl->TestAttr(Attribute::OBJ_C_FOREIGN)) {
                DiagObjCOptionalFuncMustBeInMirrorClass(fd);
                fd.EnableAttr(Attribute::IS_BROKEN);
        }

        if (!fd.TestAttr(Attribute::IS_BROKEN)) {
            fd.EnableAttr(Attribute::OBJ_C_OPTIONAL);
        }
    }
}

void OCFFIParserImpl::CheckInitAnnotation(AST::FuncDecl& fd) const
{
    CJC_ASSERT(!fd.TestAttr(Attribute::CONSTRUCTOR));

    for (auto& anno : fd.annotations) {
        if (anno->kind != AnnotationKind::OBJ_C_INIT) {
            continue;
        }

        if (!fd.outerDecl || fd.outerDecl->astKind != ASTKind::CLASS_DECL
            || !fd.outerDecl->TestAttr(Attribute::OBJ_C_FOREIGN)) {
                DiagObjCInitFuncMustBeInMirrorClass(fd);
                fd.EnableAttr(Attribute::IS_BROKEN);
        }

        if (!fd.TestAttr(Attribute::STATIC)) {
            DiagObjCInitFuncMustBeStatic(fd);
            fd.EnableAttr(Attribute::IS_BROKEN);
        }

        if (!fd.TestAttr(Attribute::IS_BROKEN)) {
            fd.EnableAttr(Attribute::OBJ_C_INIT);
        }
    }
}
