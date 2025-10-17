// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the DiagnosticEngine related classes.
 */
#include "DiagnosticEngineImpl.h"

#include <cstddef>
#include <set>
#include <sstream>
#include <string>
#include <thread>

#include "cangjie/AST/Utils.h"
#include "cangjie/Basic/DiagnosticEmitter.h"
#include "cangjie/Basic/DiagnosticJsonFormatter.h"
#include "cangjie/Basic/Display.h"
#include "cangjie/Basic/Print.h"
#include "cangjie/Utils/CheckUtils.h"
#include "cangjie/Utils/Unicode.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace Cangjie {
Range MakeRange(const Position& begin, Position end)
{
    // The fileID of position may be different, may come from the macro definition or from the macrocall.
    if (begin.fileID != end.fileID) {
        end = begin + 1;
    }
    return Range(begin, end);
}

Range MakeRange(const Position& identifierPos, const std::string& identifier)
{
    for (auto ch : identifier) {
        CJC_ASSERT(Unicode::IsASCII(static_cast<unsigned char>(ch)));
    }
    return MakeRange(identifierPos, identifierPos + identifier.size());
}

Range MakeRange(const Identifier& id)
{
    return MakeRange(id.Begin(), id.End());
}

DiagCategory Diagnostic::GetDiagnoseCategory(DiagKind diagKind)
{
    DiagCategory dc = DiagCategory::OTHER;
#define GET_CATE(kind, KIND)                                                                                           \
    do {                                                                                                               \
        if (diagKind > DiagKind::kind##_diag_begin && diagKind <= DiagKind::kind##_diag_end) {                         \
            dc = DiagCategory::KIND;                                                                                   \
        }                                                                                                              \
    } while (0)
    GET_CATE(macro_expand, MACRO_EXPAND);
    GET_CATE(sema, SEMA);
#undef GET_CATE
    return dc;
}

struct DiagRange {
    DiagKindRefactor start;
    DiagKindRefactor end;
    DiagCategory category;
};

DiagCategory Diagnostic::GetDiagnoseCategory(DiagKindRefactor diagKind)
{
    static const std::array<DiagRange, 11> RANGES = {{
        {DiagKindRefactor::lex_diag_begin, DiagKindRefactor::lex_diag_end, DiagCategory::LEX},
        {DiagKindRefactor::parse_diag_begin, DiagKindRefactor::parse_diag_end, DiagCategory::PARSE},
        {DiagKindRefactor::sema_diag_begin, DiagKindRefactor::sema_diag_end, DiagCategory::SEMA},
        {DiagKindRefactor::chir_diag_begin, DiagKindRefactor::chir_diag_end, DiagCategory::CHIR},
        {DiagKindRefactor::import_package_diag_begin, DiagKindRefactor::import_package_diag_end,
            DiagCategory::IMPORT_PACKAGE},
        {DiagKindRefactor::module_diag_begin, DiagKindRefactor::module_diag_end, DiagCategory::MODULE},
        {DiagKindRefactor::driver_diag_begin, DiagKindRefactor::driver_diag_end, DiagCategory::OTHER},
        {DiagKindRefactor::incremental_compilation_diag_begin, DiagKindRefactor::incremental_compilation_diag_end,
            DiagCategory::OTHER},
        {DiagKindRefactor::parse_query_diag_begin, DiagKindRefactor::parse_query_diag_end, DiagCategory::PARSE_QUERY},
        {DiagKindRefactor::frontend_diag_begin, DiagKindRefactor::frontend_diag_end, DiagCategory::OTHER},
        {DiagKindRefactor::conditional_compilation_diag_begin, DiagKindRefactor::conditional_compilation_diag_end,
            DiagCategory::CONDITIONAL_COMPILATION},
    }};

    for (const auto& range : RANGES) {
        if (diagKind > range.start && diagKind < range.end) {
            return range.category;
        }
    }

    CJC_ABORT();
    return DiagCategory::OTHER;
}

bool Diagnostic::IsValid() const
{
    // only focus `mainHint`'s range, which is used by lsp
    return !mainHint.range.HasZero() &&
        std::none_of(subDiags.begin(), subDiags.end(), [](auto& subDiag) { return subDiag.mainHint.range.HasZero(); });
}

Position Diagnostic::GetBegin()
{
    if (!start.IsZero()) {
        return start;
    }
    return mainHint.range.begin;
}

Position Diagnostic::GetEnd()
{
    if (!end.IsZero()) {
        return end;
    }
    return mainHint.range.end;
}

std::string Diagnostic::GetErrorMessage()
{
    if (!diagMessage.empty()) {
        return diagMessage;
    }
    return errorMessage;
}

