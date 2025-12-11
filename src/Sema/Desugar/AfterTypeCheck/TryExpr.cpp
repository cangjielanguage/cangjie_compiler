// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "TypeCheckerImpl.h"

#include "Desugar/AfterTypeCheck.h"
#include "JoinAndMeet.h"
#include "TypeCheckUtil.h"

#include "cangjie/AST/Create.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Sema/TypeManager.h"
#include "cangjie/Utils/CheckUtils.h"

using namespace Cangjie;
using namespace AST;
using namespace TypeCheckUtil;
using namespace Sema::Desugar::AfterTypeCheck;

namespace {
// Create `Some<Exception>`.
OwnedPtr<RefExpr> CreateRefSome(FuncTy& funcTy)
{
    CJC_ASSERT(Ty::IsTyCorrect(funcTy.retTy) && funcTy.retTy->IsEnum());
    EnumTy& enumTy = StaticCast<EnumTy&>(*funcTy.retTy);
    auto decl = LookupEnumMember(enumTy.declPtr, OPTION_VALUE_CTOR);
    CJC_NULLPTR_CHECK(decl);
    auto refExpr = CreateRefExpr(*decl);
    (void)refExpr->ref.targets.emplace_back(decl); // for `GetFuncTargets`
    refExpr->ty = &funcTy;
    refExpr->instTys = enumTy.typeArgs;
    return refExpr;
}

// Create `None<Exception>`.
OwnedPtr<RefExpr> CreateRefNone(EnumTy& enumTy)
{
    auto decl = LookupEnumMember(enumTy.declPtr, OPTION_NONE_CTOR);
    CJC_NULLPTR_CHECK(decl);
    auto refExpr = CreateRefExpr(*decl);
    refExpr->ty = &enumTy;
    refExpr->instTys = enumTy.typeArgs;
    return refExpr;
}

// Create `throw v`.
OwnedPtr<ThrowExpr> CreateThrowExpr(const VarPattern& vp)
{
    auto ret = MakeOwnedNode<ThrowExpr>();
    ret->expr = CreateRefExpr(*vp.varDecl);
    return ret;
}

/** Create the following match expression.
 *       match (freshExc) {
 *           case Some(v) => throw v
 *           case None => noneBody
 *       }
 */
OwnedPtr<MatchExpr> CreateMatchExpr(FuncTy& someTy, VarDecl& freshExc, OwnedPtr<Expr> noneBody)
{
    // `case Some(v) => throw v`
    CJC_ASSERT(Ty::IsTyCorrect(freshExc.ty) && freshExc.ty->IsEnum() && freshExc.ty->typeArgs.size() == 1);
    auto vp = CreateVarPattern(V_COMPILER, freshExc.ty->typeArgs.front());
    auto throwExpr = CreateThrowExpr(*vp);
    auto somePattern = MakeOwnedNode<EnumPattern>();
    somePattern->constructor = CreateRefSome(someTy);
    (void)somePattern->patterns.emplace_back(std::move(vp));
    auto caseSome = CreateMatchCase(std::move(somePattern), std::move(throwExpr));
    // `case None => noneBody`
    auto nonePattern = MakeOwnedNode<EnumPattern>();
    nonePattern->constructor = CreateRefNone(StaticCast<EnumTy&>(*freshExc.ty));
    auto caseNone = CreateMatchCase(std::move(nonePattern), std::move(noneBody));
    // `match`
    std::vector<OwnedPtr<MatchCase>> matchCases;
    (void)matchCases.emplace_back(std::move(caseSome));
    (void)matchCases.emplace_back(std::move(caseNone));
    return CreateMatchExpr(CreateRefExpr(freshExc), std::move(matchCases), nullptr);
}

// Create `x.isClosed()` or `x.close()`.
OwnedPtr<CallExpr> CreateCallExpr(VarDecl& vd, Decl& func)
{
    auto baseFunc = CreateMemberAccess(CreateRefExpr(vd), func);
    if (baseFunc->target != nullptr && baseFunc->target->astKind == ASTKind::FUNC_DECL) {
        (void)baseFunc->targets.emplace_back(StaticCast<FuncDecl*>(baseFunc->target)); // for `GetFuncTargets`
    }
    CopyBasicInfo(vd.initializer.get(), baseFunc->baseExpr.get());
    CopyBasicInfo(vd.initializer.get(), baseFunc.get());
    auto callExpr = CreateCallExpr(std::move(baseFunc), {});
    CopyBasicInfo(&vd, callExpr.get());
    return callExpr;
}

} // namespace

/** Create the following try-catch expression.
 *      try {
 *          if (!x.isClosed()) {
 *              x.close()
 *          }
 *      } catch (v: Exeption) {
 *          match (freshExc) {
 *              case Some(v) => throw v
 *              case None => throw v
 *          }
 *      }
 */
