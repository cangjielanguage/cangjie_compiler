// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_TRANSFORMATION_MERGE_BLOCKS_H
#define CANGJIE_CHIR_TRANSFORMATION_MERGE_BLOCKS_H

#include "cangjie/Option/Option.h"
#include "cangjie/CHIR/Package.h"
#include "cangjie/CHIR/Value.h"

namespace Cangjie::CHIR {
/**
 * CHIR Opt Pass: do block merge for CHIR IR.
 */
class MergeBlocks {
public:
    /**
     * @brief constructor for mergin blocks.
     */
    explicit MergeBlocks() = default;

    /**
     * @brief Main process to do block merge.
     * @param package package to do optimization.
     * @param builder CHIR builder for generating IR.
     * @param opts global options from Cangjie inputs.
     */
    static void RunOnPackage(const Package& package, CHIRBuilder& builder, const GlobalOptions& opts);

    /**
     * @brief Main process to do block merge per func.
     * @param body func body to merge blocks
     * @param builder CHIR builder for generating IR.
     * @param opts global options from Cangjie inputs.
     */
    static void RunOnFunc(const BlockGroup& body, CHIRBuilder& builder, const GlobalOptions& opts);
};
} // namespace Cangjie::CHIR

#endif