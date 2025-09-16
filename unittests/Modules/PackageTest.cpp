// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "gtest/gtest.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>

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
#include "cangjie/IncrementalCompilation/IncrementalScopeAnalysis.h"
#include "cangjie/Modules/ASTSerialization.h"
#include "cangjie/Utils/FileUtil.h"

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
#elif __unix__
        invocation.globalOptions.target.os = Cangjie::Triple::OSType::LINUX;
#endif
        invocation.globalOptions.outputMode = GlobalOptions::OutputMode::STATIC_LIB;
        invocation.globalOptions.disableReflection = true;
        invocation.globalOptions.importPaths = {packagePath};
        invocation.globalOptions.compilationCachedPath = ".";
        for (unsigned int i = 0; i < fileNames.size(); i++) {
            auto srcFile = srcPath + fileNames[i] + ".cj";
            std::string failedReason;
            auto content = FileUtil::ReadFileContent(srcFile, failedReason);
            if (!content.has_value()) {
                diag.DiagnoseRefactor(
                    DiagKindRefactor::module_read_file_to_buffer_failed, DEFAULT_POSITION, srcFile, failedReason);
            }
            instance = std::make_unique<TestCompilerInstance>(invocation, diag);
            instance->code = std::move(content.value());
            instance->invocation.globalOptions.implicitPrelude = false;
            instance->Compile();

            std::vector<uint8_t> astData;
            instance->importManager.ExportAST(false, astData, *instance->GetSourcePackages()[0]);
            allAstData[fileNames[i]] = astData;
            std::string astFile = packagePath + fileNames[i] + ".cjo";
            ASSERT_TRUE(FileUtil::WriteBufferToASTFile(astFile, astData));
            diag.Reset();
        }
    }

#ifdef PROJECT_SOURCE_DIR
    // Gets the absolute path of the project from the compile parameter.
    std::string projectPath = PROJECT_SOURCE_DIR;
#else
    // Just in case, give it a default value.
    // Assume the initial is in the build directory.
    std::string projectPath = "..";
#endif
    std::vector<std::string> fileNames{"vardecl", "funcdecl", "math", "recorddecl", "interfacedecl", "classdecl"};
    std::vector<std::string> fullNames{
        "goodboy/vardecl", "badboy/funcdecl", "math", "badboy/recorddecl", "badboy/interfacedecl", "goodboy/classdecl"};
    std::string packagePath = "testTempFiles";
    std::string srcPath;
    DiagnosticEngine diag;
    CompilerInvocation invocation;
    std::unique_ptr<TestCompilerInstance> instance;
    std::unordered_map<std::string, std::vector<uint8_t>> allAstData;
};

