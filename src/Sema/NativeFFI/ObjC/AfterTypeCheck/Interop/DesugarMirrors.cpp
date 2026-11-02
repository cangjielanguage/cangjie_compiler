// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements desugaring of Objective-C mirror declarations.
 */

#include "Handlers.h"
#include "NativeFFI/ObjC/Utils/ASTQuery.h"
#include "NativeFFI/Utils.h"
#include "cangjie/AST/Create.h"
#include "cangjie/Utils/CheckUtils.h"
#include <iterator>

using namespace Cangjie::AST;
using namespace Cangjie::Interop::ObjC;
using namespace Cangjie::Native::FFI;

void DesugarMirrors::HandleImpl(InteropContext& ctx)
{
    auto genMirrorLikeClassBody = [this, &ctx](ClassLikeDecl& decl) {
        if (decl.TestAttr(Attribute::IS_BROKEN)) {
            return;
        }

        for (auto& memberDecl : decl.GetMemberDeclPtrs()) {
            if (memberDecl->TestAttr(Attribute::IS_BROKEN)) {
                continue;
            }

            if (IsGeneratedMember(*memberDecl)) {
                continue;
            }

            memberDecl->DisableAttr(Attribute::ABSTRACT);
            switch (memberDecl->astKind) {
                case ASTKind::FUNC_DECL: {
                    auto& fd = *StaticAs<ASTKind::FUNC_DECL>(memberDecl);
                    if (fd.TestAttr(Attribute::CONSTRUCTOR)) {
                        DesugarCtor(ctx, decl, fd);
                    } else if (fd.TestAttr(Attribute::FINALIZER)) {
                        continue;
                    } else if (IsObjCInitMethod(fd)) {
                        DesugarStaticMethodInitializer(ctx, fd);
                    } else {
                        // method branch
                        DesugarMethod(ctx, decl, fd);
                    }
                    break;
                }
                case ASTKind::PROP_DECL: {
                    auto& pd = *StaticAs<ASTKind::PROP_DECL>(memberDecl);
                    if (memberDecl->TestAttr(Attribute::DESUGARED_FOREIGN_FIELD)) {
                        DesugarField(ctx, decl, pd);
                    } else {
                        DesugarProp(ctx, decl, pd);
                    }
                    break;
                }
                case ASTKind::VAR_DECL:
                    // Unreachable, because all @ObjCMirror fields are converted to props on previous stages.
                    CJC_ABORT();
                    break;
                default:
                    break;
            }
        }
    };

    for (auto& mirror : ctx.mirrors) {
        genMirrorLikeClassBody(*mirror);
    }

    for (auto&& mirror : ctx.mirrorTopLevelFuncs) {
        if (mirror->TestAttr(Attribute::IS_BROKEN)) {
            continue;
        }
        DesugarTopLevelFunc(ctx, *mirror);
    }
}

