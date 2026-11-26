// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "../ArkTSInterop.h"

#include "../Utils/ASTFactory.h"
#include "../Utils/EntrySelection.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Mangle/ASTMangler.h"
#include "cangjie/Utils/Utils.h"

#include <unordered_map>

using namespace Cangjie;
using namespace Cangjie::AST;
namespace Cangjie::Native::FFI::ArkTS {
namespace {
using GeneratedDeclarations = std::unordered_map<std::string, Ptr<Decl>>;

// Before: File.decls mixes source roots and generated callback/registration roots.
// After result: a name -> existing COMPILER_ADD declaration index. This is a
// read-only view; ownership and File.decls are unchanged.
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

// Whether the file already binds both interop package names. The compiler
// injects these imports when the packages are resolvable; a star import binds
// members, not the package name, so it does not count. An entry callback
// cannot be generated without them: its generated signature would otherwise
// reach PreCheck as undeclared type names.
bool FileBindsInteropPackages(const File& file)
{
    bool interop = false;
    bool contextAbi = false;
    for (auto& import : file.imports) {
        if (import == nullptr) {
            continue;
        }
        const auto& content = import->content;
        if (content.kind == ImportKind::IMPORT_ALL || content.kind == ImportKind::IMPORT_MULTI ||
            !content.aliasName.Empty()) {
            continue;
        }
        const auto packageName = content.GetImportedPackageName();
        interop = interop || packageName == ARKTS_INTEROP_FULL_PACKAGE;
        contextAbi = contextAbi || packageName == ARKTS_CONTEXT_ABI_FULL_PACKAGE;
    }
    return interop && contextAbi;
}

// Before: a candidate generated root and a source entry. After result: whether
// COMPILER_ADD, curFile and source range match; neither AST is modified.
bool IsGeneratedForEntry(const Decl& decl, const FuncDecl& entry)
{
    return decl.TestAttr(Attribute::COMPILER_ADD) && decl.curFile == entry.curFile && decl.begin == entry.begin &&
        decl.end == entry.end;
}

// The callee name of a call. The generated context ABI calls are
// package-qualified (cj_arkts_context_v1.__cj_arkts_enter), which parses as a
// member access; a plain reference is accepted too so the recognition does not
// depend on the qualification spelling.
const Identifier* CalleeName(const Expr* callee)
{
    if (auto ref = DynamicCast<const RefExpr*>(callee)) {
        return &ref->ref.identifier;
    }
    if (auto access = DynamicCast<const MemberAccess*>(callee)) {
        return &access->field;
    }
    return nullptr;
}

// Before: a callback FuncDecl candidate. After result: whether its AST has
//   [let previous = enter(ctx), try { entry(); return ... } finally { restore(previous) }]
// and the expected parameter/source-reference identities. This only recognizes
// the generated shape; it does not insert, replace or repair callback nodes.
// The callback consists of a saved context and a try/finally expression.
// The try block calls the source entry and returns undefined; finally restores
// the saved context. Its parameters are the current context and argument list.
bool CallsSourceEntry(const FuncDecl& wrapper, const FuncDecl& entry)
{
    constexpr size_t callbackStatements = 2;
    constexpr size_t guardedStatements = 2;
    constexpr size_t callbackParameters = 2;
    if (!wrapper.funcBody || !wrapper.funcBody->body || wrapper.funcBody->body->body.size() != callbackStatements) {
        return false;
    }
    const auto& body = wrapper.funcBody->body->body;
    auto previous = DynamicCast<const VarDecl*>(body[0].get());
    auto guarded = DynamicCast<const TryExpr*>(body[1].get());
    if (!previous || !guarded || !guarded->tryBlock || !guarded->finallyBlock ||
        guarded->tryBlock->body.size() != guardedStatements || guarded->finallyBlock->body.size() != 1 ||
        !guarded->catchBlocks.empty() || wrapper.funcBody->paramLists.size() != 1 ||
        wrapper.funcBody->paramLists.front()->params.size() != callbackParameters) {
        return false;
    }
    auto enter = DynamicCast<const CallExpr*>(previous->initializer.get());
    auto enterName = enter ? CalleeName(enter->baseFunc.get()) : nullptr;
    auto context = enter && enter->args.size() == 1 ?
        DynamicCast<const RefExpr*>(enter->args.front()->expr.get()) : nullptr;
    auto contextParam = wrapper.funcBody->paramLists.front()->params.front().get();
    auto call = DynamicCast<const CallExpr*>(guarded->tryBlock->body.front().get());
    auto ref = call ? DynamicCast<const RefExpr*>(call->baseFunc.get()) : nullptr;
    auto restored = DynamicCast<const CallExpr*>(guarded->finallyBlock->body.front().get());
    auto restoreName = restored ? CalleeName(restored->baseFunc.get()) : nullptr;
    auto restoredValue = restored && restored->args.size() == 1 ?
        DynamicCast<const RefExpr*>(restored->args.front()->expr.get()) : nullptr;
    // The scope guard may be imported under the aliased spellings
    // __cj_arkts_enter_scope / __cj_arkts_restore_scope instead of the bare
    // SDK names, so accept both when recognising a reusable wrapper pair.
    return enterName && (*enterName == "__cj_arkts_enter" || *enterName == "__cj_arkts_enter_scope") && context &&
        context->ref.identifier == contextParam->identifier &&
        (!context->ref.target || context->ref.target == contextParam) &&
        ref && (!ref->ref.target || ref->ref.target == &entry) && ref->ref.identifier == entry.identifier &&
        ref->ref.identifier.Begin() == entry.identifier.Begin() &&
        ref->ref.identifier.End() == entry.identifier.End() &&
        guarded->tryBlock->body.back()->astKind == ASTKind::RETURN_EXPR &&
        restoreName && (*restoreName == "__cj_arkts_restore" || *restoreName == "__cj_arkts_restore_scope") &&
        restoredValue &&
        restoredValue->ref.identifier == previous->identifier &&
        (!restoredValue->ref.target || restoredValue->ref.target == previous);
}

// Before: a candidate VarDecl initializer. After result: whether its CallExpr
// arguments are [StringLiteral(entry.name), RefExpr(wrapper)]. This read-only
// check complements callback provenance; it does not generate a registration.
bool RegistersWrapper(const VarDecl& registration, const FuncDecl& wrapper, const FuncDecl& entry)
{
    auto call = DynamicCast<const CallExpr*>(registration.initializer.get());
    // The registration call is registerFunc(name, wrapper): exactly two arguments.
    constexpr size_t registrationArguments = 2;
    if (!call || call->args.size() != registrationArguments) {
        return false;
    }
    auto name = DynamicCast<const LitConstExpr*>(call->args[0]->expr.get());
    auto ref = DynamicCast<const RefExpr*>(call->args[1]->expr.get());
    return name && name->kind == LitConstKind::STRING && name->stringValue == entry.identifier.Val() && ref &&
        ref->ref.identifier == wrapper.identifier && (!ref->ref.target || ref->ref.target == &wrapper);
}

// Before: indexed generated roots and a source entry. After result: true only
// for a matching callback/registration pair; the declarations remain unchanged.
// A false result makes PrepareTypeCheck generate a new pair, rather than treating
// a matching reserved spelling alone as proof that generation already happened.
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

// Classify foreign declarations before the symbol table sees their ABI: mark
// each surviving @ArkTS foreign function as its wrapper and drop the external
// linkage markers. A declaration already refused by a declaration rule must
// not get a wrapper, or it would emit a second round of diagnostics.
void MarkForeignFunctionWrappers(Package& pkg)
{
    for (auto& file : pkg.files) {
        for (auto& decl : file->decls) {
            auto fd = DynamicCast<FuncDecl*>(decl.get());
            if (!fd || !IsArkTSForeignFunction(*fd) || fd->TestAttr(Attribute::IS_BROKEN)) {
                continue;
            }
            fd->EnableAttr(Attribute::ARKTS_FUNCTION_WRAPPER);
            fd->DisableAttr(Attribute::C, Attribute::NO_MANGLE);
            fd->funcBody->DisableAttr(Attribute::C);
            Utils::EraseIf(fd->modifiers, [](auto& mod) { return mod.modifier == TokenKind::FOREIGN; });
        }
    }
}

// Generate the callback and registration pair of one exported source entry.
// Generating the callback first and failing later in BindSupportReferences
// would leave PreCheck to report the generated signature's package-qualified
// types as undeclared names, so the missing interop packages are detected up
// front and nothing is generated.
void GenerateEntryForDecl(File& file, FuncDecl& fd, ImportManager* importManager, DiagnosticEngine& diag,
    ASTFactory& factory, ASTMangler& mangler, const std::string& prefix, size_t& index,
    const GeneratedDeclarations& prepared, std::vector<OwnedPtr<Decl>>& generated)
{
    if (fd.TestAttr(Attribute::IS_BROKEN)) {
        return;
    }
    const auto suffix = std::to_string(index++);
    if (importManager != nullptr && !FileBindsInteropPackages(file)) {
        auto builder =
            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_support_unavailable, fd, fd.identifier.Val());
        builder.AddNote("the entry registration requires 'ohos.ark_interop' and 'cj_arkts_context_v1' imports");
        return;
    }
    if (HasGeneratedEntry(prepared, fd, suffix, prefix)) {
        return;
    }
    for (auto& item : factory.CreateEntry(fd, suffix, prefix)) {
        item->rawMangleName = mangler.Mangle(*item);
        item->toBeCompiled = true;
        generated.emplace_back(std::move(item));
    }
}

// Reuse a verified existing callback/registration pair, otherwise generate one
// per exported source entry and append all of them to their permanent files.
void GenerateEntryDeclarations(
    Package& pkg, DiagnosticEngine& diag, SourceManager& sourceManager, ImportManager* importManager)
{
    ASTFactory factory(diag, sourceManager);
    ASTMangler mangler(pkg.fullPackageName);
    const std::string prefix = ARKTS_GENERATED_PREFIX;
    std::vector<OwnedPtr<Decl>> generated;
    size_t index = 0;
    for (auto& file : pkg.files) {
        const auto prepared = CollectGeneratedDeclarations(*file);
        for (auto& decl : file->decls) {
            auto fd = DynamicCast<FuncDecl*>(decl.get());
            if (fd && IsArkTSEntry(*fd) && fd->TestAttr(Attribute::PUBLIC)) {
                GenerateEntryForDecl(
                    *file, *fd, importManager, diag, factory, mangler, prefix, index, prepared, generated);
            }
        }
    }
    for (auto& decl : generated) {
        decl->curFile->decls.emplace_back(std::move(decl));
    }
}

} // namespace

