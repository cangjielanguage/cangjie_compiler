// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the Option related classes, which provides options for compiler.
 */

#ifndef CANGJIE_OPTION_OPTION_H
#define CANGJIE_OPTION_OPTION_H

#include <climits>
#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "cangjie/Driver/TempFileInfo.h"
#include "cangjie/Option/OptionTable.h"
#include "cangjie/Utils/FileUtil.h"
#include "cangjie/Utils/Utils.h"
#include  "cangjie/Utils/Semaphore.h"

namespace Cangjie {

bool SetupConditionalCompilationCfgFromFile(const std::string& filePath,
    std::unordered_map<std::string, std::string>& passedWhenKeyValue, DiagnosticEngine& diag);
enum class DiagKindRefactor : unsigned;
/**
 * A help structure for storing paths. GlobalOptions checks whether a path is a directory
 * or a file, we may use the structure to represent directory path and file path.
 * As a result, we don't have to call IsDir() everywhere.
 */
struct Path {
    enum class Type { FILE, DIRECTORY };
    Type type;
    std::string path;
};

enum class WarnGroup : uint8_t {
#define WARN_GROUP(DESCR, KIND) KIND,
#include "cangjie/Basic/DiagRefactor/DiagnosticWarnGroupKind.def"
#undef WARN_GROUP
    UNGROUPED,
    NONE,
};

/*
 * The options that control diagnostic output strategy.
 * json: output to console with json format.
 * noColor: output to console without color.
 * default: output to  console with color.
 * */
enum class DiagFormat : uint8_t { JSON, NO_COLOR, DEFAULT };

/*
 * The options that control mock support strategy.
 * on: compile with mocking support and allow using mocking features in the test mode.
 * off: compile in the regular mode, forbid using mocking features.
 * runtimeError: compile in the regular mode, allow using mocking features in the test mode,
 *               but throw an exception at runtime when trying to create mock.
 * default: compile with no passed explicit mock option
 * */
enum class MockMode : uint8_t { ON, OFF, RUNTIME_ERROR, DEFAULT };

class Option {
public:
    /**
     * @brief The destructor of class Option.
     */
    virtual ~Option()
    {
        argList = nullptr;
    }

    /**
     * @brief Parse from arguments.
     *
     * @param argList The argument list.
     * @return bool Return true if pass success.
     */
    virtual bool ParseFromArgs(ArgList& argList) = 0;

    ArgList* argList = nullptr;
};

/**
 * For target format information.
 */
namespace Triple {
enum class BackendType : uint8_t {
    CJNATIVE = 0,
    UNKNOWN,
};

enum class ArchType : uint8_t {
    X86_64 = 0,
    AARCH64,
    ARM64,
    ARM32,
    UNKNOWN,
};

enum class OSType : uint8_t {
    WINDOWS,
    LINUX,
    DARWIN, // MacOS
    IOS, // iOS
    UNKNOWN,
};

enum class Vendor : uint8_t {
    PC,
    APPLE,
    UNKNOWN,
};

enum class Environment : uint8_t {
    OHOS,
    GNU,
    ANDROID,
    SIMULATOR,
    NOT_AVAILABLE,
};

inline static const std::string DEFALUT_ANDROID_API {"31"};
inline static const std::string MIN_ANDROID_API {"26"};

struct Info {
    ArchType arch;
    Vendor vendor;
    OSType os;
    Environment env;

    std::string apiLevel{};

    /**
     * @brief Convert triple information to format triple string.
     *
     * @return std::string The triple name.
     */
    std::string ToTripleString() const;

    /**
     * @brief Convert triple information to full triple string.
     *
     * @return std::string The triple name.
     */
    std::string ToFullTripleString() const;

    /**
     * @brief Determine if current triple information is equal to other.
     *
     * @return bool Return true If It is equal.
     */
    inline bool operator==(const Info& other) const
    {
        return arch == other.arch && vendor == other.vendor && os == other.os && env == other.env;
    }

    /**
     * @brief Determine if current triple information is not equal to other.
     *
     * @return bool Return true If It is not equal.
     */
    inline bool operator!=(const Info& other) const
    {
        return !(*this == other);
    }

    /**
     * @brief Get OS family.
     *
     * @return OSType The OS family.
     */
    inline OSType GetOSFamily() const
    {
        return os;
    }

    /**
     * @brief Determine if current is mingw.
     *
     * @return bool Return true If It is mingw.
     */
    inline bool IsMinGW() const
    {
        return os == OSType::WINDOWS && env == Environment::GNU;
    }

