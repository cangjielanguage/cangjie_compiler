// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the DiagnosticEngine related classes, which provides diagnostic capabilities.
 */

#ifndef CANGJIE_BASIC_DIAGNOSTICENGINE_H
#define CANGJIE_BASIC_DIAGNOSTICENGINE_H

#include <list>

#include "cangjie/AST/Node.h"
#include "cangjie/Basic/Position.h"
#include "cangjie/Basic/SourceManager.h"
#include "cangjie/Option/Option.h"

namespace Cangjie {
const int DEFAULT_DIAG_NUM = 8;
class DiagnosticEngine;
using ArgType = std::variant<int, std::string, char, Position>;
// It is true if all args type are std::string, otherwise false.
template <typename... Args> constexpr bool IsAllString = std::conjunction_v<std::is_convertible<Args, std::string>...>;
const DiagColor MAIN_HINT_COLOR = DiagColor::RED;
const DiagColor OTHER_HINT_COLOR = DiagColor::CYAN;
const DiagColor NOTE_COLOR = DiagColor::BLUE;
const DiagColor HELP_COLOR = DiagColor::GREEN;
const DiagColor NO_COLOR = DiagColor::NO_COLOR;
const std::string MACROCALL_CODE = "the code after the macro is expanded as follows";

/**
 * DiagArgument is a struct packing the argument of Diagnose function.
 */
struct DiagArgument {
public:
    ArgType arg;
    DiagArgument() = default;
    DiagArgument(int args) : arg(args)
    {
    }
    DiagArgument(const std::string& args) : arg(args)
    {
    }
    DiagArgument(const char* args) : arg(args)
    {
    }
    DiagArgument(int64_t args) : arg(static_cast<int>(args))
    {
    }
    DiagArgument(size_t args) : arg(static_cast<int>(args))
    {
    }
    DiagArgument(char args) : arg(args)
    {
    }
    DiagArgument(Position pos) : arg(pos)
    {
    }
    ~DiagArgument() = default;
};

/*
 * DiagKind is the specific diagnostic kind.
 */
enum class DiagKind {
#define NOTE(Kind, Info) Kind,
#define ERROR(Kind, Info) Kind,
#define WARNING(Kind, Group, Info) Kind,
#include "cangjie/Basic/DiagnosticsAll.def"
#undef ERROR
#undef WARNING
#undef NOTE
};

constexpr const char* const DIAG_KIND_STR[]{
#define NOTE(Kind, Info) #Kind,
#define ERROR(Kind, Info) #Kind,
#define WARNING(Kind, Group, Info) #Kind,
#include "cangjie/Basic/DiagnosticsAll.def"
#undef ERROR
#undef WARNING
#undef NOTE
};

constexpr size_t DIAG_KIND_STR_SIZE = sizeof(DIAG_KIND_STR) / sizeof(*DIAG_KIND_STR);

enum class DiagSeverity : uint8_t { DS_ERROR, DS_WARNING, DS_NOTE, DS_HINT };

enum class DiagCategory : uint8_t {
    LEX = 0,
    PARSE,
    PARSE_QUERY, // Parse query only used for lsp.
    CONDITIONAL_COMPILATION,
    IMPORT_PACKAGE,
    MODULE,
    MACRO_EXPAND,
    SEMA,
    CHIR,
    OTHER
};

constexpr const DiagSeverity DiagSeveritys[]{
#define ERROR(Kind, Info) DiagSeverity::DS_ERROR,
#define NOTE(Kind, Info) DiagSeverity::DS_NOTE,
#define WARNING(Kind, Group, Info) DiagSeverity::DS_WARNING,
#include "cangjie/Basic/DiagnosticsAll.def"
#undef ERROR
#undef WARNING
#undef NOTE
};

constexpr const char* const DiagMessages[] = {
#define ERROR(Kind, Info) Info,
#define WARNING(Kind, Group, Info) Info,
#define NOTE(Kind, Info) Info,
#include "cangjie/Basic/DiagnosticsAll.def"
#undef ERROR
#undef WARNING
#undef NOTE
};

constexpr const WarnGroup warnGroups[] = {
#define ERROR(Kind, ...) WarnGroup::NONE,
#define WARNING(Kind, Group, ...) WarnGroup::Group,
#define NOTE(Kind, Info) WarnGroup::NONE,
#include "cangjie/Basic/DiagnosticsAll.def"
#undef WARNING
#undef ERROR
#undef NOTE
};

/**
 * This is class to keep diagnostic data for single error.
 * Format of one diagnostic like:
 * ```
 * error: `error message'
 *  ==> file.cj:line:column
 *   |      source code 1
 *   |      ^^^^^^^^^^^^^ 'mainHint'
 *   |    source code 2            source code 3
 *   |    ~~~~~~ `otherHint 1`       ~~~~~~ 'otherHint 2`
 * ```
 * note: the 'error message' and 'mainHint' share same source code position. the 'otherHints' need register new position
 *       if it has.
 * */
struct ErrorData {
    std::string message;
    std::string mainHint;
    std::vector<std::string> otherHints{};
    ErrorData(std::string message1 = "", std::string mainHint1 = "", std::vector<std::string> otherHints1 = {})
        : message(std::move(message1)), mainHint(std::move(mainHint1)), otherHints(std::move(otherHints1))
    {
    }
};

/**
 *  This is new diagKind for refactoring, will replace previous DiagKind if all diag have been modified.
 */
const ErrorData errorData[] = {
#define ERROR(Kind, ...) {__VA_ARGS__},
#define WARNING(Kind, Group, ...) {__VA_ARGS__},
#include "cangjie/Basic/DiagRefactor/DiagnosticAll.def"
#undef WARNING
#undef ERROR
};

const std::map<DiagSeverity, DiagColor> SEVE_TO_COLOR{
    {DiagSeverity::DS_ERROR, DiagColor::RED}, {DiagSeverity::DS_WARNING, DiagColor::YELLOW},
    {DiagSeverity::DS_NOTE, DiagColor::RED}};
const DiagKind DEFAULT_KIND = DiagKind::sema_diag_begin;

/**
 *  This is new diagKind for refactoring, will replace previous DiagKind if all diag have been modified.
 */
constexpr const DiagSeverity rDiagSeveritys[] = {
#define ERROR(Kind, ...) DiagSeverity::DS_ERROR,
#define WARNING(Kind, ...) DiagSeverity::DS_WARNING,
#include "cangjie/Basic/DiagRefactor/DiagnosticAll.def"
#undef WARNING
#undef ERROR
};

constexpr const WarnGroup rWarnGroups[] = {
#define ERROR(Kind, ...) WarnGroup::NONE,
#define WARNING(Kind, Group, ...) WarnGroup::Group,
#include "cangjie/Basic/DiagRefactor/DiagnosticAll.def"
#undef WARNING
#undef ERROR
};

constexpr const char* warnGroupDescrs[] = {
#define WARN_GROUP(DESCR, KIND) DESCR,
#include "cangjie/Basic/DiagRefactor/DiagnosticWarnGroupKind.def"
#undef WARN_GROUP
};
constexpr size_t WARN_GROUP_DESCRS_SIZE = sizeof(warnGroupDescrs) / sizeof(*warnGroupDescrs);

/**
 *  This is new diagKind for refactoring, will replace previous DiagKind if all diag have been modified.
 */
enum class DiagKindRefactor : unsigned {
#define ERROR(Kind, ...) Kind,
#define WARNING(Kind, ...) Kind,
#include "cangjie/Basic/DiagRefactor/DiagnosticAll.def"
#undef WARNING
#undef ERROR
};

constexpr const char* const RE_DIAG_KIND_STR[]{
#define ERROR(Kind, ...) #Kind,
#define WARNING(Kind, ...) #Kind,
#include "cangjie/Basic/DiagRefactor/DiagnosticAll.def"
#undef WARNING
#undef ERROR
};

constexpr size_t RE_DIAG_KIND_STR_SIZE = sizeof(RE_DIAG_KIND_STR) / sizeof(*RE_DIAG_KIND_STR);
struct Range {
    Position begin;
    Position end;
    bool operator==(const Range& right) const
    {
        return begin == right.begin && end == right.end;
    }
    size_t Hash() const
    {
        return (static_cast<size_t>(begin.fileID)) ^ (static_cast<size_t>(begin.line) << 8u) ^
            (static_cast<size_t>(begin.column) << 16u) ^ (static_cast<size_t>(begin.fileID) << 24u) ^
            (static_cast<size_t>(begin.line) << 32u) ^ (static_cast<size_t>(begin.column) << 40u);
    }
    bool EqualForHash(const Range& right) const
    {
        return begin.fileID == right.begin.fileID && begin == right.begin && end == right.end;
    }
    bool IsDefault() const
    {
        return begin == DEFAULT_POSITION && end == DEFAULT_POSITION;
    }
    bool HasZero() const
    {
        return begin.IsZero() || end.IsZero();
    }
    friend Range MakeRange(const Position& begin, Position end);
    friend Range MakeRange(const Position& identifierPos, const std::string& identifier);

private:
    // The constructor of range is private, use MakeRange to make a Range.
    Range(Position b, Position e) : begin(b), end(e)
    {
    }
};

Range MakeRange(const Position& begin, Position end);
Range MakeRange(const Position& identifierPos, const std::string& identifier);
Range MakeRange(const Identifier& id);

struct IntegratedString {
    Range range = MakeRange(DEFAULT_POSITION, DEFAULT_POSITION);
    std::string str;
    DiagColor color{DiagColor::RESET};
    IntegratedString() = default;
    IntegratedString(Range r, std::string s, DiagColor c) : range(r), str(std::move(s)), color(c)
    {
    }
    inline bool IsDefault() const
    {
        return range.begin == DEFAULT_POSITION && range.end == DEFAULT_POSITION;
    }
};

struct Substitution {
    Range range = MakeRange(DEFAULT_POSITION, DEFAULT_POSITION);
    std::string str;
    Substitution(Range& r, std::string& s) : range(r), str(s)
    {
    }
};

struct DiagHelp {
    std::vector<Substitution> substitutions;
    std::string helpMes;
    DiagHelp() = default;
    DiagHelp(std::string s) : helpMes(std::move(s))
    {
    }
    void AddSubstitution(Position p, std::string s)
    {
        auto range = MakeRange(p, p + 1);
        substitutions.emplace_back(range, s);
    }
    void AddSubstitution(Range range, std::string s)
    {
        substitutions.emplace_back(range, s);
    }
    void AddSubstitution(const Token& t, std::string s)
    {
        auto range = MakeRange(t.Begin(), t.End());
        substitutions.emplace_back(range, s);
    }
    void AddSubstitution(AST::Node& node, std::string s)
    {
        auto range = MakeRange(node.begin, node.end);
        substitutions.emplace_back(range, s);
    }
    bool IsShowSource() const
    {
        return !substitutions.empty();
    }
    bool IsDefault() const
    {
        return helpMes.empty() && substitutions.empty();
    }
};

/**
 *  SubDiagnostic attach to diagnostic, like note attach to error or warning.
 */
struct SubDiagnostic {
    std::string subDiagMessage;
    IntegratedString mainHint;
    std::vector<IntegratedString> otherHints;
    DiagHelp help;
    SubDiagnostic() = delete;
    explicit SubDiagnostic(std::string s) : subDiagMessage(std::move(s))
    {
    }
    SubDiagnostic(const Range& range, const std::string& s) : subDiagMessage(s)
    {
        mainHint = IntegratedString{range, "", NOTE_COLOR};
    }

