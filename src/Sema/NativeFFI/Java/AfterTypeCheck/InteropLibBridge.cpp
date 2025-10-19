// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "InteropLibBridge.h"
#include "Desugar/AfterTypeCheck.h"
#include "JavaDesugarManager.h"
#include "TypeCheckUtil.h"
#include "cangjie/AST/Clone.h"
#include "cangjie/AST/Create.h"
#include "NativeFFI/Utils.h"

namespace {

// vars
constexpr auto INTEROPLIB_VERSION_FIELD_ID = "INTEROPLIB_VERSION";
constexpr auto JAVA_CONSTRUCTOR = "<init>";

// types
constexpr auto INTEROPLIB_JNI_ENV_PTR_ID = "JNIEnv_ptr";
constexpr auto INTEROPLIB_JNI_JLONG_ID = "jlong";
constexpr auto INTEROPLIB_JNI_JOBJECT_ID = "jobject";
constexpr auto INTEROPLIB_JAVA_ENTITY_KIND = "Java_CFFI_JavaEntityKind";
constexpr auto INTEROPLIB_JAVA_ENTITY_KIND_JOBJECT = "JOBJECT";
constexpr auto INTEROPLIB_JAVA_ENTITY_KIND_JBYTE = "JBYTE";
constexpr auto INTEROPLIB_JAVA_ENTITY_KIND_JSHORT = "JSHORT";
constexpr auto INTEROPLIB_JAVA_ENTITY_KIND_JCHAR = "JCHAR";
constexpr auto INTEROPLIB_JAVA_ENTITY_KIND_JINT = "JINT";
constexpr auto INTEROPLIB_JAVA_ENTITY_KIND_JLONG = "JLONG";
constexpr auto INTEROPLIB_JAVA_ENTITY_KIND_JFLOAT = "JFLOAT";
constexpr auto INTEROPLIB_JAVA_ENTITY_KIND_JDOUBLE = "JDOUBLE";
constexpr auto INTEROPLIB_JAVA_ENTITY_KIND_JBOOLEAN = "JBOOLEAN";

// funcs
constexpr auto INTEROPLIB_JNI_GET_ENV_ID = "Java_CFFI_get_env";
constexpr auto INTEROPLIB_CFFI_NEW_GLOBAL_REF_ID = "Java_CFFI_newGlobalReference";
constexpr auto INTEROPLIB_CFFI_DELETE_GLOBAL_REF_ID = "Java_CFFI_deleteGlobalReference";
constexpr auto INTEROPLIB_CFFI_NEW_JAVA_OBJECT_ID = "Java_CFFI_newJavaObject";
constexpr auto INTEROPLIB_CFFI_NEW_JAVA_ARRAY_ID = "Java_CFFI_newJavaArray";
constexpr auto INTEROPLIB_CFFI_NEW_JAVA_PROXY_OBJECT_FOR_CJMAPPING_ID = "Java_CFFI_newJavaProxyObjectForCJMapping";
constexpr auto INTEROPLIB_CFFI_JAVA_ARRAY_GET_ID = "Java_CFFI_arrayGet";
constexpr auto INTEROPLIB_CFFI_JAVA_ARRAY_SET_ID = "Java_CFFI_arraySet";
constexpr auto INTEROPLIB_CFFI_JAVA_ARRAY_GET_LENGTH = "Java_CFFI_arrayGetLength";
constexpr auto INTEROPLIB_CFFI_JAVA_ENTITY_JOBJECT_ID = "Java_CFFI_JavaEntityJobject";
constexpr auto INTEROPLIB_CFFI_JAVA_ENTITY_NULL_ID = "Java_CFFI_JavaEntityJobjectNull";
constexpr auto INTEROPLIB_CFFI_JAVA_ENTITY_IS_NULL_ID = "isNull";
constexpr auto INTEROPLIB_JNI_PUT_TO_REGISTRY_DECL_ID = "Java_CFFI_put_to_registry_1";
constexpr auto INTEROPLIB_JNI_PUT_TO_REGISTRY_SELF_INIT_DECL_ID = "Java_CFFI_putToRegistry";
constexpr auto INTEROPLIB_JNI_DELETE_CJ_OBJECT_DECL_ID = "Java_CFFI_deleteCJObject";
constexpr auto INTEROPLIB_JNI_REMOVE_FROM_REGISTRY_DECL_ID = "Java_CFFI_removeFromRegistry";
constexpr auto INTEROPLIB_CFFI_CALL_METHOD_DECL_ID = "Java_CFFI_callVirtualMethod";
constexpr auto INTEROPLIB_CFFI_NON_VIRT_CALL_METHOD_DECL_ID = "Java_CFFI_callMethod";
constexpr auto INTEROPLIB_CFFI_CALL_STATIC_METHOD_DECL_ID = "Java_CFFI_callStaticMethod";
constexpr auto INTEROPLIB_CFFI_UNWRAP_JAVA_ENTITY_METHOD_DECL_ID = "Java_CFFI_unwrapJavaEntityAsValue";
constexpr auto INTEROPLIB_CFFI_GET_FROM_REGISTRY_METHOD_DECL_ID = "Java_CFFI_getFromRegistry";
constexpr auto INTEROPLIB_CFFI_GET_FROM_REGISTRY_OPTION_METHOD_DECL_ID = "Java_CFFI_getFromRegistryOption";
constexpr auto INTEROPLIB_CFFI_GET_FIELD_METHOD_DECL_ID = "Java_CFFI_getField";
constexpr auto INTEROPLIB_CFFI_GET_STATIC_FIELD_METHOD_DECL_ID = "Java_CFFI_getStaticField";
constexpr auto INTEROPLIB_CFFI_SET_FIELD_METHOD_DECL_ID = "Java_CFFI_setField";
constexpr auto INTEROPLIB_CFFI_SET_STATIC_FIELD_METHOD_DECL_ID = "Java_CFFI_setStaticField";
constexpr auto INTEROPLIB_CFFI_ENSURE_NOT_NULL_METHOD_DECL_ID = "Java_CFFI_ensure_not_null";
constexpr auto INTEROPLIB_CFFI_GET_JAVA_ENTITY_OR_NULL_METHOD_DECL_ID = "Java_CFFI_getJavaEntityOrNull";
constexpr auto INTEROPLIB_CFFI_IS_INSTANCE_OF_DECL_ID = "Java_CFFI_isInstanceOf";
constexpr auto INTEROPLIB_CFFI_GET_FROM_REGISTRY_BY_ENTITY_OPTION_DECL_ID = "Java_CFFI_getFromRegistryByObjOption";
constexpr auto INTEROPLIB_CFFI_GET_FROM_REGISTRY_BY_ENTITY_DECL_ID = "Java_CFFI_getFromRegistryByObj";
constexpr auto INTEROPLIB_CFFI_JAVA_STRING_TO_CANGJIE = "Java_CFFI_JavaStringToCangjie";
constexpr auto INTEROPLIB_CFFI_CANGJIE_STRING_TO_JAVA = "Java_CFFI_CangjieStringToJava";
constexpr auto INTEROPLIB_CFFI_WITH_EXCEPTION_HANDLING_ID = "withExceptionHandling";
constexpr auto INTEROPLIB_CFFI_JAVA_CFFI_CLASS_ID = "Java_CFFI_ClassInit";
constexpr auto INTEROPLIB_CFFI_PARSE_METHOD_SIGNATURE_ID = "Java_CFFI_parseMethodSignature";
constexpr auto INTEROPLIB_CFFI_PARSE_COMPONENT_SIGNATURE_ID = "Java_CFFI_parseComponentSignature";
constexpr auto INTEROPLIB_CFFI_JAVA_CALLNEST_ID = "Java_CFFI_JavaCallNestInit";
constexpr auto INTEROPLIB_CFFI_JAVA_METHODID_CONSTR_ID = "Java_CFFI_MethodIDConstr";
constexpr auto INTEROPLIB_CFFI_JAVA_METHODID_CONSTR_STATIC_ID = "Java_CFFI_MethodIDConstrStatic";
constexpr auto INTEROPLIB_CFFI_JAVA_FIELDID_CONSTR_ID = "Java_CFFI_FieldIDConstr";
constexpr auto INTEROPLIB_CFFI_JAVA_FIELDID_CONSTR_STATIC_ID = "Java_CFFI_FieldIDConstrStatic";

} // namespace

using namespace Cangjie;
using namespace AST;
using namespace TypeCheckUtil;
using namespace Sema::Desugar::AfterTypeCheck;

namespace Cangjie::Interop::Java {

using namespace Cangjie::Native::FFI;

// declarations

Ptr<TypeAliasDecl> InteropLibBridge::GetJobjectDecl()
{
    return GetInteropLibDecl<ASTKind::TYPE_ALIAS_DECL>(INTEROPLIB_JNI_JOBJECT_ID);
}

Ptr<StructDecl> InteropLibBridge::GetJavaEntityDecl()
{
    return GetInteropLibDecl<ASTKind::STRUCT_DECL>(INTEROPLIB_CFFI_JAVA_ENTITY);
}

Ptr<TypeAliasDecl> InteropLibBridge::GetJniEnvPtrDecl()
{
    return GetInteropLibDecl<ASTKind::TYPE_ALIAS_DECL>(INTEROPLIB_JNI_ENV_PTR_ID);
}

Ptr<EnumDecl> InteropLibBridge::GetJavaEntityKindDecl()
{
    return GetInteropLibDecl<ASTKind::ENUM_DECL>(INTEROPLIB_JAVA_ENTITY_KIND);
}

Decl& InteropLibBridge::GetJavaEntityKindJObject()
{
    auto entityKindDecl = GetJavaEntityKindDecl();
    return *LookupEnumMember(entityKindDecl, INTEROPLIB_JAVA_ENTITY_KIND_JOBJECT);
}

Ptr<FuncDecl> InteropLibBridge::GetNewGlobalRefDecl()
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_CFFI_NEW_GLOBAL_REF_ID);
}

