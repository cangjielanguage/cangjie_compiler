// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "Desugar/AfterTypeCheck.h"

#include "cangjie/Driver/StdlibMap.h"

#include "TypeCheckUtil.h"
#include "TypeCheckerImpl.h"

#include "cangjie/AST/Create.h"
#include "cangjie/AST/Utils.h"

using namespace Cangjie;
using namespace AST;
using namespace TypeCheckUtil;
using namespace Sema::Desugar::AfterTypeCheck;

namespace {
OwnedPtr<VarDecl> CreateIndexVar(const ForInExpr& forInExpr, const RangeExpr& rangeExpr)
{
    auto index = MakeOwnedNode<VarDecl>();
    index->EnableAttr(Attribute::IMPLICIT_ADD);
    index->ty = rangeExpr.startExpr->ty;
    index->initializer = ASTCloner::Clone(rangeExpr.startExpr.get());
    if (auto vp = DynamicCast<VarPattern*>(forInExpr.pattern.get()); vp) {
        // var pattern
        index->identifier = "$iter-" + vp->varDecl->identifier;
        index->isVar = true;
        index->assignPos = index->begin;
        index->curFile = forInExpr.curFile;
    } else {
        // wildcard pattern
        index->identifier = "$index-compiler";
        index->isVar = true;
        index->assignPos = forInExpr.pattern->begin;
        index->curFile = forInExpr.curFile;
    }
    index->begin = forInExpr.pattern->begin;
    index->end = forInExpr.pattern->end;
    return index;
}

OwnedPtr<RefExpr> GetVarRef(VarDecl& var, const Node& pos)
{
    auto r = CreateRefExpr(var.identifier);
    r->curFile = var.curFile;
    r->ref.target = &var;
    r->ty = var.ty;
    r->begin = pos.begin;
    r->end = pos.end;
    return r;
}

OwnedPtr<RefExpr> GetVarRef(VarDecl& var)
{
    auto r = CreateRefExpr(var.identifier);
    r->curFile = var.curFile;
    r->ref.target = &var;
    r->ty = var.ty;
    return r;
}

// 'curFile' value will be added at the end of all ast node creation.
OwnedPtr<VarDecl> CreateStopVar(const RangeExpr& rangeExpr)
{
    auto rangeStop = CreateVarDecl("$stop-compiler", ASTCloner::Clone(rangeExpr.stopExpr.get()));
    rangeStop->begin = rangeExpr.stopExpr->begin;
    rangeStop->end = rangeExpr.stopExpr->end;
    return rangeStop;
}

OwnedPtr<VarDecl> CreateFirstFlag(const RangeExpr& rangeExpr)
{
    auto boolTy = TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN);
    auto firstFlag = CreateVarDecl("$isFirst-compiler", CreateLitConstExpr(LitConstKind::BOOL, "true", boolTy));
    firstFlag->isVar = true;
    firstFlag->begin = rangeExpr.begin;
    firstFlag->end = rangeExpr.end;
    return firstFlag;
}

OwnedPtr<BinaryExpr> CreateRangeCond(
    const ForInExpr& forInExpr, VarDecl& index, VarDecl& rangeStop, const TokenKind& opToken)
{
    auto rangeCond = CreateBinaryExpr(GetVarRef(index, rangeStop), GetVarRef(rangeStop, rangeStop), opToken);
    rangeCond->curFile = forInExpr.curFile;
    rangeCond->ty = TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN);
    rangeCond->begin = forInExpr.inExpression->begin;
    rangeCond->end = forInExpr.inExpression->end;
    return rangeCond;
}

OwnedPtr<IfExpr> CreateGuard(ForInExpr& forInExpr, BinaryExpr& rangeCond, const bool isClose)
{
    auto condBody = MakeOwnedNode<IfExpr>();
    condBody->sugarKind = Expr::SugarKind::FOR_IN_EXPR;
    condBody->curFile = forInExpr.curFile;
    condBody->begin = rangeCond.begin;
    condBody->end = rangeCond.end;
    if (forInExpr.patternGuard) {
        if (isClose) {
            auto andCond =
                CreateBinaryExpr(ASTCloner::Clone(Ptr(&rangeCond)), std::move(forInExpr.patternGuard), TokenKind::AND);
            andCond->curFile = forInExpr.curFile;
            andCond->ty = TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN);
            andCond->begin = andCond->rightExpr->begin;
            andCond->end = andCond->rightExpr->end;
            condBody->condExpr = std::move(andCond);
        } else {
            condBody->condExpr = std::move(forInExpr.patternGuard);
        }
    } else {
        if (isClose) {
            condBody->condExpr = ASTCloner::Clone(Ptr(&rangeCond));
        } else {
            condBody->condExpr = MakeOwned<AST::LitConstExpr>(LitConstKind::BOOL, "true");
            condBody->condExpr->ty = TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN);
            condBody->condExpr->begin = rangeCond.begin;
            condBody->condExpr->end = rangeCond.end;
            condBody->condExpr->EnableAttr(Attribute::COMPILER_ADD);
        }
    }
    condBody->thenBody = std::move(forInExpr.body);
    condBody->ty = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
    condBody->EnableAttr(Attribute::IMPLICIT_ADD);
    return condBody;
}

OwnedPtr<AssignExpr> CreateUpdate(const ForInExpr& forInExpr, RangeExpr& rangeExpr, VarDecl& index)
{
    auto update = MakeOwnedNode<AST::AssignExpr>();
    update->curFile = forInExpr.curFile;
    update->leftValue = GetVarRef(index, rangeExpr);
    if (rangeExpr.stepExpr) {
        update->rightExpr = std::move(rangeExpr.stepExpr);
        CJC_ASSERT(update->rightExpr->ty);
    } else {
        update->rightExpr = MakeOwned<AST::LitConstExpr>(LitConstKind::INTEGER, "1");
        update->rightExpr->ty = index.ty;
    }
    update->begin = rangeExpr.begin;
    update->end = rangeExpr.end;
    update->op = TokenKind::ADD_ASSIGN;
    update->isCompound = true;
    update->leftValue->ty = index.ty;
    return update;
}

