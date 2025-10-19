// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "TypeCheckAnnotation.h"
#include "DiagsInterop.h"

namespace Cangjie::Interop::Java {

void CheckJavaHasDefaultAnnotation(DiagnosticEngine& diag, const Annotation& ann, const Decl& decl)
{
    CJC_ASSERT(ann.kind == AnnotationKind::JAVA_HAS_DEFAULT);
    if (!ann.args.empty()) {
        DiagJavaHasDefaultNoArgs(diag, ann);
    }

    auto isOuterDeclJavaMirror = decl.outerDecl && decl.outerDecl->TestAttr(Attribute::JAVA_MIRROR);
    auto isOuterDeclInterface = isOuterDeclJavaMirror && (decl.outerDecl->astKind == ASTKind::INTERFACE_DECL);
    auto isCurrentDeclFunc = decl.astKind == ASTKind::FUNC_DECL;

    auto isInvalidScope = !isOuterDeclJavaMirror || !isOuterDeclInterface || !isCurrentDeclFunc;

    if (isInvalidScope) {
        DiagJavaHasDefaultIncorrectScope(diag, ann);
    }
    if (decl.TestAttr(Attribute::STATIC)) {
        DiagJavaHasDefaultConflictWithStatic(diag, decl);
    }
}

} // namespace Cangjie::Interop::Java