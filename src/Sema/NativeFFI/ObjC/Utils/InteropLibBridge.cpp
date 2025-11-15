// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements a thin bridge to interoplib.objc library
 */

#include "InteropLibBridge.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Node.h"

using namespace Cangjie::AST;
using namespace Cangjie::Interop::ObjC;

namespace {

constexpr auto INTEROPLIB_NATIVE_OBJ_C_ID = "NativeObjCId";
constexpr auto INTEROPLIB_NATIVE_OBJ_C_SEL = "NativeObjCSel";
constexpr auto INTEROPLIB_NATIVE_OBJ_C_SUPER_PTR = "NativeObjCSuperPtr";
constexpr auto INTEROPLIB_REGISTRY_ID = "RegistryId";
constexpr auto INTEROPLIB_OBJ_C_UNREACHABLE_CODE_EXCEPTION = "ObjCUnreachableCodeException";
constexpr auto INTEROPLIB_OBJ_C_OPTIONAL_METHOD_UNIMPLEMENTED_EXCEPTION = "ObjCOptionalMethodUnimplementedException";
constexpr auto INTEROPLIB_OBJ_C_GET_FROM_REGISTRY_BY_NATIVE_HANDLE = "getFromRegistryByNativeHandle";
constexpr auto INTEROPLIB_OBJ_C_GET_FROM_REGISTRY_BY_ID = "getFromRegistryById";
constexpr auto INTEROPLIB_OBJ_C_PUT_TO_REGISTRY = "putToRegistry";
constexpr auto INTEROPLIB_OBJ_C_REMOVE_FROM_REGISTRY = "removeFromRegistry";
constexpr auto INTEROPLIB_OBJ_C_ALLOC = "alloc";
constexpr auto INTEROPLIB_OBJ_C_WITH_AUTORELEASE_POOL = "withAutoreleasePool";
constexpr auto INTEROPLIB_OBJ_C_WITH_AUTORELEASE_POOL_OBJ = "withAutoreleasePoolObj";
constexpr auto INTEROPLIB_OBJ_C_REGISTER_NAME = "registerName";
constexpr auto INTEROPLIB_OBJ_C_GET_INSTANCE_VARIABLE_OBJ = "getInstanceVariableObj";
constexpr auto INTEROPLIB_OBJ_C_SET_INSTANCE_VARIABLE_OBJ = "setInstanceVariableObj";
constexpr auto INTEROPLIB_OBJ_C_GET_INSTANCE_VARIABLE = "getInstanceVariable";
constexpr auto INTEROPLIB_OBJ_C_SET_INSTANCE_VARIABLE = "setInstanceVariable";
constexpr auto INTEROPLIB_OBJ_C_GET_CLASS = "getClass";
constexpr auto INTEROPLIB_OBJ_C_WITH_METHOD_ENV = "withMethodEnv";
constexpr auto INTEROPLIB_OBJ_C_WITH_METHOD_ENV_OBJ = "withMethodEnvObj";
constexpr auto INTEROPLIB_OBJ_C_MSG_SEND = "objCMsgSend";
constexpr auto INTEROPLIB_OBJ_C_MSG_SEND_SUPER = "objCMsgSendSuper";
constexpr auto INTEROPLIB_OBJ_C_RELEASE = "objCRelease";
constexpr auto INTEROPLIB_OBJ_C_RESPONSE_TO_SELECTOR = "respondsToSelector";
constexpr auto INTEROPLIB_OBJ_C_AUTO_RELEASE = "objCAutorelease";
constexpr auto INTEROPLIB_OBJ_C_RETAIN = "objCRetain";
constexpr auto INTEROPLIB_FORWARDER_MUTEX = "ForwarderMutex";
constexpr auto MUTEX_LOCK_IDENT = "lock";
constexpr auto MUTEX_UNLOCK_IDENT = "unlock";

// objc.lang
constexpr auto OBJ_C_FUNC_GET_FPOINTER = "unsafeGetFunctionPointer";
constexpr auto OBJ_C_BLOCK_GET_ABI_POINTER = "unsafeGetNativeABIPointer";

template<typename T>
Ptr<T> GetMemberOfDecl(Decl& decl, std::function<bool(const Decl&)> pred)
{
    Ptr<Decl> result = nullptr;
    for (auto& member : decl.GetMemberDeclPtrs()) {
        if (pred(*member)) {
            result = member;
            break;
        }
    }
    return Cangjie::DynamicCast<T>(result);
}
} // namespace

/**
 * Gets ForwarderMutex declaration.
 */
