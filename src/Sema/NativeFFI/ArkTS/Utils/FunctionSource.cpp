// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "../ArkTSInterop.h"
#include "../FunctionSource.h"
#include "../FunctionSourceInternal.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <unordered_set>

#include "cangjie/AST/Match.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Basic/StringConvertor.h"
#include "cangjie/Modules/ImportManager.h"
#include "cangjie/Utils/CheckUtils.h"

namespace Cangjie::Native::FFI::ArkTS {
using namespace Cangjie::AST;

namespace {
// Qualified spellings of the interop types the wrapper writes. They are
// single-sourced here so the generator and its comments cannot drift from the
// package names the import injection binds.
const std::string ARKTS_JS_CONTEXT = std::string(ARKTS_INTEROP_PACKAGE) + ".JSContext";
const std::string ARKTS_JS_VALUE = std::string(ARKTS_INTEROP_PACKAGE) + ".JSValue";
} // namespace
// Canonical value of an omitted 'module' option: the Cangjie package name
// of the declaration, returned verbatim - no ETS source-hierarchy
// mapping, and a single-segment package name is a legal module name. An
// empty return only means an empty package name (a degenerate compilation
// unit), which lowering refuses through the generic wrapper-unavailable
// diagnostic.
// Example: package "entry" (no module option) -> requireArkModule("entry").
std::string ArkTSDefaultModule(const std::string& package)
{
    return package;
}

// Checked scalar names live in Utils/TypeMapper (ArkTSScalarWhitelist),
// shared with the classification and the generatability guard.

// Checked-scalar-name adapter: one semantic Ty in, one whitelist name (or
// the empty refusal) out - the classification input of every SCALAR
// description. TYPE_INT32 / TYPE_RUNE / TYPE_UNIT give "Int32" / "Rune" /
// "Unit", the std.core String struct gives "String" and std.math.numeric
// BigInt gives "BigInt"; BigInt in any other package, IntNative or a null Ty
// give the empty refusal "".
std::string ArkTSScalarName(Ptr<Ty> ty)
{
    if (!Ty::IsTyCorrect(ty)) {
        return {};
    }
    if (ty->IsString()) {
        return "String";
    }
    if (ty->IsRune()) {
        return "Rune";
    }
    if (ty->IsInteger() || ty->IsFloating() || ty->IsBoolean() || ty->IsUnitOrNothing()) {
        auto name = Ty::ToString(ty);
        return ArkTSScalarWhitelist().count(name) ? name : std::string{};
    }
    auto decl = Ty::GetDeclPtrOfTy(ty);
    return decl && decl->fullPackageName == "std.math.numeric" && decl->identifier == "BigInt" ? "BigInt" : "";
}

ArkTSOptionStatus CheckArkTSOptionLayer(const Annotation& annotation)
{
    ArkTSOptionStatus status;
    std::unordered_set<std::string> explicitOptions;
    for (auto& arg : annotation.args) {
        const auto name = arg->name.Val();
        auto literal = DynamicCast<LitConstExpr*>(arg->expr.get());
        const bool isGlobalVar = name == "globalVar";
        // module / namespace / foreignName take a string literal; globalVar
        // takes a boolean literal. A wrong kind is a value error, not an
        // unknown option: the name is known, the literal is not acceptable.
        if (!literal || (isGlobalVar ? literal->kind != LitConstKind::BOOL : literal->kind != LitConstKind::STRING)) {
            status.problem = isGlobalVar ? ArkTSOptionProblem::ILL_FORMED_VALUE : ArkTSOptionProblem::NON_LITERAL;
            status.option = name;
            status.at = arg->expr.get();
            return status;
        }
        if (!isGlobalVar && name != "module" && name != "namespace" && name != "foreignName") {
            status.problem = ArkTSOptionProblem::UNKNOWN_NAME;
            status.option = name;
            status.at = arg->expr.get();
            return status;
        }
        if (!explicitOptions.emplace(name).second) {
            status.problem = ArkTSOptionProblem::DUPLICATE;
            status.option = name;
            status.at = arg->expr.get();
            return status;
        }
    }
    return status;
}

bool IsGlobalVar(const AST::Decl& decl)
{
    // The option fold applies the layers in order and gives the last explicit
    // value precedence, so a member's 'globalVar: false' cancels a block
    // default of true. A malformed layer is refused by its owning fold and
    // never selects the read path.
    std::unordered_map<std::string, std::string> options;
    if (!ArkTSDeclOptions(decl, options)) {
        return false;
    }
    auto found = options.find("globalVar");
    return found != options.end() && found->second == "true";
}

bool ArkTSNamespaceValid(const std::string& ns)
{
    return ns.empty() || (ns.front() != '.' && ns.back() != '.' && ns.find("..") == std::string::npos);
}

// Single split spelling shared by the function and class member lowering
// paths: every segment becomes one __namespace_<i>__ placeholder, so both
// paths must agree on the segment list a value produces.
std::vector<std::string> ArkTSNamespaceSegments(const std::string& ns)
{
    std::vector<std::string> segments;
    size_t start = 0;
    while (start < ns.size()) {
        auto end = ns.find('.', start);
        segments.emplace_back(ns.substr(start, end == std::string::npos ? end : end - start));
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return segments;
}

// The receiver rule beside ArkTSReceiverKind: the GLOBAL cases are the
// package-pinned arkts.lang facade (or a subpackage) whose module option is
// omitted; every other spelling - explicit option included - resolves the
// module object.
ArkTSReceiverKind ArkTSReceiverFor(bool explicitModule, const std::string& module)
{
    const bool facadeLike = module == "arkts.lang" || module.rfind("arkts.lang.", 0) == 0;
    return !explicitModule && facadeLike ? ArkTSReceiverKind::GLOBAL : ArkTSReceiverKind::MODULE;
}

bool ArkTSOptionValueEmpty(const std::string& value)
{
    return std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c) != 0; });
}

bool ArkTSParamIsBroken(const FuncParam& param)
{
    return param.TestAttr(Attribute::HAS_BROKEN) || param.TestAttr(Attribute::IS_BROKEN);
}

bool ArkTSTypeIsSourceOption(const Type& type)
{
    if (type.astKind == ASTKind::OPTION_TYPE) {
        return true;
    }
    // A type alias such as 'type Maybe = Option<String>' passes the syntactic
    // checks below; the semantic type is the reliable answer once Sema has run.
    if (type.GetTy() != nullptr && type.GetTy()->IsCoreOptionType()) {
        return true;
    }
    if (auto ref = DynamicCast<const RefType*>(&type)) {
        return ref->ref.identifier == "Option" && ref->typeArguments.size() == 1;
    }
    if (auto qualified = DynamicCast<const QualifiedType*>(&type)) {
        return qualified->field == "Option" && qualified->typeArguments.size() == 1;
    }
    return false;
}

bool ArkTSDefaultedNamedParamIsNotOption(const FuncParam& param)
{
    return param.isNamedParam && param.assignment != nullptr && param.type != nullptr &&
        !ArkTSParamIsBroken(param) && !ArkTSTypeIsSourceOption(*param.type);
}

bool DiagnoseArkTSDefaultedNamedParamNotOption(DiagnosticEngine& diag, const FuncParam& param, const Decl& decl)
{
    if (!ArkTSDefaultedNamedParamIsNotOption(param)) {
        return false;
    }
    diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_named_param_not_option, param,
        param.identifier.Val(), decl.identifier.Val(), param.type->ToString());
    return true;
}

bool ArkTSBodyHasRestParam(const FuncBody& body)
{
    for (const auto& list : body.paramLists) {
        if (list == nullptr) {
            continue;
        }
        for (const auto& param : list->params) {
            if (param == nullptr) {
                continue;
            }
            if (std::any_of(param->annotations.begin(), param->annotations.end(), [](const auto& annotation) {
                    return annotation->kind == AnnotationKind::ARKTS_REST_PARAM;
                })) {
                return true;
            }
        }
    }
    return false;
}

bool ArkTSForeignClassLikeMember(const Decl& member)
{
    // Preparation rewrites a non-generic @ArkTS foreign class in place: it
    // keeps FOREIGN and marks the declaration ARKTS_CLASS_WRAPPER before
    // PreCheck reaches the option fold. Both spellings are the same
    // class-like owner, so the member rule accepts either; a generic class
    // keeps the external declaration form and has no marker.
    //
    // A prepared interface is excluded. Its members are lowered into generated
    // bodies that carry their own parameter binding, so the member rules this
    // predicate gates - the named-parameter default requirement in particular -
    // do not apply; an index setter spells its value parameter as a named
    // parameter without a default. Before FOREIGN survived preparation the
    // predicate excluded prepared interfaces by the absent FOREIGN; the marker
    // now has to say so explicitly.
    return member.outerDecl != nullptr && member.outerDecl->IsClassLikeDecl() &&
        member.outerDecl->HasAnno(AnnotationKind::ARKTS) &&
        (IsExternalForeign(*member.outerDecl) ||
            member.outerDecl->TestAttr(Attribute::ARKTS_CLASS_WRAPPER));
}

// Scan core shared by the two lowering paths; see the declaration for the
// caller-fixed scope. The last annotated position wins, matching the
// parser's final-parameter rest contract.
size_t ArkTSRestPosition(const std::vector<Ptr<FuncParam>>& params, size_t scanned)
{
    auto restIndex = ARKTS_NO_REST;
    for (size_t i = 0; i < scanned; ++i) {
        for (auto& annotation : params[i]->annotations) {
            if (annotation->kind == AnnotationKind::ARKTS_REST_PARAM && !params[i]->isNamedParam) {
                restIndex = i;
            }
        }
    }
    return restIndex;
}

bool ArkTSDeclOptions(const Decl& decl, std::unordered_map<std::string, std::string>& options,
    ArkTSOptionStatus* status)
{
    if (status != nullptr) {
        *status = ArkTSOptionStatus{};
    }
    // The namespace literal is the anchor of the ill-formed-namespace
    // diagnostic: it is the value the user must fix, not the whole annotation.
    Ptr<Node> namespaceAt = nullptr;
    for (auto& annotation : decl.annotations) {
        if (annotation->kind != AnnotationKind::ARKTS) {
            continue;
        }
        auto layer = CheckArkTSOptionLayer(*annotation);
        if (layer.problem != ArkTSOptionProblem::NONE) {
            if (status != nullptr) {
                *status = layer;
            }
            return false;
        }
        for (auto& arg : annotation->args) {
            // The layer was validated above, so every argument is a literal;
            // a boolean option is stored as its canonical "true"/"false" text.
            const auto& literal = StaticCast<LitConstExpr>(*arg->expr);
            options[arg->name.Val()] = literal.kind == LitConstKind::BOOL
                                           ? (literal.stringValue == "true" ? "true" : "false")
                                           : literal.stringValue;
            if (arg->name.Val() == "namespace") {
                namespaceAt = arg->expr.get();
            }
        }
    }
    // Options arrive as ordered layers: inherited block defaults first, then
    // the declaration's own options. A repeated key within one layer is
    // rejected even when the values are equal; an explicit layer overrides
    // the inherited default. The effective namespace must have no empty
    // leading, trailing or inner segment.
    const auto ns = options.find("namespace");
    if (ns != options.end() && !ArkTSNamespaceValid(ns->second)) {
        if (status != nullptr) {
            status->problem = ArkTSOptionProblem::ILL_FORMED_NAMESPACE;
            status->option = "namespace";
            status->at = namespaceAt;
        }
        return false;
    }
    return true;
}

void DiagnoseArkTSOptionProblem(DiagnosticEngine& diag, Decl& decl,
    const std::unordered_map<std::string, std::string>& options, const ArkTSOptionStatus& status)
{
    auto annotationAt = [&decl]() -> Node& {
        for (auto& annotation : decl.annotations) {
            if (annotation->kind == AnnotationKind::ARKTS) {
                return *annotation;
            }
        }
        return decl;
    };
    auto& at = status.at ? *status.at : annotationAt();
    if (status.problem == ArkTSOptionProblem::DUPLICATE) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_option_duplicated, at, status.option);
    } else if (status.problem == ArkTSOptionProblem::ILL_FORMED_VALUE) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_global_var_value_invalid, at);
    } else if (status.problem == ArkTSOptionProblem::ILL_FORMED_NAMESPACE) {
        // The fold assigns the namespace value into `options` before
        // validating it, so the offending literal is already available here;
        // fall back to the anchored literal if that ever changes.
        std::string namespaceValue = options.count("namespace") ? options.at("namespace") : std::string();
        if (namespaceValue.empty() && status.at != nullptr) {
            if (auto literal = DynamicCast<LitConstExpr*>(status.at.get());
                literal && literal->kind == LitConstKind::STRING) {
                namespaceValue = literal->stringValue;
            }
        }
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_namespace_invalid, at, namespaceValue);
    } else {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_option_invalid, at);
    }
}

bool ResolveArkTSModule(Decl& decl, const std::unordered_map<std::string, std::string>& options, const Package& pkg,
    DiagnosticEngine& diag, DiagKindRefactor invalidKind, std::string& module)
{
    Ptr<Annotation> emptyLayer = nullptr;
    std::string emptyName;
    Ptr<Node> moduleNode = nullptr;
    for (auto& annotation : decl.annotations) {
        if (annotation->kind != AnnotationKind::ARKTS) {
            continue;
        }
        for (auto& arg : annotation->args) {
            if (arg->name.Val() == "module") {
                moduleNode = arg->expr.get();
            }
            auto literal = DynamicCast<LitConstExpr*>(arg->expr.get());
            if (!literal || literal->kind != LitConstKind::STRING || !ArkTSOptionValueEmpty(literal->stringValue)) {
                continue;
            }
            emptyLayer = annotation.get();
            emptyName = arg->name.Val();
            break;
        }
        if (emptyLayer != nullptr) {
            break;
        }
    }
    if (emptyLayer != nullptr) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_option_empty, *emptyLayer, emptyName);
        return false;
    }
    module = options.count("module") ? options.at("module") : ArkTSDefaultModule(pkg.fullPackageName);
    if (options.count("module") != 0 && ArkTSModulePathLike(module)) {
        // The path rule names the declaration kind and anchors at the option
        // literal.
        const bool isClass = invalidKind == DiagKindRefactor::sema_arkts_foreign_class_invalid;
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_module_path_invalid,
            moduleNode ? *moduleNode : static_cast<Node&>(decl), module,
            isClass ? "foreign class" : "foreign interface", decl.identifier.Val());
        return false;
    }
    if (module.empty()) {
        diag.DiagnoseRefactor(invalidKind, decl, decl.identifier.Val(), "empty module name");
        return false;
    }
    return true;
}

