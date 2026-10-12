// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "../../src/Sema/NativeFFI/ArkTS/ArkTSInterop.h"
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
#include "gtest/gtest.h"

#include <utility>

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

TEST_F(ArkTSEntryTest, RegistrationCallsOriginalWithoutContextProvider)
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
    EXPECT_EQ(
        StaticCast<RefExpr&>(*StaticCast<CallExpr&>(*body.body[0]).baseFunc).ref.identifier, "__cj_arkts_original");
    EXPECT_EQ(body.body[1]->astKind, ASTKind::RETURN_EXPR);
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
    pkg.files.emplace_back(Parse(R"(import ohos.ark_interop.*

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
    EXPECT_EQ(file->imports.front()->content.GetPrefixPath(), "ohos.ark_interop");
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
