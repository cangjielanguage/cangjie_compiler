// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "TypeCheckerImpl.h"

#include "Desugar/AfterTypeCheck.h"

#include "cangjie/AST/Create.h"
#include "cangjie/AST/Utils.h"

using namespace Cangjie;
using namespace AST;
using namespace TypeCheckUtil;
using namespace Sema::Desugar::AfterTypeCheck;

namespace {
// Create `stdx.effect.ImmediateHandlerReturn.Result` from the type
// (Any) -> ImmediateHandlerReturn
OwnedPtr<RefExpr> CreateRefValue(FuncTy& funcTy)
{
    CJC_ASSERT(Ty::IsTyCorrect(funcTy.retTy) && funcTy.retTy->IsEnum());
    EnumTy& enumTy = StaticCast<EnumTy&>(*funcTy.retTy);
    auto decl = LookupEnumMember(enumTy.declPtr, "Result");
    CJC_NULLPTR_CHECK(decl);
    auto refExpr = CreateRefExpr(*decl);
    (void)refExpr->ref.targets.emplace_back(decl); // for `GetFuncTargets`
    refExpr->ty = &funcTy;
    refExpr->instTys = enumTy.typeArgs;
    return refExpr;
}

// Create `stdx.effect.ImmediateHandlerReturn.Exc` from the type
// (Exception) -> ImmediateHandlerReturn
OwnedPtr<RefExpr> CreateRefExc(FuncTy& funcTy)
{
    CJC_ASSERT(Ty::IsTyCorrect(funcTy.retTy) && funcTy.retTy->IsEnum());
    EnumTy& enumTy = StaticCast<EnumTy&>(*funcTy.retTy);
    auto decl = LookupEnumMember(enumTy.declPtr, "Exc");
    CJC_NULLPTR_CHECK(decl);
    auto refExpr = CreateRefExpr(*decl);
    (void)refExpr->ref.targets.emplace_back(decl); // for `GetFuncTargets`
    refExpr->ty = &funcTy;
    refExpr->instTys = enumTy.typeArgs;
    return refExpr;
}

// Create `stdx.effect.ImmediateHandlerReturn.Err` from the type
// (Error) -> ImmediateHandlerReturn
OwnedPtr<RefExpr> CreateRefErr(FuncTy& funcTy)
{
    CJC_ASSERT(Ty::IsTyCorrect(funcTy.retTy) && funcTy.retTy->IsEnum());
    EnumTy& enumTy = StaticCast<EnumTy&>(*funcTy.retTy);
    auto decl = LookupEnumMember(enumTy.declPtr, "Err");
    CJC_NULLPTR_CHECK(decl);
    auto refExpr = CreateRefExpr(*decl);
    (void)refExpr->ref.targets.emplace_back(decl); // for `GetFuncTargets`
    refExpr->ty = &funcTy;
    refExpr->instTys = enumTy.typeArgs;
    return refExpr;
}

/**
 * Get the declaration of the default initializer of a ClassDecl
 *
 * Note: It's assumed there's only one initializer without arguments
 */
Ptr<FuncDecl> GetDefaultInitDecl(ClassDecl& classDecl)
{
    std::vector<Ptr<FuncDecl>> inits;
    for (auto& decl : classDecl.GetMemberDecls()) {
        CJC_NULLPTR_CHECK(decl);
        if (decl->astKind == ASTKind::FUNC_DECL && decl.get()->TestAttr(Attribute::CONSTRUCTOR)) {
            auto funcDecl = StaticCast<FuncDecl*>(decl.get());
            if (funcDecl->funcBody->paramLists.size() == 1 && funcDecl->funcBody->paramLists[0]->params.size() == 0) {
                inits.emplace_back(funcDecl);
            }
        }
    }
    CJC_ASSERT(inits.size() == 1); // `init(fn: ()->T)` is the only constructor
    return inits.front();
}

} // namespace

/**
 * Enclose the tryLambda of a try-handle expression to handle some special
 * exceptions.
 *
 * From:
 * { (<args>) => <body> }
 *
 * To:
 * { (<args>) =>
 *      try {
 *          <body>
 *      } catch (e: ImmediateFrameExceptionWrapper) {
 *          if (HandlerFrame.getActiveFrame() == v.frame) {
 *              throw v.exception
 *          } else {
 *              throw v
 *          }
 *      } catch (e: ImmediateFrameErrorWrapper) {
 *          if (HandlerFrame.getActiveFrame() == v.frame) {
 *              throw v.error
 *          } else {
 *              throw v
 *          }
 *      } catch (e: ImmediateEarlyReturn) {
 *          if (HandlerFrame.getActiveFrame() == e.frame) {
 *              // This is similar to a desugared `(e.result as T).getOrThrow`
 *              match (e.result) {
 *                  case newVar : T => return newVar
 *                  case _ => throw Exception()
 *              }
 *          } else {
 *              throw e
 *          }
 *      }
 * }
 */
