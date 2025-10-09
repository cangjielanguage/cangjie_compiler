// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares class MPParserImpl.
 */

#ifndef CANGJIE_PARSE_MPPARSERIMPL_H
#define CANGJIE_PARSE_MPPARSERIMPL_H

#include "cangjie/Parse/Parser.h"

namespace Cangjie {
using namespace AST;
class MPParserImpl final {
public:
    explicit MPParserImpl(const ParserImpl& parserImpl): ref(&parserImpl)
    {
    }
    ~MPParserImpl() = default;

    // set compile options for cjmp.
    void SetCompileOptions(const GlobalOptions& opts);
    // Check CJMP modifier rules.
    bool CheckCJMPModifiers(const std::set<AST::Modifier> &modifiers) const;
    // The entry of checking CJMP decl rules.
    void CheckCJMPDecl(AST::Decl& decl) const;
    // Check whether the given modifier is a CJMP modifier (COMMON or PLATFORM).
    bool HasCJMPModifiers(const AST::Modifier& modifier) const;

private:
    bool CheckCJMPModifiersOf(const AST::Decl& decl) const;
    bool CheckCJMPModifiersBetween(const AST::Decl& inner, const AST::Decl& outer) const;
    void CheckCJMPFuncParams(AST::Decl& decl, const Ptr<AST::FuncBody> funcBody) const;
    void CheckPlatformInterface(const AST::InterfaceDecl& decl) const;
    // Diag report
    void DiagOuterDeclMissMatch(const AST::Node& node,
        const std::string& p0, const std::string& p1, const std::string& p2, const std::string& p3) const;
private:
    const ParserImpl* ref;
    bool compileCommon{false}; // true if compiling common part
    bool compilePlatform{false}; // true if compiling platform part
};

} // namespace Cangjie
#endif
