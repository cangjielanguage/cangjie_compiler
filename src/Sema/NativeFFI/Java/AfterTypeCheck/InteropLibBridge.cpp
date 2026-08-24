// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "InteropLibBridge.h"
#include "Desugar/AfterTypeCheck.h"
#include "JavaDesugarManager.h"
#include "NativeFFI/Java/AfterTypeCheck/Utils.h"
#include "NativeFFI/Utils.h"
#include "TypeCheckUtil.h"
#include "cangjie/AST/Clone.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Utils/CastingTemplate.h"
#include "cangjie/Utils/CheckUtils.h"
#include <utility>

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
constexpr auto INTEROPLIB_JNI_JNINativeInterface_ID = "JNINativeInterface_";

// funcs
constexpr auto INTEROPLIB_JNI_GET_ENV_ID = "Java_CFFI_get_env";
constexpr auto INTEROPLIB_CFFI_GET_METHOD_ID = "Java_CFFI_getMethodID";
constexpr auto INTEROPLIB_CFFI_GET_STATIC_METHOD_ID = "Java_CFFI_getStaticMethodID";
constexpr auto INTEROPLIB_JNI_HANDLE_PENDING_EXCEPTION_DECL_ID = "handlePendingException";
constexpr auto INTEROPLIB_CFFI_NEW_GLOBAL_REF_ID = "Java_CFFI_newGlobalReference";
constexpr auto INTEROPLIB_CFFI_DELETE_GLOBAL_REF_ID = "Java_CFFI_deleteGlobalReference";
constexpr auto INTEROPLIB_CFFI_NEW_JAVA_ARRAY_ID = "Java_CFFI_newJavaArray";
constexpr auto INTEROPLIB_CFFI_JAVA_ARRAY_GET_ID = "Java_CFFI_arrayGet";
constexpr auto INTEROPLIB_CFFI_JAVA_ARRAY_SET_ID = "Java_CFFI_arraySet";
constexpr auto INTEROPLIB_CFFI_JAVA_ARRAY_GET_LENGTH = "Java_CFFI_arrayGetLength";
constexpr auto INTEROPLIB_CFFI_JAVA_ENTITY_JOBJECT_ID = "Java_CFFI_JavaEntityJobject";
constexpr auto INTEROPLIB_CFFI_JAVA_ENTITY_NULL_ID = "Java_CFFI_JavaEntityJobjectNull";
constexpr auto INTEROPLIB_CFFI_JAVA_ENTITY_IS_NULL_ID = "isNull";
constexpr auto INTEROPLIB_JNI_PUT_TO_REGISTRY_DECL_ID = "Java_CFFI_put_to_registry_1";
constexpr auto INTEROPLIB_JNI_PUT_SET_TO_REGISTRY_DECL_ID = "Java_CFFI_putToRegistry";
constexpr auto INTEROPLIB_JNI_REMOVE_FROM_REGISTRY_DECL_ID = "Java_CFFI_removeFromRegistry";
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
constexpr auto INTEROPLIB_CFFI_GET_REGISTRY_ID = "Java_CFFI_getRegistryId";
constexpr auto INTEROPLIB_CFFI_GET_REGISTRY_ID_OR_NONE = "Java_CFFI_getRegistryIdOrNone";
constexpr auto INTEROPLIB_CFFI_JAVA_STRING_TO_CANGJIE = "Java_CFFI_JavaStringToCangjie";
constexpr auto INTEROPLIB_CFFI_CANGJIE_STRING_TO_JAVA = "Java_CFFI_CangjieStringToJava";
constexpr auto INTEROPLIB_CFFI_WITH_EXCEPTION_HANDLING_ID = "withExceptionHandling";
constexpr auto INTEROPLIB_CFFI_JAVA_CFFI_CLASS_INIT = "Java_CFFI_ClassInit";
constexpr auto INTEROPLIB_CFFI_JAVA_CFFI_CLASS = "Java_CFFI_getClass";
constexpr auto INTEROPLIB_CFFI_PARSE_METHOD_SIGNATURE_ID = "Java_CFFI_parseMethodSignature";
constexpr auto INTEROPLIB_CFFI_PARSE_COMPONENT_SIGNATURE_ID = "Java_CFFI_parseComponentSignature";
constexpr auto INTEROPLIB_CFFI_JAVA_CALLNEST_ID = "Java_CFFI_JavaCallNestInit";
constexpr auto INTEROPLIB_CFFI_JAVA_METHODID_CONSTR_ID = "Java_CFFI_MethodIDConstr";
constexpr auto INTEROPLIB_CFFI_JAVA_METHODID_CONSTR_STATIC_ID = "Java_CFFI_MethodIDConstrStatic";
constexpr auto INTEROPLIB_CFFI_JAVA_FIELDID_CONSTR_ID = "Java_CFFI_FieldIDConstr";
constexpr auto INTEROPLIB_CFFI_JAVA_FIELDID_CONSTR_STATIC_ID = "Java_CFFI_FieldIDConstrStatic";
constexpr auto DELETE_LOCAL_REF = "deleteLocalRef";

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

Ptr<StructDecl> InteropLibBridge::GetJavaEntityDecl() const
{
    return GetInteropLibDecl<ASTKind::STRUCT_DECL>(INTEROPLIB_CFFI_JAVA_ENTITY);
}

Ptr<TypeAliasDecl> InteropLibBridge::GetJniEnvPtrDecl() const
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

Ptr<FuncDecl> InteropLibBridge::GetNewJavaArrayDecl()
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_CFFI_NEW_JAVA_ARRAY_ID);
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

Ptr<StructDecl> InteropLibBridge::GetJNINativeInterfaceDecl() const
{
    return GetInteropLibDecl<ASTKind::STRUCT_DECL>(INTEROPLIB_JNI_JNINativeInterface_ID);
}

Ptr<FuncDecl> InteropLibBridge::FindAsJValueDecl() const
{
    static Ptr<FuncDecl> result = nullptr;
    if (result != nullptr) {
        return result;
    }

    auto entityDecl = GetJavaEntityDecl();
    for (auto& decl : entityDecl->GetMemberDeclPtrs()) {
        if (decl->identifier == "asJValue") {
            result = As<ASTKind::FUNC_DECL>(decl.get());
            break;
        }
    }

    CJC_NULLPTR_CHECK(result);
    return result;
}

Ptr<FuncDecl> InteropLibBridge::FindAsJObjectDecl() const
{
    static Ptr<FuncDecl> result = nullptr;
    if (result != nullptr) {
        return result;
    }

    auto entityDecl = GetJavaEntityDecl();
    for (auto& decl : entityDecl->GetMemberDeclPtrs()) {
        if (decl->identifier == "asJObject") {
            result = As<ASTKind::FUNC_DECL>(decl.get());
            break;
        }
    }

    CJC_NULLPTR_CHECK(result);
    return result;
}

Ptr<VarDecl> InteropLibBridge::GetJNINativeInterfaceField(const std::string_view name)
{
    static auto jniInterfaceDecl = GetJNINativeInterfaceDecl();
    CJC_NULLPTR_CHECK(jniInterfaceDecl);
    for (auto& decl : jniInterfaceDecl->GetMemberDecls()) {
        if (decl->astKind != ASTKind::VAR_DECL) {
            continue;
        }
        auto* vd = StaticAs<ASTKind::VAR_DECL>(decl.get());
        if (vd->identifier.Val() == name) {
            return vd;
        }
    }
    CJC_ABORT_WITH_MSG("Failed to find JNINativeInterface field");
    return nullptr;
}

Ptr<FuncDecl> InteropLibBridge::GetJNIHandlePendingExceptionDecl() const
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(
        INTEROPLIB_JNI_HANDLE_PENDING_EXCEPTION_DECL_ID);
}

Ptr<FuncDecl> InteropLibBridge::GetGetMethodIdDecl() const
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_CFFI_GET_METHOD_ID);
}

Ptr<FuncDecl> InteropLibBridge::GetGetStaticMethodIdDecl() const
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_CFFI_GET_STATIC_METHOD_ID);
}

Ptr<Ty> InteropLibBridge::GetJValueTy()
{
    return typeManager.GetPrimitiveTy(TypeKind::TYPE_UINT64);
}

Ptr<FuncDecl> InteropLibBridge::GetRemoveFromRegistryDecl()
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_JNI_REMOVE_FROM_REGISTRY_DECL_ID);
}

Ptr<FuncDecl> InteropLibBridge::GetPutToRegistryDecl()
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_JNI_PUT_TO_REGISTRY_DECL_ID);
}

Ptr<FuncDecl> InteropLibBridge::GetPutSetToRegistryDecl()
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_JNI_PUT_SET_TO_REGISTRY_DECL_ID);
}

Ptr<FuncDecl> InteropLibBridge::GetUnwrapJavaEntityDecl()
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_CFFI_UNWRAP_JAVA_ENTITY_METHOD_DECL_ID);
}

Ptr<FuncDecl> InteropLibBridge::GetGetRegistryIdDecl()
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_CFFI_GET_REGISTRY_ID);
}

Ptr<FuncDecl> InteropLibBridge::GetGetRegistryIdOrNoneDecl()
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_CFFI_GET_REGISTRY_ID_OR_NONE);
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

Ptr<FuncDecl> InteropLibBridge::GetJClassDecl() const
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_CFFI_JAVA_CFFI_CLASS_INIT);
}

Ptr<FuncDecl> InteropLibBridge::GetJClassIdDecl() const
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_CFFI_JAVA_CFFI_CLASS);
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

Ptr<FuncDecl> InteropLibBridge::GetDeleteLocalRefDecl()
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(DELETE_LOCAL_REF);
}
// ty

