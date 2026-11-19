// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "TypeCheckerImpl.h"

#include <algorithm>
#include <iterator>
#include <set>
#include <tuple>
#include <unordered_map>

#include "../FunctionSource.h"
#include "../Utils/ASTFactory.h"
#include "Collector.h"
#include "PlaceholderBinding.h"
#include "cangjie/AST/Utils.h"

using namespace Cangjie;
using namespace Cangjie::AST;
using namespace Cangjie::Native::FFI::ArkTS;

namespace {
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
    static const std::unordered_map<std::string, std::string> packages{{"BigInt", "std.math.numeric"},
        {"Exception", "std.core"}, {"Option", "std.core"},
        {"ExternConversionException", Native::FFI::ArkTS::ARKTS_LANGUAGE_PACKAGE},
        {Native::FFI::ArkTS::ARKTS_RUNTIME_EXCEPTION, Native::FFI::ArkTS::ARKTS_LANGUAGE_PACKAGE},
        {Native::FFI::ArkTS::ARKTS_BUSINESS_EXCEPTION, Native::FFI::ArkTS::ARKTS_LANGUAGE_PACKAGE}};
    // Base references of the package-qualified generated names; the value is
    // the full package name the injected import must bind.
    static const std::unordered_map<std::string, std::string> supportPackages{
        {ARKTS_INTEROP_PACKAGE, "ohos.ark_interop"},
        {ARKTS_CONTEXT_ABI_PACKAGE, "cj_arkts_context_v1"}};
    bool valid = true;
    auto reportMissing = [&]() {
        if (!valid) {
            return;
        }
        if (generatedSupport && reportedRanges != nullptr &&
            !reportedRanges->emplace(owner.curFile, owner.begin, owner.end).second) {
            return; // The entry's other generated declaration already reported it.
        }
        const auto name = diagnosticName.empty() ? owner.identifier.Val() : diagnosticName;
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_support_unavailable, owner, name);
    };
    // Only committed once every support reference is bound, so a rejected
    // wrapper (whose generated body is dropped) leaves no stale node keys.
    std::vector<std::pair<Ptr<Node>, Ptr<Decl>>> bound;
    Walker(&root, [&](Ptr<Node> node) {
        auto expr = DynamicCast<RefExpr*>(node);
        auto type = DynamicCast<RefType*>(node);
        auto ref = expr ? &expr->ref : type ? &type->ref : nullptr;
        // A reference that already carries a target keeps it: this walker only
        // supplies declarations for the names the generator left unresolved.
        // Two kinds of generated references are already bound before this walk:
        // the wrapper's parameter placeholders (rebound to their FUNC_PARAM in
        // LowerArkTSFunctions) and the entry callback's call to the source entry
        // (bound to the user FuncDecl by ASTFactory::CreateEntry). Either may be
        // named like a support item, so re-resolving by name here would replace
        // a deliberate binding (an entry named BigInt would be captured by
        // std.math.numeric.BigInt and the callback's call would break).
        if (!ref || ref->target) {
            return VisitAction::WALK_CHILDREN;
        }
        const auto name = ref->identifier.Val();
        if (auto package = supportPackages.find(name); package != supportPackages.end()) {
            const auto visible = imports.GetImportedDeclsByName(*owner.curFile, name);
            auto target = std::find_if(visible.begin(), visible.end(), [&](auto decl) {
                return decl->astKind == ASTKind::PACKAGE_DECL && decl->fullPackageName == package->second;
            });
            if (target == visible.end()) {
                reportMissing();
                valid = false;
            } else {
                ref->target = *target;
                ref->targets = {*target};
                // A package qualifier has no value type; the qualified node
                // resolves its member through the PackageDecl. Do not set a Ty
                // here or the restoring re-check would treat the qualifier as a
                // type name.
                node->SetTy(TypeManager::GetInvalidTy());
                bound.emplace_back(node, *target);
            }
            return VisitAction::WALK_CHILDREN;
        }
        auto found = packages.find(name);
        if (found == packages.end()) {
            return VisitAction::WALK_CHILDREN;
        }
        const auto visible = imports.GetImportedDeclsByName(*owner.curFile, found->first);
        auto target = std::find_if(visible.begin(), visible.end(), [&](auto decl) {
            if (decl->identifier != found->first) {
                return false;
            }
            if (decl->fullPackageName == found->second) {
                return true;
            }
            // The interop library's exception carrier is re-exported by the
            // facade under its original package name; both spellings denote
            // the one declaration the call translation must catch.
            if (found->first == Native::FFI::ArkTS::ARKTS_BUSINESS_EXCEPTION &&
                decl->fullPackageName == Native::FFI::ArkTS::ARKTS_BUSINESS_EXCEPTION_PACKAGE) {
                return true;
            }
            // The runtime exception is hosted by the call-scope package, which
            // detects the missing scope, and re-exported by the facade.
            return found->first == Native::FFI::ArkTS::ARKTS_RUNTIME_EXCEPTION &&
                decl->fullPackageName == ARKTS_CONTEXT_ABI_PACKAGE;
        });
        if (target == visible.end() || !Ty::IsTyCorrect((*target)->GetTy())) {
            reportMissing();
            valid = false;
        } else {
            ref->target = *target;
            ref->targets = {*target};
            // A generic type reference keeps its type arguments: presetting the
            // declaration's own generic type (e.g. Option<T>) would drop the
            // instantiation and make the generated annotation uninferable.
            // Only a reference without type arguments takes the declaration
            // type here; a parameterized one is instantiated by body checking
            // from the now-bound target.
            if (!type || type->typeArguments.empty()) {
                node->SetTy((*target)->GetTy());
            }
            bound.emplace_back(node, *target);
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    if (valid) {
        for (auto& [node, target] : bound) {
            supportRefs[node] = target;
        }
    }
    return valid;
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
    if (pkg.TestAttr(Attribute::IMPORTED)) {
        return;
    }
    // The option fold below covers the lowered wrappers; every other
    // @ArkTS-carrying declaration (class, interface, variable, parameter,
    // entry) is validated once per package by LowerArkTSClasses, which runs
    // first on the PreCheck path, because its named-default rule must mark a
    // member broken before any class body is generated.
    for (auto& file : pkg.files) {
        // One entry callback/registration pair is one user entry: both carry
        // its source range, so a missing package is reported once for the pair.
        std::set<std::tuple<Ptr<File>, Position, Position>> reportedSupportRanges;
        for (auto& decl : file->decls) {
            // Support entry declarations carry generated bodies, so they are
            // bound before the wrapper filter skips body-carrying declarations.
            if (decl->TestAttr(Attribute::COMPILER_ADD) &&
                decl->identifier.Val().rfind(ARKTS_GENERATED_PREFIX, 0) == 0) {
                // The generated declarations share the source entry's range;
                // resolve the user-facing name from it so a diagnostic never
                // names the internal symbol.
                std::string entryName;
                for (auto& source : file->decls) {
                    if (source != nullptr && !source->TestAttr(Attribute::COMPILER_ADD) &&
                        source->begin == decl->begin && source->end == decl->end) {
                        entryName = source->identifier.Val();
                        break;
                    }
                }
                if (!BindSupportReferences(*decl, *decl, *ci->importManager, diag, arktsSupportRefs, true, entryName,
                        &reportedSupportRanges)) {
                    // The callback cannot compile without its interop packages;
                    // mark it broken so body checking does not restate the same
                    // missing declarations as undeclared identifiers.
                    decl->EnableAttr(Attribute::IS_BROKEN);
                }
            }
            auto fd = DynamicCast<FuncDecl*>(decl.get());
            if (!fd || !fd->TestAttr(Attribute::ARKTS_FUNCTION_WRAPPER) || !fd->funcBody || fd->funcBody->body ||
                IsGlobalVar(*fd)) {
                continue;
            }
            auto& fb = *fd->funcBody;
            // Type description of one signature position, after alias
            // resolution; the spelling is shared with the class member path,
            // so a position cannot mean two things across the two lowering
            // paths.
            auto classify = [&](Ptr<Type> type) { return ArkTSClassifyPosition(ctx, type); };
            auto resultType = classify(fb.retType.get());
            std::vector<ArkTSTypeDesc> types;
            std::vector<Ptr<FuncParam>> params;
            // Diagnose unsupported positions here and mark the declaration
            // broken for every later gate instead of silently skipping the
            // wrapper. The generatability/Nothing predicates come from
            // FunctionSource.h, so lowering and the generator can never
            // drift apart. A Nothing result is the never contract (the
            // wrapper throws), so only generatability is checked there.
            if (!ArkTSTypeDescGeneratable(resultType)) {
                // A return position spelled with an external ArkTS preset
                // type (std.interop.arkts ArkTSArray<T>, ArkTSString,
                // ArkTSType) is not a spelling mistake - its mapping is
                // deferred - so it reports the dedicated deferral diagnostic
                // instead of the generic unsupported-type error. The routing
                // helper takes no part in classification: the position still
                // classifies UNSUPPORTED, the declaration still ends up
                // broken and no wrapper is generated.
                auto deferredName = fb.retType ? ArkTSDeferredExternalTypeName(fb.retType->GetTy()) : std::string{};
                if (!deferredName.empty()) {
                    auto& deferredAt = fb.retType->begin.IsZero() ? static_cast<Node&>(*fd)
                                                                  : static_cast<Node&>(*fb.retType);
                    diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_external_type_deferred, deferredAt,
                        deferredName);
                    fd->EnableAttr(Attribute::IS_BROKEN);
                    continue;
                }
                // An imported enum without a value table comes from a package
                // compiled with an older CJO format (its @RawValue values are
                // absent). Report the general unsupported return type with a
                // recompilation note.
                auto importedEnum = ArkTSTypeDescImportedEnumName(resultType);
                if (!importedEnum.empty()) {
                    auto& at = fb.retType && !fb.retType->begin.IsZero() ? static_cast<Node&>(*fb.retType)
                                                                        : static_cast<Node&>(*fd);
                    auto builder = diag.DiagnoseRefactor(
                        DiagKindRefactor::sema_arkts_foreign_ret_type_unsupported, at, importedEnum,
                        fd->identifier.Val());
                    builder.AddNote("recompile the enum's package with the current compiler");
                    fd->EnableAttr(Attribute::IS_BROKEN);
                    continue;
                }
                // Report on the written return type when it carries a
                // position, else on the whole declaration (the Java
                // interop fallback); a missing type node only arises from
                // defensive classification.
                if (fb.retType && !fb.retType->begin.IsZero()) {
                    diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_ret_type_unsupported, *fb.retType,
                        Ty::ToString(fb.retType->GetTy()), fd->identifier.Val());
                } else {
                    diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_ret_type_unsupported, *fd,
                        std::string(), fd->identifier.Val());
                }
                fd->EnableAttr(Attribute::IS_BROKEN);
                continue;
            }
            bool supported = true;
            // A function with a rest parameter cannot declare named
            // parameters; the parser has already reported that mix through
            // parse_rest_param_function_has_named_params, so the named-default
            // rule below stays silent for this signature. The declaration is
            // still refused (no wrapper), because the parser rejected it.
            const bool restParam = ArkTSFuncBodyHasRestParam(fb);
            for (auto& list : fb.paramLists) {
                for (auto& param : list->params) {
                    // Every named parameter ('x!:') of an @ArkTS foreign
                    // declaration must carry a default value (user rule,
                    // 2026-11-05). The wrapper cannot tell an omitted named
                    // argument from an explicitly passed value, so
                    // `x!: Option<T>` without `= None` - and any other named
                    // parameter without a default - is rejected here. This
                    // walker only visits declarations the ArkTS lowering
                    // owns, so ordinary Cangjie named parameters are
                    // unaffected. A malformed parameter, or one belonging to
                    // the already-rejected rest+named shape, is not reported
                    // by this rule a second time.
                    if (param->isNamedParam && param->assignment == nullptr) {
                        if (!restParam && !ArkTSParamIsBroken(*param)) {
                            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_named_param_without_default,
                                *param, param->identifier.Val(), fd->identifier.Val());
                        }
                        supported = false;
                    }
                    // This declaration rule runs before the semantic type of a
                    // bodyless foreign parameter is available, so the written
                    // spelling (`?T` or `Option<T>`) is the contract.
                    if (DiagnoseArkTSDefaultedNamedParamNotOption(diag, *param, *fd)) {
                        supported = false;
                    }
                    auto desc = classify(param->type.get());
                    if (!ArkTSTypeDescGeneratable(desc) || ArkTSTypeDescContainsNothing(desc)) {
                        auto& position = param->type ? static_cast<Node&>(*param->type) : static_cast<Node&>(*param);
                        // A parameter spelled with an external ArkTS
                        // preset type (std.interop.arkts ArkTSArray<T>,
                        // ArkTSString, ArkTSType) reports the dedicated
                        // deferral diagnostic; the routing helper takes no
                        // part in classification (the position still
                        // classifies UNSUPPORTED). Every other unsupported
                        // or Nothing-carrying parameter keeps the generic
                        // message below.
                        auto deferredName =
                            param->type ? ArkTSDeferredExternalTypeName(param->type->GetTy()) : std::string{};
                        if (!deferredName.empty()) {
                            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_external_type_deferred,
                                position, deferredName);
                        } else if (auto importedEnum = ArkTSTypeDescImportedEnumName(desc); !importedEnum.empty()) {
                            // Same legacy-cjo rule as the return position.
                            auto builder = diag.DiagnoseRefactor(
                                DiagKindRefactor::sema_arkts_foreign_param_type_unsupported, position,
                                param->identifier.Val(), fd->identifier.Val(), importedEnum);
                            builder.AddNote("recompile the enum's package with the current compiler");
                        } else {
                            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_param_type_unsupported,
                                position, param->identifier.Val(), fd->identifier.Val(),
                                param->type ? Ty::ToString(param->type->GetTy()) : std::string());
                        }
                        supported = false;
                    }
                    types.emplace_back(std::move(desc));
                    params.emplace_back(param.get());
                }
            }
            if (!supported) {
                fd->EnableAttr(Attribute::IS_BROKEN);
                continue;
            }
            std::unordered_map<std::string, std::string> options;
            // Every illegal option shape - a non-literal value, an unknown
            // name, a name repeated inside one @ArkTS layer, an empty
            // namespace segment - is reported here, once per declaration;
            // the option fold itself is the single source of that rule. A
            // repeated key gets its own message: the key and value are legal,
            // only the repetition is not. An ill-formed namespace also has its
            // own message and points at the namespace literal.
            ArkTSOptionStatus optionStatus;
            if (!ArkTSDeclOptions(*fd, options, &optionStatus)) {
                if (optionStatus.problem == ArkTSOptionProblem::DUPLICATE) {
                    auto& at = optionStatus.at ? *optionStatus.at : static_cast<Node&>(*fd);
                    diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_option_duplicated, at,
                        optionStatus.option, fd->identifier.Val());
                } else if (optionStatus.problem == ArkTSOptionProblem::ILL_FORMED_NAMESPACE) {
                    auto& at = optionStatus.at ? *optionStatus.at : static_cast<Node&>(*fd);
                    diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_namespace_invalid, at,
                        options.count("namespace") ? options.at("namespace") : std::string(), fd->identifier.Val());
                } else {
                    diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_option_invalid, *fd,
                        fd->identifier.Val());
                }
                fd->EnableAttr(Attribute::IS_BROKEN);
                continue;
            }
            // The explicit-module path check reports on the module option's
            // own literal when it is attached to this declaration, else on
            // the whole declaration (the fallback for a merged option).
            Ptr<Node> moduleNode = fd;
            if (options.count("module") != 0) {
                for (auto& annotation : fd->annotations) {
                    if (annotation->kind != AnnotationKind::ARKTS) {
                        continue;
                    }
                    for (auto& arg : annotation->args) {
                        if (arg->name.Val() == "module") {
                            moduleNode = arg->expr.get();
                        }
                    }
                }
            }
            // An explicitly written empty value (only whitespace counts as
            // empty) would flow into requireArkModule(""), callMethod("") or
            // an empty namespace segment and only fail at run time; reject it
            // at compile time. The scan covers every layer attached to this
            // declaration, including inherited block defaults, and reports at
            // the option's own literal. A non-literal or unknown option was
            // already rejected by the fold above, so every argument here is a
            // known string key. The legal-specifier whitelist lives in the
            // interop library and changes with its versions, so only the
            // empty spelling is rejected.
            Ptr<Node> emptyAt = nullptr;
            DiagKindRefactor emptyKind = DiagKindRefactor::sema_arkts_foreign_module_empty;
            for (auto& annotation : fd->annotations) {
                if (annotation->kind != AnnotationKind::ARKTS) {
                    continue;
                }
                for (auto& arg : annotation->args) {
                    auto literal = DynamicCast<LitConstExpr*>(arg->expr.get());
                    if (!literal || literal->kind != LitConstKind::STRING ||
                        !ArkTSOptionValueEmpty(literal->stringValue)) {
                        continue;
                    }
                    emptyAt = arg->expr.get();
                    if (arg->name.Val() == "foreignName") {
                        emptyKind = DiagKindRefactor::sema_arkts_foreign_name_empty;
                    } else if (arg->name.Val() == "namespace") {
                        emptyKind = DiagKindRefactor::sema_arkts_foreign_namespace_empty;
                    }
                    break;
                }
                if (emptyAt) {
                    break;
                }
            }
            if (emptyAt) {
                diag.DiagnoseRefactor(emptyKind, *emptyAt, fd->identifier.Val());
                fd->EnableAttr(Attribute::IS_BROKEN);
                continue;
            }
            // An explicit module is a module specifier: a system module name
            // ("@..."), a project module name, or a project ETS source path
            // such as "entry/src/main/ets/Foo". Any separator shape that is
            // not a leading relative or absolute spelling ("a/", "a//b",
            // "a\b") passes. Only "./"/"../" starts, OS-absolute paths,
            // drive-absolute paths and UNC paths are rejected. Both
            // separators are checked on every host, because the ArkTS project
            // may be built on Windows. Existence checking would need the
            // ArkTS project root, which is not a compiler input, so only the
            // format is rejected here; existence and named-module
            // resolvability stay with the runtime module loading.
            if (options.count("module") != 0) {
                const std::string& moduleValue = options.at("module");
                if (ArkTSModulePathLike(moduleValue)) {
                    diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_module_path_invalid, *moduleNode,
                        moduleValue, fd->identifier.Val());
                    fd->EnableAttr(Attribute::IS_BROKEN);
                    continue;
                }
            }
            // The module name is the explicit option, or - when omitted -
            // the Cangjie package name verbatim (ArkTSDefaultModule). The
            // one receiver exception: the package arkts.lang is the TS
            // preset (its declarations are TypeScript builtins, not
            // exports of a real ArkTS runtime module), so a declaration
            // there without an explicit module option takes ctx.global
            // as the receiver instead of requireArkModule("arkts.lang");
            // an explicit option always selects the module form. The
            // receiver rule is the shared ArkTSReceiverFor spelling, so
            // the class member path can never drift from it.
            std::string module;
            if (options.count("module")) {
                module = options.at("module");
            } else {
                module = ArkTSDefaultModule(pkg.fullPackageName);
            }
            // The only way to an empty module name now is an empty package
            // name (a degenerate compilation unit; an explicitly empty
            // module option was already rejected above): there is no module
            // to require, so the wrapper is refused through the generic
            // unsupported-signature diagnostic.
            if (module.empty()) {
                diag.DiagnoseRefactor(
                    DiagKindRefactor::sema_arkts_foreign_unsupported_signature, *fd, fd->identifier.Val());
                fd->EnableAttr(Attribute::IS_BROKEN);
                continue;
            }
            ArkTSReceiverKind receiver = ArkTSReceiverFor(options.count("module") != 0, module);
            std::unordered_map<std::string, std::string> literals{{ARKTS_PLACEHOLDER_NAME,
                options.count("foreignName") ? options.at("foreignName") : fd->identifier.Val()},
                {ARKTS_PLACEHOLDER_MODULE, module}};
            // Namespace segments follow the shared split spelling, so the
            // placeholder count and the generator's namespace depth agree.
            auto segments = ArkTSNamespaceSegments(options.count("namespace") ? options.at("namespace") : "");
            size_t depth = 0;
            for (const auto& segment : segments) {
                literals[ARKTS_PLACEHOLDER_NAMESPACE + std::to_string(depth++) + "__"] = segment;
            }
            // @RestParam is a parse-time rule on the first parameter list only
            // (ArkTSParserImpl::CheckRestParamPlacementImpl). Cangjie itself
            // accepts multiple parameter lists, but the ArkTS proposal only
            // gives rest parameters a meaning in the first list, so lowering
            // scans only that list: its positions are the prefix of the
            // flattened 'params' vector, and the scan bound passed to the
            // shared ArkTSRestPosition is that list's size. A rest annotation
            // written on a later list is outside the proposal's contract and
            // stays an ordinary parameter: the generator's behaviour is
            // unchanged and no new user-visible rejection is introduced here.
            size_t restIndex =
                ArkTSRestPosition(params, fb.paramLists.empty() ? 0 : fb.paramLists.front()->params.size());
            // The @RestParam position itself must carry Array<T'> (its
            // element was already validated by the parameter loop above).
            // An ArkTSArray<T'> rest position never reaches this check:
            // the parameter loop above classifies it UNSUPPORTED and
            // already reported the dedicated deferral diagnostic there.
            if (restIndex != ARKTS_NO_REST &&
                types[restIndex].kind != ArkTSTypeDesc::Kind::ARRAY_COPY) {
                diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_rest_type_unsupported,
                    *params[restIndex]->type, params[restIndex]->identifier.Val(), fd->identifier.Val(),
                    Ty::ToString(params[restIndex]->type->GetTy()));
                fd->EnableAttr(Attribute::IS_BROKEN);
                continue;
            }
            // ArkTS optional parameters are the trailing named parameters
            // that carry a default value (`x!: Option<T> = None`, design
            // §4.1.2): only such a parameter may be omitted by the caller.
            // The named-default rule above already rejected every named
            // parameter without a default, so the assignment test here only
            // keeps the gate tied to the written spelling; a positional
            // Option (the required nullable argument of the with-rest/open
            // form) is still always forwarded, forwarding None as
            // ctx.undefined().
            size_t optionalStart = params.size();
            while (optionalStart > 0 && params[optionalStart - 1]->isNamedParam &&
                params[optionalStart - 1]->assignment != nullptr &&
                types[optionalStart - 1].kind == ArkTSTypeDesc::Kind::OPTIONAL) {
                --optionalStart;
            }
            // Reference positions record their class declarations by
            // placeholder; the walker below binds them as AST values.
            std::unordered_map<std::string, Ptr<Decl>> classRefs;
            auto code = restIndex == ARKTS_NO_REST
                            ? ArkTSForeignWrapperSource(types, resultType, depth, ArkTSForeignLocalPrefix(params),
                                receiver, optionalStart, &classRefs)
                            : ArkTSForeignWrapperSource(types, resultType, depth, ArkTSForeignLocalPrefix(params),
                                restIndex, receiver, optionalStart, &classRefs);
            // A generator refusal must never silently drop the wrapper.
            // The remaining refusals are rest placements the parse checks
            // already reject (kept here as defense in depth in case a
            // broken parse ever slips such a signature through) and
            // hand-built descriptions; every case with a precise message
            // was already rejected above, so the message here stays generic
            // and carries no internal reason. The declaration is marked
            // broken like every other rejected signature and no wrapper is
            // produced.
            if (code.empty()) {
                diag.DiagnoseRefactor(
                    DiagKindRefactor::sema_arkts_foreign_unsupported_signature, *fd, fd->identifier.Val());
                fd->EnableAttr(Attribute::IS_BROKEN);
                continue;
            }
            Native::FFI::ArkTS::ASTFactory factory(diag, ci->GetSourceManager());
            fb.body = factory.CreateForeignBody(*fd, code);
            // An empty body here means the generated skeleton itself failed
            // to parse: an internal error in the wrapper emitter, not a
            // user-signature problem (every user-reachable refusal was
            // already diagnosed above), and the skeleton parser has already
            // reported its own diagnostics, so nothing is added here.
            if (!fb.body) {
                continue;
            }
            // Type placeholders are merged into the long-lived support map
            // only after the body passes BindArkTSSupportReferences.
            std::unordered_map<Ptr<const Node>, Ptr<Decl>> pendingRefs;
            Walker(fb.body.get(), [&](Ptr<Node> node) {
                node->curFile = file.get();
                node->begin = fd->begin;
                node->end = fd->end;
                if (auto literal = DynamicCast<LitConstExpr*>(node); literal && literals.count(literal->stringValue)) {
                    literal->stringValue = literals.at(literal->stringValue);
                }
                // Type placeholders carry a reference element in generated
                // annotations (Array<T>, Option<T>): bind the reference type
                // and register it like a support declaration, so name
                // resolution and speculative re-checks keep the class.
                if (auto refType = DynamicCast<RefType*>(node)) {
                    BindArkTSRefTypePlaceholder(*refType, classRefs, pendingRefs);
                    return VisitAction::WALK_CHILDREN;
                }
                if (auto ref = DynamicCast<RefExpr*>(node)) {
                    bool argument = false;
                    for (size_t i = 0; i < params.size(); ++i) {
                        if (ref->ref.identifier == ARKTS_PLACEHOLDER_ARGUMENT + std::to_string(i)) {
                            ref->ref.identifier = params[i]->identifier;
                            ref->ref.target = params[i];
                            ref->SetTy(params[i]->type->GetTy());
                            ref->begin = fd->end;
                            argument = true;
                            break;
                        }
                    }
                    if (argument) {
                        return VisitAction::WALK_CHILDREN;
                    }
                    if (auto found = classRefs.find(ref->ref.identifier.Val()); found != classRefs.end()) {
                        ref->ref.identifier = found->second->identifier;
                        ref->ref.target = found->second;
                        ref->ref.targets = {found->second};
                        ref->SetTy(found->second->GetTy());
                    }
                }
                if (auto returned = DynamicCast<ReturnExpr*>(node)) {
                    returned->refFuncBody = &fb;
                }
                return VisitAction::WALK_CHILDREN;
            }).Walk();
            if (!BindSupportReferences(*fd, *fb.body, *ci->importManager, diag, pendingRefs, false,
                    fd->identifier.Val(), nullptr)) {
                fb.body.reset();
                fd->EnableAttr(Attribute::IS_BROKEN);
                fb.EnableAttr(Attribute::IS_BROKEN);
                continue;
            }
            for (auto& [node, target] : pendingRefs) {
                arktsSupportRefs[node] = target;
            }
            // Match Collector::CollectFuncBody: locals share parameter scope.
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
                collector.BuildSymbolTable(ctx, statement.get(), ci->buildTrie);
            }
            auto first = previous == ctx.symbolTable.end() ? ctx.symbolTable.begin() : std::next(previous);
            for (auto it = first; it != ctx.symbolTable.end(); ++it) {
                auto& symbol = *it;
                if (auto local = DynamicCast<Decl*>(symbol->node)) {
                    ctx.AddDeclName(
                        {symbol->name, ScopeManagerApi::GetScopeNameWithoutTail(symbol->scopeName)}, *local);
                }
            }
            ctx.currentScopeName = oldScope;
            ctx.currentScopeLevel = oldLevel;
        }
    }
}
