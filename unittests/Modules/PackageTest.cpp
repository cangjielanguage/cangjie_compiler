// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "gtest/gtest.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <limits>

#define private public
#define protected public
#include "TestCompilerInstance.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include "CompilationCacheSerialization.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Basic/Version.h"
#include "cangjie/IncrementalCompilation/IncrementalScopeAnalysis.h"
#include "cangjie/Modules/ASTSerialization.h"
#include "cangjie/Modules/CjoManager.h"
#include "cangjie/Modules/CjoVersion.h"
#include "cangjie/Utils/FileUtil.h"
#include "flatbuffers/CjoFormat_generated.h"

using namespace Cangjie;
using namespace AST;

class PackageTest : public testing::Test {
protected:
    void SetUp() override
    {
#ifdef _WIN32
        std::string command;
        int err = 0;
        if (FileUtil::FileExist("testTempFiles")) {
            std::string command = "rmdir /s /q testTempFiles";
            int err = system(command.c_str());
            std::cout << err;
        }
        command = "mkdir testTempFiles";
        err = system(command.c_str());
        ASSERT_EQ(0, err);

        srcPath = projectPath + "\\unittests\\Modules\\CangjieFiles\\";
        packagePath = packagePath + "\\";
#else
        std::string command;
        int err = 0;
        if (FileUtil::FileExist("testTempFiles")) {
            command = "rm -rf testTempFiles";
            err = system(command.c_str());
            ASSERT_EQ(0, err);
        }
        command = "mkdir -p testTempFiles";
        err = system(command.c_str());
        ASSERT_EQ(0, err);
        srcPath = projectPath + "/unittests/Modules/CangjieFiles/";
        packagePath = packagePath + "/";
#endif
#ifdef __x86_64__
        invocation.globalOptions.target.arch = Cangjie::Triple::ArchType::X86_64;
#else
        invocation.globalOptions.target.arch = Cangjie::Triple::ArchType::AARCH64;
#endif
#ifdef _WIN32
        invocation.globalOptions.target.os = Cangjie::Triple::OSType::WINDOWS;
#elif defined(__unix__)
        invocation.globalOptions.target.os = Cangjie::Triple::OSType::LINUX;
#endif
        invocation.globalOptions.outputMode = GlobalOptions::OutputMode::STATIC_LIB;
        invocation.globalOptions.disableReflection = true;
        invocation.globalOptions.importPaths = {packagePath};
        invocation.globalOptions.compilationCachedPath = ".";

        // Compile core mock.
        auto coreFile = srcPath + "coremock.cj";
        std::string failedReason;
        auto content = FileUtil::ReadFileContent(coreFile, failedReason);
        if (!content.has_value()) {
            diag.DiagnoseRefactor(
                DiagKindRefactor::module_read_file_to_buffer_failed, DEFAULT_POSITION, coreFile, failedReason);
        }
        instance = std::make_unique<TestCompilerInstance>(invocation, diag);
        // create modules dir.
        auto modulesName = FileUtil::JoinPath(instance->cangjieHome, "modules");
        auto libPathName = invocation.globalOptions.GetCangjieLibTargetPathName();
        auto cangjieModules = FileUtil::JoinPath(modulesName, libPathName);
        if (!FileUtil::FileExist(cangjieModules)) {
            FileUtil::CreateDirs(FileUtil::JoinPath(cangjieModules, ""));
        }
        instance->code = std::move(content.value());
        instance->invocation.globalOptions.implicitPrelude = false;
        instance->Compile(CompileStage::DESUGAR_AFTER_SEMA);
        std::vector<uint8_t> astData;
        instance->importManager->ExportAST(false, astData, *instance->GetSourcePackages()[0]);

        std::string coreCjo = FileUtil::JoinPath(cangjieModules, "std.core.cjo");
        if (FileUtil::FileExist(coreCjo)) {
            FileUtil::Remove(coreCjo);
        }
        FileUtil::WriteBufferToASTFile(coreCjo, astData);
        instance->invocation.globalOptions.implicitPrelude = true;

        for (unsigned int i = 0; i < fileNames.size(); i++) {
            auto srcFile = srcPath + fileNames[i] + ".cj";
            content = FileUtil::ReadFileContent(srcFile, failedReason);
            if (!content.has_value()) {
                diag.DiagnoseRefactor(
                    DiagKindRefactor::module_read_file_to_buffer_failed, DEFAULT_POSITION, srcFile, failedReason);
            }
            instance = std::make_unique<TestCompilerInstance>(invocation, diag);
            instance->code = std::move(content.value());
            instance->Compile();
            std::vector<uint8_t> astData;
            instance->importManager->ExportAST(false, astData, *instance->GetSourcePackages()[0]);
            allAstData[fileNames[i]] = astData;
            std::string astFile = packagePath + fileNames[i] + ".cjo";
            ASSERT_TRUE(FileUtil::WriteBufferToASTFile(astFile, astData));
            diag.Reset();
        }
        instance->invocation.globalOptions.implicitPrelude = false;
    }

#ifdef PROJECT_SOURCE_DIR
    // Gets the absolute path of the project from the compile parameter.
    std::string projectPath = PROJECT_SOURCE_DIR;
#else
    // Just in case, give it a default value.
    // Assume the initial is in the build directory.
    std::string projectPath = "..";
#endif
    std::vector<std::string> fileNames{
        "vardecl", "funcdecl", "math", "recorddecl", "interfacedecl", "classdecl", "extenddecl"};
    std::string packagePath = "testTempFiles";
    std::string srcPath;
    DiagnosticEngine diag;
    CompilerInvocation invocation;
    std::unique_ptr<TestCompilerInstance> instance;
    std::unordered_map<std::string, std::vector<uint8_t>> allAstData;
};

// Test for package with only one cangjie file.
TEST_F(PackageTest, SingleFile_Package)
{
    std::vector<std::set<std::string>> expectDecls{{"a", "b", "c", "d"},
        {"fun1", "fun1", "fun2", "fun3", "fun4", "a...0", "b...0", "c...0", "a...1"}, {"plus"}, {"Rectangle"},
        {"I1", "I2"}, {"Base2", "Derive1", "I3", "Base3"}};

    for (unsigned int i = 0; i < fileNames.size(); i++) {
        instance = std::make_unique<TestCompilerInstance>(invocation, diag);
        instance->code = "import " + fileNames[i] + ".*";
        instance->Compile();
        Package* pkg = nullptr;
        for (auto pd : instance->importManager->GetAllImportedPackages()) {
            if (pd->fullPackageName != CORE_PACKAGE_NAME) {
                pkg = pd->srcPackage;
                break;
            }
        }
        ASSERT_TRUE(pkg != nullptr);

        std::vector<std::string> declNames;
        for (auto& file : pkg->files) {
            for (auto& decl : file->decls) {
                if (Is<VarDecl>(decl.get())) {
                    EXPECT_TRUE(expectDecls[i].find(decl->identifier) != expectDecls[i].end());
                }
                if (Is<FuncDecl>(decl.get())) {
                    EXPECT_TRUE(expectDecls[i].find(decl->identifier) != expectDecls[i].end());
                }
                if (Is<ClassDecl>(decl.get())) {
                    EXPECT_TRUE(expectDecls[i].find(decl->identifier) != expectDecls[i].end());
                }
                if (Is<InterfaceDecl>(decl.get())) {
                    EXPECT_TRUE(expectDecls[i].find(decl->identifier) != expectDecls[i].end());
                }
            }
        }
        EXPECT_TRUE(diag.GetErrorCount() == 0);
    }
}

TEST_F(PackageTest, SingleFile_LoadCache_Package)
{
    std::vector<std::set<std::string>> expectDecls{{"a", "b", "c", "d"},
        {"fun1", "fun1", "fun2", "fun3", "fun4", "a...0", "b...0", "c...0", "a...1"}, {"plus"}, {"Rectangle"},
        {"I1", "I2"}, {"Base2", "Derive1", "I3", "Base3"}};

    for (unsigned int i = 0; i < fileNames.size(); i++) {
        instance = std::make_unique<TestCompilerInstance>(invocation, diag);
        instance->importManager->SetPackageCjoCache(fileNames[i], allAstData[fileNames[i]]);
        instance->code = "import " + fileNames[i] + ".*";
        instance->Compile();

        Package* pkg = nullptr;
        for (auto pd : instance->importManager->GetAllImportedPackages()) {
            if (pd->fullPackageName != CORE_PACKAGE_NAME) {
                pkg = pd->srcPackage;
                break;
            }
        }
        ASSERT_TRUE(pkg != nullptr);

        std::vector<std::string> declNames;
        for (auto& file : pkg->files) {
            for (auto& decl : file->decls) {
                if (Is<VarDecl>(decl.get())) {
                    EXPECT_TRUE(expectDecls[i].find(decl->identifier) != expectDecls[i].end());
                }
                if (Is<FuncDecl>(decl.get())) {
                    EXPECT_TRUE(expectDecls[i].find(decl->identifier) != expectDecls[i].end());
                }
                if (Is<ClassDecl>(decl.get())) {
                    EXPECT_TRUE(expectDecls[i].find(decl->identifier) != expectDecls[i].end());
                }
                if (Is<InterfaceDecl>(decl.get())) {
                    EXPECT_TRUE(expectDecls[i].find(decl->identifier) != expectDecls[i].end());
                }
            }
        }
        EXPECT_TRUE(diag.GetErrorCount() == 0);
    }
}

// Test for one package with multi cangjie files.
TEST_F(PackageTest, MultiFile_Package)
{
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->srcDirs = {srcPath};
    instance->code = "import funcdecl.*";
    instance->Compile();

    diag.Reset();

    std::vector<uint8_t> astData;
    instance->importManager->ExportAST(false, astData, *instance->GetSourcePackages()[0]);
    std::string astFile = packagePath + "MultiFile.cjo";
    ASSERT_TRUE(FileUtil::WriteBufferToASTFile(astFile, astData));

    std::unordered_map<std::string, bool> expectVarDecls{{"a", true}, {"b", true}, {"c", true}, {"d", true}};
    std::unordered_map<std::string, bool> expectFuncDecls{{"fun1", true}, {"fun1", true}, {"fun2", true},
        {"fun3", true}, {"fun4", true}, {"a...0", true}, {"b...0", true}, {"c...0", true}, {"a...1", true},
        {"plus", true}};
    std::unordered_map<std::string, bool> expectRecordDecls{{"Rectangle", true}};
    std::unordered_map<std::string, bool> expectInterfaceDecls{{"I1", true}, {"I2", true}, {"I3", true}};
    std::unordered_map<std::string, bool> expectClassDecls{{"Base2", true}, {"Derive1", true}, {"Base3", true}};

    Package* pkg = nullptr;
    for (auto pd : instance->importManager->GetAllImportedPackages()) {
        if (pd->fullPackageName != CORE_PACKAGE_NAME) {
            pkg = pd->srcPackage;
            break;
        }
    }
    ASSERT_TRUE(pkg != nullptr);

    std::vector<std::string> declNames;
    for (auto& file : pkg->files) {
        for (auto& decl : file->decls) {
            if (Is<VarDecl>(decl.get())) {
                EXPECT_TRUE(expectVarDecls[decl->identifier]);
            }
            if (Is<FuncDecl>(decl.get())) {
                EXPECT_TRUE(expectFuncDecls[decl->identifier]);
            }
            if (Is<StructDecl>(decl.get())) {
                EXPECT_TRUE(expectRecordDecls[decl->identifier]);
            }
            if (Is<InterfaceDecl>(decl.get())) {
                EXPECT_TRUE(expectInterfaceDecls[decl->identifier]);
            }
            if (Is<ClassDecl>(decl.get())) {
                EXPECT_TRUE(expectClassDecls[decl->identifier]);
            }
        }
    }

    EXPECT_TRUE(diag.GetErrorCount() == 0);
}

