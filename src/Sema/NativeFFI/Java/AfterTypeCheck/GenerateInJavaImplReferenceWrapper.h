// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares after-typecheck Java interop stage: generating members within @JavaImpl reference wrappers.
 */
#ifndef CANGJIE_SEMA_AFTER_TYPECHECK_NATIVE_FFI_JAVA_GENERATE_IN_JAVA_IMPL_REFERENCE_WRAPPER
#define CANGJIE_SEMA_AFTER_TYPECHECK_NATIVE_FFI_JAVA_GENERATE_IN_JAVA_IMPL_REFERENCE_WRAPPER

#include "AfterTypeCheckStage.h"
#include "InteropLibBridge.h"
#include "NativeFFI/Java/AfterTypeCheck/ASTFactory.h"
#include "NativeFFI/Java/AfterTypeCheck/AfterTypeCheckContext.h"
#include "cangjie/AST/Node.h"
#include "cangjie/Modules/ImportManager.h"
#include "cangjie/Sema/TypeManager.h"

namespace Cangjie::Native::FFI::Java {
using namespace Interop::Java;

/**
 * Generates members within @JavaImpl reference wrappers.
 */
class GenerateInJavaImplReferenceWrapper : public AfterTypeCheckStage {
public:
    explicit GenerateInJavaImplReferenceWrapper(TypeManager& typeManager, InteropLibBridge& ilib, ASTFactory& factory);
protected:
    void Process(AfterTypeCheckContext& ctx) override;
private:
    void Process(AST::ClassDecl& refWrapper, AfterTypeCheckContext& ctx) const;

    /**
     * See `GenerateJavaSideConstructor`.
     */
    AST::FuncDecl& InsertJavaSideConstructor(AST::FuncDecl& userCtor,
        AST::VarDecl& companionRefField, AST::ClassDecl& companion, AfterTypeCheckContext& ctx) const;

    /**
     * Generates constructor for calls from java.
     * It mostly looks similar with user-defined constructors of @JavaImpl-s with the following difference:
     * - java-side constructor accepts extra 2 parameters:
     *     1. reference to java object ($obj: Java_CFFI_JavaEntity);
     *     2. registry companion reference ($reg: <companion.ty>);
     * - instead of creating java side object in super-constructor initialization call, it passes reference
     *   to existing java object (`super($obj)`).
     * Example:
     * ```cangjie
     * init($obj: Java_CFFI_JavaEntity, reg: <companion.ty>, arg: Bool, <user-defined constructor parameters>) {
     *        super($obj) // creates strong global reference
     *        $reg = reg
     *        // user-defined constructor logics
	 * }
     * ```
     */
    OwnedPtr<AST::FuncDecl> GenerateJavaSideConstructor(AST::FuncDecl& userCtor,
        AST::VarDecl& companionRefField, AST::ClassDecl& companion) const;

    /**
     * Generates wrapping constructor.
     * Wrapping constructor does not have user initialization logics.
     * Instead, it wraps existing reference into reference wrapper.
     * Example:
     * ```cangjie
     * init($obj: Java_CFFI_JavaEntity, $regId: RegistryId) {
		super($obj)
		$reg = getFromRegistryById<Foo$reg>($regId)
		// no user-defined constructor logics
	}
     * ```
     */
    void GenerateWrappingConstructorBody(AST::FuncDecl& wrappingCtor,
        AST::VarDecl& companionRefField, AST::ClassDecl& companion) const;

    void RewriteUserDefinedConstructorsInitialization(AfterTypeCheckContext& ctx,
        AST::ClassDecl& refWrapper, AST::VarDecl& companionRefField, AST::ClassDecl& companion) const;

    /**
     * Desugars (callable from cangjie) constructor initialization logics.
     * Desugaring actions:
     * 1. Insert `super` constructor call with java object creation instead.
     * 2. Insert registry companion reference initialization.
     * before [ctor]:
     *   init(a1: A, a2: B, ..., an: N) {
     *       [super(...)] // optional
     *       ...body...
     *   }
     * --------------------------------
     * after:
     *   init(a1: A, a2: B, ..., an: N) {
     *       super({
     *           let jniEnv = Java_CFFI_get_env()
     *           Java_CFFI_newJavaObject(jniEnv, typeSignature, "(<argsSignature>)V", [
     *               Java_CFFI_JavaEntity(a1),
     *               Java_CFFI_JavaEntity(a2),
     *               ...,
     *               Java_CFFI_JavaEntity(an),
     *               Java_CFFI_JavaEntity(null) // mark argument for generated java constructor
     *                                          // of Type $$NativeConstructorMarker
     *               ])
     *       }) // @JavaMirror constructor call
     *       $reg = <companion>(this.$obj)
     *       [super(...)] // optional <- removed after java code generation stage.
     *       ...body...
     *   }
     */
    void RewriteUserDefinedConstructorInitialization(AfterTypeCheckContext& ctx,
        AST::FuncDecl& ctor, AST::VarDecl& companionRefField, AST::ClassDecl& companion) const;

    /**
     *
     * ~init() {
     *     Java_CFFI_deleteGlobalReference($jnienv, this.javaref)
     * }
     */
    void InsertFinalizer(AST::ClassDecl& refWrapper) const;

    TypeManager& typeManager;
    InteropLibBridge& ilib;
    ASTFactory& factory;
};

/**
 * Generates API stubs for Java Impls.
 * Note: Corresponding API for passing impl objects from java to cangjie is available after this stage (wrap/unwrap).
 */
class GenerateJavaImplRegistryCompanionReferenceField : public AfterTypeCheckStage {
public:
    explicit GenerateJavaImplRegistryCompanionReferenceField()
    {
    }
protected:
    void Process(AfterTypeCheckContext& ctx) override;
private:
    /**
     * Inserts a field generated in `CreateJavaImplRegistryCompanionReferenceField`.
     * @see CreateJavaImplRegistryCompanionReferenceField
     */
    VarDecl& InsertJavaImplRegistryCompanionReferenceField(ClassDecl& companion, ClassDecl& refWrapper) const;

    /**
     * Creates a field with corresponding registry companion type.
     */
    OwnedPtr<VarDecl> CreateJavaImplRegistryCompanionReferenceField(ClassDecl& companion) const;
};

} // namespace Cangjie::Native::FFI::Java

#endif // CANGJIE_SEMA_AFTER_TYPECHECK_NATIVE_FFI_JAVA_GENERATE_IN_JAVA_IMPL_REFERENCE_WRAPPER
