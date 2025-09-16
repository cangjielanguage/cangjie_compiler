// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include <fstream>
#include <algorithm>

#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Frontend/CompilerInstance.h"
#include "cangjie/Macro/MacroEvaluation.h"
#include "cangjie/Macro/MacroExpansion.h"
#include "cangjie/Lex/Lexer.h"

using namespace Cangjie;
using namespace AST;

namespace {
bool IsSpecialToken(TokenKind prevTokenKind, TokenKind curTokenKind)
{
    // For case: A() A[] A<> A?? >> >= ~init.
    static std::set<TokenKind> tkSet0 = {
        TokenKind::LPAREN, TokenKind::RPAREN, TokenKind::LSQUARE, TokenKind::RSQUARE, TokenKind::LT, TokenKind::QUEST};
    static std::set<std::pair<TokenKind, TokenKind>> tkSet1 = {
        std::pair<TokenKind, TokenKind>(TokenKind::GT, TokenKind::GT),
        std::pair<TokenKind, TokenKind>(TokenKind::GT, TokenKind::ASSIGN),
        std::pair<TokenKind, TokenKind>(TokenKind::BITNOT, TokenKind::INIT)};
    return tkSet0.find(curTokenKind) != tkSet0.end() ||
        tkSet1.find(std::pair<TokenKind, TokenKind>(prevTokenKind, curTokenKind)) != tkSet1.end();
}

void ReportMappingFailedDiag(
    std::vector<Token>& oldTokens, std::vector<Token>& newTokens, const Position mcPos, CompilerInstance& ci)
{
    auto oldSize = oldTokens.size();
    auto newSize = newTokens.size();
    auto minSize = oldSize < newSize ? oldSize : newSize;
    for (size_t i = 0; i < minSize; ++i) {
        // not check newline
        if (oldTokens[i].kind == TokenKind::NL && newTokens[i].kind == TokenKind::NL) {
            continue;
        }
        if (oldTokens[i].kind != newTokens[i].kind || oldTokens[i].Value() != newTokens[i].Value()) {
            if (oldTokens[i].Value().empty()) {
                (void)ci.diag.Diagnose(mcPos, DiagKind::macro_call_map_to_empty_value_token,
                    TOKEN_KIND_VALUES[static_cast<int>(oldTokens[i].kind)]);
                break;
            }
            (void)ci.diag.Diagnose(
                mcPos, DiagKind::macro_call_map_info_failed, oldTokens[i].Value(), newTokens[i].Value());
            break;
        }
    }
    return;
}

size_t GetTokenLenth(const Token& token)
{
    return GetTokenLength(token.Value().size(), token.kind, token.delimiterNum);
}

void CollectMacroDebugPosition(
    MacroInvocation& invocation, const Token& token, unsigned int lastColumn)
{
    if (!token.Begin().isCurFile || token.kind == TokenKind::COMMENT) {
        return;
    }
    auto key = token.Begin().Hash64();
    if (invocation.origin2newPosMap.find(key) != invocation.origin2newPosMap.end()) {
        return;
    }
    // Collect the position information of tokens from macrocalls for debugging purposes.
    if (token.kind == TokenKind::NL) {
        if (!invocation.macroDebugMap.empty()) {
            auto lastPos = invocation.macroDebugMap.begin()->second;
            // If the last token is newline, then the lastPos's column is 1, and it only needs to be collected once.
            if (lastPos.column != 1) {
                invocation.macroDebugMap[lastColumn] = Position{lastPos.fileID, lastPos.line + 1, 1};
            }
        }
        return;
    }
    // Other tokens except newline.
    if (!invocation.macroDebugMap.empty()) {
        auto lastPos = invocation.macroDebugMap.begin()->second;
        if (lastPos == token.Begin()) {
            // No need to collect the same position.
            return;
        }
        // If the last token is newline, replace the last position with the position of current token.
        if (lastPos.column == 1) {
            invocation.macroDebugMap.begin()->second = token.Begin();
            invocation.macroDebugMap[static_cast<unsigned>(lastColumn + GetTokenLenth(token))] = token.End();
            return;
        }
    }
    invocation.macroDebugMap[lastColumn] = token.Begin();
    invocation.macroDebugMap[static_cast<unsigned>(lastColumn + GetTokenLenth(token))] = token.End();
}

static void HandleInterplationStringPosMap(Token& strToken, Position newStartPos,
                                           const Ptr<MacroInvocation> pMacroInvocation,
                                           const CompilerInstance& ci,
                                           bool isMacroMap = false)
{
    std::vector<Token> tokens{strToken};
    Lexer lexer(tokens, ci.diag, ci.diag.GetSourceManager());
    auto strParts = lexer.GetStrParts(strToken);
    Position deltaBegin = newStartPos - strToken.Begin();
    // multi_string start in newline
    auto multiStrDeltaLine = 1;
    for (auto& strP : strParts) {
        if (strP.strKind == StringPart::STR && strToken.kind == TokenKind::MULTILINE_STRING) {
            if (strP.value.find("\n") != std::string::npos) {
                multiStrDeltaLine += std::count(strP.value.begin(), strP.value.end(), '\n');
            }
            continue;
        } else if (strP.strKind != StringPart::EXPR) {
            continue;
        }
        std::vector<Token> tks = GetTokensFromString(strP.value, ci.diag, strP.begin);
        // first token must be `$` and second token must be `{`
        CJC_ASSERT(!tks.empty());
        for (size_t i = 2; i < tks.size() - 1; i++) {
            auto &t = tks[i];
            if (t.kind == TokenKind::COMMENT) {
                continue;
            }
            t.SetCurFile(strToken.Begin().isCurFile);
            Position newPos = t.Begin() + deltaBegin;
            if (isMacroMap) {
                t.SetCurFile(strToken.Begin().isCurFile);
                auto key = pMacroInvocation->isCurFile ? newPos.Hash32() : static_cast<unsigned int>(newPos.column);
                pMacroInvocation->new2macroCallPosMap[key] = t.Begin();
            } else if (t.kind != TokenKind::NL) {
                Position begin{
                    newStartPos.fileID, newStartPos.line, static_cast<int>(newPos.column), t.Begin().isCurFile};
                if (strToken.kind == TokenKind::MULTILINE_STRING) {
                    newPos = Position(newStartPos.fileID, newStartPos.line + multiStrDeltaLine, t.Begin().column,
                        t.Begin().isCurFile);
                    begin = newPos;
                }
                pMacroInvocation->new2originPosMap[newPos.Hash32()] = t.Begin();

                auto key = t.Begin().Hash64();
                if (pMacroInvocation->origin2newPosMap.find(key) == pMacroInvocation->origin2newPosMap.end()) {
                    pMacroInvocation->origin2newPosMap[key] = begin;
                }
            }
        }
    }
}
/**
 * Update positions of generated Tokens after macro expansion.
 *
 * // Before
 * @Moo // line l1
 * class A{
 *   var a = 42
 * }
 *
 * // After
 * class A { var a = 42; func getA() {return a} } // line l1
 *
 * We put all generated Tokens into the same line where the macro call lies.
 */
void RefreshNewTokensPos(Node& node, SourceManager& sm, const CompilerInstance& ci)
{
    auto pMacroInvocation = node.GetInvocation();
    if (pMacroInvocation->isCurFile) {
        return;
    }
    auto mcPos = node.begin;
    pMacroInvocation->origin2newPosMap[mcPos.Hash64()] = mcPos;
    auto lastColumn = static_cast<unsigned int>(sm.GetLineEnd(mcPos) + 1);
    auto prevTokenKind = TokenKind::END;
    pMacroInvocation->macroAtPosition.emplace_back(mcPos);
    size_t threadNum = std::thread::hardware_concurrency();
    Cangjie::Utils::TaskQueue taskQueue(threadNum);
    for (auto& token : pMacroInvocation->newTokens) {
        taskQueue.AddTask<void>([&token, &pMacroInvocation, &mcPos]() {
            if (token.kind == TokenKind::NL || token.kind == TokenKind::COMMENT) {
                return token.SetCurFile(true);
            }
            // Tokens defined in topLevel was not refreshed to at position.
            if (mcPos.fileID != token.Begin().fileID) {
                return token.SetCurFile(false);
            }
            bool notCurFile =
                std::any_of(pMacroInvocation->macroAtPosition.begin(), pMacroInvocation->macroAtPosition.end(),
                    [&token](auto& atPos) { return token.Begin() == atPos || token.Begin() == atPos + 1; });
            return token.SetCurFile(!notCurFile);
        });
    }
    taskQueue.RunAndWaitForAllTasksCompleted();
    for (auto& token : pMacroInvocation->newTokens) {
        // For case: A() A[] A<> A?? >> >=.
        if (!IsSpecialToken(prevTokenKind, token.kind)) {
            lastColumn += 1;
        }
        if (ci.invocation.globalOptions.enableCompileDebug || ci.invocation.globalOptions.displayLineInfo) {
            CollectMacroDebugPosition(*pMacroInvocation, token, lastColumn);
        }
        // still have problem with MULTILINE_STRING, turns out Node generated from interpolation part
        // not stay in same line, impossible to establish a mapping
        Position keyPos(mcPos.line, static_cast<int>(lastColumn));
        if (token.kind == TokenKind::STRING_LITERAL || token.kind == TokenKind::MULTILINE_STRING) {
            pMacroInvocation->new2originPosMap[keyPos.Hash32()] = token.Begin(); // map original STRING_LITERAL token
            HandleInterplationStringPosMap(token, Position(mcPos.fileID, mcPos.line, static_cast<int>(lastColumn)),
                                           pMacroInvocation, ci);
        } else if (token.kind != TokenKind::NL && token.kind != TokenKind::COMMENT) {
            pMacroInvocation->new2originPosMap[keyPos.Hash32()] = token.Begin();
        }
        auto key = token.Begin().Hash64();
        Position begin = token.Begin();
        begin.fileID = mcPos.fileID;
        begin.line = mcPos.line;
        begin.column = static_cast<int>(lastColumn);
        lastColumn += static_cast<unsigned int>(GetTokenLenth(token));

        Position end = token.End();
        end.fileID = mcPos.fileID;
        end.line = mcPos.line;
        end.column = static_cast<int>(lastColumn);
        token.SetValuePos(token.Value(), begin, end);

        if (token.kind != TokenKind::NL && token.kind != TokenKind::COMMENT &&
            pMacroInvocation->origin2newPosMap.find(key) == pMacroInvocation->origin2newPosMap.end()) {
            pMacroInvocation->origin2newPosMap[key] = token.Begin();
        }
        prevTokenKind = token.kind;
    }
    if (ci.invocation.globalOptions.enableCompileDebug || ci.invocation.globalOptions.displayLineInfo) {
        pMacroInvocation->macroDebugMap[lastColumn] = node.end;
    }
}

void RefreshTokensPosMap(Node& node, SourceManager& sm, CompilerInstance& ci)
{
    auto pMacroInvocation = node.GetInvocation();
    if (pMacroInvocation->newTokens.empty()) {
        return;
    }
    auto find = std::find_if(pMacroInvocation->newTokens.begin(), pMacroInvocation->newTokens.end(),
        [&sm, &node](auto& token) { return !IsCurFile(sm, token, node.begin.fileID); });
    if (find == pMacroInvocation->newTokens.end()) {
        pMacroInvocation->isCurFile = true;
    }
    // Generate new tokens in *.macrocall file.
    pMacroInvocation->mcBegin = node.curFile->macroCallPosBase;
    auto tokens = GetTokensFromString(pMacroInvocation->newTokensStr, ci.diag, node.curFile->macroCallPosBase);
    if (tokens.empty()) {
        return;
    }
    pMacroInvocation->mcEnd = tokens.back().End();

    if (tokens.size() != pMacroInvocation->newTokens.size()) {
        ReportMappingFailedDiag(pMacroInvocation->newTokens, tokens, node.begin, ci);
        return;
    }
    // Debug mode: no need to Mapping position.
    if (ci.invocation.globalOptions.enableMacroDebug) {
        return;
    }
    // Bind new tokens pos after macrocall node's begin Pos.
    RefreshNewTokensPos(node, sm, ci);

    // Mapping new position column with new source position in *.macrocall file.
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i].kind != TokenKind::COMMENT) {
            auto pos = pMacroInvocation->newTokens[i].Begin();
            auto key = pMacroInvocation->isCurFile ? pos.Hash32() : static_cast<unsigned int>(pos.column);
            pMacroInvocation->new2macroCallPosMap[key] = tokens[i].Begin();
        }
        // only fixed STRING_LITERAL, MULTILINE_STRING still has problem
        if (tokens[i].kind == TokenKind::STRING_LITERAL || tokens[i].kind == TokenKind::MULTILINE_STRING) {
            HandleInterplationStringPosMap(
                tokens[i], pMacroInvocation->newTokens[i].Begin(), pMacroInvocation, ci, true);
        }
    }
    return;
}