TEST_F(PackageTest, ImportPackage)
{
#ifdef _WIN32
    srcPath = projectPath + "\\unittests\\Modules\\ImportPackage\\";
#elif defined(__unix__)
    srcPath = projectPath + "/unittests/Modules/ImportPackage/";
#endif

    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->srcDirs = {srcPath};
    for (unsigned int i = 0; i < fileNames.size(); i++) {
        instance->importManager->SetPackageCjoCache(fileNames[i], allAstData[fileNames[i]]);
    }
    instance->Compile();

    diag.Reset();

    std::set<std::string> expectImportSet{"Base2", "Base3", "Derive1", "I2", "I3", "Rectangle", "a", "classdecl",
        "fun1", "funcdecl", "interfacedecl", "math", "plus", "recorddecl", "vardecl"};
    std::set<std::string> importSet;
    auto pkg = instance->GetSourcePackages()[0];
    for (auto& file : pkg->files) {
        for (auto& [name, _] : instance->importManager->GetImportedDecls(*file)) {
            importSet.emplace(name);
        }
    }

    for (auto pd : instance->importManager->GetCurImportedPackages(instance->GetSourcePackages()[0]->fullPackageName)) {
        importSet.insert(pd->srcPackage->fullPackageName);
    }

    EXPECT_TRUE(diag.GetErrorCount() == 0);
    EXPECT_TRUE(std::equal(importSet.begin(), importSet.end(), expectImportSet.begin()));
}

namespace {
bool NoImportedContent(CompilerInstance& ci)
{
    // All imported declaration should not have content body.
    auto pkgs = ci.GetPackages();
    bool noSrc = true;
    for (auto pkg : pkgs) {
        if (!pkg->TestAttr(Attribute::IMPORTED)) {
            continue;
        }
        Walker(pkg, [&noSrc](auto node) {
            if (auto vd = DynamicCast<VarDecl*>(node); vd && vd->initializer) {
                noSrc = false;
                return VisitAction::STOP_NOW;
            } else if (auto fd = DynamicCast<FuncDecl*>(node); fd && fd->funcBody && fd->funcBody->body) {
                noSrc = false;
                return VisitAction::STOP_NOW;
            }
            return VisitAction::WALK_CHILDREN;
        }).Walk();
    }
    return noSrc;
}
} // namespace

TEST_F(PackageTest, LSPBasicCompileCost)
{
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->invocation.globalOptions.implicitPrelude = true;
    instance->code = R"(
        // Also test for using imported typealias
        func test(x: Byte) : Unit {
            var a : Byte = 1
            var c : UInt8 = a
            c = x
        }

        func foo() {
            var c : (UInt8)->Unit = test
        }
    )";
    instance->importManager->SetSourceCodeImportStatus(false);
    auto start = std::chrono::high_resolution_clock::now();
    instance->Compile(CompileStage::SEMA);
    auto end = std::chrono::high_resolution_clock::now();
    // Compile time for basic case should shorter than 200 ms.
    std::chrono::duration<double, std::milli> cost = end - start;
    auto costMs = static_cast<long>(cost.count());
    std::cout << "cost: " << costMs << std::endl;
    EXPECT_TRUE(NoImportedContent(*instance));
    EXPECT_TRUE(diag.GetErrorCount() == 0);
}

TEST_F(PackageTest, LSPImportLotPkgsCompileCost)
{
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->invocation.globalOptions.implicitPrelude = true;
    instance->cangjieHome = FileUtil::JoinPath(projectPath, "output");
    instance->code = R"(
        import std.ast.*
        import std.collection.*
        import std.io.*
        import std.random.*
        import std.unittest.*
        import std.unittest.testmacro.*

        func test() {
            let a = ArrayList<Any>()
            a.add(1)
            a.add("2")
            for (v in a) {
                println((v as ToString).getOrThrow())
            }
        }
    )";
    instance->importManager->SetSourceCodeImportStatus(false);
    auto start = std::chrono::high_resolution_clock::now();
    instance->Compile(CompileStage::SEMA);
    auto end = std::chrono::high_resolution_clock::now();
    // Compile time for the case importing lot of packages should shorter than 1 s.
    std::chrono::duration<double, std::milli> cost = end - start;
    auto costMs = static_cast<long>(cost.count());
    std::cout << "cost: " << costMs << std::endl;
    EXPECT_TRUE(NoImportedContent(*instance));
    EXPECT_TRUE(diag.GetErrorCount() == 0);
}

TEST_F(PackageTest, LSPCompileComposition)
{
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->invocation.globalOptions.implicitPrelude = true;
    instance->code = R"(
        func f(x: Int32) : Float32 {
            return Float32(x)
        }
        func g(x: Float32): Int32 {
            return Int32(x)
        }
        let fg = f ~> g
        main() :Unit {}
    )";
    instance->importManager->SetSourceCodeImportStatus(false);
    instance->Compile(CompileStage::SEMA);
    EXPECT_TRUE(diag.GetErrorCount() == 0);
    auto pkgs = instance->GetSourcePackages();
    ASSERT_EQ(pkgs.size(), 1);
    auto fg = pkgs[0]->files[0]->decls[2].get();
    ASSERT_TRUE(fg->astKind == ASTKind::VAR_DECL);
    ASSERT_TRUE(StaticCast<VarDecl*>(fg)->initializer != nullptr);
    auto callExpr = DynamicCast<CallExpr*>(StaticCast<VarDecl*>(fg)->initializer->desugarExpr.get());
    ASSERT_TRUE(callExpr != nullptr);
    ASSERT_TRUE(callExpr->resolvedFunction);
    EXPECT_EQ(callExpr->resolvedFunction->identifier, "composition");
}

TEST_F(PackageTest, LSPCompileWithConstraint)
{
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->invocation.globalOptions.implicitPrelude = true;
    instance->code = R"(
        import extenddecl.*
        main() {
            0
        }
    )";
    instance->importManager->SetSourceCodeImportStatus(false);
    instance->Compile(CompileStage::SEMA);
    EXPECT_EQ(diag.GetErrorCount(), 0);
    EXPECT_EQ(diag.GetWarningCount(), 1);
    auto enumDecl = instance->importManager->GetImportedDecl<EnumDecl>("extenddecl", "EnumExtend");
    ASSERT_TRUE(enumDecl != nullptr);
    ASSERT_TRUE(instance->typeManager != nullptr);
    auto extends = instance->typeManager->GetDeclExtends(*enumDecl);
    EXPECT_FALSE(extends.empty());

    size_t count = 0;
    for (auto extend : extends) {
        ASSERT_TRUE(extend->generic != nullptr);
        if (!extend->generic->genericConstraints.empty()) {
            count++;
            for (auto& constraint : extend->generic->genericConstraints) {
                ASSERT_TRUE(constraint != nullptr && constraint->type != nullptr);
                auto gTy = RawStaticCast<GenericsTy*>(constraint->type->DataTy());
                EXPECT_TRUE(gTy && !gTy->upperBounds.empty());
                EXPECT_FALSE(constraint->upperBounds.empty());
            }
        }
    }
    // NOTE: Current the count of all extends in 'core' package have generic constraints is 3.
    EXPECT_EQ(count, 3);
}

TEST_F(PackageTest, LSPImportFunctionWithNamedParam)
{
    diag.ClearError();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->invocation.globalOptions.implicitPrelude = true;
    instance->invocation.globalOptions.compilePackage = true;
    instance->code = R"(
        package pkg
        public func name(a_123!:Int64) {}
    )";
    instance->importManager->SetSourceCodeImportStatus(false);
    instance->Compile(CompileStage::SEMA);
    std::vector<uint8_t> astData;
    instance->importManager->ExportAST(false, astData, *instance->GetSourcePackages()[0]);

    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->importManager->SetPackageCjoCache("pkg", astData);
    instance->invocation.globalOptions.implicitPrelude = true;
    instance->code = R"(
        import pkg.*
        main() {
            var xx = name(a_123: 5)
        }
    )";
    instance->importManager->SetSourceCodeImportStatus(false);
    instance->Compile(CompileStage::SEMA);
    auto pkgs = instance->GetSourcePackages();
    ASSERT_EQ(pkgs.size(), 1);
    ASTContext& ctx = *instance->GetASTContextByPackage(pkgs[0]);
    EXPECT_TRUE(diag.GetErrorCount() == 0);
    Searcher searcher;
    std::vector<Symbol*> res = searcher.Search(ctx, "ast_kind: ref_expr");
    ASSERT_EQ(res.size(), 1);
    auto target = res[0]->target;
    ASSERT_TRUE(target != nullptr && target->astKind == ASTKind::FUNC_DECL && target->TestAttr(Attribute::IMPORTED));
    auto fd = RawStaticCast<FuncDecl*>(target);
    ASSERT_TRUE(fd->funcBody && !fd->funcBody->paramLists.empty());
    EXPECT_EQ(fd->funcBody->paramLists[0]->params.size(), 1);
    for (auto& param : fd->funcBody->paramLists[0]->params) {
        EXPECT_TRUE(param->outerDecl == fd);
    }
}

TEST_F(PackageTest, ExportVisibleMembersForLSP)
{
    diag.ClearError();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->invocation.globalOptions.implicitPrelude = true;
    instance->invocation.globalOptions.compilePackage = true;
    instance->code = R"(
        package pkg
        public class A {
            public let a = 1
            protected let b = 1
            internal let c = 1
            private let d = 1
        }
    )";
    instance->importManager->SetSourceCodeImportStatus(false);
    instance->Compile(CompileStage::SEMA);
    std::vector<uint8_t> astData;
    instance->importManager->ExportAST(false, astData, *instance->GetSourcePackages()[0]);

    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->importManager->SetPackageCjoCache("pkg", astData);
    instance->invocation.globalOptions.implicitPrelude = true;
    instance->code = R"(
        import pkg.*
        main() {}
    )";
    instance->importManager->SetSourceCodeImportStatus(false);
    instance->Compile(CompileStage::IMPORT_PACKAGE);
    auto pd = instance->importManager->GetPackageDecl("pkg");
    ASSERT_TRUE(pd != nullptr && !pd->srcPackage->files.empty() && !pd->srcPackage->files[0]->decls.empty());
    std::vector<std::string> expected{"a", "b", "c"};
    std::vector<std::string> res;
    for (auto it : pd->srcPackage->files[0]->decls[0]->GetMemberDeclPtrs()) {
        if (it->astKind == ASTKind::VAR_DECL) {
            res.emplace_back(it->identifier);
        }
    }
    EXPECT_TRUE(std::equal(res.cbegin(), res.cend(), expected.cbegin(), expected.cend()));
}

