// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_TRANSFORMATION_UNIT_UNIFY_H
#define CANGJIE_CHIR_TRANSFORMATION_UNIT_UNIFY_H

#include "cangjie/CHIR/IR/CHIRBuilder.h"
#include "cangjie/CHIR/IR/Expression/Terminator.h"
#include "cangjie/CHIR/IR/Package.h"
#include "cangjie/CHIR/IR/Value/Value.h"

namespace Cangjie::CHIR {
/**
 * @brief unify all used units to one in a function.
 *    eliminate side effects for unit variables
 *
 * before pass:
 *     %0: Unit = Apply(@_CN7default3fooEv)
 *     %1: Void = Apply(@_CN7default3foo2Ev, %0)
 *     %2: Void = Apply(@_CN7default3foo3Ev, %0)
 * after pass:
 *     %3: Unit = Constant(unit)
 *     %0: Unit = Apply(@_CN7default3fooEv)
 *     %1: Void = Apply(@_CN7default3foo2Ev, %3)  // change used unit to const value %3
 *     %2: Void = Apply(@_CN7default3foo3Ev, %3)  // change used unit to const value %3
 */
class UnitUnify {
public:
    /**
     * @brief constructor to unify all used units to one in a function.
     * @param builder CHIR builder for generating IR.
     */
    explicit UnitUnify(CHIRBuilder& builder);

    /**
     * @brief Main process to unify all used units to one in a function.
     * @param package package to do optimization.
     * @param isDebug flag whether print debug log.
     */
    void RunOnPackage(const Ptr<const Package>& package, bool isDebug);

private:
    void RunOnFunc(const Ptr<Func>& func, bool isDebug);

    void LoadOrCreateUnit(Ptr<Constant>& constant, const Ptr<BlockGroup>& group);

    CHIRBuilder& builder;
};
} // namespace Cangjie::CHIR

#endif