namespace {

/**
 * Nodes instantiating an Objective-C object along with the temporary holding it, @see CreateCheckedAllocInit.
 */
struct CheckedAllocInit {
    /**
     * `let $tmp = [[T alloc] init ...]` followed by the nil check.
     */
    std::vector<OwnedPtr<Node>> nodes;
    /**
     * The `$tmp` declaration, owned by `nodes`.
     */
    Ptr<VarDecl> entity;
};

/**
 * Creates the nodes instantiating an Objective-C object and rejecting a nil result:
 * ```
 * let $tmp = objc_msgSend(objc_alloc(Cls), "init...")
 * if (getPointerAddress<Unit>($tmp) == 0) { throw ObjCInitException }
 * ```
 * The caller is expected to build the desugaring result out of the returned entity.
 *
 * @param ctor a mirror constructor or an @ObjCInit method, @see ASTFactory::CreateAllocInitCall.
 */
CheckedAllocInit CreateCheckedAllocInit(InteropContext& ctx, FuncDecl& ctor)
{
    CJC_ASSERT_WITH_MSG(ctor.outerDecl->astKind == ASTKind::CLASS_DECL,
        "Expected ASTKind::CLASS_DECL instead of " + ASTKIND_TO_STR.at(ctor.outerDecl->astKind));
    auto& mirror = *StaticAs<ASTKind::CLASS_DECL>(ctor.outerDecl);
    auto curFile = ctor.curFile;

    auto initCall = ctx.factory.CreateAllocInitCall(ctor);
    auto type = CreateType(initCall->GetTy());       // class type
    auto tempVar = CreateTmpVarDecl(type, initCall); // let tmp = objc alloc init
    tempVar->curFile = curFile;

    // (getPointerAddress<Unit>($tmp1) == 0)
    auto checkForNull = ctx.factory.CreateGetObjcEntityOrNullCall(*tempVar, curFile);
    // ObjCInitException
    auto throwExpr = ctx.factory.CreateObjCInitException(*curFile, mirror);

    auto body = CreateBlock(Nodes(std::move(throwExpr)));
    CopyBasicInfo(&mirror, body);
    body->curFile = curFile;
    body->SetTy({ctx.typeManager.GetPrimitiveTy(TypeKind::TYPE_NOTHING)});
    auto ifExpr = CreateIfExpr(std::move(checkForNull), std::move(body)); // if (tmp.isNull()) { throw ... }
    CopyBasicInfo(&mirror, ifExpr);
    ifExpr->curFile = curFile;
    ifExpr->SetTy({ctx.typeManager.GetPrimitiveTy(TypeKind::TYPE_UNIT)});

    CheckedAllocInit res;
    res.entity = tempVar;
    res.nodes.push_back(std::move(tempVar));
    res.nodes.push_back(std::move(ifExpr));
    return res;
}

} // namespace

void DesugarMirrors::DesugarCtor(InteropContext& ctx, ClassLikeDecl& mirror, FuncDecl& ctor)
{
    CJC_ASSERT(ctor.TestAttr(Attribute::CONSTRUCTOR));
    auto curFile = ctor.curFile;
    CJC_NULLPTR_CHECK(ctor.funcBody);
    CJC_ASSERT(!ctor.funcBody->paramLists.empty());
    CJC_ASSERT_WITH_MSG(mirror.astKind == ASTKind::CLASS_DECL,
        "Expected ASTKind::CLASS_DECL instead of " + ASTKIND_TO_STR.at(mirror.astKind));
    auto mirrorClass = StaticAs<ASTKind::CLASS_DECL>(&mirror);
    auto& baseCtor = *GetObjCMirrorBaseCtor(*mirrorClass);
    auto thisCall = CreateThisCall(mirror, baseCtor, baseCtor.DataTy(), curFile);

    auto allocInit = CreateCheckedAllocInit(ctx, ctor);
    allocInit.nodes.push_back(WithinFile(CreateRefExpr(*allocInit.entity), curFile));
    // this({ let tmp = objc alloc init; if (tmp.isNull()) { throw ... }; tmp }())
    auto lambda = WrapReturningLambdaCall(ctx.typeManager, std::move(allocInit.nodes));
    CopyBasicInfo(ctor.outerDecl, lambda);
    lambda->curFile = curFile;

    thisCall->args.push_back(CreateFuncArg(std::move(lambda)));
    ctx.factory.AppendNativeObjCIdMarkerIfNeeded(*thisCall, curFile);

    ctor.constructorCall = ConstructorCall::OTHER_INIT;
    ctor.funcBody->body->body.push_back(std::move(thisCall));
}

