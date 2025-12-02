// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Optimization/RedundantFutureRemoval.h"

#include "cangjie/CHIR/Analysis/Utils.h"
#include "cangjie/CHIR/Utils/Utils.h"
#include "cangjie/CHIR/Utils/Visitor/Visitor.h"

namespace Cangjie::CHIR {

RedundantFutureRemoval::RedundantFutureRemoval(const Package& pkg, bool isDebug)
    : package(pkg), isDebug(isDebug)
{
}

void RedundantFutureRemoval::RunOnPackage()
{
    for (auto func : package.GetGlobalFuncs()) {
        RunOnFunc(*func);
    }
}

void RedundantFutureRemoval::RunOnFunc(const Func& func)
{
    auto visitExitAction = [this](Expression& expr) {
        auto [future, apply] = CheckSpawnWithFuture(expr);
        if (future != nullptr) {
            auto spawnExpr = StaticCast<Spawn*>(&expr);
            RewriteSpawnWithOutFuture(*spawnExpr, *future, *apply);
            if (isDebug) {
                std::string message = "[RedundantFutureRemoval] The call to Spawn" +
                    ToPosInfo(expr.GetDebugLocation()) +
                    " has been optimised due to redundant future in spawn.\n";
                std::cout << message;
            }
        }
        return VisitResult::CONTINUE;
    };
    Visitor::Visit(func, visitExitAction);
}

FuncBase* RedundantFutureRemoval::GetExecureClosureFunc() const
{
    for (auto def : package.GetAllCustomTypeDef()) {
        if (!IsCoreFuture(*def)) {
            continue;
        }
        for (auto method : def->GetMethods()) {
            if (method->GetSrcCodeIdentifier() == "executeClosure") {
                return method;
            }
        }
        return nullptr;
    }
    return nullptr;
}

void RedundantFutureRemoval::RewriteSpawnWithOutFuture(Spawn& spawnExpr, LocalVar& futureValue, Apply& apply)
{
    /* change from:
        %a : future = Allocate()
        %b : funcType = Lambda()
        %c : Apply(Future, %a, %b)
        %d : Spawn(%a)
        change to:
        %b : funcType = Lambda()
        %d : Spwan(%b)
    */
    // 1. Get Lambda from apply expression
    auto lambda = apply.GetOperand(2U);
    CJC_ASSERT(lambda->GetType()->IsFunc());

    // 2. Replace spawn and remove useless node
    auto futureExpression = futureValue.GetExpr();
    apply.RemoveSelfFromBlock();
    futureValue.ReplaceWith(*lambda, spawnExpr.GetParentBlock()->GetParentBlockGroup());
    futureExpression->RemoveSelfFromBlock();
    if (executeClosure == nullptr) {
        executeClosure = GetExecureClosureFunc();
        CJC_NULLPTR_CHECK(executeClosure);
    }
    spawnExpr.SetExecuteClosure(*executeClosure);
}

std::pair<LocalVar*, Apply*> RedundantFutureRemoval::CheckSpawnWithFuture(Expression& expr) const
{
    if (expr.GetExprKind() != ExprKind::SPAWN) {
        return {nullptr, nullptr};
    }
    auto spawnExpr = StaticCast<Spawn*>(&expr);
    if (spawnExpr->IsExecuteClosure()) {
        return {nullptr, nullptr};
    }
    auto spawnOperand = spawnExpr->GetFuture();
    if (!spawnOperand->IsLocalVar()) {
        return {nullptr, nullptr};
    }
    auto localFuture = StaticCast<LocalVar*>(spawnOperand);
    auto users = localFuture->GetUsers();
    std::unordered_set<Expression*> usersSet(users.begin(), users.end());
    if (usersSet.size() == 3U) {
        // if spawn and future debug is only users of future, then optimize.
        // future would have exactly three users: apply future, as a paramter in spawn and debug
        usersSet.erase(localFuture->GetDebugExpr());
    }
    if (usersSet.size() == 2U) {
        // if spawn is only user of future, then optimize.
        // future would have exactly two users: apply future and use in spawn
        usersSet.erase(spawnExpr);
    }
    if (usersSet.size() != 1) {
        return {nullptr, nullptr};
    }
    // optimize spawn if only apply is left
    auto apply = *usersSet.begin();
    CJC_ASSERT(apply->GetExprKind() == ExprKind::APPLY);
    return {localFuture, StaticCast<Apply*>(apply)};
}

} // namespace Cangjie::CHIR