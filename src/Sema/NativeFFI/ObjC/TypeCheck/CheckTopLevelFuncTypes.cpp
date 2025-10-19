// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements checks for Objective-C mirror/subtype member declarations.
 */

#include "Handlers.h"
#include "cangjie/AST/Match.h"

using namespace Cangjie::AST;
using namespace Cangjie::Interop::ObjC;

void CheckTopLevelFuncTypes::HandleImpl(TypeCheckContext& ctx)
{
    auto fd = As<ASTKind::FUNC_DECL>(&ctx.target);
    if (fd == nullptr) {
        return;
    }

    for (auto& paramList : fd->funcBody->paramLists) {
        for (auto& param : paramList->params) {
            if (!ctx.typeMapper.IsObjCCompatible(*param->ty)) {
                ctx.diag.DiagnoseRefactor(DiagKindRefactor::sema_objc_interop_toplevel_param_must_be_objc_compatible,
                    *param->type, fd->identifier.Val());

                fd->EnableAttr(Attribute::IS_BROKEN);
            }
        }
    }
    if (fd->funcBody->retType && !ctx.typeMapper.IsObjCCompatible(*fd->funcBody->retType->ty)) {
        ctx.diag.DiagnoseRefactor(DiagKindRefactor::sema_objc_interop_toplevel_ret_must_be_objc_compatible,
            *fd->funcBody->retType, fd->identifier.Val());

        fd->EnableAttr(Attribute::IS_BROKEN);
    }
}
