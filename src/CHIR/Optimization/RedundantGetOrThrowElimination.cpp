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

RedundantGetOrThrowElimination::RedundantGetOrThrowElimination()
{
}

void RedundantGetOrThrowElimination::RunOnPackage(const Ptr<const Package>& package, bool isDebug) const
{
    for (auto func : package->GetGlobalFuncs()) {
        RunOnFunc(func, isDebug);
    }
}

void RedundantGetOrThrowElimination::RunOnFunc(const Ptr<const Func>& func, bool isDebug) const
{
    auto analysis = std::make_unique<GetOrThrowResultAnalysis>(func, isDebug);
    auto engine = Engine<GetOrThrowResultDomain>(func, std::move(analysis));
    auto result = engine.IterateToFixpoint();
    CJC_NULLPTR_CHECK(result);

    const auto actionBeforeVisitExpr = [func, isDebug](const GetOrThrowResultDomain& state, Expression* expr, size_t) {
        if (!IsGetOrThrowFunction(*expr)) {
            return;
        }
        auto apply = StaticCast<Apply*>(expr);
        auto arg = apply->GetArgs()[0];
        if (auto result = state.CheckGetOrThrowResult(arg); result) {
            apply->GetResult()->ReplaceWith(*result->GetResult(), func->GetBody());
            if (isDebug) {
                std::string message = "[RGetOtThrowE] The usages of the result of getOrThrow" +
                    ToPosInfo(apply->GetDebugLocation()) + " have been replaced by the value" +
                    ToPosInfo(result->GetDebugLocation()) + "\n";
                std::cout << message;
            }
        }
    };

    const auto actionAfterVisitExpr = [](const GetOrThrowResultDomain&, Expression*, size_t) {};

    const auto actionOnTerminator = [](const GetOrThrowResultDomain&, Terminator*, std::optional<Block*>) {};

    result->VisitWith(actionBeforeVisitExpr, actionAfterVisitExpr, actionOnTerminator);
}
