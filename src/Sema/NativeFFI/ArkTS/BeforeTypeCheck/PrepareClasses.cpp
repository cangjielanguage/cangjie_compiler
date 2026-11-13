// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "../ArkTSInterop.h"

#include <functional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "../../ObjC/Utils/ASTInserter.h"
#include "../FunctionSource.h"
#include "cangjie/AST/Clone.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Modules/ImportManager.h"
#include "cangjie/Modules/ModulesUtils.h"
#include "cangjie/Parse/Parser.h"

using namespace Cangjie;
using namespace Cangjie::AST;
using namespace Cangjie::Interop::ObjC;

namespace Cangjie::Native::FFI::ArkTS {
namespace {
constexpr const char* ARKTS_SUPPORT_HOLDER = "__cj_arkts_support";
constexpr const char* ARKTS_RESERVED_PREFIX = "__cj_arkts_";
constexpr const char* ARKTS_JS_OBJECT = "JSObject";
// The full package name the JSObject import must come from. FunctionSource.h
// uses ARKTS_INTEROP_PACKAGE for the short package alias ("ark_interop") the
// generated code spells; this constant is the full name the injected imports
// bind, so the two spellings stay apart.
constexpr const char* ARKTS_INTEROP_FULL_PACKAGE = "ohos.ark_interop";

Ptr<Decl> FindImportedJSObject(ImportManager& imports, const File& file)
{
    for (auto decl : imports.GetImportedDeclsByName(file, ARKTS_JS_OBJECT)) {
        if (decl->fullPackageName == ARKTS_INTEROP_FULL_PACKAGE) {
            return decl;
        }
    }
    return nullptr;
}

// Before: the class body carries no generated storage. After result: the
// compiler-owned wrapper members parsed from a holder shape and reparented
// onto the user class. Source positions and the permanent file are the
// class's; the holder itself is discarded.
// A root wrapper (no wrapper parent) owns the storage:
//   class C { ... }
//     -> class C { ...; private var __cj_arkts_ref: JSObject
//                  public init(__cj_arkts_ref: JSObject, _: Unit) { ... }
//                  public init(__cj_arkts_ref: () -> JSObject, _: Unit) { ... }
//                  public func __cj_arkts_ref_value(): JSObject { ... } }
// The thunk-taking constructor lets a subclass constructor forward a freshly
// built ArkTS object through super(...) (Cangjie requires super(...) to be the
// constructor's first expression, so the multi-statement construction reaches
// it as one thunk value).
// A child wrapper (wrapper parent) inherits the root's field and accessor and
// only redeclares the two wrapping constructors as delegations, because
// constructors are not inherited and the missing declarations would otherwise
// make the class unconstructible from an ArkTS value at every level below the
// root:
//   class C <: WrapperParent { ... }
//     -> class C <: WrapperParent { ...;
//                  public init(__cj_arkts_ref: JSObject, _: Unit) { super(__cj_arkts_ref, ()) }
//                  public init(__cj_arkts_ref: () -> JSObject, _: Unit) { super(__cj_arkts_ref, ()) } }
bool InsertReferenceMembers(
    ClassDecl& cd, bool wrapperParent, DiagnosticEngine& diag, SourceManager& sourceManager, ImportManager& imports)
{
    std::string code = std::string("class ") + ARKTS_SUPPORT_HOLDER + " {\n";
    if (!wrapperParent) {
        code += "    private var " + std::string(ARKTS_CLASS_REF_FIELD) + ": " + ARKTS_JS_OBJECT + "\n";
    }
    code += "    public init(" + std::string(ARKTS_CLASS_CTOR_PARAM) + ": " + ARKTS_JS_OBJECT + ", _: Unit) {\n";
    code += wrapperParent ? std::string("        super(") + ARKTS_CLASS_CTOR_PARAM + ", ())\n"
                          : std::string("        this.") + ARKTS_CLASS_REF_FIELD + " = " + ARKTS_CLASS_CTOR_PARAM +
            "\n";
    code += "    }\n";
    code += "    public init(" + std::string(ARKTS_CLASS_CTOR_PARAM) + ": () -> " + ARKTS_JS_OBJECT + ", _: Unit) {\n";
    code += wrapperParent ? std::string("        super(") + ARKTS_CLASS_CTOR_PARAM + ", ())\n"
                          : std::string("        this.") + ARKTS_CLASS_REF_FIELD + " = " + ARKTS_CLASS_CTOR_PARAM +
            "()\n";
    code += "    }\n";
    if (!wrapperParent) {
        code += "    public func " + std::string(ARKTS_CLASS_REF_ACCESSOR) + "(): " + ARKTS_JS_OBJECT +
            " {\n        return this." + ARKTS_CLASS_REF_FIELD + "\n    }\n";
    }
    code += "}\n";
    const auto errorsBefore = diag.GetErrorCount();
    Parser parser(code, diag, sourceManager, cd.begin);
    parser.SetCurFile(cd.curFile);
    auto holder = parser.ParseTopLevel();
    if (!holder || diag.GetErrorCount() != errorsBefore || holder->decls.empty()) {
        return false;
    }
    auto holderClass = DynamicCast<ClassDecl*>(holder->decls.front().get());
    if (!holderClass || !holderClass->body) {
        return false;
    }
    auto jsObject = FindImportedJSObject(imports, *cd.curFile);
    if (jsObject == nullptr) {
        return false;
    }
    std::vector<OwnedPtr<Decl>> members;
    for (auto& member : holderClass->body->decls) {
        member->EnableAttr(Attribute::COMPILER_ADD);
        members.emplace_back(std::move(member));
    }
    ASTInserter inserter;
    for (auto& member : members) {
        Walker(member.get(), [&](Ptr<Node> node) {
            auto type = DynamicCast<RefType*>(node);
            if (type && type->ref.identifier == ARKTS_JS_OBJECT && !type->ref.target) {
                type->ref.target = jsObject;
                type->SetTy(jsObject->GetTy());
            }
            return VisitAction::WALK_CHILDREN;
        }).Walk();
        inserter.InsertInto(cd, std::move(member), InsertMode::RECURSIVE);
    }
    return true;
}

// The qualification path a parent entry is written with: `Base` (RefType) is
// the one-segment path Base; `dep.Base`, `d.Base` and `a.b.Base`
// (QualifiedType, whose field is the last segment) keep every qualifier
// segment, base first. Names cannot be resolved through the checker yet
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
    for (auto& member : cd.GetMemberDecls()) {
        if (member->identifier.Val().rfind(ARKTS_RESERVED_PREFIX, 0) == 0) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_class_invalid, cd, cd.identifier.Val(),
                "member name '" + member->identifier.Val() + "' is reserved for generated code");
            cd.EnableAttr(Attribute::IS_BROKEN);
            return false;
        }
        auto fd = DynamicCast<FuncDecl*>(member.get());
        if (!fd || !fd->funcBody) {
            continue;
        }
        for (auto& list : fd->funcBody->paramLists) {
            for (auto& param : list->params) {
                if (param->identifier.Val().rfind(ARKTS_RESERVED_PREFIX, 0) != 0) {
                    continue;
                }
                diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_class_invalid, cd, cd.identifier.Val(),
                    "parameter name '" + param->identifier.Val() + "' is reserved for generated code");
                cd.EnableAttr(Attribute::IS_BROKEN);
                return false;
            }
        }
    }
    // Member shape rules from the declaration constraints: no private
    // members, no member variables, no finalizer or primary constructor, and
    // no user-written member body (lowering supplies every body).
    for (auto& member : cd.GetMemberDecls()) {
        if (member->TestAttr(Attribute::PRIVATE)) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_class_invalid, cd, cd.identifier.Val(),
                "member '" + member->identifier.Val() + "' must not be private");
            cd.EnableAttr(Attribute::IS_BROKEN);
            return false;
        }
        if (member->astKind == ASTKind::VAR_DECL || member->astKind == ASTKind::PRIMARY_CTOR_DECL) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_class_invalid, cd, cd.identifier.Val(),
                "member variables and primary constructors are not supported");
            cd.EnableAttr(Attribute::IS_BROKEN);
            return false;
        }
        // A user-written property accessor is refused by the accessor-body
        // rule in the PropDecl block below; the parser requires an accessor
        // body, so every valid source accessor carries one and a second
        // declaration-level check here would only duplicate that rule.
        auto fd = DynamicCast<FuncDecl*>(member.get());
        // A property accessor is not in GetMemberDecls() (only the PropDecl
        // is), so the FuncDecl body rule below never sees a user-written
        // getter or setter body. Lowering supplies every accessor body the
        // same way it supplies member function bodies, so a user body is
        // refused with the same message. The accessor signatures inserted by
        // preparation are COMPILER_ADD and bodyless, and are not present yet
        // at this point, so they cannot be misreported.
        if (auto prop = DynamicCast<PropDecl*>(member.get())) {
            // A compiler-inserted property carries generated accessors only;
            // never apply the user-body rule to it.
            if (prop->TestAttr(Attribute::COMPILER_ADD)) {
                continue;
            }
            bool hasBody = false;
            for (const auto& accessor : prop->getters) {
                hasBody = hasBody || (accessor && accessor->funcBody && accessor->funcBody->body);
            }
            for (const auto& accessor : prop->setters) {
                hasBody = hasBody || (accessor && accessor->funcBody && accessor->funcBody->body);
            }
            if (hasBody) {
                diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_class_invalid, cd, cd.identifier.Val(),
                    "member '" + prop->identifier.Val() + "' must not have a body");
                cd.EnableAttr(Attribute::IS_BROKEN);
                return false;
            }
        }
        if (!fd) {
            continue;
        }
        if (fd->IsFinalizer()) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_class_invalid, cd, cd.identifier.Val(),
                "a finalizer is not supported");
            cd.EnableAttr(Attribute::IS_BROKEN);
            return false;
        }
        if (fd->funcBody && fd->funcBody->body) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_class_invalid, cd, cd.identifier.Val(),
                "member '" + fd->identifier.Val() + "' must not have a body");
            cd.EnableAttr(Attribute::IS_BROKEN);
            return false;
        }
    }
    auto jsObject = FindImportedJSObject(imports, *cd.curFile);
    if (jsObject == nullptr) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_class_invalid, cd, cd.identifier.Val(),
            "the wrapper needs the imported declaration 'ohos.ark_interop." + std::string(ARKTS_JS_OBJECT) + "'");
        cd.EnableAttr(Attribute::IS_BROKEN);
        return false;
    }
    for (auto& member : cd.GetMemberDecls()) {
        member->DisableAttr(Attribute::ABSTRACT);
        // A property declared without accessors gets accessor signatures with
        // the property type cloned onto them; lowering fills their bodies.
        auto prop = DynamicCast<PropDecl*>(member.get());
        if (!prop || prop->TestAttr(Attribute::COMPILER_ADD)) {
            continue;
        }
        if (ArkTSWriteOnlyProp(*prop)) {
            // Persist the write-only contract for importing packages: the
            // WRITE_ONLY annotation itself has no CJO record.
            prop->EnableAttr(Attribute::ARKTS_WRITE_ONLY_PROP);
            if (!prop->isVar) {
                diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_class_invalid, cd, cd.identifier.Val(),
                    "@WriteOnly requires a mutable property");
                cd.EnableAttr(Attribute::IS_BROKEN);
                return false;
            }
            // Keep a getter declaration for the property contract. The read
            // rejection itself comes from the ArkTS write-only reference
            // check, not from access control (the reference resolves to the
            // property declaration, so the getter visibility does not gate
            // the source read).
            if (prop->getters.empty()) {
                InsertPropGetterSignature(*prop, Attribute::ARKTS_CLASS_WRAPPER);
            }
            for (auto& getter : prop->getters) {
                getter->DisableAttr(Attribute::PUBLIC);
                getter->EnableAttr(Attribute::PRIVATE);
            }
        } else if (prop->getters.empty()) {
            InsertPropGetterSignature(*prop, Attribute::ARKTS_CLASS_WRAPPER);
        }
        if (prop->isVar && prop->setters.empty()) {
            InsertPropSetterSignature(*prop, Attribute::ARKTS_CLASS_WRAPPER);
        }
        for (auto& getter : prop->getters) {
            if (getter->funcBody && !getter->funcBody->retType && prop->type) {
                getter->funcBody->retType = ASTCloner::Clone(prop->type.get());
            }
        }
        for (auto& setter : prop->setters) {
            if (!setter->funcBody || setter->funcBody->paramLists.empty() ||
                setter->funcBody->paramLists.front()->params.empty() || !prop->type) {
                continue;
            }
            auto& param = setter->funcBody->paramLists.front()->params.front();
            if (!param->type) {
                param->type = ASTCloner::Clone(prop->type.get());
            }
        }
    }
    return true;
}