    /**
     * @brief Determine if current is macos.
     *
     * @return bool Return true If It is macos.
     */
    inline bool IsMacOS() const
    {
        return vendor == Vendor::APPLE && (os == OSType::DARWIN || os == OSType::IOS);
    }

    /**
     * @brief Get Arch type.
     *
     * @return OSType The Arch type.
     */
    inline ArchType GetArchType() const
    {
        return arch;
    }

    /**
     * @brief Get effective triple string.
     *
     * @return std::string The triple name.
     */
    std::string GetEffectiveTripleString() const;

    /**
     * @brief Convert Arch type to string.
     *
     * @return std::string The Arch type name.
     */
    std::string ArchToString() const;

    /**
     * @brief Convert OS to string.
     *
     * @return std::string The OS name.
     */
    std::string OSToString() const;

    /**
     * @brief Convert vendor to string.
     *
     * @return std::string The vendor name.
     */
    std::string VendorToString() const;

    /**
     * @brief Convert environment to string.
     *
     * @return std::string The environment name.
     */
    std::string EnvironmentToString() const;
};

/**
 * @brief Convert backend type to string.
 *
 * @param backend The different backend.
 * @return std::string The backend name.
 */
std::string BackendToString(const BackendType& backend);

/**
 * @brief Is valid triple name.
 *
 * @param info The triple info.
 * @param name The triple name.
 * @return bool Return true If It is valid triple name.
 */
bool IsPossibleMatchingTripleName(const Info& info, const std::string& name);
} // namespace Triple

class GlobalOptions : public Option {
public:
    struct Environment {
        std::unordered_map<std::string, std::string> allVariables;
        std::optional<std::string> cangjieHome = std::nullopt;
        std::vector<std::string> cangjiePaths;
        std::vector<std::string> libraryPaths;
        std::vector<std::string> paths;
        std::optional<std::string> macOSSDKRoot = std::nullopt;
    };

    /**
     * @brief The destructor of class GlobalOptions.
     */
    virtual ~GlobalOptions()
    {
    }

    /**
     * @brief Parse from arguments.
     *
     * @param argList The argument list.
     * @return bool Return true if pass success.
     */
    bool ParseFromArgs(ArgList& argList) override;

    /**
     * @brief Generate frontend options.
     *
     * @return std::vector<std::string> The frontend options vector.
     */
    std::vector<std::string> GenerateFrontendOptions() const;

    bool frontendMode = false;

    Environment environment;

    Triple::Info host = {
#ifdef __aarch64__
        Triple::ArchType::AARCH64,
#elif __x86_64__
        Triple::ArchType::X86_64,
#else
        Triple::ArchType::UNKNOWN,
#endif
#ifdef __APPLE__
        Triple::Vendor::APPLE,
#else
        Triple::Vendor::UNKNOWN,
#endif
#ifdef _WIN32
        Triple::OSType::WINDOWS,
#elif defined(__APPLE__)
        Triple::OSType::DARWIN,
#else
        Triple::OSType::LINUX,
#endif
#ifdef __APPLE__
        Triple::Environment::NOT_AVAILABLE,
#elif __ohos__
        Triple::Environment::OHOS,
#else
        Triple::Environment::GNU,
#endif
    };

    Triple::Info target = host;

    std::unordered_map<std::string, std::string> passedWhenKeyValue;
    std::vector<std::string> passedWhenCfgPaths;

    /**
     * @brief Get the relative file path related to cache.
     *
     * @param filePath The full path of the file.
     * @return std::string Representing the relative file path with respect to the cache directory.
     */
    std::string GetCacheRelativeFilePath(const std::string& filePath) const;

    /**
     * @brief Generate the cached path name.
     *
     * @param fullPackageName The full package name.
     * @param extension The file extension.
     * @return std::string Representing the generated cached path name.
     */
    std::string GenerateCachedPathName(const std::string& fullPackageName, const std::string& extension) const;

    /**
     * @brief Generate the cached path name for code generation.
     *
     * @param subModuleName The sub-module name.
     * @param extension The file extension.
     * @return std::string Representing the generated cached path name.
     */
    std::string GenerateCachedPathNameForCodeGen(const std::string& subModuleName, const std::string& extension) const;

    /**
     * @brief Generates the names of the cached directory and file based on the full package name.
     *
     * @param fullPackageName The full package name.
     * @return std::pair<std::string, std::string> The first element is the directory name and the second element is
     * the file name.
     */
    std::pair<std::string, std::string> GenerateNamesOfCachedDirAndFile(const std::string& fullPackageName) const;