    void AddMainHint(const Range& range, const std::string& str)
    {
        mainHint = IntegratedString{range, str, NOTE_COLOR};
    }
    void AddHelp(DiagHelp& h)
    {
        help = h;
    }
    bool IsShowSource() const
    {
        return !(mainHint.IsDefault() && otherHints.empty());
    }

private:
    void AddMainHint(const Position& pos, const std::string& str)
    {
        Range range = MakeRange(pos, pos + 1);
        AddMainHint(range, str);
    }
    void AddMainHint(const Token& tok, const std::string& str)
    {
        Range range = MakeRange(tok.Begin(), tok.End());
        AddMainHint(range, str);
    }
    void AddMainHint(const AST::Node& node, const std::string& str)
    {
        Range range = MakeRange(node.begin, node.end);
        AddMainHint(range, str);
    }
};

/**
 *  Diagnostic contains all diagnostic information.
 *  regular variable name: diagnostic
 */
class Diagnostic {
public:
    template <typename... Args>
    Diagnostic(const Position s, const Position e, const DiagKind kind, const Args... args) : start(s), end(e),
        kind(kind), args{args...}
    {
        diagSeverity = DiagSeveritys[static_cast<int>(kind)];
        diagCategory = GetDiagnoseCategory(kind);
        warnGroup = warnGroups[static_cast<unsigned>(kind)];
        // Refactor kind is set by default.
        rKind = DiagKindRefactor::parse_diag_begin;
    };
    // This is new diagKind for refactoring, will replace previous DiagKind if all diag have been modified.
    template <typename... Args>
    Diagnostic(bool refactor, const Range range, const DiagKindRefactor kind, const Args... args)
        : isRefactor(refactor), rKind(kind)
    {
        static_assert(IsAllString<Args...>, "the diagnostic only support string type argument");

        std::vector<std::string> arguments{args...};
        auto errData = errorData[static_cast<unsigned>(kind)];
        diagSeverity = rDiagSeveritys[static_cast<unsigned>(kind)];
        warnGroup = rWarnGroups[static_cast<unsigned>(kind)];

        errorMessage = InsertArguments(errData.message, arguments);
        if (SEVE_TO_COLOR.find(diagSeverity) != SEVE_TO_COLOR.end()) {
            mainHint = IntegratedString(range, errData.mainHint, SEVE_TO_COLOR.at(diagSeverity));
        } else {
            mainHint = IntegratedString(range, errData.mainHint, DiagColor::RED);
        }

        diagCategory = GetDiagnoseCategory(kind);
    }

