// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the Option related classes.
 */

#include "cangjie/Option/Option.h"

#include <string>
#include <unordered_map>

#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Utils/Utils.h"
#include "cangjie/Utils/Unicode.h"

#define OPTION_TRUE_ACTION(EXPR)                                                                                       \
    [](GlobalOptions& opts, OptionArgInstance&) {                                                                      \
        (EXPR);                                                                                                        \
        return true;                                                                                                   \
    }

using namespace Cangjie;
using namespace Cangjie::Triple;
using namespace Cangjie::Utils;
using namespace Cangjie::FileUtil;

namespace {
constexpr size_t ELEMENT_NUM_OF_KV_PAIR = 2;

const std::unordered_map<std::string, uint8_t> BACKEND_STR_MAP = {
    {"llvm", uint8_t(BackendType::CJNATIVE)},
};

const std::unordered_map<std::string, uint8_t> LTO_MODE_MAP = {
    {"full", uint8_t(GlobalOptions::LTOMode::FULL_LTO)},
    {"thin", uint8_t(GlobalOptions::LTOMode::THIN_LTO)},
};

const std::unordered_map<std::string, uint8_t> WARN_GROUP_MAP = {
#define WARN_GROUP(DESCR, KIND) {DESCR, uint8_t(WarnGroup::KIND)},
#include "cangjie/Basic/DiagRefactor/DiagnosticWarnGroupKind.def"
#undef WARN_GROUP
};

const std::unordered_map<std::string, uint8_t> DIAG_FORMAT_MAP = {
    {"json", uint8_t(DiagFormat::JSON)},
    {"noColor", uint8_t(DiagFormat::NO_COLOR)},
    {"default", uint8_t(DiagFormat::DEFAULT)},
};

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
const std::unordered_map<std::string, uint8_t> OUTPUT_MODE_MAP = {
    {"exe", uint8_t(GlobalOptions::OutputMode::EXECUTABLE)},
    {"staticlib", uint8_t(GlobalOptions::OutputMode::STATIC_LIB)},
    {"dylib", uint8_t(GlobalOptions::OutputMode::SHARED_LIB)},
};
#endif

const std::unordered_map<std::string, ArchType> STRING_ARCH_MAP = {
    {"x86_64", ArchType::X86_64}, {"aarch64", ArchType::AARCH64}, {"unknown", ArchType::UNKNOWN}
};

const std::unordered_map<std::string, OSType> STRING_OS_MAP = {
    {"windows", OSType::WINDOWS}, {"w64", OSType::WINDOWS}, {"linux", OSType::LINUX},
    {"darwin", OSType::DARWIN}, {"unknown", OSType::UNKNOWN}
};

const std::unordered_map<std::string, Environment> STRING_ENVIRONMENT_MAP = {
    {"ohos", Environment::OHOS},
    {"gnu", Environment::GNU},
    {"mingw32", Environment::GNU},
    {"", Environment::NOT_AVAILABLE},
};

const std::unordered_map<Environment, std::string> ENVIRONMENT_STRING_MAP = {
    {Environment::OHOS, "ohos"},
    {Environment::GNU, "gnu"},
    {Environment::GNU, "mingw32"},
    {Environment::NOT_AVAILABLE, ""},
};

const std::unordered_map<std::string, uint8_t> DUMP_CHIR_MODE_MAP = {
    {"na", uint8_t(GlobalOptions::CHIRMode::NA)},
    {"standard", uint8_t(GlobalOptions::CHIRMode::STANDARD)},
    {"withID", uint8_t(GlobalOptions::CHIRMode::WITH_ID)},
    {"all", uint8_t(GlobalOptions::CHIRMode::ALL)},
};

const std::unordered_map<std::string, GlobalOptions::CandidateEmitCHIRPhase> EMIT_CHIR_PHASE_MAP = {
    {"raw", GlobalOptions::CandidateEmitCHIRPhase::RAW},
    {"opt", GlobalOptions::CandidateEmitCHIRPhase::OPT}};

const std::unordered_map<std::string, uint8_t> PRINT_BCHIR_MODE_MAP = {
    {"deserialized", uint8_t(GlobalOptions::PrintBCHIROption::DESERIALIED)},
    {"linked", uint8_t(GlobalOptions::PrintBCHIROption::LINKED)},
    {"chir2bchir", uint8_t(GlobalOptions::PrintBCHIROption::CHIR2BCHIR)},
    {"interpreter", uint8_t(GlobalOptions::PrintBCHIROption::INTERPRETER)},
    {"ce-linked", uint8_t(GlobalOptions::PrintBCHIROption::CE_LINKED)},
    {"ce-chir2bchir", uint8_t(GlobalOptions::PrintBCHIROption::CE_CHIR2BCHIR)},
    {"all", uint8_t(GlobalOptions::PrintBCHIROption::ALL)},
};

bool SeeingTomlComment(const std::string inputLine)
{
    if (inputLine.empty()) {
        return false;
    }
    if (inputLine.find('#') == std::string::npos) {
        return false;
    }
    for (auto c : inputLine) {
        if (c == ' ' || c == '\t') {
            continue;
        }
        if (c != '#') {
            return false;
        } else {
            return true;
        }
    }
    return true;
}

std::optional<GlobalOptions::OptimizationLevel> ParseOptimizationValue(const std::string& value)
{
    int intValue = Utils::TryParseInt(value).value_or(-1);
    if (intValue < 0) {
        Errorf("Optimization level %s is invalid\n", value.c_str());
        return std::nullopt;
    }
    if (intValue == 0) { // 0 stands for O0
        return {GlobalOptions::OptimizationLevel::O0};
    } else if (intValue == 1) { // 1 stands for O1
        return {GlobalOptions::OptimizationLevel::O1};
    } else if (intValue == 2) { // 2 stands for O2
        return {GlobalOptions::OptimizationLevel::O2};
    } else { // Any value greater than 2 stands for O3
#ifdef CANGJIE_VISIBLE_OPTIONS_ONLY
        Errorf("Optimization level %s is invalid\n", value.c_str());
        return std::nullopt;
#else
        return {GlobalOptions::OptimizationLevel::O3};
#endif
    }
}

bool ParseTargetTriple(GlobalOptions& opts, const std::string& triple)
{
    auto parts = Utils::SplitString(triple, "-");
    if (parts.size() != 3 && parts.size() != 4) { // A well-formed triple should have 3 or 4 fields
        Errorln("The target must be in either form of {ARCH}-{VENDOR}-{SYSTEM}-{ENV} or {ARCH}-{SYSTEM}-{ENV}.");
        return false;
    }
    auto archStr = parts[0];
    if (auto arch = STRING_ARCH_MAP.find(archStr); arch != STRING_ARCH_MAP.end()) {
        opts.target.arch = arch->second;
    } else {
        Errorln("The architecture \"" + archStr + "\" is not found or supported!");
        return false;
    }
    // Hack for <arch>-apple-darwin, which has no environment field. The current function cannot handle such cases.
    // Index 1 responds to vendor name and 2 responds to os name.
    if (parts[1] == "apple" && parts[2] == "darwin") {
        opts.target.vendor = Vendor::APPLE;
        opts.target.os = OSType::DARWIN;
        opts.target.env = Environment::NOT_AVAILABLE;
        return true;
    }
    auto osStr = parts[parts.size() - 2];
    if (auto os = STRING_OS_MAP.find(osStr); os != STRING_OS_MAP.end()) {
        opts.target.os = os->second;
    } else {
        Errorln("The OS \"" + osStr + "\" is not found or supported!");
        return false;
    }
    auto envStr = parts[parts.size() - 1];
    if (auto env = STRING_ENVIRONMENT_MAP.find(envStr); env != STRING_ENVIRONMENT_MAP.end()) {
        opts.target.env = env->second;
    } else {
        Errorln("The environment \"" + envStr + "\" is not found or supported!");
        return false;
    }
    return true;
};

const std::string LIMIT_ERROR_COUNT_ALL = "all";
bool ParseErrorCountLimit(GlobalOptions& opts, const OptionArgInstance& arg)
{
    if (arg.value == LIMIT_ERROR_COUNT_ALL) {
        opts.errorCountLimit = std::nullopt;
        return true;
    }
    int number = Utils::TryParseInt(arg.value).value_or(-1);
    if (number < 0) {
        Errorf("The value of %s is invalid.\n", arg.name.c_str());
        return false;
    }
    opts.errorCountLimit = static_cast<unsigned int>(number);
    return true;
}

std::optional<GlobalOptions::SanitizerCoverageOptions::Type> ParseSancovValue(const std::string& value)
{
    int intValue = Utils::TryParseInt(value).value_or(-1);
    if (intValue < 0) {
        Errorf("Sancov level %s is invalid\n", value.c_str());
        return std::nullopt;
    }
    if (intValue == 0) {
        return {GlobalOptions::SanitizerCoverageOptions::Type::SCK_NONE};
    } else if (intValue == 1) {
        return {GlobalOptions::SanitizerCoverageOptions::Type::SCK_FUNCTION};
    } else { // All values greater than 2 are considered level=2.
        return {GlobalOptions::SanitizerCoverageOptions::Type::SCK_BB};
    }
}

enum class ConditionalCompileConfigMode { PATH, KEY_VALUE };
ConditionalCompileConfigMode ParseConditionalCompileConfigMode(const std::string& cfgValue)
{
    auto countNum = [](const std::string str, const std::string sub) -> size_t {
        if (sub.length() == 0) {
            return 0;
        }
        size_t count = 0;
        for (size_t offset = str.find(sub); offset != std::string::npos;
             offset = str.find(sub, offset + sub.length())) {
            ++count;
        }
        return count;
    };
    size_t equalNum = countNum(cfgValue, "=");
    size_t escapeEqualNum = countNum(cfgValue, "\\=");
    return equalNum == escapeEqualNum ? ConditionalCompileConfigMode::PATH : ConditionalCompileConfigMode::KEY_VALUE;
}

void RemoveEscapedSymbol(std::string &input, const std::string& from, const std::string& to)
{
    size_t startPos = 0;
    while ((startPos = input.find(from, startPos)) != std::string::npos) {
        input.replace(startPos, from.length(), to);
        startPos += to.length();
    }
}

bool CheckPassWhenKey(const std::string key, DiagnosticEngine& diag)
{
    if (!IsIdentifier(key, true)) {
        diag.DiagnoseRefactor(DiagKindRefactor::dirver_cfg_invaild_identifier, DEFAULT_POSITION);
        return false;
    }
    return true;
}

const static std::unordered_set<std::string> BUILTIN_CONDITION = {
    "os", "backend", "arch", "debug", "cjc_version", "test"};

bool ParseAndCheckConditionVar(
    std::string& id, std::unordered_map<std::string, std::string>& res, DiagnosticEngine& diag)
{
    if (id.size() > std::string("``").size() && id[0] == '`') {
        id.substr(1, id.size() - std::string("``").size()); // parse raw id
    }
    Unicode::NFC(id);
    if (BUILTIN_CONDITION.find(id) != BUILTIN_CONDITION.end()) {
        diag.DiagnoseRefactor(DiagKindRefactor::dirver_cfg_same_with_builtin, DEFAULT_POSITION);
        return false;
    }
    if (res.find(id) != res.end()) {
        diag.DiagnoseRefactor(DiagKindRefactor::dirver_cfg_key_repeat, DEFAULT_POSITION);
        return false;
    }
    return true;
}

bool ParsePassedWhen(
    const std::string& input, std::unordered_map<std::string, std::string>& res, std::vector<std::string>& paths)
{
    DiagnosticEngine diag;
    auto cfgMode = ParseConditionalCompileConfigMode(input);
    if (cfgMode == ConditionalCompileConfigMode::PATH) {
        std::string cfgDir = input;
        RemoveEscapedSymbol(cfgDir, "\\=", "=");
        if (!IsDir(cfgDir)) {
            diag.DiagnoseRefactor(DiagKindRefactor::dirver_cfg_not_a_dir, DEFAULT_POSITION, cfgDir);
            return false;
        }
        paths.emplace_back(cfgDir);
        return true;
    }
    auto keyValuePairs = Utils::SplitString(input, ",");
    for (const auto& it : keyValuePairs) {
        auto kv = Utils::SplitString(it, "=");
        if (kv.size() != ELEMENT_NUM_OF_KV_PAIR) {
            diag.DiagnoseRefactor(DiagKindRefactor::driver_cfg_value_err, DEFAULT_POSITION);
            return false;
        }
        Utils::TrimStringVector(kv);
        std::string id = kv[0];
        if (id.empty() || kv[1].empty()) {
            diag.DiagnoseRefactor(DiagKindRefactor::driver_cfg_value_err, DEFAULT_POSITION);
            return false;
        }
        if (!CheckPassWhenKey(id, diag)) {
            return false;
        }
        if (!ParseAndCheckConditionVar(id, res, diag)) {
            return false;
        }
        res.emplace(id, kv[1]);
    }
    return true;
}

bool ParseCHIREA(GlobalOptions& opts, const OptionArgInstance& arg)
{
    // Get CHIR escape analysis flag.
    if (arg.value == "on") {
        opts.chirEA = true;
    } else {
        CJC_ASSERT(arg.value == "off");
        if (arg.value != "off") {
            return false;
        }
        opts.chirEA = false;
    }
    return true;
}

bool ParseCHIRWFC(GlobalOptions& opts, const OptionArgInstance& arg)
{
    // Get CHIR well-formness-check flag.
    if (arg.value == "on") {
        opts.chirWFC = true;
    } else {
        CJC_ASSERT(arg.value == "off");
        if (arg.value != "off") {
            return false;
        }
        opts.chirWFC = false;
    }
    return true;
}

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
std::optional<std::size_t> ParseJobsValue(const OptionArgInstance& arg)
{
    // check if the input of jobs contains illegal character.
    std::string numStr = arg.value;
    if (numStr.find_first_not_of("0123456789") != std::string::npos) {
        Errorf("'%s' only accepts a positive integer number as value.\n", arg.name.c_str());
        return std::nullopt;
    }

    // check if the input of jobs is an illegal big number.
    constexpr std::size_t maxLen = 9;
    if (numStr.length() > maxLen) {
        Errorf("'%s' gets an invalid number input.\n", arg.name.c_str());
        return std::nullopt;
    }

    auto inputJobs = std::stoul(arg.value);
    auto threadNum = std::thread::hardware_concurrency();
    // refactor the input of jobs in a valid range which is [1, threadNum].
    // 1. when the inputJobs > threadNum, refactor inputJobs to threadNum;
    // 2. when the inputJobs < 1, refactor inputJobs to 1.
    if (inputJobs < 1) {
        Warningf("'%s' value must be greater than 0, the compiler will use 1 as the value.\n", arg.name.c_str());
        inputJobs = 1;
    } else if (inputJobs > threadNum) {
        inputJobs = threadNum;
    }
    return inputJobs;
}

bool ParseJobs(GlobalOptions& opts, const OptionArgInstance& arg)
{
    // Not allowed to be empty.
    if (arg.value.empty()) {
        Errorf("'%s' requires a non-empty value.\n", arg.name.c_str());
        return false;
    }

    auto jobsValue = ParseJobsValue(arg);
    if (!jobsValue.has_value()) {
        return false;
    }
    opts.jobs = jobsValue;
    return true;
}

bool ParseAPCJobs(GlobalOptions& opts, const OptionArgInstance& arg)
{
    if (arg.value.empty()) {
        opts.aggressiveParallelCompileWithoutArg = true;
        return true;
    }

    auto jobsValue = ParseJobsValue(arg);
    if (!jobsValue.has_value()) {
        return false;
    }

    constexpr std::size_t allowance = 2;
    Utils::Semaphore::Get().SetCount(jobsValue.value() + allowance);
    opts.aggressiveParallelCompile = jobsValue;
    return true;
}
#endif

bool IsEscaped(const std::string& str, size_t index)
{
    unsigned int backslashes = 0;
    while (index < str.size() && str[index] == '\\') {
        backslashes++;
        index--;
    }
    // backslashes is odd means the previous symbol is escaped backslash
    return (backslashes & 0x1) == 0x1;
}

size_t FindTomlComment(const std::string& inputLine)
{
    // The pound sign marks the rest of the line as a comment, unless it is in a string.
    bool insideString = false;
    for (size_t i = 0; i < inputLine.length(); i++) {
        if (!insideString && inputLine[i] == '#') {
            return i;
        } else if (inputLine[i] == '"' && (i == 0 || !IsEscaped(inputLine, i - 1))) {
            insideString = !insideString;
        }
    }
    return std::string::npos;
}

// parse key value like: key = "value"
bool ParseConditionKeyValueLine(
    const std::string& inputLine, std::unordered_map<std::string, std::string>& res, DiagnosticEngine& diag)
{
    auto processedInput = inputLine;
    auto tomlCommentPos = FindTomlComment(inputLine);
    if (tomlCommentPos != std::string::npos) {
        processedInput = inputLine.substr(0, tomlCommentPos);
    }
    auto kv = Utils::SplitString(processedInput, "=");
    if (kv.size() != ELEMENT_NUM_OF_KV_PAIR) {
        diag.DiagnoseRefactor(DiagKindRefactor::driver_cfg_toml_content_err, DEFAULT_POSITION);
        return false;
    }
    Utils::TrimStringVector(kv);
    constexpr size_t quotesSizeOfValue = 2; // for ""
    std::string id = kv[0];
    if (id.empty() || kv[1].size() <= quotesSizeOfValue || kv[1][0] != '"' || kv[1].back() != '"') {
        diag.DiagnoseRefactor(DiagKindRefactor::driver_cfg_toml_content_err, DEFAULT_POSITION);
        return false;
    }
    if (!CheckPassWhenKey(id, diag)) {
        return false;
    }
    if (!ParseAndCheckConditionVar(id, res, diag)) {
        return false;
    }
    auto value = kv[1].substr(1, kv[1].size() - quotesSizeOfValue);
    RemoveEscapedSymbol(value, "\\\"", "\"");
    res.emplace(id, value); // rm ""
    return true;
}

// Pre-actions are actions processed at the first, before In-actions are processed.
// Some options, for example, `-h`, `-v`, should prevent parsing proceeds, therefore,
// we define actions for such options in a separated action map.
std::unordered_map<Options::ID, std::function<bool(GlobalOptions&, OptionArgInstance&, bool&)>> g_preactions = {
    {Options::ID::VERBOSE,
        [](GlobalOptions& opts, OptionArgInstance&, bool& skipParsing) {
            opts.enableVerbose = true;
            skipParsing = false;
            return true;
        }},
    {Options::ID::VERSION,
        [](GlobalOptions& opts, OptionArgInstance&, bool& skipParsing) {
            opts.printVersionOnly = true;
            skipParsing = true;
            return true;
        }},
    {Options::ID::HELP,
        [](GlobalOptions& opts, OptionArgInstance&, bool& skipParsing) {
            opts.showUsage = true;
            skipParsing = true;
            return true;
        }},
    {Options::ID::EXPERIMENTAL,
        [](GlobalOptions& opts, OptionArgInstance&, bool& skipParsing) {
            opts.experimentalMode = true;
            skipParsing = false;
            return true;
        }},
    {Options::ID::BACKEND_MODE,
        [](GlobalOptions& opts, const OptionArgInstance& arg, bool& skipParsing) {
            CJC_ASSERT(BACKEND_STR_MAP.count(arg.value) != 0);
            if (BACKEND_STR_MAP.count(arg.value) == 0) {
                return false;
            }
            opts.backend = BackendType(BACKEND_STR_MAP.at(arg.value));
            skipParsing = false;
            return true;
        }},
};

// g_actions are actions for general options. After Pre-actions are processed, if
// no special skipping logic is taken, In-actions are performed. Most options
// should be defined in g_actions.
std::unordered_map<Options::ID, std::function<bool(GlobalOptions&, OptionArgInstance&)>> g_actions = {
    // ---------- GENERAL COMPILE OPTIONS ----------
    { Options::ID::MODULE_NAME, []([[maybe_unused]] GlobalOptions& opts, const OptionArgInstance& arg) {
        DiagnosticEngine diag;
        diag.DiagnoseRefactor(DiagKindRefactor::driver_useless_option, DEFAULT_POSITION, arg.name);
        return true;
    }},
    { Options::ID::PACKAGE_COMPILE, OPTION_TRUE_ACTION(opts.compilePackage = true) },
    { Options::ID::NO_PRELUDE, OPTION_TRUE_ACTION(opts.implicitPrelude = false) },
    { Options::ID::INT_OVERFLOW_MODE, [](GlobalOptions& opts, const OptionArgInstance& arg) {
        CJC_ASSERT(ValidOverflowStrategy(arg.value));
        if (!ValidOverflowStrategy(arg.value)) { return false; }
        opts.overflowStrategy = StringToOverflowStrategy(arg.value);
        return true;
    }},
    { Options::ID::STATIC_STD, OPTION_TRUE_ACTION(opts.linkStaticStd = true) },
    { Options::ID::DY_STD, OPTION_TRUE_ACTION(opts.linkStaticStd = false) },
    { Options::ID::STATIC_LIBS, OPTION_TRUE_ACTION((void)opts) },
    { Options::ID::DY_LIBS, OPTION_TRUE_ACTION((void)opts) },
    { Options::ID::LTO,  [](GlobalOptions& opts, const OptionArgInstance& arg) {
        if (LTO_MODE_MAP.count(arg.value) == 0) {
            return false;
        }
        opts.ltoMod = GlobalOptions::LTOMode(LTO_MODE_MAP.at(arg.value));
        return true;
    }},
    { Options::ID::COMPILE_AS_EXE, OPTION_TRUE_ACTION(opts.enableCompileAsExe = true) },
    { Options::ID::TARGET, [](GlobalOptions& opts, const OptionArgInstance& arg) {
        return ParseTargetTriple(opts, arg.value);
    }},

    { Options::ID::PROFILE_COMPILE_TIME, OPTION_TRUE_ACTION(opts.enableTimer = true) },
    { Options::ID::PROFILE_COMPILE_MEMORY, OPTION_TRUE_ACTION(opts.enableMemoryCollect = true) },
    { Options::ID::IMPORT_PATH, [](GlobalOptions& opts, const OptionArgInstance& arg) {
        auto maybePath = opts.CheckDirectoryPath(arg.value);
        if (maybePath.has_value()) {
            opts.importPaths.emplace_back(maybePath.value());
        }
        return true;
    }},
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    { Options::ID::PLUGIN_PATH, [](GlobalOptions& opts, const OptionArgInstance& arg) {
         auto maybePath = opts.CheckInputFilePath(arg.value);
         auto suffix = GlobalOptions::GetSharedLibraryExtension(opts.host.os);
         if (maybePath.has_value()) {
             auto path = maybePath.value();
             if ('.' + FileUtil::GetFileExtension(path) == suffix) {
                 opts.pluginPaths.emplace_back(maybePath.value());
                 return true;
             }
             Errorf("'%s' requires a dynamic library path with '%s' suffix.\n", arg.name.c_str(), suffix.c_str());
             return false;
         }
         Errorf("'%s' only accepts an existing dynamic library path.\n", arg.name.c_str());
         return false;
     }},
#endif
    { Options::ID::INCRE_COMPILE, OPTION_TRUE_ACTION(opts.enIncrementalCompilation = true) },
    { Options::ID::INCRE_DEBUG, OPTION_TRUE_ACTION(opts.printIncrementalInfo = true) },
    { Options::ID::DUMP_DEPENDENT_PACKAGE, OPTION_TRUE_ACTION(opts.scanDepPkg = true) },
    { Options::ID::NO_SUB_PACKAGE, OPTION_TRUE_ACTION(opts.noSubPkg = true) },

    // ---------- GENERAL DEBUG CONTROL OPTIONS ----------
    { Options::ID::NO_STACKTRACE_INFO, OPTION_TRUE_ACTION(opts.displayLineInfo = false) },
    { Options::ID::COMPILE_DEBUG, OPTION_TRUE_ACTION(opts.enableCompileDebug = true) },
    { Options::ID::TRIMPATH, [](GlobalOptions& opts, const OptionArgInstance& arg) {
        auto tmpPath = arg.value;
        if (!tmpPath.empty()) {
            tmpPath = FileUtil::Normalize(tmpPath);
#ifdef _WIN32
            tmpPath = tmpPath[tmpPath.size() - 1] == '\\' ? tmpPath : tmpPath + "\\";
#else
                tmpPath = tmpPath[tmpPath.size() - 1] == '/' ? tmpPath : tmpPath + "/";
#endif
            opts.removedPathPrefix.emplace_back(tmpPath);
        }
        return true;
    }},

    // ---------- COVERAGE OPTIONS ----------
    { Options::ID::ENABLE_COVERAGE, OPTION_TRUE_ACTION(opts.enableCoverage = true) },

    // ---------- WARNING & ERROR CONTROL OPTIONS ----------'
    { Options::ID::WARN_OFF, [](GlobalOptions& /* opts */, const OptionArgInstance& arg) {
        if (arg.value == "all") {
            WarningOptionMgr::UpdateFlags(std::vector<bool>(static_cast<size_t>(WarnGroup::NONE), true));
        } else {
            CJC_ASSERT(WARN_GROUP_MAP.count(arg.value) != 0);
            if (WARN_GROUP_MAP.count(arg.value) == 0) { return false; }
            auto groupIdx = static_cast<size_t>(WarnGroup(WARN_GROUP_MAP.at(arg.value)));
            WarningOptionMgr::UpdateFlag(groupIdx, true);
        }
        return true;
    }},
    { Options::ID::WARN_ON, [](GlobalOptions& /* opts */, const OptionArgInstance& arg) {
        if (arg.value == "all") {
            WarningOptionMgr::UpdateFlags(std::vector<bool>(static_cast<size_t>(WarnGroup::NONE), false));
        } else {
            CJC_ASSERT(WARN_GROUP_MAP.count(arg.value) != 0);
            if (WARN_GROUP_MAP.count(arg.value) == 0) { return false; }
            auto groupIdx = static_cast<size_t>(WarnGroup(WARN_GROUP_MAP.at(arg.value)));
            WarningOptionMgr::UpdateFlag(groupIdx, false);
        }
        return true;
    }},

    // ---------- SANITIZER OPTIONS ----------
    { Options::ID::SANITIZE, [](GlobalOptions& opts, const OptionArgInstance& arg) {
        if (arg.value == "address") {
            opts.sanitizerType = Cangjie::GlobalOptions::SanitizerType::ADDRESS;
        } else if (arg.value == "thread") {
            opts.sanitizerType = Cangjie::GlobalOptions::SanitizerType::THREAD;
        } else if (arg.value == "hwaddress") {
            opts.sanitizerType = Cangjie::GlobalOptions::SanitizerType::HWADDRESS;
        } else {
            return false;
        }
        return true;
    }},
    // ---------- ERROR CONTROL OPTIONS ----------
    { Options::ID::ERROR_COUNT_LIMIT, ParseErrorCountLimit },
    { Options::ID::DIAG_FORMAT, [](GlobalOptions& opts, const OptionArgInstance& arg) {
        CJC_ASSERT(DIAG_FORMAT_MAP.count(arg.value) != 0);
        if (DIAG_FORMAT_MAP.count(arg.value) == 0) { return false; }
        opts.diagFormat = DiagFormat(DIAG_FORMAT_MAP.at(arg.value));
        return true;
    }},

    // ---------- TEST OPTIONS ----------
    { Options::ID::COMPILE_TEST, [](GlobalOptions& opts, [[maybe_unused]] OptionArgInstance& arg) {
        opts.parseTest = true;
        opts.enableCompileTest = true;
        return true;
    }},
    { Options::ID::COMPILE_TESTS_ONLY, [](GlobalOptions& opts, [[maybe_unused]] OptionArgInstance& arg) {
        opts.parseTest = true;
        opts.enableCompileTest = true;
        opts.compileTestsOnly = true;
        return true;
    }},
    { Options::ID::EXPORT_FOR_TESTS, [](GlobalOptions& opts, [[maybe_unused]] OptionArgInstance& arg) {
        opts.exportForTest = true;
        return true;
    }},
    // ---------- MOCKING OPTIONS ----------
    { Options::ID::MOCK, [](GlobalOptions& opts, const OptionArgInstance& arg) {
        if (arg.value == "on") {
            opts.mock = MockSupportKind::ON;
        } else if (arg.value == "runtime-error") {
            opts.mock = MockSupportKind::RUNTIME_ERROR;
        } else if (arg.value == "off") {
            opts.mock = MockSupportKind::OFF;
        }
        return true;
    }},
    // ---------- MACRO OPTIONS ----------
    { Options::ID::COMPILE_DEBUG_MACRO, OPTION_TRUE_ACTION(opts.enableMacroDebug = true) },
    { Options::ID::PARALLEL_MACRO_EXPANSION, OPTION_TRUE_ACTION(opts.enableParallelMacro = true) },

    // ---------- GENERAL OPTIMIZATION OPTIONS ----------
    { Options::ID::OPTIMIZATION_0, [](GlobalOptions& opts, [[maybe_unused]] OptionArgInstance& arg) {
        return opts.SetOptimizationLevel(GlobalOptions::OptimizationLevel::O0);
    }},
    { Options::ID::OPTIMIZATION_1, [](GlobalOptions& opts, [[maybe_unused]] OptionArgInstance& arg) {
        return opts.SetOptimizationLevel(GlobalOptions::OptimizationLevel::O1);
    }},
    { Options::ID::OPTIMIZATION_2, [](GlobalOptions& opts, [[maybe_unused]] OptionArgInstance& arg) {
        return opts.SetOptimizationLevel(GlobalOptions::OptimizationLevel::O2);
    }},
    { Options::ID::OPTIMIZATION_3, [](GlobalOptions& opts, [[maybe_unused]] OptionArgInstance& arg) {
        return opts.SetOptimizationLevel(GlobalOptions::OptimizationLevel::O3);
    }},
    { Options::ID::OPTIMIZATION_S, [](GlobalOptions& opts, [[maybe_unused]] OptionArgInstance& arg) {
        return opts.SetOptimizationLevel(GlobalOptions::OptimizationLevel::Os);
    }},
    { Options::ID::OPTIMIZATION_Z, [](GlobalOptions& opts, [[maybe_unused]] OptionArgInstance& arg) {
        return opts.SetOptimizationLevel(GlobalOptions::OptimizationLevel::Oz);
    }},
    { Options::ID::OPTIMIZATION_CUSTOM, [](GlobalOptions& opts, const OptionArgInstance& arg) {
        auto maybeOptimizationLevel = ParseOptimizationValue(arg.value);
        if (maybeOptimizationLevel.has_value()) {
            return opts.SetOptimizationLevel(maybeOptimizationLevel.value());
        }
        return false;
    }},
    { Options::ID::OPT_PASS_OPTIONS, [](GlobalOptions& opts, const OptionArgInstance& arg) {
        opts.optPassOptions = arg.value;
        return true;
    }},
    { Options::ID::DISABLE_BACKEND_OPT, OPTION_TRUE_ACTION(opts.disableBackendOpt = true) },
    { Options::ID::FAST_MATH_MODE, OPTION_TRUE_ACTION(opts.fastMathMode = true) },
    // ---------- GENERAL SANCOV OPTIONS ----------
    { Options::ID::SANCOV_LEVEL_0, [](GlobalOptions& opts, [[maybe_unused]] OptionArgInstance& arg) {
        opts.sancovOption.SetSancovLevel(GlobalOptions::SanitizerCoverageOptions::Type::SCK_NONE);
        return true;
    }},
    { Options::ID::SANCOV_LEVEL_1, [](GlobalOptions& opts, [[maybe_unused]] OptionArgInstance& arg) {
        opts.sancovOption.SetSancovLevel(GlobalOptions::SanitizerCoverageOptions::Type::SCK_FUNCTION);
        return true;
    }},
    { Options::ID::SANCOV_LEVEL_2, [](GlobalOptions& opts, [[maybe_unused]] OptionArgInstance& arg) {
        opts.sancovOption.SetSancovLevel(GlobalOptions::SanitizerCoverageOptions::Type::SCK_BB);
        return true;
    }},
    { Options::ID::SANCOV_LEVEL_CUSTOM, [](GlobalOptions& opts, const OptionArgInstance& arg) {
        auto maybeSancovLevel = ParseSancovValue(arg.value);
        if (maybeSancovLevel.has_value()) {
            opts.sancovOption.SetSancovLevel(maybeSancovLevel.value());
            return true;
        }
        return false;
    }},

    // ---------- COMPILE CONTROL OPTIONS ----------*/
    { Options::ID::OUTPUT_TYPE, [](GlobalOptions& opts, const OptionArgInstance& arg) {
        // `--output-type=hotreload` generates DSO as well, except that we need to do more (or less) work on it.
        // To prevent adding checking everywhere `SharedLib` is checked for newly added hotreload output type,
        // we use another variable, `enableHotReload`, to indicate whether hot reload feature is enabled.
        if (arg.value == "hotreload") {
            opts.outputMode = GlobalOptions::OutputMode::SHARED_LIB;
            opts.enableHotReload = true;
            opts.linkStaticStd = false; // waiting for hotreload's bugfix
        } else {
            CJC_ASSERT(OUTPUT_MODE_MAP.count(arg.value) != 0);
            if (OUTPUT_MODE_MAP.count(arg.value) == 0) { return false; }
            opts.outputMode = GlobalOptions::OutputMode(OUTPUT_MODE_MAP.at(arg.value));
            opts.enableHotReload = false;
        }
        opts.enableOutputType = true;
        return true;
    }},
     { Options::ID::COMPILE_MACRO, [](GlobalOptions& opts, [[maybe_unused]] const OptionArgInstance& arg) {
        opts.compileMacroPackage = true;
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
            opts.outputMode = GlobalOptions::OutputMode::SHARED_LIB;
#endif
        return true;
    }},
    // ---------- OUTPUT CONTROL OPTIONS ----------
    { Options::ID::OUTPUT_FILE, [](GlobalOptions& opts, const OptionArgInstance& arg) {
        // Not allowed to be empty.
        if (arg.value.empty()) {
            Errorf("'%s' requires a non-empty value.\n", arg.name.c_str());
            return false;
        }
        opts.output = arg.value;
        return true;
    }},
    { Options::ID::OUTPUT_DIR, [](GlobalOptions& opts, const OptionArgInstance& arg) {
        DiagnosticEngine diag;
        if (!IsDir(arg.value)) {
            (void)diag.DiagnoseRefactor(DiagKindRefactor::no_such_directory, DEFAULT_POSITION, arg.value);
            return false;
        }
        opts.outputDir = {arg.value};
        return true;
    }},
    { Options::ID::SAVE_TEMPS, [](GlobalOptions& opts, const OptionArgInstance& arg) {
        if (!IsDir(arg.value)) {
            DiagnosticEngine diag;
            (void)diag.DiagnoseRefactor(DiagKindRefactor::no_such_directory, DEFAULT_POSITION, arg.value);
            return false;
        }
        opts.saveTemps = true;
        opts.tempFolderPath = arg.value;
        return true;
    }},

    // ---------- CONDITIONAL COMPILE OPTIONS ----------
    { Options::ID::CONDITIONAL_COMPILATION_CONFIG, [](GlobalOptions& opts, const OptionArgInstance& arg) {
        if (arg.value.empty()) {
            DiagnosticEngine diag;
            (void)diag.DiagnoseRefactor(DiagKindRefactor::driver_cfg_value_err, DEFAULT_POSITION);
            return false;
        }
        return ParsePassedWhen(arg.value, opts.passedWhenKeyValue, opts.passedWhenCfgPaths);
    }},

    // ---------- CHIR GENERAL OPTIONS ----------
    { Options::ID::CHIR_EA, ParseCHIREA },
    { Options::ID::CHIR_WFC, ParseCHIRWFC },
    { Options::ID::ENABLE_CHIR_REDUNDANT_GETORTHROW_ELIMINATION,
        [](GlobalOptions& opts, [[maybe_unused]] const OptionArgInstance& arg) {
            opts.selectedCHIROpts.insert(GlobalOptions::OptimizationFlag::REDUNDANT_LOAD);
            opts.enableChirRGetOrThrowE = true;
	    return true;
    }},
    { Options::ID::DISABLE_CHIR_USELESS_IMPORT_ELIMINATION, [](
        GlobalOptions& opts, [[maybe_unused]] const OptionArgInstance& arg) {
        opts.disableChirUselessImportElimination = true;
        return true;
    }},
    { Options::ID::DISABLE_SEMA_VIC, OPTION_TRUE_ACTION(opts.disableSemaVic = true)},
    { Options::ID::DISABLE_INSTANTIATION, [](GlobalOptions& opts, [[maybe_unused]] OptionArgInstance& arg) {
        opts.disableInstantiation = true;
        return true;
    }},
    { Options::ID::DEBUG_CODEGEN, OPTION_TRUE_ACTION(opts.codegenDebugMode = true) },
    { Options::ID::CHIR_OPT_DEBUG, OPTION_TRUE_ACTION(opts.chirDebugOptimizer = true) },
    { Options::ID::DUMP_CHIR_DEBUG, [](GlobalOptions& opts, [[maybe_unused]] const OptionArgInstance& arg) {
        opts.chirDumpDebugMode = true;
        return true;
    }},
    { Options::ID::EMIT_CHIR, [](GlobalOptions& opts, [[maybe_unused]] const OptionArgInstance& arg) {
        if (arg.value.empty()) {
            opts.emitCHIRPhase = GlobalOptions::CandidateEmitCHIRPhase::OPT;
            return true;
        }
        if (EMIT_CHIR_PHASE_MAP.count(arg.value) == 0) { return false; }
        opts.emitCHIRPhase = EMIT_CHIR_PHASE_MAP.at(arg.value);
        return true;
    }},
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    { Options::ID::DUMP_ANNOTATIONS_DEBUG, [](GlobalOptions& opts, [[maybe_unused]] const OptionArgInstance& arg) {
        opts.computeAnnotationsDebug = true;
        return true;
    }},
#endif
    { Options::ID::RENDER_CHIR, [](GlobalOptions& opts, const OptionArgInstance& arg) {
        CJC_ASSERT(DUMP_CHIR_MODE_MAP.count(arg.value) != 0);
        if (DUMP_CHIR_MODE_MAP.count(arg.value) == 0) { return false; }
        opts.chirRenderMode = GlobalOptions::CHIRMode(DUMP_CHIR_MODE_MAP.at(arg.value));
        return true;
    }},
    // ------------CHIR SANCOV OPTIONS----------------
    { Options::ID::SANCOV_INLINE_8BIT, OPTION_TRUE_ACTION(opts.sancovOption.inline8bitCounters = true) },
    { Options::ID::SANCOV_INLINE_BOOL_FLAG, OPTION_TRUE_ACTION(opts.sancovOption.inlineBoolFlag = true) },
    { Options::ID::SANCOV_TRACE_PC_GUARD, OPTION_TRUE_ACTION(opts.sancovOption.tracePCGuard = true) },
    { Options::ID::SANCOV_PC_TABLE, OPTION_TRUE_ACTION(opts.sancovOption.pcTable = true) },
    { Options::ID::SANCOV_STACK_DEPTH, OPTION_TRUE_ACTION(opts.sancovOption.stackDepth = true) },
    { Options::ID::SANCOV_TRACE_COMPARES, OPTION_TRUE_ACTION(opts.sancovOption.traceCmp = true) },
    { Options::ID::SANCOV_TRACE_MEM_COMPARES, OPTION_TRUE_ACTION(opts.sancovOption.traceMemCmp = true) },
    // ---------- CHIR OPTIMIZATION OPTIONS ----------
    { Options::ID::DISABLE_CHIR_OPT, OPTION_TRUE_ACTION(opts.disableChirOpt = true) },
    { Options::ID::CONST_PROPAGATION,
        OPTION_TRUE_ACTION(opts.selectedCHIROpts.insert(GlobalOptions::OptimizationFlag::CONST_PROPAGATION)) },
    { Options::ID::NO_CONST_PROPAGATION,
        OPTION_TRUE_ACTION(opts.selectedCHIROpts.erase(GlobalOptions::OptimizationFlag::CONST_PROPAGATION)) },
    { Options::ID::FUNC_INLINING,
        OPTION_TRUE_ACTION(opts.selectedCHIROpts.insert(GlobalOptions::OptimizationFlag::FUNC_INLINING)) },
    { Options::ID::NO_FUNC_INLINING,
        OPTION_TRUE_ACTION(opts.selectedCHIROpts.erase(GlobalOptions::OptimizationFlag::FUNC_INLINING)) },
    { Options::ID::DEVIRTUALIZATION,
        OPTION_TRUE_ACTION(opts.selectedCHIROpts.insert(GlobalOptions::OptimizationFlag::DEVIRTUALIZATION)) },
    { Options::ID::NO_DEVIRTUALIZATION,
        OPTION_TRUE_ACTION(opts.selectedCHIROpts.erase(GlobalOptions::OptimizationFlag::DEVIRTUALIZATION)) },
    { Options::ID::SROA_OPT,
        OPTION_TRUE_ACTION(opts.selectedCHIROpts.insert(GlobalOptions::OptimizationFlag::SROA_OPT)) },
    { Options::ID::NO_SROA_OPT,
        OPTION_TRUE_ACTION(opts.selectedCHIROpts.erase(GlobalOptions::OptimizationFlag::SROA_OPT)) },
    { Options::ID::REDUANDANT_RETURN_REMOVAL,
        OPTION_TRUE_ACTION(opts.selectedCHIROpts.insert(GlobalOptions::OptimizationFlag::REDUNDANT_RETURNS)) },
    { Options::ID::NO_REDUANDANT_RETURN_REMOVAL,
        OPTION_TRUE_ACTION(opts.selectedCHIROpts.erase(GlobalOptions::OptimizationFlag::REDUNDANT_RETURNS)) },
    { Options::ID::REDUNDANT_FUTURE_REMOVAL,
        OPTION_TRUE_ACTION(opts.selectedCHIROpts.insert(GlobalOptions::OptimizationFlag::REDUNDANT_FUTURE)) },
    { Options::ID::NO_REDUNDANT_FUTURE_REMOVAL,
        OPTION_TRUE_ACTION(opts.selectedCHIROpts.erase(GlobalOptions::OptimizationFlag::REDUNDANT_FUTURE)) },
    { Options::ID::REDUANDANT_ASSIGN_REMOVAL,
        OPTION_TRUE_ACTION(opts.selectedCHIROpts.insert(GlobalOptions::OptimizationFlag::REMOVE_REDUNDANT_ASG)) },
    { Options::ID::NO_REDUANDANT_ASSIGN_REMOVAL,
        OPTION_TRUE_ACTION(opts.selectedCHIROpts.erase(GlobalOptions::OptimizationFlag::REMOVE_REDUNDANT_ASG)) },
    { Options::ID::LETC_FOLDING,
        OPTION_TRUE_ACTION(opts.selectedCHIROpts.insert(GlobalOptions::OptimizationFlag::LETC_FOLDING)) },
    { Options::ID::SWITCH_OPT, [](GlobalOptions& opts, [[maybe_unused]] const OptionArgInstance& arg) {
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
        opts.selectedCHIROpts.insert(GlobalOptions::OptimizationFlag::SWITCH_OPT);
#endif
         return true;
     }},
    { Options::ID::REF_FOLDING,
        OPTION_TRUE_ACTION(opts.selectedCHIROpts.insert(GlobalOptions::OptimizationFlag::REF_FOLDING)) },
    { Options::ID::ARRAY_LAMBDA_OPT,
        OPTION_TRUE_ACTION(opts.selectedCHIROpts.insert(GlobalOptions::OptimizationFlag::ARRAY_LAMBDA_OPT)) },
    { Options::ID::LOOP_INVARIANT_CODE_MOTION,
        OPTION_TRUE_ACTION(opts.chirLICM = true) },
    { Options::ID::NO_LOOP_INVARIANT_CODE_MOTION,
        OPTION_TRUE_ACTION(opts.chirLICM = false) },
    // ---------- CHIR INTERPRETER ----------
    { Options::ID::PRINT_BCHIR, [](GlobalOptions& opts, const OptionArgInstance& arg) {
        CJC_ASSERT(PRINT_BCHIR_MODE_MAP.count(arg.value) != 0);
        if (PRINT_BCHIR_MODE_MAP.count(arg.value) == 0) { return false; }
        opts.printBCHIR[static_cast<uint8_t>(PRINT_BCHIR_MODE_MAP.at(arg.value))] = true;
        return true;
    }},
    { Options::ID::INTERP_CONST_EVAL_DEBUG, OPTION_TRUE_ACTION(opts.constEvalDebug = true) },
    { Options::ID::DISABLE_CODEGEN, [](GlobalOptions& opts, [[maybe_unused]] OptionArgInstance& arg) {
        opts.disableCodeGen = true;
        return true;
    }},
    { Options::ID::DISABLE_REFLECTION, OPTION_TRUE_ACTION(opts.disableReflection = true) },
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    { Options::ID::FUNC_SECTIONS, OPTION_TRUE_ACTION(opts.enableFuncSections = true) },
    { Options::ID::NO_FUNC_SECTIONS, OPTION_TRUE_ACTION(opts.enableFuncSections = false) },
    { Options::ID::DATA_SECTIONS, OPTION_TRUE_ACTION(opts.enableDataSections = true) },
    { Options::ID::NO_DATA_SECTIONS, OPTION_TRUE_ACTION(opts.enableDataSections = false) },
    { Options::ID::GC_SECTIONS, OPTION_TRUE_ACTION(opts.enableGcSections = true) },
    { Options::ID::NO_GC_SECTIONS, OPTION_TRUE_ACTION(opts.enableGcSections = false) },
    { Options::ID::PGO_INSTR_GEN, OPTION_TRUE_ACTION(opts.enablePgoInstrGen = true) },
    { Options::ID::PGO_INSTR_USE, [](GlobalOptions& opts, const OptionArgInstance& arg) {
        opts.enablePgoInstrUse = true;
        opts.pgoProfileFile = arg.value;
        return true;
    }},
    { Options::ID::STACK_TRACE_FORMAT, [](GlobalOptions& opts, const OptionArgInstance& arg) {
        if (arg.value == "default") {
            opts.stackTraceFmt = GlobalOptions::StackTraceFormat::DEFAULT;
        } else if (arg.value == "simple") {
            opts.stackTraceFmt = GlobalOptions::StackTraceFormat::SIMPLE;
        } else if (arg.value == "all") {
            opts.stackTraceFmt = GlobalOptions::StackTraceFormat::ALL;
        } else {
            return false;
        }
        return true;
    }},
    { Options::ID::DISCARD_EH_FRAME, OPTION_TRUE_ACTION(opts.discardEhFrame = true) },
    {Options::ID::JOBS, ParseJobs},
    {Options::ID::AGGRESSIVE_PARALLEL_COMPILE, ParseAPCJobs},
#ifndef DISABLE_EFFECT_HANDLERS
    {Options::ID::ENABLE_EFFECTS, OPTION_TRUE_ACTION(opts.enableEH = true) },
#endif
#endif // CANGJIE_CODEGEN_CJNATIVE_BACKEND
};
} // namespace

