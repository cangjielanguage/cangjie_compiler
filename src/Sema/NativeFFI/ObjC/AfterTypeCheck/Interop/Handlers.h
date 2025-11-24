// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares core handlers for implementation of Cangjie <-> Objective-C interopability.
 */

#ifndef CANGJIE_SEMA_DESUGAR_OBJ_C_INTEROP_INTEROP_HANDLERS
#define CANGJIE_SEMA_DESUGAR_OBJ_C_INTEROP_INTEROP_HANDLERS

#include "Context.h"
#include "NativeFFI/ObjC/Utils/Handler.h"

namespace Cangjie::Interop::ObjC {

/**
 * Finds all @ObjCMirror and @ObjCImpl declarations.
 */
class FindMirrors : public Handler<FindMirrors, InteropContext> {
public:
    void HandleImpl(InteropContext& ctx);
};

/**
 * Performs all necessary syntax and semantic checks on @ObjCMirror declarations.
 */
class CheckMirrorTypes : public Handler<CheckMirrorTypes, InteropContext> {
public:
    void HandleImpl(InteropContext& ctx);
};

/**
 * Performs all necessary syntax and semantic checks on @ObjCImpl declarations.
 */
class CheckImplTypes : public Handler<CheckImplTypes, InteropContext> {
public:
    void HandleImpl(InteropContext& ctx);
};

/**
 * Creates and inserts a field of type `NativeObjCId` in all hierarchy root @ObjCMirror declarations.
 *
 * `public var $obj: NativeObjCId`
 */
class InsertNativeHandleField : public Handler<InsertNativeHandleField, InteropContext> {
public:
    explicit InsertNativeHandleField(InteropType interopType) : interopType(interopType)
    {
    }
    void HandleImpl(InteropContext& ctx);
private:
    InteropType interopType{InteropType::NA};
};

/**
 * Creates and inserts an accessor instance method for `$obj: NativeObjCId` field for each @ObjCMirror interface
 * declaration(according to inheritance).
 *
 * `public func $getObj(): NativeObjCId`
 */
class InsertNativeHandleGetterDecl : public Handler<InsertNativeHandleGetterDecl, InteropContext> {
public:
    explicit InsertNativeHandleGetterDecl(InteropType interopType) : interopType(interopType)
    {
    }
    void HandleImpl(InteropContext& ctx);
private:
    InteropType interopType{InteropType::NA};
};

/**
 * Creates and inserts a body of `$getObj(): NativeObjCId` for each @ObjCMirror/@ObjCImpl(according to inheritance)
 * class:
 *
 * ```cangjie
 * public func $getObj(): NativeObjCId { // decl was inserted in `InsertNativeHandleGetterDecl`
 *     $obj
 * }
 * ```
 */
class InsertNativeHandleGetterBody : public Handler<InsertNativeHandleGetterBody, InteropContext> {
public:
    explicit InsertNativeHandleGetterBody(InteropType interopType) : interopType(interopType)
    {
    }
    void HandleImpl(InteropContext& ctx);
private:
    InteropType interopType{InteropType::NA};
};

/**
 * Creates and inserts constructors skeletons for each @ObjCMirror class:
 *
 * `public init($obj: NativeObjCId)`
 */
class InsertBaseCtorDecl : public Handler<InsertBaseCtorDecl, InteropContext> {
public:
    explicit InsertBaseCtorDecl(InteropType interopType) : interopType(interopType)
    {
    }
    void HandleImpl(InteropContext& ctx);
private:
    InteropType interopType{InteropType::NA};
};

/**
 * Creates and inserts constructors bodies for each @ObjCMirror class:
 *
 * ```cangjie
 * public init($obj: NativeObjCId) { // decl was inserted in `InsertMirrorCtorDecl`
 *     this.$obj = $obj
 * }
 * ```
 * or
 * ```cangjie
 * public init($obj: NativeObjCId) {
 *     super($obj)
 * }
 * ```
 */
class InsertBaseCtorBody : public Handler<InsertBaseCtorBody, InteropContext> {
public:
    explicit InsertBaseCtorBody(InteropType interopType) : interopType(interopType)
    {
    }
    void HandleImpl(InteropContext& ctx);
private:
    InteropType interopType{InteropType::NA};
};

/**
 * Creates and inserts finalizers (with special private field $hasInited) for each @ObjCMirror class:
 * ```cangjie
 * private var $hasInited: Bool = false
 * ~init() {
 *     unsafe { objCRelease($obj) }
 * }
 * ```
 */
class InsertFinalizer : public Handler<InsertFinalizer, InteropContext> {
public:
    explicit InsertFinalizer(InteropType interopType) : interopType(interopType)
    {
    }
    void HandleImpl(InteropContext& ctx);
private:
    InteropType interopType{InteropType::NA};
};

/**
 * Generates top-level `CJImpl_ObjC_{ForeignName}_deleteCJObject($registryId: RegistryId): Unit`
 * method for each @ObjCImpl declaration.
 */
class GenerateDeleteCJObjectMethod : public Handler<GenerateDeleteCJObjectMethod, InteropContext> {
public:
    explicit GenerateDeleteCJObjectMethod(InteropType interopType) : interopType(interopType)
    {
    }
    void HandleImpl(InteropContext& ctx);

private:
    InteropType interopType{InteropType::NA};
};

/**
 * Desugars all members of every @ObjCMirror declarations.
 * Methods are desugared as follows:
 * 1. If method returns @ObjCMirror/@ObjCImpl value:
 * 1.1 Generates proper objCMsgSend call, e.g. ```unsafe { CFunc<(NativeObjCId, NativeObjCSel, ...ArgTypes) ->
 * NativeObjCId>(objCMsgSend())($obj, registerName("${methodForeignName}"), ...args) }```.
 * 1.2 Wraps it with withAutoreleasePoolObj call, e.g. ```withAutoreleasePoolObj{ => // msgSend call })```.
 * 1.3 Wraps it with ctor call of return value type, e.g. ```M(// withAutoreleasePool call)```.
 *
 * 2. Else if method returns Objective-C compatible primitive type value:
 * 2.1 Generates proper objCMsgSend call, e.g. ```unsafe { CFunc<(NativeObjCId, NativeObjCSel, ...ArgTypes) ->
 * RetType>(objCMsgSend())($obj, registerName("${methodForeignName}"), ...args) }```.
 * 2.2 Wraps it with withAutoreleasePool<RetType> call, e.g. ```withAutoreleasePool<RetType>{ => // msgSend call })```.
 *
 * 3. Body is inserted into the ctor declaration generated on previous steps.
 *
 * Prop getters are desugared as follows:
 * 1. If prop has @ObjCMirror/@ObjCImpl value:
 * 1.1 Generates proper ObjCRuntime.msgSend call, e.g. ```unsafe { CFunc<(NativeObjCId, NativeObjCSel) ->
 * NativeObjCId>(objCMsgSend())($obj, registerName("foo")) }```. 1.2 Wraps it with
 * withAutoreleasePoolObj call, e.g. ```withAutoreleasePoolObj{ => // msgSend call })```.
 * 1.3 Wraps it with getFromRegistry call, e.g. ```getFromRegistry(...//
 * ObjCRuntime.withAutoreleasePool call)```.
 *
 * 2. Else if prop has Objective-C compatible primitive type value:
 * 2.1 Generates proper objCMsgSend() call, e.g. ```unsafe { CFunc<(NativeObjCId, NativeObjCSel) ->
 * RetType>(objCMsgSend())($obj, registerName("foo"), ...args) }```
 *
 * 3. Accessor body is inserted in the corresponding declaration generated on previous steps.
 *
 * Prop setters are desugared as follows:
 * Generates proper objCMsgSend call, e.g. ```unsafe { CFunc<(NativeObjCId, NativeObjCSel, NativeObjCId) ->
 * Unit>(objCMsgSend())($obj, registerName("setFoo:"), value.$getObj) }```.
 *
 * Fields are desugared as follows:
 * 1. On parse stage, all fields are replaced with corresponding prop declaration.
 * 2. Prop getter is essentially getInstanceVariable call (getInstanceVariableObj for object types wrapped
 * with getFromRegistryByHandle call).
 * 3. Prop setter (@ObjCMirror field is always var) is essentially setInstanceVariable
 * (setInstanceVariableObj for object type) call.
 *
 * @ObjCInit static method initializers are desugared the same way as init-constructors.
 */
class DesugarMirrors : public Handler<DesugarMirrors, InteropContext> {
public:
    explicit DesugarMirrors(InteropType interopType) : interopType(interopType)
    {
    }
    void HandleImpl(InteropContext& ctx);

private:
    InteropType interopType{InteropType::NA};
    void DesugarTopLevelFunc(InteropContext& ctx, AST::FuncDecl& func);
    void DesugarMethod(InteropContext& ctx, AST::ClassLikeDecl& mirror, AST::FuncDecl& method);
    void DesugarCtor(InteropContext& ctx, AST::ClassLikeDecl& mirror, AST::FuncDecl& ctor);
    /**
     * @ObjCInit static method initializer
     */
    void DesugarStaticMethodInitializer(InteropContext& ctx, AST::FuncDecl& initializer);
    void DesugarProp(InteropContext& ctx, AST::ClassLikeDecl& mirror, AST::PropDecl& prop);
    void DesugarField(InteropContext& ctx, AST::ClassLikeDecl& mirror, AST::PropDecl& field);
};

class DesugarSyntheticWrappers : public Handler<DesugarSyntheticWrappers, InteropContext> {
public:
    void HandleImpl(InteropContext& ctx);
};

/**
 * Does generation inside impl body. Inserts:
 * - A constructor (with extra handle parameter) for each user-defined constructor. It is callable from Objective-C
 */
class GenerateObjCImplMembers : public Handler<GenerateObjCImplMembers, InteropContext> {
public:
    void HandleImpl(InteropContext& ctx);

private:
    void GenerateCtor(InteropContext& ctx, AST::ClassDecl& target, AST::FuncDecl& from);
};

/**
 * Generates top-level `CJImpl_ObjC_{ForeignName}($obj: NativeObjCId, ...): RegistryId` methods
 * for each @ObjCImpl ctor declaration (with first $obj param, as they have proper types for interop).
 */
class GenerateInitCJObjectMethods : public Handler<GenerateInitCJObjectMethods, InteropContext> {
public:
    explicit GenerateInitCJObjectMethods(InteropType interopType) : interopType(interopType)
    {
    }
    void HandleImpl(InteropContext& ctx);

private:
    InteropType interopType{InteropType::NA};
    void GenNativeInitMethodForEnumCtor(InteropContext& ctx, AST::EnumDecl& enumDecl);
};

/**
 * Desugars all members of every @ObjCImpl declaration.
 * Common steps are:
 * 1. All method/prop accessors bodies wrapped with `ObjC_ISL_withMethodEnv_ret<T>`/`ObjC_ISL_withMethodEnv_retObj`
 * method call.
 * 2. All expressions with Objective-C mirror/mirror subtype declarations involved are desugared to
 * `objc_msgSend`/`objc_msgSendSuper` calls.
 * 3. Desugared bodies are placed in the corresponding mirrors ($objC prefixed) members.
 * 4. Original members bodies are replaced with throwing `ObjC_ISL_UnreachableCodeException`.
 */
class DesugarImpls : public Handler<DesugarImpls, InteropContext> {
public:
    void HandleImpl(InteropContext& ctx);

private:
    void Desugar(InteropContext& ctx, AST::ClassDecl& impl, AST::FuncDecl& method);
    // void DesugarCtor(InteropContext& ctx, AST::ClassDecl& impl, AST::FuncDecl& ctor);
    void Desugar(InteropContext& ctx, AST::ClassDecl& impl, AST::PropDecl& prop);
    void DesugarCallExpr(InteropContext& ctx, AST::ClassDecl& impl, AST::CallExpr& ce);
    void DesugarGetForPropDecl(InteropContext& ctx, AST::ClassDecl& impl, AST::MemberAccess& ma);
};

/**
 * Generates top-level function wrappers for:
 * 1. Methods (static included)
 * 2. Props (getter and *setter static included)
 * 3. Fields (getter and *setter static included)
 *
 * for each @ObjCImpl declaration.
 *
 * It has to be done because we need a way to call them from Objective-C runtime.
 *
 * NOTE: setters are generated if and only if the prop/field is mutable.
 */
class GenerateWrappers : public Handler<GenerateWrappers, InteropContext> {
public:
    explicit GenerateWrappers(InteropType interopType) : interopType(interopType)
    {
    }
    void HandleImpl(InteropContext& ctx);

private:
    void GenerateWrapper(InteropContext& ctx, AST::FuncDecl& method);
    // Generic methods for prop and field with SFINAE?
    void GenerateWrapper(InteropContext& ctx, AST::PropDecl& prop);
    void GenerateSetterWrapper(InteropContext& ctx, AST::PropDecl& prop);
    void GenerateWrapper(InteropContext& ctx, AST::VarDecl& field);
    void GenerateSetterWrapper(InteropContext& ctx, AST::VarDecl& field);
    bool SkipSetterForValueTypeDecl(AST::Decl& decl) const;
    InteropType interopType{InteropType::NA};
};

/**
 * Generates Objective-C glue code for all @ObjCImpl declarations.
 * For each valid @ObjCImpl would be generated the following:
 * 1. Header file ({ForeignName}.h)
 * 2. Implementation file ({ForeignName}.cpp)
 */
class GenerateGlueCode : public Handler<GenerateGlueCode, InteropContext> {
public:
    explicit GenerateGlueCode(InteropType interopType) : interopType(interopType)
    {
    }
    void HandleImpl(InteropContext& ctx);

private:
    InteropType interopType{InteropType::NA};
};

/**
 * Report errors on usage of ObjCPointer with ObjC-incompatible types
 */
class CheckObjCPointerTypeArguments : public Handler<CheckObjCPointerTypeArguments, InteropContext> {
public:
    void HandleImpl(InteropContext& ctx);
};

/**
 * Report errors on usage of ObjCFunc with ObjC-incompatible types
 */
class CheckObjCFuncTypeArguments : public Handler<CheckObjCFuncTypeArguments, InteropContext> {
public:
    void HandleImpl(InteropContext& ctx);
};

/**
 * Rewrite pointer access performed by ObjCPointer methods to proper FFI calls
 */
class RewriteObjCPointerAccess : public Handler<RewriteObjCPointerAccess, InteropContext> {
public:
    void HandleImpl(InteropContext& ctx);
};

/**
 * Rewrite pointer access performed by ObjCPointer methods to proper FFI calls
 */
class RewriteObjCFuncCall : public Handler<RewriteObjCFuncCall, InteropContext> {
public:
    void HandleImpl(InteropContext& ctx);
};

/**
 * Drains all declarations generated on the previous step to their corresponding files which finishes the desugaring.
 */
class DrainGeneratedDecls : public Handler<DrainGeneratedDecls, InteropContext> {
public:
    void HandleImpl(InteropContext& ctx);
};

/**
 * Finds all Cangjie declarations which are mapped to Objective-C side.
 */
class FindCJMapping : public Handler<FindCJMapping, InteropContext> {
public:
    void HandleImpl(InteropContext& ctx);
};

/**
 * Finds all Cangjie interface declarations which are mapped to Objective-C side.
 */
class FindCJMappingInterface : public Handler<FindCJMappingInterface, InteropContext> {
public:
    void HandleImpl(InteropContext& ctx);
};

/**
 * Finds all forward classes previously generated.
 */
class FindFwdClass : public Handler<FindFwdClass, InteropContext> {
public:
    void HandleImpl(InteropContext& ctx);
};

/**
 * Performs all necessary syntax and semantic checks on CJMapping declarations.
 */
class CheckCJMappingTypes : public Handler<CheckCJMappingTypes, InteropContext> {
public:
    void HandleImpl(InteropContext& ctx);
};

/**
 * Generate and insert a forward class for each CJ-Mapping interface
 */
class InsertFwdClasses : public Handler<InsertFwdClasses, InteropContext> {
public:
    void HandleImpl(InteropContext& ctx);
};


} // namespace Cangjie::Interop::ObjC

#endif // CANGJIE_SEMA_DESUGAR_OBJ_C_INTEROP_INTEROP_HANDLERS