DiagCategory Diagnostic::GetDiagCategory() const
{
    return diagCategory;
}

int Diagnostic::GetDiagKind() const
{
    if (!start.IsZero()) {
        return static_cast<int>(kind);
    }
    // Sema diag end is last position in normal diag kind.
    // In order to make diagKind is unique, we only need to add rKind on it.
    return static_cast<int>(DiagKind::sema_diag_end) + static_cast<int>(rKind);
}

std::string Diagnostic::InsertArguments(std::string& rawString, std::vector<std::string>& arguments)
{
    if (rawString == "") {
        return rawString;
    }
    auto formatPos = rawString.find("%s");
    size_t index = 0;
    while (formatPos != std::string::npos) {
        CJC_ASSERT(index < arguments.size());
        (void)rawString.replace(formatPos, std::string("%s").size(), arguments[index]);
        formatPos += arguments[index].size();
        index++;
        formatPos = rawString.find("%s", formatPos);
    }
    CJC_ASSERT(index == arguments.size());
    return rawString;
}

void Diagnostic::HandleBadOtherHints()
{
    if (otherHints.empty()) {
        return;
    }
    for (auto it = otherHints.begin(); it != otherHints.end();) {
        if (it->range.begin.fileID == mainHint.range.begin.fileID) {
            ++it;
        } else {
            subDiags.emplace_back(it->range, it->str);
            it = otherHints.erase(it);
        }
    }
}

DiagnosticBuilder::DiagnosticBuilder(DiagnosticEngine& diag, Diagnostic diagnostic)
    : diagnostic(std::move(diagnostic)), diag(diag)
{
}

DiagnosticBuilder::~DiagnosticBuilder()
{
#ifndef CANGJIE_ENABLE_GCOV
    try {
#endif
        if (!diag.GetEnableDiagnose()) {
            auto storedDiags = diag.ConsumeStoredDiags();
            storedDiags.emplace_back(diagnostic);
            diag.SetStoredDiags(std::move(storedDiags));
            return;
        }
        if (!diagnostic.isRefactor) {
            diag.ConvertArgsToDiagMessage(diagnostic);
            if (diag.DiagFilter(diagnostic)) {
                return;
            }
            if (diagnostic.kind == DEFAULT_KIND) {
                return;
            }
        }
        // No error will be reported during semantic analysis of code in macroCall.
        if (diagnostic.isInMacroCall) {
            return;
        }
        diag.HandleDiagnostic(diagnostic);
#ifndef CANGJIE_ENABLE_GCOV
    } catch (...) {
        CJC_ABORT();
    }
#endif
}

void DiagnosticBuilder::AddHint(const Range& range, std::vector<std::string>& arguments)
{
    diag.CheckRange(diagnostic.GetDiagCategory(), range);

    auto errData = errorData[static_cast<unsigned>(diagnostic.rKind)];

    if (diagnostic.otherHints.size() >= errData.otherHints.size()) {
        CJC_ASSERT(arguments.size() <= 1);
        auto str = arguments.empty() ? "" : arguments.front();
        diagnostic.otherHints.emplace_back(range, str, OTHER_HINT_COLOR);
        return;
    }
    auto insertedStr = Diagnostic::InsertArguments(errData.otherHints.at(diagnostic.otherHints.size()), arguments);
    auto styledString = IntegratedString(range, insertedStr, OTHER_HINT_COLOR);
    diagnostic.otherHints.push_back(styledString);
}

void DiagnosticBuilder::AddNote(const SubDiagnostic& sub)
{
    diag.CheckRange(diagnostic.GetDiagCategory(), sub.mainHint.range);

    for (auto& hint : sub.otherHints) {
        diag.CheckRange(diagnostic.GetDiagCategory(), hint.range);
    }

    for (auto& substitution : sub.help.substitutions) {
        diag.CheckRange(diagnostic.GetDiagCategory(), substitution.range);
    }

    diagnostic.subDiags.push_back(sub);
}

void DiagnosticBuilder::AddNote(const Range& range, const std::string& note)
{
    diag.CheckRange(diagnostic.GetDiagCategory(), range);
    SubDiagnostic subDiag(range, note);
    AddNote(subDiag);
}

void DiagnosticBuilder::AddNote(const Position& pos, const std::string& note)
{
    auto end = pos == DEFAULT_POSITION ? pos : pos + 1;
    AddNote(MakeRange(pos, end), note);
}

