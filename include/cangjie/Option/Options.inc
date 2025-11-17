// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file lists the configuration for all cjc options, which is included by headers.
 */

// ------------------------------ A guide to Options.inc ------------------------------
// This file lists all compile options `cjc` supports. Each option listed in the file
// has a number of properties. Command line argument parser will check properties and
// use different parsing rules on options for different properties.
//
// Definition of a compile option looks as follows:
// OPTION(NAME, ID, KIND, BACKENDS, GROUPS, ALIAS, VALUES, OCCURRENCE, HELPTEXT)
// - NAME
//     is a string type option name, e.g. "--module-name" or "--output"
// - ID
//     is used to identify the option in cpp code, it should be unique in the list
// - KIND
//     is the type of option. For example, "-g" is an option which requires no argument,
//     "-o" is an option which requires one argument (the output name). See KIND for
//     details.
// - BACKENDS
//     is a brace enclosed list of BACKEND(...), the options will only be visible and
//     available in a version of `cjc` that supports given BACKEND(...). See BACKEND_DEF
//     for supported BACKEND(...). Note that, "," is used as paramter separator in macro
//     expansion. To be able to separate elements in a list, we defined a special macro,
//     COMMA (see below), to separate elements in lists. COMMA will be translated to ","
//     after macro expansion.
// - GROUPS
//     is a brace enclosed list of GROUP(...), different version of `cjc` supports
//     different group of options, see GROUP_DEF for details.
// - ALIAS
//     is a string type option name, which is an alias of NAME. If ALIAS is defined,
//     then both NAME and ALIAS could be recognized as option ID. In case that ALIAS is
//     recognized, for SEPARATED kind options, the argument could follows the ALIAS name
//     without space in command line. It could be `nullptr` if the option has no alias
//     name.
// - VALUES
//     is a list of values that the option supports. In case that a SEPARATED kind
//     option only supports limited number of values, we could use VALUES to defined a
//     list of supported values. The command line argument parser could give a pretty
//     print on supported values, and refined parsing control (for different backends or
//     groups) could be applied on different values. See PREDEFVALUE for details.
// - OCCURRENCE
//     affects the behaviour of parser when user specified the option multiple times in a
//     command. For example, in case of "-o", which is marked as `SINGLE_OCCURRENCE`, if
//     user inputs `cjc main.cj -o main -o a.out`, a warning will be printed since `-o main`
//     is overwritten by `-o a.out`. For FLAG kind options, `MULTIPLE_OCCURRENCE` should be
//     used since it does not change the semantic of command (in most times).

/**
 * Kinds of option.
 */
#ifdef KIND
// A one-way switch without arguments, e.g. "-g", "-c".
KIND(FLAG)
KIND(FLAG_WITH_ARG)
// An option that takes an argument separated by space, e.g. "--output ./main". If a
// SEPARATED kind option is specified by NAME, the argument could be separated by space
// or "="; if the options is specified by ALIAS, the argument could be separated by space
// or nothing (in other words, the space could be omitted).
KIND(SEPARATED)
// An option that takes an argument without separator, e.g. "-O<N>". "-O <N>" will not be
// recognized.
KIND(CONTINOUS)
#endif

/**
 * Groups of option.
 * Groups affect the output of show usage and how parser recognizes options.
 */
#ifdef GROUP_DEF
// Global options, both `cjc` and `cjc-frontend` recognizes.
GROUP_DEF(GLOBAL)
// Driver options, only `cjc` recognizes.
GROUP_DEF(DRIVER)
// Frontend options, only `cjc-frontend` recognizes.
GROUP_DEF(FRONTEND)
// Stable options, or non-experimental options. If an option does not belongs to GROUP(STABLE),
// then it must be used with "--experimental" option.
GROUP_DEF(STABLE)
// Options that will be visible in community distribution (external release). Options that belong
// to GROUP(VISIBLE) must be listed on `cjc` user manual.
GROUP_DEF(VISIBLE)
#endif

/**
 * Backends options support.
 * An option could belongs to mutiple BACKEND groups.
 */
#ifdef BACKEND_DEF
// For general options that support all backends.
BACKEND_DEF(ALL)
// The option will be visible and available in `cjc` that supports CJNATIVE BE.
BACKEND_DEF(CJNATIVE)
#endif

/**
 * Occurrence type of an option in a command.
 */
#ifdef OCCURRENCE
// A warning will be printed if the option occurs multiple times in a command.
OCCURRENCE(SINGLE_OCCURRENCE)
// No occurrence checking for the option while parsing.
OCCURRENCE(MULTIPLE_OCCURRENCE)
#endif

#ifdef PREDEFVALUE
// Note: Use COMMA instead of "," to separate elements in a list
#define COMMA ,
PREDEFVALUE(backend_value, {
    VALUE("llvm", "compile with cjnative backend (default)", { BACKEND(ALL) }, { GROUP(STABLE) })});

PREDEFVALUE(warn_group, {
    VALUE("all", "", { BACKEND(ALL) }, { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }) COMMA
    VALUE("unused", "", { BACKEND(ALL) }, { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }) COMMA
    VALUE("unused-main", "", { BACKEND(ALL) }, { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }) COMMA
    VALUE("driver-arg", "", { BACKEND(ALL) }, { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }) COMMA
    VALUE("deprecated", "", { BACKEND(ALL) }, { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }) COMMA
    VALUE("unsupport-compile-source", "", { BACKEND(ALL) }, { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA
        GROUP(VISIBLE) }) COMMA
    VALUE("package-import", "", { BACKEND(ALL) }, { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }) COMMA
    VALUE("parser", "", { BACKEND(ALL) }, { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }) COMMA
    VALUE("semantics", "", { BACKEND(ALL) }, { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }) COMMA
    VALUE("interpreter", "", { BACKEND(ALL) }, { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }) COMMA
    VALUE("apilevel-check", "", { BACKEND(ALL) }, { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) })});
