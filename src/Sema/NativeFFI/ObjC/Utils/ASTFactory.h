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

#ifndef CANGJIE_SEMA_OBJ_C_UTILS_DEPRECATED_AST_FACTORY_H
#define CANGJIE_SEMA_OBJ_C_UTILS_DEPRECATED_AST_FACTORY_H

#include "InteropLibBridge.h"
#include "NameGenerator.h"
#include "TypeMapper.h"
#include "DeclarationCache.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Types.h"
#include "cangjie/Utils/SafePointer.h"
#include "NativeFFI/Utils.h"

namespace Cangjie::Interop::ObjC {

/**
 * Indicates whether the object needs to be retained
 */
enum class Retain {
    UNRETAINED,
    RETAINED,
    // The difference with RETAINED is that this works faster but has a more limited scope of application.
    // It works only on Objective-C object values affected by objc_autoreleaseReturnValue.
    RETAIN_AUTORELEASED_RETURN_VALUE
};

/** @deprecated use Create + Transform + Insert instead */
class ASTFactory {
public:
    ASTFactory(InteropLibBridge& bridge, TypeManager& typeManager, NameGenerator& nameGenerator,
        TypeMapper& typeMapper, ImportManager& importManager, DeclarationCache& declarationCache)
        : bridge(bridge),
          typeManager(typeManager),
          nameGenerator(nameGenerator),
          typeMapper(typeMapper),
          importManager(importManager),
          declarationCache(declarationCache)
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
     * - for CPointer<Unit> (mirror interface M): constructor call `M$wrap(expr)`
     * - for CPointer<Unit> (impl I): constructor call `I(expr, getRegistryId(expr))`
     *
     * If `withRetain == Retain::RETAINED`, then `objCRetain(expr)` is called additionally.
     * If `withRetain == Retain::RETAIN_AUTORELEASED_RETURN_VALUE`, then `objCRetainAutoreleasedReturnValue(expr)` is
     * called additionally.
     */
    OwnedPtr<AST::Expr> WrapEntity(OwnedPtr<AST::Expr> expr, AST::Ty& wrapTy, Retain withRetain = Retain::UNRETAINED);

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
    OwnedPtr<AST::FuncDecl> CreateGetObjCClassDecl(AST::ClassLikeDecl& target);
    OwnedPtr<AST::FuncDecl> CreateGetObjCClass(AST::ClassLikeDecl& target);
    OwnedPtr<AST::FuncDecl> CreateInitCjObjectReturningObjCSelf(
        const AST::ClassDecl& impl, const AST::ClassDecl& regComp, AST::FuncDecl& ctor);
    OwnedPtr<AST::FuncDecl> CreateDeleteCjObject(AST::Decl& target);
    /**
     * Returns generated top-level @C function (callable from obj-c) that calls @ObjCImpl `originMethod`.
     */
    OwnedPtr<AST::FuncDecl> CreateMethodWrapper(AST::FuncDecl& method, AST::ClassDecl& impl);
    OwnedPtr<AST::FuncDecl> CreateGetterWrapper(AST::PropDecl& prop, AST::ClassDecl& impl);
    OwnedPtr<AST::FuncDecl> CreateSetterWrapper(AST::PropDecl& prop, AST::ClassDecl& impl);
    OwnedPtr<AST::FuncDecl> CreateGetterWrapper(AST::VarDecl& field, AST::ClassDecl& impl);
    OwnedPtr<AST::FuncDecl> CreateSetterWrapper(AST::VarDecl& field, AST::ClassDecl& impl);
    OwnedPtr<AST::ThrowExpr> CreateObjCInitException(AST::File& file, AST::ClassLikeDecl& cls);
    OwnedPtr<AST::ThrowExpr> CreateThrowUnreachableCodeExpr(AST::File& file);
    OwnedPtr<AST::ThrowExpr> CreateThrowOptionalMethodUnimplemented(AST::File& file);
    OwnedPtr<AST::ThrowExpr> CreateThrowStaticMethodCallOnInterfaceExpr(AST::File& file);
    std::set<Ptr<AST::FuncDecl>> GetAllParentCtors(AST::ClassDecl& target) const;
    /**
     * Creates `init($obj: NativeObjCId)` for a class generated whole - a registry companion or a mirror
     * interface handle wrapper. Such a class holds no user-written member, so no marker is needed to keep the
     * signature apart from a user one, @see InteropLibBridge::AppendNativeObjCIdMarkerIfNeeded.
     */
    OwnedPtr<AST::FuncDecl> CreateBaseCtorDecl(AST::ClassDecl& target);

