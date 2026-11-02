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
#include "NativeFFI/ObjC/Utils/ASTQuery.h"
#include "cangjie/AST/Clone.h"
#include "cangjie/AST/Types.h"

namespace Cangjie::Interop::ObjC {

using namespace Cangjie::AST;
using namespace Cangjie::Native::FFI;

void GenerateObjCImplMembers::HandleImpl(InteropContext& ctx)
{
    for (auto& impl : ctx.impls) {
        if (impl->TestAttr(Attribute::IS_BROKEN)) {
            continue;
        }

        auto regComp = ctx.implToRegCompanion.at(impl);

        // Collect user constructors first (to avoid iterator invalidation during insertion)
        // Skip the generated base ctor
        std::vector<Ptr<FuncDecl>> userCtors;
        for (auto& memberDecl : impl->GetMemberDeclPtrs()) {
            if (memberDecl->TestAttr(Attribute::IS_BROKEN)) {
                continue;
            }
            auto fd = As<ASTKind::FUNC_DECL>(memberDecl);
            if (!fd || !fd->TestAttr(Attribute::CONSTRUCTOR)) {
                continue;
            }
            if (IsObjCExportBaseCtor(*fd)) {
                continue;
            }
            userCtors.push_back(fd);
        }

        for (auto& origin : userCtors) {
            auto regDataCtor = ASTCloner::Clone(Ptr(origin));
            ctx.astTransformer.TransformToObjCImplRegDataCtorDecl(*regDataCtor, *impl, *regComp);
            auto regDataCtorPtr = regDataCtor.get();
            ctx.astInserter.InsertInto(*impl, std::move(regDataCtor));

            ctx.astTransformer.TransformToObjCImplUserCtorBody(*origin->funcBody, *regDataCtorPtr, *impl, *regComp,
                ctx.factory.CreateGetCachedClassAccess(*StaticCast<ClassLikeTy>(impl->DataTy()), impl->curFile));
        }
    }
}

} // namespace Cangjie::Interop::ObjC
