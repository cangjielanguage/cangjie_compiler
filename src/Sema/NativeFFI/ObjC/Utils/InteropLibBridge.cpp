// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements a thin bridge to interop runtime and support library
 */

#include "InteropLibBridge.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Node.h"

using namespace Cangjie;
using namespace Cangjie::AST;
using namespace Cangjie::Interop::ObjC;

namespace {

constexpr auto INTEROPLIB_OBJ_C_ID = "ObjCId";
constexpr auto INTEROPLIB_NATIVE_OBJ_C_ID = "NativeObjCId";
constexpr auto INTEROPLIB_NATIVE_OBJ_C_SEL = "NativeObjCSel";
constexpr auto INTEROPLIB_NATIVE_OBJ_C_SUPER_PTR = "NativeObjCSuperPtr";
constexpr auto INTEROPLIB_NATIVE_OBJ_C_CLASS = "NativeObjCClass";
constexpr auto INTEROPLIB_REGISTRY_ID = "RegistryId";
constexpr auto INTEROPLIB_OBJ_C_UNREACHABLE_CODE_EXCEPTION = "ObjCUnreachableCodeException";
constexpr auto INTEROPLIB_OBJ_C_OPTIONAL_METHOD_UNIMPLEMENTED_EXCEPTION = "ObjCOptionalMethodUnimplementedException";
constexpr auto INTEROPLIB_OBJ_C_INIT_EXCEPTION = "ObjCInitException";
constexpr auto INTEROPLIB_OBJ_C_STATIC_METHOD_CALL_ON_INTERFACE_EXCEPTION = "ObjCStaticMethodCallOnInterfaceException";
constexpr auto INTEROPLIB_NATIVE_BLOCK_ABI = "NativeBlockABI";
constexpr auto INTEROPLIB_CANGJIE_BLOCK_ABI = "CangjieBlockABI";
constexpr auto INTEROPLIB_OBJ_C_GET_FROM_REGISTRY_BY_NATIVE_HANDLE = "getFromRegistryByNativeHandle";
constexpr auto INTEROPLIB_OBJ_C_GET_FROM_REGISTRY_BY_ID = "getFromRegistryById";
constexpr auto INTEROPLIB_OBJ_C_PUT_TO_REGISTRY = "putToRegistry";
constexpr auto INTEROPLIB_OBJ_C_REMOVE_FROM_REGISTRY = "removeFromRegistry";
constexpr auto INTEROPLIB_OBJ_C_ALLOC = "objCAlloc";
constexpr auto INTEROPLIB_OBJ_C_WITH_AUTORELEASE_POOL = "withAutoreleasePool";
constexpr auto INTEROPLIB_OBJ_C_WITH_AUTORELEASE_POOL_OBJ = "withAutoreleasePoolObj";
constexpr auto INTEROPLIB_OBJ_C_REGISTER_NAME = "registerName";
constexpr auto INTEROPLIB_OBJ_C_GET_INSTANCE_VARIABLE_OBJ = "getInstanceVariableObj";
constexpr auto INTEROPLIB_OBJ_C_SET_INSTANCE_VARIABLE_OBJ = "setInstanceVariableObj";
constexpr auto INTEROPLIB_OBJ_C_GET_INSTANCE_VARIABLE = "getInstanceVariable";
constexpr auto INTEROPLIB_OBJ_C_SET_INSTANCE_VARIABLE = "setInstanceVariable";
constexpr auto INTEROPLIB_OBJ_C_GET_CLASS = "getClass";
constexpr auto INTEROPLIB_OBJ_C_GET_PROTOCOL = "getProtocol";
constexpr auto INTEROPLIB_OBJ_C_GET_SUPER_CLASS = "getSuperClass";
constexpr auto INTEROPLIB_OBJ_C_WITH_OBJ_C_SUPER = "withObjCSuper";
constexpr auto INTEROPLIB_OBJ_C_MSG_SEND = "objCMsgSend";
constexpr auto INTEROPLIB_OBJ_C_MSG_SEND_SUPER = "objCMsgSendSuper";
constexpr auto INTEROPLIB_OBJ_C_RELEASE = "objCRelease";
constexpr auto INTEROPLIB_OBJ_C_RESPONSE_TO_SELECTOR = "respondsToSelector";
constexpr auto INTEROPLIB_OBJ_C_AUTO_RELEASE = "objCAutorelease";
constexpr auto INTEROPLIB_OBJ_C_RETAIN = "objCRetain";
constexpr auto INTEROPLIB_OBJC_STORE_LAMBDA_AS_BLOCK = "registerCangjieLambdaAsBlock";
constexpr auto INTEROPLIB_OBJC_GET_LAMBDA_FROM_BLOCK = "getCangjieLambdaFromBlock";
constexpr auto INTEROPLIB_OBJ_C_OBJECT_GET_CLASS = "objectGetClass";
constexpr auto INTEROPLIB_OBJ_C_IS_KIND_OF_CLASS = "isKindOfClass";
constexpr auto INTEROPLIB_OBJ_C_CONFORMS_TO_PROTOCOL = "conformsToProtocol";
constexpr auto INTEROPLIB_OBJ_C_CONVERT_TO_NSSTRING = "convertToNSString";
constexpr auto INTEROPLIB_OBJ_C_DESCRIPTION_AS_STRING = "descriptionAsString";
constexpr auto INTEROPLIB_OBJ_C_RETAIN_AUTORELEASED_RETURN_VALUE = "objCRetainAutoreleasedReturnValue";

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

Ptr<TypeAliasDecl> InteropLibBridge::GetNativeObjCIdDecl()
{
    static auto decl = GetInteropLibDecl<ASTKind::TYPE_ALIAS_DECL>(INTEROPLIB_NATIVE_OBJ_C_ID);
    return decl;
}

Ptr<InterfaceDecl> InteropLibBridge::GetObjCIdDecl()
{
    static auto decl = GetObjCLangDecl<ASTKind::INTERFACE_DECL>(INTEROPLIB_OBJ_C_ID);
    return decl;
}

Ptr<Ty> InteropLibBridge::GetNativeObjCIdTy()
{
    return GetNativeObjCIdDecl()->type->DataTy();
}

Ptr<TypeAliasDecl> InteropLibBridge::GetNativeObjCSelDecl()
{
    static auto decl = GetInteropLibDecl<ASTKind::TYPE_ALIAS_DECL>(INTEROPLIB_NATIVE_OBJ_C_SEL);
    return decl;
}

Ptr<TypeAliasDecl> InteropLibBridge::GetNativeObjCClassDecl()
{
    static auto decl = GetInteropLibDecl<ASTKind::TYPE_ALIAS_DECL>(INTEROPLIB_NATIVE_OBJ_C_CLASS);
    return decl;
}

Ptr<Ty> InteropLibBridge::GetNativeObjCClassTy()
{
    return GetNativeObjCClassDecl()->type->DataTy();
}

Ptr<TypeAliasDecl> InteropLibBridge::GetNativeObjCSuperPtrDecl()
{
    static auto decl = GetInteropLibDecl<ASTKind::TYPE_ALIAS_DECL>(INTEROPLIB_NATIVE_OBJ_C_SUPER_PTR);
    return decl;
}

Ptr<Ty> InteropLibBridge::GetNativeObjCSuperPtrTy()
{
    return GetNativeObjCSuperPtrDecl()->type->DataTy();
}

Ptr<TypeAliasDecl> InteropLibBridge::GetRegistryIdDecl()
{
    static auto decl = GetInteropLibDecl<ASTKind::TYPE_ALIAS_DECL>(INTEROPLIB_REGISTRY_ID);
    return decl;
}

Ptr<Ty> InteropLibBridge::GetRegistryIdTy()
{
    return GetRegistryIdDecl()->type->DataTy();
}

Ptr<ClassDecl> InteropLibBridge::GetObjCUnreachableCodeExceptionDecl()
{
    static auto decl = GetInteropLibDecl<ASTKind::CLASS_DECL>(INTEROPLIB_OBJ_C_UNREACHABLE_CODE_EXCEPTION);
    return decl;
}

Ptr<ClassDecl> InteropLibBridge::GetObjCOptionalMethodUnimplementedExceptionDecl()
{
    static auto decl = GetObjCLangDecl<ASTKind::CLASS_DECL>(INTEROPLIB_OBJ_C_OPTIONAL_METHOD_UNIMPLEMENTED_EXCEPTION);
    return decl;
}

Ptr<ClassDecl> InteropLibBridge::GetObjCInitException()
{
    static auto decl = GetObjCLangDecl<ASTKind::CLASS_DECL>(INTEROPLIB_OBJ_C_INIT_EXCEPTION);
    return decl;
}

Ptr<ClassDecl> InteropLibBridge::GetObjCStaticMethodCallOnIntefaceExceptionDecl()
{
    static auto decl = GetInteropLibDecl<ASTKind::CLASS_DECL>(
        INTEROPLIB_OBJ_C_STATIC_METHOD_CALL_ON_INTERFACE_EXCEPTION);
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

Ptr<FuncDecl> InteropLibBridge::GetGetProtoDecl()
{
    static auto decl = GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_OBJ_C_GET_PROTOCOL);
    return decl;
}

Ptr<FuncDecl> InteropLibBridge::GetGetSuperClassDecl()
{
    static auto decl = GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_OBJ_C_GET_SUPER_CLASS);
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

Ptr<FuncDecl> InteropLibBridge::GetWithObjCSuperDecl()
{
    static auto decl = GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_OBJ_C_WITH_OBJ_C_SUPER);
    return decl;
}

