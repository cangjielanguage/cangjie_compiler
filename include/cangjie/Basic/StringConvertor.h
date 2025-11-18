// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the StringConvertor, which can convert string to codepoint vector and do string
 * normalization.
 */

#ifndef CANGJIE_BASIC_STRINGCONVERTOR_H
#define CANGJIE_BASIC_STRINGCONVERTOR_H

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <cstdint>

namespace Cangjie {

class StringConvertor {
public:
    /// Normalize function is used to deal with escape characters and convert raw string into string which can be
    /// processed in codegen directly.
    static std::string Normalize(const std::string& str, bool isRawString = false);
    /// convert string into raw string which contains escape characters.
    static std::string Recover(const std::string& str);

    /**
    * Convert utf-8 string to codepoint vector.
    *
    * UTF-8 Binary                         |  codepoint Binary
    * 0xxxxxxx                             |  00000000 000000000 00000000 0xxxxxxx
    * 110xxxxx 10xxxxxx                    |  00000000 000000000 00000xxx xxxxxxxx
    * 1110xxxx 10xxxxxx 10xxxxxx           |  00000000 000000000 xxxxxxxx xxxxxxxx
    * 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx  |  00000000 0000xxxxx xxxxxxxx xxxxxxxx
    */
    static std::vector<uint32_t> UTF8ToCodepoint(const std::string& str);
    static std::string CodepointToUTF8(const uint32_t pointValue);
    /// Read a hex number from \ref it and convert it to a unicode point, forward \ref it past the end of the read
    /// hex value, and returns a string that represents the unicode point
    /// The behaviour is erroneous if the input number is not a unicode point
    static std::string GetUnicodeAndToUTF8(std::string::const_iterator& it, const std::string::const_iterator& strEnd);
    /// Convert escape sequences to literal from.
    /// e.g., "\b" is converted to "\\b", so a literal "\b" is in the result
    static std::string EscapeToJsonString(const std::string& str);

#ifdef _WIN32
    enum StrEncoding {
        UTF8,
        GBK,
        UNKNOWN
    };
    static StrEncoding GetStringEncoding(const std::string& data);
    static std::optional<std::string> GBKToUTF8(const std::string& gbkStr);
    static std::optional<std::string> UTF8ToGBK(const std::string& utf8Str);
    static std::optional<std::string> NormalizeStringToUTF8(const std::string& data);
    static std::optional<std::string> NormalizeStringToGBK(const std::string& data);
    static std::optional<std::wstring> StringToWString(const std::string& data);
#endif
};
} // namespace Cangjie
#endif // CANGJIE_BASIC_STRINGCONVERTOR_H
