// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "UserCodeInfo.h"

namespace Cangjie {

void UserCodeInfo::RecordInfo(const std::string& item, int64_t value)
{
    codeInfo.emplace_back(item, value);
}

std::string UserCodeInfo::GetJson() const
{
    std::string output;
    output += "{";
    for (auto& it : codeInfo) {
        output += ("\n   \"" + it.first + "\": " + std::to_string(it.second) + ",");
    }
    if (!codeInfo.empty()) {
        output.pop_back();
    }
    output += "\n}\n";
    return output;
}
} // namespace Cangjie