bool ArkTSWriteOnlyProp(const Decl& decl)
{
    // The annotation identifies a source declaration; the attribute carries
    // the same contract through the CJO round trip, because the annotation
    // kinds serialized by ASTWriter do not include ARKTS_WRITE_ONLY.
    if (decl.TestAttr(Attribute::ARKTS_WRITE_ONLY_PROP)) {
        return true;
    }
    for (auto& annotation : decl.annotations) {
        if (annotation->kind == AnnotationKind::ARKTS_WRITE_ONLY) {
            return true;
        }
    }
    return false;
}

Ptr<PropDecl> ArkTSWriteOnlyTargetProp(const Node& usage, bool readThroughLeftValue)
{
    if (!CapabilityEnabled() ||
        (usage.astKind != ASTKind::MEMBER_ACCESS && usage.astKind != ASTKind::REF_EXPR) ||
        (usage.TestAttr(Attribute::LEFT_VALUE) && !readThroughLeftValue) || usage.TestAttr(Attribute::COMPILER_ADD)) {
        return nullptr;
    }
    auto target = usage.GetTarget();
    Ptr<PropDecl> prop = DynamicCast<PropDecl*>(target);
    if (!prop) {
        if (auto fd = DynamicCast<FuncDecl*>(target); fd && fd->propDecl) {
            prop = fd->propDecl;
        }
    }
    // Only a property of a prepared wrapper class carries the generated
    // accessor contract; every other placement is outside this requirement and
    // is not diagnosed here.
    if (prop && prop->outerDecl && prop->outerDecl->TestAttr(Attribute::ARKTS_CLASS_WRAPPER)) {
        return prop;
    }
    return nullptr;
}

void CheckArkTSWriteOnlyCompoundTarget(Node& usage, DiagnosticEngine& diag)
{
    auto prop = ArkTSWriteOnlyTargetProp(usage, true);
    if (prop && ArkTSWriteOnlyProp(*prop)) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_write_only_read, usage, prop->identifier.Val());
    }
}

void CheckArkTSWriteOnlyRead(Node& usage, DiagnosticEngine& diag)
{
    auto prop = ArkTSWriteOnlyTargetProp(usage);
    if (prop && ArkTSWriteOnlyProp(*prop)) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_write_only_read, usage, prop->identifier.Val());
    }
}

bool IsArkTSDecl(const Decl& decl)
{
    if (decl.HasAnno(AnnotationKind::ARKTS) || decl.TestAttr(Attribute::ARKTS)) {
        return true;
    }
    return decl.outerDecl != nullptr &&
        (decl.outerDecl->HasAnno(AnnotationKind::ARKTS) || decl.outerDecl->TestAttr(Attribute::ARKTS));
}

namespace {
// The parameter annotations that make a declaration ArkTS-only even when the
// declaration itself carries no @ArkTS layer.
bool IsGatedArkTSParamAnnotation(const Ptr<Annotation>& anno)
{
    return anno != nullptr &&
        (anno->kind == AnnotationKind::ARKTS || anno->kind == AnnotationKind::ARKTS_RAW_VALUE ||
            anno->kind == AnnotationKind::ARKTS_REST_PARAM || anno->kind == AnnotationKind::ARKTS_WRITE_ONLY);
}

bool ArkTSGatedParam(const FuncParam& param)
{
    for (const auto& anno : param.annotations) {
        if (IsGatedArkTSParamAnnotation(anno)) {
            return true;
        }
    }
    return false;
}

bool ArkTSBodyHasGatedParam(const FuncBody& body)
{
    for (const auto& list : body.paramLists) {
        if (list == nullptr) {
            continue;
        }
        for (const auto& param : list->params) {
            if (param != nullptr && ArkTSGatedParam(*param)) {
                return true;
            }
        }
    }
    return false;
}
} // namespace

bool IsGatedArkTSDecl(const Decl& decl)
{
    if (CapabilityEnabled()) {
        return false;
    }
    if (IsArkTSDecl(decl)) {
        return true;
    }
    // A malformed declaration may carry only @RestParam on a parameter; it is
    // still ArkTS-only and must not gain the unsafe-invoke cascade.
    auto fd = DynamicCast<const FuncDecl*>(&decl);
    return fd != nullptr && fd->funcBody != nullptr && ArkTSBodyHasGatedParam(*fd->funcBody);
}

bool IsArkTSReferenceDecl(const Decl& decl)
{
    return decl.TestAttr(Attribute::ARKTS_CLASS_WRAPPER) || IsArkTSPreparedInterface(decl);
}

bool IsArkTSPreparedInterface(const Decl& decl)
{
    return decl.astKind == ASTKind::INTERFACE_DECL && decl.TestAttr(Attribute::ARKTS_INTERFACE_WRAPPER);
}

bool IsArkTSForeignInterface(const InterfaceDecl& id)
{
    return id.HasAnno(AnnotationKind::ARKTS) && id.TestAttr(Attribute::FOREIGN) &&
        !IsArkTSPreparedInterface(id) && (!id.generic || id.generic->typeParameters.empty());
}

bool IsArkTSInterfaceImpl(const Decl& decl)
{
    return decl.astKind == ASTKind::CLASS_DECL && decl.TestAttr(Attribute::ARKTS_INTERFACE_WRAPPER);
}

std::string ArkTSInterfaceImplName(const Decl& iface)
{
    return iface.identifier.Val() + "$arkts_impl";
}

static Ptr<ClassDecl> FindArkTSInterfaceImplInPackage(const Package& pkg, const std::string& name)
{
    for (auto& file : pkg.files) {
        for (auto& decl : file->decls) {
            if (decl->identifier.Val() == name && IsArkTSInterfaceImpl(*decl)) {
                return DynamicCast<ClassDecl*>(decl.get());
            }
        }
    }
    return nullptr;
}

// Resolve the generated implementation class. The current package is searched
// in its own AST; an imported interface is resolved through the package CJO by
// the same deterministic name. A null result is a compilation-local resolution
// failure and makes the caller refuse a generated reference construction.
Ptr<ClassDecl> ArkTSInterfaceImpl(const Decl& iface, ImportManager* imports)
{
    if (!IsArkTSPreparedInterface(iface)) {
        return nullptr;
    }
    const auto name = ArkTSInterfaceImplName(iface);
    if (auto decl = imports ? imports->GetImportedDecl<ClassDecl>(iface.fullPackageName, name) : nullptr) {
        return decl;
    }
    if (iface.curFile && iface.curFile->curPackage) {
        return FindArkTSInterfaceImplInPackage(*iface.curFile->curPackage, name);
    }
    return nullptr;
}

// Walk the package and validate the declaration rules the function fold does
// not own. A lowered wrapper carries ARKTS_FUNCTION_WRAPPER and is validated
// by ArkTSDeclOptions in LowerArkTSForeignFunctions, so it is skipped here.
// Every other declaration with an @ArkTS annotation - a class, an interface,
// a variable, a parameter, an entry - is checked layer by layer, and one
// declaration yields at most one option diagnostic at its first illegal
// layer. In addition, every bodyless member of an @ArkTS foreign class-like
// (a function or primary constructor the fold never lowers) must give each
// named parameter a default value; such a member is marked IS_BROKEN and
// yields one sema_arkts_foreign_named_param_without_default per offending
// parameter. A parameter the parser already rejected, and a signature the
// parser already rejected as rest+named, keep that one diagnostic instead.
// Input -> outcome: '@ArkTS[bogus: "x"] public class C {}' -> one
// sema_arkts_option_invalid on that annotation; '@ArkTS[namespace: "a..b"]' ->
// one sema_arkts_namespace_invalid on the namespace literal;
// '@ArkTS[module: "a"]' -> none; an @ArkTS foreign class bodyless
// 'func f(x!: Int32)' -> one sema_arkts_foreign_named_param_without_default on x.
// Whether a 'module' option value is a relative or OS-absolute file path
// spelling the compiler refuses. A module specifier is otherwise accepted
// verbatim: a system module ("@ohos.util", "@scope/name"), a project module
// ("utils"), a project ETS source path ("entry/src/main/ets/EntryFixture")
// and the separator shapes "a/" and "a//b" all pass; resolving them is the
// runtime module loader's job. The authority design §2.1.1 defines the legal
// value range: a non-empty string that does not start with "./" or "../" and
// contains no backslash. The OS-absolute shapes below are refused by the
// AR-04 base ruling that owns this predicate rather than derived from §2.1.1.
// The rejected shapes, each annotated with the rule that refuses it, follow.
//   any "\"                       backslash (design §2.1.1, tightened by R-D1)
//   "/x"                          OS-absolute (POSIX root; base ruling)
//   "C:/x"                        drive-absolute (base ruling)
//   "./x", "../x"                 dot-relative prefix (design §2.1.1)
// A drive-relative spelling ("C:x", no separator after the colon) is not
// matched: there is no directory separator, so it stays a plain specifier
// and its resolution failure is left to the runtime like any unknown module.
bool ArkTSModulePathLike(const std::string& module)
{
    if (module.empty()) {
        return false;
    }
    if (module.find('\\') != std::string::npos) {
        return true;
    }
    if (module.front() == '/') {
        return true;
    }
    const bool drive = module.size() > 2 && module[1] == ':' && module[2] == '/' &&
        ((module[0] >= 'A' && module[0] <= 'Z') || (module[0] >= 'a' && module[0] <= 'z'));
    if (drive) {
        return true;
    }
    if (module.front() != '.') {
        return false;
    }
    // A bare dot segment is relative in the same sense as "./x" and "../x";
    // the module loader would reject it after installation, so the format
    // check rejects it here together with the prefixed forms.
    if (module == "." || module == "..") {
        return true;
    }
    if (module.size() > 1 && module[1] == '/') {
        return true;
    }
    return module.rfind("../", 0) == 0;
}

namespace {
// The per-layer result of the annotation scan: the layer status plus the
// first offending argument the layer-level check does not already describe.
struct ArkTSOptionLayerIssue {
    bool valid{true};
    std::string emptyName;
    // The namespace literal is the anchor of the ill-formed-namespace
    // diagnostic, so it is kept apart from the annotation-level fallback.
    Ptr<Node> namespaceAt{nullptr};
    std::string namespaceValue;
};

ArkTSOptionLayerIssue ScanArkTSOptionLayer(const Annotation& annotation, const ArkTSOptionStatus& layer)
{
    ArkTSOptionLayerIssue issue;
    issue.valid = layer.problem == ArkTSOptionProblem::NONE;
    if (!issue.valid) {
        return issue;
    }
    for (const auto& arg : annotation.args) {
        const auto& literal = StaticCast<LitConstExpr>(*arg->expr);
        const auto name = arg->name.Val();
        // The boolean option carries no string value, so the empty-value rule
        // does not apply to it.
        if (name == "globalVar") {
            continue;
        }
        if (name == "namespace" && !ArkTSNamespaceValid(literal.stringValue)) {
            issue.valid = false;
            issue.namespaceAt = arg->expr.get();
            issue.namespaceValue = literal.stringValue;
            break;
        }
        // An explicitly empty value is a separate mistake from a malformed
        // namespace or an unknown option: it is fully spelled and would
        // silently reach the runtime.
        if (ArkTSOptionValueEmpty(literal.stringValue)) {
            issue.emptyName = name;
            break;
        }
    }
    return issue;
}

void ReportArkTSOptionLayerIssue(const Ptr<Annotation>& annotation, const ArkTSOptionStatus& layer,
    const ArkTSOptionLayerIssue& issue, DiagnosticEngine& diag)
{
    if (!issue.valid) {
        // A repeated key has its own message: both the key and the value shape
        // are legal, only the repetition is not. An ill-formed namespace also
        // has its own message.
        if (layer.problem == ArkTSOptionProblem::DUPLICATE) {
            auto& at = layer.at ? *layer.at : static_cast<Node&>(*annotation);
            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_option_duplicated, at, layer.option);
        } else if (layer.problem == ArkTSOptionProblem::ILL_FORMED_VALUE) {
            auto& at = layer.at ? *layer.at : static_cast<Node&>(*annotation);
            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_global_var_value_invalid, at);
        } else if (issue.namespaceAt != nullptr) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_namespace_invalid, *issue.namespaceAt,
                issue.namespaceValue);
        } else {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_option_invalid, *annotation);
        }
        return;
    }
    if (!issue.emptyName.empty()) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_option_empty, *annotation, issue.emptyName);
    }
}

void DiagnoseArkTSOptionLayers(const Decl& decl, DiagnosticEngine& diag)
{
    for (const auto& annotation : decl.annotations) {
        if (annotation->kind != AnnotationKind::ARKTS) {
            continue;
        }
        auto layer = CheckArkTSOptionLayer(*annotation);
        auto issue = ScanArkTSOptionLayer(*annotation, layer);
        if (!issue.valid || !issue.emptyName.empty()) {
            ReportArkTSOptionLayerIssue(annotation, layer, issue, diag);
            break;
        }
    }
}

void DiagnoseArkTSForeignMemberParam(Decl& decl, const FuncParam& param, bool restParam, DiagnosticEngine& diag)
{
    if (param.isNamedParam && param.assignment == nullptr) {
        // A malformed parameter or a rest+named signature was already rejected;
        // do not restate a different rule on top of that diagnostic.
        if (!restParam && !ArkTSParamIsBroken(param)) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_named_param_without_default, param,
                param.identifier.Val(), decl.identifier.Val());
        }
        decl.EnableAttr(Attribute::IS_BROKEN);
        return;
    }
    if (DiagnoseArkTSDefaultedNamedParamNotOption(diag, param, decl)) {
        decl.EnableAttr(Attribute::IS_BROKEN);
    }
}

