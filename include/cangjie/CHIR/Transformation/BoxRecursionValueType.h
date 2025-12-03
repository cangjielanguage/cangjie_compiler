// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_TRANSFORMATION_BOX_RECURSION_VALUE_TYPE_H
#define CANGJIE_CHIR_TRANSFORMATION_BOX_RECURSION_VALUE_TYPE_H

#include "cangjie/CHIR/IR/CHIRBuilder.h"

namespace Cangjie::CHIR {
/**
 * CHIR Normal Pass: add box and unbox between of several certain expressions, such as GetElementRef, tuple.
 */
class BoxRecursionValueType {
public:
    /**
     * @brief constructor for pass to add box and unbox expressions.
     * @param pkg input package.
     * @param builder CHIR builder for generating IR.
     */
    BoxRecursionValueType(Package& pkg, CHIRBuilder& builder);

    /**
     * @brief main process to add box and unbox expressions.
     */
    void Run();

private:
    void CreateBoxTypeForRecursionEnum();
    void CreateBoxTypeForRecursionStruct();
    void InsertBoxAndUnboxExprForRecursionValueType();

    Package& pkg;
    CHIRBuilder& builder;
};
}
#endif
