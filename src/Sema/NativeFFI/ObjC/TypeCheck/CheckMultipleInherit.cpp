// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements check that Objective-C mirror subtype declaration has at most one supertype (except an Object).
 */

#include "Handlers.h"

using namespace Cangjie::AST;
using namespace Cangjie::Interop::ObjC;

void CheckMultipleInherit::HandleImpl(TypeCheckContext& ctx)
{
    auto superTypesCount = 0;
    auto& inheritableDecl = dynamic_cast<InheritableDecl&>(ctx.target);
    for (auto& parent : inheritableDecl.inheritedTypes) {
        if (parent->ty && parent->ty->IsObject()) {
            continue;
        }

        superTypesCount++;
        if (superTypesCount > 1) {
            ctx.diag.DiagnoseRefactor(DiagKindRefactor::sema_objc_mirror_subtype_cannot_multiple_inherit, *parent);
            inheritableDecl.EnableAttr(Attribute::IS_BROKEN);
            return;
        }
    }
}