// Before: a foreign class whose shape checks passed. After result: the class
// is an ordinary Cangjie class marked ARKTS_CLASS_WRAPPER with its reference
// members declared; FOREIGN is removed so member bodies can be attached by
// lowering. @p wrapperParent tells InsertReferenceMembers whether a prepared
// wrapper parent supplies the storage (delegating wrapping constructors only)
// or the class is a root wrapper (field, accessor and both wrapping
// constructors). Returns true when the class became a wrapper; a failed
// installation diagnoses the class and marks it IS_BROKEN.
bool InstallWrapperMembers(
    ClassDecl& cd, bool wrapperParent, DiagnosticEngine& diag, SourceManager& sourceManager, ImportManager& imports)
{
    if (!InsertReferenceMembers(cd, wrapperParent, diag, sourceManager, imports)) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_class_invalid, cd, cd.identifier.Val(),
            "cannot declare the wrapper reference members");
        cd.EnableAttr(Attribute::IS_BROKEN);
        return false;
    }
    cd.DisableAttr(Attribute::FOREIGN);
    Utils::EraseIf(cd.modifiers, [](auto& mod) { return mod.modifier == TokenKind::FOREIGN; });
    cd.EnableAttr(Attribute::ARKTS_CLASS_WRAPPER);
    return true;
}
} // namespace

