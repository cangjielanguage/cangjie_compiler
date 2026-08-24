// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "JniBridge.h"
#include "NativeFFI/Utils.h"
#include "Utils.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Types.h"
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
    const BaseMangler& mangler,
    Interop::Java::Utils& utils,
    Decl& jniEnvPtrDecl,
    Decl& jniJobjectDecl) : typeManager(typeManager), mangler(mangler), utils(utils),
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
    std::string fqname = GetJavaFQName(*(propDecl.outerDecl));

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

std::string JniBridge::GetJniInitCjObjectFuncNameForVarDecl(const AST::VarDecl& ctor) const
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

} // namespace Cangjie::Native::FFI::Java
