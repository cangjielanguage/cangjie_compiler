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
     * - for mirror/impl: `$expr.$getObj()`
     * - for primitive value: the value itself
     */
    OwnedPtr<AST::Expr> UnwrapEntity(OwnedPtr<AST::Expr> expr);

    /**
     * Returns obj-c compatible expression over `expr` of CType:
     * - for primitive value: the value itself
     * - for CPointer<Unit> (mirror class M): constructor call `M(expr)`
     * - for CPointer<Unit> (mirror interface M): constructor call `M$impl(expr)`
     * - for CPointer<Unit> (impl I): call to retrieve corresponding instance from regirstry for `expr`
     */
    OwnedPtr<AST::Expr> WrapEntity(OwnedPtr<AST::Expr> expr, AST::Ty& wrapTy);

    // Option type handling
    Ptr<AST::Ty> GetOptionTy(Ptr<AST::Ty> ty);
    Ptr<AST::EnumDecl> GetOptionDecl();
    Ptr<AST::Decl> GetOptionSomeDecl();
    Ptr<AST::Decl> GetOptionNoneDecl();
    OwnedPtr<AST::Expr> CreateOptionSomeRef(Ptr<AST::Ty> ty);
    OwnedPtr<AST::Expr> CreateOptionNoneRef(Ptr<AST::Ty> ty);
    OwnedPtr<AST::Expr> CreateOptionSomeCall(OwnedPtr<AST::Expr> expr, Ptr<AST::Ty> ty);
    OwnedPtr<AST::Expr> CreateMirrorConstructorCall(OwnedPtr<AST::Expr> entity, Ptr<AST::Ty> mirrorTy);

    /**
     * Returns native handle for decl mirror/impl type.
     * If `isStatic`, then returns handle related to class. (objc_getClass)
     * If not `isStatic`, then returns handle related to instance (this.$getObj())
     */
    OwnedPtr<AST::Expr> CreateNativeHandleExpr(AST::ClassLikeDecl& decl, bool isStatic, Ptr<AST::File> curFile);

    /**
     * For mirror/impl `entity`, it returns pointer on obj-c object: `$entity.$getObj()`
     */
    OwnedPtr<AST::Expr> CreateNativeHandleExpr(OwnedPtr<AST::Expr> entity);

    /**
     * For mirror/impl type `ty`, it returns this.$getObj(), where `this` is a reference on `ty`
     */
    OwnedPtr<AST::Expr> CreateNativeHandleExpr(AST::ClassLikeTy& ty, Ptr<AST::File> curFile);

    OwnedPtr<AST::VarDecl> CreateNativeHandleField(AST::ClassDecl& target);
    OwnedPtr<AST::FuncDecl> CreateInitCjObject(
        const AST::Decl& target, AST::FuncDecl& ctor, bool generateForOneWayMapping = false);
    /**
     * @C
     * public func CJImpl_ObjC_PackageName_VarDeclNameInitCJObject() : RegistryID {
     *     let tu = EnumName.VarDeclName
     *     putToRegistry(tu)
     * }
     */
    OwnedPtr<AST::FuncDecl> CreateInitCjObjectForEnumNoParams(AST::EnumDecl& target, AST::VarDecl& ctor);
    OwnedPtr<AST::FuncDecl> CreateDeleteCjObject(AST::Decl& target, bool generateForOneWayMapping = false);
    /**
     * Returns generated top-level @C function (callable from obj-c) that calls @ObjCImpl `originMethod`.
     */
    OwnedPtr<AST::FuncDecl> CreateMethodWrapper(AST::FuncDecl& method);
    OwnedPtr<AST::FuncDecl> CreateGetterWrapper(AST::PropDecl& prop);
    OwnedPtr<AST::FuncDecl> CreateSetterWrapper(AST::PropDecl& prop);
    OwnedPtr<AST::FuncDecl> CreateGetterWrapper(AST::VarDecl& field);
    OwnedPtr<AST::FuncDecl> CreateSetterWrapper(AST::VarDecl& field);
    OwnedPtr<AST::ThrowExpr> CreateThrowUnreachableCodeExpr(AST::File& file);
    OwnedPtr<AST::ThrowExpr> CreateThrowOptionalMethodUnimplemented(AST::File& file);
    std::set<Ptr<AST::FuncDecl>> GetAllParentCtors(AST::ClassDecl& target) const;
    OwnedPtr<AST::FuncDecl> CreateImplCtor(AST::ClassDecl& target, AST::FuncDecl& from);
    OwnedPtr<AST::FuncDecl> CreateBaseCtorDecl(AST::ClassDecl& target);
    bool IsGeneratedMember(const AST::Decl& decl) const;
    bool IsGeneratedNativeHandleField(const AST::Decl& decl) const;
    bool IsGeneratedHasInitedField(const AST::Decl& decl) const;
    bool IsGeneratedCtor(const AST::Decl& decl) const;
    bool IsGeneratedNativeHandleGetter(const AST::Decl& decl) const;
    Ptr<AST::FuncDecl> GetGeneratedBaseCtor(AST::Decl& decl);
    Ptr<AST::FuncDecl> GetGeneratedImplCtor(const AST::Decl& declArg, const AST::FuncDecl& origin);
    OwnedPtr<AST::CallExpr> CreateObjCMsgSendCall(OwnedPtr<AST::Expr> nativeHandle, const std::string& selector,
        Ptr<AST::Ty> retTy, std::vector<OwnedPtr<AST::Expr>> args);
    OwnedPtr<AST::CallExpr> CreateObjCMsgSendCall(
        Ptr<AST::FuncTy> ty, OwnedPtr<AST::FuncType> funcType, std::vector<OwnedPtr<AST::Expr>> funcArgs);
    OwnedPtr<AST::CallExpr> CreateGetInstanceVariableCall(const AST::PropDecl& field, OwnedPtr<AST::Expr> nativeHandle);
    OwnedPtr<AST::CallExpr> CreateSetInstanceVariableCall(
        const AST::PropDecl& field, OwnedPtr<AST::Expr> nativeHandle, OwnedPtr<AST::Expr> value);
    OwnedPtr<AST::CallExpr> CreateRegisterNameCall(OwnedPtr<AST::Expr> selectorExpr);
    OwnedPtr<AST::CallExpr> CreateRegisterNameCall(const std::string& selector, Ptr<AST::File> curFile);
    OwnedPtr<AST::Expr> CreateGetClassCall(AST::ClassTy& ty, Ptr<AST::File> curFile);
    OwnedPtr<AST::Expr> CreateObjCRespondsToSelectorCall(OwnedPtr<AST::Expr> id, OwnedPtr<AST::Expr> sel, Ptr<AST::File> file);

    /**
     * alloc($classHandle)
     */
    OwnedPtr<AST::CallExpr> CreateAllocCall(OwnedPtr<AST::Expr> className);
    /**
     * alloc(decl)
     */
    OwnedPtr<AST::CallExpr> CreateAllocCall(AST::Decl& decl, Ptr<AST::File> curFile);

    OwnedPtr<AST::Expr> CreateMethodCallViaMsgSend(
        AST::FuncDecl& fd, OwnedPtr<AST::Expr> nativeHandle, std::vector<OwnedPtr<AST::Expr>> rawArgs);

    /**
     * Creates method call of `fd` passing exact parameters mapping with  unwrapping and `handle`.
     */
    OwnedPtr<AST::Expr> CreateMethodCallViaMsgSend(AST::FuncDecl& fd, OwnedPtr<AST::Expr> handle);

    /**
     * Creates alloc-init chain over $fd.outerDecl as native handle with corresponding parameters passed
     */
    OwnedPtr<AST::Expr> CreateAllocInitCall(AST::FuncDecl& fd);

    OwnedPtr<AST::Expr> CreatePropGetterCallViaMsgSend(
        AST::PropDecl& pd,
        OwnedPtr<AST::Expr> nativeHandle
    );
    OwnedPtr<AST::Expr> CreatePropSetterCallViaMsgSend(
        AST::PropDecl& pd,
        OwnedPtr<AST::Expr> nativeHandle,
        OwnedPtr<AST::Expr> arg
    );

    OwnedPtr<AST::Expr> CreateFuncCallViaOpaquePointer(
        OwnedPtr<AST::Expr> ptr,
        Ptr<AST::Ty> retTy,
        std::vector<OwnedPtr<AST::Expr>> args
    );

    OwnedPtr<AST::Expr> CreateAutoreleasePoolScope(Ptr<AST::Ty> ty, std::vector<OwnedPtr<AST::Node>> actions);
    OwnedPtr<AST::FuncDecl> CreateFinalizer(AST::ClassDecl& target);
    OwnedPtr<AST::VarDecl> CreateHasInitedField(AST::ClassDecl& target);

    OwnedPtr<AST::Expr> CreateUnsafePointerCast(OwnedPtr<AST::Expr> expr, Ptr<AST::Ty> elementType);

    void SetDesugarExpr(Ptr<AST::Expr> original, OwnedPtr<AST::Expr> desugared);
    OwnedPtr<AST::Expr> WrapObjCMirrorOption(
        const Ptr<AST::Expr> entity, Ptr<AST::ClassLikeDecl> mirror, const Ptr<AST::File> curFile);
    OwnedPtr<AST::Expr> CreateObjCobjectNull();
    Ptr<AST::Ty> GetObjCTy();
    OwnedPtr<AST::Expr> CreateGetObjcEntityOrNullCall(AST::VarDecl& entity, Ptr<AST::File> file);
    OwnedPtr<AST::Expr> CreateOptionCast(Ptr<AST::VarDecl> jObjectVar, AST::ClassLikeDecl castDecl);
    OwnedPtr<AST::Expr> UnwrapObjCMirrorOption(OwnedPtr<AST::Expr> entity, Ptr<AST::Ty> ty);
    OwnedPtr<AST::Expr> CreateOptionMatch(OwnedPtr<AST::Expr> selector,
        std::function<OwnedPtr<AST::Expr>(AST::VarDecl&)> someBranch, std::function<OwnedPtr<AST::Expr>()> noneBranch,
        Ptr<AST::Ty> ty);

    OwnedPtr<AST::FuncDecl> CreateNativeHandleGetterDecl(AST::ClassLikeDecl& target);
    OwnedPtr<AST::Expr> CreateNativeHandleFieldExpr(AST::ClassDecl& target);
    /**
     * objCRelease($obj)
     */
    OwnedPtr<AST::Expr> CreateObjCReleaseCall(OwnedPtr<AST::Expr> nativeHandle);
    OwnedPtr<AST::Expr> CreateWithMethodEnvScope(OwnedPtr<AST::Expr> nativeHandle, Ptr<AST::Ty> retTy,
        std::function<std::vector<OwnedPtr<AST::Node>>(OwnedPtr<AST::Expr>, OwnedPtr<AST::Expr>)> bodyFactory);

    OwnedPtr<AST::Expr> CreateMethodCallViaMsgSendSuper(AST::FuncDecl& fd, OwnedPtr<AST::Expr> receiver,
        OwnedPtr<AST::Expr> objCSuper, std::vector<OwnedPtr<AST::Expr>> rawArgs);
    OwnedPtr<AST::Expr> CreatePropGetterCallViaMsgSendSuper(
        AST::PropDecl& pd, OwnedPtr<AST::Expr> receiver, OwnedPtr<AST::Expr> objCSuper);
    OwnedPtr<AST::Expr> CreatePropSetterCallViaMsgSendSuper(
        AST::PropDecl& pd, OwnedPtr<AST::Expr> receiver, OwnedPtr<AST::Expr> objCSuper, OwnedPtr<AST::Expr> value);
    /**
     * putToRegistry($obj)
     */
    OwnedPtr<AST::CallExpr> CreatePutToRegistryCall(OwnedPtr<AST::Expr> nativeHandle);
    /**
     * getFromRegistry<typeArg>(registryId)
     */
    OwnedPtr<AST::CallExpr> CreateGetFromRegistryByIdCall(OwnedPtr<AST::Expr> registryId, OwnedPtr<AST::Type> typeArg);

    /**
     * match(respondsToSelector($obj, selector)) {
     *      true => msgSend()
     *      false => throw ObjCOptionalMethodUnimplementedException()
     * }
    */
    OwnedPtr<AST::Expr> CreateOptionalMethodGuard(OwnedPtr<AST::Expr> msgSend, OwnedPtr<AST::Expr> id, const std::string& selector,
        const Ptr<AST::File> curFile);

