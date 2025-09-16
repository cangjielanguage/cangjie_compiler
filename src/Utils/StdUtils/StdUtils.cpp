// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/Utils/StdUtils.h"

namespace Cangjie {
std::optional<int> Stoi(const std::string& s, int base)
{
    try {
        return std::stoi(s, nullptr, base);
    } catch (...) {
        return {};
    }
}
std::optional<long> Stol(const std::string& s, int base)
{
    try {
        return std::stol(s, nullptr, base);
    } catch (...) {
        return {};
    }
}
std::optional<unsigned long> Stoul(const std::string& s, int base)
{
    try {
        return std::stoul(s, nullptr, base);
    } catch (...) {
        return {};
    }
}
std::optional<long long> Stoll(const std::string& s, int base)
{
    try {
        return std::stoll(s, nullptr, base);
    } catch (...) {
        return {};
    }
}
std::optional<unsigned long long> Stoull(const std::string& s, int base)
{
    try {
        return std::stoull(s, nullptr, base);
    } catch (...) {
        return {};
    }
}

std::optional<double> Stod(const std::string& s)
{
    try {
        return std::stod(s, nullptr);
    } catch (...) {
        return {};
    }
}
std::optional<long double> Stold(const std::string& s)
{
    try {
        return std::stold(s, nullptr);
    } catch (...) {
        return {};
    }
}
}
