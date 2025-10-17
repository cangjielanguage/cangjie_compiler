// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements macro evaluation related apis for macro.
 */

#include "cangjie/Macro/MacroEvaluation.h"

#include "cangjie/AST/Walker.h"
#include "cangjie/Basic/Match.h"
#include "cangjie/Basic/Print.h"
#include "cangjie/Lex/Lexer.h"
#include "cangjie/Macro/InvokeUtil.h"
#include "cangjie/Macro/TokenSerialization.h"
#include "cangjie/Modules/PackageManager.h"
#include "cangjie/Parse/Parser.h"
#include "cangjie/Utils/ProfileRecorder.h"
#include "cangjie/Utils/Utils.h"

using namespace Cangjie;
using namespace AST;
using namespace InvokeRuntime;

namespace {

/*
 * Preprocess Tokens to deal with escape for @.
 */
bool PreprocessTokens(TokenVector& inputTokens, std::vector<Position>& escapePosVec)
{
    size_t tokLen = inputTokens.size();
    size_t index = 0;
    TokenVector eraseTokens;
    // Update escapePosVec.
    while (index < tokLen) {
        if (inputTokens[index].Value() == "\\") {
            if (index == tokLen - 1) {
                return false;
            }
            if (inputTokens[index + 1].Value() == "@") {
                (void)escapePosVec.emplace_back(inputTokens[index + 1].Begin());
                (void)eraseTokens.emplace_back(inputTokens[index]);
            }
        }
        index++;
    }
    // Erase escape.
    Utils::EraseIf(inputTokens, [&](const Token& t) { return Utils::In(t, eraseTokens) && t.Value() == "\\"; });
    return true;
}

bool IsInPrimaryFuncParam(const OwnedPtr<Decl>& decl, const Position& mcPos)
{
    bool primaryFuncParam = false;
    auto checkMacroCallScope = [&primaryFuncParam, &mcPos](Ptr<const Node> curNode) -> VisitAction {
        if (curNode->astKind == ASTKind::MACRO_EXPAND_PARAM && curNode->begin == mcPos) {
            if (auto sc = std::get_if<ScopeKind>(&(curNode->GetConstInvocation()->scope)); sc) {
                auto scope = *sc;
                if (scope == ScopeKind::PRIMARY_CONSTRUCTOR_BODY_FOR_CLASS ||
                    scope == ScopeKind::PRIMARY_CONSTRUCTOR_BODY_FOR_STRUCT) {
                    primaryFuncParam = true;
                }
            }
            return VisitAction::SKIP_CHILDREN;
        }
        return VisitAction::WALK_CHILDREN;
    };
    Walker mcWalker(decl.get(), checkMacroCallScope);
    mcWalker.Walk();
    return primaryFuncParam;
}

auto GetMacroScopeKind(MacroInvocation& invocation, const Position& mcPos)
{
    static std::unordered_map<ASTKind, ScopeKind> scopeOfDecl = {
        {ASTKind::INTERFACE_DECL, ScopeKind::INTERFACE_BODY},
        {ASTKind::EXTEND_DECL, ScopeKind::EXTEND_BODY},
        {ASTKind::CLASS_DECL, ScopeKind::CLASS_BODY},
        {ASTKind::STRUCT_DECL, ScopeKind::STRUCT_BODY},
        {ASTKind::ENUM_DECL, ScopeKind::ENUM_BODY},
        {ASTKind::FUNC_DECL, ScopeKind::FUNC_BODY},
    };
    // If outer macro have parenthesis, the scope rule is unknown to avoid scope related errors.
    if (invocation.hasParenthesis) {
        return static_cast<size_t>(ScopeKind::UNKNOWN_SCOPE);
    }
    if (invocation.decl) {
        if (IsInPrimaryFuncParam(invocation.decl, mcPos)) {
            return static_cast<size_t>(ScopeKind::PRIMARY_CONSTRUCTOR_FUNC_PARAM);
        }
        if (scopeOfDecl.find(invocation.decl->astKind) != scopeOfDecl.end()) {
            return static_cast<size_t>(scopeOfDecl[invocation.decl->astKind]);
        }
    }
    if (auto sc = std::get_if<ScopeKind>(&invocation.scope); sc) {
        return static_cast<size_t>(*sc);
    }
    return static_cast<size_t>(ScopeKind::TOPLEVEL);
}

auto GetMacroScopeKindReEval(MacroInvocation& invocation)
{
    // If outer macro have parenthesis, the scope rule is unknown to avoid scope related errors.
    if (invocation.hasParenthesis) {
        return static_cast<size_t>(ScopeKind::UNKNOWN_SCOPE);
    }
    // macro in reEval step produced by another macro Invocation, use invocation's scope.
    if (auto sc = std::get_if<ScopeKind>(&invocation.scope); sc) {
        return static_cast<size_t>(*sc);
    }
    return static_cast<size_t>(ScopeKind::TOPLEVEL);
}

std::string GetPrimaryName(const MacroInvocation& invocation)
{
    if (invocation.decl) {
        if (invocation.decl->astKind == ASTKind::STRUCT_DECL || invocation.decl->astKind == ASTKind::CLASS_DECL) {
            return invocation.decl->identifier;
        } else {
            return invocation.outerDeclIdent;
        }
    }
    return "";
}

bool HasMacroCallToEval(const std::string& moduleName, const TokenVector& inputTokens, size_t curIndex,
    size_t tokenSize, std::vector<Position>& escapePosVec)
{
    if (inputTokens[curIndex].Value() != "@" || curIndex >= tokenSize - 1) {
        return false;
    }
    // Ignore an escape @.
    if (std::find(escapePosVec.begin(), escapePosVec.end(), inputTokens[curIndex].Begin()) != escapePosVec.end()) {
        return false;
    }
    if (IsBuiltinAnnotation(moduleName, inputTokens[curIndex + 1].Value())) {
        return false;
    }
    if (!IsIdentifierOrContextualKeyword(inputTokens[curIndex + 1].kind)) {
        return false;
    }
    return true;
}

bool HasChildMacroCall(MacroCall& macCall)
{
    for (auto mc : macCall.children) {
        auto pInvocation = mc->GetInvocation();
        if (pInvocation && !pInvocation->isCustom) {
            return true;
        }
    }
    return false;
}

size_t CalculateEndIdx(const TokenVector& tks, const Position& targetPos)
{
    if (tks.empty()) {
        return 0;
    }
    auto size = tks.size();
    for (size_t i = 0; i < size; i++) {
        if (tks[i].Begin() == targetPos) {
            return i;
        }
        if (tks[i].End() == targetPos) {
            return i + 1;
        }
    }
    return size - 1;
}

size_t GetUnUsedThread(std::vector<bool>& isThreadUseds)
{
    auto size = isThreadUseds.size();
    for (size_t i = 0; i < size; i++) {
        if (!isThreadUseds[i]) {
            return i;
        }
    }
    return size;
}

/**
 * Refresh the tokens position for nestedMacro,
 * Macro invocation without parentheses: we need to parse if "EvaluateTokens" generates new tokens.
 */
void RefreshTokensPos(std::vector<Token>& inputTokens, const Position pos, SourceManager& sm)
{
    if (inputTokens.empty()) {
        return;
    }
    auto meFileID = pos.fileID;
    auto meLine = pos.line;
    auto lastColumn = pos.column + static_cast<int>(inputTokens[0].Length()) + 1;
    for (auto& token : inputTokens) {
        if (IsCurFile(sm, token)) {
            meFileID = token.Begin().fileID;
            meLine = token.Begin().line;
            lastColumn = token.Begin().column + static_cast<int>(token.Value().size()) + 1;
        } else {
            token.SetValuePos(token.Value(), {meFileID, meLine, lastColumn, token.Begin().isCurFile},
                /* multiline string is compressed into one line in macro expansion */
                {meFileID, meLine, lastColumn + static_cast<int>(token.Value().size()), token.End().isCurFile});
        }
    }
}

bool IsMacroDefUndeclared(const MacroCall& macCall)
{
    auto pInvocation = macCall.GetInvocation();
    if (!pInvocation) {
        return false;
    }
    // For case: @M(...).
    if (pInvocation->hasParenthesis && !pInvocation->IsIfAvailable()) {
        return true;
    }
    // For custom annotation.
    return false;
}

void RefreshMacroCallArgs(MacroCall& macCall, DiagnosticEngine& diag)
{
    auto pInvocation = macCall.GetInvocation();
    TokenVector tokensAfterEval;
    if (macCall.status == MacroEvalStatus::ANNOTATION) {
        // Add @annotation[a,b] tokens.
        size_t atTokenSize{1};
        if (pInvocation->isCompileTimeVisible) {
            std::string atExclToken = "@!";
            atTokenSize = atExclToken.size();
            tokensAfterEval.emplace_back(
                TokenKind::AT_EXCL, atExclToken, pInvocation->atPos, pInvocation->atPos + atTokenSize);
        } else {
            tokensAfterEval.emplace_back(Token(TokenKind::AT, "@", pInvocation->atPos, pInvocation->atPos + 1));
        }
        auto tks = GetTokensFromString(pInvocation->fullName, diag, pInvocation->atPos + atTokenSize);
        (void)tokensAfterEval.insert(tokensAfterEval.end(), tks.begin(), tks.end());
        if (!pInvocation->attrs.empty()) {
            (void)tokensAfterEval.emplace_back(
                Token(TokenKind::LSQUARE, "[", pInvocation->leftSquarePos, pInvocation->leftSquarePos + 1));
            (void)tokensAfterEval.insert(tokensAfterEval.end(), pInvocation->attrs.begin(), pInvocation->attrs.end());
            (void)tokensAfterEval.emplace_back(
                Token(TokenKind::RSQUARE, "]", pInvocation->rightSquarePos, pInvocation->rightSquarePos + 1));
        }
        if (pInvocation->hasParenthesis && pInvocation->nodes.empty()) {
            (void)tokensAfterEval.emplace_back(
                Token(TokenKind::LPAREN, "(", pInvocation->leftParenPos, pInvocation->leftParenPos + 1));
            (void)tokensAfterEval.insert(tokensAfterEval.end(), pInvocation->args.begin(), pInvocation->args.end());
            (void)tokensAfterEval.emplace_back(
                Token(TokenKind::RPAREN, ")", pInvocation->rightParenPos, pInvocation->rightParenPos + 1));
        } else {
            auto nlpos = tokensAfterEval.back().Begin() + 1;
            (void)tokensAfterEval.emplace_back(Token(TokenKind::NL, "\n", nlpos, nlpos + 1));
            if (pInvocation->nodes.empty()) {
                (void)tokensAfterEval.insert(tokensAfterEval.end(), pInvocation->args.begin(), pInvocation->args.end());
            }
        }
    }
    if (macCall.status == MacroEvalStatus::ANNOTATION && pInvocation->hasParenthesis && !pInvocation->nodes.empty()) {
        (void)tokensAfterEval.emplace_back(
            Token(TokenKind::LPAREN, "(", pInvocation->leftParenPos, pInvocation->leftParenPos + 1));
    }
    for (auto& node : pInvocation->nodes) {
        if (node->astKind == ASTKind::TOKEN_PART) {
            auto tp = RawStaticCast<TokenPart*>(node.get());
            (void)tokensAfterEval.insert(tokensAfterEval.end(), tp->tokens.begin(), tp->tokens.end());
        } else {
            auto pInv = node->GetInvocation();
            (void)tokensAfterEval.insert(tokensAfterEval.end(), pInv->newTokens.begin(), pInv->newTokens.end());
        }
    }
    if (macCall.status == MacroEvalStatus::ANNOTATION && pInvocation->hasParenthesis && !pInvocation->nodes.empty()) {
        (void)tokensAfterEval.emplace_back(
            Token(TokenKind::RPAREN, ")", pInvocation->rightParenPos, pInvocation->rightParenPos + 1));
    }
    if (macCall.isForInterpolation) {
        pInvocation->newTokens = {Token(macCall.strKind, LineToString(tokensAfterEval))};
        macCall.status = MacroEvalStatus::SUCCESS;
        return;
    }
    if (macCall.status == MacroEvalStatus::REEVAL || macCall.status == MacroEvalStatus::ANNOTATION) {
        pInvocation->newTokens.assign(tokensAfterEval.begin(), tokensAfterEval.end());
        macCall.status = MacroEvalStatus::SUCCESS;
        return;
    }
    pInvocation->args.assign(tokensAfterEval.begin(), tokensAfterEval.end());
    macCall.status = MacroEvalStatus::READY;
}

void CheckMacroCallArgs(MacroCall& macCall, DiagnosticEngine& diag, bool compileCjd)
{
    if (macCall.status != MacroEvalStatus::READY) {
        return;
    }
    auto pInvocation = macCall.GetInvocation();
    // If the inner macrocall does not have parentheses, new generated tokens need to be parsed again.
    if (!macCall.isOuterMost && !pInvocation->hasParenthesis && !pInvocation->nodes.empty()) {
        auto scopeKind = *std::get_if<ScopeKind>(&(pInvocation->scope));
        auto primaryName = GetPrimaryName(*pInvocation);
        auto pos = macCall.GetBeginPos() + macCall.GetIdentifier().size();
        std::vector<Token> newTokens = pInvocation->args;
        RefreshTokensPos(newTokens, pos, diag.GetSourceManager());
        auto errCnt = diag.GetErrorCount();
        Parser parser{newTokens, diag, diag.GetSourceManager(), false, compileCjd};
        parser.SetPrimaryDecl(primaryName).SetCurFile(macCall.GetNode()->curFile);
        parser.ParseDecl(scopeKind);
        if (diag.GetErrorCount() > errCnt) {
            macCall.status = MacroEvalStatus::FAIL;
            return;
        }
    }
}

void SetMacroCallEvalResult(MacroCall& macCall, DiagnosticEngine& diag)
{
    // Evaluate failed or success.
    auto pInvocation = macCall.GetInvocation();
    if (MacroExpandFailed(pInvocation->newTokens) || macCall.status == MacroEvalStatus::FAIL) {
        macCall.status = MacroEvalStatus::FAIL;
        (void)diag.Diagnose(macCall.GetBeginPos(), DiagKind::macro_evaluate_failed, macCall.GetFullName());
    } else {
        macCall.status = MacroEvalStatus::SUCCESS;
    }
}

bool HandleBeforeMacroCallEval(MacroCall& macCall, DiagnosticEngine& diag)
{
    std::string macCallFullName = macCall.GetFullName();
    // If use build-in macros, the macro is converted to token information.
    if (Utils::In(macCallFullName, BUILD_IN_MACROS)) {
        if (macCall.GetInvocation()->HasAttr()) {
            (void)diag.Diagnose(macCall.GetBeginPos(), DiagKind::macro_build_in_unexpect_params_attrs, macCallFullName);
        }
        if (!macCall.GetInvocation()->args.empty()) {
            (void)diag.Diagnose(macCall.GetBeginPos(), DiagKind::macro_build_in_unexpect_params, macCallFullName);
        }
        auto macroNode = macCall.GetNode();
        TokenVector newTokens;
        if (macCallFullName == SOURCE_PACKAGE) {
            auto pos = macCall.GetBeginPos();
            (void)newTokens.emplace_back(Token(macCall.strKind, macroNode->curFile->curPackage->fullPackageName, pos,
                pos + macroNode->curFile->curPackage->fullPackageName.size()));
        } else if (macCallFullName == SOURCE_FILE) {
            auto pos = macCall.GetBeginPos();
            (void)newTokens.emplace_back(
                Token(macCall.strKind, macroNode->curFile->fileName, pos, pos + macroNode->curFile->fileName.size()));
        } else {
            auto pos = macCall.useParentPos ? macCall.parentMacroCall->GetBeginPos() : macCall.GetBeginPos();
            (void)newTokens.emplace_back(Token(
                TokenKind::INTEGER_LITERAL, std::to_string(pos.line), pos, pos + std::to_string(pos.line).size()));
        }
        macCall.GetInvocation()->newTokens = newTokens;
        macCall.status = MacroEvalStatus::SUCCESS;
        return true;
    }
    return false;
}

bool IsMacroCallReadyForEval(MacroCall& macCall, DiagnosticEngine& diag, bool compileCjd)
{
    if (HandleBeforeMacroCallEval(macCall, diag)) {
        return false;
    }
    if (macCall.status == MacroEvalStatus::INIT || macCall.status == MacroEvalStatus::REEVAL ||
        macCall.status == MacroEvalStatus::ANNOTATION) {
        size_t sucCnt = 0;
        for (auto& mc : macCall.children) {
            if (mc->status == MacroEvalStatus::SUCCESS) {
                sucCnt++;
                continue;
            }
            if (mc->status == MacroEvalStatus::FAIL) {
                macCall.status = MacroEvalStatus::FAIL;
                break;
            }
        }
        if (sucCnt == macCall.children.size()) {
            RefreshMacroCallArgs(macCall, diag);
        }
    }
    CheckMacroCallArgs(macCall, diag, compileCjd);
    if (macCall.status == MacroEvalStatus::READY) {
        macCall.status = MacroEvalStatus::EVAL;
        return true;
    }
    return false;
}

void ExpandBuildInMacro(TokenVector& attrTokens, MacroCall& macCall)
{
    auto replaceTokens = [&macCall, &attrTokens](auto begin, std::string& macCallFullName, auto pos) {
        if (macCallFullName == SOURCE_PACKAGE) {
            (void)attrTokens.emplace(begin,
                Token(macCall.strKind, macCall.GetNode()->curFile->curPackage->fullPackageName, pos,
                    pos + macCall.GetNode()->curFile->curPackage->fullPackageName.size()));
        } else if (macCallFullName == SOURCE_FILE) {
            (void)attrTokens.emplace(begin,
                Token(macCall.strKind, macCall.GetNode()->curFile->fileName, pos,
                    pos + std::to_string(begin->Begin().line).size()));
        } else {
            (void)attrTokens.emplace(begin,
                Token(TokenKind::INTEGER_LITERAL, std::to_string(begin->Begin().line), pos,
                    pos + std::to_string(begin->Begin().line).size()));
        }
    };

    for (auto it = attrTokens.begin(); it != attrTokens.end();) {
        auto begin = it;
        if ((++it) == attrTokens.end() || begin->kind != TokenKind::AT) {
            continue;
        }
        auto buildInMacro = it->Value();
        auto pos = macCall.GetBeginPos();
        if ((++it) == attrTokens.end() || !Utils::In(buildInMacro, BUILD_IN_MACROS)) {
            continue;
        }
        auto kind = it->kind;
        if ((++it) == attrTokens.end() || kind != TokenKind::LPAREN) {
            continue;
        }
        if (it->kind != TokenKind::RPAREN) {
            ++it;
            continue;
        }
        it = attrTokens.erase(begin, it + 1);
        replaceTokens(begin, buildInMacro, pos);
    }
}
} // namespace

