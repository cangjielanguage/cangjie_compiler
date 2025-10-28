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
#include "cangjie/Utils/CastingTemplate.h"
#include "cangjie/AST/Utils.h"

using namespace Cangjie;
using namespace AST;

void OCFFIParserImpl::CheckAnnotation(const Annotation& anno, ScopeKind scopeKind) const
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
        case AnnotationKind::OBJ_C_INIT: {
            CheckInitAnnoArgs(anno);
            CheckInitAnnoTarget(anno);
            break;
        }
        default: CJC_ABORT(); // Unexpected annotation
    }
}

void OCFFIParserImpl::CheckMirrorSignature(ClassLikeDecl& decl, const PtrVector<Annotation>& annos) const
{
    CJC_ASSERT(p.HasAnnotation(annos, AnnotationKind::OBJ_C_MIRROR));
    decl.EnableAttr(Attribute::OBJ_C_MIRROR);

    if (decl.TestAttr(Attribute::SEALED)) {
        DiagObjCMirrorCannotBeSealed(decl);
        decl.EnableAttr(Attribute::IS_BROKEN);
    }
}

void OCFFIParserImpl::CheckMirrorSignature(FuncDecl& decl, const PtrVector<Annotation>& annos) const
{
    CJC_ASSERT(p.HasAnnotation(annos, AnnotationKind::OBJ_C_MIRROR));
    decl.EnableAttr(Attribute::OBJ_C_MIRROR);

    if (decl.TestAttr(Attribute::FOREIGN)) {
        DiagObjCMirrorFuncCannotBeForeign(decl);
        decl.EnableAttr(Attribute::IS_BROKEN);
    }

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

    if (decl.isConst || p.HasModifier(decl.modifiers, TokenKind::CONST)) {
        DiagObjCMirrorFuncCannotBeConst(decl);
        decl.EnableAttr(Attribute::IS_BROKEN);
    }

    if (decl.outerDecl != nullptr) {
        DiagObjCMirrorFuncMustBeTopLevel(decl);
        decl.EnableAttr(Attribute::IS_BROKEN);
    }
}

void OCFFIParserImpl::CheckImplSignature(ClassLikeDecl& decl, const PtrVector<Annotation>& annos) const
{
    CJC_ASSERT(p.HasAnnotation(annos, AnnotationKind::OBJ_C_IMPL));
    decl.EnableAttr(Attribute::OBJ_C_MIRROR_SUBTYPE);

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

    if (decl.astKind == ASTKind::CLASS_DECL && decl.TestAttr(Attribute::OPEN)) {
        DiagObjCImplCannotBeOpen(decl);
        decl.EnableAttr(Attribute::IS_BROKEN);
    }

    if (decl.astKind == ASTKind::INTERFACE_DECL) {
        DiagObjCImplCannotBeInterface(decl);
        decl.EnableAttr(Attribute::IS_BROKEN);
    }
}

void OCFFIParserImpl::CheckMirrorAnnoArgs(const Annotation& anno) const
{
    static const std::string OBJC_MIRROR_NAME = "@ObjCMirror";
    p.ffiParser->CheckZeroOrSingleStringLitArgAnnotation(anno, OBJC_MIRROR_NAME);
}

void OCFFIParserImpl::CheckImplAnnoArgs(const Annotation& anno) const
{
    static const std::string OBJC_IMPL_NAME = "@ObjCImpl";
    p.ffiParser->CheckZeroOrSingleStringLitArgAnnotation(anno, OBJC_IMPL_NAME);
}

void OCFFIParserImpl::CheckInitAnnoArgs(const Annotation& anno) const
{
    static const std::string OBJC_INIT_NAME = "@ObjCInit";
    p.ffiParser->CheckNoArgAnnotation(anno, OBJC_INIT_NAME);
}

void OCFFIParserImpl::CheckMirrorAnnoTarget(const Annotation& anno, ScopeKind scopeKind) const
{
    if (p.SeeingAny({TokenKind::CLASS, TokenKind::INTERFACE})) {
        return;
    }
    if (p.Seeing(TokenKind::FUNC) && scopeKind == ScopeKind::TOPLEVEL) {
        return;
    }

    if (anno.kind == AnnotationKind::OBJ_C_MIRROR) {
        auto& lah = p.lookahead;
        auto qualifier = (scopeKind == ScopeKind::TOPLEVEL)? "": "member or local ";
        p.DiagUnexpectedAnnoOn(anno, lah.Begin(), anno.identifier, qualifier + lah.Value());
    }
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

void OCFFIParserImpl::CheckInitAnnotation(AST::FuncDecl& fd) const
{
    CJC_ASSERT(!fd.TestAttr(Attribute::CONSTRUCTOR));

    for (auto& anno : fd.annotations) {
        if (anno->kind != AnnotationKind::OBJ_C_INIT) {
            continue;
        }

        if (!fd.outerDecl || fd.outerDecl->astKind != ASTKind::CLASS_DECL
            || !fd.outerDecl->TestAttr(Attribute::OBJ_C_MIRROR)) {
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
