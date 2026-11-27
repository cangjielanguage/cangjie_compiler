// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "ArkTSParserImpl.h"

#include <algorithm>

#include "../../ParserImpl.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Utils.h"
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
// at parse time: FOREIGN reaches members only through Sema's owner
// propagation (DeclAttributeChecker::SetCommonAttributes, which a prepared
// owner skips) and the @ArkTS annotation stays on the enclosing declaration,
// so the class-like is resolved
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

// Whether a declaration parsed directly in a class-like body belongs to an
// @ArkTS enclosing declaration. A primary constructor (or an 'init' function)
// is not parented to its class-like until after ParseDecl returns
// (SetMemberParentInheritableDecl runs later in ParseClassBody), so
// `decl.outerDecl` is still null while the rest-parameter check runs; resolve
// the immediate class-like through the active parser chain instead. Gated to
// body scopes so a function nested in a member body never resolves to the
// outer class-like.
bool EnclosingClassLikeHasARKTSAnnotation(
    const std::vector<Ptr<Node>>& chain, const Decl& decl, ScopeKind scopeKind)
{
    if (scopeKind != ScopeKind::CLASS_BODY && scopeKind != ScopeKind::INTERFACE_BODY &&
        scopeKind != ScopeKind::STRUCT_BODY) {
        return false;
    }
    for (auto node = chain.rbegin(); node != chain.rend(); ++node) {
        if (node->get() == &decl) {
            continue;
        }
        auto classLike = DynamicCast<ClassLikeDecl*>(node->get());
        if (classLike == nullptr) {
            continue;
        }
        return HasArkTSAnnotation(classLike->annotations);
    }
    return false;
}

// Whether the declaration itself, its owner or its enclosing class-like carries
// @ArkTS. `annos` may be empty on the init/primary-constructor/prop paths,
// where the declaration already owns its parsed annotations, and a constructor
// is not yet parented to its class-like at parse time, so the parser chain is
// consulted as well.
bool HasRestParamArkTSContext(const Decl& decl, const PtrVector<Annotation>& annos,
    const std::vector<Ptr<Node>>& chain, ScopeKind scopeKind)
{
    return HasArkTSAnnotation(annos) || HasArkTSAnnotation(decl.annotations) ||
        (decl.outerDecl != nullptr && HasArkTSAnnotation(decl.outerDecl->annotations)) ||
        EnclosingClassLikeHasARKTSAnnotation(chain, decl, scopeKind);
}

// Mirrors IsArkTSForeignFunction (EntrySelection.cpp): FOREIGN alone would also
// accept ordinary C foreign, and a member of an @ArkTS foreign class/interface
// matches neither half of that predicate at parse time.
bool IsArkTSRestParamForeign(
    const Decl& decl, bool hasArkTS, const std::vector<Ptr<Node>>& chain, ScopeKind scopeKind)
{
    return (decl.TestAttr(Attribute::FOREIGN) && hasArkTS) ||
        IsArkTSForeignClassMember(chain, decl, scopeKind);
}

// The first named parameter of the list, if any. Cangjie parameter ordering
// (DiagNamedParameterAfterUnnamed, ParseDecl.cpp) already forces named
// parameters behind every positional one, so its presence means either a named
// parameter follows the rest position or the rest position itself is named.
const FuncParam* FindFirstNamedParam(const std::vector<OwnedPtr<FuncParam>>& params)
{
    for (const auto& param : params) {
        if (param->isNamedParam) {
            return param.get();
        }
    }
    return nullptr;
}

enum class RestParamOutcome {
    // The annotation was accepted or refused here.
    HANDLED,
    // The signature mixes a rest parameter with named parameters; the caller
    // reports it once through the dedicated diagnostic.
    NAMED_PARAM_MIX,
};

