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

    // Parse without emitting: the stored PARSE diagnostics stay inspectable
    // through diag.GetCategoryDiagnostic. The Lexer keeps a raw pointer into
    // the input buffer, so the string passed here must outlive the parser;
    // routing the literal through this helper keeps the temporary alive for
    // the whole call instead of ending it right after the Parser declaration.
    OwnedPtr<File> ParseKeepingDiagnostics(const std::string& code, bool scanDepPkg = false)
    {
        diag.SetSourceManager(&sm);
        GlobalOptions options;
        options.scanDepPkg = scanDepPkg;
        Parser parser(code, diag, sm);
        parser.SetCompileOptions(options);
        return parser.ParseTopLevel();
    }
};

// Description helpers for the generated-form checks below; they mirror the
// shape helpers of ArkTSTypeMappingTest -- with the scalar-name overload gone,
// every wrapper-source call site builds descriptions.
static ArkTSTypeDesc ArkTSScalarDesc(const std::string& name)
{
    return {ArkTSTypeDesc::Kind::SCALAR, name, {}};
}

static ArkTSTypeDesc ArkTSWrappedDesc(ArkTSTypeDesc::Kind kind, ArkTSTypeDesc inner)
{
    return {kind, {}, {std::move(inner)}};
}

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
    // The REST_PARAM annotation kind sits directly after ARKTS and UNKNOWN
    // stays the trailing sentinel, so the old "ARKTS + 1 == UNKNOWN"
    // invariant now reads as the two adjacent steps below.
    EXPECT_EQ(static_cast<int>(AnnotationKind::ARKTS) + 1, static_cast<int>(AnnotationKind::REST_PARAM));
    EXPECT_EQ(static_cast<int>(AnnotationKind::REST_PARAM) + 1, static_cast<int>(AnnotationKind::UNKNOWN));
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
    // The context ABI call is package-qualified: cj_arkts_context_v1.__cj_arkts_restore.
    auto& restoreCallee = StaticCast<MemberAccess&>(*restored.baseFunc);
    EXPECT_EQ(restoreCallee.field.Val(), "__cj_arkts_restore");
    auto& restoreBase = StaticCast<RefExpr&>(*restoreCallee.baseExpr);
    EXPECT_EQ(restoreBase.ref.identifier.Val(), "cj_arkts_context_v1");
    // The saved context comes from the qualified current-context call too.
    auto& previousInit = StaticCast<VarDecl&>(*body.body[0]);
    auto& enter = StaticCast<CallExpr&>(*previousInit.initializer);
    auto& enterCallee = StaticCast<MemberAccess&>(*enter.baseFunc);
    EXPECT_EQ(enterCallee.field.Val(), "__cj_arkts_enter");
    auto& enterBase = StaticCast<RefExpr&>(*enterCallee.baseExpr);
    EXPECT_EQ(enterBase.ref.identifier.Val(), "cj_arkts_context_v1");
    EXPECT_EQ(originalCalls, 1);
}

TEST_F(ArkTSEntryTest, DependencyScanPreservesExplicitInteropImport)
{
    auto file = ParseKeepingDiagnostics(R"(package entry

import ohos.ark_interop.*

@ArkTS
public func run(): Unit {}
)", true);
    ASSERT_EQ(diag.GetErrorCount(), 0);
    ASSERT_EQ(file->imports.size(), 1);
    EXPECT_EQ(file->imports[0]->content.GetImportedPackageName(), "ohos.ark_interop");
    EXPECT_FALSE(file->imports[0]->TestAttr(Attribute::COMPILER_ADD));
    EXPECT_FALSE(file->imports[0]->TestAttr(Attribute::IMPLICIT_ADD));
}

TEST_F(ArkTSEntryTest, DependencyScanDoesNotTreatStringOrCommentAsAnnotation)
{
    auto file = ParseKeepingDiagnostics(R"(package entry
// @ArkTS
let text = "@ArkTS"
)", true);
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
    auto file = ParseKeepingDiagnostics(R"(package entry

@ArkTS
public func run(): Unit {}
)", true);
    ASSERT_EQ(diag.GetErrorCount(), 0);
    EXPECT_TRUE(file->imports.empty());
}

TEST_F(ArkTSEntryTest, ExplicitImportWithoutAnnotationRemainsDependency)
{
    auto file = ParseKeepingDiagnostics(R"(package entry

import ohos.ark_interop.*

public func helper(): Unit {}
)", true);
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

// A declaration that carries @RestParam must be hashable: the incremental
// hasher has two exhaustive AnnotationKind switches, so an unhandled kind used
// to abort under the assertion build. A decl-level annotation participates in
// the body hash only (the generated body changes, the Cangjie signature does
// not), like the other ArkTS annotations; a param-level annotation follows the
// same rule through the enclosing function's body hash.
TEST_F(ArkTSEntryTest, HasherHandlesRestParamAnnotation)
{
    // Decl-level shape: the annotation is reported as misplaced by the parser
    // but stays on the AST, so every hash path must still accept it.
    auto file = ParseKeepingDiagnostics(R"(@RestParam
type Alias = Int64
)");
    ASSERT_EQ(file->decls.size(), 1);
    auto& alias = *file->decls.front();
    ASSERT_EQ(alias.annotations.size(), 1);
    EXPECT_EQ(alias.annotations.front()->kind, AnnotationKind::REST_PARAM);
    const auto withAnnotation = ASTHasher::SigHash(alias);
    const auto withAnnotationBody = ASTHasher::BodyHash(alias, {false, false});
    (void)ASTHasher::SrcUseHash(alias);
    alias.annotations.clear();
    EXPECT_NE(ASTHasher::SigHash(alias), withAnnotation);
    EXPECT_NE(ASTHasher::BodyHash(alias, {false, false}), withAnnotationBody);

    // Param-level shape on an @ArkTS foreign signature: every hash path
    // completes without asserting, and the annotation changes the enclosing
    // function's body hash while leaving its signature hash alone - it only
    // drives generated wrapper code.
    auto function = ParseKeepingDiagnostics(R"(@ArkTS[module: "actual"]
foreign func sum(base: Float64, @RestParam nums: Array<Float64>): Float64
)");
    ASSERT_EQ(function->decls.size(), 1);
    auto& sum = StaticCast<FuncDecl&>(*function->decls.front());
    ASSERT_EQ(sum.funcBody->paramLists[0]->params.size(), 2);
    ASSERT_EQ(sum.funcBody->paramLists[0]->params[1]->annotations.size(), 1);
    EXPECT_EQ(sum.funcBody->paramLists[0]->params[1]->annotations.front()->kind, AnnotationKind::REST_PARAM);
    auto plainFunction = ParseKeepingDiagnostics(R"(@ArkTS[module: "actual"]
foreign func sum(base: Float64, nums: Array<Float64>): Float64
)");
    ASSERT_EQ(plainFunction->decls.size(), 1);
    auto& plainSum = StaticCast<FuncDecl&>(*plainFunction->decls.front());
    EXPECT_EQ(ASTHasher::SigHash(sum), ASTHasher::SigHash(plainSum));
    EXPECT_NE(ASTHasher::BodyHash(sum, {false, false}), ASTHasher::BodyHash(plainSum, {false, false}));
    EXPECT_EQ(ASTHasher::SrcUseHash(sum), ASTHasher::SrcUseHash(plainSum));
}

// Incremental compilation must notice adding or removing a parameter-level
// @RestParam: the generated wrapper body changes even though the Cangjie
// signature does not. Two otherwise-identical functions differ in the body
// hash only, and only while the annotation is present.
TEST_F(ArkTSEntryTest, RestParamAnnotationChangesOnlyTheBodyHashOfAFunction)
{
    auto file = Parse(R"(@ArkTS[module: "actual"]
foreign func sum(base: Float64, @RestParam nums: Array<Float64>): Float64
@ArkTS[module: "actual"]
foreign func plain(base: Float64, nums: Array<Float64>): Float64
)");
    ASSERT_EQ(diag.GetErrorCount(), 0);
    ASSERT_EQ(file->decls.size(), 2);
    auto& sum = StaticCast<FuncDecl&>(*file->decls[0]);
    auto& plain = StaticCast<FuncDecl&>(*file->decls[1]);
    EXPECT_EQ(ASTHasher::SigHash(sum), ASTHasher::SigHash(plain));
    EXPECT_NE(ASTHasher::BodyHash(sum, {false, false}), ASTHasher::BodyHash(plain, {false, false}));

    auto& sumParams = sum.funcBody->paramLists[0]->params;
    ASSERT_EQ(sumParams.size(), 2);
    ASSERT_EQ(sumParams[1]->annotations.size(), 1);
    sumParams[1]->annotations.clear();
    EXPECT_EQ(ASTHasher::BodyHash(sum, {false, false}), ASTHasher::BodyHash(plain, {false, false}));
}

// A parameter may carry @RestParam at most once; the duplicated-annotation
// check is the shared parser one and names the offending annotation.
TEST_F(ArkTSEntryTest, DuplicateRestParamAnnotationsOnOneParameterAreRejected)
{
    auto file = ParseKeepingDiagnostics(R"(@ArkTS[module: "actual"]
foreign func sum(base: Float64, @RestParam @RestParam nums: Array<Float64>): Float64
)");
    ASSERT_NE(file, nullptr);
    const auto diagnostics = diag.GetCategoryDiagnostic(DiagCategory::PARSE);
    EXPECT_EQ(std::count_if(diagnostics.begin(), diagnostics.end(),
                  [](const auto& diagnostic) {
                      return diagnostic.rKind == DiagKindRefactor::parse_duplicated_annotation;
                  }),
        1);
    ASSERT_EQ(file->decls.size(), 1);
    auto& params = StaticCast<FuncDecl&>(*file->decls.front()).funcBody->paramLists[0]->params;
    ASSERT_EQ(params.size(), 2);
    EXPECT_EQ(params[1]->annotations.size(), 2);
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
        ASSERT_TRUE(ArkTSDeclOptions(*restored, merged));
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
    ASSERT_TRUE(ArkTSDeclOptions(function, options));
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
        ASSERT_TRUE(ArkTSDeclOptions(StaticCast<FuncDecl&>(*file->decls[i]), options));
        EXPECT_EQ(options.at("module"), expected[i][0]);
        EXPECT_EQ(options.at("namespace"), expected[i][1]);
        EXPECT_EQ(options.at("foreignName"), expected[i][2]);
    }
}