    /**
     * Creates `init($obj: NativeObjCId, $mrk: NativeObjCIdMarker)` for a @ObjCMirror. The marker is what keeps
     * the ctor from clashing with a user-written `init(p: CPointer<Unit>)`, ObjC-compatible since `CPointer<T>`
     * became a mirror-usable type.
     */
    OwnedPtr<AST::FuncDecl> CreateObjCMirrorBaseCtorDecl(AST::ClassDecl& target);

    /**
     * Creates a reference to `__NATIVE_OBJC_ID_MARKER`, the sole value of the marker type.
     */
    OwnedPtr<AST::RefExpr> CreateNativeObjCIdMarkerRef(Ptr<AST::File> curFile);

    /**
     * Appends the marker argument to @p call if the ctor it resolves to takes one.
     *
     * The marker makes the signature of a generated ctor unwritable by a user, which is what tells it apart
     * from a user ctor of otherwise equal signature - `NativeObjCId` is an alias of `CPointer<Unit>` and so
     * `RegistryId` of `Int64`, both of them plain ObjC-compatible types a user may take. Whether a callee
     * carries the marker is therefore decided by the type of its params and never by their names, which take
     * no part in overload resolution; BACKCOMPAT, a mirror compiled before `CPointer<T>` became
     * ObjC-compatible has a marker-less base ctor and such a call is left alone.
     */
    void AppendNativeObjCIdMarkerIfNeeded(AST::CallExpr& call, Ptr<AST::File> curFile);
    OwnedPtr<AST::CallExpr> CreateObjCMsgSendCall(OwnedPtr<AST::Expr> nativeHandle, const std::string& selector,
        AST::DataTy retTy, std::vector<OwnedPtr<AST::Expr>> args);
    OwnedPtr<AST::CallExpr> CreateObjCMsgSendCall(
        Ptr<AST::FuncTy> ty, OwnedPtr<AST::FuncType> funcType, std::vector<OwnedPtr<AST::Expr>> funcArgs);
    OwnedPtr<AST::CallExpr> CreateGetInstanceVariableCall(const AST::PropDecl& field, OwnedPtr<AST::Expr> nativeHandle);
    OwnedPtr<AST::CallExpr> CreateSetInstanceVariableCall(
        const AST::PropDecl& field, OwnedPtr<AST::Expr> nativeHandle, OwnedPtr<AST::Expr> value);
    OwnedPtr<AST::Expr> CreateGetProtoCall(std::string& protoName, Ptr<AST::File> curFile);
    OwnedPtr<AST::Expr> CreateObjCRespondsToSelectorCall(
        OwnedPtr<AST::Expr> id, OwnedPtr<AST::Expr> sel, Ptr<AST::File> file);
    OwnedPtr<AST::Expr> CreateGetSuperClassExpr(OwnedPtr<AST::Expr> objCSuper, Ptr<AST::File> file);

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
        OwnedPtr<AST::Expr> ptr, AST::DataTy retTy, std::vector<OwnedPtr<AST::Expr>> args);

    OwnedPtr<AST::Expr> CreateAutoreleasePoolScope(AST::DataTy ty, std::vector<OwnedPtr<AST::Node>> actions);
    OwnedPtr<AST::FuncDecl> CreateFinalizer(AST::ClassDecl& target);

    OwnedPtr<AST::Expr> CreateUnsafePointerCast(OwnedPtr<AST::Expr> expr, AST::DataTy elementType);

    void SetDesugarExpr(Ptr<AST::Expr> original, OwnedPtr<AST::Expr> desugared);
    OwnedPtr<AST::Expr> WrapObjCMirrorOption(const Ptr<AST::Expr> entity, Ptr<AST::ClassLikeDecl> mirror,
        const Ptr<AST::File> curFile, Retain withRetain = Retain::UNRETAINED);
    OwnedPtr<AST::Expr> CreateObjCobjectNull();
    AST::DataTy GetObjCTy();
    OwnedPtr<AST::Expr> CreateGetObjcEntityOrNullCall(AST::VarDecl& entity, Ptr<AST::File> file);
    OwnedPtr<AST::Expr> CreateOptionCast(Ptr<AST::VarDecl> jObjectVar, AST::ClassLikeDecl castDecl);
    OwnedPtr<AST::Expr> UnwrapObjCMirrorOption(OwnedPtr<AST::Expr> entity, AST::DataTy ty);
    OwnedPtr<AST::Expr> CreateOptionMatch(OwnedPtr<AST::Expr> selector,
        std::function<OwnedPtr<AST::Expr>(AST::VarDecl&)> someBranch, std::function<OwnedPtr<AST::Expr>()> noneBranch,
        AST::ModalTy ty);