// Bodyless members of an external @ArkTS foreign class-like carry no marker of
// their own: Sema propagates FOREIGN to them and the @ArkTS annotation stays on
// the enclosing declaration, so the owner is consulted (the same shape the
// parser uses to accept a bodyless init). Their named parameters must have a
// default value for the same reason as a top-level wrapper: the generated
// binding cannot tell an omitted named argument from an explicitly passed
// value. A member has no wrapper of its own, so the declaration is only marked
// broken.
// The value parameter of an index setter is the one exemption. The value is
// required, and the design spells it as a named parameter without a default -
// the exact shape ClassifyArkTSIndexSignature accepts and lowers into
// setProperty; the interface path already relies on the same spelling. A
// defaulted value parameter still reaches the operator rule of the language
// (sema_cannot_have_default_param) and a positional one the index shape
// refusal, so only this legal spelling skips the rule below.
bool ArkTSIndexSetterValueSpelling(const FuncDecl& fd, const FuncBody& body)
{
    if (fd.op != TokenKind::LSQUARE || body.paramLists.empty() || body.paramLists[0] == nullptr) {
        return false;
    }
    // An index setter spells its value as one named parameter after the key.
    constexpr size_t indexSetterParamCount = 2;
    const auto& params = body.paramLists[0]->params;
    return params.size() == indexSetterParamCount && params[1] != nullptr && params[1]->isNamedParam &&
        params[1]->assignment == nullptr;
}

void DiagnoseArkTSBodylessForeignMember(Decl& decl, DiagnosticEngine& diag)
{
    FuncBody* memberBody = nullptr;
    FuncDecl* memberFunc = nullptr;
    if (auto fd = DynamicCast<FuncDecl*>(&decl)) {
        memberFunc = fd;
        memberBody = fd->funcBody.get();
    } else if (auto ctor = DynamicCast<PrimaryCtorDecl*>(&decl)) {
        // Defensive coverage: an @ArkTS foreign class never declares a primary
        // constructor today, so this branch is unreachable from source; it
        // stays here so a future parser change cannot silently bypass the rule.
        // The 'init' spelling is a FuncDecl (covered by
        // BodylessForeignClassMemberNamedParameterWithoutDefaultIsRejected).
        memberBody = ctor->funcBody.get();
    }
    if (memberBody == nullptr || memberBody->body != nullptr || !ArkTSForeignClassLikeMember(decl)) {
        return;
    }
    // A rest parameter forbids named parameters outright; the parser already
    // reported that mix, so this rule stays silent. The member is still marked
    // broken: the shape is rejected either way.
    const bool restParam = ArkTSBodyHasRestParam(*memberBody);
    // An index setter's value parameter is required; see
    // ArkTSIndexSetterValueSpelling for the shape that skips the rule below.
    const bool indexSetterValue = memberFunc != nullptr && ArkTSIndexSetterValueSpelling(*memberFunc, *memberBody);
    for (size_t listIndex = 0; listIndex < memberBody->paramLists.size(); ++listIndex) {
        auto& list = memberBody->paramLists[listIndex];
        if (list == nullptr) {
            continue;
        }
        for (size_t i = 0; i < list->params.size(); ++i) {
            auto& param = list->params[i];
            if (param == nullptr || (indexSetterValue && listIndex == 0 && i == 1)) {
                continue;
            }
            DiagnoseArkTSForeignMemberParam(decl, *param, restParam, diag);
        }
    }
}
} // namespace

void ArkTSDeclOptionsDiagnose(Package& pkg, DiagnosticEngine& diag)
{
    Walker(&pkg, [&diag](Ptr<Node> node) {
        auto decl = DynamicCast<Decl*>(node);
        if (decl == nullptr ||
            decl->TestAnyAttr(Attribute::IS_BROKEN, Attribute::ARKTS_FUNCTION_WRAPPER,
                Attribute::ARKTS_CLASS_WRAPPER, Attribute::ARKTS_INTERFACE_WRAPPER)) {
            return VisitAction::WALK_CHILDREN;
        }
        DiagnoseArkTSOptionLayers(*decl, diag);
        DiagnoseArkTSBodylessForeignMember(*decl, diag);
        return VisitAction::WALK_CHILDREN;
    }).Walk();
}


// Option constructors are spelled as qualified members of the std.core
// enum: Some/None are the enum's constructors, not package members, so no
// import lookup can pin a bare Some/None - a same-named source function
// would capture it. `Option.Some(x)` / `Option.None` (also inside match
// patterns) leave only `Option` as a name to pin to std.core, and the
// constructor resolves through the member access.
static constexpr const char* const ARKTS_OPTION = "Option";

// Failure categories: kind checks come first, then value checks, and the
// message states which category fired.
static constexpr const char* const ARKTS_MISMATCH = "ArkTS type mismatch: ";
static constexpr const char* const ARKTS_INVALID = "ArkTS value invalid: ";

// A never-typed position that unexpectedly produces a value is an ArkTS
// runtime failure (Appendix B: an ArkTS-side failure reaches Cangjie as
// ForeignRuntimeException), not a value conversion, so it bypasses the
// conversion exception.
static constexpr const char* const ARKTS_NEVER_EXCEPTION = ARKTS_RUNTIME_EXCEPTION;
static constexpr const char* const ARKTS_NEVER_MESSAGE = "ArkTS never function returned";

// ---- Verified ohos.ark_interop array face ----
// Array conversions use only the real library API, never compiler-owned
// helper names: ctx.array(Array<ark_interop.JSValue>) packages converted values,
// JSHeapObject.toJSValue() turns a JSArray into a call argument,
// value.isArray() is the kind predicate, asArray().size the element
// count and getElement(index) one element. std.core Array<T> is a
// fixed-length struct with no append member, so both copy directions
// build their buffer through the size-and-initializer constructor with
// the source length and one read per element; the preinstalled
// JSInteropByte byte-view extend is a different semantics, not used.

// The undefined checks below use the isNull()/isUndefined() predicates of
// the real ohos.ark_interop face, matching the library's own
// Option<T>.fromJSValue path; those predicates run the shared
// lifecycle/thread check of the library.

// ArkTSJoin and ArkTSTypeDescName live in Utils/TypeMapper: the
// description spelling is part of the type-mapping surface and is shared
// with the classification predicates.

// One emitted conversion: statements that must run before the expression
// ("pre", each line already indented for its final position) plus the value
// expression itself. Composite nodes embed their children's pre-statements
// inside the construct that consumes them, one nesting level deeper.
struct ArkTSEmitted {
    std::string pre;
    std::string expr;
};

// Recursive conversion emitter. Every generated local is prefixed with the
// parameter-disjoint prefix chosen by ArkTSForeignLocalPrefix and suffixed
// with a per-role counter, so source identifiers can never collide with
// generated names and each role keeps stable, predictable numbering; user
// values only enter as AST-bound placeholder references.
struct ArkTSValueEmitter {
    std::string prefix;
    std::unordered_map<std::string, size_t> nextLocal{};
    // Reference conversion target: placeholder identifier -> class declaration.
    // The lowering walker rebinds each recorded placeholder to the class as an
    // AST value, so no user identifier enters generated syntax.
    std::unordered_map<std::string, Ptr<Decl>>* classRefs{nullptr};
    // Set when a leaf conversion cannot produce a value (a reference or enum
    // position without its declaration, binding map or value table). The
    // emission such a leaf leaves behind is not spellable source, so every
    // generation entry point refuses the whole body when this is set instead
    // of splicing the broken conversion into the generated code.
    bool broken{false};

    std::string Fresh(const std::string& role)
    {
        return prefix + role + "_" + std::to_string(nextLocal[role]++);
    }

    // Type spelling for generated locals: the composite recursion is shared
    // with ArkTSTypeDescName, and a reference leaf is recorded as a
    // placeholder for the lowering walker to bind. Without the placeholder map
    // the source is refused (empty type argument) instead of splicing a user
    // class identifier.
    std::string TypeName(const ArkTSTypeDesc& desc)
    {
        return ArkTSTypeDescName(desc, [this](const ArkTSTypeDesc& reference) {
            if (reference.reference == nullptr || classRefs == nullptr) {
                return std::string{};
            }
            auto slot = Fresh("type");
            (*classRefs)[slot] = reference.reference;
            return slot;
        });
    }

    ArkTSEmitted ToJS(const ArkTSTypeDesc& desc, const std::string& source, const std::string& ctx,
        const std::string& indent)
    {
        ArkTSEmitted emitted;
        if (desc.kind == ArkTSTypeDesc::Kind::SCALAR) {
            return ToJSScalar(desc, source, ctx, indent);
        }
        if (desc.kind == ArkTSTypeDesc::Kind::OPTIONAL) {
            return ToJSOptional(desc, source, ctx, indent);
        }
        if (desc.kind == ArkTSTypeDesc::Kind::REFERENCE) {
            // The stored reference is a global handle; toJSValue() gives the
            // ArkTS side a local view of the same object, preserving identity.
            emitted.expr = "(" + source + ")." + ARKTS_CLASS_REF_ACCESSOR + "().toJSValue()";
            return emitted;
        }
        if (desc.kind == ArkTSTypeDesc::Kind::ENUM) {
            return ToJSEnum(desc, source, ctx, indent);
        }
        if (desc.kind == ArkTSTypeDesc::Kind::ARRAY_COPY) {
            return ToJSArrayCopy(desc, source, ctx, indent);
        }
        if (desc.kind == ArkTSTypeDesc::Kind::TUPLE) {
            return ToJSTuple(desc, source, ctx, indent);
        }
        return emitted;
    }

    ArkTSEmitted ToJSScalar(const ArkTSTypeDesc& desc, const std::string& source, const std::string& ctx,
        const std::string& indent)
    {
        ArkTSEmitted emitted;
        // Hoist the bound constructor out of the argument subtree of
        // ctx.bigint: speculative overload checking clears and re-resolves
        // that subtree, which would drop the BigInt conversion.
        if (desc.scalar == "UInt64") {
            auto big = Fresh("bigint");
            emitted.pre = indent + "let " + big + " = BigInt(" + source + ")\n";
            emitted.expr = ctx + ".bigint(" + big + ").toJSValue()";
            return emitted;
        }
        emitted.expr = ArkTSScalarToJS(desc.scalar, ctx, source);
        return emitted;
    }

    ArkTSEmitted ToJSOptional(const ArkTSTypeDesc& desc, const std::string& source, const std::string& ctx,
        const std::string& indent)
    {
        ArkTSEmitted emitted;
        auto bound = Fresh("some");
        auto inner = ToJS(desc.elements.front(), bound, ctx, indent + "        ");
        emitted.expr = "match (" + source + ") {\n" + indent + "    case " + ARKTS_OPTION + ".Some(" + bound +
            ") =>\n" + inner.pre + indent + "        " + inner.expr + "\n" + indent + "    case " + ARKTS_OPTION +
            ".None => " + ctx + ".undefined().toJSValue()\n" + indent + "}";
        return emitted;
    }

    ArkTSEmitted ToJSEnum(const ArkTSTypeDesc& desc, const std::string& source, const std::string& ctx,
        const std::string& indent)
    {
        ArkTSEmitted emitted;
        // A plain enum maps each constructor to its @RawValue spelling:
        // a numeric member becomes ctx.number(Float64(<value>)), a string
        // member ctx.string("<value>"), a boolean member ctx.boolean(true/false).
        // The enum itself is recorded by placeholder so the lowering walker
        // binds the constructor reference without splicing the enum's own
        // name; the member name resolves through that member access, the
        // same way Option.Some resolves through the pinned Option.
        if (desc.reference == nullptr || classRefs == nullptr || desc.enumMembers.empty()) {
            return emitted;
        }
        auto slot = Fresh("enum");
        (*classRefs)[slot] = desc.reference;
        std::string body = "match (" + source + ") {\n";
        for (const auto& member : desc.enumMembers) {
            if (member.kind == ArkTSEnumValueKind::PAYLOAD && !member.payload.empty()) {
                auto bound = Fresh("payload");
                auto inner = ToJS(member.payload.front(), bound, ctx, indent + "        ");
                body += indent + "    case " + slot + "." + member.name + "(" + bound + ") =>\n" + inner.pre +
                    indent + "        " + inner.expr + "\n";
                continue;
            }
            std::string converted;
            if (member.kind == ArkTSEnumValueKind::STRING) {
                converted =
                    ctx + ".string(\"" + ArkTSUserStringSpelling(member.value) + "\").toJSValue()";
            } else if (member.kind == ArkTSEnumValueKind::BOOLEAN) {
                converted = ctx + ".boolean(" + member.value + ").toJSValue()";
            } else {
                converted = ctx + ".number(Float64(" + member.value + ")).toJSValue()";
            }
            body += indent + "    case " + slot + "." + member.name + " => " + converted + "\n";
        }
        body += indent + "}";
        emitted.expr = body;
        return emitted;
    }

    ArkTSEmitted ToJSArrayCopy(const ArkTSTypeDesc& desc, const std::string& source, const std::string& ctx,
        const std::string& indent)
    {
        ArkTSEmitted emitted;
        auto elements = Fresh("elems");
        auto index = Fresh("index");
        auto item = Fresh("elem");
        auto inner = ToJS(desc.elements.front(), item, ctx, indent + "    ");
        emitted.pre = indent + "let " + elements + " = Array<" + ARKTS_JS_VALUE + ">(" + source + ".size, { " +
            index + " =>\n" + indent + "    let " + item + " = " + source + "[" + index + "]\n" + inner.pre +
            indent + "    " + inner.expr + "\n" + indent + "})\n";
        emitted.expr = ctx + ".array(" + elements + ").toJSValue()";
        return emitted;
    }

    ArkTSEmitted ToJSTuple(const ArkTSTypeDesc& desc, const std::string& source, const std::string& ctx,
        const std::string& indent)
    {
        ArkTSEmitted emitted;
        std::vector<std::string> converted;
        for (size_t i = 0; i < desc.elements.size(); ++i) {
            auto part = Fresh("part");
            auto inner = ToJS(desc.elements[i], source + "[" + std::to_string(i) + "]", ctx, indent);
            emitted.pre += inner.pre;
            emitted.pre += indent + "let " + part + " = " + inner.expr + "\n";
            converted.emplace_back(std::move(part));
        }
        emitted.expr = ctx + ".array([" + ArkTSJoin(converted, ", ") + "]).toJSValue()";
        return emitted;
    }