Ptr<Ty> InteropLibBridge::GetJNIEnvPtrTy()
{
    auto decl = GetJniEnvPtrDecl();
    if (!decl) {
        return nullptr;
    }
    return decl->type->DataTy();
}

Ptr<Ty> InteropLibBridge::GetJavaEntityTy()
{
    auto decl = GetJavaEntityDecl();
    if (!decl) {
        return nullptr;
    }

    return decl->DataTy();
}

Ty& InteropLibBridge::GetJniJobjectTy() const
{
    static auto ty = typeManager.GetPointerTy(typeManager.GetPrimitiveTy(TypeKind::TYPE_UNIT));
    CJC_NULLPTR_CHECK(ty);
    return *ty;
}

OwnedPtr<PointerExpr> InteropLibBridge::CreateJobjectNull()
{
    auto pointerExpr = MakeOwnedNode<PointerExpr>();
    pointerExpr->type = MakeOwnedNode<Type>();
    pointerExpr->type->SetTy({&GetJniJobjectTy()});
    pointerExpr->SetTy(pointerExpr->type->GetTy());
    return pointerExpr;
}


Ptr<Ty> InteropLibBridge::GetJlongTy() const
{
    return typeManager.GetPrimitiveTy(TypeKind::TYPE_INT64);
}

// type

OwnedPtr<Type> InteropLibBridge::CreateJobjectType() const
{
    Ptr<TypeAliasDecl> jobjectDecl = GetInteropLibDecl<ASTKind::TYPE_ALIAS_DECL>(INTEROPLIB_JNI_JOBJECT_ID);
    if (!jobjectDecl) {
        return nullptr;
    }
    return ASTCloner::Clone(Ptr(StaticAs<ASTKind::TYPE_ALIAS_DECL>(jobjectDecl)->type.get()));
}

OwnedPtr<Type> InteropLibBridge::CreateJlongType() const
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
            CJC_ASSERT_WITH_MSG(!ctor->funcBody->paramLists.empty(), "at least one paramLists expected");
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

OwnedPtr<CallExpr> InteropLibBridge::CreateJavaEntityJobjectCall(OwnedPtr<Expr> arg)
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
    CJC_ASSERT_WITH_MSG(!option->DataTy()->TyArgs().empty(), "Option type must be generic");
    ModalTy mirrorTy{option->DataTy()->TyArg(0)};
    // `case Some(argv) => argv.javaref`
    auto vp = CreateVarPattern(V_COMPILER, mirrorTy);
    vp->curFile = curFile;
    vp->varDecl->curFile = curFile;
    auto javarefAccess = CreateJavaRefCall(WithinFile(CreateRefExpr(*vp->varDecl), curFile), mirror);

    auto somePattern = MakeOwnedNode<EnumPattern>();
    somePattern->SetTy(utils.GetOptionTy(mirrorTy.Ty()));
    somePattern->constructor = utils.CreateOptionSomeRef(mirrorTy.Ty());
    somePattern->patterns.emplace_back(std::move(vp));
    somePattern->curFile = curFile;
    auto caseSome = CreateMatchCase(std::move(somePattern), std::move(javarefAccess));

    // `case None => Java_CFFI_JavaEntityJobjectNull()`
    auto nonePattern = MakeOwnedNode<EnumPattern>();
    nonePattern->constructor = utils.CreateOptionNoneRef(mirrorTy.Ty());
    nonePattern->SetTy(nonePattern->constructor->GetTy());
    nonePattern->curFile = curFile;
    auto caseNone = CreateMatchCase(std::move(nonePattern), CreateJavaEntityNullCall(curFile));

    // `match`
    std::vector<OwnedPtr<MatchCase>> matchCases;
    matchCases.emplace_back(std::move(caseSome));
    matchCases.emplace_back(std::move(caseNone));
    return WithinFile(CreateMatchExpr(std::move(option), std::move(matchCases), {GetJavaEntityTy()}), curFile);
}

OwnedPtr<Expr> InteropLibBridge::CreateJavaEntityCall(OwnedPtr<Expr> arg)
{
    auto javaEntityDecl = GetJavaEntityDecl();
    if (!javaEntityDecl) {
        return nullptr;
    }

    if (auto classLTy = DynamicCast<ClassLikeTy*>(arg->DataTy())) {
        if (auto decl = classLTy->commonDecl; decl && (decl->IsJavaMirror() || decl->IsJavaImpl())) {
            return CreateJavaRefCall(std::move(arg));
        }
    } else if (arg->GetTy()->IsCoreOptionType()) {
        CJC_ASSERT_WITH_MSG(!arg->GetTy()->typeArgs.empty(), "Option type must be generic");
        if (auto classALTy = DynamicCast<ClassLikeTy*>(arg->DataTy()->TyArg(0))) {
            if (auto decl = classALTy->commonDecl; decl && (decl->IsJavaMirror() || decl->IsJavaImpl())) {
                return CreateJavaEntityFromOptionMirror(std::move(arg), *decl);
            }
        }
    }

    Ptr<FuncDecl> suitableCtor;
    for (auto& decl : javaEntityDecl->body->decls) {
        if (auto ctor = As<ASTKind::FUNC_DECL>(decl.get()); ctor && ctor->TestAttr(Attribute::CONSTRUCTOR)) {
            CJC_ASSERT_WITH_MSG(!ctor->funcBody->paramLists.empty(), "paramLists cannot be empty");
            if (ctor->funcBody->paramLists[0]->params.size() != 1) {
                continue;
            }
            if (typeManager.IsTyEqual(arg->GetTy(), ctor->funcBody->paramLists[0]->params[0]->GetTy())) {
                suitableCtor = ctor;
            }
        }
    }

    if (!suitableCtor) {
        diag.DiagnoseRefactor(
            DiagKindRefactor::sema_java_interop_not_supported, arg->begin, "Type " + arg->DataTy()->name);
        return nullptr;
    }

    auto curFile = arg->curFile;
    auto call = CreateCall(suitableCtor, curFile, std::move(arg));
    if (call) {
        call->callKind = CallKind::CALL_STRUCT_CREATION;
    }
    return call;
}

/**
 * Lower CJ expression into Java boundary representation (JavaEntity / jobject).
 */
 OwnedPtr<Expr> InteropLibBridge::WrapJavaEntity(OwnedPtr<Expr> cjExpr)
{
    CJC_NULLPTR_CHECK(cjExpr->curFile);
    if (cjExpr->TyKind() == TypeKind::TYPE_UNIT) {
        return CreateJavaEntityCall(cjExpr->curFile);
    }

    if (IsOptionOfString(cjExpr->GetTy().Ty())) {
        auto curFile = cjExpr->curFile;
        auto match = utils.CreateOptionMatch(std::move(cjExpr),
            [&](VarDecl& v) -> OwnedPtr<Expr> {
                auto ref = WithinFile(CreateRefExpr(v), curFile);
                return WrapJavaEntity(std::move(ref));
            },
            [&]() -> OwnedPtr<Expr> { return CreateJavaEntityNullCall(curFile); },
            GetJavaEntityTy()
        );
        return WithinFile(std::move(match), curFile);
    }

    if (cjExpr->GetTy()->IsString()) {
        // Special handling for String: convert Cangjie String to jstring
        // before wrapping it into JavaEntity for JNI interop.
        auto curFile = cjExpr->curFile;
        auto env = CreateGetJniEnvCall(curFile);
        return CreateCangjieStringToJavaCall(std::move(env), std::move(cjExpr));
    }

    return CreateJavaEntityCall(std::move(cjExpr));
}

OwnedPtr<CallExpr> InteropLibBridge::CreateJNINewObjectCall(Ptr<Expr> env, OwnedPtr<Expr> javaClass,
    OwnedPtr<Expr> methodId, OwnedPtr<Expr> argsExpr)
{
    auto pcurFile = env->curFile;
    auto newObjectVar = GetJNINativeInterfaceField(argsExpr ? "NewObjectA" : "NewObject");
    if (!newObjectVar) {
        return nullptr;
    }
    auto envCJ = CreateJNIEnvReadCall(ASTCloner::Clone(env));
    if (!envCJ) {
        return nullptr;
    }
    envCJ->curFile = pcurFile;

    auto newObjectAccess = WithinFile(CreateMemberAccess(std::move(envCJ), *newObjectVar), pcurFile);
    newObjectAccess->EnableAttr(Attribute::UNSAFE);
    std::vector<OwnedPtr<FuncArg>> callArgs;
    callArgs.emplace_back(CreateFuncArg(ASTCloner::Clone(env)));
    callArgs.emplace_back(CreateFuncArg(std::move(javaClass)));
    callArgs.emplace_back(CreateFuncArg(std::move(methodId)));
    if (argsExpr) {
        callArgs.emplace_back(PrepareJNIArgsVArray(std::move(argsExpr)));
    }
    auto funcTy = StaticCast<FuncTy*>(newObjectVar->DataTy());
    auto callExpr = CreateCallExpr(std::move(newObjectAccess), std::move(callArgs), nullptr,
        funcTy->retTy, CallKind::CALL_FUNCTION_PTR);
    callExpr->EnableAttr(Attribute::UNSAFE);
    return CreateJavaEntityJobjectCall(std::move(callExpr));
}

