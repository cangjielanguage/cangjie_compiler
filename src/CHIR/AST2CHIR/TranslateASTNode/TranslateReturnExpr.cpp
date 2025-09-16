// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/AST2CHIR/TranslateASTNode/Translator.h"

using namespace Cangjie::CHIR;
using namespace Cangjie;

int64_t Translator::CalculateDelayExitLevelForReturn()
{
    // If there are other blockGroup exprs, please modify the condition here.
    // for example: WHILE, LOOP...
    int64_t level = 0;
    Ptr<BlockGroup> bg = blockGroupStack.back();
    for (auto rBegin = blockGroupStack.crbegin(), rEnd = blockGroupStack.crend(); rBegin != rEnd; ++rBegin) {
        bg = *rBegin;
        CJC_NULLPTR_CHECK(currentBlock->GetTopLevelFunc());
        auto parentLambda = DynamicCast<Lambda*>(bg->GetOwnerExpression());
        if (bg == currentBlock->GetTopLevelFunc()->GetBody() || parentLambda != nullptr) {
            break;
        }
        Expression* expr = bg->GetOwnerExpression();
        if (expr && Is<ForIn>(expr)) {
            // cond blockGroup need sub 1, and in the end of for-in node
            // need also sub 1, so here set to 2.
            level += 2U;
        } else if (expr && expr->GetExprKind() == ExprKind::IF) {
            ++level;
        }
    }
    return level;
}

Ptr<Value> Translator::GetOuterBlockGroupReturnValLocation()
{
    for (auto reverseBegin = blockGroupStack.crbegin(), reverseEnd = blockGroupStack.crend();
        reverseBegin != reverseEnd; ++reverseBegin) {
        Ptr<BlockGroup> bg = *reverseBegin;
        Expression* ownedExpr = bg->GetOwnerExpression();
        if (ownedExpr && (Is<ForIn>(ownedExpr) || ownedExpr->GetExprKind() == ExprKind::IF) &&
            forInExprReturnMap.count(ownedExpr->GetResult()) != 0) {
            return forInExprReturnMap[ownedExpr->GetResult()];
        }
    }
    if (blockGroupStack.empty()) {
        return nullptr;
    }
    auto curBlockGroup = blockGroupStack.back();
    if (auto func = curBlockGroup->GetOwnerFunc()) {
        return func->GetReturnValue();
    } else if (auto lambda = DynamicCast<Lambda*>(curBlockGroup->GetOwnerExpression())) {
        return lambda->GetReturnValue();
    }
    return nullptr;
}

Ptr<Value> Translator::Visit(const AST::ReturnExpr& expr)
{
    const auto& loc = TranslateLocation(expr);
    CJC_NULLPTR_CHECK(expr.expr);
    auto retVal = TranslateExprArg(*expr.expr);
    int64_t level = CalculateDelayExitLevelForReturn();
    Ptr<Value> ret = GetOuterBlockGroupReturnValLocation();
    if (ret != nullptr) {
        if (expr.TestAttr(AST::Attribute::COMPILER_ADD)) {
            CreateWrappedStore(retVal, ret, currentBlock);
        } else {
            CreateWrappedStore(loc, retVal, ret, currentBlock);
        }
    }
    if (level > 0 && delayExitSignal) {
        UpdateDelayExitSignal(level);
    }
    Ptr<Terminator> terminator = nullptr;
    if (finallyContext.empty()) {
        terminator = CreateAndAppendTerminator<Exit>(loc, currentBlock);
        /* compile add return expr should not print warning.
            public open func foo(): Int64 {
                unsafe{        ---------------> will have a compiler add return expr.
                    return 3
                }
            }
        */
        if (expr.TestAttr(AST::Attribute::COMPILER_ADD)) {
            terminator->Set<SkipCheck>(SkipKind::SKIP_DCE_WARNING);
        }
    } else {
        // When current is in try-finally context, store the control blocks with control kind value.
        auto& [_, controlBlocks] = finallyContext.top();
        auto index = static_cast<uint8_t>(ControlType::RETURN);
        auto prevBlock = currentBlock;
        // Create return in separate block, and control flow will be redirected to this block at the end of finally.
        currentBlock = CreateBlock();
        terminator = CreateAndAppendTerminator<Exit>(loc, currentBlock);
        // the pair of blocks is {the block before control flow, control flow's target block}.
        controlBlocks[index].emplace_back(prevBlock, currentBlock);
    }
    // For following unreachable expressions, and return also has value of type 'Nothing'.
    currentBlock = CreateBlock();
    maybeUnreachable.emplace(currentBlock, terminator);
    return nullptr;
}
