// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "../ArkTSInterop.h"

#include <cstddef>
#include <functional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "../FunctionSource.h"
#include "../Utils/ReferenceMembers.h"
#include "cangjie/AST/Clone.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Modules/ImportManager.h"
#include "cangjie/Modules/ModulesUtils.h"

using namespace Cangjie;
using namespace Cangjie::AST;

namespace Cangjie::Native::FFI::ArkTS {
namespace {
// The qualification path a parent entry is written with: `Base` (RefType) is
// the one-segment path Base; `b.Base` and `ab.Base` (QualifiedType, whose
// field is the last segment) keep the qualifier segment, base first. A
// package import is registered under its last segment only (`import a.b`
// binds `b`, `import a.b as ab` binds `ab`), so a longer spelling such as
// `a.b.Base` names no imported package and is not a supported parent
// spelling. Names cannot be resolved through the checker yet
// (preparation runs before decl resolution), so the wrapper-parent tests
// below resolve this spelling against the package's own declarations and the
// file's imports; a qualified spelling never falls back to a bare-name match,
// because its qualifier selects the package the parent must come from.
std::vector<std::string> ParentSpellingPath(const Type& type)
{
    if (auto ref = DynamicCast<const RefType*>(&type)) {
        // A wrapper class is never generic, so a parameterized spelling cannot
        // name one; leaving it unmatched keeps the class a root wrapper.
        if (!ref->typeArguments.empty()) {
            return {};
        }
        return {ref->ref.identifier.Val()};
    }
    if (auto qualified = DynamicCast<const QualifiedType*>(&type)) {
        if (!qualified->baseType || !qualified->typeArguments.empty() || qualified->field.Val().empty()) {
            return {};
        }
        auto path = ParentSpellingPath(*qualified->baseType);
        if (path.empty()) {
            return {};
        }
        path.emplace_back(qualified->field.Val());
        return path;
    }
    return {};
}

// What a written parent spelling names at preparation time. The answer
// decides only the wrapper shape; the checked inheritance itself is still
// resolved by the generic checker.
enum class ParentKind {
    // Not a wrapper parent: the spelling names a local declaration of another
    // kind, an imported non-wrapper, or nothing that could be a wrapper.
    PLAIN,
    // A foreign class of this package, which preparation will turn into a
    // wrapper; its own preparation result decides the child's shape.
    PACKAGE_CANDIDATE,
    // An imported declaration that is already a prepared wrapper.
    IMPORTED_WRAPPER,
};

// Resolve one parent spelling. @p packageDecls indexes every top-level
// declaration of this package by identifier, because a bare spelling resolves
// against the package first: a local declaration of any kind shadows an
// imported same-named one, so a same-named local plain class must not borrow
// the imported wrapper's child shape. A qualified spelling instead resolves
// the qualifier to the imported package it names (an alias included) and
// consults that package's own member.
ParentKind ResolveParentKind(const std::vector<std::string>& path, const ClassDecl& child, ImportManager& imports,
    const std::string& srcPackageName, const std::unordered_map<std::string, Ptr<Decl>>& packageDecls)
{
    if (path.empty() || child.curFile == nullptr) {
        return ParentKind::PLAIN;
    }
    if (path.size() == 1) {
        auto found = packageDecls.find(path.front());
        if (found != packageDecls.end()) {
            auto cd = DynamicCast<ClassDecl*>(found->second);
            if (cd != nullptr && (IsArkTSForeignClass(*cd) || IsArkTSClassWrapper(*cd))) {
                return ParentKind::PACKAGE_CANDIDATE;
            }
            return ParentKind::PLAIN;
        }
        // Exactly one imported declaration may answer the name: two distinct
        // imports of one name are ambiguous to the checker, and a parent the
        // checker cannot resolve must not select the child shape.
        auto imported = imports.GetImportedDeclsByName(*child.curFile, path.front());
        if (imported.size() == 1 && IsArkTSClassWrapper(*imported.front())) {
            return ParentKind::IMPORTED_WRAPPER;
        }
        return ParentKind::PLAIN;
    }
    auto qualifier = path.front();
    for (size_t i = 1; i + 1 < path.size(); ++i) {
        qualifier += "." + path[i];
    }
    auto [packageDecl, conflicted] = imports.GetImportedPackageDecl(&child, qualifier);
    if (conflicted || packageDecl == nullptr) {
        return ParentKind::PLAIN;
    }
    auto member = imports.GetImportedDecl(packageDecl->fullPackageName, path.back());
    if (member == nullptr) {
        return ParentKind::PLAIN;
    }
    // The checker resolves the qualified member only when it is visible from
    // this package; an invisible parent would leave the child's generated
    // super delegations unresolved, so it must not select the child shape
    // either.
    auto relation = Modules::GetPackageRelation(srcPackageName, packageDecl->fullPackageName);
    if (!Modules::IsVisible(*member, relation)) {
        return ParentKind::PLAIN;
    }
    return IsArkTSClassWrapper(*member) ? ParentKind::IMPORTED_WRAPPER : ParentKind::PLAIN;
}

// Refuse a foreign class: report one shape violation and mark the class
// broken. Returns false so a check can `return RefuseForeignClass(...)`.
bool RefuseForeignClass(ClassDecl& cd, DiagnosticEngine& diag, const std::string& reason)
{
    diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_class_invalid, cd, cd.identifier.Val(), reason);
    cd.EnableAttr(Attribute::IS_BROKEN);
    return false;
}

// The class-level modifiers a foreign class must not carry.
bool CheckForeignClassModifiers(ClassDecl& cd, DiagnosticEngine& diag)
{
    if (cd.TestAttr(Attribute::SEALED)) {
        return RefuseForeignClass(cd, diag, "a sealed class is not supported");
    }
    return true;
}

// A parameter name with the generated-code prefix is reserved.
bool CheckForeignClassParamNames(const FuncDecl& fd, ClassDecl& cd, DiagnosticEngine& diag)
{
    for (auto& list : fd.funcBody->paramLists) {
        for (auto& param : list->params) {
            if (param->identifier.Val().rfind(ARKTS_RESERVED_PREFIX, 0) == 0) {
                return RefuseForeignClass(
                    cd, diag, "parameter name '" + param->identifier.Val() + "' is reserved for generated code");
            }
        }
    }
    return true;
}

// Member and parameter names reserved for generated code.
bool CheckForeignClassReservedNames(ClassDecl& cd, DiagnosticEngine& diag)
{
    for (auto& member : cd.GetMemberDecls()) {
        if (member->identifier.Val().rfind(ARKTS_RESERVED_PREFIX, 0) == 0) {
            return RefuseForeignClass(
                cd, diag, "member name '" + member->identifier.Val() + "' is reserved for generated code");
        }
        auto fd = DynamicCast<FuncDecl*>(member.get());
        if (!fd || !fd->funcBody) {
            continue;
        }
        if (!CheckForeignClassParamNames(*fd, cd, diag)) {
            return false;
        }
    }
    return true;
}

// A user-written accessor body, which lowering cannot coexist with.
bool PropHasUserWrittenBody(const PropDecl& prop)
{
    for (const auto& accessor : prop.getters) {
        if (accessor && accessor->funcBody && accessor->funcBody->body) {
            return true;
        }
    }
    for (const auto& accessor : prop.setters) {
        if (accessor && accessor->funcBody && accessor->funcBody->body) {
            return true;
        }
    }
    return false;
}

// Member shape rules from the declaration constraints: no private members, no
// member variables, no finalizer or primary constructor, and no user-written
// member body (lowering supplies every body).
bool CheckForeignClassMemberShape(Decl& member, ClassDecl& cd, DiagnosticEngine& diag)
{
    if (member.TestAttr(Attribute::PRIVATE)) {
        return RefuseForeignClass(cd, diag, "member '" + member.identifier.Val() + "' must not be private");
    }
    if (member.astKind == ASTKind::VAR_DECL || member.astKind == ASTKind::PRIMARY_CTOR_DECL) {
        return RefuseForeignClass(cd, diag, "member variables and primary constructors are not supported");
    }
    // A user-written property accessor is refused by the accessor-body rule;
    // a compiler-inserted property carries generated accessors only.
    if (auto prop = DynamicCast<PropDecl*>(&member); prop && !prop->TestAttr(Attribute::COMPILER_ADD)) {
        if (PropHasUserWrittenBody(*prop)) {
            return RefuseForeignClass(cd, diag, "member '" + prop->identifier.Val() + "' must not have a body");
        }
    }
    auto fd = DynamicCast<FuncDecl*>(&member);
    if (fd == nullptr) {
        return true;
    }
    if (fd->IsFinalizer()) {
        return RefuseForeignClass(cd, diag, "a finalizer is not supported");
    }
    if (fd->funcBody && fd->funcBody->body) {
        return RefuseForeignClass(cd, diag, "member '" + fd->identifier.Val() + "' must not have a body");
    }
    return true;
}

bool CheckForeignClassMemberShapes(ClassDecl& cd, DiagnosticEngine& diag)
{
    for (auto& member : cd.GetMemberDecls()) {
        if (!CheckForeignClassMemberShape(*member, cd, diag)) {
            return false;
        }
    }
    return true;
}

// The wrapper needs the interop declaration of its carrier imported: JSObject
// for an ordinary wrapper.
bool CheckForeignClassJSObject(ClassDecl& cd, DiagnosticEngine& diag, ImportManager& imports)
{
    if (FindArkTSJSObject(imports, *cd.curFile) != nullptr) {
        return true;
    }
    return RefuseForeignClass(cd, diag,
        "the wrapper needs the imported declaration 'ohos.ark_interop." + std::string(ARKTS_JS_OBJECT) + "'");
}

// Clone the property type onto the first setter parameter when it lacks one.
void PrepareSetterParameterType(const PropDecl& prop, FuncDecl& setter)
{
    if (!setter.funcBody || setter.funcBody->paramLists.empty() ||
        setter.funcBody->paramLists.front()->params.empty() || !prop.type) {
        return;
    }
    auto& param = setter.funcBody->paramLists.front()->params.front();
    if (!param->type) {
        param->type = ASTCloner::Clone(prop.type.get());
    }
}

// Give a property without accessor signatures the ones lowering fills.
void PreparePropertySignatures(PropDecl& prop)
{
    if (ArkTSWriteOnlyProp(prop)) {
        // Persist the write-only contract for importing packages: the
        // ARKTS_WRITE_ONLY annotation itself has no CJO record. A non-mutable
        // property has already been refused by the placement check.
        prop.EnableAttr(Attribute::ARKTS_WRITE_ONLY_PROP);
        // Keep a getter declaration for the property contract. The read
        // rejection itself comes from the ArkTS write-only reference check,
        // not from access control (the reference resolves to the property
        // declaration, so the getter visibility does not gate the read).
        if (prop.getters.empty()) {
            InsertPropGetterSignature(prop, Attribute::ARKTS_CLASS_WRAPPER);
        }
        for (auto& getter : prop.getters) {
            getter->DisableAttr(Attribute::PUBLIC);
            getter->EnableAttr(Attribute::PRIVATE);
        }
    } else if (prop.getters.empty()) {
        InsertPropGetterSignature(prop, Attribute::ARKTS_CLASS_WRAPPER);
    }
    if (prop.isVar && prop.setters.empty()) {
        InsertPropSetterSignature(prop, Attribute::ARKTS_CLASS_WRAPPER);
    }
    for (auto& getter : prop.getters) {
        if (getter->funcBody && !getter->funcBody->retType && prop.type) {
            getter->funcBody->retType = ASTCloner::Clone(prop.type.get());
        }
    }
    for (auto& setter : prop.setters) {
        PrepareSetterParameterType(prop, *setter);
    }
}

// Prepare the user members before wrapper installation: design §5.1.2 keeps
// the abstract members of an abstract foreign class abstract (a Cangjie
// subclass implements them), the rest are cleared, and user properties gain
// their accessor signatures.
void PrepareForeignClassMembers(ClassDecl& cd)
{
    for (auto& member : cd.GetMemberDecls()) {
        if (!(cd.TestAttr(Attribute::ABSTRACT) && member->TestAttr(Attribute::ABSTRACT))) {
            member->DisableAttr(Attribute::ABSTRACT);
        }
        auto prop = DynamicCast<PropDecl*>(member.get());
        if (!prop || prop->TestAttr(Attribute::COMPILER_ADD)) {
            continue;
        }
        PreparePropertySignatures(*prop);
    }
}

// Before: an unprepared @ArkTS foreign class. After result: every refusal
// check has run and the user members carry their accessor signatures; the
// class is marked IS_BROKEN when any check refused it. This is the
// user-visible half of preparation: every diagnostic the class can produce is
// emitted here, so the wrapper-member installation below (and the parent
// shape decision that orders it) stays silent and cannot reorder the
// diagnostics. Returns true when the class survived and will be installed as
// a wrapper.
bool CheckForeignClassShape(ClassDecl& cd, DiagnosticEngine& diag, ImportManager& imports)
{
    if (!CheckForeignClassModifiers(cd, diag) || !CheckForeignClassReservedNames(cd, diag) ||
        !CheckForeignClassMemberShapes(cd, diag) || !CheckForeignClassJSObject(cd, diag, imports)) {
        return false;
    }
    PrepareForeignClassMembers(cd);
    return true;
}

// Before: a foreign class whose shape checks passed. After result: the class
// is an ordinary Cangjie class marked ARKTS_CLASS_WRAPPER with its reference
// members declared; FOREIGN stays as the source identity while the marker
// carries the prepared state, so member bodies can be attached by lowering.
// @p wrapperParent tells InsertReferenceMembers whether a prepared
// wrapper parent supplies the storage (delegating wrapping constructors only)
// or the class is a root wrapper (field, accessor and both wrapping
// constructors). Returns true when the class became a wrapper; a failed
// installation diagnoses the class and marks it IS_BROKEN.
bool InstallWrapperMembers(
    ClassDecl& cd, bool wrapperParent, DiagnosticEngine& diag, SourceManager& sourceManager, ImportManager& imports)
{
    if (!InsertArkTSReferenceMembers(cd, wrapperParent, diag, sourceManager, imports)) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_class_invalid, cd, cd.identifier.Val(),
            "cannot declare the wrapper reference members");
        cd.EnableAttr(Attribute::IS_BROKEN);
        return false;
    }
    Utils::EraseIf(cd.modifiers, [](auto& mod) { return mod.modifier == TokenKind::FOREIGN; });
    cd.EnableAttr(Attribute::ARKTS_CLASS_WRAPPER);
    return true;
}

