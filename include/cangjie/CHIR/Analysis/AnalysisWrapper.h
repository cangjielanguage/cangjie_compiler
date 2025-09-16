// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_ANALYSIS_ANALYSISWRAPPER_H
#define CANGJIE_CHIR_ANALYSIS_ANALYSISWRAPPER_H

#include "cangjie/CHIR/Analysis/Engine.h"
#include "cangjie/CHIR/Package.h"
#include "cangjie/Utils/TaskQueue.h"

#include <future>

namespace Cangjie::CHIR {

/// template false type of value analysis.
template <typename T, typename U = void> struct IsValueAnalysis : std::false_type {};

/// template true type of value analysis.
template <typename T> struct IsValueAnalysis<T, std::void_t<typename T::isValueAnalysis>> : std::true_type {};

/**
 * @brief wrapper class of analysis pass, using to do parallel or check works.
 * @tparam TAnalysis analysis to wrapper.
 * @tparam TDomain domain of analysis.
 */
template <typename TAnalysis, typename TDomain,
    typename = std::enable_if_t<std::is_base_of_v<AbstractDomain<TDomain>, TDomain>>,
    typename = std::enable_if_t<std::is_base_of_v<Analysis<TDomain>, TAnalysis>>>
class AnalysisWrapper {
public:
    /**
     * @brief abstract class for CHIR analysis wrapper.
     * @param builder CHIR builder for generating IR.
     */
    explicit AnalysisWrapper(CHIRBuilder& builder) : builder(builder)
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
    std::unique_ptr<Results<TDomain>> RunOnFunc(const Func* func, bool isDebug, Args&&... args)
    {
        auto analysis = std::make_unique<TAnalysis>(func, builder, isDebug, std::forward<Args>(args)...);
        auto engine = Engine<TDomain>(func, std::move(analysis));
        return engine.IterateToFixpoint();
    }

    /**
     * @brief return result of analysis for certain function
     * @param func function to return analysis result
     * @return analysis result
     */
    Results<TDomain>* CheckFuncResult(const Func* func)
    {
        if (auto it = resultsMap.find(func); it != resultsMap.end()) {
            return it->second.get();
        } else {
            return nullptr;
        }
    }

    /**
     * @brief clear analysis result
     */
    void InvalidateAllAnalysisResults()
    {
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
        } else {
            return false;
        }
    }

private:
    template <typename... Args>
    void RunOnPackageInSerial(const Package* package, bool isDebug, Args&&... args)
    {
        if constexpr (IsValueAnalysis<TAnalysis>::value) {
            SetUpGlobalVarState(*package, isDebug, std::forward<Args>(args)...);
        }
        for (auto func : package->GetGlobalFuncs()) {
            if (ShouldBeAnalysed(*func)) {
                if (auto res = RunOnFunc(func, isDebug, std::forward<Args>(args)...)) {
                    resultsMap.emplace(func, std::move(res));
                }
            }
        }
    }

    template <typename... Args>
    void RunOnPackageInParallel(const Package* package, bool isDebug, size_t threadNum, Args&&... args)
    {
        if constexpr (IsValueAnalysis<TAnalysis>::value) {
            SetUpGlobalVarState(*package, isDebug, std::forward<Args>(args)...);
        }
        Utils::TaskQueue taskQueue(threadNum);
        using ResTy = std::unique_ptr<Results<TDomain>>;
        std::vector<Cangjie::Utils::TaskResult<ResTy>> results;
        for (auto func : package->GetGlobalFuncs()) {
            if (ShouldBeAnalysed(*func)) {
                results.emplace_back(taskQueue.AddTask<ResTy>(
                    [func, isDebug, &args..., this]() { return RunOnFunc(func, isDebug, std::forward<Args>(args)...); },
                    // Roughly use the number of Blocks as the cost of task weight
                    func->GetBody()->GetBlocks().size()));
            }
        }

        taskQueue.RunAndWaitForAllTasksCompleted();

        for (auto& result : results) {
            if (auto res = result.get()) {
                resultsMap.emplace(res->func, std::move(res));
            }
        }
    }

    bool ShouldBeAnalysed(const Func& func)
    {
        if constexpr (IsValueAnalysis<TAnalysis>::value) {
            if (resultsMap.find(&func) != resultsMap.end()) {
                return false;
            }
        }
        return TAnalysis::Filter(func);
    }

    template <typename... Args> void SetUpGlobalVarState(const Package& package, bool isDebug, Args&&... args)
    {
        TAnalysis::InitialiseLetGVState(package, builder);
        for (auto gv : package.GetGlobalVars()) {
            if (auto init = gv->GetInitFunc();
                gv->TestAttr(Attribute::READONLY) && init && resultsMap.find(init) == resultsMap.end()) {
                // Multiple global vars may be initialised in the same function.
                // e.g. let (x, y) = (1, 2)
                resultsMap.emplace(init, RunOnFunc(init, isDebug, std::forward<Args>(args)...));
            }
        }
    }

    std::unordered_map<const Func*, std::unique_ptr<Results<TDomain>>> resultsMap;
    CHIRBuilder& builder;
};

} // namespace Cangjie::CHIR

#endif