// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file instantiate functions for CHIR pass Function Inline and Devirtualization
 */

#ifndef CANGJIE_CHIR_TRANSFORMATION_BLOCK_GROUP_COPY_HELPER_H
#define CANGJIE_CHIR_TRANSFORMATION_BLOCK_GROUP_COPY_HELPER_H

#include "cangjie/CHIR/CHIRBuilder.h"
#include "cangjie/CHIR/Expression/Terminator.h"

namespace Cangjie::CHIR {
/**
 * helper class for copying blockgroup, do instantiation if instMap is set from apply
 * using in two pass now:
 *  1. devirtalizaion
 *  2. function inline
 */
class BlockGroupCopyHelper {
public:
    /**
     * @brief constructor of block group copy helper class
     * @param builder CHIR builder for generating IR
     */
    explicit BlockGroupCopyHelper(CHIRBuilder& builder) : builder(builder)
    {
    }

    /**
     * @brief main entry for copying block group, return result block group and its return value.
     * @param other block group copy from
     * @param parentFunc parent func where copy block in
     * @return
     */
    std::pair<BlockGroup*, LocalVar*> CloneBlockGroup(const BlockGroup& other, Func& parentFunc);

    /**
     * @brief get instantiation map from apply call, using for function instantiation.
     * @param apply input apply expression to get instantiation map
     */
    void GetInstMapFromApply(const Apply& apply);

    /**
     * @brief replace value with extra value map, such as parameter value map from old block group.
     * @param block block to replace value
     * @param valueMap extra value map to replace value
     */
    void SubstituteValue(Ptr<BlockGroup> block, std::unordered_map<Value*, Value*>& valueMap);
private:
    void InstBlockGroup(Ptr<BlockGroup> group);

    static void CollectValueMap(const Lambda& oldLambda, const Lambda& newLambda,
        std::unordered_map<Value*, Value*>& valueMap, std::unordered_set<Expression*>& newDebugs);
    static void CollectValueMap(const Block& oldBlk, const Block& newBlk,
        std::unordered_map<Value*, Value*>& valueMap, std::unordered_set<Expression*>& newDebugs);
    static void CollectValueMap(const BlockGroup& oldBG, const BlockGroup& newBG,
        std::unordered_map<Value*, Value*>& valueMap, std::unordered_set<Expression*>& newDebugs);

    static void ReplaceExprOperands(const Block& block, const std::unordered_map<Value*, Value*>& valueMap);
    static void ReplaceExprOperands(const BlockGroup& bg, const std::unordered_map<Value*, Value*>& valueMap);

    // map for block group instantiation
    std::unordered_map<const GenericType*, Type*> instMap;
    // mark this type if need replace this type
    Type* thisType = nullptr;
    CHIRBuilder& builder;
};

void FixCastProblemAfterInst(Ptr<BlockGroup> group, CHIRBuilder& builder);
}  // namespace Cangjie::CHIR

#endif
