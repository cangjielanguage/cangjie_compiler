// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "../ArkTSInterop.h"

#include "../Utils/EntrySelection.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Option/Option.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace Cangjie;
using namespace Cangjie::AST;
namespace Cangjie::Native::FFI::ArkTS {
namespace {
constexpr const char* ENTRY_SHAPE_NOTE =
    "this release implements only the minimal entry shape: a public parameterless function with an explicit 'Unit' "
    "return type";
constexpr const char* ENTRY_DUPLICATE_NOTE = "entry names are registered with JSModule.registerFunc";

const Annotation* FindAnnotation(const Decl& decl, AnnotationKind kind)
{
    for (const auto& annotation : decl.annotations) {
        if (annotation->kind == kind) {
            return annotation.get();
        }
    }
    return nullptr;
}

Annotation* FindAnnotation(Decl& decl, AnnotationKind kind)
{
    for (auto& annotation : decl.annotations) {
        if (annotation->kind == kind) {
            return annotation.get();
        }
    }
    return nullptr;
}

const Annotation* FindArkTSAnnotation(const Decl& decl)
{
    return FindAnnotation(decl, AnnotationKind::ARKTS);
}

// The annotations that exist only for the ArkTS interop capability. Any one of
// them in source selects the capability and therefore the experimental gate.
bool IsArkTSOnlyAnnotation(AnnotationKind kind)
{
    return kind == AnnotationKind::ARKTS || kind == AnnotationKind::ARKTS_RAW_VALUE ||
        kind == AnnotationKind::ARKTS_REST_PARAM ||
        kind == AnnotationKind::ARKTS_WRITE_ONLY || kind == AnnotationKind::ARKTS_HAS_THIS;
}

// A non-foreign entry may not declare type parameters. The parser rejects the
// same shape only for a `foreign` function, so the generic spelling reaches
// this pass for an ordinary @ArkTS function.
bool HasTypeParameters(const FuncDecl& fd)
{
    return fd.funcBody && fd.funcBody->generic && !fd.funcBody->generic->typeParameters.empty();
}

bool HasSingleEmptyParameterList(const FuncDecl& fd)
{
    return fd.funcBody && fd.funcBody->paramLists.size() == 1 && fd.funcBody->paramLists.front() &&
        fd.funcBody->paramLists.front()->params.empty();
}

// Only an explicitly written 'Unit' satisfies the entry contract; an omitted
// return type parses as an absent node and is a different spelling.
bool DeclaresExplicitUnitReturnType(const FuncDecl& fd)
{
    if (!fd.funcBody || !fd.funcBody->retType) {
        return false;
    }
    auto primitive = DynamicCast<const PrimitiveType*>(fd.funcBody->retType.get());
    return primitive && primitive->str == "Unit";
}

// A well-formed 'module' option is the single option on an @ArkTS entry. Its
// refusal belongs to the foreign-only option rule rather than the entry shape.
bool HasOnlyWellFormedModuleOption(const Annotation& annotation)
{
    if (annotation.args.size() != 1) {
        return false;
    }
    const auto& arg = annotation.args.front();
    if (arg == nullptr || arg->name.Val() != "module") {
        return false;
    }
    auto literal = DynamicCast<LitConstExpr*>(arg->expr.get());
    return literal != nullptr && literal->kind == LitConstKind::STRING &&
        !ArkTSOptionValueEmpty(literal->stringValue);
}

// The first unmet rule of this release's minimal entry shape, in declaration
// order. One entry reports one rule, so a later shape mistake is not restated
// on the same declaration. The reasons describe the staged limitation, not a
// language rule; a non-public declaration never reaches this function.
std::string EntryShapeReason(const FuncDecl& fd)
{
    if (!HasSingleEmptyParameterList(fd)) {
        return "it declares parameters";
    }
    if (!DeclaresExplicitUnitReturnType(fd)) {
        return "it does not declare an explicit 'Unit' return type";
    }
    const auto* annotation = FindArkTSAnnotation(fd);
    if (annotation != nullptr && !annotation->args.empty() && !HasOnlyWellFormedModuleOption(*annotation)) {
        return "it declares options other than 'module'";
    }
    return {};
}

// A non-public @ArkTS declaration is accepted but not exported, so it is not
// an entry: it is neither shape-checked nor registered, and no callback is
// generated for it.
bool IsExportedArkTSEntry(const FuncDecl& fd)
{
    return IsArkTSEntry(fd) && fd.TestAttr(Attribute::PUBLIC);
}

// The declaration kind that @ArkTS cannot annotate. An empty result means the
// declaration is a supported top-level kind. A function is a class member when
// it already carries a class-like owner (the parser sets that for members);
// otherwise it is a local function.
std::string UnsupportedPlacementKind(const Decl& decl, bool topLevel)
{
    switch (decl.astKind) {
        case ASTKind::FUNC_DECL:
            if (!topLevel) {
                return decl.outerDecl && decl.outerDecl->IsClassLikeDecl() ? "a class member" : "a local function";
            }
            return {};
        case ASTKind::STRUCT_DECL:
            return "a struct";
        case ASTKind::TYPE_ALIAS_DECL:
            return "a type alias";
        case ASTKind::EXTEND_DECL:
            return "an extend";
        case ASTKind::MACRO_DECL:
            return "a macro";
        case ASTKind::VAR_DECL:
            if (!topLevel) {
                return decl.outerDecl && decl.outerDecl->IsNominalDecl() ? "a class member" : "a local variable";
            }
            return {};
        case ASTKind::PROP_DECL:
            return "a class member";
        case ASTKind::PRIMARY_CTOR_DECL:
            return "a class member";
        case ASTKind::FUNC_PARAM:
            // A parameter annotation has no declaration to carry the
            // interop contract; it must not be accepted silently.
            return "a function parameter";
        default:
            return {};
    }
}

// An extend declaration has no identifier; its extended type is the spelling
// a user recognizes in the diagnostic.
std::string DeclarationDisplayName(const Decl& decl)
{
    if (auto extend = DynamicCast<const ExtendDecl*>(&decl); extend && extend->extendedType) {
        return extend->extendedType->ToString();
    }
    return decl.identifier.Val();
}

bool IsPreparedOrGenerated(const Decl& decl)
{
    return decl.TestAnyAttr(Attribute::FOREIGN, Attribute::COMPILER_ADD, Attribute::IS_BROKEN,
        Attribute::ARKTS_FUNCTION_WRAPPER, Attribute::ARKTS_CLASS_WRAPPER, Attribute::ARKTS_INTERFACE_WRAPPER,
        Attribute::ARKTS_LOWERED);
}

// Generated entry and registration declarations use these names. Any
// declaration whose name starts with either form is reserved.
bool IsReservedGeneratedName(const std::string& name)
{
    const std::string prefix = ARKTS_GENERATED_PREFIX;
    return name.rfind(prefix + "entry_", 0) == 0 || name.rfind(prefix + "registration_", 0) == 0;
}

// A reserved name is refused whenever the package declares an @ArkTS entry,
// regardless of the entry suffix or of missing interop imports. The entries
// are refused so no generated declaration collides with the occupied name.
void DiagnoseReservedGeneratedNames(Package& pkg, DiagnosticEngine& diag)
{
    bool hasEntry = false;
    for (auto& file : pkg.files) {
        for (auto& decl : file->decls) {
            auto fd = DynamicCast<FuncDecl*>(decl.get());
            if (fd != nullptr && IsExportedArkTSEntry(*fd)) {
                hasEntry = true;
                break;
            }
        }
        if (hasEntry) {
            break;
        }
    }
    if (!hasEntry) {
        return;
    }
    bool conflict = false;
    for (auto& file : pkg.files) {
        for (auto& decl : file->decls) {
            if (decl->TestAttr(Attribute::COMPILER_ADD) || !IsReservedGeneratedName(decl->identifier.Val())) {
                continue;
            }
            auto builder = diag.DiagnoseRefactor(
                DiagKindRefactor::sema_arkts_entry_generated_name_conflict, *decl, decl->identifier.Val());
            builder.AddNote("generated entry declarations are reserved by the compiler");
            conflict = true;
        }
    }
    if (!conflict) {
        return;
    }
    for (auto& file : pkg.files) {
        for (auto& decl : file->decls) {
            auto fd = DynamicCast<FuncDecl*>(decl.get());
            if (fd != nullptr && IsExportedArkTSEntry(*fd)) {
                fd->EnableAttr(Attribute::IS_BROKEN);
            }
        }
    }
}

void DiagnoseEntries(Package& pkg, DiagnosticEngine& diag)
{
    std::unordered_set<std::string> registeredNames;
    for (auto& file : pkg.files) {
        for (auto& decl : file->decls) {
            auto fd = DynamicCast<FuncDecl*>(decl.get());
            if (!fd || !IsArkTSEntry(*fd) || !fd->TestAttr(Attribute::PUBLIC) ||
                fd->TestAttr(Attribute::IS_BROKEN) || !fd->funcBody) {
                continue;
            }
            if (HasTypeParameters(*fd)) {
                fd->EnableAttr(Attribute::IS_BROKEN);
                diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_function_generic,
                    fd->identifier.Begin(), fd->identifier.Val());
                continue;
            }
            const auto reason = EntryShapeReason(*fd);
            if (!reason.empty()) {
                fd->EnableAttr(Attribute::IS_BROKEN);
                auto builder = diag.DiagnoseRefactor(
                    DiagKindRefactor::sema_arkts_entry_shape_not_supported, *fd, fd->identifier.Val(), reason);
                builder.AddNote(ENTRY_SHAPE_NOTE);
                continue;
            }
            // A well-formed 'module' option on an entry is refused by the
            // foreign-only option rule at the option literal.
            if (const auto* annotation = FindArkTSAnnotation(*fd);
                annotation != nullptr && HasOnlyWellFormedModuleOption(*annotation)) {
                fd->EnableAttr(Attribute::IS_BROKEN);
                diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_option_module_only_foreign,
                    *annotation->args.front()->expr, fd->identifier.Val());
                continue;
            }
            // Only entries that survive the shape rule register a name: a
            // refused entry produces no registration and cannot collide.
            if (!registeredNames.emplace(fd->identifier.Val()).second) {
                fd->EnableAttr(Attribute::IS_BROKEN);
                auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_entry_name_duplicated, *fd,
                    fd->identifier.Val(), pkg.fullPackageName);
                builder.AddNote(ENTRY_DUPLICATE_NOTE);
            }
        }
    }
}

