// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares a factory class for creating AST nodes.
 */

#ifndef CANGJIE_SEMA_OBJ_C_UTILS_AST_FACTORY_H
#define CANGJIE_SEMA_OBJ_C_UTILS_AST_FACTORY_H

#include "InteropLibBridge.h"
#include "NameGenerator.h"
#include "TypeMapper.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Types.h"
#include "cangjie/Utils/SafePointer.h"

namespace Cangjie::Interop::ObjC {

class ASTFactory {
public:
    explicit ASTFactory(InteropLibBridge& bridge, TypeManager& typeManager, NameGenerator& nameGenerator,
        TypeMapper& typeMapper, ImportManager& importManager)
        : bridge(bridge),
          typeManager(typeManager),
          nameGenerator(nameGenerator),
          typeMapper(typeMapper),
          importManager(importManager)
    {
    }

    /**
     * For obj-c compatible expression `expr`, it returns corresponding expr over CType:
     * - for mirror/impl: `$expr.$obj`
     * - for primitive value: the value itself
     */
    OwnedPtr<AST::Expr> UnwrapEntity(OwnedPtr<AST::Expr> expr);

    /**
     * Returns obj-c compatible expression over `expr` of CType:
     * - for primitive value: the value itself
     * - for CPointer<Unit> (mirror M): constructor call `M(expr)`
     * - for CPointer<Unit> (impl I): call to retrieve corresponding instance from regirstry for `expr`
     */
    OwnedPtr<AST::Expr> WrapEntity(OwnedPtr<AST::Expr> expr, AST::Ty& wrapTy);

    /**
     * Returns native handle for decl mirror/impl type.
     * If `isStatic`, then returns handle related to class. (objc_getClass)
     * If not `isStatic`, then returns handle related to instance (this.$obj)
     */
    OwnedPtr<AST::Expr> CreateNativeHandleExpr(AST::ClassLikeDecl& decl, bool isStatic, Ptr<AST::File> curFile);

    /**
     * For mirror/impl `entity`, it returns pointer on obj-c object: `$entity.$obj`
     */
    OwnedPtr<AST::Expr> CreateNativeHandleExpr(OwnedPtr<AST::Expr> entity);

    /**
     * For mirror/impl type `ty`, it returns this.$obj, where `this` is a reference on `ty`
     */
    OwnedPtr<AST::Expr> CreateNativeHandleExpr(AST::ClassLikeTy& ty, Ptr<AST::File> curFile);

    OwnedPtr<AST::VarDecl> CreateNativeHandleField(AST::ClassDecl& target);
    OwnedPtr<AST::Expr> CreateNativeHandleInit(AST::FuncDecl& ctor);
    OwnedPtr<AST::FuncDecl> CreateInitCjObject(const AST::ClassDecl& target, AST::FuncDecl& ctor);
    OwnedPtr<AST::FuncDecl> CreateDeleteCjObject(AST::ClassDecl& target);
    /**
     * Returns generated top-level @C function (callable from obj-c) that calls @ObjCImpl `originMethod`.
     */
    OwnedPtr<AST::FuncDecl> CreateMethodWrapper(AST::FuncDecl& method);
    OwnedPtr<AST::FuncDecl> CreateGetterWrapper(AST::PropDecl& prop);
    OwnedPtr<AST::FuncDecl> CreateSetterWrapper(AST::PropDecl& prop);
    OwnedPtr<AST::FuncDecl> CreateGetterWrapper(AST::VarDecl& field);
    OwnedPtr<AST::FuncDecl> CreateSetterWrapper(AST::VarDecl& field);
    OwnedPtr<AST::ThrowExpr> CreateThrowUnreachableCodeExpr(AST::File& file);
    std::set<Ptr<AST::FuncDecl>> GetAllParentCtors(AST::ClassDecl& target);
    OwnedPtr<AST::FuncDecl> CreateImplCtor(AST::ClassDecl& target, AST::FuncDecl& from);
    OwnedPtr<AST::FuncDecl> CreateMirrorCtorDecl(AST::ClassDecl& target);
    bool IsGeneratedMember(const AST::Decl& decl);
    bool IsGeneratedNativeHandleField(const AST::Decl& decl);
    bool IsGeneratedCtor(const AST::Decl& decl);
    Ptr<AST::FuncDecl> GetGeneratedMirrorCtor(AST::Decl& decl);
    Ptr<AST::FuncDecl> GetGeneratedImplCtor(const AST::ClassDecl& impl, const AST::FuncDecl& origin);
    OwnedPtr<AST::CallExpr> CreateObjCRuntimeMsgSendCall(
        OwnedPtr<AST::Expr> nativeHandle,
        const std::string& selector,
        Ptr<AST::Ty> retTy,
        std::vector<OwnedPtr<AST::Expr>> args
    );
    OwnedPtr<AST::CallExpr> CreateObjCRuntimeMsgSendCall(
        Ptr<AST::FuncTy> ty,
        OwnedPtr<AST::FuncType> funcType,
        std::vector<OwnedPtr<AST::Expr>> funcArgs
    );
    OwnedPtr<AST::CallExpr> CreateGetInstanceVariableCall(
        const AST::PropDecl& field,
        OwnedPtr<AST::Expr> nativeHandle
    );
    OwnedPtr<AST::CallExpr> CreateObjCRuntimeSetInstanceVariableCall(
        const AST::PropDecl& field,
        OwnedPtr<AST::Expr> nativeHandle,
        OwnedPtr<AST::Expr> value
    );
    OwnedPtr<AST::CallExpr> CreateRegisterNameCall(OwnedPtr<AST::Expr> selectorExpr);
    OwnedPtr<AST::CallExpr> CreateRegisterNameCall(const std::string& selector, Ptr<AST::File> curFile);
    OwnedPtr<AST::Expr> CreateGetClassCall(AST::ClassLikeTy& ty, Ptr<AST::File> curFile);

