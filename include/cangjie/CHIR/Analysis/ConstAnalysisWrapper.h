// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_ANALYSIS_CONST_ANALYSISWRAPPER_H
#define CANGJIE_CHIR_ANALYSIS_CONST_ANALYSISWRAPPER_H

#include "cangjie/CHIR/Analysis/Engine.h"
#include "cangjie/CHIR/Analysis/ConstAnalysis.h"
#include "cangjie/CHIR/Package.h"
#include "cangjie/Utils/TaskQueue.h"

#include <future>

namespace Cangjie::CHIR {
namespace {
// Huge block size, do nothing
const size_t OVERHEAD_BLOCK_SIZE = 1000U;
// Big size, use active pool
const size_t USE_ACTIVE_BLOCK_SIZE = 300U;

/// Get all block size from a lambda, get 0 if not a lambda.
size_t GetBlockSize(const Expression& expr)
{
    size_t blockSize = 0;
    if (expr.GetExprKind() != ExprKind::LAMBDA) {
        return blockSize;
    }
    auto lambdaBody = Cangjie::StaticCast<const Lambda&>(expr).GetBody();
    blockSize += lambdaBody->GetBlocks().size();
    auto postVisit = [&blockSize](Expression& e) {
        blockSize += GetBlockSize(e);
        return VisitResult::CONTINUE;
    };
    Visitor::Visit(*lambdaBody, postVisit);
    return blockSize;
}

/// count all blocks in func, including lambda expr.
size_t CountBlockSize(const Func& func)
{
    size_t blockSize = func.GetBody()->GetBlocks().size();
    if (blockSize > OVERHEAD_BLOCK_SIZE) {
        return OVERHEAD_BLOCK_SIZE + 1;
    }
    for (auto block : func.GetBody()->GetBlocks()) {
        for (auto e : block->GetExpressions()) {
            blockSize += GetBlockSize(*e);
            if (blockSize > OVERHEAD_BLOCK_SIZE) {
                return OVERHEAD_BLOCK_SIZE + 1;
            }
        }
    }
    return blockSize;
}
}

/**
 * @brief wrapper class of constant analysis pass, using to do parallel or check works.
 */
class ConstAnalysisWrapper {
public:
    /**
     * @brief wrapper of const analysis.
     * @param builder CHIR builder for generating IR.
     */
    explicit ConstAnalysisWrapper(CHIRBuilder& builder) : builder(builder)
    {
    }

    /**
     * @brief main method to analysis from wrapper class.
     * @tparam Args the args type of analysis.
     * @param package package to do optimization.
     * @param isDebug flag whether print debug log.
     * @param threadNum thread num to do analysis
     * @param args args of analysis
     */
    template <typename... Args>
    void RunOnPackage(const Package* package, bool isDebug, size_t threadNum, Args&&... args)
    {
        compileThreadNum = threadNum;
        if (threadNum == 1) {
            RunOnPackageInSerial(package, isDebug, std::forward<Args>(args)...);
        } else {
            RunOnPackageInParallel(package, isDebug, threadNum, std::forward<Args>(args)...);
        }
    }

    /**
     * @brief main method to analysis from wrapper class per function.
     * @tparam Args the args type of analysis.
     * @param func function CHIR IR to do optimization.
     * @param isDebug flag whether print debug log.
     * @param args args of analysis
     * @return result of analysis per function
     */
    template <typename... Args>
    std::unique_ptr<Results<ConstDomain>> RunOnFunc(const Func* func, bool isDebug, Args&&... args)
    {
        auto analysis =
            std::make_unique<ConstAnalysis<ConstStatePool>>(func, builder, isDebug, std::forward<Args>(args)...);
        auto engine = Engine<ConstDomain>(func, std::move(analysis));
        return engine.IterateToFixpoint();
    }

    /**
     * @brief main method to analysis from wrapper class per function using pool domain.
     * @tparam Args the args type of analysis.
     * @param func function CHIR IR to do optimization.
     * @param isDebug flag whether print debug log.
     * @param args args of analysis
     * @return result of analysis per function
     */
    template <typename... Args>
    std::unique_ptr<Results<ConstPoolDomain>> RunOnFuncWithPool(const Func* func, bool isDebug, Args&&... args)
    {
        auto analysis =
            std::make_unique<ConstAnalysis<ConstActivePool>>(func, builder, isDebug, std::forward<Args>(args)...);
        auto engine = Engine<ConstPoolDomain>(func, std::move(analysis));
        return engine.IterateToFixpoint();
    }

    /**
     * @brief return result of analysis for certain function
     * @param func function to return analysis result
     * @return analysis result
     */
    std::optional<Results<ConstDomain>*> CheckFuncResult(const Func* func)
    {
        if (auto it = resultsMap.find(func); it != resultsMap.end()) {
            return it->second.get();
        } else if (funcWithPoolDomain.count(func) != 0) {
            // pool domain result only using for analysis.
            return nullptr;
        } else {
            return nullptr;
        }
    }

    /**
     * @brief clear analysis result
     */
    void InvalidateAllAnalysisResults()
    {
        funcWithPoolDomain.clear();
        resultsMap.clear();
    }

