// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "JMethodIdCache.h"
#include "NativeFFI/Java/AfterTypeCheck/AfterTypeCheckContext.h"
#include "NativeFFI/Java/AfterTypeCheck/InteropLibBridge.h"
#include "NativeFFI/Java/JavaMemberSignature.h"
#include "NativeFFI/Utils.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Node.h"

namespace Cangjie::Native::FFI::Java {
using namespace Cangjie::AST;

JMethodIdCache::JMethodIdCache(const ImportManager& importManager,
    TypeManager& typeManager,
    InteropLibBridge& ilib) : importManager(importManager), typeManager(typeManager), ilib(ilib)
{}

std::string JMethodIdCache::GetNewAccessorName(const JavaMemberSignature& method) const
{
    static size_t unique_id = 0;
    std::string mangledVarName = "jmethod$";

    mangledVarName += method.GetClassSignature().GetUnqualifiedTypeName("$");
    mangledVarName += "$$";
    mangledVarName += method.GetName();
    mangledVarName += std::to_string(unique_id++);
    return mangledVarName;
}

void JMethodIdCache::Clear() noexcept
{
    cache.clear();
}

OwnedPtr<Expr> JMethodIdCache::CreateJMethodIdAccess(AfterTypeCheckContext& ctx,
    JavaMemberSignature method, Ptr<AST::Expr> jclass, Ptr<AST::Expr> envPtr)
{
    auto& curFile = *envPtr->curFile;
    auto& jmethodVar = GetOrPut(ctx, method, curFile);

    std::vector<OwnedPtr<Node>> ifNull;
    std::vector<OwnedPtr<Node>> ifNotNull;

    ifNull.push_back(CreateAssignExpr(WithinFile(CreateRefExpr(jmethodVar), &curFile),
        ilib.CreateGetMethodIdCall(
            envPtr, ASTCloner::Clone(jclass), method.GetName(), method.GetSignature(), method.IsStatic()),
        {TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT)}));
    ifNull.push_back(WithinFile(CreateRefExpr(jmethodVar), &curFile));

    ifNotNull.push_back(WithinFile(CreateRefExpr(jmethodVar), &curFile));

    return CreateIfExpr(
        CreateIsPtrNullCheckCall(importManager, typeManager, WithinFile(CreateRefExpr(jmethodVar), &curFile)),
        CreateBlock(std::move(ifNull), {&ilib.GetJniJClassTy()}),
        CreateBlock(std::move(ifNotNull), {&ilib.GetJniJClassTy()}), {&ilib.GetJniJClassTy()});
}

VarDecl& JMethodIdCache::GetOrPut(AfterTypeCheckContext& ctx, JavaMemberSignature method, File& curFile)
{
    if (auto it = cache.find(method); it != cache.end()) {
        return *it->second;
    }
    // Create variable since it does not exist yet
    auto& jmethodIdTy = ilib.GetJniJmethodIdTy();

    auto null = MakeOwned<PointerExpr>();
    null->SetTy({&jmethodIdTy});
    auto varDecl = CreateVarDecl(GetNewAccessorName(method), std::move(null), nullptr);
    varDecl->EnableAttr(Attribute::INTERNAL, Attribute::GLOBAL, Attribute::INITIALIZED, Attribute::NO_REFLECT_INFO);
    varDecl->isVar = true;
    varDecl->curFile = &curFile;
    varDecl->fullPackageName = curFile.GetFullPackageName();

    cache[method] = varDecl.get();
    auto& ret = *varDecl;
    ctx.AddGeneratedDecl(std::move(varDecl));
    return ret;
}

}
