// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "TypeCheckUtil.h"
#include "JavaDesugarManager.h"

#include "cangjie/AST/AttributePack.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/AST/Match.h"
#include "cangjie/Utils/ConstantsUtils.h"
#include "Utils.h"
#include "NativeFFI/Utils.h"
#include "cangjie/AST/Utils.h"


namespace Cangjie::Interop::Java {
using namespace Cangjie::Native::FFI;

// For an array of object-based type we add additional `get` method to perform unwrapping further,
// at call site with a concrete type
void JavaDesugarManager::InsertArrayJavaEntityGet(ClassDecl& decl)
{
    if (!decl.body) {
        return;
    }

    Ptr<FuncDecl> getOperationDecl;
    for (auto& member : decl.body->decls) {
        auto funcDecl = As<ASTKind::FUNC_DECL>(member);
        if (!funcDecl || !funcDecl->funcBody || funcDecl->funcBody->paramLists.empty()) {
            continue;
        }
        if (funcDecl && funcDecl->identifier == "[]" && funcDecl->funcBody->paramLists[0]->params.size() == 1) {
            getOperationDecl = As<ASTKind::FUNC_DECL>(member);
            break;
        }
    }

    if (!getOperationDecl) {
        return;
    }

    auto javaEntityGetDecl = ASTCloner::Clone(getOperationDecl);
    javaEntityGetDecl->identifier = JAVA_ARRAY_GET_FOR_REF_TYPES;
    javaEntityGetDecl->ty = typeManager.GetFunctionTy(
        {TypeManager::GetPrimitiveTy(TypeKind::TYPE_INT32)}, lib.GetJavaEntityTy());

    auto javaEntity = lib.GetJavaEntityTy();
    if (!javaEntity || !javaEntityGetDecl->funcBody->retType) {
        return;
    }
    javaEntityGetDecl->funcBody->retType->ty = javaEntity;
    decl.body->decls.push_back(std::move(javaEntityGetDecl));
}

// For an array of object-based type we add additional `set` method to perform unwrapping further,
// at call site with a concrete type
void JavaDesugarManager::InsertArrayJavaEntitySet(ClassDecl& decl)
{
    if (!decl.body) {
        return;
    }

    Ptr<FuncDecl> setOperationDecl;
    for (auto& member : decl.body->decls) {
        auto funcDecl = As<ASTKind::FUNC_DECL>(member);
        if (!funcDecl || !funcDecl->funcBody || funcDecl->funcBody->paramLists.empty()) {
            continue;
        }
        if (funcDecl && funcDecl->identifier == "[]" && funcDecl->funcBody->paramLists[0]->params.size() == 2) {
            setOperationDecl = As<ASTKind::FUNC_DECL>(member);
            break;
        }
    }

    if (!setOperationDecl) {
        return;
    }

    auto javaEntity = lib.GetJavaEntityTy();
    auto javaEntitySetDecl = ASTCloner::Clone(setOperationDecl);
    if (!javaEntity || !javaEntitySetDecl->funcBody->retType) {
        return;
    }

    auto unitTy = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
    javaEntitySetDecl->identifier = JAVA_ARRAY_SET_FOR_REF_TYPES;
    javaEntitySetDecl->funcBody->paramLists[0]->params[1]->ty = javaEntity;
    javaEntitySetDecl->ty = typeManager.GetFunctionTy({TypeManager::GetPrimitiveTy(TypeKind::TYPE_INT32), javaEntity},
        unitTy);

    javaEntitySetDecl->funcBody->retType->ty = unitTy;
    decl.body->decls.push_back(std::move(javaEntitySetDecl));
}

void JavaDesugarManager::InsertJavaRefVarDecl(ClassDecl& decl)
{
    auto& javaEntityDecl = *lib.GetJavaEntityDecl();

    auto javaref = CreateVarDecl(JAVA_REF_FIELD_NAME, nullptr, CreateRefType(javaEntityDecl));
    javaref->ty = javaEntityDecl.ty;
    javaref->EnableAttr(Attribute::JAVA_MIRROR);
    javaref->begin = decl.body ? decl.body->begin : decl.begin;
    javaref->curFile = decl.curFile;

    Modifier protectedMod = Modifier(TokenKind::PUBLIC, javaref->begin);
    protectedMod.curFile = decl.curFile;
    javaref->modifiers.emplace(std::move(protectedMod));

    javaref->isVar = false;
    javaref->outerDecl = Ptr(&decl);
    javaref->fullPackageName = decl.fullPackageName;

    decl.body->decls.push_back(std::move(javaref));
}

void JavaDesugarManager::InsertJavaMirrorCtor(ClassDecl& decl, bool doStub)
{
    auto curFile = decl.curFile;
    auto isJObject = IsJObject(decl);
    auto& javaEntityDecl = *lib.GetJavaEntityDecl();

    auto param = CreateFuncParam("$ref", CreateRefType(javaEntityDecl), nullptr, javaEntityDecl.ty);

    std::vector<OwnedPtr<Node>> ctorNodes;

    if (isJObject) {
        // for JObject, body can be created and filled in one step
        if (!doStub) {
            return;
        }
        auto lhsRef = WithinFile(CreateRefExpr(*GetJavaRefField(decl)), curFile);
        auto rhs = lib.CreateNewGlobalRefCall(
            lib.CreateGetJniEnvCall(curFile),
            WithinFile(CreateRefExpr(*param), curFile),
            false);

        auto unitTy = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
        auto refAssignment = CreateAssignExpr(std::move(lhsRef), std::move(rhs), unitTy);
        ctorNodes.push_back(lib.CreateEnsureNotNullCall(WithinFile(CreateRefExpr(*param), curFile)));
        ctorNodes.push_back(std::move(refAssignment));
    } else if (!doStub) {
        auto ctor = GetGeneratedJavaMirrorConstructor(decl);
        CJC_NULLPTR_CHECK(ctor);
        auto actualParam = CreateFuncArg(WithinFile(CreateRefExpr(*ctor->funcBody->paramLists[0]->params[0]), curFile));
        auto& superCtor = *GetGeneratedJavaMirrorConstructor(*decl.GetSuperClassDecl());
        auto superCall = CreateSuperCall(decl, superCtor, superCtor.ty);
        superCall->args.emplace_back(std::move(actualParam));
        ctor->funcBody->body->body.emplace_back(std::move(superCall));
        return;
    }

    std::vector<Ptr<Ty>> ctorFuncParamTys;
    ctorFuncParamTys.push_back(param->ty);
    auto ctorFuncTy = typeManager.GetFunctionTy(std::move(ctorFuncParamTys), decl.ty);

    std::vector<OwnedPtr<FuncParam>> ctorParams;
    ctorParams.push_back(std::move(param));
    auto paramList = CreateFuncParamList(std::move(ctorParams));

    std::vector<OwnedPtr<FuncParamList>> paramLists;
    paramLists.push_back(std::move(paramList));

    auto ctorFuncBody = CreateFuncBody(
        std::move(paramLists),
        CreateRefType(decl),
        CreateBlock(std::move(ctorNodes), decl.ty),
        decl.ty);

    auto fd = CreateFuncDecl("init", std::move(ctorFuncBody), ctorFuncTy);
    fd->fullPackageName = decl.fullPackageName;
    fd->outerDecl = &decl;
    fd->funcBody->funcDecl = fd.get();
    fd->constructorCall = ConstructorCall::SUPER;
    fd->EnableAttr(
        Attribute::PUBLIC, Attribute::JAVA_MIRROR,
        Attribute::IN_CLASSLIKE, Attribute::CONSTRUCTOR);
    fd->funcBody->parentClassLike = &decl;
    fd->begin = decl.begin;
    fd->curFile = decl.curFile;
    fd->fullPackageName = decl.fullPackageName;

    decl.body->decls.emplace_back(std::move(fd));
}

void JavaDesugarManager::InsertJavaMirrorFinalizer(ClassDecl& mirror)
{
    static auto unitTy = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
    auto curFile = mirror.curFile;
    auto fbody = CreateFuncBody({}, nullptr, CreateBlock({}, unitTy), unitTy);
    fbody->paramLists.emplace_back(MakeOwned<FuncParamList>());
    auto delCall = lib.CreateDeleteGlobalRefCall(lib.CreateGetJniEnvCall(curFile), CreateJavaRefCall(mirror, curFile));
    fbody->body->body.emplace_back(std::move(delCall));
    auto fd = CreateFuncDecl("~init", std::move(fbody), typeManager.GetFunctionTy({}, unitTy));
    fd->EnableAttr(Attribute::PRIVATE, Attribute::FINALIZER, Attribute::IN_CLASSLIKE);
    fd->linkage = Linkage::EXTERNAL;
    fd->funcBody->funcDecl = fd.get();
    fd->fullPackageName = mirror.fullPackageName;
    fd->outerDecl = Ptr(&mirror);

    mirror.body->decls.emplace_back(std::move(fd));
}

void JavaDesugarManager::InsertJavaMirrorHasInited(ClassDecl& mirror)
{
    static auto boolTy = typeManager.GetPrimitiveTy(TypeKind::TYPE_BOOLEAN);
    auto curFile = mirror.curFile;
    auto initializer = CreateLitConstExpr(LitConstKind::BOOL, "false", boolTy);
    auto ret = WithinFile(CreateVarDecl(HAS_INITED_IDENT, std::move(initializer)), curFile);
    ret->isVar = true;
    ret->fullPackageName = mirror.fullPackageName;
    ret->outerDecl = Ptr(&mirror);
    ret->EnableAttr(
        Attribute::PRIVATE, Attribute::NO_REFLECT_INFO, Attribute::IN_CLASSLIKE, Attribute::HAS_INITED_FIELD);

    mirror.body->decls.emplace_back(std::move(ret));
}

void JavaDesugarManager::InsertAbstractJavaRefGetter(ClassLikeDecl& decl)
{
    auto& javaEntityDecl = *lib.GetJavaEntityDecl();
    std::vector<OwnedPtr<FuncParam>> callParams;
    std::vector<OwnedPtr<FuncParamList>> paramLists;
    paramLists.emplace_back(CreateFuncParamList(std::move(callParams)));

    auto funcBody = CreateFuncBody(
        std::move(paramLists),
        CreateRefType(javaEntityDecl),
        nullptr,
        TypeManager::GetNothingTy());

    funcBody->parentClassLike = &decl;

    std::vector<Ptr<Ty>> funcParamTys;
    Ptr<FuncTy> funcTy = typeManager.GetFunctionTy(std::move(funcParamTys), javaEntityDecl.ty);

    auto fd = CreateFuncDecl(JAVA_REF_GETTER_FUNC_NAME, std::move(funcBody), funcTy);
    fd->EnableAttr(Attribute::PUBLIC, Attribute::IN_CLASSLIKE, Attribute::ABSTRACT);
    fd->funcBody->funcDecl = fd.get();
    fd->fullPackageName = decl.fullPackageName;
    fd->outerDecl = Ptr(&decl);

    if (auto iDecl = As<ASTKind::INTERFACE_DECL>(&decl)) {
        iDecl->body->decls.emplace_back(std::move(fd));
    } else if (auto clDecl = As<ASTKind::CLASS_DECL>(&decl)) {
        clDecl->body->decls.emplace_back(std::move(fd));
    }
}

void JavaDesugarManager::InsertJavaRefGetterWithBody(ClassDecl& decl)
{
    auto iter = decl.GetMemberDecls().begin();
    for (auto& member : decl.GetMemberDecls()) {
        if (auto fd = DynamicCast<FuncDecl*>(member.get())) {
            if (IsJavaRefGetter(*fd)) {
                // remove stub from parser stage
                decl.GetMemberDecls().erase(iter);
                break;
            }
        }
        iter++;
    }
    auto javaEntityDecl = lib.GetJavaEntityDecl();

    std::vector<OwnedPtr<FuncParam>> callParams;
    std::vector<OwnedPtr<FuncParamList>> paramLists;
    paramLists.push_back(CreateFuncParamList(std::move(callParams)));

    auto javaRef = GetJavaRefField(decl);
    auto ref = CreateRefExpr(*javaRef);

    auto ret = CreateReturnExpr(std::move(ref));
    ret->ty = TypeManager::GetNothingTy();
    std::vector<OwnedPtr<Node>> nodes;
    nodes.emplace_back(std::move(ret));

    auto block = CreateBlock(std::move(nodes), javaEntityDecl->ty);

    auto funcBody = CreateFuncBody(
        std::move(paramLists),
        CreateRefType(*javaEntityDecl),
        std::move(block),
        TypeManager::GetNothingTy());

    std::vector<Ptr<Ty>> funcParamTys;
    Ptr<FuncTy> funcTy = typeManager.GetFunctionTy(std::move(funcParamTys), javaEntityDecl->ty);

    auto fd = CreateFuncDecl(JAVA_REF_GETTER_FUNC_NAME, std::move(funcBody), funcTy);
    fd->EnableAttr(Attribute::PUBLIC, Attribute::IN_CLASSLIKE, Attribute::INITIALIZED,
        Attribute::IS_CHECK_VISITED);
    fd->fullPackageName = decl.fullPackageName;
    fd->funcBody->funcDecl = fd.get();
    fd->funcBody->parentClassLike = &decl;
    fd->outerDecl = &decl;

    decl.body->decls.emplace_back(std::move(fd));
}

void JavaDesugarManager::DesugarJavaMirrorConstructor(FuncDecl& ctor, FuncDecl& generatedCtor)
{
    auto curFile = ctor.curFile;
    CJC_ASSERT(ctor.TestAttr(Attribute::CONSTRUCTOR) && ctor.TestAttr(Attribute::JAVA_MIRROR));
    ctor.constructorCall = ConstructorCall::OTHER_INIT;
    auto thisCall = CreateThisCall(*ctor.outerDecl, generatedCtor, generatedCtor.ty, curFile);

    CJC_ASSERT(ctor.funcBody);
    CJC_ASSERT(ctor.funcBody->paramLists.size() == 1);

    auto jniEnvCall = lib.CreateGetJniEnvCall(curFile);
    if (!jniEnvCall) {
        ctor.EnableAttr(Attribute::IS_BROKEN);
        return;
    }

    auto jniEnvPtrDecl = lib.GetJniEnvPtrDecl();
    if (!jniEnvPtrDecl) {
        ctor.EnableAttr(Attribute::IS_BROKEN);
        return;
    }

    auto jniEnvVar = CreateTmpVarDecl(jniEnvPtrDecl->type, jniEnvCall);
    OwnedPtr<CallExpr> newObjectCall;

    if (IsJArray(*ctor.outerDecl)) {
        auto genericParam = ctor.outerDecl->generic->typeParameters[0].get();
        newObjectCall = lib.CreateCFFINewJavaArrayCall(
            WithinFile(CreateRefExpr(*jniEnvVar), curFile), *ctor.funcBody->paramLists[0], genericParam);
    } else {
        newObjectCall = lib.CreateCFFINewJavaObjectCall(
            WithinFile(CreateRefExpr(*jniEnvVar), curFile), utils.GetJavaClassNormalizeSignature(*ctor.outerDecl->ty),
            *ctor.funcBody->paramLists[0], true, *curFile);
    }

    if (newObjectCall) {
        std::vector<OwnedPtr<Node>> nodes;
        nodes.push_back(std::move(jniEnvVar));
        nodes.push_back(std::move(newObjectCall));

        thisCall->args.push_back(CreateFuncArg(WrapReturningLambdaCall(typeManager, std::move(nodes))));
        ctor.funcBody->body->body.push_back(std::move(thisCall));
    } else {
        ctor.EnableAttr(Attribute::IS_BROKEN);
    }
}

void JavaDesugarManager::AddJavaMirrorMethodBody(const ClassLikeDecl& mirror, FuncDecl& fun, OwnedPtr<Expr> javaRefCall)
{
    if (fun.TestAttr(Attribute::ABSTRACT) && !IsSynthetic(mirror)) {
        return;
    }
    auto curFile = fun.curFile;
    CJC_NULLPTR_CHECK(curFile);

    OwnedPtr<CallExpr> jniEnvCall = lib.CreateGetJniEnvCall(curFile);
    if (!jniEnvCall) {
        fun.EnableAttr(Attribute::IS_BROKEN);
        return;
    }

    OwnedPtr<Expr> methodCall;
    auto& paramList = *fun.funcBody->paramLists[0].get();
    if (fun.TestAttr(Attribute::STATIC)) {
        methodCall = lib.CreateCFFICallStaticMethodCall(
            std::move(jniEnvCall), MemberJNISignature(utils, fun),
            paramList, *curFile);
    } else {
        if (IsJArray(mirror)) {
            auto genericParam = mirror.generic->typeParameters[0].get();
            CJC_ASSERT(genericParam);
            methodCall = lib.CreateCFFICallArrayMethodCall(
                std::move(jniEnvCall), std::move(javaRefCall), paramList, genericParam,
                GetArrayOperationKind(fun));
        } else {
            methodCall = lib.CreateCFFICallMethodCall(
                std::move(jniEnvCall), std::move(javaRefCall),
                MemberJNISignature(utils, fun),
                paramList, *fun.curFile);
        }
    }

    if (!methodCall) {
        fun.EnableAttr(Attribute::IS_BROKEN);
        return;
    }

    auto methodCallRes = CreateTmpVarDecl(nullptr, std::move(methodCall));
    methodCallRes->ty = methodCallRes->initializer->ty;
    CopyBasicInfo(methodCallRes->initializer.get(), methodCallRes.get());
    methodCallRes->begin = fun.begin;
    methodCallRes->curFile = fun.curFile;

    auto callResRef = CreateRefExpr(*methodCallRes);
    CopyBasicInfo(methodCallRes.get(), callResRef.get());
    auto unwrapJavaEntityCall = lib.UnwrapJavaEntity(std::move(callResRef), fun.funcBody->retType->ty, mirror);
    if (!unwrapJavaEntityCall) {
        fun.EnableAttr(Attribute::IS_BROKEN);
        return;
    }
    std::vector<OwnedPtr<Node>> blockNodes;
    auto retExpr = CreateReturnExpr(std::move(unwrapJavaEntityCall), fun.funcBody.get());
    retExpr->ty = TypeManager::GetNothingTy();
    retExpr->refFuncBody = fun.funcBody.get();
    blockNodes.push_back(std::move(methodCallRes));
    blockNodes.push_back(std::move(retExpr));
    // Return type has to be specified, so it's safe to use it below
    fun.funcBody->body = CreateBlock(std::move(blockNodes), fun.funcBody->retType->ty);
    fun.funcBody->ty = TypeManager::GetNothingTy();
    if (IsSynthetic(mirror)) {
        fun.DisableAttr(Attribute::ABSTRACT);
    }
}

void JavaDesugarManager::DesugarJavaMirrorMethod(FuncDecl& fun, ClassLikeDecl& mirror)
{
    CJC_ASSERT(!fun.TestAttr(Attribute::CONSTRUCTOR) &&
        (fun.TestAttr(Attribute::JAVA_MIRROR)));
    AddJavaMirrorMethodBody(mirror, fun, CreateJavaRefCall(mirror, mirror.curFile));
}

void JavaDesugarManager::InsertJavaMirrorPropGetter(PropDecl& prop)
{
    auto& mirror = *StaticAs<ASTKind::CLASS_LIKE_DECL>(prop.outerDecl);
    auto curFile = prop.curFile;
    OwnedPtr<CallExpr> jniGetterCall;
    auto isArrayGetLength =
        IsJArray(*prop.outerDecl) && GetArrayOperationKind(prop) == ArrayOperationKind::GET_LENGTH;
    if (isArrayGetLength) {
        jniGetterCall = lib.CreateCFFIArrayLengthGetCall(CreateJavaRefCall(mirror, curFile), curFile);
    } else if (prop.TestAttr(Attribute::STATIC)) {
        jniGetterCall = lib.CreateGetStaticFieldCall(
            lib.CreateGetJniEnvCall(curFile), utils.GetJavaClassNormalizeSignature(*mirror.ty), GetJavaMemberName(prop),
            utils.GetJavaTypeSignature(*prop.ty));
    } else {
        jniGetterCall = lib.CreateGetFieldCall(
            lib.CreateGetJniEnvCall(curFile), CreateJavaRefCall(mirror, curFile),
            utils.GetJavaClassNormalizeSignature(*prop.outerDecl->ty), GetJavaMemberName(prop),
            utils.GetJavaTypeSignature(*prop.ty));
    }

    auto jniGetterCallRes = CreateTmpVarDecl(nullptr, std::move(jniGetterCall));
    jniGetterCallRes->ty = jniGetterCallRes->initializer->ty;
    CopyBasicInfo(jniGetterCallRes->initializer.get(), jniGetterCallRes.get());

    auto callResRef = CreateRefExpr(*jniGetterCallRes);
    CopyBasicInfo(jniGetterCallRes.get(), callResRef.get());

    auto outerDecl = As<ASTKind::CLASS_LIKE_DECL>(prop.outerDecl);
    OwnedPtr<Expr> unwrapJavaEntityCall = isArrayGetLength || !outerDecl ? std::move(callResRef) :
        lib.UnwrapJavaEntity(std::move(callResRef), prop.ty, *outerDecl);
    if (!unwrapJavaEntityCall) {
        prop.EnableAttr(Attribute::IS_BROKEN);
        prop.outerDecl->EnableAttr(Attribute::HAS_BROKEN);
        return;
    }

    auto& getter = *prop.getters.begin();
    auto& getterBody = getter->funcBody;
    getterBody->body = CreateBlock({}, prop.ty);
    getterBody->ty = prop.ty;

    getterBody->body->body.emplace_back(std::move(jniGetterCallRes));
    getterBody->body->body.emplace_back(CreateReturnExpr(std::move(unwrapJavaEntityCall), getterBody.get()));
}

void JavaDesugarManager::InsertJavaMirrorPropSetter(PropDecl& prop)
{
    auto& mirror = *StaticAs<ASTKind::CLASS_LIKE_DECL>(prop.outerDecl);
    auto curFile = prop.curFile;
    OwnedPtr<CallExpr> jniSetterCall;

    auto paramRef = WithinFile(CreateRefExpr(*prop.setters[0]->funcBody->paramLists[0]->params[0]), curFile);

    if (prop.TestAttr(Attribute::STATIC)) {
        jniSetterCall = lib.CreateSetStaticFieldCall(
            lib.CreateGetJniEnvCall(curFile), utils.GetJavaClassNormalizeSignature(*mirror.ty), GetJavaMemberName(prop),
            utils.GetJavaTypeSignature(*prop.ty), std::move(paramRef));
    } else {
        jniSetterCall = lib.CreateSetFieldCall(
            lib.CreateGetJniEnvCall(curFile), CreateJavaRefCall(mirror, curFile),
            MemberJNISignature(utils, prop),
            std::move(paramRef));
    }

    auto block = CreateBlock({}, prop.ty);
    auto unitTy = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);

