// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_USERMEMORYUSAGE_H
#define CANGJIE_USERMEMORYUSAGE_H

#include <string>
#include <unordered_map>
#include <vector>

#include "UserBase.h"

namespace Cangjie {
class UserMemoryUsage : public UserBase {
public:
    UserMemoryUsage() = default;
    ~UserMemoryUsage() override
    {
        OutputResult();
    }
    static UserMemoryUsage& Instance()
    {
        static UserMemoryUsage single{};
        return single;
    }

    void Start(const std::string& title, const std::string& subtitle, const std::string& desc);
    void Stop(const std::string& title, const std::string& subtitle, const std::string& desc);

private:
    std::string GetJson() const override;
    std::string GetSuffix() const final
    {
        return ".mem.prof";
    }
    /**
     * @brief get current process(cjc)'s memory usage at callsite
     *
     * @return float
     */
    static float Sampling();

    struct Info {
        std::string title;
        std::string subtitle;
        std::string desc;
        float start{0.};
        float end{0.};
        explicit Info(std::string title, std::string subtitle, std::string desc, float start)
            : title(std::move(title)), subtitle(std::move(subtitle)), desc(std::move(desc)), start(start)
        {
        }
    };

    std::vector<std::string> titleOrder;
    std::unordered_map<std::string, std::vector<Info>> titleInfoMap;
};
} // namespace Cangjie

#endif // CANGJIE_USERMEMORYUSAGE_H
