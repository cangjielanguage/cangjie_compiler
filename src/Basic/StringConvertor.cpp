// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the StringConvertor.
 */

#include "cangjie/Basic/StringConvertor.h"

#include "cangjie/Lex/Lexer.h"

#ifdef _WIN32
#include <wchar.h>
#include <windows.h>
#endif

using namespace Cangjie;

namespace {
#ifdef _WIN32
std::optional<std::wstring> CodePageToUTF16(unsigned codepage, const std::string& original)
{
    std::wstring utf16;
    if (!original.empty()) {
        int len = MultiByteToWideChar(
            codepage, MB_ERR_INVALID_CHARS, const_cast<char*>(original.data()), original.size(), nullptr, 0);
        if (len <= 0) {
            return {};
        }
        utf16.reserve(len + 1);
        utf16.resize(len);
        len = MultiByteToWideChar(codepage, MB_ERR_INVALID_CHARS, const_cast<char*>(original.data()), original.size(),
            const_cast<wchar_t*>(utf16.data()), utf16.size());
        if (len <= 0) {
            return {};
        }
    }
    return utf16;
}

std::optional<std::string> UTF16ToCodePage(unsigned codepage, std::wstring& utf16)
{
    std::string converted;
    if (!utf16.empty()) {
        int len = WideCharToMultiByte(
            codepage, 0, const_cast<wchar_t*>(utf16.data()), utf16.size(), nullptr, 0, nullptr, nullptr);
        if (len <= 0) {
            return {};
        }
        converted.reserve(len + 1);
        converted.resize(len);
        len = WideCharToMultiByte(codepage, 0, const_cast<wchar_t*>(utf16.data()), utf16.size(),
            const_cast<char*>(converted.data()), converted.size(), nullptr, nullptr);
        if (len <= 0) {
            return {};
        }
    }
    return converted;
}

/*
 * Returns the number of bits before the first 0 bit in the first eight bits of the first byte.
 * The number is also the number of bytes used by the character.
 * 110X_XXXX 10XX_XXXX
 * 1110_XXXX 10XX_XXXX 10XX_XXXX
 * 1111_0XXX 10XX_XXXX 10XX_XXXX 10XX_XXXX
 * 1111_10XX 10XX_XXXX 10XX_XXXX 10XX_XXXX 10XX_XXXX
 * 1111_110X 10XX_XXXX 10XX_XXXX 10XX_XXXX 10XX_XXXX 10XX_XXXX
 */
size_t PreNum(unsigned char byte)
{
    unsigned char mask = 0x80;
    size_t num = 0;
    for (int i = 0; i < 8; i++) { // A byte has 8 bits.
        if ((byte & mask) == mask) {
            mask = mask >> 1;
            num++;
        } else {
            break;
        }
    }
    return num;
}

bool IsGBK(const std::string& data)
{
    size_t i = 0;
    while (i < data.size()) {
        unsigned char dataI = static_cast<unsigned char>(data[i]);
        if (dataI <= 0x7f) {
            // The code must be less than or equal to 127. The code contains only one byte and is compatible with ASCII.
            i++;
            continue;
        }
        // If the value is greater than 127, double-byte encoding is used.
        if (i + 1 >= data.size()) {
            // Not enough bytes
            return false;
        }
        unsigned char dataII = static_cast<unsigned char>(data[i + 1]);
        if (dataI >= 0x81 && dataI <= 0xfe && dataII >= 0x40 && dataII <= 0xfe && dataII != 0xf7) {
            i += 2; // A character contains 2 bytes.
            continue;
        } else {
            return false;
        }
    }
    return true;
}

bool IsUTF8(const std::string& data)
{
    size_t num = 0;
    size_t i = 0;
    const size_t MIN_PRENUM = 3;
    while (i < data.size()) {
        if ((data[i] & 0x80) == 0x00) {
            // 0XXX_XXXX for ASCII
            i++;
            continue;
        }
        size_t num = PreNum(data[i]);
        if (num < MIN_PRENUM) {
            // In UTF-8 encoding, multiple bytes must contain at least 2 bytes,
            // the min number of bytes used by the character is 3.
            // In other cases, the utf-8 is not used.
            return false;
        }
        i++;
        for (size_t j = 1; j < num; j++) {
            // Check whether the following num - 1 byte is 10 open.
            if (i >= data.size()) {
                // Not enough bytes
                return false;
            }
            if ((data[i] & 0xc0) != 0x80) {
                return false;
            }
            i++;
        }
    }
    return true;
}
#endif
} // namespace