    auto& setter = *prop.setters.begin();
    auto& setterBody = setter->funcBody;
    setterBody->body = std::move(block);
    setterBody->ty = unitTy;

    setterBody->body->body.emplace_back(std::move(jniSetterCall));
}

void JavaDesugarManager::DesugarJavaMirrorProp(PropDecl& prop)
{
    CJC_ASSERT(prop.outerDecl);
    InsertJavaMirrorPropGetter(prop);

    if (prop.isVar) {
        InsertJavaMirrorPropSetter(prop);
    }
}

void JavaDesugarManager::DesugarJavaMirror(ClassDecl& mirror)
{
    if (IsSynthetic(mirror)) {
        mirror.DisableAttr(Attribute::ABSTRACT);
    }
    Ptr<FuncDecl> generatedCtor = GetGeneratedJavaMirrorConstructor(mirror);
    for (auto& decl : mirror.GetMemberDecls()) {
        if (auto fd = As<ASTKind::FUNC_DECL>(decl.get())) {
            if (fd->TestAttr(Attribute::IS_BROKEN)) {
                continue;
            }

            if (fd->TestAttr(Attribute::CONSTRUCTOR)) {
                if (!IsGeneratedJavaMirrorConstructor(*fd)) {
                    DesugarJavaMirrorConstructor(*fd, *generatedCtor);
                }
            } else if (fd->TestAttr(Attribute::JAVA_MIRROR)) {
                DesugarJavaMirrorMethod(*fd, mirror);
            }
        } else if (auto prop = As<ASTKind::PROP_DECL>(decl.get())) {
            DesugarJavaMirrorProp(*prop);
        }
    }
}

