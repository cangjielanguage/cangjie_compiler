// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_SEMA_NATIVE_FFI_JAVA_INTEROPLIB_BRIDGE
#define CANGJIE_SEMA_NATIVE_FFI_JAVA_INTEROPLIB_BRIDGE

#include "NativeFFI/Java/Utils.h"
#include "Utils.h"
#include "NativeFFI/Java/JavaMemberSignature.h"

#include "cangjie/AST/Node.h"
#include "cangjie/Modules/ImportManager.h"
#include "cangjie/Sema/TypeManager.h"
#include "cangjie/AST/Match.h"
#include "cangjie/Utils/CheckUtils.h"
#include "cangjie/Utils/SafePointer.h"

namespace Cangjie::Interop::Java {
using namespace AST;

class InteropLibBridge {
public:
    InteropLibBridge(
        ImportManager& importManager, TypeManager& typeManager, DiagnosticEngine& diag, Utils& utils)
        : importManager(importManager), typeManager(typeManager), diag(diag), utils(utils) { }

    /**
     * jobject
     */
    Ptr<TypeAliasDecl> GetJobjectDecl();

    /**
     * Java_CFFI_JavaEntity
     */
    Ptr<StructDecl> GetJavaEntityDecl() const;

    /**
     * Java_CFFI_JavaEntityKind
     */
    Ptr<EnumDecl> GetJavaEntityKindDecl();

    /**
     * Java_CFFI_JavaEntityKind.JOBJECT
     */
    Decl& GetJavaEntityKindJObject();

    /**
     * Java_CFFI_newGlobalRef
     */
    Ptr<FuncDecl> GetNewGlobalRefDecl();

    /**
     * Java_CFFI_deleteGlobalRef
     */
    Ptr<FuncDecl> GetDeleteGlobalRefDecl() const;

    /**
     * Java_CFFI_swapLocalWithGlobalReference
     */
    Ptr<FuncDecl> GetSwapLocalWithGlobalRefDecl() const;

    /**
     * JNIEnv_ptr
     */
    Ptr<TypeAliasDecl> GetJniEnvPtrDecl() const;

    /**
     * Java_CFFI_get_env
     */
    Ptr<FuncDecl> GetGetJniEnvDecl();

    /**
     * Java_CFFI_JavaEntityJobject
     */
    Ptr<FuncDecl> GetCreateJavaEntityJobjectDecl();

    /**
     * Java_CFFI_JavaEntityJobjectNull
     */
    Ptr<FuncDecl> GetCreateJavaEntityNullDecl();

    /**
     * Java_CFFI_newJavaArray
     */
    Ptr<FuncDecl> GetNewJavaArrayDecl();

    /**
     * Java_CFFI_arrayGet
     */
    Ptr<FuncDecl> GetJavaArrayGetDecl();

    /**
     * Java_CFFI_arraySet
     */
    Ptr<FuncDecl> GetJavaArraySetDecl();

    /**
     * Java_CFFI_arrayGetLength
     */
    Ptr<FuncDecl> GetJavaArrayGetLengthDecl();

    Ptr<FuncDecl> GetJNIHandlePendingExceptionDecl() const;

    /**
     * Java_CFFI_removeFromRegistry
     */
    Ptr<FuncDecl> GetRemoveFromRegistryDecl();

    /**
     * Java_CFFI_put_to_registry_1
     */
    Ptr<FuncDecl> GetPutToRegistryDecl();

    /**
     * Java_CFFI_put_to_registry
     * Puts to registry the passed object.
     * Sets registry id field for corresponding java object.
     */
    Ptr<FuncDecl> GetPutSetToRegistryDecl();

    /**
     * Java_CFFI_unwrapJavaEntityAsValue
     */
    Ptr<FuncDecl> GetUnwrapJavaEntityDecl();

    /**
     * Java_CFFI_unwrapJavaMirror
     */
    Ptr<FuncDecl> GetUnwrapJavaMirrorDecl();

    /**
     * Java_CFFI_getFromRegistryByEntityOption<T>
     */
    Ptr<FuncDecl> GetGetFromRegistryByEntityOptionDecl();

    /**
     * Java_CFFI_getRegistryId
     */
    Ptr<AST::FuncDecl> GetGetRegistryIdDecl();

    /**
     * Java_CFFI_getRegistryIdOrNone
     */
    Ptr<AST::FuncDecl> GetGetRegistryIdOrNoneDecl();