namespace Cangjie {
std::string Triple::Info::EnvironmentToString() const
{
    if (auto search = ENVIRONMENT_STRING_MAP.find(env); search != ENVIRONMENT_STRING_MAP.end()) {
        return search->second;
    } else {
        return "";
    }
}

bool SetupConditionalCompilationCfgFromFile(const std::string& filePath,
    std::unordered_map<std::string, std::string>& passedWhenKeyValue, DiagnosticEngine& diag)
{
    std::string failReason;
    auto content = ReadFileContent(filePath, failReason);
    if (content.has_value()) {
        auto cfgLines = SplitLines(content.value());
        for (auto kv : cfgLines) {
            if (kv.empty() || SeeingTomlComment(kv)) {
                continue;
            }
            if (!ParseConditionKeyValueLine(kv, passedWhenKeyValue, diag)) {
                return false;
            }
        }
        return true;
    }
    diag.DiagnoseRefactor(DiagKindRefactor::driver_cfg_file_read_failed, DEFAULT_POSITION, filePath, failReason);
    return false;
}

} // namespace Cangjie

bool GlobalOptions::TryParsePreOption(OptionArgInstance& arg, ArgList& argList, bool& skipParsing)
{
    if (g_preactions.find(arg.info.GetID()) == g_preactions.end()) {
        return false;
    }
    bool success = g_preactions[arg.info.GetID()](*this, arg, skipParsing);
    OccurrenceCheck(arg, argList);
    DeprecatedOptionCheck(arg);
    return success;
}