// The 'module' option on a top-level declaration that is not an entry. The
// option-shape rule keeps precedence: a malformed layer is not re-reported
// here, and the scan stops at the first 'module' argument.
void DiagnoseNonEntryModuleOption(const Decl& decl, const Annotation& annotation, DiagnosticEngine& diag)
{
    if (CheckArkTSOptionLayer(annotation).problem != ArkTSOptionProblem::NONE) {
        return;
    }
    for (const auto& arg : annotation.args) {
        if (arg->name.Val() != "module") {
            continue;
        }
        auto literal = DynamicCast<LitConstExpr*>(arg->expr.get());
        if (literal && literal->kind == LitConstKind::STRING && !ArkTSOptionValueEmpty(literal->stringValue)) {
            diag.DiagnoseRefactor(
                DiagKindRefactor::sema_arkts_option_module_only_foreign, *arg->expr, decl.identifier.Val());
        }
        break;
    }
}

// Placement and the 'module' option of one declaration. A declaration already
// refused for its placement must not also report its options.
VisitAction DiagnosePlacementAndOptionsOfDecl(
    Ptr<Node> node, DiagnosticEngine& diag, const std::unordered_set<const Decl*>& topLevel)
{
    auto decl = DynamicCast<Decl*>(node);
    if (decl == nullptr || IsPreparedOrGenerated(*decl)) {
        return VisitAction::WALK_CHILDREN;
    }
    const auto* annotation = FindArkTSAnnotation(*decl);
    if (annotation == nullptr) {
        return VisitAction::WALK_CHILDREN;
    }
    const bool isTopLevel = topLevel.count(decl) != 0;
    const auto kind = UnsupportedPlacementKind(*decl, isTopLevel);
    if (!kind.empty()) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_annotation_placement_invalid, *annotation, kind,
            DeclarationDisplayName(*decl));
        return VisitAction::WALK_CHILDREN;
    }
    // An entry reports its annotation options as an entry-shape reason; only
    // a non-entry declaration reports the dedicated module rule.
    auto fd = DynamicCast<FuncDecl*>(decl);
    if (fd != nullptr && isTopLevel && IsArkTSEntry(*fd)) {
        return VisitAction::WALK_CHILDREN;
    }
    // 'module' is checked only on the top-level declaration kinds it applies
    // to; parameters, enums and other declarations have their own rules.
    const bool moduleOptionKind = decl->astKind == ASTKind::FUNC_DECL ||
        decl->astKind == ASTKind::CLASS_DECL || decl->astKind == ASTKind::INTERFACE_DECL ||
        decl->astKind == ASTKind::VAR_DECL;
    if (!isTopLevel || !moduleOptionKind) {
        return VisitAction::WALK_CHILDREN;
    }
    DiagnoseNonEntryModuleOption(*decl, *annotation, diag);
    return VisitAction::WALK_CHILDREN;
}

