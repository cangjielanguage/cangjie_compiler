// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements inserting NativeObjCId field in Objective-C mirrors
 */

#include "NativeFFI/ObjC/Utils/Common.h"
#include "Handlers.h"

using namespace Cangjie::AST;
using namespace Cangjie::Interop::ObjC;

void InsertNativeHandleField::HandleImpl(InteropContext& ctx)
{
    for (auto& mirror : ctx.mirrors) {
        if (mirror->TestAttr(Attribute::IS_BROKEN)) {
            continue;
        }
        auto mirrorClass = As<ASTKind::CLASS_DECL>(mirror);
        if (!mirrorClass || HasMirrorSuperClass(*mirrorClass)) {
            continue;
        }

        auto nativeObjCIdField = ctx.factory.CreateNativeHandleField(*mirrorClass);
        mirrorClass->body->decls.emplace_back(std::move(nativeObjCIdField));
    }
}
