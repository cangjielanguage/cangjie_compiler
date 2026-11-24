// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "TypeCheckerImpl.h"

#include <algorithm>
#include <functional>
#include <iterator>
#include <set>
#include <tuple>
#include <unordered_map>

#include "../ArkTSInterop.h"
#include "../FunctionSource.h"
#include "../Utils/ASTFactory.h"
#include "Collector.h"
#include "PlaceholderBinding.h"
#include "cangjie/AST/Utils.h"

using namespace Cangjie;
using namespace Cangjie::AST;
using namespace Cangjie::Native::FFI::ArkTS;

namespace {
// Reporting state of one support-reference bind: the user-facing name and the
// per-entry deduplication set, so one missing package yields exactly one
// diagnostic for the user entry.
struct ArkTSSupportReport {
    DiagnosticEngine& diag;
    Decl& owner;
    bool generatedSupport;
    const std::string& diagnosticName;
    std::set<std::tuple<Ptr<File>, Position, Position>>* reportedRanges;

    void Missing() const
    {
        if (generatedSupport && reportedRanges != nullptr &&
            !reportedRanges->emplace(owner.curFile, owner.begin, owner.end).second) {
            return; // The entry's other generated declaration already reported it.
        }
        const auto name = diagnosticName.empty() ? owner.identifier.Val() : diagnosticName;
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_support_unavailable, owner, name);
    }
};

// Resolve the package-qualified base reference of one generated name through
// the injected import. Main's import model does not expose a package import's
// PackageDecl through GetImportedDeclsByName (a package import binds the
// package name, not a member), so the injected import is checked for presence
// and the decl is taken from the package manager.
Ptr<Decl> ResolveArkTSSupportPackage(Decl& owner, ImportManager& imports, const std::string& package)
{
    const auto hasImport = std::any_of(owner.curFile->imports.begin(), owner.curFile->imports.end(),
        [&package](const auto& import) {
            return import != nullptr && import->content.GetImportedPackageName() == package;
        });
    return hasImport ? imports.GetPackageDecl(package) : nullptr;
}

// True when @p decl is the support declaration @p name must bind: the exact
// package, or one of the facade re-exports of the interop exception carriers.
bool IsArkTSSupportDecl(const std::string& name, const std::string& package, const Decl& decl)
{
    if (decl.identifier != name) {
        return false;
    }
    if (decl.fullPackageName == package) {
        return true;
    }
    // The interop library's exception carrier is re-exported by the facade
    // under its original package name; both spellings denote the one
    // declaration the call translation must catch.
    if (name == Native::FFI::ArkTS::ARKTS_BUSINESS_EXCEPTION &&
        decl.fullPackageName == Native::FFI::ArkTS::ARKTS_BUSINESS_EXCEPTION_PACKAGE) {
        return true;
    }
    // The runtime exception is hosted by the call-scope package, which detects
    // the missing scope, and re-exported by the facade.
    return name == Native::FFI::ArkTS::ARKTS_RUNTIME_EXCEPTION && decl.fullPackageName == ARKTS_CONTEXT_ABI_PACKAGE;
}

// The visible declaration @p name binds in @p package, or null when it is
// missing or carries no valid type.
Ptr<Decl> FindArkTSSupportDecl(ImportManager& imports, File& file, const std::string& name,
    const std::string& package)
{
    const auto visible = imports.GetImportedDeclsByName(file, name);
    auto target = std::find_if(visible.begin(), visible.end(), [&name, &package](auto decl) {
        return IsArkTSSupportDecl(name, package, *decl);
    });
    if (target == visible.end() || !Ty::IsTyCorrect((*target)->GetTy())) {
        return nullptr;
    }
    return *target;
}

// Bind one package-qualified support reference (`ark_interop`,
// `cj_arkts_context_v1`). Returns false when @p name is not a support package.
bool BindArkTSSupportPackageReference(Node& node, Reference& ref, Decl& owner, ImportManager& imports,
    const std::string& name, ArkTSSupportReport& report, std::vector<std::pair<Ptr<Node>, Ptr<Decl>>>& bound,
    bool& valid)
{
    static const std::unordered_map<std::string, std::string> supportPackages{
        {ARKTS_INTEROP_PACKAGE, "ohos.ark_interop"},
        {ARKTS_CONTEXT_ABI_PACKAGE, "cj_arkts_context_v1"}};
    auto package = supportPackages.find(name);
    if (package == supportPackages.end()) {
        return false;
    }
    auto target = ResolveArkTSSupportPackage(owner, imports, package->second);
    if (target == nullptr) {
        if (valid) {
            report.Missing();
        }
        valid = false;
        return true;
    }
    ref.target = target;
    ref.targets = {target};
    // A package qualifier has no value type; the qualified node resolves its
    // member through the PackageDecl. Do not set a Ty here or the restoring
    // re-check would treat the qualifier as a type name.
    node.SetTy({TypeManager::GetInvalidTy()});
    bound.emplace_back(&node, target);
    return true;
}

