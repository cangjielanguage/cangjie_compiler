// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Optimization/ArrayLambdaOpt.h"

#include "cangjie/CHIR/Analysis/Utils.h"
#include "cangjie/CHIR/Utils/CHIRCasting.h"
#include "cangjie/CHIR/Utils/Utils.h"
#include "cangjie/CHIR/Utils/Visitor/Visitor.h"

using namespace Cangjie::CHIR;

ArrayLambdaOpt::ArrayLambdaOpt(CHIRBuilder& builder) : builder(builder)
{
}

void ArrayLambdaOpt::RunOnPackage(const Ptr<const Package>& package, bool isDebug)
{
    for (auto func : package->GetGlobalFuncs()) {
        RunOnFunc(func, isDebug);
    }
}

void ArrayLambdaOpt::RunOnFunc(const Ptr<Func>& func, bool isDebug)
{
    auto preAcation = [this, isDebug](Expression& expr) {
        if (auto constVal = CheckCanRewriteLambda(&expr); constVal) {
            RewriteArrayInitFunc(StaticCast<Apply&>(expr), constVal);
            if (isDebug) {
                std::string message = "[ArrayLambda] The call to arrayInitByFunction" +
                    ToPosInfo(expr.GetDebugLocation()) + " has been optimised to a call to arrayInitByValue.\n";
                std::cout << message;
            }
        } else if (auto zeroValue = CheckCanRewriteZeroValue(&expr); zeroValue) {
            RewriteZeroValue(StaticCast<RawArrayInitByValue*>(&expr), zeroValue);
            if (isDebug) {
                std::string message = "[ArrayLambda] The call to arrayInitByValue" +
                    ToPosInfo(expr.GetDebugLocation()) + " has been optimised due to the item is ZeroValue.\n";
                std::cout << message;
            }
        }
        return VisitResult::CONTINUE;
    };
    Visitor::Visit(*func, preAcation);
}

static const FuncInfo ARRAY_INIT_FUNC_INFO{"arrayInitByFunction", "", {NOT_CARE}, NOT_CARE, Cangjie::CORE_PACKAGE_NAME};
Ptr<Constant> ArrayLambdaOpt::CheckCanRewriteLambda(const Ptr<Expression>& expr) const
{
    if (expr->GetExprKind() != ExprKind::APPLY) {
        return nullptr;
    }
    auto apply = StaticCast<Apply*>(expr);
    auto callee = apply->GetCallee();
    if (!callee->IsFuncWithBody()) {
        return nullptr;
    }
    if (!IsExpectedFunction(*VirtualCast<Func*>(callee), ARRAY_INIT_FUNC_INFO)) {
        return nullptr;
    }

    constexpr size_t INIT_LAMBDA_INDEX = 1;
    CJC_ASSERT(apply->GetArgs().size() == INIT_LAMBDA_INDEX + 1);
    auto closureVar = apply->GetArgs()[INIT_LAMBDA_INDEX];
    if (!closureVar->IsLocalVar()) {
        return nullptr;
    }
    auto closure = StaticCast<LocalVar*>(closureVar)->GetExpr();
    if (closure->GetExprKind() != ExprKind::LAMBDA) {
        return nullptr;
    }
    return CheckIfLambdaReturnConst(*StaticCast<const Lambda*>(closure));
}

Ptr<Constant> ArrayLambdaOpt::CheckIfLambdaReturnConst(const Lambda& lambda) const
{
    auto ret = lambda.GetReturnValue();
    if (!ret) {
        return nullptr;
    }
    CJC_ASSERT(ret->GetExpr()->GetExprKind() == ExprKind::ALLOCATE);
    if (auto users = ret->GetUsers(); users.size() == 1) {
        if (auto store = users[0]; store->GetExprKind() == ExprKind::STORE) {
            auto retVal = StaticCast<Store*>(store)->GetValue();
            if (!retVal->IsLocalVar()) {
                return nullptr;
            }
            auto constant = StaticCast<LocalVar*>(retVal)->GetExpr();
            if (constant->GetExprKind() != ExprKind::CONSTANT) {
                return nullptr;
            }

            std::unordered_set<Expression*> validExprs{ret->GetExpr(), store, constant};

            auto blocksInLambda = lambda.GetBody()->GetBlocks();
            if (blocksInLambda.size() > 1) {
                return nullptr;
            }
            for (auto e : blocksInLambda[0]->GetExpressions()) {
                if (e->IsDebug() || e->IsTerminator()) {
                    continue;
                }
                if (validExprs.find(e) == validExprs.end()) {
                    return nullptr;
                }
            }

            return StaticCast<Constant*>(constant);
        }
    }
    return nullptr;
}
void ArrayLambdaOpt::RewriteArrayInitFunc(Apply& apply, const Ptr<const Constant>& constant)
{
    auto& loc = apply.GetDebugLocation();
    auto rawArray = apply.GetArgs()[0];
    auto size = StaticCast<LocalVar*>(rawArray)->GetExpr()->GetOperand(0);
    auto parent = apply.GetParentBlock();
    auto initVal = builder.CreateExpression<Constant>(constant->GetDebugLocation(), constant->GetResult()->GetType(),
        StaticCast<LiteralValue*>(constant->GetValue()), parent);
    initVal->SetDebugLocation(constant->GetDebugLocation());
    auto newExpr = builder.CreateExpression<RawArrayInitByValue>(
        loc, builder.GetUnitTy(), rawArray, size, initVal->GetResult(), parent);

    initVal->MoveBefore(&apply);
    apply.ReplaceWith(*newExpr);
    newExpr->GetResult()->ReplaceWith(*rawArray, parent->GetParentBlockGroup());
}

Ptr<Intrinsic> ArrayLambdaOpt::CheckCanRewriteZeroValue(const Ptr<Expression>& expr) const
{
    if (expr->GetExprKind() != ExprKind::RAW_ARRAY_INIT_BY_VALUE) {
        return nullptr;
    }

    auto init = StaticCast<RawArrayInitByValue*>(expr);
    if (!init->GetInitValue()->IsLocalVar()) {
        return nullptr;
    }

    auto initVal = StaticCast<LocalVar*>(init->GetInitValue());
    auto initExpr = initVal->GetExpr();
    if (initExpr->GetExprKind() != ExprKind::INTRINSIC) {
        return nullptr;
    }
    auto intrinsic = StaticCast<Intrinsic*>(initExpr);
    if (intrinsic->GetIntrinsicKind() != IntrinsicKind::OBJECT_ZERO_VALUE) {
        return nullptr;
    }

    return intrinsic;
}

void ArrayLambdaOpt::RewriteZeroValue(const Ptr<RawArrayInitByValue>& init, const Ptr<Intrinsic>& zeroVal) const
{
    CJC_ASSERT(init->GetResult()->GetUsers().empty());
    init->RemoveSelfFromBlock();

    auto users = zeroVal->GetResult()->GetUsers();
    if (users.empty()) {
        zeroVal->RemoveSelfFromBlock();
    } else if (users.size() == 1 && users[0]->GetExprKind() == ExprKind::DEBUGEXPR) {
        users[0]->RemoveSelfFromBlock();
        zeroVal->RemoveSelfFromBlock();
    }
}
