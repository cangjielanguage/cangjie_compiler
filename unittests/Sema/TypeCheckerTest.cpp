// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include <cstdlib>
#include <string>
#include <vector>
#include "gtest/gtest.h"
#include "TestCompilerInstance.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/PrintNode.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Basic/Match.h"

using namespace Cangjie;
using namespace AST;

class TypeCheckerTest : public testing::Test {
protected:
    void SetUp() override
    {
        instance = std::make_unique<TestCompilerInstance>(invocation, diag);
#ifdef PROJECT_SOURCE_DIR
        // Gets the absolute path of the project from the compile parameter.
        projectPath = PROJECT_SOURCE_DIR;
#else
        // Just in case, give it a default value.
        // Assume the initial is in the build directory.
        projectPath = "..";
#endif
#ifdef __x86_64__
        instance->invocation.globalOptions.target.arch = Cangjie::Triple::ArchType::X86_64;
#else
        instance->invocation.globalOptions.target.arch = Cangjie::Triple::ArchType::AARCH64;
#endif
#ifdef _WIN32
        instance->invocation.globalOptions.target.os = Cangjie::Triple::OSType::WINDOWS;
#elif __unix__
        instance->invocation.globalOptions.target.os = Cangjie::Triple::OSType::LINUX;
#endif
        Cangjie::MacroProcMsger::GetInstance().CloseMacroSrv();
    }
    std::string srcPath;
    std::string projectPath;
    DiagnosticEngine diag;
    CompilerInvocation invocation;
    std::unique_ptr<TestCompilerInstance> instance;
};

TEST_F(TypeCheckerTest, DISABLED_TypecheckTest)
{
    std::string code = R"(
main() {
    var t: Int32
    t = 1
    return 0
}
    )";

    instance->code = code;
    instance->Compile(CompileStage::SEMA);

    Walker walker(instance->GetSourcePackages()[0]->files[0].get(), [](Ptr<Node> node) -> VisitAction {
        Meta::match (*node)(
            [](const FuncDecl& decl) {
                auto ty = dynamic_cast<FuncTy*>(decl.ty.get());
                EXPECT_EQ(ty->retTy->kind, TypeKind::TYPE_INT64);
            },
            [](const VarDecl& decl) { EXPECT_EQ(decl.ty->kind, TypeKind::TYPE_INT32); });
        return VisitAction::WALK_CHILDREN;
    });
    walker.Walk();
    EXPECT_EQ(diag.GetErrorCount(), 0);
}

TEST_F(TypeCheckerTest, DISABLED_MacroDeclTest)
{
    std::string code = R"(
    public macro
    )";

    instance->code = code;
    instance->PerformParse();
    instance->PerformImportPackage();
    instance->PerformSema();
    std::vector<uint8_t> astData;
    instance->importManager.ExportAST(false, astData, *instance->GetSourcePackages()[0]);

    EXPECT_EQ(diag.GetErrorCount(), 3);
}

TEST_F(TypeCheckerTest, DISABLED_MacroCallInLSPTest)
{
    std::string code = R"(
    @M1
    func test() {
        var a = 1
        a = 2
    }
    @M2
    class A {
        var a = 1
        func test() {
            this.a = 2
        }
        func test1() {
            var b = 1
            b = 2
        }
    }
    @M3
    enum E {
        EE
        func test() {
            let a = EE
        }
    }
    main() {0}
    )";

    instance->code = code;
    instance->invocation.globalOptions.enableMacroInLSP = true;
    instance->Compile(CompileStage::SEMA);
    // Test MemberAccess has target or not in macrocall.
    auto visitPre = [&](Ptr<Node> curNode) -> VisitAction {
        if (curNode->astKind == ASTKind::MEMBER_ACCESS) {
            auto ma = StaticAs<ASTKind::MEMBER_ACCESS>(curNode);
            EXPECT_TRUE(ma->target);
            if (ma->target) {
                std::cout << "MemberAccess field " << ma->field.Val() << " has target." << std::endl;
            } else {
                std::cout << "MemberAccess field " << ma->field.Val() << " has no target." << std::endl;
            }
        }
        if (curNode->astKind == ASTKind::REF_EXPR) {
            auto re = StaticAs<ASTKind::REF_EXPR>(curNode);
            if (re->ref.identifier == "M1" || re->ref.identifier == "M2" || re->ref.identifier == "M3") {
                return VisitAction::SKIP_CHILDREN;
            }
            EXPECT_TRUE(re->ref.target);
            if (re->ref.target) {
                std::cout << re->ref.identifier.Val() << " has target." << std::endl;
            } else {
                std::cout << re->ref.identifier.Val() << " has no target." << std::endl;
            }
            return VisitAction::SKIP_CHILDREN;
        }
        return VisitAction::WALK_CHILDREN;
    };
    Walker macroCallWalker(instance->GetSourcePackages()[0]->files[0].get(), visitPre);
    macroCallWalker.Walk();
    // error: undeclared identifier 'M1'/'M2'/'M3'.
    EXPECT_EQ(diag.GetErrorCount(), 3);
    Cangjie::MacroProcMsger::GetInstance().CloseMacroSrv();
}