OwnedPtr<Block> InteropLibBridge::CreateJavaConstructorBlock(Ptr<Ty> classTy, FuncParamList& paramList,
    Ptr<File> curFile, bool isMirror)
{
    auto jniEnvPtrDecl = GetJniEnvPtrDecl();
    auto jniEnvVar = CreateTmpVarDecl(jniEnvPtrDecl->type, CreateGetJniEnvCall(curFile));
    static auto markerClassDecl = CreateConstructorMarkerClassDecl();
    OwnedPtr<VarDecl> argsVar = nullptr;
    OwnedPtr<Expr> argsRef = nullptr;
    auto paramTys = Native::FFI::GetParamTys(paramList);
    bool addCtorArgs = !paramList.params.empty() || !isMirror;
    // Build JNI arguments, including NativeConstructorMarker for JavaImpl.
    if (addCtorArgs) {
        std::vector<OwnedPtr<Expr>> args;
        if (!paramList.params.empty()) {
            args = CreateJNIArgJValueExprs(paramList, *curFile);
        }
        if (!isMirror) {
            args.push_back(CreateJavaEntityNullCall(curFile));
            paramTys.push_back({typeManager.GetClassTy(*markerClassDecl, {})});
        }
        argsVar = CreateJNIArgsVar(std::move(args), *curFile);
        argsRef = WithinFile(CreateRefExpr(*argsVar), curFile);
    }
    auto ctorSignature = utils.GetJavaTypeSignature(*TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT), paramTys);
    auto className = utils.GetJavaClassNormalizeSignature(*classTy);
    auto clazzVar = CreateJNIClassVar(WithinFile(CreateRefExpr(*jniEnvVar), curFile), className);
    auto ctorMethodId = CreateJNIMethodId(WithinFile(CreateRefExpr(*jniEnvVar), curFile),
        WithinFile(CreateRefExpr(*clazzVar), curFile), JAVA_CONSTRUCTOR, ctorSignature, false);
    if (!ctorMethodId) {
        return nullptr;
    }
    auto newObjectCall = CreateJNINewObjectCall(WithinFile(CreateRefExpr(*jniEnvVar), curFile),
        WithinFile(CreateRefExpr(*clazzVar), curFile), std::move(ctorMethodId), std::move(argsRef));
    if (!newObjectCall) {
        return nullptr;
    }
    auto resultVar = CreateTmpVarDecl(nullptr, std::move(newObjectCall));
    auto resultVarPtr = resultVar.get();
    static auto handlePendingDecl = GetJNIHandlePendingExceptionDecl();
    auto handlePending = CreateCall(handlePendingDecl, curFile, WithinFile(CreateRefExpr(*jniEnvVar), curFile));

    std::vector<OwnedPtr<Node>> nodes;
    nodes.push_back(std::move(jniEnvVar));
    nodes.push_back(std::move(clazzVar));
    if (addCtorArgs) {
        nodes.push_back(std::move(argsVar));
    }
    nodes.push_back(std::move(resultVar));
    nodes.push_back(std::move(handlePending));
    nodes.push_back(WithinFile(CreateRefExpr(*resultVarPtr), curFile));
    auto retTy = resultVarPtr->GetTy();
    return CreateBlock(std::move(nodes), retTy);
}

