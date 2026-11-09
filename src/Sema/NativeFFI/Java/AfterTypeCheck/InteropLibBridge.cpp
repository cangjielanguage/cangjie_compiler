// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "InteropLibBridge.h"
#include "Desugar/AfterTypeCheck.h"
#include "NativeFFI/Java/AfterTypeCheck/Utils.h"
#include "NativeFFI/Java/Utils.h"
#include "NativeFFI/Utils.h"
#include "TypeCheckUtil.h"
#include "cangjie/AST/Clone.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Sema/TypeManager.h"
#include "cangjie/Utils/CastingTemplate.h"
#include "cangjie/Utils/CheckUtils.h"
#include <utility>

namespace {

// vars
constexpr auto INTEROPLIB_VERSION_FIELD_ID = "INTEROPLIB_VERSION";

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
constexpr auto INTEROPLIB_CFFI_GET_INSTANCE_FIELD_ID = "Java_CFFI_getInstanceFieldId";
constexpr auto INTEROPLIB_CFFI_GET_STATIC_FIELD_ID = "Java_CFFI_getStaticFieldId";
constexpr auto INTEROPLIB_JNI_HANDLE_PENDING_EXCEPTION_DECL_ID = "handlePendingException";
constexpr auto INTEROPLIB_CFFI_SWAP_LOCAL_WITH_GLOBAL_REF_ID = "Java_CFFI_swapLocalWithGlobalRef";
constexpr auto INTEROPLIB_CFFI_NEW_GLOBAL_REF_ID = "Java_CFFI_newGlobalRef";
constexpr auto INTEROPLIB_CFFI_DELETE_GLOBAL_REF_ID = "Java_CFFI_deleteGlobalRef";
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
constexpr auto INTEROPLIB_CFFI_JAVA_CFFI_GET_CLASS = "Java_CFFI_getClass";

} // namespace

using namespace Cangjie;
using namespace AST;
using namespace TypeCheckUtil;
using namespace Sema::Desugar::AfterTypeCheck;

namespace Cangjie::Interop::Java {

using namespace Cangjie::Native::FFI;
using namespace Cangjie::Native::FFI::Java;

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

Ptr<FuncDecl> InteropLibBridge::GetDeleteGlobalRefDecl() const
{
    static auto deleteGlobalRef = GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_CFFI_DELETE_GLOBAL_REF_ID);
    return deleteGlobalRef;
}

Ptr<FuncDecl> InteropLibBridge::GetSwapLocalWithGlobalRefDecl() const
{
    static auto deleteLocalRef = GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_CFFI_SWAP_LOCAL_WITH_GLOBAL_REF_ID);
    return deleteLocalRef;
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

OwnedPtr<CallExpr> InteropLibBridge::CreateAsJvalueCall(OwnedPtr<Expr> javaEntity) const
{
    return CreateMemberCall(std::move(javaEntity), FindAsJValueDecl());
}

OwnedPtr<CallExpr> InteropLibBridge::CreateAsJniJobjectCall(OwnedPtr<Expr> javaEntity) const
{
    return CreateMemberCall(std::move(javaEntity), FindAsJObjectDecl());
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

Ptr<FuncDecl> InteropLibBridge::GetGetInstanceFieldIdDecl() const
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_CFFI_GET_INSTANCE_FIELD_ID);
}

Ptr<FuncDecl> InteropLibBridge::GetGetStaticFieldIdDecl() const
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_CFFI_GET_STATIC_FIELD_ID);
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