    /**
     * Java_CFFI_getFromRegistryByEntity<T>
     */
    Ptr<FuncDecl> GetGetFromRegistryByEntityDecl();

    /**
     * Java_CFFI_JavaStringToCangjie
     */
    Ptr<FuncDecl> GetJavaStringToCangjie();

    /**
     * Java_CFFI_CangjieStringToJava
     */
    Ptr<FuncDecl> GetCangjieStringToJava();

    /**
     * Java_CFFI_getFromRegistry<T>
     */
    Ptr<FuncDecl> GetGetFromRegistryDecl();

    /**
     * Java_CFFI_getFromRegistryOption<T>
     */
    Ptr<FuncDecl> GetGetFromRegistryOptionDecl();

    /**
     * INTEROPLIB_VERSION
     */
    Ptr<VarDecl> GetInteropLibVersionVarDecl();

    /**
     * Java_CFFI_ensure_not_null
     */
    Ptr<FuncDecl> GetEnsureNotNullDecl();

    /**
     * Java_CFFI_getOrNull
     */
    Ptr<FuncDecl> GetGetJavaEntityOrNullDecl();

    /**
     * Java_CFFI_isInstanceOf
     */
    Ptr<FuncDecl> GetIsInstanceOf();

    /**
     * withExceptionHandling
     */
    Ptr<FuncDecl> GetWithExceptionHandlingDecl();

    Ptr<FuncDecl> GetGetClassDecl() const;
    Ptr<FuncDecl> GetGetMethodIdDecl() const;
    Ptr<FuncDecl> GetGetStaticMethodIdDecl() const;
    Ptr<FuncDecl> GetGetInstanceFieldIdDecl() const;
    Ptr<FuncDecl> GetGetStaticFieldIdDecl() const;

    Ptr<Ty> GetJValueTy();

    /**
     * JNIEnv_ptr ty
     */
    Ptr<Ty> GetJNIEnvPtrTy();

    /**
     * Java_CFFI_JavaEntity ty
     */
    Ptr<Ty> GetJavaEntityTy();

    /**
     * jobject ty
     */
    AST::Ty& GetJniJobjectTy() const;

    /**
     * jclass ty
     */
    AST::Ty& GetJniJClassTy() const;

    /**
     * jmethodId ty
     */
    AST::Ty& GetJniJmethodIdTy() const;

    /**
     * jfieldId ty
     */
    AST::Ty& GetJniJfieldIdTy() const;

    /**
     * jlong
     */
    Ptr<Ty> GetJlongTy() const;

    /**
     * jobject type
     */
    OwnedPtr<Type> CreateJobjectType() const;

    /**
     * jlong
     */
    OwnedPtr<Type> CreateJlongType() const;

    /**
     * Returns cjExpr wrapped into java entity:
     *
     * Java_CFFI_JavaEntity(cjExpr)
     */
    OwnedPtr<Expr> WrapJavaEntity(OwnedPtr<Expr> cjExpr);

    /**
     * Java_CFFI_JavaEntityJobject(jobject: CPointer<Unit>)
     */
    OwnedPtr<CallExpr> CreateJavaEntityJobjectCall(OwnedPtr<Expr> arg);

    /**
     * Java_CFFI_JavaEntityJobjectNull()
     */
    OwnedPtr<Expr> CreateJavaEntityNullCall(Ptr<File> curFile);

    /**
     * match (arg) {
     *     case Some(argv) => argv.javaref
     *     case None => Java_CFFI_JavaEntityJobjectNull()
     * }
     */
    OwnedPtr<Expr> CreateJavaEntityFromOptionMirror(OwnedPtr<Expr> option, ClassLikeDecl& mirror);

    /**
     * Java_CFFI_JavaEntity() // Unit
     */
    OwnedPtr<CallExpr> CreateJavaEntityCall(Ptr<File> file);

    /**
     * Java_CFFI_JavaEntity(arg)
     */
    OwnedPtr<Expr> CreateJavaEntityCall(OwnedPtr<Expr> arg);

    /**
     * Java_CFFI_get_env()
     */
    OwnedPtr<CallExpr> CreateGetJniEnvCall(Ptr<File> curFile);

    /**
     * Java_CFFI_newJavaArray(env, signature, [args])
     */
    OwnedPtr<CallExpr> CreateCFFINewJavaArrayCall(OwnedPtr<Expr> jniEnv, FuncParamList& params);

