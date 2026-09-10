// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "JavaDesugarManager.h"
#include "GenerateInJavaImplReferenceWrapper.h"
#include "NativeFFI/Utils.h"
#include "Utils.h"
#include "NativeFFI/Java/Utils.h"
#include "InteropLibBridge.h"

#include "cangjie/AST/Create.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Types.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Utils/CheckUtils.h"
#include "cangjie/Utils/SafePointer.h"

namespace Cangjie::Native::FFI::Java {
using namespace Cangjie::Interop::Java;

namespace {

OwnedPtr<CallExpr> CreateRegistryCompanionConstructorCall(ClassDecl& companion, VarDecl& javaRefField)
{
    auto curFile = companion.curFile;
    auto& companionCtor = *GetJavaImplRegistryCompanionConstructor(companion);
    std::vector<OwnedPtr<FuncArg>> args;
    args.emplace_back(CreateFuncArg(WithinFile(CreateRefExpr(javaRefField), curFile)));

    return CreateCallExpr(
        WithinFile(CreateRefExpr(companionCtor), curFile),
        std::move(args),
        &companionCtor, companion.GetTy(), CallKind::CALL_OBJECT_CREATION);
}

OwnedPtr<AssignExpr> CreateRegistryCompanionRefFieldAssignment(ClassDecl& companion, ClassDecl& refWrapper,
    VarDecl& companionRefField)
{
    auto curFile = refWrapper.curFile;
    // `$reg = <companion>(this.$obj)`
    return CreateAssignExpr(WithinFile(CreateRefExpr(companionRefField), curFile),
        CreateRegistryCompanionConstructorCall(companion, *GetJavaRefField(refWrapper)),
        {TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT)});
}

/**
  * For original PrimaryCtorDecl, insert dummy registry companion reference initializer
  * since it is not actually used after desugaring into FuncDecl, but still participates in CHIR-level checks.
  */
void RewriteRefWrapperPrimaryConstructor(ClassDecl& refWrapper, ClassDecl& companion, VarDecl& companionRefField)
{
    CJC_ASSERT(Interop::Java::IsImplReferenceWrapper(refWrapper));

    for (auto member : refWrapper.GetMemberDeclPtrs()) {
        auto pctor = As<ASTKind::PRIMARY_CTOR_DECL>(member);
        if (!pctor) {
            continue;
        }

        auto init = CreateRegistryCompanionRefFieldAssignment(companion, refWrapper, companionRefField);
        auto& pbody = pctor->funcBody->body->body;
        auto initPos = pbody.begin();

        if (!pbody.empty()) {
            auto firstNode = pbody[0].get();
            if (auto callExpr = As<ASTKind::CALL_EXPR>(firstNode); callExpr && IsSuperConstructorCall(*callExpr)) {
                // Insert field initialization right after parent constructor call.
                initPos++;
            }
        }

        pbody.insert(initPos, std::move(init));
    }
}
} // namespace

OwnedPtr<VarDecl> GenerateJavaImplRegistryCompanionReferenceField::CreateJavaImplRegistryCompanionReferenceField(
    ClassDecl& companion) const
{
    constexpr auto registryCompanionReferenceFieldName = "$reg";
    auto field = CreateVarDecl(registryCompanionReferenceFieldName, nullptr, CreateRefType(companion));
    field->SetTy(companion.GetTy());
    field->doNotExport = true;
    field->isVar = false;
    field->EnableAttr(Attribute::PRIVATE);
    return field;
}

VarDecl& GenerateJavaImplRegistryCompanionReferenceField::InsertJavaImplRegistryCompanionReferenceField(
    ClassDecl& companion, ClassDecl& refWrapper) const
{
    auto field = WithinFile(CreateJavaImplRegistryCompanionReferenceField(companion), refWrapper.curFile);
    auto& fieldRef = *field;
    field->outerDecl = &refWrapper;
    field->fullPackageName = refWrapper.fullPackageName;
    field->moduleName = refWrapper.moduleName;
    field->begin = refWrapper.body->begin;
    field->end = refWrapper.body->begin;
    field->EnableAttr(Attribute::IN_CLASSLIKE);

    refWrapper.GetMemberDecls().emplace(refWrapper.GetMemberDecls().begin(), std::move(field));

    return fieldRef;
}

