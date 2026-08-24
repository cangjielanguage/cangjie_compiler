// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_SEMA_NATIVE_FFI_JAVA_INTEROPLIB_BRIDGE
#define CANGJIE_SEMA_NATIVE_FFI_JAVA_INTEROPLIB_BRIDGE

#include "NativeFFI/Java/Utils.h"
#include "Utils.h"
#include "NativeFFI/Utils.h"

#include "cangjie/Modules/ImportManager.h"
#include "cangjie/Sema/TypeManager.h"
#include "cangjie/AST/Match.h"

namespace Cangjie::Interop::Java {
using namespace AST;


/**
 * method or field jni signature
 */
struct MemberJNISignature {
    std::string classTypeSignature;
    std::string name;
    std::string signature;

    MemberJNISignature(std::string classTypeSignature, std::string name, std::string signature)
        : classTypeSignature(classTypeSignature), name(name), signature(signature) {}

    MemberJNISignature(Utils& utils, FuncDecl& member)
        : MemberJNISignature(utils, member, StaticAs<ASTKind::CLASS_LIKE_DECL>(member.outerDecl))
    {
        auto& retTy = *member.funcBody->retType->GetTy();
        std::vector<ModalTy> paramTys = Native::FFI::GetParamTys(*member.funcBody->paramLists[0]);
        signature = utils.GetJavaTypeSignature(retTy, paramTys);
    }

    MemberJNISignature(Utils& utils, PropDecl& member)
        : MemberJNISignature(utils, member, StaticAs<ASTKind::CLASS_LIKE_DECL>(member.outerDecl))
    {
        signature = utils.GetJavaTypeSignature(*member.GetTy());
    }

    MemberJNISignature(Utils& utils, Decl& member, Ptr<ClassLikeDecl> jobject)
    {
        CJC_ASSERT(jobject);
        Ptr<Ty> ty = jobject->DataTy();

        if (Java::IsSyntheticMirrorWrapper(*jobject)) {
            if (jobject->inheritedTypes.size() > 1) {
                ty = jobject->inheritedTypes[1]->DataTy(); // take interface ty
            } else {
                CJC_ASSERT_WITH_MSG(!jobject->inheritedTypes.empty(), "JObject must inherit Cangjie Object");
                ty = jobject->inheritedTypes[0]->DataTy(); // take superclass ty
            }
        }
        classTypeSignature = utils.GetJavaClassNormalizeSignature(*ty);
        name = GetJavaMemberName(member);
        CJC_ASSERT(member.astKind == ASTKind::FUNC_DECL || member.astKind == ASTKind::PROP_DECL);
        signature = utils.GetJavaTypeSignature(*member.GetTy());
    }
};

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
     * Java_CFFI_newGlobalReference
     */
    Ptr<FuncDecl> GetNewGlobalRefDecl();

    /**
     * Java_CFFI_deleteGlobalReference
     */
    Ptr<FuncDecl> GetDeleteGlobalRefDecl();

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
     * Java_CFFI_getField_raw
     */
    Ptr<FuncDecl> GetGetFieldDecl();

    /**
     * Java_CFFI_setField_raw
     */
    Ptr<FuncDecl> GetSetFieldDecl();

    /**
     * Java_CFFI_getStaticField_raw
     */
    Ptr<FuncDecl> GetGetStaticFieldDecl();

    /**
     * Java_CFFI_setStaticField_raw
     */
    Ptr<FuncDecl> GetSetStaticFieldDecl();

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

    Ptr<FuncDecl> GetJClassDecl() const;
    Ptr<FuncDecl> GetJClassIdDecl() const;
    Ptr<FuncDecl> GetGetMethodIdDecl() const;
    Ptr<FuncDecl> GetGetStaticMethodIdDecl() const;
    Ptr<FuncDecl> GetParseMethodSignatureDecl();
    Ptr<FuncDecl> GetParseComponentSignatureDecl();
    Ptr<FuncDecl> GetCallNestDecl();
    Ptr<FuncDecl> GetMethodIdConstr();
    Ptr<FuncDecl> GetMethodIdConstrStatic();
    Ptr<FuncDecl> GetFieldIdConstr();
    Ptr<FuncDecl> GetFieldIdConstrStatic();

    /**
     * deleteLocalRef()
     */
    Ptr<FuncDecl> GetDeleteLocalRefDecl();

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
     * Creates a block that performs Java object construction,
     * including all required JNI setup (JNIEnv, class, constructor ID,
     * arguments and exception handling), and returns the created object.
     */
    OwnedPtr<Block> CreateJavaConstructorBlock(Ptr<Ty> classTy, FuncParamList& paramList,
        Ptr<File> curFile, bool isMirror);

