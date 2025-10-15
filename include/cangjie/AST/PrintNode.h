// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares AST and related data print apis.
 */

#ifndef CANGJIE_AST_PRINTNODE_H
#define CANGJIE_AST_PRINTNODE_H

#include <string>
#include <iostream>

#include "cangjie/AST/Node.h"

namespace Cangjie {
void PrintNode(
    Ptr<const AST::Node> node, unsigned indent = 0, const std::string& addition = "", std::ostream& stream = std::cout);
} // namespace Cangjie

#endif // CANGJIE_AST_PRINTNODE_H