void JavaDesugarManager::DesugarJavaMirror(InterfaceDecl& mirror)
{
    for (auto& decl : mirror.GetMemberDecls()) {
        if (decl->TestAttr(Attribute::IS_BROKEN)) {
            continue;
        }
        bool isConstructor = decl->TestAttr(Attribute::CONSTRUCTOR);

        if (FuncDecl* fd = As<ASTKind::FUNC_DECL>(decl.get()); fd && !isConstructor) {
            bool isMirror = decl->TestAttr(Attribute::JAVA_MIRROR);
            bool isStatic = decl->TestAttr(Attribute::STATIC);
            bool isDefault = decl->TestAttr(Attribute::JAVA_HAS_DEFAULT);
            if (isMirror && (isStatic || isDefault)) {
                DesugarJavaMirrorMethod(*fd, mirror);
            }
        } else if (auto prop = As<ASTKind::PROP_DECL>(decl.get())) {
            // not supported yet
            diag.DiagnoseRefactor(DiagKindRefactor::sema_java_interop_not_supported, *prop, "property in interface");
        }
    }
}

void JavaDesugarManager::GenerateInMirror(ClassDecl& classDecl, bool doStub)
{
    // Note: on [doStub] = true, no javaref field references may exist
    if (IsJObject(classDecl) && doStub) {
        // Generate java reference field for JObject. This field is accessible inside predecessors
        InsertJavaRefVarDecl(classDecl);
    }
    InsertJavaMirrorCtor(classDecl, doStub);

    if (!doStub) {
        InsertJavaMirrorHasInited(classDecl);
        InsertJavaMirrorFinalizer(classDecl);
    }

    if (&classDecl == utils.GetJStringDecl()) {
        InsertJStringOfStringCtor(classDecl, doStub);
    }

    if (!doStub && IsJArray(classDecl)) {
        InsertArrayJavaEntityGet(classDecl);
        InsertArrayJavaEntitySet(classDecl);
    }

    if (!doStub && IsJObject(classDecl)) {
        InsertJavaRefGetterWithBody(classDecl);
    }
}