void GenerateInJavaImplReferenceWrapper::GenerateWrappingConstructorBody(FuncDecl& wrappingCtor,
    VarDecl& companionRefField, ClassDecl& companion) const
{
    auto curFile = wrappingCtor.curFile;

    CJC_ASSERT(wrappingCtor.outerDecl && wrappingCtor.outerDecl->astKind == ASTKind::CLASS_DECL);
    auto& refWrapper = *StaticAs<ASTKind::CLASS_DECL>(wrappingCtor.outerDecl);

    auto& params = wrappingCtor.funcBody->paramLists[0]->params;

    CJC_ASSERT(!params.empty()); // params[0] is java entity parameter
    auto& entityParam = *params[0];

    CJC_ASSERT(params.size() > 1); // params[1] is registry id parameter
    auto& regIdParam = *params[1];

    wrappingCtor.constructorCall = ConstructorCall::SUPER;

    Ptr<FuncDecl> parentCtor = GetJavaMirrorWrappingConstructor(*refWrapper.GetSuperClassDecl());
    CJC_ASSERT(parentCtor);
    CJC_ASSERT(parentCtor->funcBody->paramLists[0]->params.size() == 1); // Java_CFFI_JavaEntity

    auto superCall = CreateSuperCall(refWrapper, *parentCtor, parentCtor->DataTy());
    superCall->args.push_back(CreateFuncArg(WithinFile(CreateRefExpr(entityParam), curFile)));

    auto& block = wrappingCtor.funcBody->body;
    block->SetTy(refWrapper.GetTy());

    // this.$reg = getFromRegistryById<companion.ty>(regId)
    auto regCompanionAssignment = CreateAssignExpr(WithinFile(CreateRefExpr(companionRefField), curFile),
        ilib.CreateGetFromRegistryCall(
            ilib.CreateGetJniEnvCall(curFile), WithinFile(CreateRefExpr(regIdParam), curFile), companion.DataTy()),
        {TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT)});

    block->body.insert(block->body.begin(), std::move(regCompanionAssignment));
    block->body.insert(block->body.begin(), std::move(superCall));
}

OwnedPtr<FuncDecl> GenerateInJavaImplReferenceWrapper::GenerateJavaSideConstructor(FuncDecl& userCtor,
    VarDecl& companionRefField, ClassDecl& companion) const
{
    auto curFile = userCtor.curFile;
    auto& refWrapper = *StaticAs<ASTKind::CLASS_DECL>(userCtor.outerDecl);
    auto ctor = ASTCloner::Clone(Ptr(&userCtor));

    CJC_ASSERT(!ctor->funcBody->paramLists.empty());
    auto& params = ctor->funcBody->paramLists[0]->params;

    // reg: companion.ty
    auto& regCompanionParam = **params.emplace(params.begin(),
        CreateFuncParam("reg", CreateRefType(companion), nullptr, companion.GetTy()));

    // obj: Java_CFFI_JavaEntity
    auto javaEntityDecl = ilib.GetJavaEntityDecl();
    if (!javaEntityDecl) {
        return nullptr;
    }

    auto& entityParam = **params.emplace(params.begin(),
        CreateFuncParam(JAVA_IMPL_ENTITY_ARG_NAME_IN_GENERATED_CTOR,
            CreateRefType(*javaEntityDecl), nullptr, javaEntityDecl->GetTy()));

    Ptr<FuncDecl> parentCtor = GetJavaMirrorWrappingConstructor(*refWrapper.GetSuperClassDecl());
    CJC_ASSERT(parentCtor);
    CJC_ASSERT(parentCtor->funcBody->paramLists[0]->params.size() == 1); // Java_CFFI_JavaEntity

    std::vector<ModalTy> paramTys;
    paramTys.push_back(javaEntityDecl->GetTy());
    paramTys.push_back(companion.GetTy());
    for (auto paramTy : StaticCast<FuncTy*>(userCtor.GetTy().get())->paramTys) {
        paramTys.push_back(paramTy);
    }
    auto ctorTy = typeManager.GetFunctionTy(paramTys, StaticCast<FuncTy*>(userCtor.GetTy().get())->retTy);

    auto superCall = CreateSuperCall(refWrapper, *parentCtor, parentCtor->DataTy());
    superCall->args.push_back(CreateFuncArg(WithinFile(CreateRefExpr(entityParam), curFile)));

    auto& block = ctor->funcBody->body;

    block->body.erase(std::remove_if(block->body.begin(), block->body.end(),
        [](auto& node) {
            if (auto call = As<ASTKind::CALL_EXPR>(node.get())) {
                return IsSuperConstructorCall(*call);
            }
            return false;
        }),
        block->body.end());

    auto regCompanionAssignment = CreateAssignExpr(WithinFile(CreateRefExpr(companionRefField), curFile),
        WithinFile(CreateRefExpr(regCompanionParam), curFile), {TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT)});

    block->body.insert(block->body.begin(), std::move(regCompanionAssignment));
    block->body.insert(block->body.begin(), std::move(superCall));

    ctor->funcBody->SetTy({ctorTy});
    ctor->SetTy({ctorTy});
    ctor->funcBody->funcDecl = ctor.get();
    ctor->constructorCall = ConstructorCall::SUPER;
    ctor->DisableAttr(Attribute::PRIMARY_CONSTRUCTOR);
    ctor->EnableAttr(Attribute::UNSAFE);
    return ctor;
}