bool WriteStringToFile(const std::string& filePath, const std::string& str)
{
    std::ofstream outStream = std::ofstream(filePath, std::ofstream::out | std::ofstream::binary);
    if (!outStream.is_open()) {
        return false;
    }
    outStream << str;
    outStream.close();
    return true;
}

void RemoveMacroCallFile(File& file)
{
    if (FileUtil::FileExist(file.macroCallFilePath)) {
        (void)FileUtil::Remove(file.macroCallFilePath.data());
    }
}

/**
 * Append to SourceManager and save file.
 */
void GenerateMacroCallFile(const std::vector<Ptr<File>>& fileSucVec,
    std::map<Ptr<File>, std::vector<Ptr<Node>>>& macroCallMap, CompilerInstance& ci)
{
    auto& sm = ci.GetSourceManager();
    std::string content;
    for (const auto& file : fileSucVec) {
        auto nodes = macroCallMap.at(file);
        auto curFilePosBase = file->begin;
        auto fileID = sm.AddSource(file->macroCallFilePath, "");
        for (auto& node : nodes) {
            auto pMacroInvocation = node->GetInvocation();
            if (!pMacroInvocation) {
                continue;
            }
            // Append source code between macroCalls to newFile.
            content = sm.GetContentBetween(curFilePosBase, node->begin);
            (void)sm.AppendSource(file->macroCallFilePath, content);
            curFilePosBase = node->end;
            file->macroCallPosBase = sm.GetSource(fileID).GetEndPos();
            // Append contents generated by macroCall to newFile.
            (void)sm.AppendSource(file->macroCallFilePath, pMacroInvocation->newTokensStr);
            // Map macro information.
            RefreshTokensPosMap(*node, sm, ci);
            pMacroInvocation->isForLSP = ci.invocation.globalOptions.enableMacroInLSP;
        }
        content = sm.GetContentBetween(curFilePosBase.fileID, curFilePosBase, file->end);
        (void)sm.AppendSource(file->macroCallFilePath, content);
        // If lsp or debug-macro, then save the expanded macro contents to a file.
        if (ci.invocation.globalOptions.enableMacroInLSP || ci.invocation.globalOptions.enableMacroDebug) {
            auto source = sm.GetSource(fileID).buffer;
            if (!WriteStringToFile(file->macroCallFilePath, source)) {
                (void)ci.diag.Diagnose(file->begin, DiagKind::macro_call_save_file_failed);
            }
        } else {
            // Need to remove *.macrocall.
            RemoveMacroCallFile(*file);
        }
    }
}