// Placement and the 'module' option share one walk because a declaration that
// is already refused for its placement must not also report its options.
void DiagnosePlacementAndOptions(Package& pkg, DiagnosticEngine& diag)
{
    for (auto& file : pkg.files) {
        std::unordered_set<const Decl*> topLevel;
        for (auto& decl : file->decls) {
            topLevel.emplace(decl.get());
        }
        Walker walker(file.get(), [&diag, &topLevel](Ptr<Node> node) {
            return DiagnosePlacementAndOptionsOfDecl(node, diag, topLevel);
        });
        walker.Walk();
    }
}

// An @ArkTS class or interface declaration. The annotation may be written on
// the declaration or inherited from a foreign block.
bool IsArkTSAnnotatedClassOrInterface(const Decl* decl)
{
    return decl != nullptr && (DynamicCast<const ClassDecl*>(decl) != nullptr ||
        DynamicCast<const InterfaceDecl*>(decl) != nullptr) && FindArkTSAnnotation(*decl) != nullptr;
}

// The first unmet @WriteOnly placement rule, in declaration order.
std::string WriteOnlyPlacementReason(const Decl& decl)
{
    auto prop = DynamicCast<const PropDecl*>(&decl);
    if (prop == nullptr) {
        return "only a property member can be annotated with '@WriteOnly'";
    }
    if (!IsArkTSAnnotatedClassOrInterface(decl.outerDecl)) {
        return "only a member of an '@ArkTS' class or interface can be annotated with '@WriteOnly'";
    }
    if (!prop->isVar) {
        return "a write-only property must be declared as 'mut prop'";
    }
    return {};
}