    /**
     * @brief Gets the hashed filename for the given object file name.
     *
     * @param objFileName The object file name.
     * @return std::string The new filename based on the hash value of the object file name.
     */
    std::string GetHashedObjFileName(const std::string& objFileName) const;

    /**
     * @brief Updates the cached directory name to reflect the full package name.
     *
     * @param fullPackageName The full package name.
     */
    void UpdateCachedDirName(const std::string& fullPackageName);

    std::string cangjieHome;

    std::string output = "./";

    std::optional<std::string> outputDir = std::nullopt;

    std::optional<std::string> outputJavaGenDir = std::nullopt;

    std::vector<std::string> importPaths; /**< .cjo search paths */
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    std::vector<std::string> pluginPaths; /**< meta-transform plugins */
#endif
    std::optional<std::string> commonPartCjo = std::nullopt; /**< .cjo path for common part of package */

    // enable incremental compilation
    bool enIncrementalCompilation = false;
    bool printIncrementalInfo = false;
    std::string compilationCachedPath;

    // cached path generated with full package name
    std::string compilationCachedDir;
    std::string compilationCachedFileName;

    std::string executablePath; /**< Absolute path of `cjc` executable program. */

    std::string moduleSrcPath; /**< module src path to be compiled. */

    std::string moduleName; /**< module name for codegen. */

    bool compilePackage = false;

    bool compileMacroPackage = false; /**< compile macro package flag. */

    bool enableOutputType = false; /**< use output type option flag. */

    bool saveTemps = false; /**< keep compilation intermediate results or not */

    std::string tempFolderPath = "."; /**< a place for saving compilation intermediate result */

    std::vector<std::string> packagePaths; /**< package paths to be compiled. */

    std::vector<std::string> srcFiles; /**< source code files. */

    std::vector<std::string> inputObjs; /**< .o files to link. */

    std::string inputCjoFile; /**< .cjo files to scan */

    std::vector<std::string> inputChirFiles; /**< .chir files to complete compilation */

    std::vector<std::string> inputPdbaFiles; /**< cbc import libraries, which used to import cbclib */

    // Built-in package dependencies which analysed by Frontend and used by Driver.
    // Built-in packages are official cangjie libraries bundled with cjc compiler release,
    // Libraries such as std module, encoding module, compress module, are built-in packages.
    // Frontend knows which built-in packages the current package depends. Frontend stores
    // dependent built-in packages into 'directBuiltinDependencies' or 'indirectBuiltinDependencies',
    // then Driver could get these info and link dependent packages at linkage time.
    std::unordered_set<std::string> directBuiltinDependencies;   /**< direct dependencies. */
    std::unordered_set<std::string> indirectBuiltinDependencies; /**< indirect dependencies. */

    std::vector<TempFileInfo> frontendOutputFiles; /**< .bc files to compile. */

    std::vector<std::string> removedPathPrefix; /**< The removed path prefix of debug info. */

    std::vector<std::string> bcInputFiles;

    // Mark the input files and their input order.
    std::vector<std::tuple<std::string, uint64_t>> inputFileOrder;
    // Mark the input library(-l) files and their input order.
    std::vector<std::tuple<std::string, uint64_t>> inputLibraryOrder;
    // Mark the input link options and their input order.
    std::vector<std::tuple<std::string, uint64_t>> inputLinkOptionsOrder;
    // Mark the input link option and their input order.
    std::vector<std::tuple<std::string, uint64_t>> inputLinkOptionOrder;

    /**
     * @brief Replaces an old input filename with a new one in the list.
     *
     * @param oldName The old filename to be replaced.
     * @param newName The new filename to replace the old one.
     */
    void ReplaceInputFileName(const std::string& oldName, const std::string& newName)
    {
        for (size_t i = 0; i < inputFileOrder.size(); i++) {
            std::tuple<std::string, uint64_t> oldTuple = inputFileOrder[i];
            if (std::get<0>(oldTuple) == oldName) {
                inputFileOrder[i] = std::make_tuple(newName, std::get<1>(oldTuple));
            }
        }
    }

    Triple::BackendType backend = Triple::BackendType::CJNATIVE;

    std::vector<std::string> macroLib; /* used in LSP and CJLINT */

    bool enableVerbose = false; /**< When enable verbose, compiler will dump every command raw string. */

    bool showUsage = false; /**< Show help when user want to, or error occurs when parsing arguments. */

