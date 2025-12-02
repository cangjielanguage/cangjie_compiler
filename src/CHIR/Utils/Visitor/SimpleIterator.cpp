// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the SimpleIterator Class in CHIR.
 */

#include "cangjie/CHIR/Utils/Visitor/SimpleIterator.h"
#include "cangjie/CHIR/IR/Expression/Terminator.h"
#include "cangjie/CHIR/IR/Value/Value.h"
#include "cangjie/CHIR/Utils/CHIRCasting.h"

using namespace Cangjie::CHIR;

std::vector<BlockGroup*> SimpleIterator::Iterate(const Expression& expr)
{
    auto kind{expr.GetExprKind()};
    if (kind == ExprKind::IF || kind == ExprKind::LOOP || Is<ForIn>(expr)) {
        return expr.GetBlockGroups();
    }
    return {};
}

std::vector<Block*> SimpleIterator::Iterate(const BlockGroup& blockGroup)
{
    return blockGroup.GetBlocks();
}

std::vector<Expression*> SimpleIterator::Iterate(const Block& block)
{
    return block.GetExpressions();
}