// Every top-level declaration of the package by identifier, for the bare
// parent spellings; the prepared entries keep the index valid on a repeated
// pass over an already prepared tree.
std::unordered_map<std::string, Ptr<Decl>> IndexPackageDecls(const Package& pkg)
{
    std::unordered_map<std::string, Ptr<Decl>> packageDecls;
    for (auto& file : pkg.files) {
        for (auto& decl : file->decls) {
            packageDecls.emplace(decl->identifier.Val(), decl.get());
        }
    }
    return packageDecls;
}

// A user generic @ArkTS foreign class cannot be lowered into a fixed wrapper;
// only the stdlib presets keep the generic external form.
bool IsUnsupportedGenericForeignClass(const ClassDecl& cd)
{
    return !cd.TestAttr(Attribute::IS_BROKEN) && cd.HasAnno(AnnotationKind::ARKTS) &&
        cd.TestAttr(Attribute::FOREIGN) && !IsArkTSClassWrapper(cd) && cd.generic != nullptr &&
        !cd.generic->typeParameters.empty();
}

// Diagnose an unsupported generic foreign class outside the preset package.
// Returns true when the class was refused.
bool RefuseUnsupportedGenericForeignClass(ClassDecl& cd, DiagnosticEngine& diag)
{
    if (!IsUnsupportedGenericForeignClass(cd)) {
        return false;
    }
    diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_generic_foreign_unsupported, cd, cd.identifier.Val());
    cd.EnableAttr(Attribute::IS_BROKEN);
    return true;
}

