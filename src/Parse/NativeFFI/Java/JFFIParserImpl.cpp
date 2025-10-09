// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements logics related to parsing in java FFI in Cangjie Native
 */
#include "../../ParserImpl.h"
#include "cangjie/AST/Utils.h"

#include "JFFIParserImpl.h"

using namespace Cangjie;
using namespace AST;

void JFFIParserImpl::CheckAnnotation(const Annotation& anno) const
{
    if (anno.kind == AnnotationKind::JAVA_MIRROR) {
        CheckMirrorAnnoArgs(anno);
        CheckMirrorAnnoTarget(anno);
    } else {
        CJC_ASSERT(anno.kind == AnnotationKind::JAVA_IMPL);
        CheckImplAnnoArgs(anno);
        CheckImplAnnoTarget(anno);
    }
}

void JFFIParserImpl::CheckMirrorAnnoTarget(const Annotation& anno) const
{
    if (p.SeeingAny({TokenKind::CLASS, TokenKind::INTERFACE})) {
        return;
    }

    if (anno.kind == AnnotationKind::JAVA_MIRROR) {
        auto& lah = p.lookahead;
        p.DiagUnexpectedAnnoOn(anno, lah.Begin(), anno.identifier, lah.Value());
    }
}

void JFFIParserImpl::CheckImplAnnoTarget(const Annotation& anno) const
{
    if (p.SeeingAny({TokenKind::CLASS, TokenKind::INTERFACE})) {
        return;
    }

    if (anno.kind == AnnotationKind::JAVA_IMPL) {
        auto& lah = p.lookahead;
        p.DiagUnexpectedAnnoOn(anno, lah.Begin(), anno.identifier, lah.Value());
    }
}

void JFFIParserImpl::CheckMirrorAnnoArgs(const Annotation& anno) const
{
    static const std::string JAVA_MIRROR_NAME = "@JavaMirror";
    p.ffiParser->CheckZeroOrSingleStringLitArgAnnotation(anno, JAVA_MIRROR_NAME);
}

void JFFIParserImpl::CheckImplAnnoArgs(const Annotation& anno) const
{
    static const std::string JAVA_IMPL_NAME = "@JavaImpl";

    p.ffiParser->CheckZeroOrSingleStringLitArgAnnotation(anno, JAVA_IMPL_NAME);
}

void JFFIParserImpl::CheckMirrorSignature(AST::ClassLikeDecl& decl, const PtrVector<Annotation>& annos) const
{
    CJC_ASSERT(p.HasAnnotation(annos, AnnotationKind::JAVA_MIRROR));
    decl.EnableAttr(Attribute::JAVA_MIRROR);
    if (!decl.inheritedTypes.empty()) {
        decl.EnableAttr(Attribute::JAVA_MIRROR_SUBTYPE);
    }
    if (decl.TestAttr(Attribute::SEALED)) {
        DiagJavaMirrorCannotBeSealed(decl);
        decl.EnableAttr(Attribute::IS_BROKEN);
    }
}

void JFFIParserImpl::CheckImplSignature(AST::ClassLikeDecl& decl, const PtrVector<Annotation>& annos) const
{
    CJC_ASSERT(p.HasAnnotation(annos, AnnotationKind::JAVA_IMPL));
    decl.EnableAttr(Attribute::JAVA_MIRROR_SUBTYPE);

    if (decl.GetGeneric() != nullptr) {
        DiagJavaImplCannotBeGeneric(decl);
        decl.EnableAttr(Attribute::IS_BROKEN);
    }

    if (decl.TestAttr(Attribute::ABSTRACT)) {
        DiagJavaImplCannotBeAbstract(decl);
        decl.EnableAttr(Attribute::IS_BROKEN);
    }

    if (decl.TestAttr(Attribute::SEALED)) {
        DiagJavaImplCannotBeSealed(decl);
        decl.EnableAttr(Attribute::IS_BROKEN);
    }

    if (decl.astKind == ASTKind::CLASS_DECL && decl.TestAttr(Attribute::OPEN)) {
        p.diag.DiagnoseRefactor(DiagKindRefactor::parse_java_impl_cannot_be_open, decl);
        decl.EnableAttr(Attribute::IS_BROKEN);
    } else if (decl.astKind == ASTKind::INTERFACE_DECL) {
        p.diag.DiagnoseRefactor(DiagKindRefactor::parse_java_impl_cannot_be_interface, decl);
        decl.EnableAttr(Attribute::IS_BROKEN);
    }
}