TEST_F(PackageTest, ConstNotExportInitializerForLSP)
{
    diag.ClearError();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->invocation.globalOptions.implicitPrelude = true;
    instance->invocation.globalOptions.compilePackage = true;
    instance->code = R"(
        package initializer

        public class A{
            static const INSTANCE = A()
            private const init() {}
        }
    )";
    instance->importManager->SetSourceCodeImportStatus(false);
    instance->Compile(CompileStage::SEMA);
    std::vector<uint8_t> astData;
    instance->importManager->ExportAST(false, astData, *instance->GetSourcePackages()[0]);

    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->importManager->SetPackageCjoCache("initializer", astData);
    instance->invocation.globalOptions.implicitPrelude = true;
    instance->code = R"(
        import initializer.*
        main() {}
    )";
    instance->importManager->SetSourceCodeImportStatus(false);
    instance->Compile(CompileStage::IMPORT_PACKAGE);
    auto pd = instance->importManager->GetPackageDecl("initializer");
    ASSERT_TRUE(pd != nullptr && !pd->srcPackage->files.empty() && !pd->srcPackage->files[0]->decls.empty());

    for (auto it : pd->srcPackage->files[0]->decls[0]->GetMemberDeclPtrs()) {
        EXPECT_TRUE(it->astKind == ASTKind::VAR_DECL);
        EXPECT_TRUE(it->identifier == "INSTANCE");
        EXPECT_TRUE(StaticCast<VarDecl*>(it)->initializer == nullptr);
    }
}

TEST_F(PackageTest, ForbiddenRunInstantiation)
{
    Cangjie::ICE::TriggerPointSetter iceSetter(static_cast<int64_t>(Cangjie::ICE::UNITTEST_TP));
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->invocation.globalOptions.implicitPrelude = true;
    instance->code = R"(
        main() {}
    )";
    instance->importManager->SetSourceCodeImportStatus(false);
    auto start = std::chrono::high_resolution_clock::now();
    bool res = instance->Compile(CompileStage::GENERIC_INSTANTIATION);
    auto end = std::chrono::high_resolution_clock::now();
    // Compile time for empty main should shorter than 100 ms.
    std::chrono::duration<double, std::milli> cost = end - start;
    auto costMs = static_cast<long>(cost.count());
    std::cout << "cost: " << costMs << std::endl;
    EXPECT_TRUE(NoImportedContent(*instance));
    // No diagnoses but have printing internal error.
    EXPECT_TRUE(diag.GetErrorCount() == 0);
    EXPECT_FALSE(res);
}

TEST_F(PackageTest, LoadBuiltInDecl)
{
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->invocation.globalOptions.implicitPrelude = true;
    instance->code = R"(
        main() {}
    )";
    bool res = instance->Compile(CompileStage::IMPORT_PACKAGE);
    EXPECT_TRUE(res);
    auto pkgs = instance->importManager->GetAllImportedPackages();
    Utils::EraseIf(pkgs, [](auto it) { return !it->TestAttr(Attribute::IMPORTED); });
    EXPECT_EQ(pkgs.size(), 1);
    if (!pkgs.empty()) {
        size_t preFileID = 0;
        for (auto& file : pkgs[0]->srcPackage->files) {
            EXPECT_NE(file->begin.fileID, 0); // Imported fileID should not be 0.
            if (preFileID == 0) {
                preFileID = file->begin.fileID;
            } else {
                // Current fileID should equal to the 'preFileID + 1'.
                EXPECT_EQ(file->begin.fileID, ++preFileID);
            }
        }
    }
    EXPECT_TRUE(diag.GetErrorCount() == 0);
}

TEST_F(PackageTest, MangleEnumExportId)
{
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->invocation.globalOptions.implicitPrelude = true;
    instance->code = R"(
package pkg

enum B {
    B1
    public func test<T>(a: T) {a}
}

public enum A<T> {
    A1
    public func test(a: T) {
        return B1.test(a)
    }
    public func foo<X>(b: X) {b}
}

public enum C {
    C1
    public func foo<X>(b: X) {b}
}

public func test() {
    if (A1<Int64>.test(123) != 123) {
        return 1
    }
    if (A1<Int64>.foo(234) != 234) {
        return 2
    }
    if (C1.foo("23") != "23") {
        return 3
    }
    return 0
}
    )";
    bool res = instance->Compile();
    ASSERT_TRUE(res);
    EXPECT_TRUE(diag.GetErrorCount() == 0);
    auto pkg = instance->GetSourcePackages()[0];
    ASSERT_TRUE(pkg != nullptr);
    std::vector<uint8_t> astData;
    instance->importManager->ExportAST(false, astData, *pkg); // ExportId will be updated during serialization.
    // Expect all generic decls of instantiated decls have valid exportId.
    std::unordered_set<std::string> exportIds;
    std::unordered_set<Ptr<Decl>> genericDecls;
    for (auto& decl : pkg->genericInstantiatedDecls) {
        ASSERT_TRUE(decl->genericDecl != nullptr);
        EXPECT_FALSE(decl->genericDecl->exportId.empty());
        exportIds.emplace(decl->genericDecl->exportId);
        genericDecls.emplace(decl->genericDecl);
    }
    EXPECT_EQ(genericDecls.size(), exportIds.size());
}

TEST_F(PackageTest, LSPExportInterfaceFuncFromMacro)
{
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->code = R"(
    // @M(interface I2 {})
    interface I2{
        func ff6():Unit {
            return
        }
    }
    main() { }
    )";
    instance->Compile(CompileStage::SEMA);
    std::vector<OwnedPtr<MacroExpandDecl>> meds;
    // Test interface decl in macrocall.
    for (auto& decl : instance->GetSourcePackages()[0]->files[0]->decls) {
        if (auto id = AST::As<ASTKind::INTERFACE_DECL>(decl.get()); id) {
            auto macroCall = MakeOwned<MacroExpandDecl>();
            id->curMacroCall = macroCall.get();
            meds.emplace_back(std::move(macroCall));
            for (auto& d : id->body->decls) {
                d->curMacroCall = macroCall.get();
            }
        }
    }
    std::vector<uint8_t> astData;
    instance->importManager->ExportAST(false, astData, *instance->GetSourcePackages()[0]);

    EXPECT_EQ(diag.GetErrorCount(), 0);
}

TEST_F(PackageTest, ImportManager_API)
{
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->invocation.globalOptions.implicitPrelude = true;
    instance->code = R"(
    main() { }
    )";
    instance->Compile(CompileStage::IMPORT_PACKAGE);
    auto members = instance->importManager->GetPackageMembers("default", "std.core");
    EXPECT_TRUE(!members.empty());
    auto obj = instance->importManager->GetImportedDecl<ClassDecl>(CORE_PACKAGE_NAME, OBJECT_NAME);
    EXPECT_TRUE(obj != nullptr);
    auto pkg = instance->GetSourcePackages()[0];
    auto importPkgs = instance->importManager->GetCurImportedPackages(pkg->fullPackageName);
    EXPECT_FALSE(importPkgs.empty()); // default 'core'.
}

TEST_F(PackageTest, LSPExportInvalidVpd)
{
    diag.ClearError();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->invocation.globalOptions.implicitPrelude = true;
    instance->invocation.globalOptions.compilePackage = true;
    instance->code = R"(
        package pkg
        public class A {
            public var (a, b) : (Int64, Int64) // This is an invalid member.
        }
    )";
    instance->importManager->SetSourceCodeImportStatus(false);
    instance->Compile(CompileStage::SEMA);
    std::vector<uint8_t> astData;
    instance->importManager->ExportAST(false, astData, *instance->GetSourcePackages()[0]);
    EXPECT_NE(diag.GetErrorCount(), 0);
}

TEST_F(PackageTest, LSPExportInvisibleMembers)
{
    diag.ClearError();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->invocation.globalOptions.implicitPrelude = true;
    instance->invocation.globalOptions.compilePackage = true;
    instance->code = R"(
        package pkg
        class C {}
        class B {
            public func f0(v: C) {}
        }
        public class A {
            public func f1(v: B) {}
            func f2(v: B) {}
        }
    )";
    instance->importManager->SetSourceCodeImportStatus(false);
    instance->Compile(CompileStage::SEMA);
    std::vector<uint8_t> astData;
    instance->importManager->ExportAST(false, astData, *instance->GetSourcePackages()[0]);
    EXPECT_NE(diag.GetErrorCount(), 0);
}

TEST_F(PackageTest, CheckCjoPathLegality)
{
    diag.ClearError();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    auto import = MakeOwned<ImportSpec>();
    import->EnableAttr(Attribute::IMPLICIT_ADD);
    bool ret = instance->importManager->CheckCjoPathLegality(import, "", "not found package name", false, false);
    ASSERT_EQ(ret, false);
    EXPECT_TRUE(diag.GetErrorCount() == 1);
}

