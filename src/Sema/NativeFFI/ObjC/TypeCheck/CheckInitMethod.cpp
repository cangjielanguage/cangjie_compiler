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
#include "NativeFFI/ObjC/Utils/ASTQuery.h"
#include "cangjie/AST/Match.h"

using namespace Cangjie::AST;
using namespace Cangjie::Interop::ObjC;

void CheckInitMethod::HandleImpl(TypeCheckContext& ctx)
{
    if (!ctx.target.IsObjCForeign()) {
        return;
    }

    for (auto member : ctx.target.GetMemberDeclPtrs()) {
        auto method = As<ASTKind::FUNC_DECL>(member);
        if (!method || !IsObjCInitMethod(*method)) {
            continue;
        }
        auto fTy = DynamicCast<FuncTy>(method->DataTy());
        if (!fTy || !method->funcBody->retType) {
            continue;
        }
        auto retTy = fTy->retTy;
        auto mirrorTy = method->outerDecl->GetTy();
        if (ctx.typeManager.IsTyEqual(retTy, mirrorTy)) {
            continue;
        }

        ctx.diag.DiagnoseRefactor(DiagKindRefactor::sema_mismatched_types, *method->funcBody->retType)
            .AddMainHintArguments(mirrorTy.String(), retTy.String());
        ctx.target.EnableAttr(Attribute::IS_BROKEN);
    }
}