OwnedPtr<Expr> InteropLibBridge::SelectJSigByTypeKind([[maybe_unused]] TypeKind kind, Ptr<Ty> ty)
{
    static auto strTy = utils.GetStringDecl().GetTy();
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

OwnedPtr<Expr> InteropLibBridge::SelectEntityWrapperByTypeKind(
    [[maybe_unused]] TypeKind kind, [[maybe_unused]] Ptr<Ty> ty, Ptr<FuncParam> param, Ptr<File> file)
{
    auto paramRef = CreateRefExpr(*param);
    paramRef->curFile = file;
    return WrapJavaEntity(CreateMatchWithTypeCast(std::move(paramRef), ty));
}

OwnedPtr<Expr> InteropLibBridge::SelectEntityUnwrapperByTypeKind(
    [[maybe_unused]] TypeKind kind, [[maybe_unused]] Ptr<Ty> ty, Ptr<Expr> entity, const ClassLikeDecl& mirror)
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

OwnedPtr<CallExpr> InteropLibBridge::CreateCFFINewJavaArrayCall(OwnedPtr<Expr> jniEnv, FuncParamList& params)
{
    constexpr int expectedParamsSize = 2;
    CJC_ASSERT_WITH_MSG(params.params.size() == expectedParamsSize, "expected 2 params: 'length', '$jniType'.");
    CJC_ASSERT_WITH_MSG(jniEnv->curFile, "'curFile' expected to be not null in 'jniEnv' param");

    static auto funcDecl = GetNewJavaArrayDecl();
    auto curFile = jniEnv->curFile;
    auto getNthParam = [&params, &curFile](size_t ind) {
        return WithinFile(CreateRefExpr(*params.params[ind]), curFile);
    };
    auto sizeParam = getNthParam(0);
    auto jniTypeParam = getNthParam(1);

    return CreateCall(funcDecl, curFile, std::move(jniEnv), std::move(jniTypeParam), std::move(sizeParam));
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

OwnedPtr<CallExpr> InteropLibBridge::CreateNewGlobalRefCall(OwnedPtr<Expr> env, OwnedPtr<Expr> obj, bool isWeak)
{
    auto curFile = obj->curFile;
    auto isWeakBoolValue = CreateBoolLit(isWeak);
    isWeakBoolValue->curFile = curFile;
    isWeakBoolValue->SetTy({TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN)});

    return CreateCall(GetNewGlobalRefDecl(), curFile, std::move(env), std::move(obj), std::move(isWeakBoolValue));
}

OwnedPtr<CallExpr> InteropLibBridge::CreateDeleteGlobalRefCall(OwnedPtr<Expr> env, OwnedPtr<Expr> obj)
{
    auto curFile = obj->curFile;
    return CreateCall(GetDeleteGlobalRefDecl(), curFile, std::move(env), std::move(obj));
}

OwnedPtr<FuncArg> InteropLibBridge::PrepareJNIArgsVArray(OwnedPtr<Expr> expr)
{
    auto arg = CreateFuncArg(std::move(expr));
    auto* varrayTy = DynamicCast<VArrayTy>(arg->DataTy());
    CJC_ASSERT_WITH_MSG(varrayTy, "Expected VArray for JNI arguments");
    CJC_ASSERT_WITH_MSG(varrayTy->typeArgs.size() == 1, "Unexpected VArray type");
    arg->withInout = true;
    arg->SetTy({typeManager.GetPointerTy(varrayTy->TyArg(0))});
    return arg;
}

OwnedPtr<CallExpr> InteropLibBridge::CreateJNICall(Ptr<Expr> env, Ptr<VarDecl> jniFunction,
    std::vector<OwnedPtr<FuncArg>> callArgs)
{
    auto curFile = env->curFile;
    auto envCJ = CreateJNIEnvReadCall(ASTCloner::Clone(env));
    CJC_NULLPTR_CHECK(envCJ);
    envCJ->curFile = curFile;
    auto functionAccess = WithinFile(CreateMemberAccess(std::move(envCJ), *jniFunction), curFile);
    functionAccess->EnableAttr(Attribute::UNSAFE);
    auto funcTy = StaticCast<FuncTy*>(jniFunction->DataTy());
    auto callExpr = CreateCallExpr(std::move(functionAccess), std::move(callArgs), nullptr,
        funcTy->retTy, CallKind::CALL_FUNCTION_PTR);
    callExpr->EnableAttr(Attribute::UNSAFE);
    return callExpr;
}

OwnedPtr<CallExpr> InteropLibBridge::CreateJNIMethodId(Ptr<Expr> env, OwnedPtr<Expr> clazz,
    const std::string& name, const std::string& signature, bool isStatic)
{
    static auto getInstanceMethodIdFuncDecl = GetGetMethodIdDecl();
    static auto getStaticMethodIdFuncDecl = GetGetStaticMethodIdDecl();
    auto getMethodIdFuncDecl = isStatic ? getStaticMethodIdFuncDecl : getInstanceMethodIdFuncDecl;
    CJC_NULLPTR_CHECK(getMethodIdFuncDecl);
    auto strTy = utils.GetStringDecl().DataTy();
    auto methodName = CreateLitConstExpr(LitConstKind::STRING, name, strTy);
    auto signatureStr = CreateLitConstExpr(LitConstKind::STRING, signature, strTy);
    return CreateCall(getMethodIdFuncDecl, env->curFile, ASTCloner::Clone(env),
        std::move(clazz), std::move(methodName), std::move(signatureStr));
}

OwnedPtr<CallExpr> InteropLibBridge::CreateAsJObjectCall(OwnedPtr<Expr> javaEntity)
{
    CJC_NULLPTR_CHECK(javaEntity);
    auto curFile = javaEntity->curFile;
    static auto asJObjectDecl = FindAsJObjectDecl();
    CJC_NULLPTR_CHECK(asJObjectDecl);
    auto memberAccess = WithinFile(CreateMemberAccess(std::move(javaEntity), *asJObjectDecl),
        curFile);
    auto retTy = StaticCast<FuncTy*>(asJObjectDecl->DataTy())->retTy.Ty();
    auto callExpr = CreateCallExpr(std::move(memberAccess), {}, asJObjectDecl,
        retTy, CallKind::CALL_DECLARED_FUNCTION);
    callExpr->curFile = curFile;
    return callExpr;
}

OwnedPtr<Block> InteropLibBridge::CreateJavaSuperMethodCallBlock(ClassDecl& impl, CallExpr& call,
    const MemberJNISignature& signature)
{
    auto curFile = call.curFile;
    CJC_NULLPTR_CHECK(curFile);
    static auto jniEnvPtrDecl = GetJniEnvPtrDecl();
    CJC_NULLPTR_CHECK(jniEnvPtrDecl);
    auto jniEnvVar = CreateTmpVarDecl(jniEnvPtrDecl->type, CreateGetJniEnvCall(curFile));
    std::vector<OwnedPtr<Expr>> jniArgs;
    for (auto& arg : call.args) {
        auto argExpr = ASTCloner::Clone(arg->expr.get());
        CJC_NULLPTR_CHECK(argExpr);
        jniArgs.emplace_back(CreateJValueExpr(std::move(argExpr)));
    }
    const bool hasArgs = !call.args.empty();
    OwnedPtr<VarDecl> argsVar = nullptr;
    OwnedPtr<Expr> argsRef = nullptr;
    if (hasArgs) {
        argsVar = CreateJNIArgsVar(std::move(jniArgs), *curFile);
        argsRef = WithinFile(CreateRefExpr(*argsVar), curFile);
    }
    auto clazzVar = CreateJNIClassVar(WithinFile(CreateRefExpr(*jniEnvVar), curFile),
        signature.classTypeSignature);
    auto methodId = CreateJNIMethodId(WithinFile(CreateRefExpr(*jniEnvVar), curFile),
        WithinFile(CreateRefExpr(*clazzVar), curFile), signature.name,
        signature.signature, false);
    CJC_NULLPTR_CHECK(methodId);
    auto retTy = call.DataTy();
    auto jniFunction = GetJNINativeInterfaceField(SelectJNIInstanceMethodName(retTy->kind, false, hasArgs));
    CJC_NULLPTR_CHECK(jniFunction);
    auto javaRef = CreateJavaRefCall(impl, curFile);
    std::vector<OwnedPtr<FuncArg>> callArgs;
    callArgs.emplace_back(CreateFuncArg(WithinFile(CreateRefExpr(*jniEnvVar), curFile)));
    callArgs.emplace_back(CreateFuncArg(CreateAsJObjectCall(std::move(javaRef))));
    callArgs.emplace_back(CreateFuncArg(WithinFile(CreateRefExpr(*clazzVar), curFile)));
    callArgs.emplace_back(CreateFuncArg(std::move(methodId)));
    if (hasArgs) {
        callArgs.emplace_back(PrepareJNIArgsVArray(std::move(argsRef)));
    }
    auto jniMethodCall = CreateJNICall(WithinFile(CreateRefExpr(*jniEnvVar), curFile),
        jniFunction, std::move(callArgs));
    CJC_NULLPTR_CHECK(jniMethodCall);
    OwnedPtr<Expr> methodCall = std::move(jniMethodCall);
    if (!retTy->IsPrimitive()) {
        methodCall = CreateJavaEntityJobjectCall(std::move(methodCall));
    }
    auto resultExpr = ConvertJavaResultToCJ(std::move(methodCall), retTy, impl);
    CJC_NULLPTR_CHECK(resultExpr);
    std::vector<OwnedPtr<Node>> nodes;
    nodes.emplace_back(std::move(jniEnvVar));
    nodes.emplace_back(std::move(clazzVar));
    if (hasArgs) {
        nodes.emplace_back(std::move(argsVar));
    }
    nodes.emplace_back(std::move(resultExpr));
    return CreateBlock(std::move(nodes), call.GetTy());
}

OwnedPtr<Block> InteropLibBridge::CreateJavaMethodCallBlock(Ptr<File> curFile, FuncParamList& paramList,
    Ptr<Ty> retTy, bool isStatic, OwnedPtr<Expr> javaRef, const MemberJNISignature& signature)
{
    static auto jniEnvPtrDecl = GetJniEnvPtrDecl();
    CJC_NULLPTR_CHECK(jniEnvPtrDecl);
    auto jniEnvVar = CreateTmpVarDecl(jniEnvPtrDecl->type, CreateGetJniEnvCall(curFile));
    const bool hasParams = !paramList.params.empty();
    
    OwnedPtr<VarDecl> argsVar = nullptr;
    OwnedPtr<Expr> argsRef = nullptr;

    if (hasParams) {
        auto args = CreateJNIArgJValueExprs(paramList, *curFile);
        argsVar = CreateJNIArgsVar(std::move(args), *curFile);
        argsRef = WithinFile(CreateRefExpr(*argsVar), curFile);
    }

    auto clazzVar = CreateJNIClassVar(WithinFile(CreateRefExpr(*jniEnvVar), curFile),
        signature.classTypeSignature);
    auto methodId = CreateJNIMethodId(WithinFile(CreateRefExpr(*jniEnvVar), curFile),
        WithinFile(CreateRefExpr(*clazzVar), curFile), signature.name,
        signature.signature, isStatic);
    CJC_NULLPTR_CHECK(methodId);

    Ptr<VarDecl> jniFunction = GetJNINativeInterfaceField(isStatic
            ? SelectJNIStaticMethodName(retTy->kind, hasParams)
            : SelectJNIInstanceMethodName(retTy->kind, true, hasParams));
    CJC_NULLPTR_CHECK(jniFunction);

    std::vector<OwnedPtr<FuncArg>> callArgs;
    callArgs.emplace_back(CreateFuncArg(WithinFile(CreateRefExpr(*jniEnvVar), curFile)));

    if (isStatic) {
        callArgs.emplace_back(CreateFuncArg(WithinFile(CreateRefExpr(*clazzVar), curFile)));
    } else {
        callArgs.emplace_back(CreateFuncArg(CreateAsJObjectCall(std::move(javaRef))));
    }

    callArgs.emplace_back(CreateFuncArg(std::move(methodId)));
    if (hasParams) {
        callArgs.emplace_back(PrepareJNIArgsVArray(std::move(argsRef)));
    }

    auto jniMethodCall = CreateJNICall(WithinFile(CreateRefExpr(*jniEnvVar), curFile),
        jniFunction, std::move(callArgs));
    CJC_NULLPTR_CHECK(jniMethodCall);
    OwnedPtr<Expr> methodCall = std::move(jniMethodCall);
    if (!retTy->IsPrimitive()) {
        methodCall = CreateJavaEntityJobjectCall(std::move(methodCall));
    }
    auto resultVar = CreateTmpVarDecl(nullptr, std::move(methodCall));
    auto resultVarPtr = resultVar.get();
    static auto handlePendingDecl = GetJNIHandlePendingExceptionDecl();
    CJC_NULLPTR_CHECK(handlePendingDecl);
    auto handlePending = CreateCall(handlePendingDecl, curFile, WithinFile(CreateRefExpr(*jniEnvVar), curFile));
    std::vector<OwnedPtr<Node>> nodes;
    nodes.push_back(std::move(jniEnvVar));
    nodes.push_back(std::move(clazzVar));
    if (hasParams) {
        nodes.push_back(std::move(argsVar));
    }
    nodes.push_back(std::move(resultVar));
    nodes.push_back(std::move(handlePending));
    nodes.push_back(WithinFile(CreateRefExpr(*resultVarPtr), curFile));
    return CreateBlock(std::move(nodes), retTy);
}

OwnedPtr<CallExpr> InteropLibBridge::CreateJNIEnvReadCall(Ptr<Expr> env)
{
    static auto readPointerDecl = importManager.GetCoreDecl<FuncDecl>("readPointer");
    CJC_NULLPTR_CHECK(readPointerDecl);
    // env : CPointer<CPointer<JNINativeInterface_>>
    auto envTy = env->DataTy();
    CJC_ASSERT(envTy && envTy->typeArgs.size() == 1);

    // CPointer<JNINativeInterface_>
    auto ptrTy = envTy->TyArg(0);
    CJC_ASSERT(ptrTy && ptrTy->typeArgs.size() == 1);

    // JNINativeInterface_
    auto interfaceTy = ptrTy->TyArg(0);

    // readPointer<CPointer<JNINativeInterface_>>(env,0)
    auto ref1 = CreateRefExpr(*readPointerDecl);
    ref1->instTys.emplace_back(ptrTy);
    ref1->typeArguments.emplace_back(CreateType(ptrTy));
    ref1->SetTy(typeManager.GetInstantiatedTy(readPointerDecl->GetTy(),
        GenerateTypeMapping(*readPointerDecl, ref1->instTys)));
    auto int64Ty = typeManager.GetPrimitiveTy(TypeKind::TYPE_INT64);
    auto zero = CreateLitConstExpr(LitConstKind::INTEGER, "0", {int64Ty});
    std::vector<OwnedPtr<FuncArg>> args1;
    args1.emplace_back(CreateFuncArg(ASTCloner::Clone(env)));
    args1.emplace_back(CreateFuncArg(ASTCloner::Clone(zero.get())));
    auto read1 = CreateCallExpr(std::move(ref1), std::move(args1), readPointerDecl,
        ptrTy, CallKind::CALL_INTRINSIC_FUNCTION);
    read1->EnableAttr(Attribute::UNSAFE);

    // readPointer<JNINativeInterface_>(read1,0)
    auto ref2 = CreateRefExpr(*readPointerDecl);
    ref2->instTys.emplace_back(interfaceTy);
    ref2->typeArguments.emplace_back(CreateType(interfaceTy));
    ref2->SetTy(typeManager.GetInstantiatedTy(readPointerDecl->GetTy(),
        GenerateTypeMapping(*readPointerDecl, ref2->instTys)));
    std::vector<OwnedPtr<FuncArg>> args2;
    args2.emplace_back(CreateFuncArg(std::move(read1)));
    args2.emplace_back(CreateFuncArg(std::move(zero)));
    auto read2 = CreateCallExpr(std::move(ref2), std::move(args2), readPointerDecl,
        interfaceTy, CallKind::CALL_INTRINSIC_FUNCTION);
    read2->EnableAttr(Attribute::UNSAFE);
    return read2;
}

OwnedPtr<MatchExpr> InteropLibBridge::CreateMatchWithTypeCast(OwnedPtr<Expr> exprToCast, Ptr<Ty> castTy)
{
    static auto exceptionDecl = importManager.GetCoreDecl<ClassDecl>(CLASS_EXCEPTION);
    if (!exceptionDecl) {
        return nullptr;
    }
    auto castType = MakeOwned<Type>();
    castType->SetTy(castTy);
    auto varPattern = CreateVarPattern(V_COMPILER, castType->GetTy());
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
        "internal error: CreateMatchWithTypeCast(" + Ty::KindName(exprToCast->TyKind()) + " -> " +
            Ty::KindName(castTy->kind) + ")",
        utils.GetStringDecl().GetTy()));
    exceptionCallArgs.back()->curFile = curFile;

    matchCases.emplace_back(CreateMatchCase(std::move(typePattern), std::move(varPatternRef)));
    matchCases[0]->curFile = curFile;
    matchCases.emplace_back(CreateMatchCase(MakeOwned<WildcardPattern>(),
        CreateThrowException(*exceptionDecl, std::move(exceptionCallArgs), *exprToCast->curFile, typeManager)));
    matchCases[1]->curFile = curFile;

    return WithinFile(CreateMatchExpr(std::move(exprToCast), std::move(matchCases), {castTy}), curFile);
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
        javaEntityKindDecl->GetTy(), WithinFile(CreateRefExpr(GetJavaEntityKindJObject()), curFile));

    if (kind == ArrayOperationKind::GET) {
        return CreateCall(funcDecl, curFile, std::move(jniEnv), std::move(matchWithJPrimitive), std::move(obj),
            WithinFile(CreateRefExpr(*indexArg), curFile));
    }

    auto valueArg = params.params[1].get();
    auto paramRef = WithinFile(CreateRefExpr(*valueArg), curFile);

    OwnedPtr<Expr> valueEntity;
    if (valueArg->DataTy()->name == INTEROPLIB_CFFI_JAVA_ENTITY) {
        // for generated function - value argument was replaced with java entity
        valueEntity = std::move(paramRef);
    } else {
        valueEntity = CreateMatchByTypeArgument(genericParam,
            GenerateTypeMappingWithSelector([this, &valueArg, &curFile](TypeKind kind, Ptr<Ty> ty) {
                return SelectEntityWrapperByTypeKind(kind, ty, valueArg, curFile);
            }),
            GetJavaEntityDecl()->GetTy(),
            CreateMemberAccess(CreateMatchWithTypeCast(std::move(paramRef), jObject->DataTy()), *javaRef));
    }

    return CreateCall(funcDecl, curFile, std::move(jniEnv), std::move(matchWithJPrimitive), std::move(obj),
        WithinFile(CreateRefExpr(*indexArg), curFile), std::move(valueEntity));
}

