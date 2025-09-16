// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_TRANSFORMATION_LAMBDA_INLINE_H
#define CANGJIE_CHIR_TRANSFORMATION_LAMBDA_INLINE_H

#include "cangjie/CHIR/CHIRBuilder.h"
#include "cangjie/CHIR/Transformation/FunctionInline.h"
#include "cangjie/Option/Option.h"

namespace Cangjie::CHIR {

/**
 * @brief inline lambda expression if meet condition as blow:
 *   1. only have one consumer as a callee to apply expression.
 *   2. only have one consumer as a parameter to apply expression, which will not escape in new function.
 */
class LambdaInline {
public:
    /**
     * @brief lambda inline constructor.
     * @param builder chir builder to create IR.
     * @param opts options to indicate whether to do optimization.
     */
    LambdaInline(CHIRBuilder& builder, const GlobalOptions& opts);

    /**
     * @brief interface to do lambda inline.
     * @param funcs all lambda functions in the package.
     */
    void InlineLambda(const std::vector<Lambda*>& funcs);

private:
    /// run on single lambda
    void RunOnLambda(Lambda& lambda);

    /// judge whether you can do optimization ob a lambda if it is passed to a new easy function.
    bool IsLambdaPassToEasyFunc(const Lambda& lambda) const;

    const GlobalOptions& opts;
    /// function inline pass
    FunctionInline inlinePass;
};

}  // namespace Cangjie::CHIR


#endif