bool IsPureCustomAnnotation(MacroInvocation& invocation, CompilerInstance& ci)
{
    if (!invocation.isCustom || invocation.newTokens.empty()) {
        return false;
    }
    // The current macrocall is actually a custom annotation.
    if (invocation.isCurFile) {
        return true;
    }
    auto& sm = ci.GetSourceManager();
    auto find = std::find_if(invocation.newTokens.begin(), invocation.newTokens.end(),
        [&sm](auto& token) { return !IsCurFile(sm, token); });
    if (find == invocation.newTokens.end()) {
        invocation.isCurFile = true;
        return true;
    }
    // The current custom annotation contains a macrocall inside.
    return false;
}

void EraseFirstNLAndLastEndToken(std::list<Token>& retTokens)
{
    while (!retTokens.empty()) {
        if (retTokens.back().kind == TokenKind::END) {
            retTokens.pop_back();
            continue;
        }
        if (retTokens.begin()->kind != TokenKind::NL) {
            break;
        }
        (void)retTokens.erase(retTokens.begin()); // Erase newline.
    }
}

void ProcessCombinedToken(std::vector<Token>& newTokens, const Token& tk, DiagnosticEngine& diag)
{
    size_t len2{2};
    if (tk.kind == TokenKind::COALESCING) {
        (void)newTokens.emplace_back(Token(TokenKind::QUEST, "?", tk.Begin(), tk.Begin() + 1));
        (void)newTokens.emplace_back(Token(TokenKind::QUEST, "?", tk.Begin() + 1, tk.Begin() + len2));
    } else if (tk.kind == TokenKind::RSHIFT) {
        (void)newTokens.emplace_back(Token(TokenKind::GT, ">", tk.Begin(), tk.Begin() + 1));
        (void)newTokens.emplace_back(Token(TokenKind::GT, ">", tk.Begin() + 1, tk.Begin() + len2));
    } else if (tk.kind == TokenKind::RSHIFT_ASSIGN) {
        (void)newTokens.emplace_back(Token(TokenKind::GT, ">", tk.Begin(), tk.Begin() + 1));
        auto pos = tk.Begin() + 1;
        (void)newTokens.emplace_back(Token(TokenKind::GT, ">", pos, pos + 1));
        pos = pos + 1;
        (void)newTokens.emplace_back(Token(TokenKind::ASSIGN, "=", pos, pos + 1));
    } else if (tk.kind == TokenKind::GE) {
        (void)newTokens.emplace_back(Token(TokenKind::GT, ">", tk.Begin(), tk.Begin() + 1));
        (void)newTokens.emplace_back(Token(TokenKind::ASSIGN, "=", tk.Begin() + 1, tk.Begin() + len2));
    } else if (tk.kind == TokenKind::IDENTIFIER) {
        // In macro definition, the user can create an identifier token
        // or obtain an identifier token from getIdentifier() of libast,
        // which is actually a keyword token. Change token from identifier to keyword here.
        auto tokens = GetTokensFromString(tk.Value(), diag, tk.Begin());
        if (!tokens.empty()) {
            (void)newTokens.insert(newTokens.end(), tokens.begin(), tokens.end());
        } else {
            (void)newTokens.emplace_back(tk);
        }
    } else {
        (void)newTokens.emplace_back(tk);
    }
}
} // namespace