struct JNIMethodNames {
    const std::string_view virtualNoArgs;
    const std::string_view virtualArgs;
    const std::string_view nonVirtualNoArgs;
    const std::string_view nonVirtualArgs;
    const std::string_view staticNoArgs;
    const std::string_view staticArgs;
};

namespace {
// Lookup tables for selecting the appropriate JNI Call<Type>Method variant.

constexpr JNIMethodNames BYTE_METHODS{
    "CallByteMethod",            // virtualNoArgs
    "CallByteMethodA",           // virtualArgs
    "CallNonvirtualByteMethod",  // nonVirtualNoArgs
    "CallNonvirtualByteMethodA", // nonVirtualArgs
    "CallStaticByteMethod",      // staticNoArgs
    "CallStaticByteMethodA",     // staticArgs
};
constexpr JNIMethodNames CHAR_METHODS{
    "CallCharMethod",            // virtualNoArgs
    "CallCharMethodA",           // virtualArgs
    "CallNonvirtualCharMethod",  // nonVirtualNoArgs
    "CallNonvirtualCharMethodA", // nonVirtualArgs
    "CallStaticCharMethod",      // staticNoArgs
    "CallStaticCharMethodA",     // staticArgs
};

constexpr JNIMethodNames SHORT_METHODS{
    "CallShortMethod",            // virtualNoArgs
    "CallShortMethodA",           // virtualArgs
    "CallNonvirtualShortMethod",  // nonVirtualNoArgs
    "CallNonvirtualShortMethodA", // nonVirtualArgs
    "CallStaticShortMethod",      // staticNoArgs
    "CallStaticShortMethodA",     // staticArgs
};

constexpr JNIMethodNames INT_METHODS{
    "CallIntMethod",            // virtualNoArgs
    "CallIntMethodA",           // virtualArgs
    "CallNonvirtualIntMethod",  // nonVirtualNoArgs
    "CallNonvirtualIntMethodA", // nonVirtualArgs
    "CallStaticIntMethod",      // staticNoArgs
    "CallStaticIntMethodA",     // staticArgs
};

constexpr JNIMethodNames LONG_METHODS{
    "CallLongMethod",            // virtualNoArgs
    "CallLongMethodA",           // virtualArgs
    "CallNonvirtualLongMethod",  // nonVirtualNoArgs
    "CallNonvirtualLongMethodA", // nonVirtualArgs
    "CallStaticLongMethod",      // staticNoArgs
    "CallStaticLongMethodA",     // staticArgs
};

constexpr JNIMethodNames FLOAT_METHODS{
    "CallFloatMethod",            // virtualNoArgs
    "CallFloatMethodA",           // virtualArgs
    "CallNonvirtualFloatMethod",  // nonVirtualNoArgs
    "CallNonvirtualFloatMethodA", // nonVirtualArgs
    "CallStaticFloatMethod",      // staticNoArgs
    "CallStaticFloatMethodA",     // staticArgs
};

constexpr JNIMethodNames DOUBLE_METHODS{
    "CallDoubleMethod",            // virtualNoArgs
    "CallDoubleMethodA",           // virtualArgs
    "CallNonvirtualDoubleMethod",  // nonVirtualNoArgs
    "CallNonvirtualDoubleMethodA", // nonVirtualArgs
    "CallStaticDoubleMethod",      // staticNoArgs
    "CallStaticDoubleMethodA",     // staticArgs
};

constexpr JNIMethodNames BOOLEAN_METHODS{
    "CallBooleanMethod",            // virtualNoArgs
    "CallBooleanMethodA",           // virtualArgs
    "CallNonvirtualBooleanMethod",  // nonVirtualNoArgs
    "CallNonvirtualBooleanMethodA", // nonVirtualArgs
    "CallStaticBooleanMethod",      // staticNoArgs
    "CallStaticBooleanMethodA",     // staticArgs
};

constexpr JNIMethodNames VOID_METHODS{
    "CallVoidMethod",            // virtualNoArgs
    "CallVoidMethodA",           // virtualArgs
    "CallNonvirtualVoidMethod",  // nonVirtualNoArgs
    "CallNonvirtualVoidMethodA", // nonVirtualArgs
    "CallStaticVoidMethod",      // staticNoArgs
    "CallStaticVoidMethodA",     // staticArgs
};

constexpr JNIMethodNames OBJECT_METHODS{
    "CallObjectMethod",            // virtualNoArgs
    "CallObjectMethodA",           // virtualArgs
    "CallNonvirtualObjectMethod",  // nonVirtualNoArgs
    "CallNonvirtualObjectMethodA", // nonVirtualArgs
    "CallStaticObjectMethod",      // staticNoArgs
    "CallStaticObjectMethodA",     // staticArgs
};

} // namespace

const JNIMethodNames& GetJNIMethodNames(TypeKind retTypeKind)
{
    switch (retTypeKind) {
        case TypeKind::TYPE_INT8:     return BYTE_METHODS;
        case TypeKind::TYPE_UINT16:   return CHAR_METHODS;
        case TypeKind::TYPE_INT16:    return SHORT_METHODS;
        case TypeKind::TYPE_INT32:    return INT_METHODS;
        case TypeKind::TYPE_INT64:    return LONG_METHODS;
        case TypeKind::TYPE_FLOAT32:  return FLOAT_METHODS;
        case TypeKind::TYPE_FLOAT64:  return DOUBLE_METHODS;
        case TypeKind::TYPE_BOOLEAN:  return BOOLEAN_METHODS;
        case TypeKind::TYPE_UNIT:     return VOID_METHODS;
        default:                      return OBJECT_METHODS;
    }
}

const std::string_view InteropLibBridge::SelectJNIInstanceMethodName(TypeKind retTypeKind, bool virt, bool hasArgs)
{
    const auto& m = GetJNIMethodNames(retTypeKind);
    if (virt) {
        return hasArgs ? m.virtualArgs : m.virtualNoArgs;
    }
    return hasArgs ? m.nonVirtualArgs : m.nonVirtualNoArgs;
}

const std::string_view InteropLibBridge::SelectJNIStaticMethodName(TypeKind retTypeKind, bool hasArgs)
{
    const auto& m = GetJNIMethodNames(retTypeKind);
    return hasArgs ? m.staticArgs : m.staticNoArgs;
}

OwnedPtr<CallExpr> InteropLibBridge::CreateBitCastExpr(OwnedPtr<Expr> expr,
    Ptr<Ty> resultTy, Ptr<Ty> valueTy, Ptr<File> curFile)
{
    static auto bitCastDecl = importManager.GetCoreDecl<FuncDecl>("bitCast");
    if (!bitCastDecl) {
        return nullptr;
    }
    auto ref = CreateRefExpr(*bitCastDecl);
    ref->instTys.emplace_back(valueTy);
    ref->instTys.emplace_back(resultTy);
    ref->typeArguments.emplace_back(CreateType(valueTy));
    ref->typeArguments.emplace_back(CreateType(resultTy));
    ref->SetTy(typeManager.GetInstantiatedTy(bitCastDecl->GetTy(),
        GenerateTypeMapping(*bitCastDecl, ref->instTys)));
    std::vector<OwnedPtr<FuncArg>> args;
    args.emplace_back(CreateFuncArg(std::move(expr)));
    auto call = CreateCallExpr(std::move(ref), std::move(args), bitCastDecl, resultTy,
        CallKind::CALL_INTRINSIC_FUNCTION);
    call->curFile = curFile;
    return call;
}

