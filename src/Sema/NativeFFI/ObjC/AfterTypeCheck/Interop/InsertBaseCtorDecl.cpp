// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements generating and inserting a constructor declaration of native handle
 */

#include "Handlers.h"
#include "NativeFFI/ObjC/Utils/Common.h"
#include "NativeFFI/Utils.h"

namespace Cangjie::Interop::ObjC {
using namespace Cangjie::AST;
using namespace Cangjie::Native::FFI;

void InsertBaseCtorDecl::HandleImpl(InteropContext& ctx)
{
    for (auto& mirror : ctx.mirrors) {
        if (mirror->TestAttr(Attribute::IS_BROKEN)) {
            continue;
        }

        auto mirrorClass = As<ASTKind::CLASS_DECL>(mirror);
        if (!mirrorClass) {
            continue;
        }

        auto ctor = ctx.factory.CreateBaseCtorDecl(*mirrorClass);
        mirrorClass->body->decls.emplace_back(std::move(ctor));
    }

    for (auto& wrapper: ctx.synWrappers) {
        if (wrapper->TestAttr(Attribute::IS_BROKEN)) {
            continue;
        }
        auto ctor = ctx.factory.CreateBaseCtorDecl(*wrapper);
        wrapper->body->decls.emplace_back(std::move(ctor));
    }

    for (auto& impl : ctx.impls) {
        if (impl->TestAttr(Attribute::IS_BROKEN)) {
            continue;
        }

        if (HasMirrorSuperClass(*impl)) {
            continue;
        }

        CJC_ASSERT(HasMirrorSuperInterface(*impl));

        auto ctor = ctx.factory.CreateBaseCtorDecl(*impl);
        impl->body->decls.emplace_back(std::move(ctor));
    }
}
} // namespace Cangjie::Interop::ObjC