void MacroEvaluation::ProcessNewTokens(MacroCall& macCall)
{
    auto pInvocation = macCall.GetInvocation();
    if (!pInvocation || IsPureAnnotation(*pInvocation)) {
        return;
    }
    std::list<Token> retTokens(pInvocation->newTokens.begin(), pInvocation->newTokens.end());
    EraseFirstNLAndLastEndToken(retTokens);
    if (retTokens.empty()) {
        return;
    }

    pInvocation->newTokens.clear();
    // Append line comment, "/* 7.1 */".
    auto subline = 1;
    auto mcline = std::to_string(macCall.GetBeginPos().line);
    auto comment = "/* " + mcline + "." + std::to_string(subline) + " */";
    (void)pInvocation->newTokens.emplace_back(
        TokenKind::COMMENT, comment, macCall.GetBeginPos(), macCall.GetEndPos(), true);
    for (const auto& tk : std::as_const(retTokens)) {
        ProcessCombinedToken(pInvocation->newTokens, tk, ci->diag);
        if (tk.kind == TokenKind::NL) {
            subline++;
            auto lineComment = "/* " + mcline + "." + std::to_string(subline) + " */";
            auto pos = Position{tk.Begin().fileID, tk.Begin().line + 1, 1};
            (void)pInvocation->newTokens.emplace_back(
                TokenKind::COMMENT, lineComment, pos, pos + lineComment.size(), true);
        }
    }
    if (ci->invocation.globalOptions.enableMacroInLSP || ci->invocation.globalOptions.enableMacroDebug) {
        auto needNewLine = (macCall.GetNode()->IsDecl() || subline > 1) ? true : false;
        auto beginComment = "/* ===== Emitted by MacroCall " + macCall.GetMacroInfo() + " ===== */";
        auto endComment = "/* ===== End of the Emit ===== */";
        if (needNewLine) {
            (void)pInvocation->newTokens.insert(pInvocation->newTokens.begin(), Token(TokenKind::NL));
            (void)pInvocation->newTokens.insert(
                pInvocation->newTokens.begin(),
                Token(TokenKind::COMMENT, beginComment, macCall.GetBeginPos(), macCall.GetEndPos(), true));
            auto pos = pInvocation->newTokens.back().Begin();
            pInvocation->newTokens.emplace_back(Token(TokenKind::NL, Utils::GetLineTerminator(), pos, pos + 1, true));
            pos = pos + 1;
            (void)pInvocation->newTokens.emplace_back(Token(TokenKind::COMMENT, endComment, pos, pos + 1, true));
        } else {
            (void)pInvocation->newTokens.insert(
                pInvocation->newTokens.begin(),
                Token(TokenKind::COMMENT, beginComment, macCall.GetBeginPos(), macCall.GetEndPos(), true));
            (void)pInvocation->newTokens.emplace_back(
                Token(TokenKind::COMMENT, endComment, pInvocation->newTokens.back().Begin(),
                pInvocation->newTokens.back().Begin() + 1, true));
        }
    }
    pInvocation->newTokensStr = ConvertTokensToString(pInvocation->newTokens, macCall.GetBeginPos().column);
    vecOfGeneratedCodes.push_back(pInvocation->newTokensStr);
}