TEST_F(PackageTest, CollectStdDependency_PackageSearchError)
{
    diag.ClearError();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->invocation.globalOptions.scanDepPkg = true;
    instance->code = R"(
        import std.reflect.*
        main() { 0 }
    )";
    bool ret = instance->Compile(CompileStage::IMPORT_PACKAGE);
    EXPECT_TRUE(ret);
    EXPECT_EQ(diag.GetErrorCount(), 1);
    auto diags = diag.GetCategoryDiagnostic(DiagCategory::IMPORT_PACKAGE);
    EXPECT_EQ(diags.size(), 1);
    bool foundPackageSearchError = false;
    for (const auto& d : diags) {
        if (static_cast<int>(d.rKind) == static_cast<int>(DiagKindRefactor::package_search_error) &&
            d.errorMessage == "can not find package 'std.reflect'") {
            foundPackageSearchError = true;
            break;
        }
    }
    EXPECT_TRUE(foundPackageSearchError);
}

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
TEST_F(PackageTest, SemanticUsage)
{
    diag.ClearError();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->invocation.globalOptions.implicitPrelude = true;
    instance->invocation.globalOptions.enIncrementalCompilation = true;
    instance->code = R"(
        package pkg
        import std.core
        var a: Any = 1
        var b: Rune = 'a'
        open class A {
            A(var a!: Int64 = 2, var b!: E = E1) {}
        }
        extend A <: core.ToString {
            public func toString() { "A" }
        }
        extend A <: Hashable {
            public func hashCode(): Int64 {1}
            static func coo() : String {"static"}
        }
        class B <: A {
            func test() {
                toString()
            }
        }
        extend Int64 {
            func foo() {
                a = A().toString() + B.coo()
            }
        }
        enum E {
            E1 | E2(A)
        }
        main() {
            1.foo()
        }
    )";
    const std::unordered_set<std::string> declNames{
        "a", "b", "A", "B", "init", "toString", "hashCode", "coo", "test", "foo", "E", "main"};
    bool ret = instance->Compile(CompileStage::DESUGAR_AFTER_SEMA);
    auto pkg = instance->GetSourcePackages()[0];
    ASSERT_TRUE(pkg != nullptr);
    ret = ret && instance->PerformDesugarAfterSema();
    ASSERT_TRUE(ret);
    EXPECT_EQ(diag.GetErrorCount(), 0);
    std::unordered_map<std::string, Ptr<const Decl>> mangleToDeclMap;
    auto collect = [&mangleToDeclMap](Ptr<Decl> d) {
        if (!d->rawMangleName.empty()) {
            mangleToDeclMap.emplace(d->rawMangleName, d);
        }
    };
    IterateToplevelDecls(*pkg, [&collect](auto& decl) {
        auto d = decl.get();
        if (auto md = DynamicCast<MainDecl*>(decl.get()); md && md->desugarDecl) {
            d = md->desugarDecl.get();
        }
        collect(d);
        for (auto& member : decl->GetMemberDecls()) {
            if (auto pd = DynamicCast<PropDecl*>(member.get())) {
                std::for_each(pd->getters.begin(), pd->getters.end(), [&collect](auto& it) { collect(it.get()); });
                std::for_each(pd->setters.begin(), pd->setters.end(), [&collect](auto& it) { collect(it.get()); });
            } else {
                collect(member.get());
            }
        }
    });
    EXPECT_EQ(mangleToDeclMap.size(), 17); // Count of all toplevel and members decls except enum constructors.
    auto checkSemaInfo = [&declNames](auto& info) {
        EXPECT_EQ(info.usages.size(), 17);              // Count of all toplevel decls except enum constructors.
        EXPECT_EQ(info.relations.size(), 2);            // class A, B
        EXPECT_EQ(info.builtInTypeRelations.size(), 1); // extend of Int64
        uint8_t extendCount = 0;
        for (auto& [decl, u] : info.usages) {
            if (decl->astKind == ASTKind::EXTEND_DECL) {
                extendCount++;
            } else {
                EXPECT_TRUE(declNames.find(decl->identifier) != declNames.end());
            }
            if (decl->identifier == "a" && decl->TestAttr(Attribute::GLOBAL)) {
                ASSERT_EQ(u.boxedTypes.size(), 1);
                EXPECT_EQ(*u.boxedTypes.begin(), "l");
                EXPECT_EQ(u.apiUsages.usedDecls.size(), 1);
                EXPECT_EQ(u.apiUsages.usedNames.size(), 1);
                EXPECT_TRUE(u.apiUsages.usedNames["Any"].hasUnqualifiedUsageOfImported);
                EXPECT_TRUE(u.bodyUsages.usedDecls.empty());
                EXPECT_TRUE(u.bodyUsages.usedNames.empty());
            } else if (decl->TestAttr(Attribute::PRIMARY_CONSTRUCTOR)) {
                EXPECT_EQ(u.boxedTypes.size(), 0);
                EXPECT_EQ(u.apiUsages.usedDecls.size(), 1); // enum E (enum ctor does not been counted separately)
                EXPECT_EQ(u.apiUsages.usedNames.size(), 1); // type E
                auto& nameUsage = u.apiUsages.usedNames["E"];
                EXPECT_TRUE(nameUsage.hasUnqualifiedUsage);
            } else if (decl->identifier == "test") {
                EXPECT_EQ(u.boxedTypes.size(), 0);
                EXPECT_EQ(u.apiUsages.usedDecls.size(), 0);
                EXPECT_EQ(u.apiUsages.usedNames.size(), 0);
                EXPECT_EQ(u.bodyUsages.usedDecls.size(), 4);
                EXPECT_EQ(u.bodyUsages.usedNames.size(), 1);
                EXPECT_TRUE(u.bodyUsages.usedNames["toString"].hasUnqualifiedUsage);
            } else if (decl->identifier == "E") {
                EXPECT_EQ(u.boxedTypes.size(), 0);
                EXPECT_EQ(u.apiUsages.usedDecls.size(), 1); // A
                EXPECT_EQ(u.apiUsages.usedNames.size(), 1);
            } else if (decl->identifier == "foo") {
                EXPECT_EQ(u.boxedTypes.size(), 1); // String
                EXPECT_EQ(u.apiUsages.usedDecls.size(), 0);
                // A init, A, toString, B, coo, global a, overload +, String
                EXPECT_EQ(u.bodyUsages.usedDecls.size(), 8);
                EXPECT_EQ(u.bodyUsages.usedNames.size(), 6); // a, A, toString, B, coo, +
                auto& nameUsage = u.bodyUsages.usedNames["coo"];
                ASSERT_EQ(nameUsage.parentDecls.size(), 1);
                EXPECT_TRUE((*nameUsage.parentDecls.begin()).find("pkg") != std::string::npos);
            }
        }
        EXPECT_EQ(extendCount, 3);
        for (auto& [mangle, rel] : info.relations) {
            if (mangle.find("B") != std::string::npos) {
                EXPECT_EQ(rel.inherits.size(), 1);
                EXPECT_TRUE(rel.extends.empty());
            } else if (mangle.find("A") != std::string::npos) {
                EXPECT_EQ(rel.inherits.size(), 1); // Object
                EXPECT_EQ(rel.extends.size(), 2);
                EXPECT_EQ(rel.extendedInterfaces.size(), 2);
            } else {
                CJC_ABORT();
            }
        }
    };
    checkSemaInfo(instance->cachedInfo.semaInfo);
    // Serialize.
    HashedASTWriter writer;
    writer.SetSemanticInfo(instance->cachedInfo.semaInfo);
    auto data = writer.AST2FB(pkg->fullPackageName);
    // Deserialize.
    auto hashedPackage = CachedASTFormat::GetHashedPackage(data.data());
    auto loaded = HashedASTLoader::LoadSemanticInfos(*hashedPackage, mangleToDeclMap);
    checkSemaInfo(loaded);
}

namespace Cangjie::Sema::Desugar::AfterTypeCheck {
SemanticInfo GetSemanticUsage(TypeManager& typeManager, const std::vector<Ptr<Package>>& pkgs);
} // namespace Cangjie::Sema::Desugar::AfterTypeCheck

TEST_F(PackageTest, IncrementalMergedSemanticUsage)
{
    diag.ClearError();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->invocation.globalOptions.implicitPrelude = true;
    instance->invocation.globalOptions.enIncrementalCompilation = true;
    instance->code = R"(
        package pkg
        import std.core
        var a: Any = 1
        var b: Rune = 'a'
        open class A {
            A(var a!: Int64 = 2, var b!: E = E1) {}
        }
        extend A <: core.ToString {
            public func toString() { "A" }
        }
        extend A <: Hashable {
            public func hashCode(): Int64 {1}
            static func coo() : String {"static"}
        }
        class B <: A {
            func test() {
                toString()
            }
        }
        extend Int64 {
            func foo() {
                a = A().toString() + B.coo()
            }
        }
        enum E {
            E1 | E2(A)
        }
        main() {
            1.foo()
        }
    )";
    bool ret = instance->Compile(CompileStage::DESUGAR_AFTER_SEMA);
    auto pkg = instance->GetSourcePackages()[0];
    ASSERT_TRUE(pkg != nullptr);
    ret = ret && instance->PerformDesugarAfterSema();
    ASSERT_TRUE(ret);
    EXPECT_EQ(diag.GetErrorCount(), 0);
    // Test for store useInfo in incremental compilation with unchanged decls.
    auto preInfo = instance->cachedInfo.semaInfo;
    // Serialize.
    HashedASTWriter writer;
    writer.SetSemanticInfo(preInfo);
    auto data = writer.AST2FB(pkg->fullPackageName);
    // Deserialize.
    auto hashedPackage = CachedASTFormat::GetHashedPackage(data.data());
    // 1. Update to data status as loaded version.
    instance->cachedInfo.semaInfo = HashedASTLoader::LoadSemanticInfos(*hashedPackage, instance->rawMangleName2DeclMap);
    auto markDecl = [](Ptr<Decl> d) {
        d->toBeCompiled = false;
        if (!d->IsNominalDecl() && !Utils::In(d->astKind, {ASTKind::PROP_DECL, ASTKind::PRIMARY_CTOR_DECL})) {
            d->EnableAttr(Attribute::INCRE_COMPILE);
        }
    };
    // 2. construct cache for unchanged incremental compilation.
    Ptr<Decl> classA = nullptr;
    IterateToplevelDecls(*pkg, [&markDecl, &classA](auto& decl) {
        auto d = decl.get();
        if (auto md = DynamicCast<MainDecl*>(d); md && md->desugarDecl) {
            d = md->desugarDecl.get();
        } else if (auto macroDecl = DynamicCast<MacroDecl*>(d); macroDecl && macroDecl->desugarDecl) {
            d = macroDecl->desugarDecl.get();
        }
        markDecl(d);
        if (d->identifier == "A") {
            classA = d;
        }
        for (auto& member : decl->GetMemberDecls()) {
            if (auto pd = DynamicCast<PropDecl*>(member.get())) {
                std::for_each(pd->getters.begin(), pd->getters.end(), [&markDecl](auto& it) { markDecl(it.get()); });
                std::for_each(pd->setters.begin(), pd->setters.end(), [&markDecl](auto& it) { markDecl(it.get()); });
            } else {
                markDecl(member.get());
            }
        }
    });
    if (classA) {
        auto& members = classA->GetMemberDecls();
        for (size_t i = 1; i < members.size(); ++i) {
            auto originDecl = instance->rawMangleName2DeclMap[members[i]->rawMangleName];
            // Since decl has been desugared, we need to update sema cache to new decl.
            auto found = instance->cachedInfo.semaInfo.usages.find(originDecl);
            if (found != instance->cachedInfo.semaInfo.usages.end()) {
                instance->cachedInfo.semaInfo.usages.emplace(members[i].get(), found->second);
                instance->cachedInfo.semaInfo.usages.erase(originDecl);
            }
        }
    }
    // 3. Expect same usage infos.
    using namespace Cangjie::Sema::Desugar::AfterTypeCheck;
    instance->CacheSemaUsage(GetSemanticUsage(*instance->typeManager, instance->GetSourcePackages()));
    auto incrInfo = instance->cachedInfo.semaInfo;
    EXPECT_EQ(preInfo.usages.size(), incrInfo.usages.size());
    EXPECT_EQ(preInfo.relations.size(), incrInfo.relations.size());
    EXPECT_EQ(preInfo.builtInTypeRelations.size(), incrInfo.builtInTypeRelations.size());
    for (auto& [decl, usage] : preInfo.usages) {
        auto found = incrInfo.usages.find(decl);
        ASSERT_TRUE(found != incrInfo.usages.end());
        EXPECT_EQ(usage.apiUsages.usedDecls.size(), found->second.apiUsages.usedDecls.size());
        EXPECT_EQ(usage.apiUsages.usedNames.size(), found->second.apiUsages.usedNames.size());
        EXPECT_EQ(usage.bodyUsages.usedDecls.size(), found->second.bodyUsages.usedDecls.size());
        EXPECT_EQ(usage.bodyUsages.usedNames.size(), found->second.bodyUsages.usedNames.size());
    }
}

