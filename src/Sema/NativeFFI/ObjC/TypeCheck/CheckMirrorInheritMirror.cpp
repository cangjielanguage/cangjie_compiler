// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements check that @ObjCMirror annotated declaration inherits an @ObjCMirror declaration or none of
 * them.
 */

#include "Handlers.h"
#include "cangjie/AST/Match.h"

using namespace Cangjie::AST;
using namespace Cangjie::Interop::ObjC;

void CheckMirrorInheritMirror::HandleImpl(TypeCheckContext& ctx)
{
    if (ctx.typeMapper.IsValidObjCMirror(*ctx.target.ty)) {
        return;
    }

    ctx.diag.DiagnoseRefactor(DiagKindRefactor::sema_objc_mirror_must_inherit_mirror, ctx.target);
    ctx.target.EnableAttr(Attribute::IS_BROKEN);
}