    // (value, typeName) -> 'if (value.isNull()) { throw
    // ExternConversionException("ArkTS type mismatch: null for <typeName>")
    // }': a JS null in a non-optional position is a type mismatch (kind
    // checks come first); JS undefined is rejected through the isUndefined()
    // predicate wherever it is not a legal value.
    std::string NullGuard(const std::string& value, const std::string& typeName, const std::string& indent)
    {
        return indent + "if (" + value + ".isNull()) {\n" +
            ArkTSConversionThrow(std::string(ARKTS_MISMATCH) + "null for " + typeName, indent + "    ") + indent +
            "}\n";
    }

    // (value, typeName) -> 'if (value.isUndefined()) { throw
    // ExternConversionException("ArkTS type mismatch: undefined for <typeName>")
    // }': a non-optional reference position rejects JS undefined explicitly;
    // the later isObject guard stays as the general kind check.
    std::string UndefinedGuard(const std::string& value, const std::string& typeName, const std::string& indent)
    {
        return indent + "if (" + value + ".isUndefined()) {\n" +
            ArkTSConversionThrow(std::string(ARKTS_MISMATCH) + "undefined for " + typeName, indent + "    ") +
            indent + "}\n";
    }

    // (value, predicate, kind, typeName) -> 'if (!value.<predicate>()) {
    // throw ExternConversionException("ArkTS type mismatch: not <kind> for
    // <typeName>") }': a wrong-kind value is a type mismatch, so every
    // from-JS accessor is preceded by the matching kind predicate of the
    // real ohos.ark_interop face (isNumber/isString/isBoolean/isArray/
    // isBigInt); the accessor's own mismatch exception stays behind the
    // guard as a redundant safety net. Unlike the null guard this is
    // never skipped inside a Some branch: a non-null value can still have
    // the wrong kind.
    std::string KindGuard(const std::string& value, const std::string& predicate, const std::string& kind,
        const std::string& typeName, const std::string& indent)
    {
        return indent + "if (!" + value + "." + predicate + "()) {\n" +
            ArkTSConversionThrow(std::string(ARKTS_MISMATCH) + "not " + kind + " for " + typeName, indent + "    ",
                ArkTSValueDescription(value)) +
            indent + "}\n";
    }

    // From-JS conversion per kind (nullablePosition: the value is already
    // known non-null, so the null guard is skipped; the kind guard never is):
    //   scalar -> the ScalarFromJS rows below
    //   Option<T'> -> null or undefined is Option.None, otherwise Option.Some(convert)
    //   Array<T'> -> null and isArray guards, the length from asArray().size,
    //             then the per-element copy through getElement into the
    //             size-and-initializer constructor
    //   tuple -> isArray guard plus an exact-arity check, then per-element conversion
    // Before: Option<String> __locals_result
    // After:  "(if (...isNull() || ...isUndefined()) { Option.None } else { Option.Some(<converted>) })"
    ArkTSEmitted FromJS(const ArkTSTypeDesc& desc, const std::string& value, const std::string& indent,
        bool nullablePosition = false)
    {
        switch (desc.kind) {
            case ArkTSTypeDesc::Kind::SCALAR:
                return ScalarFromJS(desc.scalar, value, indent, nullablePosition);
            case ArkTSTypeDesc::Kind::OPTIONAL:
                return FromJSOptional(desc, value, indent);
            case ArkTSTypeDesc::Kind::REFERENCE:
                return FromJSReference(desc, value, indent, nullablePosition);
            case ArkTSTypeDesc::Kind::ENUM:
                return FromJSEnum(desc, value, indent, nullablePosition);
            case ArkTSTypeDesc::Kind::ARRAY_COPY:
                return FromJSArrayCopy(desc, value, indent, nullablePosition);
            case ArkTSTypeDesc::Kind::TUPLE:
                return FromJSTuple(desc, value, indent, nullablePosition);
            default:
                return {};
        }
    }

    // Option<T'> from-JS pseudocode: JS null and JS undefined both map to
    // Option.None, checked through the isNull() and isUndefined() predicates;
    // anything else reaches Option.Some with the element's own kind and value
    // checks inside the branch, where the value is already known non-null.
    ArkTSEmitted FromJSOptional(const ArkTSTypeDesc& desc, const std::string& value, const std::string& indent)
    {
        ArkTSEmitted emitted;
        auto inner = FromJS(desc.elements.front(), value, indent + "    ", true);
        emitted.expr = "(if (" + value + ".isNull() || " + value + ".isUndefined()) {\n" + indent + "    " +
            ARKTS_OPTION + ".None\n" + indent + "} else {\n" + inner.pre + indent + "    " + ARKTS_OPTION +
            ".Some(" + inner.expr + ")\n" + indent + "})";
        return emitted;
    }

    // An ArkTS object reference: null and undefined are not instances, and the
    // value must be an object before the wrapping constructor is called. The
    // class is recorded by placeholder so the lowering walker can bind it
    // without spelling its name here. An interface reference is constructed
    // through its generated implementation class; a class reference through
    // its wrapper constructor.
    ArkTSEmitted FromJSReference(const ArkTSTypeDesc& desc, const std::string& value, const std::string& indent,
        bool nullablePosition)
    {
        ArkTSEmitted emitted;
        if (!desc.reference || classRefs == nullptr) {
            broken = true;
            return emitted;
        }
        Ptr<Decl> constructor = desc.reference;
        if (IsArkTSPreparedInterface(*desc.reference)) {
            if (!desc.referenceImpl) {
                broken = true;
                return emitted;
            }
            constructor = desc.referenceImpl;
        }
        auto slot = Fresh("class");
        (*classRefs)[slot] = constructor;
        if (!nullablePosition) {
            emitted.pre += NullGuard(value, ArkTSTypeDescName(desc), indent);
            emitted.pre += UndefinedGuard(value, ArkTSTypeDescName(desc), indent);
        }
        emitted.pre += KindGuard(value, "isObject", "an object", ArkTSTypeDescName(desc), indent);
        emitted.expr = slot + "(" + value + ".asObject(), ())";
        return emitted;
    }

    static std::string EnumThrowInline(const std::string& reason)
    {
        return std::string("throw ") + ARKTS_CONVERSION_EXCEPTION + "(\"" +
            StringConvertor::RecoverForArkTSSource(reason) + "\")";
    }

    static void NoteEnumKindPredicate(std::vector<std::string>& kindPredicates, const std::string& predicate)
    {
        if (predicate.empty() || predicate == "true" || predicate == "false") {
            return;
        }
        if (std::find(kindPredicates.begin(), kindPredicates.end(), predicate) == kindPredicates.end()) {
            kindPredicates.push_back(predicate);
        }
    }

    static std::string EnumScalarPayloadGuard(const std::string& scalar, const std::string& value,
        std::vector<std::string>& kindPredicates)
    {
        if (scalar == "String") {
            NoteEnumKindPredicate(kindPredicates, value + ".isString()");
            return value + ".isString()";
        }
        if (scalar == "Bool") {
            NoteEnumKindPredicate(kindPredicates, value + ".isBoolean()");
            return value + ".isBoolean()";
        }
        if (scalar == "Int64" || scalar == "UInt64" || scalar == "BigInt") {
            NoteEnumKindPredicate(kindPredicates, value + ".isBigInt()");
            return value + ".isBigInt()";
        }
        if (scalar == "Unit") {
            return "(" + value + ".isNull() || " + value + ".isUndefined())";
        }
        if (scalar == "Nothing") {
            return "false";
        }
        NoteEnumKindPredicate(kindPredicates, value + ".isNumber()");
        return value + ".isNumber()";
    }

    // A nested enum can be pre-checked only when every constructor agrees on
    // the JS kind; a mixed union stays unroutable and is matched in declaration
    // order.
    static std::string EnumNestedEnumGuard(const ArkTSTypeDesc& payload, const std::string& value,
        std::vector<std::string>& kindPredicates)
    {
        std::string common;
        for (const auto& member : payload.enumMembers) {
            std::string current;
            if (member.kind == ArkTSEnumValueKind::NUMBER) {
                current = value + ".isNumber()";
            } else if (member.kind == ArkTSEnumValueKind::STRING) {
                current = value + ".isString()";
            } else if (member.kind == ArkTSEnumValueKind::BOOLEAN) {
                current = value + ".isBoolean()";
            } else if (member.kind == ArkTSEnumValueKind::PAYLOAD && !member.payload.empty()) {
                current = EnumPayloadGuard(member.payload.front(), value, kindPredicates);
            }
            if (current.empty() || current == "true") {
                return "true";
            }
            if (common.empty()) {
                common = current;
            } else if (common != current) {
                return "true";
            }
        }
        if (common.empty()) {
            return "true";
        }
        NoteEnumKindPredicate(kindPredicates, common);
        return common;
    }

    static std::string EnumPayloadGuard(const ArkTSTypeDesc& payload, const std::string& value,
        std::vector<std::string>& kindPredicates)
    {
        switch (payload.kind) {
            case ArkTSTypeDesc::Kind::SCALAR:
                return EnumScalarPayloadGuard(payload.scalar, value, kindPredicates);
            case ArkTSTypeDesc::Kind::REFERENCE:
                NoteEnumKindPredicate(kindPredicates, value + ".isObject()");
                return value + ".isObject()";
            case ArkTSTypeDesc::Kind::ARRAY_COPY:
            case ArkTSTypeDesc::Kind::TUPLE:
                NoteEnumKindPredicate(kindPredicates, value + ".isArray()");
                return value + ".isArray()";
            case ArkTSTypeDesc::Kind::OPTIONAL: {
                if (payload.elements.empty()) {
                    return "true";
                }
                const auto inner = EnumPayloadGuard(payload.elements.front(), value, kindPredicates);
                if (inner == "true") {
                    return "true";
                }
                return "(" + value + ".isNull() || " + value + ".isUndefined() || (" + inner + "))";
            }
            case ArkTSTypeDesc::Kind::ENUM:
                return EnumNestedEnumGuard(payload, value, kindPredicates);
            default:
                return "true";
        }
    }

    static std::string EnumLiteralGuard(const ArkTSEnumMember& member, const std::string& value,
        std::vector<std::string>& kindPredicates)
    {
        switch (member.kind) {
            case ArkTSEnumValueKind::NUMBER:
                NoteEnumKindPredicate(kindPredicates, value + ".isNumber()");
                return value + ".isNumber() && " + value + ".toNumber() == Float64(" + member.value + ")";
            case ArkTSEnumValueKind::STRING:
                NoteEnumKindPredicate(kindPredicates, value + ".isString()");
                return value + ".isString() && " + value + ".asString().toString() == \"" +
                    ArkTSUserStringSpelling(member.value) + "\"";
            case ArkTSEnumValueKind::BOOLEAN:
                NoteEnumKindPredicate(kindPredicates, value + ".isBoolean()");
                return value + ".isBoolean() && " + value + ".toBoolean() == " + member.value;
            default:
                return "";
        }
    }

    static void NoteEnumKindLabel(std::string& kindList, const std::string& label)
    {
        if (kindList.find(label) != std::string::npos) {
            return;
        }
        if (!kindList.empty()) {
            kindList += ", ";
        }
        kindList += label;
    }

    static void AddEnumBranch(std::string& dispatch, bool& firstGuard, const std::string& indent,
        const std::string& condition, const std::string& body)
    {
        dispatch += (firstGuard ? "if (" : " else if (") + condition + ") {\n" + body + indent + "}";
        firstGuard = false;
    }

    // Append one PAYLOAD constructor branch. Returns false when the payload
    // shadows every later constructor and the caller must stop the member loop.
    bool AppendEnumPayloadBranch(const ArkTSEnumMember& member, const std::string& value, const std::string& indent,
        const std::string& slot, std::vector<std::string>& kindPredicates, std::string& dispatch, bool& firstGuard,
        bool& shadowed, std::string& kindList)
    {
        if (member.payload.empty()) {
            return true;
        }
        const auto guard = EnumPayloadGuard(member.payload.front(), value, kindPredicates);
        auto inner = FromJS(member.payload.front(), value, indent + "    ");
        std::string body = inner.pre + indent + "    " + slot + "." + member.name + "(" + inner.expr + ")\n";
        if (guard == "true") {
            // An unroutable payload catches every remaining value; later
            // constructors are shadowed by declaration order.
            if (firstGuard) {
                // The body may carry guard/conversion statements, so it needs a
                // block expression rather than a parenthesized statement list.
                dispatch = "{\n" + body + indent + "}";
            } else {
                dispatch += " else {\n" + body + indent + "})";
            }
            shadowed = true;
            return false;
        }
        AddEnumBranch(dispatch, firstGuard, indent, guard, body);
        NoteEnumKindLabel(kindList, "payload");
        return true;
    }

    // Close the dispatch with the invalid-value and mismatch fallbacks; a
    // shadowing payload or an empty dispatch keeps it as assembled.
    static std::string FinishEnumDispatch(std::string dispatch, const std::vector<std::string>& kindPredicates,
        const std::string& kindList, const std::string& name, const std::string& indent, bool shadowed,
        bool firstGuard)
    {
        if (shadowed || firstGuard) {
            return dispatch;
        }
        std::string predicate;
        for (const auto& one : kindPredicates) {
            if (!predicate.empty()) {
                predicate += " || ";
            }
            predicate += one;
        }
        if (!predicate.empty()) {
            // A value of a constructible kind that matched no constructor is
            // invalid; any other kind is a mismatch.
            dispatch += " else if (" + predicate + ") {\n" + indent + "    " +
                EnumThrowInline(std::string(ARKTS_INVALID) + "unknown value for " + name) + "\n" + indent + "}";
        }
        dispatch += " else {\n" + indent + "    " +
            EnumThrowInline(std::string(ARKTS_MISMATCH) + "not a " + kindList + " for " + name) + "\n" + indent + "})";
        return dispatch;
    }