std::optional<bool> GlobalOptions::ParseOption(OptionArgInstance& arg)
{
    if (g_actions.find(arg.info.GetID()) == g_actions.end()) {
        return std::nullopt;
    }
    return {g_actions[arg.info.GetID()](*this, arg)};
}

bool GlobalOptions::SetupConditionalCompilationCfg()
{
    DiagnosticEngine diag;
    if (!passedWhenKeyValue.empty()) {
        if (!passedWhenCfgPaths.empty()) {
            diag.DiagnoseRefactor(DiagKindRefactor::driver_cfg_path_ignored, DEFAULT_POSITION);
        }
        return true;
    }
    const std::string cfgFileName("cfg.toml");
    for (auto& path : passedWhenCfgPaths) {
        std::string filePath = JoinPath(path, cfgFileName);
        if (!FileExist(filePath)) {
            diag.DiagnoseRefactor(DiagKindRefactor::driver_warning_no_such_file, DEFAULT_POSITION, filePath);
            continue;
        }
        return SetupConditionalCompilationCfgFromFile(filePath, passedWhenKeyValue, diag);
    }
    // parse cfg.toml in default path
    std::string defaultCfgFilePath = packagePaths.empty() ? cfgFileName : JoinPath(packagePaths[0], cfgFileName);
    if (!FileExist(defaultCfgFilePath)) {
        return true;
    }
    if (SetupConditionalCompilationCfgFromFile(defaultCfgFilePath, passedWhenKeyValue, diag)) {
        return true;
    }
    return false;
}