bool MacroEvaluation::CheckAttrTokens(TokenVector& attrTokens, MacroCall& macCall)
{
    if (attrTokens.empty()) {
        return true;
    }
    // For the macro call: @M[@sourceFile()](), we need to make further conversions.
    // After conversion: @M["default.cj"]()
    ExpandBuildInMacro(attrTokens, macCall);
    // Make sure @ is used as escape.
    if (!PreprocessTokens(attrTokens, escapePosVec)) {
        (void)ci->diag.Diagnose(attrTokens[0].Begin(), DiagKind::macro_expand_invalid_escape);
        return false;
    }
    for (size_t i = 0; i < attrTokens.size(); i++) {
        auto posTmp = attrTokens[i].Begin();
        if (attrTokens[i].kind == TokenKind::AT &&
            std::find(escapePosVec.begin(), escapePosVec.end(), posTmp) == escapePosVec.end()) {
            (void)ci->diag.Diagnose(posTmp, DiagKind::macro_expand_invalid_attr_tokens, attrTokens[i].Value());
            return false;
        }
    }
    return true;
}

bool MacroEvaluation::HasMacroCallInStrInterpolation(const std::string& str, MacroCall& macCall)
{
    std::string strExprStart = "${";
    std::string strExprEnd = "}";
    size_t strExprStartLen = strExprStart.length();
    size_t strExprEndLen = strExprEnd.length();
    auto interpStr = str.substr(strExprStartLen, str.length() - (strExprStartLen + strExprEndLen));

    auto tmpMacExpr = MakeOwned<MacroExpandExpr>();
    tmpMacExpr->curFile = macCall.GetNode()->curFile;
    auto tmpMacCall = std::make_unique<MacroCall>(tmpMacExpr.get());
    tmpMacCall->isForInterpolation = true;
    auto ptmpInvocation = tmpMacCall->GetInvocation();
    ptmpInvocation->args = GetTokensFromString(interpStr, ci->diag);
    CreateMacroCallTree(*tmpMacCall);
    if (ptmpInvocation->nodes.empty()) {
        return false;
    }
    macCall.newStr += strExprStart;
    (void)macCall.children.insert(macCall.children.begin(), tmpMacCall->children.begin(), tmpMacCall->children.end());
    auto pInvocation = macCall.GetInvocation();
    auto tks = GetTokensFromString(macCall.newStr, ci->diag);
    (void)pInvocation->nodes.emplace_back(MakeOwned<TokenPart>(tks));
    (void)pInvocation->nodes.insert(pInvocation->nodes.end(), std::make_move_iterator(ptmpInvocation->nodes.begin()),
        std::make_move_iterator(ptmpInvocation->nodes.end()));
    macCall.newStr = strExprEnd;
    return true;
}