PREDEFVALUE(diagnostic_value, {
    VALUE("json", "", { BACKEND(ALL) }, { GROUP(STABLE) COMMA GROUP(VISIBLE) }) COMMA
    VALUE("noColor", "", { BACKEND(ALL) }, { GROUP(STABLE) COMMA GROUP(VISIBLE) }) COMMA
    VALUE("default", "", { BACKEND(ALL) }, { GROUP(STABLE) COMMA GROUP(VISIBLE) }) });
PREDEFVALUE(emit_chir_phase, {
    VALUE("raw", "emit CHIR before compiler optimization", {BACKEND(ALL)}, {GROUP(STABLE) COMMA GROUP(VISIBLE)}) COMMA
    VALUE("opt", "emit CHIR after compiler optimization (default)", {BACKEND(ALL)},
        {GROUP(STABLE) COMMA GROUP(VISIBLE)})});
PREDEFVALUE(chir_mode, {
    VALUE("standard", "", { BACKEND(ALL) }, { GROUP(STABLE) COMMA GROUP(VISIBLE) }) COMMA
    VALUE("withID", "", { BACKEND(ALL) }, { GROUP(STABLE) COMMA GROUP(VISIBLE) }) COMMA
    VALUE("all", "", { BACKEND(ALL) }, { GROUP(STABLE) COMMA GROUP(VISIBLE) }) });
PREDEFVALUE(simple_on_off_mode, {
    VALUE("on", "", { BACKEND(ALL) }, { GROUP(STABLE) COMMA GROUP(VISIBLE) }) COMMA
    VALUE("off", "", { BACKEND(ALL) }, { GROUP(STABLE) COMMA GROUP(VISIBLE) }) });
PREDEFVALUE(integer_overflow_mode, {
    VALUE("throwing", "", { BACKEND(ALL) }, { GROUP(STABLE) COMMA GROUP(VISIBLE) }) COMMA
    VALUE("wrapping", "", { BACKEND(ALL) }, { GROUP(STABLE) COMMA GROUP(VISIBLE) }) COMMA
    VALUE("saturating", "", { BACKEND(ALL) }, { GROUP(STABLE) COMMA GROUP(VISIBLE) }) });
PREDEFVALUE(sanitizer_type, {
    VALUE("address", "enable address sanitizer", { BACKEND(CJNATIVE) }, { GROUP(STABLE) COMMA GROUP(VISIBLE) }) COMMA
    VALUE("thread", "enable thread sanitizer", { BACKEND(CJNATIVE) }, { GROUP(STABLE) COMMA GROUP(VISIBLE) }) COMMA
    VALUE("hwaddress", "enable hardware-assisted address sanitizer", { BACKEND(CJNATIVE) },
                                            { GROUP(STABLE) COMMA GROUP(VISIBLE) }) });
PREDEFVALUE(stack_trace_format, {
    VALUE("default", "", { BACKEND(CJNATIVE) }, { GROUP(STABLE) COMMA GROUP(VISIBLE) }) COMMA
    VALUE("simple", "", { BACKEND(CJNATIVE) }, { GROUP(STABLE) COMMA GROUP(VISIBLE) }) COMMA
    VALUE("all", "", { BACKEND(CJNATIVE) }, { GROUP(STABLE) COMMA GROUP(VISIBLE) }) });
PREDEFVALUE(output_type_list, {
    // for CJNATIVE backend
    VALUE("exe", "emit executable (default)", { BACKEND(CJNATIVE) }, { GROUP(STABLE) COMMA GROUP(VISIBLE) }) COMMA
    VALUE("staticlib", "emit static library", { BACKEND(CJNATIVE) }, { GROUP(STABLE) COMMA GROUP(VISIBLE) }) COMMA
    VALUE("dylib", "emit dynamic library", { BACKEND(CJNATIVE) }, { GROUP(STABLE) COMMA GROUP(VISIBLE) }) COMMA
#if !defined(CANGJIE_VISIBLE_OPTIONS_ONLY)
    VALUE("hotreload", "emit dynamic library with hot reload supports", { BACKEND(CJNATIVE) }, { GROUP(VISIBLE) })
#endif
});
PREDEFVALUE(lto_mode, {
    VALUE("full", "", { BACKEND(CJNATIVE) }, { GROUP(STABLE) COMMA GROUP(VISIBLE) }) COMMA
    VALUE("thin", "", { BACKEND(CJNATIVE) }, { GROUP(STABLE) COMMA GROUP(VISIBLE) }) });
PREDEFVALUE(bchir_print_mode, {
    VALUE("deserialized", "after deserialisation", { BACKEND(CJNATIVE) }, { GROUP(STABLE) }) COMMA
    VALUE("linked", "after linkage", { BACKEND(CJNATIVE) }, { GROUP(STABLE) }) COMMA
    VALUE("chir2bchir", "after chir2bchir", { BACKEND(CJNATIVE) }, { GROUP(STABLE) }) COMMA
    VALUE("interpreter", "interpretation trace", { BACKEND(CJNATIVE) }, { GROUP(STABLE) }) COMMA
    VALUE("ce-linked", "const eval after linkage", { BACKEND(CJNATIVE) }, { GROUP(STABLE) }) COMMA
    VALUE("ce-chir2bchir", "const eval after chir2bchir", { BACKEND(CJNATIVE) }, { GROUP(STABLE) }) COMMA
    VALUE("all", "all of the above", { BACKEND(CJNATIVE) }, { GROUP(STABLE) }) });
