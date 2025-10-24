// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file contains logics related to Cangjie Native interoperation with Java, Objective-C
 */

#include "../ParserImpl.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Utils.h"

#include "FFIParserImpl.h"

namespace Cangjie {
using namespace AST;

namespace {
bool IsLitString(Ptr<Expr> expr)
{
    if (!expr || expr->astKind != ASTKind::LIT_CONST_EXPR) {
        return false;
    }
    auto lce = DynamicCast<LitConstExpr>(expr);
    if (!lce) {
        return false;
    }

    if (lce->kind != LitConstKind::STRING) {
        return false;
    }

    return true;
}

} // namespace

void DiagConflictingAnnos(DiagnosticEngine &diag, Annotation &first, Annotation &second)
{
    auto minPos = first.begin < second.begin ? first.begin : second.begin;
    auto maxPos = first.end > second.end ? first.end : second.end;

    diag.DiagnoseRefactor(
        DiagKindRefactor::parse_conflict_annotation,
        MakeRange(minPos, maxPos),
        first.identifier, second.identifier);
}

void FFIParserImpl::CheckAnnotationsConflict(const PtrVector<Annotation>& annos) const
{
    std::vector<Ptr<Annotation>> candidates;
    for (auto& it : annos) {
        if (CONFLICTED_FFI_ANNOS.find(it->kind) != CONFLICTED_FFI_ANNOS.end()) {
            candidates.push_back(it.get());
        }
    }

    if (candidates.size() < 2) {
        return;
    }

    // report diag only in relation with first annotation
    auto first = *candidates.begin();
    for (auto conflicted = std::next(candidates.begin()); conflicted != candidates.end(); ++conflicted) {
        DiagConflictingAnnos(p.diag, *first, **conflicted);
    }
}

void FFIParserImpl::CheckAnnotations(const PtrVector<Annotation>& annos, ScopeKind scopeKind) const
{
    for (auto& it : annos) {
        auto& anno = *it;
        switch (it->kind) {
            case AnnotationKind::JAVA_MIRROR:
            case AnnotationKind::JAVA_IMPL: {
                jp.CheckAnnotation(anno);
                break;
            }
            case AnnotationKind::FOREIGN_NAME: {
                CheckForeignNameAnnoArgs(anno);
                CheckForeignNameAnnoTarget(anno);
                break;
            }
            case AnnotationKind::JAVA_HAS_DEFAULT: {
                jp.CheckJavaHasDefaultAnnotation(anno);
                break;
            }
            case AnnotationKind::OBJ_C_MIRROR:
            case AnnotationKind::OBJ_C_IMPL:
            case AnnotationKind::OBJ_C_INIT: {
                op.CheckAnnotation(anno, scopeKind);
                break;
            }
            default: continue;
        }
    }

    CheckAnnotationsConflict(annos);
}

void FFIParserImpl::CheckForeignNameAnnoTarget(const Annotation& anno) const
{
    if (p.SeeingAny({TokenKind::FUNC, TokenKind::PROP, TokenKind::INIT, TokenKind::LET,
        TokenKind::VAR, TokenKind::CONST})) {
        return;
    }

    if (anno.kind == AnnotationKind::FOREIGN_NAME) {
        auto& lah = p.lookahead;
        p.DiagUnexpectedAnnoOn(anno, lah.Begin(), anno.identifier, lah.Value());
    }
}

void FFIParserImpl::CheckForeignNameAnnoArgs(const Annotation& anno) const
{
    static const std::string FOREIGN_NAME_NAME = "@ForeignName";

    if (anno.args.size() != 1 || !IsLitString(anno.args[0]->expr)) {
        p.DiagAnnotationExpectsOneArgument(anno, FOREIGN_NAME_NAME, "'String' literal");
        return;
    }
}

void FFIParserImpl::CheckForeignNameAnnotation(Decl& decl) const
{
    CJC_ASSERT(decl.IsFuncOrProp() || decl.astKind == ASTKind::VAR_DECL);
    for (auto& it : decl.annotations) {
        if (it->kind != AnnotationKind::FOREIGN_NAME) {
            continue;
        }
        if (!decl.outerDecl || !decl.outerDecl->TestAnyAttr(Attribute::JAVA_MIRROR, Attribute::JAVA_MIRROR_SUBTYPE,
            Attribute::OBJ_C_MIRROR, Attribute::OBJ_C_MIRROR_SUBTYPE)) {
                p.diag.DiagnoseRefactor(DiagKindRefactor::parse_foreign_name_on_ffi_decl_member, decl);
                decl.EnableAttr(Attribute::IS_BROKEN);
                return;
        }
    }
}

JFFIParserImpl FFIParserImpl::Java() const
{
    return jp;
}

OCFFIParserImpl FFIParserImpl::ObjC() const
{
    return op;
}

void FFIParserImpl::CheckZeroOrSingleStringLitArgAnnotation(const AST::Annotation &anno,
                                                            const std::string &annotationName) const
{
    if (anno.args.size() == 0) {
        return;
    }

    if (anno.args.size() > 1 || !IsLitString(anno.args[0]->expr)) {
        p.DiagAnnotationMoreThanOneArgs(anno, annotationName, "'String' literal");
        return;
    }
}

void FFIParserImpl::CheckNoArgAnnotation(const AST::Annotation &anno, const std::string &annotationName) const
{
    if (anno.args.empty()) {
        return;
    }

    p.DiagAnnotationShouldNotHaveArgs(anno, annotationName);
}


void FFIParserImpl::CheckClassLikeSignature(AST::ClassLikeDecl& decl, const PtrVector<Annotation>& annos) const
{
    for (auto& anno : annos) {
        switch (anno->kind) {
            case AnnotationKind::JAVA_MIRROR:
                jp.CheckMirrorSignature(decl, annos);
                break;
            case AnnotationKind::JAVA_IMPL:
                jp.CheckImplSignature(decl, annos);
                break;
            case AnnotationKind::OBJ_C_MIRROR:
                op.CheckMirrorSignature(decl, annos);
                break;
            case AnnotationKind::OBJ_C_IMPL:
                op.CheckImplSignature(decl, annos);
                break;
            default: break;
        }
    }
}


void FFIParserImpl::CheckFuncSignature(AST::FuncDecl& decl, const PtrVector<Annotation>& annos) const
{
    for (auto& anno : annos) {
        switch (anno->kind) {
            case AnnotationKind::OBJ_C_MIRROR:
                op.CheckMirrorSignature(decl, annos);
                break;
            default: break;
        }
    }
}


namespace Native::FFI {

} // namespace Native::FFI
} // namespace Cangjie