// The check phase for one declaration: the generic rule first, then the shape
// checks of an unprepared foreign class. Returns the surviving class or null.
Ptr<ClassDecl> CheckForeignClassDecl(Ptr<Decl> decl, DiagnosticEngine& diag, ImportManager& imports)
{
    auto cd = DynamicCast<ClassDecl*>(decl);
    if (cd != nullptr && RefuseUnsupportedGenericForeignClass(*cd, diag)) {
        return nullptr;
    }
    // A class already marked broken keeps its refusal diagnostic and its
    // FOREIGN source identity, so a repeated preparation skips the check (an
    // installed wrapper also carries ARKTS_CLASS_WRAPPER).
    if (cd == nullptr || !IsArkTSForeignClass(*cd) || cd->TestAttr(Attribute::IS_BROKEN)) {
        return nullptr;
    }
    return CheckForeignClassShape(*cd, diag, imports) ? cd : nullptr;
}

// The install result of one class.
enum class InstallResult {
    // The class became a prepared wrapper.
    WRAPPER,
    // The class is broken: its own checks refused it, its parent broke it, or
    // the wrapper members could not be declared.
    BROKEN,
    // The class names itself as an ancestor; the inheritance cycle is
    // reported by the generic checker, so the class is installed without
    // taking this parent entry for a wrapper.
    CYCLIC,
};