Ptr<FuncDecl> InteropLibBridge::GetDeleteGlobalRefDecl()
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_CFFI_DELETE_GLOBAL_REF_ID);
}

Ptr<FuncDecl> InteropLibBridge::GetGetJniEnvDecl()
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_JNI_GET_ENV_ID);
}

Ptr<FuncDecl> InteropLibBridge::GetCreateJavaEntityJobjectDecl()
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_CFFI_JAVA_ENTITY_JOBJECT_ID);
}

Ptr<FuncDecl> InteropLibBridge::GetCreateJavaEntityNullDecl()
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_CFFI_JAVA_ENTITY_NULL_ID);
}

Ptr<FuncDecl> InteropLibBridge::GetNewJavaObjectDecl()
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_CFFI_NEW_JAVA_OBJECT_ID);
}

Ptr<FuncDecl> InteropLibBridge::GetNewJavaArrayDecl()
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_CFFI_NEW_JAVA_ARRAY_ID);
}

Ptr<FuncDecl> InteropLibBridge::GetNewJavaProxyObjectForCJMappingDecl()
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_CFFI_NEW_JAVA_PROXY_OBJECT_FOR_CJMAPPING_ID);
}

Ptr<FuncDecl> InteropLibBridge::GetJavaArrayGetDecl()
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_CFFI_JAVA_ARRAY_GET_ID);
}

Ptr<FuncDecl> InteropLibBridge::GetJavaArraySetDecl()
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_CFFI_JAVA_ARRAY_SET_ID);
}

Ptr<FuncDecl> InteropLibBridge::GetJavaArrayGetLengthDecl()
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_CFFI_JAVA_ARRAY_GET_LENGTH);
}

Ptr<FuncDecl> InteropLibBridge::GetCallMethodDecl()
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_CFFI_CALL_METHOD_DECL_ID);
}

Ptr<FuncDecl> InteropLibBridge::GetNonVirtualCallMethodDecl()
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_CFFI_NON_VIRT_CALL_METHOD_DECL_ID);
}

Ptr<FuncDecl> InteropLibBridge::GetCallStaticMethodDecl()
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_CFFI_CALL_STATIC_METHOD_DECL_ID);
}

Ptr<FuncDecl> InteropLibBridge::GetDeleteCJObjectDecl()
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_JNI_DELETE_CJ_OBJECT_DECL_ID);
}

Ptr<FuncDecl> InteropLibBridge::GetRemoveFromRegistryDecl()
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_JNI_REMOVE_FROM_REGISTRY_DECL_ID);
}

Ptr<FuncDecl> InteropLibBridge::GetPutToRegistryDecl()
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_JNI_PUT_TO_REGISTRY_DECL_ID);
}

Ptr<FuncDecl> InteropLibBridge::GetPutToRegistrySelfInitDecl()
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_JNI_PUT_TO_REGISTRY_SELF_INIT_DECL_ID);
}

Ptr<FuncDecl> InteropLibBridge::GetUnwrapJavaEntityDecl()
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_CFFI_UNWRAP_JAVA_ENTITY_METHOD_DECL_ID);
}

Ptr<FuncDecl> InteropLibBridge::GetGetFromRegistryByEntityOptionDecl()
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_CFFI_GET_FROM_REGISTRY_BY_ENTITY_OPTION_DECL_ID);
}

Ptr<FuncDecl> InteropLibBridge::GetGetFromRegistryByEntityDecl()
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_CFFI_GET_FROM_REGISTRY_BY_ENTITY_DECL_ID);
}

Ptr<FuncDecl> InteropLibBridge::GetJavaStringToCangjie()
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_CFFI_JAVA_STRING_TO_CANGJIE);
}

Ptr<FuncDecl> InteropLibBridge::GetCangjieStringToJava()
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_CFFI_CANGJIE_STRING_TO_JAVA);
}

Ptr<FuncDecl> InteropLibBridge::GetGetFromRegistryDecl()
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_CFFI_GET_FROM_REGISTRY_METHOD_DECL_ID);
}

Ptr<FuncDecl> InteropLibBridge::GetGetFromRegistryOptionDecl()
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_CFFI_GET_FROM_REGISTRY_OPTION_METHOD_DECL_ID);
}

Ptr<FuncDecl> InteropLibBridge::GetGetFieldDecl()
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_CFFI_GET_FIELD_METHOD_DECL_ID);
}

Ptr<FuncDecl> InteropLibBridge::GetGetStaticFieldDecl()
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_CFFI_GET_STATIC_FIELD_METHOD_DECL_ID);
}

Ptr<FuncDecl> InteropLibBridge::GetSetFieldDecl()
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_CFFI_SET_FIELD_METHOD_DECL_ID);
}

Ptr<FuncDecl> InteropLibBridge::GetSetStaticFieldDecl()
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_CFFI_SET_STATIC_FIELD_METHOD_DECL_ID);
}

Ptr<VarDecl> InteropLibBridge::GetInteropLibVersionVarDecl()
{
    return GetInteropLibDecl<ASTKind::VAR_DECL>(INTEROPLIB_VERSION_FIELD_ID, true);
}

Ptr<FuncDecl> InteropLibBridge::GetEnsureNotNullDecl()
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_CFFI_ENSURE_NOT_NULL_METHOD_DECL_ID);
}

Ptr<FuncDecl> InteropLibBridge::GetGetJavaEntityOrNullDecl()
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_CFFI_GET_JAVA_ENTITY_OR_NULL_METHOD_DECL_ID);
}

Ptr<FuncDecl> InteropLibBridge::GetIsInstanceOf()
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_CFFI_IS_INSTANCE_OF_DECL_ID);
}

Ptr<FuncDecl> InteropLibBridge::GetWithExceptionHandlingDecl()
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_CFFI_WITH_EXCEPTION_HANDLING_ID);
}

Ptr<FuncDecl> InteropLibBridge::GetJClassDecl()
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_CFFI_JAVA_CFFI_CLASS_ID);
}

Ptr<FuncDecl> InteropLibBridge::GetParseMethodSignatureDecl()
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_CFFI_PARSE_METHOD_SIGNATURE_ID);
}

Ptr<FuncDecl> InteropLibBridge::GetParseComponentSignatureDecl()
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_CFFI_PARSE_COMPONENT_SIGNATURE_ID);
}

Ptr<FuncDecl> InteropLibBridge::GetCallNestDecl()
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_CFFI_JAVA_CALLNEST_ID);
}

Ptr<FuncDecl> InteropLibBridge::GetMethodIdConstr()
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_CFFI_JAVA_METHODID_CONSTR_ID);
}

Ptr<FuncDecl> InteropLibBridge::GetMethodIdConstrStatic()
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_CFFI_JAVA_METHODID_CONSTR_STATIC_ID);
}

Ptr<FuncDecl> InteropLibBridge::GetFieldIdConstr()
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_CFFI_JAVA_FIELDID_CONSTR_ID);
}

Ptr<FuncDecl> InteropLibBridge::GetFieldIdConstrStatic()
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_CFFI_JAVA_FIELDID_CONSTR_STATIC_ID);
}

// ty

Ptr<Ty> InteropLibBridge::GetJNIEnvPtrTy()
{
    auto decl = GetJniEnvPtrDecl();
    if (!decl) {
        return nullptr;
    }
    return decl->type->ty;
}

Ptr<Ty> InteropLibBridge::GetJavaEntityTy()
{
    auto decl = GetJavaEntityDecl();
    if (!decl) {
        return nullptr;
    }

    return decl->ty;
}

Ptr<Ty> InteropLibBridge::GetJobjectTy()
{
    return typeManager.GetPointerTy(typeManager.GetPrimitiveTy(TypeKind::TYPE_UNIT));
}

OwnedPtr<PointerExpr> InteropLibBridge::CreateJobjectNull()
{
    auto pointerExpr = MakeOwnedNode<PointerExpr>();
    pointerExpr->type = MakeOwnedNode<Type>();
    pointerExpr->type->ty = GetJobjectTy();
    pointerExpr->ty = pointerExpr->type->ty;
    return pointerExpr;
}

OwnedPtr<FuncParam> InteropLibBridge::CreateEnvFuncParam()
{
    auto decl = GetJniEnvPtrDecl();
    CJC_NULLPTR_CHECK(decl);
    CJC_NULLPTR_CHECK(decl->type);
    const std::string name = "env";
    return CreateFuncParam(name, ASTCloner::Clone(decl->type.get()), nullptr, decl->type->ty);
}