bool MacroEvaluation::HasMacroCallInStrInterpolation(
    TokenVector& input, size_t startIndex, size_t curIndex, MacroCall& parentMacCall)
{
    auto tk = input[curIndex];
    std::set<TokenKind> tkKinds = {TokenKind::STRING_LITERAL, TokenKind::MULTILINE_STRING};
    if (tkKinds.count(tk.kind) == 0 || tk.Value().find("${") == std::string::npos) {
        return false;
    }
    auto macExpr = MakeOwned<MacroExpandExpr>();
    macExpr->curFile = parentMacCall.GetNode()->curFile;
    auto macCall = std::make_unique<MacroCall>(macExpr.get());
    macCall->isForInterpolation = true;
    macCall->strKind = tk.kind;

    std::vector<Token> tokens{tk};
    Lexer lexer(tokens, ci->diag, ci->diag.GetSourceManager());
    auto strParts = lexer.GetStrParts(tk);
    for (auto& strP : strParts) {
        // Check interpolation contains macrocall or not.
        if (strP.strKind == StringPart::EXPR && HasMacroCallInStrInterpolation(strP.value, *macCall)) {
            continue;
        }
        macCall->newStr += strP.value;
    }
    auto pInvocation = macCall->GetInvocation();
    if (pInvocation->nodes.empty()) {
        return false;
    }
    auto tks = GetTokensFromString(macCall->newStr, ci->diag);
    (void)pInvocation->nodes.emplace_back(MakeOwned<TokenPart>(tks));

    if (curIndex > startIndex) {
        auto beginIter = input.begin() + static_cast<int>(startIndex);
        auto endIter = input.begin() + static_cast<int>(curIndex);
        TokenVector tks1{beginIter, endIter};
        (void)parentMacCall.GetInvocation()->nodes.emplace_back(MakeOwned<TokenPart>(tks1));
    }
    // For save child macrocall expression in memory.
    (void)parentMacCall.GetInvocation()->nodes.emplace_back(std::move(macExpr));
    (void)parentMacCall.children.emplace_back(macCall.get()); // For evaluate status.
    (void)pMacroCalls.emplace_back(macCall.get());            // For evaluate.
    (void)childMacCalls.emplace_back(std::move(macCall));     // For save child macrocall in memory.
    return true;
}