    bool scanDepPkg = false; /**< Get the package(s) which the current package depends on. */

    bool noSubPkg = false; /**< Whether the package has sub-packages. */

    bool displayLineInfo = true; /**< Whether display line information. */

    bool enableCompileDebug = false; /**< Whether enable compile debug. */

    bool enableAddCommentToAst = false; /**< Whether enable add comment to ast. */

    bool enableMacroInLSP = false; /**< Whether enable macro in LSP. */

    bool enableMacroDebug = false; /**< Whether enable debug macro. */

    bool enableParallelMacro = false; /**< Whether enable parallel macro expansion. */

    bool enableCompileTest = false; /**< Whether enable compile test. */

    bool compileTestsOnly = false; /** Compile *_test.cj files only */

    bool exportForTest = false; /** Export some additional kinds of declaration specifically for using them in tests */

    bool enableEH = false; /** Whether support for effect handlers is enabled */

    MockMode mock = MockMode::DEFAULT; /**< Whether enable mocking. */

    DiagFormat diagFormat = DiagFormat::DEFAULT; /** Whether output diagnostic with color*/

    bool parseTest = false; /**< If we set flag '-test', this will be true. */

    bool implicitPrelude = true; /**< Whether import prelude libraries by default. */

    bool enableInteropCJMapping = false; /**< Whether enable cj data structure mapping for interop */

    bool enableTimer = false; /**< Whether enable timer report. */

    bool enableMemoryCollect = false; /**< Whether enable memory usage report. */

    enum class InteropLanguage : uint8_t { NA, Java, ObjC };

    InteropLanguage targetInteropLanguage{InteropLanguage::NA};

    std::optional<unsigned int> errorCountLimit = 8; /**< limits the amount of errors compiler prints */

#ifdef CANGJIE_CHIR_WFC_OFF
    bool chirWFC = false;
#else
    bool chirWFC = true; /**< Whether enable well-formedness check on CHIR. */
#endif
    bool chirEA = false;   /**< Whether enable escape analysis on CHIR. */
    bool chirLICM = false; /**< Whether enable LICM on CHIR (this depends on escape analysis) */

    // CHIR closure-conversion
    bool chirCC = false;

    enum class OptimizationLevel : uint8_t { O0, O1, O2, O3, Os, Oz };
    OptimizationLevel optimizationLevel = OptimizationLevel::O0; /**< Backend optimization level in CJNative. */

    std::string optPassOptions = ""; /**< customized opt pass options from user.*/

    enum class OutputMode : uint8_t {
        EXECUTABLE, STATIC_LIB, SHARED_LIB, CHIR
    };
    OutputMode outputMode = OutputMode::EXECUTABLE;

    bool enableFuncSections = false;
    bool enableDataSections = false;
    bool enableGcSections = false;

    bool enableHotReload = false;

    bool disableReflection = false;

    bool enablePgoInstrGen = false;
    bool enablePgoInstrUse = false;
    std::string pgoProfileFile = "";

    bool discardEhFrame = false;

    // Control link mode of std module.
    // The 'linkStaticStd' is 'true' when cjc uses '--static-std' link option.
    // The 'linkStaticStd' is 'false' when cjc uses '--dy-std' link option.
    // The 'linkStaticStd' is 'std::nullopt' when cjc neither uses '--static-std' nor '--dy-std' link option.
    std::optional<bool> linkStaticStd = std::nullopt;

    // LTO optimization options
    enum class LTOMode : uint8_t { FULL_LTO, THIN_LTO, NO_LTO };
    LTOMode ltoMod = LTOMode::NO_LTO;
    bool enableCompileAsExe = false;

    /**
     * @brief Checks whether LTO is enabled.
     *
     * @return bool Returns true if LTO is enabled, otherwise returns false.
     */
    bool IsLTOEnabled() const
    {
        return ltoMod != LTOMode::NO_LTO;
    }

    bool IsCompileAsExeEnabled() const 
    {
        return enableCompileAsExe;
    }

    /**
     * @brief Checks whether LTO is full enabled.
     *
     * @return bool Returns true if LTO is full enabled, otherwise returns false.
     */
    bool IsFullLTOEnabled() const
    {
        return ltoMod == LTOMode::FULL_LTO;
    }