OwnedPtr<TryExpr> TypeChecker::TypeCheckerImpl::CreateTryInFinally(
    ASTContext& ctx, ClassDecl& exceptionDecl, FuncTy& someTy, VarDecl& x, VarDecl& freshExc)
{
    CJC_ASSERT(Ty::IsTyCorrect(freshExc.ty) && freshExc.ty->typeArgs.size() == 1);
    auto vp = CreateVarPattern(V_COMPILER, freshExc.ty->typeArgs.front());
    // `if`
    std::vector<OwnedPtr<Node>> thenBlockNodes;

    auto findFunc = [this, &ctx, &x](const std::string& fieldName) -> Ptr<Decl> {
        LookupInfo info{x.ty, x.curFile, true, true, false};
        std::vector<Ptr<Decl>> decls = FieldLookup(ctx, Ty::GetDeclPtrOfTy(x.ty), fieldName, info);
        Ptr<Decl> func = nullptr;
        for (auto& decl : decls) {
            if (auto funcDecl = DynamicCast<FuncDecl*>(decl); funcDecl && funcDecl->funcBody &&
                !funcDecl->funcBody->paramLists.empty() && funcDecl->funcBody->paramLists.front()->params.size() == 0) {
                func = decl;
                break;
            }
        }
        // The error should have been reported in type checking phase.
        CJC_NULLPTR_CHECK(func);
        return func;
    };

    (void)thenBlockNodes.emplace_back(CreateCallExpr(x, *findFunc("close")));

    auto ifExpr = CreateIfExpr(CreateUnaryExpr(CreateCallExpr(x, *findFunc("isClosed")), TokenKind::NOT),
        CreateBlock(std::move(thenBlockNodes)));
    // `match`
    auto matchExp = CreateMatchExpr(someTy, freshExc, CreateThrowExpr(*vp));
    // `v: Exception`
    auto exceptTypePattern = MakeOwnedNode<ExceptTypePattern>();
    exceptTypePattern->pattern = std::move(vp);
    (void)exceptTypePattern->types.emplace_back(CreateRefType(exceptionDecl));
    // `try-catch`
    std::vector<OwnedPtr<Node>> tryBlockNodes;
    (void)tryBlockNodes.emplace_back(std::move(ifExpr));
    std::vector<OwnedPtr<Node>> catchBlockNodes;
    (void)catchBlockNodes.emplace_back(std::move(matchExp));
    auto tryExpr = MakeOwnedNode<TryExpr>();
    tryExpr->tryBlock = CreateBlock(std::move(tryBlockNodes));
    (void)tryExpr->catchPatterns.emplace_back(std::move(exceptTypePattern));
    (void)tryExpr->catchBlocks.emplace_back(CreateBlock(std::move(catchBlockNodes)));
    return tryExpr;
}

/** Create the following try-catch-finally expression.
 *       try {
 *           tryBlock
 *       } catch (v: Exception) {
 *           freshExc = Some(v)
 *       } finally {
 *           try {
 *               if (!x.isClosed()) {
 *                   x.close()
 *               }
 *           } catch (v: Exception) {
 *               match (freshExc) {
 *                   case Some(v) => throw v
 *                   case None => throw v
 *               }
 *           }
 *           match (freshExc) {
 *               case Some(v) => throw v
 *               case None => ()
 *           }
 *       }
 */
OwnedPtr<TryExpr> TypeChecker::TypeCheckerImpl::CreateTryCatchFinally(
    ASTContext& ctx, ClassDecl& exceptionDecl, FuncTy& someTy, VarDecl& x, VarDecl& freshExc, OwnedPtr<Block> tryBlock)
{
    CJC_ASSERT(Ty::IsTyCorrect(freshExc.ty) && freshExc.ty->IsEnum() && freshExc.ty->typeArgs.size() == 1);
    auto vp = CreateVarPattern(V_COMPILER, freshExc.ty->typeArgs.front());
    std::vector<OwnedPtr<FuncArg>> args;
    (void)args.emplace_back(CreateFuncArg(CreateRefExpr(*vp->varDecl)));
    auto assignExpr = CreateAssignExpr(CreateRefExpr(freshExc), CreateCallExpr(CreateRefSome(someTy), std::move(args)));
    auto exceptTypePattern = MakeOwnedNode<ExceptTypePattern>();
    exceptTypePattern->pattern = std::move(vp);
    (void)exceptTypePattern->types.emplace_back(CreateRefType(exceptionDecl));
    // `try`
    std::vector<OwnedPtr<Node>> catchBlockNodes;
    (void)catchBlockNodes.emplace_back(std::move(assignExpr));
    std::vector<OwnedPtr<Node>> finallyBlockNodes;
    (void)finallyBlockNodes.emplace_back(CreateTryInFinally(ctx, exceptionDecl, someTy, x, freshExc));
    (void)finallyBlockNodes.emplace_back(CreateMatchExpr(someTy, freshExc, CreateUnitExpr()));
    auto tryExpr = MakeOwnedNode<TryExpr>();
    CopyBasicInfo(tryBlock.get(), tryExpr.get());
    tryExpr->tryBlock = std::move(tryBlock);
    (void)tryExpr->catchPatterns.emplace_back(std::move(exceptTypePattern));
    (void)tryExpr->catchBlocks.emplace_back(CreateBlock(std::move(catchBlockNodes)));
    tryExpr->finallyBlock = CreateBlock(std::move(finallyBlockNodes));
    return tryExpr;
}

