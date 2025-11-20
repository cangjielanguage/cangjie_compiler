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
    auto genGlueCode = [this, &ctx](Decl& decl) {
        if (decl.TestAnyAttr(Attribute::IS_BROKEN, Attribute::HAS_BROKEN)) {
            return;
        }
        auto codegen = ObjCGenerator(ctx, &decl, "objc-gen", ctx.cjLibOutputPath, this->interopType);
        codegen.Generate();
    };

    if (interopType == InteropType::ObjC_Mirror) {
        for (auto& impl : ctx.impls) {
            genGlueCode(*impl);
        }
    } else if (interopType == InteropType::CJ_Mapping) {
        for (auto& cjmapping : ctx.cjMappings) {
            genGlueCode(*cjmapping);
        }
    } else if (interopType == InteropType::CJ_Mapping_Interface) {
        for (auto& cjmapping : ctx.cjMappingInterfaces) {
            genGlueCode(*cjmapping);
        }
    }
}
