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

    void CheckAnnotation(const AST::Annotation& annotation) const;

    std::set<AST::Modifier> PrepareForeignFunctionModifiers(ScopeKind scopeKind,
        const std::set<AST::Modifier>& modifiers, const PtrVector<AST::Annotation>& annotations,
        AST::FuncDecl& function) const;

private:
    ParserImpl& p;
};
} // namespace Cangjie
#endif