void MacroEvaluation::ProcessTokensInQuoteExpr(
    TokenVector& input, size_t& startIndex, size_t& curIndex, MacroCall& macCall, bool reEval)
{
    // Start with 0 for the opening parenthesis we found
    auto balance = 0;
    auto inQuoteInterpolation = false;
    while (curIndex < input.size()) {
        // macro M in quote($(@M(111))) should be expanded
        if (inQuoteInterpolation && input[curIndex].kind == TokenKind::AT) {
            CreateChildMacroCall(input, startIndex, curIndex, macCall, reEval);
            inQuoteInterpolation = false;
        }
        if (input[curIndex].kind == TokenKind::DOLLAR && input[curIndex + 1].kind == TokenKind::LPAREN) {
            inQuoteInterpolation = true;
        }
        // ignore escaped parenthesis \( \)
        if (input[curIndex].kind == TokenKind::LPAREN &&
            input[curIndex].Begin() + input[curIndex].Value().size() + 1 != input[curIndex].End()) {
            balance++;
        } else if (input[curIndex].kind == TokenKind::RPAREN &&
            input[curIndex].Begin() + input[curIndex].Value().size() + 1 != input[curIndex].End()) {
            balance--;
            // If balance reaches 0, we found the matching closing parenthesis
            if (balance == 0) {
                return;
            }
        }
        curIndex++;
    }
    return;
}