TEST_F(PackageTest, UsageOfTypeAlias)
{
    diag.ClearError();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->invocation.globalOptions.implicitPrelude = true;
    instance->invocation.globalOptions.enIncrementalCompilation = true;
    instance->code = R"(
        interface interfaceA<T, K>{}
        type A<T>= interfaceA<T, T>
        class B<T> <: A <T> {}
        main() {}
    )";
    bool ret = instance->Compile(CompileStage::DESUGAR_AFTER_SEMA);
    auto pkg = instance->GetSourcePackages()[0];
    ASSERT_TRUE(pkg != nullptr);
    ret = ret && instance->PerformDesugarAfterSema();
    ASSERT_TRUE(ret);
    EXPECT_EQ(diag.GetErrorCount(), 0);
    bool checked = false;
    auto info = instance->cachedInfo.semaInfo;
    for (auto& [decl, use] : info.usages) {
        if (decl->identifier == "B") {
            checked = true;
            EXPECT_TRUE(use.apiUsages.usedNames.count("A") != 0);
            EXPECT_TRUE(use.apiUsages.usedDecls.count("7default10interfaceA<2>I") != 0);
        }
    }
    EXPECT_TRUE(checked);
}

TEST_F(PackageTest, UsageOfOnTheLeftOfPipeline)
{
    diag.ClearError();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->invocation.globalOptions.implicitPrelude = true;
    instance->invocation.globalOptions.enIncrementalCompilation = true;
    instance->code = R"(
        open class A{
            public var b :Int64 = 0
            public func a(input:Int64){input}
        }

        class B <: A {
            public func test() {
                super.b |> super.a
            }
        }
        main() {}
    )";
    bool ret = instance->Compile(CompileStage::DESUGAR_AFTER_SEMA);
    auto pkg = instance->GetSourcePackages()[0];
    ASSERT_TRUE(pkg != nullptr);
    ASSERT_TRUE(ret);
    EXPECT_EQ(diag.GetErrorCount(), 0);
    bool checked = false;
    auto info = instance->cachedInfo.semaInfo;
    for (auto& [decl, use] : info.usages) {
        if (decl->identifier == "test") {
            checked = true;
            EXPECT_TRUE(use.bodyUsages.usedDecls.count("7default1AC1aFl$$") != 0);
            EXPECT_TRUE(use.bodyUsages.usedDecls.count("7default1AC1bV$$l") != 0);
        }
    }
    EXPECT_TRUE(checked);
}

TEST_F(PackageTest, LoadAnnotationTarget)
{
    diag.ClearError();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->invocation.globalOptions.implicitPrelude = true;
    instance->invocation.globalOptions.enIncrementalCompilation = true;
    instance->code = R"(
        @Annotation[target: [Init, Type]]
        public class A {
            public const init() {}
        }
        main() {}
    )";
    bool ret = instance->Compile(CompileStage::DESUGAR_AFTER_SEMA);
    auto pkg = instance->GetSourcePackages()[0];
    ASSERT_TRUE(pkg != nullptr);
    ret = ret && instance->PerformDesugarAfterSema();
    ASSERT_TRUE(ret);
    EXPECT_EQ(diag.GetErrorCount(), 0);
    auto classA = pkg->files[0]->decls[0].get();
    auto& annotations = classA->annotations;
    auto found = std::find_if(
        annotations.begin(), annotations.end(), [](const auto& it) { return it->kind == AnnotationKind::ANNOTATION; });
    ASSERT_TRUE(found != annotations.end());
    classA->EnableAttr(Attribute::IS_ANNOTATION);
    found->get()->target = 3; // Set to 3.
    std::map<std::string, Ptr<Decl>> mangledName2DeclMap;
    for (auto [ident, decl] : instance->rawMangleName2DeclMap) {
        mangledName2DeclMap.emplace(ident, const_cast<Decl*>(decl.get()));
    }
    auto data = instance->importManager->ExportASTSignature(*pkg);
    // Unset annotation status.
    classA->DisableAttr(Attribute::IS_ANNOTATION);
    found->get()->target = 0;

    // Test for loading annotation status.
    pkg->EnableAttr(Attribute::INCRE_COMPILE);
    auto loader = std::make_unique<ASTLoader>(std::move(data), pkg->fullPackageName, *instance->typeManager,
        *instance->importManager->cjoManager, instance->invocation.globalOptions);
    (void)loader->LoadCachedTypeForPackage(*pkg, mangledName2DeclMap);
    EXPECT_TRUE(classA->TestAttr(Attribute::IS_ANNOTATION));
    EXPECT_EQ(found->get()->target, 3);
}
#endif

TEST_F(PackageTest, LoadPackageFromCjo)
{
    CompilerInvocation testInvocation;
#ifdef __x86_64__
    testInvocation.globalOptions.target.arch = Cangjie::Triple::ArchType::X86_64;
#else
    testInvocation.globalOptions.target.arch = Cangjie::Triple::ArchType::AARCH64;
#endif
#ifdef _WIN32
    testInvocation.globalOptions.target.os = Cangjie::Triple::OSType::WINDOWS;
#elif defined(__unix__)
    testInvocation.globalOptions.target.os = Cangjie::Triple::OSType::LINUX;
#endif
    DiagnosticEngine testDiag;
    auto testIns = std::make_unique<TestCompilerInstance>(testInvocation, testDiag);
    testIns->invocation.globalOptions.implicitPrelude = true;
    testIns->code = "main() {}";
    testIns->Compile(CompileStage::IMPORT_PACKAGE);

    std::unordered_map<std::string, std::string> fullPkgName2CjoFileNames = {
        {"classdecl", "classdecl.cjo"},
        {"extenddecl", "extenddecl.cjo"},
        {"funcdecl", "funcdecl.cjo"},
        {"vardecl", "vardecl.cjo"},
        {"recorddecl", "recorddecl.cjo"},
    };
    std::unordered_map<std::string, Ptr<Package>> pkgName2PkgNodeMap;

    auto checkPkg = [this, &testIns, &pkgName2PkgNodeMap](const std::string& pkgName, const std::string& cjoFileName) {
        auto pkg = testIns->importManager->LoadPackageFromCjo(pkgName, FileUtil::JoinPath(packagePath, cjoFileName));
        EXPECT_NE(pkg, nullptr);
        auto found = std::as_const(pkgName2PkgNodeMap).find(pkgName);
        if (found == pkgName2PkgNodeMap.cend()) {
            pkgName2PkgNodeMap.emplace(pkgName, pkg);
        } else {
            EXPECT_EQ(pkg, found->second);
        }
        EXPECT_GT(pkg->files.size(), 0);
        for (auto& file : std::as_const(pkg->files)) {
            if (file->decls.size() > 0) {
                for (auto& decl : std::as_const(file->decls)) {
                    EXPECT_TRUE(decl->GetTy().IsCorrect());
                }
            }
        }
    };
    for (size_t i = 0; i < 2; ++i) {
        for (auto& [fullPkgName, cjoFileName] : std::as_const(fullPkgName2CjoFileNames)) {
            checkPkg(fullPkgName, cjoFileName);
        }
    }
}

// Helper: locate the in-buffer byte offset of the CjoVersion struct inside a serialized Package.
// Returns a pointer to the major/minor/patch triple within 'data', or nullptr if absent.
static uint8_t* MutableCjoVersionBytes(std::vector<uint8_t>& data)
{
    auto* pkg = PackageFormat::GetPackage(data.data());
    if (pkg == nullptr || pkg->cjoVersion() == nullptr) {
        return nullptr;
    }
    // CjoVersion is an inline struct field of the Package table. Its in-buffer address equals
    // the address returned by the accessor (which points directly into 'data').
    return const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(pkg->cjoVersion()));
}

