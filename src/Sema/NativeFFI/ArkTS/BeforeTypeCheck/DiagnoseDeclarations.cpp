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

#include <string>
#include <unordered_set>

using namespace Cangjie;
using namespace Cangjie::AST;
namespace Cangjie::Native::FFI::ArkTS {
namespace {
constexpr const char* ENTRY_SHAPE_NOTE =
    "an @ArkTS entry is a top-level public parameterless function with an explicit 'Unit' return type";
constexpr const char* ENTRY_DUPLICATE_NOTE = "entry names are registered with JSModule.registerFunc";

const Annotation* FindArkTSAnnotation(const Decl& decl)
{
    for (const auto& annotation : decl.annotations) {
        if (annotation->kind == AnnotationKind::ARKTS) {
            return annotation.get();
        }
    }
    return nullptr;
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

// The first unmet entry rule in declaration order. One entry reports one rule,
// so a later shape mistake is not restated on the same declaration.
std::string EntryShapeReason(const FuncDecl& fd)
{
    if (!fd.TestAttr(Attribute::PUBLIC)) {
        return "must be 'public'";
    }
    if (!HasSingleEmptyParameterList(fd)) {
        return "must not take parameters";
    }
    if (!DeclaresExplicitUnitReturnType(fd)) {
        return "must declare an explicit 'Unit' return type";
    }
    const auto* annotation = FindArkTSAnnotation(fd);
    if (annotation != nullptr && !annotation->args.empty() && !HasOnlyWellFormedModuleOption(*annotation)) {
        return "must not declare options";
    }
    return {};
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
            if (fd != nullptr && IsArkTSEntry(*fd)) {
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
            if (fd != nullptr && IsArkTSEntry(*fd)) {
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
            if (!fd || !IsArkTSEntry(*fd) || fd->TestAttr(Attribute::IS_BROKEN) || !fd->funcBody) {
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
                    DiagKindRefactor::sema_arkts_entry_invalid, *fd, fd->identifier.Val(), reason);
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
            // An entry reports its annotation options as an entry-shape
            // reason; only a non-entry declaration reports the dedicated
            // module rule.
            auto fd = DynamicCast<FuncDecl*>(decl);
            if (fd != nullptr && isTopLevel && IsArkTSEntry(*fd)) {
                return VisitAction::WALK_CHILDREN;
            }
            // 'module' is checked only on the top-level declaration kinds it
            // applies to; parameters, enums and other declarations are handled
            // by their own rules.
            const bool moduleOptionKind = decl->astKind == ASTKind::FUNC_DECL ||
                decl->astKind == ASTKind::CLASS_DECL || decl->astKind == ASTKind::INTERFACE_DECL ||
                decl->astKind == ASTKind::VAR_DECL;
            if (!isTopLevel || !moduleOptionKind) {
                return VisitAction::WALK_CHILDREN;
            }
            // Option-shape diagnostics keep precedence: when the layer is
            // malformed (non-literal, unknown, duplicated or empty value), the
            // existing option rule is the single report for this declaration.
            if (CheckArkTSOptionLayer(*annotation).problem != ArkTSOptionProblem::NONE) {
                return VisitAction::WALK_CHILDREN;
            }
            for (const auto& arg : annotation->args) {
                if (arg->name.Val() != "module") {
                    continue;
                }
                auto literal = DynamicCast<LitConstExpr*>(arg->expr.get());
                if (literal && literal->kind == LitConstKind::STRING && !ArkTSOptionValueEmpty(literal->stringValue)) {
                    diag.DiagnoseRefactor(
                        DiagKindRefactor::sema_arkts_option_module_only_foreign, *arg->expr, decl->identifier.Val());
                }
                break;
            }
            return VisitAction::WALK_CHILDREN;
        });
        walker.Walk();
    }
}
} // namespace

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
} // namespace Cangjie::Native::FFI::ArkTS
