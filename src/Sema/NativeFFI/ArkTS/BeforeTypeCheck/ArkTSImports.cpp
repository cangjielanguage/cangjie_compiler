// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * The @ArkTS interop package imports. They live in the ArkTS layer, not in the
 * generic module layer: the package names and the annotation that selects the
 * capability are ArkTS knowledge, and the generic ImportManager only delegates.
 */

#include <algorithm>
#include <string>
#include <vector>

#include "cangjie/AST/Create.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Basic/SourceManager.h"
#include "cangjie/Lex/Lexer.h"
#include "cangjie/Modules/ImportManager.h"
#include "cangjie/Utils/ProfileRecorder.h"
#include "../FunctionSource.h"

using namespace Cangjie;
using namespace Cangjie::AST;

namespace Cangjie::Native::FFI::ArkTS {
namespace {

// ---- @ArkTS interop package imports ----
// The wrapper generator spells the interop surface with package-qualified
// names (ark_interop.JSContext, cj_arkts_context_v1.__cj_arkts_current), so a
// file that uses the @ArkTS capability needs those package names in scope even
// when the user wrote no package import. The imports are compiler-added,
// private and implicit: the user must not see glue imports (an @ArkTS class or
// an early-rejected signature generates no wrapper, which would otherwise
// surface an unused-import warning).
//
// The generated references are pre-bound by the lowering walker, so the
// injected imports carry no user-visible names: they are marked as implicit
// link dependencies (ImportSpec::implicitDependencyPackage), which keeps them
// out of the per-file imported-name map and lets dependency generation record
// their packages without knowing any ArkTS package name.

// The generated global-variable getter binds BigInt under a compiler-chosen
// alias and calls the `parse` extensions through the std.convert interface;
// the user file need not import either package.
constexpr const char* ARKTS_NUMERIC_FULL_PACKAGE = "std.math.numeric";
constexpr const char* ARKTS_BIGINT_MEMBER = "BigInt";
constexpr const char* ARKTS_BIGINT_ALIAS = "__cj_arkts_BigInt";
constexpr const char* ARKTS_CONVERT_FULL_PACKAGE = "std.convert";
constexpr const char* ARKTS_PARSABLE_MEMBER = "Parsable";

// The compiler-injected @ArkTS imports are implicit so the user never sees
// them in unused-import warnings, but they are still runtime link
// dependencies. Dependency generation normally skips implicit imports; the two
// interop packages and the single-member support imports are the deliberate
// exception.
// Whether @p member from @p fullPackageName is already visible as a short
// name. A star import of the package binds all its members; a single import of
// the member binds it directly. A package import only binds the package name,
// and an alias binds the alias, so neither counts.
// Whether the file binds @p member through a single-member import. A star
// import is deliberately not enough: another star import of a different
// package may export the same name (std.core and the ArkTS facade both spell
// the interop exceptions), so the compiler-injected single-member import must
// still be added to make the generated reference unambiguous.
bool FileBindsMemberDirectly(const File& file, const std::string& fullPackageName, const std::string& member)
{
    const auto memberPackage = fullPackageName + "." + member;
    for (auto& import : file.imports) {
        if (import == nullptr) {
            continue;
        }
        const auto& content = import->content;
        if (content.kind == ImportKind::IMPORT_SINGLE && content.aliasName.Empty() &&
            content.identifier.Val() == member && content.GetImportedPackageName() == memberPackage) {
            return true;
        }
    }
    return false;
}

// Whether the file already binds @p fullPackageName as a package name. A
// star import does not bind the package name (only its members), so it does
// not count; an alias import binds the alias instead.
bool FileBindsPackageName(const File& file, const std::string& fullPackageName)
{
    for (auto& import : file.imports) {
        if (import == nullptr) {
            continue;
        }
        const auto& content = import->content;
        if (content.kind == ImportKind::IMPORT_ALL || content.kind == ImportKind::IMPORT_MULTI ||
            !content.aliasName.Empty()) {
            continue;
        }
        if (content.GetImportedPackageName() == fullPackageName) {
            return true;
        }
    }
    return false;
}

// Whether the file already binds @p alias through an alias import (either a
// whole import or a member of a multi-import). Injection must be idempotent:
// BuildIndex runs again on the incremental/LSP path.
bool FileBindsAlias(const File& file, const std::string& alias)
{
    auto binds = [&alias](const ImportContent& content) {
        return content.aliasName.Val() == alias;
    };
    for (auto& import : file.imports) {
        if (import == nullptr) {
            continue;
        }
        if (binds(import->content)) {
            return true;
        }
        if (std::any_of(import->content.items.begin(), import->content.items.end(), binds)) {
            return true;
        }
    }
    return false;
}

// Whether the imported package can be loaded: its cjo is on the search path,
// or an LSP/unit-test cache already holds it (those caches have no file on
// disk). Checked only after UpdateSearchPath.
bool IsPackageResolvable(const ImportManager& importMgr, const std::string& fullPackageName)
{
    if (!FileUtil::FindSerializationFile(fullPackageName, SERIALIZED_FILE_EXTENSION, importMgr.GetSearchPath())
             .empty()) {
        return true;
    }
    return !importMgr.GetCjoManager()->GetPackageCjoPath(fullPackageName).empty();
}

// In --scan-dependency mode the parser reads only the package header and
// imports, so a file that carries @ArkTS has no annotation AST node. Detect the
// annotation lexically instead; comments, string literals and raw identifiers
// are tokenized the same way the parser sees them. Only called for scan-mode
// files (no parsed declarations).
bool SourceUsesArkTSAnnotation(DiagnosticEngine& diag, SourceManager& sourceManager, const File& file)
{
    if (file.begin.IsZero()) {
        return false;
    }
    auto& source = sourceManager.GetSource(file.begin.fileID);
    if (source.buffer.empty()) {
        return false;
    }
    Lexer lexer(file.begin.fileID, source.buffer, diag, sourceManager);
    bool afterAt = false;
    for (auto token = lexer.Next(); token.kind != TokenKind::END; token = lexer.Next()) {
        if (token.kind == TokenKind::COMMENT) {
            continue;
        }
        if (afterAt && token.kind == TokenKind::IDENTIFIER && token.Value() == "ArkTS") {
            return true;
        }
        afterAt = token.kind == TokenKind::AT;
    }
    return false;
}

// Position a synthesized import at @p at. The real source range keeps the
// dependency json and diagnostics locatable; an IMPLICIT_ADD import is allowed
// a zero position, but pointing at the first @ArkTS annotation is still more
// useful than a default position.
void SetSyntheticImportRange(ImportSpec& import, const Range& at)
{
    import.begin = at.begin;
    import.end = at.end;
    import.importPos = at.begin;
    import.content.begin = at.begin;
    import.content.end = at.end;
    import.content.identifier.SetPos(at.begin, at.end);
    if (!import.content.prefixPaths.empty()) {
        import.content.prefixPoses.assign(import.content.prefixPaths.size(), at.begin);
        import.content.prefixDotPoses.assign(import.content.prefixPaths.size(), at.begin);
    }
}

// Every import this pass synthesizes must stay invisible to user name lookup:
// the generated references are pre-bound by the lowering walker. An import
// that is also a runtime link dependency additionally names the package the
// dependency json has to record (a single-member import would otherwise be
// recorded by its qualified member name).
void MarkCompilerInjected(ImportSpec& import, bool hideNames)
{
    import.isCompilerInjectedImport = true;
    import.hidesFromNameLookup = hideNames;
}

// The three single-member support imports injected into every @ArkTS file;
// they are the member imports that contribute a runtime dependency.
bool IsArkTSSupportMember(const std::string& member)
{
    return member == ARKTS_CONVERSION_EXCEPTION || member == ARKTS_RUNTIME_EXCEPTION ||
        member == ARKTS_BUSINESS_EXCEPTION;
}

// Build `import <fullPackageName>` (package import, not a member import).
// A dotted name splits at the last dot; a top-level package has no prefix
// path, matching the parser's representation of `import name`.
OwnedPtr<ImportSpec> CreateSyntheticPackageImport(const std::string& fullPackageName, const Range& at)
{
    OwnedPtr<ImportSpec> import;
    const auto lastDot = fullPackageName.rfind('.');
    if (lastDot == std::string::npos) {
        import = MakeOwned<ImportSpec>();
        import->content.kind = ImportKind::IMPORT_SINGLE;
        import->content.identifier = Identifier(fullPackageName, at.begin, at.end);
        import->EnableAttr(Attribute::COMPILER_ADD, Attribute::IMPLICIT_ADD, Attribute::PRIVATE);
    } else {
        import = CreateImportSpec(fullPackageName.substr(0, lastDot), fullPackageName.substr(lastDot + 1));
    }
    // COMPILER_ADD, IMPLICIT_ADD and PRIVATE stay: the import is owned by the
    // compiler, hidden from user-facing checks, and not re-exported.
    SetSyntheticImportRange(*import, at);
    return import;
}


namespace {

// First @ArkTS annotation in the file, if the AST carries one. It selects the
// interop capability and gives the synthesized imports a real source range.
Ptr<Annotation> FindFirstArkTSAnnotation(File& file)
{
    Ptr<Annotation> firstAnnotation{nullptr};
    Walker(&file, [&firstAnnotation](Ptr<Node> node) {
        if (firstAnnotation != nullptr) {
            return VisitAction::STOP_NOW;
        }
        auto annotation = DynamicCast<Annotation*>(node);
        if (annotation != nullptr && annotation->kind == AnnotationKind::ARKTS) {
            firstAnnotation = annotation;
            return VisitAction::STOP_NOW;
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    return firstAnnotation;
}

// Whether a file selects the ArkTS interop capability. The scan-dependency
// parser produces imports only, so fall back to the source text so the
// auto-import contract also holds for `--scan-dependency`.
bool FileSelectsArkTSInterop(
    DiagnosticEngine& diag, SourceManager* sourceManager, File& file, Ptr<Annotation>& firstAnnotation)
{
    firstAnnotation = FindFirstArkTSAnnotation(file);
    if (firstAnnotation != nullptr) {
        return true;
    }
    return sourceManager != nullptr && file.decls.empty() &&
        SourceUsesArkTSAnnotation(diag, *sourceManager, file);
}

// Point the synthesized imports at the first @ArkTS annotation, or at the file
// when the annotation has no source positions.
Range SyntheticImportRange(File& file, const Ptr<Annotation>& firstAnnotation)
{
    auto at = firstAnnotation != nullptr ? MakeRange(firstAnnotation->begin, firstAnnotation->end)
                                         : MakeRange(file.begin, file.end);
    if (at.HasZero() && firstAnnotation != nullptr) {
        at = MakeRange(file.begin, file.end);
    }
    return at;
}

// Phase: package import (`import ohos.ark_interop`).
void AddSyntheticPackageImport(
    const ImportManager& importMgr, const Package& pkg, File& file, const Range& at, const std::string& fullPackageName)
{
    // Never import the package being compiled: a self import is rejected by
    // CheckImports.
    bool resolvable = IsPackageResolvable(importMgr, fullPackageName);
    if (pkg.fullPackageName == fullPackageName || FileBindsPackageName(file, fullPackageName) || !resolvable) {
        return;
    }
    auto import = CreateSyntheticPackageImport(fullPackageName, at);
    // The package import stays registered: a generated package qualifier that
    // is re-resolved after a speculative re-check needs the PackageDecl. It is
    // still marked compiler-injected so import-conflict checks skip it.
    MarkCompilerInjected(*import, false);
    import->implicitDependencyPackage = fullPackageName;
    import->curFile = &file;
    import->content.curFile = &file;
    file.imports.emplace_back(std::move(import));
}

// Phase: single-member import (`import arkts.lang.ExternConversionException`).
void AddSyntheticMemberImport(const ImportManager& importMgr, const Package& pkg, File& file, const Range& at,
    const std::string& fullPackageName, const std::string& member)
{
    if (pkg.fullPackageName == fullPackageName || FileBindsMemberDirectly(file, fullPackageName, member) ||
        !IsPackageResolvable(importMgr, fullPackageName)) {
        return;
    }
    auto import = CreateImportSpec(fullPackageName, member);
    SetSyntheticImportRange(*import, at);
    MarkCompilerInjected(*import, true);
    if (IsArkTSSupportMember(member)) {
        import->implicitDependencyPackage = fullPackageName;
    }
    import->curFile = &file;
    import->content.curFile = &file;
    file.imports.emplace_back(std::move(import));
}

// Phase: aliased single-member import (`import x.y.Member as Alias`).
void AddSyntheticMemberImportAs(const ImportManager& importMgr, const Package& pkg, File& file, const Range& at,
    const std::string& fullPackageName, const std::string& member, const std::string& alias)
{
    if (pkg.fullPackageName == fullPackageName || FileBindsAlias(file, alias) ||
        !IsPackageResolvable(importMgr, fullPackageName)) {
        return;
    }
    auto import = CreateImportSpec(fullPackageName, member, alias);
    SetSyntheticImportRange(*import, at);
    MarkCompilerInjected(*import, true);
    import->curFile = &file;
    import->content.curFile = &file;
    file.imports.emplace_back(std::move(import));
}

// Phase: inject the complete interop import set, in the original order.
void InjectArkTSInteropImports(
    const ImportManager& importMgr, const Package& pkg, File& file, const Range& at)
{
    AddSyntheticPackageImport(importMgr, pkg, file, at, ARKTS_INTEROP_FULL_PACKAGE);
    AddSyntheticPackageImport(importMgr, pkg, file, at, ARKTS_CONTEXT_ABI_FULL_PACKAGE);
    // The generated guards always throw or catch these public types; import
    // the single member so the wrapper compiles without asking the user for
    // the package. A star import of the package already makes the name visible.
    AddSyntheticMemberImport(importMgr, pkg, file, at, ARKTS_LANGUAGE_PACKAGE, ARKTS_CONVERSION_EXCEPTION);
    AddSyntheticMemberImport(importMgr, pkg, file, at, ARKTS_LANGUAGE_PACKAGE, ARKTS_RUNTIME_EXCEPTION);
    AddSyntheticMemberImport(importMgr, pkg, file, at, ARKTS_LANGUAGE_PACKAGE, ARKTS_BUSINESS_EXCEPTION);
    // The generated global-variable getter spells BigInt through a
    // compiler-generated alias and reaches the `parse` extensions through the
    // std.convert interface. The references are pre-bound by the lowering
    // walker, so the imports only keep those packages loaded and linked; the
    // aliases stay invisible to user code like the rest of the set. The
    // conversion exception also has its own private alias, because a bare name
    // is ambiguous when std.core also exports it.
    AddSyntheticMemberImportAs(importMgr, pkg, file, at, ARKTS_LANGUAGE_PACKAGE, ARKTS_CONVERSION_EXCEPTION,
        ARKTS_CONVERSION_ALIAS);
    AddSyntheticMemberImportAs(importMgr, pkg, file, at, ARKTS_NUMERIC_FULL_PACKAGE, ARKTS_BIGINT_MEMBER,
        ARKTS_BIGINT_ALIAS);
    AddSyntheticMemberImport(importMgr, pkg, file, at, ARKTS_CONVERT_FULL_PACKAGE, ARKTS_PARSABLE_MEMBER);
    // The getter's prologue reads the current call-scope entry through the
    // bare generated name; bind it from its host package.
    AddSyntheticMemberImport(importMgr, pkg, file, at, ARKTS_CONTEXT_ABI_FULL_PACKAGE, ARKTS_CONTEXT_CURRENT);
}

} // namespace

} // namespace

} // namespace Cangjie::Native::FFI::ArkTS

namespace Cangjie {

void ImportManager::AddArkTSInteropImports(
    const std::vector<Ptr<Package>>& packages, SourceManager* sourceManager)
{
    // The interop packages are compiler-injected support for an experimental
    // capability; without --experimental the gate diagnostic owns the source
    // and no implicit import may widen the program's dependencies.
    if (!opts.experimentalMode) {
        return;
    }
    Utils::ProfileRecorder recorder("BuildIndex", "AddArkTSInteropImports");
    for (auto pkg : packages) {
        CJC_NULLPTR_CHECK(pkg);
        for (auto& file : pkg->files) {
            CJC_NULLPTR_CHECK(file);
            // Any @ArkTS annotation in the file (on a top-level declaration,
            // a class/interface member, a parameter or a global variable)
            // selects the ArkTS interop capability.
            Ptr<Annotation> firstAnnotation{nullptr};
            if (!Native::FFI::ArkTS::FileSelectsArkTSInterop(diag, sourceManager, *file, firstAnnotation)) {
                continue;
            }
            Native::FFI::ArkTS::InjectArkTSInteropImports(
                *this, *pkg, *file, Native::FFI::ArkTS::SyntheticImportRange(*file, firstAnnotation));
        }
    }
}

} // namespace Cangjie