    bool disableChirOpt = false;    /**< Disable Chir Optimization When use -O2 and --disable-chir-opt*/
    bool disableBackendOpt = false; /**< Disable Backend Optimization When use -O2 and --disable-backend-opt*/
    struct SanitizerCoverageOptions {
        enum class Type {
            SCK_UNKNOW = -1,
            SCK_NONE = 0,
            SCK_FUNCTION,
            SCK_BB,
        } coverageType = Type::SCK_UNKNOW;
        bool traceCmp = false;
        bool traceMemCmp = false;
        bool tracePCGuard = false;
        bool inline8bitCounters = false;
        bool inlineBoolFlag = false;
        bool pcTable = false;
        bool stackDepth = false;

        /**
         * @brief Default constructor of SanitizerCoverageOptions.
         *
         * @return The SanitizerCoverageOptions object.
         */
        SanitizerCoverageOptions() = default;

        /**
         * @brief Sets the sanitizer coverage level.
         *
         * @param level The coverage level.
         */
        void SetSancovLevel(Type level)
        {
            coverageType = level;
        }

        /**
         * @brief Checks if the sanitizer coverage feature is enabled.
         *
         * @return bool Returns true if the sanitizer coverage feature is enabled, otherwise returns false.
         */
        bool IsSancovEnabled() const
        {
            return traceCmp || traceMemCmp || tracePCGuard ||
                inline8bitCounters || inlineBoolFlag || pcTable || stackDepth;
        }

        /**
         * @brief Serializes the SanitizerCoverageOptions object into string.
         *
         * @return std::string A serialized string.
         */
        std::string ToSerializedString() const;
    };
    SanitizerCoverageOptions sancovOption;

    // stacktrace format options: default/simple/all
    enum class StackTraceFormat : uint8_t { DEFAULT, SIMPLE, ALL };
    StackTraceFormat stackTraceFmt = StackTraceFormat::DEFAULT;

    enum class OptimizationFlag : uint8_t {
        SWITCH_OPT,
        CONST_PROPAGATION,
        LETC_FOLDING,
        REF_FOLDING,
        REF_TO_LETC,
        REDUNDANT_RETURNS,
        REDUNDANT_FUTURE,
        FUNC_INLINING,
        DEVIRTUALIZATION,
        REMOVE_REDUNDANT_ASG,
        ARRAY_LAMBDA_OPT,
        SROA_OPT,
        LOOP_INVARIANT_CODE_MOTION,
        VALUE_RANGE_ANALYSIS,
        REDUNDANT_LOAD,
    };
    std::set<OptimizationFlag> selectedCHIROpts;

    bool interpreter = false; /**< Run interpreter */
    enum class PrintBCHIROption : uint8_t {
        DESERIALIED,
        LINKED,
        CHIR2BCHIR,
        CE_CHIR2BCHIR,
        INTERPRETER,
        CE_LINKED,
        ALL
    };
    std::vector<bool> printBCHIR = {false, false, false, false, false, false, false};

    /**
     * @brief Print the PrintBCHIROption object.
     *
     * @return bool Returns true if print success, otherwise returns false.
     */
    bool PrintBchir(PrintBCHIROption stage) const
    {
        return printBCHIR[static_cast<uint8_t>(PrintBCHIROption::ALL)] || printBCHIR[static_cast<uint8_t>(stage)];
    }
    std::vector<std::string>
        interpreterSearchPaths; /**< Interpreter will search in these paths for dependencies that cannot deserialize. */
    std::vector<std::string> interpLoadLib;
    std::vector<std::string> interpreterArgs;
    bool interpreterPrintResult = false;
    bool interpFullBchir = false;
    bool interpMainNoLinkage = false;
    bool interpCHIR = false;
    bool constEvalDebug = false;
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    bool computeAnnotationsDebug{false}; // --debug-annotations
#endif
    bool disableCodeGen = false;
    bool disableDeserializer = false;

    bool chirDebugOptimizer = false;

    enum class CHIRMode : uint8_t { NA, STANDARD, WITH_ID, ALL };

    bool codegenDebugMode = false;
    bool dumpAST = false; 
    bool dumpCHIR = false;
    bool dumpIR = false;
    bool dumpAll = false; // dump all of AST, CHIR, IR
    bool dumpToScreen = false; // dump to screen instead of file
    bool chirDeserialize = false;
    std::string chirDeserializePath;

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    bool disableInstantiation = true;
#endif
    bool disableSemaVic = false;
    bool enableChirRGetOrThrowE = false;
    bool disableChirUselessImportElimination = false;
    bool enableOpaque = false;
    CHIRMode chirRenderMode = CHIRMode::NA;

    bool chirHLIR = false;

    bool chirLLVM = false;

    bool strictNumberMode = false; /**< Whether apply strict number mode. */

