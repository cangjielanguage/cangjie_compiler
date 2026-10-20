// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "../../src/Sema/NativeFFI/ArkTS/ArkTSInterop.h"
#include "../../src/Sema/NativeFFI/ArkTS/FunctionSource.h"
#include "../../src/Sema/NativeFFI/ArkTS/Utils/EntrySelection.h"
#include "cangjie/AST/Clone.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Mangle/ASTMangler.h"
#include "cangjie/Modules/ASTSerialization.h"
#include "cangjie/Modules/CjoManager.h"
#include "cangjie/Modules/CjoVersion.h"
#include "cangjie/Parse/ASTHasher.h"
#include "cangjie/Parse/Parser.h"
#include "cangjie/Sema/TypeManager.h"
#include "gtest/gtest.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace Cangjie;
using namespace Cangjie::AST;
using namespace Cangjie::Native::FFI::ArkTS;

class ArkTSEntryTest : public testing::Test {
protected:
    SourceManager sm;
    DiagnosticEngine diag;
    OwnedPtr<File> Parse(const std::string& code)
    {
        diag.SetSourceManager(&sm);
        Parser parser(code, diag, sm);
        auto file = parser.ParseTopLevel();
        diag.EmitCategoryDiagnostics(DiagCategory::PARSE);
        return file;
    }

    OwnedPtr<File> Scan(const std::string& code)
    {
        diag.SetSourceManager(&sm);
        GlobalOptions options;
        options.scanDepPkg = true;
        Parser parser(code, diag, sm);
        parser.SetCompileOptions(options);
        return parser.ParseTopLevel();
    }
};

TEST_F(ArkTSEntryTest, BuiltinIdentityAndMinimalEntry)
{
    auto file = Parse(R"(@ArkTS
public func run(): Unit {
    println(42)
}
)");
    ASSERT_NE(file, nullptr);
    ASSERT_EQ(diag.GetErrorCount(), 0);
    ASSERT_EQ(file->decls.size(), 1);
    EXPECT_TRUE(file->imports.empty());
    auto& fd = StaticCast<FuncDecl&>(*file->decls[0]);
    EXPECT_TRUE(IsArkTSEntry(fd));
    EXPECT_TRUE(fd.TestAttr(Attribute::ARKTS));
    EXPECT_EQ(fd.annotations[0]->kind, AnnotationKind::ARKTS);
    EXPECT_EQ(AnnotationKindToString(AnnotationKind::ARKTS), "ArkTS");
    EXPECT_EQ(static_cast<int>(AnnotationKind::ARKTS) + 1, static_cast<int>(AnnotationKind::UNKNOWN));
}