void TypeChecker::TypeCheckerImpl::EncloseTryLambda(ASTContext& ctx, OwnedPtr<AST::LambdaExpr>& tryLambda)
{
    OwnedPtr<AST::Block> innerBlock = std::move(tryLambda->funcBody->body);

    // `try { ... }
    auto tryExpr = MakeOwnedNode<TryExpr>();
    CopyBasicInfo(innerBlock, tryExpr);
    tryExpr->tryBlock = std::move(innerBlock);
    tryExpr->ty = tryLambda->funcBody->retType->ty.get();

    // Import declarations and types
    // stdx.effect.ImmediateFrameExceptionWrapper
    auto exceptionWrapperDecl =
        importManager.GetImportedDecl<ClassDecl>(EFFECT_INTERNALS_PACKAGE_NAME, CLASS_FRAME_EXCEPTION_WRAPPER);
    CJC_NULLPTR_CHECK(exceptionWrapperDecl);
    auto exceptionWrapperTy = typeManager.GetClassTy(*exceptionWrapperDecl, {});
    // stdx.effect.ImmediateFrameErrorWrapper
    auto errorWrapperDecl =
        importManager.GetImportedDecl<ClassDecl>(EFFECT_INTERNALS_PACKAGE_NAME, CLASS_FRAME_ERROR_WRAPPER);
    CJC_NULLPTR_CHECK(errorWrapperDecl);
    auto errorWrapperTy = typeManager.GetClassTy(*errorWrapperDecl, {});
    // stdx.effect.ImmediateEarlyReturn
    auto earlyReturnDecl = importManager.GetImportedDecl<ClassDecl>(EFFECT_INTERNALS_PACKAGE_NAME, CLASS_EARLY_RETURN);
    CJC_NULLPTR_CHECK(earlyReturnDecl);
    auto earlyReturnTy = typeManager.GetClassTy(*earlyReturnDecl, {});

    {
        // (v: ImmediateFrameExceptionWrapper)
        auto vp = CreateVarPattern(V_COMPILER, exceptionWrapperTy);
        AST::CopyNodeScopeInfo(vp->varDecl, tryExpr);

        // v.frame
        auto vFrameRef = CreateRefExpr(*vp->varDecl);
        AST::CopyNodeScopeInfo(vFrameRef, tryExpr.get());
        auto frameAccess = CreateMemberAccess(std::move(vFrameRef), "frame");

        // HandlerFrame.getActiveFrame().getOrThrow().parent
        auto getActiveFrame = GetHelperFrameMethod(*tryExpr, "getActiveFrame", {});
        auto getActiveFrameCall = CreateCallExpr(std::move(getActiveFrame), {});
        auto getOrThrowAccess = CreateMemberAccess(std::move(getActiveFrameCall), "getOrThrow");
        auto getOrThrowCall = CreateCallExpr(std::move(getOrThrowAccess), {});
        auto parentAccess = CreateMemberAccess(std::move(getOrThrowCall), "parent");
        SynthesizeWithoutRecover(ctx, parentAccess.get());

        // `(HandlerFrame.getActiveFrame() == v.frame)
        auto binExpr = CreateBinaryExpr(std::move(parentAccess), std::move(frameAccess), TokenKind::EQUAL);
        binExpr->ty = TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN);
        CopyBasicInfo(tryExpr, binExpr);

        // v.exception
        auto vExcRef = CreateRefExpr(*vp->varDecl);
        AST::CopyNodeScopeInfo(vExcRef, tryExpr);
        auto excAccess = CreateMemberAccess(std::move(vExcRef), "exception");

        // throw v.exception
        auto throwExcExpr = MakeOwned<ThrowExpr>();
        throwExcExpr->expr = std::move(excAccess);
        throwExcExpr->ty = TypeManager::GetNothingTy();

        // v
        auto vRef = CreateRefExpr(*vp->varDecl);
        AST::CopyNodeScopeInfo(vRef, tryExpr);

        // throw v
        auto throwExpr = MakeOwned<ThrowExpr>();
        throwExpr->expr = std::move(vRef);
        throwExpr->ty = TypeManager::GetNothingTy();

        // `if (HandlerFrame.getActiveFrame() == v.frame) {
        // `     throw v.exception
        // `} else {
        // `     throw v
        // `}
        std::vector<OwnedPtr<Node>> thenBlockNodes;
        (void)thenBlockNodes.emplace_back(std::move(throwExcExpr));
        auto thenBlock = CreateBlock(std::move(thenBlockNodes));
        thenBlock->ty = TypeManager::GetNothingTy();

        std::vector<OwnedPtr<Node>> elseBlockNodes;
        (void)elseBlockNodes.emplace_back(std::move(throwExpr));
        auto elseBlock = CreateBlock(std::move(elseBlockNodes));
        elseBlock->ty = TypeManager::GetNothingTy();

        auto ifExpr = CreateIfExpr(std::move(binExpr), std::move(thenBlock), std::move(elseBlock));

        // `catch (v: ImmediateFrameExceptionWrapper)
        auto exceptTypePattern = MakeOwnedNode<ExceptTypePattern>();
        exceptTypePattern->pattern = std::move(vp);
        auto excWrapperRefTy = CreateRefType(*exceptionWrapperDecl);
        AST::CopyNodeScopeInfo(excWrapperRefTy, tryExpr);
        excWrapperRefTy->EnableAttr(Attribute::TOOL_ADD);
        (void)exceptTypePattern->types.emplace_back(std::move(excWrapperRefTy));
        exceptTypePattern->ty = exceptionWrapperTy;

        // `catch (v: ImmediateFrameExceptionWrapper) {
        // `    if (HandlerFrame.getActiveFrame() == v.frame) {
        // `        ...
        // `    } else {
        // `         throw v
        // `    }
        // `}
        std::vector<OwnedPtr<Node>> catchBlockNodes;
        (void)catchBlockNodes.emplace_back(std::move(ifExpr));
        auto catchBlock = CreateBlock(std::move(catchBlockNodes));
        catchBlock->ty = TypeManager::GetNothingTy();

        // Added to the tryExpr
        (void)tryExpr->catchPatterns.emplace_back(std::move(exceptTypePattern));
        (void)tryExpr->catchBlocks.emplace_back(std::move(catchBlock));
    }

    {
        // (v: ImmediateFrameErrorWrapper)
        auto vp = CreateVarPattern(V_COMPILER, errorWrapperTy);
        AST::CopyNodeScopeInfo(vp->varDecl, tryExpr);

        // v.frame
        auto vFrameRef = CreateRefExpr(*vp->varDecl);
        AST::CopyNodeScopeInfo(vFrameRef, tryExpr.get());
        auto frameAccess = CreateMemberAccess(std::move(vFrameRef), "frame");

        // HandlerFrame.getActiveFrame().getOrThrow().parent
        auto getActiveFrame = GetHelperFrameMethod(*tryExpr, "getActiveFrame", {});
        auto getActiveFrameCall = CreateCallExpr(std::move(getActiveFrame), {});
        auto getOrThrowAccess = CreateMemberAccess(std::move(getActiveFrameCall), "getOrThrow");
        auto getOrThrowCall = CreateCallExpr(std::move(getOrThrowAccess), {});
        auto parentAccess = CreateMemberAccess(std::move(getOrThrowCall), "parent");
        SynthesizeWithoutRecover(ctx, parentAccess.get());

        // `(HandlerFrame.getActiveFrame() == v.frame)
        auto binExpr = CreateBinaryExpr(std::move(parentAccess), std::move(frameAccess), TokenKind::EQUAL);
        binExpr->ty = TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN);
        CopyBasicInfo(tryExpr, binExpr);

        // v.error
        auto vExcRef = CreateRefExpr(*vp->varDecl);
        AST::CopyNodeScopeInfo(vExcRef, tryExpr);
        auto excAccess = CreateMemberAccess(std::move(vExcRef), "error");

        // throw v.error
        auto throwExcExpr = MakeOwned<ThrowExpr>();
        throwExcExpr->expr = std::move(excAccess);
        throwExcExpr->ty = TypeManager::GetNothingTy();

        // v
        auto vRef = CreateRefExpr(*vp->varDecl);
        AST::CopyNodeScopeInfo(vRef, tryExpr);

        // throw v
        auto throwExpr = MakeOwned<ThrowExpr>();
        throwExpr->expr = std::move(vRef);
        throwExpr->ty = TypeManager::GetNothingTy();

        // `if (HandlerFrame.getActiveFrame() == v.frame) {
        // `     throw v.error
        // `} else {
        // `     throw v
        // `}
        std::vector<OwnedPtr<Node>> thenBlockNodes;
        (void)thenBlockNodes.emplace_back(std::move(throwExcExpr));
        auto thenBlock = CreateBlock(std::move(thenBlockNodes));
        thenBlock->ty = TypeManager::GetNothingTy();

        std::vector<OwnedPtr<Node>> elseBlockNodes;
        (void)elseBlockNodes.emplace_back(std::move(throwExpr));
        auto elseBlock = CreateBlock(std::move(elseBlockNodes));
        elseBlock->ty = TypeManager::GetNothingTy();

        auto ifExpr = CreateIfExpr(std::move(binExpr), std::move(thenBlock), std::move(elseBlock));

        // `catch (v: ImmediateFrameErrorWrapper)
        auto exceptTypePattern = MakeOwnedNode<ExceptTypePattern>();
        exceptTypePattern->pattern = std::move(vp);
        auto excWrapperRefTy = CreateRefType(*errorWrapperDecl);
        AST::CopyNodeScopeInfo(excWrapperRefTy, tryExpr);
        excWrapperRefTy->EnableAttr(Attribute::TOOL_ADD);
        (void)exceptTypePattern->types.emplace_back(std::move(excWrapperRefTy));
        exceptTypePattern->ty = errorWrapperTy;

        // `catch (v: ImmediateFrameExceptionWrapper) {
        // `    if (HandlerFrame.getActiveFrame() == v.frame) {
        // `        ...
        // `    } else {
        // `         throw v
        // `    }
        // `}
        std::vector<OwnedPtr<Node>> catchBlockNodes;
        (void)catchBlockNodes.emplace_back(std::move(ifExpr));
        auto catchBlock = CreateBlock(std::move(catchBlockNodes));
        catchBlock->ty = TypeManager::GetNothingTy();

        // Added to the tryExpr
        (void)tryExpr->catchPatterns.emplace_back(std::move(exceptTypePattern));
        (void)tryExpr->catchBlocks.emplace_back(std::move(catchBlock));
    }

    {
        std::vector<OwnedPtr<MatchCase>> matchCases;

        // (v: ImmediateEarlyReturn)
        auto vp = CreateVarPattern(V_COMPILER, earlyReturnTy);
        AST::CopyNodeScopeInfo(vp->varDecl, tryExpr);

        // v.frame
        auto vFrameRef = CreateRefExpr(*vp->varDecl);
        AST::CopyNodeScopeInfo(vFrameRef, tryExpr.get());
        auto frameAccess = CreateMemberAccess(std::move(vFrameRef), "frame");

        // HandlerFrame.getActiveFrame().getOrThrow().parent
        auto getActiveFrame = GetHelperFrameMethod(*tryExpr, "getActiveFrame", {});
        auto getActiveFrameCall = CreateCallExpr(std::move(getActiveFrame), {});
        auto getOrThrowAccess = CreateMemberAccess(std::move(getActiveFrameCall), "getOrThrow");
        auto getOrThrowCall = CreateCallExpr(std::move(getOrThrowAccess), {});
        auto parentAccess = CreateMemberAccess(std::move(getOrThrowCall), "parent");
        SynthesizeWithoutRecover(ctx, parentAccess.get());

        // `(HandlerFrame.getActiveFrame() == v.frame)
        auto binExpr = CreateBinaryExpr(std::move(parentAccess), std::move(frameAccess), TokenKind::EQUAL);
        binExpr->ty = TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN);
        CopyBasicInfo(tryExpr, binExpr);

        // v.result
        auto vpRef = CreateRefExpr(*vp->varDecl);
        AST::CopyNodeScopeInfo(vpRef, tryExpr);
        auto excAccess = CreateMemberAccess(std::move(vpRef), "result");

        // case newVar: T
        auto varPattern = CreateVarPattern(V_COMPILER, tryLambda->funcBody->retType->ty.get());
        AST::CopyNodeScopeInfo(varPattern->varDecl, tryExpr);

        auto vpRef2 = CreateRefExpr(*varPattern->varDecl);
        AST::CopyNodeScopeInfo(vpRef2, innerBlock);

        // return newVar
        auto returnExpr = MakeOwnedNode<ReturnExpr>();
        returnExpr->expr = std::move(vpRef2);
        returnExpr->refFuncBody = tryLambda->funcBody;
        returnExpr->ty = TypeManager::GetNothingTy();
        CopyNodeScopeInfo(returnExpr, tryExpr);

        // case newVar: T => return newVar
        matchCases.emplace_back(CreateMatchCase(CreateRuntimePreparedTypePattern(typeManager, std::move(varPattern),
            ASTCloner::Clone(tryLambda->funcBody->retType.get()), *excAccess),
            std::move(returnExpr)));

        // case _
        auto wildcard = MakeOwnedNode<WildcardPattern>();
        wildcard->ty = excAccess->ty;

        // Exception()
        auto exceptionDecl = importManager.GetCoreDecl<ClassDecl>(CLASS_EXCEPTION);
        CJC_NULLPTR_CHECK(exceptionDecl);
        auto exceptionType = exceptionDecl->ty;
        auto excInitDecl = GetDefaultInitDecl(*exceptionDecl);
        OwnedPtr<RefExpr> re23 = CreateRefExpr("Exception");
        CopyBasicInfo(innerBlock, re23.get());
        re23->isAlone = false;
        re23->ref.target = excInitDecl;
        re23->ty = exceptionType;

        auto earlyCreation =
            AST::CreateCallExpr(std::move(re23), {}, excInitDecl, exceptionType, CallKind::CALL_OBJECT_CREATION);
        CopyBasicInfo(innerBlock, earlyCreation);

        // throw Exception()
        auto throwExpr = MakeOwned<ThrowExpr>();
        throwExpr->expr = std::move(earlyCreation);
        throwExpr->ty = TypeManager::GetNothingTy();

        // case _ => throw Exception()
        matchCases.emplace_back(CreateMatchCase(std::move(wildcard), std::move(throwExpr)));

        // match (e.result) {
        //      case newVar : T => return newVar
        //      case _ => throw Exception()
        // }
        auto matchExpr = CreateMatchExpr(
            std::move(excAccess), std::move(matchCases), TypeManager::GetInvalidTy(), Expr::SugarKind::AS);
        matchExpr->ty = TypeManager::GetNothingTy();

        // v
        auto vRef = CreateRefExpr(*vp->varDecl);
        AST::CopyNodeScopeInfo(vRef, tryExpr);

        // throw v
        auto reThrowExpr = MakeOwned<ThrowExpr>();
        reThrowExpr->expr = std::move(vRef);
        reThrowExpr->ty = TypeManager::GetNothingTy();

        // `if (HandlerFrame.getActiveFrame() == v.frame) {
        // `     match (e.result) {
        // `          case newVar : T => return newVar
        // `          case _ => throw Exception()
        // `     }
        // `} else {
        // `     throw v
        // `}
        std::vector<OwnedPtr<Node>> thenBlockNodes;
        (void)thenBlockNodes.emplace_back(std::move(matchExpr));
        auto thenBlock = CreateBlock(std::move(thenBlockNodes));
        thenBlock->ty = TypeManager::GetNothingTy();

        std::vector<OwnedPtr<Node>> elseBlockNodes;
        (void)elseBlockNodes.emplace_back(std::move(reThrowExpr));
        auto elseBlock = CreateBlock(std::move(elseBlockNodes));
        elseBlock->ty = TypeManager::GetNothingTy();

        auto ifExpr = CreateIfExpr(std::move(binExpr), std::move(thenBlock), std::move(elseBlock));

        // `catch (v: ImmediateEarlyReturn)
        auto exceptTypePattern = MakeOwnedNode<ExceptTypePattern>();
        exceptTypePattern->pattern = std::move(vp);
        auto excWrapperRefTy = CreateRefType(*earlyReturnDecl);
        AST::CopyNodeScopeInfo(excWrapperRefTy, tryExpr);
        excWrapperRefTy->EnableAttr(Attribute::TOOL_ADD);
        (void)exceptTypePattern->types.emplace_back(std::move(excWrapperRefTy));
        exceptTypePattern->ty = earlyReturnTy;

        // `catch (e: ImmediateEarlyReturn) {
        // `    match (e.result) {
        // `        case newVar : T => return newVar
        // `        case _ => throw Exception()
        // `    }
        // `}
        std::vector<OwnedPtr<Node>> catchBlockNodes;
        (void)catchBlockNodes.emplace_back(std::move(ifExpr));
        auto catchBlock = CreateBlock(std::move(catchBlockNodes));
        catchBlock->ty = TypeManager::GetNothingTy();

        // Added to the tryExpr
        (void)tryExpr->catchPatterns.emplace_back(std::move(exceptTypePattern));
        (void)tryExpr->catchBlocks.emplace_back(std::move(catchBlock));
    }

    // Since we're modifying the top block of the lambda expression, it must
    // finish with an explicit return expression
    auto returnExpr = MakeOwnedNode<ReturnExpr>();
    returnExpr->expr = std::move(tryExpr);
    returnExpr->refFuncBody = tryLambda->funcBody;
    CopyNodeScopeInfo(returnExpr, tryExpr);

    std::vector<OwnedPtr<Cangjie::AST::Node>> nodes;
    nodes.emplace_back(std::move(returnExpr));
    auto resultBlock = CreateBlock(std::move(nodes));
    resultBlock->ty = TypeManager::GetNothingTy();

    tryLambda->funcBody->body = std::move(resultBlock);
}