// State shared by the recursive wrapper install.
struct InstallState {
    ImportManager& imports;
    DiagnosticEngine& diag;
    SourceManager& sourceManager;
    const std::string& srcPackageName;
    const std::unordered_map<std::string, Ptr<Decl>>& packageDecls;
    std::set<Ptr<ClassDecl>>& installing;
};

using InstallFn = std::function<InstallResult(Ptr<ClassDecl>)>;

void DropBrokenParents(ClassDecl& cd, const std::vector<size_t>& brokenParents)
{
    for (auto i = brokenParents.rbegin(); i != brokenParents.rend(); ++i) {
        cd.inheritedTypes.erase(cd.inheritedTypes.begin() + static_cast<std::ptrdiff_t>(*i));
    }
    cd.EnableAttr(Attribute::IS_BROKEN);
}

// Decide the root/child shape from the parents the class spells, installing
// same-package wrapper parents first. Returns false when a named parent broke,
// in which case the child breaks silently.
bool ResolveWrapperParents(ClassDecl& cd, InstallState& state, const InstallFn& install, bool& wrapperParent)
{
    std::vector<size_t> brokenParents;
    for (size_t i = 0; i < cd.inheritedTypes.size(); ++i) {
        auto path = ParentSpellingPath(*cd.inheritedTypes[i]);
        switch (ResolveParentKind(path, cd, state.imports, state.srcPackageName, state.packageDecls)) {
            case ParentKind::PACKAGE_CANDIDATE: {
                auto parent = DynamicCast<ClassDecl*>(state.packageDecls.at(path.front()));
                auto parentResult = install(parent);
                if (parentResult == InstallResult::BROKEN) {
                    brokenParents.push_back(i);
                } else if (parentResult == InstallResult::WRAPPER) {
                    wrapperParent = true;
                }
                // CYCLIC leaves this parent entry out of the shape decision;
                // the generic checker owns the cycle diagnostic.
                break;
            }
            case ParentKind::IMPORTED_WRAPPER:
                wrapperParent = true;
                break;
            case ParentKind::PLAIN:
            default:
                break;
        }
    }
    if (brokenParents.empty()) {
        return true;
    }
    // A child shape would resolve the generated super(...) delegations against
    // a class that never became a wrapper; the parent refusal has already
    // spoken, so the child breaks silently and declares no wrapper member.
    DropBrokenParents(cd, brokenParents);
    return false;
}