// The placement of a '@HasThis' annotation that is not on an '@ArkTS' foreign
// function. The wording names the declaration kind the user wrote, so the
// message reads as a placement mistake rather than an unknown annotation.
std::string HasThisPlacementKind(const Decl& decl)
{
    if (DynamicCast<const FuncDecl*>(&decl) != nullptr) {
        return "a function that is not an '@ArkTS' foreign function";
    }
    switch (decl.astKind) {
        case ASTKind::CLASS_DECL:
            return "a class declaration";
        case ASTKind::INTERFACE_DECL:
            return "an interface declaration";
        case ASTKind::STRUCT_DECL:
            return "a struct declaration";
        case ASTKind::ENUM_DECL:
            return "an enum declaration";
        case ASTKind::VAR_DECL:
            return "a variable declaration";
        case ASTKind::PROP_DECL:
            return "a property declaration";
        case ASTKind::FUNC_PARAM:
            return "a function parameter";
        case ASTKind::TYPE_ALIAS_DECL:
            return "a type alias";
        default:
            return "this declaration";
    }
}

// The '@HasThis' argument must be a type reference, which is an identifier or
// a dotted identifier path. The parser rejects other shapes before this pass;
// this check keeps a rejected shape from being recorded as a contract if the
// declaration is reached anyway.
bool IsHasThisTypeReference(const Expr* expr)
{
    if (expr == nullptr) {
        return false;
    }
    if (expr->astKind == ASTKind::REF_EXPR) {
        return true;
    }
    if (auto access = DynamicCast<const MemberAccess*>(expr); access != nullptr) {
        return IsHasThisTypeReference(access->baseExpr.get());
    }
    return false;
}

