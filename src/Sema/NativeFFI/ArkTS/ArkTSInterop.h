// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_PRIVATE_NATIVE_FFI_ARKTS_INTEROP_H
#define CANGJIE_PRIVATE_NATIVE_FFI_ARKTS_INTEROP_H

#include "FunctionSource.h"
#include "cangjie/AST/Node.h"

namespace Cangjie {
class DiagnosticEngine;
class SourceManager;
class ImportManager;
namespace Native::FFI::ArkTS {
// The real compiler path passes the import manager so an @ArkTS entry whose
// interop packages are not visible is refused once, before the generated
// callback signature can surface undeclared-type noise. Unit tests that call
// this entry point without an import manager (nullptr) keep the unconditional
// generation behaviour they exercise.
void PrepareTypeCheck(AST::Package& pkg, DiagnosticEngine& diag, SourceManager& sourceManager,
    ImportManager* importManager = nullptr);
void PrepareClasses(AST::Package& pkg, DiagnosticEngine& diag, SourceManager& sourceManager,
    ImportManager* imports);
void PrepareInterfaces(AST::Package& pkg, DiagnosticEngine& diag, SourceManager& sourceManager,
    ImportManager* imports);

std::string ArkTSEntryWrapperSource(const std::string& suffix, const std::string& prefix = ARKTS_GENERATED_PREFIX);
} // namespace Native::FFI::ArkTS
} // namespace Cangjie
#endif
