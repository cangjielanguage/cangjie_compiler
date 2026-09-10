// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "JavaDesugarManager.h"
#include "GenerateJavaImplApiStub.h"
#include "JniBridge.h"
#include "NativeFFI/Utils.h"
#include "InteropLibBridge.h"

#include "cangjie/AST/Create.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Types.h"

namespace Cangjie::Native::FFI::Java {

OwnedPtr<AST::FuncDecl> GenerateJavaImplApiStub::CreateWrappingConstructorStub(
    ClassDecl& refWrapper) const
{
    auto ctor = CreateFuncDecl("init", CreateFuncBody({}, CreateRefType(refWrapper), CreateBlock({})));
    ctor->funcBody->paramLists.emplace_back(MakeOwned<FuncParamList>());
    ctor->funcBody->paramLists[0]->EnableAttr(Attribute::COMPILER_ADD);
    auto& params = ctor->funcBody->paramLists[0]->params;

    // obj: Java_CFFI_JavaEntity
    auto javaEntityDecl = ilib.GetJavaEntityDecl();
    if (!javaEntityDecl) {
        return nullptr;
    }

    params.emplace_back(CreateFuncParam(
        JAVA_IMPL_ENTITY_ARG_NAME_IN_GENERATED_CTOR, CreateRefType(*javaEntityDecl), nullptr, javaEntityDecl->GetTy()));
    auto& entityParam = *params[0];

    // regId: RegistryId (Int64)
    auto& regIdParam = *params.emplace_back(jni.CreateRegistryIdParam());

    std::vector<ModalTy> paramTys;
    paramTys.push_back(entityParam.GetTy());
    paramTys.push_back(regIdParam.GetTy());
    ModalTy ctorTy{typeManager.GetFunctionTy(paramTys, refWrapper.GetTy())};

    ctor->funcBody->SetTy(ctorTy);
    ctor->SetTy(ctorTy);
    ctor->funcBody->funcDecl = ctor.get();
    ctor->EnableAttr(Attribute::CONSTRUCTOR);

    auto& block = ctor->funcBody->body;
    block->SetTy(refWrapper.GetTy());

    return ctor;
}

FuncDecl& GenerateJavaImplApiStub::InsertWrappingConstructor(ClassDecl& refWrapper) const
{
    auto ctor = WithinFile(CreateWrappingConstructorStub(refWrapper), refWrapper.curFile);
    auto& res = *ctor;
    ctor->outerDecl = &refWrapper;
    ctor->fullPackageName = refWrapper.fullPackageName;
    ctor->moduleName = refWrapper.moduleName;
    ctor->begin = refWrapper.body->begin;
    ctor->end = refWrapper.body->begin;
    ctor->funcBody->parentClassLike = &refWrapper;
    ctor->EnableAttr(Attribute::IN_CLASSLIKE, Attribute::PUBLIC);
    refWrapper.GetMemberDecls().emplace_back(std::move(ctor));
    return res;
}

GenerateJavaImplApiStub::GenerateJavaImplApiStub(
    TypeManager& typeManager, InteropLibBridge& ilib, JniBridge& jni) : typeManager(typeManager), ilib(ilib), jni(jni)
{
}

void GenerateJavaImplApiStub::Process(AfterTypeCheckContext& ctx)
{
    for (auto refWrapper : ctx.GetJavaImplReferenceWrappers()) {
        auto& ctor = InsertWrappingConstructor(*refWrapper);
        ctx.CacheJavaImplWrappingConstructor(*refWrapper, ctor);
    }
}

}