void DesugarMirrors::DesugarStaticMethodInitializer(InteropContext& ctx, FuncDecl& initializer)
{
    CJC_ASSERT(IsObjCInitMethod(initializer));
    auto curFile = initializer.curFile;
    auto retTy = StaticCast<FuncTy>(initializer.DataTy())->retTy;

    auto allocInit = CreateCheckedAllocInit(ctx, initializer);
    // return wrap(tmp)
    auto wrappedInit = ctx.factory.WrapEntity(WithinFile(CreateRefExpr(*allocInit.entity), curFile), *retTy);
    auto returnExpr = WithinFile(CreateReturnExpr(std::move(wrappedInit)), curFile);
    returnExpr->SetTy({TypeManager::GetNothingTy()});
    allocInit.nodes.push_back(std::move(returnExpr));

    initializer.funcBody->body = CreateBlock(std::move(allocInit.nodes), retTy);
}

void DesugarMirrors::DesugarMethod(InteropContext& ctx, ClassLikeDecl& mirror, FuncDecl& method)
{
    auto methodTy = StaticCast<FuncTy>(method.DataTy());
    auto curFile = method.curFile;

    auto nativeHandle =
        ctx.factory.CreateNativeHandleExpr(mirror, method.TestAttr(Attribute::STATIC), curFile);
    std::vector<OwnedPtr<Expr>> msgSendArgs;

    auto& params = method.funcBody->paramLists[0]->params;
    std::transform(params.begin(), params.end(), std::back_inserter(msgSendArgs), [&ctx, curFile](auto& param) {
        return ctx.factory.UnwrapEntity(WithinFile(CreateRefExpr(*param), curFile));
    });

    auto methodCall = ctx.factory.CreateMethodCallViaMsgSend(
        method, ASTCloner::Clone(nativeHandle.get()), std::move(msgSendArgs));
    methodCall->curFile = curFile;

    // We use objc_retainAutoreleasedReturnValue instead of objc_retain here, because
    // we assume that ARC applied objc_autoreleaseReturnValue to the result of the method
    auto wrappedMethodCall = ctx.factory.WrapEntity(
        std::move(methodCall), *methodTy->retTy, Retain::RETAIN_AUTORELEASED_RETURN_VALUE);

    method.funcBody->body = CreateBlock({}, methodTy->retTy);

    if (method.HasAnno(AST::AnnotationKind::OBJ_C_OPTIONAL)) {
        auto selectorName = ctx.nameGenerator.GetObjCDeclName(method);
        auto cls = ctx.factory.CreateObjectGetClassCall(ASTCloner::Clone(nativeHandle.get()), curFile);
        auto guardCall = ctx.factory.CreateOptionalMethodGuard(
            std::move(wrappedMethodCall), std::move(cls), selectorName, curFile);
        guardCall->curFile = curFile;
        method.funcBody->body->body.emplace_back(std::move(guardCall));
    } else {
        method.funcBody->body->body.emplace_back(std::move(wrappedMethodCall));
    }
}