std::string StringConvertor::GetUnicodeAndToUTF8(
    std::string::const_iterator& it, const std::string::const_iterator& strEnd)
{
    static std::unordered_map<char, uint32_t> hexadecimalMap = {{'0', 0}, {'1', 1}, {'2', 2}, {'3', 3}, {'4', 4},
        {'5', 5}, {'6', 6}, {'7', 7}, {'8', 8}, {'9', 9}, {'A', 10}, {'B', 11}, {'C', 12}, {'D', 13}, {'E', 14},
        {'F', 15}, {'a', 10}, {'b', 11}, {'c', 12}, {'d', 13}, {'e', 14}, {'f', 15}};
    auto temp = it;
    uint32_t unicodePoint = 0;
    while (temp != strEnd && isxdigit(*temp)) {
        // hexadecimal to decimal need multiplying 16
        unicodePoint = (unicodePoint * 16) + hexadecimalMap[*temp];
        ++temp;
    }
    it = temp;
    return CodepointToUTF8(unicodePoint);
}

/**
 * Normalize function is used to deal with escape characters and convert raw string into string which can be processed
 * in codegen directly.
 */
std::string StringConvertor::Normalize(const std::string& str, bool isRawString)
{
    std::unordered_map<char, char> escapeList{{'0', '\0'}, {'\\', '\\'}, {'b', '\b'}, {'f', '\f'}, {'n', '\n'},
        {'r', '\r'}, {'t', '\t'}, {'v', '\v'}, {'\'', '\''}, {'\"', '\"'}, {'\?', '\?'}};
    std::string str1{};
    str1.reserve(str.size());
    for (auto it = str.begin(); it != str.cend(); ++it) {
        if (*it == '\\' && !isRawString) {
            // it + 1 mast 'u' and  it + 2 mast '{'
            if ((it + 1) != str.cend() && (it + 2) != str.cend() && *(it + 1) == 'u' && *(it + 2) == '{') {
                // "\u{" need add 3
                it += 3;
                str1 += GetUnicodeAndToUTF8(it, str.cend());
            } else if (escapeList.find(*(it + 1)) != escapeList.end()) {
                ++it;
                str1 += escapeList.find(*(it))->second;
            }
            if (it == str.cend()) {
                break;
            }
        } else if (*it == '\r') {
            // normalize \r\n to \n in Multiline String
            if ((it + 1) != str.cend() && *(it + 1) == '\n') {
                ++it;
                str1 += *it;
            } else {
                str1 += "\n";
            }
        } else {
            str1 += *it;
        }
    }
    return str1;
}

std::string StringConvertor::CodepointToUTF8(const uint32_t pointValue)
{
    std::string result;
    // max size is 6
    result.reserve(6);
    if (pointValue <= UTF8_1_MAX) {
        result += static_cast<char>(static_cast<uint8_t>(pointValue));
    } else if (pointValue <= UTF8_2_MAX) {
        result +=
            static_cast<char>(static_cast<uint8_t>(((pointValue >> LEFT_SHIFT_6) & LOW_5_BIT_MASK) | BYTE_2_FLAG));
        result += static_cast<char>(static_cast<uint8_t>((pointValue & LOW_6_BIT_MASK) | BYTE_X_FLAG));
    } else if (pointValue <= UTF8_3_MAX) {
        result +=
            static_cast<char>(static_cast<uint8_t>(((pointValue >> LEFT_SHIFT_12) & LOW_4_BIT_MASK) | BYTE_3_FLAG));
        result +=
            static_cast<char>(static_cast<uint8_t>(((pointValue >> LEFT_SHIFT_6) & LOW_6_BIT_MASK) | BYTE_X_FLAG));
        result += static_cast<char>(static_cast<uint8_t>((pointValue & LOW_6_BIT_MASK) | BYTE_X_FLAG));
    } else if (pointValue <= UTF8_4_MAX) {
        result +=
            static_cast<char>(static_cast<uint8_t>(((pointValue >> LEFT_SHIFT_18) & LOW_3_BIT_MASK) | BYTE_4_FLAG));
        result +=
            static_cast<char>(static_cast<uint8_t>(((pointValue >> LEFT_SHIFT_12) & LOW_6_BIT_MASK) | BYTE_X_FLAG));
        result +=
            static_cast<char>(static_cast<uint8_t>(((pointValue >> LEFT_SHIFT_6) & LOW_6_BIT_MASK) | BYTE_X_FLAG));
        result += static_cast<char>(static_cast<uint8_t>((pointValue & LOW_6_BIT_MASK) | BYTE_X_FLAG));
    } else if (pointValue <= UTF8_5_MAX) {
        result +=
            static_cast<char>(static_cast<uint8_t>(((pointValue >> LEFT_SHIFT_24) & LOW_2_BIT_MASK) | BYTE_5_FLAG));
        result +=
            static_cast<char>(static_cast<uint8_t>(((pointValue >> LEFT_SHIFT_18) & LOW_6_BIT_MASK) | BYTE_X_FLAG));
        result +=
            static_cast<char>(static_cast<uint8_t>(((pointValue >> LEFT_SHIFT_12) & LOW_6_BIT_MASK) | BYTE_X_FLAG));
        result +=
            static_cast<char>(static_cast<uint8_t>(((pointValue >> LEFT_SHIFT_6) & LOW_6_BIT_MASK) | BYTE_X_FLAG));
        result += static_cast<char>(static_cast<uint8_t>((pointValue & LOW_6_BIT_MASK) | BYTE_X_FLAG));
    } else if (pointValue <= UTF8_6_MAX) {
        result +=
            static_cast<char>(static_cast<uint8_t>(((pointValue >> LEFT_SHIFT_30) & LOW_1_BIT_MASK) | BYTE_6_FLAG));
        result +=
            static_cast<char>(static_cast<uint8_t>(((pointValue >> LEFT_SHIFT_24) & LOW_6_BIT_MASK) | BYTE_X_FLAG));
        result +=
            static_cast<char>(static_cast<uint8_t>(((pointValue >> LEFT_SHIFT_18) & LOW_6_BIT_MASK) | BYTE_X_FLAG));
        result +=
            static_cast<char>(static_cast<uint8_t>(((pointValue >> LEFT_SHIFT_12) & LOW_6_BIT_MASK) | BYTE_X_FLAG));
        result +=
            static_cast<char>(static_cast<uint8_t>(((pointValue >> LEFT_SHIFT_6) & LOW_6_BIT_MASK) | BYTE_X_FLAG));
        result += static_cast<char>(static_cast<uint8_t>((pointValue & LOW_6_BIT_MASK) | BYTE_X_FLAG));
    }
    return result;
}