// The cjo format version is a contract over the serialized structure (schema/CjoFormat.fbs).
// Rules:
//   - major: exact match, any mismatch is incompatible (no forward/backward compatibility);
//   - minor: only backward compatible, a cjo whose minor is greater than the compiler's is refused;
//   - patch: never participates in the check.
// The compatibility decision (CheckCjoVersionCompat) and the version-branching primitives
// (CompareCjoVersion, VersionAtLeast/AtMost/Exactly on raw versions) are internal to the
// ASTLoader load pipeline (anonymous namespace of ASTLoader.cpp), so the rule is tested through
// the public ASTLoader queries: "cjo (M, m) is accepted by the loader" is equivalent to
// "loader.IsCjoVersionAtMost(M, m)" for a loader whose cjo carries version (CJO_MAJOR_VERSION,
// CJO_MINOR_VERSION); the branching predicates are exercised against mutated versions.
TEST_F(PackageTest, CjoVersionCompatLogic)
{
    // The expectations below are written against the compiler's own constants so they stay
    // valid when CJO_MAJOR_VERSION / CJO_MINOR_VERSION are bumped.
    diag.ClearError();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->invocation.globalOptions.implicitPrelude = true;
    instance->code = "import vardecl.*";
    instance->Compile(CompileStage::IMPORT_PACKAGE);
    ASSERT_FALSE(instance->GetSourcePackages().empty());
    std::vector<uint8_t> cjoData;
    instance->importManager->ExportAST(false, cjoData, *instance->GetSourcePackages()[0]);
    ASSERT_FALSE(cjoData.empty());
    auto cjoManager = instance->importManager->GetCjoManager();
    ASSERT_NE(cjoManager, nullptr);

    auto makeLoader = [this, &cjoManager](std::vector<uint8_t> data) {
        auto loader = std::make_unique<ASTLoader>(std::move(data), "vercheck_logic", *instance->typeManager,
            *cjoManager, instance->invocation.globalOptions);
        diag.Reset();
        (void)loader->LoadPackageDepInfo();
        EXPECT_EQ(diag.GetErrorCount(), 0u);
        return loader;
    };
    // Variant that expects the load to be refused by the version gate.
    auto makeRefusedLoader = [this, &cjoManager](std::vector<uint8_t> data) {
        auto loader = std::make_unique<ASTLoader>(std::move(data), "vercheck_logic", *instance->typeManager,
            *cjoManager, instance->invocation.globalOptions);
        diag.Reset();
        EXPECT_EQ(loader->LoadPackageDepInfo(), "");
        EXPECT_GT(diag.GetErrorCount(), 0u);
        return loader;
    };

    // A cjo with an older minor is accepted and reports AtMost(current) == true, Exactly(current)
    // == false: same major, minor <= compiler's minor -> backward compatible (patch ignored).
    {
        std::vector<uint8_t> mutated = cjoData;
        auto* ver = MutableCjoVersionBytes(mutated);
        ASSERT_NE(ver, nullptr);
        ver[1] = 0;
        auto* mutatedPackage = PackageFormat::GetPackage(mutated.data());
        ASSERT_NE(mutatedPackage, nullptr);
        ASSERT_NE(mutatedPackage->cjoVersion(), nullptr);
        EXPECT_EQ(mutatedPackage->cjoVersion()->major_num(), CJO_MAJOR_VERSION);
        EXPECT_EQ(mutatedPackage->cjoVersion()->minor_num(), 0u);
        EXPECT_EQ(ver[0], CJO_MAJOR_VERSION);
        EXPECT_EQ(ver[1], 0u);
        auto loader = makeLoader(std::move(mutated));
        EXPECT_TRUE(loader->IsCjoVersionAtMost(CJO_MAJOR_VERSION, CJO_MINOR_VERSION));
        EXPECT_FALSE(loader->IsCjoVersionExactly(CJO_MAJOR_VERSION, CJO_MINOR_VERSION));
        EXPECT_TRUE(loader->IsCjoVersionExactly(CJO_MAJOR_VERSION, 0));
    }

    // Same major but minor newer than the compiler -> produced by a newer compiler, refused:
    // the gate rejects the load and emits the version-mismatch error. The load through
    // LoadPackageDepInfo also proves the gate runs on this entry point, while the refusal
    // itself is covered end to end by CjoVersionCheckOnLoad case 3.
    {
        std::vector<uint8_t> mutated = cjoData;
        auto* ver = MutableCjoVersionBytes(mutated);
        ASSERT_NE(ver, nullptr);
        ver[1] = static_cast<uint8_t>(CJO_MINOR_VERSION + 1);
        auto loader = makeRefusedLoader(std::move(mutated));
        EXPECT_FALSE(loader->HasCjoVersion() && loader->IsCjoVersionAtMost(CJO_MAJOR_VERSION, CJO_MINOR_VERSION));
    }

    // The current-version cjo satisfies AtLeast(0.0)/AtMost(future 0.2)/Exactly(current), and a
    // requirement on a different major is never satisfied: cross-major versions have no defined
    // ordering, so any predicate with a foreign-major requirement returns false.
    {
        auto loader = makeLoader(cjoData);
        EXPECT_TRUE(loader->IsCjoVersionAtLeast(CJO_MAJOR_VERSION, 0));
        EXPECT_TRUE(loader->IsCjoVersionAtMost(CJO_MAJOR_VERSION, static_cast<uint8_t>(CJO_MINOR_VERSION + 1)));
        EXPECT_TRUE(loader->IsCjoVersionExactly(CJO_MAJOR_VERSION, CJO_MINOR_VERSION));
        EXPECT_FALSE(loader->IsCjoVersionAtLeast(static_cast<uint8_t>(CJO_MAJOR_VERSION + 1), 0));
        EXPECT_FALSE(loader->IsCjoVersionExactly(static_cast<uint8_t>(CJO_MAJOR_VERSION + 1), CJO_MINOR_VERSION));
    }

    diag.Reset();
}

// End-to-end: a cjo written by the current compiler carries version (0, 1, 0) and must load
// silently; a cjo whose version was bumped to an incompatible value must be rejected with a
// diagnostic, exercising the read-side guard in ASTLoader::ASTLoaderImpl::CheckCjoVersion.
TEST_F(PackageTest, CjoVersionCheckOnLoad)
{
    diag.ClearError();
    // Compile one source package and export its .cjo (written with the current version 0.1.0).
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->invocation.globalOptions.implicitPrelude = true;
    instance->code = "import vardecl.*";
    instance->Compile(CompileStage::IMPORT_PACKAGE);
    ASSERT_FALSE(instance->GetSourcePackages().empty());
    std::vector<uint8_t> cjoData;
    instance->importManager->ExportAST(false, cjoData, *instance->GetSourcePackages()[0]);
    ASSERT_FALSE(cjoData.empty());
    std::string cjoFile = FileUtil::JoinPath(packagePath, "vercheck.cjo");
    ASSERT_TRUE(FileUtil::WriteBufferToASTFile(cjoFile, cjoData));

    // Sanity: the exported cjo carries the current cjo format version.
    {
        std::vector<uint8_t> copy = cjoData;
        auto* ver = MutableCjoVersionBytes(copy);
        ASSERT_NE(ver, nullptr);
        EXPECT_EQ(ver[0], CJO_MAJOR_VERSION);
        EXPECT_EQ(ver[1], CJO_MINOR_VERSION);
        EXPECT_EQ(ver[2], CJO_PATCH_VERSION);
    }

    // Each load below runs against the shared compiler instance. A version-mismatch rejection
    // happens inside LoadPackageDependencies (before any dependency is loaded), so it returns
    // false without registering the package, leaving no state behind in the CjoManager.
    // The signal of acceptance vs. rejection is the return value of LoadPackageFromCjo (nullptr
    // when LoadPackageHeader fails), which is also what the existing LoadPackageFromCjo test relies on.

    // 1) Compatible: load the freshly produced cjo, expect success.
    {
        diag.ClearError();
        auto pkg = instance->importManager->LoadPackageFromCjo("vercheck", cjoFile);
        ASSERT_NE(pkg, nullptr);
        EXPECT_GT(pkg->files.size(), 0u);
    }

    // 2) Major mismatch: bump major to 1.x -> refused (nullptr).
    {
        std::vector<uint8_t> mutated = cjoData;
        auto* ver = MutableCjoVersionBytes(mutated);
        ASSERT_NE(ver, nullptr);
        ver[0] = static_cast<uint8_t>(CJO_MAJOR_VERSION + 1); // major mismatch
        std::string badFile = FileUtil::JoinPath(packagePath, "vercheck_major.cjo");
        ASSERT_TRUE(FileUtil::WriteBufferToASTFile(badFile, mutated));
        diag.Reset();
        auto pkg = instance->importManager->LoadPackageFromCjo("vercheck_major", badFile);
        EXPECT_EQ(pkg, nullptr);
        EXPECT_GT(diag.GetErrorCount(), 0u);
    }

    // 3) Minor too new: same major, minor greater than the compiler's -> refused (nullptr).
    {
        std::vector<uint8_t> mutated = cjoData;
        auto* ver = MutableCjoVersionBytes(mutated);
        ASSERT_NE(ver, nullptr);
        ver[1] = static_cast<uint8_t>(CJO_MINOR_VERSION + 1); // minor too new
        std::string badFile = FileUtil::JoinPath(packagePath, "vercheck_minor.cjo");
        ASSERT_TRUE(FileUtil::WriteBufferToASTFile(badFile, mutated));
        diag.Reset();
        auto pkg = instance->importManager->LoadPackageFromCjo("vercheck_minor", badFile);
        EXPECT_EQ(pkg, nullptr);
        EXPECT_GT(diag.GetErrorCount(), 0u);
    }

    // 4) Older minor (same major) -> backward compatible, loads successfully.
    {
        std::vector<uint8_t> mutated = cjoData;
        auto* ver = MutableCjoVersionBytes(mutated);
        ASSERT_NE(ver, nullptr);
        ver[1] = 0; // older minor, same major -> compatible
        std::string oldFile = FileUtil::JoinPath(packagePath, "vercheck_old_minor.cjo");
        ASSERT_TRUE(FileUtil::WriteBufferToASTFile(oldFile, mutated));
        diag.Reset();
        auto pkg = instance->importManager->LoadPackageFromCjo("vercheck_old_minor", oldFile);
        ASSERT_NE(pkg, nullptr);
        EXPECT_GT(pkg->files.size(), 0u);
    }

    diag.Reset();
}

TEST_F(PackageTest, CjoValidationDiagnosticIncludesProducerVersion)
{
    diag.ClearError();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->invocation.globalOptions.implicitPrelude = true;
    instance->code = R"(
        internal class InternalType {
            internal func value(): Int64 { return 0 }
        }
    )";
    ASSERT_TRUE(instance->Compile(CompileStage::DESUGAR_AFTER_SEMA));
    ASSERT_FALSE(instance->GetSourcePackages().empty());

    std::vector<uint8_t> cjoData;
    instance->importManager->ExportAST(false, cjoData, *instance->GetSourcePackages()[0]);
    ASSERT_FALSE(cjoData.empty());
    auto* package = PackageFormat::GetPackage(cjoData.data());
    ASSERT_NE(package, nullptr);
    auto* producerVersion = package->version();
    ASSERT_NE(producerVersion, nullptr);
    auto* mutableProducerVersion = const_cast<char*>(producerVersion->c_str());
    mutableProducerVersion[0] = mutableProducerVersion[0] == '9' ? '8' : '9';
    std::string expectedProducerVersion = producerVersion->str();
    auto* fullPkgName = package->fullPkgName();
    ASSERT_NE(fullPkgName, nullptr);

    // Keep the root table and producer version valid, but make another string invalid so that
    // full verification fails after the diagnostic-only version check can still succeed.
    auto* fullPkgNameLength = const_cast<uint32_t*>(
        reinterpret_cast<const uint32_t*>(fullPkgName) - 1);
    *fullPkgNameLength = std::numeric_limits<uint32_t>::max();

    auto loader = std::make_unique<ASTLoader>(std::move(cjoData), "diagnostic.package", *instance->typeManager,
        *instance->importManager->cjoManager, instance->invocation.globalOptions);
    loader->SetCjoPath("diagnostic.cjo");
    diag.Reset();
    EXPECT_EQ(loader->LoadPackageDepInfo(), "");

    auto diagnostics = diag.GetCategoryDiagnostic(DiagCategory::MODULE);
    ASSERT_EQ(diagnostics.size(), 1u);
    EXPECT_EQ(diagnostics[0].rKind, DiagKindRefactor::module_loaded_ast_failed_with_version);
    EXPECT_NE(diagnostics[0].errorMessage.find("compiled with cjc '" + expectedProducerVersion + "'"),
        std::string::npos);
    EXPECT_NE(diagnostics[0].errorMessage.find("current compiler is cjc '" + std::string(CANGJIE_VERSION) + "'"),
        std::string::npos);
    EXPECT_NE(diagnostics[0].errorMessage.find("diagnostic.package"), std::string::npos);
}

