// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "../ArkTSInterop.h"

#include "../../ObjC/Utils/ASTInserter.h"
#include "../FunctionSource.h"
#include "cangjie/AST/Clone.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Modules/ImportManager.h"
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

// Before: the class body carries no generated storage. After result: one
// private field, one public wrapping constructor and one public accessor,
// parsed from the compiler-owned holder shape and reparented onto the user
// class. Source positions and the permanent file are the class's; the holder
// itself is discarded.
//   class C { ... }
//     -> class C { ...; private var __cj_arkts_ref: JSObject
//                  public init(__cj_arkts_ref: JSObject, _: Unit) { ... }
//                  public func __cj_arkts_ref_value(): JSObject { ... } }
bool InsertReferenceMembers(ClassDecl& cd, DiagnosticEngine& diag, SourceManager& sourceManager, ImportManager& imports)
{
    std::string code = std::string("class ") + ARKTS_SUPPORT_HOLDER + " {\n    private var " + ARKTS_CLASS_REF_FIELD +
        ": " + ARKTS_JS_OBJECT + "\n    public init(" + ARKTS_CLASS_CTOR_PARAM + ": " + ARKTS_JS_OBJECT + ", " +
        "_: Unit) {\n        this." + ARKTS_CLASS_REF_FIELD + " = " + ARKTS_CLASS_CTOR_PARAM +
        "\n    }\n    public func " + ARKTS_CLASS_REF_ACCESSOR + "(): " + ARKTS_JS_OBJECT +
        " {\n        return this." + ARKTS_CLASS_REF_FIELD + "\n    }\n}\n";
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

// Before: an unprepared @ArkTS foreign class. After result: the class is an
// ordinary Cangjie class marked ARKTS_CLASS_WRAPPER with its reference members
// declared; FOREIGN is removed so member bodies can be attached by lowering.
// A member spelling a compiler-owned name is refused and the class is broken.
void PrepareClass(ClassDecl& cd, DiagnosticEngine& diag, SourceManager& sourceManager, ImportManager& imports)
{
    for (auto& member : cd.GetMemberDecls()) {
        if (member->identifier.Val().rfind(ARKTS_RESERVED_PREFIX, 0) == 0) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_class_invalid, cd, cd.identifier.Val(),
                "member name '" + member->identifier.Val() + "' is reserved for generated code");
            cd.EnableAttr(Attribute::IS_BROKEN);
            return;
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
                return;
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
            return;
        }
        if (member->astKind == ASTKind::VAR_DECL || member->astKind == ASTKind::PRIMARY_CTOR_DECL) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_class_invalid, cd, cd.identifier.Val(),
                "member variables and primary constructors are not supported");
            cd.EnableAttr(Attribute::IS_BROKEN);
            return;
        }
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
                return;
            }
        }
        if (!fd) {
            continue;
        }
        if (fd->IsFinalizer()) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_class_invalid, cd, cd.identifier.Val(),
                "a finalizer is not supported");
            cd.EnableAttr(Attribute::IS_BROKEN);
            return;
        }
        if (fd->funcBody && fd->funcBody->body) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_class_invalid, cd, cd.identifier.Val(),
                "member '" + fd->identifier.Val() + "' must not have a body");
            cd.EnableAttr(Attribute::IS_BROKEN);
            return;
        }
    }
    auto jsObject = FindImportedJSObject(imports, *cd.curFile);
    if (jsObject == nullptr) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_class_invalid, cd, cd.identifier.Val(),
            "the wrapper needs the imported declaration 'ohos.ark_interop." + std::string(ARKTS_JS_OBJECT) + "'");
        cd.EnableAttr(Attribute::IS_BROKEN);
        return;
    }
    if (!InsertReferenceMembers(cd, diag, sourceManager, imports)) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_class_invalid, cd, cd.identifier.Val(),
            "cannot declare the wrapper reference members");
        cd.EnableAttr(Attribute::IS_BROKEN);
        return;
    }
    for (auto& member : cd.GetMemberDecls()) {
        member->DisableAttr(Attribute::ABSTRACT);
        // A property declared without accessors gets accessor signatures with
        // the property type cloned onto them; lowering fills their bodies.
        auto prop = DynamicCast<PropDecl*>(member.get());
        if (!prop || prop->TestAttr(Attribute::COMPILER_ADD)) {
            continue;
        }
        if (prop->getters.empty()) {
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
    cd.DisableAttr(Attribute::FOREIGN);
    Utils::EraseIf(cd.modifiers, [](auto& mod) { return mod.modifier == TokenKind::FOREIGN; });
    cd.EnableAttr(Attribute::ARKTS_CLASS_WRAPPER);
}
} // namespace

// Walk every source file and prepare each unprepared non-generic foreign
// class; member bodies stay empty until lowering fills them. Generic classes
// and the stdlib presets keep their external declaration form.
void PrepareClasses(Package& pkg, DiagnosticEngine& diag, SourceManager& sourceManager, ImportManager* imports)
{
    if (imports == nullptr) {
        return;
    }
    for (auto& file : pkg.files) {
        for (auto& decl : file->decls) {
            auto cd = DynamicCast<ClassDecl*>(decl.get());
            if (!cd || !IsArkTSForeignClass(*cd)) {
                continue;
            }
            PrepareClass(*cd, diag, sourceManager, *imports);
        }
    }
}
} // namespace Cangjie::Native::FFI::ArkTS