// Test for package with only one cangjie file.
TEST_F(PackageTest, DISABLED_SingleFile_Package)
{
    std::vector<std::set<std::string>> expectDecls{{"a", "b", "c", "d"},
        {"fun1", "fun1", "fun2", "fun3", "fun4", "a...0", "b...0", "c...0", "a...1"}, {"plus"}, {"Rectangle"},
        {"I1", "I2"}, {"Base2", "Derive1", "I3", "Base3"}};

    for (unsigned int i = 0; i < fileNames.size(); i++) {
        instance = std::make_unique<TestCompilerInstance>(invocation, diag);
        instance->code = "import " + fileNames[i] + ".*";
        instance->Compile();
        Package* pkg = nullptr;
        for (auto pd : instance->importManager.GetAllImportedPackages()) {
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

TEST_F(PackageTest, DISABLED_SingleFile_LoadCache_Package)
{
    std::vector<std::set<std::string>> expectDecls{{"a", "b", "c", "d"},
        {"fun1", "fun1", "fun2", "fun3", "fun4", "a...0", "b...0", "c...0", "a...1"}, {"plus"}, {"Rectangle"},
        {"I1", "I2"}, {"Base2", "Derive1", "I3", "Base3"}};

    for (unsigned int i = 0; i < fileNames.size(); i++) {
        instance = std::make_unique<TestCompilerInstance>(invocation, diag);
        instance->importManager.SetPackageCjoCache(fileNames[i], allAstData[fileNames[i]]);
        instance->code = "import " + fileNames[i] + ".*";
        instance->Compile();

        Package* pkg = nullptr;
        for (auto pd : instance->importManager.GetAllImportedPackages()) {
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
TEST_F(PackageTest, DISABLED_MultiFile_Package)
{
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->srcDirs = {srcPath};
    instance->code = "import funcdecl.*";
    instance->Compile();

    diag.Reset();

    std::vector<uint8_t> astData;
    instance->importManager.ExportAST(false, astData, *instance->GetSourcePackages()[0]);
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
    for (auto pd : instance->importManager.GetAllImportedPackages()) {
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

TEST_F(PackageTest, DISABLED_ImportPackage)
{
#ifdef _WIN32
    srcPath = projectPath + "\\unittests\\Modules\\ImportPackage\\";
#elif __unix__
    srcPath = projectPath + "/unittests/Modules/ImportPackage/";
#endif

    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->srcDirs = {srcPath};
    for (unsigned int i = 0; i < fileNames.size(); i++) {
        instance->importManager.SetPackageCjoCache(fileNames[i], allAstData[fileNames[i]]);
    }
    instance->Compile();

    diag.Reset();

    std::set<std::string> expectImportSet{"Base2", "Base3", "Derive1", "I2", "I3", "Rectangle", "a", "classdecl",
        "fun1", "funcdecl", "interfacedecl", "math", "plus", "recorddecl", "vardecl"};
    std::set<std::string> importSet;
    auto pkg = instance->GetSourcePackages()[0];
    for (auto& file : pkg->files) {
        for (auto& [name, _] : instance->importManager.GetImportedDecls(*file)) {
            importSet.emplace(name);
        }
    }

    for (auto pd : instance->importManager.GetCurImportedPackages(instance->GetSourcePackages()[0]->fullPackageName)) {
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

TEST_F(PackageTest, DISABLED_LSPBasicCompileCost)
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
    instance->importManager.SetSourceCodeImportStatus(false);
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

TEST_F(PackageTest, DISABLED_LSPImportLotPkgsCompileCost)
{
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->invocation.globalOptions.implicitPrelude = true;
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
    instance->importManager.SetSourceCodeImportStatus(false);
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

TEST_F(PackageTest, DISABLED_LSPCompileComposition)
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
    instance->importManager.SetSourceCodeImportStatus(false);
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

TEST_F(PackageTest, DISABLED_LSPCompileWithConstraint)
{
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->invocation.globalOptions.implicitPrelude = true;
    instance->code = R"(
        main() {
            match (0) {
                case x: ToString => 0
                case x: ?Any => 1  // unreachable
                case _ => 2 // unreachable
            }
        }
    )";
    instance->importManager.SetSourceCodeImportStatus(false);
    instance->Compile(CompileStage::SEMA);
    EXPECT_TRUE(diag.GetErrorCount() == 0);
    EXPECT_TRUE(diag.GetWarningCount() == 2);
    auto enumDecl = instance->importManager.GetCoreDecl<EnumDecl>("Option");
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
                auto gTy = RawStaticCast<GenericsTy*>(constraint->type->ty);
                EXPECT_TRUE(gTy && !gTy->upperBounds.empty());
                EXPECT_FALSE(constraint->upperBounds.empty());
            }
        }
    }
    // NOTE: Current the count of all extends in 'core' package have generic constraints is 3.
    EXPECT_EQ(count, 3);
}

TEST_F(PackageTest, DISABLED_LSPImportFunctionWithNamedParam)
{
    diag.ClearError();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->invocation.globalOptions.implicitPrelude = true;
    instance->invocation.globalOptions.compilePackage = true;
    instance->code = R"(
        package pkg
        public func name(a_123!:Int64) {}
    )";
    instance->importManager.SetSourceCodeImportStatus(false);
    instance->Compile(CompileStage::SEMA);
    std::vector<uint8_t> astData;
    instance->importManager.ExportAST(false, astData, *instance->GetSourcePackages()[0]);

    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->importManager.SetPackageCjoCache("pkg", astData);
    instance->invocation.globalOptions.implicitPrelude = true;
    instance->code = R"(
        import pkg.*
        main() {
            var xx = name(a_123: 5)
        }
    )";
    instance->importManager.SetSourceCodeImportStatus(false);
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

TEST_F(PackageTest, DISABLED_ExportVisibleMembersForLSP)
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
    instance->importManager.SetSourceCodeImportStatus(false);
    instance->Compile(CompileStage::SEMA);
    std::vector<uint8_t> astData;
    instance->importManager.ExportAST(false, astData, *instance->GetSourcePackages()[0]);

    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->importManager.SetPackageCjoCache("pkg", astData);
    instance->invocation.globalOptions.implicitPrelude = true;
    instance->code = R"(
        import pkg.*
        main() {}
    )";
    instance->importManager.SetSourceCodeImportStatus(false);
    instance->Compile(CompileStage::IMPORT_PACKAGE);
    auto pd = instance->importManager.GetPackageDecl("pkg");
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

TEST_F(PackageTest, DISABLED_ConstNotExportInitializerForLSP)
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
    instance->importManager.SetSourceCodeImportStatus(false);
    instance->Compile(CompileStage::SEMA);
    std::vector<uint8_t> astData;
    instance->importManager.ExportAST(false, astData, *instance->GetSourcePackages()[0]);

    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->importManager.SetPackageCjoCache("initializer", astData);
    instance->invocation.globalOptions.implicitPrelude = true;
    instance->code = R"(
        import initializer.*
        main() {}
    )";
    instance->importManager.SetSourceCodeImportStatus(false);
    instance->Compile(CompileStage::IMPORT_PACKAGE);
    auto pd = instance->importManager.GetPackageDecl("initializer");
    ASSERT_TRUE(pd != nullptr && !pd->srcPackage->files.empty() && !pd->srcPackage->files[0]->decls.empty());

    for (auto it : pd->srcPackage->files[0]->decls[0]->GetMemberDeclPtrs()) {
        EXPECT_TRUE(it->astKind == ASTKind::VAR_DECL);
        EXPECT_TRUE(it->identifier == "INSTANCE");
        EXPECT_TRUE(StaticCast<VarDecl*>(it)->initializer == nullptr);
    }
}

TEST_F(PackageTest, DISABLED_ForbiddenRunInstantiation)
{
    Cangjie::ICE::TriggerPointSetter iceSetter(static_cast<int64_t>(Cangjie::ICE::UNITTEST_TP));
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->invocation.globalOptions.implicitPrelude = true;
    instance->code = R"(
        main() {}
    )";
    instance->importManager.SetSourceCodeImportStatus(false);
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

TEST_F(PackageTest, DISABLED_LoadBuiltInDecl)
{
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->invocation.globalOptions.implicitPrelude = true;
    instance->code = R"(
        main() {}
    )";
    bool res = instance->Compile(CompileStage::IMPORT_PACKAGE);
    EXPECT_TRUE(res);
    auto pkgs = instance->importManager.GetAllImportedPackages();
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

TEST_F(PackageTest, DISABLED_MangleEnumExportId)
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
    instance->importManager.ExportAST(false, astData, *pkg); // ExportId will be updated during serialization.
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

TEST_F(PackageTest, DISABLED_LSPExportInterfaceFuncFromMacro)
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
            id->EnableAttr(Attribute::MACRO_EXPANDED_NODE);
            for (auto& d : id->body->decls) {
                d->EnableAttr(Attribute::MACRO_EXPANDED_NODE);
            }
        }
    }
    std::vector<uint8_t> astData;
    instance->importManager.ExportAST(false, astData, *instance->GetSourcePackages()[0]);

    EXPECT_EQ(diag.GetErrorCount(), 0);
}

