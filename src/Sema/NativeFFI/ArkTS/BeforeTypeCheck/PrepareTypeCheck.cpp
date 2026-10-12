// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "../ArkTSInterop.h"
#include "../Utils/ASTFactory.h"
#include "../Utils/EntrySelection.h"
#include "cangjie/AST/Match.h"
#include "cangjie/Mangle/ASTMangler.h"
#include <unordered_map>

using namespace Cangjie;
using namespace Cangjie::AST;
namespace Cangjie::Native::FFI::ArkTS {
namespace {
using GeneratedDeclarations = std::unordered_map<std::string, Ptr<Decl>>;

GeneratedDeclarations CollectGeneratedDeclarations(const File& file)
{
    GeneratedDeclarations declarations;
    for (auto& decl : file.decls) {
        if (decl->TestAttr(Attribute::COMPILER_ADD)) {
            declarations.emplace(decl->identifier.Val(), decl.get());
        }
    }
    return declarations;
}

bool IsGeneratedForEntry(const Decl& decl, const FuncDecl& entry)
{
    return decl.TestAttr(Attribute::COMPILER_ADD) && decl.curFile == entry.curFile && decl.begin == entry.begin &&
        decl.end == entry.end;
}

bool CallsSourceEntry(const FuncDecl& wrapper, const FuncDecl& entry)
{
    if (!wrapper.funcBody || !wrapper.funcBody->body || wrapper.funcBody->body->body.empty()) {
        return false;
    }
    auto call = DynamicCast<const CallExpr*>(wrapper.funcBody->body->body.front().get());
    auto ref = call ? DynamicCast<const RefExpr*>(call->baseFunc.get()) : nullptr;
    return ref && (!ref->ref.target || ref->ref.target == &entry) && ref->ref.identifier == entry.identifier &&
        ref->ref.identifier.Begin() == entry.identifier.Begin() && ref->ref.identifier.End() == entry.identifier.End();
}

bool RegistersWrapper(const VarDecl& registration, const FuncDecl& wrapper, const FuncDecl& entry)
{
    auto call = DynamicCast<const CallExpr*>(registration.initializer.get());
    if (!call || call->args.size() != 2) {
        return false;
    }
    auto name = DynamicCast<const LitConstExpr*>(call->args[0]->expr.get());
    auto ref = DynamicCast<const RefExpr*>(call->args[1]->expr.get());
    return name && name->kind == LitConstKind::STRING && name->stringValue == entry.identifier.Val() && ref &&
        ref->ref.identifier == wrapper.identifier && (!ref->ref.target || ref->ref.target == &wrapper);
}

// Pseudocode: find the callback and registration already attached to this File;
// require compiler provenance, the source entry's range and original call identifier,
// then verify that the exported-name literal registers that callback. A user declaration
// with the same reserved name cannot act as evidence of completed generation.
bool HasGeneratedEntry(const GeneratedDeclarations& declarations, const FuncDecl& entry, const std::string& suffix,
    const std::string& prefix)
{
    auto wrapperIt = declarations.find(prefix + "entry_" + suffix);
    auto registrationIt = declarations.find(prefix + "registration_" + suffix);
    if (wrapperIt == declarations.end() || registrationIt == declarations.end() ||
        !IsGeneratedForEntry(*wrapperIt->second, entry) || !IsGeneratedForEntry(*registrationIt->second, entry)) {
        return false;
    }
    auto wrapper = DynamicCast<const FuncDecl*>(wrapperIt->second);
    auto registration = DynamicCast<const VarDecl*>(registrationIt->second);
    return wrapper && registration && CallsSourceEntry(*wrapper, entry) &&
        RegistersWrapper(*registration, *wrapper, entry);
}
} // namespace

// Pseudocode: index compiler-generated root declarations once for each File;
// enumerate annotated source functions using a stable package-wide index;
// reuse a verified generated pair when this unchanged Package is prepared again.
// Otherwise construct its callback and registration using the source's explicit SDK imports.
// Buffer new declarations until the source walk finishes, then append them to their
// permanent File before symbol-table construction and type checking. Source annotations
// and imports remain intact; generation state is derived from the owned AST, not a global cache.
// For each actual incremental change, give every newly generated root an AST mangled identity
// and mark it for recompilation. The cached-type loader runs before this phase and removes
// previous generated roots absent from the newly parsed source map; the current callback and
// registration are then emitted again. An unchanged incremental invocation skips the semantic
// stage, so it reuses its products without generating or removing declarations.
void PrepareTypeCheck(Package& pkg, DiagnosticEngine& diag, SourceManager& sourceManager)
{
    ASTFactory factory(diag, sourceManager);
    ASTMangler mangler(pkg.fullPackageName);
    const std::string prefix = "__cj_arkts_";
    std::vector<OwnedPtr<Decl>> generated;
    size_t index = 0;
    for (auto& file : pkg.files) {
        const auto prepared = CollectGeneratedDeclarations(*file);
        for (auto& decl : file->decls) {
            auto fd = DynamicCast<FuncDecl*>(decl.get());
            if (fd && IsArkTSEntry(*fd)) {
                const auto suffix = std::to_string(index++);
                if (HasGeneratedEntry(prepared, *fd, suffix, prefix)) {
                    continue;
                }
                for (auto& item : factory.CreateEntry(*fd, suffix, prefix)) {
                    item->rawMangleName = mangler.Mangle(*item);
                    item->toBeCompiled = true;
                    generated.emplace_back(std::move(item));
                }
            }
        }
    }
    for (auto& decl : generated) {
        decl->curFile->decls.emplace_back(std::move(decl));
    }
}
} // namespace Cangjie::Native::FFI::ArkTS
