// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_PRIVATE_NATIVE_FFI_ARKTS_AST_FACTORY_H
#define CANGJIE_PRIVATE_NATIVE_FFI_ARKTS_AST_FACTORY_H
#include "cangjie/AST/Node.h"
namespace Cangjie {
class DiagnosticEngine;
class SourceManager;
namespace Native::FFI::ArkTS {
class ASTFactory {
public:
    ASTFactory(DiagnosticEngine& diagnostics, SourceManager& sources) : diag(diagnostics), sourceManager(sources) {}
    std::vector<OwnedPtr<AST::Decl>> CreateEntry(AST::FuncDecl& entry, const std::string& suffix,
        const std::string& prefix) const;
private:
    DiagnosticEngine& diag;
    SourceManager& sourceManager;
};
} // namespace Native::FFI::ArkTS
} // namespace Cangjie
#endif
