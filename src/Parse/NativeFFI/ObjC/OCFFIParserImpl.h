// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares class NativeFFIObjCParserImpl.
 */

#ifndef CANGJIE_PARSE_NATIVEFFIOBJCPARSERIMPL_H
#define CANGJIE_PARSE_NATIVEFFIOBJCPARSERIMPL_H

#include "cangjie/Parse/Parser.h"

namespace Cangjie {
class OCFFIParserImpl final {
public:
    explicit OCFFIParserImpl(ParserImpl& parserImpl) : p(parserImpl)
    {
    }
    ~OCFFIParserImpl() = default;

    void CheckAnnotation(const AST::Annotation& anno, ScopeKind scopeKind) const;
    /**
     * Checks `@ObjCInit` annotation correctness within `fd` as a target
     */
    void CheckInitAnnotation(AST::FuncDecl& fd) const;
    void CheckMirrorSignature(AST::ClassLikeDecl& decl, const PtrVector<AST::Annotation>& annos) const;
    void CheckImplSignature(AST::ClassLikeDecl& decl, const PtrVector<AST::Annotation>& annos) const;
    void CheckMirrorSignature(AST::FuncDecl& decl, const PtrVector<AST::Annotation>& annos) const; 

    // Make private, when checking @ObjCMirror/@ObjCImpl decl members moved to this class
    void DiagObjCMirrorCannotHaveFinalizer(const AST::Node& node) const;
    void DiagObjCMirrorCannotHavePrivateMember(const AST::Node& node) const;
    void DiagObjCMirrorCannotHaveConstMember(const AST::Node& node) const;
    void DiagObjCMirrorCannotHaveStaticInit(const AST::Node& node) const;
    void DiagObjCMirrorFieldCannotHaveInitializer(const AST::Node& node) const;
    void DiagObjCMirrorCannotHavePrimaryCtor(const AST::Node& node) const;
    void DiagObjCMirrorFieldCannotBeStatic(const AST::Node& node) const;

    void DiagObjCImplCannotHaveStaticInit(const AST::Node& node) const;
    void DiagObjCImplCannotBeGeneric(const AST::Node& node) const;
    void DiagObjCInitFuncMustBeStatic(const AST::Node& node) const;
    void DiagObjCInitFuncMustBeInMirrorClass(const AST::FuncDecl& node) const;

    void DiagObjCMirrorFuncCannotBeForeign(const AST::FuncDecl& node) const;
    void DiagObjCMirrorFuncCannotBeC(const AST::FuncDecl& node) const;
    void DiagObjCMirrorFuncCannotBeGeneric(const AST::FuncDecl& node) const;
    void DiagObjCMirrorFuncCannotHaveBody(const AST::FuncDecl& node) const;
    void DiagObjCMirrorFuncMustHaveExplicitType(const AST::FuncDecl& node) const;
    void DiagObjCMirrorFuncCannotBeConst(const AST::FuncDecl& node) const;
    void DiagObjCMirrorFuncMustBeTopLevel(const AST::FuncDecl& node) const;

private:
    void CheckMirrorAnnoArgs(const AST::Annotation& anno) const;
    void CheckImplAnnoArgs(const AST::Annotation& anno) const;
    void CheckInitAnnoArgs(const AST::Annotation& anno) const;

    void CheckMirrorAnnoTarget(const AST::Annotation& anno, ScopeKind scopeKind) const;
    void CheckImplAnnoTarget(const AST::Annotation& anno) const;
    void CheckInitAnnoTarget(const AST::Annotation& anno) const;

    void DiagObjCMirrorCannotBeSealed(const AST::Node& node) const;

    void DiagObjCImplCannotBeOpen(const AST::Node& node) const;
    void DiagObjCImplCannotBeInterface(const AST::Node& node) const;
    void DiagObjCImplCannotBeAbstract(const AST::Node& node) const;
    void DiagObjCImplCannotBeSealed(const AST::Node& node) const;

    ParserImpl& p;
};
} // namespace Cangjie

#endif // CANGJIE_PARSE_NATIVEFFIOBJCPARSERIMPL_H