TEST_F(PackageTest, DISABLED_ImportManager_API)
{
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->invocation.globalOptions.implicitPrelude = true;
    instance->code = R"(
    main() { }
    )";
    instance->Compile(CompileStage::IMPORT_PACKAGE);
    auto members = instance->importManager.GetPackageMembers("default", "std.core");
    EXPECT_TRUE(!members.empty());
    auto obj = instance->importManager.GetImportedDecl<ClassDecl>(CORE_PACKAGE_NAME, OBJECT_NAME);
    EXPECT_TRUE(obj != nullptr);
    auto pkg = instance->GetSourcePackages()[0];
    auto importPkgs = instance->importManager.GetCurImportedPackages(pkg->fullPackageName);
    EXPECT_FALSE(importPkgs.empty()); // default 'core'.
}

TEST_F(PackageTest, DISABLED_LSPExportInvalidVpd)
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
    instance->importManager.SetSourceCodeImportStatus(false);
    instance->Compile(CompileStage::SEMA);
    std::vector<uint8_t> astData;
    instance->importManager.ExportAST(false, astData, *instance->GetSourcePackages()[0]);
    EXPECT_NE(diag.GetErrorCount(), 0);
}

TEST_F(PackageTest, DISABLED_LSPExportInvisibleMembers)
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
    instance->importManager.SetSourceCodeImportStatus(false);
    instance->Compile(CompileStage::SEMA);
    std::vector<uint8_t> astData;
    instance->importManager.ExportAST(false, astData, *instance->GetSourcePackages()[0]);
    EXPECT_NE(diag.GetErrorCount(), 0);
}

