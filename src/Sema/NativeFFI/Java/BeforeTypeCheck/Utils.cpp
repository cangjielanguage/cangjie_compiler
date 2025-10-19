// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "Utils.h"
#include "NativeFFI/Utils.h"
#include "cangjie/Utils/CastingTemplate.h"
#include "cangjie/AST/Create.h"
#include "TypeCheckUtil.h"

using namespace Cangjie;
using namespace Cangjie::Native::FFI;
using namespace Cangjie::AST;

namespace {
using namespace Cangjie::TypeCheckUtil;
using namespace Cangjie::Interop::Java;

void InsertMethodStub(FuncDecl& fd, const ImportManager& importManager, TypeManager& typeManager)
{
    CJC_ASSERT(fd.funcBody);
    auto argTy = GetStringDecl(importManager).ty;
    auto arg = CreateLitConstExpr(LitConstKind::STRING, "It's compiler generated stub.", argTy);
    std::vector<OwnedPtr<Expr>> args;
    args.emplace_back(std::move(arg));

    static auto& exception = GetExceptionDecl(importManager);
    auto throwExpr = CreateThrowException(exception, std::move(args), *fd.curFile, typeManager);

    std::vector<OwnedPtr<Node>> nodes;
    nodes.emplace_back(std::move(throwExpr));

    fd.funcBody->body = CreateBlock(std::move(nodes));
}
}

namespace Cangjie::Interop::Java {

void InsertJavaHasDefaultMethodStubs(
    const InterfaceDecl& id,
    const ImportManager& importManager,
    TypeManager& typeManager)
{
    for (auto& decl : id.GetMemberDeclPtrs()) {
        if (auto fd = As<ASTKind::FUNC_DECL>(decl);
            fd && fd->TestAttr(Attribute::JAVA_HAS_DEFAULT)) {
            InsertMethodStub(*fd, importManager, typeManager);
        }
    }
}

void RemoveAbstractAttributeForJavaHasDefaultMethods(const InterfaceDecl& decl)
{
    for (const auto& member : decl.GetMemberDeclPtrs()) {
        if (member->TestAttr(Attribute::JAVA_HAS_DEFAULT)) {
            member->DisableAttr(Attribute::ABSTRACT);
            /*
            cj and java have different typechecks,
            default attribute makes this difference.
            */
            member->EnableAttr(Attribute::DEFAULT);
        }
    }
}

ClassDecl& GetExceptionDecl(const ImportManager& importManager)
{
    const auto exceptionDecl = importManager.GetCoreDecl("Exception");
    CJC_NULLPTR_CHECK(exceptionDecl);
    
    ClassDecl* exception = nullptr;
    if (auto ex = As<ASTKind::CLASS_DECL>(exceptionDecl)) {
        exception = ex;
    }
    CJC_NULLPTR_CHECK(exception);

    return *exception;
}

}