void JavaDesugarManager::ReplaceCallsWithArrayJavaEntityGet(File& file)
{
    Walker(&file, Walker::GetNextWalkerID(), [this, &file](auto node) {
        if (!node->IsSamePackage(*file.curPackage)) {
            return VisitAction::WALK_CHILDREN;
        }

        Ptr<CallExpr> callExpr = As<ASTKind::CALL_EXPR>(node);
        if (!callExpr) {
            return VisitAction::WALK_CHILDREN;
        }

        auto funcDecl = callExpr->resolvedFunction;
        if (!funcDecl || !funcDecl->outerDecl || !IsJArray(*funcDecl->outerDecl) ||
            funcDecl->identifier != "[]" || callExpr->args.size() != 1
        ) {
            return VisitAction::WALK_CHILDREN;
        }

        auto ma = As<ASTKind::MEMBER_ACCESS>(callExpr->baseFunc);
        auto arrayElementType = ma->baseExpr->ty->typeArgs[0];

        if (!arrayElementType->IsClass() && !arrayElementType->IsCoreOptionType()) {
            return VisitAction::WALK_CHILDREN;
        }

        static auto arrayJavaEntityGetDecl = lib.FindArrayJavaEntityGetDecl(
            *As<ASTKind::CLASS_DECL>(funcDecl->outerDecl));
        CJC_ASSERT(arrayJavaEntityGetDecl);
        auto newCallExpr = ASTCloner::Clone(callExpr);
        newCallExpr->resolvedFunction = arrayJavaEntityGetDecl;
        auto base = As<ASTKind::MEMBER_ACCESS>(newCallExpr->baseFunc);
        base->target = arrayJavaEntityGetDecl;
        base->ty = arrayJavaEntityGetDecl->ty;
        callExpr->desugarExpr = lib.UnwrapJavaEntity(
            std::move(newCallExpr), arrayElementType, *As<ASTKind::CLASS_LIKE_DECL>(funcDecl->outerDecl));
        callExpr->desugarArgs = std::nullopt;

        return VisitAction::WALK_CHILDREN;
    }).Walk();
}

