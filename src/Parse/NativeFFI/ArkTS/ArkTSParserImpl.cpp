// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "ArkTSParserImpl.h"

#include <algorithm>

#include "../../ParserImpl.h"
#include "cangjie/AST/Match.h"
#include "cangjie/Utils/Utils.h"

using namespace Cangjie;
using namespace Cangjie::AST;

namespace {
// Before: parsed annotation nodes. After result: whether an ARKTS kind exists;
// this selects modifier preparation without adding or changing annotation nodes.
bool HasArkTSAnnotation(const PtrVector<Annotation>& annotations)
{
    return std::any_of(annotations.begin(), annotations.end(),
        [](const auto& annotation) { return annotation->kind == AnnotationKind::ARKTS; });
}

// A member of an @ArkTS foreign class-like carries neither marker on itself
// at parse time: FOREIGN is only propagated to members by Sema
// (DeclAttributeChecker::SetCommonAttributes) and the @ArkTS annotation
// stays on the enclosing declaration, so the class-like is resolved
// through the parser chain. The body scope kinds are produced only while
// the enclosing ChainScope is active, FOREIGN enabled and @ArkTS attached,
// so a function nested in a member body never resolves to the outer
// class-like. The prop-member form must hang off exactly one PropDecl
// directly off the class-like; an ordinary C foreign class-like (FOREIGN
// but no @ArkTS) keeps its members rejected.
bool IsArkTSForeignClassMember(const std::vector<Ptr<Node>>& chain, const Decl& decl, ScopeKind scopeKind)
{
    if (scopeKind == ScopeKind::CLASS_BODY || scopeKind == ScopeKind::INTERFACE_BODY) {
        for (auto node = chain.rbegin(); node != chain.rend(); ++node) {
            if (node->get() == &decl) {
                continue;
            }
            auto classLike = DynamicCast<ClassLikeDecl*>(node->get());
            if (classLike == nullptr) {
                continue;
            }
            return classLike->TestAttr(Attribute::FOREIGN) && classLike->HasAnno(AnnotationKind::ARKTS);
        }
        return false;
    }
    if (scopeKind != ScopeKind::PROP_MEMBER_SETTER_BODY && scopeKind != ScopeKind::PROP_MEMBER_GETTER_BODY) {
        return false;
    }
    bool throughProp = false;
    for (auto node = chain.rbegin(); node != chain.rend(); ++node) {
        if (node->get() == &decl) {
            continue;
        }
        if (auto prop = DynamicCast<PropDecl*>(node->get()); prop != nullptr && !throughProp) {
            throughProp = true;
            continue;
        }
        auto classLike = DynamicCast<ClassLikeDecl*>(node->get());
        if (classLike == nullptr) {
            return false;
        }
        return throughProp && classLike->TestAttr(Attribute::FOREIGN) && classLike->HasAnno(AnnotationKind::ARKTS);
    }
    return false;
}

} // namespace

// Before: top-level @ArkTS foreign func has FOREIGN in the source modifiers.
// After: return a checking-only copy without FOREIGN and mark FuncDecl FOREIGN;
// ParseFuncDecl keeps the original source modifier on the AST for body parsing.
// This postpones C ABI checks until PrepareTypeCheck can classify a CJ wrapper.
// Other functions return the modifiers unchanged; no glue body is created here.
std::set<Modifier> ArkTSParserImpl::PrepareForeignFunctionModifiers(ScopeKind scopeKind,
    const std::set<Modifier>& modifiers, const PtrVector<Annotation>& annotations, FuncDecl& function) const
{
    // Preserve ordinary C foreign checks. ArkTS foreign will become an
    // ordinary CJ wrapper in NativeFFI before C ABI/type prechecking.
    auto checked = modifiers;
    if (scopeKind == ScopeKind::TOPLEVEL && p.HasModifier(modifiers, TokenKind::FOREIGN) &&
        HasArkTSAnnotation(annotations)) {
        Utils::EraseIf(checked, [](const auto& modifier) { return modifier.modifier == TokenKind::FOREIGN; });
        function.EnableAttr(Attribute::FOREIGN);
    }
    return checked;
}

// Before: a parsed declaration's function body. After result: whether its first
// parameter list carries @RestParam. The ArkTS rest-parameter rules only have an
// input when the annotation is used, so every other signature skips them and
// keeps its own language's handling.
bool ArkTSParserImpl::CarriesRestParam(const FuncBody* funcBody) const
{
    if (funcBody == nullptr || funcBody->paramLists.empty()) {
        return false;
    }
    for (const auto& param : funcBody->paramLists.front()->params) {
        for (const auto& annotation : param->annotations) {
            if (annotation->kind == AnnotationKind::REST_PARAM) {
                return true;
            }
        }
    }
    return false;
}

