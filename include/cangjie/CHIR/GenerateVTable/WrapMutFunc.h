// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file generate mut func wrapper
 */

#ifndef CANGJIE_CHIR_WRAP_MUT_FUNC_H
#define CANGJIE_CHIR_WRAP_MUT_FUNC_H

#include "cangjie/CHIR/CHIRBuilder.h"
#include "cangjie/CHIR/Type/ExtendDef.h"

namespace Cangjie {
namespace CHIR {
class WrapMutFunc {
public:
    WrapMutFunc(CHIRBuilder& b);

    /**
     * @brief Create wrapper func for mut method.
     *
     * @param customTypeDef Visit all mut methods in this CustomTypeDef.
     */
    void Run(CustomTypeDef& customTypeDef);

    /**
     * @brief Return cache info, map<mangled name, func pointer>.
     */
    std::unordered_map<std::string, FuncBase*>&& GetWrappers();

private:
    void CreateMutFuncWrapper(FuncBase* rawFunc, CustomTypeDef& curDef, ClassType& srcClassTy);

    CHIRBuilder& builder;
    std::unordered_map<std::string, FuncBase*> wrapperFuncs;
};
}
}
#endif