OwnedPtr<VarDecl> InteropLibBridge::CreateJNIClassVar(Ptr<Expr> env, const std::string& className)
{
    auto pcurFile = env->curFile;
    static auto jclassInitFuncDecl = GetJClassIdDecl();
    CJC_NULLPTR_CHECK(jclassInitFuncDecl);
    auto strTy = jclassInitFuncDecl->funcBody->paramLists[0]->params[1]->GetTy();
    auto classNameExpr = CreateLitConstExpr(LitConstKind::STRING, className, strTy);
    classNameExpr->curFile = pcurFile;
    auto initCall = CreateCall(jclassInitFuncDecl, pcurFile, ASTCloner::Clone(env), std::move(classNameExpr));
    return CreateTmpVarDecl(nullptr, std::move(initCall));
}

OwnedPtr<Expr> InteropLibBridge::CreateJValueExpr(OwnedPtr<Expr> expr)
{
    CJC_NULLPTR_CHECK(expr);
    auto curFile = expr->curFile;
    auto ty = expr->GetTy();
    if (ty->IsPrimitive()) {
        switch (ty->kind) {
            case TypeKind::TYPE_FLOAT32:
                return CreateBitCastExpr(std::move(expr),
                    typeManager.GetPrimitiveTy(TypeKind::TYPE_UINT32),
                    typeManager.GetPrimitiveTy(TypeKind::TYPE_FLOAT32),
                    curFile);
            case TypeKind::TYPE_FLOAT64:
                return CreateBitCastExpr(std::move(expr),
                    typeManager.GetPrimitiveTy(TypeKind::TYPE_UINT64),
                    typeManager.GetPrimitiveTy(TypeKind::TYPE_FLOAT64),
                    curFile);
            default:
                return expr;
        }
    }

    auto entityExpr = WrapJavaEntity(std::move(expr));
    auto asJValueDecl = FindAsJValueDecl();
    CJC_NULLPTR_CHECK(asJValueDecl);
    auto member =
        WithinFile(CreateMemberAccess(std::move(entityExpr), *asJValueDecl), curFile);

    auto call = CreateCallExpr(std::move(member), {}, asJValueDecl, GetJValueTy(), CallKind::CALL_DECLARED_FUNCTION);
    call->curFile = curFile;
    return call;
}

OwnedPtr<VarDecl> InteropLibBridge::CreateJNIArgsVar(std::vector<OwnedPtr<Expr>> args, File& curFile)
{
    if (args.empty()) {
        return nullptr;
    }
    auto jvalueTy = GetJValueTy();
    CJC_NULLPTR_CHECK(jvalueTy);
    auto varrayTy = typeManager.GetVArrayTy(*jvalueTy, static_cast<int64_t>(args.size()));
    auto argsArray = CreateArrayLit(std::move(args), {varrayTy});
    auto argsVar = CreateVarDecl("$argsPtr", std::move(argsArray), nullptr);
    argsVar->isVar = true;
    argsVar->SetTy(argsVar->initializer->GetTy());
    CopyBasicInfo(argsVar->initializer.get(), argsVar.get());
    argsVar->curFile = Ptr(&curFile);
    return argsVar;
}

std::vector<OwnedPtr<Expr>> InteropLibBridge::CreateJNIArgJValueExprs(FuncParamList& paramList, File& curFile)
{
    CJC_ASSERT_WITH_MSG(!paramList.params.empty(), "JNI argument list cannot be empty");
    std::vector<OwnedPtr<Expr>> args;
    args.reserve(paramList.params.size());
    for (auto& param : paramList.params) {
        args.emplace_back(CreateJValueExpr(WithinFile(CreateRefExpr(*param), Ptr(&curFile))));
    }
    return args;
}

OwnedPtr<Expr> InteropLibBridge::ConvertJavaResultToCJ(OwnedPtr<Expr> result, Ptr<Ty> resultTy, Decl& scope)
{
    CJC_NULLPTR_CHECK(result);
    CJC_NULLPTR_CHECK(resultTy);
    if (!resultTy->IsPrimitive()) {
        return UnwrapJavaEntity(std::move(result), resultTy, scope);
    }
    if (resultTy->kind == TypeKind::TYPE_BOOLEAN) {
        auto zero = CreateLitConstExpr(LitConstKind::INTEGER, "0", result->GetTy());
        auto cmp = CreateBinaryExpr(std::move(result), std::move(zero), TokenKind::NOTEQ);
        cmp->SetTy({typeManager.GetPrimitiveTy(TypeKind::TYPE_BOOLEAN)});
        return cmp;
    }
    return result;
}

OwnedPtr<CallExpr> InteropLibBridge::CreateRemoveFromRegistryCall(OwnedPtr<Expr> registryId)
{
    auto curFile = registryId->curFile;
    return CreateCall(GetRemoveFromRegistryDecl(), curFile, std::move(registryId));
}

OwnedPtr<CallExpr> InteropLibBridge::CreatePutToRegistryCall(OwnedPtr<Expr> obj)
{
    auto curFile = obj->curFile;
    return CreateCall(GetPutToRegistryDecl(), curFile, std::move(obj));
}

OwnedPtr<CallExpr> InteropLibBridge::CreatePutSetToRegistryCall(
    OwnedPtr<Expr> env, OwnedPtr<Expr> entity, OwnedPtr<Expr> obj)
{
    auto curFile = entity->curFile;
    return CreateCall(GetPutSetToRegistryDecl(), curFile, std::move(env), std::move(entity), std::move(obj));
}

OwnedPtr<CallExpr> InteropLibBridge::CreateGetRegistryIdCall(OwnedPtr<Expr> env, OwnedPtr<Expr> entity, bool nullable)
{
    auto curFile = entity->curFile;
    auto decl = nullable ? GetGetRegistryIdOrNoneDecl() : GetGetRegistryIdDecl();
    return CreateCall(decl, curFile, std::move(env), std::move(entity));
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
    fdRef->SetTy(typeManager.GetInstantiatedTy(funcDecl->GetTy(), GenerateTypeMapping(*funcDecl, fdRef->instTys)));
    auto retTy = retAsOption ? utils.GetOptionTy(ty) : ty;
    return CreateCallExpr(std::move(fdRef), std::move(callArgs), funcDecl, {retTy}, CallKind::CALL_DECLARED_FUNCTION);
}

OwnedPtr<CallExpr> InteropLibBridge::CreateCangjieStringToJavaCall(OwnedPtr<Expr> env, OwnedPtr<Expr> string)
{
    auto curFile = string->curFile;
    auto funcDecl = GetCangjieStringToJava();
    if (!funcDecl) {
        return nullptr;
    }

    return CreateCall(funcDecl, curFile, std::move(env), std::move(string));
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

OwnedPtr<CallExpr> InteropLibBridge::CreateGetFromRegistryCall(OwnedPtr<Expr> env, OwnedPtr<Expr> regId, Ptr<Ty> ty)
{
    auto curFile = regId->curFile;
    auto funcDecl = GetGetFromRegistryDecl();
    if (!funcDecl) {
        return nullptr;
    }

    std::vector<OwnedPtr<FuncArg>> callArgs;
    callArgs.push_back(CreateFuncArg(std::move(env)));
    callArgs.push_back(CreateFuncArg(std::move(regId)));

    auto fdRef = WithinFile(CreateRefExpr(*funcDecl), curFile);
    fdRef->instTys.push_back(ty);
    fdRef->SetTy(typeManager.GetInstantiatedTy(funcDecl->GetTy(), GenerateTypeMapping(*funcDecl, fdRef->instTys)));
    return CreateCallExpr(std::move(fdRef), std::move(callArgs), funcDecl, {ty}, CallKind::CALL_DECLARED_FUNCTION);
}

OwnedPtr<CallExpr> InteropLibBridge::CreateGetFromRegistryOptionCall(OwnedPtr<Expr> regId, Ptr<Ty> ty)
{
    auto curFile = regId->curFile;
    CJC_ASSERT(ty->IsCoreOptionType());
    auto funcDecl = GetGetFromRegistryOptionDecl();
    if (!funcDecl) {
        return nullptr;
    }

    std::vector<OwnedPtr<FuncArg>> callArgs;
    callArgs.push_back(CreateFuncArg(std::move(regId)));

    auto fdRef = WithinFile(CreateRefExpr(*funcDecl), curFile);
    CJC_ASSERT_WITH_MSG(!ty->TyArgs().empty(), "Option type expected to be generic");
    fdRef->instTys.push_back(ty->TyArg(0));
    fdRef->SetTy(typeManager.GetInstantiatedTy(funcDecl->GetTy(), GenerateTypeMapping(*funcDecl, fdRef->instTys)));
    return CreateCallExpr(std::move(fdRef), std::move(callArgs), funcDecl, {ty}, CallKind::CALL_DECLARED_FUNCTION);
}

OwnedPtr<CallExpr> InteropLibBridge::CreateGetFieldCall(Ptr<Expr> env, OwnedPtr<Expr> obj,
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

    CJC_ASSERT_WITH_MSG(!jclassInitFuncDecl->funcBody->paramLists.empty(), "paramLists cannot be empty");
    CJC_ASSERT_WITH_MSG(jclassInitFuncDecl->funcBody->paramLists[0]->params.size() > 1,
        "at least 2 parameters expected for classInit function");
    CJC_ASSERT(jclassInitFuncDecl->funcBody->paramLists[0] && jclassInitFuncDecl->funcBody->paramLists[0]->params[1]);
    auto strTy = jclassInitFuncDecl->funcBody->paramLists[0]->params[1]->GetTy();

    auto tsigExpr = CreateLitConstExpr(LitConstKind::STRING, typeSignature, strTy);
    auto fieldNameExpr = CreateLitConstExpr(LitConstKind::STRING, fieldName, strTy);
    auto fsigExpr = CreateLitConstExpr(LitConstKind::STRING, fieldSignature, strTy);

    auto jclass = CreateCall(jclassInitFuncDecl, curFile, ASTCloner::Clone(env), std::move(tsigExpr));
    auto filedSignature = CreateCall(parseSignatureFuncDecl, curFile, std::move(fsigExpr));
    auto fieldID = CreateCall(fieldIDFuncDecl, curFile, ASTCloner::Clone(env), std::move(jclass),
        std::move(fieldNameExpr), std::move(filedSignature));

    return CreateCall(funcDecl, curFile, ASTCloner::Clone(env), std::move(obj), std::move(fieldID));
}

OwnedPtr<CallExpr> InteropLibBridge::CreateGetStaticFieldCall(
    Ptr<Expr> env, std::string typeSignature, std::string fieldName, std::string fieldSignature)
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

    CJC_ASSERT_WITH_MSG(!jclassInitFuncDecl->funcBody->paramLists.empty(), "paramLists cannot be empty");
    CJC_ASSERT_WITH_MSG(jclassInitFuncDecl->funcBody->paramLists[0]->params.size() > 1,
        "at least 2 parameters expected for classInit function");
    CJC_ASSERT(jclassInitFuncDecl->funcBody->paramLists[0] && jclassInitFuncDecl->funcBody->paramLists[0]->params[1]);
    auto strTy = jclassInitFuncDecl->funcBody->paramLists[0]->params[1]->GetTy();

    auto tsigExpr = CreateLitConstExpr(LitConstKind::STRING, typeSignature, strTy);
    auto fieldNameExpr = CreateLitConstExpr(LitConstKind::STRING, fieldName, strTy);
    auto fsigExpr = CreateLitConstExpr(LitConstKind::STRING, fieldSignature, strTy);

    auto jclass = CreateCall(jclassInitFuncDecl, curFile, ASTCloner::Clone(env), std::move(tsigExpr));
    auto filedSignature = CreateCall(parseSignatureFuncDecl, curFile, std::move(fsigExpr));
    auto fieldID = CreateCall(fieldIDFuncDecl, curFile, ASTCloner::Clone(env), ASTCloner::Clone(jclass.get()),
        std::move(fieldNameExpr), std::move(filedSignature));

    return CreateCall(funcDecl, curFile, ASTCloner::Clone(env), std::move(jclass), std::move(fieldID));
}

