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
 * CHIR Opt Pass: reuse successful GetOrThrow results and remove redundant calls.
 */
class RedundantGetOrThrowElimination {
public:
    /**
     * @brief constructor for GetOrThrow function elimination.
     * @param builder CHIR builder used to replace redundant try-apply terminators.
     */
    explicit RedundantGetOrThrowElimination(CHIRBuilder& builder);

    /**
     * @brief Main process to do GetOrThrow elimination.
     * @param package package to do optimization.
     */
    void RunOnPackage(const Ptr<const Package>& package) const;

private:
    void RunOnFunc(const Ptr<const Function>& func) const;

    CHIRBuilder& builder;
};
} // namespace Cangjie::CHIR

#endif