// '@HasThis' maps an ArkTS method `this`, so the annotated declaration has a
// fixed function type and must not participate in a Cangjie overload set: its
// name has to be unique among the package's top-level functions. Member
// functions live in their own scopes and are deliberately not indexed, and a
// declaration already refused by another rule is out of the package scope, so
// it cannot create a conflict.
using TopLevelFunctions = std::unordered_map<std::string, std::vector<FuncDecl*>>;

TopLevelFunctions IndexTopLevelFunctions(Package& pkg)
{
    TopLevelFunctions byName;
    for (auto& file : pkg.files) {
        for (auto& decl : file->decls) {
            auto fd = DynamicCast<FuncDecl*>(decl.get());
            if (fd == nullptr || fd->identifier.Empty() ||
                fd->TestAnyAttr(Attribute::COMPILER_ADD, Attribute::IS_BROKEN)) {
                continue;
            }
            byName[fd->identifier.Val()].push_back(fd);
        }
    }
    return byName;
}

bool HasSameNameFunction(const FuncDecl& fd, const TopLevelFunctions& byName)
{
    auto found = byName.find(fd.identifier.Val());
    if (found == byName.end()) {
        return false;
    }
    return std::any_of(found->second.begin(), found->second.end(),
        [&fd](const FuncDecl* other) { return other != &fd; });
}

// A refused @WriteOnly leaves a foreign owner without a complete wrapper
// shape; other owners keep their remaining diagnostics.
void MarkForeignOwnerBroken(Decl* owner)
{
    if (auto classDecl = DynamicCast<ClassDecl*>(owner);
        classDecl != nullptr && IsArkTSForeignClass(*classDecl)) {
        classDecl->EnableAttr(Attribute::IS_BROKEN);
        return;
    }
    if (auto interfaceDecl = DynamicCast<InterfaceDecl*>(owner);
        interfaceDecl != nullptr && IsArkTSForeignInterface(*interfaceDecl)) {
        interfaceDecl->EnableAttr(Attribute::IS_BROKEN);
    }
}
} // namespace