// A name repeated inside one @ArkTS layer is rejected by the option fold even
// when both values are equal. The parser adds no diagnostic for it: the fold is
// the single owner of every option rule, so one mistake is reported once (by
// the Sema diagnostic that consumes this false result).
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
            EXPECT_EQ(diagnostics.GetErrorCount(), 0);
            std::unordered_map<std::string, std::string> options;
            EXPECT_FALSE(ArkTSDeclOptions(StaticCast<FuncDecl&>(*file->decls.front()), options));
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

TEST_F(ArkTSEntryTest, DefaultModuleIsThePackageNameVerbatim)
{
    // An omitted module option takes the Cangjie package name as the module
    // name, verbatim - no ETS source-hierarchy mapping, and a single-segment
    // package name is a legal module name. Only an empty package name stays
    // empty (a degenerate compilation unit that lowering refuses through the
    // wrapper-unavailable diagnostic).
    EXPECT_EQ(ArkTSDefaultModule("entry.ForeignFixtures"), "entry.ForeignFixtures");
    EXPECT_EQ(ArkTSDefaultModule("library.utils.Ping_2"), "library.utils.Ping_2");
    EXPECT_EQ(ArkTSDefaultModule("entry"), "entry");
    EXPECT_EQ(ArkTSDefaultModule("entry..Fixture"), "entry..Fixture");
    EXPECT_EQ(ArkTSDefaultModule("entry.Fixture."), "entry.Fixture.");
    EXPECT_TRUE(ArkTSDefaultModule("").empty());
}

TEST_F(ArkTSEntryTest, ScalarBodiesAreParseableForEverySupportedType)
{
    const std::vector<std::string> types{"Int8", "Int16", "Int32", "UInt8", "UInt16", "UInt32", "Float16", "Float32",
        "Float64", "Rune", "Int64", "UInt64", "BigInt", "Bool", "String", "Unit"};
    for (const auto& type : types) {
        auto generated = Parse(ArkTSForeignWrapperSource({ArkTSScalarDesc(type)}, ArkTSScalarDesc(type), 2,
            "__locals_"));
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
    Parse(ArkTSForeignWrapperSource({}, ArkTSScalarDesc("Nothing"), 0, "__locals_"));
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
        auto code = ArkTSForeignWrapperSource({ArkTSScalarDesc(type)}, ArkTSScalarDesc(type), 0, "__locals_");
        EXPECT_NE(code.find(".bigint("), std::string::npos);
        EXPECT_NE(code.find(".asBigInt().toBigInt()"), std::string::npos);
        EXPECT_EQ(code.find(".number("), std::string::npos);
        EXPECT_EQ(code.find(".toNumber()"), std::string::npos);
        EXPECT_EQ(code.find(".parse("), std::string::npos);
    }
    auto unsignedCode = ArkTSForeignWrapperSource({ArkTSScalarDesc("UInt64")}, ArkTSScalarDesc("UInt64"), 0,
        "__locals_");
    EXPECT_NE(unsignedCode.find("BigInt(__argument_0)"), std::string::npos);
    EXPECT_NE(unsignedCode.find(".toUInt64()"), std::string::npos);
    auto signedCode = ArkTSForeignWrapperSource({}, ArkTSScalarDesc("Int64"), 0, "__locals_");
    EXPECT_NE(signedCode.find(".toInt64()"), std::string::npos);
}

TEST_F(ArkTSEntryTest, ModuleAndNamesAreBoundValuesInsteadOfSourceInterpolation)
{
    auto file = Parse(R"(@ArkTS[module: "actual", foreignName: "quote\"name"]
foreign func echo(): String
)");
    ASSERT_EQ(diag.GetErrorCount(), 0);
    std::unordered_map<std::string, std::string> options;
    ASSERT_TRUE(ArkTSDeclOptions(StaticCast<FuncDecl&>(*file->decls.front()), options));
    EXPECT_EQ(options.at("foreignName"), "quote\"name");
    auto generated = ArkTSForeignWrapperSource({}, ArkTSScalarDesc("String"), 0, "__locals_");
    EXPECT_EQ(generated.find("quote"), std::string::npos);
    EXPECT_NE(generated.find("__name__"), std::string::npos);
    EXPECT_NE(generated.find(": ark_interop.JSContext ="), std::string::npos);
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
    auto body = Parse(ArkTSForeignWrapperSource(std::vector<ArkTSTypeDesc>(params.size(), ArkTSScalarDesc("Int32")),
        ArkTSScalarDesc("Int32"), 2, prefix));
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
        EXPECT_FALSE(ArkTSDeclOptions(StaticCast<FuncDecl&>(*file->decls.front()), options));
    }
    auto file = Parse(R"(@ArkTS[namespace: "bad..default"]
foreign {
    @ArkTS[namespace: "api.nested"]
    func echo(): Unit
}
)");
    ASSERT_EQ(diag.GetErrorCount(), 0);
    std::unordered_map<std::string, std::string> options;
    EXPECT_TRUE(ArkTSDeclOptions(StaticCast<FuncDecl&>(*file->decls.front()), options));
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
        if (auto ref = DynamicCast<RefExpr*>(node);
            ref && ref->ref.identifier == entry->identifier && ref->ref.target == entry) {
            ++sourceCalls;
        }
        // The context ABI call is package-qualified now:
        // cj_arkts_context_v1.__cj_arkts_enter, so it is a MemberAccess.
        if (auto access = DynamicCast<MemberAccess*>(node); access && access->field == "__cj_arkts_enter") {
            auto base = DynamicCast<RefExpr*>(access->baseExpr.get());
            if (base && base->ref.identifier.Val() == "cj_arkts_context_v1") {
                ++supportCalls;
            }
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    EXPECT_EQ(sourceCalls, 1);
    EXPECT_EQ(supportCalls, 1);
}