OwnedPtr<IfExpr> CreateFirstIf(const ForInExpr& forInExpr, VarDecl& firstFlag, OwnedPtr<AssignExpr>&& update)
{
    auto firstRun = MakeOwnedNode<IfExpr>();
    firstRun->hasElse = true;
    firstRun->curFile = forInExpr.curFile;
    
    firstRun->condExpr = GetVarRef(firstFlag, *forInExpr.inExpression);
    constexpr int inLen{2};
    firstRun->condExpr->begin = firstRun->begin = forInExpr.inPos;
    firstRun->condExpr->end = firstRun->end = forInExpr.inPos + inLen;
    firstRun->thenBody = MakeOwnedNode<Block>();
    firstRun->thenBody->ty = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
    firstRun->thenBody->begin = forInExpr.body->begin;
    firstRun->thenBody->end = forInExpr.body->end;
    // first = false
    auto assignFalse = MakeOwnedNode<AST::AssignExpr>();
    assignFalse->op = TokenKind::ASSIGN;
    assignFalse->curFile = forInExpr.curFile;
    assignFalse->leftValue = GetVarRef(firstFlag, *forInExpr.inExpression);
    assignFalse->leftValue->ty = TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN);
    assignFalse->rightExpr = MakeOwned<AST::LitConstExpr>(LitConstKind::BOOL, "false");
    assignFalse->rightExpr->curFile = forInExpr.curFile;
    assignFalse->rightExpr->ty = TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN);
    assignFalse->ty = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
    assignFalse->begin = forInExpr.inPos;
    assignFalse->end = forInExpr.inPos + inLen;
    firstRun->thenBody->body.push_back(std::move(assignFalse));

    auto elseBody = MakeOwnedNode<Block>();
    elseBody->body.push_back(std::move(update));
    elseBody->ty = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
    firstRun->begin = elseBody->begin = forInExpr.body->begin;
    firstRun->end = elseBody->end = forInExpr.body->end;
    firstRun->elseBody = std::move(elseBody);

    firstRun->ty = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
    return firstRun;
}

OwnedPtr<VarDecl> CreateLetIter(ASTContext& ctx, VarPattern& vp, ForInExpr& forInExpr, VarDecl& index)
{
    auto letValue = MakeOwnedNode<VarDecl>();
    // To: let i = iter-i
    letValue = std::move(vp.varDecl);
    letValue->parentPattern = nullptr;
    auto initRef = GetVarRef(index, *letValue);
    letValue->initializer = std::move(initRef);
    ctx.DeleteInvertedIndexes(forInExpr.pattern.get());
    forInExpr.pattern = nullptr;
    return letValue;
}

OwnedPtr<MatchCase> CreateNoneCaseForForInIter(Ptr<Decl> optionDecl, Expr& refLoop, Ptr<Ty> caseTy)
{
    auto caseNone = MakeOwnedNode<MatchCase>();
    auto nonePattern = MakeOwnedNode<EnumPattern>();
    nonePattern->ty = caseTy;
    auto noneRef = MakeOwnedNode<RefExpr>();
    noneRef->ref.identifier = OPTION_NONE_CTOR;
    noneRef->ref.target = LookupEnumMember(optionDecl, OPTION_NONE_CTOR);
    noneRef->ty = nonePattern->ty;
    noneRef->instTys = nonePattern->ty->typeArgs;
    auto breakExpr = MakeOwnedNode<JumpExpr>();
    breakExpr->isBreak = true;
    breakExpr->refLoop = &refLoop;
    breakExpr->ty = TypeManager::GetNothingTy();
    nonePattern->constructor = std::move(noneRef);
    caseNone->patterns.emplace_back(std::move(nonePattern));
    caseNone->exprOrDecls = MakeOwnedNode<Block>();
    caseNone->exprOrDecls->body.push_back(std::move(breakExpr));
    caseNone->exprOrDecls->ty = TypeManager::GetNothingTy();
    caseNone->ty = TypeManager::GetNothingTy();
    return caseNone;
}

OwnedPtr<MatchCase> CreateSomeCaseForForInIter(
    Ptr<Decl> optionDecl, Expr& refLoop, Ptr<FuncTy> someRefTy, ForInExpr& forInExpr)
{
    Ptr<Ty> patternTy = forInExpr.pattern->ty;
    CJC_NULLPTR_CHECK(patternTy);
    // To : case Some(v)
    auto caseSome = MakeOwnedNode<MatchCase>();
    auto somePattern = MakeOwnedNode<EnumPattern>();
    somePattern->ty = someRefTy->retTy;
    auto someRef = MakeOwnedNode<RefExpr>();
    someRef->ref.identifier = OPTION_VALUE_CTOR;
    someRef->ref.target = LookupEnumMember(optionDecl, OPTION_VALUE_CTOR);
    someRef->ty = someRefTy;
    somePattern->constructor = std::move(someRef);
    auto vPattern = CreateVarPattern(V_COMPILER, patternTy);
    auto vDecl = vPattern->varDecl.get();
    somePattern->patterns.emplace_back(std::move(vPattern));
    caseSome->patterns.emplace_back(std::move(somePattern));

    // To : match v { case pat if e2 => b; case _ => continue}
    auto insideMatch = MakeOwnedNode<MatchExpr>();
    insideMatch->matchMode = true;
    insideMatch->selector = CreateRefExpr(*vDecl);

    // To : case pat if e2 => b
    auto casePat = MakeOwnedNode<MatchCase>();
    casePat->patterns.emplace_back(std::move(forInExpr.pattern));
    forInExpr.patternInDesugarExpr = casePat->patterns.front().get();
    casePat->patternGuard = std::move(forInExpr.patternGuard);
    casePat->exprOrDecls = std::move(forInExpr.body);
    casePat->exprOrDecls->begin.Mark(PositionStatus::IGNORE);
    casePat->exprOrDecls->end.Mark(PositionStatus::IGNORE);
    RearrangeRefLoop(forInExpr, refLoop, casePat->exprOrDecls.get());
    insideMatch->ty = casePat->exprOrDecls->ty;
    casePat->ty = casePat->exprOrDecls->ty;
    insideMatch->matchCases.push_back(std::move(casePat));

    // To : case _ => continue
    auto wildCase = MakeOwnedNode<MatchCase>();
    auto wildcard = MakeOwnedNode<WildcardPattern>();
    wildcard->ty = patternTy;
    wildCase->patterns.emplace_back(std::move(wildcard));
    auto continueExpr = MakeOwnedNode<JumpExpr>();
    continueExpr->refLoop = &refLoop;
    continueExpr->isBreak = false;
    continueExpr->ty = TypeManager::GetNothingTy();
    wildCase->exprOrDecls = MakeOwnedNode<Block>();
    wildCase->exprOrDecls->body.push_back(std::move(continueExpr));
    wildCase->exprOrDecls->ty = TypeManager::GetNothingTy();
    wildCase->ty = TypeManager::GetNothingTy();
    insideMatch->matchCases.push_back(std::move(wildCase));

    caseSome->exprOrDecls = MakeOwnedNode<Block>();
    caseSome->exprOrDecls->body.push_back(std::move(insideMatch));
    caseSome->exprOrDecls->ty = TypeManager::GetNothingTy();
    caseSome->ty = TypeManager::GetNothingTy();
    return caseSome;
}
} // namespace

