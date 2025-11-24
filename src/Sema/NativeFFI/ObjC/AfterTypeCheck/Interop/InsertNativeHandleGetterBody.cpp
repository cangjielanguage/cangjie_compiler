// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements inserting a body of `$getObj(): NativeObjCId` for each @ObjCMirror/@ObjCImpl
 */

#include "Handlers.h"
#include "NativeFFI/ObjC/Utils/Common.h"
#include "NativeFFI/Utils.h"
#include "cangjie/AST/Create.h"

namespace Cangjie::Interop::ObjC {
using namespace Cangjie::AST;
using namespace Cangjie::Native::FFI;

void InsertNativeHandleGetterBody::HandleImpl(InteropContext& ctx)
{
    if (interopType == InteropType::Fwd_Class) {
        for (auto& fwdClass : ctx.fwdClasses) {
            if (fwdClass->TestAttr(Attribute::IS_BROKEN)) {
                continue;
            }

            auto getterDecl = GetNativeHandleGetter(*fwdClass);
            CJC_NULLPTR_CHECK(getterDecl);
            auto nativeHandleFieldExpr = ctx.factory.CreateNativeHandleFieldExpr(*fwdClass);
            getterDecl->funcBody->body->body.emplace_back(std::move(nativeHandleFieldExpr));
        }
        return;
    }

    for (auto& mirror : ctx.mirrors) {
        if (mirror->TestAttr(Attribute::IS_BROKEN)) {
            continue;
        }

        if (HasMirrorSuperClass(*mirror)) {
            continue;
        }

        auto mirrorClass = As<ASTKind::CLASS_DECL>(mirror);
        if (!mirrorClass) {
            continue;
        }

        auto getterDecl = GetNativeHandleGetter(*mirrorClass);
        CJC_NULLPTR_CHECK(getterDecl);
        auto nativeHandleFieldExpr = ctx.factory.CreateNativeHandleFieldExpr(*mirrorClass);
        getterDecl->funcBody->body->body.emplace_back(std::move(nativeHandleFieldExpr));
    }

    for (auto& wrapper : ctx.synWrappers) {
        if (wrapper->TestAttr(Attribute::IS_BROKEN)) {
            continue;
        }
        auto getterDecl = GetNativeHandleGetter(*wrapper);
        CJC_NULLPTR_CHECK(getterDecl);
        auto nativeHandleFieldExpr = ctx.factory.CreateNativeHandleFieldExpr(*wrapper);
        getterDecl->funcBody->body->body.emplace_back(std::move(nativeHandleFieldExpr));
    }

    for (auto& impl : ctx.impls) {
        if (impl->TestAttr(Attribute::IS_BROKEN)) {
            continue;
        }

        if (HasMirrorSuperClass(*impl)) {
            continue;
        }

        auto getterDecl = GetNativeHandleGetter(*impl);
        CJC_NULLPTR_CHECK(getterDecl);
        auto nativeHandleFieldExpr = ctx.factory.CreateNativeHandleFieldExpr(*impl);
        getterDecl->funcBody->body->body.emplace_back(std::move(nativeHandleFieldExpr));
    }
}

} // namespace Cangjie::Interop::ObjC