std::string MacroEvaluation::ConvertTokensToString(
    const TokenVector& tokens, int offset)
{
    MacroFormatter formatter = MacroFormatter(tokens, escapePosVec, offset);
    return formatter.Produce();
}

/**
 * Map macro information and save the expanded macro contents to a file.
 */
void MacroExpansion::ProcessMacros(Package& package)
{
    // Collect successful macrocall.
    std::map<Ptr<File>, std::vector<Ptr<Node>>> foundSucMacroMap;
    std::vector<Ptr<File>> fileSucVec;
    for (auto& mc : macroCollector.macCalls) {
        auto pInvocation = mc.GetInvocation();
        if (!pInvocation || MacroExpandFailed(pInvocation->newTokens) || mc.status == MacroEvalStatus::FAIL ||
            IsPureCustomAnnotation(*pInvocation, *ci)) {
            continue;
        }
        auto file = mc.GetNode()->curFile;
        if (!file) {
            continue;
        }
        if (foundSucMacroMap.find(file) != foundSucMacroMap.end()) {
            (void)foundSucMacroMap[file].emplace_back(mc.GetNode());
        } else {
            (void)fileSucVec.emplace_back(file);
            foundSucMacroMap[file] = {mc.GetNode()};
        }
    }
    // Need to remove *.macrocall.
    for (const auto& file : package.files) {
        if (!file) {
            continue;
        }
        file->macroCallFilePath = file->filePath + ".macrocall";
        if (foundSucMacroMap.find(file.get()) == foundSucMacroMap.end() &&
            (ci->invocation.globalOptions.enableMacroInLSP || ci->invocation.globalOptions.enableMacroDebug)) {
            RemoveMacroCallFile(*file);
        }
    }
    if (foundSucMacroMap.empty()) {
        return;
    }
    // Generate *.macrocall file.
    GenerateMacroCallFile(fileSucVec, foundSucMacroMap, *ci);
    return;
}
