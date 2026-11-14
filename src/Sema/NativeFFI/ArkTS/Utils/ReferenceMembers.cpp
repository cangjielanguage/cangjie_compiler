// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "ReferenceMembers.h"

#include "../../ObjC/Utils/ASTInserter.h"
#include "../FunctionSource.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Modules/ImportManager.h"
#include "cangjie/Parse/Parser.h"

using namespace Cangjie;
using namespace Cangjie::AST;
using namespace Cangjie::Interop::ObjC;

namespace Cangjie::Native::FFI::ArkTS {
namespace {
constexpr const char* ARKTS_INTEROP_FULL_PACKAGE = "ohos.ark_interop";
} // namespace

Ptr<Decl> FindArkTSJSObject(ImportManager& imports, const File& file)
{
    for (auto decl : imports.GetImportedDeclsByName(file, ARKTS_JS_OBJECT)) {
        if (decl->fullPackageName == ARKTS_INTEROP_FULL_PACKAGE) {
            return decl;
        }
    }
    return nullptr;
}

OwnedPtr<Decl> ParseArkTSReferenceHolder(const std::string& code, const Decl& anchor, DiagnosticEngine& diag,
    SourceManager& sourceManager, ImportManager& imports)
{
    const auto errorsBefore = diag.GetErrorCount();
    Parser parser(code, diag, sourceManager, anchor.begin);
    parser.SetCurFile(anchor.curFile);
    auto holder = parser.ParseTopLevel();
    if (!holder || diag.GetErrorCount() != errorsBefore || holder->decls.empty()) {
        return nullptr;
    }
    auto jsObject = FindArkTSJSObject(imports, *anchor.curFile);
    if (jsObject == nullptr) {
        return nullptr;
    }
    auto decl = std::move(holder->decls.front());
    Walker(decl.get(), [&jsObject](Ptr<Node> node) {
        auto type = DynamicCast<RefType*>(node);
        if (type && type->ref.identifier == ARKTS_JS_OBJECT && !type->ref.target) {
            type->ref.target = jsObject;
            type->SetTy(jsObject->GetTy());
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    return decl;
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
bool InsertArkTSReferenceMembers(
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
    auto holder = ParseArkTSReferenceHolder(code, cd, diag, sourceManager, imports);
    auto holderClass = holder ? DynamicCast<ClassDecl*>(holder.get()) : nullptr;
    if (!holderClass || !holderClass->body) {
        return false;
    }
    std::vector<OwnedPtr<Decl>> members;
    for (auto& member : holderClass->body->decls) {
        member->EnableAttr(Attribute::COMPILER_ADD);
        members.emplace_back(std::move(member));
    }
    ASTInserter inserter;
    for (auto& member : members) {
        inserter.InsertInto(cd, std::move(member), InsertMode::RECURSIVE);
    }
    return true;
}
} // namespace Cangjie::Native::FFI::ArkTS