/*
    `let frame = Frame({=>
    `    try {
    `        println("trying...")
    `        let a = Frame.perf(Comm("foo"))
    `        println("tried a = ${a}")
    `    } catch (e: Exception){
    `        println("exception")
    `    }
    `})
*/
VarDecl& TypeChecker::TypeCheckerImpl::CreateFrame(ASTContext& ctx, TryExpr& te, std::vector<OwnedPtr<Node>>& block)
{
    auto frameClassName = CLASS_IMMEDIATE_FRAME;
    // To: Frame({=> try{...} catch{...}})
    auto lambdaTy = DynamicCast<FuncTy*>(te.tryLambda->ty);
    CJC_NULLPTR_CHECK(lambdaTy);
    // create Frame's reference
    auto re = CreateRefExpr(frameClassName);
    AST::CopyNodeScopeInfo(re, te.tryLambda);
    re->isAlone = false;
    // create function call: Frame(...)
    auto frameDecl = importManager.GetImportedDecl<ClassDecl>(EFFECT_INTERNALS_PACKAGE_NAME, frameClassName);
    CJC_NULLPTR_CHECK(frameDecl);
    re->SetTarget(frameDecl);

    auto tryExprTy = lambdaTy->retTy;
    auto frameClassTy = typeManager.GetClassTy(*frameDecl, {tryExprTy});
    re->ty = frameClassTy;
    re->instTys.push_back(tryExprTy);

    auto frameInit = CreateMemberAccess(std::move(re), "init");
    CJC_ASSERT(frameInit->target);
    frameInit->targets.emplace_back(StaticCast<FuncDecl*>(frameInit->target));

    EncloseTryLambda(ctx, te.tryLambda);

    // We cannot just pass the lambda directly to the constructor, because then the call
    // to ChkCallExpr would delete the desugaring information, so we bind the lambda to
    // a variable before calling the constructor
    auto lambdaVarDecl = CreateVarDecl("$frameLambda", std::move(te.tryLambda));
    AST::CopyNodeScopeInfo(re, lambdaVarDecl);
    auto lambdaVar = CreateRefExpr(*lambdaVarDecl);
    AST::CopyNodeScopeInfo(re, lambdaVar);

    std::vector<OwnedPtr<FuncArg>> args;
    args.emplace_back(CreateFuncArg(std::move(lambdaVar)));
    auto frameCreation = AST::CreateCallExpr(
        std::move(frameInit), std::move(args), nullptr, frameClassTy, CallKind::CALL_OBJECT_CREATION);
    frameCreation->desugarArgs = {};
    auto typecheckOk = ChkCallExpr(ctx, frameClassTy, *frameCreation);
    CJC_ASSERT(typecheckOk);
    OwnedPtr<VarDecl> frame = CreateVarDecl("frame", std::move(frameCreation));
    frame->ty = frameClassTy;
    auto& frameRef = *frame;

    block.emplace_back(std::move(lambdaVarDecl));
    block.emplace_back(std::move(frame));

    return frameRef;
}

