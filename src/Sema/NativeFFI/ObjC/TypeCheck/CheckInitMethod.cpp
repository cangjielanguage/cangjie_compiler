// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements checks of @ObjCInit annotated method.
 */

#include "Handlers.h"
#include "NativeFFI/ObjC/Utils/Common.h"
#include "cangjie/AST/Match.h"

using namespace Cangjie::AST;
using namespace Cangjie::Interop::ObjC;

void CheckInitMethod::HandleImpl(TypeCheckContext& ctx)
{
    if (!TypeMapper::IsObjCMirror(ctx.target)) {
        return;
    }

    for (auto member : ctx.target.GetMemberDeclPtrs()) {
        if (!IsStaticInitMethod(*member)) {
            continue;
        }
        auto& method = *StaticAs<ASTKind::FUNC_DECL>(member);
        auto fTy = DynamicCast<FuncTy*>(method.ty);
        if (!fTy || !method.funcBody->retType) {
            continue;
        }
        auto retTy = fTy->retTy;
        auto mirrorTy = method.outerDecl->ty;
        
        if (ctx.typeManager.IsTyEqual(retTy, mirrorTy)) {
            continue;
        }

        ctx.diag.DiagnoseRefactor(DiagKindRefactor::sema_mismatched_types, *method.funcBody->retType)
            .AddMainHintArguments(Ty::ToString(mirrorTy), Ty::ToString(retTy));
        ctx.target.EnableAttr(Attribute::IS_BROKEN);
    }
}