void MacroEvaluation::CreateChildMacroCall(
    TokenVector& inputTokens, size_t& startIndex, size_t& curIndex, MacroCall& macCall, bool reEval)
{
    auto pInvocation = macCall.GetInvocation();
    auto primaryName = GetPrimaryName(*pInvocation);
    auto scopeKind =
        reEval ? GetMacroScopeKindReEval(*pInvocation) : GetMacroScopeKind(*pInvocation, inputTokens[curIndex].Begin());
    DiagnosticEngine tempDiag;
    SourceManager sm;
    tempDiag.SetSourceManager(&sm);
    // No need to diagnose the error here if in reEvaluate status, ReplaceEachMacro will diagnose the error.
    auto& diag = reEval ? tempDiag : ci->diag;
    auto errCnt = diag.GetErrorCount();
    TokenVector tokensToParse(inputTokens.begin() + static_cast<int>(curIndex), inputTokens.end());
    // we don't trust the position of inputTokens. they may all have the same pos
    // and parser relay on position information to collect macro input tokens
    std::vector<Position> originPos;
    if (reEval) {
        for (size_t i = 0; i < tokensToParse.size(); i++) {
            originPos.emplace_back(tokensToParse[i].Begin());
        }
        for (size_t i = 1; i < tokensToParse.size(); i++) {
            auto begin = tokensToParse[i - 1].End() + 1;
            auto end = begin + tokensToParse[i].Value().size();
            tokensToParse[i].SetValuePos(tokensToParse[i].Value(), begin, end);
        }
    }
    Parser parser{tokensToParse, diag, ci->diag.GetSourceManager(), false,
        ci->invocation.globalOptions.compileCjd};
    parser.SetPrimaryDecl(primaryName).SetCurFile(macCall.GetNode()->curFile);
    parser.SetCompileOptions(ci->invocation.globalOptions);
    auto decl = parser.ParseDecl(ScopeKind(scopeKind));
    if (!decl->IsMacroCallNode()) {
        // Filter decls such as builtin annotations.
        curIndex++;
        return;
    }
    auto mc = std::make_unique<MacroCall>(decl.get());
    mc->parentMacroCall = &macCall;
    (void)macCall.children.emplace_back(mc.get()); // For evaluate status.
    if (diag.GetErrorCount() > errCnt) {
        mc->status = MacroEvalStatus::FAIL;
    } else {
        CreateMacroCallTree(*mc);
    }
    if (mc->status == MacroEvalStatus::FAIL) {
        macCall.status = MacroEvalStatus::FAIL;
    }
    (void)pInvocation->macroAtPosition.emplace_back(mc->GetBeginPos());
    if (!mc->GetInvocation()->macroAtPosition.empty()) {
        pInvocation->macroAtPosition.insert(pInvocation->macroAtPosition.end(),
            mc->GetInvocation()->macroAtPosition.begin(), mc->GetInvocation()->macroAtPosition.end());
    }
    (void)childMacCalls.emplace_back(std::move(mc)); // For save child macrocall in memory.
    if (startIndex < curIndex) {
        auto beginIter = inputTokens.begin() + static_cast<int>(startIndex);
        auto endIter = inputTokens.begin() + static_cast<int>(curIndex);
        TokenVector tokens{beginIter, endIter};
        (void)pInvocation->nodes.emplace_back(MakeOwned<TokenPart>(tokens));
    }
    auto endIndex = curIndex + CalculateEndIdx(tokensToParse, decl->end);
    (void)pInvocation->nodes.emplace_back(std::move(decl)); // For save child macrocall expression in memory.
    // refresh back tokens position. CalculateEndIdx also use the position,
    // so we have to refresh back position after CalculateEndIdx.
    if (reEval) {
        for (size_t i = 1; i < tokensToParse.size(); i++) {
            tokensToParse[i].SetValuePos(
                tokensToParse[i].Value(), originPos[i], originPos[i] + tokensToParse[i].Value().size());
        }
    }
    startIndex = endIndex;
    curIndex = endIndex;
}

