// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "Utils.h"
#include "TypeCheckUtil.h"

#include "Desugar/AfterTypeCheck.h"
#include "cangjie/Mangle/BaseMangler.h"
#include "cangjie/Modules/ImportManager.h"
#include "cangjie/AST/Match.h"
#include "cangjie/Utils/CheckUtils.h"
#include "cangjie/Utils/ConstantsUtils.h"

namespace Cangjie::Native::FFI {

using namespace TypeCheckUtil;

OwnedPtr<RefExpr> CreateThisRef(Ptr<Decl> target, Ptr<Ty> ty, Ptr<File> curFile)
{
    auto thisRef = MakeOwned<RefExpr>();
    thisRef->isThis = true;
    thisRef->ty = ty;
    thisRef->ref.identifier = SrcIdentifier("this");
    thisRef->ref.target = target;
    thisRef->curFile = curFile;
    return thisRef;
}

OwnedPtr<CallExpr> CreateThisCall(Decl& target, FuncDecl& baseTarget, Ptr<Ty> funcTy, Ptr<File> curFile)
{
    auto call = CreateCallExpr(CreateThisRef(Ptr(&baseTarget), funcTy, curFile), {});
    call->callKind = CallKind::CALL_OBJECT_CREATION;
    call->ty = target.ty;
    call->resolvedFunction = Ptr(&baseTarget);

    return call;
}

OwnedPtr<PrimitiveType> CreateUnitType(Ptr<File> curFile)
{
    auto ret = MakeOwned<PrimitiveType>();
    ret->str = "Unit";
    ret->kind = TypeKind::TYPE_UNIT;
    ret->ty = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
    ret->curFile = curFile;

    return ret;
}

std::vector<Ptr<Ty>> GetParamTys(FuncParamList& params)
{
    std::vector<Ptr<Ty>> paramTys;

    for (auto& param : params.params) {
        paramTys.push_back(param->ty);
    }
    return paramTys;
}

OwnedPtr<RefExpr> CreateSuperRef(Ptr<Decl> target, Ptr<Ty> ty)
{
    auto superRef = MakeOwned<RefExpr>();
    superRef->isSuper = true;
    superRef->ty = ty;
    superRef->ref.identifier = SrcIdentifier("super");
    superRef->ref.target = target;
    return superRef;
}

OwnedPtr<CallExpr> CreateSuperCall(Decl& target, FuncDecl& baseTarget, Ptr<Ty> funcTy)
{
    auto call = CreateCallExpr(CreateSuperRef(Ptr(&baseTarget), funcTy), {});
    call->callKind = CallKind::CALL_SUPER_FUNCTION;
    call->ty = target.ty;
    call->resolvedFunction = Ptr(&baseTarget);

    return call;
}

OwnedPtr<Type> CreateType(Ptr<Ty> ty)
{
    auto res = MakeOwned<Type>();
    res->ty = ty;
    return res;
}

OwnedPtr<Type> CreateFuncType(Ptr<FuncTy> ty)
{
    auto res = MakeOwned<FuncType>();
    res->ty = ty;

    for (auto param : ty->paramTys) {
        res->paramTypes.push_back(CreateType(param));
    }

    return res;
}

OwnedPtr<Expr> CreateBoolMatch(
    OwnedPtr<Expr> selector, OwnedPtr<Expr> trueBranch, OwnedPtr<Expr> falseBranch, Ptr<Ty> ty)
{
    static const auto BOOL_TY = TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN);

    OwnedPtr<ConstPattern> truePattern = MakeOwned<ConstPattern>();
    truePattern->literal = CreateLitConstExpr(LitConstKind::BOOL, "true", BOOL_TY);
    truePattern->ty = BOOL_TY;

    OwnedPtr<ConstPattern> falsePattern = MakeOwned<ConstPattern>();
    falsePattern->literal = CreateLitConstExpr(LitConstKind::BOOL, "false", BOOL_TY);
    falsePattern->ty = BOOL_TY;

