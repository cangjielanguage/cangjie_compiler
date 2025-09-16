// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Transformation/FlatForInExpr.h"

#include "cangjie/CHIR/Analysis/Utils.h"
#include "cangjie/CHIR/CHIRCasting.h"
#include "cangjie/CHIR/GeneratedFromForIn.h"
#include "cangjie/CHIR/Utils.h"
#include "cangjie/CHIR/Visitor/Visitor.h"

#include <future>
#include <queue>

using namespace Cangjie::CHIR;

FlatForInExpr::FlatForInExpr(CHIRBuilder& builder)
    : builder(builder)
{
}

void FlatForInExpr::RunOnPackage(const Package& package)
{
    for (auto& func : package.GetGlobalFuncs()) {
        RunOnFunc(*func);
    }
}

namespace {
using namespace Cangjie;
void MoveBlocksToParentBG(const ForIn& expr)
{
    auto bg = expr.GetParentBlockGroup();
    CJC_NULLPTR_CHECK(bg);
    for (auto bg1 : expr.GetExecutionOrder()) {
        for (auto bl : bg1->GetBlocks()) {
            bl->MoveTo(*bg);
            bl->SetParentBlockGroup(bg);
        }
    }
}

void ReplaceExitWithGoto(CHIRBuilder& builder, const BlockGroup& src, Block& target)
{
    for (auto bl : src.GetBlocks()) {
        auto term = bl->GetTerminator();
        CJC_NULLPTR_CHECK(term);
        if (term->GetExprKind() == CHIR::ExprKind::EXIT) {
            auto oldAnnotations = term->MoveAnnotation();
            term->RemoveSelfFromBlock();
            auto gotoCond = builder.CreateTerminator<GoTo>(&target, bl);
            gotoCond->SetAnnotation(std::move(oldAnnotations));
            bl->AppendExpression(gotoCond);
        }
    }
}

Block* GetEntryBlock(const ForIn& expr)
{
    return (*expr.GetExecutionOrder().begin())->GetEntryBlock();
}

using ExprIt = std::vector<Expression*>::iterator;
/// Move expressions in this block after the ForIn to a new block. The new block will serve as the common block to jump
/// to after the ForIn is executed in CFG.
Block* MoveExpressionAfterForIn(CHIRBuilder& builder, ExprIt it, ExprIt end)
{
    ++it;
    auto& expr = **it;
    auto parentBlock = expr.GetParentBlock();
    // create a new block to collect expressions after this ForInExpr before the terminator
    auto newSuc = builder.CreateBlock(parentBlock->GetParentBlockGroup());
    for (auto mv = it; mv != end; ++mv) {
        // move all expression except the last (the goto) to the new block
        (*mv)->MoveTo(*newSuc);
    }
    return newSuc;
}
}

void FlatForInExpr::FlatternForInExpr(ExprIt it, ExprIt end)
{
    auto& expr = StaticCast<ForIn>(**it);
    if (Is<ForInClosedRange>(&expr)) {
        return FlatternForInClosedRange(it, end);
    }
    auto parentBlock = expr.GetParentBlock();
    auto forInSuc = MoveExpressionAfterForIn(builder, it, end);

    auto bg = expr.GetParentBlockGroup();
    auto jumpBlock = builder.CreateBlock(bg);
    auto loadCondVar = builder.CreateExpression<Load>(builder.GetBoolTy(), expr.GetLoopCondVar(), jumpBlock);
    auto jumpBr = builder.CreateTerminator<Branch>(
        expr.GetDebugLocation(), loadCondVar->GetResult(), expr.GetBody()->GetEntryBlock(), forInSuc, jumpBlock);
    jumpBr->SetSourceExpr(SourceExpr::FOR_IN_EXPR);
    jumpBlock->AppendExpression(loadCondVar);
    jumpBlock->AppendExpression(jumpBr);

    if (Is<ForInRange>(expr)) {
        ReplaceExitWithGoto(builder, *expr.GetCond(), *jumpBlock);
        ReplaceExitWithGoto(builder, *expr.GetBody(), *expr.GetLatch()->GetEntryBlock());
        ReplaceExitWithGoto(builder, *expr.GetLatch(), *expr.GetCond()->GetEntryBlock());
    } else {
        CJC_ASSERT(Is<ForInIter>(expr));
        ReplaceExitWithGoto(builder, *expr.GetLatch(), *expr.GetCond()->GetEntryBlock());
        ReplaceExitWithGoto(builder, *expr.GetCond(), *expr.GetBody()->GetEntryBlock());
        ReplaceExitWithGoto(builder, *expr.GetBody(), *jumpBlock);
    }

    auto parentGotoCond = builder.CreateTerminator<GoTo>(GetEntryBlock(expr), parentBlock);
    parentBlock->AppendExpression(parentGotoCond);
    MoveBlocksToParentBG(expr);
    expr.RemoveSelfFromBlock();
}