    /**
     * Java_CFFI_newJavaArray(env, signature, [args])
     */
    OwnedPtr<CallExpr> CreateCFFINewJavaArrayCall(OwnedPtr<Expr> jniEnv, FuncParamList& params);

    /**
     * Java_CFFI_newGlobalReference(env, obj, isWeak)
     */
    OwnedPtr<CallExpr> CreateNewGlobalRefCall(OwnedPtr<Expr> env, OwnedPtr<Expr> obj, bool isWeak);

    /**
     * Java_CFFI_deleteGlobalReference(env, obj)
     */
    OwnedPtr<CallExpr> CreateDeleteGlobalRefCall(OwnedPtr<Expr> env, OwnedPtr<Expr> obj);

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
        AST::FuncParamList& params, const Ptr<AST::GenericParamDecl> genericParam, ArrayOperationKind kind);

    /**
     * Converts the result of a direct JNI method call to the Cangjie value expected
     * by the generated code.
     *
     * Reference types are converted using the existing Java interop conversion logic
     * (currently based on JavaEntity). JNI boolean results are normalized to
     * Cangjie Bool. Other primitive results are returned unchanged.
     */
    OwnedPtr<Expr> ConvertJavaResultToCJ(OwnedPtr<Expr> result, Ptr<Ty> resultTy, Decl& scope);
    
   /**
    * Creates a block that prepares the JNI state and invokes a Java method.
    *
    * @param curFile   Current source file.
    * @param paramList Method arguments.
    * @param retTy     Method return type.
    * @param isStatic  Whether the target method is static.
    * @param javaRef   Receiver object for instance calls; unused for static calls.
    * @param signature JNI signature of the target method.
    */
    OwnedPtr<Block> CreateJavaMethodCallBlock(Ptr<File> curFile, FuncParamList& paramList, Ptr<Ty> retTy,
        bool isStatic, OwnedPtr<Expr> javaRef, const MemberJNISignature& signature);

   /**
    * Creates a block that prepares the JNI state and invokes the Java
    * superclass method corresponding to the given call.
    *
    * @param impl      JavaImpl class containing the call.
    * @param call      Original super-method call expression.
    * @param signature JNI signature of the target superclass method.
    */
    OwnedPtr<Block> CreateJavaSuperMethodCallBlock(ClassDecl& impl, CallExpr& call,
        const MemberJNISignature& signature);

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
     * Java_CFFI_getField_raw(env, obj, typeSignature, fieldName, fieldSignature)
     */
    OwnedPtr<CallExpr> CreateGetFieldCall(Ptr<Expr> env, OwnedPtr<Expr> obj, std::string typeSignature,
                                          std::string fieldName, std::string fieldSignature);

    /**
     * Java_CFFI_getStaticField_raw(env, typeSignature, fieldName, fieldSignature)
     */
    OwnedPtr<CallExpr> CreateGetStaticFieldCall(Ptr<Expr> env, std::string typeSignature,
                                          std::string fieldName, std::string fieldSignature);

    /**
     * Java_CFFI_setField_raw(env, obj, signature.classTypeSignature,
     *                        signature.name, signature.signature, Java_CFFI_JavaEntity(value))
     */
    OwnedPtr<CallExpr> CreateSetFieldCall(Ptr<Expr> env, OwnedPtr<Expr> obj,
                                          const MemberJNISignature& signature,
                                          OwnedPtr<Expr> value);

    /**
     * Java_CFFI_setStaticField_raw(env, typeSignature, fieldName, fieldSignature, Java_CFFI_JavaEntity(value))
     */
    OwnedPtr<CallExpr> CreateSetStaticFieldCall(Ptr<Expr> env, std::string typeSignature, std::string fieldName,
                                          std::string fieldSignature, OwnedPtr<Expr> value);

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
        OwnedPtr<Expr> entityOption, Ptr<Ty> ty, const ClassLikeDecl& mirror, bool toRaw = false);

    OwnedPtr<Expr> UnwrapJavaImplOption(OwnedPtr<Expr> env, OwnedPtr<Expr> entityOption, Ptr<Ty> ty,
        const ClassLikeDecl& mirror, bool toRaw = false);

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
    OwnedPtr<Expr> UnwrapJavaEntity(OwnedPtr<Expr> entity, Ptr<Ty> ty, const Decl& outerDecl, bool toRaw = false);

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
        TypeKind kind, Ptr<Ty> ty, Ptr<Expr> entity, const ClassLikeDecl& mirror);
    std::map<std::string, OwnedPtr<Expr>> GenerateTypeMappingWithSelector(
        std::function<OwnedPtr<Expr>(TypeKind, Ptr<Ty>)> selector
    );

    /**
     * ~init() {
     *     Java_CFFI_deleteGlobalReference($jnienv, this.javaref)
     * }
     */
    OwnedPtr<AST::FuncDecl> CreateDeletingGlobalRefFinalizer(AST::ClassDecl& decl);

    bool IsInteropLibAccessible() const;
    void CheckInteropLibVersion();
    static bool IsInteropLibAccessible(const ImportManager& importManager);
    static bool IsJavaEntityTy(Ty& ty);