/**
 * Desugar ForInExpr of close range to while expr. for example:
 * *************** before desugar ****************
 *  for (int in range: step where guard) {
 *     body
 *  }
 * *************** after desugar ****************
 *  var iter = range.start
 *  let end: T = range.end
 *  var first: Bool = true
 *  while (iter < end || first) {
 *      if (first) {
 *          first = false
 *      } else {
 *          iter += step // step defaults to 1
 *          // note that a non-constant step will result desugaring to ForInIter
 *      }
 *      let i = iter // For user declared 'i', which should be immutable if been captured.
 *      if (iter <= end && guard) {
 *          body
 *      }
 *  }
 *  ************** please note the following case:
 *  for (i in 0..10) {
 *      print(i.toString())
 *      continue
 *  }
 */
void TypeChecker::TypeCheckerImpl::DesugarForInCloseRange(ASTContext& ctx, AST::ForInExpr& forInExpr)
{
    CJC_ASSERT(forInExpr.inExpression->astKind == ASTKind::RANGE_EXPR);
    auto rangeExpr = StaticCast<RangeExpr>(forInExpr.inExpression.get());
    // To : var iter-i = range.start
    auto index = CreateIndexVar(forInExpr, *rangeExpr);
    // To: let end = range.stop
    auto rangeStop = CreateStopVar(*rangeExpr);
    // To : var first: Bool = true
    auto firstFlag = CreateFirstFlag(*rangeExpr);
    bool increasing = !rangeExpr->stepExpr || !rangeExpr->stepExpr->constNumValue.asInt.IsNegativeNum();
    auto whileExpr = MakeOwnedNode<WhileExpr>();
    whileExpr->sugarKind = Expr::SugarKind::FOR_IN_EXPR;
    CopyBasicInfo(&forInExpr, whileExpr.get());

    // To : while (i < end || first)
    auto firstFlagRef = GetVarRef(*firstFlag, *rangeStop);
    // Notice: need use non-close compare here.
    auto rangeCondExpr = CreateRangeCond(forInExpr, *index, *rangeStop, increasing ? TokenKind::LT : TokenKind::GT);
    rangeCondExpr->begin = firstFlagRef->begin = rangeExpr->begin;
    rangeCondExpr->end = firstFlagRef->end = rangeExpr->end;
    whileExpr->condExpr = CreateBinaryExpr(
        std::move(rangeCondExpr), std::move(firstFlagRef), TokenKind::OR);
    CopyBasicInfo(&forInExpr, whileExpr->condExpr.get());

    whileExpr->body = MakeOwnedNode<Block>();
    CopyBasicInfo(&forInExpr, whileExpr->body.get());
    // For update the iter 'iter += step'.
    auto update = CreateUpdate(forInExpr, *rangeExpr, *index);
    AddCurFile(*update, forInExpr.curFile);
    Ptr<Ty> updateTy = SynthesizeWithoutRecover(ctx, update.get()); // Need syn to desugar.
    CJC_ASSERT(updateTy && updateTy->kind != TypeKind::TYPE_INVALID);
    // Create first if-else part in while body.
    auto firstRun = CreateFirstIf(forInExpr, *firstFlag, std::move(update));
    whileExpr->body->body.push_back(std::move(firstRun));
    // For 'let i = iter'.
    if (auto vp = DynamicCast<VarPattern*>(forInExpr.pattern.get()); vp) {
        auto letValue = CreateLetIter(ctx, *vp, forInExpr, *index);
        // Put let value declaration between.
        whileExpr->body->body.push_back(std::move(letValue));
    }
    // To: if (iter <= end && guard) { body }
    auto rangeCond = CreateRangeCond(forInExpr, *index, *rangeStop, increasing ? TokenKind::LE : TokenKind::GE);
    auto condBody = CreateGuard(forInExpr, *rangeCond, true);
    whileExpr->body->body.push_back(std::move(condBody));
    whileExpr->ty = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
    whileExpr->body->ty = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
    auto refWhile = whileExpr.get();

    auto blockExpr = MakeOwnedNode<Block>();
    CopyBasicInfo(&forInExpr, blockExpr.get());
    blockExpr->body.push_back(std::move(index));
    blockExpr->body.push_back(std::move(rangeStop));
    blockExpr->body.push_back(std::move(firstFlag));
    blockExpr->begin = whileExpr->begin;
    blockExpr->end = whileExpr->end;
    blockExpr->body.push_back(std::move(whileExpr));
    AddCurFile(*blockExpr, forInExpr.curFile);
    Ptr<Ty> blockExprTy = SynthesizeWithoutRecover(ctx, blockExpr.get());
    CJC_ASSERT(blockExprTy && blockExprTy->kind != TypeKind::TYPE_INVALID);
    /* must do after synthesize */
    RearrangeRefLoop(forInExpr, *refWhile, refWhile->body.get());
    // Add file for all created ast.
    forInExpr.desugarExpr = std::move(blockExpr);

    ctx.DeleteInvertedIndexes(forInExpr.inExpression.get());
    forInExpr.inExpression = nullptr;
}