void JavaDesugarManager::ReplaceCallsWithArrayJavaEntitySet(File& file)
{
    Walker(&file, Walker::GetNextWalkerID(), [this, &file](auto node) {
        if (!node->IsSamePackage(*file.curPackage)) {
            return VisitAction::WALK_CHILDREN;
        }

        Ptr<CallExpr> callExpr = As<ASTKind::CALL_EXPR>(node);
        if (!callExpr) {
            return VisitAction::WALK_CHILDREN;
        }

        auto funcDecl = callExpr->resolvedFunction;
        if (!funcDecl || !funcDecl->outerDecl || !IsJArray(*funcDecl->outerDecl) ||
            funcDecl->identifier != "[]" || callExpr->args.size() != 2
        ) {
            return VisitAction::WALK_CHILDREN;
        }

        auto ma = As<ASTKind::MEMBER_ACCESS>(callExpr->baseFunc);
        auto arrayElementType = ma->baseExpr->ty->typeArgs[0];

        if (!arrayElementType->IsClass() && !arrayElementType->IsCoreOptionType()) {
            return VisitAction::WALK_CHILDREN;
        }

        static auto arrayJavaEntitySetDecl = lib.FindArrayJavaEntitySetDecl(
            *As<ASTKind::CLASS_DECL>(funcDecl->outerDecl));
        CJC_ASSERT(arrayJavaEntitySetDecl);
        auto newCallExpr = ASTCloner::Clone(callExpr);
        newCallExpr->resolvedFunction = arrayJavaEntitySetDecl;

        newCallExpr->args[1] = ASTCloner::Clone(newCallExpr->args[1].get());
        newCallExpr->args[1]->expr = lib.WrapJavaEntity(std::move(newCallExpr->args[1]->expr));
        newCallExpr->desugarArgs = std::nullopt;
        callExpr->desugarArgs = std::nullopt;
        auto base = As<ASTKind::MEMBER_ACCESS>(newCallExpr->baseFunc);
        base->target = arrayJavaEntitySetDecl;
        base->ty = arrayJavaEntitySetDecl->ty;
        callExpr->args[1] = ASTCloner::Clone(newCallExpr->args[1].get());

        callExpr->baseFunc = ASTCloner::Clone(newCallExpr->baseFunc.get());
        callExpr->resolvedFunction = newCallExpr->resolvedFunction;
        callExpr->desugarExpr = std::move(newCallExpr);

        return VisitAction::WALK_CHILDREN;
    }).Walk();
}

