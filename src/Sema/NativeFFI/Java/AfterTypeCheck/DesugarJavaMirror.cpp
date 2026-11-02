// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "TypeCheckUtil.h"
#include "JavaDesugarManager.h"
#include "ExclusiveScope.h"

#include "NativeFFI/Utils.h"
#include "cangjie/AST/AttributePack.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"
#include "cangjie/Utils/CheckUtils.h"
#include "cangjie/Utils/ConstantsUtils.h"
#include "Utils.h"
#include "NativeFFI/Utils.h"
#include "NativeFFI/Java/AfterTypeCheck/Utils.h"
#include "cangjie/AST/Utils.h"

#include "cangjie/Utils/SafePointer.h"
#include "cangjie/AST/Create.h"


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
    javaEntityGetDecl->SetTy(
        {typeManager.GetFunctionTy({{TypeManager::GetPrimitiveTy(TypeKind::TYPE_INT32)}}, {lib.GetJavaEntityTy()})});

    auto javaEntity = lib.GetJavaEntityTy();
    if (!javaEntity || !javaEntityGetDecl->funcBody->retType) {
        return;
    }
    javaEntityGetDecl->funcBody->retType->SetTy(javaEntity);
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
            // Array "set" call requires 2 parameters: index and value to be set
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
    javaEntitySetDecl->funcBody->paramLists[0]->params[1]->SetTy(javaEntity);
    if (javaEntitySetDecl->funcBody->paramLists[0]->params[1]->type) {
        javaEntitySetDecl->funcBody->paramLists[0]->params[1]->type->SetTy({javaEntity});
    }
    javaEntitySetDecl->SetTy(
        {typeManager.GetFunctionTy({{TypeManager::GetPrimitiveTy(TypeKind::TYPE_INT32)}, {javaEntity}}, {unitTy})});

    javaEntitySetDecl->funcBody->retType->SetTy({unitTy});
    decl.body->decls.push_back(std::move(javaEntitySetDecl));
}