    Diagnostic()
    {
        diagSeverity = DiagSeveritys[static_cast<int>(kind)];
        diagCategory = GetDiagnoseCategory(kind);
        warnGroup = warnGroups[static_cast<unsigned>(kind)];
    };

    Position start;                            /**< Diagnostic start position */
    Position end;                              /**< Diagnostic end position */
    DiagKind kind{DEFAULT_KIND}; /**< Diagnostic kind */
    bool printSourceCode{true};                /**< Whether to print the related source code. */
    /// Whether this Diagnostic is created from \ref DiagnoseRefactor
    bool isRefactor{false};
    bool isConvertedToRefactor{false};
    /// refactor kind
    DiagKindRefactor rKind{DiagKindRefactor::lex_diag_begin};
    std::string errorMessage;
    /// 3 | func foo1() : (Float32,String) {}
    ///   |               ~~~~~~~~~~~~~~~~ ^^ expected 'Tuple<Float32, Struct-String>', found 'Unit'
    ///   |               |
    ///   |               expected 'Tuple<Float32, Struct-String>' because of return type
    /// In such a diagnostic, the red message with ^ is called \ref mainHint, and the blue message with ~ is
    /// called \ref otherHints
    IntegratedString mainHint;
    std::vector<IntegratedString> otherHints;
    // Stands for refactor notes to differ with notes, need modify after refactor.
    std::vector<SubDiagnostic> subDiags;
    std::vector<DiagHelp> helps;
    std::vector<DiagArgument> args;
    DiagSeverity diagSeverity;
    std::string diagMessage;
    DiagCategory diagCategory;
    WarnGroup warnGroup;
    /// A note often points to another code segment that together with the main diagnostic position to cause the issue.
    /// For example, such message is a note \ref notes:
    /// note: the overriden function
    /// ==> xxx.cj:6:22:
    /// 6 |     public open func foo(): This {
    ///   |                      ^^^
    std::vector<Diagnostic> notes;
    Ptr<AST::Node> curMacroCall{nullptr};
    bool isInMacroCall{false};