// Install the wrapper members of one class, and first those of the
// same-package wrapper parents it names.
InstallResult InstallClassWrapper(Ptr<ClassDecl> cd, InstallState& state, const InstallFn& install)
{
    if (IsArkTSClassWrapper(*cd) && !cd->TestAttr(Attribute::IS_BROKEN)) {
        return InstallResult::WRAPPER; // installed on demand or by an earlier round
    }
    if (cd->TestAttr(Attribute::IS_BROKEN)) {
        return InstallResult::BROKEN; // the check phase refused this class or its parent
    }
    if (!state.installing.insert(cd).second) {
        return InstallResult::CYCLIC;
    }
    bool wrapperParent = false;
    if (!ResolveWrapperParents(*cd, state, install, wrapperParent)) {
        state.installing.erase(cd);
        return InstallResult::BROKEN;
    }
    auto installed = InstallWrapperMembers(*cd, wrapperParent, state.diag, state.sourceManager, state.imports);
    state.installing.erase(cd);
    return installed ? InstallResult::WRAPPER : InstallResult::BROKEN;
}
} // namespace

// Walk every source file and prepare each unprepared non-generic foreign
// class; member bodies stay empty until lowering fills them. Generic classes
// keep their external declaration form.
// Preparation runs in two phases over the package:
// - the check phase (file order) runs every refusal check and the
//   user-member preparation of each foreign class, so each class's fate is
//   known before any wrapper member is inserted. Its diagnostics come first,
//   in file order;
// - the install phase inserts the wrapper members with each class's
//   root/child shape decided from the declarations its parents spell: a
//   same-package wrapper parent is installed first (a class may be declared
//   before the parent it names), and a parent that ended up broken breaks
//   the child directly with no wrapper member at all - the child shape's
//   generated super(...) delegations would otherwise resolve against a class
//   the checker cannot use as their target and crash the diagnostic
//   printer. The parent refusal has already diagnosed itself, so the child
//   breaks silently.
// The split moves one diagnostic relative to a single walk: a wrapper member
// declaration that fails ('cannot declare the wrapper reference members') is
// only detected during install, so it is reported after the checks of every
// class instead of at its own position. That path is reachable only when the
// compiler-owned holder source fails to parse, so it is an internal-error
// shape, not a user-facing ordering contract.
void PrepareClasses(Package& pkg, DiagnosticEngine& diag, SourceManager& sourceManager, ImportManager* imports)
{
    if (!PassEnabled(pkg) || imports == nullptr) {
        return;
    }
    const auto packageDecls = IndexPackageDecls(pkg);
    std::vector<Ptr<ClassDecl>> survivors;
    for (auto& file : pkg.files) {
        for (auto& decl : file->decls) {
            if (auto survivor = CheckForeignClassDecl(decl.get(), diag, *imports)) {
                survivors.emplace_back(survivor);
            }
        }
    }
    std::set<Ptr<ClassDecl>> installing;
    InstallState state{*imports, diag, sourceManager, pkg.fullPackageName, packageDecls, installing};
    InstallFn install = [&state, &install](Ptr<ClassDecl> cd) -> InstallResult {
        return InstallClassWrapper(cd, state, install);
    };
    for (auto cd : survivors) {
        (void)install(cd);
    }
}
} // namespace Cangjie::Native::FFI::ArkTS