// Bind the short-name support reference @p name. The stored type follows the
// declaration unless the reference is a parameterized type reference: a
// generic type reference keeps its type arguments, because presetting the
// declaration's own generic type (e.g. Option<T>) would drop the
// instantiation and make the generated annotation uninferable.
void BindArkTSSupportDeclReference(Node& node, Reference& ref, RefType* type, Decl& owner, ImportManager& imports,
    const std::string& name, ArkTSSupportReport& report, std::vector<std::pair<Ptr<Node>, Ptr<Decl>>>& bound,
    bool& valid)
{
    static const std::unordered_map<std::string, std::string> packages{{"BigInt", "std.math.numeric"},
        {"Exception", "std.core"}, {"Option", "std.core"},
        {"ExternConversionException", Native::FFI::ArkTS::ARKTS_LANGUAGE_PACKAGE},
        {Native::FFI::ArkTS::ARKTS_RUNTIME_EXCEPTION, Native::FFI::ArkTS::ARKTS_LANGUAGE_PACKAGE},
        {Native::FFI::ArkTS::ARKTS_BUSINESS_EXCEPTION, Native::FFI::ArkTS::ARKTS_LANGUAGE_PACKAGE}};
    auto found = packages.find(name);
    if (found == packages.end()) {
        return;
    }
    auto target = FindArkTSSupportDecl(imports, *owner.curFile, found->first, found->second);
    if (target == nullptr) {
        if (valid) {
            report.Missing();
        }
        valid = false;
        return;
    }
    ref.target = target;
    ref.targets = {target};
    if (!type || type->typeArguments.empty()) {
        node.SetTy(target->GetTy());
    }
    bound.emplace_back(&node, target);
}

// Bind the support references of one generated node. A reference that already
// carries a target keeps it: only the names the generator left unresolved are
// supplied here. The wrapper's parameter placeholders and the entry
// callback's call to the source entry are bound before this walk and may be
// named like a support item, so re-resolving by name would replace a
// deliberate binding.
VisitAction BindSupportReferenceNode(Node& node, Decl& owner, ImportManager& imports, ArkTSSupportReport& report,
    std::vector<std::pair<Ptr<Node>, Ptr<Decl>>>& bound, bool& valid)
{
    auto expr = DynamicCast<RefExpr*>(&node);
    auto type = DynamicCast<RefType*>(&node);
    auto ref = expr ? &expr->ref : type ? &type->ref : nullptr;
    if (!ref || ref->target) {
        return VisitAction::WALK_CHILDREN;
    }
    const auto name = ref->identifier.Val();
    if (!BindArkTSSupportPackageReference(node, *ref, owner, imports, name, report, bound, valid)) {
        BindArkTSSupportDeclReference(node, *ref, type, owner, imports, name, report, bound, valid);
    }
    return VisitAction::WALK_CHILDREN;
}

// Resolve the generated context/BigInt/Exception/Option references to the
// declarations visible through this file's imports. A same-named business
// declaration must not capture them, and a missing import stays a compilation
// error instead of an unresolved reference inside generated code.
// The interop surface itself is never written as a short name: generated code
// spells it as ark_interop.JSContext / cj_arkts_context_v1.__cj_arkts_current,
// so only the package-qualified base references are pinned here, to the
// PackageDecl bound by the compiler-injected package imports. The remaining
// short names are the packages the application imports itself (arkts.lang and
// std.core); Option is pinned to std.core because its constructors are enum
// members, so generated code spells them as Option.Some/Option.None and only
// the enum name is resolved here.
// generatedSupport is true for a COMPILER_ADD entry callback/registration: the
// owner is an internal symbol, so the diagnostic names the user entry (the
// generated declarations carry its source range) and stays on that range.
// @p reportedRanges deduplicates the pair's two declarations so one missing
// package yields exactly one diagnostic for the user entry.
bool BindSupportReferences(Decl& owner, Node& root, ImportManager& imports, DiagnosticEngine& diag,
    std::unordered_map<Ptr<const Node>, Ptr<Decl>>& supportRefs, bool generatedSupport,
    const std::string& diagnosticName, std::set<std::tuple<Ptr<File>, Position, Position>>* reportedRanges)
{
    // Only committed once every support reference is bound, so a rejected
    // wrapper (whose generated body is dropped) leaves no stale node keys.
    std::vector<std::pair<Ptr<Node>, Ptr<Decl>>> bound;
    bool valid = true;
    ArkTSSupportReport report{diag, owner, generatedSupport, diagnosticName, reportedRanges};
    Walker(&root, [&owner, &imports, &report, &bound, &valid](Ptr<Node> node) {
        return BindSupportReferenceNode(*node, owner, imports, report, bound, valid);
    }).Walk();
    if (valid) {
        for (auto& [node, target] : bound) {
            supportRefs[node] = target;
        }
    }
    return valid;
}

