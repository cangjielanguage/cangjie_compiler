// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "JavaDesugarManager.h"
#include "NativeFFI/Java/JavaCodeGenerator/JavaSourceCodeGenerator.h"
#include "Utils.h"

#include "cangjie/AST/Create.h"
#include "cangjie/AST/Match.h"

namespace Cangjie::Interop::Java {
using namespace Cangjie::Native::FFI;

inline void JavaDesugarManager::PushEnvParams(std::vector<OwnedPtr<FuncParam>>& params, std::string name)
{
    auto jniEnvPtrDecl = lib.GetJniEnvPtrDecl();
    CJC_NULLPTR_CHECK(jniEnvPtrDecl);
    params.push_back(CreateFuncParam(name, ASTCloner::Clone(jniEnvPtrDecl->type.get()), nullptr, lib.GetJNIEnvPtrTy()));
}

inline void JavaDesugarManager::PushObjParams(std::vector<OwnedPtr<FuncParam>>& params, std::string name)
{
    params.push_back(CreateFuncParam(name, lib.CreateJobjectType(), nullptr, lib.GetJobjectTy()));
}

inline void JavaDesugarManager::PushSelfParams(std::vector<OwnedPtr<FuncParam>>& params, std::string name)
{
    params.push_back(CreateFuncParam(name, lib.CreateJlongType(), nullptr, lib.GetJlongTy()));
}

OwnedPtr<CallExpr> JavaDesugarManager::GetFwdClassInstance(OwnedPtr<RefExpr> paramRef, Decl& fwdClassDecl)
{
        Ptr<FuncDecl> ctor(nullptr);
        for (auto& member : fwdClassDecl.GetMemberDecls()) {
            if (auto fd = As<ASTKind::FUNC_DECL>(member); fd && fd->TestAttr(Attribute::CONSTRUCTOR)) {
                ctor = fd;
                break;
            }
        }
        CJC_ASSERT(ctor);

        auto curFile = fwdClassDecl.curFile;

        auto entity = lib.CreateJavaEntityJobjectCall(std::move(paramRef));
        std::vector<OwnedPtr<FuncArg>> ctorCallArgs;
        ctorCallArgs.push_back(CreateFuncArg(std::move(entity)));

        auto fwdTy = fwdClassDecl.ty;
        auto fdRef = WithinFile(CreateRefExpr(*ctor), curFile);
        return CreateCallExpr(std::move(fdRef), std::move(ctorCallArgs), ctor, fwdTy, CallKind::CALL_OBJECT_CREATION);
}

bool JavaDesugarManager::FillMethodParamsByArg(std::vector<OwnedPtr<FuncParam>>& params,
    std::vector<OwnedPtr<FuncArg>>& callArgs, FuncDecl& funcDecl, OwnedPtr<FuncParam>& arg, FuncParam& jniEnvPtrParam)
{
    CJC_NULLPTR_CHECK(funcDecl.curFile);
    auto jniArgTy = GetJNITy(arg->ty);
    OwnedPtr<FuncParam> param = CreateFuncParam(arg->identifier.GetRawText(), nullptr, nullptr, jniArgTy);
    auto classLikeTy = DynamicCast<ClassLikeTy*>(arg->ty);
    if (classLikeTy && !classLikeTy->commonDecl) {
        return false;
    }
    auto outerDecl = funcDecl.outerDecl;
    auto paramRef = WithinFile(CreateRefExpr(*param), funcDecl.curFile);
    OwnedPtr<FuncArg> methodArg;
    if (IsMirror(*arg->ty)) {
        auto entity = lib.CreateJavaEntityJobjectCall(std::move(paramRef));
        methodArg = CreateFuncArg(CreateMirrorConstructorCall(importManager, std::move(entity), arg->ty));
    } else if (IsImpl(*arg->ty)) {
        auto entity = lib.CreateJavaEntityJobjectCall(std::move(paramRef));
        methodArg = CreateFuncArg(lib.UnwrapJavaEntity(std::move(entity), arg->ty, *outerDecl));
    } else if (arg->ty->IsCoreOptionType() && IsMirror(*arg->ty->typeArgs[0])) {
        // funcDecl(Java_CFFI_JavaEntity(arg)) // if arg is null (as jobject == 0) -> java entity will preserve it
        auto entity = lib.CreateJavaEntityJobjectCall(std::move(paramRef));
        methodArg = CreateFuncArg(lib.UnwrapJavaEntity(std::move(entity), arg->ty, *outerDecl));
    } else if (arg->ty->IsCoreOptionType() && IsImpl(*arg->ty->typeArgs[0])) {
        auto entity = lib.CreateJavaEntityJobjectCall(std::move(paramRef));
        methodArg = CreateFuncArg(lib.UnwrapJavaEntity(std::move(entity), arg->ty, *outerDecl));
    } else if (IsCJMapping(*arg->ty)) {
        auto entity = lib.CreateGetFromRegistryCall(
            WithinFile(CreateRefExpr(jniEnvPtrParam), funcDecl.curFile), std::move(paramRef), arg->ty);
        methodArg = CreateFuncArg(WithinFile(std::move(entity), funcDecl.curFile));
    } else if (IsCJMappingInterface(*arg->ty)) {
        Ptr<Ty> fwdTy = TypeManager::GetInvalidTy();
        for (auto it : classLikeTy->directSubtypes) {
            if (it->name == classLikeTy->name + JAVA_FWD_CLASS_SUFFIX) {
                fwdTy = it;
                break;
            }
        }
        CJC_ASSERT(Ty::IsTyCorrect(fwdTy));

        auto fwdClassDecl = Ty::GetDeclOfTy(fwdTy);

        auto fwdClassInstance = GetFwdClassInstance(std::move(paramRef), *fwdClassDecl);
        methodArg = CreateFuncArg(WithinFile(std::move(fwdClassInstance), funcDecl.curFile));
    } else {
        methodArg = CreateFuncArg(std::move(paramRef));
    }

    params.push_back(std::move(param));
    callArgs.push_back(std::move(methodArg));
    return true;
}

OwnedPtr<Decl> JavaDesugarManager::GenerateNativeMethod(FuncDecl& sampleMethod, Decl& decl)
{
    auto curFile = sampleMethod.curFile;
    CJC_NULLPTR_CHECK(curFile);
    auto retTy = StaticCast<FuncTy*>(sampleMethod.ty)->retTy;

    std::vector<OwnedPtr<FuncParam>> params;
    PushEnvParams(params);
    // jobject or jclass
    PushObjParams(params, "_");
    auto& jniEnvPtrParam = *params[0];
    if (!sampleMethod.TestAttr(Attribute::STATIC)) {
        if (sampleMethod.TestAttr(Attribute::CJ_MIRROR_JAVA_INTERFACE_DEFAULT)) {
            PushObjParams(params, JAVA_SELF_OBJECT);
        } else {
            PushSelfParams(params);
        }

    }
    auto& selfParam = *params.back();

    std::vector<OwnedPtr<FuncArg>> methodCallArgs;
    for (auto& arg : sampleMethod.funcBody->paramLists[0]->params) {
        if (!FillMethodParamsByArg(params, methodCallArgs, sampleMethod, arg, jniEnvPtrParam)) {
            return nullptr;
        }
    }
    OwnedPtr<MemberAccess> methodAccess;
    if (sampleMethod.TestAttr(Attribute::STATIC)) {
        methodAccess = CreateMemberAccess(WithinFile(CreateRefExpr(decl), curFile), sampleMethod);
    } else if (sampleMethod.TestAttr(Attribute::CJ_MIRROR_JAVA_INTERFACE_DEFAULT)) {
        auto& objParam = *params[2];
        auto paramRef = WithinFile(CreateRefExpr(objParam), curFile);
        auto fwdClassInstance = GetFwdClassInstance(std::move(paramRef), decl);
        methodAccess = CreateMemberAccess(std::move(fwdClassInstance), sampleMethod);
    } else {
        auto reg = lib.CreateGetFromRegistryCall(
            WithinFile(CreateRefExpr(jniEnvPtrParam), curFile), WithinFile(CreateRefExpr(selfParam), curFile), decl.ty);
        methodAccess = CreateMemberAccess(std::move(reg), sampleMethod);
    }
    methodAccess->curFile = curFile;

    auto methodCall = CreateCallExpr(std::move(methodAccess), std::move(methodCallArgs), Ptr(&sampleMethod), retTy,
        CallKind::CALL_DECLARED_FUNCTION);

    auto methodCallRes = CreateTmpVarDecl(nullptr, std::move(methodCall));
    methodCallRes->ty = retTy;
    OwnedPtr<Expr> retExpr;
    auto createCJMappingCall = [&library = this->lib, &jniEnvPtrParam, &curFile, &methodCallRes, &retExpr](
                                   std::string& clazzName, bool needCtorArgs) {
        std::replace(clazzName.begin(), clazzName.end(), '.', '/');
        auto entity = library.CreateCFFINewJavaCFFINewJavaProxyObjectForCJMappingCall(
            WithinFile(CreateRefExpr(jniEnvPtrParam), curFile), WithinFile(CreateRefExpr(*methodCallRes), curFile),
            clazzName, needCtorArgs);
        retExpr = WithinFile(std::move(entity), curFile);
    };
    if (retTy->IsPrimitive()) {
        retExpr = WithinFile(CreateRefExpr(*methodCallRes), curFile);
    } else if (IsCJMapping(*retTy)) {
        if (auto retStructTy = DynamicCast<StructTy*>(retTy)) {
            std::string clazzName = retStructTy->decl->fullPackageName + "." + retTy->name;
            createCJMappingCall(clazzName, true);
        } else if (auto retEnumTy = DynamicCast<EnumTy*>(retTy)) {
            std::string clazzName = retEnumTy->decl->fullPackageName + "." + retTy->name;
            createCJMappingCall(clazzName, false);
        } else if (auto retClassTy = DynamicCast<ClassTy*>(retTy)) {
            std::string clazzName = retClassTy->decl->fullPackageName + "." + retTy->name;
            createCJMappingCall(clazzName, true);
        }
    } else {
        OwnedPtr<Expr> methodResRef = WithinFile(CreateRefExpr(*methodCallRes), curFile);
        auto entity = lib.WrapJavaEntity(std::move(methodResRef));
        retExpr = lib.UnwrapJavaEntity(std::move(entity), retTy, *(sampleMethod.outerDecl), true);
    }

    auto wrappedNodesLambda = WrapReturningLambdaExpr(typeManager, Nodes(std::move(methodCallRes), std::move(retExpr)));
    std::string funcName = GetJniMethodName(sampleMethod);
    std::vector<OwnedPtr<FuncParamList>> paramLists;
    paramLists.push_back(CreateFuncParamList(std::move(params)));

    return GenerateNativeFuncDeclBylambda(decl, wrappedNodesLambda, paramLists, jniEnvPtrParam, retTy, funcName);
}

void JavaDesugarManager::GenerateFuncParamsForNativeDeleteCjObject(
    Decl& decl, std::vector<OwnedPtr<FuncParam>>& params, FuncParam*& jniEnv, OwnedPtr<Expr>& selfRef)
{
    PushEnvParams(params);
    PushObjParams(params);
    PushSelfParams(params);
    jniEnv = &(*params[0]);
    CJC_NULLPTR_CHECK(decl.curFile);
    constexpr int SELF_REF_INDEX = 2;
    selfRef = WithinFile(CreateRefExpr(*params[SELF_REF_INDEX]), decl.curFile);
}

OwnedPtr<Decl> JavaDesugarManager::GenerateNativeFuncDeclBylambda(Decl& decl, OwnedPtr<LambdaExpr>& wrappedNodesLambda,
    std::vector<OwnedPtr<FuncParamList>>& paramLists, FuncParam& jniEnvPtrParam, Ptr<Ty>& retTy, std::string funcName)
{
    CJC_NULLPTR_CHECK(decl.curFile);
    auto catchingCall = lib.WrapExceptionHandling(
        WithinFile(CreateRefExpr(jniEnvPtrParam), decl.curFile), std::move(wrappedNodesLambda));
    //  For ty is CJMapping:
    //  when ty is ArgsTy, we could use the Java_CFFI_getFromRegistry with [id: jlong] to get the cangjie side
    //  struct/class. when ty is RetTy, just use [jobjectTy] for we need JNI to construct the ret object.
    auto jniRetTy = IsCJMapping(*retTy) ? lib.GetJobjectTy() : GetJNITy(retTy);
    auto block = CreateBlock(Nodes(std::move(catchingCall)), jniRetTy);
    auto funcBody = CreateFuncBody(std::move(paramLists), nullptr, std::move(block), jniRetTy);
    std::vector<Ptr<Ty>> funcTyParams;
    for (auto& param : funcBody->paramLists[0]->params) {
        funcTyParams.push_back(param->ty);
    }
    auto funcTy = typeManager.GetFunctionTy(funcTyParams, jniRetTy, {.isC = true});
    auto fdecl = CreateFuncDecl(funcName, std::move(funcBody), funcTy);
    fdecl->funcBody->funcDecl = fdecl.get();
    fdecl->EnableAttr(Attribute::C);
    fdecl->EnableAttr(Attribute::GLOBAL);
    fdecl->EnableAttr(Attribute::PUBLIC);
    fdecl->EnableAttr(Attribute::NO_MANGLE);
    fdecl->EnableAttr(Attribute::UNSAFE);
    fdecl->curFile = decl.curFile;
    fdecl->moduleName = decl.moduleName;
    fdecl->fullPackageName = decl.fullPackageName;

    return std::move(fdecl);
}

/**
 * when isClassLikeDecl is true: argument ctor: generated constructor mapped with Java_ClassName_initCJObject func
 * when isClassLikeDecl is false: argument ctor: origin constructor mapped with Java_ClassName_initCJObject func
 */
OwnedPtr<Decl> JavaDesugarManager::GenerateNativeInitCjObjectFunc(FuncDecl& ctor, bool isClassLikeDecl, bool isOpenClass, Ptr<FuncDecl> fwdCtor)
{
    if (isClassLikeDecl) {
        CJC_ASSERT(!ctor.funcBody->paramLists[0]->params.empty()); // it contains obj: JavaEntity as minimum
    }

    if (isOpenClass) {
        CJC_ASSERT(fwdCtor);
    }

    // func decl arguments construction
    std::vector<OwnedPtr<FuncParam>> params;
    std::vector<OwnedPtr<FuncArg>> ctorCallArgs;
    PushEnvParams(params);
    PushObjParams(params);

    auto curFile = ctor.curFile;
    CJC_NULLPTR_CHECK(curFile);
    auto& jniEnvPtrParam = *(params[0]);
    auto objParamRef = WithinFile(CreateRefExpr(*params[1]), curFile);
    if (isClassLikeDecl || isOpenClass) {
        auto objAsEntity = lib.CreateJavaEntityJobjectCall(std::move(objParamRef));
        auto objWeakRef = lib.CreateNewGlobalRefCall(
            WithinFile(CreateRefExpr(jniEnvPtrParam), curFile), std::move(objAsEntity), true);
        ctorCallArgs.push_back(CreateFuncArg(std::move(objWeakRef)));
    }
    
    if (isOpenClass) {
        auto overrideMaskParam = CreateFuncParam(JAVA_OVERRIDE_MASK_NAME, lib.CreateJlongType(), nullptr, lib.GetJlongTy());
        auto paramRef = WithinFile(CreateRefExpr(*overrideMaskParam), curFile);
        ctorCallArgs.push_back(CreateFuncArg(std::move(paramRef)));
        params.push_back(std::move(overrideMaskParam));
    }
    
    for (size_t argIdx = 0; argIdx < ctor.funcBody->paramLists[0]->params.size(); ++argIdx) {
        auto& arg = ctor.funcBody->paramLists[0]->params[argIdx];

        if (isClassLikeDecl && argIdx == 0) {
            continue;
        }

        if (!FillMethodParamsByArg(params, ctorCallArgs, ctor, arg, jniEnvPtrParam)) {
            return nullptr;
        }
    }

    std::vector<OwnedPtr<FuncParamList>> paramLists;
    paramLists.push_back(CreateFuncParamList(std::move(params)));

    OwnedPtr<CallExpr> objectCtorCall;

    if (isOpenClass) {
        auto fwdCtorRef = WithinFile(CreateRefExpr(*fwdCtor), curFile);
        objectCtorCall = CreateCallExpr(std::move(fwdCtorRef), std::move(ctorCallArgs), fwdCtor,
            fwdCtor->outerDecl->ty, CallKind::CALL_OBJECT_CREATION);
    } else {
        auto ctorRef = WithinFile(CreateRefExpr(ctor), curFile);
        objectCtorCall = CreateCallExpr(std::move(ctorRef), std::move(ctorCallArgs), Ptr(&ctor), ctor.outerDecl->ty,
            CallKind::CALL_OBJECT_CREATION);
    }

    auto putToRegistryCall = lib.CreatePutToRegistryCall(std::move(objectCtorCall));
    auto bodyLambda = WrapReturningLambdaExpr(typeManager, Nodes(std::move(putToRegistryCall)));
    auto jlongTy = lib.GetJlongTy();
    auto funcName = GetJniInitCjObjectFuncName(ctor, isClassLikeDecl);
    return GenerateNativeFuncDeclBylambda(ctor, bodyLambda, paramLists, jniEnvPtrParam, jlongTy, funcName);
}

} // namespace Cangjie::Interop::Java