// @RestParam placement, checked once per signature. The annotation is
// accepted only on the final positional parameter of an ArkTS foreign
// function, takes no arguments, and such a function must not declare
// named parameters at all; ordinary C foreign declarations stay rejected.
// A mixed rest+named signature is reported once through
// parse_rest_param_function_has_named_params and the placement checks are
// skipped for that function, so the shape is not double-reported; each
// diagnostic names the offending parameter or the declaration itself.
// Input -> outcome: '@ArkTS foreign func sum(@RestParam first: Array<Float64>,
//   second: Float64): Float64' -> "unexpected annotation '@RestParam' on first".
void ArkTSParserImpl::CheckRestParamPlacement(
    const FuncDecl& function, const PtrVector<Annotation>& annos, ScopeKind scopeKind) const
{
    CheckRestParamPlacementImpl(function, function.funcBody.get(), annos, scopeKind);
}

void ArkTSParserImpl::CheckRestParamPlacement(
    const PrimaryCtorDecl& ctor, const PtrVector<Annotation>& annos, ScopeKind scopeKind) const
{
    CheckRestParamPlacementImpl(ctor, ctor.funcBody.get(), annos, scopeKind);
}

void ArkTSParserImpl::CheckRestParamPlacementImpl(const Decl& decl, const FuncBody* funcBody,
    const PtrVector<Annotation>& annos, ScopeKind scopeKind) const
{
    if (funcBody == nullptr || funcBody->paramLists.empty()) {
        return;
    }
    const auto& params = funcBody->paramLists.front()->params;
    // Mirrors IsArkTSForeignFunction (EntrySelection.cpp), which gates
    // lowering: FOREIGN alone would also accept ordinary C foreign. A member
    // of an @ArkTS foreign class/interface matches neither half of that
    // predicate at parse time, so the enclosing class-like is consulted as
    // an additional source.
    // `annos` may be empty on the init/primary-constructor/prop paths, where
    // the declaration already owns its parsed annotations.
    const bool hasArkTS = HasArkTSAnnotation(annos) || HasArkTSAnnotation(decl.annotations);
    const bool arktsForeign = (decl.TestAttr(Attribute::FOREIGN) && hasArkTS) ||
        IsArkTSForeignClassMember(p.chainedAST, decl, scopeKind);
    // Pre-scan for the first named parameter of the list, if any. Cangjie
    // parameter ordering (DiagNamedParameterAfterUnnamed, ParseDecl.cpp)
    // already forces named parameters behind every positional one, so its
    // presence means either a named parameter follows the rest position or
    // the rest position itself is named - both are the same rule violation:
    // a function containing a rest parameter cannot contain named
    // parameters. A named rest lands here too (it is itself a named
    // parameter of the function), so in practice the placement branch below
    // only ever sees an unnamed rest; its isNamedParam disjunct stays as the
    // written contract of the preserved check.
    const FuncParam* namedParam = nullptr;
    for (const auto& param : params) {
        if (param->isNamedParam) {
            namedParam = param.get();
            break;
        }
    }
    bool reportedNamedParamMix = false;
    for (size_t index = 0; index < params.size(); ++index) {
        for (auto& annotation : params[index]->annotations) {
            if (annotation->kind != AnnotationKind::REST_PARAM) {
                continue;
            }
            // The annotation takes no arguments; ParserDiag.cpp routes the
            // check to the shared FFIParserImpl::CheckNoArgAnnotation.
            CheckRestParamAnnoArgs(*annotation);
            const auto& param = *params[index];
            // The placement rule is that the rest parameter must be the
            // final positional parameter; on top of it, a function containing
            // a rest parameter cannot contain any named parameter. A mixed
            // signature is reported once through the dedicated diagnostic
            // and the placement checks below are skipped for the whole
            // function, so the same signature is not double-reported.
            if (!arktsForeign) {
                DiagRestParamOnNonArkTSForeign(decl, *annotation);
            } else if (namedParam != nullptr) {
                if (!reportedNamedParamMix) {
                    p.DiagRestParamFunctionHasNamedParams(*annotation, *namedParam);
                    reportedNamedParamMix = true;
                }
            } else if (param.isNamedParam || index + 1 != params.size()) {
                DiagRestParamNotLastPositional(param, *annotation);
            }
        }
    }
}