// One generated support entry declaration: the pair shares the source entry's
// range, so the user-facing name is resolved from it (a diagnostic never names
// the internal symbol) and the callback is marked broken when its interop
// packages are missing, so body checking does not restate the same missing
// declarations as undeclared identifiers.
void BindGeneratedArkTSSupportDecl(Decl& decl, File& file, ImportManager& imports, DiagnosticEngine& diag,
    std::unordered_map<Ptr<const Node>, Ptr<Decl>>& supportRefs,
    std::set<std::tuple<Ptr<File>, Position, Position>>& reportedRanges)
{
    std::string entryName;
    for (auto& source : file.decls) {
        if (source != nullptr && !source->TestAttr(Attribute::COMPILER_ADD) && source->begin == decl.begin &&
            source->end == decl.end) {
            entryName = source->identifier.Val();
            break;
        }
    }
    if (!BindSupportReferences(decl, decl, imports, diag, supportRefs, true, entryName, &reportedRanges)) {
        decl.EnableAttr(Attribute::IS_BROKEN);
    }
}
} // namespace

// Class member bodies bind their support names through the same pins as
// function wrappers: a member is the diagnostic owner of its own generated
// body, so the entry-pair deduplication is not needed here.
bool Cangjie::Native::FFI::ArkTS::BindArkTSSupportReferences(Decl& owner, Node& root, ImportManager& imports,
    DiagnosticEngine& diag, std::unordered_map<Ptr<const Node>, Ptr<Decl>>& supportRefs,
    const std::string& diagnosticName)
{
    return BindSupportReferences(owner, root, imports, diag, supportRefs, false, diagnosticName, nullptr);
}

namespace {
// One lowered function wrapper's analyzed signature and the values its
// generated body is built from.
struct ArkTSFunctionSignature {
    std::unordered_map<std::string, std::string> options;
    std::vector<ArkTSTypeDesc> types;
    std::vector<Ptr<FuncParam>> params;
    ArkTSTypeDesc resultType;
    std::unordered_map<std::string, Ptr<Decl>> classRefs;
    std::unordered_map<std::string, std::string> literals;
    std::string module;
    ArkTSReceiverKind receiver{ArkTSReceiverKind::MODULE};
    size_t depth{0};
    size_t restIndex{ARKTS_NO_REST};
    size_t optionalStart{0};
};

// ArkTS optional parameters are the trailing named parameters that carry a
// default value (`x!: Option<T> = None`, design §4.1.2): only such a parameter
// may be omitted by the caller. A positional Option (the required nullable
// argument of the with-rest/open form) is still always forwarded.
size_t ArkTSTrailingOptionalStart(const std::vector<Ptr<FuncParam>>& params,
    const std::vector<ArkTSTypeDesc>& types)
{
    size_t optionalStart = params.size();
    while (optionalStart > 0 && params[optionalStart - 1]->isNamedParam &&
        params[optionalStart - 1]->assignment != nullptr &&
        types[optionalStart - 1].kind == ArkTSTypeDesc::Kind::OPTIONAL) {
        --optionalStart;
    }
    return optionalStart;
}

// Report one option-fold failure. Every illegal option shape - a non-literal
// value, an unknown name, a name repeated inside one @ArkTS layer, an empty
// namespace segment - is reported with its own message and anchor.
void DiagnoseArkTSFunctionOptionProblem(DiagnosticEngine& diag, FuncDecl& fd,
    const std::unordered_map<std::string, std::string>& options, const ArkTSOptionStatus& optionStatus)
{
    if (optionStatus.problem == ArkTSOptionProblem::DUPLICATE) {
        auto& at = optionStatus.at ? *optionStatus.at : static_cast<Node&>(fd);
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_option_duplicated, at, optionStatus.option,
            fd.identifier.Val());
    } else if (optionStatus.problem == ArkTSOptionProblem::ILL_FORMED_VALUE) {
        auto& at = optionStatus.at ? *optionStatus.at : static_cast<Node&>(fd);
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_global_var_value_invalid, at);
    } else if (optionStatus.problem == ArkTSOptionProblem::ILL_FORMED_NAMESPACE) {
        auto& at = optionStatus.at ? *optionStatus.at : static_cast<Node&>(fd);
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_namespace_invalid, at,
            options.count("namespace") ? options.at("namespace") : std::string(), fd.identifier.Val());
    } else {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_option_invalid, fd, fd.identifier.Val());
    }
}

