// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_TRANSFORMATION_GET_REF_TO_ARRAY_ELEM_H
#define CANGJIE_CHIR_TRANSFORMATION_GET_REF_TO_ARRAY_ELEM_H

#include "cangjie/CHIR/IR/CHIRBuilder.h"
#include "cangjie/CHIR/IR/Value/Value.h"

namespace Cangjie::CHIR {
/**
 * CHIR Opt Pass: ARRAY_GET_UNCHECKED intrinsic optimization.
 */
class GetRefToArrayElem {
public:
    /**
     * @brief Main process to ARRAY_GET_UNCHECKED intrinsic optimization.
     * @param package package to do optimization.
     * @param builder CHIR builder for generating IR.
     */
    static void RunOnPackage(const Package& package, CHIRBuilder& builder);
private:
    static void RunOnFunc(const Func& func, CHIRBuilder& builder);
};
} // namespace Cangjie::CHIR

#endif