private:
   /**
    * Version value should be the same as for java interop library for the same SDK.
    * Version value must be bumped up on: API changes in interop library that require compatibility with cjc.
    */
    static constexpr auto INTEROPLIB_VERSION = 11;
    static constexpr auto INTEROPLIB_PACKAGE_NAME = "java.internal";

    const std::vector<TypeKind> supportedArrayPrimitiveElementType = {
        TypeKind::TYPE_BOOLEAN,
        TypeKind::TYPE_INT8, TypeKind::TYPE_UINT16, TypeKind::TYPE_INT16, TypeKind::TYPE_INT32, TypeKind::TYPE_INT64,
        TypeKind::TYPE_FLOAT32, TypeKind::TYPE_FLOAT64
    };

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

    const std::string_view SelectJNIStaticMethodName(TypeKind retTypeKind, bool hasArgs);
    const std::string_view SelectJNIInstanceMethodName(TypeKind retTypeKind, bool virt, bool hasArgs);

    /**
     * onverts a Cangjie expression to its JNI jvalue representation.
     */
    OwnedPtr<Expr> CreateJValueExpr(OwnedPtr<Expr> expr);
    /**
     * Creates JNI jvalue-compatible expressions for a non-empty function
     * parameter list.
     *
     * Each returned expression corresponds to one function parameter converted
     * to its JNI jvalue representation.
     */
    std::vector<OwnedPtr<Expr>> CreateJNIArgJValueExprs(FuncParamList& paramList, File& curFile);

    /**
    * Creates a variable holding a Cangjie VArray of JNI jvalue-compatible
    * argument representations.
    *
    * The input expressions must already be converted to the representation
    * expected in JNI argument slots.
    *
    * Returns nullptr if the argument list is empty
    */
    OwnedPtr<VarDecl> CreateJNIArgsVar(std::vector<OwnedPtr<Expr>> args, File& curFile);

   /**
    * Creates a variable holding the Java class resolved by its JNI class name.
    *
    * Generates a Java_CFFI_ClassInit(env, className) call and stores its result
    * in a temporary variable for use by subsequent JNI operations.
    *
    * Returns nullptr if the required CFFI class initialization function
    * cannot be resolved.
    */
    OwnedPtr<VarDecl> CreateJNIClassVar(Ptr<Expr> env, const std::string& className);

   /**
    * Builds a JNI NewObject/NewObjectA call and wraps the result as JavaEntity.
    *
    * @param env       JNI environment (`JNIEnv*`).
    * @param javaClass Target Java class (`jclass`) to instantiate.
    * @param methodID  Constructor method ID (`jmethodID`).
    * @param argsExpr  Constructor arguments as a `jvalue[]` expression.
    *                   If null, `NewObject` is used; otherwise `NewObjectA`.
    */
    OwnedPtr<CallExpr> CreateJNINewObjectCall(Ptr<Expr> env, OwnedPtr<Expr> javaClass,
        OwnedPtr<Expr> methodID, OwnedPtr<Expr> argsExpr);

    OwnedPtr<CallExpr> CreateJNICall(Ptr<Expr> env, Ptr<VarDecl> jniFunction, std::vector<OwnedPtr<FuncArg>> callArgs);
    Ptr<StructDecl> GetJNINativeInterfaceDecl() const;
    Ptr<VarDecl> GetJNINativeInterfaceField(const std::string_view name);
    OwnedPtr<FuncArg> PrepareJNIArgsVArray(OwnedPtr<Expr> expr);
    OwnedPtr<CallExpr> CreateJNIEnvReadCall(Ptr<Expr> env);
    OwnedPtr<CallExpr> CreateAsJObjectCall(OwnedPtr<Expr> javaEntity);
    OwnedPtr<CallExpr> CreateBitCastExpr(OwnedPtr<Expr> expr, Ptr<Ty> resultTy, Ptr<Ty> valueTy, Ptr<File> curFile);
    OwnedPtr<CallExpr> CreateJNIMethodId(Ptr<Expr> env, OwnedPtr<Expr> clazz,
        const std::string& name, const std::string& signature, bool isStatic);

    const ImportManager& importManager;
    TypeManager& typeManager;
    DiagnosticEngine& diag;
    Utils& utils;
};
}

#endif // CANGJIE_SEMA_NATIVE_FFI_JAVA_INTEROPLIB_BRIDGE