OwnedPtr<FuncParam> InteropLibBridge::CreateJClassOrJObjectFuncParam(const std::string& name)
{
    return CreateFuncParam(name, CreateJobjectType(), nullptr, GetJobjectTy());
}

OwnedPtr<FuncParam> InteropLibBridge::CreateSelfFuncParam()
{
    const std::string name = "self";
    return CreateFuncParam(name, CreateJlongType(), nullptr, GetJlongTy());
}

Ptr<Ty> InteropLibBridge::GetJlongTy()
{
    return typeManager.GetPrimitiveTy(TypeKind::TYPE_INT64);
}

// type

OwnedPtr<Type> InteropLibBridge::CreateJobjectType()
{
    Ptr<TypeAliasDecl> jobjectDecl = GetInteropLibDecl<ASTKind::TYPE_ALIAS_DECL>(INTEROPLIB_JNI_JOBJECT_ID);
    if (!jobjectDecl) {
        return nullptr;
    }
    return ASTCloner::Clone(Ptr(StaticAs<ASTKind::TYPE_ALIAS_DECL>(jobjectDecl)->type.get()));
}

OwnedPtr<Type> InteropLibBridge::CreateJlongType()
{
    Ptr<TypeAliasDecl> jlongDecl = GetInteropLibDecl<ASTKind::TYPE_ALIAS_DECL>(INTEROPLIB_JNI_JLONG_ID);
    if (!jlongDecl) {
        return nullptr;
    }
    return ASTCloner::Clone(Ptr(StaticAs<ASTKind::TYPE_ALIAS_DECL>(jlongDecl)->type.get()));
}

// applications

OwnedPtr<CallExpr> InteropLibBridge::CreateGetJniEnvCall(Ptr<File> curFile)
{
    return CreateCall(GetGetJniEnvDecl(), curFile);
}

OwnedPtr<CallExpr> InteropLibBridge::CreateJavaEntityCall(Ptr<File> file)
{
    auto javaEntityDecl = GetJavaEntityDecl();
    if (!javaEntityDecl) {
        return nullptr;
    }

    Ptr<FuncDecl> suitableCtor;

    for (auto& decl : javaEntityDecl->body->decls) {
        if (auto ctor = As<ASTKind::FUNC_DECL>(decl.get())) {
            if (ctor->funcBody->paramLists[0]->params.empty()) {
                suitableCtor = ctor;
                break;
            }
        }
    }

    if (!suitableCtor) {
        diag.DiagnoseRefactor(
            DiagKindRefactor::sema_member_not_imported, DEFAULT_POSITION, INTEROPLIB_CFFI_JAVA_ENTITY);
    }

    auto call = CreateCall(suitableCtor, file);
    if (call) {
        call->callKind = CallKind::CALL_STRUCT_CREATION;
    }
    return call;
}

OwnedPtr<Expr> InteropLibBridge::CreateJavaEntityJobjectCall(OwnedPtr<Expr> arg)
{
    auto curFile = arg->curFile;
    return CreateCall(GetCreateJavaEntityJobjectDecl(), curFile, std::move(arg));
}

OwnedPtr<Expr> InteropLibBridge::CreateJavaEntityNullCall(Ptr<File> curFile)
{
    return CreateCall(GetCreateJavaEntityNullDecl(), curFile);
}

OwnedPtr<Expr> InteropLibBridge::CreateJavaEntityFromOptionMirror(OwnedPtr<Expr> option, ClassLikeDecl& mirror)
{
    auto curFile = option->curFile;
    CJC_NULLPTR_CHECK(curFile);
    auto mirrorTy = option->ty->typeArgs[0];
    // `case Some(argv) => argv.javaref`
    auto vp = CreateVarPattern(V_COMPILER, mirrorTy);
    vp->curFile = curFile;
    vp->varDecl->curFile = curFile;
    auto javarefAccess = CreateJavaRefCall(WithinFile(CreateRefExpr(*vp->varDecl), curFile), mirror);

    auto somePattern = MakeOwnedNode<EnumPattern>();
    somePattern->ty = utils.GetOptionTy(mirrorTy);
    somePattern->constructor = utils.CreateOptionSomeRef(mirrorTy);
    somePattern->patterns.emplace_back(std::move(vp));
    somePattern->curFile = curFile;
    auto caseSome = CreateMatchCase(std::move(somePattern), std::move(javarefAccess));

    // `case None => Java_CFFI_JavaEntityJobjectNull()`
    auto nonePattern = MakeOwnedNode<EnumPattern>();
    nonePattern->constructor = utils.CreateOptionNoneRef(mirrorTy);
    nonePattern->ty = nonePattern->constructor->ty;
    nonePattern->curFile = curFile;
    auto caseNone = CreateMatchCase(std::move(nonePattern), CreateJavaEntityNullCall(curFile));

    // `match`
    std::vector<OwnedPtr<MatchCase>> matchCases;
    matchCases.emplace_back(std::move(caseSome));
    matchCases.emplace_back(std::move(caseNone));
    return WithinFile(CreateMatchExpr(std::move(option), std::move(matchCases), GetJavaEntityTy()), curFile);
}

OwnedPtr<Expr> InteropLibBridge::CreateJavaEntityCall(OwnedPtr<Expr> arg)
{
    auto javaEntityDecl = GetJavaEntityDecl();
    if (!javaEntityDecl) {
        return nullptr;
    }

    if (auto classLTy = DynamicCast<ClassLikeTy*>(arg->ty)) {
        if (auto decl = classLTy->commonDecl;
            decl && decl->TestAnyAttr(Attribute::JAVA_MIRROR, Attribute::JAVA_MIRROR_SUBTYPE)) {
            return CreateJavaRefCall(std::move(arg));
        }
    } else if (arg->ty->IsCoreOptionType()) {
        if (auto classALTy = DynamicCast<ClassLikeTy*>(arg->ty->typeArgs[0])) {
            if (auto decl = classALTy->commonDecl;
                decl && decl->TestAnyAttr(Attribute::JAVA_MIRROR, Attribute::JAVA_MIRROR_SUBTYPE)) {
                return CreateJavaEntityFromOptionMirror(std::move(arg), *decl);
            }
        }
    }

    Ptr<FuncDecl> suitableCtor;

    for (auto& decl : javaEntityDecl->body->decls) {
        if (auto ctor = As<ASTKind::FUNC_DECL>(decl.get()); ctor && ctor->TestAttr(Attribute::CONSTRUCTOR)) {
            if (ctor->funcBody->paramLists[0]->params.size() != 1) {
                continue;
            }
            if (typeManager.IsTyEqual(arg->ty, ctor->funcBody->paramLists[0]->params[0]->ty)) {
                suitableCtor = ctor;
            }
        }
    }

    if (!suitableCtor) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_java_interop_not_supported, arg->begin, "Type " + arg->ty->name);
        return nullptr;
    }

    auto curFile = arg->curFile;
    auto call = CreateCall(suitableCtor, curFile, std::move(arg));
    if (call) {
        call->callKind = CallKind::CALL_STRUCT_CREATION;
    }
    return call;
}

OwnedPtr<Expr> InteropLibBridge::WrapJavaEntity(OwnedPtr<Expr> cjExpr)
{
    CJC_NULLPTR_CHECK(cjExpr->curFile);
    if (cjExpr->ty->kind == TypeKind::TYPE_UNIT) {
        return CreateJavaEntityCall(cjExpr->curFile);
    }

    return CreateJavaEntityCall(std::move(cjExpr));
}

OwnedPtr<CallExpr> InteropLibBridge::CreateNewJavaObjectCall(OwnedPtr<Expr> env, const std::string& classTypeSignature,
    const std::string& constructorSignature, std::vector<OwnedPtr<Expr>> args)
{
    auto curFile = env->curFile;
    auto funcDecl = GetNewJavaObjectDecl();
    auto jclassInitFuncDecl = GetJClassDecl();
    auto parseSignatureFuncDecl = GetParseMethodSignatureDecl();
    auto callNestFuncDecl = GetCallNestDecl();
    auto methodIDFuncDecl = GetMethodIdConstr();
    if (!funcDecl || !jclassInitFuncDecl || !parseSignatureFuncDecl || !callNestFuncDecl || !methodIDFuncDecl) {
        return nullptr;
    }

    CJC_ASSERT(jclassInitFuncDecl->funcBody->paramLists[0] && jclassInitFuncDecl->funcBody->paramLists[0]->params[1]);
    CJC_ASSERT(callNestFuncDecl->funcBody->paramLists[0] && callNestFuncDecl->funcBody->paramLists[0]->params[0]);
    auto strTy = jclassInitFuncDecl->funcBody->paramLists[0]->params[1]->ty;
    auto intTy = callNestFuncDecl->funcBody->paramLists[0]->params[0]->ty;

    auto lclassName = CreateLitConstExpr(LitConstKind::STRING, classTypeSignature, strTy);
    lclassName->curFile = curFile;
    auto lConstructorSignature = CreateLitConstExpr(LitConstKind::STRING, constructorSignature, strTy);
    lConstructorSignature->curFile = curFile;
    auto lmethodName = CreateLitConstExpr(LitConstKind::STRING, JAVA_CONSTRUCTOR, strTy);
    lmethodName->curFile = curFile;
    auto jclass = CreateCall(jclassInitFuncDecl, curFile, CreateGetJniEnvCall(curFile), std::move(lclassName));
    auto methodSignature = CreateCall(parseSignatureFuncDecl, curFile, std::move(lConstructorSignature));
    auto methodID = CreateCall(methodIDFuncDecl, curFile, CreateGetJniEnvCall(curFile), ASTCloner::Clone(jclass.get()),
        std::move(lmethodName), std::move(methodSignature));

    auto arrayStruct = importManager.GetCoreDecl<StructDecl>(STD_LIB_ARRAY);
    if (!arrayStruct) {
        return nullptr;
    }
    std::vector<Ptr<Ty>> arrParamsTy;
    arrParamsTy.push_back(GetJavaEntityTy());
    auto arrayTy = typeManager.GetStructTy(*arrayStruct, arrParamsTy);
    auto argsSize = CreateLitConstExpr(LitConstKind::INTEGER, std::to_string(args.size()), intTy);
    auto argsAsArray = WithinFile(CreateArrayLit(std::move(args), arrayTy), curFile);
    auto callNest = CreateCall(callNestFuncDecl, curFile, std::move(argsSize));

    return CreateCall(funcDecl, curFile, std::move(env), std::move(jclass), std::move(methodID), std::move(argsAsArray),
        std::move(callNest));
}