void TypeChecker::TypeCheckerImpl::CreateSetFinally(
    ASTContext& ctx, TryExpr& te, VarDecl& frame, std::vector<OwnedPtr<Node>>& block)
{
    auto target = CreateRefExpr(frame);
    AST::CopyNodeScopeInfo(target, &te);
    auto setFinally = CreateMemberAccess(std::move(target), "setFinally");
    setFinally->begin = te.begin;
    setFinally->end = te.end;
    InferMemberAccess(ctx, *setFinally);
    std::vector<OwnedPtr<FuncArg>> args;
    args.emplace_back(CreateFuncArg(std::move(te.finallyLambda)));
    auto setFinallyCall = CreateCallExpr(std::move(setFinally), std::move(args));
    setFinallyCall->ty = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
    setFinallyCall->callKind = AST::CallKind::CALL_DECLARED_FUNCTION;
    block.emplace_back(std::move(setFinallyCall));
}

/*
    frame.setHandler({effect: Command, resumption: Resumption<Any> =>
        println("handling")
        resumption.proceed(Foo())
    })
*/
void TypeChecker::TypeCheckerImpl::CreateSetHandler(
    ASTContext& ctx, TryExpr& te, VarDecl& frame, std::vector<OwnedPtr<Node>>& block)
{
    auto unitTy = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
    // each effect will have its handler, so there will be multipl setHandler call
    for (auto& handler : te.handlers) {
        CommandTypePattern* commandPattern = RawStaticCast<CommandTypePattern*>(handler.commandPattern.get());
        for (uint64_t j = 0; j < commandPattern->types.size(); j++) {
            // To: create memberAccess
            auto re = CreateRefExpr(frame);
            AST::CopyNodeScopeInfo(re, handler.block);
            OwnedPtr<MemberAccess> ma;
            ma = CreateMemberAccess(std::move(re), "setImmediateHandler");

            // Function `CreateMemberAccess` will not initialise `ma.targets`,
            // so we need to assign `targets` by calling `ChkCallExpr` later
            OwnedPtr<Type> effectTypeArgument = ASTCloner::Clone(commandPattern->types[j].get());
            ma->typeArguments.emplace_back(std::move(effectTypeArgument));
            ma->instTys.emplace_back(commandPattern->types[j]->ty);

            OwnedPtr<Type> resumptionTypeArgument = MakeOwned<Type>();
            CJC_ASSERT(handler.commandResultTy);
            resumptionTypeArgument->ty = handler.commandResultTy;
            ma->typeArguments.emplace_back(std::move(resumptionTypeArgument));
            ma->instTys.emplace_back(handler.commandResultTy);

            // setImmediateHandler lives in HandlerFrame, which is not parameterized
            // by return type, so it takes the return type as an extra type parameter
            OwnedPtr<Type> resultTypeArgument = MakeOwned<Type>();
            resultTypeArgument->ty = te.ty;
            ma->typeArguments.emplace_back(std::move(resultTypeArgument));
            ma->instTys.emplace_back(te.ty);

            // We need to set the targets by hand, because the call to ChkCallExpr
            // below expects targets to be nonempty
            CJC_ASSERT(ma->target);
            ma->targets.emplace_back(StaticCast<FuncDecl*>(ma->target));

            OwnedPtr<AST::LambdaExpr> handlerLambda = std::move(handler.desugaredLambda);
            /**
             * When a handler is immediate we need to treat exceptions in the handler
             * and results of the desugaredLambda differently, since the stack of the
             * try expression needs to be unwinded. In here we modify the desugaredLambda
             * to achieve that
             *
             * From:
             * {(e:Eff) =>
             *  <block>
             * }
             *
             * To:
             * {(e: Eff) =>
             *  let result = try {
             *      <block>
             *  } catch (x: Exception) {
             *      throw ImmediateFrameExceptionWrapper(HandlerFrame.getActiveFrame(), x)
             *  } catch (x: Error) {
             *      throw ImmediateFrameErrorWrapper(HandlerFrame.getActiveFrame(), x)
             *  }
             *  throw ImmediateFrameEarlyReturn(HandlerFrame.getActiveFrame(), result)
             * }
             */
            // Get the inner block
            OwnedPtr<AST::Block> innerBlock = std::move(handlerLambda->funcBody->body);

            // remove return expr from inner block since we wan't to treat it as
            // an exception now
            if (auto lastExpr = std::move(innerBlock->body.back())) {
                auto returnExpr = StaticCast<ReturnExpr*>(lastExpr.get());
                CJC_NULLPTR_CHECK(returnExpr);
                innerBlock->body.back() = std::move(returnExpr->expr);
            }
            innerBlock->ty = Ty::GetInitialTy();

            // Get all declarations and types needed
            // std.core.Exception
            auto exceptionDecl = importManager.GetCoreDecl<ClassDecl>(CLASS_EXCEPTION);
            CJC_NULLPTR_CHECK(exceptionDecl);
            auto exceptionType = exceptionDecl->ty;
            // std.core.Error
            auto errorDecl = importManager.GetCoreDecl<ClassDecl>(CLASS_ERROR);
            CJC_NULLPTR_CHECK(errorDecl);
            auto errorType = errorDecl->ty;
            // stdx.effect.ImmediateFrameExceptionWrapper
            auto exceptionWrapperDecl = importManager.GetImportedDecl<ClassDecl>(
                EFFECT_INTERNALS_PACKAGE_NAME, CLASS_FRAME_EXCEPTION_WRAPPER);
            CJC_NULLPTR_CHECK(exceptionWrapperDecl);
            auto exceptionWrapperTy = typeManager.GetClassTy(*exceptionWrapperDecl, {});
            // stdx.effect.ImmediateFrameErrorWrapper
            auto errorWrapperDecl =
                importManager.GetImportedDecl<ClassDecl>(EFFECT_INTERNALS_PACKAGE_NAME, CLASS_FRAME_ERROR_WRAPPER);
            CJC_NULLPTR_CHECK(errorWrapperDecl);
            auto errorWrapperTy = typeManager.GetClassTy(*errorWrapperDecl, {});
            // stdx.effect.ImmediateEarlyReturn
            auto earlyReturnDecl =
                importManager.GetImportedDecl<ClassDecl>(EFFECT_INTERNALS_PACKAGE_NAME, CLASS_EARLY_RETURN);
            CJC_NULLPTR_CHECK(earlyReturnDecl);
            auto earlyReturnTy = typeManager.GetClassTy(*earlyReturnDecl, {});

            // `try { ... }
            auto tryExpr = MakeOwnedNode<TryExpr>();
            CopyBasicInfo(innerBlock, tryExpr);

            {
                // (v: Exception)
                auto vp = CreateVarPattern(V_COMPILER, exceptionType);
                AST::CopyNodeScopeInfo(vp->varDecl, innerBlock);

                // ImmediateFrameExceptionWrapper( ... )
                std::vector<Ptr<FuncDecl>> inits;
                for (auto& decl : exceptionWrapperDecl->GetMemberDecls()) {
                    CJC_NULLPTR_CHECK(decl);
                    if (decl->astKind == ASTKind::FUNC_DECL && decl.get()->TestAttr(Attribute::CONSTRUCTOR)) {
                        inits.emplace_back(StaticCast<FuncDecl*>(decl.get()));
                    }
                }
                CJC_ASSERT(inits.size() == 1);
                auto initDecl = inits.front();

                CJC_ASSERT(Ty::IsTyCorrect(initDecl->ty) && initDecl->ty->IsFunc());

                OwnedPtr<RefExpr> re2 = CreateRefExpr(CLASS_FRAME_EXCEPTION_WRAPPER);
                re2->isAlone = false;
                re2->ref.target = initDecl;
                re2->ty = initDecl->ty;
                CopyBasicInfo(innerBlock, re2.get());

                // (v)
                std::vector<OwnedPtr<FuncArg>> args;

                // HandlerFrame.getActiveFrame()
                auto getActiveFrame = GetHelperFrameMethod(*innerBlock, "getActiveFrame", {});
                auto getActiveFrameCall = CreateCallExpr(std::move(getActiveFrame), {});
                SynthesizeWithoutRecover(ctx, getActiveFrameCall.get());

                auto vpRef = CreateRefExpr(*vp->varDecl);
                AST::CopyNodeScopeInfo(vpRef, innerBlock);

                // ImmediateFrameExceptionWrapper(HandlerFrame.getActiveFrame(), v)
                args.emplace_back(CreateFuncArg(std::move(getActiveFrameCall)));
                args.emplace_back(CreateFuncArg(std::move(vpRef)));
                auto wrapperCreation = AST::CreateCallExpr(
                    std::move(re2), std::move(args), initDecl, exceptionWrapperTy, CallKind::CALL_OBJECT_CREATION);
                CopyBasicInfo(innerBlock, wrapperCreation);
                wrapperCreation->desugarArgs = {};

                // throw ImmediateFrameExceptionWrapper(...)
                auto throwExpr = MakeOwned<ThrowExpr>();
                throwExpr->expr = std::move(wrapperCreation);
                throwExpr->ty = TypeManager::GetNothingTy();

                // {
                //      throw ImmediateFrameExceptionWrapper(...)
                // }
                std::vector<OwnedPtr<Node>> catchBlockNodes;
                (void)catchBlockNodes.emplace_back(std::move(throwExpr));
                auto catchBlock = CreateBlock(std::move(catchBlockNodes));

                // (v: Exception)
                auto exceptTypePattern = MakeOwnedNode<ExceptTypePattern>();
                exceptTypePattern->pattern = std::move(vp);
                (void)exceptTypePattern->types.emplace_back(CreateRefType(*exceptionDecl));
                exceptTypePattern->ty = exceptionType;

                // `catch (v: Exception) {
                // `     throw ImmediateFrameExceptionWrapper(...)
                // `}
                (void)tryExpr->catchBlocks.emplace_back(std::move(catchBlock));
                (void)tryExpr->catchPatterns.emplace_back(std::move(exceptTypePattern));
            }

            {
                // (v: Error)
                auto vp = CreateVarPattern(V_COMPILER, errorType);
                AST::CopyNodeScopeInfo(vp->varDecl, innerBlock);

                // ImmediateFrameErrorWrapper( ... )
                std::vector<Ptr<FuncDecl>> inits;
                for (auto& decl : errorWrapperDecl->GetMemberDecls()) {
                    CJC_NULLPTR_CHECK(decl);
                    if (decl->astKind == ASTKind::FUNC_DECL && decl.get()->TestAttr(Attribute::CONSTRUCTOR)) {
                        inits.emplace_back(StaticCast<FuncDecl*>(decl.get()));
                    }
                }
                CJC_ASSERT(inits.size() == 1);
                auto initDecl = inits.front();

                CJC_ASSERT(Ty::IsTyCorrect(initDecl->ty) && initDecl->ty->IsFunc());

                OwnedPtr<RefExpr> re2 = CreateRefExpr(CLASS_FRAME_ERROR_WRAPPER);
                re2->isAlone = false;
                re2->ref.target = initDecl;
                re2->ty = initDecl->ty;
                CopyBasicInfo(innerBlock, re2.get());

                // (v)
                std::vector<OwnedPtr<FuncArg>> args;

                // HandlerFrame.getActiveFrame()
                auto getActiveFrame = GetHelperFrameMethod(*innerBlock, "getActiveFrame", {});
                auto getActiveFrameCall = CreateCallExpr(std::move(getActiveFrame), {});
                SynthesizeWithoutRecover(ctx, getActiveFrameCall.get());

                auto vpRef = CreateRefExpr(*vp->varDecl);
                AST::CopyNodeScopeInfo(vpRef, innerBlock);

                // ImmediateFrameErrorWrapper(HandlerFrame.getActiveFrame(), v)
                args.emplace_back(CreateFuncArg(std::move(getActiveFrameCall)));
                args.emplace_back(CreateFuncArg(std::move(vpRef)));
                auto wrapperCreation = AST::CreateCallExpr(
                    std::move(re2), std::move(args), initDecl, errorWrapperTy, CallKind::CALL_OBJECT_CREATION);
                CopyBasicInfo(innerBlock, wrapperCreation);
                wrapperCreation->desugarArgs = {};

                // throw ImmediateFrameErrorWrapper(...)
                auto throwExpr = MakeOwned<ThrowExpr>();
                throwExpr->expr = std::move(wrapperCreation);
                throwExpr->ty = TypeManager::GetNothingTy();

                // {
                //      throw ImmediateFrameErrorWrapper(...)
                // }
                std::vector<OwnedPtr<Node>> catchBlockNodes;
                (void)catchBlockNodes.emplace_back(std::move(throwExpr));
                auto catchBlock = CreateBlock(std::move(catchBlockNodes));

                // (v: Error)
                auto exceptTypePattern = MakeOwnedNode<ExceptTypePattern>();
                exceptTypePattern->pattern = std::move(vp);
                (void)exceptTypePattern->types.emplace_back(CreateRefType(*errorDecl));
                exceptTypePattern->ty = errorType;

                // `catch (v: Error) {
                // `     throw ImmediateFrameErrorWrapper(...)
                // `}
                (void)tryExpr->catchBlocks.emplace_back(std::move(catchBlock));
                (void)tryExpr->catchPatterns.emplace_back(std::move(exceptTypePattern));
            }

            tryExpr->tryBlock = std::move(innerBlock);

            // Recover all types in the try expression
            SynthesizeWithoutRecover(ctx, tryExpr);

            std::vector<OwnedPtr<Cangjie::AST::Node>> nodes;

            {
                // let result = try { ... } catch { ... }
                auto tryResult = CreateVarDecl(V_COMPILER, std::move(tryExpr));
                AST::CopyNodeScopeInfo(tryResult, tryResult);

                auto resRef = CreateRefExpr(*tryResult);
                CopyNodeScopeInfo(resRef, tryResult);

                std::vector<Ptr<FuncDecl>> inits;
                for (auto& decl : earlyReturnDecl->GetMemberDecls()) {
                    CJC_NULLPTR_CHECK(decl);
                    if (decl->astKind == ASTKind::FUNC_DECL && decl.get()->TestAttr(Attribute::CONSTRUCTOR)) {
                        inits.emplace_back(StaticCast<FuncDecl*>(decl.get()));
                    }
                }
                CJC_ASSERT(inits.size() == 1);
                auto initDecl = inits.front();
                CJC_ASSERT(Ty::IsTyCorrect(initDecl->ty) && initDecl->ty->IsFunc());

                // HandlerFrame.getActiveFrame()
                auto getActiveFrame = GetHelperFrameMethod(*tryResult, "getActiveFrame", {});
                auto getActiveFrameCall = CreateCallExpr(std::move(getActiveFrame), {});
                SynthesizeWithoutRecover(ctx, getActiveFrameCall.get());

                // ImmediateEarlyReturn(...)
                OwnedPtr<RefExpr> re2 = CreateRefExpr(CLASS_EARLY_RETURN);
                re2->isAlone = false;
                re2->ref.target = initDecl;
                re2->ty = earlyReturnTy;
                CopyBasicInfo(tryResult, re2.get());

                // ImmediateEarlyReturn(HandlerFrame.getActiveFrame(), result)
                std::vector<OwnedPtr<FuncArg>> args2;
                args2.emplace_back(CreateFuncArg(std::move(getActiveFrameCall)));
                args2.emplace_back(CreateFuncArg(std::move(resRef)));
                auto earlyCreation = AST::CreateCallExpr(
                    std::move(re2), std::move(args2), initDecl, earlyReturnTy, CallKind::CALL_OBJECT_CREATION);
                CopyBasicInfo(tryResult, earlyCreation);
                earlyCreation->desugarArgs = {};

                // throw ImmediateEarlyReturn(...)
                auto throwExpr2 = MakeOwned<ThrowExpr>();
                throwExpr2->expr = std::move(earlyCreation);
                throwExpr2->ty = TypeManager::GetNothingTy();

                nodes.emplace_back(std::move(tryResult));
                nodes.emplace_back(std::move(throwExpr2));
            }

            // `let result = try {
            // `     <block>
            // `} catch (x: Exception) {
            // `     throw ImmediateFrameExceptionWrapper(...)
            // `}
            // `throw ImmediateFrameEarlyReturn(...)
            auto resultBlock = CreateBlock(std::move(nodes));

            // Recover the types of the whole block
            SynthesizeWithoutRecover(ctx, resultBlock);
            handlerLambda->funcBody->body = std::move(resultBlock);

            // Finally, we need to change the type of the try lambda from T
            // to ImmediateHandlerReturn<T>
            auto resultDecl =
                importManager.GetImportedDecl<EnumDecl>(EFFECT_INTERNALS_PACKAGE_NAME, "ImmediateHandlerReturn");
            CJC_NULLPTR_CHECK(resultDecl);
            auto resultDeclTy = typeManager.GetEnumTy(*resultDecl, {handler.commandResultTy});

            handlerLambda->ty =
                typeManager.GetFunctionTy(DynamicCast<FuncTy*>(handlerLambda->ty)->paramTys, resultDeclTy);
            handlerLambda->funcBody->ty =
                typeManager.GetFunctionTy(DynamicCast<FuncTy*>(handlerLambda->ty)->paramTys, resultDeclTy);
            handlerLambda->funcBody->retType = CreateRefType(*resultDeclTy->decl);

            // Like in createFrame, we can't just pass the lambda to `setHandler`
            OwnedPtr<AST::VarDecl> handlerLambdaVarDecl = CreateVarDecl("$handlerLambda", std::move(handlerLambda));
            AST::CopyNodeScopeInfo(re, handlerLambdaVarDecl);
            auto handlerLambdaVar = CreateRefExpr(*handlerLambdaVarDecl);
            AST::CopyNodeScopeInfo(re, handlerLambdaVar);

            std::vector<OwnedPtr<FuncArg>> args;
            args.emplace_back(CreateFuncArg(std::move(handlerLambdaVar)));
            auto setHandlerCall = AST::CreateCallExpr(std::move(ma), std::move(args));
            setHandlerCall->callKind = CallKind::CALL_DECLARED_FUNCTION;

            ChkCallExpr(ctx, unitTy, *setHandlerCall);

            block.emplace_back(std::move(handlerLambdaVarDecl));
            block.emplace_back(std::move(setHandlerCall));
        }
    }
}