/**
 * Desugar ForInExpr of non-close range to while expr. for example:
 * *************** before desugar ****************
 *  for (int in range: step where guard) {
 *     body
 *  }
 * *************** after desugar ****************
 *  var iter = range.start
 *  let end: T = range.end
 *  while (iter < end) {
 *      let i = iter // For user declared 'i', which should be immutable if been captured.
 *      iter += step
 *      if (guard) {
 *          body
 *      }
 *  }
 *  ************** please note the following case:
 *  for (i in 0..10) {
 *      print(i.toString())
 *      continue
 *  }
 */
void TypeChecker::TypeCheckerImpl::DesugarForInNonCloseRange(ASTContext& ctx, AST::ForInExpr& forInExpr)
{
    CJC_ASSERT(forInExpr.inExpression->astKind == ASTKind::RANGE_EXPR);
    auto rangeExpr = StaticCast<RangeExpr>(forInExpr.inExpression.get());
    // To : var iter-i = range.start
    auto index = CreateIndexVar(forInExpr, *rangeExpr);
    // To: let end = range.stop
    auto rangeStop = CreateStopVar(*rangeExpr);
    // To : where (i < end)
    bool increasing = !rangeExpr->stepExpr || !rangeExpr->stepExpr->constNumValue.asInt.IsNegativeNum();
    auto rangeCond = CreateRangeCond(forInExpr, *index, *rangeStop, increasing ? TokenKind::LT : TokenKind::GT);
    // To: if (guard) { body }
    auto condBody = CreateGuard(forInExpr, *rangeCond, false);

    auto whileExpr = MakeOwnedNode<WhileExpr>();
    whileExpr->sugarKind = Expr::SugarKind::FOR_IN_EXPR;
    CopyBasicInfo(&forInExpr, whileExpr.get());
    whileExpr->condExpr = std::move(rangeCond);
    whileExpr->body = MakeOwnedNode<Block>();
    CopyBasicInfo(&forInExpr, whileExpr->body.get());
    // To : 'let i = iter'
    if (auto vp = DynamicCast<VarPattern*>(forInExpr.pattern.get()); vp) {
        auto letValue = CreateLetIter(ctx, *vp, forInExpr, *index);
        // Put let value declaration between.
        whileExpr->body->body.push_back(std::move(letValue));
    }
    // To: 'iter += step'
    auto update = CreateUpdate(forInExpr, *rangeExpr, *index);
    AddCurFile(*update, forInExpr.curFile);
    Ptr<Ty> updateTy = SynthesizeWithoutRecover(ctx, update.get());
    CJC_ASSERT(updateTy && updateTy->kind != TypeKind::TYPE_INVALID);
    whileExpr->body->body.push_back(std::move(update));
    CopyBasicInfo(condBody->thenBody.get(), whileExpr->body.get());
    whileExpr->body->body.push_back(std::move(condBody));
    whileExpr->ty = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
    whileExpr->body->ty = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
    auto refWhile = whileExpr.get();

    auto blockExpr = MakeOwnedNode<Block>();
    CopyBasicInfo(&forInExpr, blockExpr.get());
    blockExpr->body.push_back(std::move(index));
    blockExpr->body.push_back(std::move(rangeStop));
    blockExpr->body.push_back(std::move(whileExpr));
    AddCurFile(*blockExpr, forInExpr.curFile);
    Ptr<Ty> blockExprTy = SynthesizeWithoutRecover(ctx, blockExpr.get());
    /* must do after synthesize */
    RearrangeRefLoop(forInExpr, *refWhile, refWhile->body.get());
    CJC_ASSERT(blockExprTy && blockExprTy->kind != TypeKind::TYPE_INVALID);
    // Add file for all created ast.
    forInExpr.desugarExpr = std::move(blockExpr);

    ctx.DeleteInvertedIndexes(forInExpr.inExpression.get());
    forInExpr.inExpression = nullptr;
}

void TypeChecker::TypeCheckerImpl::ReArrangeForInRangeExpr(ASTContext& ctx, ForInExpr& forInExpr)
{
    CJC_ASSERT(forInExpr.inExpression->astKind == ASTKind::RANGE_EXPR);
    auto re = DynamicCast<RangeExpr*>(forInExpr.inExpression.get());
    auto rangeExpr = StaticCast<RangeExpr>(forInExpr.inExpression.get());
    // To : var iter-i = range.start
    auto index = CreateIndexVar(forInExpr, *rangeExpr);
    // To: let end = range.stop
    auto rangeStop = CreateStopVar(*rangeExpr);
    // To : where (i < end)
    bool increasing = !rangeExpr->stepExpr || !rangeExpr->stepExpr->constNumValue.asInt.IsNegativeNum();
    auto rangeCond = CreateRangeCond(forInExpr, *index, *rangeStop,
        increasing ? (re->isClosed ? TokenKind::LE : TokenKind::LT) : (re->isClosed ? TokenKind::GE : TokenKind::GT));
    // To : 'let i = iter'
    if (auto vp = DynamicCast<VarPattern*>(forInExpr.pattern.get()); vp) {
        auto letValue = MakeOwnedNode<VarDecl>();
        CopyBasicInfo(forInExpr.pattern, letValue.get());
        AddCurFile(*letValue, forInExpr.curFile);
        // To: let i = iter-i
        letValue = std::move(vp->varDecl);
        letValue->parentPattern = nullptr;
        auto initRef = GetVarRef(*index, *index);
        initRef->begin = index->begin;
        initRef->end = index->end;
        letValue->initializer = std::move(initRef);
        vp->varDecl = std::move(letValue);
    }
    auto update = CreateUpdate(forInExpr, *rangeExpr, *index);
    CopyBasicInfo(forInExpr.inExpression, update.get());
    AddCurFile(*update, forInExpr.curFile);
    Ptr<Ty> updateTy = SynthesizeWithoutRecover(ctx, update.get());
    CJC_ASSERT(updateTy && updateTy->kind != TypeKind::TYPE_INVALID);
    // composite block structure
    auto blockExpr = MakeOwnedNode<Block>();
    CopyBasicInfo(forInExpr.inExpression, blockExpr.get());
    AddCurFile(*blockExpr, forInExpr.curFile);
    blockExpr->body.push_back(std::move(index));
    blockExpr->body.push_back(std::move(rangeStop));
    blockExpr->body.push_back(std::move(update));
    blockExpr->body.push_back(std::move(rangeCond));
    blockExpr->ty = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
    forInExpr.inExpression = std::move(blockExpr);
    forInExpr.forInKind = ForInKind::FORIN_RANGE;
    /* setup flag for jumpExpr: continue/break */
    RearrangeRefLoop(forInExpr, forInExpr, forInExpr.body.get());
}