void DiagnosticBuilder::AddNote(const AST::Node& node, const std::string& note)
{
    auto range = MakeRange(node.begin, node.end);
    diag.CheckRange(diagnostic.GetDiagCategory(), range);
    SubDiagnostic subDiag(range, note);
    AddNote(subDiag);
}

void DiagnosticBuilder::AddNote(const AST::Node& node, const Range& range, const std::string& note)
{
    diag.CheckRange(diagnostic.GetDiagCategory(), range);

    auto newRange = MakeRange(node.GetMacroCallPos(range.begin), node.GetMacroCallPos(range.end, true));
    SubDiagnostic subDiag(newRange, note);
    AddNote(subDiag);
}

void DiagnosticBuilder::AddNote(const std::string& note)
{
    SubDiagnostic subDiag(note);
    AddNote(subDiag);
}

void DiagnosticBuilder::AddHelp(const DiagHelp& help)
{
    for (auto& sub : help.substitutions) {
        diag.CheckRange(diagnostic.GetDiagCategory(), sub.range);
    }
    diagnostic.helps.push_back(help);
}
void DiagnosticEngineImpl::RegisterHandler(std::unique_ptr<DiagnosticHandler>&& h)
{
    handler = std::move(h);
}

void DiagnosticEngineImpl::IncreaseErrorCount(DiagCategory category)
{
    firstErrorCategoryMtx.lock();
    if (!firstErrorCategory.has_value() || category < firstErrorCategory.value()) {
        firstErrorCategory = category;
    }
    firstErrorCategoryMtx.unlock();
    mux.lock();
    countByCategory[category].first = countByCategory[category].first + 1;
    mux.unlock();
    IncreaseErrorCount();
}

void DiagnosticEngineImpl::IncreaseWarningCount(DiagCategory category)
{
    mux.lock();
    countByCategory[category].second = countByCategory[category].second + 1;
    warningCount++;
    mux.unlock();
}

void DiagnosticEngineImpl::IncreaseErrorCount()
{
    mux.lock();
    errorCount++;
    mux.unlock();
}

uint64_t DiagnosticEngineImpl::GetWarningCount()
{
    [[maybe_unused]] std::lock_guard<std::mutex> guard(firstErrorCategoryMtx);
    if (firstErrorCategory.has_value()) {
        uint64_t cnt = 0;
        // For warning, the count is total from start to first error category.
        for (auto i = 0; i <= static_cast<int>(firstErrorCategory.value()); i++) {
            cnt += countByCategory[static_cast<DiagCategory>(i)].second;
        }
        return cnt;
    }
    return warningCount;
}

uint64_t DiagnosticEngineImpl::GetErrorCount()
{
    [[maybe_unused]] std::lock_guard<std::mutex> guard(firstErrorCategoryMtx);
    if (firstErrorCategory.has_value()) {
        // For warning, the count is number of first error category.
        if (auto iter = countByCategory.find(firstErrorCategory.value()); iter != countByCategory.end()) {
            return iter->second.first;
        }
    }
    return 0;
}

void DiagnosticEngine::RegisterHandler(DiagFormat format)
{
#if defined __GNUC__ && not defined __clang__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-default"
#endif
    switch (format) {
        case DiagFormat::JSON: {
            auto h = std::make_unique<CompilerDiagnosticHandler>(*this, true, true);
            RegisterHandler(std::move(h));
            break;
        }
        case DiagFormat::NO_COLOR: {
            auto h = std::make_unique<CompilerDiagnosticHandler>(*this, true);
            RegisterHandler(std::move(h));
            break;
        }
        case DiagFormat::DEFAULT: {
            auto h = std::make_unique<CompilerDiagnosticHandler>(*this);
            RegisterHandler(std::move(h));
            break;
        }
    }
#if defined __GNUC__ && not defined __clang__
#pragma GCC diagnostic pop
#endif
}

bool DiagnosticEngineImpl::IsSupressedUnusedMain(const Diagnostic& diagnostic) noexcept
{
    return warningOption->IsSuppressed(static_cast<size_t>(WarnGroup::UNUSED_MAIN)) &&
        diagnostic.rKind == DiagKindRefactor::chir_dce_unused_function_main;
}

