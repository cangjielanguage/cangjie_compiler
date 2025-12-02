// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/AST2CHIR/TranslateASTNode/Translator.h"

#include "cangjie/CHIR/AST2CHIR/Utils.h"
#include "cangjie/CHIR/IR/Value/LiteralValue.h"

using namespace Cangjie::CHIR;
using namespace Cangjie;
using namespace Cangjie::AST;

bool Translator::OverloadableExprMayThrowException(const AST::OverloadableExpr& expr, const Type& leftValTy) const
{
    bool mayOverflow = leftValTy.IsInteger() && expr.overflowStrategy == OverflowStrategy::THROWING;
    bool mayDivZero = (expr.op == TokenKind::DIV || expr.op == TokenKind::DIV_ASSIGN || expr.op == TokenKind::MOD ||
                          expr.op == TokenKind::MOD_ASSIGN) &&
        leftValTy.IsInteger();
    bool mayOverShift = expr.op == TokenKind::LSHIFT || expr.op == TokenKind::LSHIFT_ASSIGN ||
        expr.op == TokenKind::RSHIFT || expr.op == TokenKind::RSHIFT_ASSIGN;
    bool noException = expr.op == TokenKind::BITAND || expr.op == TokenKind::BITAND_ASSIGN ||
        expr.op == TokenKind::BITOR || expr.op == TokenKind::BITOR_ASSIGN || expr.op == TokenKind::BITXOR ||
        expr.op == TokenKind::BITXOR_ASSIGN || expr.op == TokenKind::NOT;

    return (mayOverflow || mayDivZero || mayOverShift) && !noException;
}

Value* Translator::TranslateVArrayAssign(const AssignExpr& assign)
{
    // note:
    // Suppose `arr` is a two-dimensional varaay, according to the specification,
    // `arr[m][n] = v` is illegal even if `m` & `n` are valid.
    // The reason is, `[]` should be regarded as a function call, and the return
    // value of a function is immutable. Thus, any assignment operation to an
    // element in a multi-dimensional varray is illegal.
    // If this restriction is relaxed, the `varraySet` operation will recursively
    // traverse the SubscriptExpr in the same way as the `varrayGet` operation.
    CJC_ASSERT(assign.leftValue->astKind == ASTKind::SUBSCRIPT_EXPR);
    auto se = StaticAs<ASTKind::SUBSCRIPT_EXPR>(assign.leftValue.get());
    CJC_ASSERT(se->IsVArrayAccess());

    const auto& loc = TranslateLocation(assign);
    auto lhsType = chirTy.TranslateType(*se->ty);

    CJC_ASSERT(se->indexExprs.size() == 1);
    auto indexLoc = TranslateLocation(*se->indexExprs[0]);
    if (COMPOUND_ASSIGN_EXPR_MAP.find(assign.op) == COMPOUND_ASSIGN_EXPR_MAP.end()) {
        // trivial assign, e.g a[0] = b
        auto rhs = TranslateExprArg(*assign.rightExpr);
        auto rightValLoc = TranslateLocation(*assign.rightExpr);
        rhs = GetDerefedValue(rhs, rightValLoc);

        auto lhsLeftValueInfo = TranslateExprAsLeftValue(*se->baseExpr.get());
        auto lhsBase = lhsLeftValueInfo.base;
        // polish this
        if (!lhsLeftValueInfo.path.empty()) {
            auto lhsCustomType = StaticCast<CustomType*>(lhsBase->GetType()->StripAllRefs());
            lhsBase = CreateGetElementRefWithPath(loc, lhsBase, lhsLeftValueInfo.path, currentBlock, *lhsCustomType);
        }
        CJC_ASSERT(lhsBase->GetType()->IsRef());

        auto index = TranslateExprArg(*se->indexExprs[0]);
        auto callContext = IntrisicCallContext {
            .kind = IntrinsicKind::VARRAY_SET,
            .args = std::vector<Value*>({lhsBase, TypeCastOrBoxIfNeeded(*rhs, *lhsType, loc), index})
        };
        CreateAndAppendExpression<Intrinsic>(loc, builder.GetUnitTy(), callContext, currentBlock);
        return nullptr;
    }

    auto lhsLeftValueInfo = TranslateExprAsLeftValue(*se->baseExpr.get());
    auto lhsBase = lhsLeftValueInfo.base;
    // polish this
    if (!lhsLeftValueInfo.path.empty()) {
        auto lhsCustomType = StaticCast<CustomType*>(lhsBase->GetType()->StripAllRefs());
        lhsBase = CreateGetElementRefWithPath(loc, lhsBase, lhsLeftValueInfo.path, currentBlock, *lhsCustomType);
    }
    CJC_ASSERT(lhsBase && lhsBase->GetType()->IsRef());
    auto loadLHSValue = CreateAndAppendExpression<Load>(
        loc, StaticCast<RefType*>(lhsBase->GetType())->GetBaseType(), lhsBase, currentBlock);
    auto index = TranslateExprArg(*se->indexExprs[0]);
    auto baseLoc = TranslateLocation(*se->baseExpr);
    auto arrGetContext = IntrisicCallContext {
        .kind = IntrinsicKind::VARRAY_GET,
        .args = std::vector<Value*>({loadLHSValue->GetResult(), index})
    };
    auto lhs = CreateAndAppendExpression<Intrinsic>(lhsType, arrGetContext, currentBlock)->GetResult();
    if (assign.op == TokenKind::AND_ASSIGN) {
        auto res = TransShortCircuitAnd(lhs, *assign.rightExpr, loc);
        auto arrSetContext = IntrisicCallContext {
            .kind = IntrinsicKind::VARRAY_SET,
            .args = std::vector<Value*>({lhsBase, TypeCastOrBoxIfNeeded(*res, *lhsType, loc), index})
        };
        CreateAndAppendExpression<Intrinsic>(loc, builder.GetUnitTy(), arrSetContext, currentBlock);
    } else if (assign.op == TokenKind::OR_ASSIGN) {
        auto res = TransShortCircuitOr(lhs, *assign.rightExpr, loc);
        auto arrSetContext = IntrisicCallContext {
            .kind = IntrinsicKind::VARRAY_SET,
            .args = std::vector<Value*>({lhsBase, TypeCastOrBoxIfNeeded(*res, *lhsType, loc), index})
        };
        CreateAndAppendExpression<Intrinsic>(loc, builder.GetUnitTy(), arrSetContext, currentBlock);
    } else {
        // normal compound assign, e.g a[0] += b
        auto rhsValue = TranslateExprArg(*assign.rightExpr);
        bool mayHaveException = OverloadableExprMayThrowException(assign, *lhs->GetType());
        auto binaryOpResult = TryCreateWithOV<BinaryExpression>(currentBlock, mayHaveException, assign.overflowStrategy,
            loc, lhs->GetType(), op2ExprKind.at(COMPOUND_ASSIGN_EXPR_MAP.at(assign.op)), lhs, rhsValue)
                                  ->GetResult();

        auto arrSetContext = IntrisicCallContext {
            .kind = IntrinsicKind::VARRAY_SET,
            .args = std::vector<Value*>({lhsBase, TypeCastOrBoxIfNeeded(*binaryOpResult, *lhsType, loc), index})
        };
        CreateAndAppendExpression<Intrinsic>(loc, builder.GetUnitTy(), arrSetContext, currentBlock);
    }
    return nullptr;
}

