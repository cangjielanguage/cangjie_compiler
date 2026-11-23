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

#include "FFIParserImpl.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <unordered_set>
#include <vector>

#include "../ParserImpl.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Utils.h"

#include "cangjie/Lex/Token.h"

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

void DiagConflictingAnnos(DiagnosticEngine &diag, Annotation &first, Annotation &second, const std::string& onStr)
{
    auto minPos = first.begin < second.begin ? first.begin : second.begin;
    auto maxPos = first.end > second.end ? first.end : second.end;

    diag.DiagnoseRefactor(
        DiagKindRefactor::parse_conflict_annotation,
        MakeRange(minPos, maxPos),
        first.identifier, second.identifier, onStr);
}

// Before: an inherited annotation kind. After result: only ARKTS may remain
// a separate default layer beside member options. This predicate changes no AST
// and does not permit repeated explicit @ArkTS annotations on one declaration.
bool FFIParserImpl::AllowsInheritedAnnotationOptions(AnnotationKind kind) const
{
    return kind == AnnotationKind::ARKTS;
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
        if (first->identifier == (*conflicted)->identifier) {
            // duplicated builtin annotations are reported with a different diagnostic
            continue;
        }
        // An ArkTS conflict without --experimental belongs to the Sema gate.
        if (!p.experimentalMode &&
            (first->kind == AnnotationKind::ARKTS || (*conflicted)->kind == AnnotationKind::ARKTS)) {
            continue;
        }
        auto& lah = p.lookahead;
        DiagConflictingAnnos(p.diag, *first, **conflicted, lah.Value());
    }
}

void FFIParserImpl::CheckForeignBlockAnnotations(PtrVector<Annotation>& annos) const
{
    // The block option rules are ArkTS-only. Without --experimental the Sema
    // gate owns the refusal, so no parser option diagnostic may pre-empt it.
    if (!p.experimentalMode) {
        return;
    }
    // A block annotation is cloned into every member, so every block-level
    // rule is applied once here and the refused part is dropped before
    // cloning.
    for (auto it = annos.begin(); it != annos.end();) {
        auto& annotation = *it;
        if (annotation->kind == AnnotationKind::WRITE_ONLY) {
            p.diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_write_only_placement_invalid, *annotation,
                "'@WriteOnly' cannot be applied to a foreign block; annotate a 'mut prop' member instead");
            it = annos.erase(it);
            continue;
        }
        if (annotation->kind == AnnotationKind::REST_PARAM) {
            // A block-level annotation is cloned into every member; refusing
            // it once here keeps the rule from repeating per member.
            p.diag.DiagnoseRefactor(DiagKindRefactor::parse_arkts_rest_param_on_block, *annotation);
            it = annos.erase(it);
            continue;
        }
        if (annotation->kind == AnnotationKind::RAW_VALUE) {
            p.diag.DiagnoseRefactor(DiagKindRefactor::parse_arkts_raw_value_on_block, *annotation);
            it = annos.erase(it);
            continue;
        }
        if (annotation->kind == AnnotationKind::HAS_THIS) {
            p.diag.DiagnoseRefactor(DiagKindRefactor::parse_arkts_has_this_on_block, *annotation);
            it = annos.erase(it);
            continue;
        }
        if (annotation->kind != AnnotationKind::ARKTS) {
            ++it;
            continue;
        }
        // Member clones keep only the valid block defaults and cannot repeat
        // the block error.
        std::unordered_set<std::string> seen;
        std::vector<OwnedPtr<FuncArg>> validArgs;
        validArgs.reserve(annotation->args.size());
        for (auto& arg : annotation->args) {
            const auto name = arg->name.Val();
            auto literal = DynamicCast<LitConstExpr*>(arg->expr.get());
            if (name == "foreignName") {
                p.diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_block_foreign_name, *arg);
                continue;
            }
            // 'globalVar' is the one boolean option and applies to every
            // member of the block, so it is validated with the same kind rule
            // as a declaration-level option. The empty-value rule is about
            // string values and does not apply to it.
            if (name == "globalVar") {
                if (!literal || literal->kind != LitConstKind::BOOL) {
                    // The same value rule as a declaration-level option: the
                    // option is known, only its value kind is wrong.
                    auto& at = arg->expr ? *arg->expr : static_cast<Node&>(*annotation);
                    p.diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_global_var_value_invalid, at);
                    continue;
                }
                if (!seen.emplace(name).second) {
                    p.diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_option_duplicated, *annotation, name);
                    continue;
                }
                validArgs.emplace_back(std::move(arg));
                continue;
            }
            if (!literal || literal->kind != LitConstKind::STRING ||
                (name != "module" && name != "namespace")) {
                p.diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_option_invalid, *annotation);
                continue;
            }
            if (!seen.emplace(name).second) {
                p.diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_option_duplicated, *annotation, name);
                continue;
            }
            if (std::all_of(literal->stringValue.begin(), literal->stringValue.end(),
                    [](unsigned char c) { return std::isspace(c) != 0; })) {
                p.diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_option_empty, *annotation, name);
                continue;
            }
            // A block namespace is cloned into every member, so an ill-formed
            // path is reported once here and dropped before cloning; members
            // keep only the valid block defaults and cannot repeat the error.
            if (name == "namespace" && !arkTS.IsArkTSNamespaceValid(literal->stringValue)) {
                p.diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_namespace_invalid, *arg->expr,
                    literal->stringValue);
                continue;
            }
            validArgs.emplace_back(std::move(arg));
        }
        annotation->args = std::move(validArgs);
        ++it;
    }
}