void DiagnosticEngineImpl::HandleDiagnostic(Diagnostic& diagnostic) noexcept
{
    if (!enableDiagnose) {
        return;
    }
    if (diagnostic.diagSeverity == DiagSeverity::DS_WARNING) {
        CJC_ASSERT(diagnostic.warnGroup != WarnGroup::NONE);
        if (disableWarning || warningOption->IsSuppressed(static_cast<size_t>(diagnostic.warnGroup)) ||
            IsSupressedUnusedMain(diagnostic)) {
            return;
        } else if (diagnostic.warnGroup != WarnGroup::UNGROUPED) {
            std::string warnGroupName = warnGroupDescrs[static_cast<unsigned>(diagnostic.warnGroup)];
            auto msg = "this warning can be suppressed by setting the compiler option `-Woff " + warnGroupName + "`";
            auto note = SubDiagnostic(msg);
            diagnostic.subDiags.push_back(note);
        }
    }
    CJC_ASSERT(handler);

    transactionMutex.lock();
    if (isInTransaction[std::this_thread::get_id()]) {
        // if diagnosticEngine is in a transaction,
        // whole diagnostics in this transaction will be stored in transactionMap temporarily until `Commit` is called
        transactionMap[std::this_thread::get_id()].emplace_back(diagnostic);
        transactionMutex.unlock();
    } else {
        transactionMutex.unlock();
        handler->HandleDiagnose(diagnostic);
    }
}

void DiagnosticEngineImpl::Prepare()
{
    transactionMutex.lock();
    CJC_ASSERT(!isInTransaction[std::this_thread::get_id()]);

    isInTransaction[std::this_thread::get_id()] = true;
    transactionMap[std::this_thread::get_id()].clear();
    transactionMutex.unlock();
}

void DiagnosticEngineImpl::Commit()
{
    transactionMutex.lock();
    CJC_ASSERT(isInTransaction[std::this_thread::get_id()]);
    auto& cachedDiagnostic = transactionMap[std::this_thread::get_id()];
    for (auto& diagnostic : cachedDiagnostic) {
        handler->HandleDiagnose(diagnostic);
    }
    transactionMap.erase(std::this_thread::get_id());
    isInTransaction.erase(std::this_thread::get_id());
    transactionMutex.unlock();
}

void DiagnosticEngineImpl::ClearTransaction()
{
    transactionMutex.lock();
    CJC_ASSERT(isInTransaction[std::this_thread::get_id()]);
    transactionMap.erase(std::this_thread::get_id());
    isInTransaction.erase(std::this_thread::get_id());
    transactionMutex.unlock();
}

std::string DiagnosticEngineImpl::GetArgStr(
    char formatChar, std::vector<DiagArgument>& formatArgs, unsigned long index, Diagnostic& diagnostic)
{
    switch (formatChar) {
        case 'd':
            if (auto val = std::get_if<int>(&formatArgs[index].arg)) {
                return std::to_string(*val);
            } else {
                Errorln("The num ", index, "format parameter does not match");
            }
            break;
        case 's':
            if (auto val = std::get_if<std::string>(&formatArgs[index].arg)) {
                if (val->empty()) {
                    diagnostic.kind = DEFAULT_KIND;
                }
                return *val;
            } else {
                Errorln("The num ", index, "format parameter does not match");
            }
            break;
        case 'c':
            if (auto val = std::get_if<char>(&formatArgs[index].arg); val) {
                return std::string(1, *val);
            } else {
                Errorln("The num ", index, "format parameter does not match");
            }
            break;
        case 'p':
            if (auto val = std::get_if<Position>(&formatArgs[index].arg); val) {
                return GetSourceManager().GetSource((*val).fileID).path + ":" + std::to_string((*val).line) + ":" +
                    std::to_string((*val).column);
            } else {
                Errorln("The num ", index, "format parameter does not match");
            }
            break;
        default:
            Errorln("%", formatChar, " is illegal format");
            break;
    }
    return {};
}

void DiagnosticEngineImpl::ConvertArgsToDiagMessage(Diagnostic& diagnostic) noexcept
{
    std::string formatStr = DiagMessages[static_cast<int>(diagnostic.kind)];
    // C string format length, like length of '%f', '%d'.
    const static size_t formatLens = 2;
    uint8_t index = 0;
    auto formatPos = formatStr.find('%');
    auto& formatArgs = diagnostic.args;
    while (formatPos != std::string::npos) {
        if (index >= formatArgs.size()) {
            break;
        }
        if (formatPos + 1 >= formatStr.size()) {
            return;
        }
        std::string argStr = GetArgStr(formatStr[formatPos + 1], formatArgs, index, diagnostic);
        if (!argStr.empty()) {
            (void)formatStr.replace(formatPos, formatLens, argStr);
            formatPos += argStr.size();
        }
        index++;
        formatPos = formatStr.find('%', formatPos);
    }
    diagnostic.diagMessage = formatStr;
    for (auto& note : diagnostic.notes) {
        ConvertArgsToDiagMessage(note);
    }
}