// ---- optional/rest parameters and receiver selection ----
static size_t ArkTSCountMemberAccess(Ptr<File> file, const std::string& field)
{
    size_t count = 0;
    Walker(file.get(), [&](Ptr<Node> node) {
        if (auto access = DynamicCast<MemberAccess*>(node)) {
            count += access->field == field ? 1 : 0;
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    return count;
}

TEST_F(ArkTSEntryTest, OptionalNamedParameterWithDefaultParsesOnForeign)
{
    // An ArkTS optional parameter maps to a named CJ parameter
    // `x!: Option<T'> = None`; the named-with-default form parses through the
    // generic parameter machinery, behind the positional parameters.
    auto file = Parse(R"(@ArkTS[module: "actual"]
foreign func greet(other: Int32, prefix!: Option<String> = None): String
)");
    ASSERT_EQ(diag.GetErrorCount(), 0);
    ASSERT_EQ(file->decls.size(), 1);
    auto& fd = StaticCast<FuncDecl&>(*file->decls.front());
    auto& params = fd.funcBody->paramLists.front()->params;
    ASSERT_EQ(params.size(), 2);
    EXPECT_FALSE(params[0]->isNamedParam);
    EXPECT_TRUE(params[1]->isNamedParam);
    ASSERT_NE(params[1]->assignment, nullptr);
}

TEST_F(ArkTSEntryTest, OptionalParameterWrapperElidesTrailingNone)
{
    // The wrapper converts the whole parameter as Option<T'>: Option.Some
    // converts the element, Option.None would cross as ctx.undefined(), but
    // the single optional parameter is forwarded only when it is Some. A None
    // therefore produces an empty argument list instead of undefined.
    auto code = ArkTSForeignWrapperSource(
        {ArkTSWrappedDesc(ArkTSTypeDesc::Kind::OPTIONAL, ArkTSScalarDesc("String"))}, ArkTSScalarDesc("String"), 0,
        "__locals_");
    EXPECT_NE(code.find("case Option.Some("), std::string::npos);
    EXPECT_NE(code.find("case Option.None => __locals_context.undefined().toJSValue()"), std::string::npos);
    // Run-time gate: Some -> last = 0 (one argument), None -> last = -1 (no
    // argument); the converted local is selected only up to "last".
    EXPECT_NE(code.find("let __locals_last_0 = if (__argument_0.isSome()) {\n        0\n    } else {\n        -1\n    }"),
        std::string::npos);
    EXPECT_NE(
        code.find("Array<ark_interop.JSValue>(__locals_last_0 + 1, { __locals_index_0 =>"), std::string::npos);
    auto parsed = Parse(code);
    ASSERT_EQ(diag.GetErrorCount(), 0);
    EXPECT_EQ(ArkTSCountMemberAccess(parsed.get(), "callMethod"), 1);
}

TEST_F(ArkTSEntryTest, RestParamAnnotationIsRecognizedOnTheFinalParameter)
{
    auto file = Parse(R"(@ArkTS[module: "actual"]
foreign func sum(base: Float64, @RestParam nums: Array<Float64>): Float64
)");
    ASSERT_EQ(diag.GetErrorCount(), 0);
    ASSERT_EQ(file->decls.size(), 1);
    auto& fd = StaticCast<FuncDecl&>(*file->decls.front());
    auto& params = fd.funcBody->paramLists.front()->params;
    ASSERT_EQ(params.size(), 2);
    EXPECT_TRUE(params[0]->annotations.empty());
    ASSERT_EQ(params[1]->annotations.size(), 1);
    EXPECT_EQ(params[1]->annotations.front()->kind, AnnotationKind::REST_PARAM);
    EXPECT_EQ(AnnotationKindToString(AnnotationKind::REST_PARAM), "RestParam");
}

TEST_F(ArkTSEntryTest, RestParamWrapperSpreadsCopyItemsIntoCallArguments)
{
    // With Array<T'> as the rest kind: each element is converted individually
    // and becomes one callMethod argument; nothing is packaged as a single
    // array argument, and the call array is sized by the fixed count plus the
    // rest length at run time. Both arrays use the block-lambda constructor
    // (a trailing closure would stay an untyped TRAIL_CLOSE_EXPR because the
    // generated body is parsed after the trailing-closure desugar pass).
    auto code = ArkTSForeignWrapperSource({ArkTSScalarDesc("Float64"),
                                              ArkTSWrappedDesc(ArkTSTypeDesc::Kind::ARRAY_COPY, ArkTSScalarDesc("Float64"))},
        ArkTSScalarDesc("Float64"), 0, "__locals_", 1);
    EXPECT_NE(code.find("Array<ark_interop.JSValue>(__argument_1.size, { __locals_index_0 =>"), std::string::npos);
    EXPECT_NE(code.find("Array<ark_interop.JSValue>(1 + __argument_1.size, { __locals_index_1 =>"), std::string::npos);
    EXPECT_NE(code.find("case _ => __locals_rest_0[__locals_index_1 - 1]"), std::string::npos);
    EXPECT_EQ(code.find(".array("), std::string::npos);
    auto parsed = Parse(code);
    ASSERT_EQ(diag.GetErrorCount(), 0);
    EXPECT_EQ(ArkTSCountMemberAccess(parsed.get(), "callMethod"), 1);
    EXPECT_EQ(ArkTSCountMemberAccess(parsed.get(), "requireArkModule"), 1);
}

TEST_F(ArkTSEntryTest, RestParamWrapperWithModuleReceiverSpreadsCopyItems)
{
    // Array<T'> is the only generable rest kind: an ArkTSArray<T'> rest
    // position classifies as unsupported and is rejected by the Sema rest
    // diagnostic, so the generator has no branch for it. Each copy element is
    // converted individually and becomes one callMethod argument. The MODULE
    // receiver - an explicit module, or a package name other than the
    // arkts.lang TS preset - reaches its target through requireArkModule.
    auto code = ArkTSForeignWrapperSource({ArkTSScalarDesc("String"),
                                              ArkTSWrappedDesc(ArkTSTypeDesc::Kind::ARRAY_COPY, ArkTSScalarDesc("Bool"))},
        ArkTSScalarDesc("Unit"), 1, "__locals_", 1);
    EXPECT_NE(code.find("Array<ark_interop.JSValue>(__argument_1.size, { __locals_index_0 =>"), std::string::npos);
    EXPECT_NE(code.find("Array<ark_interop.JSValue>(1 + __argument_1.size, { __locals_index_1 =>"), std::string::npos);
    EXPECT_NE(code.find(".boolean("), std::string::npos);
    EXPECT_NE(code.find("requireArkModule(\"__module__\")"), std::string::npos);
    auto parsed = Parse(code);
    ASSERT_EQ(diag.GetErrorCount(), 0);
    EXPECT_EQ(ArkTSCountMemberAccess(parsed.get(), "callMethod"), 1);
    EXPECT_EQ(ArkTSCountMemberAccess(parsed.get(), "requireArkModule"), 1);
}

TEST_F(ArkTSEntryTest, RestParamRequiresAFinalArrayTypedPosition)
{
    // Generator contract: the rest index must be the last position and carry
    // Array<T'> (an ArkTSArray<T'> rest position is rejected upstream by the
    // Sema rest diagnostic); anything else produces no wrapper rather than a
    // mis-mapped call.
    auto scalar = ArkTSScalarDesc("Int32");
    auto arrayRest = ArkTSWrappedDesc(ArkTSTypeDesc::Kind::ARRAY_COPY, ArkTSScalarDesc("Int32"));
    EXPECT_TRUE(
        ArkTSForeignWrapperSource({arrayRest, scalar}, ArkTSScalarDesc("Unit"), 0, "__locals_", 0).empty());
    EXPECT_TRUE(ArkTSForeignWrapperSource({scalar}, ArkTSScalarDesc("Unit"), 0, "__locals_", 0).empty());
}

TEST_F(ArkTSEntryTest, RestParamOnANonFinalParameterIsRejected)
{
    const auto errorsBefore = diag.GetErrorCount();
    Parse(R"(@ArkTS[module: "actual"]
foreign func sum(@RestParam first: Array<Float64>, second: Float64): Float64
)");
    EXPECT_GT(diag.GetErrorCount(), errorsBefore);
}

TEST_F(ArkTSEntryTest, RestParamOnANamedParameterIsRejected)
{
    const auto errorsBefore = diag.GetErrorCount();
    Parse(R"(@ArkTS[module: "actual"]
foreign func sum(other: Float64, @RestParam rest!: Array<Float64>): Float64
)");
    EXPECT_GT(diag.GetErrorCount(), errorsBefore);
}

TEST_F(ArkTSEntryTest, RestParamOnAnOrdinaryFunctionIsRejected)
{
    const auto errorsBefore = diag.GetErrorCount();
    Parse(R"(func sum(@RestParam nums: Array<Float64>): Float64 {
    0.0
})");
    EXPECT_GT(diag.GetErrorCount(), errorsBefore);
}

TEST_F(ArkTSEntryTest, RestParamOnTheDeclarationItselfIsRejected)
{
    const auto errorsBefore = diag.GetErrorCount();
    Parse(R"(@RestParam
@ArkTS[module: "actual"]
foreign func sum(nums: Array<Float64>): Float64
)");
    EXPECT_GT(diag.GetErrorCount(), errorsBefore);
}

TEST_F(ArkTSEntryTest, RestParamOnAnOrdinaryCForeignFunctionIsRejected)
{
    // @RestParam is an ArkTS-foreign-only shape. An
    // ordinary C foreign declaration (no @ArkTS) never receives a rest
    // wrapper, so the annotation must be diagnosed instead of silently
    // accepted; the declaration itself parses cleanly without it.
    const auto errorsBefore = diag.GetErrorCount();
    Parse(R"(foreign func sum(base: Float64, @RestParam nums: Array<Float64>): Float64
)");
    EXPECT_GT(diag.GetErrorCount(), errorsBefore);
}

TEST_F(ArkTSEntryTest, RestParamOnACForeignBlockMemberIsRejected)
{
    // Block form: the inherited @C annotation reaches the
    // member's annotation list, which must not satisfy the ArkTS-foreign
    // predicate of the placement check.
    const auto errorsBefore = diag.GetErrorCount();
    Parse(R"(@C
foreign {
    func sum(base: Float64, @RestParam nums: Array<Float64>): Float64
}
)");
    EXPECT_GT(diag.GetErrorCount(), errorsBefore);
}

TEST_F(ArkTSEntryTest, RestParamOnANonForeignArkTSFunctionIsRejected)
{
    // An @ArkTS entry (no foreign modifier) is not a rest target either:
    // the tightened predicate requires FOREIGN and @ArkTS together, in line
    // with IsArkTSForeignFunction, which gates lowering.
    const auto errorsBefore = diag.GetErrorCount();
    Parse(R"(@ArkTS
public func sum(@RestParam nums: Array<Float64>): Float64 {
    0.0
}
)");
    EXPECT_GT(diag.GetErrorCount(), errorsBefore);
}

TEST_F(ArkTSEntryTest, RestParamOnAnArkTSForeignBlockMemberIsAccepted)
{
    // Positive guard for the tightened predicate: inside an @ArkTS foreign
    // block the annotation is cloned into the member's declaration
    // annotations, so the final positional parameter stays legal there.
    auto file = Parse(R"(@ArkTS[module: "actual"]
foreign {
    func sum(base: Float64, @RestParam nums: Array<Float64>): Float64
}
)");
    ASSERT_EQ(diag.GetErrorCount(), 0);
    ASSERT_EQ(file->decls.size(), 1);
    auto& fd = StaticCast<FuncDecl&>(*file->decls.front());
    EXPECT_TRUE(IsArkTSForeignFunction(fd));
    auto& params = fd.funcBody->paramLists.front()->params;
    ASSERT_EQ(params.size(), 2);
    ASSERT_EQ(params[1]->annotations.size(), 1);
    EXPECT_EQ(params[1]->annotations.front()->kind, AnnotationKind::REST_PARAM);
}

TEST_F(ArkTSEntryTest, RestParamWithArgumentsIsRejected)
{
    // Red round-1 P2-1: the annotation takes no arguments; the generic
    // argument parser accepts the bracket form, so the rejection is the
    // placement check's own diagnostic.
    const auto errorsBefore = diag.GetErrorCount();
    Parse(R"(@ArkTS[module: "actual"]
foreign func sum(base: Float64, @RestParam[1] nums: Array<Float64>): Float64
)");
    EXPECT_GT(diag.GetErrorCount(), errorsBefore);
}

TEST_F(ArkTSEntryTest, ExplicitModuleKeepsTheModuleReceiver)
{
    // A foreign declaration with an explicit module option keeps the
    // requireArkModule form; the module placeholder is a bound value.
    auto file = Parse(R"(@ArkTS[module: "actual"]
foreign func echo(): String
)");
    ASSERT_EQ(diag.GetErrorCount(), 0);
    std::unordered_map<std::string, std::string> options;
    ASSERT_TRUE(ArkTSDeclOptions(StaticCast<FuncDecl&>(*file->decls.front()), options));
    EXPECT_EQ(options.at("module"), "actual");
    auto code = ArkTSForeignWrapperSource({}, ArkTSScalarDesc("String"), 0, "__locals_");
    EXPECT_NE(code.find("requireArkModule(\"__module__\")"), std::string::npos);
    auto parsed = Parse(code);
    ASSERT_EQ(diag.GetErrorCount(), 0);
    EXPECT_EQ(ArkTSCountMemberAccess(parsed.get(), "requireArkModule"), 1);
}

TEST_F(ArkTSEntryTest, OmittedModuleTakesThePackageNameAsModule)
{
    // No module option - the module name is the Cangjie package name of the
    // declaration, verbatim; a single-segment package name is a legal module
    // name, so lowering emits the requireArkModule form for every package
    // other than arkts.lang (the TS-preset exception selects the global
    // receiver and is pinned by the Sema test of the same name). The inputs
    // and the emission form are checked here; the selection itself lives in
    // lowering.
    auto file = Parse(R"(package entry

@ArkTS
foreign func parseInt(text: String): Float64
)");
    ASSERT_EQ(diag.GetErrorCount(), 0);
    ASSERT_EQ(file->decls.size(), 1);
    std::unordered_map<std::string, std::string> options;
    ASSERT_TRUE(ArkTSDeclOptions(StaticCast<FuncDecl&>(*file->decls.front()), options));
    EXPECT_EQ(options.count("module"), 0u);
    EXPECT_EQ(ArkTSDefaultModule("entry"), "entry");
    auto code = ArkTSForeignWrapperSource({}, ArkTSScalarDesc("Float64"), 0, "__locals_");
    EXPECT_NE(code.find("requireArkModule(\"__module__\")"), std::string::npos);
    EXPECT_NE(code.find("__module__"), std::string::npos);
    auto parsed = Parse(code);
    ASSERT_EQ(diag.GetErrorCount(), 0);
    EXPECT_EQ(ArkTSCountMemberAccess(parsed.get(), "callMethod"), 1);
    EXPECT_EQ(ArkTSCountMemberAccess(parsed.get(), "requireArkModule"), 1);
}

// ---- the arkts.lang package is the TS preset: its foreign declarations
// without an explicit module option take the global object as the receiver, so
// the generator's GLOBAL form emits ctx.global and neither requireArkModule
// nor the module placeholder. ----

TEST_F(ArkTSEntryTest, GlobalReceiverWrapperUsesTheContextGlobalObject)
{
    // The GLOBAL receiver binds the same generated local (prefix +
    // "module") to the context's global object; no requireArkModule call
    // and no __module__ placeholder are emitted, and the call still goes
    // through callMethod on that receiver.
    auto code = ArkTSForeignWrapperSource({}, ArkTSScalarDesc("Float64"), 0, "__locals_",
        ArkTSReceiverKind::GLOBAL);
    EXPECT_NE(code.find("let __locals_module = __locals_context.global"), std::string::npos);
    EXPECT_EQ(code.find("requireArkModule"), std::string::npos);
    EXPECT_EQ(code.find("__module__"), std::string::npos);
    auto parsed = Parse(code);
    ASSERT_EQ(diag.GetErrorCount(), 0);
    EXPECT_EQ(ArkTSCountMemberAccess(parsed.get(), "callMethod"), 1);
    EXPECT_EQ(ArkTSCountMemberAccess(parsed.get(), "requireArkModule"), 0);
    EXPECT_EQ(ArkTSCountMemberAccess(parsed.get(), "global"), 1);
}

TEST_F(ArkTSEntryTest, GlobalReceiverNamespaceChainResolvesThroughGetProperty)
{
    // On the global receiver the namespace chain resolves segment by
    // segment through the same getProperty(...).asObject() shape the module
    // form uses, starting from ctx.global itself.
    auto code = ArkTSForeignWrapperSource({}, ArkTSScalarDesc("Unit"), 2, "__locals_", ArkTSReceiverKind::GLOBAL);
    EXPECT_NE(code.find("let __locals_module = __locals_context.global"), std::string::npos);
    EXPECT_NE(code.find("__locals_module.getProperty(\"__namespace_0__\")"), std::string::npos);
    EXPECT_NE(code.find("__locals_namespace_0.getProperty(\"__namespace_1__\")"), std::string::npos);
    EXPECT_EQ(code.find("requireArkModule"), std::string::npos);
    EXPECT_EQ(code.find("__module__"), std::string::npos);
    auto parsed = Parse(code);
    ASSERT_EQ(diag.GetErrorCount(), 0);
    EXPECT_EQ(ArkTSCountMemberAccess(parsed.get(), "callMethod"), 1);
    EXPECT_EQ(ArkTSCountMemberAccess(parsed.get(), "requireArkModule"), 0);
    EXPECT_EQ(ArkTSCountMemberAccess(parsed.get(), "global"), 1);
}

TEST_F(ArkTSEntryTest, ModuleReceiverWrapperKeepsRequireArkModuleAndNoGlobal)
{
    // The MODULE receiver (the default, and the shape an explicit module
    // option always selects) keeps the requireArkModule form and never touches
    // the context's global object.
    auto code = ArkTSForeignWrapperSource({}, ArkTSScalarDesc("Float64"), 1, "__locals_",
        ArkTSReceiverKind::MODULE);
    EXPECT_NE(code.find("let __locals_module = __locals_context.requireArkModule(\"__module__\")"),
        std::string::npos);
    EXPECT_NE(code.find("__locals_module.getProperty(\"__namespace_0__\")"), std::string::npos);
    EXPECT_EQ(code.find(".global"), std::string::npos);
    auto parsed = Parse(code);
    ASSERT_EQ(diag.GetErrorCount(), 0);
    EXPECT_EQ(ArkTSCountMemberAccess(parsed.get(), "requireArkModule"), 1);
    EXPECT_EQ(ArkTSCountMemberAccess(parsed.get(), "global"), 0);
}

TEST_F(ArkTSEntryTest, RestWrapperHonorsTheReceiverSelection)
{
    // Rest form: the receiver selection is shared by the rest overload, so a
    // GLOBAL rest wrapper spreads its items into individual callMethod
    // arguments on the global object without any module load.
    auto code = ArkTSForeignWrapperSource({ArkTSScalarDesc("Float64"),
                                              ArkTSWrappedDesc(ArkTSTypeDesc::Kind::ARRAY_COPY, ArkTSScalarDesc("Float64"))},
        ArkTSScalarDesc("Float64"), 0, "__locals_", 1, ArkTSReceiverKind::GLOBAL);
    EXPECT_NE(code.find("let __locals_module = __locals_context.global"), std::string::npos);
    EXPECT_NE(code.find("Array<ark_interop.JSValue>(1 + __argument_1.size, { __locals_index_1 =>"), std::string::npos);
    EXPECT_EQ(code.find("requireArkModule"), std::string::npos);
    EXPECT_EQ(code.find("__module__"), std::string::npos);
    auto parsed = Parse(code);
    ASSERT_EQ(diag.GetErrorCount(), 0);
    EXPECT_EQ(ArkTSCountMemberAccess(parsed.get(), "callMethod"), 1);
    EXPECT_EQ(ArkTSCountMemberAccess(parsed.get(), "requireArkModule"), 0);
}

TEST_F(ArkTSEntryTest, NothingParameterPositionsProduceNoWrapper)
{
    // Only the result type may be never: a Nothing parameter,
    // even nested inside a supported composite, yields no wrapper, while a
    // Nothing result keeps the never contract.
    auto never = ArkTSScalarDesc("Nothing");
    auto unit = ArkTSScalarDesc("Unit");
    EXPECT_TRUE(ArkTSForeignWrapperSource({never}, unit, 0, "__locals_").empty());
    EXPECT_TRUE(
        ArkTSForeignWrapperSource({ArkTSWrappedDesc(ArkTSTypeDesc::Kind::OPTIONAL, never)}, unit, 0, "__locals_")
            .empty());
    auto code = ArkTSForeignWrapperSource({}, never, 0, "__locals_");
    EXPECT_NE(code.find("ArkTS never function returned"), std::string::npos);
}

// ---- @RestParam on @ArkTS foreign class/interface members: the annotation is
// accepted on a member of an @ArkTS foreign class or interface, and nothing
// may follow the rest position ----

TEST_F(ArkTSEntryTest, RestParamOnAnArkTSForeignClassMemberIsAccepted)
{
    // A member of an @ArkTS foreign class carries neither the FOREIGN
    // attribute (Sema's DeclAttributeChecker propagates it later) nor the
    // @ArkTS annotation (it stays on the class), so the placement check must
    // resolve the enclosing class through the parser chain. Bodyless member
    // declarations are the designed member spelling.
    auto file = Parse(R"(@ArkTS
foreign class Queue {
    public func push(@RestParam items: Array<Int32>): Unit
}
)");
    ASSERT_EQ(diag.GetErrorCount(), 0);
    ASSERT_EQ(file->decls.size(), 1);
    auto& cd = StaticCast<ClassDecl&>(*file->decls.front());
    EXPECT_TRUE(cd.TestAttr(Attribute::FOREIGN));
    EXPECT_TRUE(cd.HasAnno(AnnotationKind::ARKTS));
    ASSERT_NE(cd.body, nullptr);
    ASSERT_EQ(cd.body->decls.size(), 1);
    auto& fd = StaticCast<FuncDecl&>(*cd.body->decls.front());
    auto& params = fd.funcBody->paramLists.front()->params;
    ASSERT_EQ(params.size(), 1);
    ASSERT_EQ(params.front()->annotations.size(), 1);
    EXPECT_EQ(params.front()->annotations.front()->kind, AnnotationKind::REST_PARAM);
}

TEST_F(ArkTSEntryTest, RestParamOnAnArkTSForeignInterfaceMemberIsAccepted)
{
    // Interface form: a foreign interface is a class-like declaration too, so
    // its members follow the same placement rule.
    auto file = Parse(R"(@ArkTS
foreign interface Listener {
    func onAll(@RestParam events: Array<Int32>): Unit
}
)");
    ASSERT_EQ(diag.GetErrorCount(), 0);
    ASSERT_EQ(file->decls.size(), 1);
    auto& id = StaticCast<InterfaceDecl&>(*file->decls.front());
    EXPECT_TRUE(id.TestAttr(Attribute::FOREIGN));
    EXPECT_TRUE(id.HasAnno(AnnotationKind::ARKTS));
    ASSERT_NE(id.body, nullptr);
    ASSERT_EQ(id.body->decls.size(), 1);
    auto& fd = StaticCast<FuncDecl&>(*id.body->decls.front());
    auto& params = fd.funcBody->paramLists.front()->params;
    ASSERT_EQ(params.size(), 1);
    ASSERT_EQ(params.front()->annotations.size(), 1);
    EXPECT_EQ(params.front()->annotations.front()->kind, AnnotationKind::REST_PARAM);
}

TEST_F(ArkTSEntryTest, RestParamFollowedByNamedParametersIsRejected)
{
    // The rest parameter is only allowed as the last positional parameter and
    // must not be mixed with optional parameters. Cangjie's parameter ordering
    // (DiagNamedParameterAfterUnnamed) puts named parameters behind all
    // positional ones, so a rest parameter followed by a named-with-default
    // parameter is never a legal shape; that mix gets its own dedicated
    // diagnostic, asserted at kind level in
    // RestParamWithNamedParametersReportsTheDedicatedDiagnostic below.
    const auto errorsBefore = diag.GetErrorCount();
    Parse(R"(@ArkTS
foreign {
    func f(@RestParam rest: Array<Int32>, tail!: Option<Int32> = None): Unit
}
)");
    EXPECT_GT(diag.GetErrorCount(), errorsBefore);
}

TEST_F(ArkTSEntryTest, NamedRestParameterItselfReportsTheDedicatedDiagnostic)
{
    // A named rest parameter makes the function contain a named
    // parameter (the rest position itself), so the pre-scan reports the
    // dedicated diagnostic once instead of the generic placement message -
    // the declaration stays rejected either way.
    auto file = ParseKeepingDiagnostics(R"(@ArkTS[module: "actual"]
foreign func sum(other: Float64, @RestParam rest!: Array<Float64>): Float64
)");
    ASSERT_NE(file, nullptr);
    const auto diagnostics = diag.GetCategoryDiagnostic(DiagCategory::PARSE);
    EXPECT_EQ(diagnostics.size(), 1);
    EXPECT_EQ(std::count_if(diagnostics.begin(), diagnostics.end(),
                  [](const auto& diagnostic) {
                      return diagnostic.rKind == DiagKindRefactor::parse_rest_param_function_has_named_params;
                  }),
        1);
    EXPECT_EQ(std::count_if(diagnostics.begin(), diagnostics.end(),
                  [](const auto& diagnostic) {
                      return diagnostic.rKind == DiagKindRefactor::parse_unexpected_anno_on;
                  }),
        0);
}

TEST_F(ArkTSEntryTest, NonArkTSRestWithNamedParametersKeepsTheTargetDiagnostic)
{
    // Ordering guard: the ArkTS-foreign target check stays ahead of the
    // named-parameter pre-scan, so an @RestParam on an ordinary function
    // keeps the not-an-ArkTS-foreign-target diagnostic even when the
    // signature also mixes named parameters into the list.
    auto file = ParseKeepingDiagnostics(R"(func sum(base: Float64, @RestParam nums: Array<Float64>, tail!: Int32): Float64 {
    0.0
})");
    ASSERT_NE(file, nullptr);
    const auto diagnostics = diag.GetCategoryDiagnostic(DiagCategory::PARSE);
    EXPECT_EQ(std::count_if(diagnostics.begin(), diagnostics.end(),
                  [](const auto& diagnostic) {
                      return diagnostic.rKind == DiagKindRefactor::parse_unexpected_anno_on;
                  }),
        1);
    EXPECT_EQ(std::count_if(diagnostics.begin(), diagnostics.end(),
                  [](const auto& diagnostic) {
                      return diagnostic.rKind == DiagKindRefactor::parse_rest_param_function_has_named_params;
                  }),
        0);
}

TEST_F(ArkTSEntryTest, RestParamOnAnOrdinaryCForeignClassMemberIsRejected)
{
    // Guard: the member form must not leak into ordinary C foreign classes.
    // The enclosing class carries FOREIGN but no @ArkTS marker, so the
    // annotation stays diagnosed instead of being silently accepted (such a
    // member never receives a rest wrapper).
    const auto errorsBefore = diag.GetErrorCount();
    Parse(R"(@C
foreign class CQueue {
    public func push(base: Float64, @RestParam nums: Array<Float64>): Float64
}
)");
    EXPECT_GT(diag.GetErrorCount(), errorsBefore);
}

TEST_F(ArkTSEntryTest, RestParamOnANonForeignArkTSClassMemberIsRejected)
{
    // Guard, other side: @ArkTS without foreign does not make the class a rest
    // target; its members keep the ordinary rejection.
    const auto errorsBefore = diag.GetErrorCount();
    Parse(R"(@ArkTS
class FutureClass {
    func method(@RestParam nums: Array<Float64>): Float64 {
        0.0
    }
}
)");
    EXPECT_GT(diag.GetErrorCount(), errorsBefore);
}

// ---- @ArkTS-gated parse relaxation: member-level semantics stay outside the
// parse-level gate, so these cases only pin that the gate itself works ----

TEST_F(ArkTSEntryTest, ArkTSAnnotatedPublicForeignClassParsesWithBodylessMembers)
{
    // With the @ArkTS annotation the access x foreign modifier conflict is
    // lifted and the constructor body requirement is waived, so the stdlib
    // spelling parses verbatim: 'public foreign class' with a generic parameter
    // and bodyless init/func/prop members.
    auto file = Parse(R"(@ArkTS[foreignName: "Array"]
public foreign class ArkTSArray<T> {
    public init(arrayLength: Float64)
    public init(@RestParam items: Array<T>)
    public static func isArray(arg: ArkTSType): Bool
    public prop prototype: ArkTSType
    public prop length: Float64
    public func pop(): Option<T>
}
)");
    ASSERT_EQ(diag.GetErrorCount(), 0);
    ASSERT_EQ(file->decls.size(), 1);
    auto& cd = StaticCast<ClassDecl&>(*file->decls.front());
    EXPECT_TRUE(cd.TestAttr(Attribute::FOREIGN));
    EXPECT_TRUE(cd.HasAnno(AnnotationKind::ARKTS));
    ASSERT_NE(cd.body, nullptr);
    ASSERT_EQ(cd.body->decls.size(), 6);
    for (const auto& member : cd.body->decls) {
        if (member->astKind == ASTKind::FUNC_DECL) {
            // Bodyless members: the two ctors are exempted by the @ArkTS gate,
            // ordinary members take the abstract route (CheckFuncBody enables
            // ABSTRACT instead of diagnosing a missing body).
            auto& fd = StaticCast<FuncDecl&>(*member);
            ASSERT_NE(fd.funcBody, nullptr);
            EXPECT_EQ(fd.funcBody->body, nullptr) << fd.identifier;
        } else {
            auto& pd = StaticCast<PropDecl&>(*member);
            EXPECT_TRUE(pd.getters.empty()) << pd.identifier;
            EXPECT_TRUE(pd.setters.empty()) << pd.identifier;
        }
    }
    EXPECT_TRUE(StaticCast<FuncDecl&>(*cd.body->decls[0]).TestAttr(Attribute::CONSTRUCTOR));
    EXPECT_TRUE(StaticCast<PropDecl&>(*cd.body->decls[4]).TestAttr(Attribute::ABSTRACT));
}

TEST_F(ArkTSEntryTest, PublicForeignClassWithoutArkTSStillReportsModifierConflict)
{
    // Gate guard: without the @ArkTS annotation the access x foreign conflict
    // keeps firing. The parse below runs without EmitCategoryDiagnostics so the
    // stored PARSE diagnostics stay inspectable (emitting erases them).
    auto file = ParseKeepingDiagnostics(R"(public foreign class ArkTSArray<T> {
    public func pop(): Option<T>
}
)");
    ASSERT_NE(file, nullptr);
    const auto diagnostics = diag.GetCategoryDiagnostic(DiagCategory::PARSE);
    EXPECT_EQ(std::count_if(diagnostics.begin(), diagnostics.end(),
        [](const auto& diagnostic) { return diagnostic.rKind == DiagKindRefactor::parse_conflict_modifier; }), 1);
}

