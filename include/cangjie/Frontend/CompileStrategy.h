// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the CompileStrategy related classes, which provide real compile ablility.
 */

#ifndef CANGJIE_FRONTEND_COMPILESTRATEGY_H
#define CANGJIE_FRONTEND_COMPILESTRATEGY_H

#include <future>
#include <memory>
#include <queue>
#include <unordered_set>

namespace Cangjie {
class CompilerInstance;
class TypeChecker;

/**
 * Strategy type.
 */
enum class StrategyType {
    DEFAULT,             /**< Default compile strategy. */
    FULL_COMPILE,        /**< Full compile strategy. */
    INCREMENTAL_COMPILE, /**< Increamental compile strategy. */
};

/**
 * Compile Strategy use different compilation unit to compile.
 */
class CompileStrategy {
public:
    explicit CompileStrategy(CompilerInstance* ci) : ci(ci)
    {
    }
    virtual ~CompileStrategy()
    {
    }
    virtual bool Parse() = 0;
    bool ConditionCompile() const;
    void DesugarAfterSema() const;
    bool ImportPackages() const;
    bool MacroExpand() const;
    virtual bool Sema() = 0;
    bool OverflowStrategy() const;
    StrategyType type{StrategyType::DEFAULT};

protected:
    /**
     * Desugar Syntactic sugar.
     */
    void PerformDesugar() const;
    /**
     * Do TypeCheck and Generic Instantiation.
     */
    void TypeCheck() const;
    CompilerInstance* ci = nullptr;
    // A collection of file ids, used to determine whether the id is conflicted.
    std::unordered_set<unsigned int> fileIds;

private:
    /**
     * @brief Do merge custom annotations from '.cj.d' file to ast tree.
     * @details If the cjd file is in the same directory as the cjo file and the file name is the same as it,
     *          this function will do parse and macroexpand for it.
     * @note This function will modify the ast tree in compiler instance.
     */
    void ParseAndMergeCjds() const;
};

/**
 * Full Compile Strategy will compile the whole module.
 */
class FullCompileStrategy : public CompileStrategy {
public:
    explicit FullCompileStrategy(CompilerInstance* ci);
    ~FullCompileStrategy();
    bool Parse() override;
    bool Sema() override;

private:
    friend class FullCompileStrategyImpl;
    class FullCompileStrategyImpl* impl;
};
} // namespace Cangjie
#endif // CANGJIE_FRONTEND_COMPILESTRATEGY_H