void TypeChecker::TypeCheckerImpl::ReArrangeForInStringExpr(ASTContext& ctx, ForInExpr& forInExpr)
{
    // To : var iter = 0
    auto litZero = CreateLitConstExpr(LitConstKind::INTEGER, "0", TypeManager::GetPrimitiveTy(TypeKind::TYPE_INT64));
    auto index = CreateVarDecl(ITER_COMPILER, std::move(litZero));
    ctx.AddDeclName(std::make_pair(ITER_COMPILER, forInExpr.scopeName), *index);
    CopyBasicInfo(forInExpr.pattern, index.get());
    index->ty = index->initializer->ty;
    index->isVar = true;
    index->begin = forInExpr.pattern->begin;
    index->end = forInExpr.pattern->end;
    // To : var tmp = e1
    auto tmp = CreateTmpVarDecl();
    tmp->initializer = std::move(forInExpr.inExpression);
    CopyBasicInfo(tmp->initializer.get(), tmp.get());
    tmp->ty = tmp->initializer->ty;
    tmp->isVar = false;
    // To: let end = tmp.size
    auto end = CreateTmpVarDecl();
    auto sizeDecl = FieldLookup(ctx, Ty::GetDeclPtrOfTy(tmp->ty), "size", {.lookupExtend = false});
    CJC_ASSERT(!sizeDecl.empty());
    auto getter = StaticCast<FuncDecl*>(GetUsedMemberDecl(*sizeDecl[0], true));
    CJC_NULLPTR_CHECK(getter);
    end->initializer = CreateCallExpr(CreateMemberAccess(CreateRefExpr(*tmp), *getter), {}, getter, sizeDecl[0]->ty);
    end->initializer->begin = tmp->begin;
    end->initializer->end = tmp->end;
    CopyBasicInfo(&forInExpr, end.get());
    end->begin = tmp->begin;
    end->end = tmp->end;
    AddCurFile(*end->initializer, forInExpr.curFile);
    end->ty = SynthesizeWithoutRecover(ctx, end->initializer.get());
    CJC_ASSERT(end->ty && end->ty->kind != TypeKind::TYPE_INVALID);
    // To : 'let pat  = tmp[iter]'
    if (auto vp = DynamicCast<VarPattern*>(forInExpr.pattern.get())) {
        auto base = CreateMemberAccess(CreateRefExpr(*tmp, *end), "[]");
        base->ty = Ty::GetInitialTy();
        CopyBasicInfo(end, base->baseExpr.get());
        std::vector<OwnedPtr<FuncArg>> args;
        args.emplace_back(CreateFuncArg(CreateRefExpr(*index, *end)));
        CopyBasicInfo(end, args[0]->expr.get());
        auto ce = CreateCallExpr(std::move(base), std::move(args));
        CopyBasicInfo(end, ce.get());
        AddCurFile(*ce, forInExpr.curFile);
        auto ceTy = SynthesizeWithoutRecover(ctx, ce.get());
        CJC_ASSERT(ceTy && ceTy->kind != TypeKind::TYPE_INVALID);
        vp->varDecl->initializer = std::move(ce);
    }

    auto blockExpr = MakeOwnedNode<Block>();
    AddCurFile(*blockExpr, forInExpr.curFile);
    CopyBasicInfo(tmp->initializer, blockExpr.get());
    blockExpr->body.push_back(std::move(index));
    blockExpr->body.push_back(std::move(tmp));
    blockExpr->body.push_back(std::move(end));
    blockExpr->ty = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
    forInExpr.inExpression = std::move(blockExpr);
    forInExpr.forInKind = ForInKind::FORIN_STRING;
    /* setup flag for jumpExpr: continue/break */
    RearrangeRefLoop(forInExpr, forInExpr, forInExpr.body.get());
}

