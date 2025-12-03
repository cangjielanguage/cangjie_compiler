// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_TRANSFORMATION_REDUNDANT_GETORTHROW_ELIMINATION_H
#define CANGJIE_CHIR_TRANSFORMATION_REDUNDANT_GETORTHROW_ELIMINATION_H

#include "cangjie/CHIR/IR/Package.h"
#include "cangjie/CHIR/IR/Value/Value.h"

namespace Cangjie::CHIR {
/**
 * CHIR Opt Pass: replace GetOrThrow function with value directly.
 */
class RedundantGetOrThrowElimination {
public:
    /**
     * @brief constructor for GetOrThrow function elimination.
     */
    explicit RedundantGetOrThrowElimination();

    /**
     * @brief Main process to do GetOrThrow elimination.
     * @param package package to do optimization.
     * @param isDebug flag whether print debug log.
     */
    void RunOnPackage(const Ptr<const Package>& package, bool isDebug) const;

private:
    void RunOnFunc(const Ptr<const Func>& func, bool isDebug) const;
};
} // namespace Cangjie::CHIR

#endif
