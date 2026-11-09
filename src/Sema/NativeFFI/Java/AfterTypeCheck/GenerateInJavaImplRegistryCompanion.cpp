// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "JavaDesugarManager.h"
#include "GenerateInJavaImplRegistryCompanion.h"
#include "NativeFFI/Utils.h"
#include "InteropLibBridge.h"

#include "cangjie/AST/Create.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Types.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Sema/TypeManager.h"
#include "cangjie/Utils/CheckUtils.h"

namespace Cangjie::Native::FFI::Java {

OwnedPtr<FuncDecl> GenerateInJavaImplRegistryCompanion::GenerateConstructor(ClassDecl& companion) const
{
    auto file = companion.curFile;
    auto ctor = CreateFuncDecl("init", CreateFuncBody({}, CreateRefType(companion), CreateBlock({})));
    ctor->funcBody->paramLists.emplace_back(MakeOwned<FuncParamList>());
    ctor->funcBody->paramLists[0]->EnableAttr(Attribute::COMPILER_ADD);

    // obj: Java_CFFI_JavaEntity
    auto javaEntityDecl = ilib.GetJavaEntityDecl();
    if (!javaEntityDecl) {
        return nullptr;
    }

    auto entityParam = CreateFuncParam(
        JAVA_IMPL_ENTITY_ARG_NAME_IN_GENERATED_CTOR, CreateRefType(*javaEntityDecl), nullptr, javaEntityDecl->GetTy());

    auto entityParamRef = WithinFile(CreateRefExpr(*entityParam), file);
    ctor->funcBody->paramLists[0]->params.emplace_back(std::move(entityParam));

    std::vector<ModalTy> paramTys;
    paramTys.push_back(javaEntityDecl->GetTy());
    auto ctorTy = typeManager.GetFunctionTy(paramTys, companion.GetTy());

    auto& block = ctor->funcBody->body;
    block->SetTy(companion.GetTy());

    block->body.emplace_back(ilib.CreatePutSetToRegistryCall(ilib.CreateGetJniEnvCall(file), std::move(entityParamRef),
        CreateThisRef(&companion, companion.DataTy(), file)));

    ctor->funcBody->SetTy({ctorTy});
    ctor->SetTy({ctorTy});
    ctor->funcBody->funcDecl = ctor.get();
    ctor->constructorCall = ConstructorCall::SUPER;
    ctor->EnableAttr(Attribute::CONSTRUCTOR, Attribute::IN_CLASSLIKE, Attribute::PUBLIC);
    ctor->outerDecl = &companion;
    ctor->funcBody->parentClassLike = &companion;
    ctor->fullPackageName = companion.fullPackageName;
    ctor->moduleName = companion.moduleName;
    // Every AST decl must carry a curFile; otherwise downstream passes (e.g. CHIR
    // AST2CHIR::CreateFuncSignatureAndSetGlobalCache) dereference a null pointer.
    ctor->curFile = file;
    return ctor;
}

void GenerateInJavaImplRegistryCompanion::Process(ClassDecl& companion) const
{
    CJC_ASSERT(IsImplRegistryCompanion(companion));

    companion.GetMemberDecls().push_back(GenerateConstructor(companion));
}

GenerateInJavaImplRegistryCompanion::GenerateInJavaImplRegistryCompanion(TypeManager& typeManager,
    InteropLibBridge& ilib) : typeManager(typeManager), ilib(ilib)
{
}

void GenerateInJavaImplRegistryCompanion::Process(AfterTypeCheckContext& ctx)
{
    for (auto refWrapper : ctx.GetJavaImplReferenceWrappers()) {
        if (refWrapper->TestAttr(Attribute::IS_BROKEN)) {
            continue;
        }
        auto& companion = ctx.GetJavaImplRegistryCompanion(*refWrapper);

        Process(companion);
    }
}

} // namespace Cangjie::Native::FFI::Java
