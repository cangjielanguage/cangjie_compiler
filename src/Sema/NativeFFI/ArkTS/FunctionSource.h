// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_PRIVATE_SEMA_ARKTS_FUNCTIONS_H
#define CANGJIE_PRIVATE_SEMA_ARKTS_FUNCTIONS_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "cangjie/AST/Node.h"
#include "Utils/TypeMapper.h"

namespace Cangjie {
class DiagnosticEngine;
} // namespace Cangjie

// Private Sema implementation surface, also used by compiler unit tests.
namespace Cangjie::Native::FFI::ArkTS {
// Canonical value of an omitted 'module' option: the Cangjie package name
// of the declaration, verbatim. Kept as a named function (not inlined at
// the call sites) so lowering and the unit tests share the single spelling
// of the rule.
// Example: package "entry" (no module option) -> requireArkModule("entry").
std::string ArkTSDefaultModule(const std::string& package);
std::string ArkTSScalarName(Ptr<AST::Ty> ty);

// Why an option set is refused. The kinds are kept apart so every caller can
// pick the matching message: a non-string value, an unknown option name and a
// name repeated inside one layer are different user mistakes (a repeated key
// is fully spelled and legal on its own).
enum class ArkTSOptionProblem {
    NONE,
    NON_LITERAL,
    UNKNOWN_NAME,
    DUPLICATE,
    ILL_FORMED_NAMESPACE,
};
struct ArkTSOptionStatus {
    ArkTSOptionProblem problem{ArkTSOptionProblem::NONE};
    // Offending option name for UNKNOWN_NAME and DUPLICATE.
    std::string option;
    // Offending argument expression, when the check has one to point at.
    Ptr<AST::Node> at{nullptr};
};

// One @ArkTS annotation is one option layer. A layer is valid when every
// argument is a string literal named 'module', 'namespace' or 'foreignName'
// and no name repeats inside the layer (repetition is rejected even for equal
// values). The annotation and its argument nodes are only read.
// Example: @ArkTS[module: "a"] -> VALID; @ArkTS[module: 1] -> NON_LITERAL;
//          @ArkTS[bogus: "a"] -> UNKNOWN_NAME;
//          @ArkTS[module: "a", module: "a"] -> DUPLICATE.
ArkTSOptionStatus CheckArkTSOptionLayer(const AST::Annotation& annotation);

// Folds every @ArkTS layer of @p fd into @p options (later layers override
// earlier inherited defaults) and validates the layer shape plus the effective
// namespace. On refusal, when @p status is not null it receives the problem
// kind and the offending option name/node.
bool ArkTSFunctionOptions(const AST::FuncDecl& fd, std::unordered_map<std::string, std::string>& options,
    ArkTSOptionStatus* status = nullptr);
// A namespace value is legal when it is empty or has no empty leading,
// trailing or inner segment; "a.b" and "a" are legal, ".a", "a." and "a..b"
// are not. The value is only read.
bool ArkTSNamespaceValid(const std::string& ns);
// An explicitly written option value is empty when it contains no non-space
// character. 'module', 'namespace' and 'foreignName' reject such a value:
// an empty module reaches requireArkModule(""), an empty foreignName reaches
// callMethod(""), and an empty namespace is an empty path segment. The
// omitted option (no layer at all) is different and keeps its default.
bool ArkTSOptionValueEmpty(const std::string& value);

// Whether the parser already rejected this parameter spelling (a missing
// identifier or ':'). Such a parameter is malformed before any signature rule
// runs, so the rules that only refine a legal signature must not report it a
// second time.
// Example: 'func f(bad:)' -> the parsed parameter carries HAS_BROKEN and
// IS_BROKEN -> true; a well-formed parameter -> false.
bool ArkTSParamIsBroken(const AST::FuncParam& param);

// Whether any parameter list of @p body carries @RestParam. A function with a
// rest parameter cannot declare named parameters; the ArkTS parser reports
// that mix once through parse_rest_param_function_has_named_params, so the
// named-parameter default rule skips such a signature instead of restating the
// same mistake. The scan covers every list, matching the rule's wording, not
// only the first list the parser inspects for placement.
bool ArkTSFuncBodyHasRestParam(const AST::FuncBody& body);

// Whether @p member hangs off an @ArkTS foreign class-like. A member carries
// neither marker on itself: FOREIGN is propagated by Sema and the @ArkTS
// annotation stays on the enclosing declaration, so the owner is consulted.
// Sema-side spelling of the parser's IsArkTSForeignClassLikeMember rule
// (Parse/ParseDecl.cpp), which is private to that translation unit.
bool ArkTSForeignClassLikeMember(const AST::Decl& member);

// Every declaration that carries @ArkTS contributes the same option shape
// contract, but only an @ArkTS foreign function is folded into a binding
// (ArkTSFunctionOptions). Validate the declarations the fold never reaches -
// classes, interfaces, variables, parameters, and any function that is not a
// lowered wrapper - so an unknown name, a non-literal value, a name repeated
// inside one layer or an ill-formed namespace is reported instead of silently
// accepted. A lowered wrapper is skipped so one mistake yields one diagnostic.
// Bodyless members of an @ArkTS foreign class/interface are additionally held
// to the named-parameter default rule: each `x!:` without an assignment is
// reported and the member is marked IS_BROKEN, because no generated binding
// can distinguish an omitted named argument from an explicit value. Ordinary
// declaration options leave the AST unmarked.
void ArkTSDeclOptionsDiagnose(AST::Package& pkg, DiagnosticEngine& diag);

std::string ArkTSForeignLocalPrefix(const std::vector<Ptr<AST::FuncParam>>& params);

// The receiver shape of a generated wrapper. MODULE is the default: the
// module object reached through ctx.requireArkModule(<module placeholder>).
// GLOBAL is the arkts.lang rule: a declaration in arkts.lang (or a
// subpackage) without an explicit module option is a TS-preset declaration,
// so its wrapper takes the context's global JS object (ctx.global) as the
// receiver and never emits requireArkModule("arkts.lang"). This is a
// language property of that one package, not a fallback for an unmappable
// package name, and must not be extended to other packages (arkts.ohos
// included); an explicit module option always selects MODULE.
enum class ArkTSReceiverKind { MODULE, GLOBAL };

// ---- Placeholder and generated-name contract (single-sourced) ----
// Every string below is shared between the wrapper generator
// (Utils/FunctionSource.cpp) and the rebinding walker
// (BeforeBodyTypeCheck/LowerFunctions.cpp): the generator emits the
// placeholder into the wrapper text and the walker looks up the very same
// composition when it rebinds literals and parameter references as AST
// values. Neither side may spell these strings out again; the two composed
// placeholders are built from the prefix constants only, as
// "__namespace_<i>__" for namespace segments and "__argument_<i>" for
// parameter references (size, indexing, value).
// Compiler-owned prefix of every generated declaration and wrapper local:
// the entry callback/registration names (PrepareTypeCheck), the local-name
// selection (ArkTSForeignLocalPrefix) and the support-declaration selection
// in lowering. Single-sourced here so generation and rebinding can never
// drift to different spellings.
constexpr const char* ARKTS_GENERATED_PREFIX = "__cj_arkts_";
constexpr const char* ARKTS_PLACEHOLDER_NAME = "__name__";
constexpr const char* ARKTS_PLACEHOLDER_MODULE = "__module__";
constexpr const char* ARKTS_PLACEHOLDER_NAMESPACE = "__namespace_";
constexpr const char* ARKTS_PLACEHOLDER_ARGUMENT = "__argument_";
constexpr const char* ARKTS_WRAPPER_ENTRY = "__generated";
constexpr const char* ARKTS_CONTEXT_CURRENT = "__cj_arkts_current";
// Package short names bound by the imports the compiler injects into every
// file that carries an @ArkTS annotation (ImportManager::AddArkTSInteropImports).
// Generated code spells the interop surface as <package>.<name>; the lowering
// walker pins these base references to the imported PackageDecl, so a
// same-named user declaration cannot capture them.
constexpr const char* ARKTS_INTEROP_PACKAGE = "ark_interop";
constexpr const char* ARKTS_CONTEXT_ABI_PACKAGE = "cj_arkts_context_v1";

// Rest-index sentinel: "this signature has no rest parameter" - one
// spelling shared by both sides of the wrapper contract, with the
// generator (Utils/FunctionSource.cpp) selecting the plain form for
// restIndex == ARKTS_NO_REST and the lowering walker (BeforeBodyTypeCheck/
// LowerFunctions.cpp) initialising its scanned rest position with the
// same value; neither side re-derives the size_t maximum locally.
//   restIndex == ARKTS_NO_REST -> the plain call form
//   restIndex == k            -> the spread form, call array sized
//                                "1 + __argument_k.size"
constexpr size_t ARKTS_NO_REST = SIZE_MAX;

// Optional-parameter run sentinel: only a named parameter with a default
// value (`x!: Option<T> = None`) is an ArkTS optional parameter, because only
// it may be omitted by the caller. Sema rejects a named parameter without a
// default before generation (user rule, 2026-11-05), so a production wrapper
// never sees `x!: Option<T>` without `= None`; a positional `Option<T>` (the
// with-rest/open form) is a required nullable argument and is always
// forwarded. The production path (BeforeBodyTypeCheck/LowerFunctions.cpp)
// must pass the real run computed from the parameter spelling, because the
// generator sees only type descriptions and cannot distinguish
// `x!: Option<T> = None` from a named `Option<T>` without a default.
// ARKTS_DERIVE_OPTIONAL therefore exists only for direct generator tests: it
// derives the run from the trailing OPTIONAL descriptions and must never be
// used unless the caller knows every trailing OPTIONAL is defaulted (i.e. in
// a hand-built description test).
constexpr size_t ARKTS_DERIVE_OPTIONAL = SIZE_MAX;

// Description-driven wrapper generation over the full ArkTS-compatible
// type set: the name, module and namespace placeholders and the argument
// references follow the ARKTS_PLACEHOLDER_* contract above and are
// rebound as AST values by the lowering walker; the receiver follows
// ArkTSReceiverKind. Returns an empty string when a parameter or the
// result is unsupported.
// ArkTS optional parameters (named `x!: Option<T'> = None`) are forwarded up
// to the last Some value: a None before it is passed as ctx.undefined() and
// every optional position after it is omitted. Required positions and
// positional `Option<T'>` positions (the with-rest/open form) are always
// forwarded explicitly; a named parameter without a default never reaches the
// generator because Sema rejects it first.
// Before: "@ArkTS public foreign func ping(value: Int32): Int32"
// After:  "let __locals_module = __locals_context.requireArkModule(\"__module__\")"
//         ... "let __locals_result = __locals_module.callMethod(\"__name__\", ...)"
//         (placeholders unrebound; see Utils/FunctionSource.cpp for the shape)
// optionalStart is the first parameter of the trailing run of ArkTS optional
// parameters (`x!: Option<T> = None`), or types.size() when there is none;
// ARKTS_DERIVE_OPTIONAL derives it from the trailing OPTIONAL descriptions
// (the generator-only entry point used by unit tests).
std::string ArkTSForeignWrapperSource(const std::vector<ArkTSTypeDesc>& types, const ArkTSTypeDesc& resultType,
    size_t namespaceDepth, const std::string& prefix, ArkTSReceiverKind receiver = ArkTSReceiverKind::MODULE,
    size_t optionalStart = ARKTS_DERIVE_OPTIONAL);
// Rest-parameter form: restIndex is the position of the @RestParam
// parameter inside types; it must be the last entry and carry ARRAY_COPY
// (Array<T'>), every element becoming one individual callMethod argument,
// referenced by size and indexing (argument_k.size, argument_k[i]) under
// the same rebinding contract. An ArkTSArray<T'> rest position classifies
// as unsupported (the reference form is deferred) and is rejected by the
// Sema rest diagnostic before the generator runs. Returns an empty string
// when the rest position or any other position is unsupported.
// Before: func sum(base: Int32, @RestParam extras: Array<Int32>): Int32
// After:  rest buffer "Array<ark_interop.JSValue>(__argument_1.size)", call array sized "1 + __argument_1.size".
std::string ArkTSForeignWrapperSource(const std::vector<ArkTSTypeDesc>& types, const ArkTSTypeDesc& resultType,
    size_t namespaceDepth, const std::string& prefix, size_t restIndex,
    ArkTSReceiverKind receiver = ArkTSReceiverKind::MODULE, size_t optionalStart = ARKTS_DERIVE_OPTIONAL);
} // namespace Cangjie::Native::FFI::ArkTS
#endif
