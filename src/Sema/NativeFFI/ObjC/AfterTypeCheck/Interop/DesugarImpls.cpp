// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements desugaring of @ObjCImpl.
 */

#include "Handlers.h"
#include "NativeFFI/ObjC/Utils/Common.h"
#include "NativeFFI/Utils.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Walker.h"

using namespace Cangjie::AST;
using namespace Cangjie::Native::FFI;
using namespace Cangjie::Interop::ObjC;

void DesugarImpls::HandleImpl(InteropContext& ctx)
{
    for (auto& impl : ctx.impls) {
        if (impl->TestAttr(Attribute::IS_BROKEN)) {
            continue;
        }

        for (auto& memberDecl : impl->GetMemberDeclPtrs()) {
            if (memberDecl->TestAttr(Attribute::IS_BROKEN)) {
                continue;
            }

            switch (memberDecl->astKind) {
                case ASTKind::FUNC_DECL: {
                    auto& fd = *StaticAs<ASTKind::FUNC_DECL>(memberDecl);

                    if (fd.TestAttr(Attribute::CONSTRUCTOR)) {
                        DesugarCtor(ctx, *impl, fd);
                    } else {
                        DesugarMethod(ctx, *impl, fd);
                    }
                    break;
                }
                case ASTKind::PROP_DECL: {
                    Desugar(ctx, *impl, *StaticAs<ASTKind::PROP_DECL>(memberDecl));
                    break;
                }
                default:
                    break;
            }
        }
    }
}

void DesugarImpls::DesugarMethod(
    [[maybe_unused]]
    InteropContext& ctx,
    [[maybe_unused]]
    ClassDecl& impl,
    [[maybe_unused]]
    FuncDecl& method)
{
}

void DesugarImpls::DesugarCtor([[maybe_unused]] InteropContext& ctx, [[maybe_unused]] ClassDecl& impl, FuncDecl& ctor)
{
    if (!ctx.factory.IsGeneratedCtor(ctor)) {
        // Call this([[ImplCls alloc] init])
        ctor.funcBody->body = CreateBlock(Nodes(ctx.factory.CreateThrowUnreachableCodeExpr(*ctor.curFile)),
            TypeManager::GetPrimitiveTy(TypeKind::TYPE_NOTHING));

        return;
    }
}

void DesugarImpls::Desugar(InteropContext& ctx, ClassDecl& impl, PropDecl& prop)
{
    for (auto& getter : prop.getters) {
        DesugarMethod(ctx, impl, *getter.get());
    }

    for (auto& setter : prop.setters) {
        DesugarMethod(ctx, impl, *setter.get());
    }
}
