// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements check the interop with Objective-C declaration which is not an generic, as it is not supported
 * yet.
 */

#include "Handlers.h"
#include "cangjie/AST/Match.h"

using namespace Cangjie::AST;
using namespace Cangjie::Interop::ObjC;

void CheckGeneric::HandleImpl(TypeCheckContext& ctx)
{
    if (ctx.target.generic) {
        ctx.diag.DiagnoseRefactor(DiagKindRefactor::sema_objc_cjmapping_generic_not_supported,
            MakeRange(ctx.target.generic->leftAnglePos, ctx.target.generic->rightAnglePos),
            std::string{ctx.target.identifier});
    }
}