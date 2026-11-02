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
#include "cangjie/AST/AttributePack.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Utils/CheckUtils.h"

#include "JFFIParserImpl.h"

using namespace Cangjie;
using namespace AST;

void JFFIParserImpl::CheckAnnotation(Annotation& anno) const
{
    switch (anno.kind) {
        case AnnotationKind::JAVA_MIRROR:
            CheckMirrorAnnoArgs(anno);
            CheckMirrorAnnoTarget(anno);
            break;
        case AnnotationKind::JAVA_IMPL:
            CheckImplAnnoArgs(anno);
            CheckImplAnnoTarget(anno);
            break;
        case AnnotationKind::JAVA:
            CheckJavaAnnoArgs(anno);
            CheckJavaAnnoTarget(anno);
            break;
        case AnnotationKind::JAVA_HAS_DEFAULT:
            CheckJavaHasDefaultAnnoArgs(anno);
            CheckJavaHasDefaultAnnoTarget(anno);
            break;
        default: CJC_ABORT();
    }
}

void JFFIParserImpl::CheckMirrorAnnoTarget(Annotation& anno) const
{
    // The same semantics as for `@Java`
    CheckJavaAnnoTarget(anno);
}

void JFFIParserImpl::CheckImplAnnoTarget(Annotation& anno) const
{
    // The same semantics as for `@Java`
    CheckJavaAnnoTarget(anno);
}

void JFFIParserImpl::CheckJavaAnnoTarget(Annotation& anno) const
{
    if (p.SeeingAny({TokenKind::CLASS, TokenKind::INTERFACE})) {
        return;
    }

    auto& lah = p.lookahead;
    p.DiagUnexpectedAnnoOn(anno, lah.Begin(), anno.identifier, lah.Value());
    anno.EnableAttr(Attribute::IS_BROKEN);
}

const static std::string at = "@";

void JFFIParserImpl::CheckMirrorAnnoArgs(Annotation& anno) const
{
    static const std::string JAVA_MIRROR_NAME = at + ANNO_KIND_TO_NAME.at(AnnotationKind::JAVA_MIRROR);
    p.ffiParser->CheckZeroOrSingleStringLitArgAnnotation(anno, JAVA_MIRROR_NAME);
}

void JFFIParserImpl::CheckImplAnnoArgs(Annotation& anno) const
{
    static const std::string JAVA_IMPL_NAME = at + ANNO_KIND_TO_NAME.at(AnnotationKind::JAVA_IMPL);
    p.ffiParser->CheckZeroOrSingleStringLitArgAnnotation(anno, JAVA_IMPL_NAME);
}

void JFFIParserImpl::CheckJavaAnnoArgs(Annotation& anno) const
{
    static const std::string JAVA_NAME = at + ANNO_KIND_TO_NAME.at(AnnotationKind::JAVA);
    p.ffiParser->CheckZeroOrSingleStringLitArgAnnotation(anno, JAVA_NAME);
}

void JFFIParserImpl::CheckMirrorSignature(AST::ClassLikeDecl& decl, const PtrVector<Annotation>& annos) const
{
    CJC_ASSERT(p.HasAnnotation(annos, AnnotationKind::JAVA_MIRROR) ||
        (p.HasAnnotation(annos, AnnotationKind::JAVA) && HasModifier(decl, TokenKind::FOREIGN)));
    decl.MarkAsJavaForeign();
    decl.EnableAttr(Attribute::NO_REFLECT_INFO);
    
    /*
     * Disable `Attribute::FOREIGN` here to avoid any possible clashes with `@C`.
     * `@Java foreign` declarations are marked with `Attribute::JAVA_FOREIGN` attribute.
     */
    decl.DisableAttr(Attribute::FOREIGN);
    
    if (decl.TestAttr(Attribute::SEALED)) {
        DiagJavaMirrorCannotBeSealed(decl);
        decl.EnableAttr(Attribute::IS_BROKEN);
    }

    if (decl.TestAnyAttr(Attribute::COMMON, Attribute::SPECIFIC)) {
        DiagJavaMirrorCannotBeCommonOrSpecific(decl);
        decl.EnableAttr(Attribute::IS_BROKEN);
    }
}

void JFFIParserImpl::CheckImplSignature(AST::ClassLikeDecl& decl, const PtrVector<Annotation>& annos) const
{
    CJC_ASSERT(p.HasAnnotation(annos, AnnotationKind::JAVA_IMPL) ||
        (p.HasAnnotation(annos, AnnotationKind::JAVA) && !HasModifier(decl, TokenKind::FOREIGN)));
    decl.MarkAsJavaExport();
    decl.EnableAttr(Attribute::NO_REFLECT_INFO);

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

    if (decl.TestAnyAttr(Attribute::COMMON, Attribute::SPECIFIC)) {
        DiagJavaImplCannotBeCommonOrSpecific(decl);
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

void JFFIParserImpl::CheckJavaHasDefaultAnnoTarget(Annotation& anno) const
{
    if (p.Seeing(TokenKind::FUNC)) {
        return;
    }
    auto& lah = p.lookahead;
    p.DiagUnexpectedAnnoOn(anno, lah.Begin(), anno.identifier, lah.Value());
    anno.EnableAttr(Attribute::IS_BROKEN);
}

void JFFIParserImpl::CheckJavaHasDefaultAnnoArgs(Annotation& anno) const
{
    static const std::string JAVA_HAS_DEFAULT_NAME = at + ANNO_KIND_TO_NAME.at(AnnotationKind::JAVA_HAS_DEFAULT);
    p.ffiParser->CheckNoArgAnnotation(anno, JAVA_HAS_DEFAULT_NAME);
}

bool JFFIParserImpl::IsAbstractFunction(const FuncDecl& fd, const Decl& outerDecl) const
{
    auto hasAbstractModifier = HasModifier(fd.modifiers, TokenKind::ABSTRACT);
    auto hasStaticModifier = HasModifier(fd.modifiers, TokenKind::STATIC);
    auto hasOuterDeclAbstractModifier = HasModifier(outerDecl.modifiers, TokenKind::ABSTRACT);
    auto isOuterDeclInterface = outerDecl.astKind == ASTKind::INTERFACE_DECL;
    auto isOuterDeclClass = outerDecl.astKind == ASTKind::CLASS_DECL;
    auto isOuterDeclJavaImpl = Interop::Java::IsImpl(outerDecl);
    auto isAbstractInsideAbsractClass = hasAbstractModifier && isOuterDeclClass && hasOuterDeclAbstractModifier;
    auto isAbstractInsideInterface = isOuterDeclInterface && !hasStaticModifier;

    return (isAbstractInsideAbsractClass || isAbstractInsideInterface) && !isOuterDeclJavaImpl;
}
