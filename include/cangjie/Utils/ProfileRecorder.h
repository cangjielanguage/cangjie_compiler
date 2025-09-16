// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file defines the ProfileRecorder class for performance analysis.
 */

#ifndef CANGJIE_UTILS_PROFILE_RECORDER_H
#define CANGJIE_UTILS_PROFILE_RECORDER_H

#include <string>
#include <functional>

namespace Cangjie::Utils {

class ProfileRecorder {
public:
    enum class Type {
        INVALID_TYPE = 0x00,
        TIMER        = 0x01,
        MEMORY       = 0x02,
        ALL          = 0x03  // TIMER | MEMORY
    };
    ProfileRecorder(
        const std::string& title, const std::string& subtitle, const std::string& desc = "");
    ~ProfileRecorder();

    static void SetPackageName(const std::string& name);
    static void SetOutputDir(const std::string& path);
    static void Enable(bool en, const Type& type = Type::ALL);

    static void Start(
        const std::string& title, const std::string& subtitle, const std::string& desc = "");
    static void Stop(
        const std::string& title, const std::string& subtitle, const std::string& desc = "");
    /**
     * @brief Record some code info. Avoid introducing other module-specific content by custom function.
     * @param item Indicates the name of a single information item.
     * @param getData Indicates the closure function for obtaining the value of the item.
     */
    static void RecordCodeInfo(const std::string& item, const std::function<int64_t(void)>& getData);
    static void RecordCodeInfo(const std::string& item, int64_t value);

    static std::string GetResult(const Type& type = Type::ALL);

private:
    std::string title_;
    std::string subtitle_;
    std::string desc_;
};
} // namespace Cangjie::Utils

#endif // CANGJIE_UTILS_PROFILE_RECORDER_H