    /**
     * Java_CFFI_swapLocalWithGlobalReference(env, localRef)
     */
    OwnedPtr<CallExpr> CreateSwapLocalWithGlobalRefCall(OwnedPtr<Expr> env, OwnedPtr<Expr> localRef) const;

    /**
     * Java_CFFI_newGlobalRef(env, obj)
     */
    OwnedPtr<CallExpr> CreateNewGlobalRefCall(OwnedPtr<Expr> env, OwnedPtr<Expr> obj);

    /**
     * Java_CFFI_deleteGlobalRef(env, obj)
     */
    OwnedPtr<CallExpr> CreateDeleteGlobalRefCall(OwnedPtr<Expr> env, OwnedPtr<Expr> obj) const;

    /**
     * Java_CFFI_arrayGetLength(env)
     */
    OwnedPtr<CallExpr> CreateCFFIArrayLengthGetCall(OwnedPtr<Expr> javarefExpr, Ptr<File> curFile);

    /**
     * CFFI method call:
     * Java_CFFI_arrayGet(jniEnv, obj, typeSignature, cffiMethodArgs)
     * Java_CFFI_arraySet(jniEnv, obj, typeSignature, cffiMethodArgs)
     */
    OwnedPtr<AST::Expr> CreateCFFICallArrayMethodCall(OwnedPtr<AST::Expr> jniEnv, OwnedPtr<AST::Expr> obj,
        AST::FuncParamList& params, const Ptr<AST::GenericParamDecl> genericParam,
        Native::FFI::Java::ArrayOperationKind kind);

    /**
     * Converts the result of a direct JNI method call to the Cangjie value expected
     * by the generated code.
     *
     * Reference types are converted using the existing Java interop conversion logic
     * (currently based on JavaEntity). JNI boolean results are normalized to
     * Cangjie Bool. Other primitive results are returned unchanged.
     */
    OwnedPtr<Expr> ConvertJavaResultToCJ(OwnedPtr<Expr> result, Ptr<Ty> resultTy, const Ptr<Decl> scope = nullptr);

    /**
     * Java_CFFI_removeFromRegistry(registryId)
     */
    OwnedPtr<CallExpr> CreateRemoveFromRegistryCall(OwnedPtr<Expr> regId);

    /**
     * Java_CFFI_put_to_registry_1(obj)
     */
    OwnedPtr<CallExpr> CreatePutToRegistryCall(OwnedPtr<Expr> obj);

    /**
     * Java_CFFI_putToRegistry($jnienv, entity, obj)
     * Puts the passed object obj to the registry.
     * Assigns registry id field to corresponding java object.
     */
    OwnedPtr<CallExpr> CreatePutSetToRegistryCall(OwnedPtr<Expr> env, OwnedPtr<Expr> entity, OwnedPtr<Expr> obj);

    OwnedPtr<AST::CallExpr> CreateGetRegistryIdCall(OwnedPtr<AST::Expr> env, OwnedPtr<AST::Expr> entity,
        bool nullable = false);

    /**
     * Java_CFFI_getFromRegistryByObj{Option}<ty>(env, obj)
     */
    OwnedPtr<CallExpr> CreateGetFromRegistryByEntityCall(OwnedPtr<Expr> env, OwnedPtr<Expr> obj, Ptr<Ty> ty,
                                                         bool retAsOption);

    /**
     * Java_CFFI_JavaStringToCangjie(env, jstring)
     */
    OwnedPtr<CallExpr> CreateJavaStringToCangjieCall(OwnedPtr<Expr> env, OwnedPtr<Expr> jstring);

    /**
     * Java_CFFI_CangjieStringToJava(env, string)
     */
    OwnedPtr<CallExpr> CreateCangjieStringToJavaCall(OwnedPtr<Expr> env, OwnedPtr<Expr> string);

    /**
     * Java_CFFI_getFromRegistry<ty>(env, regId)
     */
    OwnedPtr<CallExpr> CreateGetFromRegistryCall(OwnedPtr<Expr> env, OwnedPtr<Expr> regId, Ptr<Ty> ty);

    /**
     * Java_CFFI_getFromRegistryOption<ty>(regId)
     */
    OwnedPtr<CallExpr> CreateGetFromRegistryOptionCall(OwnedPtr<Expr> regId, Ptr<Ty> ty);

    /**
     * Java_CFFI_ensure_not_null(entity)
     */
    OwnedPtr<Expr> CreateEnsureNotNullCall(OwnedPtr<Expr> entity);

