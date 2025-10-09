// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements generating init Cangjie object method for @ObjCImpls.
 */

#include "Handlers.h"

using namespace Cangjie::AST;
using namespace Cangjie::Interop::ObjC;

void GenerateInitCJObjectMethods::HandleImpl(InteropContext& ctx)
{
    for (auto& impl : ctx.impls) {
        if (impl->TestAttr(Attribute::IS_BROKEN)) {
            continue;
        }

        for (auto& memberDecl : impl->GetMemberDeclPtrs()) {
            if (memberDecl->TestAttr(Attribute::IS_BROKEN)) {
                continue;
            }

            if (!memberDecl->TestAttr(Attribute::CONSTRUCTOR)) {
                continue;
            }

            if (memberDecl->astKind != ASTKind::FUNC_DECL) {
                // skip primary ctor, as it is desugared to init already
                continue;
            }

            auto& ctorDecl = *StaticAs<ASTKind::FUNC_DECL>(memberDecl);

            // skip original ctors
            if (!ctx.factory.IsGeneratedCtor(ctorDecl)) {
                continue;
            }

            auto initCjObject = ctx.factory.CreateInitCjObject(*impl, ctorDecl);
            CJC_ASSERT(initCjObject);
            ctx.genDecls.emplace_back(std::move(initCjObject));
        }
    }
}