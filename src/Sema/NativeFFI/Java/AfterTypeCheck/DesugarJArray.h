// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares after-typecheck Java interop stage:
 * - desugaring JArray constructor, get, set and call-site call transformations.
 */
#ifndef CANGJIE_SEMA_AFTER_TYPECHECK_NATIVE_FFI_JAVA_DESUGAR_JARRAY
#define CANGJIE_SEMA_AFTER_TYPECHECK_NATIVE_FFI_JAVA_DESUGAR_JARRAY

#include "AfterTypeCheckStage.h"
#include "cangjie/AST/Node.h"
#include "cangjie/Modules/ImportManager.h"
#include "cangjie/Sema/TypeManager.h"
#include "NativeFFI/Java/AfterTypeCheck/InteropLibBridge.h"
#include "NativeFFI/Java/AfterTypeCheck/ASTFactory.h"

namespace Cangjie::Native::FFI::Java {
using namespace Interop::Java;

/**
 * Rewrites JArray get/set operations at callsites.
 */
class DesugarJArray : public AfterTypeCheckStage {
public:
    explicit DesugarJArray(
        TypeManager& typeManager,
        const ImportManager& importManager,
        InteropLibBridge& ilib, ASTFactory& factory);
protected:
    void Process(AfterTypeCheckContext& ctx) override;
private:
    void ReplaceCallsWithArrayJavaEntityGet(AfterTypeCheckContext& ctx, AST::File& file) const;
    void ReplaceCallsWithArrayJavaEntitySet(AfterTypeCheckContext& ctx, AST::File& file) const;

    /**
     * Inserts a new constructor with a jniType parameter.
     * * [ GENERATED CODE ]
     * -------------------------------------------------------------------------
     * init(length: Int32, $jniType: String) {
     *      this({
     *       =>
     *           let $tmp1: CPointer<CPointer<JNINativeInterface_>> = Java_CFFI_get_env()
     *          return unsafe {
     *              Java_CFFI_newJavaArray($tmp1, $jniType, length)
     *          }
     *      }())
     * }
     * -------------------------------------------------------------------------
     */
    void GenerateJniTypeConstructor(AfterTypeCheckContext& ctx, AST::ClassDecl& jarray) const;

    /**
     * Inserts throw Exception("unexpected call") into body of given size constructor.
     * If provided constructor is not exact init(length: Int32), no transformations are performed.
     *
     * ------------------------------------------------------------
     * init(length: Int32) {
     * // throw Exception("unexpected call") will be inserted here
     * }
     * ------------------------------------------------------------
     */
    void InsertOriginalSizeConstructorBody(AST::FuncDecl& constr) const;

    /**
    * Transforms all java.lang.JArray's constructor calls:
    * init(length: Int32) -> init(length: Int32, $jniType: String)
    *
    * example:
    * let u = JArray<User>(1) -> let u = JArray<User>(1, "Lutils/User;")
    *
    * Note: original constructor init(length: Int32) must throw Exception
    */
    void TransformConstructorCallsToPassJNIParam(AST::File& file) const;

    void InsertJniTypeParamIntoConstructor(AST::FuncDecl& constr) const;
    void InsertConstructorBody(AfterTypeCheckContext& ctx, AST::FuncDecl& constr) const;
    Ptr<AST::FuncDecl> FindSizeJNITypeConstructor(AST::ClassDecl& jarray) const;
    Ptr<AST::FuncDecl> FindSizeJNITypeConstructorFromInnerDecl(const AST::Decl& decl) const;

    TypeManager& typeManager;
    const ImportManager& importManager;
    InteropLibBridge& ilib;
    ASTFactory& factory;
};

} // namespace Cangjie::Native::FFI::Java

#endif // CANGJIE_SEMA_AFTER_TYPECHECK_NATIVE_FFI_JAVA_DESUGAR_JARRAY