TEST_F(PackageTest, DISABLED_CheckCjoPathLegality)
{
    diag.ClearError();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    auto import = MakeOwned<ImportSpec>();
    import->EnableAttr(Attribute::IMPLICIT_ADD);
    bool ret = instance->importManager.CheckCjoPathLegality(import, "", "not found package name", false, false);
    ASSERT_EQ(ret, false);
    EXPECT_TRUE(diag.GetErrorCount() == 1);
}

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
TEST_F(PackageTest, DISABLED_SemanticUsage)
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

TEST_F(PackageTest, DISABLED_IncrementalMergedSemanticUsage)
{
    diag.ClearError();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->invocation.globalOptions.implicitPrelude = true;
    instance->invocation.globalOptions.enIncrementalCompilation = true;
    instance->code = R"(
        macro package pkg
        import std.ast.*
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
        public macro M(input: Tokens) {
            input
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

TEST_F(PackageTest, DISABLED_UsageOfTypeAlias)
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

TEST_F(PackageTest, DISABLED_UsageOfOnTheLeftOfPipeline)
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
    ret = ret && instance->PerformDesugarAfterSema();
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

TEST_F(PackageTest, DISABLED_LoadAnnotationTarget)
{
    diag.ClearError();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->invocation.globalOptions.implicitPrelude = true;
    instance->invocation.globalOptions.enIncrementalCompilation = true;
    instance->code = R"(
        @Annotation
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
    auto data = instance->importManager.ExportASTSignature(*pkg);
    // Unset annotation status.
    classA->DisableAttr(Attribute::IS_ANNOTATION);
    found->get()->target = 0;

    // Test for loading annotation status.
    pkg->EnableAttr(Attribute::INCRE_COMPILE);
    auto loader = std::make_unique<ASTLoader>(std::move(data), pkg->fullPackageName, *instance->typeManager,
        *instance->importManager.cjoManager, instance->invocation.globalOptions);
    (void)loader->LoadCachedTypeForPackage(*pkg, mangledName2DeclMap);
    EXPECT_TRUE(classA->TestAttr(Attribute::IS_ANNOTATION));
    EXPECT_EQ(found->get()->target, 3);
}
#endif

TEST_F(PackageTest, DISABLED_LoadPackageFromCjo)
{
    CompilerInvocation testInvocation;
#ifdef __x86_64__
    testInvocation.globalOptions.target.arch = Cangjie::Triple::ArchType::X86_64;
#else
    testInvocation.globalOptions.target.arch = Cangjie::Triple::ArchType::AARCH64;
#endif
#ifdef _WIN32
    testInvocation.globalOptions.target.os = Cangjie::Triple::OSType::WINDOWS;
#elif __unix__
    testInvocation.globalOptions.target.os = Cangjie::Triple::OSType::LINUX;
#endif
    DiagnosticEngine testDiag;
    auto testIns = std::make_unique<TestCompilerInstance>(testInvocation, testDiag);
    testIns->invocation.globalOptions.implicitPrelude = true;
    testIns->code = "main() {}";
    testIns->Compile(CompileStage::IMPORT_PACKAGE);

    auto& searchPath = testIns->importManager.GetSearchPath();
    auto getCjoPath = [&searchPath](const std::string& fullPkgName) {
        return FileUtil::FindSerializationFile(fullPkgName, SERIALIZED_FILE_EXTENSION, searchPath);
    };

    std::unordered_set<std::string> fullPkgNames = {
        "std.math",
        "std.sync",
        "std.time",
    };
    std::unordered_map<std::string, Ptr<Package>> pkgName2PkgNodeMap;

    auto checkPkg = [&testIns, &pkgName2PkgNodeMap, &getCjoPath](const std::string& pkgName) {
        auto pkg = testIns->importManager.LoadPackageFromCjo(pkgName, getCjoPath(pkgName));
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
                    EXPECT_TRUE(Ty::IsTyCorrect(decl->ty));
                }
            }
        }
    };
    for (size_t i = 0; i < 2; ++i) {
        for (auto& fullPkgName : std::as_const(fullPkgNames)) {
            checkPkg(fullPkgName);
        }
    }
}