// Before -> after, for each source declaration:
//   @ArkTS foreign func echo(...): T, with FOREIGN and an empty body slot
//     -> same FuncDecl marked ARKTS_FUNCTION_WRAPPER; the C/NO_MANGLE flags and
//        the FOREIGN modifier are removed (also C on FuncBody) while FOREIGN
//        itself stays as the declaration's source identity. Every check that
//        used FOREIGN to mean "external" reads IsExternalForeign instead. Body
//        stays empty until LowerArkTSFunctions resolves its semantic
//        parameter/return types.
//   @ArkTS public func run(): Unit { ... }
//     -> unchanged run plus a generated callback and registration VarDecl in
//        its File.decls (CreateEntry documents their complete source/AST shape).
// New roots receive rawMangleName and toBeCompiled=true. A verified existing
// pair is reused; imports and original entry bodies are never inserted/replaced.
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
void PrepareTypeCheck(Package& pkg, DiagnosticEngine& diag, SourceManager& sourceManager,
    ImportManager* importManager)
{
    if (!PassEnabled(pkg)) {
        // The checker reports the gate once per package before this call; the
        // prepare pass itself must not run on a refused source.
        return;
    }
    // Validate '@HasThis' placement and its 'globalVar: true' conflict on the
    // source AST, before preparation rewrites the declaration shape. A valid
    // contract is recorded as ARKTS_HAS_THIS and checked again after the
    // argument's type is resolved.
    DiagnoseArkTSHasThis(pkg, diag);
    // Validate @WriteOnly placement on the source AST, before preparation
    // rewrites member shapes and installs generated accessors.
    DiagnoseArkTSWriteOnlyPlacement(pkg, diag);
    // Foreign classes are classified before any function work: they become
    // ordinary classes owning a reference to an ArkTS object, so the symbol
    // table and the C ABI checks below see the generated member declarations.
    PrepareClasses(pkg, diag, sourceManager, importManager);
    // Interfaces follow the class pass: a generated implementation class may
    // reference a prepared foreign class in member signatures, and both must be
    // visible to the symbol table below.
    PrepareInterfaces(pkg, diag, sourceManager, importManager);
    // Classify foreign declarations before the symbol table sees their ABI;
    // their bodies are generated after semantic alias types are resolved.
    MarkForeignFunctionWrappers(pkg);
    // Validate declaration contracts before any entry is generated: a rejected
    // entry must not reach CreateEntry and produce derived diagnostics.
    DiagnoseArkTSDeclarations(pkg, diag);
    GenerateEntryDeclarations(pkg, diag, sourceManager, importManager);
}
} // namespace Cangjie::Native::FFI::ArkTS