// @WriteOnly is valid only on a mutable property member of an @ArkTS class or
// interface; a refused annotation is marked broken before preparation.
void DiagnoseArkTSWriteOnlyPlacement(Package& pkg, DiagnosticEngine& diag)
{
    if (pkg.TestAttr(Attribute::IMPORTED)) {
        return;
    }
    for (auto& file : pkg.files) {
        Walker walker(file.get(), [&diag](Ptr<Node> node) {
            auto decl = DynamicCast<Decl*>(node);
            // An enum constructor is refused by the parser's annotation
            // allow-list; other enum members follow the placement rule.
            const bool enumConstructor = decl != nullptr && decl->TestAttr(Attribute::ENUM_CONSTRUCTOR);
            if (decl == nullptr || enumConstructor ||
                decl->TestAnyAttr(Attribute::COMPILER_ADD, Attribute::IS_BROKEN,
                    Attribute::ARKTS_FUNCTION_WRAPPER, Attribute::ARKTS_CLASS_WRAPPER,
                    Attribute::ARKTS_INTERFACE_WRAPPER, Attribute::ARKTS_LOWERED)) {
                return VisitAction::WALK_CHILDREN;
            }
            Annotation* annotation = FindAnnotation(*decl, AnnotationKind::ARKTS_WRITE_ONLY);
            // The parser reports a malformed annotation (arguments present)
            // itself; one annotation keeps one report.
            if (annotation == nullptr || annotation->TestAttr(Attribute::IS_BROKEN)) {
                return VisitAction::WALK_CHILDREN;
            }
            const auto reason = WriteOnlyPlacementReason(*decl);
            if (reason.empty()) {
                return VisitAction::WALK_CHILDREN;
            }
            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_write_only_placement_invalid, *annotation, reason);
            // The annotation itself is refused; the declaration stays usable
            // for the rest of the package, except a foreign owner whose
            // wrapper shape cannot be built.
            annotation->EnableAttr(Attribute::IS_BROKEN);
            MarkForeignOwnerBroken(decl->outerDecl);
            return VisitAction::WALK_CHILDREN;
        });
        walker.Walk();
    }
}

// Validate non-foreign @ArkTS declaration contracts: entry shape and name,
// reserved generated names, annotation placement and the foreign-only
// 'module' option. Every refused entry is marked IS_BROKEN so no callback or
// registration is generated for it. Legal declarations and Java/ObjC/C
// foreign declarations are unchanged.
void DiagnoseArkTSDeclarations(Package& pkg, DiagnosticEngine& diag)
{
    if (pkg.TestAttr(Attribute::IMPORTED)) {
        return;
    }
    DiagnoseEntries(pkg, diag);
    DiagnosePlacementAndOptions(pkg, diag);
    DiagnoseReservedGeneratedNames(pkg, diag);
}

bool ArkTSTargetSupported([[maybe_unused]] const GlobalOptions& opts)
{
#ifdef CANGJIE_ENABLE_ARKTS_INTEROP
    // Host-target LLT and unit suites have no OHOS std module set, so they
    // cannot compile for the real target. The variable is a test-harness
    // escape, deliberately undocumented and never emitted by the driver.
    const char* testHostTarget = std::getenv("CANGJIE_ARKTS_INTEROP_TEST_HOST_TARGET");
    if (testHostTarget != nullptr && std::string(testHostTarget) == "1") {
        return true;
    }
    return opts.target.env == Triple::Environment::OHOS;
#else
    // The target gate is part of the optional interop capability: a build that does
    // not enable it treats every target as unsupported.
    return false;
#endif
}

// One diagnostic per source file, at its first ArkTS-only annotation. The walk
// covers declarations and parameters alike because annotations are AST nodes;
// a file with several annotated declarations reports the failed gate once.
void DiagnoseCapabilityGate(Package& pkg, DiagnosticEngine& diag, bool experimentalGate)
{
    if (pkg.TestAttr(Attribute::IMPORTED)) {
        return;
    }
    const auto kind = experimentalGate ? DiagKindRefactor::sema_arkts_requires_experimental
                                       : DiagKindRefactor::sema_arkts_requires_ohos_target;
    for (auto& file : pkg.files) {
        if (file == nullptr) {
            continue;
        }
        bool reported = false;
        Walker(file.get(), [&diag, &reported, kind](Ptr<Node> node) {
            // Report once, at the first ArkTS-only annotation of the file.
            // The declaration is deliberately left unbroken: marking a gated
            // declaration IS_BROKEN removes it from symbol lookup and turns
            // every ordinary reference to it into a cascading "undeclared"
            // error, which would defeat the one-diagnostic gate contract.
            auto annotation = DynamicCast<Annotation*>(node);
            if (!reported && annotation != nullptr && IsArkTSOnlyAnnotation(annotation->kind)) {
                diag.DiagnoseRefactor(kind, *annotation);
                reported = true;
            }
            return VisitAction::WALK_CHILDREN;
        }).Walk();
    }
}

