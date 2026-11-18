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

void CheckAnnoRawValue(Decl& decl, const Annotation& anno, DiagnosticEngine& diag)
{
    // @RawValue maps a constructor of an ArkTS foreign enum to its ArkTS
    // value. Those enums are marked ARKTS_LOWERED during prepare-typecheck,
    // which always runs before this check.
    auto owner = decl.outerDecl;
    if (decl.TestAttr(Attribute::ENUM_CONSTRUCTOR) && owner && owner->astKind == ASTKind::ENUM_DECL &&
        owner->TestAttr(Attribute::ARKTS_LOWERED)) {
        // A constructor with an associated value maps a type member; its value
        // is the payload, not a literal, so @RawValue cannot describe it.
        if (decl.astKind == ASTKind::FUNC_DECL) {
            auto& func = static_cast<FuncDecl&>(decl);
            bool hasPayload = false;
            if (func.funcBody != nullptr) {
                for (auto& list : func.funcBody->paramLists) {
                    hasPayload = hasPayload || !list->params.empty();
                }
            }
            if (hasPayload) {
                diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_raw_value_payload_ctor, anno,
                    decl.identifier.Val());
                return;
            }
        }
        const Annotation* first = nullptr;
        for (auto& other : decl.annotations) {
            if (other->kind != AnnotationKind::RAW_VALUE) {
                continue;
            }
            if (first == nullptr) {
                first = other.get();
            }
            if (other.get() == &anno) {
                if (first != &anno) {
                    diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_raw_value_duplicate, anno,
                        decl.identifier.Val());
                }
                return;
            }
        }
    }
    diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_raw_value_misplaced, anno, decl.identifier.Val());
}
} // namespace Cangjie::Native::FFI::ArkTS