TEST_F(PackageTest, CjoValidationDiagnosticFallsBackForSameProducerVersion)
{
    diag.ClearError();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->invocation.globalOptions.implicitPrelude = true;
    instance->code = R"(
        internal class SameVersionType {
            internal func value(): Int64 { return 0 }
        }
    )";
    ASSERT_TRUE(instance->Compile(CompileStage::DESUGAR_AFTER_SEMA));
    ASSERT_FALSE(instance->GetSourcePackages().empty());

    std::vector<uint8_t> cjoData;
    instance->importManager->ExportAST(false, cjoData, *instance->GetSourcePackages()[0]);
    ASSERT_FALSE(cjoData.empty());
    auto* package = PackageFormat::GetPackage(cjoData.data());
    ASSERT_NE(package, nullptr);
    ASSERT_NE(package->version(), nullptr);
    ASSERT_EQ(package->version()->str(), std::string(CANGJIE_VERSION));
    auto* fullPkgName = package->fullPkgName();
    ASSERT_NE(fullPkgName, nullptr);

    // Keep the producer version unchanged and corrupt another string so that full verification
    // fails while the diagnostic-only version check can still read the current version.
    auto* fullPkgNameLength = const_cast<uint32_t*>(
        reinterpret_cast<const uint32_t*>(fullPkgName) - 1);
    *fullPkgNameLength = std::numeric_limits<uint32_t>::max();

    auto loader = std::make_unique<ASTLoader>(std::move(cjoData), "same-version.package", *instance->typeManager,
        *instance->importManager->cjoManager, instance->invocation.globalOptions);
    loader->SetCjoPath("same-version.cjo");
    diag.Reset();
    EXPECT_EQ(loader->LoadPackageDepInfo(), "");

    auto diagnostics = diag.GetCategoryDiagnostic(DiagCategory::MODULE);
    ASSERT_EQ(diagnostics.size(), 1u);
    EXPECT_EQ(diagnostics[0].rKind, DiagKindRefactor::module_loaded_ast_failed);
    EXPECT_NE(diagnostics[0].errorMessage.find("compiler whose version is '" + std::string(CANGJIE_VERSION) + "'"),
        std::string::npos);
}

TEST_F(PackageTest, CjoValidationDiagnosticFallsBackWhenProducerVersionIsUnavailable)
{
    diag.ClearError();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    auto cjoManager = instance->importManager->GetCjoManager();
    ASSERT_NE(cjoManager, nullptr);

    std::vector<uint8_t> invalidCjo(sizeof(flatbuffers::uoffset_t), 0);
    auto loader = std::make_unique<ASTLoader>(std::move(invalidCjo), "diagnostic.package", *instance->typeManager,
        *cjoManager, instance->invocation.globalOptions);
    loader->SetCjoPath("diagnostic.cjo");
    diag.Reset();
    EXPECT_EQ(loader->LoadPackageDepInfo(), "");

    auto diagnostics = diag.GetCategoryDiagnostic(DiagCategory::MODULE);
    ASSERT_EQ(diagnostics.size(), 1u);
    EXPECT_EQ(diagnostics[0].rKind, DiagKindRefactor::module_loaded_ast_failed);
    EXPECT_NE(diagnostics[0].errorMessage.find("compiler whose version is '" + std::string(CANGJIE_VERSION) + "'"),
        std::string::npos);
}

// Incremental-compilation cache path: an incompatible cache cjo must be rejected *silently* —
// no diagnostic is emitted and no cached type is consumed (sema keeps running incrementally,
// there is no rollback to a full recompile). Compatible caches are still consumed.
TEST_F(PackageTest, CjoVersionCheckOnIncrCache)
{
    diag.ClearError();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->invocation.globalOptions.implicitPrelude = true;
    instance->code = R"(
        public class CachedType {
            public func value(): Int64 { return 0 }
        }
    )";
    ASSERT_TRUE(instance->Compile(CompileStage::DESUGAR_AFTER_SEMA));
    ASSERT_FALSE(instance->GetSourcePackages().empty());
    auto srcPkg = instance->GetSourcePackages()[0];
    std::map<std::string, Ptr<Decl>> mangledName2DeclMap;
    for (auto [ident, decl] : instance->rawMangleName2DeclMap) {
        mangledName2DeclMap.emplace(ident, const_cast<Decl*>(decl.get()));
    }
    auto cacheData = instance->importManager->ExportASTSignature(*srcPkg);
    ASSERT_FALSE(cacheData.empty());
    srcPkg->EnableAttr(Attribute::INCRE_COMPILE);

    auto* cachedPackage = PackageFormat::GetPackage(cacheData.data());
    ASSERT_NE(cachedPackage, nullptr);
    ASSERT_NE(cachedPackage->allDecls(), nullptr);
    ASSERT_GT(cachedPackage->allDecls()->size(), 0u);
    auto* cachedDecl = cachedPackage->allDecls()->Get(0);
    ASSERT_NE(cachedDecl, nullptr);
    ASSERT_NE(cachedDecl->mangledBeforeSema(), nullptr);
    auto compatibleDeclMap = mangledName2DeclMap;
    compatibleDeclMap.erase(cachedDecl->mangledBeforeSema()->str());

    auto loadCache = [this, &srcPkg](std::vector<uint8_t> data,
                           const std::map<std::string, Ptr<Decl>>& declMap) {
        auto loader = std::make_unique<ASTLoader>(std::move(data), srcPkg->fullPackageName, *instance->typeManager,
            *instance->importManager->cjoManager, instance->invocation.globalOptions);
        return loader->LoadCachedTypeForPackage(*srcPkg, declMap);
    };

    // Removing a declaration serialized in the cache makes an accepted cache report it as
    // unfounded, while an incompatible cache returns before inspecting the map.
    //
    // 1) Major mismatch cache: rejected silently, no diagnostic, and no removed declarations.
    {
        std::vector<uint8_t> mutated = cacheData;
        auto* ver = MutableCjoVersionBytes(mutated);
        ASSERT_NE(ver, nullptr);
        ver[0] = static_cast<uint8_t>(CJO_MAJOR_VERSION + 1);
        diag.Reset();
        auto unfounded = loadCache(std::move(mutated), compatibleDeclMap);
        EXPECT_TRUE(unfounded.empty());
        EXPECT_EQ(diag.GetErrorCount(), 0u); // degrades silently, no error
    }

    // 2) Minor too new cache: rejected silently, no diagnostic, and no removed declarations.
    {
        std::vector<uint8_t> mutated = cacheData;
        auto* ver = MutableCjoVersionBytes(mutated);
        ASSERT_NE(ver, nullptr);
        ver[1] = static_cast<uint8_t>(CJO_MINOR_VERSION + 1);
        diag.Reset();
        auto unfounded = loadCache(std::move(mutated), compatibleDeclMap);
        EXPECT_TRUE(unfounded.empty());
        EXPECT_EQ(diag.GetErrorCount(), 0u);
    }

    // 3) Compatible cache: accepted without a diagnostic and reports the deliberately removed
    // declaration as unfounded.
    {
        diag.Reset();
        auto unfounded = loadCache(cacheData, compatibleDeclMap);
        EXPECT_FALSE(unfounded.empty());
        EXPECT_EQ(diag.GetErrorCount(), 0u);
    }
}

// ASTLoader version-branching API: after loading a cjo the reader can query the cjo's version to
// decide which semantic to apply. We build an ASTLoader directly
// from cjo bytes produced by the current compiler, run LoadPackageDepInfo() (the lightest path
// that assigns 'package' and passes the version check), then query GetCjoVersion /
// HasCjoVersion / IsCjoVersionAtLeast / AtMost / Exactly. We mutate the version bytes to exercise
// the predicates against an older and a same-version cjo.
TEST_F(PackageTest, CjoVersionQueryOnLoader)
{
    diag.ClearError();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->invocation.globalOptions.implicitPrelude = true;
    instance->code = "import vardecl.*";
    instance->Compile(CompileStage::IMPORT_PACKAGE);
    ASSERT_FALSE(instance->GetSourcePackages().empty());
    std::vector<uint8_t> cjoData;
    instance->importManager->ExportAST(false, cjoData, *instance->GetSourcePackages()[0]);
    ASSERT_FALSE(cjoData.empty());
    auto* originalVersion = MutableCjoVersionBytes(cjoData);
    ASSERT_NE(originalVersion, nullptr);
    EXPECT_EQ(originalVersion[0], CJO_MAJOR_VERSION);
    EXPECT_EQ(originalVersion[1], CJO_MINOR_VERSION);

    auto cjoManager = instance->importManager->GetCjoManager();
    ASSERT_NE(cjoManager, nullptr);

    // 1) Freshly produced cjo carries the current version (0.1.0). After LoadPackageDepInfo the
    //    loader has read the header and the version queries reflect the cjo's version.
    {
        std::vector<uint8_t> copy = cjoData;
        auto* ver = MutableCjoVersionBytes(copy);
        ASSERT_NE(ver, nullptr);
        ASTLoader loader(std::move(copy), "vercheck_q", *instance->typeManager, *cjoManager,
            instance->invocation.globalOptions);
        diag.Reset();
        // LoadPackageDepInfo runs VerifyForData + CheckCjoVersion (compatible) and assigns 'package'.
        (void)loader.LoadPackageDepInfo();
        EXPECT_EQ(diag.GetErrorCount(), 0u);

        EXPECT_TRUE(loader.HasCjoVersion());
        auto trip = loader.GetCjoVersion();
        EXPECT_EQ(trip.major, CJO_MAJOR_VERSION);
        EXPECT_EQ(trip.minor, CJO_MINOR_VERSION);
        EXPECT_EQ(trip.patch, CJO_PATCH_VERSION);

        // Branching predicates against the current version as the requirement.
        EXPECT_TRUE(loader.IsCjoVersionAtLeast(CJO_MAJOR_VERSION, CJO_MINOR_VERSION));
        EXPECT_TRUE(loader.IsCjoVersionAtMost(CJO_MAJOR_VERSION, CJO_MINOR_VERSION));
        EXPECT_TRUE(loader.IsCjoVersionExactly(CJO_MAJOR_VERSION, CJO_MINOR_VERSION));
        // The current cjo is at least version 0.0, at most a future 0.2, but not exactly 0.0.
        EXPECT_TRUE(loader.IsCjoVersionAtLeast(CJO_MAJOR_VERSION, 0));
        EXPECT_TRUE(loader.IsCjoVersionAtMost(CJO_MAJOR_VERSION, static_cast<uint8_t>(CJO_MINOR_VERSION + 1)));
        EXPECT_FALSE(loader.IsCjoVersionExactly(CJO_MAJOR_VERSION, 0));
        // A requirement on a different major is never satisfied.
        EXPECT_FALSE(loader.IsCjoVersionAtLeast(static_cast<uint8_t>(CJO_MAJOR_VERSION + 1), 0));
    }

    // 2) Older minor (same major): backward compatible, loads, and the version queries report the
    //    older minor. This is the case a newer compiler branches on to apply old semantics.
    {
        std::vector<uint8_t> copy = cjoData;
        auto* ver = MutableCjoVersionBytes(copy);
        ASSERT_NE(ver, nullptr);
        ver[1] = 0; // older minor 0.0.x
        ASTLoader loader(std::move(copy), "vercheck_old", *instance->typeManager, *cjoManager,
            instance->invocation.globalOptions);
        diag.Reset();
        (void)loader.LoadPackageDepInfo();
        EXPECT_EQ(diag.GetErrorCount(), 0u);
        EXPECT_TRUE(loader.HasCjoVersion());
        auto trip = loader.GetCjoVersion();
        EXPECT_EQ(trip.major, CJO_MAJOR_VERSION);
        EXPECT_EQ(trip.minor, 0u);

        // 0.0 is at least 0.0, at most 0.1, but NOT at least 0.1.
        EXPECT_TRUE(loader.IsCjoVersionAtLeast(CJO_MAJOR_VERSION, 0));
        EXPECT_TRUE(loader.IsCjoVersionAtMost(CJO_MAJOR_VERSION, CJO_MINOR_VERSION));
        EXPECT_FALSE(loader.IsCjoVersionAtLeast(CJO_MAJOR_VERSION, CJO_MINOR_VERSION));
        EXPECT_FALSE(loader.IsCjoVersionExactly(CJO_MAJOR_VERSION, CJO_MINOR_VERSION));
        EXPECT_TRUE(loader.IsCjoVersionExactly(CJO_MAJOR_VERSION, 0));
    }

    // 3) A cjo whose version was stripped (nullptr) reports HasCjoVersion()==false and the
    //    predicates all return false (cannot be proven to satisfy any in-major requirement).
    //    We cannot easily null out the inline struct in-buffer, so we instead verify the
    //    absence semantics on a loader that has not yet loaded any header: before
    //    LoadPackageDepInfo the queries are safe and report "no version".
    {
        std::vector<uint8_t> copy = cjoData;
        ASTLoader loader(std::move(copy), "vercheck_nohdr", *instance->typeManager, *cjoManager,
            instance->invocation.globalOptions);
        diag.Reset();
        // No LoadPackageDepInfo call: 'package' is still null.
        EXPECT_FALSE(loader.HasCjoVersion());
        EXPECT_FALSE(loader.IsCjoVersionAtLeast(CJO_MAJOR_VERSION, CJO_MINOR_VERSION));
        EXPECT_FALSE(loader.IsCjoVersionAtMost(CJO_MAJOR_VERSION, CJO_MINOR_VERSION));
        EXPECT_FALSE(loader.IsCjoVersionExactly(CJO_MAJOR_VERSION, CJO_MINOR_VERSION));
        auto trip = loader.GetCjoVersion();
        EXPECT_EQ(trip.major, 0u);
        EXPECT_EQ(trip.minor, 0u);
        EXPECT_EQ(diag.GetErrorCount(), 0u);
    }

    diag.Reset();
}

