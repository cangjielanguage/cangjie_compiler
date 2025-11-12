// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_USERTIMER_H
#define CANGJIE_USERTIMER_H

#include <chrono>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

#include "UserBase.h"

namespace Cangjie {
class UserTimer : public UserBase {
public:
    UserTimer() = default;
    ~UserTimer() override
    {
        OutputResult();
    }
    static UserTimer& Instance()
    {
        static UserTimer single{};
        return single;
    }

    void Start(const std::string& title, const std::string& subtitle, const std::string& desc);
    void Stop(const std::string& title, const std::string& subtitle, const std::string& desc);

private:
    using ResultDataType = std::unordered_map<std::string, std::vector<std::pair<std::string, long int>>>;
    struct Info {
        std::string title;
        std::string subtitle;
        std::string desc;
        std::chrono::system_clock::time_point start;
        std::chrono::system_clock::time_point end;
        std::chrono::duration<double, std::milli> costMs{};
        bool isDone = false;
        explicit Info() = default;
        explicit Info(std::string title, std::string subtitle, std::string desc)
            : title(std::move(title)), subtitle(std::move(subtitle)), desc(std::move(desc))
        {
#if defined(__APPLE__) || defined(__MINGW64__) || defined(__ohos__)
            this->start = std::chrono::system_clock::now();
#else
            this->start = std::chrono::high_resolution_clock::now();
#endif
        }
    };
    std::pair<ResultDataType, std::vector<std::string>> GetDataAndOrder() const;
    std::string GetJson() const override;
    std::string GetSuffix() const final
    {
        return ".time.prof";
    }
    std::list<Info> infoList;
};
} // namespace Cangjie

#endif // CANGJIE_USERTIMER_H