// The placement verdict for one @RestParam annotation. A mixed signature is
// handled by the caller, and the placement checks are skipped for the whole
// function, so the same signature is not double-reported.
RestParamOutcome DiagnoseRestParamMisuse(ParserImpl& parser, const ArkTSParserImpl& self, const Decl& decl,
    const FuncParam& param, Annotation& annotation, size_t index, size_t paramCount, const FuncParam* namedParam,
    bool arktsForeign, bool hasArkTS)
{
    if (!arktsForeign) {
        // A non-foreign @ArkTS declaration is legal but its rest-parameter
        // mapping is not implemented in this release; a plain C foreign
        // declaration stays under the ordinary annotation rule.
        if (hasArkTS) {
            parser.GetDiagnosticEngine().DiagnoseRefactor(
                DiagKindRefactor::parse_arkts_rest_param_non_foreign_not_supported, annotation,
                decl.identifier.Val());
        } else {
            self.DiagRestParamOnNonArkTSForeign(decl, annotation);
        }
        return RestParamOutcome::HANDLED;
    }
    if (namedParam != nullptr) {
        return RestParamOutcome::NAMED_PARAM_MIX;
    }
    if (param.isNamedParam || index + 1 != paramCount) {
        self.DiagRestParamNotLastPositional(param, annotation);
    }
    return RestParamOutcome::HANDLED;
}

} // namespace

// Before: top-level @ArkTS foreign func has FOREIGN in the source modifiers.
// After: mark FuncDecl FOREIGN and return the modifiers unchanged, so the
// parser's modifier check still sees FOREIGN. ParseFuncDecl passes the @ArkTS
// relaxation beside it, which exempts the access x FOREIGN pair except
// PRIVATE; every other FOREIGN conflict (UNSAFE/EXCLAVE/COMMON/SPECIFIC/
// CONST) is therefore reported. The declaration becomes an ordinary CJ
// wrapper in NativeFFI before C ABI/type prechecking; no glue body is created
// here and other functions return their modifiers unchanged.
std::set<Modifier> ArkTSParserImpl::PrepareForeignFunctionModifiers(ScopeKind scopeKind,
    const std::set<Modifier>& modifiers, const PtrVector<Annotation>& annotations, FuncDecl& function) const
{
    if (scopeKind == ScopeKind::TOPLEVEL && AST::HasModifier(modifiers, TokenKind::FOREIGN) &&
        HasArkTSAnnotation(annotations)) {
        function.EnableAttr(Attribute::FOREIGN);
    }
    return modifiers;
}

// A namespace value is legal when it is empty or has no empty leading,
// trailing or inner segment; "a.b" and "a" are legal, ".a", "a." and "a..b"
// are not. The block-level option is validated once here before it is cloned
// into every member; the Sema option fold applies the same rule to
// declaration-level options.
bool ArkTSParserImpl::IsArkTSNamespaceValid(const std::string& ns) const
{
    return ns.empty() || (ns.front() != '.' && ns.back() != '.' && ns.find("..") == std::string::npos);
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
            if (annotation->kind == AnnotationKind::ARKTS_REST_PARAM) {
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
    const bool hasArkTS = HasRestParamArkTSContext(decl, annos, p.chainedAST, scopeKind);
    const bool arktsForeign = IsArkTSRestParamForeign(decl, hasArkTS, p.chainedAST, scopeKind);
    const FuncParam* namedParam = FindFirstNamedParam(params);
    bool reportedNamedParamMix = false;
    for (size_t index = 0; index < params.size(); ++index) {
        for (auto& annotation : params[index]->annotations) {
            if (annotation->kind != AnnotationKind::ARKTS_REST_PARAM) {
                continue;
            }
            // The annotation takes no arguments; ParserDiag.cpp routes the
            // check to the shared FFIParserImpl::CheckNoArgAnnotation.
            CheckRestParamAnnoArgs(*annotation);
            if (DiagnoseRestParamMisuse(p, *this, decl, *params[index], *annotation, index, params.size(),
                namedParam, arktsForeign, hasArkTS) == RestParamOutcome::NAMED_PARAM_MIX &&
                !reportedNamedParamMix) {
                p.DiagRestParamFunctionHasNamedParams(*annotation, *namedParam);
                reportedNamedParamMix = true;
            }
        }
    }
}