OwnedPtr<Expr> InteropLibBridge::SelectJSigByTypeKind([[maybe_unused]] TypeKind kind, Ptr<Ty> ty)
{
    static auto strTy = utils.GetStringDecl().ty;
    return CreateLitConstExpr(LitConstKind::STRING, utils.GetJavaTypeSignature(*ty), strTy);
}

OwnedPtr<Expr> InteropLibBridge::SelectJPrimitiveNameByTypeKind(TypeKind kind, [[maybe_unused]] Ptr<Ty> ty)
{
    static auto javaEntityKindDecl = GetJavaEntityKindDecl();
    std::string typeName;
    switch (kind) {
        case TypeKind::TYPE_INT8:
            typeName = INTEROPLIB_JAVA_ENTITY_KIND_JBYTE;
            break;
        case TypeKind::TYPE_INT16:
            typeName = INTEROPLIB_JAVA_ENTITY_KIND_JSHORT;
            break;
        case TypeKind::TYPE_UINT16:
            typeName = INTEROPLIB_JAVA_ENTITY_KIND_JCHAR;
            break;
        case TypeKind::TYPE_INT32:
            typeName = INTEROPLIB_JAVA_ENTITY_KIND_JINT;
            break;
        case TypeKind::TYPE_INT64:
            typeName = INTEROPLIB_JAVA_ENTITY_KIND_JLONG;
            break;
        case TypeKind::TYPE_FLOAT32:
            typeName = INTEROPLIB_JAVA_ENTITY_KIND_JFLOAT;
            break;
        case TypeKind::TYPE_FLOAT64:
            typeName = INTEROPLIB_JAVA_ENTITY_KIND_JDOUBLE;
            break;
        case TypeKind::TYPE_BOOLEAN:
            typeName = INTEROPLIB_JAVA_ENTITY_KIND_JBOOLEAN;
            break;
        case TypeKind::TYPE_ENUM:
            typeName = INTEROPLIB_JAVA_ENTITY_KIND_JOBJECT;
            break;
        default:
            CJC_ABORT();
            break;
    }
    return CreateRefExpr(*LookupEnumMember(javaEntityKindDecl, typeName));
}

OwnedPtr<Expr> InteropLibBridge::SelectEntityWrapperByTypeKind([[maybe_unused]] TypeKind kind,
    [[maybe_unused]] Ptr<Ty> ty, Ptr<FuncParam> param, Ptr<File> file)
{
    auto paramRef = CreateRefExpr(*param);
    paramRef->curFile = file;
    return WrapJavaEntity(CreateMatchWithTypeCast(std::move(paramRef), ty));
}

OwnedPtr<Expr> InteropLibBridge::SelectEntityUnwrapperByTypeKind([[maybe_unused]] TypeKind kind,
    [[maybe_unused]] Ptr<Ty> ty, Ptr<Expr> entity, const ClassLikeDecl& mirror)
{
    auto cEntity = ASTCloner::Clone(entity);
    cEntity->curFile = entity->curFile;
    return CreateMatchWithTypeCast(UnwrapJavaEntity(std::move(cEntity), ty, mirror), ty);
}

std::map<std::string, OwnedPtr<Expr>> InteropLibBridge::GenerateTypeMappingWithSelector(
    std::function<OwnedPtr<Expr>(TypeKind, Ptr<Ty>)> selector)
{
    std::map<std::string, OwnedPtr<Expr>> typeMapping;
    for (auto& type : supportedArrayPrimitiveElementType) {
        auto ty = TypeManager::GetPrimitiveTy(type);
        typeMapping.insert(std::make_pair(Ty::ToString(ty), selector(type, ty)));
    }
    return typeMapping;
}

OwnedPtr<CallExpr> InteropLibBridge::CreateCFFIArrayLengthGetCall(OwnedPtr<Expr> javarefExpr, Ptr<File> curFile)
{
    return CreateCall(GetJavaArrayGetLengthDecl(), curFile, CreateGetJniEnvCall(curFile), std::move(javarefExpr));
}

OwnedPtr<CallExpr> InteropLibBridge::CreateCFFINewJavaArrayCall(
    OwnedPtr<Expr> jniEnv, FuncParamList& params, const Ptr<GenericParamDecl> genericParam)
{
    auto curFile = genericParam->curFile;
    static auto funcDecl = GetNewJavaArrayDecl();
    if (!funcDecl) {
        return nullptr;
    }

    static auto strTy = utils.GetStringDecl().ty;
    auto defaultTypeOption =
        CreateLitConstExpr(LitConstKind::STRING, utils.GetJavaTypeSignature(*utils.GetJObjectDecl()->ty), strTy);
    defaultTypeOption->curFile = curFile;
    auto typeMatch = CreateMatchByTypeArgument(genericParam,
        GenerateTypeMappingWithSelector([this](TypeKind kind, Ptr<Ty> ty) { return SelectJSigByTypeKind(kind, ty); }),
        strTy, std::move(defaultTypeOption));
    auto sizeParam = WithinFile(CreateRefExpr(*params.params[0]), curFile);

    return CreateCall(funcDecl, curFile, std::move(jniEnv), std::move(typeMatch), std::move(sizeParam));
}

OwnedPtr<CallExpr> InteropLibBridge::CreateCFFINewJavaCFFINewJavaProxyObjectForCJMappingCall(
    OwnedPtr<Expr> jniEnv, OwnedPtr<Expr> entity, std::string name, bool withMarkerParam)
{
    auto curFile = entity->curFile;
    static auto funcDecl = GetNewJavaProxyObjectForCJMappingDecl();
    if (!funcDecl) {
        return nullptr;
    }
    static auto retTy = utils.GetStringDecl().ty;
    auto expr = CreateLitConstExpr(LitConstKind::STRING, name, retTy);
    std::string hasArg = withMarkerParam ? "true" : "false";
    static const auto BOOL_TY = TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN);
    auto literal = CreateLitConstExpr(LitConstKind::BOOL, hasArg, BOOL_TY);
    return CreateCall(funcDecl, curFile, std::move(jniEnv), std::move(entity), std::move(expr), std::move(literal));
}

Ptr<FuncDecl> InteropLibBridge::FindArrayJavaEntityGetDecl(ClassDecl& jArrayDecl) const
{
    for (auto& member : jArrayDecl.body->decls) {
        if (member->identifier == JAVA_ARRAY_GET_FOR_REF_TYPES) {
            return As<ASTKind::FUNC_DECL>(member);
        }
    }
    return nullptr;
}

Ptr<FuncDecl> InteropLibBridge::FindArrayJavaEntitySetDecl(ClassDecl& jArrayDecl) const
{
    for (auto& member : jArrayDecl.body->decls) {
        if (member->identifier == JAVA_ARRAY_SET_FOR_REF_TYPES) {
            return As<ASTKind::FUNC_DECL>(member);
        }
    }
    return nullptr;
}

OwnedPtr<CallExpr> InteropLibBridge::CreateCFFINewJavaObjectCall(
    OwnedPtr<Expr> jniEnv, std::string classTypeSignature, FuncParamList& params, bool isMirror, File& curFile)
{
    static auto markerClassDecl = CreateConstructorMarkerClassDecl();
    std::vector<OwnedPtr<Expr>> cffiCtorFuncArgs;

    for (auto& param : params.params) {
        cffiCtorFuncArgs.push_back(WrapJavaEntity(WithinFile(CreateRefExpr(*param), Ptr(&curFile))));
    }

    std::vector<Ptr<Ty>> paramTys = Native::FFI::GetParamTys(params);

    if (!isMirror) {
        // in java, the constructor with additional fake null argument
        // of special marker types is created.
        cffiCtorFuncArgs.push_back(CreateJavaEntityNullCall(&curFile));
        paramTys.push_back(typeManager.GetClassTy(*markerClassDecl, {}));
    }

    return CreateNewJavaObjectCall(std::move(jniEnv), classTypeSignature,
        utils.GetJavaTypeSignature(*TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT), paramTys),
        std::move(cffiCtorFuncArgs));
}

