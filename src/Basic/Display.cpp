// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/Basic/Display.h"

#include <codecvt>
#include <cassert>

#include "cangjie/Basic/Print.h"
#include "cangjie/Utils/CheckUtils.h"
#include "cangjie/Utils/Unicode.h"

namespace Cangjie {
static const int UNICODE_OUTPUT_WIDTH = 8;
static const std::vector<std::pair<uint32_t, uint32_t>> combining = {
#define BIND(left, right) {left, right},
#include "cangjie/Basic/DisplayColumn.def"
#undef BIND
};
static int WideCharWidth(char32_t ucs)
{
    if (ucs == 0x0a) { // newline character:'\n'
        return 1;
    }
    if (ucs == 0x09) { // It is horizontal tab.
        return HORIZONTAL_TAB_LEN;
    }
    // Other control characters.
    if ((0 <= ucs && ucs <= 0x8) || (0xb <= ucs && ucs <= 0x1f) || ucs == 0x7f) {
        return UNICODE_OUTPUT_WIDTH;
    }
    if (ucs > 0x7f && ucs < 0xa0) {
        return 0;
    }

    auto pred = [](auto& combine, auto& u) {
        return u.first > combine.second;
    };

    // Binary search in table of non-spacing characters.
    if (std::binary_search(combining.begin(), combining.end(), std::make_pair(ucs, ucs), pred)) {
        return 0;
    }
    // If we arrive here, ucs is not a combining or C0/C1 control character.
    return 1 + static_cast<int>((ucs >= 0x1100 && (ucs <= 0x115f || ucs == 0x2329 || ucs == 0x232a ||
               (ucs >= 0x2e80 && ucs <= 0xa4cf && ucs != 0x303f) ||
               (ucs >= 0xac00 && ucs <= 0xd7a3) || /* Hangul Syllables */
               (ucs >= 0xf900 && ucs <= 0xfaff) || /* CJK Compatibility Ideographs */
               (ucs >= 0xfe10 && ucs <= 0xfe19) || /* Vertical forms */
               (ucs >= 0xfe30 && ucs <= 0xfe6f) || /* CJK Compatibility Forms */
               (ucs >= 0xff00 && ucs <= 0xff60) || /* Fullwidth Forms */
               (ucs >= 0xffe0 && ucs <= 0xffe6) ||
               (ucs >= 0x20000 && ucs <= 0x2fffd) ||
               (ucs >= 0x30000 && ucs <= 0x3fffd))));
}

std::basic_string<char32_t> UTF8ToChar32(const std::string& str)
{
    std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> conv;
    return conv.from_bytes(str);
}

std::string Char32ToUTF8(const char32_t& str)
{
    auto s = std::basic_string<char32_t>{static_cast<char32_t>(str)};
    return Char32ToUTF8(s);
}

std::string Char32ToUTF8(const std::basic_string<char32_t>& str)
{
    std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> conv;
    std::string res;
#ifndef CANGJIE_ENABLE_GCOV
    try {
#endif
        res = conv.to_bytes(str);
#ifndef CANGJIE_ENABLE_GCOV
    } catch (const std::range_error& e) {
        res = "";
    }
#endif
    return res;
}

std::string ConvertUnicode(const int32_t& str)
{
    return "\\u{" + ToHexString(str, NORMAL_CODEPOINT_LEN) + "}";
}

std::string ConvertChar(const int32_t& ch)
{
    if (ch < 0) {
        return "terminated";
    }
    if (static_cast<char32_t>(ch) > ASCII_BASE) {
        return Char32ToUTF8(static_cast<char32_t>(ch));
    }
    auto c = static_cast<uint8_t>(ch);
    static std::unordered_map<uint8_t, std::string> escapeMap = {
        {'\t', "tab"}, {'\n', "new line"},
    };
    if (escapeMap.count(c) > 0) {
        return std::string{escapeMap[c]} + " character";
    }
    // Control character.
    if (c < 0x20 || c == 0x7f) {
        return "\\u{" + ToHexString(ch, NORMAL_CODEPOINT_LEN) + "}";
    }
    return Char32ToUTF8(static_cast<char32_t>(ch));
}

size_t DisplayWidth(const std::basic_string<char32_t>& pwcs)
{
    int width = 0;
    for (auto s : pwcs) {
        width += WideCharWidth(s);
    }
    width = width < 0 ? -width : width; // If it only contains escape.
    return static_cast<size_t>(width);
}

// If str contains illegal utf-8 string, UTF8ToChar32 will throw a range_error.
size_t DisplayWidth(const std::string& str) noexcept
{
    size_t len;
#ifndef CANGJIE_ENABLE_GCOV
    try {
#endif
        len = DisplayWidth(UTF8ToChar32(str));
#ifndef CANGJIE_ENABLE_GCOV
    } catch (const std::range_error&) {
        len = str.size();
    }
#endif
    return len;
}

std::string GetSpaceBeforeTarget(const std::string& content, int column)
{
    CJC_ASSERT(!content.empty() && "source content is empty");
    if (content.empty()) {
        return {};
    }
    CJC_ASSERT(column > 0);
    column = static_cast<int>(std::min(static_cast<size_t>(column), content.size() + 1));
    auto length = Unicode::DisplayWidth(content.substr(0, static_cast<size_t>(column - 1)));
    return std::string(static_cast<size_t>(static_cast<unsigned>(length)), ' ');
}
}

