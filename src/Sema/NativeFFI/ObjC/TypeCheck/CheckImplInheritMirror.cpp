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

using namespace Cangjie::AST;
using namespace Cangjie::Interop::ObjC;

void CheckImplInheritMirror::HandleImpl(TypeCheckContext& ctx)
{
    if (!ctx.typeMapper.IsObjCImpl(*ctx.target.ty)) {
        return;
    }

    // TODO: remove the whole if when hierarchy root @ObjCImpl is supported
    if (auto classTy = DynamicCast<ClassTy*>(ctx.target.ty); classTy) {
        auto hasOnlyMirrorSuperInterfaces = classTy->GetSuperInterfaceTys().size() > 0;
        for (auto superInterfaceTy : classTy->GetSuperInterfaceTys()) {
            if (!ctx.typeMapper.IsValidObjCMirror(*superInterfaceTy)) {
                hasOnlyMirrorSuperInterfaces = false;
                break;
            }
        }

        if (hasOnlyMirrorSuperInterfaces && (!classTy->GetSuperClassTy() || classTy->GetSuperClassTy()->IsObject())) {
            ctx.diag.DiagnoseRefactor(DiagKindRefactor::sema_objc_impl_must_have_objc_mirror_super_class, ctx.target);
            ctx.target.EnableAttr(Attribute::IS_BROKEN);
        }
    }

    if (ctx.typeMapper.IsValidObjCMirrorSubtype(*ctx.target.ty)) {
        return;
    }

    ctx.diag.DiagnoseRefactor(DiagKindRefactor::sema_objc_mirror_subtype_must_inherit_mirror, ctx.target);
    ctx.target.EnableAttr(Attribute::IS_BROKEN);
}