TEST_F(TypeCheckerTest, DISABLED_MacroDiagInLSPTest)
{
    instance->invocation.globalOptions.enableMacroInLSP = true;
    srcPath = projectPath + "/unittests/Sema/SemaCangjieFiles/";
    invocation.globalOptions.executablePath = projectPath + "/output/bin/";
    instance->compileOnePackageFromSrcFiles = true;

    instance->srcFilePaths = {srcPath + "UnableToInferGenericArgumentInTest.cj"};
    invocation.globalOptions.outputMode = GlobalOptions::OutputMode::STATIC_LIB;
    invocation.globalOptions.enableCompileTest = true;
    instance->Compile(CompileStage::SEMA);
    auto dv = diag.GetCategoryDiagnostic(DiagCategory::SEMA);
    bool findOptNone = false;
    for (auto& d : dv) {
        for (auto& n : d.subDiags) {
            if (n.mainHint.str.find("Enum-Option<Int64>") != std::string::npos) {
                findOptNone = true;
                EXPECT_EQ(n.mainHint.range.end.line, 14);
                EXPECT_EQ(n.mainHint.range.end.column, 55);
            }
        }
    }
    EXPECT_EQ(findOptNone, true);
    Cangjie::MacroProcMsger::GetInstance().CloseMacroSrv();
}

TEST_F(TypeCheckerTest, DISABLED_NoDiagInLSPMacroCallTest)
{
    srcPath = projectPath + "/unittests/Sema/SemaCangjieFiles/";
    std::string command = "cjc " + srcPath + "AddClassTyInfoMacro.cj --compile-macro -Woff all";
    int err = system(command.c_str());
    ASSERT_EQ(0, err);

    instance->invocation.globalOptions.enableMacroInLSP = false;
    invocation.globalOptions.executablePath = projectPath + "/output/bin/";
    instance->compileOnePackageFromSrcFiles = true;

    instance->srcFilePaths = {srcPath + "NoDiagInLSPMacroCall.cj"};
    invocation.globalOptions.outputMode = GlobalOptions::OutputMode::STATIC_LIB;
    invocation.globalOptions.enableCompileTest = true;
    instance->Compile(CompileStage::SEMA);
    EXPECT_EQ(diag.GetErrorCount(), 0);
    Cangjie::MacroProcMsger::GetInstance().CloseMacroSrv();
}

TEST_F(TypeCheckerTest, DISABLED_NoDiagInLSPMacroCallForTest)
{
    srcPath = projectPath + "/unittests/Sema/SemaCangjieFiles/";
    std::string command = "cjc " + srcPath + "ModifyClassBuildFunc.cj --compile-macro -Woff all";
    int err = system(command.c_str());
    ASSERT_EQ(0, err);

    instance->invocation.globalOptions.enableMacroInLSP = true;
    invocation.globalOptions.executablePath = projectPath + "/output/bin/";
    instance->compileOnePackageFromSrcFiles = true;

    instance->srcFilePaths = {srcPath + "NoDiagInLSPMacroCallNode.cj"};
    invocation.globalOptions.outputMode = GlobalOptions::OutputMode::STATIC_LIB;
    invocation.globalOptions.enableCompileTest = true;
    instance->Compile(CompileStage::SEMA);
    EXPECT_EQ(diag.GetErrorCount(), 0);
    Cangjie::MacroProcMsger::GetInstance().CloseMacroSrv();
}

TEST_F(TypeCheckerTest, DISABLED_MacroCallOfTopLevelInLSPTest)
{
    std::string code = R"(
    @M1
    func test(v: String) {
        var a = 1
        a = 2
    }
    main() {0}
    )";

    instance->code = code;
    instance->invocation.globalOptions.implicitPrelude = true;
    instance->invocation.globalOptions.enableMacroInLSP = true;
    instance->Compile(CompileStage::IMPORT_PACKAGE);
    // Skip macro expand stage and move node in 'originalMacroCallNodes' to simulate original macro code for LSP.
    auto file = instance->GetSourcePackages()[0]->files[0].get();
    for (auto& it : file->decls) {
        if (auto md = DynamicCast<MacroExpandDecl>(it.get())) {
            file->originalMacroCallNodes.emplace_back(std::move(md->invocation.decl));
        }
    }
    instance->PerformSema();
    unsigned checkCount = 0;
    // Test ty is set correctly.
    auto visitPre = [&](Ptr<Node> curNode) -> VisitAction {
        if (auto fp = DynamicCast<FuncParam>(curNode); fp && fp->identifier == "v") {
            checkCount++;
            EXPECT_TRUE(fp->type != nullptr);
            if (fp->type != nullptr) {
                EXPECT_EQ(fp->type->ty->String(), "Struct-String");
            }
        }
        return VisitAction::WALK_CHILDREN;
    };
    for (auto& it : file->originalMacroCallNodes) {
        Walker(it.get(), visitPre).Walk();
    }
    EXPECT_EQ(checkCount, 1);
    Cangjie::MacroProcMsger::GetInstance().CloseMacroSrv();
}

