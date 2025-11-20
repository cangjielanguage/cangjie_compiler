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
    if (interopType == InteropType::Fwd_Class) {
        for (auto& fwdClass : ctx.fwdClasses) {
            if (fwdClass->TestAttr(Attribute::IS_BROKEN)) {
                continue;
            }

            auto nativeObjCIdField = ctx.factory.CreateNativeHandleField(*fwdClass);
            CJC_NULLPTR_CHECK(nativeObjCIdField);
            fwdClass->body->decls.emplace_back(std::move(nativeObjCIdField));
        }
        return;
    }

    for (auto& mirror : ctx.mirrors) {
        if (mirror->TestAttr(Attribute::IS_BROKEN)) {
            continue;
        }
        auto mirrorClass = As<ASTKind::CLASS_DECL>(mirror);
        if (!mirrorClass || HasMirrorSuperClass(*mirrorClass)) {
            continue;
        }

        auto nativeObjCIdField = ctx.factory.CreateNativeHandleField(*mirrorClass);
        CJC_NULLPTR_CHECK(nativeObjCIdField);
        mirrorClass->body->decls.emplace_back(std::move(nativeObjCIdField));
    }

    for (auto& wrapper: ctx.synWrappers) {
        if (wrapper->TestAttr(Attribute::IS_BROKEN)) {
            continue;
        }
        auto nativeObjCIdField = ctx.factory.CreateNativeHandleField(*wrapper);
        CJC_NULLPTR_CHECK(nativeObjCIdField);
        wrapper->body->decls.emplace_back(std::move(nativeObjCIdField));
    }

    for (auto& impl : ctx.impls) {
        if (impl->TestAttr(Attribute::IS_BROKEN)) {
            continue;
        }

        if (HasMirrorSuperClass(*impl)) {
            continue;
        }

        CJC_ASSERT(HasMirrorSuperInterface(*impl));

        auto nativeObjCIdField = ctx.factory.CreateNativeHandleField(*impl);
        CJC_NULLPTR_CHECK(nativeObjCIdField);
        impl->body->decls.emplace_back(std::move(nativeObjCIdField));
    }
}
