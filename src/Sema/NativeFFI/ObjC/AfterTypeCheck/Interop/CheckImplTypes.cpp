// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements typechecking pipeline for Objective-C mirror subtypes.
 */

#include "NativeFFI/ObjC/TypeCheck/Handlers.h"
#include "Handlers.h"

using namespace Cangjie::AST;
using namespace Cangjie::Interop::ObjC;

void CheckImplTypes::HandleImpl(InteropContext& ctx)
{
    auto checker = HandlerFactory<TypeCheckContext>::Start<CheckMultipleInherit>()
                       .Use<CheckMirrorSubtypeAttr>()
                       .Use<CheckImplInheritMirror>()
                       .Use<CheckMemberTypes>(InteropType::ObjC_Mirror);

    for (auto& impl : ctx.impls) {
        auto typeCheckCtx = TypeCheckContext(*impl, ctx.diag, ctx.typeMapper);

        checker.Handle(typeCheckCtx);
    }
}