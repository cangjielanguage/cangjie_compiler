// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_STRING_WRAPPER_H
#define CANGJIE_CHIR_STRING_WRAPPER_H

#include <string>

namespace Cangjie::CHIR {

/**
 * @brief Extend api for std::string
 */
class StringWrapper {
public:
    explicit StringWrapper(const std::string& initVal = "");

    /**
     * @brief Return current object as a string.
     *
     * @return The content of current object.
     */
    const std::string& Str() const;

    /**
     * @brief Append new content to the old one.
     *
     * @param newValue The new content.
     */
    void Append(const std::string& newValue);

    /**
     * @brief Append new content to the old one.
     *
     * @param newValue The new content.
     * @param delimiter The delimiter.
     */
    void Append(const std::string& newValue, const std::string& delimiter);

    /**
     * @brief Remove the last N characters.
     *
     * @param n The number of characters.
     */
    void RemoveLastNChars(const size_t n);

    /**
     * @brief If current object has content, then append delimiter to the content, otherwise, not append.
     *
     * @param delimiter The delimiter.
     * @return an object which has already appended delimiter.
     */
    StringWrapper& AddDelimiterOrNot(const std::string& delimiter);

    /**
     * @brief If the `newValue` is empty, then clear current object's content,
     * if not, append the `newValue` to current object's content.
     *
     * @param newValue The new content.
     * @return an object which has already appended or cleared.
     */
    StringWrapper& AppendOrClear(const std::string& newValue);

private:
    std::string value;
};
} // namespace Cangjie::CHIR

#endif