Value* Translator::TranslateCompoundAssign(const AssignExpr& assign)
{
    CJC_ASSERT(COMPOUND_ASSIGN_EXPR_MAP.find(assign.op) != COMPOUND_ASSIGN_EXPR_MAP.end());
    auto loc = TranslateLocation(assign);
    auto leftValLoc = TranslateLocation(*assign.leftValue);
    auto rightValLoc = TranslateLocation(*assign.rightExpr);

    // The style of compound assign will be like:
    //      X += Y
    // where "X" and "Y" can be some compilcated exprs

    // Translate the LHS part

    // 1) Get the LHS part "X" as left-value
    auto lhsLeftValueInfo = TranslateExprAsLeftValue(*assign.leftValue);
    auto lhsLeftValuePath = lhsLeftValueInfo.path;
    auto lhsLeftValueBase = lhsLeftValueInfo.base;
    auto lhsLeftValueBaseRefTy = lhsLeftValueBase->GetType();
    CJC_ASSERT(lhsLeftValueBaseRefTy->IsRef());
    auto lhsLeftValueBaseTy = StaticCast<RefType*>(lhsLeftValueBaseRefTy)->GetBaseType();

    // 2) Get the LHS part "X" as right-value
    Value* lhsRightValue = nullptr;
    Type* storeTargetTy = nullptr;
    if (!lhsLeftValuePath.empty()) {
        // If the path is not empty, it means the "X" is some member access chain like:
        //      A.b.c
        // where "A" can be some compilcated exprs, "b" and "c" must be member variables.
        // Specially, "A" can also be a implicit/explicit `this` or `super` param
        CJC_ASSERT(lhsLeftValueBaseRefTy->IsReferenceTypeWithRefDims(1) ||
            lhsLeftValueBaseRefTy->IsValueOrGenericTypeWithRefDims(1));
        auto lhsLeftValueBaseCustomType = StaticCast<CustomType*>(lhsLeftValueBaseTy);
        // Therefore we need to get the element value from the base
        auto getMemberRef = CreateGetElementRefWithPath(
            leftValLoc, lhsLeftValueBase, lhsLeftValuePath, currentBlock, *lhsLeftValueBaseCustomType);
        auto memberType = StaticCast<RefType*>(getMemberRef->GetType())->GetBaseType();
        storeTargetTy = memberType;
        auto loadMember = CreateAndAppendExpression<Load>(leftValLoc, memberType, getMemberRef, currentBlock);
        lhsRightValue = loadMember->GetResult();
    } else {
        // If the path is empty, it means the "X" is some simple expression like reference to other
        // variables or function call, therefore we just need to get the right value from the left value by loading
        CJC_ASSERT(!lhsLeftValueBaseTy->IsRef());
        storeTargetTy = lhsLeftValueBaseTy;
        auto loadMember = CreateAndAppendExpression<Load>(loc, lhsLeftValueBaseTy, lhsLeftValueBase, currentBlock);
        lhsRightValue = loadMember->GetResult();
    }

    // 3) Get the compound value "X + Y"
    bool mayHaveException = OverloadableExprMayThrowException(assign, *lhsRightValue->GetType());
    Value* compoundValue = nullptr;
    if (assign.op == TokenKind::AND_ASSIGN) {
        compoundValue = TransShortCircuitAnd(lhsRightValue, *assign.rightExpr, loc);
    } else if (assign.op == TokenKind::OR_ASSIGN) {
        compoundValue = TransShortCircuitOr(lhsRightValue, *assign.rightExpr, loc);
    } else {
        // Translate the RHS part "Y"
        Value* rhs = TranslateExprArg(*assign.rightExpr);
        auto binOp = TryCreateWithOV<BinaryExpression>(currentBlock, mayHaveException, assign.overflowStrategy, loc,
            lhsRightValue->GetType(), op2ExprKind.at(COMPOUND_ASSIGN_EXPR_MAP.at(assign.op)), lhsRightValue, rhs);
        compoundValue = binOp->GetResult();
    }
    compoundValue = TypeCastOrBoxIfNeeded(*compoundValue, *storeTargetTy, loc);

    // 4) Implement the store
    if (!lhsLeftValuePath.empty()) {
        CreateAndAppendExpression<StoreElementByName>(
            loc, builder.GetUnitTy(), compoundValue, lhsLeftValueBase, lhsLeftValuePath, currentBlock);
    } else {
        CreateAndAppendExpression<Store>(loc, builder.GetUnitTy(), compoundValue, lhsLeftValueBase, currentBlock);
    }
    return nullptr;
}