    /**
     * Java_CFFI_getJavaEntityOrNull(entity)
     */
    OwnedPtr<Expr> CreateGetJavaEntityOrNullCall(OwnedPtr<Expr> entity);

    OwnedPtr<Expr> WrapExceptionHandling(OwnedPtr<Expr> env, OwnedPtr<LambdaExpr> action);

    /**
     * CPointer<Unit>()
     */
    OwnedPtr<PointerExpr> CreateJobjectNull();

    /**
     * // entityOption: Java_CFFI_JavaEntity
     *   match (entityOption.isNull) {
     *       case true => None<ty>
     *       case false => Some<ty>(ty(entityOption))
     *   }
     * If [toRaw] = `true`, then it returns java reference as CPointer<Unit> instead of an instance
     */
    OwnedPtr<Expr> UnwrapJavaMirrorOption(
        OwnedPtr<Expr> entityOption, Ptr<Ty> ty, Ptr<const Decl> scope, bool toRaw = false);

    OwnedPtr<Expr> UnwrapJavaImplOption(OwnedPtr<Expr> env, OwnedPtr<Expr> entityOption, Ptr<Ty> ty,
        Ptr<const Decl> scope, bool toRaw = false);

    /**
     * Java_CFFI_JavaEntity -> reference wrapper
     */
    OwnedPtr<Expr> UnwrapJavaImpl(OwnedPtr<Expr> env, OwnedPtr<Expr> entity, Ptr<Ty> ty);

    /**
     * Creates unwrap call Java_CFFI_JavaEntity [entity] and returns the value it stores as [ty] if [toRaw] is `false`:
     * Java_CFFI_unwrapJavaEntityAsValue<ty>(entity)
     *
     * If ty of decl T is @JavaMirror:
     * T(entity)
     *
     * If ty of decl T is @JavaImpl:
     * Java_CFFI_getFromRegistry<T>(entity).
     * Else, if [toRaw] is `true` and T is @JavaMirror or @JavaImpl class, then it returns its CPointer<Unit> java ref
     */
    OwnedPtr<Expr> UnwrapJavaEntity(OwnedPtr<Expr> entity, Ptr<Ty> ty, Ptr<const Decl> scope, bool toRaw = false);

    OwnedPtr<AST::MatchExpr> CreateMatchByTypeArgument(const Ptr<AST::GenericParamDecl> genericParam,
        std::map<std::string, OwnedPtr<Expr>> typeToCaseMap, ModalTy retTy, OwnedPtr<Expr> defaultCase);
    OwnedPtr<AST::MatchExpr> CreateMatchWithTypeCast(OwnedPtr<Expr> exprToCast, Ptr<Ty> castTy);
    OwnedPtr<Expr> CreateGetTypeForTypeParameterCall(const Ptr<GenericParamDecl> genericParam) const;

    Ptr<FuncDecl> FindGetTypeForTypeParamDecl(File& file) const;
    Ptr<FuncDecl> FindCStringToStringDecl();
    Ptr<FuncDecl> FindStringEqualsDecl();
    Ptr<FuncDecl> FindStringStartsWithDecl();
    Ptr<FuncDecl> FindArrayJavaEntityGetDecl(ClassDecl& jArrayDecl) const;
    Ptr<FuncDecl> FindArrayJavaEntitySetDecl(ClassDecl& jArrayDecl) const;
    Ptr<FuncDecl> FindAsJValueDecl() const;
    Ptr<FuncDecl> FindAsJObjectDecl() const;

    OwnedPtr<Expr> SelectJSigByTypeKind(TypeKind kind, Ptr<Ty> ty);
    OwnedPtr<Expr> SelectJPrimitiveNameByTypeKind(TypeKind kind, Ptr<Ty> ty);
    OwnedPtr<Expr> SelectEntityWrapperByTypeKind(TypeKind kind, Ptr<Ty> ty, Ptr<FuncParam> param, Ptr<File> file);
    OwnedPtr<Expr> SelectEntityUnwrapperByTypeKind(
        TypeKind kind, Ptr<Ty> ty, Ptr<Expr> entity, Ptr<const Decl> scope);
    std::map<std::string, OwnedPtr<Expr>> GenerateTypeMappingWithSelector(
        std::function<OwnedPtr<Expr>(TypeKind, Ptr<Ty>)> selector
    );

