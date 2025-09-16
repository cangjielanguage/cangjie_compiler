// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_USERBASE_H
#define CANGJIE_USERBASE_H

#include <string>

namespace Cangjie {
class UserBase {
public:
    virtual std::string GetResult() const;
    void OutputResult() const noexcept;

    void WriteJson(const std::string& context, const std::string& suffix) const;

    virtual ~UserBase() = default;

    void Enable(bool en);
    bool IsEnable() const;
    void SetPackageName(const std::string& name);
    void SetOutputDir(const std::string& path);

protected:
    bool enable{false};
    std::string packageName;
    std::string outputDir;

private:
    virtual std::string GetSuffix() const = 0;
    virtual std::string GetJson() const = 0;
};
} // namespace Cangjie

#endif // CANGJIE_USERBASE_H
