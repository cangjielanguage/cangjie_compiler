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
