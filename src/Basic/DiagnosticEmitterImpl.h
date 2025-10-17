// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares class DiagnosticEmitterImpl, which is an implementation class of DiagnosticEmitter.
 */
#ifndef CANGJIE_BASIC_DIAGNOSTICEMITTERIMPL_H
#define CANGJIE_BASIC_DIAGNOSTICEMITTERIMPL_H

#include "cangjie/Basic/DiagnosticEmitter.h"
#include "cangjie/Basic/DiagnosticEngine.h"

namespace Cangjie {
using SubstitutionMap = std::map<unsigned int, std::vector<Substitution>>;
using HangingStr = std::vector<std::vector<std::string>>;
void HandleSpecialCharacters(std::string& str);

// This struct is to record information of one printed line.
struct CombinedLine {
    std::string meta;    // Line materials, without any color.
    unsigned int line;   // Source line number. It is 0 if meta is added.
    bool hasSourceFile{false};
    std::vector<std::tuple<size_t, size_t, DiagColor>> colors; // Range and color style to colorize meta.
};
using SourceCombinedVec = std::vector<CombinedLine>;

/**
 *  This is class collecting information to analyse added line and hanging line.
 */
struct CollectedInfo {
    Range range = MakeRange(DEFAULT_POSITION, DEFAULT_POSITION);
    // If this information is main hint.
    bool isMain{false};
    // If this information is navigating multiple line. if is multiple line, the range will be divided to two range, The
    // begin is one and the end is another one to keep all range in CollectedInfo is in same line.
    bool isMultiLine{false};
    // If this information is end of multiple line.
    bool isEnd{false};
    std::string hint;
    DiagColor color{DiagColor::RESET};
    bool IsDefault() const
    {
        return range.begin == DEFAULT_POSITION;
    }
};
using CollectedInfoMap = std::map<unsigned int, std::vector<CollectedInfo>>;

struct MultiLineHashFunc {
    size_t operator()(const CollectedInfo& info) const
    {
        const auto rangeHashLen = 2;
        return (info.range.Hash() << rangeHashLen) ^ static_cast<size_t>(info.isMain << 1) ^ (info.isMultiLine);
    }
};

struct MultiLineEqualFunc {
    size_t operator()(const CollectedInfo& l, const CollectedInfo& r) const
    {
        return l.range == r.range && l.isMain == r.isMain && l.isMultiLine == r.isMultiLine && l.isEnd == r.isEnd;
    }
};

class DiagnosticEmitterImpl final {
public:
    DiagnosticEmitterImpl(
        Diagnostic& d, bool nc, bool enableRangeCheckICE, std::basic_ostream<char>& o, SourceManager& sourceManager)
        : diag(d), noColor(nc), enableRangeCheckICE(enableRangeCheckICE), out(o), sm(sourceManager)
    {
    }
    bool Emit(bool enableOnlyHint = false);

private:
    Diagnostic& diag;
    bool noColor{false};
    bool enableRangeCheckICE{true};
    bool rangeCheckError{false};
    mutable size_t maxLineNum{0};
    std::basic_ostream<char>& out{std::cerr};
    SourceManager& sm;
    std::unordered_map<CollectedInfo, size_t, MultiLineHashFunc, MultiLineEqualFunc> multiLineRecordMap;
    std::vector<std::pair<size_t, size_t>> multiLineHangingPtrVec{};
    std::vector<std::vector<std::tuple<size_t, size_t, DiagColor>>> multiLineHangingVec{};
    void CollectInformation(std::vector<CollectedInfo>& vec, IntegratedString& str, bool isMain);
    void SortAndCheck(std::vector<CollectedInfo>& errorInfo) const;
    size_t GetDisplayedWidthFromSource(const std::string& source, const Range& range) const;
    void InsertSymbolInFirstLine(
        CombinedLine& combinedLine, size_t loc, const CollectedInfo& info, const std::string& sourceLine) const;
    void InsertSymbolNotFirstLine(CombinedLine& combinedLine, size_t loc, const CollectedInfo& info) const;
    void InsertSymbolToUpperLine(
        SourceCombinedVec& insertedStr, size_t loc, const CollectedInfo& info, const std::string& sourceLine) const;
    CombinedLine CombineErrorPrintSingleLineHelper(
        const std::string& sourceLine, const CollectedInfo& info, bool isFirstLine = false) const;
    void CombineErrorPrintSingleLine(
        SourceCombinedVec& insertedStr, const CollectedInfo& info, const std::string& sourceLine);
    HangingStr ConvertHangingContents(size_t line);
    void ConvertHangingContentsHelper(
        HangingStr& hanging, size_t i, size_t begin, size_t end, const DiagColor& color) const;
    void AnalyseMultiLineHanging(const CollectedInfo& info, size_t combinedVecSize);
    CombinedLine CombineErrorPrintMultiLineHelper(
        const std::string& sourceLine, const CollectedInfo& info, bool isFirstLine, size_t combinedVecSize);
    void CombineErrorPrintMultiLine(SourceCombinedVec& insertedStr, const CollectedInfo& info,
        const std::string& sourceLine, size_t combinedVecSize);
    void ColorizeCombinedVec(SourceCombinedVec& combinedVec) const;
    std::string GetSourceCode(std::vector<CollectedInfo>& errorInfo) const;
    bool CombineErrorPrint(CollectedInfoMap& infoMap, SourceCombinedVec& combinedVec);
    void HandleUnprintableChar(SourceCombinedVec& combinedVec) const;
    void CompressLineCode(CollectedInfoMap& infoMap, SourceCombinedVec& bindLineCodes) const;
    void EmitErrorMessage(DiagColor color, const std::string& err, const std::string& mes);
    void EmitErrorLocation(const Position& pos);
    void ConstructAndEmitSourceCode(std::vector<CollectedInfo>& errorInfo);
    void EmitSourceCode(SourceCombinedVec& combinedVec);
    void EmitNote();
    void EmitSingleNoteWithSource(SubDiagnostic& note);
    void EmitSingleMessageWithoutSource(const std::string& str, std::string host);
    SourceCombinedVec GetHelpSubstituteSource(DiagHelp& help);
    SubstitutionMap HelpSubstitutionToMap(DiagHelp& help) const;
    void HelpSubstituteConvertHelper(
        SubstitutionMap& subMap, std::string& rawStr, unsigned int line, std::vector<CollectedInfo>& infos) const;
    std::vector<CollectedInfo> HelpSubstituteConvert(DiagHelp& help, SourceCombinedVec& combinedVec) const;
    void EmitSingleHelpWithSource(DiagHelp& help);
    void EmitHelp(std::vector<DiagHelp>& helps);
};
} // namespace Cangjie
#endif
