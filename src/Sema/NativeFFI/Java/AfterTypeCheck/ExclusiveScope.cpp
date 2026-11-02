// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "ExclusiveScope.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Types.h"
#include "cangjie/AST/Utils.h"
#include "TypeCheckUtil.h"
#include "NativeFFI/Utils.h"
#include "Desugar/AfterTypeCheck.h"

namespace Cangjie::Native::FFI::Java {

using namespace AST;

namespace {

const std::string EXCLUSIVE_SCOPE_NAME = "exclusiveScope";
const std::string CAUSED_BY_PROPERTY_NAME = "causedBy";
const std::string CANGJIE_EXCEPTION_CLASS_NAME = "Exception";
const std::string EXCLUSIVE_SCOPE_EXCEPTION_CLASS_NAME = "ExclusiveScopeException";

OwnedPtr<Expr> WrapWithExclusiveScopeBody(AfterTypeCheckContext& ctx, OwnedPtr<Block> body, ModalTy retTy)
{
    auto exclusiveDecl = ctx.importManager.GetCoreDecl<FuncDecl>(EXCLUSIVE_SCOPE_NAME);
    if (!exclusiveDecl) {
        ctx.importManager.GetDiagnosticEngine().DiagnoseRefactor(
            DiagKindRefactor::sema_java_interop_not_supported, DEFAULT_POSITION,
            "target platform " + ctx.target.ToFullTripleString());
        CJC_ASSERT_WITH_MSG(exclusiveDecl, "Java interop requires exclusiveScope in the core library");
        return nullptr;
    }

    auto exclusiveRef = CreateRefExpr(*exclusiveDecl);

    exclusiveRef->instTys.emplace_back(retTy.Ty());
    auto mapping = TypeCheckUtil::GenerateTypeMapping(*exclusiveDecl, exclusiveRef->instTys);
    exclusiveRef->SetTy(ctx.typeManager.GetInstantiatedTy(exclusiveDecl->GetTy(), mapping));
    exclusiveRef->typeArguments.emplace_back(CreateType(retTy));

    CJC_NULLPTR_CHECK(body);
    auto curFile = body->curFile;

    auto paramLists = Nodes<FuncParamList>(CreateFuncParamList(Nodes<FuncParam>()));
    auto retTypeNode = CreateType(retTy);
    auto lambdaFuncBody = CreateFuncBody(std::move(paramLists), std::move(retTypeNode), std::move(body), retTy);

    auto lambda = CreateLambdaExpr(std::move(lambdaFuncBody));
    lambda->curFile = curFile;
    lambda->SetTy({ctx.typeManager.GetFunctionTy({}, retTy)});

    auto args = Nodes<FuncArg>(CreateFuncArg(std::move(lambda)));
    auto realRetTy = StaticCast<FuncTy>(exclusiveRef->DataTy())->retTy;

    auto call = CreateCallExpr(std::move(exclusiveRef), std::move(args), exclusiveDecl,
        realRetTy, CallKind::CALL_DECLARED_FUNCTION);
    call->curFile = curFile;
    return call;
}

OwnedPtr<Expr> CreateCausedByGetterCall(AfterTypeCheckContext& ctx, VarDecl& scopeExDecl, File& curFile)
{
    auto causedByProp = CreateMemberAccess(WithinFile(CreateRefExpr(scopeExDecl), &curFile), CAUSED_BY_PROPERTY_NAME);
    CJC_NULLPTR_CHECK(causedByProp);
    CJC_NULLPTR_CHECK(causedByProp->GetTarget());

    auto propDecl = StaticAs<ASTKind::PROP_DECL>(causedByProp->GetTarget());
    CJC_NULLPTR_CHECK(propDecl);

    auto getter = TypeCheckUtil::GetUsedMemberDecl(*propDecl, true);
    CJC_NULLPTR_CHECK(getter);
    CJC_ASSERT(getter->astKind == ASTKind::FUNC_DECL);

    auto causedByBase = CreateMemberAccess(WithinFile(CreateRefExpr(scopeExDecl), &curFile), *getter);
    CJC_NULLPTR_CHECK(causedByBase);

    causedByBase->SetTy({ctx.typeManager.GetFunctionTy({}, causedByProp->GetTy())});

    auto causedBy = CreateCallExpr(std::move(causedByBase), {});
    CJC_NULLPTR_CHECK(causedBy);

    causedBy->callKind = CallKind::CALL_DECLARED_FUNCTION;
    causedBy->resolvedFunction = StaticCast<FuncDecl*>(getter);
    causedBy->SetTy(causedByProp->GetTy());
    causedBy->curFile = &curFile;

    return causedBy;
}

OwnedPtr<Expr> CreateCausedByMatch(AfterTypeCheckContext& ctx, OwnedPtr<Expr> causedBy,
    VarDecl& exDecl, Ptr<Ty> exceptionTy, File& curFile)
{
    CJC_NULLPTR_CHECK(causedBy);
    CJC_ASSERT(causedBy->GetTy()->IsCoreOptionType());
    auto optionTy = causedBy->GetTy();

    // Build the None case.
    auto noneRef = WithinFile(CreateOptionNoneRef(ctx.importManager, ctx.typeManager,
        optionTy->typeArgs[0]), &curFile);

    // Build the Some($cause) case.
    auto causePattern = CreateVarPattern("$cause", exceptionTy);
    CJC_NULLPTR_CHECK(causePattern);
    CJC_NULLPTR_CHECK(causePattern->varDecl);
    causePattern->curFile = &curFile;
    causePattern->varDecl->curFile = &curFile;
    auto& causeDecl = *causePattern->varDecl;

    auto someRef = WithinFile(CreateOptionSomeRef(ctx.importManager, ctx.typeManager,
        exceptionTy), &curFile);

    auto somePattern = MakeOwned<EnumPattern>();
    CJC_NULLPTR_CHECK(somePattern);
    somePattern->constructor = std::move(someRef);
    somePattern->patterns.emplace_back(std::move(causePattern));
    somePattern->SetTy(optionTy);
    somePattern->curFile = &curFile;

    auto throwCause = MakeOwned<ThrowExpr>();
    CJC_NULLPTR_CHECK(throwCause);
    throwCause->expr = WithinFile(CreateRefExpr(causeDecl), &curFile);
    throwCause->SetTy({TypeManager::GetNothingTy()});
    throwCause->curFile = &curFile;

    auto nonePattern = MakeOwned<EnumPattern>();
    CJC_NULLPTR_CHECK(nonePattern);
    nonePattern->constructor = std::move(noneRef);
    nonePattern->SetTy(optionTy);
    nonePattern->curFile = &curFile;

    auto throwEx = MakeOwned<ThrowExpr>();
    CJC_NULLPTR_CHECK(throwEx);
    throwEx->expr = WithinFile(CreateRefExpr(exDecl), &curFile);
    throwEx->SetTy({TypeManager::GetNothingTy()});
    throwEx->curFile = &curFile;

    std::vector<OwnedPtr<MatchCase>> cases;
    cases.emplace_back(CreateMatchCase(std::move(somePattern), std::move(throwCause)));
    cases.emplace_back(CreateMatchCase(std::move(nonePattern), std::move(throwEx)));

    for (auto& matchCase : cases) {
        CJC_NULLPTR_CHECK(matchCase);
        matchCase->SetCtxExprForPatterns(causedBy.get());
        matchCase->curFile = &curFile;
    }

    auto result = CreateMatchExpr(std::move(causedBy), std::move(cases), {TypeManager::GetNothingTy()});
    return WithinFile(std::move(result), &curFile);
}

OwnedPtr<Expr> CreateExceptionDispatch(AfterTypeCheckContext& ctx, VarDecl& exDecl,
    Ptr<Ty> exceptionTy, Ptr<Ty> scopeExceptionTy, File& curFile)
{
    auto exRef = WithinFile(CreateRefExpr(exDecl), &curFile);
    exRef->SetTy(exceptionTy);

    auto scopeExPattern = CreateVarPattern("$scopeEx", scopeExceptionTy);
    CJC_NULLPTR_CHECK(scopeExPattern);
    CJC_NULLPTR_CHECK(scopeExPattern->varDecl);
    scopeExPattern->curFile = &curFile;
    scopeExPattern->varDecl->curFile = &curFile;
    auto& scopeExDecl = *scopeExPattern->varDecl;

    auto scopeType = CreateType(scopeExceptionTy);
    CJC_NULLPTR_CHECK(scopeType);
    scopeType->curFile = &curFile;

    auto scopeTypePattern = CreateTypePattern(std::move(scopeExPattern), std::move(scopeType), *exRef);
    CJC_NULLPTR_CHECK(scopeTypePattern);

    scopeTypePattern->matchBeforeRuntime = false;
    scopeTypePattern->curFile = &curFile;

    auto causedBy = CreateCausedByGetterCall(ctx, scopeExDecl, curFile);
    CJC_NULLPTR_CHECK(causedBy);

    auto unwrapScopeException = CreateCausedByMatch(ctx, std::move(causedBy), exDecl,
        exceptionTy, curFile);
    CJC_NULLPTR_CHECK(unwrapScopeException);

    // _ => throw $ex
    auto throwEx = MakeOwned<ThrowExpr>();
    CJC_NULLPTR_CHECK(throwEx);
    throwEx->expr = WithinFile(CreateRefExpr(exDecl), &curFile);
    throwEx->SetTy({TypeManager::GetNothingTy()});
    throwEx->curFile = &curFile;

    std::vector<OwnedPtr<MatchCase>> cases;
    cases.emplace_back(CreateMatchCase(std::move(scopeTypePattern), std::move(unwrapScopeException)));
    cases.emplace_back(CreateMatchCase(MakeOwned<WildcardPattern>(), std::move(throwEx)));

    for (auto& matchCase : cases) {
        CJC_NULLPTR_CHECK(matchCase);
        matchCase->SetCtxExprForPatterns(exRef.get());
        matchCase->curFile = &curFile;
    }

    auto result = CreateMatchExpr(std::move(exRef), std::move(cases), {TypeManager::GetNothingTy()});
    return WithinFile(std::move(result), &curFile);
}

OwnedPtr<Expr> WrapWithExclusiveScopeExceptionHandling(AfterTypeCheckContext& ctx,
    OwnedPtr<Expr> exclusiveCall, ModalTy retTy)
{
    CJC_NULLPTR_CHECK(exclusiveCall);
    auto curFile = exclusiveCall->curFile;
    CJC_NULLPTR_CHECK(curFile);
    auto exceptionDecl = ctx.importManager.GetCoreDecl<ClassDecl>(CANGJIE_EXCEPTION_CLASS_NAME);
    auto scopeExceptionDecl = ctx.importManager.GetCoreDecl<ClassDecl>(EXCLUSIVE_SCOPE_EXCEPTION_CLASS_NAME);
    CJC_NULLPTR_CHECK(exceptionDecl);
    CJC_NULLPTR_CHECK(scopeExceptionDecl);
    auto exceptionTy = ctx.typeManager.GetClassTy(*exceptionDecl, {});
    auto scopeExceptionTy = ctx.typeManager.GetClassTy(*scopeExceptionDecl, {});
    CJC_NULLPTR_CHECK(exceptionTy);
    CJC_NULLPTR_CHECK(scopeExceptionTy);

    // Build the try block.
    auto tryBlock = CreateBlock(Nodes(std::move(exclusiveCall)), retTy);
    CJC_NULLPTR_CHECK(tryBlock);
    tryBlock->curFile = curFile;

    // Build the Exception catch pattern.
    auto exPattern = CreateVarPattern("$ex", {exceptionTy});
    CJC_NULLPTR_CHECK(exPattern);
    CJC_NULLPTR_CHECK(exPattern->varDecl);
    exPattern->curFile = curFile;
    exPattern->varDecl->curFile = curFile;
    auto& exDecl = *exPattern->varDecl;

    auto catchPattern = MakeOwned<ExceptTypePattern>();
    CJC_NULLPTR_CHECK(catchPattern);
    catchPattern->pattern = std::move(exPattern);
    catchPattern->types.emplace_back(CreateRefType(*exceptionDecl));
    catchPattern->SetTy({exceptionTy});
    catchPattern->curFile = curFile;

    // Build exception dispatch for ExclusiveScopeException:
    // - ExclusiveScopeException: unwrap causedBy and rethrow the cause when present;
    //   otherwise rethrow the original exception.
    // - Other exceptions: rethrow the original exception.
    auto exceptionDispatch = CreateExceptionDispatch(ctx, exDecl, exceptionTy, scopeExceptionTy, *curFile);
    CJC_NULLPTR_CHECK(exceptionDispatch);

    auto catchBlock = CreateBlock(Nodes(std::move(exceptionDispatch)), {TypeManager::GetNothingTy()});
    CJC_NULLPTR_CHECK(catchBlock);
    catchBlock->curFile = curFile;

    auto tryExpr = MakeOwned<TryExpr>();
    CJC_NULLPTR_CHECK(tryExpr);
    tryExpr->tryBlock = std::move(tryBlock);
    tryExpr->catchPatterns.emplace_back(std::move(catchPattern));
    tryExpr->catchBlocks.emplace_back(std::move(catchBlock));
    tryExpr->SetTy(retTy);
    tryExpr->curFile = curFile;
    return tryExpr;
}
} // namespace

OwnedPtr<Expr> WrapWithExclusiveScope(AfterTypeCheckContext& ctx, OwnedPtr<Block> body, ModalTy retTy)
{
    auto exclusiveCall = WrapWithExclusiveScopeBody(ctx, std::move(body), retTy);
    if (!exclusiveCall) {
        return nullptr;
    }
    return WrapWithExclusiveScopeExceptionHandling(ctx, std::move(exclusiveCall), retTy);
}

} // namespace Cangjie::Native::FFI::Java