void DesugarMirrors::DesugarTopLevelFunc(InteropContext& ctx, FuncDecl& func)
{
    auto methodTy = StaticCast<FuncTy>(func.DataTy());
    std::vector<Ptr<Ty>> cParamTys;
    std::transform(methodTy->paramTys.begin(), methodTy->paramTys.end(), std::back_inserter(cParamTys),
        [&ctx](auto& paramTy) { return ctx.typeMapper.Cj2CType(paramTy.Ty()); });
    std::vector<ModalTy> cParamModalTys;
    for (auto cParam : cParamTys) {
        cParamModalTys.emplace_back(cParam);
    }
    auto cFuncTy = ctx.typeManager.GetFunctionTy(
        cParamModalTys, {ctx.typeMapper.Cj2CType(methodTy->retTy.Ty())}, FuncTy::Config{.isC = true});
    auto curFile = func.curFile;
    std::vector<OwnedPtr<FuncParam>> funcParams;
    std::transform(cParamTys.begin(), cParamTys.end(), std::back_inserter(funcParams),
        [](auto& paramTy) { return CreateFuncParam("_", CreateType(paramTy), nullptr, {paramTy}); });
    auto funcParamList = CreateFuncParamList(std::move(funcParams), {});
    std::vector<OwnedPtr<FuncParamList>> funcParamLists;
    funcParamLists.push_back(std::move(funcParamList));
    auto cFuncDecl = CreateFuncDecl(func.identifier.Val(),
        CreateFuncBody(std::move(funcParamLists), CreateType(cFuncTy->retTy.Ty()), nullptr, {cFuncTy}));
    CopyBasicInfo(&func, cFuncDecl);
    cFuncDecl->EnableAttr(
        Attribute::C,
        Attribute::GLOBAL,
        Attribute::NO_MANGLE,
        Attribute::UNSAFE,
        Attribute::FOREIGN
    );
    cFuncDecl->curFile = func.curFile;
    cFuncDecl->moduleName = func.moduleName;
    cFuncDecl->fullPackageName = func.fullPackageName;

    std::vector<OwnedPtr<FuncArg>> nativeCallArgs;

    auto& params = func.funcBody->paramLists[0]->params;
    std::transform(params.begin(), params.end(), std::back_inserter(nativeCallArgs), [&ctx, curFile](auto& param) {
        return CreateFuncArg(ctx.factory.UnwrapEntity(WithinFile(CreateRefExpr(*param), curFile)));
    });

    auto funcAccess = WithinFile(CreateRefExpr(*cFuncDecl), func.curFile);

    auto call = CreateCallExpr(std::move(funcAccess), std::move(nativeCallArgs), cFuncDecl, cFuncTy->retTy,
        CallKind::CALL_DECLARED_FUNCTION);
    CopyBasicInfo(&func, call);

    // We use objc_retainAutoreleasedReturnValue instead of objc_retain here, because
    // we assume that ARC applied objc_autoreleaseReturnValue to the result of a top level function
    auto wrappedCall =
        ctx.factory.WrapEntity(std::move(call), *methodTy->retTy, Retain::RETAIN_AUTORELEASED_RETURN_VALUE);

    func.funcBody->body = CreateBlock({}, methodTy->retTy);
    func.funcBody->body->body.emplace_back(std::move(wrappedCall));
    ctx.genDecls.push_back(std::move(cFuncDecl));
}