OwnedPtr<CallExpr> InteropLibBridge::CreateNewGlobalRefCall(OwnedPtr<Expr> env, OwnedPtr<Expr> obj, bool isWeak)
{
    auto curFile = obj->curFile;
    auto isWeakBoolValue = CreateBoolLit(isWeak);
    isWeakBoolValue->curFile = curFile;
    isWeakBoolValue->ty = TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN);

    return CreateCall(GetNewGlobalRefDecl(), curFile, std::move(env), std::move(obj), std::move(isWeakBoolValue));
}

OwnedPtr<CallExpr> InteropLibBridge::CreateDeleteGlobalRefCall(OwnedPtr<Expr> env, OwnedPtr<Expr> obj)
{
    auto curFile = obj->curFile;
    return CreateCall(GetDeleteGlobalRefDecl(), curFile, std::move(env), std::move(obj));
}

OwnedPtr<CallExpr> InteropLibBridge::CreateCallMethodCall(OwnedPtr<Expr> env, OwnedPtr<Expr> obj,
    const MemberJNISignature& signature, std::vector<OwnedPtr<Expr>> args, File& curFile, bool virt)
{
    auto pcurFile = Ptr(&curFile);
    auto funcDecl = virt ? GetCallMethodDecl() : GetNonVirtualCallMethodDecl();
    auto jclassInitFuncDecl = GetJClassDecl();
    auto parseSignatureFuncDecl = GetParseMethodSignatureDecl();
    auto callNestFuncDecl = GetCallNestDecl();
    auto methodIDFuncDecl = GetMethodIdConstr();
    if (!funcDecl || !jclassInitFuncDecl || !parseSignatureFuncDecl || !callNestFuncDecl || !methodIDFuncDecl) {
        return nullptr;
    }

    CJC_ASSERT(jclassInitFuncDecl->funcBody->paramLists[0] && jclassInitFuncDecl->funcBody->paramLists[0]->params[1]);
    CJC_ASSERT(callNestFuncDecl->funcBody->paramLists[0] && callNestFuncDecl->funcBody->paramLists[0]->params[0]);
    auto strTy = jclassInitFuncDecl->funcBody->paramLists[0]->params[1]->ty;
    auto intTy = callNestFuncDecl->funcBody->paramLists[0]->params[0]->ty;

    auto lclassName = CreateLitConstExpr(LitConstKind::STRING, signature.classTypeSignature, strTy);
    lclassName->curFile = pcurFile;
    auto lmethodName = CreateLitConstExpr(LitConstKind::STRING, signature.name, strTy);
    lmethodName->curFile = pcurFile;
    auto lsignature = CreateLitConstExpr(LitConstKind::STRING, signature.signature, strTy);
    lsignature->curFile = pcurFile;
    auto jclass = CreateCall(jclassInitFuncDecl, pcurFile, CreateGetJniEnvCall(pcurFile), std::move(lclassName));
    auto methodSignature = CreateCall(parseSignatureFuncDecl, pcurFile, std::move(lsignature));
    auto methodID = CreateCall(methodIDFuncDecl, pcurFile, CreateGetJniEnvCall(pcurFile), std::move(jclass),
        std::move(lmethodName), std::move(methodSignature));

    auto arrayStruct = importManager.GetCoreDecl<StructDecl>(STD_LIB_ARRAY);
    if (!arrayStruct) {
        return nullptr;
    }
    auto entityTy = GetJavaEntityTy();
    if (!entityTy) {
        return nullptr;
    }

    std::vector<Ptr<Ty>> arrParamsTy;
    arrParamsTy.push_back(entityTy);
    auto arrayTy = typeManager.GetStructTy(*arrayStruct, arrParamsTy);
    auto argsSize = CreateLitConstExpr(LitConstKind::INTEGER, std::to_string(args.size()), intTy);
    auto callNest = CreateCall(callNestFuncDecl, pcurFile, std::move(argsSize));

    return CreateCall(funcDecl, pcurFile, std::move(env), std::move(obj), std::move(methodID),
        CreateArrayLit(std::move(args), arrayTy), std::move(callNest));
}

OwnedPtr<CallExpr> InteropLibBridge::CreateCallStaticMethodCall(
    OwnedPtr<Expr> env, const MemberJNISignature& signature, std::vector<OwnedPtr<Expr>> args, File& curFile)
{
    auto pcurFile = Ptr(&curFile);
    auto funcDecl = GetCallStaticMethodDecl();
    auto jclassInitFuncDecl = GetJClassDecl();
    auto parseSignatureFuncDecl = GetParseMethodSignatureDecl();
    auto callNestFuncDecl = GetCallNestDecl();
    auto methodIDFuncDecl = GetMethodIdConstrStatic();
    if (!funcDecl || !jclassInitFuncDecl || !parseSignatureFuncDecl || !callNestFuncDecl || !methodIDFuncDecl) {
        return nullptr;
    }

    CJC_ASSERT(jclassInitFuncDecl->funcBody->paramLists[0] && jclassInitFuncDecl->funcBody->paramLists[0]->params[1]);
    CJC_ASSERT(callNestFuncDecl->funcBody->paramLists[0] && callNestFuncDecl->funcBody->paramLists[0]->params[0]);
    auto strTy = jclassInitFuncDecl->funcBody->paramLists[0]->params[1]->ty;
    auto intTy = callNestFuncDecl->funcBody->paramLists[0]->params[0]->ty;

    auto lclassName = CreateLitConstExpr(LitConstKind::STRING, signature.classTypeSignature, strTy);
    lclassName->curFile = pcurFile;
    auto lmethodName = CreateLitConstExpr(LitConstKind::STRING, signature.name, strTy);
    lmethodName->curFile = pcurFile;
    auto lsignature = CreateLitConstExpr(LitConstKind::STRING, signature.signature, strTy);
    lsignature->curFile = pcurFile;
    auto jclass = CreateCall(jclassInitFuncDecl, pcurFile, CreateGetJniEnvCall(pcurFile), std::move(lclassName));
    auto methodSignature = CreateCall(parseSignatureFuncDecl, pcurFile, std::move(lsignature));
    auto methodID = CreateCall(methodIDFuncDecl, pcurFile, CreateGetJniEnvCall(pcurFile),
        ASTCloner::Clone(jclass.get()), std::move(lmethodName), std::move(methodSignature));
    auto arrayStruct = importManager.GetCoreDecl<StructDecl>(STD_LIB_ARRAY);
    if (!arrayStruct) {
        return nullptr;
    }
    auto entityTy = GetJavaEntityTy();
    if (!entityTy) {
        return nullptr;
    }

    std::vector<Ptr<Ty>> arrParamsTy;
    arrParamsTy.push_back(entityTy);
    auto arrayTy = typeManager.GetStructTy(*arrayStruct, arrParamsTy);
    auto argsSize = CreateLitConstExpr(LitConstKind::INTEGER, std::to_string(args.size()), intTy);
    auto callNest = CreateCall(callNestFuncDecl, pcurFile, std::move(argsSize));

    return CreateCall(funcDecl, pcurFile, std::move(env), std::move(jclass), std::move(methodID),
        CreateArrayLit(std::move(args), arrayTy), std::move(callNest));
}

OwnedPtr<MatchExpr> InteropLibBridge::CreateMatchWithTypeCast(OwnedPtr<Expr> exprToCast, Ptr<Ty> castTy)
{
    static auto exceptionDecl = importManager.GetCoreDecl<ClassDecl>(CLASS_EXCEPTION);
    if (!exceptionDecl) {
        return nullptr;
    }
    auto castType = MakeOwned<Type>();
    castType->ty = castTy;
    auto varPattern = CreateVarPattern(V_COMPILER, castType->ty);
    auto curFile = exprToCast->curFile;
    CJC_NULLPTR_CHECK(curFile);
    varPattern->curFile = curFile;
    varPattern->varDecl->curFile = curFile;
    auto varPatternRef = WithinFile(CreateRefExpr(*(varPattern->varDecl)), curFile);

    std::vector<OwnedPtr<MatchCase>> matchCases;

    auto typePattern = CreateTypePattern(std::move(varPattern), std::move(castType), *exprToCast);
    typePattern->matchBeforeRuntime = false;

    std::vector<OwnedPtr<Expr>> exceptionCallArgs;
    exceptionCallArgs.emplace_back(CreateLitConstExpr(LitConstKind::STRING,
        "internal error: CreateMatchWithTypeCast(" + Ty::KindName(exprToCast->ty->kind) + " -> " +
            Ty::KindName(castTy->kind) + ")",
        utils.GetStringDecl().ty));
    exceptionCallArgs.back()->curFile = curFile;

    matchCases.emplace_back(CreateMatchCase(std::move(typePattern), std::move(varPatternRef)));
    matchCases[0]->curFile = curFile;
    matchCases.emplace_back(CreateMatchCase(MakeOwned<WildcardPattern>(),
        CreateThrowException(*exceptionDecl, std::move(exceptionCallArgs), *exprToCast->curFile, typeManager)));
    matchCases[1]->curFile = curFile;

    return WithinFile(CreateMatchExpr(std::move(exprToCast), std::move(matchCases), castTy), curFile);
}