OwnedPtr<CallExpr> InteropLibBridge::CreateSetFieldCall(
    Ptr<Expr> env, OwnedPtr<Expr> obj, const MemberJNISignature& signature, OwnedPtr<Expr> value)
{
    auto curFile = value->curFile;
    auto funcDecl = GetSetFieldDecl();
    auto jclassInitFuncDecl = GetJClassDecl();
    auto parseSignatureFuncDecl = GetParseComponentSignatureDecl();
    auto fieldIDFuncDecl = GetFieldIdConstr();
    if (!funcDecl || !jclassInitFuncDecl || !parseSignatureFuncDecl || !fieldIDFuncDecl) {
        return nullptr;
    }

    CJC_ASSERT_WITH_MSG(!jclassInitFuncDecl->funcBody->paramLists.empty(), "paramLists cannot be empty");
    CJC_ASSERT_WITH_MSG(jclassInitFuncDecl->funcBody->paramLists[0]->params.size() > 1,
        "at least 2 parameters expected for classInit function");
    CJC_ASSERT(jclassInitFuncDecl->funcBody->paramLists[0] && jclassInitFuncDecl->funcBody->paramLists[0]->params[1]);
    auto strTy = jclassInitFuncDecl->funcBody->paramLists[0]->params[1]->GetTy();

    auto tsigExpr = CreateLitConstExpr(LitConstKind::STRING, signature.classTypeSignature, strTy);
    tsigExpr->curFile = curFile;
    auto fieldNameExpr = CreateLitConstExpr(LitConstKind::STRING, signature.name, strTy);
    fieldNameExpr->curFile = curFile;
    auto fsigExpr = CreateLitConstExpr(LitConstKind::STRING, signature.signature, strTy);
    fsigExpr->curFile = curFile;

    auto jclass = CreateCall(jclassInitFuncDecl, curFile, ASTCloner::Clone(env), std::move(tsigExpr));
    auto filedSignature = CreateCall(parseSignatureFuncDecl, curFile, std::move(fsigExpr));
    auto fieldID = CreateCall(fieldIDFuncDecl, curFile, ASTCloner::Clone(env), std::move(jclass),
        std::move(fieldNameExpr), std::move(filedSignature));

    return CreateCall(funcDecl,
        curFile, ASTCloner::Clone(env), std::move(obj), std::move(fieldID), WrapJavaEntity(std::move(value)));
}

OwnedPtr<CallExpr> InteropLibBridge::CreateSetStaticFieldCall(Ptr<Expr> env, std::string typeSignature,
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

    CJC_ASSERT_WITH_MSG(!jclassInitFuncDecl->funcBody->paramLists.empty(), "paramLists cannot be empty");
    CJC_ASSERT_WITH_MSG(jclassInitFuncDecl->funcBody->paramLists[0]->params.size() > 1,
        "at least 2 parameters expected for classInit function");
    CJC_ASSERT(jclassInitFuncDecl->funcBody->paramLists[0] && jclassInitFuncDecl->funcBody->paramLists[0]->params[1]);
    auto strTy = jclassInitFuncDecl->funcBody->paramLists[0]->params[1]->GetTy();

    auto tsigExpr = CreateLitConstExpr(LitConstKind::STRING, typeSignature, strTy);
    tsigExpr->curFile = curFile;
    auto fieldNameExpr = CreateLitConstExpr(LitConstKind::STRING, fieldName, strTy);
    fieldNameExpr->curFile = curFile;
    auto fsigExpr = CreateLitConstExpr(LitConstKind::STRING, fieldSignature, strTy);
    fsigExpr->curFile = curFile;

    auto jclass = CreateCall(jclassInitFuncDecl, curFile, ASTCloner::Clone(env), std::move(tsigExpr));
    auto filedSignature = CreateCall(parseSignatureFuncDecl, curFile, std::move(fsigExpr));
    auto fieldID = CreateCall(fieldIDFuncDecl, curFile, ASTCloner::Clone(env), ASTCloner::Clone(jclass.get()),
        std::move(fieldNameExpr), std::move(filedSignature));
    return CreateCall(funcDecl,
        curFile, ASTCloner::Clone(env), std::move(jclass), std::move(fieldID), WrapJavaEntity(std::move(value)));
}

OwnedPtr<Expr> InteropLibBridge::WrapExceptionHandling(OwnedPtr<Expr> env, OwnedPtr<LambdaExpr> action)
{
    auto curFile = env->curFile;
    auto retTy = action->funcBody->GetTy();
    auto fd = GetWithExceptionHandlingDecl();
    auto fdRef = WithinFile(CreateRefExpr(*fd), curFile);
    fdRef->instTys.push_back(retTy.Ty());
    fdRef->SetTy(typeManager.GetInstantiatedTy(fd->GetTy(), GenerateTypeMapping(*fd, fdRef->instTys)));

    std::vector<OwnedPtr<FuncArg>> args;
    args.emplace_back(CreateFuncArg(std::move(env)));
    args.emplace_back(CreateFuncArg(std::move(action)));
    return CreateCallExpr(std::move(fdRef), std::move(args), nullptr, retTy, CallKind::CALL_DECLARED_FUNCTION);
}

bool InteropLibBridge::IsJavaEntityTy(Ty& ty)
{
    auto javaEntityTy = DynamicCast<StructTy>(&ty);
    if (!javaEntityTy) {
        return false;
    }
    auto javaEntity = javaEntityTy->decl;
    if (!javaEntity || javaEntity->fullPackageName != INTEROPLIB_PACKAGE_NAME) {
        return false;
    }
    return javaEntity->identifier == INTEROPLIB_CFFI_JAVA_ENTITY;
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
    CJC_ASSERT_WITH_MSG(!isNullProp->getters.empty(), "getters cannot be empty");
    auto access = CreateMemberAccess(std::move(entity), *isNullProp->getters[0]);
    CopyBasicInfo(access->baseExpr.get(), access.get());
    auto call = CreateCallExpr(
        std::move(access), {}, isNullProp->getters[0], isNullProp->GetTy(), CallKind::CALL_DECLARED_FUNCTION);
    CopyBasicInfo(call->baseFunc.get(), call.get());
    return call;
}

bool IsWrappingConstructorOfJavaImpl(Decl& refWrapperMember)
{
    if (!refWrapperMember.TestAttr(Attribute::CONSTRUCTOR, Attribute::COMPILER_ADD)) {
        return false;
    }

    auto method = As<ASTKind::FUNC_DECL>(&refWrapperMember);
    if (!method) {
        return false;
    }
    auto& ctor = *method;

    if (!ctor.funcBody || ctor.funcBody->paramLists.empty()) {
        return false;
    }

    auto& params = ctor.funcBody->paramLists[0]->params;
    if (params.size() != 2) {
        return false;
    }

    auto& javaRefParam = *params[0];
    if (!InteropLibBridge::IsJavaEntityTy(*javaRefParam.GetTy())) {
        return false;
    }

    auto& regIdParam = *params[1];
    if (regIdParam.GetTy()->kind != TypeKind::TYPE_INT64) {
        return false;
    }

    return true;
}

