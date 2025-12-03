// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the Simple Iterator in CHIR.
 */

#ifndef CANGJIE_CHIR_SIMPLEITERATOR_H
#define CANGJIE_CHIR_SIMPLEITERATOR_H

#include <vector>

namespace Cangjie::CHIR {
class Expression;
class Block;
class BlockGroup;
/*
 * @brief Simple Iterator for CHIR node.
 *
 */
class SimpleIterator {
public:
    /**
     * @brief Iterates over an expression and returns a vector of block groups.
     *
     * @param expr The expression to iterate over.
     * @return A vector of block groups.
     */
    static std::vector<BlockGroup*> Iterate(const Expression& expr);
    
    /**
     * @brief Iterates over a block group and returns a vector of blocks.
     *
     * @param blockGroup The block group to iterate over.
     * @return A vector of blocks.
     */
    static std::vector<Block*> Iterate(const BlockGroup& blockGroup);
    
    /**
     * @brief Iterates over a block and returns a vector of expressions.
     *
     * @param block The block to iterate over.
     * @return A vector of expressions.
     */
    static std::vector<Expression*> Iterate(const Block& block);
};

} // namespace Cangjie::CHIR
#endif // CANGJIE_CHIR_SIMPLEITERATOR_H