TEST_F(ArkTSEntryTest, RegistrationRestoresContextInFinally)
{
    auto source = Parse(R"(@ArkTS
public func run(): Unit {}
)");
    ASSERT_EQ(diag.GetErrorCount(), 0);
    auto generated = Parse(ArkTSEntryWrapperSource("0"));
    ASSERT_EQ(diag.GetErrorCount(), 0);
    ASSERT_EQ(generated->decls.size(), 2);
    auto& wrapper = StaticCast<FuncDecl&>(*generated->decls[0]);
    EXPECT_EQ(wrapper.identifier.Val(), "__cj_arkts_entry_0");
    EXPECT_EQ(wrapper.funcBody->paramLists[0]->params.size(), 2);
    EXPECT_EQ(wrapper.funcBody->paramLists[0]->params[1]->identifier.Val(), "_");
    auto& body = *wrapper.funcBody->body;
    ASSERT_EQ(body.body.size(), 2);
    ASSERT_EQ(body.body[0]->astKind, ASTKind::VAR_DECL);
    ASSERT_EQ(body.body[1]->astKind, ASTKind::TRY_EXPR);
    auto& guarded = StaticCast<TryExpr&>(*body.body[1]);
    ASSERT_NE(guarded.tryBlock, nullptr);
    ASSERT_NE(guarded.finallyBlock, nullptr);
    size_t originalCalls = 0;
    Walker(guarded.tryBlock.get(), [&](Ptr<Node> node) {
        if (auto reference = DynamicCast<RefExpr*>(node)) {
            originalCalls += reference->ref.identifier == "__cj_arkts_original" ? 1 : 0;
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    ASSERT_EQ(guarded.finallyBlock->body.size(), 1);
    auto& restored = StaticCast<CallExpr&>(*guarded.finallyBlock->body.front());
    EXPECT_EQ(StaticCast<RefExpr&>(*restored.baseFunc).ref.identifier, "__cj_arkts_restore");
    EXPECT_EQ(originalCalls, 1);
}

TEST_F(ArkTSEntryTest, DependencyScanPreservesExplicitInteropImport)
{
    GlobalOptions options;
    options.scanDepPkg = true;
    Parser parser(R"(package entry

import ohos.ark_interop.*

@ArkTS
public func run(): Unit {}
)",
        diag, sm);
    parser.SetCompileOptions(options);
    auto file = parser.ParseTopLevel();
    ASSERT_EQ(diag.GetErrorCount(), 0);
    ASSERT_EQ(file->imports.size(), 1);
    EXPECT_EQ(file->imports[0]->content.GetImportedPackageName(), "ohos.ark_interop");
    EXPECT_FALSE(file->imports[0]->TestAttr(Attribute::COMPILER_ADD));
    EXPECT_FALSE(file->imports[0]->TestAttr(Attribute::IMPLICIT_ADD));
}

TEST_F(ArkTSEntryTest, DependencyScanDoesNotTreatStringOrCommentAsAnnotation)
{
    GlobalOptions options;
    options.scanDepPkg = true;
    Parser parser(R"(package entry
// @ArkTS
let text = "@ArkTS"
)",
        diag, sm);
    parser.SetCompileOptions(options);
    auto file = parser.ParseTopLevel();
    ASSERT_EQ(diag.GetErrorCount(), 0);
    EXPECT_TRUE(file->imports.empty());
}

TEST_F(ArkTSEntryTest, NativeFFIPreparationPreservesFileOwnershipAndImportVisibility)
{
    Package pkg;
    pkg.fullPackageName = "entry";
    pkg.files.emplace_back(Parse(R"(import arkts.lang.*

@ArkTS
public func run(): Unit {}
)"));
    auto file = pkg.files.front().get();
    Walker(file, [&](Ptr<Node> node) {
        node->curFile = file;
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    ASSERT_EQ(file->imports.size(), 1);
    auto originalImport = file->imports.front().get();
    const auto importBegin = originalImport->content.begin;
    const auto importEnd = originalImport->content.end;
    const auto importFile = originalImport->content.curFile;
    Native::FFI::ArkTS::PrepareTypeCheck(pkg, diag, sm);
    ASSERT_EQ(diag.GetErrorCount(), 0);
    ASSERT_EQ(file->decls.size(), 3);
    ASSERT_EQ(file->imports.size(), 1);
    std::vector<std::string> generatedIdentities;
    for (size_t i = 1; i < file->decls.size(); ++i) {
        const auto& generated = file->decls[i];
        EXPECT_TRUE(generated->TestAttr(Attribute::COMPILER_ADD));
        EXPECT_TRUE(generated->toBeCompiled);
        EXPECT_FALSE(generated->rawMangleName.empty());
        EXPECT_EQ(generated->rawMangleName.substr(0, 6), "5entry");
        generatedIdentities.emplace_back(generated->rawMangleName);
    }
    EXPECT_NE(generatedIdentities[0], generatedIdentities[1]);
    Native::FFI::ArkTS::PrepareTypeCheck(pkg, diag, sm);
    ASSERT_EQ(diag.GetErrorCount(), 0);
    ASSERT_EQ(file->decls.size(), 3);
    for (size_t i = 1; i < file->decls.size(); ++i) {
        EXPECT_EQ(file->decls[i]->rawMangleName, generatedIdentities[i - 1]);
        EXPECT_TRUE(file->decls[i]->toBeCompiled);
    }
    for (auto& decl : file->decls) {
        Walker(decl.get(), [&](Ptr<Node> node) {
            EXPECT_EQ(node->curFile, file);
            return VisitAction::WALK_CHILDREN;
        }).Walk();
    }
    EXPECT_EQ(file->imports.front().get(), originalImport);
    EXPECT_EQ(originalImport->content.begin, importBegin);
    EXPECT_EQ(originalImport->content.end, importEnd);
    for (auto& imported : file->imports) {
        EXPECT_EQ(imported->curFile, file);
        EXPECT_FALSE(imported->TestAttr(Attribute::COMPILER_ADD));
        EXPECT_FALSE(imported->TestAttr(Attribute::IMPLICIT_ADD));
        EXPECT_EQ(imported->content.prefixPoses.size(), imported->content.prefixPaths.size());
        EXPECT_EQ(imported->content.prefixDotPoses.size(), imported->content.prefixPaths.size());
        EXPECT_EQ(imported->content.curFile, importFile);
        Walker(imported.get(), [&](Ptr<Node> node) {
            EXPECT_EQ(node->curFile, file);
            return VisitAction::WALK_CHILDREN;
        }).Walk();
    }
    EXPECT_EQ(file->imports.front()->content.GetPrefixPath(), "arkts.lang");
    EXPECT_EQ(file->imports.front()->content.identifier.Val(), "*");
    EXPECT_EQ(file->imports.front()->content.kind, ImportKind::IMPORT_ALL);
    EXPECT_TRUE(file->imports.front()->content.aliasName.Val().empty());
}

TEST_F(ArkTSEntryTest, DependencyScanDoesNotInjectImportForAnnotation)
{
    GlobalOptions options;
    options.scanDepPkg = true;
    Parser parser(R"(package entry

@ArkTS
public func run(): Unit {}
)",
        diag, sm);
    parser.SetCompileOptions(options);
    auto file = parser.ParseTopLevel();
    ASSERT_EQ(diag.GetErrorCount(), 0);
    EXPECT_TRUE(file->imports.empty());
}

TEST_F(ArkTSEntryTest, ExplicitImportWithoutAnnotationRemainsDependency)
{
    GlobalOptions options;
    options.scanDepPkg = true;
    Parser parser(R"(package entry

import ohos.ark_interop.*

public func helper(): Unit {}
)",
        diag, sm);
    parser.SetCompileOptions(options);
    auto file = parser.ParseTopLevel();
    ASSERT_EQ(diag.GetErrorCount(), 0);
    ASSERT_EQ(file->imports.size(), 1);
    EXPECT_EQ(file->imports[0]->content.GetImportedPackageName(), "ohos.ark_interop");
    EXPECT_FALSE(file->imports[0]->TestAttr(Attribute::COMPILER_ADD));
    EXPECT_FALSE(file->imports[0]->TestAttr(Attribute::IMPLICIT_ADD));
}

TEST_F(ArkTSEntryTest, RepeatedPreparationPreservesMultiFileEntries)
{
    Package pkg;
    pkg.files.emplace_back(Parse(R"(import ohos.ark_interop.*

@ArkTS
public func first(): Unit {}

@ArkTS
public func second(): Unit {}

public func helper(): Unit {}
)"));
    pkg.files.emplace_back(Parse(R"(import ohos.ark_interop.*

@ArkTS
public func third(): Unit {}
)"));
    ASSERT_EQ(diag.GetErrorCount(), 0);
    std::vector<Ptr<FuncDecl>> entries;
    std::vector<Ptr<Annotation>> annotations;
    std::vector<Ptr<ImportSpec>> imports;
    std::vector<Position> importBegins;
    for (auto& file : pkg.files) {
        Walker(file.get(), [target = file.get()](Ptr<Node> node) {
            node->curFile = target;
            return VisitAction::WALK_CHILDREN;
        }).Walk();
        for (auto& decl : file->decls) {
            if (auto fd = DynamicCast<FuncDecl*>(decl.get()); fd && IsArkTSEntry(*fd)) {
                entries.emplace_back(fd);
                annotations.emplace_back(fd->annotations.front().get());
            }
        }
        imports.emplace_back(file->imports.front().get());
        importBegins.emplace_back(file->imports.front()->content.begin);
    }
    ASSERT_EQ(entries.size(), 3);
    PrepareTypeCheck(pkg, diag, sm);
    ASSERT_EQ(pkg.files[0]->decls.size(), 7);
    ASSERT_EQ(pkg.files[1]->decls.size(), 3);
    std::vector<std::vector<Ptr<Decl>>> prepared;
    for (auto& file : pkg.files) {
        std::vector<Ptr<Decl>> declarations;
        for (auto& decl : file->decls) {
            declarations.emplace_back(decl.get());
        }
        prepared.emplace_back(std::move(declarations));
    }

    PrepareTypeCheck(pkg, diag, sm);
    ASSERT_EQ(diag.GetErrorCount(), 0);
    for (size_t i = 0; i < pkg.files.size(); ++i) {
        auto& file = pkg.files[i];
        ASSERT_EQ(file->decls.size(), prepared[i].size());
        for (size_t j = 0; j < file->decls.size(); ++j) {
            EXPECT_EQ(file->decls[j].get(), prepared[i][j]);
            Walker(file->decls[j].get(), [target = file.get()](Ptr<Node> node) {
                EXPECT_EQ(node->curFile, target);
                return VisitAction::WALK_CHILDREN;
            }).Walk();
        }
        ASSERT_EQ(file->imports.size(), 1);
        EXPECT_EQ(file->imports.front().get(), imports[i]);
        EXPECT_EQ(file->imports.front()->content.begin, importBegins[i]);
        EXPECT_FALSE(file->imports.front()->TestAttr(Attribute::COMPILER_ADD));
    }
    for (size_t i = 0; i < entries.size(); ++i) {
        EXPECT_TRUE(IsArkTSEntry(*entries[i]));
        ASSERT_EQ(entries[i]->annotations.size(), 1);
        EXPECT_EQ(entries[i]->annotations.front().get(), annotations[i]);
        EXPECT_FALSE(entries[i]->TestAttr(Attribute::COMPILER_ADD));
    }
    EXPECT_TRUE(pkg.files[0]->decls[3]->TestAttr(Attribute::COMPILER_ADD));
    EXPECT_TRUE(pkg.files[0]->decls[4]->TestAttr(Attribute::COMPILER_ADD));
    EXPECT_EQ(pkg.files[0]->decls[3]->identifier.Val(), "__cj_arkts_entry_0");
    EXPECT_EQ(pkg.files[0]->decls[5]->identifier.Val(), "__cj_arkts_entry_1");
    EXPECT_EQ(pkg.files[1]->decls[1]->identifier.Val(), "__cj_arkts_entry_2");
}

TEST_F(ArkTSEntryTest, PreparationStateBelongsToEachPackage)
{
    Package first;
    Package second;
    for (auto pkg : {&first, &second}) {
        pkg->files.emplace_back(Parse(R"(@ArkTS
public func run(): Unit {}
)"));
        auto file = pkg->files.front().get();
        Walker(file, [file](Ptr<Node> node) {
            node->curFile = file;
            return VisitAction::WALK_CHILDREN;
        }).Walk();
        PrepareTypeCheck(*pkg, diag, sm);
        ASSERT_EQ(file->decls.size(), 3);
    }
    auto firstWrapper = first.files.front()->decls[1].get();
    auto secondWrapper = second.files.front()->decls[1].get();
    EXPECT_NE(firstWrapper, secondWrapper);
    PrepareTypeCheck(first, diag, sm);
    PrepareTypeCheck(second, diag, sm);
    ASSERT_EQ(diag.GetErrorCount(), 0);
    ASSERT_EQ(first.files.front()->decls.size(), 3);
    ASSERT_EQ(second.files.front()->decls.size(), 3);
    EXPECT_EQ(first.files.front()->decls[1].get(), firstWrapper);
    EXPECT_EQ(second.files.front()->decls[1].get(), secondWrapper);
}

TEST_F(ArkTSEntryTest, EntryAnnotationChangesBodyHashAndPreservesSignature)
{
    auto plain = Parse(R"(public func run(): Unit {}
)");
    auto annotated = Parse(R"(@ArkTS
public func run(): Unit {}
)");
    ASSERT_EQ(diag.GetErrorCount(), 0);
    auto& entry = StaticCast<FuncDecl&>(*plain->decls.front());
    auto& source = StaticCast<FuncDecl&>(*annotated->decls.front());
    const auto body = ASTHasher::BodyHash(entry, {false, false});
    const auto signature = ASTHasher::SigHash(entry);
    const auto sourceUse = ASTHasher::SrcUseHash(entry);
    ASSERT_EQ(source.annotations.size(), 1);
    entry.annotations.emplace_back(std::move(source.annotations.front()));
    const auto entryBody = ASTHasher::BodyHash(entry, {false, false});
    EXPECT_NE(entryBody, body);
    EXPECT_EQ(ASTHasher::SigHash(entry), signature);
    EXPECT_EQ(ASTHasher::SrcUseHash(entry), sourceUse);
    EXPECT_EQ(ASTHasher::BodyHash(entry, {false, false}, false), body);

    auto annotation = std::move(entry.annotations.front());
    entry.annotations.clear();
    EXPECT_EQ(ASTHasher::BodyHash(entry, {false, false}), body);
    entry.annotations.emplace_back(std::move(annotation));
    EXPECT_EQ(ASTHasher::BodyHash(entry, {false, false}), entryBody);
    EXPECT_EQ(ASTHasher::SigHash(entry), signature);
}

TEST_F(ArkTSEntryTest, LegacyBodyHashHandlesArkTSMetadata)
{
    auto file = Parse(R"(type Alias = Int64
)");
    ASSERT_EQ(diag.GetErrorCount(), 0);
    ASSERT_EQ(file->decls.size(), 1);
    auto& alias = *file->decls.front();
    const auto withoutAnnotation = ASTHasher::SigHash(alias);
    // An internal AST fixture exercises the legacy body-hash branch. This does
    // not declare support for applying @ArkTS to a source type alias.
    auto annotation = MakeOwned<Annotation>();
    annotation->kind = AnnotationKind::ARKTS;
    annotation->identifier = "ArkTS";
    alias.annotations.emplace_back(std::move(annotation));
    EXPECT_NE(ASTHasher::SigHash(alias), withoutAnnotation);
    alias.annotations.clear();
    EXPECT_EQ(ASTHasher::SigHash(alias), withoutAnnotation);
}

TEST_F(ArkTSEntryTest, CjoRoundTripPreservesOnlyArkTSDeclarationMarker)
{
    // This serializer fixture assigns primitive types directly. It does not
    // run entry generation or claim SDK import/type-check integration coverage.
    struct Input {
        bool forIncremental;
        bool markerOnly;
        bool withCustom;
    };
    for (const auto& mode : {Input{false, false, false}, Input{false, true, false},
        Input{true, false, false}, Input{true, true, false}, Input{false, false, true},
        Input{false, true, true}, Input{true, false, true}, Input{true, true, true}}) {
        TypeManager types;
        GlobalOptions options;
        bool importSource = false;
        CjoManager writerPackages({diag, types, options, importSource});
        Package source;
        source.fullPackageName = "arkts_metadata";
        source.files.emplace_back(Parse(R"(package arkts_metadata

@ArkTS
public func run(): Unit {}

public func helper(): Unit {}
)"));
        ASSERT_EQ(diag.GetErrorCount(), 0);
        auto file = source.files.front().get();
        file->curPackage = &source;
        auto unit = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
        auto signature = types.GetFunctionTy({}, unit);
        for (auto& item : file->decls) {
            auto fd = DynamicCast<FuncDecl*>(item.get());
            ASSERT_NE(fd, nullptr);
            if (mode.markerOnly) {
                fd->annotations.clear();
            }
            fd->EnableAttr(Attribute::GLOBAL, Attribute::PUBLIC);
            fd->linkage = Linkage::EXTERNAL;
            fd->fullPackageName = source.fullPackageName;
            fd->SetTy(signature);
            fd->funcBody->SetTy(signature);
            fd->funcBody->retType->SetTy(unit);
            fd->funcBody->body->SetTy(unit);
            Walker(fd, [file](Ptr<Node> node) {
                node->curFile = file;
                return VisitAction::WALK_CHILDREN;
            }).Walk();
            fd->rawMangleName = ASTMangler(source.fullPackageName).Mangle(*fd);
            fd->mangledName = "__metadata_fixture_" + fd->identifier.Val();
            fd->hash.bodyHash = ASTHasher::BodyHash(*fd, {false, false});
        }
        auto sourceRun = DynamicCast<FuncDecl*>(file->decls.front().get());
        auto sourceHelper = DynamicCast<FuncDecl*>(file->decls.back().get());
        ASSERT_NE(sourceRun, nullptr);
        ASSERT_NE(sourceHelper, nullptr);
        if (mode.withCustom) {
            // Artificial constructor targets isolate reference wiring without
            // claiming custom-annotation semantic validation in this fixture.
            for (bool visible : {false, true}) {
                auto annotation = MakeOwned<Annotation>();
                annotation->kind = AnnotationKind::CUSTOM;
                annotation->identifier = std::string(visible ? "Visible" : "RuntimeOnly");
                annotation->isCompileTimeVisible = visible;
                auto reference = MakeOwned<RefExpr>();
                reference->ref.target = sourceHelper;
                reference->ref.identifier = sourceHelper->identifier;
                reference->SetTy(sourceHelper->GetTy());
                annotation->baseExpr = std::move(reference);
                sourceRun->annotations.emplace_back(std::move(annotation));
            }
        }
        writerPackages.AddSourcePackage(source);
        auto packageDecl = writerPackages.GetPackageDecl(source.fullPackageName);
        ASSERT_NE(packageDecl, nullptr);
        ExportConfig config;
        config.exportForIncr = mode.forIncremental;
        ASTWriter writer(diag, "", config, writerPackages, types);
        writer.PreSaveFullExportDecls(source);
        writer.ExportAST(*packageDecl);
        std::vector<uint8_t> buffer;
        writer.AST2FB(buffer, *packageDecl);
        ASSERT_FALSE(buffer.empty());
        const auto rawPackage = PackageFormat::GetPackage(buffer.data());
        ASSERT_EQ(rawPackage->allDecls()->size(), 2);
        ASSERT_NE(rawPackage->cjoVersion(), nullptr);
        EXPECT_EQ(rawPackage->cjoVersion()->major_num(), CJO_MAJOR_VERSION);
        EXPECT_EQ(rawPackage->cjoVersion()->minor_num(), CJO_MINOR_VERSION);
        EXPECT_EQ(rawPackage->cjoVersion()->patch_num(), CJO_PATCH_VERSION);
        for (auto rawDecl : *rawPackage->allDecls()) {
            const bool entry = rawDecl->identifier()->str() == "run";
            ASSERT_NE(rawDecl->annotations(), nullptr);
            EXPECT_EQ(rawDecl->annotations()->size(), entry ? (mode.withCustom ? 2 : 1) : 0);
            size_t markers = 0;
            for (auto annotation : *rawDecl->annotations()) {
                if (annotation->kind() == PackageFormat::AnnoKind_ArkTS) {
                    ++markers;
                    EXPECT_EQ(annotation->identifier()->str(), "ArkTS");
                    ASSERT_NE(annotation->args(), nullptr);
                    EXPECT_EQ(annotation->args()->size(), 0);
                    EXPECT_EQ(annotation->target(), nullptr);
                } else {
                    EXPECT_EQ(annotation->kind(), PackageFormat::AnnoKind_Custom);
                    EXPECT_EQ(annotation->identifier()->str(), "Visible");
                    EXPECT_NE(annotation->target(), nullptr);
                }
            }
            EXPECT_EQ(markers, entry ? 1 : 0);
        }
        CjoManager readerPackages({diag, types, options, importSource});
        ASTLoader reader(std::move(buffer), source.fullPackageName, types, readerPackages, options);
        auto imported = reader.LoadPackageDependencies();
        ASSERT_NE(imported, nullptr);
        auto importedPtr = imported.get();
        readerPackages.AddImportedPackageFromASTNode(std::move(imported));
        reader.LoadPackageDecls();
        reader.LoadRefs();
        ASSERT_EQ(diag.GetErrorCount(), 0);
        ASSERT_EQ(importedPtr->files.size(), 1);
        auto& declarations = importedPtr->files.front()->decls;
        ASSERT_EQ(declarations.size(), 2);
        Ptr<FuncDecl> run = nullptr;
        Ptr<FuncDecl> plain = nullptr;
        for (auto& item : declarations) {
            auto function = DynamicCast<FuncDecl*>(item.get());
            if (function && function->identifier == "run") {
                run = function;
            } else if (function && function->identifier == "helper") {
                plain = function;
            }
        }
        ASSERT_NE(run, nullptr);
        ASSERT_NE(plain, nullptr);
        EXPECT_EQ(run->identifier.Val(), "run");
        EXPECT_EQ(plain->identifier.Val(), "helper");
        EXPECT_TRUE(run->TestAttr(Attribute::IMPORTED));
        EXPECT_TRUE(run->TestAttr(Attribute::ARKTS));
        EXPECT_FALSE(run->HasAnno(AnnotationKind::ARKTS));
        EXPECT_EQ(run->annotations.size(), mode.withCustom ? 1 : 0);
        EXPECT_FALSE(IsArkTSEntry(*run));
        EXPECT_FALSE(plain->TestAttr(Attribute::ARKTS));
        EXPECT_FALSE(plain->HasAnno(AnnotationKind::ARKTS));
        EXPECT_TRUE(plain->annotations.empty());
        EXPECT_TRUE(Ty::IsTyCorrect(run->GetTy()));
        EXPECT_EQ(run->GetTy()->kind, TypeKind::TYPE_FUNC);
        auto cloned = ASTCloner::Clone(run);
        ASSERT_NE(cloned, nullptr);
        EXPECT_TRUE(cloned->TestAttr(Attribute::ARKTS));
        EXPECT_EQ(cloned->annotations.size(), run->annotations.size());
        if (mode.withCustom) {
            ASSERT_NE(run->annotations.front()->baseExpr, nullptr);
            EXPECT_EQ(run->annotations.front()->baseExpr->GetTarget(), plain);
            auto importedAnnotations = std::move(run->annotations);
            // The reference loader also receives current-source annotation vectors
            // when reusing cached types. Exercise that shape, including a runtime-only
            // custom annotation and an optional real source @ArkTS node.
            run->annotations = ASTCloner::CloneVector(sourceRun->annotations);
            for (auto& annotation : run->annotations) {
                annotation->baseExpr.reset();
            }
            reader.LoadRefs();
            ASSERT_NE(run->annotations.back()->baseExpr, nullptr);
            EXPECT_EQ(run->annotations.back()->baseExpr->GetTarget(), plain);
            EXPECT_EQ(run->annotations[mode.markerOnly ? 0 : 1]->baseExpr, nullptr);
            run->annotations = std::move(importedAnnotations);
        }
        PrepareTypeCheck(*importedPtr, diag, sm);
        EXPECT_EQ(declarations.size(), 2);
        EXPECT_EQ(diag.GetErrorCount(), 0);
    }
}

TEST_F(ArkTSEntryTest, CjoOptionsRoundTripPreservesDeclarationPriority)
{
    // Typed serializer-only fixture: no SDK calls or entry generation are run.
    for (bool incremental : {false, true}) {
        TypeManager types;
        GlobalOptions options;
        bool importSource = false;
        CjoManager sourcePackages({diag, types, options, importSource});
        Package source;
        source.fullPackageName = "arkts_option_metadata";
        source.files.emplace_back(Parse(R"(package arkts_option_metadata

@ArkTS[module: "block", namespace: "outer", foreignName: "original"]
public func source(): Unit {}
)"));
        auto declaration = Parse(R"(@ArkTS[module: "actual", foreignName: "echo"]
public func member(): Unit {}
)");
        ASSERT_EQ(diag.GetErrorCount(), 0);
        auto file = source.files.front().get();
        file->curPackage = &source;
        auto function = DynamicCast<FuncDecl*>(file->decls.front().get());
        ASSERT_NE(function, nullptr);
        function->annotations.emplace_back(std::move(declaration->decls.front()->annotations.front()));
        auto unit = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
        auto signature = types.GetFunctionTy({}, unit);
        function->EnableAttr(Attribute::GLOBAL, Attribute::PUBLIC);
        function->linkage = Linkage::EXTERNAL;
        function->fullPackageName = source.fullPackageName;
        function->SetTy(signature);
        function->funcBody->SetTy(signature);
        function->funcBody->retType->SetTy(unit);
        function->funcBody->body->SetTy(unit);
        const auto begin = function->begin;
        const auto end = function->end;
        Walker(function, [file, begin, end](Ptr<Node> node) {
            node->curFile = file;
            node->begin = begin;
            node->end = end;
            return VisitAction::WALK_CHILDREN;
        }).Walk();
        function->rawMangleName = ASTMangler(source.fullPackageName).Mangle(*function);
        function->mangledName = "__arkts_option_metadata_source";
        function->hash.bodyHash = ASTHasher::BodyHash(*function, {false, false});
        sourcePackages.AddSourcePackage(source);
        auto packageDecl = sourcePackages.GetPackageDecl(source.fullPackageName);
        ASSERT_NE(packageDecl, nullptr);
        ExportConfig config;
        config.exportForIncr = incremental;
        ASTWriter writer(diag, "", config, sourcePackages, types);
        writer.PreSaveFullExportDecls(source);
        writer.ExportAST(*packageDecl);
        std::vector<uint8_t> buffer;
        writer.AST2FB(buffer, *packageDecl);
        ASSERT_FALSE(buffer.empty());
        auto raw = PackageFormat::GetPackage(buffer.data());
        ASSERT_EQ(raw->allDecls()->size(), 1);
        auto records = raw->allDecls()->Get(0)->annotations();
        ASSERT_NE(records, nullptr);
        ASSERT_EQ(records->size(), 2);
        EXPECT_EQ(records->Get(0)->kind(), PackageFormat::AnnoKind_ArkTS);
        EXPECT_EQ(records->Get(1)->kind(), PackageFormat::AnnoKind_ArkTS);
        EXPECT_EQ(records->Get(0)->args()->size(), 3);
        EXPECT_EQ(records->Get(1)->args()->size(), 2);
        EXPECT_EQ(records->Get(0)->target(), nullptr);
        EXPECT_EQ(records->Get(1)->target(), nullptr);
        EXPECT_EQ(raw->cjoVersion()->minor_num(), 2);
        CjoManager importedPackages({diag, types, options, importSource});
        ASTLoader reader(std::move(buffer), source.fullPackageName, types, importedPackages, options);
        auto imported = reader.LoadPackageDependencies();
        ASSERT_NE(imported, nullptr);
        auto package = imported.get();
        importedPackages.AddImportedPackageFromASTNode(std::move(imported));
        reader.LoadPackageDecls();
        reader.LoadRefs();
        ASSERT_EQ(diag.GetErrorCount(), 0);
        ASSERT_EQ(package->files.size(), 1);
        ASSERT_EQ(package->files.front()->decls.size(), 1);
        auto restored = DynamicCast<FuncDecl*>(package->files.front()->decls.front().get());
        ASSERT_NE(restored, nullptr);
        EXPECT_TRUE(restored->TestAttr(Attribute::ARKTS));
        EXPECT_TRUE(restored->HasAnno(AnnotationKind::ARKTS));
        ASSERT_EQ(restored->annotations.size(), 2);
        std::unordered_map<std::string, std::string> merged;
        ASSERT_TRUE(ArkTSFunctionOptions(*restored, merged));
        EXPECT_EQ(merged.at("module"), "actual");
        EXPECT_EQ(merged.at("namespace"), "outer");
        EXPECT_EQ(merged.at("foreignName"), "echo");
        for (auto& annotation : restored->annotations) {
            EXPECT_EQ(annotation->baseExpr, nullptr);
        }

        // Re-emit the imported metadata as a typed source fixture. This isolates
        // write/read/write preservation from package-public-reexport linking.
        Package reexport;
        reexport.fullPackageName = source.fullPackageName;
        reexport.files.emplace_back(Parse("package arkts_option_metadata\n"));
        auto secondFile = reexport.files.front().get();
        secondFile->curPackage = &reexport;
        auto copied = ASTCloner::Clone(Ptr<FuncDecl>(restored));
        copied->DisableAttr(Attribute::IMPORTED);
        Walker(copied.get(), [secondFile](Ptr<Node> node) {
            node->curFile = secondFile;
            node->begin = secondFile->begin;
            node->end = secondFile->end;
            return VisitAction::WALK_CHILDREN;
        }).Walk();
        secondFile->decls.emplace_back(std::move(copied));
        CjoManager reexportPackages({diag, types, options, importSource});
        reexportPackages.AddSourcePackage(reexport);
        auto reexportDecl = reexportPackages.GetPackageDecl(reexport.fullPackageName);
        ASSERT_NE(reexportDecl, nullptr);
        ASTWriter secondWriter(diag, "", config, reexportPackages, types);
        secondWriter.PreSaveFullExportDecls(reexport);
        secondWriter.ExportAST(*reexportDecl);
        std::vector<uint8_t> secondBuffer;
        secondWriter.AST2FB(secondBuffer, *reexportDecl);
        ASSERT_FALSE(secondBuffer.empty());
        auto secondRaw = PackageFormat::GetPackage(secondBuffer.data());
        ASSERT_EQ(secondRaw->allDecls()->size(), 1);
        auto secondRecords = secondRaw->allDecls()->Get(0)->annotations();
        ASSERT_EQ(secondRecords->size(), 2);
        EXPECT_EQ(secondRecords->Get(0)->args()->size(), 3);
        EXPECT_EQ(secondRecords->Get(1)->args()->size(), 2);
        EXPECT_EQ(secondRaw->cjoVersion()->minor_num(), 2);
        EXPECT_EQ(diag.GetErrorCount(), 0);
    }
}


TEST_F(ArkTSEntryTest, PublicForeignFunctionInheritsBlockOptions)
{
    auto file = Parse(R"(import ohos.ark_interop.*

@ArkTS[module: "actual", namespace: "outer.inner"]
foreign {
    @ArkTS[foreignName: "echo"]
    public func renamed(value: String): String

    public func sideEffect(value: Int32): Unit
}
)");
    ASSERT_EQ(diag.GetErrorCount(), 0);
    ASSERT_EQ(file->decls.size(), 2);
    auto& function = StaticCast<FuncDecl&>(*file->decls.front());
    EXPECT_TRUE(IsArkTSForeignFunction(function));
    EXPECT_TRUE(function.TestAttr(Attribute::PUBLIC));
    std::unordered_map<std::string, std::string> options;
    ASSERT_TRUE(ArkTSFunctionOptions(function, options));
    EXPECT_EQ(options.at("module"), "actual");
    EXPECT_EQ(options.at("namespace"), "outer.inner");
    EXPECT_EQ(options.at("foreignName"), "echo");
}

TEST_F(ArkTSEntryTest, DuplicateMemberAnnotationStillUsesCommonCheck)
{
    Parse(R"(@ArkTS[module: "actual"]
foreign {
    @ArkTS[foreignName: "echo"]
    @ArkTS[foreignName: "echo"]
    func renamed(): Unit
}
)");
    EXPECT_GT(diag.GetErrorCount(), 0);
}

TEST_F(ArkTSEntryTest, DeclarationOptionsOverrideOnlyTheirBlockDefaults)
{
    auto file = Parse(R"(@ArkTS[module: "first", namespace: "outer", foreignName: "original"]
foreign {
    @ArkTS[module: "second", namespace: "inner", foreignName: "echo"]
    func renamed(): Unit

    @ArkTS[foreignName: "another"]
    func sibling(): Unit

    func inherited(): Unit
}
)");
    ASSERT_EQ(diag.GetErrorCount(), 0);
    ASSERT_EQ(file->decls.size(), 3);
    const std::vector<std::vector<std::string>> expected{
        {"second", "inner", "echo"}, {"first", "outer", "another"}, {"first", "outer", "original"}};
    for (size_t i = 0; i < file->decls.size(); ++i) {
        std::unordered_map<std::string, std::string> options;
        ASSERT_TRUE(ArkTSFunctionOptions(StaticCast<FuncDecl&>(*file->decls[i]), options));
        EXPECT_EQ(options.at("module"), expected[i][0]);
        EXPECT_EQ(options.at("namespace"), expected[i][1]);
        EXPECT_EQ(options.at("foreignName"), expected[i][2]);
    }
}

TEST_F(ArkTSEntryTest, RepeatedOptionAtOneLevelIsRejectedEvenForEqualValues)
{
    for (const auto& name : {"module", "namespace", "foreignName"}) {
        for (const auto& second : {"first", "second"}) {
            // Separate diagnostic engines keep equal source positions in these
            // independent inputs from being deduplicated across test cases.
            SourceManager sourceManager;
            DiagnosticEngine diagnostics;
            diagnostics.SetSourceManager(&sourceManager);
            const auto code = "@ArkTS[" + std::string(name) + ": \"first\", " + name + ": \"" +
                second + "\"] foreign func invalid(): Unit\n";
            Parser parser(code, diagnostics, sourceManager);
            auto file = parser.ParseTopLevel();
            diagnostics.EmitCategoryDiagnostics(DiagCategory::PARSE);
            EXPECT_EQ(diagnostics.GetErrorCount(), 1);
            std::unordered_map<std::string, std::string> options;
            EXPECT_FALSE(ArkTSFunctionOptions(StaticCast<FuncDecl&>(*file->decls.front()), options));
        }
    }
}

TEST_F(ArkTSEntryTest, SeparateMemberAnnotationsDoNotSplitOneOptionList)
{
    Parse(R"(@ArkTS[module: "actual"]
foreign {
    @ArkTS[namespace: "inner"]
    @ArkTS[foreignName: "echo"]
    func renamed(): Unit
}
)");
    EXPECT_GT(diag.GetErrorCount(), 0);
}

TEST_F(ArkTSEntryTest, OrdinaryCForeignBlockPreservesAnnotationsAndImports)
{
    auto file = Parse(R"(@C
foreign func ordinary(value: Int32): Int32
)");
    ASSERT_EQ(diag.GetErrorCount(), 0);
    ASSERT_EQ(file->decls.size(), 1);
    auto& function = StaticCast<FuncDecl&>(*file->decls.front());
    EXPECT_TRUE(function.TestAttr(Attribute::FOREIGN));
    EXPECT_TRUE(function.HasAnno(AnnotationKind::C));
    EXPECT_FALSE(IsArkTSForeignFunction(function));
    EXPECT_TRUE(file->imports.empty());
}

TEST_F(ArkTSEntryTest, OrdinaryCDuplicateBlockAnnotationStillRejected)
{
    Parse(R"(@C
foreign {
    @C
    func ordinary(): Unit
}
)");
    EXPECT_GT(diag.GetErrorCount(), 0);
}

TEST_F(ArkTSEntryTest, DefaultModuleUsesTheUnmodifiedDeclarationPackage)
{
    EXPECT_EQ(ArkTSDefaultModule("entry.ForeignFixtures"), "entry.ForeignFixtures");
    EXPECT_EQ(ArkTSDefaultModule("library.utils.Math"), "library.utils.Math");
    EXPECT_EQ(ArkTSDefaultModule("entry"), "entry");
    EXPECT_TRUE(ArkTSDefaultModule("").empty());
}

TEST_F(ArkTSEntryTest, ScalarBodiesAreParseableForEverySupportedType)
{
    const std::vector<std::string> types{"Int8", "Int16", "Int32", "UInt8", "UInt16", "UInt32", "Float16", "Float32",
        "Float64", "Rune", "Int64", "UInt64", "BigInt", "Bool", "String", "Unit"};
    for (const auto& type : types) {
        auto generated = Parse(ArkTSForeignWrapperSource({type}, type, 2, "__locals_"));
        ASSERT_EQ(diag.GetErrorCount(), 0) << type;
        ASSERT_EQ(generated->decls.size(), 1);
        size_t calls = 0;
        size_t loads = 0;
        Walker(generated.get(), [&](Ptr<Node> node) {
            if (auto access = DynamicCast<MemberAccess*>(node)) {
                calls += access->field == "callMethod" ? 1 : 0;
                loads += access->field == "requireArkModule" ? 1 : 0;
            }
            return VisitAction::WALK_CHILDREN;
        }).Walk();
        EXPECT_EQ(calls, 1) << type;
        EXPECT_EQ(loads, 1) << type;
    }
    Parse(ArkTSForeignWrapperSource({}, "Nothing", 0, "__locals_"));
    EXPECT_EQ(diag.GetErrorCount(), 0);
}

TEST_F(ArkTSEntryTest, SemanticScalarTypesUseTheCheckedWhitelist)
{
    const std::vector<std::pair<TypeKind, std::string>> types{{TypeKind::TYPE_INT8, "Int8"},
        {TypeKind::TYPE_INT16, "Int16"}, {TypeKind::TYPE_INT32, "Int32"}, {TypeKind::TYPE_INT64, "Int64"},
        {TypeKind::TYPE_UINT8, "UInt8"}, {TypeKind::TYPE_UINT16, "UInt16"}, {TypeKind::TYPE_UINT32, "UInt32"},
        {TypeKind::TYPE_UINT64, "UInt64"}, {TypeKind::TYPE_FLOAT16, "Float16"}, {TypeKind::TYPE_FLOAT32, "Float32"},
        {TypeKind::TYPE_FLOAT64, "Float64"}, {TypeKind::TYPE_RUNE, "Rune"}, {TypeKind::TYPE_BOOLEAN, "Bool"},
        {TypeKind::TYPE_UNIT, "Unit"}, {TypeKind::TYPE_NOTHING, "Nothing"}};
    for (const auto& [kind, name] : types) {
        EXPECT_EQ(ArkTSScalarName(TypeManager::GetPrimitiveTy(kind)), name);
    }
    EXPECT_TRUE(ArkTSScalarName(nullptr).empty());
    EXPECT_TRUE(ArkTSScalarName(TypeManager::GetPrimitiveTy(TypeKind::TYPE_INT_NATIVE)).empty());
}

TEST_F(ArkTSEntryTest, BigIntIdentityUsesItsSemanticPackage)
{
    StructDecl standard;
    standard.identifier = "BigInt";
    standard.fullPackageName = "std.math.numeric";
    StructTy standardType("BigInt", standard, {});
    EXPECT_EQ(ArkTSScalarName(&standardType), "BigInt");
    StructDecl user;
    user.identifier = "BigInt";
    user.fullPackageName = "application";
    StructTy userType("BigInt", user, {});
    EXPECT_TRUE(ArkTSScalarName(&userType).empty());
}

TEST_F(ArkTSEntryTest, BigIntConversionDoesNotUseNumberOrStringParsing)
{
    for (const auto& type : {"Int64", "UInt64", "BigInt"}) {
        auto code = ArkTSForeignWrapperSource({type}, type, 0, "__locals_");
        EXPECT_NE(code.find(".bigint("), std::string::npos);
        EXPECT_NE(code.find(".asBigInt().toBigInt()"), std::string::npos);
        EXPECT_EQ(code.find(".number("), std::string::npos);
        EXPECT_EQ(code.find(".toNumber()"), std::string::npos);
        EXPECT_EQ(code.find(".parse("), std::string::npos);
    }
    auto unsignedCode = ArkTSForeignWrapperSource({"UInt64"}, "UInt64", 0, "__locals_");
    EXPECT_NE(unsignedCode.find("BigInt(__argument_0)"), std::string::npos);
    EXPECT_NE(unsignedCode.find(".toUInt64()"), std::string::npos);
    auto signedCode = ArkTSForeignWrapperSource({}, "Int64", 0, "__locals_");
    EXPECT_NE(signedCode.find(".toInt64()"), std::string::npos);
}

TEST_F(ArkTSEntryTest, ModuleAndNamesAreBoundValuesInsteadOfSourceInterpolation)
{
    auto file = Parse(R"(@ArkTS[module: "actual", foreignName: "quote\"name"]
foreign func echo(): String
)");
    ASSERT_EQ(diag.GetErrorCount(), 0);
    std::unordered_map<std::string, std::string> options;
    ASSERT_TRUE(ArkTSFunctionOptions(StaticCast<FuncDecl&>(*file->decls.front()), options));
    EXPECT_EQ(options.at("foreignName"), "quote\"name");
    auto generated = ArkTSForeignWrapperSource({}, "String", 0, "__locals_");
    EXPECT_EQ(generated.find("quote"), std::string::npos);
    EXPECT_NE(generated.find("__name__"), std::string::npos);
    EXPECT_NE(generated.find(": JSContext ="), std::string::npos);
}

TEST_F(ArkTSEntryTest, ForeignClassificationNeverRegistersAnEntry)
{
    Package package;
    package.files.emplace_back(Parse(R"(import ohos.ark_interop.*

@ArkTS[module: "actual"]
foreign func echo(value: String): String
)"));
    auto file = package.files.front().get();
    auto& function = StaticCast<FuncDecl&>(*file->decls.front());
    EXPECT_TRUE(IsArkTSForeignFunction(function));
    EXPECT_FALSE(IsArkTSEntry(function));
    PrepareTypeCheck(package, diag, sm);
    EXPECT_TRUE(function.TestAttr(Attribute::ARKTS_FUNCTION_WRAPPER));
    EXPECT_FALSE(function.TestAnyAttr(Attribute::FOREIGN, Attribute::C, Attribute::NO_MANGLE));
    EXPECT_FALSE(IsArkTSEntry(function));
    EXPECT_EQ(file->decls.size(), 1);
}

TEST_F(ArkTSEntryTest, ForeignParsingDoesNotInjectSupportImports)
{
    auto file = Parse(R"(import ohos.ark_interop.*

@ArkTS[module: "actual"]
foreign func echo(value: UInt64): UInt64
)");
    ASSERT_EQ(diag.GetErrorCount(), 0);
    ASSERT_EQ(file->imports.size(), 1);
    EXPECT_EQ(file->imports.front()->content.GetImportedPackageName(), "ohos.ark_interop");
    EXPECT_FALSE(file->imports.front()->TestAttr(Attribute::COMPILER_ADD));
    EXPECT_FALSE(file->imports.front()->TestAttr(Attribute::IMPLICIT_ADD));
}

TEST_F(ArkTSEntryTest, ForeignWithoutExplicitSDKDoesNotInjectAnyDependency)
{
    const std::string source = R"(@ArkTS[module: "actual"]
foreign func echo(value: UInt64): UInt64
)";
    auto parsed = Parse(source);
    auto scanned = Scan(source);
    ASSERT_EQ(diag.GetErrorCount(), 0);
    EXPECT_TRUE(parsed->imports.empty());
    EXPECT_TRUE(scanned->imports.empty());
}

TEST_F(ArkTSEntryTest, ForeignDependencyScanMatchesExplicitLangImport)
{
    const std::string source = R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
foreign func echo(value: UInt64): UInt64
)";
    auto parsed = Parse(source);
    auto scanned = Scan(source);
    ASSERT_EQ(diag.GetErrorCount(), 0);
    ASSERT_EQ(parsed->imports.size(), 1);
    ASSERT_EQ(scanned->imports.size(), parsed->imports.size());
    EXPECT_EQ(parsed->imports.front()->content.GetImportedPackageName(), "arkts.lang");
    for (size_t i = 0; i < parsed->imports.size(); ++i) {
        EXPECT_EQ(parsed->imports[i]->content.GetImportedPackageName(),
            scanned->imports[i]->content.GetImportedPackageName());
        EXPECT_EQ(parsed->imports[i]->TestAttr(Attribute::COMPILER_ADD),
            scanned->imports[i]->TestAttr(Attribute::COMPILER_ADD));
        EXPECT_FALSE(scanned->imports[i]->TestAttr(Attribute::IMPLICIT_ADD));
    }
}

TEST_F(ArkTSEntryTest, ExplicitSDKWithStringOrCommentDoesNotInjectSupport)
{
    auto file = Scan(R"(import ohos.ark_interop.*

// @ArkTS foreign func ignored(): Unit
let text = "@ArkTS foreign func ignored(): Unit"
)");
    ASSERT_EQ(diag.GetErrorCount(), 0);
    ASSERT_EQ(file->imports.size(), 1);
    EXPECT_EQ(file->imports.front()->content.GetImportedPackageName(), "ohos.ark_interop");
}

TEST_F(ArkTSEntryTest, DependencyScanIgnoresOtherAnnotationTargetsAndNestedFunctions)
{
    auto file = Scan(R"(import ohos.ark_interop.*

@ArkTS
class FutureClass {
    func method(): Unit {}
}

func ordinary(): Unit {
    @ArkTS
    func nested(): Unit {}
}
)");
    ASSERT_EQ(diag.GetErrorCount(), 0);
    ASSERT_EQ(file->imports.size(), 1);
}

TEST_F(ArkTSEntryTest, AlreadyImportedNumericSupportIsNotDuplicated)
{
    auto file = Parse(R"(import ohos.ark_interop.*
import std.math.numeric.BigInt

@ArkTS[module: "actual"]
foreign func echo(value: BigInt): BigInt
)");
    ASSERT_EQ(diag.GetErrorCount(), 0);
    ASSERT_EQ(file->imports.size(), 2);
    EXPECT_EQ(std::count_if(file->imports.begin(), file->imports.end(),
                  [](const auto& imported) {
                      return imported->content.GetImportedPackageName() == "std.math.numeric.BigInt";
                  }),
        1);
}

TEST_F(ArkTSEntryTest, ForeignLocalPrefixAvoidsSourceParameterNames)
{
    auto file = Parse(R"(@ArkTS[module: "actual", namespace: "api.nested"]
foreign func echo(
    __cj_arkts_context: Int32,
    __cj_arkts_module: Int32,
    __cj_arkts_namespace_0: Int32,
    __cj_arkts_result: Int32,
    __cj_arkts_0_context: Int32,
    __cj_arkts_1_module: Int32,
    __cj_arkts_current: Int32,
    __argument_0: Int32
): Int32
)");
    ASSERT_EQ(diag.GetErrorCount(), 0);
    auto& fd = StaticCast<FuncDecl&>(*file->decls.front());
    std::vector<Ptr<FuncParam>> params;
    for (auto& param : fd.funcBody->paramLists.front()->params) {
        params.emplace_back(param.get());
    }
    auto prefix = ArkTSForeignLocalPrefix(params);
    EXPECT_EQ(prefix, "__cj_arkts_2_");
    for (auto param : params) {
        EXPECT_NE(param->identifier.Val().compare(0, prefix.size(), prefix), 0);
    }
    EXPECT_EQ(ArkTSForeignLocalPrefix({}), "__cj_arkts_");
    auto body = Parse(ArkTSForeignWrapperSource(std::vector<std::string>(params.size(), "Int32"), "Int32", 2, prefix));
    ASSERT_NE(body, nullptr);
    EXPECT_EQ(diag.GetErrorCount(), 0);
}

TEST_F(ArkTSEntryTest, LangFacadeUsesOrdinaryPublicImports)
{
    auto file = Parse(R"(package arkts.lang

public import ohos.ark_interop.*
public import cj_arkts_context_v1.*
public import std.math.numeric.BigInt
)");
    ASSERT_EQ(diag.GetErrorCount(), 0);
    ASSERT_EQ(file->imports.size(), 3);
    const std::vector<std::string> packages{"ohos.ark_interop", "cj_arkts_context_v1", "std.math.numeric.BigInt"};
    for (size_t i = 0; i < packages.size(); ++i) {
        EXPECT_EQ(file->imports[i]->content.GetImportedPackageName(), packages[i]);
        EXPECT_TRUE(file->imports[i]->TestAttr(Attribute::PUBLIC));
        EXPECT_FALSE(file->imports[i]->TestAttr(Attribute::COMPILER_ADD));
        EXPECT_FALSE(file->imports[i]->TestAttr(Attribute::IMPLICIT_ADD));
    }
}

TEST_F(ArkTSEntryTest, AttributeNamesRemainComplete)
{
    AttributePack ordinary;
    ordinary.SetAttr(Attribute::PUBLIC, true);
    EXPECT_EQ(ordinary.ToString(), "[PUBLIC]");
    AttributePack lowered;
    lowered.SetAttr(Attribute::ARKTS_FUNCTION_WRAPPER, true);
    EXPECT_EQ(lowered.ToString(), "[ARKTS_FUNCTION_WRAPPER]");
}

TEST_F(ArkTSEntryTest, ArkTSRejectsConflictingForeignABIAnnotations)
{
    const std::vector<std::string> sources{
        "@C @ArkTS[module: \"m\"] foreign func echo(v: Int32): Int32",
        "@ArkTS[module: \"m\"] @C foreign func echo(v: Int32): Int32",
        "@C foreign { @ArkTS[module: \"m\"] func echo(v: Int32): Int32 }",
        "@ArkTS[module: \"m\"] foreign { @C func echo(v: Int32): Int32 }"};
    for (const auto& code : sources) {
        SourceManager sourceManager;
        DiagnosticEngine diagnostics;
        diagnostics.SetSourceManager(&sourceManager);
        Parser parser(code, diagnostics, sourceManager);
        auto file = parser.ParseTopLevel();
        diagnostics.EmitCategoryDiagnostics(DiagCategory::PARSE);
        ASSERT_NE(file, nullptr);
        EXPECT_GT(diagnostics.GetErrorCount(), 0) << code;
    }
}

TEST_F(ArkTSEntryTest, ForeignNamespaceRejectsEmptySegments)
{
    for (const auto& ns : {".api", "api.", "api..nested"}) {
        auto file = Parse(std::string("@ArkTS[namespace: \"") + ns + "\"] foreign func echo(): Unit");
        ASSERT_EQ(diag.GetErrorCount(), 0);
        std::unordered_map<std::string, std::string> options;
        EXPECT_FALSE(ArkTSFunctionOptions(StaticCast<FuncDecl&>(*file->decls.front()), options));
    }
    auto file = Parse(R"(@ArkTS[namespace: "bad..default"]
foreign {
    @ArkTS[namespace: "api.nested"]
    func echo(): Unit
}
)");
    ASSERT_EQ(diag.GetErrorCount(), 0);
    std::unordered_map<std::string, std::string> options;
    EXPECT_TRUE(ArkTSFunctionOptions(StaticCast<FuncDecl&>(*file->decls.front()), options));
    EXPECT_EQ(options.at("namespace"), "api.nested");
}

TEST_F(ArkTSEntryTest, GeneratedEntryRetainsSourceTargetForSupportName)
{
    Package pkg;
    pkg.fullPackageName = "entry";
    pkg.files.emplace_back(Parse(R"(@ArkTS
public func __cj_arkts_enter(): Unit {}
)"));
    auto file = pkg.files.front().get();
    Walker(file, [file](Ptr<Node> node) {
        node->curFile = file;
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    auto entry = StaticCast<FuncDecl*>(file->decls.front().get());
    PrepareTypeCheck(pkg, diag, sm);
    ASSERT_EQ(diag.GetErrorCount(), 0);
    ASSERT_EQ(file->decls.size(), 3);
    size_t sourceCalls = 0;
    size_t supportCalls = 0;
    Walker(file->decls[1].get(), [&](Ptr<Node> node) {
        if (auto ref = DynamicCast<RefExpr*>(node); ref && ref->ref.identifier == entry->identifier) {
            if (ref->ref.target == entry) {
                ++sourceCalls;
            } else if (!ref->ref.target) {
                ++supportCalls;
            }
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    EXPECT_EQ(sourceCalls, 1);
    EXPECT_EQ(supportCalls, 1);
}
