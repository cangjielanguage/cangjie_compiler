// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Checker/ConstSafetyCheck.h"
#include "cangjie/CHIR/Analysis/Engine.h"

using namespace Cangjie::CHIR;

ConstSafetyCheck::ConstSafetyCheck(ConstAnalysisWrapper* constAnalysisWrapper) : analysisWrapper(constAnalysisWrapper)
{
}

void ConstSafetyCheck::RunOnPackage(const Package& package, size_t threadNum) const
{
    if (threadNum == 1) {
        for (auto func : package.GetGlobalFuncs()) {
            RunOnFunc(func);
        }
    } else {
        Utils::TaskQueue taskQueue(threadNum);
        for (auto func : package.GetGlobalFuncs()) {
            taskQueue.AddTask<void>([this, func]() { return RunOnFunc(func); });
        }
        taskQueue.RunAndWaitForAllTasksCompleted();
    }
}

void ConstSafetyCheck::RunOnFunc(const Func* func) const
{
    auto result = analysisWrapper->CheckFuncResult(func);
    CJC_ASSERT(result);

    const auto actionBeforeVisitExpr = [](const ConstDomain&, Expression*, size_t) {};
    const auto actionAfterVisitExpr = [](const ConstDomain&, Expression*, size_t) {};
    const auto actionOnTerminator = [](const ConstDomain&, Terminator*, std::optional<Block*>) {};

    result->VisitWith(actionBeforeVisitExpr, actionAfterVisitExpr, actionOnTerminator);
}