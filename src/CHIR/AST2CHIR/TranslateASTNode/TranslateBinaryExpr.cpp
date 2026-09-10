// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/AST2CHIR/TranslateASTNode/Translator.h"

using namespace Cangjie::CHIR;
using namespace Cangjie;

Ptr<Value> Translator::Visit(const AST::BinaryExpr& binaryExpr)
{
    if (!binaryExpr.TestAttr(AST::Attribute::SIDE_EFFECT)) {
        return ProcessBinaryExpr(binaryExpr);
    }
    /* The following cangjie code may have side effect, and sema will set attribute SIDE_EFFECT on binaryExpr.
        test(1) == (true, true, true))
        desugar to:
        test(1)[0] == true && test(1)[1] == true && test(1)[2] == true
                                                | |=========================> has the Attribute: SIDE_EFFECT
        |     | ============================================================> the callExpr has mapExpr.

    */
    if (auto rightBinaryExpr = DynamicCast<AST::BinaryExpr*>(binaryExpr.rightExpr.get()); rightBinaryExpr) {
        AST::SubscriptExpr* subscriptExpr = nullptr;
        if (rightBinaryExpr->leftExpr && rightBinaryExpr->leftExpr->astKind == AST::ASTKind::SUBSCRIPT_EXPR) {
            subscriptExpr = StaticCast<AST::SubscriptExpr*>(rightBinaryExpr->leftExpr.get());
        } else if (rightBinaryExpr->rightExpr && rightBinaryExpr->rightExpr->astKind == AST::ASTKind::SUBSCRIPT_EXPR) {
            subscriptExpr = StaticCast<AST::SubscriptExpr*>(rightBinaryExpr->rightExpr.get());
        }
        if (subscriptExpr) {
            if (auto mapExpr = GetMapExpr(*subscriptExpr->baseExpr)) {
                if (!exprValueTable.Has(*mapExpr)) {
                    auto chirNode = TranslateExprArg(*subscriptExpr->baseExpr);
                    exprValueTable.Set(*mapExpr, *chirNode);
                }
            }
        }
    }
    return ProcessBinaryExpr(binaryExpr);
}

Ptr<Value> Translator::ProcessBinaryExpr(const AST::BinaryExpr& binaryExpr)
{
    const auto chirType = TranslateType(binaryExpr.GetTy());
    const auto& loc = TranslateLocation(binaryExpr);
    auto it = tokenKindToBinaryExprKind.find(binaryExpr.op);
    CJC_ASSERT(it != tokenKindToBinaryExprKind.end());
    BinaryExprKind kd = it->second;
    auto lhs = TranslateExprArg(*binaryExpr.leftExpr);
    CJC_NULLPTR_CHECK(lhs);
    if (kd == BinaryExprKind::AND) {
        return TransShortCircuitAnd(lhs, *binaryExpr.rightExpr, loc, binaryExpr.TestAttr(AST::Attribute::COMPILER_ADD));
    }
    if (kd == BinaryExprKind::OR) {
        return TransShortCircuitOr(lhs, *binaryExpr.rightExpr, loc, binaryExpr.TestAttr(AST::Attribute::COMPILER_ADD));
    }
    auto rightExpr = TranslateExprArg(*binaryExpr.rightExpr);
    bool mayHaveException = OverloadableExprMayThrowException(binaryExpr, *chirType);
    if (binaryExpr.leftExpr->GetTy()->IsNothing()) {
        const auto& rightExprLoc = TranslateLocation(*binaryExpr.rightExpr);
        return TryCreateWithOV<BinaryExpression>(currentBlock, mayHaveException, binaryExpr.overflowStrategy,
            rightExprLoc, loc, chirType, kd, lhs, rightExpr)
            ->GetResult();
    } else {
        const auto& operatorLoc = GetOperatorLoc(binaryExpr);
        return TryCreateWithOV<BinaryExpression>(
            currentBlock, mayHaveException, binaryExpr.overflowStrategy, operatorLoc, loc, chirType, kd, lhs, rightExpr)
            ->GetResult();
    }
}