/*
#parentBlock:
    var iter = a
    var cond = true
    let outerRangeCheck = LE(%a, %b)
    Branch(%outerRangeCheck, #body, #endBlock)

#body:
    let i = Load(iter)
    if guard {
        body
    }
    GoTo(#cond)

#cond:
    // cond is true here
    cond = i != b
        // != instead of <= is where the optimisation goes
    GoTo(#latchEntryBlock)

#latchEntryBlock:
    let condValue = Load(cond)
    Branch(%condValue, #latch, #oldSuc)

#latch:
    iter = i + 1
    GoTo(#body)

#oldSuc:
    let forinResult = ()
*/
void FlatForInExpr::FlatternForInClosedRange(ExprIt it, ExprIt end)
{
    auto& expr = StaticCast<ForInClosedRange>(**it);
    auto parentBlock = expr.GetParentBlock();
    auto forInSuc = MoveExpressionAfterForIn(builder, it, end);

    auto gotoForin = builder.CreateTerminator<GoTo>(GetEntryBlock(expr), parentBlock);
    CJC_ASSERT(!parentBlock->GetTerminator());
    parentBlock->AppendExpression(gotoForin);
    
    ReplaceExitWithGoto(builder, *expr.GetBody(), *expr.GetCond()->GetEntryBlock());
    auto bg = expr.GetParentBlockGroup();
    auto latchEntryBlock = builder.CreateBlock(bg);
    latchEntryBlock->Set<GeneratedFromForIn>(true);
    ReplaceExitWithGoto(builder, *expr.GetCond(), *latchEntryBlock);
    auto condValue = builder.CreateExpression<Load>(
        expr.GetDebugLocation(), builder.GetBoolTy(), expr.GetLoopCondVar(), latchEntryBlock);
    latchEntryBlock->AppendExpression(condValue);
    auto contLoopBr = builder.CreateTerminator<Branch>(
        expr.GetDebugLocation(), condValue->GetResult(), expr.GetLatch()->GetEntryBlock(), forInSuc, latchEntryBlock);
    latchEntryBlock->AppendExpression(contLoopBr);
    ReplaceExitWithGoto(builder, *expr.GetLatch(), *expr.GetBody()->GetEntryBlock());
    MoveBlocksToParentBG(expr);
    expr.RemoveSelfFromBlock();
}

void FlatForInExpr::RunOnBlockGroup(BlockGroup& blockGroup)
{
    for (auto block : blockGroup.GetBlocks()) {
        auto exprs = block->GetExpressions();
        for (auto exprIt = exprs.begin(); exprIt != exprs.end(); ++exprIt) {
            auto expr = *exprIt;
            if (auto lambdaExpr = DynamicCast<const Lambda*>(expr); lambdaExpr) {
                RunOnBlockGroup(*lambdaExpr->GetBody());
                continue;
            }
            if (!Is<ForIn>(expr)) {
                continue;
            }
            auto forInExpr = StaticCast<ForIn*>(expr);
            RunOnBlockGroup(*forInExpr->GetBody());
            RunOnBlockGroup(*forInExpr->GetLatch()); // there may be complex expressions in latch if the latch block
                // is a next() call and the call is inlined
            FlatternForInExpr(exprIt, exprs.end());
            break;
        }
    }
}

void FlatForInExpr::RunOnFunc(Func& func)
{
    if (func.TestAttr(Attribute::SKIP_ANALYSIS)) {
        return;
    }
    return RunOnBlockGroup(*func.GetBody());
}
