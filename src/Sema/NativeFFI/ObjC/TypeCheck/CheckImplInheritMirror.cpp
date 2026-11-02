// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements check that @ObjCImpl annotated declaration inherits an @ObjCMirror declaration.
 */

#include "Handlers.h"
#include "cangjie/AST/Match.h"
#include "NativeFFI/ObjC/Utils/ASTQuery.h"

using namespace Cangjie::AST;
using namespace Cangjie::Interop::ObjC;

void CheckImplInheritMirror::HandleImpl(TypeCheckContext& ctx)
{
    if (!IsObjCExport(*ctx.target.GetTy())) {
        return;
    }

    if (auto classTy = DynamicCast<ClassTy>(ctx.target.DataTy())) {
        auto hasOnlyMirrorSuperInterfaces = classTy->GetSuperInterfaceTys().size() > 0;
        for (auto superInterfaceTy : classTy->GetSuperInterfaceTys()) {
            if (!IsObjCForeign(*superInterfaceTy)) {
                hasOnlyMirrorSuperInterfaces = false;
                break;
            }
        }

        if (hasOnlyMirrorSuperInterfaces && (!classTy->GetSuperClassTy() || classTy->GetSuperClassTy()->IsObject())) {
            ctx.diag.DiagnoseRefactor(DiagKindRefactor::sema_objc_impl_must_have_objc_mirror_super_class, ctx.target);
            ctx.target.EnableAttr(Attribute::IS_BROKEN);
        }
    }

    if (IsValidObjCMirrorSubtype(*ctx.target.GetTy())) {
        return;
    }

    ctx.diag.DiagnoseRefactor(DiagKindRefactor::sema_objc_mirror_subtype_must_inherit_mirror, ctx.target);
    ctx.target.EnableAttr(Attribute::IS_BROKEN);
}
