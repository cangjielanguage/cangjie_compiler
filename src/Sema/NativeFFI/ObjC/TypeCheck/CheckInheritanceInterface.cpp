// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements check that interop with Objective-C declaration doesn't inheritant an interface.
 */

#include "Handlers.h"
#include "cangjie/AST/Match.h"

using namespace Cangjie::AST;
using namespace Cangjie::Interop::ObjC;

void CheckInheritanceInterface::HandleImpl(TypeCheckContext& ctx)
{
    auto& inheritableDecl = dynamic_cast<InheritableDecl&>(ctx.target);
    for (auto& parent : inheritableDecl.inheritedTypes) {
        if (parent->ty && parent->ty->IsInterface()) {
            ctx.diag.DiagnoseRefactor(DiagKindRefactor::sema_objc_cjmapping_inheritance_interface_not_supported, *parent);
            inheritableDecl.EnableAttr(Attribute::IS_BROKEN);
            return;
        }
    }
}