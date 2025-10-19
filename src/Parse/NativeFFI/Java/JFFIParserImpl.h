// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares class NativeFFIJavaParserImpl
 */

#ifndef CANGJIE_PARSE_NATIVEFFIJAVAPARSERIMPL_H
#define CANGJIE_PARSE_NATIVEFFIJAVAPARSERIMPL_H

#include "cangjie/Parse/Parser.h"

namespace Cangjie {
using namespace AST;

class JFFIParserImpl final {
public:
    explicit JFFIParserImpl(ParserImpl& parserImpl): p(parserImpl)
    {
    }
    ~JFFIParserImpl() = default;

    void CheckAnnotation(const Annotation& anno) const;
    void CheckMirrorSignature(ClassLikeDecl& decl, const PtrVector<Annotation>& annos) const;
    void CheckImplSignature(ClassLikeDecl& decl, const PtrVector<Annotation>& annos) const;
    void CheckJavaHasDefaultAnnotation(const Annotation& anno) const;
    bool IsAbstractFunction(const FuncDecl& fd, const Decl& outerDecl) const;

    void DiagJavaMirrorCannotHaveFinalizer(const Node& node) const;
    void DiagJavaMirrorCannotHavePrivateMember(const Node& node) const;
    void DiagJavaMirrorCannotHaveStaticInit(const Node& node) const;
    void DiagJavaMirrorCannotHaveConstMember(const Node& node) const;
    void DiagJavaImplCannotBeGeneric(const Node& node) const;
    void DiagJavaImplCannotBeAbstract(const Node& node) const;
    void DiagJavaImplCannotBeSealed(const Node& node) const;
    void DiagJavaMirrorCannotBeSealed(const Node& node) const;
    void DiagJavaImplCannotHaveStaticInit(const Node& node) const;

private:
    void CheckMirrorAnnoArgs(const Annotation& anno) const;
    void CheckImplAnnoArgs(const Annotation& anno) const;

    void CheckMirrorAnnoTarget(const Annotation& anno) const;
    void CheckImplAnnoTarget(const Annotation& anno) const;

    // Diag report
    void DiagOuterDeclMissMatch(const Node& node,
        const std::string& p0, const std::string& p1, const std::string& p2, const std::string& p3) const;
private:
    ParserImpl& p;
    bool compileCommon{false}; // true if compiling common part
    bool compilePlatform{false}; // true if compiling platform part
};

} // namespace Cangjie

#endif