    /**
     * ObjCRuntime.alloc($classHandle)
     */
    OwnedPtr<AST::CallExpr> CreateAllocCall(OwnedPtr<AST::Expr> className);
    /**
     * ObjCRuntime.alloc(decl)
     */
    OwnedPtr<AST::CallExpr> CreateAllocCall(AST::ClassDecl& decl, Ptr<AST::File> curFile);

    OwnedPtr<AST::Expr> CreateMethodCallViaMsgSend(
        AST::FuncDecl& fd,
        OwnedPtr<AST::Expr> nativeHandle,
        std::vector<OwnedPtr<AST::Expr>> rawArgs
    );

    OwnedPtr<AST::Expr> CreatePropGetterCallViaMsgSend(
        AST::PropDecl& pd,
        OwnedPtr<AST::Expr> nativeHandle
    );
    OwnedPtr<AST::Expr> CreatePropSetterCallViaMsgSend(
        AST::PropDecl& pd,
        OwnedPtr<AST::Expr> nativeHandle,
        OwnedPtr<AST::Expr> arg
    );

    OwnedPtr<AST::Expr> CreateAutoreleasePoolScope(Ptr<AST::Ty> ty, std::vector<OwnedPtr<AST::Node>> actions);

    OwnedPtr<AST::Expr> CreateUnsafePointerCast(OwnedPtr<AST::Expr> expr, Ptr<AST::Ty> elementType);
    Ptr<AST::FuncDecl> GetObjCPointerConstructor();
    Ptr<AST::VarDecl> GetObjCPointerPointerField();

    static constexpr auto NATIVE_HANDLE_IDENT = "$obj";
    static constexpr auto REGISTRY_ID_IDENT = "$registryId";

private:
    void PutDeclToClassBody(AST::Decl& decl, AST::ClassDecl& target);
    void PutDeclToFile(AST::Decl& decl, AST::File& target);
    /**
     * ObjCRuntime.putToRegistry($obj)
     */
    OwnedPtr<AST::CallExpr> CreatePutToRegistryCall(OwnedPtr<AST::Expr> nativeHandle);
    /**
     * ObjCRuntime.getFromRegistry<typeArg>(registryId)
     */
    OwnedPtr<AST::CallExpr> CreateGetFromRegistryByIdCall(
        OwnedPtr<AST::Expr> registryId, OwnedPtr<AST::Type> typeArg);

    OwnedPtr<AST::CallExpr> CreateGetFromRegistryByNativeHandleCall(
        OwnedPtr<AST::Expr> nativeHandle, OwnedPtr<AST::Type> typeArg);

    /**
     * ObjCRuntime.removeFromRegistry(registryId)
     */
    OwnedPtr<AST::CallExpr> CreateRemoveFromRegistryCall(OwnedPtr<AST::Expr> registryId);
    /**
     * ObjCRuntime.release(obj)
     */
    OwnedPtr<AST::CallExpr> CreateObjCRuntimeReleaseCall(OwnedPtr<AST::Expr> nativeHandle);

    InteropLibBridge& bridge;
    TypeManager& typeManager;
    NameGenerator& nameGenerator;
    TypeMapper& typeMapper;
    ImportManager& importManager;
};

} // namespace Cangjie::Interop::ObjC

#endif // CANGJIE_SEMA_OBJ_C_UTILS_AST_FACTORY_H
