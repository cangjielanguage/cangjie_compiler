// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/StringWrapper.h"

using namespace Cangjie::CHIR;

StringWrapper::StringWrapper(const std::string& initVal) : value(initVal)
{
}

const std::string& StringWrapper::Str() const
{
    return value;
}

void StringWrapper::Append(const std::string& newValue)
{
    value += newValue;
}

void StringWrapper::Append(const std::string& newValue, const std::string& delimiter)
{
    if (!newValue.empty() && !value.empty()) {
        value += delimiter + " " + newValue;
    }
}

void StringWrapper::RemoveLastNChars(const size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        value.pop_back();
    }
}

StringWrapper& StringWrapper::AddDelimiterOrNot(const std::string& delimiter)
{
    if (!value.empty()) {
        value += delimiter;
    }
    return *this;
}

StringWrapper& StringWrapper::AppendOrClear(const std::string& newValue)
{
    if (newValue.empty()) {
        value.clear();
    } else {
        value += newValue;
    }
    return *this;
}