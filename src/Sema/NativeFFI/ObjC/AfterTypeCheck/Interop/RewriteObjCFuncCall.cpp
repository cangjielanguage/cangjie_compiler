// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements desugaring of ObjCPointer struct accessors
 */


#include "NativeFFI/Utils.h"
#include "NativeFFI/ObjC/Utils/Common.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Clone.h"
#include "Handlers.h"

using namespace Cangjie::AST;
using namespace Cangjie::Interop::ObjC;
using namespace Cangjie::Native::FFI;
using namespace Cangjie;

namespace {

Ptr<Decl> GetDirectlyReferencedMemberDecl(Ptr<Expr> expr)
{
    Ptr<Decl> referencedDecl;
    if (Ptr<MemberAccess> ma = As<ASTKind::MEMBER_ACCESS>(expr)) {
        referencedDecl = ma->target;
    } else if (Ptr<RefExpr> ref = As<ASTKind::REF_EXPR>(expr)) {
        referencedDecl = ref->ref.target;
    }
    return referencedDecl;
}

OwnedPtr<Expr> CreateReceiverExpr(Ptr<Expr> expr)
{
    OwnedPtr<Expr> referencedExpr;
    if (Ptr<MemberAccess> ma = As<ASTKind::MEMBER_ACCESS>(expr)) {
        referencedExpr = ASTCloner::Clone(ma->baseExpr.get());
    } else if (Ptr<RefExpr> ref = As<ASTKind::REF_EXPR>(expr)) {
        auto owningDecl = ref->ref.target->outerDecl;
        referencedExpr = CreateThisRef(owningDecl, owningDecl->ty, expr->curFile);
    }
    return referencedExpr;
}

enum class ObjCFTypeKind {
    BLOCK, FUNCPTR, NONE
};

ObjCFTypeKind FTypeKindByCallPropertyDecl(TypeMapper& tm, Ptr<Decl> decl)
{
    if (decl == nullptr
     || decl->astKind != ASTKind::PROP_DECL
     || decl->outerDecl == nullptr
     || decl->identifier != "call") {
        return ObjCFTypeKind::NONE;
    }
    auto typeDecl = decl->outerDecl;
    if (tm.IsObjCFunc(*typeDecl)) {
        return ObjCFTypeKind::FUNCPTR;
    }
    if (tm.IsObjCBlock(*typeDecl)) {
        return ObjCFTypeKind::BLOCK;
    }

    return ObjCFTypeKind::NONE;
}

} // namespace

void RewriteObjCFuncCall::HandleImpl(InteropContext& ctx)
{
    for (auto& file : ctx.pkg.files) {
        std::set<Ptr<Node>> safeUsage;
        Walker(file, Walker::GetNextWalkerID(), [&file, &ctx, &safeUsage](Ptr<Node> node) {
            if (!node->IsSamePackage(*file->curPackage)) {
                return VisitAction::WALK_CHILDREN;
            }
            if (node->astKind != ASTKind::CALL_EXPR
              && node->astKind != ASTKind::MEMBER_ACCESS
              && node->astKind != ASTKind::REF_EXPR) {
                return VisitAction::WALK_CHILDREN;
            }
            if (safeUsage.count(node)) {
                return VisitAction::WALK_CHILDREN;
            }
            auto decl = GetDirectlyReferencedMemberDecl(StaticCast<Expr>(node));

            if (FTypeKindByCallPropertyDecl(ctx.typeMapper, decl) != ObjCFTypeKind::NONE) {
                ctx.diag.DiagnoseRefactor(
                    DiagKindRefactor::sema_objc_func_call_property_can_only_be_called,
                    *node,
                    decl->outerDecl->identifier.Val());
                node->EnableAttr(Attribute::IS_BROKEN);
                return VisitAction::WALK_CHILDREN;
            }

            Ptr<CallExpr> callExpr = As<ASTKind::CALL_EXPR>(node);
            if (!callExpr) {
                return VisitAction::WALK_CHILDREN;
            }

            auto& ma = callExpr->baseFunc;
            Ptr<Decl> referencedDecl = GetDirectlyReferencedMemberDecl(ma);
            auto fTypeKind = FTypeKindByCallPropertyDecl(ctx.typeMapper, referencedDecl);
            if (fTypeKind == ObjCFTypeKind::NONE) {
                return VisitAction::WALK_CHILDREN;
            }
            safeUsage.insert(ma);
            std::vector<OwnedPtr<Expr>> unwrappedArguments;
            auto tmpVar = CreateTmpVarDecl(nullptr, CreateReceiverExpr(ma));
            if (fTypeKind == ObjCFTypeKind::BLOCK) {
                unwrappedArguments.push_back(
                    CreateMemberCall(
                        WithinFile(CreateRefExpr(*tmpVar), node->curFile),
                        ctx.bridge.GetObjCBlockAbiPointerAccessor()));
            }
            for (auto& originalArg : callExpr->args) {
                OwnedPtr<Expr> cloned = ASTCloner::Clone(originalArg->expr.get());
                unwrappedArguments.push_back(
                    ctx.factory.UnwrapEntity(std::move(cloned)));
            }
            auto fptrAccessor = fTypeKind == ObjCFTypeKind::BLOCK ?
                ctx.bridge.GetObjCBlockFPointerAccessor() : ctx.bridge.GetObjCFuncFPointerAccessor();

            auto call = ctx.factory.CreateFuncCallViaOpaquePointer(
                CreateMemberCall(
                    WithinFile(CreateRefExpr(*tmpVar), node->curFile),
                    fptrAccessor),
                ctx.typeMapper.Cj2CType(callExpr->ty),
                std::move(unwrappedArguments));
            std::vector<OwnedPtr<Node>> block;
            block.emplace_back(std::move(tmpVar));
            block.emplace_back(ctx.factory.WrapEntity(std::move(call), *callExpr->ty));
            ctx.factory.SetDesugarExpr(
                callExpr,
                CreateBlock(
                    std::move(block),
                    callExpr->ty)
            );
            return VisitAction::WALK_CHILDREN;
        }).Walk();
    }
}