    bool fastMathMode = false; /**< Whether apply fast math mode. */

    bool printVersionOnly = false; /**< Whether print version only. */

    bool enableCoverage = false; /** < Whether instrument code to generate coverage info. */

    /// Whether to compile .cj.d files. Note that when true, .cj files are not processed.
    bool compileCjd{false};

    std::string interopCJPackageConfigPath = "./"; /**< cjinterop .toml package config file paths */

    enum class SanitizerType : uint8_t {
        NONE,
        ADDRESS,
        THREAD,
        HWADDRESS
    };

    SanitizerType sanitizerType = SanitizerType::NONE;

    bool experimentalMode = false;

    std::optional<std::size_t> jobs; /* parallel compile jobs. */
    std::optional<std::size_t> aggressiveParallelCompile = std::nullopt;
    bool aggressiveParallelCompileWithoutArg = false;
    std::vector<std::string> bitcodeFilesName; /** < the name of packageMoudle.bc. */
    std::vector<std::string> symbolsNeedLocalized; /** < Symbols that need to be localized in the compiled binary. */

    /**
     * @brief Determine if the output mode is executable.
     *
     * @return bool Returns true if the output mode is executable, otherwise returns false.
     */
    bool CompileExecutable() const
    {
        return (outputMode == GlobalOptions::OutputMode::EXECUTABLE);
    }

    /**
     * @brief Get options backend type.
     *
     * @return Options::Backend The backend type.
     */
    Options::Backend GetOptionsBackend()
    {
        switch (backend) {
            case Triple::BackendType::CJNATIVE:
                return Options::Backend::CJNATIVE;
            default:
                break;
        }
        return Options::Backend::UNKNOWN;
    }

    OverflowStrategy overflowStrategy = OverflowStrategy::THROWING;

    /**
     * @brief Get Cangjie library path name of host.
     *
     * @return std::string Cangjie library path name is a name consists of os, arch and
     * backend, such as linux_aarch64_cjnative.
     */
    std::string GetCangjieLibHostPathName() const;

    /**
     * @brief Get Cangjie library path of target.
     *
     * @return std::string Cangjie library path name.
     */
    std::string GetCangjieLibTargetPathName() const;

    /**
     * @brief Get serialized stack trace format string.
     *
     * @return std::string The stack trace format string.
     */
    std::string GetStackTraceFormat() const
    {
        return "--" + StackTraceFormatToSerializedString();
    };

    /**
     * @brief Determine if it is cross compiling.
     *
     * @return bool Return true If it is cross compiling.
     */
    inline bool IsCrossCompiling() const
    {
        return host != target;
    }

    /**
     * @brief Determine if chir optimization level is over O2.
     *
     * @return bool Return true If chir optimization level is over O2.
     */
    bool IsCHIROptimizationLevelOverO2() const
    {
        return (!disableChirOpt) && optimizationLevel >= GlobalOptions::OptimizationLevel::O2;
    }

    /**
     * @brief Determine if chir optimization exists.
     *
     * @return bool Return true If chir optimization exists.
     */
    bool IsOptimizationExisted(OptimizationFlag flag) const
    {
        if (disableChirOpt) {
            return false;
        }
        return selectedCHIROpts.find(flag) != selectedCHIROpts.end();
    }

    /**
     * @brief Set optimization level.
     *
     * @return bool Returns true If it set success.
     */
    bool SetOptimizationLevel(OptimizationLevel level);

    /**
     * @brief Checks if constant expression evaluation is enabled.
     *
     * @return bool Returns true if ir is enabled; otherwise returns false.
     */
    bool IsConstEvalEnabled() const
    {
        // No constant evaluation when:
        // - Running with `--chir-interpreter` - everything gets interpreted anyway.
        return !interpreter && !interpCHIR;
    }

    /**
     * @brief Checks if the Sancov option is enabled.
     *
     * @return bool Returns true if the Sancov option is enabled, otherwise returns false.
     */
    bool IsSancovOptionEnabled() const
    {
        return sancovOption.inline8bitCounters || sancovOption.inlineBoolFlag || sancovOption.tracePCGuard ||
            sancovOption.pcTable || sancovOption.stackDepth || sancovOption.traceCmp || sancovOption.traceMemCmp;
    }

    /**
     * @brief Checks if the sanitizer is enabled.
     *
     * @return bool Returns true if the sanitizer is enabled; otherwise returns false.
     */
    inline bool EnableSanitizer() const
    {
        return sanitizerType != SanitizerType::NONE;
    }

