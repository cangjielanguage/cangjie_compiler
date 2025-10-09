// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements generating Objective-C glue code.
 */

#include "NativeFFI/ObjC/ObjCCodeGenerator/ObjCGenerator.h"
#include "Handlers.h"
#include "cangjie/Mangle/BaseMangler.h"

using namespace Cangjie::AST;
using namespace Cangjie::Interop::ObjC;

void GenerateGlueCode::HandleImpl(InteropContext& ctx)
{
    for (auto& impl : ctx.impls) {
        if (impl->TestAnyAttr(Attribute::IS_BROKEN, Attribute::HAS_BROKEN)) {
            continue;
        }

        auto codegen = ObjCGenerator(ctx, impl, "objc-gen", ctx.cjLibOutputPath);
        codegen.Generate();
    }
}