    // This is API supported to lsp. Will delete after refactoring.
    bool IsValid() const;                 // Check if the diagnostic is valid.
    Position GetBegin();                  // Get begin position of the diagnostic.
    Position GetEnd();                    // Get end position of the diagnostic.
    std::string GetErrorMessage();        // Get error message.
    DiagCategory GetDiagCategory() const; // Get category.
    int GetDiagKind() const;
    void HandleBadOtherHints();
    static DiagCategory GetDiagnoseCategory(DiagKind diagKind);
    static DiagCategory GetDiagnoseCategory(DiagKindRefactor diagKind);
    static std::string InsertArguments(std::string& rawString, std::vector<std::string>& arguments);
};

enum class DiagHandlerKind : uint8_t {
    HANDLER,
    COMPILER_HANDLER,
    LSP_HANDLER,
};

/**
 * DiagnosticHandler is a abstract base class.It is responsible for handling diagnostic.
 */
class DiagnosticHandler {
public:
    /**
     * This is a Diagnostic handling function, it receives @p myDiag from DiagnosticEngine.
     * @param myDiag The Diagnostic to be handled.
     */
    virtual void HandleDiagnose(Diagnostic& myDiag)
    {
        (void)myDiag;
    }
    // This two structs are used to filter old style error messages.
    struct OldHashFunc {
        size_t operator()(const std::pair<Position, std::string>& pair) const
        {
            static const std::hash<std::string> hashString{};
            size_t stringHash = hashString(pair.second);
            const Position& pos = pair.first;
            return stringHash ^ (static_cast<size_t>(pos.fileID) << 8u) ^ (static_cast<size_t>(pos.line) << 16u) ^
                (static_cast<size_t>(pos.column) << 24u);
        }
    };
    struct OldEqualFunc {
        bool operator()(const std::pair<Position, std::string>& a, const std::pair<Position, std::string>& b) const
        {
            const Position& p1 = a.first;
            const Position& p2 = b.first;
            return p1.fileID == p2.fileID && p1.line == p2.line && p1.column == p2.column && a.second == b.second;
        }
    };
    virtual void Clear() const
    {
        prevDiags.clear();
    }
    void SetPrevDiag(Position pos, std::string str);
    bool HasPrevDiag(Position pos, std::string str);

    DiagHandlerKind GetKind() const noexcept
    {
        return kind;
    }
    
    friend class DiagnosticEngine;
    
    DiagnosticHandler(DiagnosticEngine& d, DiagHandlerKind k) : diag(d), kind(k)
    {
    }
    virtual ~DiagnosticHandler() = default;
protected:
    std::mutex mtx;
    DiagnosticEngine& diag;
    const DiagHandlerKind kind{DiagHandlerKind::HANDLER};
    mutable std::unordered_set<std::pair<Position, std::string>, OldHashFunc, OldEqualFunc> prevDiags;
};

/**
 * CompilerDiagObserver is the default DiagObserver of the compiler, and the diagnostic message will output to stdout or
 * stderr.
 */
class CompilerDiagnosticHandler : public DiagnosticHandler {
public:
    explicit CompilerDiagnosticHandler(DiagnosticEngine& diag, bool noC = false, bool json = false)
        : DiagnosticHandler(diag, DiagHandlerKind::COMPILER_HANDLER), noColor(noC), jsonFormat(json)
    {
    }
    void EmitCategoryDiagnostics(DiagCategory cate);
    void EmitDiagnoseGroup();
    void EmitDiagnosesInJson() noexcept;
    std::vector<Diagnostic> GetCategoryDiagnostic(
        DiagCategory cate) const
    {
        auto set = diagnostics[cate];
        return std::vector<Diagnostic>{set.begin(), set.end()};
    }
    struct hashFunc {
        // Only take the position and severity into account, if it is too restricted.
        size_t operator()(const Diagnostic& diag) const
        {
            return (diag.mainHint.range.Hash() >> 1) ^ (diag.diagSeverity == DiagSeverity::DS_ERROR);
        }
    };
    struct equalFunc {
        bool operator()(const Diagnostic& a, const Diagnostic& b) const
        {
            return a.mainHint.range.EqualForHash(b.mainHint.range) && (a.diagSeverity == b.diagSeverity);
        }
    };
    void Clear() const override
    {
        prevDiags.clear();
        diagnostics.clear();
    }
    /**
     * The compiler real behavior after @p diag emitting. Like: error message print to stderr.
     */
    bool SaveCategoryDiagnostic(const Diagnostic& d)
    {
        bool success = false;
        mtx.lock();
        if (diagnostics[d.diagCategory].find(d) == diagnostics[d.diagCategory].end()) {
            diagnostics[d.diagCategory].insert(d);
            success = true;
        }
        mtx.unlock();
        return success;
    }
    void EmitDiagnose(Diagnostic d);
    