Ptr<TypeAliasDecl> InteropLibBridge::GetForwarderMutexDecl()
{
    static auto decl = GetInteropLibDecl<ASTKind::TYPE_ALIAS_DECL>(INTEROPLIB_FORWARDER_MUTEX);
    return decl;
}

/**
 * Gets ForwarderMutex semantic type (std.sync.Mutex).
 */
Ptr<Ty> InteropLibBridge::GetForwarderMutexTy()
{
    return GetForwarderMutexDecl()->type->ty;
}

/**
 * Gets the lock decl of ForwarderMutex (std.sync.Mutex).
 */
Ptr<FuncDecl> InteropLibBridge::GetMutexLock()
{
    static Ptr<FuncDecl> result = nullptr;
    if (result) {
        return result;
    }
    auto decl = Ty::GetDeclOfTy(GetForwarderMutexTy());
    CJC_NULLPTR_CHECK(decl);
    result = GetMemberOfDecl<FuncDecl>(*decl, [](const Decl& mem) {
        return mem.identifier == MUTEX_LOCK_IDENT;
    });
    CJC_NULLPTR_CHECK(result);
    return result;
}

/**
 * Gets the unlock decl of ForwarderMutex (std.sync.Mutex).
 */
Ptr<FuncDecl> InteropLibBridge::GetMutexUnlock()
{
    static Ptr<FuncDecl> result = nullptr;
    if (result) {
        return result;
    }
    auto decl = Ty::GetDeclOfTy(GetForwarderMutexTy());
    CJC_NULLPTR_CHECK(decl);
    result = GetMemberOfDecl<FuncDecl>(*decl, [](const Decl& mem) {
        return mem.identifier == MUTEX_UNLOCK_IDENT;
    });
    CJC_NULLPTR_CHECK(result);
    return result;
}

/**
 * Gets the constructor decl of ForwarderMutex.
 */
Ptr<FuncDecl> InteropLibBridge::GetMutexConstructor()
{
    static Ptr<FuncDecl> result = nullptr;
    if (result) {
        return result;
    }
    auto decl = Ty::GetDeclOfTy(GetForwarderMutexTy());
    CJC_NULLPTR_CHECK(decl);
    result = GetMemberOfDecl<FuncDecl>(*decl, [](const Decl& mem) {
        return mem.TestAttr(Attribute::CONSTRUCTOR);
    });
    CJC_NULLPTR_CHECK(result);
    return result;
}

Ptr<TypeAliasDecl> InteropLibBridge::GetNativeObjCIdDecl()
{
    static auto decl = GetInteropLibDecl<ASTKind::TYPE_ALIAS_DECL>(INTEROPLIB_NATIVE_OBJ_C_ID);
    return decl;
}

Ptr<Ty> InteropLibBridge::GetNativeObjCIdTy()
{
    return GetNativeObjCIdDecl()->type->ty;
}

Ptr<TypeAliasDecl> InteropLibBridge::GetNativeObjCSelDecl()
{
    static auto decl = GetInteropLibDecl<ASTKind::TYPE_ALIAS_DECL>(INTEROPLIB_NATIVE_OBJ_C_SEL);
    return decl;
}

Ptr<TypeAliasDecl> InteropLibBridge::GetNativeObjCSuperPtrDecl()
{
    static auto decl = GetInteropLibDecl<ASTKind::TYPE_ALIAS_DECL>(INTEROPLIB_NATIVE_OBJ_C_SUPER_PTR);
    return decl;
}

Ptr<Ty> InteropLibBridge::GetNativeObjCSuperPtrTy()
{
    return GetNativeObjCSuperPtrDecl()->type->ty;
}

Ptr<TypeAliasDecl> InteropLibBridge::GetRegistryIdDecl()
{
    static auto decl = GetInteropLibDecl<ASTKind::TYPE_ALIAS_DECL>(INTEROPLIB_REGISTRY_ID);
    return decl;
}

Ptr<Ty> InteropLibBridge::GetRegistryIdTy()
{
    return GetRegistryIdDecl()->type->ty;
}

Ptr<ClassDecl> InteropLibBridge::GetObjCUnreachableCodeExceptionDecl()
{
    static auto decl = GetInteropLibDecl<ASTKind::CLASS_DECL>(INTEROPLIB_OBJ_C_UNREACHABLE_CODE_EXCEPTION);
    return decl;
}

Ptr<ClassDecl> InteropLibBridge::GetObjCOptionalMethodUnimplementedExceptionDecl()
{
    static auto decl = GetInteropLibDecl<ASTKind::CLASS_DECL>(INTEROPLIB_OBJ_C_OPTIONAL_METHOD_UNIMPLEMENTED_EXCEPTION);
    return decl;
}