    /**
     * ~init() {
     *     Java_CFFI_deleteGlobalReference($jnienv, this.javaref)
     * }
     */
    OwnedPtr<AST::FuncDecl> CreateDeletingGlobalRefFinalizer(AST::ClassDecl& decl);

    OwnedPtr<Expr> CreateGetClassCall(const AST::Ty& javaTy, Ptr<Expr> envPtr, File& curFile);
    OwnedPtr<Expr> CreateGetClassCall(const std::string& className, Ptr<Expr> envPtr, File& curFile);

    OwnedPtr<CallExpr> CreateGetMethodIdCall(Ptr<Expr> env, OwnedPtr<Expr> clazz,
        const std::string& name, const std::string& signature, bool isStatic);

    OwnedPtr<CallExpr> CreateGetFieldIdCall(Ptr<Expr> env, OwnedPtr<Expr> clazz,
        const Native::FFI::Java::JavaMemberSignature javaField);

    bool IsInteropLibAccessible() const;
    void CheckInteropLibVersion();
    static bool IsInteropLibAccessible(const ImportManager& importManager);
    static bool IsJavaEntityTy(Ty& ty);

    OwnedPtr<CallExpr> CreateAsJniJobjectCall(OwnedPtr<Expr> javaEntity) const;
    OwnedPtr<CallExpr> CreateAsJvalueCall(OwnedPtr<Expr> javaEntity) const;

    Ptr<StructDecl> GetJNINativeInterfaceDecl() const;

    /**
     * Converts a Cangjie expression to its JNI jvalue representation.
     */
    OwnedPtr<Expr> CreateJValueExpr(OwnedPtr<Expr> expr);
    
    OwnedPtr<Expr> CreateJObjectExpr(OwnedPtr<Expr> cjExpr);

    OwnedPtr<Expr> CreateJNIHandlePendingExceptionCall(Ptr<Expr> jniEnvPtr);

private:
   /**
    * Version value should be the same as for java interop library for the same SDK.
    * Version value must be bumped up on: API changes in interop library that require compatibility with cjc.
    */
    static constexpr auto INTEROPLIB_VERSION = 12;
    static constexpr auto INTEROPLIB_PACKAGE_NAME = "java.internal";

    const std::vector<TypeKind> supportedArrayPrimitiveElementType = {
        TypeKind::TYPE_BOOLEAN,
        TypeKind::TYPE_INT8, TypeKind::TYPE_UINT16, TypeKind::TYPE_INT16, TypeKind::TYPE_INT32, TypeKind::TYPE_INT64,
        TypeKind::TYPE_FLOAT32, TypeKind::TYPE_FLOAT64
    };

    /**
     * Unwraps value of generic type within JArray.
     */
    OwnedPtr<Expr> UnwrapJavaArrayEntity(OwnedPtr<Expr> entity, Ptr<Ty> ty, const ClassLikeDecl& mirror);
    OwnedPtr<Expr> UnwrapJavaPrimitiveEntity(OwnedPtr<Expr> entity, Ptr<Ty> ty);

    template <ASTKind K = ASTKind::DECL>
    auto ImportDecl(const std::string& package, const std::string& declname, bool silent = false) const
    {
        auto decl = importManager.GetImportedDecl(package, declname);
        if (!decl) {
            if (!silent) {
                diag.DiagnoseRefactor(DiagKindRefactor::sema_member_not_imported,
                                      DEFAULT_POSITION, package + "." + declname);
            }
            return Ptr(As<K>(nullptr));
        }

        CJC_ASSERT(decl && decl->astKind == K);
        return Ptr(StaticAs<K>(decl));
    }

    template <ASTKind K = ASTKind::DECL>
    auto GetInteropLibDecl(const std::string& declname, bool silent = false) const
    {
        return ImportDecl<K>(INTEROPLIB_PACKAGE_NAME, declname, silent);
    }

    template <ASTKind K = ASTKind::DECL>
    inline auto GetJavaLangDecl(const std::string& declname)
    {
        return ImportDecl<K>(INTEROP_JAVA_LANG_PACKAGE, declname);
    }

    OwnedPtr<CallExpr> CreateBitCastExpr(OwnedPtr<Expr> expr, Ptr<Ty> resultTy, Ptr<Ty> valueTy, Ptr<File> curFile);

    const ImportManager& importManager;
    TypeManager& typeManager;
    DiagnosticEngine& diag;
    Utils& utils;
};
}

#endif // CANGJIE_SEMA_NATIVE_FFI_JAVA_INTEROPLIB_BRIDGE