// ASTWriter version-branching API is intentionally NOT provided: write-side branching is out of
// scope for now (there is no mechanism yet to pre-plant handling of future incompatible changes),
// so the writer always stamps CJO_MAJOR/MINOR/PATCH_VERSION unconditionally.

TEST_F(PackageTest, ImportOptFlag)
{
    diag.ClearError();
    std::vector<std::string> fileNames = {"a", "b", "c", "d", "e", "f", "middle"};
    for (auto fileName : fileNames) {
        auto subDir = fileName == "middle" ? "middle" : "top";
        auto srcFile =
            FileUtil::JoinPath(FileUtil::JoinPath(srcPath, "ImportOpt" + DIR_SEPARATOR + subDir), fileName + ".cj");
        std::string failedReason;
        auto content = FileUtil::ReadFileContent(srcFile, failedReason);
        if (!content.has_value()) {
            diag.DiagnoseRefactor(
                DiagKindRefactor::module_read_file_to_buffer_failed, DEFAULT_POSITION, srcFile, failedReason);
        }
        instance = std::make_unique<TestCompilerInstance>(invocation, diag);
        instance->invocation.globalOptions.implicitPrelude = true;
        instance->code = std::move(content.value());
        instance->Compile();
        instance->PerformDesugarAfterSema();
        std::vector<uint8_t> astData;
        instance->importManager->ExportAST(false, astData, *instance->GetSourcePackages()[0]);
        std::string astFile = packagePath + fileName + ".cjo";
        FileUtil::WriteBufferToASTFile(astFile, astData);
        diag.Reset();
    }
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->code = R"(
        import middle.*
        main() {}
    )";
    bool ret = instance->Compile(CompileStage::IMPORT_PACKAGE);
    EXPECT_TRUE(ret);
    auto depPkgs = instance->importManager->GetAllImportedPackages();
    EXPECT_EQ(depPkgs.size(), 8); // 8 is size of {a, c, d, e, f, middle, core, default}
    auto found = std::find_if(
        depPkgs.begin(), depPkgs.end(), [](Ptr<PackageDecl>& pkg) { return pkg->GetFullPackageName() == "b"; });
    EXPECT_EQ(found, depPkgs.end());
}

TEST_F(PackageTest, TypeAliasRefExport)
{
    // Compile dependency.
    diag.ClearError();
    invocation.globalOptions.srcFiles = {
        FileUtil::JoinPath(srcPath, "TypeAliasRefExport/typealiases.cj"),
        FileUtil::JoinPath(srcPath, "TypeAliasRefExport/typealiasRef.cj"),
    };
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->invocation.globalOptions.implicitPrelude = true;
    instance->compileOnePackageFromSrcFiles = true;
    instance->Compile();
    instance->PerformDesugarAfterSema();
    std::vector<uint8_t> astData;
    instance->importManager->ExportAST(false, astData, *instance->GetSourcePackages()[0]);
    std::string astFile = FileUtil::JoinPath(packagePath, "dep.cjo");
    FileUtil::WriteBufferToASTFile(astFile, astData);
    diag.Reset();

    // Compile default package.
    invocation.globalOptions.srcFiles.clear();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->code = R"(
        import dep.*
        main() {
        }
    )";
    bool ret = instance->Compile(CompileStage::IMPORT_PACKAGE);
    EXPECT_TRUE(ret);
    auto depPkg = instance->importManager->GetPackage("dep");
    ASSERT_TRUE(depPkg != nullptr);
    const std::vector<std::string> orginNames{"A", "A_G", "B", "C", "C_1", "D", "D_G"};
    std::function<void(Ptr<Type>)> visitType = [&orginNames, &visitType](Ptr<Type> type) {
        if (!type) {
            return;
        }
        if (auto rt = DynamicCast<RefType>(type)) {
            EXPECT_FALSE(Utils::In(rt->ref.identifier.Val(), orginNames));
            for (auto& typeArg : rt->typeArguments) {
                visitType(typeArg.get());
            }
        } else if (auto ft = DynamicCast<FuncType>(type)) {
            for (auto& paramType : ft->paramTypes) {
                visitType(paramType.get());
            }
            visitType(ft->retType.get());
        } else if (auto tt = DynamicCast<TupleType>(type)) {
            for (auto& type : tt->fieldTypes) {
                visitType(type.get());
            }
        } else if (type->astKind == ASTKind::PRIMITIVE_TYPE) {
        } else {
            EXPECT_TRUE(false);
        }
    };
    for (auto& file : depPkg->files) {
        if (file->fileName != "typealiasRef.cj") {
            continue;
        }
        std::unordered_set<Ptr<Decl>> allDecls;
        for (auto& decl : file->exportedInternalDecls) {
            allDecls.emplace(decl.get());
        }
        for (auto& decl : file->decls) {
            allDecls.emplace(decl.get());
        }
        for (auto decl : allDecls) {
            if (decl->astKind >= ASTKind::VAR_DECL && decl->astKind <= ASTKind::VAR_WITH_PATTERN_DECL) {
                auto vda = StaticCast<VarDeclAbstract>(decl.get());
                visitType(vda->type.get());
            } else if (auto fd = DynamicCast<FuncDecl>(decl.get()); fd) {
                for (auto& param : fd->funcBody->paramLists[0]->params) {
                    visitType(param->type.get());
                }
                visitType(fd->funcBody->retType.get());
                if (fd->funcBody->generic) {
                    for (auto& constraint : fd->funcBody->generic->genericConstraints) {
                        for (auto& bound : constraint->upperBounds) {
                            visitType(bound.get());
                        }
                    }
                }
            } else if (decl->astKind >= ASTKind::CLASS_LIKE_DECL && decl->astKind <= ASTKind::STRUCT_DECL) {
                auto id = StaticCast<InheritableDecl>(decl.get());
                for (auto& inheritedType : id->inheritedTypes) {
                    visitType(inheritedType.get());
                }
                if (auto ed = DynamicCast<ExtendDecl>(decl.get()); ed) {
                    visitType(ed->extendedType.get());
                }
                if (id->generic) {
                    // ExtendDecl will add constraints for extended decl's constraints.
                    // class A<T> where T <: I1 {}
                    // extend<T> A<T> where T <: I2 {} ==> extend<T> A<T> where T <: I1, T <: I1 & I2 {}
                    //                                          Just check here ^^^^^^^
                    for (size_t i = 0; i < id->generic->typeParameters.size(); ++i) {
                        if (i >= id->generic->genericConstraints.size()) {
                            break;
                        }
                        for (auto& bound : id->generic->genericConstraints[i]->upperBounds) {
                            visitType(bound.get());
                        }
                    }
                }
            } else if (decl->astKind == ASTKind::TYPE_ALIAS_DECL) {
                auto tad = StaticCast<TypeAliasDecl>(decl.get());
                visitType(tad->type.get());
            }
        }
    }
    Walker(depPkg, [](Ptr<Node> node) {
        if (node->astKind != ASTKind::TYPE_ALIAS_DECL) {
            EXPECT_NE(node->TyKind(), TypeKind::TYPE);
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
}

// The imports of one dependency are held in a std::set ordered by (fileID, begin),
// and CmpNodeByPos only breaks remaining ties by heap address. Serializing that set
// therefore produced bytes that depended on the allocator. The pkgDepInfo import
// list must instead follow a canonical order derived from the rendered content, so
// this test checks that the emitted order does not follow the fileID order.
TEST_F(PackageTest, DeterministicPkgDepInfoImportOrder)
{
    diag.ClearError();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);

    auto pkg = MakeOwned<Package>();
    pkg->fullPackageName = "test_pkg";

    // Both files import the same package, so their imports share one
    // DependencyInfoItem. The larger file name is given the smaller fileID: the old
    // (fileID-ordered) set emits "z.cj" before "a.cj" while the canonical order
    // emits "a.cj" first.
    auto addFile = [&](const std::string& name, unsigned int fileID) {
        auto file = MakeOwned<File>();
        file->fileName = name;
        file->filePath = name;
        auto import = MakeOwned<ImportSpec>();
        import->content.kind = ImportKind::IMPORT_SINGLE;
        import->content.prefixPaths = {"foo"};
        import->content.identifier = "bar";
        import->begin = Position(fileID, 1, 0);
        import->end = Position(fileID, 1, 5);
        import->curFile = file.get();
        file->imports.emplace_back(std::move(import));
        pkg->files.emplace_back(std::move(file));
    };
    addFile("z.cj", 1);
    addFile("a.cj", 2);

    auto json = instance->importManager->GeneratePkgDepInfo(*pkg, true);
    auto posA = json.find("\"a.cj\"");
    auto posZ = json.find("\"z.cj\"");
    ASSERT_NE(posA, std::string::npos);
    ASSERT_NE(posZ, std::string::npos);
    EXPECT_LT(posA, posZ);
}
