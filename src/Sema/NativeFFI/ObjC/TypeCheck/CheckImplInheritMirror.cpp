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

    if (ctx.typeMapper.IsValidObjCMirrorSubtype(*ctx.target.ty)) {
        return;
    }

    ctx.diag.DiagnoseRefactor(DiagKindRefactor::sema_objc_mirror_subtype_must_inherit_mirror, ctx.target);
    ctx.target.EnableAttr(Attribute::IS_BROKEN);
}