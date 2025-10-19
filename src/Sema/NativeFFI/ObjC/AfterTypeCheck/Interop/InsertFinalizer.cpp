// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements generating and inserting a finalizer for each @ObjCMirror class:
 */

#include "Handlers.h"
#include "NativeFFI/ObjC/Utils/Common.h"
#include "NativeFFI/Utils.h"

using namespace Cangjie::AST;
using namespace Cangjie::Interop::ObjC;
using namespace Cangjie::Native::FFI;

void InsertFinalizer::HandleImpl(InteropContext& ctx)
{
    for (auto& mirror : ctx.mirrors) {
        if (mirror->TestAttr(Attribute::IS_BROKEN)) {
            continue;
        }
        auto mirrorClass = As<ASTKind::CLASS_DECL>(mirror);
        if (!mirrorClass) {
            continue;
        }

        auto hasInitedField = ctx.factory.CreateHasInitedField(*mirrorClass);
        CJC_NULLPTR_CHECK(hasInitedField);
        mirrorClass->body->decls.emplace_back(std::move(hasInitedField));

        auto finalizer = ctx.factory.CreateFinalizer(*mirrorClass);
        CJC_NULLPTR_CHECK(finalizer);
        mirrorClass->body->decls.emplace_back(std::move(finalizer));
    }
}