void MacroEvaluation::CheckDeprecatedMacrosUsage(MacroCall& macCall) const
{
    auto definition = macCall.GetDefinition();
    if (!definition) {
        return;
    }

    for (auto& anno : definition->annotations) {
        if (anno->kind == AnnotationKind::DEPRECATED) {
            auto deprecated = anno.get();

            std::string message = " ";
            std::string since = ".";
            bool strict = false;

            AST::ExtractArgumentsOfDeprecatedAnno(deprecated, message, since, strict);

            auto diagnoseKind = strict ? DiagKind::macro_is_deprecated_error : DiagKind::macro_is_deprecated_warning;

            ci->diag.Diagnose(macCall.GetBeginPos(), diagnoseKind, macCall.GetFullName(), since, message);
        }
    }
}

void MacroEvaluation::SaveUsedMacros(MacroCall& macCall)
{
    SaveUsedMacroPkgs(macCall.packageName);
    auto node = macCall.GetNode();
    auto decl = macCall.GetDefinition();
    if (!node || !node->curFile || !decl) {
        return;
    }
    ci->importManager.AddUsedMacroDecls(macCall.GetNode()->curFile, decl);
}

void MacroEvaluation::SaveUsedMacroPkgs(const std::string packageName)
{
    if (packageName.empty() || usedMacroPkgs.find(packageName) != usedMacroPkgs.end()) {
        return;
    }
    usedMacroPkgs[packageName] = false;
}

bool MacroEvaluation::NeedCreateMacroCallTree(MacroCall& macCall, bool reEval)
{
    if (macCall.isForInterpolation) {
        return true;
    }
    auto pInvocation = macCall.GetInvocation();
    if (reEval) {
        if (pInvocation->isCustom) {
            // No need to reEvaluate Annotation.
            macCall.status = MacroEvalStatus::FINISH;
            return false;
        }
        if (macCall.status == MacroEvalStatus::REEVALFAILED) {
            return false;
        }
        return true;
    }
    if (macCall.ResolveMacroCall(ci)) {
        SaveUsedMacros(macCall);
        CheckDeprecatedMacrosUsage(macCall);
        // Find macrodef and check attr.
        if (!CheckAttrTokens(pInvocation->attrs, macCall)) {
            macCall.status = MacroEvalStatus::FAIL;
            return false;
        }
        if (pInvocation->isCompileTimeVisible) {
            // issue a non-fatal error when evaluating macro call with @!
            ci->diag.Diagnose(macCall.GetBeginPos(), DiagKind::macro_expand_atexcl);
        }
        return true;
    }
    // Can not find macrodef.
    if (IsMacroDefUndeclared(macCall)) {
        macCall.status = MacroEvalStatus::FAIL;
        return false;
    }
    // For annotation case, should change Macro to Annotation.
    macCall.status = MacroEvalStatus::ANNOTATION;
    pInvocation->isCustom = true;
    return true;
}

