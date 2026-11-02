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
#include "NativeFFI/ObjC/Utils/ASTQuery.h"
#include "NativeFFI/Utils.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Node.h"

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

        auto ctor = GetObjCMirrorBaseCtor(*mirrorClass);
        CJC_NULLPTR_CHECK(ctor);
        auto curFile = ctor->curFile;

        auto handleParam = WithinFile(CreateRefExpr(*ctor->funcBody->paramLists[0]->params[0]), curFile);

        if (HasObjCMirrorSuperClass(*mirrorClass)) {
            auto superCtor = GetObjCMirrorBaseCtor(*mirrorClass->GetSuperClassDecl());
            auto superCall = CreateSuperCall(*mirrorClass, *superCtor, superCtor->DataTy());
            superCall->args.emplace_back(CreateFuncArg(std::move(handleParam)));
            ctx.factory.AppendNativeObjCIdMarkerIfNeeded(*superCall, curFile);
            ctor->funcBody->body->body.emplace_back(std::move(superCall));
        } else {
            auto lhs = ctx.factory.CreateNativeHandleFieldExpr(*mirrorClass);
            static ModalTy unitTy{TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT)};
            auto nativeHandleAssignExpr = CreateAssignExpr(std::move(lhs), std::move(handleParam), {unitTy});
            ctor->funcBody->body->body.emplace_back(std::move(nativeHandleAssignExpr));
        }
    }

    for (auto& wrapper : ctx.mirrorInterfaceHandleWrappers) {
        auto ctor = GetObjCMirrorInterfaceHandleWrapperBaseCtor(*wrapper);
        CJC_NULLPTR_CHECK(ctor);
        auto curFile = ctor->curFile;

        auto handleParam = WithinFile(CreateRefExpr(*ctor->funcBody->paramLists[0]->params[0]), curFile);
        auto lhs = ctx.factory.CreateNativeHandleFieldExpr(*wrapper);
        static auto unitTy = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
        auto nativeHandleAssignExpr = CreateAssignExpr(std::move(lhs), std::move(handleParam), {unitTy});
        ctor->funcBody->body->body.emplace_back(std::move(nativeHandleAssignExpr));
    }

    for (auto& impl : ctx.impls) {
        if (impl->TestAttr(Attribute::IS_BROKEN)) {
            continue;
        }

        auto ctor = GetObjCImplBaseCtor(*impl);
        CJC_ASSERT_WITH_MSG(ctor, "expected base ctor in the @ObjCImpl class");
        ctx.astTransformer.TransformToObjCImplBaseCtorBody(*ctor->funcBody, *impl, *ctx.implToRegCompanion.at(impl));
    }

    for (auto& regComp : ctx.regCompanions) {
        // no need to check if broken, because class is just generated
        auto ctor = GetObjCImplRegCompanionBaseCtor(*regComp);
        CJC_ASSERT_WITH_MSG(ctor, "expected base ctor in the @ObjCImpl registry companion class");
        ctx.astTransformer.TransformToObjCImplRegCompBaseCtorBody(*ctor->funcBody, *regComp);
    }
}

} // namespace Cangjie::Interop::ObjC
