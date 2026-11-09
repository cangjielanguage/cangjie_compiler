// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "JniBridge.h"
#include "Utils.h"
#include "NativeFFI/Utils.h"
#include "NativeFFI/Java/Utils.h"
#include "cangjie/AST/Clone.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Types.h"
#include "cangjie/Utils/CheckUtils.h"
#include <string>

namespace Cangjie::Native::FFI::Java {

using namespace Cangjie::AST;
using namespace Interop::Java;

namespace {
Ty& GetUnderlyingTy(Ty& ty)
{
    if (Is<TypeAliasTy>(ty)) {
        return GetUnderlyingTy(*StaticCast<TypeAliasTy&>(ty).declPtr->type->GetTy());
    }
    return ty;
}

/**
 * Java Native Method Names Resolution scheme:
 * 1. `_` -> `_1`
 * 2. `.` -> '_' | (in fully-quialified names)
 */
std::string ApplyJavaNativeNameEscaping(const std::string& name)
{
    constexpr std::string_view underscopeSequence1 = "_1";

    std::string escapedName(name);
    size_t startPos = 0;
    // "_" in name should be replaced with `_1` since `_` is reserved symbol in JNI signatures
    while ((startPos = escapedName.find("_", startPos)) != std::string::npos) {
        escapedName.replace(startPos, 1, underscopeSequence1);
        startPos += underscopeSequence1.size();
        // Continue after inserted "_1" substring (2 characters).
    }

    std::replace(escapedName.begin(), escapedName.end(), '.', '_');
    return escapedName;
}

}

JniBridge::JniBridge(
    TypeManager& typeManager,
    const ImportManager& importManager,
    const BaseMangler& mangler,
    Interop::Java::Utils& utils,
    Decl& jniEnvPtrDecl,
    Decl& jniJobjectDecl,
    StructDecl& jniInterfaceDecl) : interface(typeManager, importManager, jniInterfaceDecl),
    typeManager(typeManager), mangler(mangler), utils(utils),
    jniEnvPtrDecl(jniEnvPtrDecl), jniJobjectDecl(jniJobjectDecl)
{}

OwnedPtr<FuncParam> JniBridge::CreateJniEnvParam(const std::string& name) const
{
    return CreateFuncParam(name, CreateType(ModalTy{Ptr<Ty>(&GetJniEnvPtrTy())}), nullptr, {&GetJniEnvPtrTy()});
}

OwnedPtr<FuncParam> JniBridge::CreateJniJobjectOrJclassParam(const std::string& name) const
{
    return CreateFuncParam(
        name, CreateType(ModalTy{Ptr<Ty>(&GetJniJobjectDeclTy())}), nullptr, {&GetJniJobjectDeclTy()});
}

OwnedPtr<FuncParam> JniBridge::CreateRegistryIdParam(const std::string& name) const
{
    return CreateFuncParam(
        name, CreateType(ModalTy{Ptr<Ty>(&GetRegistryIdJavaTy())}), nullptr, {&GetRegistryIdJavaTy()});
}

std::string JniBridge::GetJniMethodName(const FuncDecl& method) const
{
    auto sampleJavaName = GetJavaMemberName(method);
    std::string fqname = GetJavaFQName(*method.outerDecl);
    CJC_ASSERT_WITH_MSG(!method.funcBody->paramLists.empty(), "paramLists cannot be empty");
    auto mangledFuncName = GetMangledMethodName(mangler, method.funcBody->paramLists[0]->params, sampleJavaName);

    return GetJavaNativeFunctionName(fqname, mangledFuncName);
}

std::string JniBridge::GetJniMethodNameForProp(
    const PropDecl& propDecl,
    bool isSet) const
{
    std::string varDecl = GetJavaMemberName(propDecl);
    std::string varDeclSuffix = varDecl;
    CJC_ASSERT_WITH_MSG(!varDeclSuffix.empty(), "identifier cannot be an empty string");
    varDeclSuffix[0] = static_cast<char>(toupper(varDeclSuffix[0]));
    std::string fqname = Native::FFI::Java::GetJavaFQName(*(propDecl.outerDecl));

    return GetJavaNativeFunctionName(fqname, (isSet ? "set" : "get") + varDeclSuffix + "Impl");
}

std::string JniBridge::GetJniInitCjObjectFuncName(
    const FuncDecl& ctor,
    bool isGeneratedCtor) const
{
    std::string fqname = GetJavaFQName(*(ctor.outerDecl));
    auto mangledFuncName = GetMangledJniInitCjObjectFuncName(mangler,
        ctor.funcBody->paramLists[0]->params, isGeneratedCtor);

    if (Is<EnumDecl>(ctor.outerDecl)) {
        mangledFuncName = ctor.identifier + mangledFuncName;
    }

    return GetJavaNativeFunctionName(fqname, mangledFuncName);
}

std::string JniBridge::GetJniInitCjObjectFuncNameForVarDecl(const VarDecl& ctor) const
{
    std::string fqname = GetJavaFQName(*(ctor.outerDecl));
    auto mangledFuncName = ctor.identifier.Val();
    return GetJavaNativeFunctionName(fqname, mangledFuncName + "initCJObject");
}

std::string JniBridge::GetJniDeleteCjObjectFuncName(const Decl& decl) const
{
    std::string fqname = GetJavaFQName(decl);
    return GetJavaNativeFunctionName(fqname, "deleteCJObject");
}

OwnedPtr<FuncDecl> JniBridge::CreateNativeJavaABIFunc(
    const std::string& name,
    std::vector<OwnedPtr<FuncParam>> userParams, Ptr<Ty> retTy,
    File& curFile, std::string& moduleName, std::string& fullPackageName,
    std::function<void(
        FuncDecl& f, FuncParam& jniEnv, FuncParam& objOrClass, std::vector<Ptr<FuncParam>> userParams)> builder) const
{
    std::vector<Ptr<FuncParam>> userParamsView;
    for (auto& userParam : userParams) {
        userParamsView.push_back(userParam.get());
    }

    auto params = std::move(userParams);
    auto& jniEnvParam = **params.insert(params.begin(), CreateJniEnvParam());
    auto& objOrClassParam = **params.insert(params.begin() + 1, CreateJniJobjectOrJclassParam());

    auto func = utils.CreateNativeFunc(name, std::move(params), retTy, {}, curFile, moduleName,
        fullPackageName);

    builder(*func, jniEnvParam, objOrClassParam, userParamsView);

    return func;
}

Ty& JniBridge::GetJniEnvPtrTy() const
{
    static auto& ty = GetUnderlyingTy(*jniEnvPtrDecl.GetTy());
    return ty;
}

Ty& JniBridge::GetJniJobjectDeclTy() const
{
    static auto& ty = GetUnderlyingTy(*jniJobjectDecl.GetTy());
    return ty;
}

Ty& JniBridge::GetRegistryIdJavaTy() const
{
    static auto& ty = *typeManager.GetPrimitiveTy(TypeKind::TYPE_INT64);
    return ty;
}

std::string JniBridge::GetJavaNativeFunctionName(const std::string& fqTypeName, const std::string& memberName) const
{
    return "Java_" + ApplyJavaNativeNameEscaping(fqTypeName) + "_" + ApplyJavaNativeNameEscaping(memberName);
}

/**
 * Map Cangjie type to corresponding JNI-level type used in generated native method.
 */
Ty& JniBridge::ConvertCangjieToJniTy(Ty& javaCompatibleTy) const
{
    if (javaCompatibleTy.IsString()) {
        // String is passed as jobject.
        return GetJniJobjectDeclTy();
    }
    if (javaCompatibleTy.IsCoreOptionType()) {
        CJC_ASSERT(!javaCompatibleTy.typeArgs.empty());
        auto argTy = javaCompatibleTy.typeArgs[0];
        CJC_ASSERT(IsMirror(*argTy) || IsImpl(*argTy) || argTy->IsString());
        return GetJniJobjectDeclTy();
    }
    if (IsMirror(javaCompatibleTy) || IsImpl(javaCompatibleTy)) {
        return GetJniJobjectDeclTy();
    }

    if (javaCompatibleTy.IsBuiltin()) {
        return javaCompatibleTy;
    }

    CJC_ABORT();
    return javaCompatibleTy; // never succeeding fallback
}

namespace {

using JniNonVirtualCallTy = OwnedPtr<Expr> (JniNativeInterfaceBridge::*)(
    Ptr<Expr>, OwnedPtr<Expr>&&, OwnedPtr<Expr>&&, OwnedPtr<Expr>&&) const;
using JniNonVirtualCallATy = OwnedPtr<Expr> (JniNativeInterfaceBridge::*)(
    Ptr<Expr>, OwnedPtr<Expr>&&, OwnedPtr<Expr>&&, OwnedPtr<Expr>&&, OwnedPtr<FuncArg>&&) const;

using JniVirtualInstanceCallTy = OwnedPtr<Expr> (JniNativeInterfaceBridge::*)(
    Ptr<Expr>, OwnedPtr<Expr>&&, OwnedPtr<Expr>&&) const;
using JniVirtualInstanceCallATy = OwnedPtr<Expr> (JniNativeInterfaceBridge::*)(
    Ptr<Expr>, OwnedPtr<Expr>&&, OwnedPtr<Expr>&&, OwnedPtr<FuncArg>&&) const;

using JniStaticCallTy = OwnedPtr<Expr> (JniNativeInterfaceBridge::*)(
    Ptr<Expr>, OwnedPtr<Expr>&&, OwnedPtr<Expr>&&) const;
using JniStaticCallATy = OwnedPtr<Expr> (JniNativeInterfaceBridge::*)(
    Ptr<Expr>, OwnedPtr<Expr>&&, OwnedPtr<Expr>&&, OwnedPtr<FuncArg>&&) const;

using JniStaticFieldGetATy = OwnedPtr<Expr> (JniNativeInterfaceBridge::*)(
    Ptr<Expr>, OwnedPtr<Expr>&&, OwnedPtr<Expr>&&) const;
using JniInstanceFieldGetATy = OwnedPtr<Expr> (JniNativeInterfaceBridge::*)(
    Ptr<Expr>, OwnedPtr<Expr>&&, OwnedPtr<Expr>&&) const;

using JniStaticFieldSetATy = OwnedPtr<Expr> (JniNativeInterfaceBridge::*)(
    Ptr<Expr>, OwnedPtr<Expr>&&, OwnedPtr<Expr>&&, OwnedPtr<Expr>&&) const;
using JniInstanceFieldSetATy = OwnedPtr<Expr> (JniNativeInterfaceBridge::*)(
    Ptr<Expr>, OwnedPtr<Expr>&&, OwnedPtr<Expr>&&, OwnedPtr<Expr>&&) const;


using Interface = JniNativeInterfaceBridge;

JniNonVirtualCallTy MatchNonVirtualCallMethod(const Ty& retTy)
{
    switch (retTy.kind) {
        case TypeKind::TYPE_UNIT: return &Interface::CreateCallNonvirtualVoidMethodCall;
        case TypeKind::TYPE_BOOLEAN: return &Interface::CreateCallNonvirtualBooleanMethodCall;
        case TypeKind::TYPE_INT8: return &Interface::CreateCallNonvirtualByteMethodCall;
        case TypeKind::TYPE_UINT16: return &Interface::CreateCallNonvirtualCharMethodCall;
        case TypeKind::TYPE_INT16: return &Interface::CreateCallNonvirtualShortMethodCall;
        case TypeKind::TYPE_INT32: return &Interface::CreateCallNonvirtualIntMethodCall;
        case TypeKind::TYPE_INT64: return &Interface::CreateCallNonvirtualLongMethodCall;
        case TypeKind::TYPE_FLOAT32: return &Interface::CreateCallNonvirtualFloatMethodCall;
        case TypeKind::TYPE_FLOAT64: return &Interface::CreateCallNonvirtualDoubleMethodCall;
        default: return &Interface::CreateCallNonvirtualObjectMethodCall;
    }
}

JniNonVirtualCallATy MatchNonVirtualCallMethodA(const Ty& retTy)
{
    switch (retTy.kind) {
        case TypeKind::TYPE_UNIT: return &Interface::CreateCallNonvirtualVoidMethodACall;
        case TypeKind::TYPE_BOOLEAN: return &Interface::CreateCallNonvirtualBooleanMethodACall;
        case TypeKind::TYPE_INT8: return &Interface::CreateCallNonvirtualByteMethodACall;
        case TypeKind::TYPE_UINT16: return &Interface::CreateCallNonvirtualCharMethodACall;
        case TypeKind::TYPE_INT16: return &Interface::CreateCallNonvirtualShortMethodACall;
        case TypeKind::TYPE_INT32: return &Interface::CreateCallNonvirtualIntMethodACall;
        case TypeKind::TYPE_INT64: return &Interface::CreateCallNonvirtualLongMethodACall;
        case TypeKind::TYPE_FLOAT32: return &Interface::CreateCallNonvirtualFloatMethodACall;
        case TypeKind::TYPE_FLOAT64: return &Interface::CreateCallNonvirtualDoubleMethodACall;
        default: return &Interface::CreateCallNonvirtualObjectMethodACall;
    }
}

JniVirtualInstanceCallTy MatchVirtualInstanceCallMethod(const Ty& retTy)
{
    switch (retTy.kind) {
        case TypeKind::TYPE_UNIT: return &Interface::CreateCallVoidMethodCall;
        case TypeKind::TYPE_BOOLEAN: return &Interface::CreateCallBooleanMethodCall;
        case TypeKind::TYPE_INT8: return &Interface::CreateCallByteMethodCall;
        case TypeKind::TYPE_UINT16: return &Interface::CreateCallCharMethodCall;
        case TypeKind::TYPE_INT16: return &Interface::CreateCallShortMethodCall;
        case TypeKind::TYPE_INT32: return &Interface::CreateCallIntMethodCall;
        case TypeKind::TYPE_INT64: return &Interface::CreateCallLongMethodCall;
        case TypeKind::TYPE_FLOAT32: return &Interface::CreateCallFloatMethodCall;
        case TypeKind::TYPE_FLOAT64: return &Interface::CreateCallDoubleMethodCall;
        default: return &Interface::CreateCallObjectMethodCall;
    }
}

JniVirtualInstanceCallATy MatchVirtualInstanceCallAMethod(const Ty& retTy)
{
    switch (retTy.kind) {
        case TypeKind::TYPE_UNIT: return &Interface::CreateCallVoidMethodACall;
        case TypeKind::TYPE_BOOLEAN: return &Interface::CreateCallBooleanMethodACall;
        case TypeKind::TYPE_INT8: return &Interface::CreateCallByteMethodACall;
        case TypeKind::TYPE_UINT16: return &Interface::CreateCallCharMethodACall;
        case TypeKind::TYPE_INT16: return &Interface::CreateCallShortMethodACall;
        case TypeKind::TYPE_INT32: return &Interface::CreateCallIntMethodACall;
        case TypeKind::TYPE_INT64: return &Interface::CreateCallLongMethodACall;
        case TypeKind::TYPE_FLOAT32: return &Interface::CreateCallFloatMethodACall;
        case TypeKind::TYPE_FLOAT64: return &Interface::CreateCallDoubleMethodACall;
        default: return &Interface::CreateCallObjectMethodACall;
    }
}

JniStaticCallTy MatchStaticCallMethod(const Ty& retTy)
{
    switch (retTy.kind) {
        case TypeKind::TYPE_UNIT: return &Interface::CreateCallStaticVoidMethodCall;
        case TypeKind::TYPE_BOOLEAN: return &Interface::CreateCallStaticBooleanMethodCall;
        case TypeKind::TYPE_INT8: return &Interface::CreateCallStaticByteMethodCall;
        case TypeKind::TYPE_UINT16: return &Interface::CreateCallStaticCharMethodCall;
        case TypeKind::TYPE_INT16: return &Interface::CreateCallStaticShortMethodCall;
        case TypeKind::TYPE_INT32: return &Interface::CreateCallStaticIntMethodCall;
        case TypeKind::TYPE_INT64: return &Interface::CreateCallStaticLongMethodCall;
        case TypeKind::TYPE_FLOAT32: return &Interface::CreateCallStaticFloatMethodCall;
        case TypeKind::TYPE_FLOAT64: return &Interface::CreateCallStaticDoubleMethodCall;
        default: return &Interface::CreateCallStaticObjectMethodCall;
    }
}

JniStaticCallATy MatchStaticCallAMethod(const Ty& retTy)
{
    switch (retTy.kind) {
        case TypeKind::TYPE_UNIT: return &Interface::CreateCallStaticVoidMethodACall;
        case TypeKind::TYPE_BOOLEAN: return &Interface::CreateCallStaticBooleanMethodACall;
        case TypeKind::TYPE_INT8: return &Interface::CreateCallStaticByteMethodACall;
        case TypeKind::TYPE_UINT16: return &Interface::CreateCallStaticCharMethodACall;
        case TypeKind::TYPE_INT16: return &Interface::CreateCallStaticShortMethodACall;
        case TypeKind::TYPE_INT32: return &Interface::CreateCallStaticIntMethodACall;
        case TypeKind::TYPE_INT64: return &Interface::CreateCallStaticLongMethodACall;
        case TypeKind::TYPE_FLOAT32: return &Interface::CreateCallStaticFloatMethodACall;
        case TypeKind::TYPE_FLOAT64: return &Interface::CreateCallStaticDoubleMethodACall;
        default: return &Interface::CreateCallStaticObjectMethodACall;
    }
}

JniStaticFieldGetATy MatchStaticFieldGetMethod(const Ty& ty)
{
    CJC_ASSERT(ty.kind != TypeKind::TYPE_UNIT);
    switch (ty.kind) {
        case TypeKind::TYPE_BOOLEAN: return &Interface::CreateGetStaticBooleanFieldCall;
        case TypeKind::TYPE_INT8: return &Interface::CreateGetStaticByteFieldCall;
        case TypeKind::TYPE_UINT16: return &Interface::CreateGetStaticCharFieldCall;
        case TypeKind::TYPE_INT16: return &Interface::CreateGetStaticShortFieldCall;
        case TypeKind::TYPE_INT32: return &Interface::CreateGetStaticIntFieldCall;
        case TypeKind::TYPE_INT64: return &Interface::CreateGetStaticLongFieldCall;
        case TypeKind::TYPE_FLOAT32: return &Interface::CreateGetStaticFloatFieldCall;
        case TypeKind::TYPE_FLOAT64: return &Interface::CreateGetStaticDoubleFieldCall;
        default: return &Interface::CreateGetStaticObjectFieldCall;
    }
}

JniInstanceFieldGetATy MatchInstanceFieldGetMethod(const Ty& ty)
{
    CJC_ASSERT(ty.kind != TypeKind::TYPE_UNIT);
    switch (ty.kind) {
        case TypeKind::TYPE_BOOLEAN: return &Interface::CreateGetBooleanFieldCall;
        case TypeKind::TYPE_INT8: return &Interface::CreateGetByteFieldCall;
        case TypeKind::TYPE_UINT16: return &Interface::CreateGetCharFieldCall;
        case TypeKind::TYPE_INT16: return &Interface::CreateGetShortFieldCall;
        case TypeKind::TYPE_INT32: return &Interface::CreateGetIntFieldCall;
        case TypeKind::TYPE_INT64: return &Interface::CreateGetLongFieldCall;
        case TypeKind::TYPE_FLOAT32: return &Interface::CreateGetFloatFieldCall;
        case TypeKind::TYPE_FLOAT64: return &Interface::CreateGetDoubleFieldCall;
        default: return &Interface::CreateGetObjectFieldCall;
    }
}

JniStaticFieldSetATy MatchStaticFieldSetMethod(const Ty& ty)
{
    CJC_ASSERT(ty.kind != TypeKind::TYPE_UNIT);
    switch (ty.kind) {
        case TypeKind::TYPE_BOOLEAN: return &Interface::CreateSetStaticBooleanFieldCall;
        case TypeKind::TYPE_INT8: return &Interface::CreateSetStaticByteFieldCall;
        case TypeKind::TYPE_UINT16: return &Interface::CreateSetStaticCharFieldCall;
        case TypeKind::TYPE_INT16: return &Interface::CreateSetStaticShortFieldCall;
        case TypeKind::TYPE_INT32: return &Interface::CreateSetStaticIntFieldCall;
        case TypeKind::TYPE_INT64: return &Interface::CreateSetStaticLongFieldCall;
        case TypeKind::TYPE_FLOAT32: return &Interface::CreateSetStaticFloatFieldCall;
        case TypeKind::TYPE_FLOAT64: return &Interface::CreateSetStaticDoubleFieldCall;
        default: return &Interface::CreateSetStaticObjectFieldCall;
    }
}

JniInstanceFieldSetATy MatchInstanceFieldSetMethod(const Ty& ty)
{
    CJC_ASSERT(ty.kind != TypeKind::TYPE_UNIT);
    switch (ty.kind) {
        case TypeKind::TYPE_BOOLEAN: return &Interface::CreateSetBooleanFieldCall;
        case TypeKind::TYPE_INT8: return &Interface::CreateSetByteFieldCall;
        case TypeKind::TYPE_UINT16: return &Interface::CreateSetCharFieldCall;
        case TypeKind::TYPE_INT16: return &Interface::CreateSetShortFieldCall;
        case TypeKind::TYPE_INT32: return &Interface::CreateSetIntFieldCall;
        case TypeKind::TYPE_INT64: return &Interface::CreateSetLongFieldCall;
        case TypeKind::TYPE_FLOAT32: return &Interface::CreateSetFloatFieldCall;
        case TypeKind::TYPE_FLOAT64: return &Interface::CreateSetDoubleFieldCall;
        default: return &Interface::CreateSetObjectFieldCall;
    }
}
} // namespace

OwnedPtr<Expr> JniBridge::CreateNonvirtualJavaMethodCall(const Ty& retTy,
    Ptr<Expr> jniEnvPtr, Ptr<Expr> jobject, Ptr<Expr> jclass, Ptr<Expr> jmethod, OwnedPtr<FuncArg> jniArgs) const
{
    if (jniArgs) {
        return (interface.*MatchNonVirtualCallMethodA(retTy))(jniEnvPtr,
            ASTCloner::Clone(jobject), ASTCloner::Clone(jclass), ASTCloner::Clone(jmethod), std::move(jniArgs));
    }
        
    return (interface.*MatchNonVirtualCallMethod(retTy))
        (jniEnvPtr, ASTCloner::Clone(jobject), ASTCloner::Clone(jclass), ASTCloner::Clone(jmethod));
}

OwnedPtr<Expr> JniBridge::CreateVirtualInstanceJavaMethodCall(const Ty& retTy,
    Ptr<Expr> jniEnvPtr, Ptr<Expr> jobject, Ptr<Expr> jmethod, OwnedPtr<FuncArg> jniArgs) const
{
    if (jniArgs) {
        return (interface.*MatchVirtualInstanceCallAMethod(retTy))
            (jniEnvPtr, ASTCloner::Clone(jobject), ASTCloner::Clone(jmethod), std::move(jniArgs));
    }
    return (interface.*MatchVirtualInstanceCallMethod(retTy))
        (jniEnvPtr, ASTCloner::Clone(jobject), ASTCloner::Clone(jmethod));
}

OwnedPtr<Expr> JniBridge::CreateStaticJavaMethodCall(const Ty& retTy,
    Ptr<Expr> jniEnvPtr, Ptr<Expr> jclass, Ptr<Expr> jmethod, OwnedPtr<FuncArg> jniArgs) const
{
    if (jniArgs) {
        return (interface.*MatchStaticCallAMethod(retTy))
            (jniEnvPtr, ASTCloner::Clone(jclass), ASTCloner::Clone(jmethod), std::move(jniArgs));
    }
    return (interface.*MatchStaticCallMethod(retTy))
        (jniEnvPtr, ASTCloner::Clone(jclass), ASTCloner::Clone(jmethod));
}

OwnedPtr<Expr> JniBridge::CreateGetStaticJavaFieldCall(const Ty& ty,
    Ptr<Expr> jniEnvPtr, Ptr<Expr> jclass, Ptr<Expr> jfield) const
{
    return (interface.*MatchStaticFieldGetMethod(ty))
        (jniEnvPtr, ASTCloner::Clone(jclass), ASTCloner::Clone(jfield));
}

OwnedPtr<Expr> JniBridge::CreateGetInstanceJavaFieldCall(const Ty& ty,
    Ptr<Expr> jniEnvPtr, Ptr<Expr> jobject, Ptr<Expr> jfield) const
{
    return (interface.*MatchInstanceFieldGetMethod(ty))
        (jniEnvPtr, ASTCloner::Clone(jobject), ASTCloner::Clone(jfield));
}

OwnedPtr<Expr> JniBridge::CreateSetStaticJavaFieldCall(const Ty& ty,
    Ptr<Expr> jniEnvPtr, Ptr<Expr> jclass, Ptr<Expr> jfield, Ptr<Expr> value) const
{
    return (interface.*MatchStaticFieldSetMethod(ty))
        (jniEnvPtr, ASTCloner::Clone(jclass), ASTCloner::Clone(jfield), ASTCloner::Clone(value));
}

OwnedPtr<Expr> JniBridge::CreateSetInstanceJavaFieldCall(const Ty& ty,
    Ptr<Expr> jniEnvPtr, Ptr<Expr> jobject, Ptr<Expr> jfield, Ptr<Expr> value) const
{
    return (interface.*MatchInstanceFieldSetMethod(ty))
        (jniEnvPtr, ASTCloner::Clone(jobject), ASTCloner::Clone(jfield), ASTCloner::Clone(value));
}

JniNativeInterfaceBridge::JniNativeInterfaceBridge(
    TypeManager& typeManager, const ImportManager& importManager, StructDecl& jniInterfaceDecl)
: typeManager(typeManager), importManager(importManager), jniInterfaceDecl(jniInterfaceDecl) {}

OwnedPtr<Expr> JniNativeInterfaceBridge::CreateJNIEnvReadCall(Ptr<Expr> env) const
{
    // readPointer<CPointer<JNINativeInterface_>>(env,0)
    auto read1 = CreateReadPointerCall(importManager, typeManager, env);
    // readPointer<JNINativeInterface_>(read1,0)
    auto read2 = CreateReadPointerCall(importManager, typeManager, read1);
    return read2;
}

} // namespace Cangjie::Native::FFI::Java