FuncDecl& GetJavaImplWrappingConstructor(ClassDecl& refWrapper)
{
    CJC_ASSERT(IsImplReferenceWrapper(refWrapper));

    Ptr<FuncDecl> ctor;
    for (auto member : refWrapper.GetMemberDeclPtrs()) {
        if (!IsWrappingConstructorOfJavaImpl(*member)) {
            continue;
        }
        ctor = StaticAs<ASTKind::FUNC_DECL>(member);
        break;
    }

    CJC_NULLPTR_CHECK(ctor);
    return *ctor;
}

} // namespace

OwnedPtr<Expr> InteropLibBridge::UnwrapJavaMirrorOption(
    OwnedPtr<Expr> entity, Ptr<Ty> ty, const ClassLikeDecl& mirror, bool toRaw)
{
    CJC_ASSERT(ty->IsCoreOptionType());
    auto curFile = entity->curFile;
    CJC_NULLPTR_CHECK(curFile);
    CJC_ASSERT_WITH_MSG(!ty->typeArgs.empty(), "Option type must be generic");
    auto declTy = ty->TyArg(0);
    auto decl = Ty::GetDeclOfTy(declTy);
    CJC_ASSERT(decl->IsJavaMirror() || declTy->IsString() || (toRaw && decl->IsJavaImpl()));

    auto actualTy = toRaw ? Ptr(&GetJniJobjectTy()) : ty;

    return utils.CreateOptionMatch(
        CreateGetJavaEntityOrNullCall(std::move(entity)),
        [this, curFile, declTy, &mirror, toRaw](VarDecl& e) {
            auto unwrapped = UnwrapJavaEntity(WithinFile(CreateRefExpr(e), curFile), declTy, mirror, toRaw);
            return unwrapped;
        },
        [this, ty, toRaw]() { return toRaw ? CreateJobjectNull() : utils.CreateOptionNoneRef(ty->TyArg(0)); },
        actualTy);
}

OwnedPtr<Expr> InteropLibBridge::UnwrapJavaImplOption(
    OwnedPtr<Expr> env, OwnedPtr<Expr> entityOption, Ptr<Ty> ty, const ClassLikeDecl& mirror, bool toRaw)
{
    CJC_ASSERT_WITH_MSG(!ty->typeArgs.empty(), "Option type must be generic");
    auto implTy = ty->typeArgs[0].Ty();
    CJC_ASSERT(implTy->IsClass());
    auto actualTy = toRaw ? Ptr(&GetJniJobjectTy()) : ty;
    auto curFile = entityOption->curFile;

    return utils.CreateOptionMatch(
        CreateGetJavaEntityOrNullCall(std::move(entityOption)),
        [this, curFile, implTy, &env, &mirror, toRaw](VarDecl& e) {
            if (toRaw) {
                // To @C CPointer appropriate for JNI
                return UnwrapJavaEntity(WithinFile(CreateRefExpr(e), curFile), implTy, mirror, toRaw);
            }

            // as impl reference wrapper
            return UnwrapJavaImpl(std::move(env), WithinFile(CreateRefExpr(e), curFile), implTy);
        },
        [this, ty, toRaw]() { return toRaw ? CreateJobjectNull() : utils.CreateOptionNoneRef(ty->TyArg(0)); },
        actualTy);
}

OwnedPtr<Expr> InteropLibBridge::UnwrapJavaImpl(OwnedPtr<Expr> env, OwnedPtr<Expr> entity, Ptr<Ty> ty)
{
    auto curFile = entity->curFile;
    auto classTy = StaticCast<ClassTy*>(ty.get());
    CJC_ASSERT(classTy->decl && IsImplReferenceWrapper(*classTy->decl));

    auto& ctor = GetJavaImplWrappingConstructor(*classTy->decl);

    std::vector<OwnedPtr<FuncArg>> refWrapperCtorArgs;
    refWrapperCtorArgs.push_back(CreateFuncArg(ASTCloner::Clone(entity.get())));
    refWrapperCtorArgs.push_back(CreateFuncArg(
        CreateGetRegistryIdCall(std::move(env), std::move(entity))));

    auto refWrapperCtorCall = CreateCallExpr(
        WithinFile(CreateRefExpr(ctor), curFile),
        std::move(refWrapperCtorArgs),
        &ctor, ty, CallKind::CALL_OBJECT_CREATION);

    return refWrapperCtorCall;
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
    castType->SetTy(ty);
    auto varPattern = CreateVarPattern(V_COMPILER, {ty});
    auto curFile = entity->curFile;
    CJC_NULLPTR_CHECK(curFile);
    varPattern->curFile = curFile;
    varPattern->varDecl->curFile = curFile;
    auto varPatternRef = WithinFile(CreateRefExpr(*(varPattern->varDecl)), curFile);
    CJC_ASSERT_WITH_MSG(!mirror.generic->typeParameters.empty(), "JArray type must be generic");
    auto genericParam = mirror.generic->typeParameters[0].get();
    auto entityPtr = entity.get();

    return CreateMatchByTypeArgument(genericParam,
        GenerateTypeMappingWithSelector([this, &entityPtr, &mirror](TypeKind kind, Ptr<Ty> ty) {
            return SelectEntityUnwrapperByTypeKind(kind, ty, entityPtr, mirror);
        }),
        {ty}, CreateMatchWithTypeCast(std::move(entity), ty));
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
    fdRef->SetTy(typeManager.GetInstantiatedTy(funcDecl->GetTy(), GenerateTypeMapping(*funcDecl, fdRef->instTys)));
    auto callExpr = CreateCallExpr(
        std::move(fdRef), std::move(callArgs), funcDecl, {actualEntityTy}, CallKind::CALL_DECLARED_FUNCTION);
    callExpr->curFile = curFile;
    return std::move(callExpr);
}

/**
  * Lift Java boundary reference (possibly null) into a typed CJ value
  */
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
        entity = CreateEnsureNotNullCall(std::move(entity));
		// Convert jstring from JavaEntity to Cangjie String.
        return CreateJavaStringToCangjieCall(CreateGetJniEnvCall(curFile), std::move(entity));
    }

    if (ty->IsCoreOptionType()) {
        auto classLikeDecl = DynamicCast<const ClassLikeDecl*>(&outerDecl);
        CJC_NULLPTR_CHECK(classLikeDecl);
        CJC_ASSERT_WITH_MSG(!ty->TyArgs().empty(), "Option type must be generic");
        ModalTy actualTy{ty->TyArg(0)};
        auto actualDecl = Ty::GetDeclOfTy(actualTy);
        if (actualDecl->IsJavaMirror() || actualTy->IsString()) {
            return UnwrapJavaMirrorOption(std::move(entity), ty, *classLikeDecl, toRaw);
        }

        return UnwrapJavaImplOption(CreateGetJniEnvCall(curFile), std::move(entity), ty, *classLikeDecl, toRaw);
    }

    if (ty->IsTuple()) {
        return CreateGetFromRegistryByEntityCall(CreateGetJniEnvCall(curFile), std::move(entity), ty, false);
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

    if (decl->IsJavaMirror()) {
        return CreateMirrorConstructorCall(importManager, std::move(entity), ty);
    } else {
        CJC_ASSERT(IsImpl(*decl));
        return UnwrapJavaImpl(CreateGetJniEnvCall(curFile), std::move(entity), ty);
    }
}

OwnedPtr<FuncDecl> InteropLibBridge::CreateDeletingGlobalRefFinalizer(ClassDecl& decl)
{
    static auto unitTy = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
    auto curFile = decl.curFile;
    auto fbody = CreateFuncBody({}, nullptr, CreateBlock({}, {unitTy}), {unitTy});
    fbody->paramLists.emplace_back(MakeOwned<FuncParamList>());
    auto delCall = CreateDeleteGlobalRefCall(CreateGetJniEnvCall(curFile), CreateJavaRefCall(decl, curFile));
    fbody->body->body.emplace_back(std::move(delCall));
    auto fd = CreateFuncDecl("~init", std::move(fbody), {typeManager.GetFunctionTy({}, {unitTy})});
    fd->EnableAttr(Attribute::PRIVATE, Attribute::FINALIZER, Attribute::IN_CLASSLIKE);
    fd->linkage = Linkage::EXTERNAL;
    fd->funcBody->funcDecl = fd.get();
    fd->fullPackageName = decl.fullPackageName;
    fd->outerDecl = Ptr(&decl);
    // The generated finalizer must be attached to a file, otherwise downstream passes (e.g. CHIR
    // AST2CHIR::CreateFuncSignatureAndSetGlobalCache) dereference a null `curFile`. `decl.curFile`
    // is guaranteed non-null here since the finalizer body was built from it above.
    fd->curFile = curFile;
    return fd;
}

bool InteropLibBridge::IsInteropLibAccessible(const ImportManager& importManager)
{
    return importManager.GetPackageDecl(INTEROPLIB_PACKAGE_NAME);
}

bool InteropLibBridge::IsInteropLibAccessible() const
{
    return IsInteropLibAccessible(importManager);
}

void InteropLibBridge::CheckInteropLibVersion()
{
    auto versionDecl = GetInteropLibVersionVarDecl();

    if (!versionDecl && !IsInteropLibAccessible()) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_java_mirror_interoplib_must_be_imported, DEFAULT_POSITION);
        return;
    }

    if (!versionDecl || !versionDecl->initializer || versionDecl->initializer->astKind != ASTKind::LIT_CONST_EXPR ||
        versionDecl->initializer->TyKind() != TypeKind::TYPE_INT64) {
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
