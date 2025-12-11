// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements generate in synthetic mirror wrappers.
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

void GenerateInSyntheticWrappers::HandleImpl(InteropContext& ctx)
{
    for (auto& wrapper : ctx.synWrappers) {
        if (wrapper->TestAttr(Attribute::IS_BROKEN)) {
            continue;
        }

        CJC_ASSERT_WITH_MSG(wrapper->astKind == ASTKind::CLASS_DECL, "Synthetic wrapper expected to be a class");
        auto cd = StaticCast<ClassDecl*>(wrapper);

        if (auto id = DynamicCast<InheritableDecl*>(wrapper)) {
            GenerateSyntheticClassAbstractMemberImplStubs(*cd, ctx.structMemberSignatures.at(id));
        } else {
            CJC_ABORT_WITH_MSG("Illegal state: synthetic classes expected to be InheritableDecl*");
        }
    }
}
} // namespace Cangjie::Interop::ObjC