    OwnedPtr<AST::FuncDecl> CreateNativeHandleGetterDecl(AST::ClassLikeDecl& target);
    OwnedPtr<AST::Expr> CreateNativeHandleFieldExpr(AST::ClassDecl& target);
    /**
     * objCRelease($obj)
     */
    OwnedPtr<AST::Expr> CreateObjCReleaseCall(OwnedPtr<AST::Expr> nativeHandle);
    OwnedPtr<AST::Expr> CreateObjCIsKindOfClassCall(OwnedPtr<AST::Expr> id, OwnedPtr<AST::Expr> cls,
        Ptr<AST::File> file);
    OwnedPtr<AST::Expr> CreateObjCConformsToProtocolCall(OwnedPtr<AST::Expr> id, OwnedPtr<AST::Expr> cls,
        Ptr<AST::File> file);
    OwnedPtr<AST::Expr> CreateWithObjCSuperScope(OwnedPtr<AST::Expr> nativeHandle, AST::ClassDecl& outerDecl,
        AST::ModalTy retTy,
        std::function<std::vector<OwnedPtr<AST::Node>>(OwnedPtr<AST::Expr>, OwnedPtr<AST::Expr>)> bodyFactory);

    OwnedPtr<AST::Expr> CreateMethodCallViaMsgSendSuper(AST::FuncDecl& fd, OwnedPtr<AST::Expr> receiver,
        OwnedPtr<AST::Expr> objCSuper, std::vector<OwnedPtr<AST::Expr>> rawArgs);
    OwnedPtr<AST::Expr> CreatePropGetterCallViaMsgSendSuper(
        AST::PropDecl& pd, OwnedPtr<AST::Expr> receiver, OwnedPtr<AST::Expr> objCSuper);
    OwnedPtr<AST::Expr> CreatePropSetterCallViaMsgSendSuper(
        AST::PropDecl& pd, OwnedPtr<AST::Expr> receiver, OwnedPtr<AST::Expr> objCSuper, OwnedPtr<AST::Expr> value);
    /**
     * putToRegistry(expr)
     */
    OwnedPtr<AST::CallExpr> CreatePutToRegistryCall(OwnedPtr<AST::Expr> expr);
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
    OwnedPtr<AST::Expr> CreateOptionalMethodGuard(OwnedPtr<AST::Expr> msgSend, OwnedPtr<AST::Expr> cls,
        const std::string& selector, const Ptr<AST::File> curFile);
    static std::vector<OwnedPtr<AST::FuncParamList>> CreateParamLists(std::vector<OwnedPtr<AST::FuncParam>>&& params);
    static std::vector<OwnedPtr<AST::FuncParam>>& GetParams(const AST::FuncDecl& fn);
    static OwnedPtr<AST::VarDecl> CreateVar(
        const std::string& name, AST::ModalTy ty, bool isVar, OwnedPtr<AST::Expr> initializer = nullptr);
    static OwnedPtr<AST::FuncDecl> CreateFunc(const std::string& name, Ptr<AST::FuncTy> fnTy,
        std::vector<OwnedPtr<AST::FuncParam>>&& params, std::vector<OwnedPtr<AST::Node>>&& nodes);
    static OwnedPtr<AST::ParenExpr> CreateParenExpr(OwnedPtr<AST::Expr> expr);

    OwnedPtr<AST::Expr> CreateNativeLambdaForBlockType(AST::Ty& ty, Ptr<AST::File> curFile);
    OwnedPtr<AST::Expr> CreateObjCBlockFromLambdaCall(OwnedPtr<AST::Expr> funcExpr);
    OwnedPtr<AST::Expr> CreateObjectGetClassCall(OwnedPtr<AST::Expr> id, Ptr<AST::File> curFile);
    OwnedPtr<AST::Expr> CreateConvertToNSStringCall(OwnedPtr<AST::Expr> id, AST::ClassDecl& classDecl,
        Ptr<AST::File> curFile);
    OwnedPtr<AST::Expr> CreateDescriptionAsStringCall(OwnedPtr<AST::Expr> id);
    OwnedPtr<AST::Expr> CreateObjCRetainCall(OwnedPtr<AST::Expr> id);
    OwnedPtr<AST::Expr> CreateObjCRetainAutoreleasedReturnValueCall(OwnedPtr<AST::Expr> id);

