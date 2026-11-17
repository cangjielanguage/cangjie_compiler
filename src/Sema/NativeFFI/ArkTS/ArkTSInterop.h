// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_PRIVATE_NATIVE_FFI_ARKTS_INTEROP_H
#define CANGJIE_PRIVATE_NATIVE_FFI_ARKTS_INTEROP_H

#include "FunctionSource.h"
#include "cangjie/AST/Node.h"

#include <functional>

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
// Validate non-foreign @ArkTS declaration contracts before entry generation:
// entry shape/name, annotation placement and the foreign-only 'module' option.
// Rejected entries are marked IS_BROKEN so the entry loop skips them.
void DiagnoseArkTSDeclarations(AST::Package& pkg, DiagnosticEngine& diag);

// Sema check for the @RawValue annotation. It lives in the ArkTS namespace
// (not the C FFI annotation table) because it validates an ArkTS-only annotation
// on a lowered `foreign enum` constructor: a valid constructor keeps the value,
// a duplicate is reported, and any other position is misplaced.
void CheckAnnoRawValue(AST::Decl& decl, const AST::Annotation& anno, DiagnosticEngine& diag);

// Entry points for the ArkTS prepare/lower state. The state itself (the
// foreign-enum value table) is private to the ArkTS lowering translation unit:
// the checker drives it only through these functions and never holds it as a
// member, so no ArkTS-specific field leaks into TypeCheckerImpl.
void ResetState();
void PrepareVariablesAndEnums(AST::Package& pkg, DiagnosticEngine& diag);
// Attach the prepared value table to every enum leaf. A payload member carries
// its associated-value type node, resolved through the classifier supplied by
// the checker (which owns alias resolution and imported-type classification).
void AttachEnumMembers(ArkTSTypeDesc& desc,
    const std::function<ArkTSTypeDesc(Ptr<AST::Type>)>& classify, ImportManager* imports);

std::string ArkTSEntryWrapperSource(const std::string& suffix, const std::string& prefix = ARKTS_GENERATED_PREFIX);
} // namespace Native::FFI::ArkTS
} // namespace Cangjie
#endif