Ptr<FuncDecl> InteropLibBridge::GetGetFromRegistryByNativeHandleDecl()
{
    static auto decl = GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_OBJ_C_GET_FROM_REGISTRY_BY_NATIVE_HANDLE);
    return decl;
}

Ptr<FuncDecl> InteropLibBridge::GetGetFromRegistryByIdDecl()
{
    static auto decl = GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_OBJ_C_GET_FROM_REGISTRY_BY_ID);
    return decl;
}

Ptr<FuncDecl> InteropLibBridge::GetPutToRegistryDecl()
{
    static auto decl = GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_OBJ_C_PUT_TO_REGISTRY);
    return decl;
}

Ptr<FuncDecl> InteropLibBridge::GetRemoveFromRegistryDecl()
{
    static auto decl = GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_OBJ_C_REMOVE_FROM_REGISTRY);
    return decl;
}

Ptr<FuncDecl> InteropLibBridge::GetAllocDecl()
{
    static auto decl = GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_OBJ_C_ALLOC);
    return decl;
}

Ptr<FuncDecl> InteropLibBridge::GetWithAutoreleasePoolDecl()
{
    static auto decl = GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_OBJ_C_WITH_AUTORELEASE_POOL);
    return decl;
}

Ptr<FuncDecl> InteropLibBridge::GetWithAutoreleasePoolObjDecl()
{
    static auto decl = GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_OBJ_C_WITH_AUTORELEASE_POOL_OBJ);
    return decl;
}

Ptr<FuncDecl> InteropLibBridge::GetRegisterNameDecl()
{
    static auto decl = GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_OBJ_C_REGISTER_NAME);
    return decl;
}

Ptr<FuncDecl> InteropLibBridge::GetGetInstanceVariableObjDecl()
{
    static auto decl = GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_OBJ_C_GET_INSTANCE_VARIABLE_OBJ);
    return decl;
}

Ptr<FuncDecl> InteropLibBridge::GetSetInstanceVariableObjDecl()
{
    static auto decl = GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_OBJ_C_SET_INSTANCE_VARIABLE_OBJ);
    return decl;
}

Ptr<FuncDecl> InteropLibBridge::GetGetInstanceVariableDecl()
{
    static auto decl = GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_OBJ_C_GET_INSTANCE_VARIABLE);
    return decl;
}

Ptr<FuncDecl> InteropLibBridge::GetSetInstanceVariableDecl()
{
    static auto decl = GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_OBJ_C_SET_INSTANCE_VARIABLE);
    return decl;
}

Ptr<FuncDecl> InteropLibBridge::GetGetClassDecl()
{
    static auto decl = GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_OBJ_C_GET_CLASS);
    return decl;
}

Ptr<FuncDecl> InteropLibBridge::GetWithMethodEnvDecl()
{
    static auto decl = GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_OBJ_C_WITH_METHOD_ENV);
    return decl;
}

Ptr<FuncDecl> InteropLibBridge::GetWithMethodEnvObjDecl()
{
    static auto decl = GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_OBJ_C_WITH_METHOD_ENV_OBJ);
    return decl;
}

Ptr<FuncDecl> InteropLibBridge::GetObjCMsgSendDecl()
{
    static auto decl = GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_OBJ_C_MSG_SEND);
    return decl;
}

Ptr<FuncDecl> InteropLibBridge::GetObjCMsgSendSuperDecl()
{
    static auto decl = GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_OBJ_C_MSG_SEND_SUPER);
    return decl;
}

Ptr<FuncDecl> InteropLibBridge::GetObjCRespondsToSelectorDecl()
{
    static auto decl = GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_OBJ_C_RESPONSE_TO_SELECTOR);
    return decl;
}

Ptr<FuncDecl> InteropLibBridge::GetObjCReleaseDecl()
{
    static auto decl = GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_OBJ_C_RELEASE);
    return decl;
}

Ptr<FuncDecl> InteropLibBridge::GetObjCRetainDecl()
{
    static auto decl = GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_OBJ_C_RETAIN);
    return decl;
}

Ptr<FuncDecl> InteropLibBridge::GetObjCAutoReleaseDecl()
{
    static auto decl = GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_OBJ_C_AUTO_RELEASE);
    return decl;
}

Ptr<StructDecl> InteropLibBridge::GetObjCPointerDecl()
{
    static auto result = GetObjCLangDecl<ASTKind::STRUCT_DECL>(OBJ_C_POINTER_IDENT);
    return result;
}

