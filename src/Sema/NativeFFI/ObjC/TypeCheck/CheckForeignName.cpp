// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements semantic checks for @ForeignName anno
 */

#include "NativeFFI/Utils.h"
#include "Handlers.h"
#include "cangjie/AST/Match.h"

using namespace Cangjie::AST;
using namespace Cangjie::Interop::ObjC;
using namespace Cangjie::Native::FFI;

namespace {
bool MustHaveForeignName(const Decl& member)
{
    if (!member.IsFunc()) {
        return false;
    }

    auto fd = StaticAs<ASTKind::FUNC_DECL>(Ptr(&member));
    if (!fd->funcBody || fd->funcBody->paramLists.empty() || fd->funcBody->paramLists[0]->params.empty()) {
        // no params
        return false;
    }

    // if the fd has 1 param we still can assume that selector must be fd.identifier + ":".
    // If the fd has more than 1 param, then we are unable to provide a proper objc name.
    return fd->funcBody->paramLists[0]->params.size() > 1;
}

bool HasForeignName(const Decl& member)
{
    return GetForeignNameAnnotation(member) != nullptr;
}

} // namespace

void CheckForeignName::HandleImpl(TypeCheckContext& ctx)
{
    auto targetKind = ctx.typeMapper.IsObjCMirror(ctx.target) ? "@ObjCMirror" : "@ObjCImpl";
    for (auto memberDecl : ctx.target.GetMemberDeclPtrs()) {
        if (memberDecl->TestAttr(Attribute::IS_BROKEN)) {
            continue;
        }

        if (ctx.typeMapper.IsObjCImpl(*ctx.target.ty) && !memberDecl->TestAttr(Attribute::PUBLIC)) {
            continue;
        }

        // Only public members of exported declarations must be checked.
        if (!MustHaveForeignName(*memberDecl) || HasForeignName(*memberDecl)) {
            continue;
        }

        memberDecl->EnableAttr(Attribute::IS_BROKEN);
        ctx.target.EnableAttr(Attribute::HAS_BROKEN);
        if (memberDecl->TestAttr(Attribute::CONSTRUCTOR)) {
            ctx.diag.DiagnoseRefactor(DiagKindRefactor::sema_objc_ctor_must_have_foreign_name, *memberDecl, targetKind);
        } else {
            ctx.diag.DiagnoseRefactor(DiagKindRefactor::sema_objc_method_must_have_foreign_name, *memberDecl, targetKind, memberDecl->identifier);
        }
    }
}
