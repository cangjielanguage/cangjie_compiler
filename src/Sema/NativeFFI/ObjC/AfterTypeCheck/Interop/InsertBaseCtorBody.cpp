// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements generating and inserting a constructor of handle
 */

#include "Handlers.h"
#include "NativeFFI/ObjC/Utils/Common.h"
#include "NativeFFI/Utils.h"
#include "cangjie/AST/Create.h"

namespace Cangjie::Interop::ObjC {

using namespace Cangjie::AST;
using namespace Cangjie::Native::FFI;

void InsertBaseCtorBody::HandleImpl(InteropContext& ctx)
{
    for (auto& mirror : ctx.mirrors) {
        if (mirror->TestAttr(Attribute::IS_BROKEN)) {
            continue;
        }
        auto mirrorClass = As<ASTKind::CLASS_DECL>(mirror);
        if (!mirrorClass) {
            continue;
        }

        auto ctor = ctx.factory.GetGeneratedBaseCtor(*mirrorClass);
        CJC_NULLPTR_CHECK(ctor);
        auto curFile = ctor->curFile;

        auto handleParam = WithinFile(CreateRefExpr(*ctor->funcBody->paramLists[0]->params[0]), curFile);

        if (HasMirrorSuperClass(*mirrorClass)) {
            auto superCtor = ctx.factory.GetGeneratedBaseCtor(*mirrorClass->GetSuperClassDecl());
            auto superCall = CreateSuperCall(*mirrorClass, *superCtor, superCtor->ty);
            superCall->args.emplace_back(CreateFuncArg(std::move(handleParam)));
            ctor->funcBody->body->body.emplace_back(std::move(superCall));
        } else {
            auto lhs = ctx.factory.CreateNativeHandleFieldExpr(*mirrorClass);
            static auto unitTy = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
            auto nativeHandleAssignExpr = CreateAssignExpr(std::move(lhs), std::move(handleParam), unitTy);
            ctor->funcBody->body->body.emplace_back(std::move(nativeHandleAssignExpr));
        }
    }

    for (auto& wrapper: ctx.synWrappers) {
        if (wrapper->TestAttr(Attribute::IS_BROKEN)) {
            continue;
        }
        auto ctor = ctx.factory.GetGeneratedBaseCtor(*wrapper);
        CJC_NULLPTR_CHECK(ctor);
        auto curFile = ctor->curFile;

        auto handleParam = WithinFile(CreateRefExpr(*ctor->funcBody->paramLists[0]->params[0]), curFile);
        auto lhs = ctx.factory.CreateNativeHandleFieldExpr(*wrapper);
        static auto unitTy = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
        auto nativeHandleAssignExpr = CreateAssignExpr(std::move(lhs), std::move(handleParam), unitTy);
        ctor->funcBody->body->body.emplace_back(std::move(nativeHandleAssignExpr));
    }

    for (auto& impl : ctx.impls) {
        if (impl->TestAttr(Attribute::IS_BROKEN)) {
            continue;
        }

        if (HasMirrorSuperClass(*impl)) {
            continue;
        }

        CJC_ASSERT(HasMirrorSuperInterface(*impl));

        auto ctor = ctx.factory.GetGeneratedBaseCtor(*impl);
        CJC_NULLPTR_CHECK(ctor);
        auto curFile = ctor->curFile;

        auto handleParam = WithinFile(CreateRefExpr(*ctor->funcBody->paramLists[0]->params[0]), curFile);
        auto lhs = ctx.factory.CreateNativeHandleFieldExpr(*impl);
        static auto unitTy = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
        auto nativeHandleAssignExpr = CreateAssignExpr(std::move(lhs), std::move(handleParam), unitTy);
        ctor->funcBody->body->body.emplace_back(std::move(nativeHandleAssignExpr));
    }
}

} // namespace Cangjie::Interop::ObjC
