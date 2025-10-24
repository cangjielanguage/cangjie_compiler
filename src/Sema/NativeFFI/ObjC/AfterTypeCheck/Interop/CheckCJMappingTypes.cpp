// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements typechecking pipeline for CJMapping which is used in Objctive-C side.
 */

#include "Handlers.h"
#include "NativeFFI/ObjC/TypeCheck/Handlers.h"

using namespace Cangjie::AST;
using namespace Cangjie::Interop::ObjC;

void CheckCJMappingTypes::HandleImpl(InteropContext& ctx)
{
    auto checker = HandlerFactory<TypeCheckContext>::Start<CheckInheritanceInterface>()
                       .Use<CheckGeneric>()
                       .Use<CheckMemberTypes>(InteropType::CJ_Mapping);

    for (auto cjmapping : ctx.cjMappings) {
        auto typeCheckCtx = TypeCheckContext(*cjmapping, ctx.diag, ctx.typeMapper,  ctx.typeManager);
        checker.Handle(typeCheckCtx);
    }
}