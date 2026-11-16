// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "../ArkTSInterop.h"

#include "../../ObjC/Utils/ASTInserter.h"
#include "../../Utils.h"
#include "../FunctionSource.h"
#include "../Utils/ReferenceMembers.h"
#include "cangjie/AST/Clone.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Modules/ImportManager.h"

using namespace Cangjie;
using namespace Cangjie::AST;
using namespace Cangjie::Interop::ObjC;

namespace Cangjie::Native::FFI::ArkTS {
namespace {

// One interface-shape refusal: the reason carries the rule and the interface
// is broken so lowering never emits a partial implementation.
void DiagnoseInterface(DiagnosticEngine& diag, InterfaceDecl& id, const std::string& reason)
{
    diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_interface_invalid, id, id.identifier.Val(), reason);
    id.EnableAttr(Attribute::IS_BROKEN);
}

void CloneVisibilityAttribute(const Decl& from, Decl& to)
{
    if (from.TestAttr(Attribute::PUBLIC)) {
        to.EnableAttr(Attribute::PUBLIC);
    } else if (from.TestAttr(Attribute::INTERNAL)) {
        to.EnableAttr(Attribute::INTERNAL);
    } else if (from.TestAttr(Attribute::PROTECTED)) {
        to.EnableAttr(Attribute::PROTECTED);
    } else if (from.TestAttr(Attribute::PRIVATE)) {
        to.EnableAttr(Attribute::PRIVATE);
    }
}

// Declaration constraints of an external interface. Parameters and members keep their
// declaration order; the first refusal marks the interface broken.
bool ValidateInterface(InterfaceDecl& id, DiagnosticEngine& diag)
{
    if (id.TestAttr(Attribute::SEALED)) {
        DiagnoseInterface(diag, id, "a sealed interface is not supported");
        return false;
    }
    for (auto& member : id.GetMemberDecls()) {
        if (member->identifier.Val().rfind(ARKTS_RESERVED_PREFIX, 0) == 0) {
            DiagnoseInterface(
                diag, id, "member name '" + member->identifier.Val() + "' is reserved for generated code");
            return false;
        }
        if (member->TestAttr(Attribute::STATIC)) {
            DiagnoseInterface(diag, id, "static member '" + member->identifier.Val() + "' is not supported");
            return false;
        }
        if (auto fd = DynamicCast<FuncDecl*>(member.get())) {
            if (fd->TestAttr(Attribute::MUT)) {
                DiagnoseInterface(diag, id, "mut function '" + fd->identifier.Val() + "' is not supported");
                return false;
            }
            if (fd->TestAttr(Attribute::OPERATOR) && fd->identifier.Val() != "[]" && fd->identifier.Val() != "()") {
                DiagnoseInterface(diag, id,
                    "operator '" + fd->identifier.Val() + "' is not supported; only index and call operators map");
                return false;
            }
            if (fd->funcBody && fd->funcBody->body) {
                DiagnoseInterface(diag, id, "member '" + fd->identifier.Val() + "' must not have a body");
                return false;
            }
            if (!fd->funcBody) {
                DiagnoseInterface(diag, id, "member '" + fd->identifier.Val() + "' has no signature");
                return false;
            }
            for (auto& list : fd->funcBody->paramLists) {
                for (auto& param : list->params) {
                    if (param->identifier.Val().rfind(ARKTS_RESERVED_PREFIX, 0) != 0) {
                        continue;
                    }
                    DiagnoseInterface(diag, id,
                        "parameter name '" + param->identifier.Val() + "' is reserved for generated code");
                    return false;
                }
            }
            continue;
        }
        if (auto pd = DynamicCast<PropDecl*>(member.get())) {
            if (!pd->getters.empty() || !pd->setters.empty()) {
                DiagnoseInterface(diag, id,
                    "property '" + pd->identifier.Val() + "' must not declare accessors");
                return false;
            }
            // @WriteOnly belongs to the class wrapper path; on an interface it
            // would otherwise be silently accepted as a read-write property
            // (the AR-09 set-only item is outside this requirement), so the
            // declared limitation is refused explicitly.
            if (ArkTSWriteOnlyProp(*pd)) {
                DiagnoseInterface(diag, id,
                    "@WriteOnly properties are not supported on a foreign interface");
                return false;
            }
            continue;
        }
        DiagnoseInterface(diag, id, "only instance properties and functions are supported");
        return false;
    }
    return true;
}

// Add the compiler-owned abstract accessor that every generated implementor
// provides. It is the only way a default member body can recover the stored
// ArkTS object from the interface-typed receiver.
bool InsertReferenceAccessor(
    InterfaceDecl& id, DiagnosticEngine& diag, SourceManager& sourceManager, ImportManager& imports)
{
    std::string code = std::string("interface ") + ARKTS_SUPPORT_HOLDER + " {\n    func " + ARKTS_CLASS_REF_ACCESSOR +
        "(): " + ARKTS_JS_OBJECT + "\n}\n";
    auto holder = ParseArkTSReferenceHolder(code, id, diag, sourceManager, imports);
    auto holderInterface = holder ? DynamicCast<InterfaceDecl*>(holder.get()) : nullptr;
    if (!holderInterface || !holderInterface->body || holderInterface->body->decls.empty()) {
        return false;
    }
    auto accessor = std::move(holderInterface->body->decls.front());
    accessor->EnableAttr(Attribute::COMPILER_ADD);
    ASTInserter inserter;
    inserter.InsertInto(id, std::move(accessor), InsertMode::RECURSIVE);
    return true;
}

// The package a declaration belongs to. The Collector fills the declaration's
// own fullPackageName after preparation, so preparation reads the file's
// package first; the declaration field is only a fallback for detached nodes.
std::string PackageNameOf(const Decl& decl)
{
    if (decl.curFile && decl.curFile->curPackage) {
        return decl.curFile->curPackage->fullPackageName;
    }
    return decl.fullPackageName;
}

// Generated implementation class: one public type per prepared interface,
// named `<Interface>$arkts_impl`. It owns the JSObject reference and the
// accessor body; interface members stay default implementations on the
// interface and never need to be cloned here.
OwnedPtr<ClassDecl> CreateInterfaceImpl(InterfaceDecl& id)
{
    auto impl = MakeOwned<ClassDecl>();
    impl->identifier = SrcIdentifier(ArkTSInterfaceImplName(id));
    impl->identifier.SetPos(id.identifier.Begin(), id.identifier.End());
    impl->begin = id.begin;
    impl->end = id.end;
    impl->curFile = id.curFile;
    // The Java mirror clone takes the package from the file, not the decl.
    const auto packageName = PackageNameOf(id);
    impl->fullPackageName = packageName;
    impl->moduleName = Cangjie::Utils::GetRootPackageName(packageName);
    CloneVisibilityAttribute(id, *impl);
    impl->EnableAttr(Attribute::COMPILER_ADD, Attribute::ARKTS_INTERFACE_WRAPPER, Attribute::NO_REFLECT_INFO);
    impl->body = WithinFile(MakeOwned<ClassBody>(), id.curFile);
    impl->body->begin = id.begin;
    impl->body->end = id.end;
    auto parent = CreateRefType(id);
    parent->begin = id.begin;
    parent->end = id.end;
    impl->inheritedTypes.emplace_back(std::move(parent));
    return impl;
}

void PrepareInterface(
    InterfaceDecl& id, DiagnosticEngine& diag, SourceManager& sourceManager, ImportManager& imports, File& file)
{
    if (!ValidateInterface(id, diag)) {
        return;
    }
    std::unordered_map<std::string, std::string> options;
    ArkTSOptionStatus optionStatus;
    if (!ArkTSDeclOptions(id, options, &optionStatus)) {
        DiagnoseArkTSOptionProblem(diag, id, options, optionStatus);
        id.EnableAttr(Attribute::IS_BROKEN);
        return;
    }
    std::string module;
    if (!ResolveArkTSModule(id, options, *file.curPackage, diag,
            DiagKindRefactor::sema_arkts_foreign_interface_invalid, module)) {
        id.EnableAttr(Attribute::IS_BROKEN);
        return;
    }
    if (FindArkTSJSObject(imports, *id.curFile) == nullptr) {
        DiagnoseInterface(diag, id,
            "the wrapper needs the imported declaration 'ohos.ark_interop." + std::string(ARKTS_JS_OBJECT) + "'");
        return;
    }
    if (!InsertReferenceAccessor(id, diag, sourceManager, imports)) {
        DiagnoseInterface(diag, id, "cannot declare the wrapper reference accessor");
        return;
    }
    // FOREIGN stays as the declaration's source identity; the marker records
    // the prepared state. The FFI overload rule exempts @ArkTS declarations by
    // annotation rather than by an absent FOREIGN, so the index get/set pair
    // remains legal. Members never carry FOREIGN: preparation never stamps
    // them and DeclAttributeChecker skips the owner->member propagation for a
    // prepared owner.
    Utils::EraseIf(id.modifiers, [](auto& mod) { return mod.modifier == TokenKind::FOREIGN; });
    for (auto& member : id.GetMemberDecls()) {
        if (auto fd = DynamicCast<FuncDecl*>(member.get()); fd && !fd->TestAttr(Attribute::CONSTRUCTOR)) {
            fd->EnableAttr(Attribute::ABSTRACT);
            continue;
        }
        if (auto pd = DynamicCast<PropDecl*>(member.get())) {
            // The accessors added below are the implementation; the property
            // itself is therefore not abstract. Leaving ABSTRACT set would
            // make the standard interface-property pass add a second,
            // bodyless accessor pair.
            pd->DisableAttr(Attribute::ABSTRACT);
            // A property declared without accessors gets default-implementation
            // signatures; lowering fills them. Interface accessors are public.
            if (pd->getters.empty()) {
                InsertPropGetterSignature(*pd, Attribute::ARKTS_INTERFACE_WRAPPER);
            }
            if (pd->isVar && pd->setters.empty()) {
                InsertPropSetterSignature(*pd, Attribute::ARKTS_INTERFACE_WRAPPER);
            }
            for (auto& getter : pd->getters) {
                if (getter->funcBody && !getter->funcBody->retType && pd->type) {
                    getter->funcBody->retType = ASTCloner::Clone(pd->type.get());
                }
            }
            for (auto& setter : pd->setters) {
                if (!setter->funcBody || setter->funcBody->paramLists.empty() ||
                    setter->funcBody->paramLists.front()->params.empty() || !pd->type) {
                    continue;
                }
                auto& param = setter->funcBody->paramLists.front()->params.front();
                if (!param->type) {
                    param->type = ASTCloner::Clone(pd->type.get());
                }
            }
        }
    }
    auto impl = CreateInterfaceImpl(id);
    if (!InsertArkTSReferenceMembers(*impl, false, diag, sourceManager, imports)) {
        DiagnoseInterface(diag, id, "cannot declare the wrapper reference members");
        return;
    }
    auto implPtr = impl.get();
    ASTInserter inserter;
    inserter.InsertInto(file, std::move(impl));
    // Match the Java/ObjC generated-declaration rule: only an exported impl
    // enters the package decl map; a private interface's impl stays local.
    if (implPtr->IsExportedDecl()) {
        imports.GetCjoManager()->AddGeneratedDeclToDeclMap(*implPtr);
    }
    id.EnableAttr(Attribute::ARKTS_INTERFACE_WRAPPER);
}
} // namespace

