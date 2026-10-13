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
class ImportManager;
enum class DiagKindRefactor : unsigned;
namespace AST {
struct Package;
}
} // namespace Cangjie

// Private Sema implementation surface, also used by compiler unit tests.
namespace Cangjie::Native::FFI::ArkTS {
// Canonical value of an omitted 'module' option: the Cangjie package name
// of the declaration, verbatim. Kept as a named function (not inlined at
// the call sites) so lowering and the unit tests share the single spelling
// of the rule.
// Example: package "entry" (no module option) -> requireArkModule("entry").
std::string ArkTSDefaultModule(const std::string& package);

// Resolve the compiler-owned support names of one generated root (the
// package-qualified interop surface, BigInt, Exception, Option and the
// conversion exception) to declarations visible through the owner file's
// imports. A same-named user declaration never captures them; a missing import
// is diagnosed and the root is refused. Accepts an already-bound reference.
// @p diagnosticName, when given, is the user-facing name the missing-support
// diagnostic names instead of the owner identifier: a generated class member
// carries an internal spelling (the wrapping constructor or the 'get'/'set'
// accessor), so the caller passes the class or property name the user wrote.
bool BindArkTSSupportReferences(AST::Decl& owner, AST::Node& root, ImportManager& imports, DiagnosticEngine& diag,
    std::unordered_map<Ptr<const AST::Node>, Ptr<AST::Decl>>& supportRefs,
    const std::string& diagnosticName = {});
std::string ArkTSScalarName(Ptr<AST::Ty> ty);

// Why an option set is refused. The kinds are kept apart so every caller can
// pick the matching message: a non-string value, an unknown option name and
// a name repeated inside one layer are different user mistakes (a repeated key
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

// Option fold for any declaration; classes carry the one option layer that
// its members inherit. On refusal, when @p status is not null it receives the
// problem kind and the offending option name/node.
bool ArkTSDeclOptions(const AST::Decl& decl, std::unordered_map<std::string, std::string>& options,
    ArkTSOptionStatus* status = nullptr);
// Report one option-fold failure with the problem kind the fold assigned and
// the offending argument as the anchor (falling back to the declaration's
// @ArkTS layer). Shared by the class and interface lowering paths so a
// repeated key and an ill-formed namespace keep their dedicated messages and
// positions in both.
void DiagnoseArkTSOptionProblem(DiagnosticEngine& diag, AST::Decl& decl,
    const std::unordered_map<std::string, std::string>& options, const ArkTSOptionStatus& status);
// Resolve and validate the module option of a prepared @ArkTS declaration.
// One rule for classes and interfaces: an explicitly empty string option, a
// relative or absolute path spelling, and an empty resolved module name are
// refused before any code is generated. The caller marks the declaration
// broken because the diagnostic kind differs per declaration kind.
bool ResolveArkTSModule(AST::Decl& decl, const std::unordered_map<std::string, std::string>& options,
    const AST::Package& pkg, DiagnosticEngine& diag, DiagKindRefactor invalidKind, std::string& module);

// A source @ArkTS foreign class that has not been prepared yet: annotated, not
// generic, and without the wrapper marker. FOREIGN alone no longer separates
// the states, because preparation keeps it. Generic declarations keep their
// external form (the stdlib presets) and are never lowered into wrappers.
bool IsArkTSForeignClass(const AST::ClassDecl& cd);
// Prepared wrapper class; also true for an imported wrapper, whose attribute
// survives the CJO round trip.
bool IsArkTSClassWrapper(const AST::Decl& decl);
// Whether the declaration carries the @WriteOnly annotation.
bool ArkTSWriteOnlyProp(const AST::Decl& decl);
// The property @p usage resolves to when it is a member of a prepared wrapper
// class, else nullptr. This is the cheap gate the shared reference-check pass
// applies before routing a usage to CheckArkTSWriteOnlyRead: it resolves the
// target and tests the wrapper-owner attribute only, so declarations of other
// languages never reach the annotation scan.
Ptr<AST::PropDecl> ArkTSWriteOnlyTargetProp(const AST::Node& usage);
// Reject a source read of a write-only property of a prepared wrapper class.
// The shared reference-check pass routes the usage here only after
// ArkTSWriteOnlyTargetProp matched; the ArkTS rule (annotation scan and
// diagnostic) lives in this stage.
void CheckArkTSWriteOnlyRead(AST::Node& usage, DiagnosticEngine& diag);
// An @ArkTS declaration, or a member of one: the declaration itself or its
// enclosing class/interface carries the annotation (or its AST attribute).
// Such a declaration uses the foreign spelling but not the C ABI, so the
// shared C-interop rules must not treat it as a C declaration.
bool IsArkTSDecl(const AST::Decl& decl);
// A declaration usable as an ArkTS object reference: a prepared foreign class
// or a prepared foreign interface.
bool IsArkTSReferenceDecl(const AST::Decl& decl);
// A prepared foreign interface: an interface declaration carrying the wrapper
// attribute set by interface preparation. The generated implementation class
// carries the same attribute, so callers must keep this predicate (which also
// checks the declaration kind) in front of any interface-specific rule.
bool IsArkTSPreparedInterface(const AST::Decl& decl);
// A source @ArkTS foreign interface that has not been prepared yet: annotated,
// an interface declaration, not generic, and without the wrapper marker.
bool IsArkTSForeignInterface(const AST::InterfaceDecl& id);
// Compiler-generated implementation class of one prepared foreign interface.
// The interface and its implementation class share the wrapper attribute; the
// class declaration kind distinguishes the implementation.
bool IsArkTSInterfaceImpl(const AST::Decl& decl);
// Deterministic generated name of the implementation class. The suffix is
// unspellable in source identifiers, so a user declaration cannot collide.
// Example: interface IPerson -> "IPerson$arkts_impl".
std::string ArkTSInterfaceImplName(const AST::Decl& iface);
// Resolve the implementation class of a prepared foreign interface. The
// current package is searched in its AST; an imported interface is resolved
// through the package CJO by the deterministic name.
Ptr<AST::ClassDecl> ArkTSInterfaceImpl(const AST::Decl& iface, ImportManager* imports);
// One @ArkTS annotation is one option layer. A layer is valid when every
// argument is a string literal named 'module', 'namespace' or 'foreignName'
// and no name repeats inside the layer (repetition is rejected even for equal
// values). The annotation and its argument nodes are only read.
// Example: @ArkTS[module: "a"] -> VALID; @ArkTS[module: 1] -> NON_LITERAL;
//          @ArkTS[bogus: "a"] -> UNKNOWN_NAME;
//          @ArkTS[module: "a", module: "a"] -> DUPLICATE.
ArkTSOptionStatus CheckArkTSOptionLayer(const AST::Annotation& annotation);

// Whether a declaration carries the @ArkTS option globalVar: true. A foreign
// func with this option maps an ArkTS global variable and is lowered to a
// getter that performs a module-property read instead of the callMethod
// wrapper; on any other declaration the option is reported as misplaced.
bool IsGlobalVar(const AST::Decl& decl);

// A namespace value is legal when it is empty or has no empty leading,
// trailing or inner segment; "a.b" and "a" are legal, ".a", "a." and "a..b"
// are not. The value is only read.
bool ArkTSNamespaceValid(const std::string& ns);
// Segments of a validated namespace value, split on '.'; an empty value
// yields no segments. Lowering builds one namespace placeholder per segment
// and the generator walks the same count, so the function and class member
// paths share this single split spelling.
std::vector<std::string> ArkTSNamespaceSegments(const std::string& ns);
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

// Whether any parameter list of @p body carries @RestParam. A function with
// a rest parameter cannot declare named parameters; the ArkTS parser reports
// that mix once through parse_rest_param_function_has_named_params, so the
// named-parameter default rule skips such a signature instead of restating the
// same mistake. The scan covers every list, matching the rule's wording, not
// only the first list the parser inspects for placement.
bool ArkTSFuncBodyHasRestParam(const AST::FuncBody& body);

// The @RestParam position among the first @p scanned entries of @p params
// (the caller fixes which parameter lists are in scope: a lowered function
// scans its first list only, because the parse-time rest rule lives there,
// while a class member scans every flattened position). A named parameter
// never counts, matching the parser's rest contract. ARKTS_NO_REST when no
// scanned position is annotated.
size_t ArkTSRestPosition(const std::vector<Ptr<AST::FuncParam>>& params, size_t scanned);

// Whether @p member hangs off an @ArkTS foreign class-like. A member carries
// neither marker on itself: the @ArkTS annotation stays on the enclosing
// declaration, so the owner is consulted. Preparation marks the class
// ARKTS_CLASS_WRAPPER without removing FOREIGN, so both the external and the
// prepared form identify the owner. Sema-side spelling of the parser's
// IsArkTSForeignClassLikeMember rule (Parse/ParseDecl.cpp), which is private to
// that translation unit.
bool ArkTSForeignClassLikeMember(const AST::Decl& member);

// Every declaration that carries @ArkTS contributes the same option shape
// contract, but only an @ArkTS foreign function is folded into a binding
// (ArkTSDeclOptions). Validate the declarations the fold never reaches -
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

// Whether a 'module' option value is a relative or OS-absolute file path
// spelling; shared by the function and class wrapper paths so both reject the
// same shapes.
bool ArkTSModulePathLike(const std::string& module);

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

// The receiver of one declaration under the rule above: MODULE unless the
// module option is omitted and the resolved module is arkts.lang (or a
// subpackage of it). Shared by the function and class member lowering paths,
// so the one receiver exception cannot drift between them.
ArkTSReceiverKind ArkTSReceiverFor(bool explicitModule, const std::string& module);

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
constexpr const char* ARKTS_PLACEHOLDER_CLASS = "__class_name__";
constexpr const char* ARKTS_PLACEHOLDER_MEMBER = "__member_name__";
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
// classRefs receives every foreign class declaration a reference position
// mentions: the generator emits a placeholder spelling and records the
// declaration here for the lowering walker to bind. A signature containing a
// REFERENCE position requires a non-null map; without it the generator refuses
// the whole wrapper instead of emitting an unbound placeholder, so the map may
// be null only for reference-free signatures (the generator-only test entry).
std::string ArkTSForeignWrapperSource(const std::vector<ArkTSTypeDesc>& types, const ArkTSTypeDesc& resultType,
    size_t namespaceDepth, const std::string& prefix, ArkTSReceiverKind receiver = ArkTSReceiverKind::MODULE,
    size_t optionalStart = ARKTS_DERIVE_OPTIONAL,
    std::unordered_map<std::string, Ptr<AST::Decl>>* classRefs = nullptr);
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
    ArkTSReceiverKind receiver = ArkTSReceiverKind::MODULE, size_t optionalStart = ARKTS_DERIVE_OPTIONAL,
    std::unordered_map<std::string, Ptr<AST::Decl>>* classRefs = nullptr);