    /**
     * @brief Checks if the asan is enabled.
     *
     * @return bool Returns true if the asan is enabled; otherwise returns false.
     */
    inline bool EnableAsan() const
    {
        return sanitizerType == SanitizerType::ADDRESS;
    }

    /**
     * @brief Checks if the tsan is enabled.
     *
     * @return bool Returns true if the tsan is enabled; otherwise returns false.
     */
    inline bool EnableTsan() const
    {
        return sanitizerType == SanitizerType::THREAD;
    }

    /**
     * @brief Checks if the HwAsan is enabled.
     *
     * @return bool Returns true if the HwAsan is enabled; otherwise returns false.
     */
    inline bool EnableHwAsan() const
    {
        return sanitizerType == SanitizerType::HWADDRESS;
    }

    /**
     * @brief Converts the sanitizer type to a short string representation.
     *
     * @return std::string The short string representation of the sanitizer type.
     */
    std::string SanitizerTypeToShortString() const;

    /**
     * @brief Checks if the obfuscation is enabled.
     *
     * @return bool Returns true if the obfuscation is enabled; otherwise returns false.
     */
    virtual bool IsObfuscationEnabled() const
    {
        return false;
    }

    /**
     * @brief Reprocesses the obfuscation option.
     *
     * @return bool Returns true if the reprocessing was successful; otherwise returns false.
     */
    virtual bool ReprocessObfuseOption()
    {
        return true;
    }

    /**
     * @brief Parses an integer value from an option argument instance.
     *
     * @param arg The optional argument instance.
     * @param from The minimum allowed value. Default is INT_MIN.
     * @param to The maximum allowed value. Default is INT_MAX.
     * @return std::optional<int> The optional integer value.
     */
    static std::optional<int> ParseIntOptionValue(const OptionArgInstance& arg, int from = INT_MIN, int to = INT_MAX);

    /**
     * @brief Gets the extension of a shared library file.
     *
     * @param osType The operating system type.
     * @return std::string The extension of the shared library file.
     */
    static std::string GetSharedLibraryExtension(Triple::OSType osType);

    /**
     * @brief Reads paths from environment variables.
     *
     * @param environmentVars An environment variable map.
     */
    virtual void ReadPathsFromEnvironmentVars(const std::unordered_map<std::string, std::string>& environmentVars);

    /**
     * @brief Checks if a directory path exists.
     *
     * @param path The optional absolute directory path.
     * @return std::string The optional absolute directory path.
     */
    std::optional<std::string> CheckDirectoryPath(const std::string& path) const;

    /**
     * @brief Checks if a directory path with file mode exists.
     *
     * @param path The optional absolute directory path.
     * @param mode The file mode.
     * @return std::string The optional absolute directory path.
     */
    std::optional<std::string> ValidateDirectoryPath(const std::string& path, FileUtil::FileMode mode) const;
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    /**
     * @brief Checks if the input file path is valid.
     *
     * @param path The input file path.
     * @return std::optional<std::string> The optional absolute file path.
     */
    std::optional<std::string> CheckInputFilePath(const std::string& path) const;
#endif

    /**
     * @brief Validates the input file path.
     *
     * @param path The input file path.
     * @param notFoundError A reference to the diagnostic kind used to generate the error message.
     * @return std::optional<std::string> The optional valid file path.
     */
    static std::optional<std::string> ValidateInputFilePath(
        const std::string& path, const DiagKindRefactor notFoundError);
    /**
     * @brief Validates the input file path.
     *
     * Functionally equivalent to the other ValidateInputFilePath, but with a diagnostic engine.
     * Used by checkcjd.
     */
    static std::optional<std::string> ValidateInputFilePath(
        const std::string& path, const DiagKindRefactor notFoundError, DiagnosticEngine& diag);

    /**
     * @brief Sets the frontend mode.
     */
    inline void SetFrontendMode()
    {
        frontendMode = true;
    }

    /**
     * @brief Sets the path for the compilation cache.
     */
    void SetCompilationCachedPath();

    /**
     * @brief Converts the object to a serialized form.
     *
     * @return std::vector<std::string> The serialized data vector.
     */
    std::vector<std::string> ToSerialized() const;

    /**
     * @brief Gets the number of jobs currently running in parallel.
     *
     * @return The number of jobs currently running in parallel.
     */
    size_t GetJobs() const
    {
        return jobs.value_or(std::thread::hardware_concurrency());
    }

