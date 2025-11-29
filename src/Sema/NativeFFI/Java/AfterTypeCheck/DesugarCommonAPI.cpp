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

OwnedPtr<PrimitiveType> GetOwnedPtrPrimitiveType(Ptr<Ty> actualType) {
    OwnedPtr<PrimitiveType> type = MakeOwned<PrimitiveType>();
    type->kind = actualType->kind;
    type->ty = TypeManager::GetPrimitiveTy(actualType->kind);
    return type;
}

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

// Match generic parameters in all function parameters to their corresponding Ptr<Ty>.
void JavaDesugarManager::GetArgsAndRetGenericActualTyVector(FuncDecl& ctor, const std::vector<std::pair<std::string, std::string>> instTypes,
    std::unordered_map<std::string, Ptr<Ty>> &actualTyArgMap, std::vector<Ptr<Ty>> &funcTyParams,
    std::vector<OwnedPtr<Type>> &actualPrimitiveType)
{
    Ptr<Ty> retTy = StaticCast<FuncTy*>(ctor.ty)->retTy;
    // Lambda function to handle generic type replacement.
    auto replaceGenericType = [&](const std::string& typeName) -> Ptr<Ty> {
        if (!typeName.empty()) {
            auto it = std::find_if(instTypes.begin(), instTypes.end(),
                [&typeName](const std::pair<std::string, std::string>& p) {
                    return p.first == typeName;
                });
            if (it != instTypes.end()) {
                Ptr<Ty> actualTy = GetGenericInstTy(it->second);
                actualTy->name = typeName;
                actualTyArgMap[typeName] = actualTy;
                return actualTy;
            }
        }
        return nullptr;
    };

    // Analyze the generic parameters of class/struct/enum/interface.
    if (ctor.outerDecl) {
        for (auto argTy : ctor.outerDecl->ty->typeArgs) {
            if (argTy->IsGeneric()) {
                replaceGenericType(argTy->name);
            }
        }
    }

    // Analyze generic parameters within inner functions.
    for (size_t argIdx = 0; argIdx < ctor.funcBody->paramLists[0]->params.size(); ++argIdx) {
        auto& arg = ctor.funcBody->paramLists[0]->params[argIdx];
        if (arg->ty->IsGeneric()) {
            if (auto actualTy = replaceGenericType(arg->ty->name)) {
                funcTyParams.emplace_back(actualTy);
                actualPrimitiveType.emplace_back(GetOwnedPtrPrimitiveType(actualTy));
            } else {
                funcTyParams.emplace_back(arg->ty);
            }
        } else {
            funcTyParams.emplace_back(arg->ty);
        }
    }

    // Analyze generic retType parameters within inner functions.
    if (retTy->IsGeneric()) {
        replaceGenericType(retTy->name);
    }

}