    /**
     * Save all diagnostic to a structure. For deduplication or some tools may need read from it.
     */
    bool SaveDiagnostics(const Diagnostic& d);

    /**
     * The current strategy is that diagnostic will not be reported if preceding stage have some errors, but it will
     * have some improper circumstance if the error has no relationship with previous stage error.
     * Consider delete this strategy in the future.
     */
    bool CanBeEmitted(const DiagCategory& d);
    void HandleDiagnose(Diagnostic& d) override;

    bool IsJsonFormat() const noexcept
    {
        return jsonFormat;
    }
    void SetOutToStringStream()
    {
        outToStringStream = true;
    }
    void SetOutToErrStream()
    {
        outToStringStream = false;
    }
    std::string GetOutString()
    {
        return strStream.str();
    }
    void CacheTheCountInJsonFormat();
    ~CompilerDiagnosticHandler() override = default;

private:
    bool noColor{false};
    bool jsonFormat{false};
    mutable std::map<DiagCategory, std::unordered_set<Diagnostic, hashFunc, equalFunc>> diagnostics;
    std::list<std::string> diagsJsonBuff;
    std::string diagNumJsonBuff;
    bool outToStringStream{false};
    std::ostringstream strStream;
    /**
     * Get all diagnostics of the diag category, and they are sorted by range(Sorted by begin position in ascending
     * order. If the begins are the same, sorted by end position in ascending order.)
     */
    std::vector<Diagnostic> GetCategoryDiagnosticsSortedByRange(DiagCategory cate) const;
};

/**
 * DiagnosticBuilder is a helper class which can add extra information (like highlight or fix) after Diagnose() and
 * invoke DiagnosticEngine to notify diagnostic when it deconstructs.
 */
class DiagnosticBuilder {
public:
    DiagnosticBuilder(DiagnosticEngine& diag, Diagnostic diagnostic);
    Diagnostic diagnostic;
    DiagnosticEngine& diag;
    DiagnosticBuilder(const DiagnosticBuilder& p) = delete;
    DiagnosticBuilder& operator=(const DiagnosticBuilder& p) = delete;
    template <typename... Args> void AddNote(const Position& pos, DiagKind kind, Args... args)
    {
        auto end = pos == DEFAULT_POSITION ? pos : pos + 1;
        Diagnostic myDiag(pos, end, kind, args...);
        diagnostic.notes.push_back(myDiag);
    }

    template <typename... Args> void AddNote(const AST::Node& node, const Position& pos, DiagKind kind, Args... args)
    {
        auto begin = node.GetMacroCallPos(pos);
        auto end = begin == DEFAULT_POSITION ? begin : begin + 1;
        Diagnostic myDiag(begin, end, kind, args...);
        diagnostic.notes.push_back(myDiag);
    }

    template <typename... Args> void AddNote(const AST::Node& node, DiagKind kind, Args... args)
    {
        AddNote(node.GetBegin(), kind, args...);
    }

    template <typename... Args> void AddHint(const Position& pos, Args... args)
    {
        static_assert(IsAllString<Args...>, "args of AddHint in diagnostic builder should all be string.");
        std::vector<std::string> arguments{args...};
        auto finalPos = pos;
        if (diagnostic.curMacroCall) {
            finalPos = diagnostic.curMacroCall->GetMacroCallPos(pos, true);
        }
        Range range = MakeRange(finalPos, finalPos + 1);
        AddHint(range, arguments);
    }

    template <typename... Args> void AddHint(const Range& range, Args... args)
    {
        static_assert(IsAllString<Args...>, "args of AddHint in diagnostic builder should all be string.");
        std::vector<std::string> arguments{args...};
        if (diagnostic.curMacroCall) {
            return AddHint(MakeRange(diagnostic.curMacroCall->GetMacroCallPos(range.begin),
                                     diagnostic.curMacroCall->GetMacroCallPos(range.end, true)),
                arguments);
        }
        AddHint(range, arguments);
    }