    // From-JS enum conversion: a numeric member matches the JS number exactly
    // against Float64(value), a string member matches the JS string exactly
    // against the value. The first member whose value matches wins, so a
    // duplicate value maps to the earliest constructor; an unmatched value
    // throws the mismatch exception named with the enum.
    ArkTSEmitted FromJSEnum(const ArkTSTypeDesc& desc, const std::string& value, const std::string& indent,
        bool nullablePosition)
    {
        ArkTSEmitted emitted;
        if (desc.reference == nullptr || classRefs == nullptr || desc.enumMembers.empty()) {
            broken = true;
            return emitted;
        }
        auto slot = Fresh("enum");
        (*classRefs)[slot] = desc.reference;
        const auto name = ArkTSTypeDescName(desc);
        if (!nullablePosition) {
            emitted.pre += NullGuard(value, name, indent);
        }
        // Runtime matching follows design §7.2: constructors are tried in
        // declaration order, a literal matches by value and a payload by its
        // mapped type, and the first match wins. A literal that does not match
        // must fall through to later constructors of the same kind; throwing
        // inside the literal branch would make a same-kind payload unreachable.
        std::vector<std::string> kindPredicates;
        std::string dispatch = "(";
        bool firstGuard = true;
        bool shadowed = false;
        std::string kindList;
        for (const auto& member : desc.enumMembers) {
            if (member.kind == ArkTSEnumValueKind::PAYLOAD) {
                if (!AppendEnumPayloadBranch(member, value, indent, slot, kindPredicates, dispatch, firstGuard,
                    shadowed, kindList)) {
                    break;
                }
                continue;
            }
            const auto guard = EnumLiteralGuard(member, value, kindPredicates);
            if (guard.empty()) {
                continue;
            }
            AddEnumBranch(dispatch, firstGuard, indent, guard, indent + "    " + slot + "." + member.name + "\n");
            const std::string label = member.kind == ArkTSEnumValueKind::NUMBER ? "number"
                : member.kind == ArkTSEnumValueKind::STRING                      ? "string"
                                                                                : "boolean";
            NoteEnumKindLabel(kindList, label);
        }
        emitted.expr = FinishEnumDispatch(dispatch, kindPredicates, kindList, name, indent, shadowed, firstGuard);
        return emitted;
    }

    ArkTSEmitted FromJSArrayCopy(const ArkTSTypeDesc& desc, const std::string& value, const std::string& indent,
        bool nullablePosition)
    {
        ArkTSEmitted emitted;
        auto length = Fresh("length");
        auto copy = Fresh("copy");
        auto index = Fresh("index");
        auto item = Fresh("item");
        auto inner = FromJS(desc.elements.front(), item, indent + "    ");
        if (!nullablePosition) {
            emitted.pre += NullGuard(value, ArkTSTypeDescName(desc), indent);
        }
        emitted.pre += KindGuard(value, "isArray", "an array", ArkTSTypeDescName(desc), indent);
        emitted.pre += indent + "let " + length + " = " + value + ".asArray().size\n";
        // Reference elements keep an explicit type argument so the initializer
        // lambda stays inferable, but the argument is a placeholder bound by
        // the lowering walker: no class name is spliced into generated syntax.
        auto elementType = "<" + TypeName(desc.elements.front()) + ">";
        emitted.pre += indent + "let " + copy + " = Array" + elementType + "(" + length + ", { " + index +
            " =>\n" + indent + "    let " + item + " = " + value + ".getElement(" + index + ")\n" + inner.pre +
            indent + "    " + inner.expr + "\n" + indent + "})\n";
        emitted.expr = copy;
        return emitted;
    }

    ArkTSEmitted FromJSTuple(const ArkTSTypeDesc& desc, const std::string& value, const std::string& indent,
        bool nullablePosition)
    {
        ArkTSEmitted emitted;
        if (!nullablePosition) {
            emitted.pre += NullGuard(value, ArkTSTypeDescName(desc), indent);
        }
        emitted.pre += KindGuard(value, "isArray", "an array", ArkTSTypeDescName(desc), indent);
        auto length = Fresh("length");
        emitted.pre += indent + "let " + length + " = " + value + ".asArray().size\n";
        emitted.pre += indent + "if (" + length + " != " + std::to_string(desc.elements.size()) + ") {\n" +
            ArkTSConversionThrow(std::string(ARKTS_MISMATCH) + "not a " + std::to_string(desc.elements.size()) +
                "-element array for " + ArkTSTypeDescName(desc), indent + "    ") +
            indent + "}\n";
        std::vector<std::string> converted;
        for (size_t i = 0; i < desc.elements.size(); ++i) {
            // Read each element exactly once through getElement, then let the
            // recursive conversion guard and convert the local. The part local
            // of an Option element is annotated: its if expression ends in
            // Option.None, whose type argument cannot be inferred in a plain
            // let binding without the annotation. Reference types in that
            // annotation are placeholders bound by the lowering walker.
            auto item = Fresh("item");
            emitted.pre += indent + "let " + item + " = " + value + ".getElement(" + std::to_string(i) + ")\n";
            auto part = Fresh("part");
            auto inner = FromJS(desc.elements[i], item, indent);
            emitted.pre += inner.pre;
            emitted.pre += indent + "let " + part +
                (desc.elements[i].kind == ArkTSTypeDesc::Kind::OPTIONAL ? ": " + TypeName(desc.elements[i]) : "") +
                " = " + inner.expr + "\n";
            converted.emplace_back(std::move(part));
        }
        emitted.expr = "(" + ArkTSJoin(converted, ", ") + ")";
        return emitted;
    }

    // Scalar from-JS rows: the kind guard first, then the value checks -
    // narrow ints: finite, range and integrality; Float16/Float32: range for
    // finite values only (NaN, +/-Infinity and negative zero pass through;
    // fractional in-range values convert at target precision);
    // Float64: the bare toNumber() (NaN and infinities are legal); Rune:
    // plus Unicode-scalar and surrogate checks; Int64/UInt64: bigint
    // bounds. The UInt64 upper bound carries a u64 suffix: an unsuffixed
    // 2^64-1 literal is typed Int64 and rejected; the Int64 minimum stays
    // unsuffixed because the parser folds the sign into the literal.
    // Before: Int32 __locals_result  After: guards, then "Int32(__locals_num_0)"
    ArkTSEmitted ScalarFromJS(const std::string& type, const std::string& value, const std::string& indent,
        bool nullablePosition = false)
    {
        std::string lower;
        std::string upper;
        if (ArkTSNarrowBounds(type, lower, upper)) {
            return ScalarFromJSNarrow(type, value, indent, nullablePosition, lower, upper);
        }
        if (type == "Rune") {
            return ScalarFromJSRune(type, value, indent, nullablePosition);
        }
        if (type == "Int64" || type == "UInt64" || type == "BigInt") {
            return ScalarFromJSBigInt(type, value, indent, nullablePosition);
        }
        if (type == "String" || type == "Bool" || type == "Float16" || type == "Float32" || type == "Float64") {
            return ScalarFromJSStringOrFloat(type, value, indent, nullablePosition);
        }
        if (type == "Unit") {
            return ScalarFromJSUnit(value, indent);
        }
        if (type == "Nothing") {
            return ScalarFromJSNothing();
        }
        // Default row of the table. Every name in ArkTSScalarWhitelist has a
        // conversion row above, so an empty expression here would emit
        // incomplete code; keep ArkTSScalarWhitelist, this table and
        // ArkTSScalarToJS in lockstep and add the row together with the
        // whitelist entry. std::abort() is unconditional: CJC_ABORT() is
        // empty when NDEBUG is defined without CMAKE_ENABLE_ASSERT, which
        // would turn this drift into a silent empty expression in a release
        // build. A missing conversion row is an internal error, never a
        // recoverable user error.
        std::abort(); // checked scalar missing a from-JS conversion row: add it to ScalarFromJS
        return {};
    }

    ArkTSEmitted ScalarFromJSNarrow(const std::string& type, const std::string& value, const std::string& indent,
        bool nullablePosition, const std::string& lower, const std::string& upper)
    {
        ArkTSEmitted emitted;
        auto number = Fresh("num");
        if (!nullablePosition) {
            emitted.pre += NullGuard(value, type, indent);
        }
        emitted.pre += KindGuard(value, "isNumber", "a number", type, indent);
        emitted.pre += indent + "let " + number + " = " + value + ".toNumber()\n";
        emitted.pre += indent + "if (" + number + " - " + number + " != 0.0) {\n" +
            ArkTSConversionThrow(std::string(ARKTS_INVALID) + "not a finite number for " + type, indent + "    ") +
            indent + "}\n";
        emitted.pre += indent + "if (" + number + " < " + lower + " || " + number + " > " + upper + ") {\n" +
            ArkTSConversionThrow(std::string(ARKTS_INVALID) + "out of " + type + " range", indent + "    ",
                number + ".toString()") +
            indent + "}\n";
        emitted.pre += indent + "if (" + number + " != Float64(Int64(" + number + "))) {\n" +
            ArkTSConversionThrow(std::string(ARKTS_INVALID) + "not an integer for " + type, indent + "    ") +
            indent + "}\n";
        emitted.expr = type + "(" + number + ")";
        return emitted;
    }

    ArkTSEmitted ScalarFromJSRune(const std::string& type, const std::string& value, const std::string& indent,
        bool nullablePosition)
    {
        ArkTSEmitted emitted;
        auto number = Fresh("num");
        if (!nullablePosition) {
            emitted.pre += NullGuard(value, type, indent);
        }
        emitted.pre += KindGuard(value, "isNumber", "a number", type, indent);
        emitted.pre += indent + "let " + number + " = " + value + ".toNumber()\n";
        emitted.pre += indent + "if (" + number + " - " + number + " != 0.0) {\n" +
            ArkTSConversionThrow(std::string(ARKTS_INVALID) + "not a finite number for Rune", indent + "    ") +
            indent + "}\n";
        emitted.pre += indent + "if (" + number + " < 0.0 || " + number + " > 1114111.0) {\n" +
            ArkTSConversionThrow(std::string(ARKTS_INVALID) + "not a Unicode scalar value for Rune", indent + "    ") +
            indent + "}\n";
        emitted.pre += indent + "if (" + number + " != Float64(Int64(" + number + "))) {\n" +
            ArkTSConversionThrow(std::string(ARKTS_INVALID) + "not an integer for Rune", indent + "    ") +
            indent + "}\n";
        emitted.pre += indent + "if (" + number + " >= 55296.0 && " + number + " <= 57343.0) {\n" +
            ArkTSConversionThrow(std::string(ARKTS_INVALID) + "surrogate code point for Rune", indent + "    ") +
            indent + "}\n";
        emitted.expr = "Rune(UInt32(" + number + "))";
        return emitted;
    }

    ArkTSEmitted ScalarFromJSBigInt(const std::string& type, const std::string& value, const std::string& indent,
        bool nullablePosition)
    {
        ArkTSEmitted emitted;
        if (!nullablePosition) {
            emitted.pre += NullGuard(value, type, indent);
        }
        emitted.pre += KindGuard(value, "isBigInt", "a bigint", type, indent);
        if (type == "BigInt") {
            emitted.expr = value + ".asBigInt().toBigInt()";
            return emitted;
        }
        auto big = Fresh("big");
        emitted.pre += indent + "let " + big + " = " + value + ".asBigInt().toBigInt()\n";
        if (type == "Int64") {
            emitted.pre += indent + "if (" + big + " < BigInt(Int64(-9223372036854775808))) {\n" +
                ArkTSConversionThrow(std::string(ARKTS_INVALID) + "below Int64 range", indent + "    ",
                    big + ".toString()") +
                indent + "}\n";
            emitted.pre += indent + "if (" + big + " > BigInt(Int64(9223372036854775807))) {\n" +
                ArkTSConversionThrow(std::string(ARKTS_INVALID) + "above Int64 range", indent + "    ",
                    big + ".toString()") +
                indent + "}\n";
            emitted.expr = big + ".toInt64()";
            return emitted;
        }
        emitted.pre += indent + "if (" + big + " < BigInt(Int64(0))) {\n" +
            ArkTSConversionThrow(std::string(ARKTS_INVALID) + "negative BigInt for UInt64", indent + "    ",
                big + ".toString()") +
            indent + "}\n";
        emitted.pre += indent + "if (" + big + " > BigInt(UInt64(18446744073709551615u64))) {\n" +
            ArkTSConversionThrow(std::string(ARKTS_INVALID) + "above UInt64 range", indent + "    ",
                big + ".toString()") + indent +
            "}\n";
        emitted.expr = big + ".toUInt64()";
        return emitted;
    }

    ArkTSEmitted ScalarFromJSStringOrFloat(const std::string& type, const std::string& value,
        const std::string& indent, bool nullablePosition)
    {
        ArkTSEmitted emitted;
        std::string lower;
        std::string upper;
        if (!nullablePosition) {
            emitted.pre += NullGuard(value, type, indent);
        }
        if (type == "String") {
            emitted.pre += KindGuard(value, "isString", "a string", type, indent);
            emitted.expr = value + ".asString().toString()";
        } else if (type == "Bool") {
            emitted.pre += KindGuard(value, "isBoolean", "a boolean", type, indent);
            emitted.expr = value + ".toBoolean()";
        } else if (ArkTSFloatBounds(type, lower, upper)) {
            // Behind the isNumber guard, only a finite value beyond the exact
            // Float64 image of the target's largest finite value is rejected:
            // the narrowing constructor would otherwise turn it into an
            // infinity silently. NaN, +/-Infinity and negative zero are values
            // the target type represents, so they convert as-is; in-range
            // values convert at the target precision and may lose precision
            // (no integrality check).
            emitted.pre += KindGuard(value, "isNumber", "a number", type, indent);
            auto number = Fresh("num");
            emitted.pre += indent + "let " + number + " = " + value + ".toNumber()\n";
            // The finite test is what exempts the specials: "n - n" is zero
            // exactly for finite n, so NaN and +/-Infinity skip the range
            // rejection while a finite overflow still throws.
            emitted.pre += indent + "if ((" + number + " - " + number + ") == 0.0 && (" + number + " < " + lower +
                " || " + number + " > " + upper + ")) {\n" +
                ArkTSConversionThrow(std::string(ARKTS_INVALID) + "out of " + type + " range", indent + "    ") +
                indent + "}\n";
            emitted.expr = type + "(" + number + ")";
        } else {
            // Float64 - the only row left in this branch - is the full-range
            // number carrier (number -> Float64 converts directly with no
            // precision loss), so NaN and infinities are legal number values
            // and toNumber() passes them through; the absence of the
            // finite/range checks here is an intentional difference from the
            // Float16/Float32 and narrow-int rows above.
            emitted.pre += KindGuard(value, "isNumber", "a number", type, indent);
            emitted.expr = value + ".toNumber()";
        }
        return emitted;
    }