TEST_F(ArkTSEntryTest, PublicForeignInterfaceRelaxationIsArkTSGated)
{
    // Interface form: '@ArkTS public foreign interface' with bodyless members
    // parses, while the same declaration without the annotation keeps the
    // access x foreign conflict.
    auto file = Parse(R"(@ArkTS
public foreign interface Indexed {
    func count(): Float64
    prop name: String
}
)");
    ASSERT_EQ(diag.GetErrorCount(), 0);
    ASSERT_EQ(file->decls.size(), 1);
    auto& id = StaticCast<InterfaceDecl&>(*file->decls.front());
    EXPECT_TRUE(id.TestAttr(Attribute::FOREIGN));
    EXPECT_TRUE(id.HasAnno(AnnotationKind::ARKTS));
    ASSERT_NE(id.body, nullptr);
    ASSERT_EQ(id.body->decls.size(), 2);

    auto plain = ParseKeepingDiagnostics(R"(public foreign interface Indexed {
    func count(): Float64
    prop name: String
}
)");
    ASSERT_NE(plain, nullptr);
    const auto diagnostics = diag.GetCategoryDiagnostic(DiagCategory::PARSE);
    EXPECT_EQ(std::count_if(diagnostics.begin(), diagnostics.end(),
        [](const auto& diagnostic) { return diagnostic.rKind == DiagKindRefactor::parse_conflict_modifier; }), 1);
}