Value* Translator::TranslateTrivialAssign(const AST::AssignExpr& assign)
{
    const auto& loc = TranslateLocation(assign);
    auto leftValLoc = TranslateLocation(*assign.leftValue);
    auto rightValLoc = TranslateLocation(*assign.rightExpr);

    Value* rhs;
    std::vector<std::string> path;
    Value* lhs;

    if (assign.leftValue->mapExpr) {
        // compound assignment, left first, right second
        auto linfo = TranslateExprAsLeftValue(*assign.leftValue);
        lhs = linfo.base;
        path = std::move(linfo.path);
        if (!path.empty()) {
            auto lhsCustomType = StaticCast<CustomType*>(lhs->GetType()->StripAllRefs());
            auto getMemberRef = CreateGetElementRefWithPath(loc, lhs, path, currentBlock, *lhsCustomType);
            exprValueTable.Set(*assign.leftValue, *getMemberRef);
        }

        rhs = TranslateExprArg(*assign.rightExpr);
    } else {
        // normal assignment, right first, left second
        rhs = TranslateExprArg(*assign.rightExpr);
        auto linfo = TranslateExprAsLeftValue(*assign.leftValue);
        lhs = linfo.base;
        path = std::move(linfo.path);
    }
    if (!lhs) {
        return nullptr;
    }
    CJC_ASSERT(lhs && lhs->GetType()->IsRef());
    if (!path.empty()) {
        auto lhsCustomType = StaticCast<CustomType*>(lhs->GetType()->StripAllRefs());
        auto memberType = GetInstMemberTypeByName(*lhsCustomType, path, builder);
        rhs = TypeCastOrBoxIfNeeded(*rhs, *memberType, loc);
        CreateAndAppendExpression<StoreElementByName>(loc, builder.GetUnitTy(), rhs, lhs, path, currentBlock);
    } else {
        rhs = TypeCastOrBoxIfNeeded(*rhs, *StaticCast<RefType*>(lhs->GetType())->GetBaseType(), loc);
        CreateAndAppendExpression<Store>(loc, builder.GetUnitTy(), rhs, lhs, currentBlock);
    }
    return nullptr;
}

Ptr<Value> Translator::Visit(const AST::AssignExpr& assign)
{
    // Case 1: assign to a wildcard expr
    if (assign.leftValue->astKind == AST::ASTKind::WILDCARD_EXPR) {
        TranslateSubExprToDiscarded(*assign.rightExpr);
        return nullptr;
    }

    // Case 2: assign to a subscript expr which must be a VArray access
    if (auto se = DynamicCast<SubscriptExpr*>(assign.leftValue.get()); se && !se->isTupleAccess) {
        return TranslateVArrayAssign(assign);
    }

    // Case 3: compound assign
    if (COMPOUND_ASSIGN_EXPR_MAP.find(assign.op) != COMPOUND_ASSIGN_EXPR_MAP.end()) {
        return TranslateCompoundAssign(assign);
    }

    // Case 4: trivial assign
    return TranslateTrivialAssign(assign);
}