    // Unit from-JS pseudocode: JS null and JS undefined both satisfy Unit,
    // checked through the isNull()/isUndefined() predicates; every other value
    // is a kind mismatch.
    ArkTSEmitted ScalarFromJSUnit(const std::string& value, const std::string& indent)
    {
        ArkTSEmitted emitted;
        emitted.expr = "(if (" + value + ".isNull() || " + value + ".isUndefined()) {\n" + indent +
            "    ()\n" + indent + "} else {\n" +
            ArkTSConversionThrow(std::string(ARKTS_MISMATCH) + "not null or undefined for Unit", indent + "    ") +
            indent + "})";
        return emitted;
    }

    // A never-typed element can never produce a value; reaching it is the same
    // contract violation as a never-typed result.
    ArkTSEmitted ScalarFromJSNothing()
    {
        ArkTSEmitted emitted;
        emitted.expr = std::string("throw ") + ARKTS_NEVER_EXCEPTION + "(\"" + ARKTS_NEVER_MESSAGE + "\")";
        return emitted;
    }
};

// Local-name selection: begin with the compiler-owned ASCII prefix; if any
// source parameter begins with that prefix, try a numbered prefix and
// repeat until all generated context/module/namespace/result names are
// disjoint. Only compiler-owned characters enter generated syntax;
// parameter names are compared here and their values remain AST references
// in the generated body.
// Example: a clash-free parameter list -> "__cj_arkts_"; a list naming
//          __cj_arkts_0_context and __cj_arkts_1_module -> "__cj_arkts_2_"
std::string ArkTSForeignLocalPrefix(const std::vector<Ptr<FuncParam>>& params)
{
    const std::string base = ARKTS_GENERATED_PREFIX;
    auto prefix = base;
    size_t suffix = 0;
    auto conflicts = [&prefix](Ptr<FuncParam> param) {
        return param->identifier.Val().compare(0, prefix.size(), prefix) == 0;
    };
    while (std::any_of(params.begin(), params.end(), conflicts)) {
        prefix = base + std::to_string(suffix++) + "_";
    }
    return prefix;
}

namespace {
// Call-array assembly shared by the function wrapper and both class member
// generators. optionalStart is the first position of the trailing ArkTS
// optional run; every position up to the last Some is forwarded (a None in
// that range is the ctx.undefined() produced by its own conversion) and every
// optional position after the last Some is omitted. `args` holds the converted
// locals of every forwarded fixed position, so its index matches the type
// position. Returns the name of the generated array local.
// With optionalStart k, last is the index of the last Some among the
// optionals (or k - 1 when none is Some) and args becomes an
// Array<ark_interop.JSValue> of last + 1 entries filled by a block lambda.
// The call array is a block-lambda constructor, never a trailing closure: the
// generated body is parsed after the trailing-closure desugar pass, so
// `Array<ark_interop.JSValue>(n) { ... }` would stay an untyped
// TRAIL_CLOSE_EXPR.
// Example: optionals [Some(a), None, None] -> last = k,     args = [.., a]
// Example: optionals [None, Some(b), None] -> last = k + 1, args = [.., undefined, b]
// Example: optionals [None, None]          -> last = k - 1, no optional forwarded
std::string ArkTSOptionalCallArray(ArkTSValueEmitter& emitter, const std::vector<ArkTSTypeDesc>& types,
    size_t optionalStart, const std::vector<std::string>& args, const std::string& ctx, std::string& code)
{
    auto last = emitter.Fresh("last");
    auto callIndex = emitter.Fresh("index");
    auto callArgs = emitter.Fresh("args");
    code += "    let " + last + " = ";
    bool firstCondition = true;
    for (size_t i = types.size(); i > optionalStart; --i) {
        const size_t position = i - 1;
        code += firstCondition ? "if (" : "else if (";
        code += std::string(ARKTS_PLACEHOLDER_ARGUMENT) + std::to_string(position) + ".isSome()) {\n        " +
            std::to_string(position) + "\n    } ";
        firstCondition = false;
    }
    code += "else {\n        " + std::to_string(static_cast<long long>(optionalStart) - 1) + "\n    }\n";
    code += "    let " + callArgs + " = Array<" + ARKTS_JS_VALUE + ">(" + last + " + 1, { " + callIndex + " =>\n";
    code += "        match (" + callIndex + ") {\n";
    for (size_t i = 0; i < args.size(); ++i) {
        code += "            case " + std::to_string(i) + " => " + args[i] + "\n";
    }
    // Unreachable fallback: the array length is last + 1 and every position up
    // to last has a case above. It keeps the match exhaustive without an
    // lvalue that could be mistaken for a forwarded value.
    code += "            case _ => " + ctx + ".undefined().toJSValue()\n";
    code += "        }\n    })\n";
    return callArgs;
}

// Defined below; the function wrapper shares the same rest call-array shape
// and converted-argument locals with the class member generators.
std::string ArkTSRestCallArray(ArkTSValueEmitter& emitter, const ArkTSTypeDesc& element, size_t restIndex,
    const std::vector<std::string>& fixed, const std::string& prefix, std::string& code);
std::vector<std::string> ArkTSArgumentSource(ArkTSValueEmitter& emitter, const std::vector<ArkTSTypeDesc>& types,
    size_t count, std::string& code);

// Signature gate of the wrapper. A REFERENCE position is emitted as a
// placeholder that the lowering walker binds through classRefs. Without the
// map the emitter cannot produce a complete value and would leave malformed
// syntax behind (for example a bare `return ;`, `Array<>` or `let x = `), so
// refuse the whole wrapper; the empty string routes to the existing
// unsupported-signature diagnostic instead.
bool ArkTSWrapperSignatureSupported(const std::vector<ArkTSTypeDesc>& types, const ArkTSTypeDesc& resultType,
    size_t restIndex, std::unordered_map<std::string, Ptr<AST::Decl>>* classRefs)
{
    const bool rest = restIndex != ARKTS_NO_REST;
    if (rest && (restIndex + 1 != types.size() || types[restIndex].kind != ArkTSTypeDesc::Kind::ARRAY_COPY)) {
        return false;
    }
    for (const auto& type : types) {
        if (!ArkTSTypeDescGeneratable(type) || ArkTSTypeDescContainsNothing(type)) {
            return false;
        }
    }
    if (!ArkTSTypeDescGeneratable(resultType)) {
        return false;
    }
    if (classRefs != nullptr) {
        return true;
    }
    bool needsClassBinding = ArkTSTypeDescContainsReference(resultType);
    for (const auto& type : types) {
        needsClassBinding = needsClassBinding || ArkTSTypeDescContainsReference(type);
    }
    return !needsClassBinding;
}

// Select and emit the call array: the optional run is derived first (the
// generator-only entry point), then the rest, optional and plain forms are
// tried in that order.
std::string ArkTSWrapperCallArray(ArkTSValueEmitter& emitter, const std::vector<ArkTSTypeDesc>& types,
    const std::vector<std::string>& args, size_t restIndex, size_t optionalStart, const std::string& ctx,
    const std::string& prefix, std::string& code)
{
    if (optionalStart == ARKTS_DERIVE_OPTIONAL) {
        optionalStart = types.size();
        while (optionalStart > 0 && types[optionalStart - 1].kind == ArkTSTypeDesc::Kind::OPTIONAL) {
            --optionalStart;
        }
    }
    if (restIndex != ARKTS_NO_REST) {
        return ArkTSRestCallArray(emitter, types[restIndex].elements.front(), restIndex, args, prefix, code);
    }
    if (optionalStart < types.size()) {
        return ArkTSOptionalCallArray(emitter, types, optionalStart, args, ctx, code);
    }
    // The empty call array spells the qualified JSValue: the injected package
    // import always binds `ark_interop`, so the wrapper never depends on a
    // user import re-exporting the interop surface.
    return args.empty() ? "Array<" + std::string(ARKTS_JS_VALUE) + ">()" : "[" + ArkTSJoin(args, ", ") + "]";
}
} // namespace

// Emits the wrapper body of a checked synchronous foreign declaration; the
// lowering walker rebinds placeholders and parameter references as AST
// values; any unsupported position returns "". Pseudocode:
//   ctx = cj_arkts_context_v1.__cj_arkts_current(); receiver =
//   ctx.requireArkModule("<module>").asObject() or ctx.global (arkts.lang)
//   receiver = receiver.getProperty("<ns_i>").asObject() ...   // per namespace segment
//   args = [toJS(ctx, argument_i, type_i) ...]; result = receiver.callMethod("<name>", args)
//   return fromJS(result, checked_result_type)   // a Nothing result throws instead
// Rest form (restIndex = k): the last parameter converts element-wise into
// Array<ark_interop.JSValue>(argument_k.size); the call array is sized "k + rest.size"
// with "case _ => rest[i - k]" - each rest item is one individual argument.

std::string ArkTSForeignWrapperSource(const std::vector<ArkTSTypeDesc>& types, const ArkTSTypeDesc& resultType,
    size_t namespaceDepth, const std::string& prefix, size_t restIndex, ArkTSReceiverKind receiver,
    size_t optionalStart, std::unordered_map<std::string, Ptr<AST::Decl>>* classRefs)
{
    if (!ArkTSWrapperSignatureSupported(types, resultType, restIndex, classRefs)) {
        return {};
    }
    auto ctx = prefix + "context";
    auto recv = prefix + std::string("module");
    const bool globalReceiver = receiver == ArkTSReceiverKind::GLOBAL;
    const auto context = ArkTSFailureContext("function", ArkTSPlaceholderLiteralImpl(ARKTS_PLACEHOLDER_NAME));
    std::string code = std::string("func ") + ARKTS_WRAPPER_ENTRY + "() {\n    let " + ctx + ": " +
        ARKTS_JS_CONTEXT + " = " + ARKTS_CONTEXT_ABI_PACKAGE + "." + ARKTS_CONTEXT_CURRENT + "()\n";
    // The receiver line is the only difference between the two forms: the
    // GLOBAL form keeps the same local (prefix + "module") so every
    // namespace and call site below is shared, but binds it to the
    // context's global object and never emits requireArkModule or the
    // module placeholder (there is no ArkTS runtime module named
    // arkts.lang to require: the package is the TS-preset facade).
    if (globalReceiver) {
        code += "    let " + recv + " = " + ctx + ".global\n";
    } else {
        code += "    let " + recv + " = " + ctx + ".requireArkModule(" +
            ArkTSPlaceholderLiteralImpl(ARKTS_PLACEHOLDER_MODULE) + ").asObject()\n";
    }
    ArkTSValueEmitter emitter{prefix};
    emitter.classRefs = classRefs;
    for (size_t i = 0; i < namespaceDepth; ++i) {
        auto value = emitter.Fresh("nsvalue");
        auto next = prefix + "namespace_" + std::to_string(i);
        code += ArkTSNamespaceGuard(recv, value, next, i, context, globalReceiver, "    ");
        recv = next;
    }
    const size_t fixed = restIndex == ARKTS_NO_REST ? types.size() : restIndex;
    auto args = ArkTSArgumentSource(emitter, types, fixed, code);
    // ArkTS optional parameters are the trailing named `Option<T'> = None`
    // parameters; lowering computes that run from the parameter spelling and
    // passes its first index. The generator-only entry point derives it from
    // the trailing OPTIONAL descriptions, but that is a test entry point
    // only: a type description cannot tell `x!: Option<T'> = None` from a
    // named `x!: Option<T'>` without a default (which Sema rejects before
    // generation), so production must always pass optionalStart from the
    // lowering walker.
    // The wrapper cannot distinguish "the caller omitted the argument" from
    // "the caller passed None", so the Option value decides the forwarded
    // list: every position up to the last Some is forwarded (a None in that
    // range becomes ctx.undefined()) and every optional position after the
    // last Some is omitted. Required positions are always forwarded. With a
    // single optional, None therefore forwards no argument at all. A
    // positional `Option<T'>` (the with-rest form) is a required nullable
    // argument and keeps being forwarded explicitly.
    auto result = prefix + "result";
    // The last Some is forwarded as its own converted value; a None before it
    // is forwarded as the ctx.undefined() produced by its conversion; every
    // position after the last Some is not passed at all (the ".." prefix
    // stands for the required positions). The shared helper assembles the
    // block-lambda array for both this wrapper and the class member
    // generators, including the rest form's element-wise buffer.
    auto callArgsExpr = ArkTSWrapperCallArray(emitter, types, args, restIndex, optionalStart, ctx, prefix, code);
    // Resolve the target first, so a missing or non-callable export names the
    // module and the export instead of falling through to the library's
    // generic property error; then translate the runtime-failure codes.
    auto target = emitter.Fresh("target");
    auto error = emitter.Fresh("error");
    code += ArkTSExportGuard(recv, target, ArkTSPlaceholderLiteralImpl(ARKTS_PLACEHOLDER_NAME), context, globalReceiver,
        "    ", namespaceDepth, error);
    code += ArkTSCallableGuard(target, context, ": the export is not a function", "    ");
    const auto callExpr =
        recv + ".callMethod(" + ArkTSPlaceholderLiteralImpl(ARKTS_PLACEHOLDER_NAME) + ", " + callArgsExpr + ")";
    code += "    let " + result + " = " + ArkTSCallTranslation(callExpr, context, error, "    ") + "\n";
    if (resultType.kind == ArkTSTypeDesc::Kind::SCALAR && resultType.scalar == "Nothing") {
        return code + "    throw " + std::string(ARKTS_NEVER_EXCEPTION) + "(\"" + ARKTS_NEVER_MESSAGE + "\")\n}\n";
    }
    auto emitted = emitter.FromJS(resultType, result, "    ");
    if (emitter.broken) {
        return {};
    }
    return code + emitted.pre + "    return " + emitted.expr + "\n}\n";
}

// Plain form: forwards to the overload above with ARKTS_NO_REST.
std::string ArkTSForeignWrapperSource(const std::vector<ArkTSTypeDesc>& types, const ArkTSTypeDesc& resultType,
    size_t namespaceDepth, const std::string& prefix, ArkTSReceiverKind receiver, size_t optionalStart,
    std::unordered_map<std::string, Ptr<AST::Decl>>* classRefs)
{
    return ArkTSForeignWrapperSource(
        types, resultType, namespaceDepth, prefix, ARKTS_NO_REST, receiver, optionalStart, classRefs);
}