void JavaDesugarManager::InsertJavaRefVarDecl(ClassDecl& decl)
{
    auto& javaEntityDecl = *lib.GetJavaEntityDecl();

    auto javaref = CreateVarDecl(JAVA_REF_FIELD_NAME, nullptr, CreateRefType(javaEntityDecl));
    javaref->SetTy(javaEntityDecl.GetTy());
    javaref->EnableAttr(Attribute::INITIALIZATION_CHECKED, Attribute::INITIALIZED);
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

void JavaDesugarManager::InsertJavaMirrorWrappingConstructorBody(ClassDecl& decl)
{
    if (IsJObject(decl)) {
        return;
    }
    auto curFile = decl.curFile;

    auto ctor = GetJavaMirrorWrappingConstructor(decl);
    CJC_NULLPTR_CHECK(ctor);
    auto actualParam = CreateFuncArg(WithinFile(CreateRefExpr(*ctor->funcBody->paramLists[0]->params[0]), curFile));
    auto& superCtor = *GetJavaMirrorWrappingConstructor(*decl.GetSuperClassDecl());
    auto superCall = CreateSuperCall(decl, superCtor, superCtor.DataTy());
    superCall->args.emplace_back(std::move(actualParam));
    ctor->funcBody->body->body.emplace_back(std::move(superCall));
    return;
}

void JavaDesugarManager::InsertJavaMirrorFinalizer(ClassDecl& mirror)
{
    mirror.body->decls.emplace_back(lib.CreateDeletingGlobalRefFinalizer(mirror));
}

void JavaDesugarManager::InsertJavaRefGetterWithBody(ClassDecl& decl)
{
    auto javaEntityDecl = lib.GetJavaEntityDecl();
    std::vector<OwnedPtr<FuncParam>> callParams;
    std::vector<OwnedPtr<FuncParamList>> paramLists;
    paramLists.push_back(CreateFuncParamList(std::move(callParams)));

    auto javaRef = GetJavaRefField(decl);
    auto ref = CreateRefExpr(*javaRef);

    auto ret = CreateReturnExpr(std::move(ref));
    ret->SetTy({TypeManager::GetNothingTy()});
    std::vector<OwnedPtr<Node>> nodes;
    nodes.emplace_back(std::move(ret));

    auto block = CreateBlock(std::move(nodes), javaEntityDecl->GetTy());
    block->curFile = decl.curFile;

    auto funcBody = CreateFuncBody(
        std::move(paramLists), CreateRefType(*javaEntityDecl), std::move(block), {TypeManager::GetNothingTy()});

    std::vector<ModalTy> funcParamTys;
    Ptr<FuncTy> funcTy = typeManager.GetFunctionTy(std::move(funcParamTys), javaEntityDecl->GetTy());

    auto fd = CreateFuncDecl(JAVA_REF_GETTER_FUNC_NAME, std::move(funcBody), {funcTy});
    fd->EnableAttr(Attribute::PUBLIC, Attribute::IN_CLASSLIKE, Attribute::INITIALIZED,
        Attribute::IS_CHECK_VISITED);
    fd->fullPackageName = decl.fullPackageName;
    fd->funcBody->funcDecl = fd.get();
    fd->funcBody->parentClassLike = &decl;
    fd->outerDecl = &decl;
    fd->curFile = decl.curFile;

    decl.body->decls.emplace_back(std::move(fd));
}

void JavaDesugarManager::GenerateInMirror(AfterTypeCheckContext& ctx, ClassDecl& classDecl)
{
    InsertJavaMirrorWrappingConstructorBody(classDecl);

    InsertJavaMirrorFinalizer(classDecl);

    if (&classDecl == utils.GetJStringDecl()) {
        InsertJStringOfStringCtorBody(ctx, classDecl);
    }

    if (IsJArray(classDecl)) {
        InsertArrayJavaEntityGet(classDecl);
        InsertArrayJavaEntitySet(classDecl);
    }

    if (IsJObject(classDecl)) {
        InsertJavaRefGetterWithBody(classDecl);
    }
}

void JavaDesugarManager::GenerateInMirrors(AfterTypeCheckContext& ctx, File& file)
{
    auto pkg = file.curPackage;

    std::once_flag flag;
    std::call_once(flag, [this, &pkg]() {
        TypeCheckUtil::GenerateGetTypeForTypeParamIntrinsic(*pkg, typeManager);
    });

    for (auto& decl : file.decls) {
        if (auto cldecl = As<ASTKind::CLASS_LIKE_DECL>(decl.get())) {
            if (!cldecl->IsJavaForeign()) {
                continue;
            }
            if (cldecl->TestAttr(Attribute::IS_BROKEN)) {
                return;
            }
            if (auto classDecl = As<ASTKind::CLASS_DECL>(cldecl)) {
                GenerateInMirror(ctx, *classDecl);
            }
        }
    }
}

void JavaDesugarManager::InsertJStringOfStringCtorBody(AfterTypeCheckContext& ctx, ClassDecl& decl)
{
    CJC_ASSERT(&decl == utils.GetJStringDecl());

    static auto getConstructorOfString = [](ClassDecl& jstring) -> FuncDecl& {
        Ptr<FuncDecl> ctor;
        for (auto member : jstring.GetMemberDeclPtrs()) {
            auto fd = As<ASTKind::FUNC_DECL>(member);
            if (!fd) {
                continue;
            }
            if (!fd->TestAttr(Attribute::CONSTRUCTOR)) {
                continue;
            }
            if (fd->funcBody->paramLists.empty()) {
                continue;
            }
            auto& params = fd->funcBody->paramLists[0];
            if (params->params.size() != 1 || !params->params[0]->GetTy()->IsString()) {
                continue;
            }
            ctor = fd;
            break;
        }
        CJC_NULLPTR_CHECK(ctor);
        return *ctor;
    };

    auto curFile = decl.curFile;
    // After constructor stub insertion, its body is filled
    auto& generatedCtor = getConstructorOfString(decl);
    auto& param = generatedCtor.funcBody->paramLists[0]->params[0];
    auto jObjectCtor = GetJavaMirrorWrappingConstructor(decl);

    auto block = factory.WithLocalJniEnvPtr(*curFile, [&](Ptr<AST::Expr> jniEnvPtr) {
        // Java_CFFI_CangjieStringToJava($jniEnvPtr, s).asJobject() | local java reference
        auto convertCall = lib.CreateAsJniJobjectCall(CreateCall(lib.GetCangjieStringToJava(), curFile,
            ASTCloner::Clone(jniEnvPtr), WithinFile(CreateRefExpr(*param), curFile)));

        auto block = CreateBlock({}, lib.GetJavaEntityTy());
        block->curFile = curFile;
        // Java_CFFI_JavaEntity($convertCall)
        auto javaEntity = lib.CreateJavaEntityJobjectCall(lib.CreateSwapLocalWithGlobalRefCall(
            ASTCloner::Clone(jniEnvPtr),
            std::move(convertCall)));
        block->body.push_back(std::move(javaEntity));
        return block;
   });

    auto exclusiveScopeCall = WrapWithExclusiveScope(ctx, std::move(block), lib.GetJavaEntityTy());
    if (!exclusiveScopeCall) {
        generatedCtor.EnableAttr(Attribute::IS_BROKEN);
        return;
    }
    auto superCall = CreateSuperCall(decl, *jObjectCtor, jObjectCtor->DataTy());
    superCall->args.emplace_back(CreateFuncArg(std::move(exclusiveScopeCall)));
    generatedCtor.funcBody->body->body.emplace_back(std::move(superCall));
}

} // namespace Cangjie::Interop::Java