// ---- Foreign class member generation ----
// Compiler-owned member names of a lowered foreign class. A class owns one
// stored reference, one accessor and one public wrapping constructor that
// recovers a Cangjie wrapper from an ArkTS value. `__cj_arkts_self` is the
// placeholder the lowering walker rebinds to `this`; class names never enter
// generated code positions - a reference value position emits
// `__cj_arkts_class_<n>`, a reference type position emits
// `__cj_arkts_type_<n>`, and both record their declaration in classRefs for
// the lowering walker to bind. Diagnostic message strings may still spell the
// class identifier through ArkTSTypeDescName.
constexpr const char* ARKTS_SELF = "__cj_arkts_self";
constexpr const char* ARKTS_CLASS_REF_FIELD = "__cj_arkts_ref";
constexpr const char* ARKTS_CLASS_REF_ACCESSOR = "__cj_arkts_ref_value";
constexpr const char* ARKTS_CLASS_CTOR_PARAM = "__cj_arkts_ref";

enum class ArkTSMemberReceiver { INSTANCE, STATIC };

// The body shape of a generated constructor. DIRECT is the root wrapper form:
// the construction statements run in the constructor and the new object
// reference is assigned to the class's own reference field. SUPER_THUNK is the
// child wrapper form (the class has a prepared wrapper parent): the same
// statements move inside a thunk literal passed to `super(..., ())`, whose
// value the parent's thunk-taking wrapping constructor stores, because Cangjie
// requires super(...) to be the constructor's first expression, so a
// multi-statement construction can only reach it as one thunk value. Either
// way the ArkTS object is constructed exactly once, by the class's own
// constructor; the parent side never constructs again.
enum class ArkTSConstructorForm { DIRECT, SUPER_THUNK };

