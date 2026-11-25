// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * Sema check for the ArkTS-only @RawValue annotation. Kept in the ArkTS
 * namespace rather than the C FFI annotation table (FFI/CFFICheck.cpp) so the
 * C FFI file stays free of ArkTS-specific rules.
 */

#include "ArkTSInterop.h"

#include "cangjie/AST/Node.h"
#include "cangjie/Basic/DiagnosticEngine.h"

namespace Cangjie::Native::FFI::ArkTS {
using namespace Cangjie::AST;

namespace {
// A constructor with an associated-value parameter maps a payload type, not a
// literal, so @RawValue cannot describe it.
bool ArkTSRawValuePayloadConstructor(const FuncDecl& func)
{
    if (func.funcBody == nullptr) {
        return false;
    }
    for (auto& list : func.funcBody->paramLists) {
        if (!list->params.empty()) {
            return true;
        }
    }
    return false;
}
} // namespace

void CheckAnnoRawValue(Decl& decl, const Annotation& anno, DiagnosticEngine& diag)
{
    if (!CapabilityEnabled()) {
        return;
    }
    // @RawValue maps a constructor of an @ArkTS enum to its ArkTS value. Both
    // a lowered foreign enum and an exported non-foreign enum are accepted:
    // the owner is checked through its annotation as well as the prepared
    // marker, so neither spelling is refused.
    auto owner = decl.outerDecl;
    const bool arktsEnum = owner && owner->astKind == ASTKind::ENUM_DECL &&
        (owner->HasAnno(AnnotationKind::ARKTS) || owner->TestAttr(Attribute::ARKTS));
    if (decl.TestAttr(Attribute::ENUM_CONSTRUCTOR) && arktsEnum) {
        // A lowered foreign enum constructor with an associated value maps a
        // type member; its value is the payload, not a literal, so @RawValue
        // cannot describe it. An exported enum with such a constructor is
        // refused by the enum shape rule instead.
        if (decl.astKind == ASTKind::FUNC_DECL && owner->TestAttr(Attribute::ARKTS_LOWERED) &&
            ArkTSRawValuePayloadConstructor(static_cast<FuncDecl&>(decl))) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_raw_value_payload_ctor, anno,
                decl.identifier.Val());
            return;
        }
        // A repeated @RawValue is reported by the parser's generic
        // duplicate-annotation check (ParserImpl::ParseAnnotations), like the
        // other builtin annotations; Sema only validates placement and payload.
        return;
    }
    diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_raw_value_misplaced, anno, decl.identifier.Val());
}
} // namespace Cangjie::Native::FFI::ArkTS