Ptr<Ty> JavaDesugarManager::GetInstantyForGenericTy(Decl& decl, const std::unordered_map<std::string, Ptr<Ty>> &actualTyArgMap) {
    std::vector<Ptr<Ty>> actualTypeArgs;
    for (const auto& typeArg : decl.ty->typeArgs) {
        std::string typeArgName = typeArg->name;

        auto it = actualTyArgMap.find(typeArgName);
        if (it != actualTyArgMap.end()) {
            actualTypeArgs.emplace_back(it->second);
        }
    }

    Ptr<Ty> instantTy;
    auto classDecl = As<ASTKind::CLASS_DECL>(&decl);
    if (classDecl) {
        instantTy = typeManager.GetClassTy(*classDecl, actualTypeArgs);
    }
    auto structDecl = As<ASTKind::STRUCT_DECL>(&decl);
    if (structDecl) {
        instantTy = typeManager.GetStructTy(*structDecl, actualTypeArgs);
    }
    auto enumDecl = As<ASTKind::ENUM_DECL>(&decl);
    if (enumDecl) {
        instantTy = typeManager.GetEnumTy(*enumDecl, actualTypeArgs);
    }
    auto interfaceDecl = As<ASTKind::INTERFACE_DECL>(&decl);
    if (interfaceDecl) {
        instantTy = typeManager.GetInterfaceTy(*interfaceDecl, actualTypeArgs);
    }
    return instantTy;
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
    std::vector<OwnedPtr<FuncArg>>& callArgs, FuncDecl& funcDecl, OwnedPtr<FuncParam>& arg, FuncParam& jniEnvPtrParam, Ptr<Ty> actualTy)
{
    CJC_NULLPTR_CHECK(funcDecl.curFile);
    Ptr<Ty> actualArgTy = arg->ty;
    actualArgTy = (actualTy && arg->ty->IsGeneric()) ? actualTy : arg->ty;
    auto jniArgTy = GetJNITy(actualArgTy);
    OwnedPtr<FuncParam> param = CreateFuncParam(arg->identifier.GetRawText(), nullptr, nullptr, jniArgTy);
    auto classLikeTy = DynamicCast<ClassLikeTy*>(actualArgTy);
    if (classLikeTy && !classLikeTy->commonDecl) {
        return false;
    }
    auto outerDecl = funcDecl.outerDecl;
    auto paramRef = WithinFile(CreateRefExpr(*param), funcDecl.curFile);
    OwnedPtr<FuncArg> methodArg;
    if (IsMirror(*actualArgTy)) {
        auto entity = lib.CreateJavaEntityJobjectCall(std::move(paramRef));
        methodArg = CreateFuncArg(CreateMirrorConstructorCall(importManager, std::move(entity), actualArgTy));
    } else if (IsImpl(*actualArgTy)) {
        auto entity = lib.CreateJavaEntityJobjectCall(std::move(paramRef));
        methodArg = CreateFuncArg(lib.UnwrapJavaEntity(std::move(entity), actualArgTy, *outerDecl));
    } else if (actualArgTy->IsCoreOptionType() && IsMirror(*actualArgTy->typeArgs[0])) {
        // funcDecl(Java_CFFI_JavaEntity(arg)) // if arg is null (as jobject == 0) -> java entity will preserve it
        auto entity = lib.CreateJavaEntityJobjectCall(std::move(paramRef));
        methodArg = CreateFuncArg(lib.UnwrapJavaEntity(std::move(entity), actualArgTy, *outerDecl));
    } else if (actualArgTy->IsCoreOptionType() && IsImpl(*actualArgTy->typeArgs[0])) {
        auto entity = lib.CreateJavaEntityJobjectCall(std::move(paramRef));
        methodArg = CreateFuncArg(lib.UnwrapJavaEntity(std::move(entity), actualArgTy, *outerDecl));
    } else if (IsCJMapping(*actualArgTy)) {
        auto entity = lib.CreateGetFromRegistryCall(
            WithinFile(CreateRefExpr(jniEnvPtrParam), funcDecl.curFile), std::move(paramRef), actualArgTy);
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

OwnedPtr<Decl> JavaDesugarManager::GenerateNativeMethod(FuncDecl& sampleMethod, Decl& decl, const GenericConfigInfo* genericConfig)
{
    auto curFile = sampleMethod.curFile;
    CJC_NULLPTR_CHECK(curFile);

    Ptr<Ty> retTy = StaticCast<FuncTy*>(sampleMethod.ty)->retTy;
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
    std::unordered_map<std::string, Ptr<Ty>> actualTyArgMap;
    std::vector<Ptr<Ty>> funcTyParams;
    std::vector<OwnedPtr<Type>> actualPrimitiveType;
    if (genericConfig && genericConfig->instTypes.size() != 0) {
        GetArgsAndRetGenericActualTyVector(sampleMethod, genericConfig->instTypes, actualTyArgMap, funcTyParams, actualPrimitiveType);
    }
    auto instantTy = GetInstantyForGenericTy(decl, actualTyArgMap);
    auto retActualTy = retTy->IsGeneric() ? actualTyArgMap[retTy->name] : retTy;
    for (auto& arg : sampleMethod.funcBody->paramLists[0]->params) {
        if (!FillMethodParamsByArg(params, methodCallArgs, sampleMethod, arg, jniEnvPtrParam, actualTyArgMap[arg->ty->name])) {
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
        OwnedPtr<CallExpr> reg;
        if (decl.ty->HasGeneric()) {
            reg = lib.CreateGetFromRegistryCall(
                WithinFile(CreateRefExpr(jniEnvPtrParam), curFile), WithinFile(CreateRefExpr(selfParam), curFile),
                instantTy);
            methodAccess = CreateMemberAccess(std::move(reg), sampleMethod);
            if (!retTy->IsGeneric()) {
                methodAccess->ty = typeManager.GetFunctionTy(funcTyParams, retTy);
            } else {
                methodAccess->ty = typeManager.GetFunctionTy(funcTyParams, retActualTy);
            }
        } else {
            reg = lib.CreateGetFromRegistryCall(
                WithinFile(CreateRefExpr(jniEnvPtrParam), curFile), WithinFile(CreateRefExpr(selfParam), curFile),
                decl.ty);
            methodAccess = CreateMemberAccess(std::move(reg), sampleMethod);
        }
    }
    methodAccess->curFile = curFile;
    auto methodCall = CreateCallExpr(std::move(methodAccess), std::move(methodCallArgs), Ptr(&sampleMethod), retActualTy,
        CallKind::CALL_DECLARED_FUNCTION);
    auto methodCallRes = CreateTmpVarDecl(nullptr, std::move(methodCall));
    methodCallRes->ty = retActualTy;
    OwnedPtr<Expr> retExpr;
    auto createCJMappingCall = [&library = this->lib, &jniEnvPtrParam, &curFile, &methodCallRes, &retExpr](
                                std::string& clazzName, bool needCtorArgs) {
        std::replace(clazzName.begin(), clazzName.end(), '.', '/');
        auto entity = library.CreateCFFINewJavaCFFINewJavaProxyObjectForCJMappingCall(
            WithinFile(CreateRefExpr(jniEnvPtrParam), curFile), WithinFile(CreateRefExpr(*methodCallRes), curFile),
            clazzName, needCtorArgs);
        retExpr = WithinFile(std::move(entity), curFile);
    };

    if (retActualTy->IsPrimitive()) {
        retExpr = WithinFile(CreateRefExpr(*methodCallRes), curFile);
    } else if (IsCJMapping(*retActualTy)) {
        if (auto retStructTy = DynamicCast<StructTy*>(retActualTy)) {
            std::string clazzName = retStructTy->decl->fullPackageName + "." + retActualTy->name;
            createCJMappingCall(clazzName, true);
        } else if (auto retEnumTy = DynamicCast<EnumTy*>(retActualTy)) {
            std::string clazzName = retEnumTy->decl->fullPackageName + "." + retActualTy->name;
            createCJMappingCall(clazzName, false);
        } else if (auto retClassTy = DynamicCast<ClassTy*>(retActualTy)) {
            std::string clazzName = retClassTy->decl->fullPackageName + "." + retActualTy->name;
            createCJMappingCall(clazzName, true);
        }
    } else {
        OwnedPtr<Expr> methodResRef = WithinFile(CreateRefExpr(*methodCallRes), curFile);
        auto entity = lib.WrapJavaEntity(std::move(methodResRef));
        retExpr = lib.UnwrapJavaEntity(std::move(entity), retActualTy, *(sampleMethod.outerDecl), true);
    }

    auto wrappedNodesLambda = WrapReturningLambdaExpr(typeManager, Nodes(std::move(methodCallRes), std::move(retExpr)));
    auto funcName = GetJniMethodName(sampleMethod);
    if (genericConfig && !genericConfig->declInstName.empty()) {
        funcName = GetJniMethodName(sampleMethod, &genericConfig->declInstName);
    }

    std::vector<OwnedPtr<FuncParamList>> paramLists;
    paramLists.push_back(CreateFuncParamList(std::move(params)));

    return GenerateNativeFuncDeclBylambda(decl, wrappedNodesLambda, paramLists, jniEnvPtrParam, retActualTy, funcName);
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
    Ptr<Ty> jniRetTy = IsCJMapping(*retTy) ? lib.GetJobjectTy() : GetJNITy(retTy);
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
OwnedPtr<Decl> JavaDesugarManager::GenerateNativeInitCjObjectFunc(FuncDecl& ctor, bool isClassLikeDecl, bool isOpenClass,
    Ptr<FuncDecl> fwdCtor,  const GenericConfigInfo* genericConfig)
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

    std::unordered_map<std::string, Ptr<Ty>> actualTyArgMap;
    std::vector<Ptr<Ty>> funcTyParams;
    std::vector<OwnedPtr<Type>> actualPrimitiveType;
    if (genericConfig && genericConfig->instTypes.size() != 0) {
        GetArgsAndRetGenericActualTyVector(ctor, genericConfig->instTypes, actualTyArgMap, funcTyParams, actualPrimitiveType);
    }
    for (size_t argIdx = 0; argIdx < ctor.funcBody->paramLists[0]->params.size(); ++argIdx) {
        auto& arg = ctor.funcBody->paramLists[0]->params[argIdx];
        if (isClassLikeDecl && argIdx == 0) {
            continue;
        }

        if (!FillMethodParamsByArg(params, ctorCallArgs, ctor, arg, jniEnvPtrParam, actualTyArgMap[arg->ty->name])) {
            return nullptr;
        }
    }

    std::vector<OwnedPtr<FuncParamList>> paramLists;
    paramLists.push_back(CreateFuncParamList(std::move(params)));

    auto jlongTy = lib.GetJlongTy();
    auto funcName = GetJniInitCjObjectFuncName(ctor, isClassLikeDecl);
    if (genericConfig && !genericConfig->declInstName.empty()) {
        funcName = GetJniInitCjObjectFuncName(ctor, isClassLikeDecl, &genericConfig->declInstName);
    }
    OwnedPtr<CallExpr> objectCtorCall;

    if (isOpenClass) {
        auto fwdCtorRef = WithinFile(CreateRefExpr(*fwdCtor), curFile);
        objectCtorCall = CreateCallExpr(std::move(fwdCtorRef), std::move(ctorCallArgs), fwdCtor,
            fwdCtor->outerDecl->ty, CallKind::CALL_OBJECT_CREATION);
    } else if (ctor.outerDecl->ty->HasGeneric()) {
        auto instantiationRefExpr = CreateRefExpr(ctor);
        auto retTy = StaticCast<FuncTy*>(ctor.ty)->retTy;
        Ptr<FuncTy> funcTy;
        auto instantTy = GetInstantyForGenericTy(*ctor.outerDecl, actualTyArgMap);
        if (retTy->HasGeneric()) {
            funcTy = typeManager.GetFunctionTy(funcTyParams, instantTy, {.isC = true});
        } else {
            funcTy = typeManager.GetFunctionTy(funcTyParams, retTy, {.isC = true});
        }
        instantiationRefExpr->typeArguments = std::move(actualPrimitiveType);
        instantiationRefExpr->ty = funcTy;
        objectCtorCall = CreateCallExpr(WithinFile(std::move(instantiationRefExpr), curFile), std::move(ctorCallArgs), Ptr(&ctor), instantTy,
            CallKind::CALL_OBJECT_CREATION);
    } else {
        auto ctorRef = WithinFile(CreateRefExpr(ctor), curFile);
        objectCtorCall = CreateCallExpr(std::move(ctorRef), std::move(ctorCallArgs), Ptr(&ctor), ctor.outerDecl->ty,
            CallKind::CALL_OBJECT_CREATION);
    }

    auto putToRegistryCall = lib.CreatePutToRegistryCall(std::move(objectCtorCall));
    auto bodyLambda = WrapReturningLambdaExpr(typeManager, Nodes(std::move(putToRegistryCall)));
    return GenerateNativeFuncDeclBylambda(ctor, bodyLambda, paramLists, jniEnvPtrParam, jlongTy, funcName);
}

} // namespace Cangjie::Interop::Java