FuncDecl& GenerateInJavaImplReferenceWrapper::InsertJavaSideConstructor(FuncDecl& userCtor,
    VarDecl& companionRefField, ClassDecl& companion, AfterTypeCheckContext& ctx) const
{
    CJC_ASSERT(userCtor.outerDecl && IsImpl(*userCtor.outerDecl));
    // reference wrapper is a class.
    auto refWrapper = StaticAs<ASTKind::CLASS_DECL>(userCtor.outerDecl);
    auto ctor = GenerateJavaSideConstructor(userCtor, companionRefField, companion);
    auto& res = *ctor;
    ctor->outerDecl = refWrapper;
    ctor->fullPackageName = refWrapper->fullPackageName;
    ctor->moduleName = refWrapper->moduleName;
    ctor->begin = refWrapper->body->begin;
    ctor->end = refWrapper->body->begin;
    ctor->funcBody->parentClassLike = refWrapper;
    ctor->EnableAttr(Attribute::IN_CLASSLIKE, Attribute::PROTECTED);

    refWrapper->GetMemberDecls().emplace_back(std::move(ctor));
    ctx.CacheJavaImplReferenceWrapperConstructorsPair(userCtor, res);
    return res;
}

void GenerateInJavaImplReferenceWrapper::RewriteUserDefinedConstructorInitialization(FuncDecl& ctor,
    VarDecl& companionRefField, ClassDecl& companion) const
{
    auto curFile = ctor.curFile;
    auto jniEnvCall = ilib.CreateGetJniEnvCall(curFile);
    if (!jniEnvCall) {
        ctor.EnableAttr(Attribute::IS_BROKEN);
        return;
    }

    auto jniEnvPtrDecl = ilib.GetJniEnvPtrDecl();
    if (!jniEnvPtrDecl) {
        ctor.EnableAttr(Attribute::IS_BROKEN);
        return;
    }

    auto& refWrapper = *StaticAs<ASTKind::CLASS_DECL>(ctor.outerDecl);
    FuncDecl& parentCtor = *GetJavaMirrorWrappingConstructor(*refWrapper.GetSuperClassDecl());
    CJC_ASSERT(parentCtor.funcBody->paramLists[0]->params.size() == 1); // Java_CFFI_JavaEntity

    auto jniEnvVar = CreateTmpVarDecl(jniEnvPtrDecl->type, jniEnvCall);

    CJC_ASSERT(ctor.funcBody);
    CJC_ASSERT(ctor.funcBody->paramLists.size() == 1);
    auto& paramList = *ctor.funcBody->paramLists[0];

    // No global reference is created here:
    // it is wrapped into global reference in `super` constructors chain call: within `JObject`.
    auto javaCtorExpr = ilib.CreateJavaConstructorBlock(refWrapper.DataTy(), paramList, curFile, false);
    if (!javaCtorExpr) {
        ctor.EnableAttr(Attribute::IS_BROKEN);
        companion.EnableAttr(Attribute::HAS_BROKEN, Attribute::IS_BROKEN);
        return;
    }

    auto superCall = CreateSuperCall(*ctor.outerDecl, parentCtor, parentCtor.DataTy());
    superCall->args.insert(superCall->args.begin(), CreateFuncArg(std::move(javaCtorExpr)));

    if (!ctor.funcBody->body->body.empty()) {
        auto firstNode = ctor.funcBody->body->body[0].get();
        if (auto callExpr = As<ASTKind::CALL_EXPR>(firstNode); callExpr && IsSuperConstructorCall(*callExpr)) {
             // This super-constructor call `callExpr` is removed in `JavaSourceCodeGenerator`.
            // TODO: remove it here and memoize with context/cache.
            callExpr->EnableAttr(Attribute::JAVA_MIRROR, Attribute::UNREACHABLE);
        }
    }

     // Inserts assignment of registry companion into an instance.
    ctor.funcBody->body->body.insert(ctor.funcBody->body->body.begin(),
        CreateRegistryCompanionRefFieldAssignment(companion, refWrapper, companionRefField));
    // Inserts generated super call at the beginning of the constructor.
    ctor.funcBody->body->body.insert(ctor.funcBody->body->body.begin(), std::move(superCall));
}