// Diagnose an unsupported return position and mark the declaration broken.
// Returns true when the wrapper must be refused.
bool RefuseArkTSFunctionReturnType(DiagnosticEngine& diag, FuncDecl& fd, const FuncBody& fb,
    const ArkTSTypeDesc& resultType)
{
    if (ArkTSTypeDescGeneratable(resultType)) {
        return false;
    }
    // A return position spelled with an external ArkTS preset type is not a
    // spelling mistake - its mapping is deferred - so it reports the dedicated
    // deferral diagnostic instead of the generic unsupported-type error.
    auto deferredName = fb.retType ? ArkTSDeferredExternalTypeName(fb.retType->DataTy()) : std::string{};
    if (!deferredName.empty()) {
        auto& deferredAt = fb.retType->begin.IsZero() ? static_cast<Node&>(fd) : static_cast<Node&>(*fb.retType);
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_external_type_deferred, deferredAt, deferredName);
        fd.EnableAttr(Attribute::IS_BROKEN);
        return true;
    }
    // An imported enum without a value table comes from a package compiled
    // with an older CJO format (its @RawValue values are absent).
    auto importedEnum = ArkTSTypeDescImportedEnumName(resultType);
    if (!importedEnum.empty()) {
        auto& at =
            fb.retType && !fb.retType->begin.IsZero() ? static_cast<Node&>(*fb.retType) : static_cast<Node&>(fd);
        auto builder = diag.DiagnoseRefactor(
            DiagKindRefactor::sema_arkts_foreign_ret_type_unsupported, at, importedEnum, fd.identifier.Val());
        builder.AddNote("recompile the enum's package with the current compiler");
        fd.EnableAttr(Attribute::IS_BROKEN);
        return true;
    }
    // Report on the written return type when it carries a position, else on
    // the whole declaration (the Java interop fallback).
    if (fb.retType && !fb.retType->begin.IsZero()) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_ret_type_unsupported, *fb.retType,
            Ty::ToString(fb.retType->DataTy()), fd.identifier.Val());
    } else {
        diag.DiagnoseRefactor(
            DiagKindRefactor::sema_arkts_foreign_ret_type_unsupported, fd, std::string(), fd.identifier.Val());
    }
    fd.EnableAttr(Attribute::IS_BROKEN);
    return true;
}

// The declaration rules on one parameter: every named parameter must carry a
// default value (the wrapper cannot tell an omitted named argument from an
// explicitly passed value), and a defaulted named parameter must spell Option.
// Returns false when the parameter is refused.
bool DiagnoseArkTSNamedParameterRules(DiagnosticEngine& diag, FuncDecl& fd, FuncParam& param, bool restParam)
{
    bool supported = true;
    // A malformed parameter, or one belonging to the already-rejected
    // rest+named shape, is not reported by this rule a second time.
    if (param.isNamedParam && param.assignment == nullptr) {
        if (!restParam && !ArkTSParamIsBroken(param)) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_named_param_without_default, param,
                param.identifier.Val(), fd.identifier.Val());
        }
        supported = false;
    }
    // This declaration rule runs before the semantic type of a bodyless
    // foreign parameter is available, so the written spelling is the contract.
    if (DiagnoseArkTSDefaultedNamedParamNotOption(diag, param, fd)) {
        supported = false;
    }
    return supported;
}

// Report one unsupported parameter position: an external preset type keeps
// the dedicated deferral diagnostic, an imported enum without a value table
// gets the legacy-CJO note, every other position the generic message.
void DiagnoseArkTSUnsupportedFunctionParameter(DiagnosticEngine& diag, FuncDecl& fd, FuncParam& param,
    const ArkTSTypeDesc& desc)
{
    auto& position = param.type ? static_cast<Node&>(*param.type) : static_cast<Node&>(param);
    auto deferredName = param.type ? ArkTSDeferredExternalTypeName(param.type->DataTy()) : std::string{};
    if (!deferredName.empty()) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_external_type_deferred, position, deferredName);
        return;
    }
    if (auto importedEnum = ArkTSTypeDescImportedEnumName(desc); !importedEnum.empty()) {
        auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_param_type_unsupported, position,
            param.identifier.Val(), fd.identifier.Val(), importedEnum);
        builder.AddNote("recompile the enum's package with the current compiler");
        return;
    }
    diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_param_type_unsupported, position,
        param.identifier.Val(), fd.identifier.Val(), param.type ? Ty::ToString(param.type->DataTy()) : std::string());
}

// Classify every parameter position of one wrapper and apply the declaration
// rules. Positions after the first failure are still classified and appended,
// so the caller keeps the same arity and one diagnostic is emitted per bad
// position. Returns false when any position is refused.
bool ClassifyArkTSFunctionParameters(DiagnosticEngine& diag, FuncDecl& fd, const FuncBody& fb,
    const std::function<ArkTSTypeDesc(Ptr<Type>)>& classify, ArkTSFunctionSignature& sig)
{
    bool supported = true;
    // A function with a rest parameter cannot declare named parameters; the
    // parser has already reported that mix, so the named-default rule stays
    // silent for this signature.
    const bool restParam = ArkTSFuncBodyHasRestParam(fb);
    for (auto& list : fb.paramLists) {
        for (auto& param : list->params) {
            if (!DiagnoseArkTSNamedParameterRules(diag, fd, *param, restParam)) {
                supported = false;
            }
            auto desc = classify(param->type.get());
            if (!ArkTSTypeDescGeneratable(desc) || ArkTSTypeDescContainsNothing(desc)) {
                DiagnoseArkTSUnsupportedFunctionParameter(diag, fd, *param, desc);
                supported = false;
            }
            sig.types.emplace_back(std::move(desc));
            sig.params.emplace_back(param.get());
        }
    }
    return supported;
}