    template <typename... Args> void AddHint(const Token& tok, Args... args)
    {
        static_assert(IsAllString<Args...>, "args of AddHint in diagnostic builder should all be string.");
        std::vector<std::string> arguments{args...};
        Range range = MakeRange(tok.Begin(), tok.End());
        AddHint(range, arguments);
    }

    template <typename... Args> void AddHint(const AST::Node& node, Args... args)
    {
        static_assert(IsAllString<Args...>, "args of AddHint in diagnostic builder should all be string.");
        std::vector<std::string> arguments{args...};
        AddHint(MakeRange(node.GetBegin(), node.GetEnd()), arguments);
    }
    
    /**
     * AddHint will insert the mark and hint message both into error. Like:
     * '''
     *      ...
     *      `source code`
     *              ~~~~ `hint message`
     *      ...
     * '''
     * */
    void AddHint(const Range& range, std::vector<std::string>& arguments);

    template <typename... Args> void AddMainHintArguments(Args... args)
    {
        static_assert(IsAllString<Args...>, "args of AddHint in diagnostic builder should all be string.");
        std::vector<std::string> arguments{args...};
        auto insertedStr = Diagnostic::InsertArguments(diagnostic.mainHint.str, arguments);
        diagnostic.mainHint.str = insertedStr;
    }

    void AddNote(const SubDiagnostic& sub);

    void AddNote(const Range& range, const std::string& note);

    void AddNote(const AST::Node& node, const std::string& note);

    void AddNote(const AST::Node& node, const Range& range, const std::string& note);

    void AddNote(const std::string& note);

    void AddNote(const Position& pos, const std::string& note);

    void AddHelp(const DiagHelp& help);

    ~DiagnosticBuilder();
};

/**
 * DiagnosticCache is a helper class that caches the stored diags in DiagnosticEngine and restores them later.
 */
class DiagnosticCache {
public:
    using DiagCacheKey = int32_t;
    DiagnosticCache() {}
    /* Remember the diags already in the diags before type check and exclude them later */
    void ToExclude(const DiagnosticEngine& diagBefore);
    void BackUp(const DiagnosticEngine& diagAfter);
    void Restore(DiagnosticEngine& dst);
    static DiagCacheKey ExtractKey(const DiagnosticEngine& diag);
    bool NoError() const;
    std::vector<Diagnostic> cachedDiags;
};
enum class DiagEngineErrorCode : uint8_t { NO_ERRORS, DIAG_RANGE_ERROR, UNKNOWN };
/**
 * DiagnosticEngine is main diagnostic processing center. It is responsible for handle diagnostics and emit
 * diagnostic information.
 * regular variable name: diag
 */
class DiagnosticEngine {
    friend class DiagSuppressor;
    friend class DiagnosticBuilder;
    friend class DiagnosticCache;
public:
    bool ignoreScopeCheck{false}; /**< If true, scope related error would be ignored. */
    // For each compilation, we only print errors of the first stage that produced errors.
    // The following variable stores which stage it is, or nullopt if no error is produced.
    DiagnosticEngine(const DiagnosticEngine& p) = delete;
    DiagnosticEngine& operator=(const DiagnosticEngine& p) = delete;
    DiagnosticEngine();
    ~DiagnosticEngine() noexcept;

    ///@{
    /// The two api below are used by CJLint and lsp.
    void SetIsEmitter(bool emitter);
    void SetDisableWarning(bool dis);
    ///@}
    ///@{
    /// Getters & setters.
    bool HasSourceManager();
    bool GetIsEmitter() const;
    void SetIsDumpErrCnt(bool dump);
    bool GetIsDumpErrCnt() const;
    void SetSourceManager(SourceManager* sm);
    SourceManager& GetSourceManager() noexcept;
    std::lock_guard<std::mutex> LockFirstErrorCategory();
    const std::optional<DiagCategory>& FirstErrorCategory() const;
    int32_t GetDisableDiagDeep() const;
    const std::vector<Diagnostic>& GetStoredDiags() const;
    void SetStoredDiags(std::vector<Diagnostic>&& value);
    bool GetEnableDiagnose() const;
    void EnableDiagnose(const std::vector<Diagnostic>& diags);
    std::vector<Diagnostic> ConsumeStoredDiags();

    bool DiagFilter(Diagnostic& diagnostic) noexcept;
    
    void AddMacroCallNote(Diagnostic& diagnostic, const AST::Node& node, const Position& pos);

    // ability of transaction
    void Prepare();
    void Commit();
    void ClearTransaction();

    // use DiagEngineErrorCode rather than internal error message (for libast)
    void EnableCheckRangeErrorCodeRatherICE();
    void DisableCheckRangeErrorCodeRatherICE();
    bool IsCheckRangeErrorCodeRatherICE() const;
    void SetDiagEngineErrorCode(DiagEngineErrorCode errorCode);