void JavaDesugarManager::GenerateInMirrors(File& file, bool doStub)
{
    auto pkg = file.curPackage;

    if (!doStub) {
        std::once_flag flag;
        std::call_once(flag, [this, &pkg]() {
            TypeCheckUtil::GenerateGetTypeForTypeParamIntrinsic(
                *pkg, typeManager, importManager.GetCoreDecl<StructDecl>(STD_LIB_STRING)->ty);
        });
    }

    for (auto& decl : file.decls) {
        if (auto cldecl = As<ASTKind::CLASS_LIKE_DECL>(decl.get())) {
            if (!cldecl->TestAttr(Attribute::JAVA_MIRROR)) {
                continue;
            }
            if (cldecl->TestAttr(Attribute::IS_BROKEN)) {
                return;
            }
            if (auto classDecl = As<ASTKind::CLASS_DECL>(cldecl)) {
                GenerateInMirror(*classDecl, doStub);
            }
            if (doStub && cldecl->astKind == ASTKind::INTERFACE_DECL) {
                InsertAbstractJavaRefGetter(*cldecl);
            }
        }
    }

    if (!doStub) {
        ReplaceCallsWithArrayJavaEntityGet(file);
        ReplaceCallsWithArrayJavaEntitySet(file);
    }
}

void JavaDesugarManager::DesugarMirrors(File& file)
{
    for (auto& decl : file.decls) {
        if (auto cldecl = As<ASTKind::CLASS_LIKE_DECL>(decl.get())) {
            if (cldecl->TestAttr(Attribute::JAVA_MIRROR)) {
                if (cldecl->TestAttr(Attribute::IS_BROKEN)) {
                    return;
                }
                if (auto cd = As<ASTKind::CLASS_DECL>(cldecl)) {
                    DesugarJavaMirror(*cd);
                } else if (auto iDecl = As<ASTKind::INTERFACE_DECL>(cldecl)) {
                    DesugarJavaMirror(*iDecl);
                }
            }
        }
    }
}

