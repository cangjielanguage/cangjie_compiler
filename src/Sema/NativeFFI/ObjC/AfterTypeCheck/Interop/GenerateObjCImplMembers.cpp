// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements generating in @ObjCImpl declarations.
 */

#include "Handlers.h"
#include "cangjie/AST/Create.h"

using namespace Cangjie::AST;
using namespace Cangjie::Interop::ObjC;

void GenerateObjCImplMembers::HandleImpl(InteropContext& ctx)
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
                case ASTKind::FUNC_DECL:
                    if (memberDecl->TestAttr(Attribute::CONSTRUCTOR)) {
                        GenerateCtor(ctx, *impl, *StaticAs<ASTKind::FUNC_DECL>(memberDecl));
                    }
                    break;
                default:
                    break;
            }
        }
    }
}

void GenerateObjCImplMembers::GenerateCtor(InteropContext& ctx, ClassDecl& target, FuncDecl& from)
{
    auto ctor = ctx.factory.CreateImplCtor(target, from);
    CJC_ASSERT(ctor);
    target.body->decls.emplace_back(std::move(ctor));
}