    /**
     * @brief clear analysis result of certain function
     * @param func function to clear analysis result
     * @return whether clear is happened
     */
    bool InvalidateAnalysisResult(const Func* func)
    {
        if (auto it = resultsMap.find(func); it != resultsMap.end()) {
            resultsMap.erase(it);
            return true;
        }
        if (auto it = funcWithPoolDomain.find(func); it != funcWithPoolDomain.end()) {
            funcWithPoolDomain.erase(it);
            return true;
        }
        return false;
    }

private:
    std::optional<bool> JudgeUsingPool(const Func* func)
    {
        auto size = CountBlockSize(*func);
        if (size > OVERHEAD_BLOCK_SIZE) {
            return std::nullopt;
        }
        return size > USE_ACTIVE_BLOCK_SIZE;
    }

    template <typename... Args>
    void RunOnPackageInSerial(const Package* package, bool isDebug, Args&&... args)
    {
        SetUpGlobalVarState(*package, isDebug, std::forward<Args>(args)...);
        for (auto func : package->GetGlobalFuncs()) {
            if (!ShouldBeAnalysed(*func)) {
                continue;
            }
            auto judgeRes = JudgeUsingPool(func);
            if (judgeRes == std::nullopt) {
                // overhead huge size, do nothing
                funcWithPoolDomain.emplace(func);
            } else if (judgeRes.value()) {
                // middle size, use acitve pool
                if (auto res = RunOnFuncWithPool(func, isDebug, std::forward<Args>(args)...)) {
                    funcWithPoolDomain.emplace(func);
                }
            } else {
                // normal const analysis
                if (auto res = RunOnFunc(func, isDebug, std::forward<Args>(args)...)) {
                    resultsMap.emplace(func, std::move(res));
                }
            }
        }
    }

    template <typename... Args>
    void RunOnPackageInParallel(const Package* package, bool isDebug, size_t threadNum, Args&&... args)
    {
        SetUpGlobalVarState(*package, isDebug, std::forward<Args>(args)...);
        Utils::TaskQueue taskQueue(threadNum);
        using ResTy = std::unique_ptr<Results<ConstDomain>>;
        using ResTyPool = std::unique_ptr<Results<ConstPoolDomain>>;
        std::vector<Cangjie::Utils::TaskResult<ResTy>> results;
        std::vector<Cangjie::Utils::TaskResult<ResTyPool>> resultsPool;
        for (auto func : package->GetGlobalFuncs()) {
            if (!ShouldBeAnalysed(*func)) {
                continue;
            }
            auto judgeRes = JudgeUsingPool(func);
            if (judgeRes == std::nullopt) {
                // overhead huge size, do nothing
                funcWithPoolDomain.emplace(func);
            } else if (judgeRes.value()) {
                // middle size, use acitve pool
                resultsPool.emplace_back(taskQueue.AddTask<ResTyPool>(
                    [func, isDebug, &args..., this]() { return RunOnFuncWithPool(func, isDebug, std::forward<Args>(args)...); },
                    // Roughly use the number of Blocks as the cost of task weight
                    func->GetBody()->GetBlocks().size()));
            } else {
                // normal const analysis
                results.emplace_back(taskQueue.AddTask<ResTy>(
                    [func, isDebug, &args..., this]() { return RunOnFunc(func, isDebug, std::forward<Args>(args)...); },
                    func->GetBody()->GetBlocks().size()));
            }
        }

        taskQueue.RunAndWaitForAllTasksCompleted();

        for (auto& result : results) {
            if (auto res = result.get()) {
                resultsMap.emplace(res->func, std::move(res));
            }
        }
        for (auto& result: resultsPool) {
            if (auto res = result.get()) {
                funcWithPoolDomain.emplace(res->func);
            }
        }
    }

    bool ShouldBeAnalysed(const Func& func)
    {
        if (resultsMap.find(&func) != resultsMap.end() || funcWithPoolDomain.find(&func) != funcWithPoolDomain.end()) {
            return false;
        }
        return ConstAnalysis<ConstStatePool>::Filter(func);
    }

    template <typename... Args> void SetUpGlobalVarState(const Package& package, bool isDebug, Args&&... args)
    {
        ConstAnalysis<ConstStatePool>::InitialiseLetGVState(package, builder);
        for (auto gv : package.GetGlobalVars()) {
            if (auto init = gv->GetInitFunc();
                gv->TestAttr(Attribute::READONLY) && init && resultsMap.find(init) == resultsMap.end()) {
                // Multiple global vars may be initialised in the same function.
                // e.g. let (x, y) = (1, 2)
                resultsMap.emplace(init, RunOnFunc(init, isDebug, std::forward<Args>(args)...));
            }
        }
    }

    std::unordered_map<const Func*, std::unique_ptr<Results<ConstDomain>>> resultsMap;
    std::unordered_set<const Func*> funcWithPoolDomain;
    CHIRBuilder& builder;
    size_t compileThreadNum = 1;
};

} // namespace Cangjie::CHIR

#endif