/*
    let result = frame.start()
*/
void TypeChecker::TypeCheckerImpl::CreateResult(
    ASTContext& ctx, const TryExpr& te, VarDecl& frame, std::vector<OwnedPtr<Node>>& block)
{
    // To: create memberAccess
    auto re = CreateRefExpr(frame);
    re->begin = te.begin;
    re->end = te.end;
    AST::CopyNodeScopeInfo(re, &frame);
    OwnedPtr<MemberAccess> ma = CreateMemberAccess(std::move(re), "start");
    CJC_ASSERT(ma->target);
    ma->targets.emplace_back(StaticCast<FuncDecl*>(ma->target));
    // To: frame.start()
    auto startCall = AST::CreateCallExpr(std::move(ma), {});
    auto typecheckOk = ChkCallExpr(ctx, te.ty, *startCall);
    CJC_ASSERT(typecheckOk);

    auto tryTy = te.ty;
    std::vector<OwnedPtr<MatchCase>> matchCases;
    auto varPattern = CreateVarPattern(V_COMPILER, tryTy);
    auto varDecl = varPattern->varDecl.get();
    varDecl->curFile = te.curFile;

    // This is a hack. We need an AST Type object to create the pattern, but we
    // don't have access to one, since tryTy was obtained by synthesis during sema, so
    // we create a fake one and bind it to the correct sema type
    auto type = CreateRefType(V_COMPILER);
    type->ty = tryTy;
    matchCases.emplace_back(CreateMatchCase(
        CreateRuntimePreparedTypePattern(typeManager, std::move(varPattern), std::move(type), *startCall),
        CreateRefExpr(*varDecl)));
    auto match = CreateMatchExpr(std::move(startCall), std::move(matchCases), tryTy);
    block.emplace_back(std::move(match));
}
/*
* *************** before desugar ***************
    try {
        println("trying...")
        let a = perform Comm("foo")
        println("tried")
    } catch (e: Exception){
        println("exception")
    } handle (e: Comm, res: Resumption<Any>) {
        println("handling")
        resume res with Foo()
    }
    println("no effects any more")
* *************** after desugar ****************
    let frame = Frame({=>
        try {
            println("trying...")
            let a = Frame.perf(Comm("foo"))
            println("tried a = ${a}")
        } catch (e: Exception){
            println("exception")
        }
    })
    frame.setHandler({e: Comm, res: Resumption<Any> =>
        println("handling")
        res.proceed(Foo())
    })
    let result = frame.start()
*/
void TypeChecker::TypeCheckerImpl::DesugarTryToFrame(ASTContext& ctx, TryExpr& te)
{
    if (te.handlers.empty() || !Ty::IsTyCorrect(te.ty)) {
        return;
    }
    std::vector<OwnedPtr<Node>> nodes;

    auto& frame = CreateFrame(ctx, te, nodes);
    CreateSetHandler(ctx, te, frame, nodes);
    if (te.finallyLambda) {
        CreateSetFinally(ctx, te, frame, nodes);
    }
    CreateResult(ctx, te, frame, nodes);

    OwnedPtr<Block> dummyBlock = CreateBlock(std::move(nodes), te.ty);
    AST::CopyNodeScopeInfo(dummyBlock, &te);

    te.desugarExpr = std::move(dummyBlock);
}

