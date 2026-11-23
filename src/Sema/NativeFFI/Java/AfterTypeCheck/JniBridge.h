// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares java JNI bridging mechanisms.
 */
#ifndef CANGJIE_SEMA_NATIVE_FFI_JAVA_JNI_BRIDGE
#define CANGJIE_SEMA_NATIVE_FFI_JAVA_JNI_BRIDGE

#include "NativeFFI/Java/AfterTypeCheck/Utils.h"
#include "NativeFFI/Utils.h"
#include "cangjie/AST/Clone.h"
#include "cangjie/AST/Node.h"
#include "cangjie/Mangle/BaseMangler.h"
#include "cangjie/Modules/ImportManager.h"
#include "cangjie/Sema/TypeManager.h"
#include "cangjie/Utils/CheckUtils.h"
#include <type_traits>

namespace Cangjie::Native::FFI::Java {

/**
 * This structure provides bridging API for JNI JNINativeInterface.
 */
struct JniNativeInterfaceBridge {
    explicit JniNativeInterfaceBridge(TypeManager& typeManager, const ImportManager& importManager,
        AST::StructDecl& jniInterfaceDecl);

    /**
     * This macro generates "create call" functions based on its names and expected parameters count.
     *
     * If you got compile error pointing here while attempting to call such function,
     * probably arguments count/type mismatch happenned.
     * Please, ensure formal parameters count is the same as passed arguments.
     */
    #define DEFINE_MEMBER(NAME, AST_KIND, PARAMS)                                                           \
    template <typename... Args> std::enable_if_t<((sizeof...(Args)) == (PARAMS) - 1), OwnedPtr<AST::Expr>>  \
        Create ## NAME ## Call(Ptr<AST::Expr> envPtr, OwnedPtr<Args>&&... args) const                       \
            {                                                                                               \
                auto& member = Get ## NAME ## Member();                                                     \
                return CreateMemberCall(typeManager,                                                        \
                    CreateJNIEnvReadCall(envPtr),                                                           \
                    member,                                                                                 \
                    ASTCloner::Clone(envPtr), std::forward<OwnedPtr<Args>>(args) ...);                      \
            }
    #include "JniNativeInterface.inc"
    #undef DEFINE_MEMBER

private:
    OwnedPtr<Expr> CreateJNIEnvReadCall(Ptr<Expr> env) const;

    template <ASTKind KIND = ASTKind::DECL>
    typename NodeKind<KIND>::Type& GetInterfaceMember(const std::string_view memberName) const
    {
        Ptr<typename NodeKind<KIND>::Type> member;
        for (auto decl : jniInterfaceDecl.GetMemberDeclPtrs()) {
            auto d = As<KIND>(decl);
            if (d && d->astKind == KIND && d->identifier.Val() == memberName) {
                member = d;
                break;
            }
        }
        CJC_NULLPTR_CHECK(member);
        return *member;
    }

/**
 * This macro generates "get member declaration" functions based on its names.
 */
#define DEFINE_MEMBER(NAME, KIND, PARAMS)                                                                     \
    NodeKind<ASTKind::KIND>::Type& Get ## NAME ## Member() const                                              \
        {                                                                                                     \
            static typename NodeKind<ASTKind::KIND>::Type& member = GetInterfaceMember<ASTKind::KIND>(#NAME); \
            return member;                                                                                    \
        }
#include "JniNativeInterface.inc"
#undef DEFINE_MEMBER

    TypeManager& typeManager;
    const ImportManager& importManager;
    AST::StructDecl& jniInterfaceDecl;
};

class JniBridge final {
public:
    explicit JniBridge(
        TypeManager& typeManager,
        const ImportManager& importManager,
        const BaseMangler& mangler,
        Interop::Java::Utils& utils,
        AST::Decl& jniEnvPtrDecl,
        AST::Decl& jniJobjectDecl,
        AST::StructDecl& jniInterfaceDecl
    );

    /**
     * $jnienv: JNIEnv_Ptr
     */
    OwnedPtr<AST::FuncParam> CreateJniEnvParam(const std::string& name = "$jnienv") const;

    /**
     * $obj: jobject or jclass.
     */
    OwnedPtr<AST::FuncParam> CreateJniJobjectOrJclassParam(const std::string& name = "$obj") const;

    /**
     * $regId: jlong
     */
    OwnedPtr<AST::FuncParam> CreateRegistryIdParam(const std::string& name = "$regId") const;

    /**
     * Creates @C function with name `name`, return type `retTy` within `curFile` at `fullPackageName` at `moduleName`.
     * Appends `userParams` to native function parameters to comply with JNI ABI.
     * Parameters order of @C function: [jniEnv, objOrClass, <userParams>].
     * jniEnv and objOrClass params are inserted automatically with references back provided.
     */
    OwnedPtr<AST::FuncDecl> CreateNativeJavaABIFunc(
        const std::string& name,
        std::vector<OwnedPtr<AST::FuncParam>> userParams,
        Ptr<AST::Ty> retTy,
        AST::File& curFile,
        std::string& moduleName,
        std::string& fullPackageName,
        std::function<void(
            AST::FuncDecl& f,
            AST::FuncParam& jniEnv,
            AST::FuncParam& objOrClass,
            std::vector<Ptr<AST::FuncParam>> userParams)> builder) const;