void DiagnosticEngineImpl::CheckRange(DiagCategory cate, const Range& range)
{
    // if Parse/Lex Stage has errors, even though the `range` in follow-up phases `HasZero`.
    // the `InternalError` is skipped.
    auto checkZero = [this, &range]() {
        // check DEFAULT_POSITION for libast before emit message
        if (checkRangeErrorCodeRatherICE) {
            if (range.begin.IsZero() || range.end.IsZero()) {
                diagEngineErrorCode = DiagEngineErrorCode::DIAG_RANGE_ERROR;
                return;
            }
        }
        if (range.begin.IsZero()) {
            InternalError("begin of range is zero");
        }
        if (range.end.IsZero()) {
            InternalError("end of range is zero");
        }
    };
    if (cate == DiagCategory::LEX || cate == DiagCategory::PARSE) {
        // Parse/Lex 's error always be checked. (in a way of InternalError)
        checkZero();
        return;
    }

    std::lock_guard<std::mutex> guard(mux);
    if ((countByCategory.count(DiagCategory::LEX) == 0 || countByCategory[DiagCategory::LEX].first == 0) &&
        (countByCategory.count(DiagCategory::PARSE) == 0 || countByCategory[DiagCategory::PARSE].first == 0)) {
        // only Parse/Lex doesn't have errors, check the range in follow-up phases' diagnose.
        checkZero();
        return;
    }
}

void DiagnosticEngineImpl::Reset()
{
    mux.lock();
    errorCount = 0;
    warningCount = 0;
    countByCategory.clear();
    mux.unlock();
    firstErrorCategoryMtx.lock();
    firstErrorCategory = std::nullopt;
    firstErrorCategoryMtx.unlock();
    handler->Clear();
}

bool DiagnosticEngineImpl::DiagFilter(Diagnostic& diagnostic) noexcept
{
    for (auto& filter : diagFilters) {
        if (filter(diagnostic)) {
            return true;
        }
    }
    return false;
}

DiagnosticEngine::DiagnosticEngine() : impl{new DiagnosticEngineImpl{}}
{
    auto h = std::make_unique<CompilerDiagnosticHandler>(*this);
    RegisterHandler(std::move(h));
}

DiagnosticEngine::~DiagnosticEngine() noexcept
{
    delete impl;
}

std::vector<Diagnostic> DiagnosticEngineImpl::DisableDiagnose()
{
    disableDiagDeep = disableDiagDeep + 1;
    if (disableDiagDeep > 0) {
        if (enableDiagnose) {
            enableDiagnose = false;
        }
    }
    return ConsumeStoredDiags();
}

void DiagnosticEngineImpl::EnableDiagnose()
{
    if (disableDiagDeep > 0) {
        disableDiagDeep = disableDiagDeep - 1;
    }
    if (disableDiagDeep == 0) {
        if (!enableDiagnose) {
            enableDiagnose = true;
            storedDiags.clear();
        }
    }
}

void DiagnosticEngineImpl::EnableDiagnose(const std::vector<Diagnostic>& diags)
{
    EnableDiagnose();
    storedDiags = diags;
}

std::vector<Diagnostic> DiagnosticEngineImpl::ConsumeStoredDiags()
{
    std::vector<Diagnostic> stored = storedDiags;
    storedDiags.clear();
    return stored;
}

Range DiagnosticEngineImpl::MakeRealRange(
    const AST::Node& node, const Position begin, const Position end, bool begLowBound) const
{
    auto newBegin = node.GetMacroCallPos(begin, begLowBound);
    auto newEnd = node.GetMacroCallPos(end, true);
    return MakeRange(newBegin, newEnd);
}

void DiagnosticEngineImpl::SetSourceManager(SourceManager* sm)
{
    CJC_NULLPTR_CHECK(sm);
    sourceManager = sm;
}

SourceManager& DiagnosticEngineImpl::GetSourceManager() noexcept
{
    CJC_NULLPTR_CHECK(sourceManager);
    return *sourceManager;
}

void DiagnosticEngineImpl::ReportErrorAndWarningCount()
{
    if (!GetIsDumpErrCnt()) {
        return;
    }
    if (handler->GetKind() == DiagHandlerKind::COMPILER_HANDLER) {
        auto hk = static_cast<CompilerDiagnosticHandler*>(handler.get());
        if (hk->IsJsonFormat()) {
            hk->CacheTheCountInJsonFormat();
            return;
        }
    }
    auto errorCnt = GetErrorCount();
    auto errorPrintCnt = GetErrorPrintCount();
    if (errorCnt > 0) {
        WriteError(errorCnt, " error", ((errorCnt > 1) ? "s" : ""), " generated, ");
        WriteError(errorPrintCnt, " error", ((errorPrintCnt > 1) ? "s" : ""), " printed.\n");
    }
    auto warningCnt = GetWarningCount();
    auto warningPrintCnt = GetWarningPrintCount();
    if (warningCnt > 0) {
        WriteError(warningCnt, " warning", ((warningCnt > 1) ? "s" : ""), " generated, ");
        WriteError(warningPrintCnt, " warning", ((warningPrintCnt > 1) ? "s" : ""), " printed.\n");
    }
}