private:
    void PutDeclToClassLikeBody(AST::Decl& decl, AST::ClassLikeDecl& target);
    void PutDeclToClassBody(AST::Decl& decl, AST::ClassDecl& target);
    void PutDeclToInterfaceBody(AST::Decl& decl, AST::InterfaceDecl& target);
    void PutDeclToFile(AST::Decl& decl, AST::File& target);

    OwnedPtr<AST::CallExpr> CreateGetFromRegistryByNativeHandleCall(
        OwnedPtr<AST::Expr> nativeHandle, OwnedPtr<AST::Type> typeArg);

    /**
     * removeFromRegistry(registryId)
     */
    OwnedPtr<AST::CallExpr> CreateRemoveFromRegistryCall(OwnedPtr<AST::Expr> registryId);
    OwnedPtr<AST::CallExpr> CreateObjCMsgSendSuperCall(OwnedPtr<AST::Expr> receiver, OwnedPtr<AST::Expr> objCSuper,
        const std::string& selector, Ptr<AST::Ty> retTy, std::vector<OwnedPtr<AST::Expr>> rawArgs);
    OwnedPtr<AST::CallExpr> CreateObjCMsgSendSuperCall(OwnedPtr<AST::Expr> objCSuper, OwnedPtr<AST::Expr> sel,
        Ptr<AST::FuncTy> ty, OwnedPtr<AST::FuncType> funcType, std::vector<OwnedPtr<AST::Expr>> funcArgs);

    InteropLibBridge& bridge;
    TypeManager& typeManager;
    NameGenerator& nameGenerator;
    TypeMapper& typeMapper;
    ImportManager& importManager;
};

} // namespace Cangjie::Interop::ObjC

#endif // CANGJIE_SEMA_OBJ_C_UTILS_AST_FACTORY_H
