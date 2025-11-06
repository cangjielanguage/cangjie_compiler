// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "TypeCheckAnnotation.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"
#include "cangjie/Utils/CheckUtils.h"

namespace Cangjie::Interop::ObjC {

void CheckForeignSetterNameAnnotation(DiagnosticEngine& diag, const Annotation& ann, const Decl& decl)
{
    CJC_ASSERT(ann.kind == AnnotationKind::FOREIGN_SETTER_NAME);

    if (decl.astKind == AST::ASTKind::PROP_DECL) {
        auto propDecl = StaticAs<ASTKind::PROP_DECL>(&decl);
        if (!propDecl->isVar) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_objc_setter_name_on_immutable_prop, ann);
            return;
        }
    }
}

} // namespace Cangjie::Interop::ObjC
