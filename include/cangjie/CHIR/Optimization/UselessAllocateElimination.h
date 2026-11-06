// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_TRANSFORMATION_USELESS_ALLOCATE_ELIMINATION_H
#define CANGJIE_CHIR_TRANSFORMATION_USELESS_ALLOCATE_ELIMINATION_H

#include "cangjie/CHIR/IR/Package.h"
#include "cangjie/CHIR/IR/Value/Value.h"

namespace Cangjie::CHIR {
class CHIRBuilder;

/**
 * CHIR Opt Pass: delete all useless allocation in CHIR IR.
 */
class UselessAllocateElimination {
public:
    /**
     * @brief Main process to do useless allocate elimination.
     * @param package package to do optimization.
     * @param builder CHIR builder used to replace try-allocate terminators.
     */
    static void RunOnPackage(const Package& package, CHIRBuilder& builder);
private:
    static void RunOnFunc(const Function& func, CHIRBuilder& builder);
};
} // namespace Cangjie::CHIR

#endif
