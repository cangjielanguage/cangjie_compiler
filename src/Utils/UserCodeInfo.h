// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_USERCODEINFO_H
#define CANGJIE_USERCODEINFO_H

#include <list>
#include <string>

#include "UserBase.h"

namespace Cangjie {
class UserCodeInfo : public UserBase {
public:
    UserCodeInfo() = default;
    ~UserCodeInfo() override
    {
        OutputResult();
    }

    static UserCodeInfo& Instance()
    {
        static UserCodeInfo single{};
        return single;
    }
    void RecordInfo(const std::string& item, int64_t value);

private:
    std::string GetJson() const override;
    std::string GetSuffix() const final
    {
        return ".info.prof";
    }
    std::list<std::pair<std::string, int64_t>> codeInfo;
};
} // namespace Cangjie

#endif // CANGJIE_USERCODEINFO_H