// '@HasThis[TypeName]' is the ArkTS method-`this` contract. Before preparation
// rewrites the declaration, the annotation must sit on an '@ArkTS' foreign
// function, appear once (the parser owns the repeated-annotation rule) and not
// coexist with 'globalVar: true'. A valid contract is recorded on the
// declaration so later stages do not re-parse the annotation; the argument's
// resolved type is checked after type checking, because at this point it is a
// type reference, not a semantic type.
namespace {
// The first usable '@HasThis' annotation of a declaration.
Annotation* FindUsableHasThisAnnotation(Decl& decl)
{
    for (auto& annotation : decl.annotations) {
        if (annotation != nullptr && annotation->kind == AnnotationKind::ARKTS_HAS_THIS &&
            !annotation->TestAttr(Attribute::IS_BROKEN)) {
            return annotation.get();
        }
    }
    return nullptr;
}

// Record a well-formed '@HasThis' contract, or refuse an overload conflict.
void ApplyHasThisContract(
    FuncDecl& fd, Annotation& hasThis, DiagnosticEngine& diag, const TopLevelFunctions& topLevelFunctions)
{
    // The parser refuses a malformed argument before this pass; only a
    // well-formed type-reference contract reaches the stored attribute and the
    // post-type-check type rule.
    const bool wellFormed = hasThis.args.size() == 1 && hasThis.args.front() != nullptr &&
        hasThis.args.front()->name.Empty() && IsHasThisTypeReference(hasThis.args.front()->expr.get());
    if (!wellFormed) {
        hasThis.EnableAttr(Attribute::IS_BROKEN);
        return;
    }
    // A '@HasThis' declaration names one ArkTS method and cannot join an
    // overload set, so a same-name function in the package scope is refused.
    if (HasSameNameFunction(fd, topLevelFunctions)) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_has_this_overload_conflict, hasThis, fd.identifier.Val());
        fd.EnableAttr(Attribute::IS_BROKEN);
        hasThis.EnableAttr(Attribute::IS_BROKEN);
        return;
    }
    fd.EnableAttr(Attribute::ARKTS_HAS_THIS);
}

// The '@HasThis' placement rules of one declaration.
VisitAction DiagnoseHasThisOnDecl(Ptr<Node> node, DiagnosticEngine& diag, const TopLevelFunctions& topLevelFunctions)
{
    auto decl = DynamicCast<Decl*>(node);
    if (decl == nullptr || decl->TestAnyAttr(Attribute::COMPILER_ADD, Attribute::IS_BROKEN)) {
        return VisitAction::WALK_CHILDREN;
    }
    auto hasThis = FindUsableHasThisAnnotation(*decl);
    if (hasThis == nullptr) {
        return VisitAction::WALK_CHILDREN;
    }
    auto fd = DynamicCast<FuncDecl*>(decl);
    // A prepared wrapper keeps the contract it validated before the rewrite;
    // a repeated preparation pass must not report it as misplaced.
    if (fd != nullptr && fd->TestAttr(Attribute::ARKTS_FUNCTION_WRAPPER)) {
        fd->EnableAttr(Attribute::ARKTS_HAS_THIS);
        return VisitAction::WALK_CHILDREN;
    }
    if (fd == nullptr || !IsArkTSForeignFunction(*fd)) {
        diag.DiagnoseRefactor(
            DiagKindRefactor::sema_arkts_has_this_placement_invalid, *hasThis, HasThisPlacementKind(*decl));
        hasThis->EnableAttr(Attribute::IS_BROKEN);
        return VisitAction::WALK_CHILDREN;
    }
    if (IsGlobalVar(*fd)) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_has_this_global_var_conflict, *hasThis);
        hasThis->EnableAttr(Attribute::IS_BROKEN);
        return VisitAction::WALK_CHILDREN;
    }
    ApplyHasThisContract(*fd, *hasThis, diag, topLevelFunctions);
    return VisitAction::WALK_CHILDREN;
}
} // namespace