namespace {

void DesugarGetter(InteropContext& ctx, ClassLikeDecl& mirror, PropDecl& prop)
{
    CJC_ASSERT(!prop.getters.empty());
    auto& getter = prop.getters[0];
    auto curFile = prop.curFile;

    if (mirror.astKind == ASTKind::INTERFACE_DECL && prop.TestAttr(Attribute::STATIC)) {
        // We are unable to provide a default implementation for the static property getter of an interface
        getter->funcBody->body = CreateBlock(Nodes(ctx.factory.CreateThrowUnreachableCodeExpr(*curFile)),
            {ctx.typeManager.GetPrimitiveTy(TypeKind::TYPE_NOTHING)});
        return;
    }

    auto nativeHandle = ctx.factory.CreateNativeHandleExpr(mirror, prop.TestAttr(Attribute::STATIC), curFile);
    auto propGetterCall =
        WithinFile(ctx.factory.CreatePropGetterCallViaMsgSend(prop, std::move(nativeHandle)), curFile);

    // We use objc_retainAutoreleasedReturnValue instead of objc_retain here, because
    // we assume that ARC applied objc_autoreleaseReturnValue to the result of the prop getter
    auto wrappedPropGetterCall = ctx.factory.WrapEntity(
        std::move(propGetterCall), *prop.GetTy(), Retain::RETAIN_AUTORELEASED_RETURN_VALUE);

    getter->funcBody->body = CreateBlock({}, prop.GetTy());
    getter->funcBody->body->body.emplace_back(std::move(wrappedPropGetterCall));
}

void DesugarSetter(InteropContext& ctx, ClassLikeDecl& mirror, PropDecl& prop)
{
    CJC_ASSERT(prop.TestAttr(Attribute::MUT));
    CJC_ASSERT(!prop.setters.empty());
    auto& setter = prop.setters[0];
    auto curFile = prop.curFile;
    auto unitTy = ctx.typeManager.GetPrimitiveTy(TypeKind::TYPE_UNIT);
    if (mirror.astKind == ASTKind::INTERFACE_DECL && prop.TestAttr(Attribute::STATIC)) {
        // We are unable to provide a default implementation for the static property setter of an interface
        setter->funcBody->body = CreateBlock(Nodes(ctx.factory.CreateThrowUnreachableCodeExpr(*curFile)), {unitTy});
        return;
    }
    setter->funcBody->body = CreateBlock({}, {unitTy});
    auto nativeHandle = ctx.factory.CreateNativeHandleExpr(mirror, prop.TestAttr(Attribute::STATIC), curFile);
    auto paramRef = WithinFile(CreateRefExpr(*setter->funcBody->paramLists[0]->params[0]), curFile);
    auto arg = ctx.factory.UnwrapEntity(std::move(paramRef));

    auto propSetterCall = WithinFile(
        ctx.factory.CreatePropSetterCallViaMsgSend(prop, std::move(nativeHandle), std::move(arg)), curFile);

    setter->funcBody->body->body.emplace_back(std::move(propSetterCall));
}

void DesugarFieldGetter(InteropContext& ctx, ClassLikeDecl& mirror, PropDecl& field)
{
    CJC_ASSERT(!field.getters.empty());
    auto& getter = field.getters[0];
    auto curFile = field.curFile;
    getter->funcBody->body = CreateBlock({}, field.GetTy());

    CJC_ASSERT(!field.TestAttr(Attribute::STATIC));
    auto nativeHandle = ctx.factory.CreateNativeHandleExpr(mirror, false, curFile);

    auto getInstanceVariableCall = ctx.factory.CreateGetInstanceVariableCall(field, std::move(nativeHandle));

    // We use objc_retain here, because ARC doesn't apply objc_autoreleaseReturnValue to the result of objc_getIvar
    auto wrappedGetInstanceVariableCall =
        ctx.factory.WrapEntity(std::move(getInstanceVariableCall), *field.GetTy(), Retain::RETAINED);

    getter->funcBody->body->body.push_back(std::move(wrappedGetInstanceVariableCall));
}

void DesugarFieldSetter(InteropContext& ctx, ClassLikeDecl& mirror, PropDecl& field)
{
    CJC_ASSERT(field.TestAttr(Attribute::MUT));
    CJC_ASSERT(!field.setters.empty());
    auto& setter = field.setters[0];
    auto curFile = field.curFile;
    auto unitTy = ctx.typeManager.GetPrimitiveTy(TypeKind::TYPE_UNIT);
    setter->funcBody->body = CreateBlock({}, {unitTy});

    CJC_ASSERT(!field.TestAttr(Attribute::STATIC));
    auto nativeHandle = ctx.factory.CreateNativeHandleExpr(mirror, false, curFile);
    auto paramRef = WithinFile(CreateRefExpr(*setter->funcBody->paramLists[0]->params[0]), curFile);
    auto arg = ctx.factory.UnwrapEntity(std::move(paramRef));

    auto setInstanceVariableCall =
        ctx.factory.CreateSetInstanceVariableCall(field, std::move(nativeHandle), std::move(arg));

    setter->funcBody->body->body.emplace_back(std::move(setInstanceVariableCall));
}
} // namespace

void DesugarMirrors::DesugarProp(InteropContext& ctx, ClassLikeDecl& mirror, PropDecl& prop)
{
    DesugarGetter(ctx, mirror, prop);
    if (prop.TestAttr(Attribute::MUT)) {
        DesugarSetter(ctx, mirror, prop);
    }
}

void DesugarMirrors::DesugarField(InteropContext& ctx, ClassLikeDecl& mirror, PropDecl& field)
{
    DesugarFieldGetter(ctx, mirror, field);
    if (field.TestAttr(Attribute::MUT)) {
        DesugarFieldSetter(ctx, mirror, field);
    }
}
