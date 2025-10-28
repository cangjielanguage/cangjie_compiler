// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file converts utf-8 string to the displayed column width of unicode as follows:
 *
 *    - The null character (U+0000) has a column width of 0.
 *
 *    - Other C0/C1 control characters and DEL will lead to a return value of -1.
 *
 *    - Non-spacing and enclosing combining characters (general category code Mn or Me in the Unicode database) have a
 *      column width of 0.
 *
 *    - SOFT HYPHEN (U+00AD) has a column width of 1.
 *
 *    - Other format characters (general category code Cf in the Unicode database) and ZERO WIDTH SPACE (U+200B)
 *      have a column width of 0.
 *
 *    - Hangul Jamo medial vowels and final consonants (U+1160-U+11FF) have a column width of 0.
 *
 *    - Spacing characters in the East Asian Wide (W) or East Asian Full-width (F) category as defined in Unicode
 *      Technical Report #11 have a column width of 2.
 *
 *    - All remaining characters (including all printable ISO 8859-1 and WGL4 characters, Unicode control characters,
 *      etc.) have a column width of 1.
 */

#ifndef CANGJIE_BASIC_DISPLAY_H
#define CANGJIE_BASIC_DISPLAY_H

#include <vector>
#include <bitset>
#include <stdint.h>
#include <unordered_map>
#include <cstdint>

namespace Cangjie {
static const size_t NORMAL_CODEPOINT_LEN = 4;
static const size_t HORIZONTAL_TAB_LEN = 4;
static const uint8_t ASCII_BASE = 127;

/// Characters that need escaped when print to console.
static std::unordered_map<uint8_t, std::string> escapePrintMap = {
    {'\b', "\\b"}, {'\t', "\\t"}, {'\n', "\\n"},
    {'\v', "\\v"}, {'\f', "\\f"}, {'\r', "\\r"}
};

/// Convert arithmetic value to hex string with length. All letters returned are in uppercase.
template<typename T> std::string ToHexString(T w, size_t len = sizeof(T) >> 1)
{
    static_assert(std::is_arithmetic<std::decay_t<T>>::value, "only support converting arithmetic value to hex");
    static const std::string digits("0123456789ABCDEF");
    std::string ret(len, '0');
    for (size_t i = 0, j = (len - 1) * NORMAL_CODEPOINT_LEN; i < len; ++i, j -= NORMAL_CODEPOINT_LEN) {
        ret[i] = digits[(w >> j) & 0x0f];
    }
    return ret;
}

inline std::string ToBinaryString(uint8_t num)
{
    const static int bStringLen = 8;
    return "0b" + std::bitset<bStringLen>(num).to_string();
}

std::basic_string<char32_t> UTF8ToChar32(const std::string& str);
std::string Char32ToUTF8(const char32_t& str);
std::string Char32ToUTF8(const std::basic_string<char32_t>& str);
/// Returns a string of spaces, with length at least enough to fill content[0..column-1] using unicode DisplayWidth.
std::string GetSpaceBeforeTarget(const std::string& content, int column);
/// Convert the input Unicode scalar point \ref ch into a string to be printed in diagnostic message.
std::string ConvertChar(const int32_t& ch);
std::string ConvertUnicode(const int32_t& str);
///@{
/// Get unicode display width, that is how many spaces it takes to render them in console.
/// Used by fmt.
size_t DisplayWidth(const std::basic_string<char32_t>& pwcs);
size_t DisplayWidth(const std::string& str) noexcept;
///@}
}

#endif // CANGJIE_BASIC_DISPLAY_H
