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

#include "Context.h"
#include "Handlers.h"
#include "NativeFFI/ObjC/Utils/ASTQuery.h"
#include "NativeFFI/Utils.h"
#include "cangjie/AST/Clone.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Walker.h"

namespace Cangjie::Interop::ObjC {

using namespace Cangjie::AST;
using namespace Cangjie::Native::FFI;

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

namespace {
void DesugarSuperCtorCall(InteropContext& ctx, ClassDecl& impl, FuncDecl& ctor)
{
    // only super(...) in reg data ctor needs to be desugared
    if (!IsObjCExportRegDataCtor(ctor)) {
        return;
    }
    auto& ctorBody = ctor.funcBody->body->body;
    if (ctorBody.empty()) {
        return;
    }
    auto& firstExpr = ctorBody[0];
    auto ce = As<ASTKind::CALL_EXPR>(firstExpr);
    if (!ce || !ce->resolvedFunction || !IsSuperConstructorCall(*ce)) {
        return;
    }
    /**
     * super(...args)
     * -->
     * if doesn't have @ObjCImpl super class:
     * super({ => [super init:...args]}(), ...args)
     *
     * else if has @ObjCImpl super class:
     * super($regData, ...args)
     */
    auto curFile = ce->curFile;
    CJC_ASSERT_WITH_MSG(!ctor.funcBody->paramLists.empty(), "expected at least one param list");
    auto& ctorParams = ctor.funcBody->paramLists[0]->params;
    CJC_ASSERT_WITH_MSG(!ctorParams.empty(), "expected at least one parameter");
    auto regDataParamRef = CreateRefExpr(*ctorParams[0]);
    auto targetFd = ce->resolvedFunction;
    auto superClass = impl.GetSuperClassDecl();
    if (superClass->IsObjCExport()) {
        std::vector<OwnedPtr<FuncArg>> args;
        args.push_back(CreateFuncArg(ASTCloner::Clone(regDataParamRef.get())));
        args.insert(args.end(), std::make_move_iterator(ce->args.begin()), std::make_move_iterator(ce->args.end()));

        auto realTarget = GetObjCImplRegDataCtor(*superClass, *targetFd);
        auto realTargetTy = StaticCast<FuncTy>(realTarget->DataTy());
        auto superCall = CreateSuperCall(*realTarget->outerDecl, *realTarget, realTargetTy);
        superCall->args = std::move(args);
        superCall->sourceExpr = ce;
        ce->desugarExpr = std::move(superCall);
    } else {
        CJC_ASSERT_WITH_MSG(superClass->IsObjCForeign(), "expected @ObjCMirror decl");
        auto objCSelf = CreateTupleAccess(
            ASTCloner::Clone(regDataParamRef.get()), 0, {TypeManager::GetPrimitiveTy(TypeKind::TYPE_INT64)});
        objCSelf->curFile = curFile;
        auto withObjCSuper = WithinFile(
            ctx.factory.CreateWithObjCSuperScope(std::move(objCSelf), impl, impl.GetTy(),
                [&](auto&& receiver, auto&& objCSuper) {
                    std::vector<OwnedPtr<Expr>> superInitArgs;
                    std::transform(ce->args.begin(), ce->args.end(), std::back_inserter(superInitArgs), [&](auto& arg) {
                        return ctx.factory.UnwrapEntity(
                            WithinFile(ASTCloner::Clone(arg->expr.get()), curFile));
                    });
                    auto superInit = ctx.factory.CreateMethodCallViaMsgSendSuper(
                        *targetFd, std::move(receiver), std::move(objCSuper), std::move(superInitArgs));

                    return Nodes<Node>(std::move(superInit));
                }),
            curFile);

        // We use objc_retainAutoreleasedReturnValue instead of objc_retain here, because
        // we assume that ARC applied objc_autoreleaseReturnValue on the result of the init method
        auto withObjCSuperRetained =
            ctx.factory.CreateObjCRetainAutoreleasedReturnValueCall(std::move(withObjCSuper));
        auto baseCtor = GetObjCMirrorBaseCtor(*superClass);
        auto baseCtorCall = WithinFile(CreateSuperCall(*superClass, *baseCtor, baseCtor->DataTy()), curFile);
        baseCtorCall->args.push_back(CreateFuncArg(std::move(withObjCSuperRetained)));
        ctx.factory.AppendNativeObjCIdMarkerIfNeeded(*baseCtorCall, curFile);
        baseCtorCall->sourceExpr = ce;
        ce->desugarExpr = std::move(baseCtorCall);
    }

    // set $reg field just after the super(...) expr
    auto lhs = CreateRefExpr(*GetObjCImplRegCompanionField(impl));
    auto rhs = WithinFile(
        CreateTupleAccess(std::move(regDataParamRef), 1, {ctx.typeManager.GetPrimitiveTy(TypeKind::TYPE_INT64)}),
        curFile);
    static auto unitTy = ctx.typeManager.GetPrimitiveTy(TypeKind::TYPE_UNIT);
    auto setRegCompField = CreateAssignExpr(std::move(lhs), std::move(rhs), {unitTy});
    ctorBody.insert(std::next(ctorBody.begin()), std::move(setRegCompField));
}

void DesugarThisCtorCall(ClassDecl& impl, FuncDecl& ctor)
{
    // only this(...) in reg data ctor needs to be desugared
    if (!IsObjCExportRegDataCtor(ctor)) {
        return;
    }
    auto& ctorBody = ctor.funcBody->body->body;
    if (ctorBody.empty()) {
        return;
    }
    auto& firstExpr = ctorBody[0];
    auto ce = As<ASTKind::CALL_EXPR>(firstExpr);
    if (!ce || !ce->resolvedFunction || !IsThisConstructorCall(*ce)) {
        return;
    }
    /**
     * this(...args)
     * -->
     * this($regData, ...args)
     */
    auto curFile = ce->curFile;
    auto targetFd = ce->resolvedFunction;
    CJC_ASSERT_WITH_MSG(!ctor.funcBody->paramLists.empty(), "expected at least one param list");
    auto& ctorParams = ctor.funcBody->paramLists[0]->params;
    CJC_ASSERT_WITH_MSG(!ctorParams.empty(), "expected at least one parameter");
    auto regDataParamRef = CreateRefExpr(*ctorParams[0]);

    std::vector<OwnedPtr<FuncArg>> args;
    args.push_back(CreateFuncArg(std::move(regDataParamRef)));
    args.insert(args.end(), std::make_move_iterator(ce->args.begin()), std::make_move_iterator(ce->args.end()));

    auto realTarget = GetObjCImplRegDataCtor(impl, *targetFd);
    CJC_ASSERT_WITH_MSG(realTarget, "expected a reg data ctor generated from the delegated user ctor");
    auto realTargetTy = StaticCast<FuncTy>(realTarget->DataTy());
    ce->desugarExpr = CreateThisCall(impl, *realTarget, realTargetTy, curFile, std::move(args));
}
} // namespace

void DesugarImpls::Desugar(InteropContext& ctx, ClassDecl& impl, FuncDecl& method)
{
    DesugarSuperCtorCall(ctx, impl, method);
    DesugarThisCtorCall(impl, method);
    // We are interested in:
    // 1. CallExpr to MemberAccess, as it could be `super.<member>(...)`
    // 2. MemberAccess, as it could be a prop getter call
    Walker(method.funcBody->body.get(), [this, &ctx, &impl](auto node) {
        if (node->TestAnyAttr(
                Attribute::HAS_BROKEN, Attribute::IS_BROKEN, Attribute::UNREACHABLE, Attribute::LEFT_VALUE)) {
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
    if (ce.desugarExpr || !ce.baseFunc || !ce.resolvedFunction) {
        return;
    }

    if (ce.callKind != CallKind::CALL_SUPER_FUNCTION) {
        return;
    }

    auto targetFd = ce.resolvedFunction;
    if (targetFd->propDecl && targetFd->propDecl->TestAttr(Attribute::DESUGARED_FOREIGN_FIELD)) {
        return;
    }

    if (IsObjCForeignBaseCtor(*targetFd)) {
        return;
    }

    auto targetFdTy = StaticCast<FuncTy>(targetFd->DataTy());
    auto curFile = ce.curFile;

    // method/prop branch
    if (!IsObjCForeign(*targetFd->outerDecl->GetTy())) {
        // no need to desugar expr, if the target is not in the @ObjCMirror declaration
        return;
    }

    std::vector<OwnedPtr<Expr>> msgSendSuperArgs;
    std::transform(ce.args.begin(), ce.args.end(), std::back_inserter(msgSendSuperArgs), [&](auto& arg) {
        return ctx.factory.UnwrapEntity(WithinFile(ASTCloner::Clone(arg->expr.get()), curFile));
    });

    auto nativeHandle = ctx.factory.CreateNativeHandleExpr(impl, false, ce.curFile);
    auto withObjCSuperCall = ctx.factory.CreateWithObjCSuperScope(
        std::move(nativeHandle), impl, targetFdTy->retTy.Ty(), [&](auto&& receiver, auto&& objCSuper) {
            OwnedPtr<Expr> msgSendSuperCall;
            if (targetFd->propDecl) {
                if (!msgSendSuperArgs.empty()) {
                    msgSendSuperCall = ctx.factory.CreatePropSetterCallViaMsgSendSuper(*targetFd->propDecl,
                        std::move(receiver), ASTCloner::Clone(objCSuper.get()), std::move(msgSendSuperArgs[0]));
                } else {
                    msgSendSuperCall = ctx.factory.CreatePropGetterCallViaMsgSendSuper(
                        *targetFd->propDecl, std::move(receiver), ASTCloner::Clone(objCSuper.get()));
                }
            } else {
                msgSendSuperCall = ctx.factory.CreateMethodCallViaMsgSendSuper(
                    *targetFd, std::move(receiver), ASTCloner::Clone(objCSuper.get()), std::move(msgSendSuperArgs));
            }

            if (targetFd->HasAnno(AST::AnnotationKind::OBJ_C_OPTIONAL)) {
                auto methodSelector = ctx.nameGenerator.GetObjCDeclName(*targetFd);
                auto superClass = ctx.factory.CreateGetSuperClassExpr(std::move(objCSuper), curFile);
                auto guardCall = ctx.factory.CreateOptionalMethodGuard(
                    std::move(msgSendSuperCall), std::move(superClass), methodSelector, curFile);
                guardCall->curFile = curFile;
                return Nodes<Node>(std::move(guardCall));
            }

            return Nodes<Node>(std::move(msgSendSuperCall));
        });
    withObjCSuperCall->curFile = curFile;

    // We use objc_retainAutoreleasedReturnValue instead of objc_retain here, because
    // we assume that ARC applied objc_autoreleaseReturnValue to the result of the method
    auto withObjCSuperCallWrapped = ctx.factory.WrapEntity(
        std::move(withObjCSuperCall), *targetFdTy->retTy, Retain::RETAIN_AUTORELEASED_RETURN_VALUE);
    ctx.factory.SetDesugarExpr(&ce, std::move(withObjCSuperCallWrapped));
}

void DesugarImpls::DesugarGetForPropDecl(InteropContext& ctx, ClassDecl& impl, MemberAccess& ma)
{
    if (ma.desugarExpr) {
        return;
    }

    auto target = ma.GetTarget();
    if (!target || target->astKind != ASTKind::PROP_DECL || target->TestAttr(Attribute::DESUGARED_FOREIGN_FIELD)) {
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
    if (!IsObjCForeign(*pd->outerDecl->GetTy())) {
        return;
    }
    auto nativeHandle = ctx.factory.CreateNativeHandleExpr(impl, false, ma.curFile);
    auto withObjCSuperCall = ctx.factory.CreateWithObjCSuperScope(
        std::move(nativeHandle), impl, ma.GetTy(), [&](auto&& receiver, auto&& objCSuper) {
            auto msgSendSuperCall = ctx.factory.CreatePropGetterCallViaMsgSendSuper(
                *pd, std::move(receiver), std::move(objCSuper));

            return Nodes<Node>(std::move(msgSendSuperCall));
        });
    withObjCSuperCall->curFile = ma.curFile;

    // We use objc_retainAutoreleasedReturnValue instead of objc_retain here, because
    // we assume that ARC applied objc_autoreleaseReturnValue on the result of the prop getter
    auto withObjCSuperCallWrapped = ctx.factory.WrapEntity(
        std::move(withObjCSuperCall), *ma.GetTy(), Retain::RETAIN_AUTORELEASED_RETURN_VALUE);
    ctx.factory.SetDesugarExpr(&ma, std::move(withObjCSuperCallWrapped));
}
} // namespace Cangjie::Interop::ObjC