    ///@{
    /**
     *  Diagnose API. Note that new code should use DiagnoseRefactor for more user-friendly message.
     * @param start The Diagnostic start position.
     * @param end The Diagnostic end position.
     * @param kind The Diagnostic kind.
     * @param args The Diagnostic format arguments.
     * @return DiagnosticBuilder A temporary local instance which contains Diagnostic.
     */
    template <typename... Args>
        DiagnosticBuilder Diagnose(const Position start, const Position end, DiagKind kind, Args... args)
    {
        if (HardDisable()) {
            return DiagnosticBuilder(*this, Diagnostic{});
        }
        Diagnostic diagnostic(start, end, kind, args...);
        return DiagnosticBuilder(*this, diagnostic);
    }

    /**
     *  Diagnose API.
     * @param pos The Diagnostic start position.
     * @param kind The Diagnostic kind.
     * @param args The Diagnostic format arguments.
     * @return DiagnosticBuilder A temporary local instance which contains Diagnostic.
     */
    template <typename... Args> DiagnosticBuilder Diagnose(const Position pos, DiagKind kind, Args... args)
    {
        return Diagnose(pos, pos + 1, kind, args...);
    }

    template <typename... Args>
    DiagnosticBuilder Diagnose(const AST::Node& node, const Position pos, DiagKind kind, Args... args)
    {
        if (node.isInMacroCall) {
            auto diagnostic = Diagnostic{};
            diagnostic.isInMacroCall = true;
            return DiagnosticBuilder(*this, diagnostic);
        }
        if (node.TestAttr(AST::Attribute::MACRO_EXPANDED_NODE) && node.curMacroCall) {
            auto begin = node.GetMacroCallPos(pos);
            Diagnostic diagnostic(begin, begin + 1, kind, args...);
            AddMacroCallNote(diagnostic, node, pos);
            return DiagnosticBuilder(*this, diagnostic);
        }
        return Diagnose(pos, kind, args...);
    }

    template <typename... Args> DiagnosticBuilder Diagnose(const AST::Node& node, DiagKind kind, Args... args)
    {
        if (node.isInMacroCall) {
            auto diagnostic = Diagnostic{};
            diagnostic.isInMacroCall = true;
            return DiagnosticBuilder(*this, diagnostic);
        }
        // Refactor the Diagnose of the node after the macro expansion.
        if (node.TestAttr(AST::Attribute::MACRO_EXPANDED_NODE) && node.curMacroCall) {
            Diagnostic diagnostic(node.GetBegin(), node.GetEnd(), kind, args...);
            AddMacroCallNote(diagnostic, node, node.begin);
            return DiagnosticBuilder(*this, diagnostic);
        }
        return Diagnose(node.GetBegin(), kind, args...);
    }

    DiagnosticBuilder Diagnose(const Diagnostic& diagnostic)
    {
        return {*this, diagnostic};
    }

    template <typename... Args> DiagnosticBuilder Diagnose(DiagKind kind, Args... args)
    {
        if (HardDisable()) {
            return DiagnosticBuilder(*this, Diagnostic{});
        }
        Diagnostic diagnostic(DEFAULT_POSITION, DEFAULT_POSITION, kind, args...);
        return DiagnosticBuilder(*this, diagnostic);
    }
    ///@}

    ///@{
    /// DiagnoseRefactor issues a diagnose with more user-friendly message than \ref Diagnose. New code should
    /// always use DiagnoseRefactor.
    template <typename... Args>
    DiagnosticBuilder DiagnoseRefactor(DiagKindRefactor kind, const Position pos, Args... args)
    {
        static_assert(IsAllString<Args...>, "the diagnose only support string type argument");
        // The span of 'range' is left off and right on, like: [begin, end).
        Range range = MakeRange(pos, pos + 1);
        Diagnostic diagnostic(true, range, kind, args...);
        return DiagnosticBuilder(*this, diagnostic);
    }

    template <typename... Args>
    DiagnosticBuilder DiagnoseRefactor(DiagKindRefactor kind, const Range range, Args... args)
    {
        static_assert(IsAllString<Args...>, "the diagnose only support string type argument");

        CheckRange(Diagnostic::GetDiagnoseCategory(kind), range);
        Diagnostic diagnostic(true, range, kind, args...);
        return DiagnosticBuilder(*this, diagnostic);
    }

    template <typename... Args>
    DiagnosticBuilder DiagnoseRefactor(DiagKindRefactor kind, const Token& token, Args... args)
    {
        static_assert(IsAllString<Args...>, "the diagnose only support string type argument");
        auto range = MakeRange(token.Begin(), token.End());
        Diagnostic diagnostic(true, range, kind, args...);
        return DiagnosticBuilder(*this, diagnostic);
    }

    template <typename... Args>
    DiagnosticBuilder DiagnoseRefactor(DiagKindRefactor kind, const AST::Node& node, Args... args)
    {
        static_assert(IsAllString<Args...>, "the diagnose only support string type argument");
        auto range = MakeRange(node.GetBegin(), node.GetEnd());
        Diagnostic diagnostic(true, range, kind, args...);
        diagnostic.isInMacroCall = node.isInMacroCall;
        AddMacroCallNote(diagnostic, node, node.begin);
        return DiagnosticBuilder(*this, diagnostic);
    }

