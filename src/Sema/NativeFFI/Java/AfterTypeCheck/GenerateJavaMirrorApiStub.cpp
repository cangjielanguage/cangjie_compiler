// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "GenerateJavaMirrorApiStub.h"
#include "NativeFFI/Utils.h"
#include "InteropLibBridge.h"

#include "cangjie/AST/Create.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Types.h"
#include "cangjie/AST/Utils.h"

namespace Cangjie::Native::FFI::Java {

FuncDecl& GenerateJavaMirrorApiStub::InsertWrappingConstructor(ClassDecl& mirror) const
{
    auto ctor = CreateWrappingConstructorStub(mirror);
    auto& res = *ctor;
    ctor->funcBody->parentClassLike = &mirror;
    ctor->fullPackageName = mirror.fullPackageName;
    ctor->outerDecl = &mirror;
    ctor->curFile = mirror.curFile;

    mirror.body->decls.emplace_back(std::move(ctor));
    return res;
}

FuncDecl& GenerateJavaMirrorApiStub::InsertJStringOfStringConstructor(ClassDecl& jstring) const
{
    auto ctor = CreateJStringOfStringConstructorStub(jstring);
    auto& res = *ctor;
    ctor->funcBody->parentClassLike = &jstring;
    ctor->fullPackageName = jstring.fullPackageName;
    ctor->outerDecl = &jstring;
    ctor->curFile = jstring.curFile;
    ctor->begin = jstring.begin;

    jstring.body->decls.emplace_back(std::move(ctor));
    return res;
}

FuncDecl& GenerateJavaMirrorApiStub::InsertAbstractJavaReferenceGetterStub(InterfaceDecl& mirror) const
{
    auto getter = CreateAbstractJavaReferenceGetterStub();
    auto& res = *getter;
    getter->funcBody->parentClassLike = &mirror;
    getter->fullPackageName = mirror.fullPackageName;
    getter->outerDecl = &mirror;
    // Every AST decl must carry a curFile (see CHIR AST2CHIR::CreateFuncSignatureAndSetGlobalCache).
    getter->curFile = mirror.curFile;
    getter->begin = mirror.begin;

    mirror.body->decls.emplace_back(std::move(getter));
    return res;
}

OwnedPtr<FuncDecl> GenerateJavaMirrorApiStub::CreateWrappingConstructorStub(ClassDecl& mirror) const
{
    auto curFile = mirror.curFile;
    auto isJObject = IsJObject(mirror);
    static auto& javaEntityDecl = *ilib.GetJavaEntityDecl();

    auto param = CreateFuncParam("$ref", CreateRefType(javaEntityDecl), nullptr, javaEntityDecl.GetTy());

    std::vector<OwnedPtr<Node>> ctorNodes;

    if (isJObject) {
        // For JObject, body for wrapping constructor is generated at this stage.
        auto lhsRef = WithinFile(CreateRefExpr(*GetJavaRefField(mirror)), curFile);
        auto rhs = WithinFile(CreateRefExpr(*param), curFile);

        auto unitTy = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
        auto refAssignment = CreateAssignExpr(std::move(lhsRef), std::move(rhs), {unitTy});
        ctorNodes.push_back(ilib.CreateEnsureNotNullCall(WithinFile(CreateRefExpr(*param), curFile)));
        ctorNodes.push_back(std::move(refAssignment));
    }

    std::vector<ModalTy> ctorFuncParamTys;
    ctorFuncParamTys.push_back(param->GetTy());
    auto ctorFuncTy = typeManager.GetFunctionTy(std::move(ctorFuncParamTys), mirror.GetTy());

    std::vector<OwnedPtr<FuncParam>> ctorParams;
    ctorParams.push_back(std::move(param));
    auto paramList = CreateFuncParamList(std::move(ctorParams));
    paramList->curFile = curFile;

    std::vector<OwnedPtr<FuncParamList>> paramLists;
    paramLists.push_back(std::move(paramList));

    auto ctorFuncBody = CreateFuncBody(std::move(paramLists),
        CreateRefType(mirror), CreateBlock(std::move(ctorNodes), mirror.GetTy()), mirror.GetTy());

    auto fd = CreateFuncDecl("init", std::move(ctorFuncBody), {ctorFuncTy});
    fd->funcBody->funcDecl = fd.get();
    fd->constructorCall = ConstructorCall::SUPER;
    fd->EnableAttr(Attribute::PUBLIC, Attribute::IN_CLASSLIKE, Attribute::CONSTRUCTOR);

    return fd;
}

OwnedPtr<FuncDecl> GenerateJavaMirrorApiStub::CreateJStringOfStringConstructorStub(ClassDecl& jstring) const
{
    static const std::string stringParamName = "s";

    CJC_ASSERT(&jstring == utils.GetJStringDecl());

    // Initially, it inserts constructor stub
    auto& stringDecl = utils.GetStringDecl();
    auto param = CreateFuncParam(stringParamName, CreateRefType(stringDecl), nullptr, stringDecl.GetTy());

    auto ctorFuncTy = typeManager.GetFunctionTy({param->GetTy()}, jstring.GetTy());

    std::vector<OwnedPtr<FuncParam>> ctorParams;
    ctorParams.emplace_back(std::move(param));

    std::vector<OwnedPtr<FuncParamList>> paramLists;
    paramLists.emplace_back(CreateFuncParamList(std::move(ctorParams)));

    auto ctorFuncBody = CreateFuncBody(std::move(paramLists),
        CreateRefType(jstring), CreateBlock({}, jstring.GetTy()), jstring.GetTy());

    auto fd = CreateFuncDecl("init", std::move(ctorFuncBody), {ctorFuncTy});
    fd->funcBody->funcDecl = fd.get();
    fd->constructorCall = ConstructorCall::SUPER;
    fd->EnableAttr(Attribute::PUBLIC, Attribute::CONSTRUCTOR, Attribute::IN_CLASSLIKE);

    return fd;
}

OwnedPtr<FuncDecl> GenerateJavaMirrorApiStub::CreateAbstractJavaReferenceGetterStub() const
{
    static auto& javaEntityDecl = *ilib.GetJavaEntityDecl();
    std::vector<OwnedPtr<FuncParam>> callParams;
    std::vector<OwnedPtr<FuncParamList>> paramLists;
    paramLists.emplace_back(CreateFuncParamList(std::move(callParams)));

    auto funcBody =
        CreateFuncBody(std::move(paramLists), CreateRefType(javaEntityDecl), nullptr, {TypeManager::GetNothingTy()});

    static Ptr<FuncTy> funcTy = typeManager.GetFunctionTy({}, javaEntityDecl.GetTy());

    auto fd = CreateFuncDecl(JAVA_REF_GETTER_FUNC_NAME, std::move(funcBody), {funcTy});
    fd->EnableAttr(Attribute::PUBLIC, Attribute::IN_CLASSLIKE, Attribute::ABSTRACT);
    fd->funcBody->funcDecl = fd.get();

    return fd;
}

GenerateJavaMirrorApiStub::GenerateJavaMirrorApiStub(TypeManager& typeManager,
    InteropLibBridge& ilib, Interop::Java::Utils& utils) : typeManager(typeManager), ilib(ilib), utils(utils)
{
}

void GenerateJavaMirrorApiStub::Process(AfterTypeCheckContext& ctx)
{
    for (auto mirror : ctx.GetJavaMirrors()) {
        if (mirror->TestAttr(Attribute::IS_BROKEN)) {
            continue;
        }

        if (auto mirrorClass = As<ASTKind::CLASS_DECL>(mirror)) {
            InsertWrappingConstructor(*mirrorClass);

            if (mirrorClass == utils.GetJStringDecl()) {
                InsertJStringOfStringConstructor(*mirrorClass);
            }
        } else if (auto mirrorInterface = As<ASTKind::INTERFACE_DECL>(mirror)) {
            InsertAbstractJavaReferenceGetterStub(*mirrorInterface);
        }
    }
}

} // namespace Cangjie::Native::FFI::Java
