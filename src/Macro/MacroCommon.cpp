// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements macro utility api.
 */

#include "cangjie/Macro/MacroCommon.h"

#include <fstream>
#include <string>
#include <vector>

#include "cangjie/Basic/Print.h"
#include "cangjie/Basic/Utils.h"
#include "cangjie/Lex/Lexer.h"
#include "cangjie/Modules/ImportManager.h"
#include "cangjie/Utils/FileUtil.h"
#include "cangjie/Macro/TokenSerialization.h"
#include "cangjie/Basic/DiagnosticEmitter.h"

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <windows.h>
#elif defined(__linux__) || defined(__APPLE__)
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace Cangjie;
using namespace AST;

namespace {
const int SPACE_NUM = 4;

bool IsPreEscapeBackslash(const std::string& value)
{
    char backslash = '\\';
    auto count = 0;
    for (auto ch = value.rbegin(); ch != value.rend(); ++ch) {
        if (*ch == backslash) {
            count++;
        } else {
            break;
        }
    }
    auto mod2 = 2;
    // count mod 2 is odd means the previous symbol is escaped backslash
    return (count % mod2 == 0) ? true : false;
}

std::string ProcessQuotaMarksForSingle(const std::string& value)
{
    bool inDollar{false};
    auto lCurlCnt = 0;
    // Quotation marks that are in interpolation do not need to be escaped, like "${"abc"}".
    std::string ret;
    for (const char ch : value) {
        if (!ret.empty() && ret.back() == '$' && ch == '{') {
            inDollar = true;
        }
        if (inDollar && ch == '{') {
            lCurlCnt++;
        }
        if (inDollar && ch == '}') {
            if (lCurlCnt > 0) {
                lCurlCnt--;
            }
            if (lCurlCnt == 0) {
                inDollar = false;
            }
        }
        if (inDollar) {
            ret += ch;
            continue;
        }
        // Special marks that are not in interpolation need to be escaped once.
        if (ch == '\"' && (ret.back() != '\\' || IsPreEscapeBackslash(ret))) {
            ret += "\\\"";
        } else if (ch == '\r' && ret.back() != '\\') {
            ret += "\\r";
        } else if (ch == '\n' && ret.back() != '\\') {
            ret += "\\n";
        } else {
            ret += ch;
        }
    }
    return ret;
}

std::string RepeatString(const std::string str, int times)
{
    std::string ret;
    for (int i = 0; i < times; i++) {
        ret += str;
    }
    return ret;
}

std::string GetMultiStringValue(const Token& tk)
{
    // """\nabc"""
    return R"(""")" + Utils::GetLineTerminator() + ProcessQuotaMarks(tk.Value()) + R"(""")";
}

std::string GetMultiRawStringValue(const Token& tk)
{
    auto value = tk.Value();
    auto delimiter = RepeatString("#", static_cast<int>(tk.delimiterNum));
    // ###"xxx"#yyy"###
    return delimiter + "\"" + value + "\"" + delimiter;
}

bool AddSpace(TokenVector& line, size_t column)
{
    if (line.empty() || column >= line.size() - 1) {
        return false;
    }
    auto curToken = line[column];
    auto nextToken = line[column + 1];
    return CheckAddSpace(curToken, nextToken);
}
} // namespace

namespace Cangjie {
bool CheckAddSpace(Token curToken, Token nextToken)
{
    // Add no space after current token.
    static std::set<TokenKind> noSpaceKinds1{TokenKind::DOT, TokenKind::QUEST, TokenKind::DOLLAR, TokenKind::LPAREN,
        TokenKind::LSQUARE, TokenKind::AT, TokenKind::AT_EXCL, TokenKind::ILLEGAL, TokenKind::NL};
    if (noSpaceKinds1.find(curToken.kind) != noSpaceKinds1.end()) {
        return false;
    }
    // Add no space before next token.
    static std::set<TokenKind> noSpaceKinds2{TokenKind::DOT, TokenKind::COLON, TokenKind::COMMA, TokenKind::SEMI,
        TokenKind::QUEST, TokenKind::LPAREN, TokenKind::RPAREN, TokenKind::LSQUARE, TokenKind::RSQUARE, TokenKind::NL,
        TokenKind::END};
    if (noSpaceKinds2.find(nextToken.kind) != noSpaceKinds2.end()) {
        return false;
    }
    // Add no space between current token and next token.
    static std::set<std::pair<TokenKind, TokenKind>> tkSet = {
        std::pair<TokenKind, TokenKind>(TokenKind::GT, TokenKind::GT),
        std::pair<TokenKind, TokenKind>(TokenKind::GT, TokenKind::ASSIGN),
        std::pair<TokenKind, TokenKind>(TokenKind::QUEST, TokenKind::QUEST),
        std::pair<TokenKind, TokenKind>(TokenKind::LPAREN, TokenKind::RPAREN),
        std::pair<TokenKind, TokenKind>(TokenKind::LSQUARE, TokenKind::RSQUARE),
        std::pair<TokenKind, TokenKind>(TokenKind::IDENTIFIER, TokenKind::NOT),
        std::pair<TokenKind, TokenKind>(TokenKind::BITNOT, TokenKind::INIT)};
    return tkSet.find(std::pair<TokenKind, TokenKind>(curToken.kind, nextToken.kind)) == tkSet.end();
}
} // namespace Cangjie

bool MacroFormatter::SeeCurlyBracket(const TokenVector& lineOfTk, const TokenKind& tk) const
{
    TokenKind other;
    TokenVector temp = lineOfTk;
    if (tk == TokenKind::RCURL) {
        other = TokenKind::LCURL;
    } else if (tk == TokenKind::LCURL) {
        other = TokenKind::RCURL;
        // Look for bracket from backwards.
        std::reverse(temp.begin(), temp.end());
    } else {
        return false;
    }
    // Return true if see a single bracket in lineOfTk.
    for (auto itr = temp.begin(); itr != temp.end(); ++itr) {
        if ((*itr).kind == other) {
            return false;
        }
        if ((*itr).kind == tk) {
            return true;
        }
    }
    return false;
}

void MacroFormatter::PushIntoLines()
{
    TokenVector tmp;
    for (const auto& tok : input) {
        tmp.push_back(tok);
        if (tok.kind == TokenKind::NL) {
            lines.push_back(tmp);
            tmp.clear();
        }
    }
    if (!tmp.empty()) {
        lines.push_back(tmp);
        tmp.clear();
    }
    return;
}

void MacroFormatter::LinesToString(bool hasComment)
{
    // Determine the initial indentation.
    if (lines.empty()) {
        return;
    }
    auto genIndent = [](int times) {
        std::string tab = "    ";
        return RepeatString(tab, times);
    };
    auto initialIndent = (offset - 1) / SPACE_NUM;
    auto indentation = 0;
    for (size_t i = 0; i < lines.size(); i++) {
        if (lines[i].size() == 0) {
            continue;
        }
        if (hasComment && lines[i].size() > 1) {
            if (i == 0) {
                retStr += lines[i][0].Value();
            } else {
                retStr += genIndent(initialIndent) + lines[i][0].Value();
            }
            (void)lines[i].erase(lines[i].begin());
        }
        auto lineStr = LineToString(lines[i]);
        if (i == 0) {
            retStr += genIndent(indentation) + lineStr;
            continue;
        }
        // Determine the indentation.
        if (!lines[i - 1].empty() && SeeCurlyBracket(lines[i - 1], TokenKind::LCURL)) {
            indentation += 1; // Right ident when see "{" last line.
        }
        if (!lines[i].empty() && SeeCurlyBracket(lines[i], TokenKind::RCURL)) {
            indentation -= 1; // Left ident when see "}" this line.
        }
        retStr += genIndent(indentation) + lineStr;
    }
}

const std::string MacroFormatter::Produce(bool hasComment)
{
    PushIntoLines();
    LinesToString(hasComment);
    return retStr;
}

namespace Cangjie {
std::string LineToString(TokenVector& line)
{
    // Maybe more cases to deal with.
    std::string ret;
    auto size = line.size();
    for (size_t i = 0; i < size; i++) {
        auto blank = AddSpace(line, i) ? " " : "";
        auto token = line[i];
        std::string quote = token.isSingleQuote ? "\'" : "\"";
        if (token.kind == TokenKind::STRING_LITERAL) {
            // For case like: let s = "hello world\n"
            ret += quote + ProcessQuotaMarksForSingle(token.Value()) + quote + blank;
        } else if (token.kind == TokenKind::JSTRING_LITERAL) {
            ret += "J" + quote + ProcessQuotaMarksForSingle(token.Value()) + quote + blank;
        } else if (token.kind == TokenKind::RUNE_LITERAL) {
            // For case: let c = r'\''
            ret += ((token == "\'") ? "r\'\\'\'" : "r" + quote + token.Value() + quote) + blank;
        } else if (token.kind == TokenKind::MULTILINE_STRING) {
            ret += GetMultiStringValue(token) + blank;
        } else if (token.kind == TokenKind::MULTILINE_RAW_STRING) {
            ret += GetMultiRawStringValue(token) + blank;
        } else if (token.kind == TokenKind::NL) {
            ret += Utils::GetLineTerminator();
        } else {
            ret += line[i].Value() + blank;
        }
    }
    return ret;
}

/**
 * Use Lexer to get the tokens from the string.
 */
std::vector<Token> GetTokensFromString(const std::string& source, DiagnosticEngine& diag, const Position pos)
{
    if (pos == INVALID_POSITION) {
        Lexer lexer(source, diag, diag.GetSourceManager());
        return lexer.GetTokens();
    }
    Lexer lexer(pos.fileID, source, diag, diag.GetSourceManager(), pos);
    return lexer.GetTokens();
}

bool IsCurFile(const SourceManager& sm, const Token& tk, unsigned int fileID)
{
    if (tk.kind == TokenKind::NL || tk.kind == TokenKind::COMMENT) {
        return true;
    }
    if (fileID != 0 && fileID != tk.Begin().fileID) {
        return false;
    }
    auto begin = tk.Begin();
    auto end = tk.End();
    auto content = sm.GetContentBetween(begin, end);
    // For case like: content is "\"name\"" and tk.Value() is "name".
    if (tk == content || content == "\"" + tk.Value() + "\"") {
        return true;
    }
    // For case like: content is "r's'" and tk.value is "s".
    if (tk.kind == TokenKind::RUNE_LITERAL && content == "r\'" + tk.Value() + "\'") {
        return true;
    }
    // For case like: content is "###"abc#xyz"###" and tk.Value() is "abc#xyz".
    if (tk.kind == TokenKind::MULTILINE_RAW_STRING && content == GetMultiRawStringValue(tk)) {
        return true;
    }
    // For case like: content is """\nabc"""
    if (tk.kind == TokenKind::MULTILINE_STRING && content == GetMultiStringValue(tk)) {
        return true;
    }
    return false;
}

/**
 * MacroExpand failed with return TokenKind::ILLEGAL, this function is used to check it.
 */
bool MacroExpandFailed(const std::vector<Token>& retTokens)
{
    if (retTokens.size() == 1 && retTokens[0].kind == TokenKind::ILLEGAL) {
        return true;
    }
    return false;
}
}
