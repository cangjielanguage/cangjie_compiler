// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements desugaring of @ObjCImpl.
 */

#include "Handlers.h"
#include "NativeFFI/ObjC/Utils/Common.h"
#include "NativeFFI/Utils.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Walker.h"

using namespace Cangjie::AST;
using namespace Cangjie::Native::FFI;
using namespace Cangjie::Interop::ObjC;

void DesugarImpls::HandleImpl(InteropContext& ctx)
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
                case ASTKind::FUNC_DECL: {
                    auto& fd = *StaticAs<ASTKind::FUNC_DECL>(memberDecl);
                    Desugar(ctx, *impl, fd);
                    break;
                }
                case ASTKind::PROP_DECL: {
                    Desugar(ctx, *impl, *StaticAs<ASTKind::PROP_DECL>(memberDecl));
                    break;
                }
                default:
                    break;
            }
        }
    }
}

void DesugarImpls::Desugar(InteropContext& ctx, ClassDecl& impl, FuncDecl& method)
{
    // We are interested in:
    // 1. CallExpr to MemberAccess, as it could be `super.<member>(...)`
    // 2. MemberAccess, as it could be a prop getter call
    Walker(method.funcBody->body.get(), [&](auto node) {
        if (node->TestAnyAttr(Attribute::HAS_BROKEN, Attribute::IS_BROKEN)) {
            return VisitAction::SKIP_CHILDREN;
        }

        switch (node->astKind) {
            case ASTKind::CALL_EXPR:
                DesugarCallExpr(ctx, impl, *StaticAs<ASTKind::CALL_EXPR>(node));
                break;
            case ASTKind::MEMBER_ACCESS:
                DesugarGetForPropDecl(ctx, impl, *StaticAs<ASTKind::MEMBER_ACCESS>(node));
                break;
            default:
                break;
        }

        return VisitAction::WALK_CHILDREN;
    }).Walk();
}

void DesugarImpls::Desugar(InteropContext& ctx, ClassDecl& impl, PropDecl& prop)
{
    for (auto& getter : prop.getters) {
        Desugar(ctx, impl, *getter.get());
    }

    for (auto& setter : prop.setters) {
        Desugar(ctx, impl, *setter.get());
    }
}

void DesugarImpls::DesugarCallExpr(InteropContext& ctx, ClassDecl& impl, CallExpr& ce)
{
    if (ce.TestAnyAttr(Attribute::UNREACHABLE, Attribute::LEFT_VALUE)) {
        return;
    }

    if (ce.desugarExpr || !ce.baseFunc || !ce.resolvedFunction) {
        return;
    }

    if (ce.callKind != CallKind::CALL_SUPER_FUNCTION) {
        return;
    }

    auto fd = StaticAs<ASTKind::FUNC_DECL>(ce.resolvedFunction);
    if (!ctx.typeMapper.IsObjCMirror(*fd->outerDecl->ty)) {
        return;
    }
    if (fd->propDecl && fd->propDecl->TestAttr(Attribute::DESUGARED_MIRROR_FIELD)) {
        return;
    }
    auto fdTy = StaticCast<FuncTy>(fd->ty);
    auto curFile = ce.curFile;

    // super(...)
    // need implementation
    if (auto re = As<ASTKind::REF_EXPR>(ce.baseFunc); re && re->isSuper) {
        return;
    }

    std::vector<OwnedPtr<Expr>> msgSendSuperArgs;
    std::transform(ce.args.begin(), ce.args.end(), std::back_inserter(msgSendSuperArgs),
        [&](auto& arg) { return ctx.factory.UnwrapEntity(WithinFile(ASTCloner::Clone(arg->expr.get()), curFile)); });

    auto nativeHandle = ctx.factory.CreateNativeHandleExpr(impl, false, ce.curFile);
    auto withMethodEnvCall = ctx.factory.CreateWithMethodEnvScope(
        std::move(nativeHandle), fdTy->retTy, [&](auto&& receiver, auto&& objCSuper) {
            OwnedPtr<Node> msgSendSuperCall;
            if (fd->propDecl) {
                if (!msgSendSuperArgs.empty()) {
                    msgSendSuperCall = ctx.factory.CreatePropSetterCallViaMsgSendSuper(
                        *fd->propDecl, std::move(receiver), std::move(objCSuper), std::move(msgSendSuperArgs[0]));
                } else {
                    msgSendSuperCall = ctx.factory.CreatePropGetterCallViaMsgSendSuper(
                        *fd->propDecl, std::move(receiver), std::move(objCSuper));
                }
            } else {
                msgSendSuperCall = ctx.factory.CreateMethodCallViaMsgSendSuper(
                    *fd, std::move(receiver), std::move(objCSuper), std::move(msgSendSuperArgs));
            }

            return Nodes<Node>(std::move(msgSendSuperCall));
        });
    withMethodEnvCall->curFile = ce.curFile;

    ce.desugarExpr = ctx.factory.WrapEntity(std::move(withMethodEnvCall), *fdTy->retTy);
}

void DesugarImpls::DesugarGetForPropDecl(InteropContext& ctx, ClassDecl& impl, MemberAccess& ma)
{
    if (ma.desugarExpr || ma.TestAnyAttr(Attribute::UNREACHABLE, Attribute::LEFT_VALUE)) {
        return;
    }

    auto target = ma.GetTarget();
    if (!target || target->astKind != ASTKind::PROP_DECL || target->TestAttr(Attribute::DESUGARED_MIRROR_FIELD)) {
        return;
    }

    auto isSuper = false;
    if (auto re = As<ASTKind::REF_EXPR>(ma.baseExpr); re) {
        isSuper = re->isSuper;
    }

    if (!isSuper) {
        return;
    }

    auto pd = StaticAs<ASTKind::PROP_DECL>(target);
    if (!ctx.typeMapper.IsObjCMirror(*pd->outerDecl->ty)) {
        return;
    }
    auto nativeHandle = ctx.factory.CreateNativeHandleExpr(impl, false, ma.curFile);
    auto withMethodEnvCall =
        ctx.factory.CreateWithMethodEnvScope(std::move(nativeHandle), ma.ty, [&](auto&& receiver, auto&& objCSuper) {
            auto msgSendSuperCall =
                ctx.factory.CreatePropGetterCallViaMsgSendSuper(*pd, std::move(receiver), std::move(objCSuper));

            return Nodes<Node>(std::move(msgSendSuperCall));
        });
    withMethodEnvCall->curFile = ma.curFile;
    ma.desugarExpr = ctx.factory.WrapEntity(std::move(withMethodEnvCall), *ma.ty);
}
