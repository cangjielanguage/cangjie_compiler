// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements typechecking pipeline for Objective-C mirrors.
 */

#include "NativeFFI/ObjC/TypeCheck/Handlers.h"
#include "Handlers.h"

using namespace Cangjie::AST;
using namespace Cangjie::Interop::ObjC;

void CheckMirrorTypes::HandleImpl(InteropContext& ctx)
{
    auto checker = HandlerFactory<TypeCheckContext>::Start<CheckInterface>()
                       .Use<CheckAbstractClass>()
                       .Use<CheckMirrorInheritMirror>()
                       .Use<CheckMemberTypes>(InteropType::ObjC_Mirror);

    for (auto& mirror : ctx.mirrors) {
        auto typeCheckCtx = TypeCheckContext(*mirror, ctx.diag, ctx.typeMapper);

        checker.Handle(typeCheckCtx);
    }

    auto funcChecker = HandlerFactory<TypeCheckContext>::Start<CheckTopLevelFuncTypes>();

    for (auto& mirror : ctx.mirrorTopLevelFuncs) {
        auto typeCheckCtx = TypeCheckContext(*mirror, ctx.diag, ctx.typeMapper);

        funcChecker.Handle(typeCheckCtx);
    }
}