Ptr<FuncDecl> InteropLibBridge::GetObjCIsKindOfClassDecl()
{
    static auto decl = GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_OBJ_C_IS_KIND_OF_CLASS);
    return decl;
}

Ptr<FuncDecl> InteropLibBridge::GetObjCConformsToProtocolDecl()
{
    static auto decl = GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_OBJ_C_CONFORMS_TO_PROTOCOL);
    return decl;
}

Ptr<FuncDecl> InteropLibBridge::GetConvertToNSStringDecl()
{
    static auto decl = GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_OBJ_C_CONVERT_TO_NSSTRING);
    return decl;
}

Ptr<FuncDecl> InteropLibBridge::GetDescriptionAsStringDecl()
{
    static auto decl = GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_OBJ_C_DESCRIPTION_AS_STRING);
    return decl;
}

Ptr<FuncDecl> InteropLibBridge::GetObjCRetainAutoreleasedReturnValue()
{
    static auto decl = GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_OBJ_C_RETAIN_AUTORELEASED_RETURN_VALUE);
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
            if (fieldDecl->DataTy()->IsPointer()) {
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

namespace {

Ptr<FuncDecl> GetObjCBlockConstructorByABIName(InteropLibBridge& bridge, const std::string& abiTypeName) {
    auto outer = bridge.GetObjCBlockDecl();
    for (auto& member : outer->body->decls) {
        if (auto funcDecl = DynamicCast<FuncDecl*>(member.get())) {
            if (!funcDecl->TestAttr(Attribute::CONSTRUCTOR)) {
                continue;
            }
            if (funcDecl->funcBody == nullptr
                || funcDecl->funcBody->paramLists.size() != 1
                || funcDecl->funcBody->paramLists[0] == nullptr
                || funcDecl->funcBody->paramLists[0]->params.size() != 1) {
                continue;
            }
            Ptr<Ty> paramTy = funcDecl->funcBody->paramLists[0]->params[0]->type->DataTy();
            if (!paramTy->IsPointer()) {
                continue;
            }
            if (Ty::GetDeclOfTy(paramTy->TyArg(0))->identifier == abiTypeName) {
                return funcDecl;
            }
        }
    }
    CJC_ABORT();
    return nullptr;
}

} // empty namespace

Ptr<FuncDecl> InteropLibBridge::GetObjCBlockConstructorFromObjC() {
    static Ptr<FuncDecl> result = GetObjCBlockConstructorByABIName(*this, INTEROPLIB_NATIVE_BLOCK_ABI);
    return result;
}

Ptr<FuncDecl> InteropLibBridge::GetObjCBlockConstructorFromCangjie() {
    static Ptr<FuncDecl> result = GetObjCBlockConstructorByABIName(*this, INTEROPLIB_CANGJIE_BLOCK_ABI);
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

Ptr<StructDecl> InteropLibBridge::GetNativeBlockABIDecl()
{
    return GetInteropLibDecl<ASTKind::STRUCT_DECL>(INTEROPLIB_NATIVE_BLOCK_ABI);
}

Ptr<StructDecl> InteropLibBridge::GetCangjieBlockABIDecl()
{
    return GetInteropLibDecl<ASTKind::STRUCT_DECL>(INTEROPLIB_CANGJIE_BLOCK_ABI);
}

Ptr<FuncDecl> InteropLibBridge::GetObjCStoreLambdaAsBlockDecl()
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_OBJC_STORE_LAMBDA_AS_BLOCK);
}

Ptr<FuncDecl> InteropLibBridge::GetObjCGetLambdaFromBlockDecl()
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_OBJC_GET_LAMBDA_FROM_BLOCK);
}

Ptr<FuncDecl> InteropLibBridge::GetObjectGetClassDecl()
{
    static auto decl = GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_OBJ_C_OBJECT_GET_CLASS);
    return decl;
}

bool InteropLibBridge::IsInteropLibAccessible(ImportManager& importManager)
{
    return importManager.GetPackageDecl(INTEROPLIB_PACKAGE_NAME);
}

bool InteropLibBridge::IsInteropLibAccessible() const
{
    return IsInteropLibAccessible(importManager);
}