void DiagnoseArkTSHasThis(Package& pkg, DiagnosticEngine& diag)
{
    if (pkg.TestAttr(Attribute::IMPORTED)) {
        return;
    }
    // The overload name rule spans the whole package, so the index is built
    // before the per-declaration walk below.
    const auto topLevelFunctions = IndexTopLevelFunctions(pkg);
    for (auto& file : pkg.files) {
        Walker walker(file.get(), [&diag, &topLevelFunctions](Ptr<Node> node) {
            return DiagnoseHasThisOnDecl(node, diag, topLevelFunctions);
        });
        walker.Walk();
    }
}

// The '@HasThis' argument is a type reference, so its semantic type only
// exists after the checker synthesized the annotation (CheckAnnotations). This
// post-type-check pass classifies that type with the same shared mapper as an
// ArkTS signature position. An ArkTS `this` is an object reference, so the
// resolved TypeName must classify as a reference (a prepared foreign class or
// interface); a scalar or any unsupported type is refused instead of being
// carried into the CJO contract.
namespace {
// Classify the resolved type of a '@HasThis' argument. The parser already
// refused a malformed argument shape (the false result lets the walker look at
// a later annotation); an ArkTS `this` is an object reference, so anything but
// a reference type is refused.
bool ValidateHasThisAnnotationType(Annotation& annotation, DiagnosticEngine& diag, ImportManager* imports)
{
    if (annotation.args.size() != 1 || annotation.args.front() == nullptr ||
        !annotation.args.front()->name.Empty() || annotation.args.front()->expr == nullptr) {
        return false;
    }
    auto& arg = *annotation.args.front();
    auto ty = arg.expr->DataTy();
    if (ty == nullptr || !Ty::IsTyCorrect(ty)) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_has_this_type_unsupported, arg, arg.expr->ToString());
        annotation.EnableAttr(Attribute::IS_BROKEN);
        return true;
    }
    auto desc = ArkTSClassifyType(ty, imports);
    if (desc.kind != ArkTSTypeDesc::Kind::REFERENCE) {
        // Report the spelling the user wrote, not the internal Ty name
        // ("Class-Plain").
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_has_this_type_unsupported, arg, arg.expr->ToString());
        annotation.EnableAttr(Attribute::IS_BROKEN);
    }
    return true;
}

// Validate the '@HasThis' argument types of one declaration.
VisitAction ValidateHasThisTypesOnDecl(Ptr<Node> node, DiagnosticEngine& diag, ImportManager* imports)
{
    auto decl = DynamicCast<Decl*>(node);
    if (decl == nullptr || !decl->TestAttr(Attribute::ARKTS_HAS_THIS)) {
        return VisitAction::WALK_CHILDREN;
    }
    for (auto& annotation : decl->annotations) {
        if (annotation == nullptr || annotation->kind != AnnotationKind::ARKTS_HAS_THIS ||
            annotation->TestAttr(Attribute::IS_BROKEN)) {
            continue;
        }
        if (ValidateHasThisAnnotationType(*annotation, diag, imports)) {
            break;
        }
    }
    return VisitAction::WALK_CHILDREN;
}
} // namespace

void ValidateArkTSHasThisTypes(Package& pkg, DiagnosticEngine& diag, ImportManager* imports)
{
    if (pkg.TestAttr(Attribute::IMPORTED) || !PassEnabled(pkg)) {
        return;
    }
    for (auto& file : pkg.files) {
        Walker walker(file.get(), [&diag, imports](Ptr<Node> node) {
            return ValidateHasThisTypesOnDecl(node, diag, imports);
        });
        walker.Walk();
    }
}
} // namespace Cangjie::Native::FFI::ArkTS