namespace {
std::string EscapeString(const std::string& str, const std::unordered_map<char, char>& escapeList)
{
    std::string newStr{};
    newStr.reserve(str.size() + 1);
    auto got = escapeList.end();
    for (char it : str) {
        got = escapeList.find(it);
        if (got != escapeList.end()) {
            newStr += '\\';
            newStr += got->second;
        } else {
            newStr += it;
        }
    }
    return newStr;
}
} // namespace end

std::string StringConvertor::Recover(const std::string& str)
{
    static const std::unordered_map<char, char> ESCAPE_LIST{{'\b', 'b'}, {'\f', 'f'}, {'\n', 'n'}, {'\r', 'r'},
        {'\t', 't'}, {'\v', 'v'}, {'\\', '\\'}, {'\"', '"'}, {'\'', '\''}, {'\0', '0'}};
    return EscapeString(str, ESCAPE_LIST);
}

std::string StringConvertor::EscapeToJsonString(const std::string& str)
{
    static const std::unordered_map<char, char> ESCAPE_LIST{
        {'\b', 'b'}, {'\f', 'f'}, {'\n', 'n'}, {'\r', 'r'}, {'\t', 't'}, {'\\', '\\'}, {'\"', '"'}};
    return EscapeString(str, ESCAPE_LIST);
}