// Walk every source file and prepare each unprepared non-generic foreign
// class; member bodies stay empty until lowering fills them. Generic classes
// and the stdlib presets keep their external declaration form.
// Preparation runs in two phases over the package:
// - the check phase (file order) runs every refusal check and the
//   user-member preparation of each foreign class, so each class's fate is
//   known before any wrapper member is inserted and the diagnostics keep the
//   order a single-pass walk produced;
// - the install phase inserts the wrapper members with each class's
//   root/child shape decided from the declarations its parents spell: a
//   same-package wrapper parent is installed first (a class may be declared
//   before the parent it names), and a parent that ended up broken breaks
//   the child directly with no wrapper member at all - the child shape's
//   generated super(...) delegations would otherwise resolve against a class
//   the checker cannot use as their target and crash the diagnostic
//   printer. The parent refusal has already diagnosed itself, so the child
//   breaks silently.
void PrepareClasses(Package& pkg, DiagnosticEngine& diag, SourceManager& sourceManager, ImportManager* imports)
{
    if (imports == nullptr) {
        return;
    }
    // Every top-level declaration of the package by identifier, for the bare
    // parent spellings; the prepared entries keep the index valid on a
    // repeated pass over an already prepared tree.
    std::unordered_map<std::string, Ptr<Decl>> packageDecls;
    for (auto& file : pkg.files) {
        for (auto& decl : file->decls) {
            packageDecls.emplace(decl->identifier.Val(), decl.get());
        }
    }
    std::vector<Ptr<ClassDecl>> survivors;
    for (auto& file : pkg.files) {
        for (auto& decl : file->decls) {
            auto cd = DynamicCast<ClassDecl*>(decl.get());
            if (!cd || !IsArkTSForeignClass(*cd)) {
                continue;
            }
            if (CheckForeignClassShape(*cd, diag, *imports)) {
                survivors.emplace_back(cd);
            }
        }
    }
    // The install result of one class.
    enum class InstallResult {
        // The class became a prepared wrapper.
        WRAPPER,
        // The class is broken: its own checks refused it, its parent broke
        // it, or the wrapper members could not be declared.
        BROKEN,
        // The class names itself as an ancestor; the inheritance cycle is
        // reported by the generic checker, so the class is installed without
        // taking this parent entry for a wrapper.
        CYCLIC,
    };
    // Install the wrapper members of @p cd, and first those of the
    // same-package wrapper parents it names.
    std::set<Ptr<ClassDecl>> installing;
    std::function<InstallResult(Ptr<ClassDecl>)> install = [&](Ptr<ClassDecl> cd) -> InstallResult {
        if (IsArkTSClassWrapper(*cd) && !cd->TestAttr(Attribute::IS_BROKEN)) {
            return InstallResult::WRAPPER; // installed on demand or by an earlier round
        }
        if (cd->TestAttr(Attribute::IS_BROKEN)) {
            return InstallResult::BROKEN; // the check phase refused this class or its parent
        }
        if (!installing.insert(cd).second) {
            return InstallResult::CYCLIC;
        }
        bool wrapperParent = false;
        bool brokenByParent = false;
        for (auto inherited = cd->inheritedTypes.begin(); inherited != cd->inheritedTypes.end();) {
            auto path = ParentSpellingPath(**inherited);
            switch (ResolveParentKind(path, *cd, *imports, pkg.fullPackageName, packageDecls)) {
                case ParentKind::PACKAGE_CANDIDATE: {
                    auto parent = DynamicCast<ClassDecl*>(packageDecls.at(path.front()));
                    auto parentResult = install(parent);
                    if (parentResult == InstallResult::BROKEN) {
                        // A child shape against this parent would resolve the
                        // generated super(...) delegations against a class
                        // that never became a wrapper; the parent refusal has
                        // already spoken, so the child breaks silently and
                        // declares no wrapper member at all. The parent entry
                        // is removed with it: it names a broken declaration,
                        // and resolving it would only report 'undeclared type
                        // name' once per subclass for a cause the parent's
                        // own diagnostic already states.
                        inherited = cd->inheritedTypes.erase(inherited);
                        brokenByParent = true;
                        continue;
                    }
                    if (parentResult == InstallResult::WRAPPER) {
                        wrapperParent = true;
                    }
                    // CYCLIC leaves this parent entry out of the shape
                    // decision; the generic checker owns the cycle
                    // diagnostic and a root-shaped member set stays
                    // resolvable while it is reported.
                    break;
                }
                case ParentKind::IMPORTED_WRAPPER:
                    wrapperParent = true;
                    break;
                case ParentKind::PLAIN:
                    break;
                default:
                    break;
            }
            ++inherited;
        }
        if (brokenByParent) {
            cd->EnableAttr(Attribute::IS_BROKEN);
            installing.erase(cd);
            return InstallResult::BROKEN;
        }
        auto installed = InstallWrapperMembers(*cd, wrapperParent, diag, sourceManager, *imports);
        installing.erase(cd);
        return installed ? InstallResult::WRAPPER : InstallResult::BROKEN;
    };
    for (auto cd : survivors) {
        (void)install(cd);
    }
}
} // namespace Cangjie::Native::FFI::ArkTS