void DiagnosticEngineImpl::AddMacroCallNote(Diagnostic& diagnostic, const AST::Node& node, const Position& pos)
{
    if (!node.TestAttr(AST::Attribute::MACRO_EXPANDED_NODE) || !node.curMacroCall) {
        return;
    }
    diagnostic.curMacroCall = node.curMacroCall;
    // Refactor the Diagnose of the node after the macro expansion.
    auto pInvocation = node.curMacroCall->GetInvocation();
    if (!pInvocation || IsPureAnnotation(*pInvocation)) {
        return;
    }

    // For cjc, display a hint message on the source code if the corresponding source code exists.
    if (!pInvocation->isForLSP) {
        Position originPos;
        auto key = pInvocation->isCurFile ? pos.Hash32() : static_cast<const uint32_t>(pos.column);
        if (pInvocation->isCurFile && pInvocation->originPosMap.find(key) != pInvocation->originPosMap.end()) {
            originPos = pInvocation->originPosMap.at(key);
        } else if (pInvocation->new2originPosMap.find(pos.Hash32()) != pInvocation->new2originPosMap.end()) {
            originPos = pInvocation->new2originPosMap.at(pos.Hash32());
            if (!originPos.isCurFile) {
                originPos = INVALID_POSITION;
            }
        }
        if (originPos != INVALID_POSITION && originPos != diagnostic.start &&
            originPos != diagnostic.mainHint.range.begin) {
            if (diagnostic.errorMessage.empty()) {
                ConvertArgsToDiagMessage(diagnostic);
                auto range = MakeRange(diagnostic.start, diagnostic.end);
                (void)diagnostic.subDiags.emplace_back(range, "which is expanded as follows");
                diagnostic.start = originPos;
                diagnostic.end = originPos + 1;
            } else {
                (void)diagnostic.subDiags.emplace_back(diagnostic.mainHint.range, "which is expanded as follows");
            }
            diagnostic.mainHint.range = MakeRange(originPos, originPos + 1);
        }
    }
    auto mcBegin = node.curMacroCall->begin;
    auto idPosEnd = pInvocation->identifierPos + pInvocation->identifier.size();
    // For lsp, the error range includes only identifier.
    // Otherwise, the error range includes the entire macrocall node.
    auto mcEnd = pInvocation->isForLSP ? idPosEnd : node.curMacroCall->end;
    std::string sevInfo = (diagnostic.diagSeverity == DiagSeverity::DS_ERROR) ? "the error" : "the warning";
    (void)diagnostic.subDiags.emplace_back(MakeRange(mcBegin, mcEnd), sevInfo + " occurs after the macro is expanded");
    if (!pInvocation->isForLSP) {
        auto codeRange = MakeRange(pInvocation->mcBegin, pInvocation->mcEnd);
        (void)diagnostic.subDiags.emplace_back(codeRange, MACROCALL_CODE);
    }
}

void DiagnosticHandler::SetPrevDiag(Position pos, std::string str)
{
    mtx.lock();
    prevDiags.emplace(pos, str);
    mtx.unlock();
}

bool DiagnosticHandler::HasPrevDiag(Position pos, std::string str)
{
    auto pair = std::make_pair(pos, str);
    mtx.lock();
    bool res = prevDiags.find(pair) != prevDiags.end();
    mtx.unlock();
    return res;
}

static void ConvertOldDiagToNew(Diagnostic& d)
{
    d.isConvertedToRefactor = d.isRefactor = true;
    d.errorMessage = d.diagMessage;
    CJC_ASSERT(SEVE_TO_COLOR.find(d.diagSeverity) != SEVE_TO_COLOR.end());
    auto first = d.start;
    auto second = (d.end == DEFAULT_POSITION || d.end > d.start) ? d.end : d.start + 1;
    d.mainHint = IntegratedString{MakeRange(first, second), "", SEVE_TO_COLOR.at(d.diagSeverity)};
    if (!d.notes.empty()) {
        for (auto& n : d.notes) {
            first = n.start;
            second = (n.end == DEFAULT_POSITION || n.end > n.start) ? n.end : n.start + 1;
            d.subDiags.emplace_back(MakeRange(first, second), n.diagMessage);
        }
    }

    d.start = INVALID_POSITION;
    d.end = INVALID_POSITION;
    d.diagMessage.clear();
    d.notes.clear();
    d.rKind = DiagKindRefactor::lex_diag_begin;
}