void MacroEvaluation::CreateMacroCallTree(MacroCall& macCall, bool reEval)
{
    macCall.ci = ci;
    if (!NeedCreateMacroCallTree(macCall, reEval)) {
        return;
    }
    auto pInvocation = macCall.GetInvocation();
    pInvocation->nodes.clear();
    auto& inputTokens = reEval ? pInvocation->newTokens : pInvocation->args;
    if (!PreprocessTokens(inputTokens, escapePosVec)) {
        (void)ci->diag.Diagnose(inputTokens[0].Begin(), DiagKind::macro_expand_invalid_escape);
        return;
    }

    auto node = macCall.GetNode();
    CJC_ASSERT(node->curFile && node->curFile->curPackage);
    auto names = Utils::SplitQualifiedName(node->curFile->curPackage->fullPackageName);
    auto moduleName = names.size() > 1 ? names.front() : "";
    size_t startIndex = 0;
    size_t curIndex = 0;
    auto tokenSize = inputTokens.size();
    while (curIndex < tokenSize) {
        auto posTmp = inputTokens[curIndex].Begin();
        // if current Token is not escaped @ or not macrocall like @MacroIdentidier, should be error
        if (inputTokens[curIndex].kind == TokenKind::AT &&
            std::find(escapePosVec.begin(), escapePosVec.end(), posTmp) == escapePosVec.end() &&
            (curIndex == tokenSize - 1 || !IsIdentifierOrContextualKeyword(inputTokens[curIndex + 1].kind))) {
            (void)ci->diag.Diagnose(posTmp, DiagKind::macro_expand_invalid_input_tokens);
        }
        if (inputTokens[curIndex].kind == TokenKind::QUOTE && inputTokens[curIndex + 1].kind == TokenKind::LPAREN) {
            curIndex++;
            ProcessTokensInQuoteExpr(inputTokens, startIndex, curIndex, macCall, reEval);
            continue;
        }
        if (HasMacroCallInStrInterpolation(inputTokens, startIndex, curIndex, macCall)) {
            curIndex++;
            startIndex = curIndex;
            continue;
        }
        if (!HasMacroCallToEval(moduleName, inputTokens, curIndex, tokenSize, escapePosVec)) {
            // Skip a normal token, which is not '@'.
            curIndex++;
            continue;
        }
        CreateChildMacroCall(inputTokens, startIndex, curIndex, macCall, reEval);
    }
    if (reEval && macCall.status == MacroEvalStatus::FAIL) {
        macCall.status = MacroEvalStatus::REEVALFAILED;
        return;
    }
    if (!pInvocation->nodes.empty()) {
        // Has child macrocall.
        TokenVector tokens{inputTokens.begin() + static_cast<int>(startIndex), inputTokens.end()};
        if (!tokens.empty()) {
            (void)pInvocation->nodes.emplace_back(MakeOwned<TokenPart>(tokens));
        }
        if (reEval && macCall.status == MacroEvalStatus::SUCCESS) {
            macCall.status = HasChildMacroCall(macCall) ? MacroEvalStatus::REEVAL : MacroEvalStatus::FINISH;
        }
    } else {
        // Has no child macrocall.
        if (macCall.status == MacroEvalStatus::ANNOTATION) {
            pInvocation->isCurFile = true;
            (void)pMacroCalls.emplace_back(&macCall); // For evaluate.
            return;
        }
        macCall.status = reEval ? MacroEvalStatus::FINISH : MacroEvalStatus::READY;
    }
    if (!macCall.isForInterpolation && macCall.status != MacroEvalStatus::FAIL &&
        macCall.status != MacroEvalStatus::FINISH) {
        (void)pMacroCalls.emplace_back(&macCall); // For evaluate.
    }
}

void MacroEvaluation::CreateMacroCallsTree(bool reEval)
{
    pMacroCalls.clear();
    for (auto& macCall : macroCollector->macCalls) {
        if (reEval && macCall.status != MacroEvalStatus::SUCCESS) {
            continue;
        }
        CreateMacroCallTree(macCall, reEval);
    }
}

bool MacroEvaluation::CreateThreadToEvalMacroCall(MacroCall& macCall)
{
    auto unUsedThreadId = GetUnUsedThread(isThreadUseds);
    if (unUsedThreadId >= threadNum) {
        return false;
    }
    isThreadUseds[unUsedThreadId] = true;
    macCall.threadId = unUsedThreadId;
    if (!useChildProcess) {
        // ParallelMacro for cjc.
        auto name = macCall.GetIdentifier() + macCall.GetBeginPos().ToString();
        Utils::ProfileRecorder::Start("Parallel Evaluate Macros", name);
        EvalOneMacroCall(macCall);
    }
    return true;
}

bool MacroEvaluation::WaitForOneMacroCallEvalFinish(std::list<MacroCall*>& evalMacCalls)
{
    if (useChildProcess) {
        // ParallelMacro for compiled Macro in lsp:
        // send macrocalls to child process, and wait macrocall's evalresult from child process.
        if (!SendMacroCallsTask(evalMacCalls)) {
            // If the child process exits abnormally, the sending may fail.
            return false;
        }
        if (!WaitMacroCallsEvalResult(evalMacCalls)) {
            // If send successful, but the child process exits abnormally, the waiting may fail.
            return false;
        }
    }
    bool bEvalFinish = false;
    while (!bEvalFinish) {
        auto iter = evalMacCalls.begin();
        while (iter != evalMacCalls.end()) {
            auto& evalMacCall = *iter;
            // need wait
            if (evalMacCall->isDataReady) {
                // Evaluate macroCall failed or success.
                SetMacroCallEvalResult(*evalMacCall, ci->diag);
            } else {
                // Evaluating macroCall.
                (void)++iter;
                continue;
            }
            auto name = evalMacCall->GetIdentifier() + evalMacCall->GetBeginPos().ToString();
            Utils::ProfileRecorder::Stop("Parallel Evaluate Macros", name);
            if (!useChildProcess) {
                // For compiled macro, need to release coroutine handle.
                ReleaseThreadHandle(*evalMacCall);
            }
            isThreadUseds[evalMacCall->threadId] = false;
            (void)evalMacCalls.erase(iter);
            bEvalFinish = true;
            break;
        }
    }
    return true;
}

void MacroEvaluation::EvalMacroCallsOnMultiThread()
{
    // For compiled macro.
    if (!useChildProcess) {
        // Init global variable.
        Utils::ProfileRecorder::Start("MacroExpand", "InitGlobalVariable");
        InitGlobalVariable();
        Utils::ProfileRecorder::Stop("MacroExpand", "InitGlobalVariable");
    }
    std::list<MacroCall*> evalMacCalls;
    size_t needEvalMacCalls = pMacroCalls.size();
    while (needEvalMacCalls > 0) {
        for (auto& macCall : std::as_const(pMacroCalls)) {
            if (macCall->status == MacroEvalStatus::FAIL) {
                continue;
            }
            if (!IsMacroCallReadyForEval(*macCall, ci->diag, ci->invocation.globalOptions.compileCjd)) {
                if (macCall->status == MacroEvalStatus::FAIL) {
                    needEvalMacCalls--;
                }
                continue;
            }
            if (!CreateThreadToEvalMacroCall(*macCall)) {
                break;
            }
            (void)evalMacCalls.emplace_back(macCall);
            if (evalMacCalls.size() >= threadNum) {
                break;
            }
        }
        if (evalMacCalls.empty()) {
            break;
        }
        if (!WaitForOneMacroCallEvalFinish(evalMacCalls)) {
            for (auto& macCall : std::as_const(pMacroCalls)) {
                if (macCall->status != MacroEvalStatus::SUCCESS && macCall->status != MacroEvalStatus::FINISH) {
                    macCall->status = MacroEvalStatus::FAIL;
                }
            }
            break;
        }
        needEvalMacCalls--;
    }
}

