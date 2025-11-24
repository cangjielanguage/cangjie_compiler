// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares a thin bridge to interoplib.objc library
 */

#ifndef CANGJIE_SEMA_OBJ_C_UTILS_INTEROPLIB_BRIDGE_H
#define CANGJIE_SEMA_OBJ_C_UTILS_INTEROPLIB_BRIDGE_H

#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"
#include "cangjie/Modules/ImportManager.h"
#include "cangjie/Utils/SafePointer.h"

namespace Cangjie::Interop::ObjC {
class InteropLibBridge {
public:
    explicit InteropLibBridge(ImportManager& importManager, DiagnosticEngine& diag)
        : importManager(importManager), diag(diag)
    {
    }

    /**
     * Gets NativeObjCId declaration.
     * id or Instance Method Pointer type.
     */
    Ptr<AST::TypeAliasDecl> GetNativeObjCIdDecl();

    /**
     * Gets NativeObjCId semantic type.
     */
    Ptr<AST::Ty> GetNativeObjCIdTy();

    /**
     * Gets NativeObjCSel declaration.
     * SEL.
     */
    Ptr<AST::TypeAliasDecl> GetNativeObjCSelDecl();

    /**
     * Gets NativeObjCSuperPtr (CPointer<NativeObjCSuper>) declaration.
     * objc_super*.
     */
    Ptr<AST::TypeAliasDecl> GetNativeObjCSuperPtrDecl();

    Ptr<AST::Ty> GetNativeObjCSuperPtrTy();

    /**
     * Gets RegistryId declaration.
     * An opaque identifier for Cangjie mirror objects.
     */
    Ptr<AST::TypeAliasDecl> GetRegistryIdDecl();

    /**
     * Gets RegistryId semantic type.
     */
    Ptr<AST::Ty> GetRegistryIdTy();

    /**
     * Gets ObjCUnreachableCodeException declaration.
     * An exception that has to be used to mark an unreachable code (e.g instantiation @ObjCImpl objects from Cangjie
     * side).
     */
    Ptr<AST::ClassDecl> GetObjCUnreachableCodeExceptionDecl();

    Ptr<AST::ClassDecl> GetObjCOptionalMethodUnimplementedExceptionDecl();

    Ptr<AST::FuncDecl> GetGetFromRegistryByNativeHandleDecl();

    Ptr<AST::FuncDecl> GetGetFromRegistryByIdDecl();

    Ptr<AST::FuncDecl> GetPutToRegistryDecl();

    Ptr<AST::FuncDecl> GetRemoveFromRegistryDecl();

    Ptr<AST::FuncDecl> GetAllocDecl();

    Ptr<AST::FuncDecl> GetWithAutoreleasePoolDecl();

    Ptr<AST::FuncDecl> GetWithAutoreleasePoolObjDecl();

    Ptr<AST::FuncDecl> GetRegisterNameDecl();

    Ptr<AST::FuncDecl> GetGetInstanceVariableObjDecl();

    Ptr<AST::FuncDecl> GetSetInstanceVariableObjDecl();

    Ptr<AST::FuncDecl> GetGetInstanceVariableDecl();

    Ptr<AST::FuncDecl> GetSetInstanceVariableDecl();

    Ptr<AST::FuncDecl> GetGetClassDecl();

    Ptr<AST::FuncDecl> GetWithMethodEnvDecl();

    Ptr<AST::FuncDecl> GetWithMethodEnvObjDecl();

    Ptr<AST::FuncDecl> GetObjCMsgSendDecl();

    Ptr<AST::FuncDecl> GetObjCMsgSendSuperDecl();

    Ptr<AST::FuncDecl> GetObjCReleaseDecl();

    Ptr<AST::FuncDecl> GetObjCRespondsToSelectorDecl();

    Ptr<AST::FuncDecl> GetGetProtocolDecl();

    /**
     * Get objc.lang.ObjCPointer declaration
    */
    Ptr<AST::StructDecl> GetObjCPointerDecl();
    Ptr<AST::FuncDecl> GetObjCPointerConstructor();
    Ptr<AST::VarDecl> GetObjCPointerPointerField();

    /**
     * Get objc.lang.ObjCFunc declaration
    */
    Ptr<AST::StructDecl> GetObjCFuncDecl();
    Ptr<AST::FuncDecl> GetObjCFuncConstructor();
    Ptr<AST::FuncDecl> GetObjCFuncFPointerAccessor();

    /**
     * Get objc.lang.ObjCBlock declaration
    */
    Ptr<AST::ClassDecl> GetObjCBlockDecl();
    Ptr<AST::FuncDecl> GetObjCBlockConstructor();
    Ptr<AST::FuncDecl> GetObjCBlockAbiPointerAccessor();
    Ptr<AST::FuncDecl> GetObjCBlockFPointerAccessor();

private:
    template <AST::ASTKind K = AST::ASTKind::DECL> auto GetInteropLibDecl(const std::string& ident)
    {
        auto decl = importManager.GetImportedDecl(INTEROPLIB_OBJ_C_PACKAGE_IDENT, ident);
        if (!decl) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_member_not_imported, DEFAULT_POSITION,
                INTEROPLIB_OBJ_C_PACKAGE_IDENT + std::string(".") + ident);
            return Ptr(AST::As<K>(nullptr));
        }

        CJC_ASSERT(decl && decl->astKind == K);
        return Ptr(AST::StaticAs<K>(decl));
    }

    template <AST::ASTKind K = AST::ASTKind::DECL> auto GetObjCLangDecl(const std::string& ident)
    {
        auto decl = importManager.GetImportedDecl(OBJ_C_LANG_PACKAGE_IDENT, ident);
        if (!decl) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_member_not_imported, DEFAULT_POSITION, ident);
            return Ptr(AST::As<K>(nullptr));
        }

        CJC_ASSERT(decl && decl->astKind == K);
        return Ptr(AST::StaticAs<K>(decl));
    }

    ImportManager& importManager;
    DiagnosticEngine& diag;
};

} // namespace Cangjie::Interop::ObjC

#endif // CANGJIE_SEMA_OBJ_C_UTILS_INTEROPLIB_BRIDGE_H
