// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares parser enhancement for Cangjie Native FFI with Java / Objective-C
 */

#ifndef CANGJIE_PARSE_NATIVEFFIPARSERIMPL_H
#define CANGJIE_PARSE_NATIVEFFIPARSERIMPL_H

#include "cangjie/AST/Node.h"
#include "cangjie/Parse/Parser.h"
#include "ArkTS/ArkTSParserImpl.h"
#include "Java/JFFIParserImpl.h"
#include "ObjC/OCFFIParserImpl.h"

namespace Cangjie {
using namespace AST;

class FFIParserImpl final {
public:
    explicit FFIParserImpl(ParserImpl& parserImpl): p(parserImpl), jp(parserImpl), op(parserImpl), arkTS(parserImpl)
    {
    }
    ~FFIParserImpl() = default;

    bool AllowsInheritedAnnotationOptions(AnnotationKind kind) const;
    void CheckAnnotationsConflict(const PtrVector<Annotation>& annos) const;
    void CheckAnnotations(const PtrVector<Annotation>& annos, ScopeKind scopeKind) const;
    void CheckForeignNameAnnotation(Decl& decl) const;
    void CheckZeroOrSingleStringLitArgAnnotation(AST::Annotation &anno, const std::string &annotationName) const;
    void CheckNoArgAnnotation(AST::Annotation &anno, const std::string &annotationName) const;

    void CheckClassLikeSignature(AST::ClassLikeDecl& decl, const PtrVector<Annotation>& annos) const;
    void CheckFuncSignature(AST::FuncDecl& decl, const PtrVector<Annotation>& annos) const;

    JFFIParserImpl Java() const;
    OCFFIParserImpl ObjC() const;
    ArkTSParserImpl ArkTS() const;

private:
    // friend JFFIParserImpl;
    void CheckForeignNameAnnoArgs(const Annotation& anno) const;
    void CheckForeignNameAnnoTarget(const Annotation& anno) const;

    void CheckForeignGetterSetterNameAnnoArgs(const Annotation& anno) const;
    void CheckForeignGetterSetterNameAnnoTarget(const Annotation& anno) const;

    void DiagDeprecatedAnnotation(const Annotation& anno) const;

    ParserImpl& p;
    JFFIParserImpl jp;
    OCFFIParserImpl op;
    ArkTSParserImpl arkTS;
};

void DiagConflictingAnnos(DiagnosticEngine& diag, Annotation& first, Annotation& second);

const std::unordered_set<AnnotationKind> CONFLICTED_FFI_ANNOS {
    AnnotationKind::JAVA_MIRROR, AnnotationKind::JAVA_IMPL,
    AnnotationKind::OBJ_C_IMPL, AnnotationKind::OBJ_C_MIRROR,
    AnnotationKind::C, AnnotationKind::JAVA, AnnotationKind::OBJ_C, AnnotationKind::ARKTS
};

namespace Native::FFI {

} // namespace Native::FFI
} // namespace Cangjie

#endif // CANGJIE_PARSE_NATIVEFFIPARSERIMPL_H