    std::string GetJniMethodName(const AST::FuncDecl& method) const;

    std::string GetJniMethodNameForProp(const AST::PropDecl& propDecl, bool isSet) const;

    std::string GetJniInitCjObjectFuncName(const AST::FuncDecl& ctor, bool isGeneratedCtor) const;

    std::string GetJniInitCjObjectFuncNameForVarDecl(const AST::VarDecl& ctor) const;

    std::string GetJniDeleteCjObjectFuncName(const AST::Decl& decl) const;

    std::string GetJavaNativeFunctionName(const std::string& fqTypeName, const std::string& memberName) const;

    /**
     * For CType ty, ty is returned. For mirrors and impls JNI jobject is returned
     */
    AST::Ty& ConvertCangjieToJniTy(AST::Ty& javaCompatibleTy) const;

    OwnedPtr<AST::Expr> CreateNonvirtualJavaMethodCall(const AST::Ty& retTy,
        Ptr<AST::Expr> jniEnvPtr, Ptr<AST::Expr> jobject, Ptr<AST::Expr> jclass, Ptr<AST::Expr> jmethod,
        OwnedPtr<AST::FuncArg> jniArgs = nullptr) const;

    OwnedPtr<AST::Expr> CreateVirtualInstanceJavaMethodCall(const AST::Ty& retTy,
        Ptr<AST::Expr> jniEnvPtr, Ptr<AST::Expr> jobject, Ptr<AST::Expr> jmethod,
        OwnedPtr<FuncArg> jniArgs = nullptr) const;

    OwnedPtr<AST::Expr> CreateStaticJavaMethodCall(const AST::Ty& retTy,
        Ptr<AST::Expr> jniEnvPtr, Ptr<AST::Expr> jclass, Ptr<AST::Expr> jmethod,
        OwnedPtr<FuncArg> jniArgs = nullptr) const;

    OwnedPtr<AST::Expr> CreateGetStaticJavaFieldCall(const AST::Ty& ty,
        Ptr<AST::Expr> jniEnvPtr, Ptr<AST::Expr> jclass, Ptr<AST::Expr> jfield) const;

    OwnedPtr<AST::Expr> CreateGetInstanceJavaFieldCall(const AST::Ty& ty,
        Ptr<AST::Expr> jniEnvPtr, Ptr<AST::Expr> jobject, Ptr<AST::Expr> jfield) const;

    OwnedPtr<AST::Expr> CreateSetStaticJavaFieldCall(const AST::Ty& ty,
        Ptr<AST::Expr> jniEnvPtr, Ptr<AST::Expr> jclass, Ptr<AST::Expr> jfield, Ptr<AST::Expr> value) const;

    OwnedPtr<AST::Expr> CreateSetInstanceJavaFieldCall(const AST::Ty& ty,
        Ptr<AST::Expr> jniEnvPtr, Ptr<AST::Expr> jobject, Ptr<AST::Expr> jfield, Ptr<AST::Expr> value) const;

    const JniNativeInterfaceBridge interface;

    OwnedPtr<Expr> CreateGetArrayRegionCall(const Ty& elementTy, Ptr<Expr> jniEnvPtr,
        OwnedPtr<Expr> array, OwnedPtr<Expr> start, OwnedPtr<Expr> len, OwnedPtr<FuncArg> buffer) const;

    OwnedPtr<Expr> CreateSetArrayRegionCall(const Ty& elementTy, Ptr<Expr> jniEnvPtr,
        OwnedPtr<Expr> array, OwnedPtr<Expr> start, OwnedPtr<Expr> len, OwnedPtr<FuncArg> buffer) const;

    OwnedPtr<Expr> CreateGetObjectArrayElementCall(Ptr<Expr> jniEnvPtr,
        OwnedPtr<Expr> javaArray, OwnedPtr<Expr> index) const;

    OwnedPtr<Expr> CreateSetObjectArrayElementCall(Ptr<Expr> jniEnvPtr,
        OwnedPtr<Expr> javaArray, OwnedPtr<Expr> index, OwnedPtr<Expr> value) const;

private:
    AST::Ty& GetJniEnvPtrTy() const;
    AST::Ty& GetJniJobjectDeclTy() const;
    AST::Ty& GetRegistryIdJavaTy() const;

    TypeManager& typeManager;
    const BaseMangler& mangler;
    Interop::Java::Utils& utils;

    AST::Decl& jniEnvPtrDecl;
    AST::Decl& jniJobjectDecl;
};

} // namespace Cangjie::Native::FFI::Java

#endif // CANGJIE_SEMA_NATIVE_FFI_JAVA_JNI_BRIDGE
