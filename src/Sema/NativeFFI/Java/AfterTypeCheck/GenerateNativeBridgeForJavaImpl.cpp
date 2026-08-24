// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "GenerateNativeBridgeForJavaImpl.h"
#include "NativeFFI/Java/AfterTypeCheck/JniBridge.h"
#include "NativeFFI/Utils.h"
#include "Utils.h"
#include "InteropLibBridge.h"

#include "cangjie/AST/AttributePack.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Types.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Sema/TypeManager.h"
#include "cangjie/Utils/CheckUtils.h"

namespace Cangjie::Native::FFI::Java {

OwnedPtr<FuncParam> GenerateNativeBridgeForJavaImpl::ConvertJavaCompatibleToCTypeParam(
    FuncParam& sample, File& curFile) const
{
    auto& jniTy = jni.ConvertCangjieToJniTy(*sample.GetTy());
    OwnedPtr<FuncParam> param =
        WithinFile(CreateFuncParam(sample.identifier.GetRawText(), nullptr, nullptr, {&jniTy}), &curFile);
    return param;
}

std::vector<OwnedPtr<FuncParam>> GenerateNativeBridgeForJavaImpl::ConvertJavaCompatibleToCTypeParams(
    std::vector<OwnedPtr<FuncParam>>& sample, File& curFile) const
{
    std::vector<OwnedPtr<FuncParam>> nativeParams;

    for (auto& param : sample) {
        nativeParams.push_back(ConvertJavaCompatibleToCTypeParam(*param, curFile));
    }
    return nativeParams;
}

OwnedPtr<Expr> GenerateNativeBridgeForJavaImpl::UnwrapCTypeExprAsJavaCompatible(
    OwnedPtr<Expr> cexpr, Ptr<Ty> javaCompatibleTy, Ptr<Decl> outerDecl) const
{
    // Only raw @C params are expected.
    CJC_ASSERT(!IsMirror(*cexpr->GetTy()) && !IsImpl(*cexpr->GetTy()));
    OwnedPtr<Expr> resExpr;

    if (IsMirror(*javaCompatibleTy)) {
        auto entity = ilib.CreateJavaEntityJobjectCall(std::move(cexpr));
        resExpr = CreateMirrorConstructorCall(importManager, std::move(entity), javaCompatibleTy);
    } else if (IsImpl(*javaCompatibleTy)) {
        auto entity = ilib.CreateJavaEntityJobjectCall(std::move(cexpr));
        resExpr = ilib.UnwrapJavaEntity(std::move(entity), javaCompatibleTy, *outerDecl);
    } else if (javaCompatibleTy->IsCoreOptionType() && IsMirror(*javaCompatibleTy->typeArgs[0])) {
        // funcDecl(Java_CFFI_JavaEntity(arg)) // if arg is null (as jobject == 0) -> java entity will preserve it
        auto entity = ilib.CreateJavaEntityJobjectCall(std::move(cexpr));
        resExpr = ilib.UnwrapJavaEntity(std::move(entity), javaCompatibleTy, *outerDecl);
    } else if (javaCompatibleTy->IsCoreOptionType() && IsImpl(*javaCompatibleTy->typeArgs[0])) {
        auto entity = ilib.CreateJavaEntityJobjectCall(std::move(cexpr));
        resExpr = ilib.UnwrapJavaEntity(std::move(entity), javaCompatibleTy, *outerDecl);
    } else if (javaCompatibleTy->IsString()) {
        // Convert JNI jobject (jstring) to Cangjie String:
        // jobject -> JavaEntity -> Struct-String.
        auto entity = ilib.CreateJavaEntityJobjectCall(std::move(cexpr));
        resExpr = ilib.UnwrapJavaEntity(std::move(entity), javaCompatibleTy, *outerDecl);
    } else if (javaCompatibleTy->IsCoreOptionType() && javaCompatibleTy->typeArgs[0]->IsString()) {
        auto entity = ilib.CreateJavaEntityJobjectCall(std::move(cexpr));
        resExpr = ilib.UnwrapJavaEntity(std::move(entity), javaCompatibleTy, *outerDecl);
    } else {
        // C-compatible param. Just pass-through.
        resExpr = std::move(cexpr);
    }

    return resExpr;
}


OwnedPtr<FuncDecl> GenerateNativeBridgeForJavaImpl::CreateConstructorBridge(
    FuncDecl& refWrapperUserCtor, FuncDecl& refWrapperGeneratedCtor,
    FuncDecl& registryCompanionCtor, ClassDecl& companion) const
{
    File& curFile = *companion.curFile;
    auto& userParams = refWrapperUserCtor.funcBody->paramLists[0]->params;
    auto nativeFuncName = jni.GetJniInitCjObjectFuncName(refWrapperUserCtor, false);

    auto func = jni.CreateNativeJavaABIFunc(nativeFuncName,
        ConvertJavaCompatibleToCTypeParams(userParams, curFile),
        typeManager.GetPrimitiveTy(AST::TypeKind::TYPE_UNIT),
        curFile, companion.moduleName, companion.fullPackageName,
        [&](FuncDecl& f, FuncParam& jniEnv, FuncParam& jobject, std::vector<Ptr<FuncParam>> cTypeUserParams) {
            std::vector<OwnedPtr<FuncArg>> refWrapperCtorArgs;

            // Construct reference wrapper object.
            // argument 1: $obj -> Java_CFFI_JavaEntity($obj)
            refWrapperCtorArgs.emplace_back(CreateFuncArg(
                ilib.CreateJavaEntityJobjectCall(WithinFile(CreateRefExpr(jobject), &curFile))));

            // argument 2: $reg -> JavaImpl$reg(Java_CFFI_JavaEntity($obj))
            refWrapperCtorArgs.emplace_back(CreateFuncArg(WithinFile(
                CreateCallExpr(
                    WithinFile(CreateRefExpr(registryCompanionCtor), &curFile),
                    Nodes<FuncArg>(CreateFuncArg(
                        ilib.CreateJavaEntityJobjectCall(WithinFile(CreateRefExpr(jobject), &curFile)))),
                    &registryCompanionCtor, companion.GetTy(), CallKind::CALL_OBJECT_CREATION),
                &curFile)));

            CJC_ASSERT(cTypeUserParams.size() == userParams.size());

            // Proxy user parameters as CTypes converted to java-compatible types
            // into reference wrapper constructor parameters.
            for (auto [userParam, ctypeParam] = std::tuple{userParams.begin(), cTypeUserParams.begin()};
                 userParam != userParams.end();
                 userParam++, ctypeParam++) {
                refWrapperCtorArgs.push_back(CreateFuncArg(UnwrapCTypeExprAsJavaCompatible(
                    WithinFile(CreateRefExpr(*ctypeParam->get()), &curFile), userParam->get()->DataTy(), &companion)));
            }

            auto& refWrapper = *refWrapperUserCtor.outerDecl;
            // `JavaImpl(Java_CFFI_JavaEntity($obj), JavaImpl$reg(Java_CFFI_JavaEntity($obj)), <user arguments>)`
            auto refWrapperCtorCall = CreateCallExpr(
                WithinFile(CreateRefExpr(refWrapperGeneratedCtor), &curFile),
                std::move(refWrapperCtorArgs),
                &refWrapperGeneratedCtor, refWrapper.GetTy(), CallKind::CALL_OBJECT_CREATION);

            f.funcBody->body->body.push_back(
                ilib.WrapExceptionHandling(WithinFile(CreateRefExpr(jniEnv), &curFile),
                WrapUnitLambdaExpr(typeManager, Nodes(std::move(refWrapperCtorCall))))
            );
    });

    return func;
}

OwnedPtr<FuncDecl> GenerateNativeBridgeForJavaImpl::CreateMethodBridge(AfterTypeCheckContext& ctx,
    AST::FuncDecl& refWrapperFunc, AST::ClassDecl& companion) const
{
    return refWrapperFunc.TestAttr(Attribute::STATIC)
        ? CreateStaticMethodBridge(refWrapperFunc, companion)
        : CreateInstanceMethodBridge(ctx, refWrapperFunc, companion);
}

OwnedPtr<FuncDecl> GenerateNativeBridgeForJavaImpl::CreateInstanceMethodBridge(AfterTypeCheckContext& ctx,
    FuncDecl& refWrapperFunc, ClassDecl& companion) const
{
    File& curFile = *companion.curFile;
    ModalTy retTy = StaticCast<FuncTy*>(refWrapperFunc.DataTy())->retTy;
    auto& userParams = refWrapperFunc.funcBody->paramLists[0]->params;

    auto ctypeParams = ConvertJavaCompatibleToCTypeParams(userParams, curFile);
    ctypeParams.insert(ctypeParams.begin(), jni.CreateRegistryIdParam());
    auto nativeFuncName = jni.GetJniMethodName(refWrapperFunc);

    auto bridgeFunc = jni.CreateNativeJavaABIFunc(
        nativeFuncName,
        std::move(ctypeParams),
        &jni.ConvertCangjieToJniTy(*retTy), curFile,
        refWrapperFunc.moduleName, refWrapperFunc.fullPackageName,
        [&](FuncDecl& f, FuncParam& jniEnv, [[maybe_unused]] FuncParam& obj,
            std::vector<Ptr<FuncParam>> ctypeUserParams) {
                auto& registryIdParam = *ctypeUserParams[0];

                CJC_ASSERT(refWrapperFunc.outerDecl && refWrapperFunc.outerDecl->astKind == ASTKind::CLASS_DECL);
                auto& refWrapper = *StaticAs<ASTKind::CLASS_DECL>(refWrapperFunc.outerDecl);

                std::vector<OwnedPtr<FuncArg>> methodCallArgs;
                // ctype params are registryId param + user params
                CJC_ASSERT(ctypeUserParams.size() == userParams.size() + 1);

                // Proxy user parameters as CTypes converted to java-compatible types
                // into reference wrapper constructor parameters.
                for (auto [userParam, ctypeParam] = std::tuple{userParams.begin(), ctypeUserParams.begin() + 1};
                    userParam != userParams.end();
                    userParam++, ctypeParam++) {
                    methodCallArgs.push_back(CreateFuncArg(UnwrapCTypeExprAsJavaCompatible(
                        WithinFile(CreateRefExpr(*ctypeParam->get()), &curFile), (*userParam)->DataTy(), &companion)));
                }

                auto& ctor = ctx.GetJavaImplWrappingConstructor(refWrapper);

                std::vector<OwnedPtr<FuncArg>> refWrapperCtorArgs;
                refWrapperCtorArgs.push_back(CreateFuncArg(ilib.CreateJavaEntityJobjectCall(
                    WithinFile(CreateRefExpr(obj), &curFile))));
                refWrapperCtorArgs.push_back(CreateFuncArg(
                    WithinFile(CreateRefExpr(registryIdParam), &curFile)));

                auto refWrapperCtorCall = CreateCallExpr(
                    WithinFile(CreateRefExpr(ctor), &curFile),
                    std::move(refWrapperCtorArgs),
                    &ctor, refWrapper.GetTy(), CallKind::CALL_OBJECT_CREATION);

                auto methodAccess = CreateMemberAccess(std::move(refWrapperCtorCall), refWrapperFunc);
                methodAccess->curFile = &curFile;
                auto methodCall = CreateCallExpr(std::move(methodAccess), std::move(methodCallArgs), &refWrapperFunc,
                    retTy, CallKind::CALL_DECLARED_FUNCTION);
                auto methodCallRes = CreateTmpVarDecl(nullptr, std::move(methodCall));
                methodCallRes->GetTy() = retTy;
                OwnedPtr<Expr> retExpr = ilib.UnwrapJavaEntity(
                    ilib.WrapJavaEntity(WithinFile(CreateRefExpr(*methodCallRes), &curFile)),
                    &jni.ConvertCangjieToJniTy(*retTy), *(refWrapperFunc.outerDecl), true);

                f.funcBody->body->body.push_back(
                    ilib.WrapExceptionHandling(WithinFile(CreateRefExpr(jniEnv), &curFile),
                    WrapReturningLambdaExpr(typeManager, Nodes(std::move(methodCallRes), std::move(retExpr))))
                );
        });

        return bridgeFunc;
}

OwnedPtr<FuncDecl> GenerateNativeBridgeForJavaImpl::CreateStaticMethodBridge(
    FuncDecl& refWrapperFunc, ClassDecl& companion) const
{
    File& curFile = *companion.curFile;
    ModalTy retTy = StaticCast<FuncTy*>(refWrapperFunc.DataTy())->retTy;
    auto& userParams = refWrapperFunc.funcBody->paramLists[0]->params;
    auto nativeFuncName = jni.GetJniMethodName(refWrapperFunc);

    auto bridgeFunc = jni.CreateNativeJavaABIFunc(
        nativeFuncName,
        ConvertJavaCompatibleToCTypeParams(userParams, curFile),
        &jni.ConvertCangjieToJniTy(*retTy), curFile,
        refWrapperFunc.moduleName, refWrapperFunc.fullPackageName,
        [&](FuncDecl& f, FuncParam& jniEnv, [[maybe_unused]] FuncParam& jclass,
            std::vector<Ptr<FuncParam>> ctypeUserParams) {
                jclass.identifier = "_"; // Unused in static function calls
                CJC_ASSERT(refWrapperFunc.outerDecl);
                auto& refWrapper = *refWrapperFunc.outerDecl;

                std::vector<OwnedPtr<FuncArg>> methodCallArgs;
                CJC_ASSERT(ctypeUserParams.size() == userParams.size());

                // Proxy user parameters as CTypes converted to java-compatible types
                // into reference wrapper constructor parameters.
                for (auto [userParam, ctypeParam] = std::tuple{userParams.begin(), ctypeUserParams.begin()};
                    userParam != userParams.end();
                    userParam++, ctypeParam++) {
                    methodCallArgs.push_back(CreateFuncArg(UnwrapCTypeExprAsJavaCompatible(
                        WithinFile(CreateRefExpr(*ctypeParam->get()), &curFile), (*userParam)->DataTy(), &companion)));
                }

                auto methodAccess = CreateMemberAccess(WithinFile(CreateRefExpr(refWrapper), &curFile), refWrapperFunc);
                methodAccess->curFile = &curFile;
                auto methodCall = CreateCallExpr(std::move(methodAccess), std::move(methodCallArgs), &refWrapperFunc,
                    retTy, CallKind::CALL_DECLARED_FUNCTION);
                auto methodCallRes = CreateTmpVarDecl(nullptr, std::move(methodCall));
                methodCallRes->GetTy() = retTy;
                OwnedPtr<Expr> retExpr = ilib.UnwrapJavaEntity(
                    ilib.WrapJavaEntity(WithinFile(CreateRefExpr(*methodCallRes), &curFile)),
                    &jni.ConvertCangjieToJniTy(*retTy), *(refWrapperFunc.outerDecl), true);

                f.funcBody->body->body.push_back(
                    ilib.WrapExceptionHandling(WithinFile(CreateRefExpr(jniEnv), &curFile),
                    WrapReturningLambdaExpr(typeManager, Nodes(std::move(methodCallRes), std::move(retExpr)))));
        });

        return bridgeFunc;
}

OwnedPtr<Decl> GenerateNativeBridgeForJavaImpl::CreateFinalizationBridge(ClassDecl& refWrapper) const
{
    static Ptr<Ty> unitTy = typeManager.GetPrimitiveTy(TypeKind::TYPE_UNIT);
    std::vector<OwnedPtr<FuncParam>> ctypeParams;
    auto& registryIdParam = *ctypeParams.emplace_back(jni.CreateRegistryIdParam());

    return jni.CreateNativeJavaABIFunc(
        jni.GetJniDeleteCjObjectFuncName(refWrapper),
        std::move(ctypeParams),
        unitTy,
        *refWrapper.curFile, refWrapper.moduleName, refWrapper.fullPackageName,
        [&](FuncDecl& fn, [[maybe_unused]] FuncParam& jniEnvParam, [[maybe_unused]] FuncParam& jobjParam,
            [[maybe_unused]] std::vector<Ptr<FuncParam>> cTypeUserParams) {
                auto removeFromRegistryCall = ilib.CreateRemoveFromRegistryCall(
                    WithinFile(CreateRefExpr(registryIdParam), refWrapper.curFile));

                auto& body = fn.funcBody->body->body;
                body.push_back(ilib.WrapExceptionHandling(
                    WithinFile(CreateRefExpr(jniEnvParam), refWrapper.curFile),
                    WrapReturningLambdaExpr(typeManager, Nodes(std::move(removeFromRegistryCall)))));
    });
}

void GenerateNativeBridgeForJavaImpl::Process(ClassDecl& companion, ClassDecl& refWrapper,
    AfterTypeCheckContext& ctx) const
{
    CJC_ASSERT(IsImplRegistryCompanion(companion));
    CJC_ASSERT(IsImplReferenceWrapper(refWrapper));

    auto& companionCtor = *GetJavaImplRegistryCompanionConstructor(companion);

    for (auto userCtor : ctx.GetJavaImplUserDefinedConstructors(refWrapper)) {
        if (userCtor->TestAttr(Attribute::IS_BROKEN)) {
            continue;
        }
        auto& generatedCtor = ctx.GetJavaImplReferenceWrapperGeneratedConstructor(*userCtor);
        ctx.AddGeneratedDecl(CreateConstructorBridge(*userCtor, generatedCtor, companionCtor, companion));
    }

    for (auto member : refWrapper.GetMemberDeclPtrs()) {
        if (member->TestAttr(Attribute::IS_BROKEN)) {
            continue;
        }
        if (!member->TestAnyAttr(Attribute::PUBLIC, Attribute::PROTECTED)) {
            continue;
        }
        auto method = As<ASTKind::FUNC_DECL>(member);
        if (!method || member->TestAttr(Attribute::CONSTRUCTOR)) {
            continue;
        }

        ctx.AddGeneratedDecl(CreateMethodBridge(ctx, *method, companion));
    }

    ctx.AddGeneratedDecl(CreateFinalizationBridge(refWrapper));
}

GenerateNativeBridgeForJavaImpl::GenerateNativeBridgeForJavaImpl(
    TypeManager& typeManager,
    const ImportManager& importManager,
    InteropLibBridge& ilib,
    JniBridge& jni)
    : typeManager(typeManager), importManager(importManager), ilib(ilib), jni(jni)
{}

void GenerateNativeBridgeForJavaImpl::Process(AfterTypeCheckContext& ctx)
{
    for (auto refWrapper : ctx.GetJavaImplReferenceWrappers()) {
        if (refWrapper->TestAttr(Attribute::IS_BROKEN)) {
            continue;
        }
        auto& companion = ctx.GetJavaImplRegistryCompanion(*refWrapper);

        Process(companion, *refWrapper, ctx);
    }
}

} // namespace Cangjie::Native::FFI::Java