OwnedPtr<Expr> InteropLibBridge::CreateCFFICallArrayMethodCall(OwnedPtr<Expr> jniEnv, OwnedPtr<Expr> obj,
    FuncParamList& params, const Ptr<GenericParamDecl> genericParam, ArrayOperationKind kind)
{
    CJC_ASSERT(kind == ArrayOperationKind::GET || kind == ArrayOperationKind::SET);

    static auto javaEntityKindDecl = GetJavaEntityKindDecl();
    static auto jObject = utils.GetJObjectDecl();
    static auto javaRef = GetJavaRefField(*jObject);
    CJC_ASSERT(javaRef);
    auto curFile = genericParam->curFile;

    auto funcDecl = kind == ArrayOperationKind::GET ? GetJavaArrayGetDecl() : GetJavaArraySetDecl();
    auto indexArg = params.params[0].get();
    auto matchWithJPrimitive = CreateMatchByTypeArgument(genericParam,
        GenerateTypeMappingWithSelector(
            [this](TypeKind kind, Ptr<Ty> ty) { return SelectJPrimitiveNameByTypeKind(kind, ty); }),
        javaEntityKindDecl->ty, WithinFile(CreateRefExpr(GetJavaEntityKindJObject()), curFile));

    if (kind == ArrayOperationKind::GET) {
        return CreateCall(funcDecl, curFile, std::move(jniEnv), std::move(matchWithJPrimitive), std::move(obj),
            WithinFile(CreateRefExpr(*indexArg), curFile));
    }

    auto valueArg = params.params[1].get();
    auto paramRef = WithinFile(CreateRefExpr(*valueArg), curFile);

    OwnedPtr<Expr> valueEntity;
    if (valueArg->ty->name == INTEROPLIB_CFFI_JAVA_ENTITY) {
        // for generated function - value argument was replaced with java entity
        valueEntity = std::move(paramRef);
    } else {
        valueEntity = CreateMatchByTypeArgument(genericParam,
            GenerateTypeMappingWithSelector([this, &valueArg, &curFile](TypeKind kind, Ptr<Ty> ty) {
                return SelectEntityWrapperByTypeKind(kind, ty, valueArg, curFile);
            }),
            GetJavaEntityDecl()->ty,
            CreateMemberAccess(CreateMatchWithTypeCast(std::move(paramRef), jObject->ty), *javaRef));
    }

    return CreateCall(funcDecl, curFile, std::move(jniEnv), std::move(matchWithJPrimitive), std::move(obj),
        WithinFile(CreateRefExpr(*indexArg), curFile), std::move(valueEntity));
}

OwnedPtr<CallExpr> InteropLibBridge::CreateCFFICallMethodCall(OwnedPtr<Expr> jniEnv, OwnedPtr<Expr> obj,
    const MemberJNISignature& signature, FuncParamList& params, File& curFile)
{
    std::vector<OwnedPtr<Expr>> cffiMethodArgs;
    CJC_NULLPTR_CHECK(obj->curFile);
    for (auto& param : params.params) {
        auto paramRef = WithinFile(CreateRefExpr(*param), Ptr(&curFile));
        paramRef->curFile = obj->curFile;
        cffiMethodArgs.push_back(WrapJavaEntity(std::move(paramRef)));
    }

    return CreateCallMethodCall(std::move(jniEnv), std::move(obj), signature, std::move(cffiMethodArgs), curFile);
}

OwnedPtr<CallExpr> InteropLibBridge::CreateCFFICallStaticMethodCall(
    OwnedPtr<Expr> jniEnv, const MemberJNISignature& signature, FuncParamList& params, File& curFile)
{
    std::vector<OwnedPtr<Expr>> cffiMethodArgs;

    for (auto& param : params.params) {
        cffiMethodArgs.push_back(WrapJavaEntity(WithinFile(CreateRefExpr(*param), Ptr(&curFile))));
    }

    return CreateCallStaticMethodCall(std::move(jniEnv), signature, std::move(cffiMethodArgs), curFile);
}

OwnedPtr<CallExpr> InteropLibBridge::CreateDeleteCJObjectCall(
    OwnedPtr<Expr> env, OwnedPtr<Expr> self, OwnedPtr<Expr> getWeakRef, Ptr<Ty> cjTy)
{
    auto curFile = self->curFile;
    auto funcDecl = GetDeleteCJObjectDecl();
    if (!funcDecl) {
        return nullptr;
    }

    std::vector<OwnedPtr<FuncArg>> callArgs;
    callArgs.push_back(CreateFuncArg(std::move(env)));
    callArgs.push_back(CreateFuncArg(std::move(self)));
    callArgs.push_back(CreateFuncArg(std::move(getWeakRef)));

    auto unitTy = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
    auto fdRef = WithinFile(CreateRefExpr(*funcDecl), curFile);
    fdRef->instTys.push_back(cjTy);
    fdRef->ty = typeManager.GetInstantiatedTy(funcDecl->ty, GenerateTypeMapping(*funcDecl, fdRef->instTys));
    return CreateCallExpr(std::move(fdRef), std::move(callArgs), funcDecl, unitTy, CallKind::CALL_DECLARED_FUNCTION);
}

OwnedPtr<CallExpr> InteropLibBridge::CreateRemoveFromRegistryCall(OwnedPtr<Expr> self)
{
    auto curFile = self->curFile;
    return CreateCall(GetRemoveFromRegistryDecl(), curFile, std::move(self));
}

OwnedPtr<CallExpr> InteropLibBridge::CreatePutToRegistryCall(OwnedPtr<Expr> obj)
{
    auto curFile = obj->curFile;
    return CreateCall(GetPutToRegistryDecl(), curFile, std::move(obj));
}

OwnedPtr<CallExpr> InteropLibBridge::CreatePutToRegistrySelfInitCall(
    OwnedPtr<Expr> env, OwnedPtr<Expr> entity, OwnedPtr<Expr> obj)
{
    auto curFile = entity->curFile;
    return CreateCall(GetPutToRegistrySelfInitDecl(), curFile, std::move(env), std::move(entity), std::move(obj));
}

OwnedPtr<CallExpr> InteropLibBridge::CreateGetFromRegistryByEntityCall(
    OwnedPtr<Expr> env, OwnedPtr<Expr> obj, Ptr<Ty> ty, bool retAsOption)
{
    auto curFile = obj->curFile;
    auto funcDecl = retAsOption ? GetGetFromRegistryByEntityOptionDecl() : GetGetFromRegistryByEntityDecl();
    if (!funcDecl) {
        return nullptr;
    }

    std::vector<OwnedPtr<FuncArg>> callArgs;
    callArgs.push_back(CreateFuncArg(std::move(env)));
    callArgs.push_back(CreateFuncArg(std::move(obj)));

    auto fdRef = WithinFile(CreateRefExpr(*funcDecl), curFile);
    fdRef->instTys.push_back(ty);
    fdRef->ty = typeManager.GetInstantiatedTy(funcDecl->ty, GenerateTypeMapping(*funcDecl, fdRef->instTys));
    auto retTy = retAsOption ? utils.GetOptionTy(ty) : ty;
    return CreateCallExpr(std::move(fdRef), std::move(callArgs), funcDecl, retTy, CallKind::CALL_DECLARED_FUNCTION);
}

OwnedPtr<CallExpr> InteropLibBridge::CreateJavaStringToCangjieCall(OwnedPtr<Expr> env, OwnedPtr<Expr> jstring)
{
    auto curFile = jstring->curFile;
    auto funcDecl = GetJavaStringToCangjie();
    if (!funcDecl) {
        return nullptr;
    }

    return CreateCall(funcDecl, curFile, std::move(env), std::move(jstring));
}

OwnedPtr<CallExpr> InteropLibBridge::CreateGetFromRegistryCall(OwnedPtr<Expr> env, OwnedPtr<Expr> self, Ptr<Ty> ty)
{
    auto curFile = self->curFile;
    auto funcDecl = GetGetFromRegistryDecl();
    if (!funcDecl) {
        return nullptr;
    }

    std::vector<OwnedPtr<FuncArg>> callArgs;
    callArgs.push_back(CreateFuncArg(std::move(env)));
    callArgs.push_back(CreateFuncArg(std::move(self)));

    auto fdRef = WithinFile(CreateRefExpr(*funcDecl), curFile);
    fdRef->instTys.push_back(ty);
    fdRef->ty = typeManager.GetInstantiatedTy(funcDecl->ty, GenerateTypeMapping(*funcDecl, fdRef->instTys));
    return CreateCallExpr(std::move(fdRef), std::move(callArgs), funcDecl, ty, CallKind::CALL_DECLARED_FUNCTION);
}

OwnedPtr<CallExpr> InteropLibBridge::CreateGetFromRegistryOptionCall(OwnedPtr<Expr> self, Ptr<Ty> ty)
{
    auto curFile = self->curFile;
    CJC_ASSERT(ty->IsCoreOptionType());
    auto funcDecl = GetGetFromRegistryOptionDecl();
    if (!funcDecl) {
        return nullptr;
    }

    std::vector<OwnedPtr<FuncArg>> callArgs;
    callArgs.push_back(CreateFuncArg(std::move(self)));

    auto fdRef = WithinFile(CreateRefExpr(*funcDecl), curFile);
    fdRef->instTys.push_back(ty->typeArgs[0]);
    fdRef->ty = typeManager.GetInstantiatedTy(funcDecl->ty, GenerateTypeMapping(*funcDecl, fdRef->instTys));
    return CreateCallExpr(std::move(fdRef), std::move(callArgs), funcDecl, ty, CallKind::CALL_DECLARED_FUNCTION);
}

