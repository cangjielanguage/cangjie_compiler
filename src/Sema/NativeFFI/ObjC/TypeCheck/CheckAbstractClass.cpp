// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements check that Objective-C mirror declaration is not an abstract class, as it is not supported yet.
 */

#include "Handlers.h"
#include "cangjie/AST/Match.h"

using namespace Cangjie::AST;
using namespace Cangjie::Interop::ObjC;

void CheckAbstractClass::HandleImpl(TypeCheckContext& ctx)
{
    if (auto cd = As<ASTKind::CLASS_DECL>(&ctx.target); cd && cd->TestAttr(Attribute::ABSTRACT)) {
        ctx.diag.DiagnoseRefactor(DiagKindRefactor::sema_objc_interop_not_supported, *cd, "abstract");
        cd->EnableAttr(Attribute::IS_BROKEN);
    }
}