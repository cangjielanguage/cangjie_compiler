// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements check that Objective-C mirror subtypes declaration MUST be annotated
 * either with @ObjCImpl or with @ObjCImpl (which leads to have an OBJ_C_MIRROR_SUBTYPE attribute, enabled by Parser).
 */

#include "Handlers.h"
#include "cangjie/AST/Match.h"

using namespace Cangjie::AST;
using namespace Cangjie::Interop::ObjC;

void CheckMirrorSubtypeAttr::HandleImpl(TypeCheckContext& ctx)
{
    auto& ty = *ctx.target.ty;
    if (!ctx.typeMapper.IsObjCMirrorSubtype(ty)) {
        return;
    }

    if ((ctx.typeMapper.IsValidObjCMirror(ty) || ctx.typeMapper.IsObjCImpl(ty))) {
        return;
    }

    ctx.diag.DiagnoseRefactor(DiagKindRefactor::sema_objc_mirror_subtype_must_be_annotated, ctx.target);
    ctx.target.EnableAttr(Attribute::IS_BROKEN);
}