OwnedPtr<CallExpr> InteropLibBridge::CreateGetFieldCall(OwnedPtr<Expr> env, OwnedPtr<Expr> obj,
    std::string typeSignature, std::string fieldName, std::string fieldSignature)
{
    auto curFile = obj->curFile;
    auto funcDecl = GetGetFieldDecl();
    auto jclassInitFuncDecl = GetJClassDecl();
    auto parseSignatureFuncDecl = GetParseComponentSignatureDecl();
    auto fieldIDFuncDecl = GetFieldIdConstr();
    if (!funcDecl || !jclassInitFuncDecl || !parseSignatureFuncDecl || !fieldIDFuncDecl) {
        return nullptr;
    }

    CJC_ASSERT(jclassInitFuncDecl->funcBody->paramLists[0] && jclassInitFuncDecl->funcBody->paramLists[0]->params[1]);
    auto strTy = jclassInitFuncDecl->funcBody->paramLists[0]->params[1]->ty;

    auto tsigExpr = CreateLitConstExpr(LitConstKind::STRING, typeSignature, strTy);
    auto fieldNameExpr = CreateLitConstExpr(LitConstKind::STRING, fieldName, strTy);
    auto fsigExpr = CreateLitConstExpr(LitConstKind::STRING, fieldSignature, strTy);

    auto jclass = CreateCall(jclassInitFuncDecl, curFile, CreateGetJniEnvCall(curFile), std::move(tsigExpr));
    auto filedSignature = CreateCall(parseSignatureFuncDecl, curFile, std::move(fsigExpr));
    auto fieldID = CreateCall(fieldIDFuncDecl, curFile, CreateGetJniEnvCall(curFile), std::move(jclass),
        std::move(fieldNameExpr), std::move(filedSignature));

    return CreateCall(funcDecl, curFile, std::move(env), std::move(obj), std::move(fieldID));
}

OwnedPtr<CallExpr> InteropLibBridge::CreateGetStaticFieldCall(
    OwnedPtr<Expr> env, std::string typeSignature, std::string fieldName, std::string fieldSignature)
{
    auto curFile = env->curFile;
    auto funcDecl = GetGetStaticFieldDecl();
    auto jclassInitFuncDecl = GetJClassDecl();
    auto parseSignatureFuncDecl = GetParseComponentSignatureDecl();
    auto fieldIDFuncDecl = GetFieldIdConstrStatic();
    if (!funcDecl || !jclassInitFuncDecl || !parseSignatureFuncDecl || !fieldIDFuncDecl) {
        return nullptr;
    }

    auto entityTy = GetJavaEntityTy();
    if (!entityTy) {
        return nullptr;
    }

    CJC_ASSERT(jclassInitFuncDecl->funcBody->paramLists[0] && jclassInitFuncDecl->funcBody->paramLists[0]->params[1]);
    auto strTy = jclassInitFuncDecl->funcBody->paramLists[0]->params[1]->ty;

    auto tsigExpr = CreateLitConstExpr(LitConstKind::STRING, typeSignature, strTy);
    auto fieldNameExpr = CreateLitConstExpr(LitConstKind::STRING, fieldName, strTy);
    auto fsigExpr = CreateLitConstExpr(LitConstKind::STRING, fieldSignature, strTy);

    auto jclass = CreateCall(jclassInitFuncDecl, curFile, CreateGetJniEnvCall(curFile), std::move(tsigExpr));
    auto filedSignature = CreateCall(parseSignatureFuncDecl, curFile, std::move(fsigExpr));
    auto fieldID = CreateCall(fieldIDFuncDecl, curFile, CreateGetJniEnvCall(curFile), ASTCloner::Clone(jclass.get()),
        std::move(fieldNameExpr), std::move(filedSignature));

    return CreateCall(funcDecl, curFile, std::move(env), std::move(jclass), std::move(fieldID));
}

OwnedPtr<CallExpr> InteropLibBridge::CreateSetFieldCall(
    OwnedPtr<Expr> env, OwnedPtr<Expr> obj, const MemberJNISignature& signature, OwnedPtr<Expr> value)
{
    auto curFile = value->curFile;
    auto funcDecl = GetSetFieldDecl();
    auto jclassInitFuncDecl = GetJClassDecl();
    auto parseSignatureFuncDecl = GetParseComponentSignatureDecl();
    auto fieldIDFuncDecl = GetFieldIdConstr();
    if (!funcDecl || !jclassInitFuncDecl || !parseSignatureFuncDecl || !fieldIDFuncDecl) {
        return nullptr;
    }

    CJC_ASSERT(jclassInitFuncDecl->funcBody->paramLists[0] && jclassInitFuncDecl->funcBody->paramLists[0]->params[1]);
    auto strTy = jclassInitFuncDecl->funcBody->paramLists[0]->params[1]->ty;

    auto tsigExpr = CreateLitConstExpr(LitConstKind::STRING, signature.classTypeSignature, strTy);
    tsigExpr->curFile = curFile;
    auto fieldNameExpr = CreateLitConstExpr(LitConstKind::STRING, signature.name, strTy);
    fieldNameExpr->curFile = curFile;
    auto fsigExpr = CreateLitConstExpr(LitConstKind::STRING, signature.signature, strTy);
    fsigExpr->curFile = curFile;

    auto jclass = CreateCall(jclassInitFuncDecl, curFile, CreateGetJniEnvCall(curFile), std::move(tsigExpr));
    auto filedSignature = CreateCall(parseSignatureFuncDecl, curFile, std::move(fsigExpr));
    auto fieldID = CreateCall(fieldIDFuncDecl, curFile, CreateGetJniEnvCall(curFile), std::move(jclass),
        std::move(fieldNameExpr), std::move(filedSignature));

    return CreateCall(
        funcDecl, curFile, std::move(env), std::move(obj), std::move(fieldID), WrapJavaEntity(std::move(value)));
}

OwnedPtr<CallExpr> InteropLibBridge::CreateSetStaticFieldCall(OwnedPtr<Expr> env, std::string typeSignature,
    std::string fieldName, std::string fieldSignature, OwnedPtr<Expr> value)
{
    auto curFile = value->curFile;
    auto funcDecl = GetSetStaticFieldDecl();
    auto jclassInitFuncDecl = GetJClassDecl();
    auto parseSignatureFuncDecl = GetParseComponentSignatureDecl();
    auto fieldIDFuncDecl = GetFieldIdConstrStatic();
    if (!funcDecl || !jclassInitFuncDecl || !parseSignatureFuncDecl || !fieldIDFuncDecl) {
        return nullptr;
    }

    CJC_ASSERT(jclassInitFuncDecl->funcBody->paramLists[0] && jclassInitFuncDecl->funcBody->paramLists[0]->params[1]);
    auto strTy = jclassInitFuncDecl->funcBody->paramLists[0]->params[1]->ty;

    auto tsigExpr = CreateLitConstExpr(LitConstKind::STRING, typeSignature, strTy);
    tsigExpr->curFile = curFile;
    auto fieldNameExpr = CreateLitConstExpr(LitConstKind::STRING, fieldName, strTy);
    fieldNameExpr->curFile = curFile;
    auto fsigExpr = CreateLitConstExpr(LitConstKind::STRING, fieldSignature, strTy);
    fsigExpr->curFile = curFile;

    auto jclass = CreateCall(jclassInitFuncDecl, curFile, CreateGetJniEnvCall(curFile), std::move(tsigExpr));
    auto filedSignature = CreateCall(parseSignatureFuncDecl, curFile, std::move(fsigExpr));
    auto fieldID = CreateCall(fieldIDFuncDecl, curFile, CreateGetJniEnvCall(curFile), ASTCloner::Clone(jclass.get()),
        std::move(fieldNameExpr), std::move(filedSignature));
    return CreateCall(
        funcDecl, curFile, std::move(env), std::move(jclass), std::move(fieldID), WrapJavaEntity(std::move(value)));
}

OwnedPtr<Expr> InteropLibBridge::WrapExceptionHandling(OwnedPtr<Expr> env, OwnedPtr<LambdaExpr> action)
{
    auto curFile = env->curFile;
    auto retTy = action->funcBody->ty;
    auto fd = GetWithExceptionHandlingDecl();
    auto fdRef = WithinFile(CreateRefExpr(*fd), curFile);
    fdRef->instTys.push_back(retTy);
    fdRef->ty = typeManager.GetInstantiatedTy(fd->ty, GenerateTypeMapping(*fd, fdRef->instTys));

    std::vector<OwnedPtr<FuncArg>> args;
    args.emplace_back(CreateFuncArg(std::move(env)));
    args.emplace_back(CreateFuncArg(std::move(action)));
    return CreateCallExpr(std::move(fdRef), std::move(args), nullptr, retTy, CallKind::CALL_DECLARED_FUNCTION);
}

