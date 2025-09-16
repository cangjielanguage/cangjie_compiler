// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_TRANSFORMATION_FLAT_FORIN_EXPR_H
#define CANGJIE_CHIR_TRANSFORMATION_FLAT_FORIN_EXPR_H

#include "cangjie/CHIR/CHIRContext.h"
#include "cangjie/CHIR/Expression/Terminator.h"
#include "cangjie/CHIR/Package.h"

namespace Cangjie::CHIR {
/**
 * CHIR normaol Pass: mainly flat ForIn Expression, generate standard CHIR IR to replace ForIn Expr.
 */
class FlatForInExpr {
public:
    /**
     * @brief constructor to flat ForIn Expression.
     * @param builder CHIR builder for generating IR.
     */
    explicit FlatForInExpr(CHIRBuilder& builder);

    /**
     * @brief Main process to flat ForIn Expression.
     * @param package package to do optimization.
     */
    void RunOnPackage(const Package& package);

private:
    CHIRBuilder& builder;

    void RunOnFunc(Func& func);

    void RunOnBlockGroup(BlockGroup& blockGroup);

    using ExprIt = std::vector<Expression*>::iterator;
    // for-in-iter before translation
    // %0 = Apply(iterator, for-value)
    // %1 = Allocate(Int64) // delay exit signal
    // %2 = Allocate(Enum-Option<...>) // iterator var
    // %3 = Allocate(Bool) // cond var
    // Store(Constant(0), %1)
    // Store(Tuple(Constant(1)), %2) // init value is None, unused
    // Store(Constant(true), %3) // init value is true, unused
    // %4 = For(%2, %3) {
    //     #2: body
    //         do body things...
    //     #3: latch
    //         %5 = Apply(next, %0)
    //         Store(%5, %2)
    //     #4: cond
    //         %6 = TypeCast(Enum-Option<...>, %5)
    //         %7 = Field(%6, 0)
    //         %8 = Not(%7)
    //         Store(%8, %3)
    // }
    // GoTo(delay-exit-true-block)

    // after translation
    // for-in-iter after translation
    // %0 = Apply(iterator, for-value)
    // %1 = Allocate(Int64) // delay exit signal
    // %2 = Allocate(Enum-Option<...>) // iterator var
    // %3 = Allocate(Bool) // cond var
    // Store(Constant(0), %1)
    // Store(Tuple(Constant(1)), %2) // init value is None, unused
    // Store(Constant(true), %3) // init value is true, unused
    // GoTo(#3)
    // #3: latch
    //     %5 = Apply(next, %0)
    //     Store(%5, %2)
    //     GoTo(#4)
    // #4: cond
    //     %6 = TypeCast(Enum-Option<...>, %5)
    //     %7 = Field(%6, 0)
    //     %8 = Not(%7)
    //     Store(%8, %3)
    //     GoTo(#5) // new block, named jump block
    // #2: body
    //     do body things...
    //     GoTo(#3)
    // #5: jump block
    //     %9 = Load(%3)
    //     Branch(%9, #2, delay-exit-true-block)
    /// \param it the iterator to forIn
    /// \param end the iterator of the end of the block containing the forIn expression
    void FlatternForInExpr(ExprIt it, ExprIt end);
    void FlatternForInClosedRange(ExprIt it, ExprIt end);
};
} // namespace Cangjie::CHIR
#endif
