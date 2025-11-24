// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements inserting an accessor instance method for `$obj: NativeObjCId` field for each @ObjCMirror
 * interface declaration(according to inheritance).
 */

#include "Handlers.h"
#include "NativeFFI/ObjC/Utils/Common.h"

using namespace Cangjie::AST;
using namespace Cangjie::Interop::ObjC;

void InsertNativeHandleGetterDecl::HandleImpl(InteropContext& ctx)
{
    if (interopType == InteropType::Fwd_Class) {
        for (auto& fwdClass : ctx.fwdClasses) {
            if (fwdClass->TestAttr(Attribute::IS_BROKEN)) {
                continue;
            }
            if (GetNativeHandleGetter(*fwdClass)) {
                continue;
            }

            auto nativeHandleGetterDecl = ctx.factory.CreateNativeHandleGetterDecl(*fwdClass);
            CJC_NULLPTR_CHECK(nativeHandleGetterDecl);
            fwdClass->body->decls.emplace_back(std::move(nativeHandleGetterDecl));
        }
        return;
    }

    for (auto& mirror : ctx.mirrors) {
        if (mirror->TestAttr(Attribute::IS_BROKEN)) {
            continue;
        }
        if (HasMirrorSuperClass(*mirror) || GetNativeHandleGetter(*mirror)) {
            continue;
        }

        auto nativeHandleGetterDecl = ctx.factory.CreateNativeHandleGetterDecl(*mirror);
        CJC_NULLPTR_CHECK(nativeHandleGetterDecl);
        switch (mirror->astKind) {
            case ASTKind::CLASS_DECL: {
                auto cd = StaticAs<ASTKind::CLASS_DECL>(mirror);
                cd->body->decls.emplace_back(std::move(nativeHandleGetterDecl));
                break;
            }
            case ASTKind::INTERFACE_DECL: {
                auto id = StaticAs<ASTKind::INTERFACE_DECL>(mirror);
                id->body->decls.emplace_back(std::move(nativeHandleGetterDecl));
                break;
            }
            default:
                // @ObjCMirror is always either an interface or a class.
                CJC_ABORT();
        }
    }

    for (auto& wrapper : ctx.synWrappers) {
        if (wrapper->TestAttr(Attribute::IS_BROKEN)) {
            continue;
        }
        if (GetNativeHandleGetter(*wrapper)) {
            continue;
        }

        auto nativeHandleGetterDecl = ctx.factory.CreateNativeHandleGetterDecl(*wrapper);
        CJC_NULLPTR_CHECK(nativeHandleGetterDecl);
        wrapper->body->decls.emplace_back(std::move(nativeHandleGetterDecl));
    }

    for (auto& impl : ctx.impls) {
        if (impl->TestAttr(Attribute::IS_BROKEN)) {
            continue;
        }

        if (HasMirrorSuperClass(*impl) || GetNativeHandleGetter(*impl)) {
            continue;
        }

        CJC_ASSERT(HasMirrorSuperInterface(*impl));
        auto nativeHandleGetterDecl = ctx.factory.CreateNativeHandleGetterDecl(*impl);
        CJC_NULLPTR_CHECK(nativeHandleGetterDecl);
        impl->body->decls.emplace_back(std::move(nativeHandleGetterDecl));
    }
}