TEST_F(ArkTSEntryTest, ArkTSForeignClassRelaxationSkipsOnlyTheAccessForeignPair)
{
    // Narrowness guard: with the gate active, every conflict pair other
    // than access x foreign keeps firing. SEALED x PRIVATE conflicts in the
    // class rule table with FOREIGN present in the same declaration, and
    // PUBLIC x PRIVATE survives next to a skipped PUBLIC x FOREIGN, so an
    // over-broad relaxation would fail here.
    const std::string stillConflicting[] = {
        R"(@ArkTS
private foreign sealed class SealedForeign {}
)",
        R"(@ArkTS
public foreign private class DoublyAccessed {}
)",
    };
    for (const auto& code : stillConflicting) {
        SCOPED_TRACE(code);
        // Each spelling is parsed into the same freshly reset engine so the
        // stored PARSE diagnostics only describe this iteration; without the
        // reset the first case's conflict would be counted again below.
        diag.Reset();
        diag.SetSourceManager(&sm);
        Parser parser(code, diag, sm);
        auto file = parser.ParseTopLevel();
        ASSERT_NE(file, nullptr);
        const auto diagnostics = diag.GetCategoryDiagnostic(DiagCategory::PARSE);
        EXPECT_EQ(std::count_if(diagnostics.begin(), diagnostics.end(),
            [](const auto& diagnostic) { return diagnostic.rKind == DiagKindRefactor::parse_conflict_modifier; }), 1);
    }
}