PREDEFVALUE(mock_mode, {
    VALUE("on", "", { BACKEND(ALL) }, { GROUP(STABLE) COMMA GROUP(VISIBLE) }) COMMA
    VALUE("off", "", { BACKEND(ALL) }, { GROUP(STABLE) COMMA GROUP(VISIBLE) }) COMMA
    VALUE("runtime-error", "", { BACKEND(ALL) }, { GROUP(STABLE) COMMA GROUP(VISIBLE) }) });
#undef COMMA
#endif

/**
 * If you are trying to add a new `cjc` option, here is what you need to do.
 * 1. Add an option definition below. Here is some notes.
 *    OPTION(NAME, ID, KIND, BACKENDS, GROUPS, ALIAS, VALUES, OCCURRENCE, HELPTEXT)
 *    - NAME is your option name, for example "--my-option". NOTE: use DOUBLE HYPHEN (--)
 *      instead of single hyphen (-) for the option prefix if option name has more than
 *      one character.
 *    - ID could be any unique name that could represent your option.
 *    - If your option takes no arguments, then use FLAG for KIND. Use SEPARATED for
 *      KIND otherwise.
 *    - If your option is for all backends, then BACKENDS should be `{ BACKEND(ALL) }`,
 *      otherwise replace ALL with the backend name your option supports. If your option
 *      supports two or more backends, such as CJNATIVE and OTHER, then you could write
 *      `{ BACKEND(CJNATIVE) COMMA BACKEND(OTHER) }`.
 *    - If your option is a temporary option or is not ready to be released, they you
 *      could give your option group `{ GROUP(GLOBAL) }`. If your option is tested and
 *      is listed on `cjc` user manual, they you may mark your option as `GROUP(VISIBLE)`.
 *    - Does your option have a short name? Set ALIAS to your option's one character
 *      short name if it has one, set ALIAS `nullptr` otherwise.
 *    - If your option has SEPARATED kind but only recognizes values from a list of
 *      candidates, then you should consider defining candidates in PREDEFVALUE. `{}` for
 *      VALUES otherwise.
 *    - If you are adding a FLAG kind option, then OCCURRENCE should be MULTIPLE_OCCURRENCE.
 *      If you are adding a SEPARATED kind option, and if the option could not be specified
 *      multiple times in a command (if later one overwrites formers), they OCCURRENCE
 *      should be SINGLE_OCCURRENCE. MULTIPLE_OCCURRENCE otherwise.
 *    - HELPTEXT is a string that describes your option. It will be displayed in `cjc -h`.
 * 2. Add corresponding data member in Option.h.
 * 3. Add option trigger logic in Option.cpp.
 *    See g_actions in Option.cpp.
 * For details, see "A guide to Options.inc" (scolls to top).
 */
#ifdef OPTION
#define COMMA ,
//////////////////////////////
// Frontend Options: Frontend Group options only work when "-frontend" is enabled.
OPTION("--dump-tokens", DUMP_TOKENS, FLAG, { BACKEND(ALL) },
    { GROUP(FRONTEND) COMMA GROUP(STABLE) },
    nullptr, {}, MULTIPLE_OCCURRENCE,
    "Print all of the tokens in one compile unit")
