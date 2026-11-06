// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Optimization/RedundantGetOrThrowElimination.h"

#include "cangjie/CHIR/Analysis/Engine.h"
#include "cangjie/CHIR/Analysis/GetOrThrowResultAnalysis.h"
#include "cangjie/CHIR/Analysis/Utils.h"

using namespace Cangjie::CHIR;

RedundantGetOrThrowElimination::RedundantGetOrThrowElimination(CHIRBuilder& builder) : builder(builder)
{
}

void RedundantGetOrThrowElimination::RunOnPackage(const Ptr<const Package>& package) const
{
    for (auto func : package->GetGlobalFuncsWithBody()) {
        RunOnFunc(func);
    }
}

void RedundantGetOrThrowElimination::RunOnFunc(const Ptr<const Function>& func) const
{
    auto analysis = std::make_unique<GetOrThrowResultAnalysis>(func);
    auto engine = Engine<GetOrThrowResultDomain>(func, std::move(analysis));
    auto result = engine.IterateToFixpoint();
    CJC_NULLPTR_CHECK(result);

    std::vector<ApplyBase*> redundantCalls;
    const auto replaceResult = [func, &redundantCalls](const GetOrThrowResultDomain& state, Expression* expr) {
        if (!IsGetOrThrowFunction(*expr)) {
            return;
        }
        auto apply = StaticCast<ApplyBase*>(expr);
        auto arg = apply->GetArgs()[0];
        if (auto result = state.CheckGetOrThrowResult(arg); result) {
            apply->GetResult()->ReplaceWith(*result->GetResult(), func->GetBody());
            redundantCalls.emplace_back(apply);
        }
    };

    const auto actionBeforeVisitExpr = [&replaceResult](const GetOrThrowResultDomain& state, Expression* expr, size_t) {
        replaceResult(state, expr);
    };

    const auto actionAfterVisitExpr = [](const GetOrThrowResultDomain&, Expression*, size_t) {};

    const auto actionOnTerminator = [&replaceResult](
        const GetOrThrowResultDomain& state, Expression* expr, std::optional<Block*>) { replaceResult(state, expr); };

    result->VisitWith(actionBeforeVisitExpr, actionAfterVisitExpr, actionOnTerminator);

    // Keep the analysed CFG intact until all expressions have been visited.
    for (auto apply : redundantCalls) {
        if (auto tryApply = DynamicCast<TryApply*>(apply)) {
            auto parent = tryApply->GetParentBlock();
            auto success = tryApply->GetSuccessBlock();
            auto loc = tryApply->GetDebugLocation();
            tryApply->RemoveSelfFromBlock();
            parent->AppendExpression(builder.CreateTerminator<GoTo>(loc, success, parent));
        } else {
            apply->RemoveSelfFromBlock();
        }
    }
}