TEST_F(ArkTSEntryTest, ArkTSForeignBlockInheritsTheAnnotationGateForClassMembers)
{
    // Block-inheritance guard: the block-level @ArkTS is inherited by every
    // member declaration (ParseInheritedAnnotations prepends the ARKTS clone)
    // and the block foreign modifier merges into it, so 'public class' inside
    // the block is gated exactly like the single-declaration spelling: the
    // access x foreign conflict is lifted, the class enters the FOREIGN chain
    // and carries the @ArkTS marker. This pins the intended equivalence
    // ("block @ArkTS == per-declaration @ArkTS") as the regression baseline
    // for the future full relaxation; other DefKinds (e.g. a let in a foreign
    // block) have no gated CheckDeclModifiers call site yet and keep reporting
    // their conflicts.
    auto file = Parse(R"(@ArkTS
foreign {
    public class C {}
}
)");
    ASSERT_EQ(diag.GetErrorCount(), 0);
    ASSERT_EQ(file->decls.size(), 1);
    auto& cd = StaticCast<ClassDecl&>(*file->decls.front());
    EXPECT_TRUE(cd.TestAttr(Attribute::FOREIGN));
    EXPECT_TRUE(cd.HasAnno(AnnotationKind::ARKTS));
}

TEST_F(ArkTSEntryTest, BodylessInitOfForeignClassWithoutArkTSStillDiagnosed)
{
    // Init-exemption gate guard: an ordinary foreign class (no @ArkTS
    // annotation) keeps the bodyless-constructor diagnostic.
    auto file = ParseKeepingDiagnostics(R"(foreign class PlainQueue {
    public init(capacity: Float64)
}
)");
    ASSERT_NE(file, nullptr);
    const auto diagnostics = diag.GetCategoryDiagnostic(DiagCategory::PARSE);
    EXPECT_EQ(std::count_if(diagnostics.begin(), diagnostics.end(),
        [](const auto& diagnostic) { return diagnostic.rKind == DiagKindRefactor::parse_missing_body; }), 1);
}

// ---- @RestParam placement on the constructor, primary constructor and prop
// getter/setter parameter lists: the parse paths that never reach
// FFIParserImpl::CheckFuncSignature, where the annotation used to be inert ----

TEST_F(ArkTSEntryTest, RestParamOnAnArkTSForeignClassInitIsAccepted)
{
    // Positive: the bodyless 'init' of an @ArkTS foreign class is the stdlib
    // preset spelling (ArkTSArray.init(@RestParam items: Array<T>)); with the
    // rest parameter in the final position and no named parameter, the
    // constructor path of the placement check stays silent.
    auto file = Parse(R"(@ArkTS
foreign class Bag {
    public init(@RestParam items: Array<Int32>)
}
)");
    ASSERT_EQ(diag.GetErrorCount(), 0);
    ASSERT_EQ(file->decls.size(), 1);
    auto& cd = StaticCast<ClassDecl&>(*file->decls.front());
    EXPECT_TRUE(cd.TestAttr(Attribute::FOREIGN));
    EXPECT_TRUE(cd.HasAnno(AnnotationKind::ARKTS));
    ASSERT_NE(cd.body, nullptr);
    ASSERT_EQ(cd.body->decls.size(), 1);
    auto& ctor = StaticCast<FuncDecl&>(*cd.body->decls.front());
    EXPECT_TRUE(ctor.TestAttr(Attribute::CONSTRUCTOR));
    auto& params = ctor.funcBody->paramLists.front()->params;
    ASSERT_EQ(params.size(), 1);
    ASSERT_EQ(params.front()->annotations.size(), 1);
    EXPECT_EQ(params.front()->annotations.front()->kind, AnnotationKind::REST_PARAM);
}

