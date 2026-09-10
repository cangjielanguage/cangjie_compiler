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

#include "Diags.h"
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
        auto fTy = DynamicCast<FuncTy*>(method.DataTy());
        if (!fTy || !method.funcBody->retType) {
            continue;
        }
        auto retTy = fTy->retTy;
        auto mirrorTy = method.outerDecl->GetTy();
        if (ctx.typeManager.IsTyEqual(retTy, mirrorTy)) {
            continue;
        }

        Sema::DiagSemaMismatchedTypes(
            ctx.diag, *method.funcBody->retType, Ty::ToString(mirrorTy.Ty()), Ty::ToString(retTy.Ty()));
        ctx.target.EnableAttr(Attribute::IS_BROKEN);
    }
}