    enum class CandidateEmitCHIRPhase : uint8_t {
        NA,
        RAW,
        OPT
    }; /**< Candidate phases of chir serialization file that can be emitted. */
    CandidateEmitCHIRPhase emitCHIRPhase{
        CandidateEmitCHIRPhase::NA}; /**< Emit chir serialization file of the specified phase. */

    /**
     * @brief Checks if CHIR emission is enabled.
     *
     * @return True if CHIR emission is enabled, false otherwise.
     */
    bool IsEmitCHIREnable() const
    {
        return emitCHIRPhase != CandidateEmitCHIRPhase::NA;
    }

    bool IsCompilingCJMP() const
    {
        return inputChirFiles.size() > 0;
    }

protected:
    virtual std::optional<bool> ParseOption(OptionArgInstance& arg);
    virtual bool PerformPostActions();

private:
    bool TryParsePreOption(OptionArgInstance& arg, ArgList& argList, bool& skipParsing);
    bool TryParseOption(OptionArgInstance& arg, ArgList& argList);
    void OccurrenceCheck(const OptionArgInstance& arg, ArgList& argList) const;
    void DeprecatedOptionCheck(const OptionArgInstance& arg) const;

    bool SetupConditionalCompilationCfg();
    void SetupChirOptions();
    bool ReprocessOutputs();
    bool CheckOutputPathLength() const;
    bool ReprocessInputs();
    bool ReprocessCoverageOptions();
    bool ReprocessReflectionOption();
    bool CheckScanDependencyOptions() const;
    bool CheckSanitizerOptions() const;
    bool CheckLtoOptions() const;
    bool CheckCompileAsExeOptions() const;
    bool CheckPgoOptions() const;
    bool CheckCompileMacro() const;
    void RefactJobs();
    void RefactAggressiveParallelCompileOption();
    void DisableStaticStdForOhos();

    bool ProcessInputs(const std::vector<std::string>& inputs);
    bool HandleArchiveExtension(DiagnosticEngine& diag, const std::string& value);
    bool HandleCJOExtension(DiagnosticEngine& diag, const std::string& value);
    bool HandleCJExtension(DiagnosticEngine& diag, const std::string& value);
    bool HandleCHIRExtension(DiagnosticEngine& diag, const std::string& value);
    bool HandleCJDExtension(DiagnosticEngine& diag, const std::string& value);
    bool HandleBCExtension(DiagnosticEngine& diag, const std::string& value);
    bool HandleNoExtension(DiagnosticEngine& diag, const std::string& value);

    void ErrorExperimentalOption(const std::string& argName)
    {
        Errorf("%s is an experimental feature. \"%s\" option is required to enable experimental features.\n",
            argName.c_str(), "--experimental");
    }

    std::string PassedWhenKeyValueToSerializedString() const;
    std::string BackendTypeToSerializedString() const;
    std::string OptimizationLevelToSerializedString() const;
    std::string StackTraceFormatToSerializedString() const;
    std::string OutputModeToSerializedString() const;
    std::string SelectedCHIROptsToSerializedString() const;
    std::string OverflowStrategyToSerializedString() const;
    std::string SanitizerTypeToSerializedString() const;
    void CollectOrderedInputFiles(ArgInstance& arg, uint64_t idx);
};

extern const std::unordered_map<GlobalOptions::OptimizationLevel, std::string> OPTIMIZATION_LEVEL_TO_BACKEND_OPTION;

bool IsUnsafeBackend(Triple::BackendType backendType);

class WarningOptionMgr {
public:
    static void UpdateFlags(const std::vector<bool> newFlags)
    {
        GetInstance()->suppressedFlags = newFlags;
    }

    static void UpdateFlag(size_t index, bool newVal)
    {
        auto& flags = GetInstance()->suppressedFlags;
        CJC_ASSERT(index < flags.size());
        flags[index] = newVal;
    }

    bool IsSuppressed(size_t index)
    {
        CJC_ASSERT(index < suppressedFlags.size());
        return suppressedFlags[index];
    }

    static WarningOptionMgr* GetInstance()
    {
        static WarningOptionMgr ins{};
        return &ins;
    }

private:
    WarningOptionMgr() = default;
    WarningOptionMgr(const WarningOptionMgr& wom) = delete;
    WarningOptionMgr& operator=(const WarningOptionMgr& wom) = delete;
    ~WarningOptionMgr() = default;

private:
    std::vector<bool> suppressedFlags = std::vector<bool>(static_cast<size_t>(WarnGroup::NONE), false);
};

} // namespace Cangjie
#endif // CANGJIE_OPTION_OPTION_H