// The diagnostic kind of an explicitly empty option value, by option name.
DiagKindRefactor ArkTSEmptyOptionKind(const std::string& optionName, DiagKindRefactor current)
{
    if (optionName == "foreignName") {
        return DiagKindRefactor::sema_arkts_foreign_name_empty;
    }
    if (optionName == "namespace") {
        return DiagKindRefactor::sema_arkts_foreign_namespace_empty;
    }
    return current;
}

// The first explicitly empty string option of the declaration, with its
// diagnostic kind, or null. Only whitespace counts as empty.
Ptr<Node> FindArkTSEmptyOptionValue(FuncDecl& fd, DiagKindRefactor& emptyKind)
{
    for (auto& annotation : fd.annotations) {
        if (annotation->kind != AnnotationKind::ARKTS) {
            continue;
        }
        for (auto& arg : annotation->args) {
            auto literal = DynamicCast<LitConstExpr*>(arg->expr.get());
            if (!literal || literal->kind != LitConstKind::STRING || !ArkTSOptionValueEmpty(literal->stringValue)) {
                continue;
            }
            emptyKind = ArkTSEmptyOptionKind(arg->name.Val(), emptyKind);
            return arg->expr.get();
        }
    }
    return nullptr;
}

// Refuse an explicitly empty option value at compile time: it would flow into
// requireArkModule(""), callMethod("") or an empty namespace segment and only
// fail at run time. The legal-specifier whitelist lives in the interop library
// and changes with its versions, so only the empty spelling is rejected.
bool DiagnoseArkTSEmptyOptionValue(DiagnosticEngine& diag, FuncDecl& fd)
{
    DiagKindRefactor emptyKind = DiagKindRefactor::sema_arkts_foreign_module_empty;
    auto emptyAt = FindArkTSEmptyOptionValue(fd, emptyKind);
    if (!emptyAt) {
        return false;
    }
    diag.DiagnoseRefactor(emptyKind, *emptyAt, fd.identifier.Val());
    fd.EnableAttr(Attribute::IS_BROKEN);
    return true;
}

// The module option's own literal when it is attached to this declaration,
// else the whole declaration (the fallback for a merged option).
Ptr<Node> FindArkTSModuleOptionNode(FuncDecl& fd, const std::unordered_map<std::string, std::string>& options)
{
    if (options.count("module") == 0) {
        return &fd;
    }
    Ptr<Node> moduleNode = &fd;
    for (auto& annotation : fd.annotations) {
        if (annotation->kind != AnnotationKind::ARKTS) {
            continue;
        }
        for (auto& arg : annotation->args) {
            if (arg->name.Val() == "module") {
                moduleNode = arg->expr.get();
            }
        }
    }
    return moduleNode;
}

// Resolve the module, receiver and generated-name literals of one wrapper. An
// explicit module is a module specifier: a leading "./"/"../", an OS-absolute
// path, a drive-absolute path, a UNC path or any backslash spelling is
// rejected; existence checking needs the ArkTS project root, which is not a
// compiler input, so only the format is rejected here. A declaration without
// an explicit module takes the Cangjie package name verbatim
// (ArkTSDefaultModule), except the arkts.lang preset, which takes the GLOBAL
// receiver. Returns false when the declaration was refused.
bool PrepareArkTSFunctionModule(DiagnosticEngine& diag, Package& pkg, FuncDecl& fd, ArkTSFunctionSignature& sig)
{
    if (DiagnoseArkTSEmptyOptionValue(diag, fd)) {
        return false;
    }
    auto moduleNode = FindArkTSModuleOptionNode(fd, sig.options);
    if (sig.options.count("module") != 0) {
        const std::string& moduleValue = sig.options.at("module");
        if (ArkTSModulePathLike(moduleValue)) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_module_path_invalid, *moduleNode, moduleValue,
                fd.identifier.Val());
            fd.EnableAttr(Attribute::IS_BROKEN);
            return false;
        }
        sig.module = moduleValue;
    } else {
        sig.module = ArkTSDefaultModule(pkg.fullPackageName);
    }
    // The only way to an empty module name now is an empty package name (a
    // degenerate compilation unit): there is no module to require, so the
    // wrapper is refused through the generic unsupported-signature diagnostic.
    if (sig.module.empty()) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_unsupported_signature, fd, fd.identifier.Val());
        fd.EnableAttr(Attribute::IS_BROKEN);
        return false;
    }
    sig.receiver = ArkTSReceiverFor(sig.options.count("module") != 0, sig.module);
    sig.literals = {
        {ARKTS_PLACEHOLDER_NAME,
            sig.options.count("foreignName") ? sig.options.at("foreignName") : fd.identifier.Val()},
        {ARKTS_PLACEHOLDER_MODULE, sig.module}};
    // Namespace segments follow the shared split spelling, so the placeholder
    // count and the generator's namespace depth agree.
    auto segments = ArkTSNamespaceSegments(sig.options.count("namespace") ? sig.options.at("namespace") : "");
    sig.depth = 0;
    for (const auto& segment : segments) {
        sig.literals[ARKTS_PLACEHOLDER_NAMESPACE + std::to_string(sig.depth++) + "__"] = segment;
    }
    return true;
}

