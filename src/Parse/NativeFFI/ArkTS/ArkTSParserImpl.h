// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_PRIVATE_PARSE_ARKTS_PARSER_IMPL_H
#define CANGJIE_PRIVATE_PARSE_ARKTS_PARSER_IMPL_H

#include <set>

#include "cangjie/Parse/Parser.h"

namespace Cangjie {
// Compiler-private declaration parsing for ArkTS wrappers.
class ArkTSParserImpl final {
public:
    explicit ArkTSParserImpl(ParserImpl& parserImpl) : p(parserImpl)
    {
    }

    std::set<AST::Modifier> PrepareForeignFunctionModifiers(ScopeKind scopeKind,
        const std::set<AST::Modifier>& modifiers, const PtrVector<AST::Annotation>& annotations,
        AST::FuncDecl& function) const;

    // Parameter-level diagnostics for @RestParam, dispatched from
    // FFIParserImpl::CheckAnnotations (one entry per language, as for Java and
    // Objective-C). The bodies live in ParserDiag.cpp; this class owns the
    // ArkTS-specific rules, the shared FFI parser only routes to them. @ArkTS
    // options are not checked here: the Sema option fold owns every option
    // rule, so one mistake is reported once.
    void CheckAnnotation(AST::Annotation& anno) const;

    // Parameter-level diagnostics of @RestParam. The rule "the annotation takes
    // no arguments" is shared, so CheckRestParamAnnoArgs delegates to
    // FFIParserImpl::CheckNoArgAnnotation (same shape as the Objective-C
    // CheckInitAnnoArgs/CheckOptionalAnnoArgs helpers); the two placement
    // rejections below are ArkTS-specific and are reported from ParserDiag.cpp.
    void CheckRestParamAnnoArgs(AST::Annotation& anno) const;
    void DiagRestParamOnNonArkTSForeign(const AST::Decl& decl, const AST::Annotation& anno) const;
    void DiagRestParamNotLastPositional(const AST::FuncParam& param, const AST::Annotation& anno) const;

    // @RestParam marks the ArkTS rest parameter: legal only on the final
    // positional parameter of an ArkTS foreign function, no named or
    // optional parameter may follow it, and the annotation takes no
    // arguments. A legal target carries @ArkTS plus foreign itself (a
    // top-level func or a foreign-block member) or is a member of an
    // @ArkTS foreign class/interface; the member carries neither marker at
    // parse time, so the enclosing declaration is resolved through the
    // parser chain. The func path arrives through CheckFuncSignature; the
    // constructor, primary-constructor and prop paths call the overloads
    // below directly. 'main' and macro parameter lists stay uncovered.
    // CarriesRestParam is the gate in front of both: a signature without the
    // annotation has nothing to place, so it never enters the ArkTS rules.
    bool CarriesRestParam(const AST::FuncBody* funcBody) const;
    void CheckRestParamPlacement(
        const AST::FuncDecl& function, const PtrVector<AST::Annotation>& annos, ScopeKind scopeKind) const;
    void CheckRestParamPlacement(
        const AST::PrimaryCtorDecl& ctor, const PtrVector<AST::Annotation>& annos, ScopeKind scopeKind) const;

private:
    // Shared core of the two CheckRestParamPlacement overloads; decl is the
    // owning declaration (used for the chain lookup, the target diagnostic
    // and the FOREIGN attribute) and funcBody carries the parameter lists.
    void CheckRestParamPlacementImpl(const AST::Decl& decl, const AST::FuncBody* funcBody,
        const PtrVector<AST::Annotation>& annos, ScopeKind scopeKind) const;

    ParserImpl& p;
};
} // namespace Cangjie
#endif
