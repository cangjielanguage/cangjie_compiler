// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements desugar synthetic mirror interface wrappers.
 */

#include "Handlers.h"
#include "NativeFFI/ObjC/Utils/Common.h"
#include "NativeFFI/Utils.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"
#include "cangjie/Utils/CheckUtils.h"

namespace Cangjie::Interop::ObjC {
using namespace Cangjie::AST;
using namespace Cangjie::Native::FFI;

void DesugarSyntheticWrappers::HandleImpl(InteropContext& ctx)
{
    for (auto& wrapper : ctx.synWrappers) {
        if (wrapper->TestAttr(Attribute::IS_BROKEN)) {
            continue;
        }
        wrapper->DisableAttr(Attribute::ABSTRACT);
    }
}
} // namespace Cangjie::Interop::ObjC