// Instance/static member function body. Instance calls run on the stored
// object reference; static calls resolve the class object through the module
// receiver. Returns "" for an unsupported position.
// optionalStart is the first parameter of the trailing run of ArkTS optional
// parameters (`x!: Option<T> = None`), or types.size() when there is none; the
// caller computes it from the written spelling exactly as the function path
// does. Every position up to the last Some is forwarded (a None in that range
// becomes ctx.undefined()) and every optional after the last Some is omitted.
// Body pseudocode:
//   ctx = current(); recv = this.ref() or module/namespace getProperty(class)
//   arg_i = toJS(argument_i); result = recv.callMethod(name, [arg_i...])
//            (static: recv.getProperty(name).asFunction().call([...], thisArg: recv))
//   return fromJS(result)   // a Nothing result throws instead
std::string ArkTSClassMethodSource(const std::vector<ArkTSTypeDesc>& types, const ArkTSTypeDesc& resultType,
    size_t namespaceDepth, const std::string& prefix, size_t restIndex, ArkTSMemberReceiver memberReceiver,
    ArkTSReceiverKind receiver, size_t optionalStart,
    std::unordered_map<std::string, Ptr<AST::Decl>>& classRefs);

// Constructor body: resolves the ArkTS class, constructs it with the converted
// arguments and stores the resulting object reference on the receiver.
// optionalStart has the same meaning as in ArkTSClassMethodSource: the
// constructor forwards the trailing optional run exactly like a member call.
// The form selects the wrapper shape per ArkTSConstructorForm: DIRECT closes
// with `__cj_arkts_self.__cj_arkts_ref = object.asObject()`; SUPER_THUNK wraps
// the whole body in `super({ => ... object.asObject() }, ())` for a class
// whose parent is a prepared wrapper (the reference field lives on the root
// wrapper, so a child constructor forwards the new object through the parent's
// thunk-taking wrapping constructor instead of assigning a field it does not
// declare).
// Body pseudocode:
//   ctx = current(); clazz = module/namespace getProperty(class).asClass()
//   object = clazz.new([toJS(argument_i)...]); this.ref() = object.asObject()
std::string ArkTSClassConstructorSource(const std::vector<ArkTSTypeDesc>& types, size_t namespaceDepth,
    const std::string& prefix, size_t restIndex, ArkTSReceiverKind receiver, size_t optionalStart,
    std::unordered_map<std::string, Ptr<AST::Decl>>& classRefs,
    ArkTSConstructorForm form = ArkTSConstructorForm::DIRECT);