OwnedPtr<Expr> TypeChecker::TypeCheckerImpl::GetHelperFrameMethod(
    AST::Node& base, const std::string& methodName, std::vector<Ptr<Ty>> typeArgs)
{
    auto frameDecl = importManager.GetImportedDecl<ClassDecl>(EFFECT_INTERNALS_PACKAGE_NAME, CLASS_HANDLER_FRAME);
    CJC_NULLPTR_CHECK(frameDecl);
    auto frameClassTy = typeManager.GetClassTy(*frameDecl, {});
    auto re = CreateRefExpr(CLASS_HANDLER_FRAME);
    re->ref.target = frameDecl;
    AST::CopyNodeScopeInfo(re, &base);
    re->ty = frameClassTy;

    auto memberFunc = CreateMemberAccess(std::move(re), methodName);
    memberFunc->instTys = std::move(typeArgs);
    CJC_ASSERT(memberFunc->target);
    memberFunc->targets.emplace_back(StaticCast<FuncDecl*>(memberFunc->target));

    return memberFunc;
}

/*
    From: perform Comm("foo")
    To: Frame.perf(Comm("foo"))
*/
void TypeChecker::TypeCheckerImpl::DesugarPerform(ASTContext& ctx, AST::PerformExpr& pe)
{
    if (!Ty::IsTyCorrect(pe.ty)) {
        // Sometimes the Desugar function is called on "junk" code that has not been
        // through the typechecker. For example, code in primary constructors gets
        // desugared early on; the pre-desugaring code is never typechecked but for
        // some reason desugaring still happens inside it; this desugaring needs to
        // have access to types, therefore we can skip it entirely if the typechecker
        // has not been run
        return;
    }
    auto cmdTy = pe.expr->ty;
    auto resultTy = pe.ty;
    auto perfMethod = GetHelperFrameMethod(pe, "perf", {cmdTy, resultTy});

    std::vector<OwnedPtr<FuncArg>> args;
    args.emplace_back(CreateFuncArg(std::move(pe.expr)));
    auto perfCall = CreateCallExpr(std::move(perfMethod), std::move(args));
    auto typecheckOk = ChkCallExpr(ctx, pe.ty, *perfCall);
    CJC_ASSERT(typecheckOk);
    // Checking for a call expr may get rid of some desugarings
    DesugarForPropDecl(*perfCall);

    pe.desugarExpr = std::move(perfCall);
}

