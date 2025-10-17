// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the DiagnosticEmitter related classes.
 */

#include "DiagnosticEmitterImpl.h"

#include <map>
#include <regex>

#include "cangjie/Basic/Display.h"
#include "cangjie/Basic/Print.h"
#include "cangjie/Utils/Unicode.h"

namespace Cangjie {
constexpr char BITOR = '|';
constexpr char BITXOR = '^';
constexpr char TILDE = '~';
constexpr char SPACE = ' ';
constexpr char COLON = ':';
constexpr char MAIN_HINT_SYMBOL = BITXOR;
constexpr char OTHER_HINT_SYMBOL = TILDE;
constexpr char MULTI_LINE_KEY_SYMBOL = BITOR;
constexpr char MULTI_LINE_SYMBOL = '_';
constexpr char WITHOUT_SOURCE_SYMBOL = '#';

inline std::string GetNewLine()
{
    return std::string{"\n"};
}

inline std::string GetLineSymbol()
{
    return std::string{"==>"};
}

inline std::string CharacterOfNum(size_t n, char c)
{
    return std::string(n, c);
}

inline auto g_spaceOfNum = std::bind(CharacterOfNum, std::placeholders::_1, ' ');

inline std::string GetColoredString(DiagColor color, const std::string& str, bool isBright = false)
{
    CJC_ASSERT(ColorPrintMap.find(color) != ColorPrintMap.end() && "no print color found");
    std::string res = ColorPrintMap.at(color);
    if (isBright) {
        res += ANSI_COLOR_BRIGHT;
    }
    res += str;
    if (color != DiagColor::NO_COLOR) {
        res += ColorPrintMap.at(DiagColor::RESET);
    }
    return res;
}

void DiagnosticEmitterImpl::CollectInformation(std::vector<CollectedInfo>& vec, IntegratedString& str, bool isMain)
{
    CollectedInfo info;
    info.hint = str.str;
    info.color = str.color;
    info.isMain = isMain;
    CJC_ASSERT(str.range.end >= str.range.begin);
    if (str.range.begin.line == str.range.end.line) {
        info.range = str.range;
    } else {
        // If it is multi-line, it will store respectively based on start position and end position.
        auto startRange = MakeRange(str.range.begin, str.range.begin + 1);
        auto startInfo = CollectedInfo{startRange, info.isMain, true, false, "", str.color};
        vec.push_back(startInfo);
        multiLineRecordMap.emplace(std::pair{startInfo, multiLineHangingPtrVec.size()});
        CJC_ASSERT(str.range.end.column > 1 && "end of range must be bigger than 1");
        auto s = str.range.end.column > 1 ? str.range.end - Position{0, 0, 1} :
                                          Position{str.range.end.fileID, str.range.end.line, 1};
        auto e = str.range.end.column > 1 ? str.range.end : Position{str.range.end.fileID, str.range.end.line, 1 + 1};
        info.range = MakeRange(s, e);
        info.isMultiLine = true;
        info.isEnd = true;
        info.hint = str.str;
        multiLineRecordMap.emplace(std::pair{info, multiLineHangingPtrVec.size()});
        multiLineHangingPtrVec.emplace_back(-1, -1);
    }
    vec.push_back(info);
}

void DiagnosticEmitterImpl::SortAndCheck(std::vector<CollectedInfo>& errorInfo) const
{
    std::sort(errorInfo.begin(), errorInfo.end(), [this](auto& a, auto& b) -> bool {
        if (enableRangeCheckICE) {
            CJC_ASSERT(a.range.begin != b.range.begin);
        }
        return a.range.begin < b.range.begin;
    });
}

std::string DiagnosticEmitterImpl::GetSourceCode(std::vector<CollectedInfo>& errorInfo) const
{
    CJC_ASSERT(!errorInfo.empty() && "error info cannot be empty");
    if (errorInfo.empty()) {
        return std::string{};
    }
    auto start = errorInfo.front().range.begin;
    auto end = errorInfo.back().range.end;
    if (start.fileID == end.fileID) {
        return sm.GetContentBetween(
            start.fileID, Position(start.line, 1), Position(end.line, std::numeric_limits<int>::max()));
    }
    // Get the code after macro expansion.
    auto fileID = start.fileID;
    std::string source;
    for (auto& info : errorInfo) {
        auto range = info.range;
        if (range.begin.fileID < fileID) {
            if (!source.empty()) {
                source += "\n";
            }
            continue;
        }
        source += sm.GetContentBetween(range.begin.fileID,
            Position(range.begin.line, 1), Position(range.end.line, std::numeric_limits<int>::max()));
    }
    return source;
}

size_t DiagnosticEmitterImpl::GetDisplayedWidthFromSource(const std::string& source, const Range& range) const
{
    const size_t defaultWidth = 1;
    CJC_ASSERT(!source.empty() && "source cannot be empty");
    if (source.empty()) {
        return defaultWidth; // 1 is default;
    }
    auto start = range.begin.column;
    auto end = range.end.column;
    auto len = source.size();
    start = start > static_cast<int>(len) ? static_cast<int>(len) : start;
    end = end > static_cast<int>(len + 1) ? static_cast<int>(len + 1) : end;
    CJC_ASSERT(start >= 1 && "wrong start position");
    CJC_ASSERT(end >= start);
    std::string substr = source.substr(static_cast<size_t>(start - 1), static_cast<size_t>(end - start));
    auto disWidth = static_cast<size_t>(static_cast<ssize_t>(Unicode::DisplayWidth(substr)));
    // If display width equals to 0, it only contains control character.
    disWidth = disWidth > 0 ? disWidth : defaultWidth;
    return disWidth;
}

void DiagnosticEmitterImpl::InsertSymbolInFirstLine(CombinedLine& combinedLine, size_t loc,
    const CollectedInfo& info, const std::string& sourceLine) const
{
    char sym = info.isMain ? MAIN_HINT_SYMBOL : OTHER_HINT_SYMBOL;
    auto len = GetDisplayedWidthFromSource(sourceLine, info.range);
    auto repStr = CharacterOfNum(len, sym);
    combinedLine.meta.replace(loc, len, repStr);
    combinedLine.colors.emplace_back(loc, loc + len, info.color);
}

void DiagnosticEmitterImpl::InsertSymbolNotFirstLine(
    CombinedLine& combinedLine, size_t loc, const CollectedInfo& info) const
{
    size_t len = 1;
    auto repStr = CharacterOfNum(len, BITOR);
    combinedLine.meta.replace(loc, len, repStr);
    combinedLine.colors.emplace_back(loc, loc + len, info.color);
}

void DiagnosticEmitterImpl::InsertSymbolToUpperLine(SourceCombinedVec& insertedStr, size_t loc,
    const CollectedInfo& info, const std::string& sourceLine) const
{
    for (size_t i = 0; i < insertedStr.size(); i++) {
        CJC_ASSERT(insertedStr[i].meta.size() >= loc);
        if (i == 0) {
            InsertSymbolInFirstLine(insertedStr[0], loc, info, sourceLine);
        } else {
            InsertSymbolNotFirstLine(insertedStr[i], loc, info);
        }
    }
}

static CollectedInfoMap StoreInfoToMap(std::vector<CollectedInfo>& errorInfos)
{
    CollectedInfoMap lineToInfoMap;
    std::for_each(errorInfos.begin(), errorInfos.end(), [&](auto& info) {
        lineToInfoMap[static_cast<unsigned>(info.range.begin.line)].push_back(info);
    });
    return lineToInfoMap;
}

static size_t GetMaxLineColumn(std::vector<CollectedInfo>& errorInfos)
{
    size_t maxLineColumn = 0;
    std::for_each(errorInfos.begin(), errorInfos.end(), [&](auto& info) {
        CJC_ASSERT(info.range.end.line >= 0);
        if (static_cast<size_t>(info.range.end.line) > maxLineColumn) {
            maxLineColumn = static_cast<size_t>(info.range.end.line);
        }
    });
    return std::to_string(maxLineColumn).length();
}


void HandleSpecialCharacters(std::string& str)
{
    // For more special characters.
    str = std::regex_replace(str, std::regex("\t"), g_spaceOfNum(HORIZONTAL_TAB_LEN));
}

static const size_t COMPRESS_LEN = 2;
void DiagnosticEmitterImpl::CompressLineCode(CollectedInfoMap& infoMap, SourceCombinedVec& bindLineCodes) const
{
    CJC_ASSERT(!infoMap.empty() && "info map is empty");
    if (infoMap.empty()) {
        return;
    }
    auto base = infoMap.cbegin()->first;
    unsigned int second = 0;
    unsigned lineNum = (infoMap.rbegin()->first - infoMap.begin()->first) + 1;
    if (lineNum > bindLineCodes.size()) {
        return;
    }
    std::for_each(infoMap.rbegin(), infoMap.rend(), [&](auto& m) {
        if (second == 0) {
            second = m.first;
            return;
        }
        if (second - m.first > COMPRESS_LEN) {
            auto targetIter = bindLineCodes.begin() + m.first - base + COMPRESS_LEN;
            targetIter->line = 0;
            targetIter->meta = "...";
            bindLineCodes.erase(targetIter + 1, bindLineCodes.begin() + second - base);
        }
        second = m.first;
    });
}

CombinedLine DiagnosticEmitterImpl::CombineErrorPrintSingleLineHelper(
    const std::string& sourceLine, const CollectedInfo& info, bool isFirstLine) const
{
    CJC_ASSERT(!info.IsDefault());
    // Every source line have a newline character in the end, need add it to calculate the length.
    auto str = sourceLine + GetNewLine();
    auto space = GetSpaceBeforeTarget(str, info.range.begin.column);
    std::string line;
    auto charSize = GetDisplayedWidthFromSource(str, info.range);
    line += space;
    auto symbol = info.isMain ? MAIN_HINT_SYMBOL : OTHER_HINT_SYMBOL;
    auto begin = line.size();
    if (isFirstLine) {
        line += CharacterOfNum(charSize, symbol) + g_spaceOfNum(1);
    }
    line += info.hint;
    return {line, 0, false, {{begin, line.size(), info.color}}};
}

void DiagnosticEmitterImpl::CombineErrorPrintSingleLine(SourceCombinedVec& insertedStr,
    const CollectedInfo& info, const std::string& sourceLine)
{
    auto isFirstLine = insertedStr.empty();
    if (isFirstLine) {
        insertedStr.push_back(CombineErrorPrintSingleLineHelper(sourceLine, info, isFirstLine));
    } else if (info.hint.empty()) {
        // If hint message is empty, only need mark the source code.
        // Every source line have a newline character in the end, need add it to calculate the length.
        auto str = sourceLine + GetNewLine();
        auto spaceSize = GetSpaceBeforeTarget(str, info.range.begin.column).size();
        CJC_ASSERT(!insertedStr.empty() && "source combined vec cannot be empty");
        if (insertedStr.empty()) {
            return;
        }
        InsertSymbolInFirstLine(insertedStr[0], spaceSize, info, str);
    } else {
        auto tl = CombineErrorPrintSingleLineHelper(sourceLine, info, isFirstLine);
        auto spaceSize = tl.meta.find_first_not_of(SPACE);
        auto blackLine = g_spaceOfNum(spaceSize + 1);
        auto str = sourceLine + GetNewLine();
        insertedStr.push_back({blackLine, 0, {}});
        InsertSymbolToUpperLine(insertedStr, spaceSize, info, str);
        insertedStr.push_back(tl);
    }
}

void DiagnosticEmitterImpl::AnalyseMultiLineHanging(const CollectedInfo& info, size_t combinedVecSize)
{
    CJC_ASSERT(info.isMultiLine);
    auto loc = multiLineRecordMap[info];
    CJC_ASSERT(loc < multiLineHangingPtrVec.size() && "multiLineHangingPtrVec is less then loc");
    if (loc >= multiLineHangingPtrVec.size()) {
        return;
    }
    if (!info.isEnd) {
        auto tup = std::tuple<size_t, size_t, DiagColor>{combinedVecSize, 0, info.color};
        if (multiLineHangingVec.empty()) {
            multiLineHangingVec.emplace_back(std::vector<std::tuple<size_t, size_t, DiagColor>>{tup});
            multiLineHangingPtrVec[loc].first = 0;
            multiLineHangingPtrVec[loc].second = 0;
        } else {
            // Hanging column.
            auto c = std::find_if(multiLineHangingVec.rbegin(), multiLineHangingVec.rend(), [](auto& vec) {
                return std::get<1>(vec.back()) == 0;
            });
            if (c == multiLineHangingVec.rbegin()) {
                multiLineHangingVec.emplace_back(std::vector<std::tuple<size_t, size_t, DiagColor>>{tup});
                multiLineHangingPtrVec[loc].first = 0;
                multiLineHangingPtrVec[loc].second = multiLineHangingVec.size() - 1;
            } else {
                auto offset = c == multiLineHangingVec.crend() ? 0 : 1;
                auto index = c.base() - multiLineHangingVec.begin() + offset;
                CJC_ASSERT(index >= 0);
                size_t uIndex = static_cast<size_t>(index);
                CJC_ASSERT(uIndex < multiLineHangingVec.size() && "index overflow in vector");
                multiLineHangingVec[uIndex].push_back(tup);
                multiLineHangingPtrVec[loc].first = multiLineHangingVec[uIndex].size() - 1;
                multiLineHangingPtrVec[loc].second = uIndex;
            }
        }
    } else {
        auto [line, column] = multiLineHangingPtrVec[loc];
        CJC_ASSERT(column < multiLineHangingVec.size() && "column is bigger than size of multiLineHangingVec");
        if (column >= multiLineHangingVec.size()) {
            return;
        }
        CJC_ASSERT(line < multiLineHangingVec[column].size() && "line is bigger than size of multiLineHangingVec");
        if (line >= multiLineHangingVec[column].size()) {
            return;
        }
        auto [begin, end, color] = multiLineHangingVec[column][line];
        multiLineHangingVec[column][line] = std::make_tuple(begin, combinedVecSize + 1, color);
    }
}

CombinedLine DiagnosticEmitterImpl::CombineErrorPrintMultiLineHelper(
    const std::string& sourceLine, const CollectedInfo& info, bool isFirstLine, size_t combinedVecSize)
{
    if (info.IsDefault()) {
        rangeCheckError = true;
        if (enableRangeCheckICE) {
            InternalError("default range. there is something wrong before");
        }
        return {info.hint, 0, false, {{}}};
    }
    AnalyseMultiLineHanging(info, combinedVecSize);
    // Every source line have a newline character in the end, need add it to calculate the length.
    auto str = sourceLine + GetNewLine();
    auto space = GetSpaceBeforeTarget(str, info.range.begin.column);
    std::string line;
    line += CharacterOfNum(space.size(), MULTI_LINE_SYMBOL);
    auto symbol = info.isMain ? MAIN_HINT_SYMBOL : OTHER_HINT_SYMBOL;
    if (isFirstLine) {
        line += symbol;
        if (info.isEnd && !info.hint.empty()) {
            line += g_spaceOfNum(1) + info.hint;
        }
    } else {
        line += MULTI_LINE_KEY_SYMBOL;
    }

    return {line, 0, false, {{0, line.size(), info.color}}};
}

void DiagnosticEmitterImpl::CombineErrorPrintMultiLine(SourceCombinedVec& insertedStr,
    const CollectedInfo& info, const std::string& sourceLine, size_t combinedVecSize)
{
    auto isFirstLine = insertedStr.empty();
    if (isFirstLine) {
        insertedStr.push_back(CombineErrorPrintMultiLineHelper(sourceLine, info, isFirstLine, combinedVecSize));
    } else {
        auto tl = CombineErrorPrintMultiLineHelper(sourceLine, info, isFirstLine, combinedVecSize + insertedStr.size());
        auto len = tl.meta.size();
        auto str = sourceLine + GetNewLine();
        InsertSymbolToUpperLine(insertedStr, len - 1, info, str);
        insertedStr.emplace_back(tl);
        if (!info.hint.empty()) {
            insertedStr.push_back({g_spaceOfNum(len - 1), 0, false, {{}}});
        }
    }
}

bool DiagnosticEmitterImpl::CombineErrorPrint(CollectedInfoMap& infoMap, SourceCombinedVec& combinedVec)
{
    if (combinedVec.begin()->hasSourceFile) {
        CompressLineCode(infoMap, combinedVec);
    }

    for (auto& infos : std::as_const(infoMap)) {
        size_t i;
        for (i = 0; i < combinedVec.size(); i++) {
            if (combinedVec[i].line == infos.first) {
                break;
            }
        }
        CJC_ASSERT(i < combinedVec.size());
        if (i >= combinedVec.size()) {
            return false;
        }
        auto sourceLine = combinedVec[i].meta;
        SourceCombinedVec insertedStr;
        std::for_each(infos.second.rbegin(), infos.second.rend(), [&, this](auto& info) {
            if (info.isMultiLine) {
                // CombinedVecSize start from 1.
                CombineErrorPrintMultiLine(insertedStr, info, sourceLine, i + 1);
            } else {
                CombineErrorPrintSingleLine(insertedStr, info, sourceLine);
            }
        });
        combinedVec.insert(combinedVec.begin() + static_cast<int64_t>(i) + 1, insertedStr.begin(), insertedStr.end());
    }
    return true;
}

/* ascii control code
+-------+-------+----------------------------+------------------+---------------------+-------------------------------+
|   Dec | Hex   | Unicode Control Pictures   | Caret notation   | C escape sequence   | Name                          |
+=======+=======+============================+==================+=====================+===============================+
|     0 | 00    | NUL                        | ^@               |                     | Null                          |
|     1 | 01    | SOH                        | ^A               |                     | Start of Heading              |
|     2 | 02    | STX                        | ^B               |                     | Start of Text                 |
|     3 | 03    | ETX                        | ^C               |                     | End of Text                   |
|     4 | 04    | EOT                        | ^D               |                     | End of Transmission           |
|     5 | 05    | ENQ                        | ^E               |                     | Enquiry                       |
|     6 | 06    | ACK                        | ^F               |                     | Acknowledgement               |
|     7 | 07    | BEL                        | ^G               | \a                  | Bell                          |
|     8 | 08    | BS                         | ^H               | \b                  | Backspace                     |
|     9 | 09    | HT                         | ^I               | \t                  | Horizontal Tab                |
|    10 | 0A    | LF                         | ^J               | \n                  | Line Feed                     |
|    11 | 0B    | VT                         | ^K               | \v                  | Vertical Tab                  |
|    12 | 0C    | FF                         | ^L               | \f                  | Form Feed                     |
|    13 | 0D    | CR                         | ^M               | \r                  | Carriage Return               |
|    14 | 0E    | SO                         | ^N               |                     | Shift Out                     |
|    15 | 0F    | SI                         | ^O               |                     | Shift In                      |
|    16 | 10    | DLE                        | ^P               |                     | Data Link Escape              |
|    17 | 11    | DC1                        | ^Q               |                     | Device Control 1 (often XON)  |
|    18 | 12    | DC2                        | ^R               |                     | Device Control 2              |
|    19 | 13    | DC3                        | ^S               |                     | Device Control 3 (often XOFF) |
|    20 | 14    | DC4                        | ^T               |                     | Device Control 4              |
|    21 | 15    | NAK                        | ^U               |                     | Negative Acknowledgement      |
|    22 | 16    | SYN                        | ^V               |                     | Synchronous Idle              |
|    24 | 18    | CAN                        | ^X               |                     | Cancel                        |
|    25 | 19    | EM                         | ^Y               |                     | End of Medium                 |
|    26 | 1A    | SUB                        | ^Z               |                     | Substitute                    |
|    27 | 1B    | ESC                        | ^[               | \e                  | Escape                        |
|    28 | 1C    | FS                         | ^\               |                     | File Separator                |
|    29 | 1D    | GS                         | ^]               |                     | Group Separator               |
|    30 | 1E    | RS                         | ^^               |                     | Record Separator              |
|    31 | 1F    | US                         | ^_               |                     | Unit Separator                |
|   127 | 7F    | DEL                        | ^?               |                     | Delete                        |
+-------+-------+----------------------------+------------------+---------------------+-------------------------------+
*/
void DiagnosticEmitterImpl::HandleUnprintableChar(SourceCombinedVec& combinedVec) const
{
    const int lengthOfDelay = 7;
    for (auto& combinedLine : combinedVec) {
        std::string& sourceCode = combinedLine.meta;
        std::string result;
        auto& colors = combinedLine.colors;
        char forwardChar;
        char backwardChar;
        size_t index;
        for (size_t i = 0; i < sourceCode.size(); i++) {
            // forward
            forwardChar = sourceCode[i];
#if defined __GNUC__ && not defined __clang__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wtype-limits"
#endif
            if ((0 <= forwardChar && forwardChar <= 0x8) || (0xb <= forwardChar && forwardChar <= 0x1f) ||
                forwardChar == 0x7f) {
                result += "\\u{" + ToHexString(forwardChar, NORMAL_CODEPOINT_LEN) + "}";
            } else {
                result += forwardChar;
            }

            // backward
            index = sourceCode.size() - (i + 1);
            backwardChar = sourceCode[index];
            if ((0 <= backwardChar && backwardChar <= 0x8) || (0xb <= backwardChar && backwardChar <= 0x1f) ||
                backwardChar == 0x7f) {
#if defined __GNUC__ && not defined __clang__
#pragma GCC diagnostic pop
#endif
                std::for_each(colors.begin(), colors.end(), [=](auto& tup) {
                    if (std::get<0>(tup) > index) {
                        std::get<0>(tup) += lengthOfDelay;
                    }
                    if (std::get<1>(tup) > index) {
                        std::get<1>(tup) += lengthOfDelay;
                    }
                });
                colors.emplace_back(index, index + lengthOfDelay + 1, DiagColor::REVERSE);
            }
        }
        combinedLine.meta = result;
    }
}

void DiagnosticEmitterImpl::EmitErrorMessage(DiagColor color, const std::string& err, const std::string& mes)
{
    color = noColor ?  NO_COLOR : color;
    CJC_ASSERT(ColorPrintMap.find(color) != ColorPrintMap.end() && "no print color found");
    if (ColorPrintMap.find(color) == ColorPrintMap.end()) {
        return;
    }
    out << ColorPrintMap.at(color);
    out << err;
    if (color != DiagColor::NO_COLOR) {
        out << ANSI_COLOR_RESET;
    }
    out << ":" << g_spaceOfNum(1) << mes << std::endl;
}

void DiagnosticEmitterImpl::EmitErrorLocation(const Position& pos)
{
    CJC_ASSERT(!pos.IsZero());
    if (!sm.IsSourceFileExist(pos.fileID)) {
        return;
    }
    auto prefix = CharacterOfNum(maxLineNum, SPACE);
    auto color = noColor ? NO_COLOR : OTHER_HINT_COLOR;
    prefix += GetColoredString(color, GetLineSymbol());
    prefix += g_spaceOfNum(1);
    auto source = sm.GetSource(pos.fileID);
    std::string path;
    if (source.packageName.has_value()) {
        path = "(package " + source.packageName.value() + ")" + FileUtil::GetFileName(source.path);
    } else {
        path = source.path;
    }
    out << prefix << path << ":" << pos.line << ":" << pos.column << ":" << std::endl;
}

void DiagnosticEmitterImpl::ConvertHangingContentsHelper(
    HangingStr& hanging, size_t i, size_t begin, size_t end, const DiagColor& color) const
{
    auto k = i + 1;
    while (k < hanging.size()) {
        CJC_ASSERT(begin < hanging[k].size() && "begin is bigger than hanging size");
        if (begin >= hanging[k].size()) {
            break;
        }
        if (hanging[k][begin] == g_spaceOfNum(1)) {
            hanging[k][begin] = GetColoredString(color, std::string{MULTI_LINE_SYMBOL});
        }
        CJC_ASSERT(end - 1 < hanging[k].size() && "end is bigger than hanging size");
        if (end - 1 >= hanging[k].size()) {
            break;
        }
        if (hanging[k][end - 1] == g_spaceOfNum(1)) {
            hanging[k][end - 1] = GetColoredString(color, std::string{MULTI_LINE_SYMBOL});
        }
        k++;
    }
    
    k = begin + 1;
    while (k < end) {
        CJC_ASSERT(k < hanging[i].size() && "begin is bigger than hanging size");
        if (k >= hanging[i].size()) {
            break;
        }
        if (hanging[i][k] == g_spaceOfNum(1)) {
            hanging[i][k] = GetColoredString(color, std::string{MULTI_LINE_KEY_SYMBOL});
        }
        k++;
    }
}

HangingStr DiagnosticEmitterImpl::ConvertHangingContents(size_t line)
{
    if (multiLineHangingVec.empty()) {
        return HangingStr{};
    }
    HangingStr hanging(multiLineHangingVec.size() + 1, std::vector<std::string>(line, g_spaceOfNum(1)));
    for (size_t i = 0; i < multiLineHangingVec.size(); i++) {
        for (size_t j = 0; j < multiLineHangingVec[i].size(); j++) {
            auto [begin, end, color] = multiLineHangingVec[i][j];
            color = noColor ? DiagColor::NO_COLOR : color;
            CJC_ASSERT(begin != end - 1);
            ConvertHangingContentsHelper(hanging, i, begin, end, color);
        }
    }
    multiLineHangingVec.clear();
    multiLineHangingPtrVec.clear();
    multiLineRecordMap.clear();
    return hanging;
}

void DiagnosticEmitterImpl::EmitSourceCode(SourceCombinedVec& combinedVec)
{
    auto hanging = ConvertHangingContents(combinedVec.size());
    // Add a empty line.
    auto line = g_spaceOfNum(maxLineNum + 1);
    DiagColor color = noColor ? NO_COLOR : OTHER_HINT_COLOR;
    line += GetColoredString(color, std::string{BITOR} + g_spaceOfNum(1), false);
    out << line << std::endl;

    // Source code.
    for (size_t i = 0; i < combinedVec.size(); i++) {
        auto [sourceLine, lineHere, hasSourceFile, _] = combinedVec[i];
        HandleSpecialCharacters(sourceLine);
        std::string str;
        if (lineHere != 0 && hasSourceFile) {
            color = noColor ? NO_COLOR : OTHER_HINT_COLOR;
            str += GetColoredString(color, std::to_string(lineHere));
            str += CharacterOfNum(maxLineNum - std::to_string(lineHere).length() + 1, SPACE);
        } else {
            str += CharacterOfNum(maxLineNum + 1, SPACE);
        }
        color = noColor ? NO_COLOR : OTHER_HINT_COLOR;
        str += GetColoredString(color, std::string{BITOR} + g_spaceOfNum(1), false);
        if (!hanging.empty()) {
            for (auto& vec : hanging) {
                str += vec[i];
            }
        }
        str += sourceLine;
        out << str << std::endl;
    }
    // Add a empty line.
    out << line << std::endl;
}

void DiagnosticEmitterImpl::EmitSingleNoteWithSource(SubDiagnostic& note)
{
    EmitErrorMessage(note.mainHint.color, "note", note.subDiagMessage);

    std::vector<CollectedInfo> errorInfo;
    CollectInformation(errorInfo, note.mainHint, true);
    std::for_each(note.otherHints.begin(), note.otherHints.end(), [&, this](auto& str) {
        CollectInformation(errorInfo, str, false);
    });

    SortAndCheck(errorInfo);

    maxLineNum = GetMaxLineColumn(errorInfo);

    if (note.mainHint.range.begin != DEFAULT_POSITION) {
        EmitErrorLocation(note.mainHint.range.begin);
    }
    
    ConstructAndEmitSourceCode(errorInfo);

    std::vector<DiagHelp> hs{note.help};
    if (!GetSourceCode(errorInfo).empty() && !note.help.IsDefault()) {
        EmitHelp(hs);
    }
}

SubstitutionMap DiagnosticEmitterImpl::HelpSubstitutionToMap(DiagHelp& help) const
{
    // Consider multiple-line?
    std::sort(help.substitutions.begin(), help.substitutions.end(), [](auto& a, auto& b) {
        return a.range.begin < b.range.begin;
    });

    SubstitutionMap subMap;

    std::for_each(help.substitutions.begin(), help.substitutions.end(), [&](auto& info) {
        subMap[static_cast<unsigned>(info.range.begin.line)].push_back(info);
    });
    return subMap;
}

void DiagnosticEmitterImpl::HelpSubstituteConvertHelper(SubstitutionMap& subMap, std::string& rawStr, unsigned int line,
    std::vector<CollectedInfo>& infos) const
{
    for (auto& sub : subMap[line]) {
        CollectedInfo info;
        auto range = sub.range;
        CJC_ASSERT(range.end.line == range.begin.line);
        auto size = range.end.column - range.begin.column;
        CJC_ASSERT(size >= 0);
        CJC_ASSERT(range.begin.column > 0);
        CJC_ASSERT(static_cast<size_t>(range.begin.column - 1) < rawStr.size() &&
                   "column is bigger than size of rawStr");
        rawStr.replace(static_cast<size_t>(range.begin.column - 1), static_cast<size_t>(size), sub.str);
        info.range = MakeRange(range.begin, range.begin + Position{0, 0, static_cast<int>(sub.str.size())});
        info.color = HELP_COLOR;
        infos.push_back(info);
    }
}

std::vector<CollectedInfo> DiagnosticEmitterImpl::HelpSubstituteConvert(
    DiagHelp& help, SourceCombinedVec& combinedVec) const
{
    auto subMap = HelpSubstitutionToMap(help);
    std::vector<CollectedInfo> infos;
    for (auto& [rawStr, line, hasSourcefile, _] : combinedVec) {
        if (subMap.count(line) > 0) {
            // Synchronize location when multi-substitution on single line.
            HelpSubstituteConvertHelper(subMap, rawStr, line, infos);
        }
    }
    return infos;
}

SourceCombinedVec DiagnosticEmitterImpl::GetHelpSubstituteSource(DiagHelp& help)
{
    CJC_ASSERT(!help.substitutions.empty());
    // Consider multi-line source.
    auto start = help.substitutions[0].range.begin;
    auto source = sm.GetContentBetween(
        start.fileID, Position(start.line, 1), Position(start.line, std::numeric_limits<int>::max()));
    if (source.empty()) {
        return {};
    }
    // If last character is newline, delete it.
    auto windowsTerminatorSize = std::string("\r\n").size();
    if (source.size() >= windowsTerminatorSize && source[source.size() - windowsTerminatorSize] == '\r' &&
        source.back() == '\n') {
        source = source.substr(0, source.size() - windowsTerminatorSize);
    } else if (source.back() == '\n' || source.back() == '\r') {
        source = source.substr(0, source.size() - 1);
    }
    auto hasSourceFile = sm.IsSourceFileExist(start.fileID);
    return SourceCombinedVec{{source, static_cast<unsigned>(start.line), hasSourceFile, {{}}}};
}

void DiagnosticEmitterImpl::EmitSingleHelpWithSource(DiagHelp& help)
{
    DiagColor color = noColor ? NO_COLOR : HELP_COLOR;
    std::string message = GetColoredString(color, "help");
    message += CharacterOfNum(1, COLON);
    message += g_spaceOfNum(1);
    message += help.helpMes;
    out << message + std::string{COLON} << std::endl;

    auto source = GetHelpSubstituteSource(help);
    if (source.empty()) {
        return;
    }
    // For macro replaced tokens, all positions are wrong.
    if (static_cast<int>(source.back().meta.size()) < help.substitutions.back().range.begin.column) {
        return;
    }

    auto collectedInfos = HelpSubstituteConvert(help, source);
    auto infoMap = StoreInfoToMap(collectedInfos);
    if (!CombineErrorPrint(infoMap, source)) {
        return;
    }

    HandleUnprintableChar(source);
    
    if (!noColor) {
        ColorizeCombinedVec(source);
    }

    EmitSourceCode(source);
}

void DiagnosticEmitterImpl::EmitSingleMessageWithoutSource(const std::string& str, std::string host)
{
    std::string combined = g_spaceOfNum(maxLineNum + 1);
    auto color = noColor ? NO_COLOR : OTHER_HINT_COLOR;
    combined += GetColoredString(color, std::string{WITHOUT_SOURCE_SYMBOL});
    combined += g_spaceOfNum(1);
    color = noColor ? NO_COLOR : DiagColor::RESET;
    combined += GetColoredString(color, std::move(host), false);
    combined += CharacterOfNum(1, COLON);
    combined += g_spaceOfNum(1);
    combined += str;
    out << combined << std::endl;
}

void DiagnosticEmitterImpl::EmitNote()
{
    if (diag.subDiags.empty()) {
        return;
    }
    // Resort all notes, the note not printed source code is being front of those printed source.
    std::partition(diag.subDiags.begin(), diag.subDiags.end(), [](auto& subDiag) {
        return !subDiag.IsShowSource();
    });

    std::for_each(diag.subDiags.begin(), diag.subDiags.end(), [this](auto& subDiag) {
        if (diag.curMacroCall && subDiag.subDiagMessage == MACROCALL_CODE) {
            auto pInvocation = diag.curMacroCall->GetInvocation();
            if (!pInvocation || pInvocation->hasShownCode) {
                return;
            }
            pInvocation->hasShownCode = true;
        }
        subDiag.IsShowSource() ? EmitSingleNoteWithSource(subDiag) :
                               EmitSingleMessageWithoutSource(subDiag.subDiagMessage, "note");
    });
}

void DiagnosticEmitterImpl::EmitHelp(std::vector<DiagHelp>& helps)
{
    if (helps.empty()) {
        return;
    }
    // Resort all helps, the note not printed source code is being front of those printed source.
    std::partition(helps.begin(), helps.end(), [](auto& help) {
        return !help.IsShowSource();
    });

    std::for_each(helps.begin(), helps.end(), [this](auto& help) {
        help.IsShowSource() ? EmitSingleHelpWithSource(help) :
                              EmitSingleMessageWithoutSource(help.helpMes, "help");
    });
}

void DiagnosticEmitterImpl::ColorizeCombinedVec(SourceCombinedVec& combinedVec) const
{
    // Get second element of tuple. And iterates negative two element when colorizes.
    static const size_t two = 2;
    for (auto& combinedLine : combinedVec) {
        // Sort all. Make sure bigger ranges are before included one.
        std::sort(combinedLine.colors.begin(), combinedLine.colors.end(), [](auto& c1, auto& c2) {
            if (std::get<0>(c1) == std::get<0>(c2)) {
                CJC_ASSERT(std::get<1>(c1) != std::get<1>(c2));
                return std::get<1>(c1) > std::get<1>(c2);
            }
            return std::get<0>(c1) < std::get<0>(c2);
        });

        // Make colorized list.
        std::vector<DiagColor> vec{combinedLine.meta.size(), DiagColor::RESET};
        std::for_each(combinedLine.colors.begin(), combinedLine.colors.end(), [&](auto& c) {
            for (auto i = std::get<0>(c); i < std::get<1>(c); i++) {
                vec[i] = std::get<two>(c);
            }
        });

        // Colorize.
        if (vec.size() >= two) {
            for (size_t i = vec.size() - 1; i > 0; i--) {
                if (vec[i - 1] != vec[i]) {
                    combinedLine.meta.insert(i, ColorPrintMap.at(vec[i]));
                }
                if (i == 1 && vec[i - 1] != DiagColor::RESET) {
                    combinedLine.meta.insert(0, ColorPrintMap.at(vec[0]));
                }
            }
        } else if (vec.size() == 1 && vec[0] != DiagColor::RESET) {
            combinedLine.meta.insert(0, ColorPrintMap.at(vec[0]));
        }
        combinedLine.meta.insert(combinedLine.meta.length(), ColorPrintMap.at(DiagColor::RESET));
    }
}

void DiagnosticEmitterImpl::ConstructAndEmitSourceCode(std::vector<CollectedInfo>& errorInfo)
{
    CJC_ASSERT(!errorInfo.empty());
    auto hasSourceFile = sm.IsSourceFileExist(errorInfo.begin()->range.begin.fileID);
    auto infoMap = StoreInfoToMap(errorInfo);
    auto lineCodes = Utils::SplitLines(GetSourceCode(errorInfo));
    // The libast using parser don't have the source line.
    if (lineCodes.empty()) {
        return;
    }
    // The last column is newline, delete it.
    if (lineCodes.back().empty()) {
        lineCodes.pop_back();
    }
    CJC_ASSERT(!infoMap.empty() && "info map is empty");
    if (infoMap.empty()) {
        return;
    }
    auto base = infoMap.begin()->first;
    SourceCombinedVec combinedVec;
    std::for_each(lineCodes.begin(), lineCodes.end(), [&](auto& line) {
        combinedVec.push_back({line, base++, hasSourceFile, {{}}});
    });

    if (!CombineErrorPrint(infoMap, combinedVec)) {
        return;
    }

    HandleUnprintableChar(combinedVec);
    
    if (!noColor) {
        ColorizeCombinedVec(combinedVec);
    }

    EmitSourceCode(combinedVec);
}

namespace {
bool IsTextOnlyWarning(const WarnGroup& warnGroup)
{
    return (warnGroup == WarnGroup::DRIVER_ARG || warnGroup == WarnGroup::UNSUPPORT_COMPILE_SOURCE);
}
}

bool DiagnosticEmitterImpl::Emit(bool enableOnlyHint)
{
    const std::map<DiagSeverity, std::string_view> seveToStr = {
        {DiagSeverity::DS_ERROR, "error"}, {DiagSeverity::DS_WARNING, "warning"},
        {DiagSeverity::DS_NOTE, "note"},
    };
    if (seveToStr.find(diag.diagSeverity) != seveToStr.end()) {
        if (!enableOnlyHint) {
            EmitErrorMessage(diag.mainHint.color, std::string(seveToStr.at(diag.diagSeverity)), diag.errorMessage);
        }
    } else {
        CJC_ABORT();
    }
    // Since some warnings are text-only, which means they do not have invalid position, so emit notes here.
    if (IsTextOnlyWarning(diag.warnGroup)) {
        EmitNote();
    }
    if (diag.mainHint.range.begin == DEFAULT_POSITION) {
        return !rangeCheckError;
    }
    std::vector<CollectedInfo> errorInfo;

    CollectInformation(errorInfo, diag.mainHint, true);
    std::for_each(diag.otherHints.begin(), diag.otherHints.end(), [&, this](auto& str) {
        CollectInformation(errorInfo, str, false);
    });

    CJC_ASSERT(!errorInfo.empty());

    SortAndCheck(errorInfo);

    maxLineNum = GetMaxLineColumn(errorInfo);

    EmitErrorLocation(diag.mainHint.range.begin);

    ConstructAndEmitSourceCode(errorInfo);

    EmitNote();

    EmitHelp(diag.helps);

    out << std::endl; // Print empty line after a error.

    return !rangeCheckError;
}

DiagnosticEmitter::DiagnosticEmitter(
    Diagnostic& d, bool nc, bool enableRangeCheckICE, std::basic_ostream<char>& o, SourceManager& sourceManager)
    : impl{new DiagnosticEmitterImpl{d, nc, enableRangeCheckICE, o, sourceManager}}
{
}
DiagnosticEmitter::~DiagnosticEmitter()
{
    delete impl;
}
bool DiagnosticEmitter::Emit(bool enableOnlyHint) const
{
    return impl->Emit(enableOnlyHint);
}
}
