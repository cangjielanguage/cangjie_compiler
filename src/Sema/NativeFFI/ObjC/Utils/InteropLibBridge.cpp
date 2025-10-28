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

} // namespace

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

Ptr<FuncDecl> InteropLibBridge::GetObjCReleaseDecl()
{
    static auto decl = GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_OBJ_C_RELEASE);
    return decl;
}

Ptr<StructDecl> InteropLibBridge::GetObjCPointerDecl()
{
    static auto decl = GetObjCLangDecl<ASTKind::STRUCT_DECL>(OBJ_C_POINTER_IDENT);
    return decl;
}
