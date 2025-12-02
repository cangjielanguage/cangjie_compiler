// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/IR/AttributeInfo.h"
#include <iostream>
#include <sstream>

using namespace Cangjie::CHIR;

std::string AttributeInfo::ToString() const
{
    std::stringstream ss;
    for (int attr = static_cast<int>(Attribute::STATIC); attr < static_cast<int>(Attribute::ATTR_END); attr++) {
        if (TestAttr(static_cast<Attribute>(attr))) {
            ss << "[" << ATTR_TO_STRING.at(static_cast<Attribute>(attr)) << "] ";
        }
    }
    return ss.str();
}

void AttributeInfo::Dump() const
{
    std::cout << ToString() << std::endl;
}