void MacroEvaluation::EvalMacroCallsOnSingleThread()
{
    for (auto& macCall : std::as_const(pMacroCalls)) {
        if (!IsMacroCallReadyForEval(*macCall, ci->diag, ci->invocation.globalOptions.compileCjd)) {
            continue;
        }
        auto name = macCall->GetIdentifier() + macCall->GetBeginPos().ToString();
        Utils::ProfileRecorder::Start("Serial Evaluate Macros", name);
        EvalOneMacroCall(*macCall);
        SetMacroCallEvalResult(*macCall, ci->diag);
        Utils::ProfileRecorder::Stop("Serial Evaluate Macros", name);
    }
}

void MacroEvaluation::EvalOneMacroCall(MacroCall& macCall)
{
    EvaluateWithRuntime(macCall);
}

void MacroEvaluation::FreeMacroInfoVecForMacroCall(MacroCall& mc) const
{
    for (size_t i = 0; i < mc.recordMacroInfo.size(); i++) {
        if (mc.recordMacroInfo[i] != nullptr) {
            free(mc.recordMacroInfo[i]);
            mc.recordMacroInfo[i] = nullptr;
        }
    }
    for (auto& child : mc.children) {
        FreeMacroInfoVecForMacroCall(*child);
    }
}

void MacroEvaluation::EvalMacroCalls()
{
    if (enableParallelMacro) {
        EvalMacroCallsOnMultiThread();
    } else {
        EvalMacroCallsOnSingleThread();
    }
    pMacroCalls.clear();
    // Free address of malloc in Macro with Context.
    for (auto& mc : macroCollector->macCalls) {
        FreeMacroInfoVecForMacroCall(mc);
        mc.children.clear();
    }
}

void MacroEvaluation::RefreshBuildInMacroPostionInfo()
{
    for (auto& macCall : std::as_const(pMacroCalls)) {
        std::string macCallFullName = macCall->GetFullName();
        if (macCall->parentMacroCall && Utils::In(macCallFullName, BUILD_IN_MACROS)) {
            macCall->useParentPos = true;
        }
    }
}

/**
 * We need constantly evaluate the tokens until no new macrocall is found in the new tokens.
 */
void MacroEvaluation::ReEvalAfterEvalMacroCalls()
{
    CreateMacroCallsTree(true);
    while (!pMacroCalls.empty()) {
        RefreshBuildInMacroPostionInfo();
        EvalMacroCalls();
        CreateMacroCallsTree(true);
    }
    for (auto& macCall : macroCollector->macCalls) {
        if (macCall.status != MacroEvalStatus::FINISH && macCall.status != MacroEvalStatus::REEVALFAILED) {
            continue;
        }
        ProcessNewTokens(macCall);
    }
}

void MacroEvaluation::EvalMacros()
{
    Utils::ProfileRecorder::Start("MacroExpand", "Evaluate Macros");
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    CollectMacroLibs();
#endif
    CreateMacroCallsTree();
    EvalMacroCalls();
    ReEvalAfterEvalMacroCalls();
    Utils::ProfileRecorder::Stop("MacroExpand", "Evaluate Macros");
}

std::unordered_set<std::string> MacroEvaluation::GetMacroDefDynamicFiles()
{
    std::unordered_set<std::string> macroDynFiles;
    // For built-in macro packages the corresponding dynamic library needs to be added.
    for (auto& packageName : ci->importManager.GetImportedStdMacroPackages()) {
        auto basePath =
            FileUtil::JoinPath(FileUtil::GetDirPath(ci->invocation.globalOptions.executablePath), "../runtime/lib");
        auto libName = "lib" + FileUtil::ConvertPackageNameToLibCangjieBaseFormat(packageName) + LIB_SUFFIX;
        auto macroPath = FileUtil::JoinPath(
            FileUtil::JoinPath(basePath, ci->invocation.globalOptions.GetCangjieLibHostPathName()), libName);
        (void)macroDynFiles.emplace(macroPath);
    }
    if (ci->invocation.globalOptions.macroLib.empty() && macroDynFiles.empty()) {
        return macroDynFiles;
    }
    for (auto lib : ci->invocation.globalOptions.macroLib) {
        (void)macroDynFiles.emplace(FileUtil::NormalizePath(lib));
    }
    return macroDynFiles;
}

void MacroEvaluation::Evaluate()
{
    if (useChildProcess) {
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
        if (!ci->invocation.globalOptions.macroLib.empty()) {
            auto dyfiles = GetMacroDefDynamicFiles();
            if (SendMacroDefTask(dyfiles)) {
                WaitMacroDefResult();
            }
        }
#endif
        EvalMacros();
        SendExitStgTask();
        return;
    }
    bool findMethodFlag{true};
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    findMethodFlag = RuntimeInit::GetInstance().InitRuntime(
        ci->invocation.GetRuntimeLibPath(), ci->invocation.globalOptions.environment.allVariables);
#endif
    if (findMethodFlag) {
        EvalMacros();
    }
}