TEST_F(ArkTSEntryTest, RestParamOnANonFinalInitParameterIsRejected)
{
    // Negative, placement on the constructor path: the rest parameter
    // must be the final positional parameter of an init as well, reported by
    // the same generic unexpected-annotation diagnostic the func path uses
    // (once; the named-parameter rule is not involved here).
    auto file = ParseKeepingDiagnostics(R"(@ArkTS
foreign class Bag {
    public init(@RestParam items: Array<Int32>, extra: Int32)
}
)");
    ASSERT_NE(file, nullptr);
    const auto diagnostics = diag.GetCategoryDiagnostic(DiagCategory::PARSE);
    EXPECT_EQ(diagnostics.size(), 1);
    EXPECT_EQ(std::count_if(diagnostics.begin(), diagnostics.end(),
        [](const auto& diagnostic) { return diagnostic.rKind == DiagKindRefactor::parse_unexpected_anno_on; }), 1);
    EXPECT_EQ(std::count_if(diagnostics.begin(), diagnostics.end(),
        [](const auto& diagnostic) {
            return diagnostic.rKind == DiagKindRefactor::parse_rest_param_function_has_named_params;
        }), 0);
}

TEST_F(ArkTSEntryTest, RestParamOnAnInitWithNamedParametersReportsTheDedicatedDiagnostic)
{
    // Negative, named-parameter rule on the constructor path: an init that
    // contains a rest parameter cannot contain a named parameter either,
    // reported once
    // through the dedicated diagnostic with the placement checks skipped -
    // the same no-double-report contract as the func path.
    auto file = ParseKeepingDiagnostics(R"(@ArkTS
foreign class Bag {
    public init(@RestParam items: Array<Int32>, extra!: Option<Int32> = None)
}
)");
    ASSERT_NE(file, nullptr);
    const auto diagnostics = diag.GetCategoryDiagnostic(DiagCategory::PARSE);
    EXPECT_EQ(diagnostics.size(), 1);
    EXPECT_EQ(std::count_if(diagnostics.begin(), diagnostics.end(),
        [](const auto& diagnostic) {
            return diagnostic.rKind == DiagKindRefactor::parse_rest_param_function_has_named_params;
        }), 1);
    EXPECT_EQ(std::count_if(diagnostics.begin(), diagnostics.end(),
        [](const auto& diagnostic) { return diagnostic.rKind == DiagKindRefactor::parse_unexpected_anno_on; }), 0);
}

TEST_F(ArkTSEntryTest, RestParamOnAPropSetterOfAnOrdinaryClassIsRejected)
{
    // Negative, prop setter: the setter parameter list is parsed on the
    // prop-member path; an ordinary class is not an @ArkTS foreign target,
    // so the annotation is diagnosed there too. A setter parameter takes no
    // type (the property type is the parameter type), so the annotation is
    // the only decoration on it.
    auto file = ParseKeepingDiagnostics(R"(class Box {
    public mut prop items: Array<Int32> {
        get() {}
        set(@RestParam values) {}
    }
}
)");
    ASSERT_NE(file, nullptr);
    const auto diagnostics = diag.GetCategoryDiagnostic(DiagCategory::PARSE);
    EXPECT_EQ(diagnostics.size(), 1);
    EXPECT_EQ(std::count_if(diagnostics.begin(), diagnostics.end(),
        [](const auto& diagnostic) { return diagnostic.rKind == DiagKindRefactor::parse_unexpected_anno_on; }), 1);
}

TEST_F(ArkTSEntryTest, RestParamOnAnInitOfAnOrdinaryForeignClassIsRejected)
{
    // Negative, target guard on the constructor path: an ordinary C
    // foreign class (FOREIGN but no @ArkTS annotation) is not a rest target,
    // so its init keeps the rejection; the ordinary bodyless-init diagnostic
    // still fires next to it.
    auto file = ParseKeepingDiagnostics(R"(foreign class PlainBag {
    public init(@RestParam items: Array<Int32>)
}
)");
    ASSERT_NE(file, nullptr);
    const auto diagnostics = diag.GetCategoryDiagnostic(DiagCategory::PARSE);
    EXPECT_EQ(std::count_if(diagnostics.begin(), diagnostics.end(),
        [](const auto& diagnostic) { return diagnostic.rKind == DiagKindRefactor::parse_unexpected_anno_on; }), 1);
    EXPECT_EQ(std::count_if(diagnostics.begin(), diagnostics.end(),
        [](const auto& diagnostic) { return diagnostic.rKind == DiagKindRefactor::parse_missing_body; }), 1);
}

TEST_F(ArkTSEntryTest, RestParamOnAPrimaryConstructorOfANonForeignArkTSClassIsRejected)
{
    // Negative, primary constructor + non-foreign guard: @ArkTS without
    // foreign does not make the class a rest target, so the primary
    // constructor parameter list keeps the rejection there too.
    auto file = ParseKeepingDiagnostics(R"(@ArkTS
class Pouch {
    Pouch(@RestParam items: Array<Int32>) {
    }
}
)");
    ASSERT_NE(file, nullptr);
    const auto diagnostics = diag.GetCategoryDiagnostic(DiagCategory::PARSE);
    EXPECT_EQ(diagnostics.size(), 1);
    EXPECT_EQ(std::count_if(diagnostics.begin(), diagnostics.end(),
        [](const auto& diagnostic) { return diagnostic.rKind == DiagKindRefactor::parse_unexpected_anno_on; }), 1);
}

// ---- regression guards for the shared class/interface parse paths.
// ParseClassDecl/ParseInterfaceDecl attach the annotations before the body is
// parsed, and ParseModifiers lifts access x FOREIGN only for @ArkTS
// declarations. Both changes are shared with Java/OC, so the mirror spellings
// below pin that the existing interop paths are unaffected, and the stdlib
// preset declarations are parsed here instead of only by the structure
// script. ----

TEST_F(ArkTSEntryTest, JavaMirrorKeepsAnnotationAndMemberOwnership)
{
    // Shared-path guard (Java): the annotations are attached to the class
    // before ParseClassBody, so the members parsed afterwards still see the
    // Java mirror only through the enclosing declaration. A bodyless member
    // function in a mirror class is accepted (CheckFuncBody marks it ABSTRACT
    // via CanBeAbstract) and keeps its outerDecl set by
    // SetMemberParentInheritableDecl.
    auto file = Parse(R"(@JavaMirror
class M {
    func f(a: Int32): Int32
}
)");
    ASSERT_NE(file, nullptr);
    ASSERT_EQ(diag.GetErrorCount(), 0);
    ASSERT_EQ(file->decls.size(), 1);
    auto& cd = StaticCast<ClassDecl&>(*file->decls.front());
    EXPECT_TRUE(cd.HasAnno(AnnotationKind::JAVA_MIRROR));
    EXPECT_TRUE(cd.TestAttr(Attribute::JAVA_MIRROR));
    ASSERT_NE(cd.body, nullptr);
    ASSERT_EQ(cd.body->decls.size(), 1);
    auto& fd = StaticCast<FuncDecl&>(*cd.body->decls.front());
    EXPECT_EQ(fd.identifier.Val(), "f");
    EXPECT_EQ(fd.outerDecl, &cd);
    ASSERT_NE(fd.funcBody, nullptr);
    EXPECT_EQ(fd.funcBody->body, nullptr);
    ASSERT_NE(fd.funcBody->paramLists.front(), nullptr);
    ASSERT_EQ(fd.funcBody->paramLists.front()->params.size(), 1);
    EXPECT_EQ(fd.funcBody->paramLists.front()->params.front()->identifier.Val(), "a");
}

TEST_F(ArkTSEntryTest, ObjCMirrorKeepsAnnotationAndMemberOwnership)
{
    // Shared-path guard (Objective-C): same shape as the Java mirror test,
    // with @ObjCMirror on the class only and bodyless member functions.
    // CheckClassLikeSignature enables OBJ_C_MIRROR before the body is parsed,
    // so CheckMemberFuncObjCMirror still marks the member and
    // CheckClassLikeFuncBodyAbstractness keeps a bodyless mirror method without
    // a missing-body diagnostic.
    auto file = Parse(R"(@ObjCMirror
class O {
    func g(): Unit
}
)");
    ASSERT_NE(file, nullptr);
    ASSERT_EQ(diag.GetErrorCount(), 0);
    ASSERT_EQ(file->decls.size(), 1);
    auto& cd = StaticCast<ClassDecl&>(*file->decls.front());
    EXPECT_TRUE(cd.HasAnno(AnnotationKind::OBJ_C_MIRROR));
    EXPECT_TRUE(cd.TestAttr(Attribute::OBJ_C_MIRROR));
    ASSERT_NE(cd.body, nullptr);
    ASSERT_EQ(cd.body->decls.size(), 1);
    auto& fd = StaticCast<FuncDecl&>(*cd.body->decls.front());
    EXPECT_EQ(fd.identifier.Val(), "g");
    EXPECT_EQ(fd.outerDecl, &cd);
    EXPECT_TRUE(fd.TestAttr(Attribute::OBJ_C_MIRROR));
    ASSERT_NE(fd.funcBody, nullptr);
    EXPECT_EQ(fd.funcBody->body, nullptr);
}