void TypeChecker::TypeCheckerImpl::ReArrangeForInIterExpr(ASTContext& ctx, ForInExpr& forInExpr)
{
    // To : let iter = e.iterator()
    auto init = MakeOwnedNode<CallExpr>();
    auto inExprBegin = forInExpr.inExpression->GetBegin();
    auto inExprEnd = forInExpr.inExpression->GetEnd();
    auto initBase = MakeOwnedNode<MemberAccess>();
    initBase->baseExpr = std::move(forInExpr.inExpression);
    initBase->field = "iterator";
    init->baseFunc = std::move(initBase);
    AddCurFile(*init, forInExpr.curFile);
    SynthesizeWithoutRecover(ctx, init.get());
    RawStaticCast<MemberAccess*>(init->baseFunc.get())->target = init->resolvedFunction;
    auto iterator = CreateVarDecl(ITER_COMPILER, std::move(init));
    iterator->begin = forInExpr.pattern->begin;
    iterator->end = forInExpr.pattern->end;

    auto matchExpr = MakeOwnedNode<MatchExpr>();
    matchExpr->matchMode = true;
    matchExpr->begin = iterator->begin;
    matchExpr->end = iterator->end;
    // To : iter.next()
    {
        auto next = MakeOwnedNode<CallExpr>();
        auto nextBase = MakeOwnedNode<MemberAccess>();
        nextBase->baseExpr = CreateRefExpr(*iterator);
        nextBase->field = "next";
        next->begin = nextBase->begin = init->begin;
        next->end = nextBase->end = init->end;
        next->baseFunc = std::move(nextBase);
        AddCurFile(*next, forInExpr.curFile);
        SynthesizeWithoutRecover(ctx, next.get());
        RawStaticCast<MemberAccess*>(next->baseFunc.get())->target = next->resolvedFunction;
        matchExpr->selector = std::move(next);
    }
    matchExpr->ty = matchExpr->selector->ty;
    auto optionDecl = importManager.GetCoreDecl(STD_LIB_OPTION);
    auto somePattern = MakeOwnedNode<EnumPattern>();
    AddCurFile(*somePattern, forInExpr.curFile);
    CopyBasicInfo(&forInExpr, somePattern.get());
    somePattern->begin = inExprBegin;
    somePattern->end = inExprEnd;
    somePattern->ty = matchExpr->selector->ty;
    auto someRef = MakeOwnedNode<RefExpr>();
    someRef->ref.identifier = OPTION_VALUE_CTOR;
    someRef->ref.target = LookupEnumMember(optionDecl, OPTION_VALUE_CTOR);
    someRef->ty = typeManager.GetFunctionTy(somePattern->ty->typeArgs, somePattern->ty);
    somePattern->constructor = std::move(someRef);
    somePattern->patterns.emplace_back(std::move(forInExpr.pattern));
    forInExpr.pattern = std::move(somePattern);
    auto blockExpr = MakeOwnedNode<Block>();
    AddCurFile(*blockExpr, forInExpr.curFile);
    CopyBasicInfo(&forInExpr, blockExpr.get());
    blockExpr->begin = inExprBegin;
    blockExpr->end = inExprEnd;
    blockExpr->body.push_back(std::move(iterator));
    blockExpr->body.push_back(std::move(matchExpr));
    blockExpr->ty = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
    forInExpr.inExpression = std::move(blockExpr);
    forInExpr.forInKind = ForInKind::FORIN_ITER;
    /* setup flag for jumpExpr: continue/break */
    RearrangeRefLoop(forInExpr, forInExpr, forInExpr.body.get());
}

/**
 * Desugar ForInExpr to while expr. for example:
 * *************** before desugar ****************
 *  for (pat in e1 where e2) {
 *     b
 *  }
 * *************** after desugar ****************
 *  let iter = e1.iterator()
 *  while (true) {
 *     match (iter.next()) {
 *          case None => break
 *          case Some(v) => match v {
 *              case pat where e2 => b
 *              case _ => continue
 *          }
 *     }
 * }
 */
void TypeChecker::TypeCheckerImpl::DesugarForInIter(ASTContext& ctx, AST::ForInExpr& forInExpr)
{
    // To : let iter = e.iterator()
    auto init = MakeOwnedNode<CallExpr>();
    auto initBase = MakeOwnedNode<MemberAccess>();
    auto inExprBegin = forInExpr.inExpression->begin;
    auto inExprEnd = forInExpr.inExpression->end;
    initBase->baseExpr = std::move(forInExpr.inExpression);
    initBase->field = "iterator";
    initBase->begin = init->begin = inExprBegin;
    initBase->end = init->end = inExprEnd;
    init->baseFunc = std::move(initBase);
    AddCurFile(*init, forInExpr.curFile);
    SynthesizeWithoutRecover(ctx, init.get());
    RawStaticCast<MemberAccess*>(init->baseFunc.get())->target = init->resolvedFunction;
    auto iterator = CreateVarDecl(ITER_COMPILER, std::move(init));
    iterator->begin = forInExpr.pattern->begin;
    iterator->end = forInExpr.pattern->end;
    // To : while true
    auto whileExpr = MakeOwnedNode<WhileExpr>();
    whileExpr->sugarKind = Expr::SugarKind::FOR_IN_EXPR;
    whileExpr->begin = forInExpr.body->begin;
    whileExpr->end = forInExpr.body->end;
    whileExpr->condExpr = MakeOwned<LitConstExpr>(LitConstKind::BOOL, "true");
    whileExpr->condExpr->begin = inExprBegin;
    whileExpr->condExpr->end = inExprEnd;
    whileExpr->condExpr->ty = TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN);
    whileExpr->EnableAttr(Attribute::IMPLICIT_ADD);

    auto matchExpr = MakeOwnedNode<MatchExpr>();
    CJC_ASSERT(forInExpr.body);
    matchExpr->ty = forInExpr.body->ty;
    matchExpr->matchMode = true;
    matchExpr->begin = inExprBegin;
    matchExpr->end = inExprEnd;
    // To : iter.next()
    {
        auto next = MakeOwnedNode<CallExpr>();
        next->begin = forInExpr.begin;
        auto nextBase = MakeOwnedNode<MemberAccess>();
        nextBase->baseExpr = CreateRefExpr(*iterator);
        nextBase->begin = inExprBegin;
        nextBase->end = inExprEnd;
        nextBase->field = "next";
        next->baseFunc = std::move(nextBase);
        AddCurFile(*next, forInExpr.curFile);
        SynthesizeWithoutRecover(ctx, next.get());
        RawStaticCast<MemberAccess*>(next->baseFunc.get())->target = next->resolvedFunction;
        matchExpr->selector = std::move(next);
    }

    auto optionDecl = importManager.GetCoreDecl(STD_LIB_OPTION);
    // To : case None => break
    matchExpr->matchCases.push_back(CreateNoneCaseForForInIter(optionDecl, *whileExpr, matchExpr->selector->ty));

    // To : case Some(v) => match (v) { case pat if e2 => b; case _ => continue}
    auto caseSome = CreateSomeCaseForForInIter(optionDecl, *whileExpr,
        typeManager.GetFunctionTy(matchExpr->selector->ty->typeArgs, matchExpr->selector->ty), forInExpr);
    matchExpr->matchCases.push_back(std::move(caseSome));

    whileExpr->body = MakeOwnedNode<Block>();
    whileExpr->body->body.push_back(std::move(matchExpr));
    whileExpr->body->ty = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
    whileExpr->ty = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
    // Connect vardecl and while expr.
    auto blockExpr = MakeOwnedNode<Block>();
    blockExpr->body.push_back(std::move(iterator));
    blockExpr->body.push_back(std::move(whileExpr));
    CopyBasicInfo(&forInExpr, blockExpr.get());
    blockExpr->ty = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
    AddCurFile(*blockExpr, forInExpr.curFile);
    forInExpr.desugarExpr = std::move(blockExpr);
}