    auto caseTrue = CreateMatchCase(std::move(truePattern), std::move(trueBranch));
    auto caseFalse = CreateMatchCase(std::move(falsePattern), std::move(falseBranch));

    std::vector<OwnedPtr<MatchCase>> matchCases;
    matchCases.emplace_back(std::move(caseTrue));
    matchCases.emplace_back(std::move(caseFalse));
    auto curFile = selector->curFile;
    return WithinFile(CreateMatchExpr(std::move(selector), std::move(matchCases), ty), curFile);
}

StructDecl& GetStringDecl(const ImportManager& importManager)
{
    static auto decl = importManager.GetCoreDecl<StructDecl>(STD_LIB_STRING);
    CJC_NULLPTR_CHECK(decl);
    return *decl;
}

OwnedPtr<CallExpr> WrapReturningLambdaCall(TypeManager& typeManager, std::vector<OwnedPtr<Node>> nodes)
{
    auto retTy = nodes.back()->ty;
    auto lambda = WrapReturningLambdaExpr(typeManager, std::move(nodes));
    return CreateCallExpr(std::move(lambda), {}, nullptr, retTy);
}

OwnedPtr<LambdaExpr> WrapReturningLambdaExpr(TypeManager& typeManager, std::vector<OwnedPtr<Node>> nodes, std::vector<OwnedPtr<FuncParam>> lambdaParams)
{
    auto curFile = nodes[0]->curFile;
    CJC_ASSERT(!nodes.empty());
    std::vector<Ptr<Ty>> lambdaParamTys;
    std::transform(
        lambdaParams.begin(), lambdaParams.end(), std::back_inserter(lambdaParamTys), [](auto& p) { return p->ty; });
    auto paramLists = Nodes<FuncParamList>(CreateFuncParamList(std::move(lambdaParams)));
    auto retTy = nodes.back()->ty;
    auto retExpr = CreateReturnExpr(ASTCloner::Clone(Ptr(As<ASTKind::EXPR>(nodes.back().get()))));
    retExpr->ty = TypeManager::GetNothingTy();
    nodes.pop_back();
    auto lambda = CreateLambdaExpr(
        CreateFuncBody(
            std::move(paramLists),
            nullptr,
            CreateBlock(std::move(nodes), retTy),
            retTy));
    retExpr->refFuncBody = lambda->funcBody.get();
    lambda->funcBody->body->body.push_back(std::move(retExpr));
    lambda->curFile = curFile;
    lambda->ty = typeManager.GetFunctionTy(std::move(lambdaParamTys), retTy);
    return lambda;
}

std::string GetCangjieLibName(const std::string& outputLibPath, const std::string& fullPackageName, bool trimmed)
{
    if (FileUtil::IsDir(outputLibPath)) {
        return fullPackageName;
    }
    auto outputFileName = FileUtil::GetFileName(outputLibPath);

    constexpr std::string_view libPrefix = "lib";
    // check if [outputLibPath] starts with [LIB_PREFIX]
    if (outputFileName.rfind(libPrefix, 0) == 0) {
        if (!trimmed) {
            return outputFileName;
        }

        size_t extIdx = outputFileName.find_last_of(".");
        if (extIdx == std::string::npos) {
            return fullPackageName;
        }
        return outputFileName.substr(libPrefix.size(), extIdx - libPrefix.size());
    }
    return fullPackageName;
}

std::string GetMangledMethodName(const BaseMangler& mangler,
    const std::vector<OwnedPtr<FuncParam>>& params, const std::string& methodName)
{
    std::string name(methodName);

    for (auto& param : params) {
        auto mangledParam = mangler.MangleType(*param->ty);
        std::replace(mangledParam.begin(), mangledParam.end(), '.', '_');
        name += mangledParam;
    }

    return name;
}

Ptr<Annotation> GetForeignNameAnnotation(const Decl& decl)
{
    auto it = std::find_if(decl.annotations.begin(), decl.annotations.end(),
        [](const auto& anno) { return anno->kind == AnnotationKind::FOREIGN_NAME; });
    return it != decl.annotations.end() ? it->get() : nullptr;
}

}