// The rest position of the first parameter list and the trailing optional run
// of the written signature. @RestParam is a parse-time rule on the first list
// only, so lowering scans only that list; the position must carry Array<T'>
// (its element was already validated by the parameter analysis).
bool FinalizeArkTSFunctionSignature(DiagnosticEngine& diag, FuncDecl& fd, const FuncBody& fb,
    ArkTSFunctionSignature& sig)
{
    sig.restIndex = ArkTSRestPosition(sig.params, fb.paramLists.empty() ? 0 : fb.paramLists.front()->params.size());
    if (sig.restIndex != ARKTS_NO_REST && sig.types[sig.restIndex].kind != ArkTSTypeDesc::Kind::ARRAY_COPY) {
        auto& param = sig.params[sig.restIndex];
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_rest_type_unsupported, *param->type,
            param->identifier.Val(), fd.identifier.Val(), Ty::ToString(param->type->DataTy()));
        fd.EnableAttr(Attribute::IS_BROKEN);
        return false;
    }
    sig.optionalStart = ArkTSTrailingOptionalStart(sig.params, sig.types);
    return true;
}

// The wrapper body source; reference positions record their class declarations
// in sig.classRefs for the placeholder walker.
std::string BuildArkTSFunctionWrapperSource(ArkTSFunctionSignature& sig)
{
    auto prefix = ArkTSForeignLocalPrefix(sig.params);
    if (sig.restIndex == ARKTS_NO_REST) {
        return ArkTSForeignWrapperSource(sig.types, sig.resultType, sig.depth, prefix, sig.receiver, sig.optionalStart,
            &sig.classRefs);
    }
    return ArkTSForeignWrapperSource(sig.types, sig.resultType, sig.depth, prefix, sig.restIndex, sig.receiver,
        sig.optionalStart, &sig.classRefs);
}

// Rebind one argument placeholder to the parameter it denotes. Returns false
// for a reference that is not an argument placeholder.
bool RebindArkTSFunctionArgumentPlaceholder(RefExpr& ref, const FuncDecl& fd,
    const std::vector<Ptr<FuncParam>>& params)
{
    for (size_t i = 0; i < params.size(); ++i) {
        if (ref.ref.identifier == ARKTS_PLACEHOLDER_ARGUMENT + std::to_string(i)) {
            ref.ref.identifier = params[i]->identifier;
            ref.ref.target = params[i];
            ref.SetTy(params[i]->type->GetTy());
            ref.begin = fd.end;
            return true;
        }
    }
    return false;
}

// Bind a foreign class reference placeholder recorded by the generator.
void RebindArkTSFunctionClassReference(RefExpr& ref, const std::unordered_map<std::string, Ptr<Decl>>& classRefs)
{
    auto found = classRefs.find(ref.ref.identifier.Val());
    if (found == classRefs.end()) {
        return;
    }
    ref.ref.identifier = found->second->identifier;
    ref.ref.target = found->second;
    ref.ref.targets = {found->second};
    ref.SetTy(found->second->GetTy());
}

// Rebind the placeholders of one generated body node: source position,
// module/name/namespace literals, type placeholders, argument/class references
// and return-function links.
VisitAction RebindArkTSFunctionPlaceholderNode(Node& node, FuncDecl& fd, File& file,
    const std::vector<Ptr<FuncParam>>& params, const std::unordered_map<std::string, std::string>& literals,
    const std::unordered_map<std::string, Ptr<Decl>>& classRefs,
    std::unordered_map<Ptr<const Node>, Ptr<Decl>>& pendingRefs)
{
    node.curFile = &file;
    node.begin = fd.begin;
    node.end = fd.end;
    if (auto literal = DynamicCast<LitConstExpr*>(&node); literal) {
        std::string rebound;
        if (ArkTSRebindPlaceholderLiteral(literal->stringValue, literals, rebound)) {
            literal->stringValue = std::move(rebound);
        }
    }
    // Type placeholders carry a reference element in generated annotations
    // (Array<T>, Option<T>): bind the reference type and register it like a
    // support declaration, so name resolution and speculative re-checks keep
    // the class.
    if (auto refType = DynamicCast<RefType*>(&node)) {
        BindArkTSRefTypePlaceholder(*refType, classRefs, pendingRefs);
        return VisitAction::WALK_CHILDREN;
    }
    if (auto ref = DynamicCast<RefExpr*>(&node)) {
        if (!RebindArkTSFunctionArgumentPlaceholder(*ref, fd, params)) {
            RebindArkTSFunctionClassReference(*ref, classRefs);
        }
    }
    if (auto returned = DynamicCast<ReturnExpr*>(&node)) {
        returned->refFuncBody = fd.funcBody.get();
    }
    return VisitAction::WALK_CHILDREN;
}