/**
 * Desugar ForInExpr to while expr. for example:
 * *************** before desugar ****************
 *  for (pat in e1 where e2) {
 *     b
 *  }
 * *************** after desugar ****************
 *  var iter = 0
 *  let tmp = e1
 *  let end: T = tmp.size
 *  while (iter < end) {
 *      pat = tmp[iter]; // For user declared 'pat', which should be immutable if been captured.
 *      iter += 1
 *      if (guard) {
 *          body
 *      }
 *  }
 */
void TypeChecker::TypeCheckerImpl::DesugarForInString(ASTContext& ctx, AST::ForInExpr& forInExpr)
{
    // To : var iter = 0
    auto litZero = CreateLitConstExpr(LitConstKind::INTEGER, "0", TypeManager::GetPrimitiveTy(TypeKind::TYPE_INT64));
    auto index = CreateVarDecl(ITER_COMPILER, std::move(litZero));
    ctx.AddDeclName(std::make_pair(ITER_COMPILER, forInExpr.scopeName), *index);
    CopyBasicInfo(&forInExpr, index.get());
    index->ty = index->initializer->ty;
    index->isVar = true;
    // To : var tmp = e1
    auto tmp = CreateTmpVarDecl();
    tmp->initializer = std::move(forInExpr.inExpression);
    CopyBasicInfo(tmp->initializer.get(), tmp.get());
    tmp->ty = tmp->initializer->ty;
    tmp->isVar = false;
    // To: let end = tmp.size
    auto end = CreateTmpVarDecl();
    auto sizeDecl = FieldLookup(ctx, Ty::GetDeclPtrOfTy(tmp->ty), "size", {.lookupExtend = false});
    CJC_ASSERT(!sizeDecl.empty());
    auto getter = StaticCast<FuncDecl*>(GetUsedMemberDecl(*sizeDecl[0], true));
    CJC_NULLPTR_CHECK(getter);
    end->initializer = CreateCallExpr(CreateMemberAccess(CreateRefExpr(*tmp), *getter), {}, getter, sizeDecl[0]->ty);
    CopyBasicInfo(&forInExpr, end.get());
    AddCurFile(*end->initializer, forInExpr.curFile);
    end->ty = SynthesizeWithoutRecover(ctx, end->initializer.get());
    CJC_ASSERT(end->ty && end->ty->kind != TypeKind::TYPE_INVALID);
    // To : where (iter < end)
    auto condition = CreateBinaryExpr(GetVarRef(*index), GetVarRef(*end), TokenKind::LT);
    condition->ty = TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN);
    // To: if (guard) { body }
    auto condBody = CreateGuard(forInExpr, *condition, false);

    auto whileExpr = MakeOwnedNode<WhileExpr>();
    whileExpr->sugarKind = Expr::SugarKind::FOR_IN_EXPR;
    CopyBasicInfo(&forInExpr, whileExpr.get());
    whileExpr->condExpr = std::move(condition);
    whileExpr->body = MakeOwnedNode<Block>();
    CopyBasicInfo(&forInExpr, whileExpr->body.get());
    // To : 'let pat  = tmp[iter]'
    if (auto vp = DynamicCast<VarPattern*>(forInExpr.pattern.get())) {
        auto base = CreateMemberAccess(CreateRefExpr(*tmp), "[]");
        base->ty = Ty::GetInitialTy();
        CopyBasicInfo(&forInExpr, base->baseExpr.get());
        std::vector<OwnedPtr<FuncArg>> args;
        args.emplace_back(CreateFuncArg(CreateRefExpr(*index)));
        CopyBasicInfo(&forInExpr, args[0]->expr.get());
        auto ce = CreateCallExpr(std::move(base), std::move(args));
        CopyBasicInfo(&forInExpr, ce.get());
        AddCurFile(*ce, forInExpr.curFile);
        auto ceTy = SynthesizeWithoutRecover(ctx, ce.get());
        CJC_ASSERT(ceTy && ceTy->kind != TypeKind::TYPE_INVALID);
        auto letValue = std::move(vp->varDecl);
        forInExpr.pattern.reset();
        letValue->initializer = std::move(ce);
        // Put let value declaration between.
        whileExpr->body->body.push_back(std::move(letValue));
    }
    // To: 'iter += 1'
    auto update = MakeOwnedNode<AssignExpr>();
    update->leftValue = GetVarRef(*index);
    update->rightExpr =
        CreateLitConstExpr(LitConstKind::INTEGER, "1", TypeManager::GetPrimitiveTy(TypeKind::TYPE_INT64));
    update->op = TokenKind::ADD_ASSIGN;
    update->isCompound = true;
    AddCurFile(*update, forInExpr.curFile);
    Ty* updateTy = SynthesizeWithoutRecover(ctx, update.get());
    CJC_ASSERT(updateTy && updateTy->kind != TypeKind::TYPE_INVALID);
    whileExpr->body->body.push_back(std::move(update));
    whileExpr->body->body.push_back(std::move(condBody));
    whileExpr->ty = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
    whileExpr->body->ty = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
    auto refWhile = whileExpr.get();

    auto blockExpr = MakeOwnedNode<Block>();
    CopyBasicInfo(&forInExpr, blockExpr.get());
    blockExpr->body.push_back(std::move(index));
    blockExpr->body.push_back(std::move(tmp));
    blockExpr->body.push_back(std::move(end));
    blockExpr->body.push_back(std::move(whileExpr));
    AddCurFile(*blockExpr, forInExpr.curFile);
    Ty* blockExprTy = SynthesizeWithoutRecover(ctx, blockExpr.get());
    /* must do after synthesize */
    RearrangeRefLoop(forInExpr, *refWhile, refWhile->body.get());
    CJC_ASSERT(blockExprTy && blockExprTy->kind != TypeKind::TYPE_INVALID);
    // Add file for all created ast.
    forInExpr.desugarExpr = std::move(blockExpr);
}