bool CompilerDiagnosticHandler::SaveDiagnostics(const Diagnostic& d)
{
    static const std::vector<DiagCategory> CATEGORY_BE_SAVEED = {
        DiagCategory::LEX,
        DiagCategory::PARSE,
        DiagCategory::CONDITIONAL_COMPILATION,
        DiagCategory::IMPORT_PACKAGE,
        DiagCategory::MODULE,
        DiagCategory::MACRO_EXPAND,
        DiagCategory::SEMA,
        DiagCategory::CHIR,
        DiagCategory::OTHER,
    };
    return Utils::In(d.diagCategory, CATEGORY_BE_SAVEED) ? SaveCategoryDiagnostic(d) : true;
}

bool CompilerDiagnosticHandler::CanBeEmitted(const DiagCategory& d)
{
    // Only emit first category currently, consider remove this limitation.
    auto lock = diag.LockFirstErrorCategory();
    bool result = !diag.FirstErrorCategory() || d <= *diag.FirstErrorCategory();
    return result;
}

void CompilerDiagnosticHandler::HandleDiagnose(Diagnostic& d)
{
    // This is for unifying old and new diagnostic messages.
    if (!d.isRefactor) {
        ConvertOldDiagToNew(d);
    }
    if (HasPrevDiag(d.mainHint.range.begin, d.errorMessage)) {
        return;
    }
    SetPrevDiag(d.mainHint.range.begin, d.errorMessage);

    // Diagnostic engine can accept diag without position, like driver diagnostics.
    if (!d.mainHint.range.IsDefault()) {
        if (!jsonFormat) {
            d.HandleBadOtherHints();
        }
        if (!SaveDiagnostics(d)) {
            return;
        }
    }

    if (!CanBeEmitted(d.diagCategory)) {
        return;
    }
    if (d.diagSeverity == DiagSeverity::DS_ERROR) {
        diag.IncreaseErrorCount(d.diagCategory);
    }
    if (d.diagSeverity == DiagSeverity::DS_WARNING) {
        diag.IncreaseWarningCount(d.diagCategory);
    }
    // The lex, chir and parse is doing in parallel. So there is data race if we emit it immediately.
    // For lex, chir and parse diagnostic category: collect and emit at the same time.
    // other category: emit immediately.
    if (d.diagCategory == DiagCategory::LEX || d.diagCategory == DiagCategory::PARSE ||
        d.diagCategory == DiagCategory::CHIR) {
        return;
    }
    EmitDiagnose(d);
}

void CompilerDiagnosticHandler::EmitDiagnose(Diagnostic d)
{
    if (!diag.GetIsEmitter()) {
        return;
    }
    // Check whether we have printed enough errors, i.e. amount of errors exceeded error count limit or not.
    auto maybeNumber = diag.GetMaxNumOfDiags();
    if (maybeNumber.has_value() && diag.GetErrorPrintCount() >= maybeNumber.value()) {
        return;
    }
    bool noRangeCheckError = true;
    if (jsonFormat) {
        DiagnosticJsonFormatter formatter(diag);
        diagsJsonBuff.push_back(formatter.FormatDiagnosticToJson(d));
    } else if (diag.HasSourceManager()) {
        DiagnosticEmitter tmp = DiagnosticEmitter(d, noColor, !diag.IsCheckRangeErrorCodeRatherICE(),
            outToStringStream ? strStream : std::cerr, diag.GetSourceManager());
        noRangeCheckError = tmp.Emit();
    } else {
        SourceManager sm;
        DiagnosticEmitter tmp = DiagnosticEmitter(
            d, noColor, !diag.IsCheckRangeErrorCodeRatherICE(), outToStringStream ? strStream : std::cerr, sm);
        noRangeCheckError = tmp.Emit();
    }
    if (!noRangeCheckError) {
        diag.SetDiagEngineErrorCode(DiagEngineErrorCode::DIAG_RANGE_ERROR);
    }
    if (d.diagSeverity == DiagSeverity::DS_ERROR) {
        diag.IncreaseErrorPrintCount();
    } else {
        diag.IncreaseWarningPrintCount();
    }
}