namespace {
// The context-establishment prologue every generated body opens with: the call
// context is always established, and a body that needs no conversion still
// reads ctx.env for the lifecycle/thread check.
std::string ArkTSBodyPrologue(const std::string& prefix, const std::string& code)
{
    const auto ctx = prefix + "context";
    // Every generated member runs under the ambient call scope, so a missing
    // entry context fails here even when no conversion needs the value.
    auto header = "    let " + ctx + ": " + ARKTS_JS_CONTEXT + " = " + ARKTS_CONTEXT_ABI_PACKAGE + "." +
        ARKTS_CONTEXT_CURRENT + "()\n";
    // Reading env performs the shared lifecycle/thread check, so a body that
    // needs no conversion still fails outside an entry scope.
    auto discard = code.find(ctx) == std::string::npos ? "    let _ = " + ctx + ".env\n" : "";
    return header + discard;
}

// Close a generated body around the prologue above.
std::string ArkTSFinishBody(const std::string& prefix, const std::string& code)
{
    return "func " + std::string(ARKTS_WRAPPER_ENTRY) + "() {\n" + ArkTSBodyPrologue(prefix, code) + code + "}\n";
}

// Close a super-delegating generated constructor body: the construction
// statements move inside a thunk literal so the super constructor call stays
// the constructor's single first expression, and the thunk's value is the new
// object reference the parent's thunk-taking wrapping constructor stores. The
// prologue (context establishment and the env check) runs inside the thunk,
// where the construction statements execute.
std::string ArkTSFinishSuperBody(const std::string& prefix, const std::string& code)
{
    return "func " + std::string(ARKTS_WRAPPER_ENTRY) + "() {\n    super({ =>\n" + ArkTSBodyPrologue(prefix, code) +
        code + "    }, ())\n}\n";
}

// Resolve the class value of a static member or constructor as a JSValue.
// Static members use it as the receiver; a constructor converts it with
// asClass() before calling new().
std::string ArkTSClassValuePreamble(const std::string& prefix, size_t namespaceDepth, ArkTSReceiverKind receiver,
    std::string& classValue)
{
    auto ctx = prefix + "context";
    auto recv = prefix + "module";
    const bool globalReceiver = receiver == ArkTSReceiverKind::GLOBAL;
    const auto context = ArkTSFailureContext("class", ArkTSPlaceholderLiteralImpl(ARKTS_PLACEHOLDER_CLASS));
    std::string code = "    let " + recv + " = ";
    if (globalReceiver) {
        code += ctx + ".global\n";
    } else {
        code += ctx + ".requireArkModule(" + ArkTSPlaceholderLiteralImpl(ARKTS_PLACEHOLDER_MODULE) + ").asObject()\n";
    }
    for (size_t i = 0; i < namespaceDepth; ++i) {
        auto value = prefix + "nsvalue_" + std::to_string(i);
        auto next = prefix + "namespace_" + std::to_string(i);
        code += ArkTSNamespaceGuard(recv, value, next, i, context, globalReceiver, "    ");
        recv = next;
    }
    classValue = prefix + "classvalue";
    code += ArkTSExportGuard(recv, classValue, ArkTSPlaceholderLiteralImpl(ARKTS_PLACEHOLDER_CLASS), context,
        globalReceiver, "    ", namespaceDepth);
    return code;
}

// Converted argument locals for one call. Returns the variable names and
// appends their pre-statements to the body.
std::vector<std::string> ArkTSArgumentSource(ArkTSValueEmitter& emitter, const std::vector<ArkTSTypeDesc>& types,
    size_t count, std::string& code)
{
    std::vector<std::string> args;
    for (size_t i = 0; i < count; ++i) {
        auto emitted = emitter.ToJS(types[i], ARKTS_PLACEHOLDER_ARGUMENT + std::to_string(i),
            emitter.prefix + "context", "    ");
        auto local = emitter.Fresh("arg");
        code += emitted.pre;
        code += "    let " + local + " = " + emitted.expr + "\n";
        args.emplace_back(std::move(local));
    }
    return args;
}

// Rest call-array assembly shared by methods and constructors: convert each
// spread element into a rest buffer, then build the call array from the fixed
// arguments followed by the rest items. Private to this translation unit.
std::string ArkTSRestCallArray(ArkTSValueEmitter& emitter, const ArkTSTypeDesc& element, size_t restIndex,
    const std::vector<std::string>& fixed, const std::string& prefix, std::string& code)
{
    auto items = emitter.Fresh("rest");
    auto index = emitter.Fresh("index");
    auto item = emitter.Fresh("elem");
    auto inner = emitter.ToJS(element, item, prefix + "context", "        ");
    auto restLength = std::string(ARKTS_PLACEHOLDER_ARGUMENT) + std::to_string(restIndex) + ".size";
    code += "    let " + items + " = Array<" + ARKTS_JS_VALUE + ">(" + restLength + ", { " + index +
        " =>\n        let " + item + " = " + ARKTS_PLACEHOLDER_ARGUMENT + std::to_string(restIndex) + "[" + index +
        "]\n" + inner.pre + "        " + inner.expr + "\n    })\n";
    if (fixed.empty()) {
        return items;
    }
    auto callIndex = emitter.Fresh("index");
    auto callArgs = emitter.Fresh("args");
    code += "    let " + callArgs + " = Array<" + ARKTS_JS_VALUE + ">(" + std::to_string(fixed.size()) + " + " +
        restLength + ", { " + callIndex + " =>\n        match (" + callIndex + ") {\n";
    for (size_t i = 0; i < fixed.size(); ++i) {
        code += "            case " + std::to_string(i) + " => " + fixed[i] + "\n";
    }
    code += "            case _ => " + items + "[" + callIndex + " - " + std::to_string(fixed.size()) +
        "]\n        }\n    })\n";
    return callArgs;
}

} // namespace

std::string ArkTSQuotedLiteral(const std::string& text)
{
    return ArkTSLiteral(text);
}

std::string ArkTSPlaceholderLiteral(const std::string& text)
{
    return ArkTSPlaceholderLiteralImpl(text);
}

bool ArkTSRebindPlaceholderLiteral(const std::string& value,
    const std::unordered_map<std::string, std::string>& literals, std::string& rebound)
{
    const std::string prefix = ARKTS_PLACEHOLDER_LITERAL_PREFIX;
    if (value.compare(0, prefix.size(), prefix) != 0) {
        return false;
    }
    auto found = literals.find(value.substr(prefix.size()));
    if (found == literals.end()) {
        return false;
    }
    rebound = found->second;
    return true;
}

// Body of a `@ArkTS[globalVar: true] foreign func`: the module (or global
// object) receiver, one guarded and translated read of the named export, then
// the same FromJS conversion the function wrapper applies to its result. Every
// generatable description is accepted here - the scalar table, Option, Array,
// tuple and foreign class/interface/enum leaves.
std::string ArkTSGlobalVariableGetterSource(const ArkTSTypeDesc& valueType, size_t namespaceDepth,
    const std::string& prefix, ArkTSReceiverKind receiver,
    std::unordered_map<std::string, Ptr<AST::Decl>>* classRefs)
{
    if (!ArkTSTypeDescGeneratable(valueType)) {
        return {};
    }
    // A REFERENCE leaf is emitted by placeholder and bound through classRefs;
    // without the map the emitter would leave malformed syntax behind.
    if (classRefs == nullptr && ArkTSTypeDescContainsReference(valueType)) {
        return {};
    }
    auto ctx = prefix + "context";
    auto recv = prefix + "module";
    const bool globalReceiver = receiver == ArkTSReceiverKind::GLOBAL;
    const auto context = ArkTSFailureContext("variable", ArkTSPlaceholderLiteralImpl(ARKTS_PLACEHOLDER_NAME));
    std::string code = std::string("func ") + ARKTS_WRAPPER_ENTRY + "() {\n    let " + ctx + ": " + ARKTS_JS_CONTEXT +
        " = " + ARKTS_CONTEXT_ABI_PACKAGE + "." + ARKTS_CONTEXT_CURRENT + "()\n";
    if (globalReceiver) {
        code += "    let " + recv + " = " + ctx + ".global\n";
    } else {
        code += "    let " + recv + " = " + ctx + ".requireArkModule(" +
            ArkTSPlaceholderLiteralImpl(ARKTS_PLACEHOLDER_MODULE) + ").asObject()\n";
    }
    ArkTSValueEmitter emitter{prefix};
    emitter.classRefs = classRefs;
    for (size_t i = 0; i < namespaceDepth; ++i) {
        auto value = emitter.Fresh("nsvalue");
        auto next = prefix + "namespace_" + std::to_string(i);
        code += ArkTSNamespaceGuard(recv, value, next, i, context, globalReceiver, "    ");
        recv = next;
    }
    // The read is the target resolution: a missing export throws
    // ForeignRuntimeException, and the translated form maps an ArkTS-side
    // getter failure through the same contract as a function call.
    auto value = emitter.Fresh("value");
    auto error = emitter.Fresh("error");
    // An undefined value is legal for an Option<T> target (design 3.4 maps it to
    // None); every other target treats undefined as a missing export.
    code += ArkTSExportGuard(recv, value, ArkTSPlaceholderLiteralImpl(ARKTS_PLACEHOLDER_NAME), context, globalReceiver,
        "    ", namespaceDepth, error, valueType.kind != ArkTSTypeDesc::Kind::OPTIONAL);
    if (valueType.kind == ArkTSTypeDesc::Kind::SCALAR && valueType.scalar == "Nothing") {
        // A never-typed property that produced a value is the same never
        // contract as a function result.
        return code + "    throw " + ARKTS_RUNTIME_EXCEPTION + "(\"" + ARKTS_NEVER_MESSAGE + "\")\n}\n";
    }
    auto emitted = emitter.FromJS(valueType, value, "    ");
    if (emitter.broken) {
        return {};
    }
    return code + emitted.pre + "    return " + emitted.expr + "\n}\n";
}

std::string ArkTSNamespaceResolution(const std::string& receiver, const std::string& value, const std::string& next,
    size_t index, const std::string& kind, const std::string& name, bool globalReceiver, const std::string& indent)
{
    return ArkTSNamespaceGuard(receiver, value, next, index, ArkTSFailureContext(kind, name), globalReceiver, indent);
}

// A class-method wrapper needs a generatable signature: a rest parameter must
// be the trailing array parameter, and no position may be deferred or contain
// Nothing.
static bool ArkTSClassMethodSignatureValid(const std::vector<ArkTSTypeDesc>& types,
    const ArkTSTypeDesc& resultType, size_t restIndex)
{
    if (restIndex != ARKTS_NO_REST &&
        (restIndex + 1 != types.size() || types[restIndex].kind != ArkTSTypeDesc::Kind::ARRAY_COPY)) {
        return false;
    }
    for (auto& type : types) {
        if (!ArkTSTypeDescGeneratable(type) || ArkTSTypeDescContainsNothing(type)) {
            return false;
        }
    }
    return ArkTSTypeDescGeneratable(resultType);
}

// Static members have no object reference: the class value itself is the
// `this` argument. Instance members call the method on the stored object.
// Both resolve the member first, so a missing or non-callable member names
// the class and member instead of the library's generic property error.
static void EmitArkTSMethodCall(ArkTSValueEmitter& emitter, std::string& code, const std::string& receiverObject,
    const std::string& context, bool staticMember, const std::string& result, const std::string& argArray)
{
    auto target = emitter.Fresh("target");
    auto error = emitter.Fresh("error");
    code += ArkTSMemberGuard(
        receiverObject, target, ArkTSPlaceholderLiteralImpl(ARKTS_PLACEHOLDER_MEMBER), context, "    ");
    code += ArkTSCallableGuard(target, context, " is not a function", "    ");
    const auto callExpr = staticMember
        ? target + ".asFunction().call(" + argArray + ", thisArg: " + receiverObject + ")"
        : receiverObject + ".callMethod(" + ArkTSPlaceholderLiteralImpl(ARKTS_PLACEHOLDER_MEMBER) + ", " + argArray +
            ")";
    code += "    let " + result + " = " + ArkTSCallTranslation(callExpr, context, error, "    ") + "\n";
}

std::string ArkTSClassMethodSource(const std::vector<ArkTSTypeDesc>& types, const ArkTSTypeDesc& resultType,
    size_t namespaceDepth, const std::string& prefix, size_t restIndex, ArkTSMemberReceiver memberReceiver,
    ArkTSReceiverKind receiver, size_t optionalStart, std::unordered_map<std::string, Ptr<Decl>>& classRefs,
    const std::string& ownerKind)
{
    if (!ArkTSClassMethodSignatureValid(types, resultType, restIndex)) {
        return {};
    }
    ArkTSValueEmitter emitter{prefix};
    emitter.classRefs = &classRefs;
    std::string code;
    std::string receiverObject;
    if (memberReceiver == ArkTSMemberReceiver::INSTANCE) {
        receiverObject = std::string(ARKTS_SELF) + "." + ARKTS_CLASS_REF_ACCESSOR + "()";
    } else {
        std::string classValue;
        code += ArkTSClassValuePreamble(prefix, namespaceDepth, receiver, classValue);
        receiverObject = classValue;
    }
    auto args = ArkTSArgumentSource(emitter, types, restIndex == ARKTS_NO_REST ? types.size() : restIndex, code);
    auto result = prefix + "result";
    const bool staticMember = memberReceiver == ArkTSMemberReceiver::STATIC;
    // A trailing ArkTS optional run is forwarded exactly like the function
    // path: up to the last Some, with a None before it as ctx.undefined().
    // The rest form keeps its existing spread behavior and never truncates.
    const bool optionals = restIndex == ARKTS_NO_REST && optionalStart < args.size();
    const auto context = ArkTSMemberFailureContext(ownerKind, ArkTSPlaceholderLiteralImpl(ARKTS_PLACEHOLDER_CLASS),
        ArkTSPlaceholderLiteralImpl(ARKTS_PLACEHOLDER_MEMBER));
    if (restIndex != ARKTS_NO_REST) {
        // Rest members spread the trailing array item by item.
        EmitArkTSMethodCall(emitter, code, receiverObject, context, staticMember, result,
            ArkTSRestCallArray(emitter, types[restIndex].elements.front(), restIndex, args, prefix, code));
    } else if (optionals) {
        EmitArkTSMethodCall(emitter, code, receiverObject, context, staticMember, result,
            ArkTSOptionalCallArray(emitter, types, optionalStart, args, prefix + "context", code));
    } else {
        EmitArkTSMethodCall(emitter, code, receiverObject, context, staticMember, result,
            args.empty() ? "Array<" + std::string(ARKTS_JS_VALUE) + ">()" : "[" + ArkTSJoin(args, ", ") + "]");
    }
    if (resultType.kind == ArkTSTypeDesc::Kind::SCALAR && resultType.scalar == "Nothing") {
        return ArkTSFinishBody(prefix,
            code + "    throw " + std::string(ARKTS_NEVER_EXCEPTION) + "(\"" + ARKTS_NEVER_MESSAGE + "\")\n");
    }
    auto emitted = emitter.FromJS(resultType, result, "    ");
    if (emitter.broken) {
        return {};
    }
    return ArkTSFinishBody(prefix, code + emitted.pre + "    return " + emitted.expr + "\n");
}