// Property accessor body. The getter returns the converted property value;
// the setter writes the converted value back. Both work on the stored object
// reference (instance) or the resolved class object (static).
// Getter pseudocode:  return fromJS(recv.getProperty(name))
// Setter pseudocode:  recv.setProperty(name, toJS(argument_0))
// where recv is this.ref() (instance) or the resolved class value (static).
std::string ArkTSClassPropertySource(const ArkTSTypeDesc& valueType, bool setter,
    ArkTSMemberReceiver memberReceiver, size_t namespaceDepth, const std::string& prefix, ArkTSReceiverKind receiver,
    std::unordered_map<std::string, Ptr<AST::Decl>>& classRefs);

// ---- Foreign interface member generation ----
// Call-signature default implementation:
//   self = this.__cj_arkts_ref_value(); self.toJSValue().asFunction()
//       .call([toJS(argument_i)...], thisArg: self.toJSValue())
std::string ArkTSInterfaceCallSource(const std::vector<ArkTSTypeDesc>& types, const ArkTSTypeDesc& resultType,
    const std::string& prefix, std::unordered_map<std::string, Ptr<AST::Decl>>& classRefs);

// Index-signature default implementation. keyType is String or Float64 (a
// finite integer converted to its property-key spelling). The getter returns
// Option<T>; the setter writes through setProperty.
//   get: value = self.getProperty(key); return None or Some(fromJS(value))
//   set: self.setProperty(key, toJS(argument_1))
std::string ArkTSInterfaceIndexSource(const ArkTSTypeDesc& keyType, const ArkTSTypeDesc& valueType, bool setter,
    const std::string& prefix, std::unordered_map<std::string, Ptr<AST::Decl>>& classRefs);
} // namespace Cangjie::Native::FFI::ArkTS
#endif