Ptr<StructDecl> InteropLibBridge::GetObjCFuncDecl()
{
    static auto result =  GetObjCLangDecl<ASTKind::STRUCT_DECL>(OBJ_C_FUNC_IDENT);
    return result;
}

Ptr<ClassDecl> InteropLibBridge::GetObjCBlockDecl() {
    static auto result = GetObjCLangDecl<ASTKind::CLASS_DECL>(OBJ_C_BLOCK_IDENT);
    return result;
}

Ptr<FuncDecl> InteropLibBridge::GetObjCPointerConstructor()
{
    static Ptr<FuncDecl> result = nullptr;
    if (result) {
        return result;
    }
    auto outer = GetObjCPointerDecl();
    for (auto& member : outer->body->decls) {
        if (auto funcDecl = DynamicCast<FuncDecl*>(member.get())) {
            if (funcDecl->TestAttr(Attribute::CONSTRUCTOR)
                && funcDecl->funcBody
                && funcDecl->funcBody->paramLists[0]
                && funcDecl->funcBody->paramLists[0]->params.size() == 1) {
                result = funcDecl;
                break;
            }
        }
    }
    return result;
}

Ptr<VarDecl> InteropLibBridge::GetObjCPointerPointerField()
{
    static Ptr<VarDecl> result = nullptr;
    if (result) {
        return result;
    }
    auto outer = GetObjCPointerDecl();
    for (auto& member : outer->body->decls) {
        if (auto fieldDecl = DynamicCast<VarDecl*>(member.get())) {
            if (fieldDecl->ty->IsPointer()) {
                result = fieldDecl;
                break;
            }
        }
    }
    return result;
}

Ptr<FuncDecl> InteropLibBridge::GetObjCFuncConstructor()
{
    static Ptr<FuncDecl> result = nullptr;
    if (result) {
        return result;
    }
    auto outer = GetObjCFuncDecl();
    for (auto& member : outer->body->decls) {
        if (auto funcDecl = DynamicCast<FuncDecl*>(member.get())) {
            if (funcDecl->TestAttr(Attribute::CONSTRUCTOR)
                && funcDecl->funcBody
                && funcDecl->funcBody->paramLists[0]
                && funcDecl->funcBody->paramLists[0]->params.size() == 1) {
                result = funcDecl;
                break;
            }
        }
    }
    return result;
}

Ptr<FuncDecl> InteropLibBridge::GetObjCFuncFPointerAccessor()
{
    static Ptr<FuncDecl> result = nullptr;
    if (result != nullptr) {
        return result;
    }
    auto outer = GetObjCFuncDecl();
    for (auto& member : outer->body->decls) {
        if (auto funcDecl = DynamicCast<FuncDecl*>(member.get())) {
            if (funcDecl->identifier == OBJ_C_FUNC_GET_FPOINTER) {
                result = funcDecl;
                break;
            }
        }
    }
    return result;
}


Ptr<FuncDecl> InteropLibBridge::GetObjCBlockConstructor() {
    static Ptr<FuncDecl> result = nullptr;
    if (result) {
        return result;
    }
    auto outer = GetObjCBlockDecl();
    for (auto& member : outer->body->decls) {
        if (auto funcDecl = DynamicCast<FuncDecl*>(member.get())) {
            if (funcDecl->TestAttr(Attribute::CONSTRUCTOR)
                && funcDecl->funcBody
                && funcDecl->funcBody->paramLists[0]
                && funcDecl->funcBody->paramLists[0]->params.size() == 1) {
                result = funcDecl;
                break;
            }
        }
    }
    return result;
}
Ptr<FuncDecl> InteropLibBridge::GetObjCBlockFPointerAccessor()
{
    static Ptr<FuncDecl> result = nullptr;
    if (result != nullptr) {
        return result;
    }
    auto outer = GetObjCBlockDecl();
    for (auto& member : outer->body->decls) {
        if (auto funcDecl = DynamicCast<FuncDecl*>(member.get())) {
            if (funcDecl->identifier == OBJ_C_FUNC_GET_FPOINTER) {
                result = funcDecl;
                break;
            }
        }
    }
    return result;
}

Ptr<FuncDecl> InteropLibBridge::GetObjCBlockAbiPointerAccessor()
{
    static Ptr<FuncDecl> result = nullptr;
    if (result != nullptr) {
        return result;
    }
    auto outer = GetObjCBlockDecl();
    for (auto& member : outer->body->decls) {
        if (auto funcDecl = DynamicCast<FuncDecl*>(member.get())) {
            if (funcDecl->identifier == OBJ_C_BLOCK_GET_ABI_POINTER) {
                result = funcDecl;
                break;
            }
        }
    }
    return result;
}
