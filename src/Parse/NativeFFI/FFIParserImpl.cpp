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
#include <functional>
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

namespace {

// What a block-level annotation does after its placement rule: an accepted
// annotation is cloned into every member, a refused one is dropped before
// the clone.
enum class BlockAnnotationAction { KEEP, DROP };

// The four annotations that cannot be a foreign-block default. Each is
// diagnosed once at the block and dropped, so the member clones cannot repeat
// the error.
BlockAnnotationAction CheckBlockAnnotationPlacement(DiagnosticEngine& diag, const Annotation& annotation)
{
    switch (annotation.kind) {
        case AnnotationKind::ARKTS_WRITE_ONLY:
            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_write_only_placement_invalid, annotation,
                "'@WriteOnly' cannot be applied to a foreign block; annotate a 'mut prop' member instead");
            return BlockAnnotationAction::DROP;
        case AnnotationKind::ARKTS_REST_PARAM:
            // A block-level annotation is cloned into every member; refusing
            // it once here keeps the rule from repeating per member.
            diag.DiagnoseRefactor(DiagKindRefactor::parse_arkts_rest_param_on_block, annotation);
            return BlockAnnotationAction::DROP;
        case AnnotationKind::ARKTS_RAW_VALUE:
            diag.DiagnoseRefactor(DiagKindRefactor::parse_arkts_raw_value_on_block, annotation);
            return BlockAnnotationAction::DROP;
        case AnnotationKind::ARKTS_HAS_THIS:
            diag.DiagnoseRefactor(DiagKindRefactor::parse_arkts_has_this_on_block, annotation);
            return BlockAnnotationAction::DROP;
        default:
            return BlockAnnotationAction::KEEP;
    }
}

// Record one option name in the block layer; a repeat is diagnosed and the
// option is refused.
bool RegisterBlockOption(DiagnosticEngine& diag, const Annotation& annotation, const std::string& name,
    std::unordered_set<std::string>& seen)
{
    if (!seen.emplace(name).second) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_option_duplicated, annotation, name);
        return false;
    }
    return true;
}

// 'globalVar' is the one boolean option and applies to every member of the
// block, so it is validated with the same kind rule as a declaration-level
// option. The empty-value rule is about string values and does not apply.
bool ValidateGlobalVarBlockOption(DiagnosticEngine& diag, const Annotation& annotation, const FuncArg& arg,
    std::unordered_set<std::string>& seen)
{
    auto literal = DynamicCast<LitConstExpr*>(arg.expr.get());
    if (!literal || literal->kind != LitConstKind::BOOL) {
        // The same value rule as a declaration-level option: the option is
        // known, only its value kind is wrong.
        const auto& at = arg.expr ? static_cast<const Node&>(*arg.expr) : static_cast<const Node&>(annotation);
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_global_var_value_invalid, at);
        return false;
    }
    return RegisterBlockOption(diag, annotation, arg.name.Val(), seen);
}

// The string-valued block options; a value of any other name is unknown.
bool IsStringBlockOption(const std::string& name)
{
    return name == "module" || name == "namespace" || name == "foreignName";
}

// Validate one 'module', 'namespace' or 'foreignName' option: value kind,
// then duplicate, then empty value, then namespace shape.
bool ValidateStringBlockOption(DiagnosticEngine& diag, const ArkTSParserImpl& arkTS, const Annotation& annotation,
    const FuncArg& arg, std::unordered_set<std::string>& seen)
{
    const auto& name = arg.name.Val();
    auto literal = DynamicCast<LitConstExpr*>(arg.expr.get());
    if (!literal || literal->kind != LitConstKind::STRING || !IsStringBlockOption(name)) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_option_invalid, annotation);
        return false;
    }
    if (!RegisterBlockOption(diag, annotation, name, seen)) {
        return false;
    }
    if (std::all_of(literal->stringValue.begin(), literal->stringValue.end(),
        [](unsigned char c) { return std::isspace(c) != 0; })) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_option_empty, annotation, name);
        return false;
    }
    // A block namespace is cloned into every member, so an ill-formed path is
    // reported once here and dropped before cloning; members keep only the
    // valid block defaults and cannot repeat the error.
    if (name == "namespace" && !arkTS.IsArkTSNamespaceValid(literal->stringValue)) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_namespace_invalid, *arg.expr, literal->stringValue);
        return false;
    }
    return true;
}

// Validate the option layer of a block-level @ArkTS annotation in source
// order and keep only the accepted arguments.
void FilterBlockAnnotationOptions(DiagnosticEngine& diag, const ArkTSParserImpl& arkTS, Annotation& annotation)
{
    std::unordered_set<std::string> seen;
    std::vector<OwnedPtr<FuncArg>> validArgs;
    validArgs.reserve(annotation.args.size());
    for (auto& arg : annotation.args) {
        const bool accepted = arg->name.Val() == "globalVar"
            ? ValidateGlobalVarBlockOption(diag, annotation, *arg, seen)
            : ValidateStringBlockOption(diag, arkTS, annotation, *arg, seen);
        if (accepted) {
            validArgs.emplace_back(std::move(arg));
        }
    }
    annotation.args = std::move(validArgs);
}

} // namespace

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
        if (CheckBlockAnnotationPlacement(p.diag, *annotation) == BlockAnnotationAction::DROP) {
            it = annos.erase(it);
            continue;
        }
        // Member clones keep only the valid block defaults and cannot repeat
        // the block error.
        if (annotation->kind == AnnotationKind::ARKTS) {
            FilterBlockAnnotationOptions(p.diag, arkTS, *annotation);
        }
        ++it;
    }
}

