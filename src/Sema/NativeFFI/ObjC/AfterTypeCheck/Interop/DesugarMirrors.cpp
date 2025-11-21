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
#include "NativeFFI/Utils.h"
#include "NativeFFI/ObjC/Utils/Common.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Utils/CheckUtils.h"
#include <iterator>

using namespace Cangjie::AST;
using namespace Cangjie::Interop::ObjC;
using namespace Cangjie::Native::FFI;

void DesugarMirrors::HandleImpl(InteropContext& ctx)
{
    for (auto& mirror : ctx.mirrors) {
        if (mirror->TestAttr(Attribute::IS_BROKEN)) {
            continue;
        }

        for (auto& memberDecl : mirror->GetMemberDeclPtrs()) {
            if (memberDecl->TestAttr(Attribute::IS_BROKEN)) {
                continue;
            }

            if (ctx.factory.IsGeneratedMember(*memberDecl)) {
                continue;
            }

            memberDecl->DisableAttr(Attribute::ABSTRACT);
            switch (memberDecl->astKind) {
                case ASTKind::FUNC_DECL: {
                    auto& fd = *StaticAs<ASTKind::FUNC_DECL>(memberDecl);
                    if (fd.TestAttr(Attribute::CONSTRUCTOR)) {
                        DesugarCtor(ctx, *mirror, fd);
                    } else if (fd.TestAttr(Attribute::FINALIZER)) {
                        continue;
                    } else if (IsStaticInitMethod(fd)) {
                        DesugarStaticMethodInitializer(ctx, fd);
                    } else {
                        // method branch
                        DesugarMethod(ctx, *mirror, fd);
                    }
                    break;
                }
                case ASTKind::PROP_DECL: {
                    auto& pd = *StaticAs<ASTKind::PROP_DECL>(memberDecl);
                    if (memberDecl->TestAttr(Attribute::DESUGARED_MIRROR_FIELD)) {
                        DesugarField(ctx, *mirror, pd);
                    } else {
                        DesugarProp(ctx, *mirror, pd);
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
    }

    for (auto&& mirror : ctx.mirrorTopLevelFuncs) {
        if (mirror->TestAttr(Attribute::IS_BROKEN)) {
            continue;
        }
        DesugarTopLevelFunc(ctx, *mirror);
    }
}

void DesugarMirrors::DesugarCtor(InteropContext& ctx, ClassLikeDecl& mirror, FuncDecl& ctor)
{ 
    CJC_ASSERT(ctor.TestAttr(Attribute::CONSTRUCTOR));
    auto curFile = ctor.curFile;
    CJC_NULLPTR_CHECK(ctor.funcBody);
    CJC_ASSERT(!ctor.funcBody->paramLists.empty());

    auto& generatedCtor = *ctx.factory.GetGeneratedBaseCtor(mirror);
    auto thisCall = CreateThisCall(mirror, generatedCtor, generatedCtor.ty, curFile);

    auto initCall = ctx.factory.CreateAllocInitCall(ctor);
    thisCall->args.emplace_back(CreateFuncArg(std::move(initCall)));

    ctor.constructorCall = ConstructorCall::OTHER_INIT;
    ctor.funcBody->body->body.emplace_back(std::move(thisCall));
}

void DesugarMirrors::DesugarStaticMethodInitializer(InteropContext& ctx, FuncDecl& initializer)
{
    CJC_ASSERT(IsStaticInitMethod(initializer));
    auto curFile = initializer.curFile;
    auto retTy = StaticCast<FuncTy>(initializer.ty)->retTy;

    auto initCall = ctx.factory.CreateAllocInitCall(initializer);
    auto returnExpr = WithinFile(CreateReturnExpr(std::move(initCall)), curFile);
    returnExpr->ty = TypeManager::GetNothingTy();
    initializer.funcBody->body = CreateBlock({}, retTy);
    initializer.funcBody->body->body.emplace_back(std::move(returnExpr));
}

void DesugarMirrors::DesugarMethod(InteropContext& ctx, ClassLikeDecl& mirror, FuncDecl& method)
{
    auto methodTy = StaticCast<FuncTy>(method.ty);
    auto curFile = method.curFile;
    if (mirror.astKind == ASTKind::INTERFACE_DECL && method.TestAttr(Attribute::STATIC)) {
        // We are unable to provide a default implementation for the static method of an interface
        method.funcBody->body =
            CreateBlock(Nodes(ctx.factory.CreateThrowUnreachableCodeExpr(*curFile)), methodTy->retTy);
        return;
    }

    auto nativeHandle = ctx.factory.CreateNativeHandleExpr(mirror, method.TestAttr(Attribute::STATIC), curFile);
    std::vector<OwnedPtr<Expr>> msgSendArgs;

    auto& params = method.funcBody->paramLists[0]->params;
    std::transform(params.begin(), params.end(), std::back_inserter(msgSendArgs),
        [&ctx, curFile](auto& param) { return ctx.factory.UnwrapEntity(WithinFile(CreateRefExpr(*param), curFile)); });

    auto arpScopeCall = ctx.factory.CreateAutoreleasePoolScope(methodTy->retTy,
        Nodes(ctx.factory.CreateMethodCallViaMsgSend(method, ASTCloner::Clone(nativeHandle.get()), std::move(msgSendArgs))));
    arpScopeCall->curFile = curFile;

    method.funcBody->body = CreateBlock({}, methodTy->retTy);

    if (method.HasAnno(AST::AnnotationKind::OBJ_C_OPTIONAL)) {
        auto guardCall = ctx.factory.CreateOptionalMethodGuard(std::move(arpScopeCall), ASTCloner::Clone(nativeHandle.get()), method.identifier, curFile);
        guardCall->curFile = curFile;
        method.funcBody->body->body.emplace_back(std::move(guardCall));
    } else {
        method.funcBody->body->body.emplace_back(ctx.factory.WrapEntity(std::move(arpScopeCall), *methodTy->retTy));
    }
}

void DesugarMirrors::DesugarTopLevelFunc(InteropContext& ctx, FuncDecl& func)
{
    auto methodTy = StaticCast<FuncTy>(func.ty);
    std::vector<Ptr<Ty>> cParamTys;
    std::transform(methodTy->paramTys.begin(), methodTy->paramTys.end(), std::back_inserter(cParamTys),
        [&ctx](auto& paramTy) { return ctx.typeMapper.Cj2CType(paramTy); });
    auto cFuncTy = ctx.typeManager.GetFunctionTy(
        cParamTys,
        ctx.typeMapper.Cj2CType(methodTy->retTy),
        FuncTy::Config { .isC = true }
    );
    auto curFile = func.curFile;
    std::vector<OwnedPtr<FuncParam>> funcParams;
    std::transform(cParamTys.begin(), cParamTys.end(), std::back_inserter(funcParams),
        [](auto& paramTy) { return CreateFuncParam("_", CreateType(paramTy), nullptr, paramTy); });
    auto funcParamList = CreateFuncParamList(std::move(funcParams), nullptr);
    std::vector<OwnedPtr<FuncParamList>> funcParamLists;
    funcParamLists.push_back(std::move(funcParamList));
    auto cFuncDecl = CreateFuncDecl(
            func.identifier.Val(),
            CreateFuncBody(std::move(funcParamLists), CreateType(cFuncTy->retTy), nullptr,
                cFuncTy
            )
        );
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
    std::transform(params.begin(), params.end(), std::back_inserter(nativeCallArgs),
        [&ctx, curFile](auto& param) { return CreateFuncArg(ctx.factory.UnwrapEntity(WithinFile(CreateRefExpr(*param), curFile))); });

    auto funcAccess = WithinFile(CreateRefExpr(*cFuncDecl), func.curFile);

    auto call = CreateCallExpr(std::move(funcAccess), std::move(nativeCallArgs), cFuncDecl, cFuncTy->retTy, CallKind::CALL_DECLARED_FUNCTION);
    CopyBasicInfo(&func, call);

    auto arpScopeCall = ctx.factory.CreateAutoreleasePoolScope(methodTy->retTy, Nodes(std::move(call)));

    func.funcBody->body = CreateBlock({}, methodTy->retTy);
    func.funcBody->body->body.emplace_back(ctx.factory.WrapEntity(std::move(arpScopeCall), *methodTy->retTy));
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
            ctx.typeManager.GetPrimitiveTy(TypeKind::TYPE_NOTHING));
        return;
    }

    auto nativeHandle = ctx.factory.CreateNativeHandleExpr(mirror, prop.TestAttr(Attribute::STATIC), curFile);
    auto arpScopeCall = ctx.factory.CreateAutoreleasePoolScope(
        prop.ty, Nodes(ctx.factory.CreatePropGetterCallViaMsgSend(prop, std::move(nativeHandle))));
    getter->funcBody->body = CreateBlock({}, prop.ty);
    getter->funcBody->body->body.emplace_back(ctx.factory.WrapEntity(std::move(arpScopeCall), *prop.ty));
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
        setter->funcBody->body = CreateBlock(Nodes(ctx.factory.CreateThrowUnreachableCodeExpr(*curFile)), unitTy);
        return;
    }
    setter->funcBody->body = CreateBlock({}, unitTy);
    auto nativeHandle = ctx.factory.CreateNativeHandleExpr(mirror, prop.TestAttr(Attribute::STATIC), curFile);
    auto paramRef = WithinFile(CreateRefExpr(*setter->funcBody->paramLists[0]->params[0]), curFile);
    auto arg = ctx.factory.UnwrapEntity(std::move(paramRef));

    auto arpScopeCall = ctx.factory.CreateAutoreleasePoolScope(
        unitTy, Nodes(ctx.factory.CreatePropSetterCallViaMsgSend(prop, std::move(nativeHandle), std::move(arg))));

    setter->funcBody->body->body.emplace_back(std::move(arpScopeCall));
}

void DesugarFieldGetter(InteropContext& ctx, ClassLikeDecl& mirror, PropDecl& field)
{
    CJC_ASSERT(!field.getters.empty());
    auto& getter = field.getters[0];
    auto curFile = field.curFile;
    getter->funcBody->body = CreateBlock({}, field.ty);

    CJC_ASSERT(!field.TestAttr(Attribute::STATIC));
    auto nativeHandle = ctx.factory.CreateNativeHandleExpr(mirror, false, curFile);

    auto getInstanceVariableCall = ctx.factory.CreateGetInstanceVariableCall(field, std::move(nativeHandle));

    getter->funcBody->body->body.emplace_back(ctx.factory.WrapEntity(std::move(getInstanceVariableCall), *field.ty));
}

void DesugarFieldSetter(InteropContext& ctx, ClassLikeDecl& mirror, PropDecl& field)
{
    CJC_ASSERT(field.TestAttr(Attribute::MUT));
    CJC_ASSERT(!field.setters.empty());
    auto& setter = field.setters[0];
    auto curFile = field.curFile;
    auto unitTy = ctx.typeManager.GetPrimitiveTy(TypeKind::TYPE_UNIT);
    setter->funcBody->body = CreateBlock({}, unitTy);

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