// Create the outermost try-block.
OwnedPtr<Block> TypeChecker::TypeCheckerImpl::CreateOuterTryBlock(ASTContext& ctx, ClassDecl& exceptionDecl,
    FuncTy& someTy, EnumTy& noneTy, std::vector<OwnedPtr<VarDecl>>& resourceSpec, OwnedPtr<Block> block)
{
    auto tryBlock = std::move(block);
    for (auto iter = resourceSpec.rbegin(); iter != resourceSpec.rend(); ++iter) {
        auto noneRef = CreateRefNone(noneTy);
        auto freshExc = CreateVarDecl(RESOURCE_NAME, std::move(noneRef));
        freshExc->isVar = true;
        auto x = std::move(*iter);
        auto tryCatchFinally = CreateTryCatchFinally(ctx, exceptionDecl, someTy, *x, *freshExc, std::move(tryBlock));
        std::vector<OwnedPtr<Node>> tryBlockNodes;
        (void)tryBlockNodes.emplace_back(std::move(freshExc));
        (void)tryBlockNodes.emplace_back(std::move(x));
        (void)tryBlockNodes.emplace_back(std::move(tryCatchFinally));
        tryBlock = CreateBlock(std::move(tryBlockNodes));
    }
    resourceSpec.clear();
    return tryBlock;
}

/**
 * *************** before desugar ***************
 * try (x = A()) {
 *     println(x)
 * } catch (_: Exception) {
 *     println("catch exception")
 * } finally {
 *     println("finally")
 * }
 * *************** after desugar ****************
 * try {
 *     var freshExc = None<Exception>
 *     let x = A()
 *     try {
 *         println(x)
 *     } catch (v: Exception) {
 *         freshExc = Some(v)
 *     } finally {
 *         try {
 *             if (!x.isClosed()) {
 *                 x.close()
 *             }
 *         } catch (v: Exception) {
 *             match (freshExc) {
 *                 case Some(v) => throw v
 *                 case None => throw v
 *             }
 *         }
 *         match (freshExc) {
 *             case Some(v) => throw v
 *             case None => ()
 *         }
 *     }
 * } catch (_: Exception) {
 *     println("catch exception")
 * } finally {
 *     println("finally")
 * }
 */
void TypeChecker::TypeCheckerImpl::DesugarTryWithResourcesExpr(ASTContext& ctx, TryExpr& te)
{
    if (te.resourceSpec.empty() || !Ty::IsTyCorrect(te.ty)) {
        return;
    }
    auto exceptionDecl = importManager.GetCoreDecl<ClassDecl>(CLASS_EXCEPTION);
    CJC_NULLPTR_CHECK(exceptionDecl);
    auto optionDecl = StaticCast<EnumDecl*>(importManager.GetCoreDecl("Option"));
    CJC_NULLPTR_CHECK(optionDecl);
    auto noneTy = typeManager.GetEnumTy(*optionDecl, {exceptionDecl->ty});
    CJC_NULLPTR_CHECK(noneTy);
    auto someTy = typeManager.GetFunctionTy({exceptionDecl->ty}, noneTy);
    auto unitTy = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
    // try-with-resources expression is of type Unit.
    if (!typeManager.IsSubtype(te.tryBlock->ty, unitTy)) {
        (void)te.tryBlock->body.emplace_back(CreateUnitExpr(unitTy));
        te.tryBlock->ty = unitTy;
    }
    auto tryBlock = CreateOuterTryBlock(ctx, *exceptionDecl, *someTy, *noneTy, te.resourceSpec, std::move(te.tryBlock));
    // Only try-block needs `Synthesize`. `catch` and `finally` were checked by `SynTryWithResourcesExpr`.
    // And they will be moved to the desugar try expression.
    SynthesizeWithoutRecover(ctx, tryBlock.get());
    CJC_ASSERT(Ty::IsTyCorrect(tryBlock->ty) && tryBlock->ty->IsUnit());
    for (auto& block : te.catchBlocks) {
        CJC_NULLPTR_CHECK(block);
        if (!typeManager.IsSubtype(block->ty, unitTy)) {
            (void)block->body.emplace_back(CreateUnitExpr(unitTy));
            block->ty = unitTy;
        }
    }
    auto emptyCatchAndFinally = te.catchBlocks.empty() && te.finallyBlock == nullptr;
    auto finallyBlock = CreateBlock(std::vector<OwnedPtr<Node>>{}, TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT));
    auto tryExpr = MakeOwnedNode<TryExpr>();
    tryExpr->tryBlock = std::move(tryBlock);
    tryExpr->catchPatterns = std::move(te.catchPatterns);
    tryExpr->catchBlocks = std::move(te.catchBlocks);
    tryExpr->finallyBlock = emptyCatchAndFinally ? std::move(finallyBlock) : std::move(te.finallyBlock);
    tryExpr->ty = unitTy;
    CopyBasicInfo(&te, tryExpr.get());
    AddCurFile(*tryExpr, te.curFile);
    te.desugarExpr = std::move(tryExpr);
}