// Rebind every placeholder of one installed generated function body.
void RebindArkTSFunctionPlaceholders(Node& body, FuncDecl& fd, File& file,
    const std::vector<Ptr<FuncParam>>& params, const std::unordered_map<std::string, std::string>& literals,
    const std::unordered_map<std::string, Ptr<Decl>>& classRefs,
    std::unordered_map<Ptr<const Node>, Ptr<Decl>>& pendingRefs)
{
    Walker(&body, [&fd, &file, &params, &literals, &classRefs, &pendingRefs](Ptr<Node> node) {
        return RebindArkTSFunctionPlaceholderNode(*node, fd, file, params, literals, classRefs, pendingRefs);
    }).Walk();
}

// Merge the retained type placeholders into the long-lived support map, only
// after the body passed BindArkTSSupportReferences.
void MergeArkTSFunctionSupportReferences(std::unordered_map<Ptr<const Node>, Ptr<Decl>>& supportRefs,
    const std::unordered_map<Ptr<const Node>, Ptr<Decl>>& pendingRefs)
{
    for (auto& [node, target] : pendingRefs) {
        supportRefs[node] = target;
    }
}

// Build the symbol table of one installed generated body: locals share the
// parameter scope (Collector::CollectFuncBody) and are registered in the
// checker's declaration names, then the previous scope state is restored.
void BuildArkTSFunctionBodySymbols(ASTContext& ctx, ScopeManager& scopeManager, FuncBody& fb,
    const std::vector<Ptr<FuncParam>>& params, bool buildTrie)
{
    auto oldScope = ctx.currentScopeName;
    auto oldLevel = ctx.currentScopeLevel;
    ctx.currentScopeName = params.empty() ? fb.scopeName : params.front()->scopeName;
    if (params.empty()) {
        auto gate = ctx.currentScopeName.find_last_of(ScopeManagerApi::childScopeNameSplit);
        if (gate != std::string::npos) {
            ctx.currentScopeName[gate] = ScopeManagerApi::scopeNameSplit;
        }
    }
    ctx.currentScopeLevel = fb.scopeLevel + 1;
    Collector collector(scopeManager);
    auto previous = ctx.symbolTable.empty() ? ctx.symbolTable.end() : std::prev(ctx.symbolTable.end());
    for (auto& statement : fb.body->body) {
        collector.BuildSymbolTable(ctx, statement.get(), buildTrie);
    }
    auto first = previous == ctx.symbolTable.end() ? ctx.symbolTable.begin() : std::next(previous);
    for (auto it = first; it != ctx.symbolTable.end(); ++it) {
        auto& symbol = *it;
        if (auto local = DynamicCast<Decl*>(symbol->node)) {
            ctx.AddDeclName({symbol->name, ScopeManagerApi::GetScopeNameWithoutTail(symbol->scopeName)}, *local);
        }
    }
    ctx.currentScopeName = oldScope;
    ctx.currentScopeLevel = oldLevel;
}

// Parse the generated wrapper into the FuncBody and rebind its placeholders,
// support references and scope names. A generated skeleton that fails to
// parse already reported its own diagnostics.
bool InstallArkTSFunctionBody(ASTContext& ctx, DiagnosticEngine& diag, ImportManager& imports,
    ScopeManager& scopeManager, SourceManager& sourceManager, bool buildTrie, File& file, FuncDecl& fd,
    ArkTSFunctionSignature& sig, std::unordered_map<Ptr<const Node>, Ptr<Decl>>& supportRefs)
{
    auto code = BuildArkTSFunctionWrapperSource(sig);
    // A generator refusal must never silently drop the wrapper: the remaining
    // refusals are rest placements the parse checks already reject and
    // hand-built descriptions, so the message stays generic.
    if (code.empty()) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_unsupported_signature, fd, fd.identifier.Val());
        fd.EnableAttr(Attribute::IS_BROKEN);
        return false;
    }
    auto& fb = *fd.funcBody;
    Native::FFI::ArkTS::ASTFactory factory(diag, sourceManager);
    fb.body = factory.CreateForeignBody(fd, code);
    if (!fb.body) {
        return false;
    }
    std::unordered_map<Ptr<const Node>, Ptr<Decl>> pendingRefs;
    RebindArkTSFunctionPlaceholders(*fb.body, fd, file, sig.params, sig.literals, sig.classRefs, pendingRefs);
    if (!BindSupportReferences(fd, *fb.body, imports, diag, pendingRefs, false, fd.identifier.Val(), nullptr)) {
        fb.body.reset();
        fd.EnableAttr(Attribute::IS_BROKEN);
        fb.EnableAttr(Attribute::IS_BROKEN);
        return false;
    }
    MergeArkTSFunctionSupportReferences(supportRefs, pendingRefs);
    BuildArkTSFunctionBodySymbols(ctx, scopeManager, fb, sig.params, buildTrie);
    return true;
}

