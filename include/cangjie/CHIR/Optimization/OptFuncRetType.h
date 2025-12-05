// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_OPT_FUNC_RET_TYPE_H
#define CANGJIE_CHIR_OPT_FUNC_RET_TYPE_H

#include "cangjie/CHIR/IR/CHIRBuilder.h"

namespace Cangjie::CHIR {

class OptFuncRetType {
public:
    explicit OptFuncRetType(Package& package, CHIRBuilder& builder);
    
    /**
     * @brief Optimize functions by converting Unit return type to Void.
     *
     * This optimization pass converts functions with Unit return type to Void return type.
     * Unit and Void are semantically equivalent (both represent no meaningful return value),
     * but certain functions (constructors, finalizers, global variable initializers) should
     * explicitly return Void for consistency and code generation purposes.
     *
     * The optimization performs the following steps for each affected function:
     * 1. Replaces the function's return value with nullptr (changing return type to Void).
     * 2. Updates all call sites (Apply and ApplyWithException expressions) to use the
     *    new return type, ensuring type consistency throughout the IR.
     *
     * This pass only processes functions that should return Void according to
     * ReturnTypeShouldBeVoid() (constructors, finalizers, global var init functions).
     */
    void Unit2Void();

private:
    Package& package;
    CHIRBuilder& builder;
};

} // namespace Cangjie::CHIR

#endif