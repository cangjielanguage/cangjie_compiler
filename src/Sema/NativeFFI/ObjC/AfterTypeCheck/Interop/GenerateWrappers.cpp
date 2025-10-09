// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements desugaring of Objective-C mirror subtypes.
 */

#include "NativeFFI/ObjC/Utils/Common.h"
#include "Handlers.h"
#include "cangjie/AST/Match.h"

using namespace Cangjie::AST;
using namespace Cangjie::Interop::ObjC;

void GenerateWrappers::HandleImpl(InteropContext& ctx)
{
    for (auto& impl : ctx.impls) {
        if (impl->TestAttr(Attribute::IS_BROKEN)) {
            continue;
        }

        for (auto& memberDecl : impl->GetMemberDeclPtrs()) {
            if (memberDecl->TestAnyAttr(Attribute::IS_BROKEN, Attribute::CONSTRUCTOR)) {
                continue;
            }
            if (!memberDecl->TestAnyAttr(Attribute::PUBLIC)) {
                continue;
            }

            if (ctx.factory.IsGeneratedMember(*memberDecl)) {
                continue;
            }

            switch (memberDecl->astKind) {
                case ASTKind::FUNC_DECL:
                    GenerateWrapper(ctx, *StaticAs<ASTKind::FUNC_DECL>(memberDecl));
                    break;
                case ASTKind::PROP_DECL:
                    GenerateWrapper(ctx, *StaticAs<ASTKind::PROP_DECL>(memberDecl));
                    break;
                case ASTKind::VAR_DECL:
                    GenerateWrapper(ctx, *StaticAs<ASTKind::VAR_DECL>(memberDecl));
                    break;
                default:
                    break;
            }
        }
    }
}

void GenerateWrappers::GenerateWrapper(InteropContext& ctx, FuncDecl& method)
{
    auto wrapper = ctx.factory.CreateMethodWrapper(method);
    CJC_NULLPTR_CHECK(wrapper);
    ctx.genDecls.emplace_back(std::move(wrapper));
}

void GenerateWrappers::GenerateWrapper(InteropContext& ctx, PropDecl& prop)
{
    auto wrapper = ctx.factory.CreateGetterWrapper(prop);
    CJC_NULLPTR_CHECK(wrapper);
    ctx.genDecls.emplace_back(std::move(wrapper));

    if (prop.isVar) {
        GenerateSetterWrapper(ctx, prop);
    }
}

void GenerateWrappers::GenerateSetterWrapper(InteropContext& ctx, PropDecl& prop)
{
    auto wrapper = ctx.factory.CreateSetterWrapper(prop);
    CJC_NULLPTR_CHECK(wrapper);
    ctx.genDecls.emplace_back(std::move(wrapper));
}

void GenerateWrappers::GenerateWrapper(InteropContext& ctx, VarDecl& field)
{
    if (ctx.factory.IsGeneratedNativeHandleField(field)) {
        return;
    }

    auto wrapper = ctx.factory.CreateGetterWrapper(field);
    CJC_NULLPTR_CHECK(wrapper);
    ctx.genDecls.emplace_back(std::move(wrapper));

    if (field.isVar) {
        GenerateSetterWrapper(ctx, field);
    }
}

void GenerateWrappers::GenerateSetterWrapper(InteropContext& ctx, VarDecl& field)
{
    auto wrapper = ctx.factory.CreateSetterWrapper(field);
    CJC_NULLPTR_CHECK(wrapper);
    ctx.genDecls.emplace_back(std::move(wrapper));
}