TEST_F(TypeCheckerTest, DISABLED_AssumptionTest)
{
#ifdef _WIN32
    srcPath = projectPath + "\\unittests\\Sema\\SemaCangjieFiles\\AssumptionTest";
#elif __unix__
    srcPath = projectPath + "/unittests/Sema/SemaCangjieFiles/AssumptionTest";
#endif

    instance->srcDirs = {srcPath};
    instance->invocation.globalOptions.implicitPrelude = true;
    instance->Compile();

    for (auto& decl : instance->GetSourcePackages()[0]->files[0]->decls) {
        if (auto cd = AST::As<ASTKind::CLASS_DECL>(decl.get()); cd) {
            if (cd->identifier == "A") {
                EXPECT_EQ(cd->generic->assumptionCollection.size(), 1);
                auto it = cd->generic->assumptionCollection.begin();
                EXPECT_EQ(it->second.size(), 1);
            } else if (cd->identifier == "B") {
                EXPECT_EQ(cd->generic->assumptionCollection.size(), 1);
                auto it = cd->generic->assumptionCollection.begin();
                EXPECT_EQ(it->second.size(), 2);
            } else if (cd->identifier == "D") {
                for (auto& it : cd->generic->assumptionCollection) {
                    if (dynamic_cast<GenericsTy*>(it.first.get())->name == "V") {
                        EXPECT_EQ(it.second.size(), 2);
                    }
                    if (dynamic_cast<GenericsTy*>(it.first.get())->name == "U") {
                        EXPECT_EQ(it.second.size(), 3);
                    }
                }
            } else if (cd->identifier == "E") {
                EXPECT_EQ(cd->generic->assumptionCollection.size(), 1);
                auto it = cd->generic->assumptionCollection.begin();
                EXPECT_EQ(it->second.size(), 1);
            } else if (cd->identifier == "F") {
                EXPECT_EQ(cd->generic->assumptionCollection.size(), 1);
                auto it = cd->generic->assumptionCollection.begin();
                EXPECT_EQ(it->second.size(), 2);
            } else if (cd->identifier == "G") {
                EXPECT_EQ(cd->generic->assumptionCollection.size(), 1);
                auto it = cd->generic->assumptionCollection.begin();
                EXPECT_EQ(it->second.size(), 2);
            }
        }
    }
}

TEST_F(TypeCheckerTest, DISABLED_SpawnTest)
{
#ifdef _WIN32
    srcPath = projectPath + "\\unittests\\Sema\\SemaCangjieFiles\\";
#elif __unix__
    srcPath = projectPath + "/unittests/Sema/SemaCangjieFiles/";
#endif
    auto srcFile = srcPath + "spawn.cj";
    std::string failedReason;
    auto content = FileUtil::ReadFileContent(srcFile, failedReason);
    if (!content.has_value()) {
        diag.DiagnoseRefactor(
            DiagKindRefactor::module_read_file_to_buffer_failed, DEFAULT_POSITION, srcFile, failedReason);
    }
    instance->code = std::move(content.value());
    instance->invocation.globalOptions.implicitPrelude = true;
    instance->Compile();

    auto expectedErrorCount = 0;
    EXPECT_EQ(diag.GetErrorCount(), expectedErrorCount);
    EXPECT_EQ(instance->GetSourcePackages()[0]->files.size(), 1);

    Ptr<Node> targetFutureObj1{nullptr};
    Ptr<Node> targetFutureObj2{nullptr};
    for (auto& decl : instance->GetSourcePackages()[0]->files[0]->decls) {
        auto main = As<ASTKind::FUNC_DECL>(decl.get());
        EXPECT_TRUE(main);
        EXPECT_TRUE(main->funcBody->body->body.size() == 4);
        targetFutureObj1 = main->funcBody->body->body[1].get();
        targetFutureObj2 = main->funcBody->body->body[2].get();
    }

    auto var1 = As<ASTKind::VAR_DECL>(targetFutureObj1);
    auto var2 = As<ASTKind::VAR_DECL>(targetFutureObj2);
    EXPECT_TRUE(var1);
    EXPECT_TRUE(var2);
    auto ty1 = dynamic_cast<ClassTy*>(var1->ty.get());
    auto ty2 = dynamic_cast<ClassTy*>(var2->ty.get());
    EXPECT_TRUE(ty1 && ty2 && ty1 == ty2);
    EXPECT_TRUE(ty1->decl->identifier == "Future");
    EXPECT_TRUE(ty1->typeArgs.size() == 1);
    EXPECT_TRUE(ty1->typeArgs[0]->kind == TypeKind::TYPE_INT64);
}