namespace {

Ptr<PropDecl> GetJavaEntityIsNullCall(StructDecl& entityDecl)
{
    for (auto& member : entityDecl.GetMemberDecls()) {
        if (auto isNullField = As<ASTKind::PROP_DECL>(member.get())) {
            if (isNullField->identifier == INTEROPLIB_CFFI_JAVA_ENTITY_IS_NULL_ID) {
                return isNullField;
            }
        }
    }

    return nullptr;
}

[[maybe_unused]] OwnedPtr<Expr> CreateJavaEntityIsNullExpr(OwnedPtr<Expr> entity, Ptr<StructDecl> entityDecl)
{
    auto isNullProp = GetJavaEntityIsNullCall(*entityDecl);
    if (!isNullProp) {
        return nullptr;
    }
    auto access = CreateMemberAccess(std::move(entity), *isNullProp->getters[0]);
    CopyBasicInfo(access->baseExpr.get(), access.get());
    auto call =
        CreateCallExpr(std::move(access), {}, isNullProp->getters[0], isNullProp->ty, CallKind::CALL_DECLARED_FUNCTION);
    CopyBasicInfo(call->baseFunc.get(), call.get());
    return call;
}

} // namespace

OwnedPtr<Expr> InteropLibBridge::UnwrapJavaMirrorOption(
    OwnedPtr<Expr> entity, Ptr<Ty> ty, const ClassLikeDecl& mirror, bool toRaw)
{
    CJC_ASSERT(ty->IsCoreOptionType());
    auto curFile = entity->curFile;
    CJC_NULLPTR_CHECK(curFile);

    auto declTy = ty->typeArgs[0];
    auto decl = Ty::GetDeclOfTy(declTy);
    CJC_ASSERT(IsMirror(*decl) || declTy->IsString() || (toRaw && IsImpl(*decl)));

    auto actualTy = toRaw ? GetJobjectTy() : ty;

    return utils.CreateOptionMatch(
        CreateGetJavaEntityOrNullCall(std::move(entity)),
        [this, curFile, declTy, &mirror, toRaw](VarDecl& e) {
            auto unwrapped = UnwrapJavaEntity(WithinFile(CreateRefExpr(e), curFile), declTy, mirror, toRaw);
            return unwrapped;
        },
        [this, ty, toRaw]() { return toRaw ? CreateJobjectNull() : utils.CreateOptionNoneRef(ty->typeArgs[0]); },
        actualTy);
}

OwnedPtr<Expr> InteropLibBridge::UnwrapJavaImplOption(
    OwnedPtr<Expr> env, OwnedPtr<Expr> entityOption, Ptr<Ty> ty, const ClassLikeDecl& mirror, bool toRaw)
{
    auto actualTy = ty->typeArgs[0];
    auto regCall = CreateGetFromRegistryByEntityCall(std::move(env), std::move(entityOption), actualTy, true);
    if (!toRaw) {
        return regCall;
    }

    return UnwrapJavaMirrorOption(WrapJavaEntity(std::move(regCall)), ty, mirror, toRaw);
}

OwnedPtr<Expr> InteropLibBridge::CreateEnsureNotNullCall(OwnedPtr<Expr> entity)
{
    auto curFile = entity->curFile;
    return CreateCall(GetEnsureNotNullDecl(), curFile, std::move(entity));
}

OwnedPtr<Expr> InteropLibBridge::CreateGetJavaEntityOrNullCall(OwnedPtr<Expr> entity)
{
    auto curFile = entity->curFile;
    return CreateCall(GetGetJavaEntityOrNullDecl(), curFile, std::move(entity));
}

OwnedPtr<Expr> InteropLibBridge::UnwrapJavaArrayEntity(OwnedPtr<Expr> entity, Ptr<Ty> ty, const ClassLikeDecl& mirror)
{
    auto isSetOperation = ty == TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
    if (isSetOperation) {
        return CreateUnitExpr();
    }

    auto castType = MakeOwned<Type>();
    castType->ty = ty;
    auto varPattern = CreateVarPattern(V_COMPILER, ty);
    auto curFile = entity->curFile;
    CJC_NULLPTR_CHECK(curFile);
    varPattern->curFile = curFile;
    varPattern->varDecl->curFile = curFile;
    auto varPatternRef = WithinFile(CreateRefExpr(*(varPattern->varDecl)), curFile);
    auto genericParam = mirror.generic->typeParameters[0].get();
    auto entityPtr = entity.get();

    return CreateMatchByTypeArgument(genericParam,
        GenerateTypeMappingWithSelector([this, &entityPtr, &mirror](TypeKind kind, Ptr<Ty> ty) {
            return SelectEntityUnwrapperByTypeKind(kind, ty, entityPtr, mirror);
        }),
        ty, CreateMatchWithTypeCast(std::move(entity), ty));
}

OwnedPtr<Expr> InteropLibBridge::UnwrapJavaPrimitiveEntity(OwnedPtr<Expr> entity, Ptr<Ty> ty)
{
    auto funcDecl = GetUnwrapJavaEntityDecl();
    if (!funcDecl) {
        return nullptr;
    }
    auto isPrimitive = ty->IsPrimitive();
    auto unitTy = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
    auto actualEntityTy = isPrimitive ? ty : typeManager.GetPointerTy(unitTy);
    auto curFile = entity->curFile;
    std::vector<OwnedPtr<FuncArg>> callArgs;
    callArgs.emplace_back(CreateFuncArg(std::move(entity)));
    auto fdRef = WithinFile(CreateRefExpr(*funcDecl), curFile);
    fdRef->instTys.push_back(actualEntityTy);
    fdRef->ty = typeManager.GetInstantiatedTy(funcDecl->ty, GenerateTypeMapping(*funcDecl, fdRef->instTys));
    auto callExpr = CreateCallExpr(
        std::move(fdRef), std::move(callArgs), funcDecl, actualEntityTy, CallKind::CALL_DECLARED_FUNCTION);
    callExpr->curFile = curFile;
    return std::move(callExpr);
}

OwnedPtr<Expr> InteropLibBridge::UnwrapJavaEntity(OwnedPtr<Expr> entity, Ptr<Ty> ty, const Decl& outerDecl, bool toRaw)
{
    auto curFile = entity->curFile;
    if (!entity || !ty) {
        return nullptr;
    }

    auto isPrimitive = ty->IsPrimitive();
    if (isPrimitive || (toRaw && !ty->IsCoreOptionType())) {
        return UnwrapJavaPrimitiveEntity(std::move(entity), ty);
    }

    if (IsJArray(outerDecl) && ty->IsGeneric()) {
        return UnwrapJavaArrayEntity(std::move(entity), ty, static_cast<const ClassLikeDecl&>(outerDecl));
    } else if (ty == GetJavaEntityTy()) {
        return std::move(entity);
    }

    if (ty->IsString()) {
        return CreateJavaStringToCangjieCall(CreateGetJniEnvCall(curFile), std::move(entity));
    }

    if (ty->IsCoreOptionType()) {
        auto classLikeDecl = DynamicCast<const ClassLikeDecl*>(&outerDecl);
        CJC_NULLPTR_CHECK(classLikeDecl);
        auto actualTy = ty->typeArgs[0];
        auto actualDecl = Ty::GetDeclOfTy(actualTy);
        if (IsMirror(*actualDecl) || actualTy->IsString()) {
            return UnwrapJavaMirrorOption(std::move(entity), ty, *classLikeDecl, toRaw);
        }

        return UnwrapJavaImplOption(CreateGetJniEnvCall(curFile), std::move(entity), ty, *classLikeDecl, toRaw);
    }

    auto classLikeTy = DynamicCast<ClassLikeTy*>(ty.get());
    if (!classLikeTy) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_java_interop_not_supported, DEFAULT_POSITION, "type " + ty->name);
        return nullptr;
    }

    auto decl = classLikeTy->commonDecl;
    if (!decl) {
        diag.DiagnoseRefactor(
            DiagKindRefactor::sema_java_interop_not_supported, DEFAULT_POSITION, "unknown decl " + ty->name);
        return nullptr;
    }

    if (decl->TestAttr(Attribute::JAVA_MIRROR)) {
        return CreateMirrorConstructorCall(importManager, std::move(entity), ty);
    } else {
        return CreateGetFromRegistryByEntityCall(CreateGetJniEnvCall(curFile), std::move(entity), ty, false);
    }
}

void InteropLibBridge::CheckInteropLibVersion()
{
    auto versionDecl = GetInteropLibVersionVarDecl();
    if (!versionDecl || !versionDecl->initializer || versionDecl->initializer->astKind != ASTKind::LIT_CONST_EXPR ||
        versionDecl->initializer->ty->kind != TypeKind::TYPE_INT64) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_java_interoplib_version_too_old, DEFAULT_POSITION,
            std::to_string(INTEROPLIB_VERSION));
        return;
    }

    auto libVersionLit = StaticAs<ASTKind::LIT_CONST_EXPR>(versionDecl->initializer.get());
    auto libVersion = std::stoi(libVersionLit->stringValue);
    if (libVersion != INTEROPLIB_VERSION) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_java_interoplib_version_mismatch, DEFAULT_POSITION,
            libVersionLit->stringValue, std::to_string(INTEROPLIB_VERSION));
    }
}

} // namespace Cangjie::Interop::Java