std::string ArkTSClassConstructorSource(const std::vector<ArkTSTypeDesc>& types, size_t namespaceDepth,
    const std::string& prefix, size_t restIndex, ArkTSReceiverKind receiver, size_t optionalStart,
    std::unordered_map<std::string, Ptr<Decl>>& classRefs, ArkTSConstructorForm form)
{
    if (restIndex != ARKTS_NO_REST &&
        (restIndex + 1 != types.size() || types[restIndex].kind != ArkTSTypeDesc::Kind::ARRAY_COPY)) {
        return {};
    }
    for (auto& type : types) {
        if (!ArkTSTypeDescGeneratable(type) || ArkTSTypeDescContainsNothing(type)) {
            return {};
        }
    }
    ArkTSValueEmitter emitter{prefix};
    emitter.classRefs = &classRefs;
    std::string classValue;
    std::string code;
    const auto context = ArkTSFailureContext("class", ArkTSPlaceholderLiteralImpl(ARKTS_PLACEHOLDER_CLASS));
    code += ArkTSClassValuePreamble(prefix, namespaceDepth, receiver, classValue);
    code += ArkTSCallableGuard(classValue, context, ": the export is not a constructor", "    ");
    auto clazz = prefix + "class";
    code += "    let " + clazz + " = " + classValue + ".asClass()\n";
    auto args = ArkTSArgumentSource(emitter, types, restIndex == ARKTS_NO_REST ? types.size() : restIndex, code);
    auto object = prefix + "object";
    // A trailing ArkTS optional run is forwarded exactly like a member call.
    // The rest form keeps its existing spread behavior and never truncates.
    const bool optionals = restIndex == ARKTS_NO_REST && optionalStart < args.size();
    std::string callArgsExpr;
    if (restIndex != ARKTS_NO_REST) {
        // Rest constructor arguments: the trailing array is spread into
        // individual construction arguments.
        callArgsExpr =
            ArkTSRestCallArray(emitter, types[restIndex].elements.front(), restIndex, args, prefix, code);
    } else if (optionals) {
        callArgsExpr = ArkTSOptionalCallArray(emitter, types, optionalStart, args, prefix + "context", code);
    } else if (args.empty()) {
        callArgsExpr = "";
    } else {
        callArgsExpr = args.size() == 1 ? args.front() : "[" + ArkTSJoin(args, ", ") + "]";
    }
    auto error = emitter.Fresh("error");
    const auto newExpr = clazz + ".new(" + callArgsExpr + ")";
    code += "    let " + object + " = " + ArkTSCallTranslation(newExpr, context, error, "    ") + "\n";
    if (form == ArkTSConstructorForm::SUPER_THUNK) {
        // A child wrapper stores nothing itself: the thunk's value is the new
        // object reference, and the parent's thunk-taking wrapping constructor
        // assigns it to the root wrapper's field.
        return ArkTSFinishSuperBody(prefix, code + "    " + object + ".asObject()\n");
    }
    return ArkTSFinishBody(prefix,
        code + "    " + ARKTS_SELF + "." + ARKTS_CLASS_REF_FIELD + " = " + object + ".asObject()\n");
}

std::string ArkTSClassPropertySource(const ArkTSTypeDesc& valueType, bool setter,
    ArkTSMemberReceiver memberReceiver, size_t namespaceDepth, const std::string& prefix, ArkTSReceiverKind receiver,
    std::unordered_map<std::string, Ptr<Decl>>& classRefs)
{
    if (!ArkTSTypeDescGeneratable(valueType) || ArkTSTypeDescContainsNothing(valueType)) {
        return {};
    }
    ArkTSValueEmitter emitter{prefix};
    emitter.classRefs = &classRefs;
    std::string code;
    std::string object;
    if (memberReceiver == ArkTSMemberReceiver::STATIC) {
        std::string classValue;
        code += ArkTSClassValuePreamble(prefix, namespaceDepth, receiver, classValue);
        object = classValue;
    } else {
        object = std::string(ARKTS_SELF) + "." + ARKTS_CLASS_REF_ACCESSOR + "()";
    }
    // A property value may legitimately be undefined, so there is no
    // existence check; the access itself is translated because a getter or
    // setter on the ArkTS side can throw.
    const auto context = ArkTSMemberFailureContext("class", ArkTSPlaceholderLiteralImpl(ARKTS_PLACEHOLDER_CLASS),
        ArkTSPlaceholderLiteralImpl(ARKTS_PLACEHOLDER_MEMBER));
    if (setter) {
        auto value = emitter.ToJS(valueType, std::string(ARKTS_PLACEHOLDER_ARGUMENT) + "0", prefix + "context", "    ");
        code += value.pre;
        auto error = emitter.Fresh("error");
        const auto setExpr =
            object + ".setProperty(" + ArkTSPlaceholderLiteralImpl(ARKTS_PLACEHOLDER_MEMBER) + ", " + value.expr + ")";
        return ArkTSFinishBody(prefix, code + "    " + ArkTSCallTranslation(setExpr, context, error, "    ") + "\n");
    }
    auto result = prefix + "result";
    auto error = emitter.Fresh("error");
    const auto getExpr = object + ".getProperty(" + ArkTSPlaceholderLiteralImpl(ARKTS_PLACEHOLDER_MEMBER) + ")";
    code += "    let " + result + " = " + ArkTSCallTranslation(getExpr, context, error, "    ") + "\n";
    auto emitted = emitter.FromJS(valueType, result, "    ");
    if (emitter.broken) {
        return {};
    }
    return ArkTSFinishBody(prefix, code + emitted.pre + "    return " + emitted.expr + "\n");
}

std::string ArkTSInterfaceCallSource(const std::vector<ArkTSTypeDesc>& types, const ArkTSTypeDesc& resultType,
    const std::string& prefix, std::unordered_map<std::string, Ptr<Decl>>& classRefs)
{
    for (auto& type : types) {
        if (!ArkTSTypeDescGeneratable(type) || ArkTSTypeDescContainsNothing(type)) {
            return {};
        }
    }
    if (!ArkTSTypeDescGeneratable(resultType)) {
        return {};
    }
    ArkTSValueEmitter emitter{prefix};
    emitter.classRefs = &classRefs;
    const auto context = ArkTSMemberFailureContext("interface", ArkTSPlaceholderLiteralImpl(ARKTS_PLACEHOLDER_CLASS),
        ArkTSPlaceholderLiteralImpl(ARKTS_PLACEHOLDER_MEMBER));
    auto self = emitter.Fresh("self");
    auto selfValue = emitter.Fresh("selfvalue");
    auto function = emitter.Fresh("function");
    std::string code = "    let " + self + " = " + ARKTS_SELF + "." + ARKTS_CLASS_REF_ACCESSOR + "()\n";
    code += "    let " + selfValue + " = " + self + ".toJSValue()\n";
    // The call target of an interface value is the value itself; a
    // non-callable value names the interface instead of failing with the
    // library's generic type mismatch.
    code += ArkTSCallableGuard(selfValue, context, " is not callable", "    ");
    code += "    let " + function + " = " + selfValue + ".asFunction()\n";
    auto args = ArkTSArgumentSource(emitter, types, types.size(), code);
    auto result = emitter.Fresh("result");
    auto error = emitter.Fresh("error");
    const auto callExpr = function + ".call(" +
        (args.empty() ? "Array<" + std::string(ARKTS_JS_VALUE) + ">()" : "[" + ArkTSJoin(args, ", ") + "]") +
        ", thisArg: " + selfValue + ")";
    code += "    let " + result + " = " + ArkTSCallTranslation(callExpr, context, error, "    ") + "\n";
    if (resultType.kind == ArkTSTypeDesc::Kind::SCALAR && resultType.scalar == "Nothing") {
        return ArkTSFinishBody(prefix,
            code + "    throw " + std::string(ARKTS_NEVER_EXCEPTION) + "(\"" + ARKTS_NEVER_MESSAGE + "\")\n");
    }
    auto emitted = emitter.FromJS(resultType, result, "    ");
    if (emitter.broken) {
        return {};
    }
    return ArkTSFinishBody(prefix, code + emitted.pre + "    return " + emitted.expr + "\n");
}

// Spell a numeric index key: JS property keys are strings, so a Float64 key
// is accepted only when it is a finite integer that fits Int64, and the key
// is named by the integer's canonical decimal spelling, so obj[2] and
// obj["2"] address the same property. Emits the guard statements into `code`
// and replaces the raw argument placeholder in `key` with the key expression.
static void EmitArkTSNumberIndexKey(ArkTSValueEmitter& emitter, std::string& key, std::string& code)
{
    auto number = emitter.Fresh("keynum");
    auto canonical = emitter.Fresh("key");
    code += "    let " + number + " = " + key + "\n";
    code += "    if (" + number + " - " + number + " != 0.0) {\n" +
        ArkTSConversionThrow(std::string(ARKTS_INVALID) + "not a finite number for an index key", "        ") +
        "    }\n";
    code += "    if (" + number + " < -9.223372036854776e18 || " + number + " >= 9.223372036854776e18) {\n" +
        ArkTSConversionThrow(std::string(ARKTS_INVALID) + "index key out of Int64 range", "        ") + "    }\n";
    code += "    if (" + number + " != Float64(Int64(" + number + "))) {\n" +
        ArkTSConversionThrow(std::string(ARKTS_INVALID) + "not an integer index key", "        ") + "    }\n";
    code += "    let " + canonical + " = Int64(" + number + ").toString()\n";
    key = canonical;
}

// Setter tail of the index-signature default implementation: convert the
// second argument to JS and store it into the indexed property.
static std::string ArkTSInterfaceIndexSetterBody(ArkTSValueEmitter& emitter, const ArkTSTypeDesc& valueType,
    const std::string& prefix, const std::string& code, const std::string& self, const std::string& key,
    const std::string& context)
{
    auto value = emitter.ToJS(valueType, std::string(ARKTS_PLACEHOLDER_ARGUMENT) + "1", prefix + "context", "    ");
    auto error = emitter.Fresh("error");
    const auto setExpr = self + ".setProperty(" + key + ", " + value.expr + ")";
    return ArkTSFinishBody(prefix,
        code + value.pre + "    " + ArkTSCallTranslation(setExpr, context, error, "    ") + "\n");
}

// Getter tail of the index-signature default implementation: read the
// indexed property and wrap it in an Option, a null or undefined property
// being Option.None and any other property Option.Some of the converted
// value. Refuses the whole body when the conversion cannot spell a value.
static std::string ArkTSInterfaceIndexGetterBody(ArkTSValueEmitter& emitter, const ArkTSTypeDesc& valueType,
    const std::string& prefix, const std::string& code, const std::string& self, const std::string& key,
    const std::string& context)
{
    auto result = emitter.Fresh("result");
    auto error = emitter.Fresh("error");
    const auto getExpr = self + ".getProperty(" + key + ")";
    const auto read = "    let " + result + " = " + ArkTSCallTranslation(getExpr, context, error, "    ") + "\n";
    auto inner = emitter.FromJS(valueType, result, "    ");
    if (emitter.broken) {
        return {};
    }
    const std::string option = "(if (" + result + ".isNull() || " + result + ".isUndefined()) {\n" + "    " +
        ARKTS_OPTION + ".None\n" + "} else {\n" + inner.pre + "    " + ARKTS_OPTION + ".Some(" + inner.expr + ")\n" +
        "})";
    return ArkTSFinishBody(prefix, code + read + "    return " + option + "\n");
}

std::string ArkTSInterfaceIndexSource(const ArkTSTypeDesc& keyType, const ArkTSTypeDesc& valueType, bool setter,
    const std::string& prefix, std::unordered_map<std::string, Ptr<Decl>>& classRefs, const std::string& ownerKind)
{
    if (!ArkTSTypeDescGeneratable(keyType) || ArkTSTypeDescContainsNothing(keyType) ||
        !ArkTSTypeDescGeneratable(valueType) || ArkTSTypeDescContainsNothing(valueType)) {
        return {};
    }
    const bool stringKey = keyType.kind == ArkTSTypeDesc::Kind::SCALAR && keyType.scalar == "String";
    const bool numberKey = keyType.kind == ArkTSTypeDesc::Kind::SCALAR && keyType.scalar == "Float64";
    if (!stringKey && !numberKey) {
        return {};
    }
    ArkTSValueEmitter emitter{prefix};
    emitter.classRefs = &classRefs;
    std::string key = std::string(ARKTS_PLACEHOLDER_ARGUMENT) + "0";
    std::string code;
    if (numberKey) {
        EmitArkTSNumberIndexKey(emitter, key, code);
    }
    const auto context = ArkTSMemberFailureContext(ownerKind, ArkTSPlaceholderLiteralImpl(ARKTS_PLACEHOLDER_CLASS),
        ArkTSPlaceholderLiteralImpl(ARKTS_PLACEHOLDER_MEMBER));
    auto self = emitter.Fresh("self");
    code = "    let " + self + " = " + ARKTS_SELF + "." + ARKTS_CLASS_REF_ACCESSOR + "()\n" + code;
    if (setter) {
        return ArkTSInterfaceIndexSetterBody(emitter, valueType, prefix, code, self, key, context);
    }
    return ArkTSInterfaceIndexGetterBody(emitter, valueType, prefix, code, self, key, context);
}

} // namespace Cangjie::Native::FFI::ArkTS