Ptr<Value> Translator::TransShortCircuitAnd(
    const Ptr<Value> leftValue, const AST::Expr& rightExpr, const DebugLocation& loc, bool isImplicitAdd)
{
    auto expr = CreateAndAppendExpression<Allocate>(
        loc, builder.GetType<RefType>(builder.GetBoolTy()), builder.GetBoolTy(), currentBlock);
    Ptr<Value> alloca = expr->GetResult();
    Ptr<Block> thenBlock = CreateBlock();
    Ptr<Block> elseBlock = CreateBlock();
    Ptr<Block> endBlock = CreateBlock();
    CJC_ASSERT(
        leftValue->GetType()->IsBoolean() || leftValue->GetType()->IsNothing() || leftValue->GetType()->IsGeneric());
    const auto& rightLoc = TranslateLocation(rightExpr);
    CreateWrappedBranch(SourceExpr::BINARY, loc, leftValue, thenBlock, elseBlock, currentBlock);
    // Create Then Block:
    currentBlock = thenBlock;
    if (!isImplicitAdd) {
        currentBlock->SetDebugLocation(rightLoc);
    }
    TranslateSubExprToLoc(rightExpr, alloca, rightLoc);
    CreateAndAppendTerminator<GoTo>(loc, endBlock, currentBlock);
    // Create Else Block:
    auto boolLiteral =
        CreateAndAppendConstantExpression<BoolLiteral>(loc, builder.GetBoolTy(), *elseBlock, false)->GetResult();
    CreateAndAppendWrappedStore(*boolLiteral, *alloca, *elseBlock, loc);
    CreateAndAppendTerminator<GoTo>(loc, endBlock, elseBlock);
    // Update 'currentBlock' at last.
    currentBlock = endBlock;
    return CreateAndAppendExpression<Load>(loc, builder.GetBoolTy(), alloca, endBlock)->GetResult();
}

Ptr<Value> Translator::TransShortCircuitOr(
    const Ptr<Value> leftValue, const AST::Expr& rightExpr, const DebugLocation& loc, bool isImplicitAdd)
{
    Ptr<Value> alloca = CreateAndAppendExpression<Allocate>(
        loc, builder.GetType<RefType>(builder.GetBoolTy()), builder.GetBoolTy(), currentBlock)
                            ->GetResult();
    Ptr<Block> thenBlock = CreateBlock();
    Ptr<Block> elseBlock = CreateBlock();
    Ptr<Block> endBlock = CreateBlock();
    CJC_ASSERT(
        leftValue->GetType()->IsBoolean() || leftValue->GetType()->IsNothing() || leftValue->GetType()->IsGeneric());
    const auto& rightLoc = TranslateLocation(rightExpr);
    CreateWrappedBranch(SourceExpr::BINARY, loc, leftValue, thenBlock, elseBlock, currentBlock);
    // Create Then Block:
    auto boolLiteral =
        CreateAndAppendConstantExpression<BoolLiteral>(builder.GetBoolTy(), *thenBlock, true)->GetResult();
    CreateAndAppendWrappedStore(*boolLiteral, *alloca, *thenBlock, rightLoc);
    CreateAndAppendTerminator<GoTo>(rightLoc, endBlock, thenBlock);
    // Create Else Block:
    currentBlock = elseBlock;
    if (!isImplicitAdd) {
        currentBlock->SetDebugLocation(rightLoc);
    }
    TranslateSubExprToLoc(rightExpr, alloca, rightLoc);
    CreateAndAppendTerminator<GoTo>(rightLoc, endBlock, currentBlock);
    // Update 'currentBlock' at last.
    this->currentBlock = endBlock;
    return CreateAndAppendExpression<Load>(loc, builder.GetBoolTy(), alloca, endBlock)->GetResult();
}