DiagnosticInfo CompilerDiagnosticHandler::GetDiagnosticInfo(Diagnostic d)
{
    DiagnosticInfo info{d.diagSeverity, d.mainHint.range, d.errorMessage};
    bool noRangeCheckError = true;
    std::ostringstream ss;
    if (diag.HasSourceManager()) {
        DiagnosticEmitter tmp = DiagnosticEmitter(d, true, !diag.IsCheckRangeErrorCodeRatherICE(),
            ss, diag.GetSourceManager());
        noRangeCheckError = tmp.Emit(true);
    } else {
        SourceManager sm;
        DiagnosticEmitter tmp = DiagnosticEmitter(
            d, true, !diag.IsCheckRangeErrorCodeRatherICE(), ss, sm);
        noRangeCheckError = tmp.Emit();
    }
    if (!noRangeCheckError) {
        diag.SetDiagEngineErrorCode(DiagEngineErrorCode::DIAG_RANGE_ERROR);
    }
    info.hint = ss.str();
    return info;
}

void CompilerDiagnosticHandler::CacheTheCountInJsonFormat()
{
    DiagnosticJsonFormatter formatter(diag);
    diagNumJsonBuff = formatter.FormatDiagnosticCountToJsonString();
}

void CompilerDiagnosticHandler::EmitDiagnoseGroup()
{
    EmitCategoryDiagnostics(DiagCategory::LEX);
    // the DiagCategory::PARSE is printed only if no DiagCategory::LEX is printed
    if (diag.GetErrorPrintCount() == 0) {
        EmitCategoryDiagnostics(DiagCategory::PARSE);
    }
}

void CompilerDiagnosticHandler::EmitDiagnosesInJson() noexcept
{
    if (!diag.GetIsEmitter()) {
        return;
    }
    std::cerr << DiagnosticJsonFormatter::AssembleDiagnosticJsonString(diagsJsonBuff, diagNumJsonBuff);
}

std::vector<Diagnostic> CompilerDiagnosticHandler::GetCategoryDiagnosticsSortedByRange(DiagCategory cate) const
{
    auto targets = GetCategoryDiagnostic(cate);
    std::sort(targets.begin(), targets.end(), [](auto& a, auto& b) -> bool {
        auto rangeA = a.mainHint.range;
        auto rangeB = b.mainHint.range;
        if (rangeA.begin < rangeB.begin) {
            return true;
        }
        if (rangeA.begin == rangeB.begin && rangeA.end < rangeB.end) {
            return true;
        }
        return false;
    });
    return targets;
}

void CompilerDiagnosticHandler::EmitCategoryDiagnostics(DiagCategory cate)
{
    if (diagnostics.count(cate) == 0) {
        return;
    }
    if (!CanBeEmitted(cate)) {
        return;
    }
    auto targets = GetCategoryDiagnosticsSortedByRange(cate);
    for (auto& d : targets) {
        EmitDiagnose(d);
    }
    diagnostics.erase(cate);
}

void CompilerDiagnosticHandler::EmitCategoryDiagnosticInfos(DiagCategory cate, std::vector<DiagnosticInfo>& diagOut)
{
    diagOut.clear();
    if (diagnostics.count(cate) == 0) {
        return;
    }
    if (!CanBeEmitted(cate)) {
        return;
    }
    auto targets = GetCategoryDiagnosticsSortedByRange(cate);
    for (auto& d : targets) {
        diagOut.emplace_back(GetDiagnosticInfo(d));
    }
    diagnostics.erase(cate);
}

bool DiagnosticCache::NoError() const
{
    for (auto& diag : cachedDiags) {
        if (diag.diagSeverity == DiagSeverity::DS_ERROR) {
            return false;
        }
    }
    return true;
}

DiagnosticCache::DiagCacheKey DiagnosticCache::ExtractKey(const DiagnosticEngine& diag)
{
    if (diag.GetDisableDiagDeep() == 0) {
        return 0;
    } else {
        return 1;
    }
}

void DiagnosticCache::ToExclude(const DiagnosticEngine& diagBefore)
{
    cachedDiags = diagBefore.GetStoredDiags();
}

void DiagnosticCache::BackUp(const DiagnosticEngine& diagAfter)
{
    auto& storedDiags = diagAfter.GetStoredDiags();
    if (storedDiags.size() > cachedDiags.size()) {
        cachedDiags = std::vector<Diagnostic>(
            storedDiags.begin() + static_cast<long>(cachedDiags.size()), storedDiags.end());
    } else {
        cachedDiags.clear();
    }
}

void DiagnosticCache::Restore(DiagnosticEngine& dst)
{
    auto old = dst.ConsumeStoredDiags();
    old.insert(old.end(), cachedDiags.begin(), cachedDiags.end());
    dst.SetStoredDiags(std::move(old));
}
}; // namespace Cangjie