TEST_F(ArkTSEntryTest, MinimalPublicForeignClassWithoutArkTSStillReportsModifierConflict)
{
    // Gate guard for the shared modifier path: the access x FOREIGN pair is
    // lifted only for @ArkTS declarations, so the minimal 'public foreign
    // class C {}' spelling (no members, no other modifiers) keeps exactly
    // one parse_conflict_modifier. This complements the pre-existing
    // PublicForeignClassWithoutArkTSStillReportsModifierConflict (generic
    // class with a member) by pinning the no-body/no-member shape the
    // stdlib and the language-level tests use.
    auto file = ParseKeepingDiagnostics(R"(public foreign class C {}
)");
    ASSERT_NE(file, nullptr);
    const auto diagnostics = diag.GetCategoryDiagnostic(DiagCategory::PARSE);
    EXPECT_EQ(std::count_if(diagnostics.begin(), diagnostics.end(),
        [](const auto& diagnostic) { return diagnostic.rKind == DiagKindRefactor::parse_conflict_modifier; }), 1);
}

namespace {
// Counts the members that belong to the class-like under test, so the preset
// declaration tests below state the member surface independently of the
// declaration text (14 members for ArkTSArray, 11 for ArkTSString).
size_t CountClassMembers(ClassDecl& cd)
{
    size_t count = 0;
    Walker(&cd, [&count](Ptr<Node> node) {
        if (node->astKind == ASTKind::FUNC_DECL || node->astKind == ASTKind::PROP_DECL) {
            ++count;
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    return count;
}

std::vector<Ptr<FuncParam>> RestParamsOf(ClassDecl& cd)
{
    std::vector<Ptr<FuncParam>> rest;
    Walker(&cd, [&rest](Ptr<Node> node) {
        auto fd = DynamicCast<FuncDecl*>(node);
        if (fd == nullptr || fd->funcBody == nullptr || fd->funcBody->paramLists.empty()) {
            return VisitAction::WALK_CHILDREN;
        }
        for (const auto& param : fd->funcBody->paramLists.front()->params) {
            if (std::any_of(param->annotations.begin(), param->annotations.end(),
                    [](const auto& anno) { return anno->kind == AnnotationKind::REST_PARAM; })) {
                rest.push_back(param.get());
            }
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    return rest;
}

// Every member of a preset declaration is implemented on the ArkTS side: a
// bodyless function member must have no body, and a prop member must declare
// neither getter nor setter (the existing bodyless-member convention of
// ArkTSAnnotatedPublicForeignClassParsesWithBodylessMembers).
void ExpectAllMembersBodyless(const ClassDecl& cd)
{
    ASSERT_NE(cd.body, nullptr);
    for (const auto& member : cd.body->decls) {
        if (auto fd = DynamicCast<FuncDecl*>(member.get())) {
            ASSERT_NE(fd->funcBody, nullptr) << fd->identifier;
            EXPECT_EQ(fd->funcBody->body, nullptr) << fd->identifier;
        } else if (auto pd = DynamicCast<PropDecl*>(member.get())) {
            EXPECT_TRUE(pd->getters.empty()) << pd->identifier;
            EXPECT_TRUE(pd->setters.empty()) << pd->identifier;
        } else {
            EXPECT_TRUE(false) << "unexpected member kind in preset declaration";
        }
    }
}
} // namespace

TEST_F(ArkTSEntryTest, PresetArkTSArrayDeclarationParsesAllFourteenMembers)
{
    // The complete stdlib declaration of ArkTSArray<T> is parsed verbatim, so
    // "the preset declaration is parseable" is exercised by the baseline ctest
    // instead of only by the structure script. The three rest parameters keep
    // the Cangjie Array<T> type - the ArkTSArray<T> reference form is out of
    // scope here - and no member has a body.
    auto file = Parse(R"(@ArkTS[foreignName: "Array"]
public foreign class ArkTSArray<T> {
    public init(arrayLength: Float64)
    public init(@RestParam items: Array<T>)
    public static func isArray(arg: ArkTSType): Bool
    public prop prototype: ArkTSType
    public prop length: Float64
    public func pop(): Option<T>
    public func push(@RestParam items: Array<T>): Float64
    public func join(separator!: Option<String> = None): String
    public func reverse(): ArkTSArray<T>
    public func shift(): Option<T>
    public func unshift(@RestParam items: Array<T>): Float64
    public func slice(start!: Option<Float64> = None, end!: Option<Float64> = None): ArkTSArray<T>
    public func indexOf(searchElement: T, fromIndex!: Option<Float64> = None): Float64
    public func lastIndexOf(searchElement: T, fromIndex!: Option<Float64> = None): Float64
}
)");
    ASSERT_NE(file, nullptr);
    ASSERT_EQ(diag.GetErrorCount(), 0);
    ASSERT_EQ(file->decls.size(), 1);
    auto& cd = StaticCast<ClassDecl&>(*file->decls.front());
    EXPECT_TRUE(cd.TestAttr(Attribute::FOREIGN));
    EXPECT_TRUE(cd.HasAnno(AnnotationKind::ARKTS));
    EXPECT_EQ(CountClassMembers(cd), 14u);
    EXPECT_EQ(cd.body->decls.size(), 14u);
    auto rest = RestParamsOf(cd);
    ASSERT_EQ(rest.size(), 3u);
    for (const auto& param : rest) {
        EXPECT_EQ(param->identifier.Val(), "items");
        EXPECT_EQ(param->type->ToString(), "Array<T>");
        auto refType = DynamicCast<RefType*>(param->type.get());
        ASSERT_NE(refType, nullptr);
        EXPECT_EQ(refType->ref.identifier.Val(), "Array");
        ASSERT_EQ(refType->typeArguments.size(), 1u);
        EXPECT_EQ(refType->typeArguments.front()->ToString(), "T");
    }
    ExpectAllMembersBodyless(cd);
}

TEST_F(ArkTSEntryTest, PresetArkTSStringDeclarationParsesAllElevenMembers)
{
    // Mirror of the array case: the complete stdlib declaration of ArkTSString
    // parses verbatim with no member body.
    auto file = Parse(R"(@ArkTS[foreignName: "String"]
public foreign class ArkTSString {
    public init(value!: Option<String> = None)
    public prop length: Float64
    public func charAt(pos: Float64): String
    public func charCodeAt(pos: Float64): Float64
    public func indexOf(searchString: String, position!: Option<Float64> = None): Float64
    public func lastIndexOf(searchString: String, position!: Option<Float64> = None): Float64
    public func slice(start!: Option<Float64> = None, end!: Option<Float64> = None): ArkTSString
    public func substring(start: Float64, end!: Option<Float64> = None): ArkTSString
    public func toUpperCase(): ArkTSString
    public func toLowerCase(): ArkTSString
    public func toString(): String
}
)");
    ASSERT_NE(file, nullptr);
    ASSERT_EQ(diag.GetErrorCount(), 0);
    ASSERT_EQ(file->decls.size(), 1);
    auto& cd = StaticCast<ClassDecl&>(*file->decls.front());
    EXPECT_TRUE(cd.TestAttr(Attribute::FOREIGN));
    EXPECT_TRUE(cd.HasAnno(AnnotationKind::ARKTS));
    EXPECT_EQ(CountClassMembers(cd), 11u);
    EXPECT_EQ(cd.body->decls.size(), 11u);
    EXPECT_TRUE(RestParamsOf(cd).empty());
    ExpectAllMembersBodyless(cd);
}

TEST_F(ArkTSEntryTest, ArkTSForeignInitRestWithNamedParameterStillReportsDedicatedDiagnostic)
{
    // Cross-guard with the shared annotation move: the constructor path
    // resolves the enclosing class through the parser chain and needs the
    // @ArkTS marker during ParseClassBody, i.e. exactly the annotation
    // that ParseClassDecl now attaches before the body. If that order
    // regressed, the init would be treated as a non-target and report
    // parse_unexpected_anno_on instead of the dedicated named-parameter
    // diagnostic, so this test also guards the annotation-attachment order.
    auto file = ParseKeepingDiagnostics(R"(@ArkTS
foreign class C {
    init(@RestParam xs: Array<Int32>, y!: Int32 = 0)
}
)");
    ASSERT_NE(file, nullptr);
    const auto diagnostics = diag.GetCategoryDiagnostic(DiagCategory::PARSE);
    EXPECT_EQ(diagnostics.size(), 1);
    EXPECT_EQ(std::count_if(diagnostics.begin(), diagnostics.end(),
        [](const auto& diagnostic) {
            return diagnostic.rKind == DiagKindRefactor::parse_rest_param_function_has_named_params;
        }), 1);
    EXPECT_EQ(std::count_if(diagnostics.begin(), diagnostics.end(),
        [](const auto& diagnostic) { return diagnostic.rKind == DiagKindRefactor::parse_unexpected_anno_on; }), 0);
}