// Prepare every unprepared non-generic foreign interface. The declaration is
// validated, gains the compiler-owned accessor and a generated implementation
// class, and carries ARKTS_INTERFACE_WRAPPER as its prepared state before
// symbol-table building. FOREIGN stays on it as the source identity.
void PrepareInterfaces(Package& pkg, DiagnosticEngine& diag, SourceManager& sourceManager, ImportManager* imports)
{
    if (imports == nullptr) {
        return;
    }
    for (auto& file : pkg.files) {
        std::vector<InterfaceDecl*> prepared;
        for (auto& decl : file->decls) {
            auto id = DynamicCast<InterfaceDecl*>(decl.get());
            // A refused interface has no ARKTS_INTERFACE_WRAPPER, so the broken
            // mark and the wrapper marker are the idempotency guards for a
            // repeated pass. FOREIGN alone no longer separates the states.
            if (!id || IsArkTSPreparedInterface(*id) || id->TestAttr(Attribute::IS_BROKEN) ||
                !IsArkTSForeignInterface(*id)) {
                continue;
            }
            prepared.emplace_back(id);
        }
        for (auto id : prepared) {
            PrepareInterface(*id, diag, sourceManager, *imports, *file);
        }
    }
}
} // namespace Cangjie::Native::FFI::ArkTS