std::vector<uint32_t> StringConvertor::UTF8ToCodepoint(const std::string& str)
{
    std::vector<uint32_t> ret;
    size_t idx = 0;
    while (idx < str.size()) {
        uint32_t ch = static_cast<uint32_t>(static_cast<uint8_t>(str[idx]));
        if (ch >= BYTE_2_FLAG) {
            uint32_t byte1 = ch;
            uint32_t byte2 = static_cast<uint32_t>(static_cast<uint8_t>(str[idx + 1]));
            if (ch >= BYTE_6_FLAG) {
                uint32_t byte3 = static_cast<uint32_t>(static_cast<uint8_t>(str[idx + BYTE_3_INDEX]));
                uint32_t byte4 = static_cast<uint32_t>(static_cast<uint8_t>(str[idx + BYTE_4_INDEX]));
                uint32_t byte5 = static_cast<uint32_t>(static_cast<uint8_t>(str[idx + BYTE_5_INDEX]));
                uint32_t byte6 = static_cast<uint32_t>(static_cast<uint8_t>(str[idx + BYTE_6_INDEX]));
                ch =
                    uint32_t(((byte1 & LOW_1_BIT_MASK) << LEFT_SHIFT_30) | ((byte2 & LOW_6_BIT_MASK) << LEFT_SHIFT_24) |
                        ((byte3 & LOW_6_BIT_MASK) << LEFT_SHIFT_18) | ((byte4 & LOW_6_BIT_MASK) << LEFT_SHIFT_12) |
                        ((byte5 & LOW_6_BIT_MASK) << LEFT_SHIFT_6) | (byte6 & LOW_6_BIT_MASK));
                idx += BYTE_6_STEP;
            } else if (ch >= BYTE_5_FLAG) {
                uint32_t byte3 = static_cast<uint32_t>(static_cast<uint8_t>(str[idx + BYTE_3_INDEX]));
                uint32_t byte4 = static_cast<uint32_t>(static_cast<uint8_t>(str[idx + BYTE_4_INDEX]));
                uint32_t byte5 = static_cast<uint32_t>(static_cast<uint8_t>(str[idx + BYTE_5_INDEX]));
                ch = uint32_t(((byte1 & LOW_2_BIT_MASK) << LEFT_SHIFT_24) |
                    ((byte2 & LOW_6_BIT_MASK) << LEFT_SHIFT_18) | ((byte3 & LOW_6_BIT_MASK) << LEFT_SHIFT_12) |
                    ((byte4 & LOW_6_BIT_MASK) << LEFT_SHIFT_6) | (byte5 & LOW_6_BIT_MASK));
                idx += BYTE_5_STEP;
            } else if (ch >= BYTE_4_FLAG) {
                uint32_t byte3 = static_cast<uint32_t>(static_cast<uint8_t>(str[idx + BYTE_3_INDEX]));
                uint32_t byte4 = static_cast<uint32_t>(static_cast<uint8_t>(str[idx + BYTE_4_INDEX]));
                ch =
                    uint32_t(((byte1 & LOW_3_BIT_MASK) << LEFT_SHIFT_18) | ((byte2 & LOW_6_BIT_MASK) << LEFT_SHIFT_12) |
                        ((byte3 & LOW_6_BIT_MASK) << LEFT_SHIFT_6) | (byte4 & LOW_6_BIT_MASK));
                idx += BYTE_4_STEP;
            } else if (ch >= BYTE_3_FLAG) {
                uint32_t byte3 = static_cast<uint32_t>(static_cast<uint8_t>(str[idx + BYTE_3_INDEX]));
                ch = uint32_t(((byte1 & LOW_4_BIT_MASK) << LEFT_SHIFT_12) | ((byte2 & LOW_6_BIT_MASK) << LEFT_SHIFT_6) |
                    (byte3 & LOW_6_BIT_MASK));
                idx += BYTE_3_STEP;
            } else {
                ch = uint32_t(((byte1 & LOW_5_BIT_MASK) << LEFT_SHIFT_6) | (byte2 & LOW_6_BIT_MASK));
                idx += BYTE_2_STEP;
            }
        } else {
            idx += BYTE_1_STEP;
        }
        ret.push_back(ch);
    }
    return ret;
}

#ifdef _WIN32

StringConvertor::StrEncoding StringConvertor::GetStringEncoding(const std::string& data)
{
    // IsGBK() is implemented by whether the double bytes fall within the encoding range of gbk,
    // while each byte in the UTF-8 encoding format falls within the encoding range of gbk.
    // Therefore, it is meaningful to call IsUTF8() first to determine whether the encoding is not UTF-8, and then to
    // call IsGBK().
    if (IsUTF8(data)) {
        return StrEncoding::UTF8;
    } else if (IsGBK(data)) {
        return StrEncoding::GBK;
    } else {
        return StrEncoding::UNKNOWN;
    }
}

std::optional<std::string> StringConvertor::GBKToUTF8(const std::string& gbkStr)
{
    std::optional<std::wstring> utf16 = CodePageToUTF16(CP_ACP, gbkStr);
    if (!utf16.has_value()) {
        return {};
    }
    std::optional<std::string> utf8 = UTF16ToCodePage(CP_UTF8, utf16.value());
    return utf8;
}

std::optional<std::string> StringConvertor::UTF8ToGBK(const std::string& utf8Str)
{
    std::optional<std::wstring> utf16 = CodePageToUTF16(CP_UTF8, utf8Str);
    if (!utf16.has_value()) {
        return {};
    }
    // Code Page 936 is the identifier for the GBK encoding in the Windows operating system.
    const static unsigned gbkCode = 936;
    std::optional<std::string> gbk = UTF16ToCodePage(gbkCode, utf16.value());
    return gbk;
}

std::optional<std::string> StringConvertor::NormalizeStringToUTF8(const std::string& data)
{
    StrEncoding encoding = GetStringEncoding(data);
    if (encoding == UTF8) {
        return data;
    } else if (encoding == GBK) {
        return GBKToUTF8(data);
    } else {
        return {};
    }
}

std::optional<std::string> StringConvertor::NormalizeStringToGBK(const std::string& data)
{
    StrEncoding encoding = GetStringEncoding(data);
    if (encoding == UTF8) {
        return UTF8ToGBK(data);
    } else if (encoding == GBK) {
        return data;
    } else {
        return {};
    }
}

std::optional<std::wstring> StringConvertor::StringToWString(const std::string& data)
{
    StrEncoding encoding = GetStringEncoding(data);
    if (encoding == UTF8) {
        return CodePageToUTF16(CP_UTF8, data);
    } else if (encoding == GBK) {
        return CodePageToUTF16(CP_ACP, data);
    } else {
        return {};
    }
}

#endif