void TypeChecker::TypeCheckerImpl::DesugarResume(ASTContext& ctx, AST::ResumeExpr& re)
{
    if (!Ty::IsTyCorrect(re.ty)) {
        // Sometimes the Desugar function is called on "junk" code that has not been
        // through the typechecker. For example, code in primary constructors gets
        // desugared early on; the pre-desugaring code is never typechecked but for
        // some reason desugaring still happens inside it; this desugaring needs to
        // have access to types, therefore we can skip it entirely if the typechecker
        // has not been run
        return;
    }
    DesugarImmediateResume(ctx, re);
}

/**
 * From: resume with val
 * To: ImmediateHandlerReturn<T>.Result(val)
 *
 * From: resume throwing exc
 * To: ImmediateHandlerReturn<T>.Exc(exc)
 */
void TypeChecker::TypeCheckerImpl::DesugarImmediateResume(ASTContext& ctx, AST::ResumeExpr& re)
{
    OwnedPtr<Expr> desugared;

    auto resultDecl = importManager.GetImportedDecl<EnumDecl>(EFFECT_INTERNALS_PACKAGE_NAME, "ImmediateHandlerReturn");
    CJC_NULLPTR_CHECK(resultDecl);
    CJC_ASSERT(re.enclosing.has_value());
    auto resultDeclTy = typeManager.GetEnumTy(*resultDecl, {re.enclosing.value()->commandResultTy});
    CJC_NULLPTR_CHECK(resultDeclTy);

    if (re.throwingExpr) {
        auto exceptionDecl = importManager.GetCoreDecl<ClassDecl>(CLASS_EXCEPTION);
        CJC_NULLPTR_CHECK(exceptionDecl);
        auto errorDecl = importManager.GetCoreDecl<ClassDecl>(CLASS_ERROR);
        CJC_NULLPTR_CHECK(errorDecl);

        OwnedPtr<RefExpr> wrapperFnRef;
        if (typeManager.IsSubtype(re.throwingExpr->ty, exceptionDecl->ty)) {
            auto excTy = typeManager.GetFunctionTy({exceptionDecl->ty}, resultDeclTy);
            wrapperFnRef = CreateRefExc(*excTy);
        } else if (typeManager.IsSubtype(re.throwingExpr->ty, errorDecl->ty)) {
            auto excTy = typeManager.GetFunctionTy({errorDecl->ty}, resultDeclTy);
            wrapperFnRef = CreateRefErr(*excTy);
        }
        CJC_NULLPTR_CHECK(wrapperFnRef);

        std::vector<OwnedPtr<FuncArg>> args;
        (void)args.emplace_back(CreateFuncArg(std::move(re.throwingExpr)));
        auto wrappedExc = CreateCallExpr(std::move(wrapperFnRef), std::move(args));

        ChkCallExpr(ctx, resultDeclTy, *wrappedExc);
        // Checking for a call expr may get rid of some desugarings
        DesugarForPropDecl(*wrappedExc);
        auto returnExpr = MakeOwnedNode<ReturnExpr>();
        returnExpr->expr = std::move(wrappedExc);
        desugared = std::move(returnExpr);
    } else {
        auto valTy = typeManager.GetFunctionTy({re.enclosing.value()->commandResultTy}, resultDeclTy);

        std::vector<OwnedPtr<FuncArg>> args;
        if (re.withExpr) {
            (void)args.emplace_back(CreateFuncArg(std::move(re.withExpr)));
        } else {
            (void)args.emplace_back(
                CreateFuncArg(AST::CreateUnitExpr(typeManager.GetPrimitiveTy(TypeKind::TYPE_UNIT))));
        }

        auto wrappedValue = CreateCallExpr(CreateRefValue(*valTy), std::move(args));
        ChkCallExpr(ctx, resultDeclTy, *wrappedValue);
        // Checking for a call expr may get rid of some desugarings
        DesugarForPropDecl(*wrappedValue);

        auto returnExpr = MakeOwnedNode<ReturnExpr>();
        returnExpr->expr = std::move(wrappedValue);
        desugared = std::move(returnExpr);
    }
    desugared->ty = TypeManager::GetNothingTy();
    re.desugarExpr = std::move(desugared);
}
