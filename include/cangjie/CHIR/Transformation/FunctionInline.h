// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares FunctionInline class for CHIR
 */

#ifndef CANGJIE_CHIR_TRANSFORMATION_FUNCTION_INLINE_H
#define CANGJIE_CHIR_TRANSFORMATION_FUNCTION_INLINE_H

#include "cangjie/CHIR/CHIRBuilder.h"
#include "cangjie/CHIR/Expression/Terminator.h"
#include "cangjie/CHIR/Package.h"
#include "cangjie/CHIR/Utils.h"
#include "cangjie/Option/Option.h"

namespace Cangjie::CHIR {
/**
 * CHIR Opt Pass: do function inline for CHIR IR.
 */
class FunctionInline {
public:
    /**
     * @brief constructor for function inline pass
     * @param builder CHIR builder for generating IR.
     * @param optLevel optimization level from Cangjie inputs.
     * @param debug flag whether print debug log.
     */
    FunctionInline(CHIRBuilder& builder, const GlobalOptions::OptimizationLevel& optLevel, bool debug)
        : builder(builder), optLevel(optLevel), debug(debug)
    {
    }

    /**
     * @brief Main process to do function inline.
     * @param func func to do function inline.
     */
    void Run(Func& func);

    /**
     * @brief Get effect map after this pass.
     * @return effect map affected by this pass.
     */
    const OptEffectCHIRMap& GetEffectMap() const;

    /**
     * @brief Main process to do function inline per apply.
     * @param apply apply expression to do function inline.
     * @param name pass name to pring log.
     */
    void DoFunctionInline(const Apply& apply, const std::string& name);

private:
    bool CheckCanRewrite(const Apply& apply);
    void RecordEffectMap(const Apply& apply);
    void ReplaceFuncResult(LocalVar* resNew, LocalVar* resOld);

    std::pair<BlockGroup*, LocalVar*> CloneBlockGroupForInline(
        const BlockGroup& other, Func& parentFunc, const Apply& apply);

    void SetGroupDebugLocation(BlockGroup& group, const DebugLocation& loc);

    void InlineImpl(BlockGroup& bg);

    CHIRBuilder& builder;
    const GlobalOptions::OptimizationLevel& optLevel;
    bool debug{false};
    Func* globalFunc{nullptr};
    std::unordered_map<Func*, size_t> inlinedCountMap;
    std::unordered_map<Func*, size_t> funcSizeMap;
    const std::string optName{"Function Inline"};
    OptEffectCHIRMap effectMap;
};
} // namespace Cangjie::CHIR

#endif