OPTION("--dump-parse", DUMP_PARSE, FLAG, { BACKEND(ALL) },
    { GROUP(FRONTEND) COMMA GROUP(STABLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Print AST after parser")
OPTION("--dump-ast", DUMP_AST, FLAG, { BACKEND(ALL) },
    { GROUP(FRONTEND) COMMA GROUP(STABLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Print AST after semantic analysis")
OPTION("--dump-symbols", DUMP_SYMBOLS, FLAG, { BACKEND(ALL) },
    { GROUP(FRONTEND) COMMA GROUP(STABLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Print symbols after semantic analysis")
OPTION("--dump-ir", DUMP_IR, FLAG, { BACKEND(ALL) },
    { GROUP(FRONTEND) COMMA GROUP(STABLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Print human readable IR")
OPTION("--dump-bc", DUMP_BC, FLAG, { BACKEND(ALL) },
    { GROUP(FRONTEND) COMMA GROUP(STABLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Dump machine-readable LLVM bitcode")
OPTION("--typecheck", TYPE_CHECK, FLAG, { BACKEND(ALL) },
    { GROUP(FRONTEND) COMMA GROUP(STABLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Parse and check type of input file(s)")
OPTION("--dump-macro", DUMP_MACRO, FLAG, { BACKEND(ALL) },
    { GROUP(FRONTEND) COMMA GROUP(STABLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Dump tokens after macro expansion")
OPTION("-d", COMPILE_CJD, FLAG, { BACKEND(ALL) },
    { GROUP(FRONTEND) COMMA GROUP(STABLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Compile declaration file(s) (.cj.d)")

OPTION("--diagnostic-format", DIAG_FORMAT, SEPARATED, { BACKEND(ALL) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE)}, nullptr, diagnostic_value, SINGLE_OCCURRENCE,
    "Diagnostic format. Candidate modes: ")
OPTION("--backend", BACKEND_MODE, SEPARATED, { BACKEND(ALL) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) }, nullptr, backend_value, SINGLE_OCCURRENCE,
    "Specify backend toolchain")
OPTION("--scan-dependency", DUMP_DEPENDENT_PACKAGE, FLAG, { BACKEND(ALL) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Get the package(s) which the current package depends on")
OPTION("--no-sub-pkg", NO_SUB_PACKAGE, FLAG, { BACKEND(ALL) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "The package doesn't have sub-packages")
OPTION("--cfg", CONDITIONAL_COMPILATION_CONFIG, SEPARATED, { BACKEND(ALL) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "User defined condition to compile")
OPTION("--debug-macro", COMPILE_DEBUG_MACRO, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Enable debug macro")
OPTION("--parallel-macro-expansion", PARALLEL_MACRO_EXPANSION, FLAG, {BACKEND(ALL)},
    {GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE)}, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Enable parallel macro expansion")
OPTION("-g", COMPILE_DEBUG, FLAG, { BACKEND(ALL) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Enable compile debug version target")
OPTION("--trimpath", TRIMPATH, SEPARATED, { BACKEND(ALL) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE)},
    nullptr, {}, MULTIPLE_OCCURRENCE, "Remove a specified path prefix in debuginfo")
OPTION("--strip-all", STRIP_ALL, FLAG, {BACKEND(CJNATIVE) },
    {GROUP(DRIVER) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE)}, "-s", {}, MULTIPLE_OCCURRENCE,
    "Strip the symbol table from executable and dynamic library")
OPTION("--no-stacktrace-info", NO_STACKTRACE_INFO, FLAG, { BACKEND(ALL) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Do not display the line number and file name information")
OPTION("--test", COMPILE_TEST, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Enable compile test")
OPTION("--test-only", COMPILE_TESTS_ONLY, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Compile test files only (`<filename>_test.cj`), production parts should be added as a dependency separately")
OPTION("--export-for-test", EXPORT_FOR_TESTS, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Export some additional kinds of declaration specifically for using them in tests")
OPTION("--mock", MOCK, SEPARATED, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, mock_mode, SINGLE_OCCURRENCE,
    "Specify whether mock features are enabled, or disabled, "
    "or a runtime exception is thrown when trying to use mock features")
OPTION("--output", OUTPUT_FILE, SEPARATED, { BACKEND(ALL) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, "-o", {}, SINGLE_OCCURRENCE,
    "Specify product name or output directory when compiling a package")
OPTION("--output-dir", OUTPUT_DIR, SEPARATED, { BACKEND(ALL) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, SINGLE_OCCURRENCE,
    "Specify output directory (it affects '--output' option)")
OPTION("--no-prelude", NO_PRELUDE, FLAG, { BACKEND(ALL) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Not import cangjie std.core package by default, only applicable when compiling std.core")
OPTION("--static", STATIC, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(DRIVER) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Statically link cangjie library")
OPTION("--static-std", STATIC_STD, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(DRIVER) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Statically link packages of the std module")
OPTION("--dy-std", DY_STD, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(DRIVER) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Dynamically link packages of the std module")
OPTION("--static-libs", STATIC_LIBS, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(DRIVER) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Statically link packages of other modules except std")
OPTION("--dy-libs", DY_LIBS, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(DRIVER) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Dynamically link packages of other modules except std")
OPTION("--lto", LTO, SEPARATED, {BACKEND(CJNATIVE)},
    {GROUP(DRIVER) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE)}, nullptr, lto_mode, SINGLE_OCCURRENCE,
    "Enable LTO to either 'full' or 'thin' (Not available for Windows target)")
OPTION("--compile-as-exe", COMPILE_AS_EXE, FLAG, {BACKEND(CJNATIVE)},
    {GROUP(DRIVER) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE)}, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Hide symbols from bitcode files in LTO mode, and only the package init symbols remain visible")
OPTION("--profile-compile-time", PROFILE_COMPILE_TIME, FLAG, { BACKEND(ALL) },
    { GROUP(GLOBAL) COMMA GROUP(VISIBLE) COMMA GROUP(STABLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Print time spent of all phases in the compilation")
OPTION("--profile-compile-memory", PROFILE_COMPILE_MEMORY, FLAG, { BACKEND(ALL) },
    { GROUP(GLOBAL) COMMA GROUP(VISIBLE) COMMA GROUP(STABLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Print memory usage of all phases in the compilation")
#ifndef DISABLE_EFFECT_HANDLERS
OPTION("--enable-eh", ENABLE_EFFECTS, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Enables experimental support for effect handlers")
#endif
OPTION("--dump-chir-debug", DUMP_CHIR_DEBUG, FLAG, { BACKEND(ALL) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Dump CHIR readable txt file when compiling for debug")
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
OPTION("--debug-annotations", DUMP_ANNOTATIONS_DEBUG, FLAG, { BACKEND(ALL) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Debug computing annotations stage")
#endif
OPTION("--deserialize-chir-and-dump", DESERIALIZE_CHIR_AND_DUMP, SEPARATED, { BACKEND(ALL) },
    { GROUP(FRONTEND) COMMA GROUP(STABLE) }, nullptr, {}, SINGLE_OCCURRENCE,
    "Deserialize CHIR and dump CHIL to file")
OPTION("--emit-chir", EMIT_CHIR, FLAG_WITH_ARG, {BACKEND(ALL)},
    {GROUP(GLOBAL) COMMA GROUP(VISIBLE) COMMA GROUP(STABLE)}, nullptr, emit_chir_phase, SINGLE_OCCURRENCE,
    "emit CHIR serialization file of the specified phase. Candidate phases:")
OPTION("--chir-wfc", CHIR_WFC, SEPARATED, { BACKEND(ALL) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) }, nullptr, simple_on_off_mode, SINGLE_OCCURRENCE,
    "Well-formedness check for CHIR. Candidate modes: ")
OPTION("--disable-inst", DISABLE_INSTANTIATION, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE)}, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Disable instantiation")
OPTION("--enable-chir-redundant-getorthrow-elimination", ENABLE_CHIR_REDUNDANT_GETORTHROW_ELIMINATION, FLAG,
    { BACKEND(CJNATIVE) }, { GROUP(GLOBAL) COMMA GROUP(STABLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Enable CHIR redundant GetOrThrow elimination.")
OPTION("--disable-chir-useless-import-elimination", DISABLE_CHIR_USELESS_IMPORT_ELIMINATION, FLAG,
    { BACKEND(CJNATIVE) }, { GROUP(GLOBAL) COMMA GROUP(STABLE) }, "--disable-chir-UIE", {}, MULTIPLE_OCCURRENCE,
    "Disable CHIR useless import elimination")
OPTION("--chir-ea", CHIR_EA, SEPARATED, { BACKEND(ALL) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) }, nullptr, simple_on_off_mode, SINGLE_OCCURRENCE,
    "escape analysis for CHIR. Candidate modes: ")
OPTION("--fchir-constant-propagation", CONST_PROPAGATION, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Enable constant propagation optimizaion in CHIR")
OPTION("--fno-chir-constant-propagation", NO_CONST_PROPAGATION, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Disable constant propagation optimizaion in CHIR")
OPTION("--fchir-function-inlining", FUNC_INLINING, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Enable function inlining optimizaion in CHIR")
OPTION("--fno-chir-function-inlining", NO_FUNC_INLINING, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Disable function inlining optimizaion in CHIR")
OPTION("--fchir-devirtualization", DEVIRTUALIZATION, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Enable devirtualization optimizaion in CHIR")
OPTION("--fno-chir-devirtualization", NO_DEVIRTUALIZATION, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Disable devirtualization optimizaion in CHIR")
OPTION("--fchir-redundant-return-removal", REDUANDANT_RETURN_REMOVAL, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Enable redundant return removal in CHIR")
OPTION("--fno-chir-redundant-return-removal", NO_REDUANDANT_RETURN_REMOVAL, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Disable redundant return removal in CHIR")
OPTION("--fchir-redundant-future-removal", REDUNDANT_FUTURE_REMOVAL, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Enable redundant future removal in CHIR")
OPTION("--fno-chir-redundant-future-removal", NO_REDUNDANT_FUTURE_REMOVAL, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Disable redundant future removal in CHIR")
OPTION("--fchir-redundant-assign-removal", REDUANDANT_ASSIGN_REMOVAL, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Enable redundant assign removal in CHIR")
OPTION("--fno-chir-redundant-assign-removal", NO_REDUANDANT_ASSIGN_REMOVAL, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Disable redundant assign removal in CHIR")
OPTION("--fchir-loop-invariant-code-motion", LOOP_INVARIANT_CODE_MOTION, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Enable loop invariant code motion in CHIR")
OPTION("--fno-chir-loop-invariant-code-motion", NO_LOOP_INVARIANT_CODE_MOTION, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Disable loop invariant code motion in CHIR")
OPTION("--fchir-sroa", SROA_OPT, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Enable scalar replacement of aggregates in CHIR")
OPTION("--fno-chir-sroa", NO_SROA_OPT, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Disable scalar replacement of aggregates in CHIR")

OPTION("--fchir-switch-opt", SWITCH_OPT, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Enable switch opt optimizaion in CHIR")
OPTION("--fchir-letc-folding", LETC_FOLDING, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Enable letc folding optimizaion in CHIR")
OPTION("--fchir-ref-folding", REF_FOLDING, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Enable ref folding optimizaion in CHIR")
OPTION("--fchir-array-lambda", ARRAY_LAMBDA_OPT, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Enable array lambda optimizaion in CHIR")

OPTION("--sanitizer-coverage-inline-8bit-counters", SANCOV_INLINE_8BIT, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE)}, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Enable sanitizer-coverage-inline-8bit-counters in CHIR")
OPTION("--sanitizer-coverage-inline-bool-flag", SANCOV_INLINE_BOOL_FLAG, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE)}, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Enable sanitizer-coverage-inline-bool-flag in CHIR")
OPTION("--sanitizer-coverage-trace-pc-guard", SANCOV_TRACE_PC_GUARD, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE)}, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Enable sanitizer-coverage-trace-pc-guard in CHIR")
OPTION("--sanitizer-coverage-pc-table", SANCOV_PC_TABLE, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE)}, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Enable sanitizer-coverage-pc-table in CHIR")
OPTION("--sanitizer-coverage-stack-depth", SANCOV_STACK_DEPTH, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE)}, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Enable sanitizer-coverage-stack-depth in CHIR")
OPTION("--sanitizer-coverage-trace-compares", SANCOV_TRACE_COMPARES, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE)}, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Enable sanitizer-coverage-trace-compares in CHIR")
OPTION("--sanitizer-coverage-trace-memcmp", SANCOV_TRACE_MEM_COMPARES, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE)}, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Enable sanitizer-coverage-trace-memcmp in CHIR")
OPTION("--sanitizer-coverage-level=0", SANCOV_LEVEL_0, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE)}, nullptr, {}, MULTIPLE_OCCURRENCE,
    "sancov level 0")
OPTION("--sanitizer-coverage-level=1", SANCOV_LEVEL_1, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE)}, nullptr, {}, MULTIPLE_OCCURRENCE,
    "sancov level 1")
OPTION("--sanitizer-coverage-level=2", SANCOV_LEVEL_2, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE)}, nullptr, {}, MULTIPLE_OCCURRENCE,
    "sancov level 2")
OPTION("--sanitizer-coverage-level", SANCOV_LEVEL_CUSTOM, SEPARATED, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE)}, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Set sanitizer-coverage level")

OPTION("--disable-sema-vic", DISABLE_SEMA_VIC, FLAG, { BACKEND(ALL) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Disable variable initialization check in SEMA")

OPTION("--disable-chir-opt", DISABLE_CHIR_OPT, FLAG, { BACKEND(ALL) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Disable optimizaion in CHIR")
OPTION("--disable-backend-opt", DISABLE_BACKEND_OPT, FLAG, { BACKEND(ALL) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Disable optimizaion in Backend")

OPTION("--chir-opt-debug", CHIR_OPT_DEBUG, FLAG, { BACKEND(ALL) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Debug optimizer")
OPTION("--render-chir", RENDER_CHIR, SEPARATED, { BACKEND(ALL) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) }, nullptr, chir_mode, SINGLE_OCCURRENCE,
    "Render CHIR as graph. Candidate modes: ")
OPTION("--int-overflow", INT_OVERFLOW_MODE, SEPARATED, { BACKEND(ALL) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, integer_overflow_mode,
    SINGLE_OCCURRENCE, "Specify default integer overflow strategy: ")
OPTION("--fast-math", FAST_MATH_MODE, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Enable fast-math mode")
OPTION("--opt-options", OPT_OPTIONS, SEPARATED, { BACKEND(CJNATIVE) },
    { GROUP(DRIVER) COMMA GROUP(STABLE) }, nullptr, {}, SINGLE_OCCURRENCE,
    "Options directly passed to opt, put the value into \"\" when there is space in it")
OPTION("--llc-options", LLC_OPTIONS, SEPARATED, { BACKEND(CJNATIVE) },
    { GROUP(DRIVER) COMMA GROUP(STABLE) }, nullptr, {}, SINGLE_OCCURRENCE,
    "Options directly passed to llc, put the value into \"\" when there is space in it")
OPTION("--link-options", LINK_OPTIONS, SEPARATED, { BACKEND(ALL) },
    { GROUP(DRIVER) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE)
    }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Options directly passed to linker")
OPTION("--library-path", LIBRARY_PATH, SEPARATED, { BACKEND(ALL) },
    { GROUP(DRIVER) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE)
    }, "-L", {}, MULTIPLE_OCCURRENCE,
    "Add directory to library search path")
OPTION("--library", LIBRARY, SEPARATED, { BACKEND(ALL) },
    { GROUP(DRIVER) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, "-l", {}, MULTIPLE_OCCURRENCE,
    "Link library")
OPTION("--set-runtime-rpath", USE_RUNTIME_RPATH, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(DRIVER) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Add cangjie runtime directory to rpath")
// "-B" is a gcc/clang convention for the option which passes the path of binaries and object files.
OPTION("--toolchain", TOOLCHAIN, SEPARATED, { BACKEND(CJNATIVE) },
    { GROUP(DRIVER) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE)
    }, "-B", {}, MULTIPLE_OCCURRENCE,
    "Use toolchain binaries and object files at the given directory")
OPTION("--target", TARGET, SEPARATED, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE)
    }, nullptr, {}, SINGLE_OCCURRENCE,
    "Generate code for the given target platform")
OPTION("--target-cpu", TARGET_CPU, SEPARATED, { BACKEND(CJNATIVE) },
    { GROUP(DRIVER) COMMA GROUP(VISIBLE) }, nullptr, {}, SINGLE_OCCURRENCE,
    "Generate instructions for the given target processor")
OPTION("--sysroot", SYSROOT, SEPARATED, { BACKEND(CJNATIVE) },
    { GROUP(DRIVER) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE)
    }, nullptr, {}, SINGLE_OCCURRENCE,
    "Set the system root directory under which bin, lib and include can be found")
OPTION("--output-type", OUTPUT_TYPE, SEPARATED, { BACKEND(ALL) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, output_type_list, SINGLE_OCCURRENCE,
    "Specify output file type")
OPTION("-O0", OPTIMIZATION_0, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Optimization level 0 (default)")
OPTION("-O1", OPTIMIZATION_1, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(VISIBLE) COMMA GROUP(STABLE)
    }, "-O", {}, MULTIPLE_OCCURRENCE,
    "Optimization level 1")
OPTION("-O2", OPTIMIZATION_2, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Optimization level 2")
OPTION("-O3", OPTIMIZATION_3, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Optimization level 3")
OPTION("-Os", OPTIMIZATION_S, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Optimization level s, like -O2 with extra optimizations for size")
OPTION("-Oz", OPTIMIZATION_Z, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Optimization level z, like -Os but reduces code size further")
// NOTE: -O<value> must not be moved to the front of -O1(-O) or -O will be treated as an invald option.
OPTION("-O", OPTIMIZATION_CUSTOM, CONTINOUS, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Set Optimization level")
OPTION("--opt-pass-options", OPT_PASS_OPTIONS, SEPARATED, { BACKEND(CJNATIVE)},
    { GROUP(GLOBAL) COMMA GROUP(STABLE) }, nullptr, {}, SINGLE_OCCURRENCE,
    "Set opt pass options")
OPTION("--module-name", MODULE_NAME, SEPARATED, { BACKEND(ALL) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, SINGLE_OCCURRENCE,
    "Tell compiler name of the module")
OPTION("--package", PACKAGE_COMPILE, FLAG, { BACKEND(ALL) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, "-p", {}, MULTIPLE_OCCURRENCE,
    "Specify package directory to be compiled")
OPTION("--import-path", IMPORT_PATH, SEPARATED, { BACKEND(ALL) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Add .cjo search path")
OPTION("--plugin", PLUGIN_PATH, SEPARATED, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Specify the dynamic library path of compiler plugin")

OPTION("--incremental-compile", INCRE_COMPILE, FLAG, {BACKEND(CJNATIVE)},
    {GROUP(GLOBAL) COMMA GROUP(VISIBLE)}, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Enable incremental compilation.")
OPTION("--incremental-debug", INCRE_DEBUG, FLAG, {BACKEND(CJNATIVE)},
    {GROUP(GLOBAL)}, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Print debug message during incremental compilation.")
OPTION("--save-temps", SAVE_TEMPS, SEPARATED, { BACKEND(CJNATIVE) },
    { GROUP(DRIVER) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, SINGLE_OCCURRENCE,
    "Save intermediate compilation results. <value>: path to save temp files.")
OPTION("--warn-off", WARN_OFF, SEPARATED, { BACKEND(ALL) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, "-Woff", warn_group, MULTIPLE_OCCURRENCE,
    "Suppress a specific group of warning")
OPTION("--warn-on", WARN_ON, SEPARATED, { BACKEND(ALL) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, "-Won", warn_group, MULTIPLE_OCCURRENCE,
    "Report a specific group of warning")
OPTION("--error-count-limit", ERROR_COUNT_LIMIT, SEPARATED, { BACKEND(ALL) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, SINGLE_OCCURRENCE,
    "Emit specified <number> of errors only. Available options: all, <number> (8 by default)")
OPTION("--verbose", VERBOSE, FLAG, { BACKEND(ALL) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, "-V", {}, MULTIPLE_OCCURRENCE,
    "Enable verbose")
OPTION("--version", VERSION, FLAG, { BACKEND(ALL) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, "-v", {}, MULTIPLE_OCCURRENCE,
    "Print compiler version information ")
OPTION("--help", HELP, FLAG, { BACKEND(ALL) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, "-h", {}, MULTIPLE_OCCURRENCE,
    "Show usage")
OPTION("--compile-macro", COMPILE_MACRO, FLAG, { BACKEND(ALL) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Options to compile the macro define package")
OPTION("--coverage", ENABLE_COVERAGE, FLAG, { BACKEND(ALL) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
       "Enable coverage")
OPTION("--experimental", EXPERIMENTAL, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
       "Enable experimental options")

// ---------- PARALLEL COMPILE OPTIONS ----------
OPTION("--debug-codegen", DEBUG_CODEGEN, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Debug codeGen mode (execute the codeGen sub-tasks in series and dump debug-ir to file)")

OPTION("--jobs", JOBS, SEPARATED, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, "-j", {}, SINGLE_OCCURRENCE,
    "Number of tasks to run at once")

OPTION("--aggressive-parallel-compile", AGGRESSIVE_PARALLEL_COMPILE, FLAG_WITH_ARG, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, "--apc", {}, SINGLE_OCCURRENCE,
    "Enable agrressive parallel compile and specify the number of tasks to run at once")

// ---------- SANITIZER OPTIONS ----------
OPTION("--sanitize", SANITIZE, SEPARATED, { BACKEND(CJNATIVE) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) }, nullptr, sanitizer_type, SINGLE_OCCURRENCE,
    "Enable sanitizer: ")
OPTION("--sanitize-set-rpath", SANITIZE_SET_RPATH, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(DRIVER) COMMA GROUP(STABLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Add -rpath with cangjie runtime directory to linker flags for sanitizer-enabled product")

// ---------- CODE OBFUSCATION OPTIONS ----------
OPTION("--fobf-string", OBF_STRING, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(DRIVER) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Enable string literal obfuscation")
OPTION("--fno-obf-string", NO_OBF_STRING, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(DRIVER) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Disable string literal obfuscation")
OPTION("--fobf-const", OBF_CONST, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(DRIVER) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Enable constant literal obfuscation")
OPTION("--fno-obf-const", NO_OBF_CONST, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(DRIVER) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Disable constant literal obfuscation")
OPTION("--fobf-layout", OBF_LAYOUT, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(DRIVER) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Enable code layout obfuscation")
OPTION("--fno-obf-layout", NO_OBF_LAYOUT, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(DRIVER) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Disable code layout obfuscation")
OPTION("--fobf-cf-flatten", OBF_CF_FLATTEN, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(DRIVER) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Enable control flow flatten obfuscation")
OPTION("--fno-obf-cf-flatten", NO_OBF_CF_FLATTEN, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(DRIVER) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Disable control flow flatten obfuscation")
OPTION("--fobf-cf-bogus", OBF_CF_BOGUS, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(DRIVER) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Enable control flow bogus obfuscation")
OPTION("--fno-obf-cf-bogus", NO_OBF_CF_BOGUS, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(DRIVER) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Disable control flow bogus obfuscation")
OPTION("--fobf-all", OBF_ALL, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(DRIVER) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Enable all obfuscations")
OPTION("--fobf-export-symbols", OBF_EXPORT_SYMS, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(DRIVER) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Obfuscate export symbols when layout obfuscation is enabled")
OPTION("--fno-obf-export-symbols", NO_OBF_EXPORT_SYMS, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(DRIVER) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Don't obfuscate export symbols when layout obfuscation is enabled")
OPTION("--obf-sym-input-mapping", OBF_SYM_INPUT_MAPPING, SEPARATED, { BACKEND(CJNATIVE) },
    { GROUP(DRIVER) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, SINGLE_OCCURRENCE,
    "Specify the input files of symbol mapping for layout obfuscation")
OPTION("--obf-sym-output-mapping", OBF_SYM_OUTPUT_MAPPING, SEPARATED, { BACKEND(CJNATIVE) },
    { GROUP(DRIVER) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, SINGLE_OCCURRENCE,
    "Specify the output file of symbol mapping for layout obfuscation")
OPTION("--obf-apply-mapping-file", OBF_MAP_FILE, SEPARATED, { BACKEND(CJNATIVE) },
    { GROUP(DRIVER) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, SINGLE_OCCURRENCE,
    "Supply user-defined symbol mapping file for layout obfuscation")
OPTION("--obf-sym-prefix", OBF_SYM_PREFIX, SEPARATED, { BACKEND(CJNATIVE) },
    { GROUP(DRIVER) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, SINGLE_OCCURRENCE,
    "Specify the prefix of obfuscated symbols for layout obfuscation")
OPTION("--fobf-source-path", OBF_SOURCE_PATH, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(DRIVER) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Obfuscate source path of symbols when layout obfuscation is enabled")
OPTION("--fno-obf-source-path", NO_OBF_SOURCE_PATH, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(DRIVER) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Don't obfuscate source path of symbols when layout obfuscation is enabled")
OPTION("--fobf-line-number", OBF_LINE_NUMBER, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(DRIVER) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Obfuscate line number of symbols when layout obfuscation is enabled")
OPTION("--fno-obf-line-number", NO_OBF_LINE_NUMBER, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(DRIVER) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Don't obfuscate line number of symbols when layout obfuscation is enabled")
OPTION("--obf-config", OBF_CONFIG, SEPARATED, { BACKEND(CJNATIVE) },
    { GROUP(DRIVER) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, SINGLE_OCCURRENCE,
    "Specify obfuscation configure file")
OPTION("--obf-level", OBF_LEVEL, SEPARATED, { BACKEND(CJNATIVE) },
    { GROUP(DRIVER) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, SINGLE_OCCURRENCE,
    "Specify obfuscation level. Available value: 1 to 9 (5 by default)")
OPTION("--obf-seed", OBF_SEED, SEPARATED, { BACKEND(CJNATIVE) },
    { GROUP(DRIVER) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, SINGLE_OCCURRENCE,
    "Specify random seed for obfuscation algorithm. Available value: <number>")
OPTION("--interp-const-eval-debug", INTERP_CONST_EVAL_DEBUG, FLAG, { BACKEND(ALL) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE)}, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Enable debug logging in const evaluation")
OPTION("--print-bchir", PRINT_BCHIR, SEPARATED, { BACKEND(ALL) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) }, nullptr, bchir_print_mode, MULTIPLE_OCCURRENCE,
    "Print BCHIR")
OPTION("--disable-codegen", DISABLE_CODEGEN, FLAG, { BACKEND(ALL) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE)}, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Disable code generation")
OPTION("--disable-reflection", DISABLE_REFLECTION, FLAG, { BACKEND(ALL) },
    { GROUP(GLOBAL) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Disable reflection")
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
OPTION("--stack-trace-format", STACK_TRACE_FORMAT, SEPARATED, {BACKEND(CJNATIVE)},
    {GROUP(DRIVER) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE)}, nullptr, stack_trace_format, SINGLE_OCCURRENCE,
    "Specify stack trace format")
OPTION("--ffunction-sections", FUNC_SECTIONS, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(DRIVER) COMMA GROUP(STABLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "enable function-sections")
OPTION("--fno-function-sections", NO_FUNC_SECTIONS, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(DRIVER) COMMA GROUP(STABLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "disable function-sections")
OPTION("--fdata-sections", DATA_SECTIONS, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(DRIVER) COMMA GROUP(STABLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "enable data-sections")
OPTION("--fno-data-sections", NO_DATA_SECTIONS, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(DRIVER) COMMA GROUP(STABLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "disable data-sections")
OPTION("--gc-sections", GC_SECTIONS, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(DRIVER) COMMA GROUP(STABLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "enable gc-sections")
OPTION("--no-gc-sections", NO_GC_SECTIONS, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(DRIVER) COMMA GROUP(STABLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "disable gc-sections")
OPTION("--pgo-instr-gen", PGO_INSTR_GEN, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(DRIVER) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "PGO instrumentation")
OPTION("--pgo-instr-use", PGO_INSTR_USE, SEPARATED, { BACKEND(CJNATIVE) },
    { GROUP(DRIVER) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, SINGLE_OCCURRENCE,
    "Read PGO instrumentation profile")
OPTION("--discard-eh-frame", DISCARD_EH_FRAME, FLAG, { BACKEND(CJNATIVE) },
    { GROUP(DRIVER) COMMA GROUP(STABLE) COMMA GROUP(VISIBLE) }, nullptr, {}, MULTIPLE_OCCURRENCE,
    "Discard the eh_frame section")
#endif
// NOTE: when adding a new option, use DOUBLE HYPHEN (--) instead of single hyphen (-) for the option,
// unless it is a single character option or it is a single character abbreviation.
#undef COMMA
#endif // OPTION
