// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements parser-related diagnostics for cangjie-native ArkTS FFI
 */

#include "../../ParserImpl.h"
#include "ArkTSParserImpl.h"

using namespace Cangjie;
using namespace Cangjie::AST;

void ArkTSParserImpl::CheckAnnotation(Annotation& anno) const
{
    if (!p.experimentalMode) {
        return;
    }
    switch (anno.kind) {
        case AnnotationKind::ARKTS_REST_PARAM: {
            // @RestParam belongs to a parameter, never to the declaration
            // itself; the annotation is marked broken so nothing downstream
            // can treat the misplaced annotation as meaningful.
            auto& lah = p.lookahead;
            p.DiagUnexpectedAnnoOn(anno, lah.Begin(), anno.identifier, lah.Value());
            anno.EnableAttr(Attribute::IS_BROKEN);
            break;
        }
        default:
            break;
    }
}

void ArkTSParserImpl::CheckRestParamAnnoArgs(Annotation& anno) const
{
    if (!p.experimentalMode) {
        return;
    }
    // The annotation takes no arguments; the shared FFI parser owns the
    // diagnostic and marks the annotation broken.
    p.ffiParser->CheckNoArgAnnotation(anno, "@RestParam");
}

void ArkTSParserImpl::DiagRestParamOnNonArkTSForeign(const Decl& decl, const Annotation& anno) const
{
    if (!p.experimentalMode) {
        return;
    }
    // @RestParam only has a meaning on an ArkTS foreign declaration; name
    // the declaration that actually carries it. The parameter is a Decl so
    // the init, primary-constructor and prop-member paths report through
    // the same helper.
    // Input -> diagnostic text (verbatim, DiagnosticParser.def):
    //   'foreign func sum(base: Float64, @RestParam nums: Array<Float64>): Float64'
    //   (no @ArkTS) -> "unexpected annotation '@RestParam' on sum"
    p.DiagUnexpectedAnnoOn(anno, decl.identifier.Begin(), anno.identifier, decl.identifier.Val());
}

void ArkTSParserImpl::DiagRestParamNotLastPositional(const FuncParam& param, const Annotation& anno) const
{
    if (!p.experimentalMode) {
        return;
    }
    // The rest parameter must be the last positional parameter, so a
    // named parameter or a trailing positional one is rejected by name.
    // Input -> diagnostic text (verbatim, DiagnosticParser.def):
    //   '@ArkTS[module: "actual"] foreign func sum(@RestParam first: Array<Float64>,
    //   second: Float64): Float64' -> "unexpected annotation '@RestParam' on first"
    p.DiagUnexpectedAnnoOn(anno, param.identifier.Begin(), anno.identifier, param.identifier.Val());
}

void ParserImpl::DiagRestParamFunctionHasNamedParams(const Annotation& anno, const FuncParam& namedParam)
{
    if (!experimentalMode) {
        return;
    }
    // A function that contains a rest parameter cannot contain any named
    // parameter. That Cangjie-side rule is a different error from a
    // misplaced rest annotation, so it gets its own diagnostic instead of
    // the generic unexpected-annotation form: the primary range is the
    // @RestParam annotation and the note marks the offending named
    // parameter.
    // Input -> diagnostic text (verbatim, DiagnosticParser.def):
    //   '@ArkTS foreign class C { init(@RestParam xs: Array<Int32>, y!: Int32 = 0) }'
    //   -> "a function with a rest parameter must not declare named parameters"
    auto builder = ParseDiagnoseRefactor(DiagKindRefactor::parse_rest_param_function_has_named_params, anno);
    builder.AddHint(namedParam);
}