Ptr<FuncDecl> InteropLibBridge::GetGetClassDecl() const
{
    return GetInteropLibDecl<ASTKind::FUNC_DECL>(INTEROPLIB_CFFI_JAVA_CFFI_GET_CLASS);
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

AST::Ty& InteropLibBridge::GetJniJClassTy() const
{
    static auto ty = typeManager.GetPointerTy(typeManager.GetPrimitiveTy(TypeKind::TYPE_UNIT));
    CJC_NULLPTR_CHECK(ty);
    return *ty;
}

AST::Ty& InteropLibBridge::GetJniJmethodIdTy() const
{
    static auto ty = typeManager.GetPointerTy(typeManager.GetPrimitiveTy(TypeKind::TYPE_UNIT));
    CJC_NULLPTR_CHECK(ty);
    return *ty;
}

AST::Ty& InteropLibBridge::GetJniJfieldIdTy() const
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
        if (auto decl = classLTy->commonDecl; decl && (decl->IsJavaForeign() || decl->IsJavaExport())) {
            return CreateJavaRefCall(std::move(arg));
        }
    } else if (arg->GetTy()->IsCoreOptionType()) {
        CJC_ASSERT_WITH_MSG(!arg->GetTy()->typeArgs.empty(), "Option type must be generic");
        if (auto classALTy = DynamicCast<ClassLikeTy*>(arg->GetTy()->TyArg(0))) {
            if (auto decl = classALTy->commonDecl; decl && (decl->IsJavaForeign() || decl->IsJavaExport())) {
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

    if (IsOptionOfString(cjExpr->DataTy())) {
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

OwnedPtr<Expr> InteropLibBridge::SelectJSigByTypeKind([[maybe_unused]] TypeKind kind, Ptr<Ty> ty)
{
    static auto strTy = utils.GetStringDecl().GetTy();
    return CreateLitConstExpr(LitConstKind::STRING, GetJniTypeSignature(*ty), strTy);
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
    [[maybe_unused]] TypeKind kind, [[maybe_unused]] Ptr<Ty> ty, Ptr<Expr> entity, Ptr<const Decl> scope)
{
    auto cEntity = ASTCloner::Clone(entity);
    cEntity->curFile = entity->curFile;
    return CreateMatchWithTypeCast(UnwrapJavaEntity(std::move(cEntity), ty, scope), ty);
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

OwnedPtr<CallExpr> InteropLibBridge::CreateNewGlobalRefCall(OwnedPtr<Expr> env, OwnedPtr<Expr> obj)
{
    auto curFile = obj->curFile;
    return CreateCall(GetNewGlobalRefDecl(), curFile, std::move(env), std::move(obj));
}

OwnedPtr<CallExpr> InteropLibBridge::CreateSwapLocalWithGlobalRefCall(OwnedPtr<Expr> env, OwnedPtr<Expr> localRef) const
{
    auto curFile = localRef->curFile;
    return CreateCall(GetSwapLocalWithGlobalRefDecl(), curFile, std::move(env), std::move(localRef));
}

OwnedPtr<CallExpr> InteropLibBridge::CreateDeleteGlobalRefCall(OwnedPtr<Expr> env, OwnedPtr<Expr> obj) const
{
    auto curFile = obj->curFile;
    return CreateCall(GetDeleteGlobalRefDecl(), curFile, std::move(env), std::move(obj));
}

OwnedPtr<CallExpr> InteropLibBridge::CreateGetMethodIdCall(Ptr<Expr> env, OwnedPtr<Expr> clazz,
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

OwnedPtr<CallExpr> InteropLibBridge::CreateGetFieldIdCall(Ptr<Expr> env, OwnedPtr<Expr> clazz,
    const Native::FFI::Java::JavaMemberSignature javaField)
{
    static auto getInstanceFieldIdDecl = GetGetInstanceFieldIdDecl();
    static auto getStaticFieldIdDecl = GetGetStaticFieldIdDecl();
    auto getFieldIdDecl = javaField.IsStatic() ? getStaticFieldIdDecl : getInstanceFieldIdDecl;
    static auto strTy = utils.GetStringDecl().GetTy();
    auto fieldNameLit = CreateLitConstExpr(LitConstKind::STRING, javaField.GetName(), strTy);
    auto fieldSignatureLit = CreateLitConstExpr(LitConstKind::STRING, javaField.GetSignature(), strTy);
    return CreateCall(getFieldIdDecl, env->curFile,
        ASTCloner::Clone(env), std::move(clazz), std::move(fieldNameLit), std::move(fieldSignatureLit));
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

OwnedPtr<Expr> InteropLibBridge::CreateJNIHandlePendingExceptionCall(Ptr<Expr> jniEnvPtr)
{
    auto curFile = jniEnvPtr->curFile;
    static auto decl = GetJNIHandlePendingExceptionDecl();
    return CreateCall(decl, curFile, ASTCloner::Clone(jniEnvPtr));
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

OwnedPtr<Expr> InteropLibBridge::CreateJValueExpr(OwnedPtr<Expr> expr)
{
    CJC_NULLPTR_CHECK(expr);
    auto curFile = expr->curFile;
    auto ty = expr->GetTy();
    if (ty->IsPrimitive()) {
        switch (ty->kind) {
            case TypeKind::TYPE_BOOLEAN: {
                static ModalTy jboolty{DataTy(TypeManager::GetPrimitiveTy(TypeKind::TYPE_UINT8))};
                // Convert to jboolean (uint8)
                return CreateIfExpr(std::move(expr),
                    CreateBlock(Nodes(CreateLitConstExpr(LitConstKind::INTEGER, "1", jboolty)), jboolty),
                    CreateBlock(Nodes(CreateLitConstExpr(LitConstKind::INTEGER, "0", jboolty)), jboolty),
                    jboolty);
            }
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

    auto entityExpr = IsJavaEntityTy(*ty) ? std::move(expr) : WrapJavaEntity(std::move(expr));
    return CreateAsJvalueCall(std::move(entityExpr));
}

OwnedPtr<Expr> InteropLibBridge::CreateJObjectExpr(OwnedPtr<Expr> cjExpr)
{
    auto entityExpr = WrapJavaEntity(std::move(cjExpr));
    return CreateAsJniJobjectCall(std::move(entityExpr));
}

OwnedPtr<Expr> InteropLibBridge::ConvertJavaResultToCJ(OwnedPtr<Expr> result, Ptr<Ty> resultTy, const Ptr<Decl> scope)
{
    CJC_NULLPTR_CHECK(result);
    CJC_NULLPTR_CHECK(resultTy);
    if (!resultTy->IsPrimitive()) {
        return UnwrapJavaEntity(std::move(result), resultTy, scope);
    }
    if (resultTy->kind == TypeKind::TYPE_BOOLEAN) {
        auto zero = CreateLitConstExpr(LitConstKind::INTEGER, "0", result->GetTy()); // JNI_FALSE constant
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
    OwnedPtr<Expr> entity, Ptr<Ty> ty, Ptr<const Decl> scope, bool toRaw)
{
    CJC_ASSERT(ty->IsCoreOptionType());
    auto curFile = entity->curFile;
    CJC_NULLPTR_CHECK(curFile);
    CJC_ASSERT_WITH_MSG(!ty->typeArgs.empty(), "Option type must be generic");
    auto declTy = ty->TyArg(0);
    auto decl = Ty::GetDeclOfTy(declTy);
    CJC_ASSERT(decl->IsJavaForeign() || declTy->IsString() || (toRaw && decl->IsJavaExport()));

    auto actualTy = toRaw ? Ptr(&GetJniJobjectTy()) : ty;

    return utils.CreateOptionMatch(
        CreateGetJavaEntityOrNullCall(std::move(entity)),
        [this, curFile, declTy, scope, toRaw](VarDecl& e) {
            auto unwrapped = UnwrapJavaEntity(WithinFile(CreateRefExpr(e), curFile), declTy, scope, toRaw);
            return unwrapped;
        },
        [this, ty, toRaw]() { return toRaw ? CreateJobjectNull() : utils.CreateOptionNoneRef(ty->TyArg(0)); },
        actualTy);
}

OwnedPtr<Expr> InteropLibBridge::UnwrapJavaImplOption(
    OwnedPtr<Expr> env, OwnedPtr<Expr> entityOption, Ptr<Ty> ty, Ptr<const Decl> scope, bool toRaw)
{
    CJC_ASSERT_WITH_MSG(!ty->typeArgs.empty(), "Option type must be generic");
    auto implTy = ty->typeArgs[0].Ty();
    CJC_ASSERT(implTy->IsClass());
    auto actualTy = toRaw ? Ptr(&GetJniJobjectTy()) : ty;
    auto curFile = entityOption->curFile;

    return utils.CreateOptionMatch(
        CreateGetJavaEntityOrNullCall(std::move(entityOption)),
        [this, curFile, implTy, &env, scope, toRaw](VarDecl& e) {
            if (toRaw) {
                // To @C CPointer appropriate for JNI
                return UnwrapJavaEntity(WithinFile(CreateRefExpr(e), curFile), implTy, scope, toRaw);
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
    CJC_ASSERT(ty->IsGeneric());
    CJC_ASSERT(IsJArray(mirror));
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
            return SelectEntityUnwrapperByTypeKind(kind, ty, entityPtr, Ptr<const Decl>(&mirror));
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
    CJC_NULLPTR_CHECK(curFile);
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
 * Lift Java boundary reference into a typed CJ value
 */
OwnedPtr<Expr> InteropLibBridge::UnwrapJavaEntity(OwnedPtr<Expr> entity, Ptr<Ty> ty, Ptr<const Decl> scope, bool toRaw)
{
    auto curFile = entity->curFile;
    CJC_NULLPTR_CHECK(entity);
    CJC_NULLPTR_CHECK(ty);

    auto isPrimitive = ty->IsPrimitive();
    if (isPrimitive || (toRaw && !ty->IsCoreOptionType())) {
        return UnwrapJavaPrimitiveEntity(std::move(entity), ty);
    }

    if (scope && IsJArray(*scope) && ty->IsGeneric()) {
        return UnwrapJavaArrayEntity(std::move(entity), ty, *StaticAs<ASTKind::CLASS_LIKE_DECL>(scope));
    } else if (ty == GetJavaEntityTy()) {
        return std::move(entity);
    }

    if (ty->IsString()) {
        entity = CreateEnsureNotNullCall(std::move(entity));
		// Convert jstring from JavaEntity to Cangjie String.
        return CreateJavaStringToCangjieCall(CreateGetJniEnvCall(curFile), std::move(entity));
    }

    if (ty->IsCoreOptionType()) {
        CJC_ASSERT_WITH_MSG(!ty->typeArgs.empty(), "Option type must be generic");
        auto actualTy = ty->TyArg(0);
        auto actualDecl = Ty::GetDeclOfTy(actualTy);
        if (actualDecl->IsJavaForeign() || actualTy->IsString()) {
            return UnwrapJavaMirrorOption(std::move(entity), ty, scope, toRaw);
        }

        return UnwrapJavaImplOption(CreateGetJniEnvCall(curFile), std::move(entity), ty, scope, toRaw);
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

    if (decl->IsJavaForeign()) {
        return CreateMirrorConstructorCall(importManager, std::move(entity), ty);
    } else {
        CJC_ASSERT(IsImpl(*decl));
        return UnwrapJavaImpl(CreateGetJniEnvCall(curFile), std::move(entity), ty);
    }
}

OwnedPtr<FuncDecl> InteropLibBridge::CreateDeletingGlobalRefFinalizer(ClassDecl& decl)
{
    static ModalTy unitTy{DataTy(TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT))};
    auto curFile = decl.curFile;
    auto fbody = CreateFuncBody({}, nullptr, CreateBlock({}, {unitTy}), {unitTy});
    fbody->paramLists.emplace_back(MakeOwned<FuncParamList>());
    auto delCall = CreateDeleteGlobalRefCall(CreateGetJniEnvCall(curFile),
        CreateAsJniJobjectCall(CreateJavaRefCall(decl, curFile)));
    fbody->body->body.emplace_back(std::move(delCall));
    auto fd = CreateFuncDecl("~init", std::move(fbody), DataTy(typeManager.GetFunctionTy({}, {unitTy})));
    fd->EnableAttr(Attribute::PRIVATE, Attribute::FINALIZER, Attribute::IN_CLASSLIKE, Attribute::DOES_NOT_THROW);
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

OwnedPtr<Expr> InteropLibBridge::CreateGetClassCall(const Ty& javaTy, Ptr<Expr> envPtr, File& curFile)
{
    return CreateGetClassCall(GetJavaJniClassName(javaTy), envPtr, curFile);
}

OwnedPtr<Expr> InteropLibBridge::CreateGetClassCall(const std::string& className, Ptr<Expr> envPtr, File& curFile)
{
    static auto getClassDecl = GetGetClassDecl();
    CJC_ASSERT_WITH_MSG(!getClassDecl->funcBody->paramLists.empty(), "paramLists cannot be empty");
    CJC_ASSERT_WITH_MSG(getClassDecl->funcBody->paramLists[0]->params.size() == 2, "expected 2 parameters");

    static auto& string = GetStringDecl(importManager);
    auto classNameExpr = CreateLitConstExpr(LitConstKind::STRING, className, string.GetTy());
    return CreateCall(getClassDecl, &curFile, ASTCloner::Clone(envPtr), std::move(classNameExpr));
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