void GenerateInJavaImplReferenceWrapper::RewriteUserDefinedConstructorsInitialization(AfterTypeCheckContext& ctx,
    ClassDecl& refWrapper, VarDecl& companionRefField, ClassDecl& companion) const
{
    for (auto ctor : ctx.GetJavaImplUserDefinedConstructors(refWrapper)) {
        RewriteUserDefinedConstructorInitialization(*ctor, companionRefField, companion);
    }

    RewriteRefWrapperPrimaryConstructor(refWrapper, companion, companionRefField);
}

void GenerateInJavaImplReferenceWrapper::InsertFinalizer(ClassDecl& refWrapper) const
{
    refWrapper.body->decls.emplace_back(ilib.CreateDeletingGlobalRefFinalizer(refWrapper));
}

void GenerateInJavaImplReferenceWrapper::Process(ClassDecl& refWrapper, AfterTypeCheckContext& ctx) const
{
    CJC_ASSERT(IsImplReferenceWrapper(refWrapper));

    auto registryCompanionName = GetImplRegistryCompanionClassName(refWrapper);
    auto registryCompanion = importManager.GetImportedDecl(refWrapper.fullPackageName, registryCompanionName);
    CJC_NULLPTR_CHECK(registryCompanion);
    CJC_ASSERT(registryCompanion->astKind == ASTKind::CLASS_DECL);

    auto& registryCompanionClass = *StaticAs<ASTKind::CLASS_DECL>(registryCompanion);

    auto& regCompanionRefField = ctx.GetJavaImplRegistryCompanionReferenceField(refWrapper);

    auto& wrappingCtor = ctx.GetJavaImplWrappingConstructor(refWrapper);
    GenerateWrappingConstructorBody(wrappingCtor, regCompanionRefField, registryCompanionClass);

    for (auto userDefinedCtor : ctx.GetJavaImplUserDefinedConstructors(refWrapper)) {
        InsertJavaSideConstructor(*userDefinedCtor, regCompanionRefField, registryCompanionClass, ctx);
    }

    RewriteUserDefinedConstructorsInitialization(ctx, refWrapper, regCompanionRefField, registryCompanionClass);
    InsertFinalizer(refWrapper);
}

GenerateInJavaImplReferenceWrapper::GenerateInJavaImplReferenceWrapper(TypeManager& typeManager,
    const ImportManager& importManager, InteropLibBridge& ilib)
    : typeManager(typeManager), importManager(importManager), ilib(ilib)
{
}

void GenerateJavaImplRegistryCompanionReferenceField::Process(AfterTypeCheckContext& ctx)
{
    for (auto refWrapper : ctx.GetJavaImplReferenceWrappers()) {
        auto& companion = ctx.GetJavaImplRegistryCompanion(*refWrapper);
        auto& field = InsertJavaImplRegistryCompanionReferenceField(companion, *refWrapper);
        ctx.CacheJavaImplRegistryCompanionReferenceField(*refWrapper, field);
    }
}

void GenerateInJavaImplReferenceWrapper::Process(AfterTypeCheckContext& ctx)
{
    GenerateJavaImplRegistryCompanionReferenceField regCompanionFieldStage;
    regCompanionFieldStage(ctx);

    for (auto& refWrapper : ctx.GetJavaImplReferenceWrappers()) {
        if (refWrapper->TestAttr(Attribute::IS_BROKEN)) {
            continue;
        }

        Process(*refWrapper, ctx);
    }
}

} // namespace Cangjie::Native::FFI::Java
