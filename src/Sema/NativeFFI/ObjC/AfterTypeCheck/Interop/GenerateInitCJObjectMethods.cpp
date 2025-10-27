// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements generating init Cangjie object method for @ObjCImpls and CJMapping.
 */

#include "Handlers.h"

using namespace Cangjie::AST;
using namespace Cangjie::Interop::ObjC;

void GenerateInitCJObjectMethods::HandleImpl(InteropContext& ctx)
{
    auto genNativeInitMethod = [this, &ctx](Decl& decl) {
        if (decl.TestAttr(Attribute::IS_BROKEN)) {
            return;
        }
        for (auto& memberDecl : decl.GetMemberDeclPtrs()) {
            if (memberDecl->TestAttr(Attribute::IS_BROKEN)) {
                continue;
            }

            if (!memberDecl->TestAttr(Attribute::CONSTRUCTOR)) {
                continue;
            }

            if (!memberDecl->TestAttr(Attribute::PUBLIC)) {
                continue;
            }

            if (memberDecl->astKind != ASTKind::FUNC_DECL) {
                // skip primary ctor, as it is desugared to init already
                continue;
            }

            auto& ctorDecl = *StaticAs<ASTKind::FUNC_DECL>(memberDecl);

            // skip original ctors
            if (this->interopType == InteropType::ObjC_Mirror && !ctx.factory.IsGeneratedCtor(ctorDecl)) {
                continue;
            }
            bool forOneWayMapping = false;
            forOneWayMapping = this->interopType == InteropType::CJ_Mapping && As<ASTKind::STRUCT_DECL>(&decl);
            auto initCjObject = ctx.factory.CreateInitCjObject(decl, ctorDecl, forOneWayMapping);
            CJC_ASSERT(initCjObject);
            ctx.genDecls.emplace_back(std::move(initCjObject));
        }
    };

    if (interopType == InteropType::ObjC_Mirror) {
        for (auto& impl : ctx.impls) {
            genNativeInitMethod(*impl);
        }
    } else if (interopType == InteropType::CJ_Mapping) {
        for (auto& cjmapping : ctx.cjMappings) {
            genNativeInitMethod(*cjmapping);
        }
    }

}