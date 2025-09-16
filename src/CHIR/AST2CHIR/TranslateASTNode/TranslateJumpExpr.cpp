// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/AST2CHIR/TranslateASTNode/Translator.h"

using namespace Cangjie::CHIR;
using namespace Cangjie;

int64_t Translator::CalculateDelayExitLevelForBreak()
{
    int64_t level = 0;
    for (auto reverseBegin = blockGroupStack.crbegin(), reverseEnd = blockGroupStack.crend();
        reverseBegin != reverseEnd; ++reverseBegin) {
        Ptr<BlockGroup> bg = *reverseBegin;
        Expression* expr = bg->GetOwnerExpression();
        if (expr && Is<ForIn>(expr)) {
            ++level;
            break;
        }
        if (expr && expr->GetExprKind() == ExprKind::IF) {
            ++level;
        }
    }
    return level;
}

int64_t Translator::CalculateDelayExitLevelForContinue()
{
    return CalculateDelayExitLevelForBreak() - 1;
}

void Translator::UpdateDelayExitSignal(int64_t level)
{
    auto constDepth = CreateAndAppendConstantExpression<IntLiteral>(builder.GetInt64Ty(),
        *currentBlock, static_cast<uint64_t>(level))->GetResult();
    CreateWrappedStore(constDepth, delayExitSignal, currentBlock);
}

Ptr<Value> Translator::Visit(const AST::JumpExpr& jumpExpr)
{
    const auto& loc = TranslateLocation(jumpExpr);
    Ptr<Terminator> terminator = nullptr;
    DebugLocation loopLocInfo;
    // If there are other blockGroup exprs, please modify the condition here.
    // for example: WHILE, LOOP...
    if (jumpExpr.refLoop->astKind == AST::ASTKind::FOR_IN_EXPR && delayExitSignal) {
        loopLocInfo = *forInExprAST2DebugLocMap[jumpExpr.refLoop];
    } else {
        const auto [conditionBlock, falseBlock] = *terminatorSymbolTable.Get(*jumpExpr.refLoop);
        loopLocInfo = falseBlock->GetDebugLocation();
    }
    auto scopeLevel = loopLocInfo.GetScopeInfo().size();
    if (!finallyContext.empty()) {
        // When current is in try-finally context, and control flow's scope level is outside(smaller) the
        // try-finally, store the control blocks with control kind value.
        auto& [finallyBlock, controlBlocks] = finallyContext.top();
        auto tryScopeLevel = finallyBlock->GetDebugLocation().GetScopeInfo().size();
        if (scopeLevel < tryScopeLevel) {
            auto controlIndex = static_cast<uint8_t>(jumpExpr.isBreak ? ControlType::BREAK : ControlType::CONTINUE);
            auto prevBlock = currentBlock;
            currentBlock = CreateBlock();
            currentBlock->SetDebugLocation(loopLocInfo);
            // the pair of blocks is {the block before control flow, control flow's target block}.
            controlBlocks[controlIndex].emplace_back(prevBlock, currentBlock);
        }
    }
    if (jumpExpr.refLoop->astKind == AST::ASTKind::FOR_IN_EXPR && delayExitSignal) {
        if (jumpExpr.isBreak) {
            UpdateDelayExitSignal(CalculateDelayExitLevelForBreak());
        } else {
            UpdateDelayExitSignal(CalculateDelayExitLevelForContinue());
        }
        terminator = CreateAndAppendTerminator<Exit>(loc, currentBlock);
    } else {
        const auto [conditionBlock, falseBlock] = *terminatorSymbolTable.Get(*jumpExpr.refLoop);
        if (jumpExpr.isBreak) {
            terminator = CreateAndAppendTerminator<GoTo>(loc, falseBlock, currentBlock);
        } else {
            terminator = CreateAndAppendTerminator<GoTo>(loc, conditionBlock, currentBlock);
        }
    }
    currentBlock = CreateBlock();
    maybeUnreachable.emplace(currentBlock, terminator);
    return nullptr;
}