    OwnedPtr<AST::Expr> CreateGetCachedSelectorAccess(std::string selector, Ptr<AST::File> curFile);
    OwnedPtr<AST::Expr> CreateGetCachedClassAccess(std::string selector, Ptr<AST::File> curFile);
    OwnedPtr<AST::Expr> CreateGetCachedClassAccess(AST::ClassLikeTy& ty, Ptr<AST::File> curFile);

    OwnedPtr<AST::Expr> CreateGetRegistryIdCall(OwnedPtr<AST::Expr> id);

private:
    /// The shared body of the two base ctor factories above; @p withMarker appends the `$mrk` param.
    OwnedPtr<AST::FuncDecl> CreateBaseCtorDecl(AST::ClassDecl& target, bool withMarker);

    /**
     * Applies `withRetain` to `expr`: `objCRetain(expr)`, `objCRetainAutoreleasedReturnValue(expr)`, or
     * `expr` itself for Retain::UNRETAINED.
     */
    OwnedPtr<AST::Expr> ApplyRetain(OwnedPtr<AST::Expr> expr, Retain withRetain);

    /**
     * `Impl(<retained nativeHandle>, registryId)`
     */
    OwnedPtr<AST::CallExpr> CreateObjCImplBaseCtorCall(AST::ClassDecl& impl, OwnedPtr<AST::Expr> nativeHandle,
        OwnedPtr<AST::Expr> registryId, Ptr<AST::File> curFile, Retain withRetain);

    void PutDeclToClassLikeBody(AST::Decl& decl, AST::ClassLikeDecl& target);
    void PutDeclToClassBody(AST::Decl& decl, AST::ClassDecl& target);
    void PutDeclToInterfaceBody(AST::Decl& decl, AST::InterfaceDecl& target);
    void PutDeclToFile(AST::Decl& decl, AST::File& target);

    /**
     * removeFromRegistry(registryId)
     */
    OwnedPtr<AST::CallExpr> CreateRemoveFromRegistryCall(OwnedPtr<AST::Expr> registryId);
    OwnedPtr<AST::CallExpr> CreateObjCMsgSendSuperCall(OwnedPtr<AST::Expr> receiver, OwnedPtr<AST::Expr> objCSuper,
        const std::string& selector, AST::DataTy retTy, std::vector<OwnedPtr<AST::Expr>> rawArgs);
    OwnedPtr<AST::CallExpr> CreateObjCMsgSendSuperCall(OwnedPtr<AST::Expr> objCSuper, OwnedPtr<AST::Expr> sel,
        Ptr<AST::FuncTy> ty, OwnedPtr<AST::FuncType> funcType, std::vector<OwnedPtr<AST::Expr>> funcArgs);

    /**
     * Raw, uncached Objective-C runtime accessors: `registerName("sel")` and `getClass("Cls")`.
     *
     * Generated code must never call these directly: every selector and class handle is obtained through
     * CreateGetCachedSelectorAccess / CreateGetCachedClassAccess, which resolve the entity once and memoize it
     * in a package-level variable.
     */
    OwnedPtr<AST::CallExpr> CreateRegisterNameCall(OwnedPtr<AST::Expr> selectorExpr);
    OwnedPtr<AST::CallExpr> CreateRegisterNameCall(const std::string& selector, Ptr<AST::File> curFile);
    OwnedPtr<AST::Expr> CreateGetClassCall(std::string& className, Ptr<AST::File> curFile);

    /**
     * `Cls.$getObjCClass()`, i.e. the generated accessor whose body is the cached class access.
     */
    OwnedPtr<AST::Expr> CreateGetClassCall(const AST::ClassLikeDecl& cls, Ptr<AST::File> curFile);

    /**
     * objCAlloc($classHandle), where $classHandle must be a NativeObjCClass expression.
     */
    OwnedPtr<AST::CallExpr> CreateAllocCall(OwnedPtr<AST::Expr> nativeClassExpr);
    /**
     * objCAlloc(decl), taking the class handle from the cache.
     */
    OwnedPtr<AST::CallExpr> CreateAllocCall(AST::Decl& decl, Ptr<AST::File> curFile);

    InteropLibBridge& bridge;
    TypeManager& typeManager;
    NameGenerator& nameGenerator;
    TypeMapper& typeMapper;
    ImportManager& importManager;
    DeclarationCache& declarationCache;
};

} // namespace Cangjie::Interop::ObjC

#endif // CANGJIE_SEMA_OBJ_C_UTILS_DEPRECATED_AST_FACTORY_H