namespace {

using DeprecatedAnnotationDiag = std::function<void(const Annotation&)>;
using AnnotationChecker = std::function<void(Annotation&)>;
using NoArgAnnotationChecker = std::function<void(Annotation&, const std::string&)>;

// Java annotations: every kind takes the J-FFI argument rules; the mirror and
// impl spellings are also deprecated.
void CheckJavaAnnotation(const JFFIParserImpl& jp, Annotation& anno, const DeprecatedAnnotationDiag& diagDeprecated)
{
    jp.CheckAnnotation(anno);
    // `@JavaMirror` and `@JavaImpl` annotations are deprecated.
    // The migration way is to use `@Java foreign` and `@Java` instead.
    if (anno.kind == AnnotationKind::JAVA_MIRROR || anno.kind == AnnotationKind::JAVA_IMPL) {
        diagDeprecated(anno);
    }
}

// Objective-C annotations: every kind takes the O-FFI target rules; the
// mirror and impl spellings are also deprecated.
void CheckObjCAnnotation(const OCFFIParserImpl& op, Annotation& anno, ScopeKind scopeKind,
    const DeprecatedAnnotationDiag& diagDeprecated)
{
    op.CheckAnnotation(anno, scopeKind);
    // `@ObjCMirror` and `@ObjCImpl` annotations are deprecated.
    // The migration way is to use `@ObjC foreign` and `@ObjC` instead.
    if (anno.kind == AnnotationKind::OBJ_C_MIRROR || anno.kind == AnnotationKind::OBJ_C_IMPL) {
        diagDeprecated(anno);
    }
}

// The two foreign-name flavours: a declaration name and an accessor name.
void CheckForeignNameAnnotation(Annotation& anno, const AnnotationChecker& checkForeignName,
    const AnnotationChecker& checkGetterSetterName)
{
    if (anno.kind == AnnotationKind::FOREIGN_NAME) {
        checkForeignName(anno);
    } else {
        checkGetterSetterName(anno);
    }
}

// @RestParam takes the shared argument rule; @WriteOnly takes no arguments and
// its placement on a member is validated by the ArkTS declaration pass.
// Without --experimental the Sema gate owns the refusal.
void CheckArkTSAnnotation(const ArkTSParserImpl& arkTS, Annotation& anno, bool experimentalMode,
    const NoArgAnnotationChecker& checkNoArg)
{
    if (anno.kind == AnnotationKind::ARKTS_REST_PARAM) {
        arkTS.CheckAnnotation(anno);
        return;
    }
    if (experimentalMode) {
        checkNoArg(anno, "@" + anno.identifier.Val());
    }
}

// Dispatch one annotation to its per-language group. The two rules owned by
// FFIParserImpl arrive as callbacks so the dispatch stays flat; the call order
// for each annotation kind is unchanged.
void CheckAnnotationByKind(const JFFIParserImpl& jp, const OCFFIParserImpl& op, const ArkTSParserImpl& arkTS,
    Annotation& anno, ScopeKind scopeKind, bool experimentalMode, const DeprecatedAnnotationDiag& diagDeprecated,
    const AnnotationChecker& checkForeignName, const AnnotationChecker& checkGetterSetterName,
    const NoArgAnnotationChecker& checkNoArg)
{
    switch (anno.kind) {
        case AnnotationKind::JAVA_MIRROR:
        case AnnotationKind::JAVA_IMPL:
        case AnnotationKind::JAVA:
        case AnnotationKind::JAVA_HAS_DEFAULT:
            CheckJavaAnnotation(jp, anno, diagDeprecated);
            break;
        case AnnotationKind::FOREIGN_NAME:
        case AnnotationKind::FOREIGN_GETTER_NAME:
        case AnnotationKind::FOREIGN_SETTER_NAME:
            CheckForeignNameAnnotation(anno, checkForeignName, checkGetterSetterName);
            break;
        case AnnotationKind::OBJ_C_MIRROR:
        case AnnotationKind::OBJ_C_IMPL:
        case AnnotationKind::OBJ_C:
        case AnnotationKind::OBJ_C_INIT:
        case AnnotationKind::OBJ_C_OPTIONAL:
            CheckObjCAnnotation(op, anno, scopeKind, diagDeprecated);
            break;
        case AnnotationKind::ARKTS_REST_PARAM:
        case AnnotationKind::ARKTS_WRITE_ONLY:
            CheckArkTSAnnotation(arkTS, anno, experimentalMode, checkNoArg);
            break;
        default:
            break;
    }
}

} // namespace

void FFIParserImpl::CheckAnnotations(const PtrVector<Annotation>& annos, ScopeKind scopeKind) const
{
    // @ArkTS declaration-annotation option rules (unknown name, non-literal
    // value, a name repeated inside one layer, namespace shape) are diagnosed
    // once by the Sema option fold; block-level options are validated in
    // CheckForeignBlockAnnotations.
    const std::function<void(const Annotation&)> diagDeprecated = [this](const Annotation& anno) {
        DiagDeprecatedAnnotation(anno);
    };
    const std::function<void(Annotation&)> checkForeignName = [this](Annotation& anno) {
        CheckForeignNameAnnoArgs(anno);
        CheckForeignNameAnnoTarget(anno);
    };
    const std::function<void(Annotation&)> checkGetterSetterName = [this](Annotation& anno) {
        CheckForeignGetterSetterNameAnnoArgs(anno);
        CheckForeignGetterSetterNameAnnoTarget(anno);
    };
    const std::function<void(Annotation&, const std::string&)> checkNoArg =
        [this](Annotation& anno, const std::string& name) { CheckNoArgAnnotation(anno, name); };
    for (auto& it : annos) {
        CheckAnnotationByKind(jp, op, arkTS, *it, scopeKind, p.experimentalMode, diagDeprecated, checkForeignName,
            checkGetterSetterName, checkNoArg);
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