// One marked wrapper: analyze (options, return, parameters), prepare (module
// and generated names), finalize (rest and optional run) and install. Every
// refusal returns without generating the remaining pieces of this wrapper.
void LowerArkTSFunctionDecl(ASTContext& ctx, DiagnosticEngine& diag, ImportManager& imports,
    ScopeManager& scopeManager, SourceManager& sourceManager, bool buildTrie, Package& pkg, File& file,
    FuncDecl& fd, std::unordered_map<Ptr<const Node>, Ptr<Decl>>& supportRefs,
    const std::function<ArkTSTypeDesc(Ptr<Type>)>& classify)
{
    ArkTSFunctionSignature sig;
    // The option contract is checked before the signature: an illegal option
    // value is a declaration error of its own, and a later unsupported
    // signature refusal must not hide it.
    ArkTSOptionStatus optionStatus;
    if (!ArkTSDeclOptions(fd, sig.options, &optionStatus)) {
        DiagnoseArkTSFunctionOptionProblem(diag, fd, sig.options, optionStatus);
        fd.EnableAttr(Attribute::IS_BROKEN);
        return;
    }
    sig.resultType = classify(fd.funcBody->retType.get());
    if (RefuseArkTSFunctionReturnType(diag, fd, *fd.funcBody, sig.resultType)) {
        return;
    }
    if (!ClassifyArkTSFunctionParameters(diag, fd, *fd.funcBody, classify, sig)) {
        fd.EnableAttr(Attribute::IS_BROKEN);
        return;
    }
    if (!PrepareArkTSFunctionModule(diag, pkg, fd, sig)) {
        return;
    }
    if (!FinalizeArkTSFunctionSignature(diag, fd, *fd.funcBody, sig)) {
        return;
    }
    InstallArkTSFunctionBody(
        ctx, diag, imports, scopeManager, sourceManager, buildTrie, file, fd, sig, supportRefs);
}
} // namespace

// Lowers every marked top-level @ArkTS foreign FuncDecl (class-like members
// are deferred): diagnose refusals, generate the wrapper body, parse it back
// into the FuncBody and rebind placeholders as AST values:
//   an unsupported/Nothing-carrying position, illegal option or module form,
//   or a named parameter without a default value is diagnosed, marks the
//   declaration IS_BROKEN, no wrapper (a Nothing result keeps the never
//   contract; an external preset type gets the dedicated deferral
//   diagnostic)
//   module = the explicit option, else the package name; arkts.lang
//   without an option takes the GLOBAL receiver (ctx.global); then bind
//   module/name/namespace literals and parameter references as AST values
// Before the wrapper loop, the same option-shape rule is applied to every
// other @ArkTS-carrying declaration in the package (ArkTSDeclOptionsDiagnose),
// so a class, interface, variable, parameter or entry cannot silently carry an
// illegal option; wrappers stay owned by the fold below.

void TypeChecker::TypeCheckerImpl::LowerArkTSFunctions(ASTContext& ctx)
{
    auto& pkg = *ctx.curPackage;
    if (!PassEnabled(pkg)) {
        return;
    }
    // The option fold below covers the lowered wrappers; every other
    // @ArkTS-carrying declaration (class, interface, variable, parameter,
    // entry) is validated once per package by LowerArkTSClasses, which runs
    // first on the PreCheck path, because its named-default rule must mark a
    // member broken before any class body is generated.
    auto classify = [this, &ctx](Ptr<Type> type) { return ArkTSClassifyPosition(ctx, type); };
    for (auto& file : pkg.files) {
        // One entry callback/registration pair is one user entry: both carry
        // its source range, so a missing package is reported once for the pair.
        std::set<std::tuple<Ptr<File>, Position, Position>> reportedSupportRanges;
        for (auto& decl : file->decls) {
            // Support entry declarations carry generated bodies, so they are
            // bound before the wrapper filter skips body-carrying declarations.
            if (decl->TestAttr(Attribute::COMPILER_ADD) &&
                decl->identifier.Val().rfind(ARKTS_GENERATED_PREFIX, 0) == 0) {
                BindGeneratedArkTSSupportDecl(
                    *decl, *file, *ci->importManager, diag, arktsSupportRefs, reportedSupportRanges);
            }
            auto fd = DynamicCast<FuncDecl*>(decl.get());
            if (!fd || !fd->TestAttr(Attribute::ARKTS_FUNCTION_WRAPPER) || !fd->funcBody || fd->funcBody->body ||
                IsGlobalVar(*fd)) {
                continue;
            }
            LowerArkTSFunctionDecl(ctx, diag, *ci->importManager, scopeManager, ci->GetSourceManager(),
                ci->buildTrie, pkg, *file, *fd, arktsSupportRefs, classify);
        }
    }
}