void JavaDesugarManager::InsertJStringOfStringCtor(ClassDecl& decl, bool doStub)
{
    static const std::string STRING_PARAM_NAME = "s";
    static Ptr<FuncDecl> generatedCtor;

    CJC_ASSERT(&decl == utils.GetJStringDecl());

    if (!doStub) {
        auto curFile = decl.curFile;
        // After constructor stub insertion, its body is filled on doStub = false
        CJC_NULLPTR_CHECK(generatedCtor);
        auto& param = generatedCtor->funcBody->paramLists[0]->params[0];
        auto jObjectCtor = GetGeneratedJavaMirrorConstructor(decl);
        auto convertCall = CreateCall(lib.GetCangjieStringToJava(), curFile,
            lib.CreateGetJniEnvCall(curFile), WithinFile(CreateRefExpr(*param), curFile));

        auto superCall = CreateSuperCall(decl, *jObjectCtor, jObjectCtor->ty);
        superCall->args.emplace_back(CreateFuncArg(std::move(convertCall)));

        generatedCtor->funcBody->body->body.emplace_back(std::move(superCall));
        return;
    }

    // Initially, on doStub = true, it inserts constructor stub
    auto& stringDecl = utils.GetStringDecl();
    auto param = CreateFuncParam(STRING_PARAM_NAME, CreateRefType(stringDecl), nullptr, stringDecl.ty);

    auto ctorFuncTy = typeManager.GetFunctionTy({param->ty}, decl.ty);

    std::vector<OwnedPtr<FuncParam>> ctorParams;
    ctorParams.emplace_back(std::move(param));

    std::vector<OwnedPtr<FuncParamList>> paramLists;
    paramLists.emplace_back(CreateFuncParamList(std::move(ctorParams)));

    auto ctorFuncBody = CreateFuncBody(
        std::move(paramLists),
        CreateRefType(decl),
        CreateBlock({}, decl.ty),
        decl.ty);

    auto fd = CreateFuncDecl("init", std::move(ctorFuncBody), ctorFuncTy);
    fd->fullPackageName = decl.fullPackageName;
    fd->outerDecl = &decl;
    fd->funcBody->funcDecl = fd.get();
    fd->constructorCall = ConstructorCall::SUPER;
    fd->funcBody->parentClassLike = &decl;
    fd->EnableAttr(
        Attribute::PUBLIC, Attribute::JAVA_MIRROR,
        Attribute::CONSTRUCTOR, Attribute::IN_CLASSLIKE);
    generatedCtor = fd.get();
    decl.body->decls.emplace_back(std::move(fd));
}

} // namespace Cangjie::Interop::Java
