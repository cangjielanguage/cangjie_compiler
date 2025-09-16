// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_UTILS_STDUTILS_H
#define CANGJIE_UTILS_STDUTILS_H

#include <optional>
#include <string>

namespace Cangjie {
constexpr int STOINT_BASE{10};
std::optional<int> Stoi(const std::string& s, int base = STOINT_BASE);
std::optional<long> Stol(const std::string& s, int base = STOINT_BASE);
std::optional<unsigned long> Stoul(const std::string& s, int base = STOINT_BASE);
std::optional<long long> Stoll(const std::string& s, int base = STOINT_BASE);
std::optional<unsigned long long> Stoull(const std::string& s, int base = STOINT_BASE);
std::optional<double> Stod(const std::string& s);
std::optional<long double> Stold(const std::string& s);
}
#endif