void FFIParserImpl::CheckAnnotations(const PtrVector<Annotation>& annos, ScopeKind scopeKind) const
{
    // @ArkTS declaration-annotation option rules (unknown name, non-literal
    // value, a name repeated inside one layer, namespace shape) are diagnosed
    // once by the Sema option fold; block-level options are validated in
    // CheckForeignBlockAnnotations.
    for (auto& it : annos) {
        auto& anno = *it;
        switch (it->kind) {
            case AnnotationKind::JAVA_MIRROR:
            case AnnotationKind::JAVA_IMPL: {
                jp.CheckAnnotation(anno);
                // `@JavaMirror` and `@JavaImpl` annotations are deprecated.
                // The migration way is to use `@Java foreign` and `@Java` instead.
                DiagDeprecatedAnnotation(anno);
                break;
            }
            case AnnotationKind::JAVA:
            case AnnotationKind::JAVA_HAS_DEFAULT: {
                jp.CheckAnnotation(anno);
                break;
            }
            case AnnotationKind::FOREIGN_NAME: {
                CheckForeignNameAnnoArgs(anno);
                
                CheckForeignNameAnnoTarget(anno);
                break;
            }
            case AnnotationKind::FOREIGN_GETTER_NAME:
            case AnnotationKind::FOREIGN_SETTER_NAME: {
                CheckForeignGetterSetterNameAnnoArgs(anno);
                CheckForeignGetterSetterNameAnnoTarget(anno);
                break;
            }
            case AnnotationKind::OBJ_C_MIRROR:
            case AnnotationKind::OBJ_C_IMPL: {
                op.CheckAnnotation(anno, scopeKind);
                // `@ObjCMirror` and `@ObjCImpl` annotations are deprecated.
                // The migration way is to use `@ObjC foreign` and `@ObjC` instead.
                DiagDeprecatedAnnotation(anno);
                break;
            }
            case AnnotationKind::OBJ_C: {
                op.CheckAnnotation(anno, scopeKind);
                break;
            }
            
            case AnnotationKind::OBJ_C_INIT:
            case AnnotationKind::OBJ_C_OPTIONAL: {
                op.CheckAnnotation(anno, scopeKind);
                break;
            }
            case AnnotationKind::REST_PARAM: {
                arkTS.CheckAnnotation(anno);
                break;
            }
            case AnnotationKind::WRITE_ONLY: {
                // The annotation takes no arguments; its placement on a member
                // is validated by the ArkTS declaration pass. Without
                // --experimental the Sema gate owns the refusal.
                if (p.experimentalMode) {
                    CheckNoArgAnnotation(anno, "@" + anno.identifier.Val());
                }
                break;
            }
            default:
                continue;
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

void FFIParserImpl::CheckForeignGetterSetterNameAnnoTarget(const Annotation& anno) const
{
    if (p.SeeingAny({TokenKind::PROP})) {
        return;
    }

    if (anno.kind == AnnotationKind::FOREIGN_GETTER_NAME || anno.kind == AnnotationKind::FOREIGN_SETTER_NAME) {
        auto& lah = p.lookahead;
        p.DiagUnexpectedAnnoOn(anno, lah.Begin(), anno.identifier, lah.Value());
    }
}

void FFIParserImpl::CheckForeignGetterSetterNameAnnoArgs(const Annotation& anno) const
{
    if (anno.args.size() != 1 || !IsLitString(anno.args[0]->expr) || !anno.args[0]->name.Empty()) {
        p.DiagAnnotationExpectsOneArgument(anno, "@" + anno.identifier.Val(), "'String' literal");
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
        // Members of an @ArkTS foreign class are FFI declarations even though
        // FOREIGN reaches the member only later, in Sema: the enclosing
        // class-like is the marker the parser can check here.
        const bool isArkTSForeignMember = decl.outerDecl != nullptr &&
            decl.outerDecl->HasAnno(AnnotationKind::ARKTS) && decl.outerDecl->TestAttr(Attribute::FOREIGN);
        if (isArkTSForeignMember && decl.TestAttr(Attribute::CONSTRUCTOR)) {
            // The ArkTS constructor maps to a fixed `new` call; there is no
            // ArkTS-side member name for it to bind, so accepting the
            // annotation only to ignore it would be a silent misdiagnosis.
            p.diag.DiagnoseRefactor(DiagKindRefactor::parse_foreign_name_on_arkts_constructor, decl);
            decl.EnableAttr(Attribute::IS_BROKEN);
            return;
        }
        if (!decl.outerDecl ||
            (!decl.outerDecl->IsJavaForeign() && !decl.outerDecl->IsJavaExport() &&
                !decl.outerDecl->TestAnyAttr(Attribute::OBJ_C_FOREIGN, Attribute::OBJ_C_EXPORT) &&
                !isArkTSForeignMember)) {
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

ArkTSParserImpl FFIParserImpl::ArkTS() const
{
    return arkTS;
}

void FFIParserImpl::CheckZeroOrSingleStringLitArgAnnotation(AST::Annotation &anno,
                                                            const std::string &annotationName) const
{
    if (anno.args.size() == 0) {
        return;
    }

    if (anno.args.size() > 1 || !IsLitString(anno.args[0]->expr)) {
        p.DiagAnnotationMoreThanOneArgs(anno, annotationName, "'String' literal");
        anno.EnableAttr(Attribute::IS_BROKEN);
        return;
    }
}

void FFIParserImpl::CheckNoArgAnnotation(AST::Annotation &anno, const std::string &annotationName) const
{
    if (anno.args.empty()) {
        return;
    }

    p.DiagAnnotationShouldNotHaveArgs(anno, annotationName);
    anno.EnableAttr(Attribute::IS_BROKEN);
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
            case AnnotationKind::JAVA:
                if (HasModifier(decl, TokenKind::FOREIGN)) {
                    jp.CheckMirrorSignature(decl, annos);
                } else {
                    jp.CheckImplSignature(decl, annos);
                }
                break;
            case AnnotationKind::OBJ_C_MIRROR:
                op.CheckMirrorSignature(decl, annos);
                break;
            case AnnotationKind::OBJ_C_IMPL:
                op.CheckImplSignature(decl, annos);
                break;
            case AnnotationKind::OBJ_C:
                if (HasModifier(decl, TokenKind::FOREIGN)) {
                    op.CheckMirrorSignature(decl, annos);
                } else {
                    op.CheckImplSignature(decl, annos);
                }
                break;
            default: break;
        }
    }
}

void FFIParserImpl::CheckFuncSignature(
    AST::FuncDecl& decl, const PtrVector<Annotation>& annos, ScopeKind scopeKind) const
{
    // Signature-level checks stay in the per-language implementations; the
    // shared entry only routes to them.
    // The @RestParam rules are ArkTS-specific, so the call is gated on the
    // annotation being present: a signature without it keeps its own
    // language's handling, and one that carries it is accepted or rejected by
    // the ArkTS implementation.
    if (arkTS.CarriesRestParam(decl.funcBody.get())) {
        arkTS.CheckRestParamPlacement(decl, annos, scopeKind);
    }
    for (auto& anno : annos) {
        switch (anno->kind) {
            case AnnotationKind::OBJ_C_MIRROR:
            case AnnotationKind::OBJ_C:
                op.CheckMirrorSignature(decl, annos);
                break;
            default:
                break;
        }
    }
}


namespace Native::FFI {

} // namespace Native::FFI
} // namespace Cangjie
