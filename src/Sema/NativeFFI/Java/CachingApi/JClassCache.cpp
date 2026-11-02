// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "JClassCache.h"
#include "NativeFFI/Java/AfterTypeCheck/AfterTypeCheckContext.h"
#include "NativeFFI/Utils.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Node.h"

namespace Cangjie::Native::FFI::Java {
using namespace Cangjie::AST;

JClassCache::JClassCache(
    const ImportManager& importManager,
    TypeManager& typeManager,
    InteropLibBridge& ilib) : importManager(importManager), typeManager(typeManager), ilib(ilib)
{}

std::string JClassCache::GetNewAccessorName(const JavaClassSignature& javaClass) const
{
    static size_t unique_id = 0;
    std::string mangledVarName = "jclass$";
    mangledVarName += javaClass.GetUnqualifiedTypeName("$");
    mangledVarName += std::to_string(unique_id++);

    return mangledVarName;
}

void JClassCache::Clear() noexcept
{
    cache.clear();
}

OwnedPtr<AST::Expr> JClassCache::CreateJClassAccess(
    AfterTypeCheckContext& ctx,
    JavaClassSignature javaClass,
    Ptr<Expr> envPtr)
{
    auto& curFile = *envPtr->curFile;
    auto& classVar = GetOrPut(ctx, javaClass, curFile);

    auto createGetClassCall = [this, envPtr, &curFile](JavaClassSignature javaClass) {
        return ilib.CreateGetClassCall(javaClass.GetJniClassName(), envPtr, curFile);
    };

    std::vector<OwnedPtr<Node>> ifNull;
    std::vector<OwnedPtr<Node>> ifNotNull;

    ifNull.push_back(CreateAssignExpr(WithinFile(CreateRefExpr(classVar), &curFile),
        ilib.CreateSwapLocalWithGlobalRefCall(ASTCloner::Clone(envPtr), createGetClassCall(javaClass)),
        {TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT)}));
    ifNull.push_back(WithinFile(CreateRefExpr(classVar), &curFile));

    ifNotNull.push_back(WithinFile(CreateRefExpr(classVar), &curFile));

    return CreateIfExpr(
        CreateIsPtrNullCheckCall(importManager, typeManager, WithinFile(CreateRefExpr(classVar), &curFile)),
        CreateBlock(std::move(ifNull), {&ilib.GetJniJClassTy()}),
        CreateBlock(std::move(ifNotNull), {&ilib.GetJniJClassTy()}), {&ilib.GetJniJClassTy()});
}

VarDecl& JClassCache::GetOrPut(AfterTypeCheckContext& ctx, JavaClassSignature javaClass, File& curFile)
{
    // Get variable that stores JClass
    if (auto it = cache.find(javaClass); it != cache.end()) {
        return *it->second;
    }
    // Create variable since it does not exist yet
    auto& jclassTy = ilib.GetJniJClassTy();

    auto null = MakeOwned<PointerExpr>();
    null->SetTy({&jclassTy});
    auto varDecl = CreateVarDecl(GetNewAccessorName(javaClass), std::move(null), nullptr);
    varDecl->EnableAttr(Attribute::INTERNAL, Attribute::GLOBAL, Attribute::INITIALIZED, Attribute::NO_REFLECT_INFO);
    varDecl->isVar = true;
    varDecl->curFile = &curFile;
    varDecl->fullPackageName = curFile.GetFullPackageName();

    cache[javaClass] = varDecl.get();
    auto& ret = *varDecl;
    ctx.AddGeneratedDecl(std::move(varDecl));
    return ret;
}

}