    template <typename... Args>
    DiagnosticBuilder DiagnoseRefactor(DiagKindRefactor kind, const AST::Node& node, const Position pos, Args... args)
    {
        static_assert(IsAllString<Args...>, "the diagnose only support string type argument");
        // The span of 'range' is left off and right on, like: [begin, end).
        auto begin = node.GetMacroCallPos(pos, true);
        Range range = MakeRange(begin, begin + 1);
        Diagnostic diagnostic(true, range, kind, args...);
        diagnostic.isInMacroCall = node.isInMacroCall;
        AddMacroCallNote(diagnostic, node, pos);
        return DiagnosticBuilder(*this, diagnostic);
    }

    template <typename... Args>
    DiagnosticBuilder DiagnoseRefactor(DiagKindRefactor kind, const AST::Node& node, const Range range, Args... args)
    {
        static_assert(IsAllString<Args...>, "the diagnose only support string type argument");
        CheckRange(Diagnostic::GetDiagnoseCategory(kind), range);
        auto newRange = MakeRealRange(node, range.begin, range.end);
        Diagnostic diagnostic(true, newRange, kind, args...);
        diagnostic.isInMacroCall = node.isInMacroCall;
        AddMacroCallNote(diagnostic, node, range.begin);
        return DiagnosticBuilder(*this, diagnostic);
    }

    template <typename... Args>
    DiagnosticBuilder DiagnoseRefactor(DiagKindRefactor kind, const AST::Node& node, const Token& token, Args... args)
    {
        static_assert(IsAllString<Args...>, "the diagnose only support string type argument");
        auto range = MakeRealRange(node, token.Begin(), token.End(), token.kind == TokenKind::END);
        Diagnostic diagnostic(true, range, kind, args...);
        diagnostic.isInMacroCall = node.isInMacroCall;
        AddMacroCallNote(diagnostic, node, token.Begin());
        diagnostic.curMacroCall = node.curMacroCall;
        return DiagnosticBuilder(*this, diagnostic);
    }
    ///@}

    /**
     * Convert unformat diagnostic message to real diagnostic message.
     */
    void ConvertArgsToDiagMessage(Diagnostic& diagnostic) noexcept;
    /**
     * Register diagnostic observer to diagnostic engine.
     */
    void RegisterHandler(std::unique_ptr<DiagnosticHandler>&& h);

    void RegisterHandler(DiagFormat format);

    void IncreaseErrorCount(DiagCategory category);
    
    void IncreaseWarningCount(DiagCategory category);

    void IncreaseErrorCount();

    uint64_t GetWarningCount();
    
    uint64_t GetErrorCount();

    void IncreaseWarningPrintCount();
    unsigned int GetWarningPrintCount() const;
    void IncreaseErrorPrintCount();
    unsigned int GetErrorPrintCount() const;
    std::optional<unsigned int> GetMaxNumOfDiags() const;
    bool IsSupressedUnusedMain(const Diagnostic& diagnostic) noexcept;
    void HandleDiagnostic(Diagnostic& diagnostic) noexcept;
    
    void EmitCategoryDiagnostics(DiagCategory cate);

    DiagEngineErrorCode GetCategoryDiagnosticsString(DiagCategory cate, std::string& diagOut);
    void EmitCategoryGroup();
    
    void SetErrorCountLimit(std::optional<unsigned int> errorCountLimit);
    
    std::vector<Diagnostic> GetCategoryDiagnostic(DiagCategory cate);
    
    void ClearError();
    
    void Reset();
    
    /**
     * Set the status of diagnostic engine.
     * @param enable
     */
    void SetDiagnoseStatus(bool enable);

    /**
     * Get the status of diagnostic engine.
     */
    bool GetDiagnoseStatus() const;

    /**
     * Report the number of errors and warnings.
     */
    void ReportErrorAndWarningCount();

    void DisableScopeCheck()
    {
        ignoreScopeCheck = true;
    }
    class StashDisableDiagnoseStatus {
    public:
        StashDisableDiagnoseStatus(DiagnosticEngine* e, bool hasTargetType);
        ~StashDisableDiagnoseStatus() noexcept;

    private:
        DiagnosticEngine* engine;
        bool enableDiagnose{true};
        int32_t disableDiagDeep = 0;
        std::vector<Diagnostic> storedDiags;
        bool hasTargetType{true};
    };

    std::unique_ptr<StashDisableDiagnoseStatus> AutoStashDisableDiagnoseStatus(bool hasTargetType = false)
    {
        return std::make_unique<StashDisableDiagnoseStatus>(this, hasTargetType);
    }

private:
    class DiagnosticEngineImpl* impl;
    bool HardDisable() const;
    void CheckRange(DiagCategory cate, const Range& range);
    Range MakeRealRange(
        const AST::Node& node, const Position begin, const Position end, bool begLowBound = false) const;
    std::vector<Diagnostic> DisableDiagnose();
    void EnableDiagnose();
};
} // namespace Cangjie

#endif // CANGJIE_BASIC_DIAGNOSTICENGINE_H