/// Returns true if range expr \ref expr has step ±1 and types of both start and stop exprs are integer.
/// A range expr without step expression equals step 1.
static bool IsStepOne(const RangeExpr& expr)
{
    if (expr.startExpr->ty->IsInvalid() || !expr.startExpr->ty->IsInteger()) {
        return false;
    }
    if (expr.stopExpr->ty->IsInvalid() || !expr.stopExpr->ty->IsInteger()) {
        return false;
    }
    if (!expr.stepExpr) {
        return true;
    }
    return expr.stepExpr->isConst && (expr.stepExpr->constNumValue.asInt.Int64() == 1 ||
        expr.stepExpr->constNumValue.asInt.Int64() == -1);
}

void TypeChecker::TypeCheckerImpl::DesugarForInExpr(ASTContext& ctx, ForInExpr& forInExpr)
{
    if (!Ty::IsTyCorrect(forInExpr.ty) || diag.GetErrorCount() != 0 || forInExpr.desugarExpr != nullptr) {
        return;
    }
    auto ty = forInExpr.inExpression->ty;
    // only support RangeExpr here, because we want to determine at compile time:
    // 1. whether it is closed range
    // 2. whether it has custom step expression
    // 3. whether that custom step expression is constant
    if (ty && ty->IsRange() && ty->typeArgs[0] && ty->typeArgs[0]->IsInteger()) {
        if (auto re = DynamicCast<RangeExpr*>(forInExpr.inExpression.get()); re && IsStepOne(*re)) {
            return re->isClosed ? DesugarForInCloseRange(ctx, forInExpr)
                                : DesugarForInNonCloseRange(ctx, forInExpr);
        }
    }
    if (ty->IsString()) {
        DesugarForInString(ctx, forInExpr);
    } else {
        DesugarForInIter(ctx, forInExpr);
    }
}

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
static bool IsCallRangeConstructor(const CallExpr& call)
{
    if (call.callKind != CallKind::CALL_OBJECT_CREATION) {
        return false;
    }
    auto callee = DynamicCast<RefExpr>(&*call.baseFunc);
    if (!callee) {
        return false;
    }
    auto calledFunc = DynamicCast<FuncDecl>(callee->ref.target);
    if (!calledFunc) {
        return false;
    }
    auto& params = calledFunc->funcBody->paramLists[0]->params;
    constexpr size_t rangeConstructorParamSize{6};
    if (params.size() != rangeConstructorParamSize) {
        return false;
    }
    auto b = TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN);
    constexpr size_t rangeConstructorIntLength{3};
    for (size_t i{0}; i < rangeConstructorIntLength; ++i) {
        if (!call.args[i]->ty->IsInteger()) {
            return false;
        }
    }
    if (auto step = DynamicCast<LitConstExpr>(&*call.args[rangeConstructorIntLength - 1]->expr)) {
        if (auto cvalue = step->constNumValue.asInt.Int64(); cvalue != 1 && cvalue != -1) {
            return false;
        }
    } else {
        return false;
    }
    for (size_t i{rangeConstructorIntLength}; i < rangeConstructorParamSize; ++i) {
        if (call.args[i]->ty != b) {
            return false;
        }
    }
    if (auto isClosed = DynamicCast<LitConstExpr>(&*call.args[rangeConstructorParamSize - 1]->expr)) {
        return isClosed->constNumValue.asBoolean;
    } else {
        return false;
    }
}

bool ForInExpr::IsClosedRangeOne() const
{
    auto range = DynamicCast<RangeExpr>(inExpression.get());
    if (!range) {
        if (auto call = DynamicCast<CallExpr>(&*inExpression)) {
            return IsCallRangeConstructor(*call);
        }
        return false;
    }
    if (range->desugarExpr) {
        if (auto call = DynamicCast<CallExpr>(&*range->desugarExpr)) {
            return IsCallRangeConstructor(*call);
        }
        return false;
    }
    if (range->isClosed && IsStepOne(*range)) {
        return true;
    }
    return false;
}

void TypeChecker::TypeCheckerImpl::ReArrangeForInExpr(ASTContext& ctx, ForInExpr& forInExpr)
{
    if (!Ty::IsTyCorrect(forInExpr.ty) || diag.GetErrorCount() != 0 || forInExpr.desugarExpr != nullptr) {
        return;
    }
    auto ty = forInExpr.inExpression->ty;
    // only support RangeExpr here, because we want to determine at compile time:
    // 1. whether it is closed range
    // 2. whether it has custom step expression
    // 3. whether that custom step expression is constant
    if (forInExpr.IsClosedRangeOne()) {
        // translate to CHIR directly, no desugaring
        return;
    }
    if (ty && ty->IsRange() && ty->typeArgs[0] && ty->typeArgs[0]->IsInteger()) {
        if (auto re = DynamicCast<RangeExpr*>(forInExpr.inExpression.get()); re && IsStepOne(*re)) {
            return ReArrangeForInRangeExpr(ctx, forInExpr);
        }
    }
    if (ty->IsString()) {
        ReArrangeForInStringExpr(ctx, forInExpr);
    } else { // rollback to old desugar for performance issue
        DesugarForInExpr(ctx, forInExpr);
    }
}
#endif
