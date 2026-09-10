// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>
#include "gtest/gtest.h"
#include "TestCompilerInstance.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/PrintNode.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Basic/Match.h"

#ifdef _WIN32
#include <windows.h>
#define CJ_ENVIRON _environ
#elif defined(__APPLE__)
#include <crt_externs.h>
#define CJ_ENVIRON (*_NSGetEnviron())
#else
#include <unistd.h>
#define CJ_ENVIRON environ
#endif

using namespace Cangjie;
using namespace AST;

namespace {
std::unordered_map<std::string, std::string> GetEnvironmentVars()
{
    std::unordered_map<std::string, std::string> envVars;
    char **env = CJ_ENVIRON;
    while (env && *env) {
        std::string entry(*env);
        size_t pos = entry.find('=');
        if (pos != std::string::npos) {
            std::string key = entry.substr(0, pos);
            std::string value = entry.substr(pos + 1);
            envVars[key] = value;
        }
        ++env;
    }
    return envVars;
}
}

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
        invocation.globalOptions.executablePath = projectPath + "\\output\\bin\\";
#elif defined(__APPLE__)
        instance->invocation.globalOptions.target.os = Cangjie::Triple::OSType::DARWIN;
        invocation.globalOptions.executablePath = projectPath + "/output/bin/";
#elif defined(__unix__)
        instance->invocation.globalOptions.target.os = Cangjie::Triple::OSType::LINUX;
        invocation.globalOptions.executablePath = projectPath + "/output/bin/";
#endif
        std::string cangjieHome = projectPath + "/output";
        // Derive the platform-specific module directory from the target triple
        // (GetCangjieLibTargetPathName returns "<os>_<arch>_cjnative") instead
        // of hard-coding "darwin_arm64"/"darwin_aarch64", which must otherwise
        // be kept in sync with the actual output directory name.
        std::string cangjiePath =
            cangjieHome + "/modules/" + invocation.globalOptions.GetCangjieLibTargetPathName();
#ifdef _WIN32
        char* oldHome = getenv("CANGJIE_HOME");
        char* oldPath = getenv("CANGJIE_PATH");
        if (oldHome) savedCangjieHome = oldHome;
        if (oldPath) savedCangjiePath = oldPath;
        _putenv_s("CANGJIE_HOME", cangjieHome.c_str());
        _putenv_s("CANGJIE_PATH", cangjiePath.c_str());
#else
        char* oldHome = getenv("CANGJIE_HOME");
        char* oldPath = getenv("CANGJIE_PATH");
        if (oldHome) savedCangjieHome = oldHome;
        if (oldPath) savedCangjiePath = oldPath;
        setenv("CANGJIE_HOME", cangjieHome.c_str(), 1);
        setenv("CANGJIE_PATH", cangjiePath.c_str(), 1);
#endif
        invocation.globalOptions.ReadPathsFromEnvironmentVars(GetEnvironmentVars());
        Cangjie::MacroProcMsger::GetInstance().CloseMacroSrv();
    }

    void TearDown() override
    {
#ifdef _WIN32
        if (savedCangjieHome.empty()) {
            _putenv_s("CANGJIE_HOME", "");
        } else {
            _putenv_s("CANGJIE_HOME", savedCangjieHome.c_str());
        }
        if (savedCangjiePath.empty()) {
            _putenv_s("CANGJIE_PATH", "");
        } else {
            _putenv_s("CANGJIE_PATH", savedCangjiePath.c_str());
        }
#else
        if (savedCangjieHome.empty()) {
            unsetenv("CANGJIE_HOME");
        } else {
            setenv("CANGJIE_HOME", savedCangjieHome.c_str(), 1);
        }
        if (savedCangjiePath.empty()) {
            unsetenv("CANGJIE_PATH");
        } else {
            setenv("CANGJIE_PATH", savedCangjiePath.c_str(), 1);
        }
#endif
    }

    std::string srcPath;
    std::string projectPath;
    std::string savedCangjieHome;
    std::string savedCangjiePath;
    DiagnosticEngine diag;
    CompilerInvocation invocation;
    std::unique_ptr<TestCompilerInstance> instance;

    void CheckInvalidVariadicOverload(const std::string& code, bool expectResolvedFunction)
    {
        instance->code = code;
        instance->invocation.globalOptions.implicitPrelude = true;
        instance->Compile(CompileStage::SEMA);

        Ptr<CallExpr> memberCall{nullptr};
        Walker walker(instance->GetSourcePackages()[0]->files[0].get(), [&memberCall](Ptr<Node> node) {
            auto call = DynamicCast<CallExpr*>(node);
            auto memberAccess = call ? DynamicCast<MemberAccess*>(call->baseFunc.get()) : nullptr;
            if (memberAccess && memberAccess->field == "f") {
                memberCall = call;
            }
            return VisitAction::WALK_CHILDREN;
        });
        walker.Walk();

        ASSERT_TRUE(memberCall);
        EXPECT_EQ(diag.GetErrorCount(), 2);
        if (!expectResolvedFunction) {
            EXPECT_FALSE(memberCall->resolvedFunction);
            return;
        }
        ASSERT_TRUE(memberCall->resolvedFunction);
        EXPECT_FALSE(memberCall->resolvedFunction->TestAttr(Attribute::IS_BROKEN));
        EXPECT_FALSE(memberCall->resolvedFunction->hasVariableLenArg);
        auto memberAccess = StaticAs<ASTKind::MEMBER_ACCESS>(memberCall->baseFunc.get());
        EXPECT_EQ(memberAccess->target, memberCall->resolvedFunction);
    }

    void CheckInvalidVariadicFunctionReference(const std::string& code)
    {
        instance->code = code;
        instance->invocation.globalOptions.implicitPrelude = true;
        instance->Compile(CompileStage::SEMA);

        Ptr<MemberAccess> functionReference{nullptr};
        Walker walker(instance->GetSourcePackages()[0]->files[0].get(), [&functionReference](Ptr<Node> node) {
            auto memberAccess = DynamicCast<MemberAccess*>(node);
            if (memberAccess && memberAccess->field == "f") {
                functionReference = memberAccess;
            }
            return VisitAction::WALK_CHILDREN;
        });
        walker.Walk();

        ASSERT_TRUE(functionReference);
        EXPECT_EQ(diag.GetErrorCount(), 2);
        auto target = DynamicCast<FuncDecl*>(functionReference->target);
        ASSERT_TRUE(target);
        EXPECT_FALSE(target->TestAttr(Attribute::IS_BROKEN));
        EXPECT_FALSE(target->hasVariableLenArg);
        ASSERT_EQ(functionReference->targets.size(), 1);
        EXPECT_EQ(functionReference->targets[0], target);
    }
};

TEST_F(TypeCheckerTest, TypecheckTest)
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
                auto ty = DynamicCast<FuncTy>(decl.DataTy());
                EXPECT_EQ(ty->retTy->kind, TypeKind::TYPE_INT64);
            },
            [](const VarDecl& decl) { EXPECT_EQ(decl.TyKind(), TypeKind::TYPE_INT32); });
        return VisitAction::WALK_CHILDREN;
    });
    walker.Walk();
    EXPECT_EQ(diag.GetErrorCount(), 0);
}

TEST_F(TypeCheckerTest, InvalidVariadicFunctionIsNotCallTarget)
{
    CheckInvalidVariadicOverload(R"(
class H {
    func f(...x: Int64): Unit
}
main() { H().f(1) }
    )", false);
}

TEST_F(TypeCheckerTest, InvalidVariadicAfterValidOverloadDoesNotAffectCallTarget)
{
    CheckInvalidVariadicOverload(R"(
class H {
    func f(x: Int64): Unit
    func f(...x: Int64): Unit
}
main() { H().f(1) }
    )", true);
}

TEST_F(TypeCheckerTest, InvalidVariadicBeforeValidOverloadDoesNotAffectCallTarget)
{
    CheckInvalidVariadicOverload(R"(
class H {
    func f(...x: Int64): Unit
    func f(x: Int64): Unit
}
main() { H().f(1) }
    )", true);
}

TEST_F(TypeCheckerTest, InvalidVariadicAfterValidOverloadDoesNotAffectFunctionReference)
{
    CheckInvalidVariadicFunctionReference(R"(
class H {
    func f(x: Int64): Unit
    func f(...x: Int64): Unit
}
main() { let functionReference = H().f }
    )");
}

TEST_F(TypeCheckerTest, InvalidVariadicBeforeValidOverloadDoesNotAffectFunctionReference)
{
    CheckInvalidVariadicFunctionReference(R"(
class H {
    func f(...x: Int64): Unit
    func f(x: Int64): Unit
}
main() { let functionReference = H().f }
    )");
}

TEST_F(TypeCheckerTest, InvalidVariadicAfterValidImplementationDoesNotAffectInterfaceConformance)
{
    CheckInvalidVariadicOverload(R"(
interface I {
    func f(x: Int64): Unit
}
class H <: I {
    func f(x: Int64): Unit {}
    func f(...x: Int64): Unit
}
main() { H().f(1) }
    )", true);
}

TEST_F(TypeCheckerTest, InvalidVariadicBeforeValidImplementationDoesNotAffectInterfaceConformance)
{
    CheckInvalidVariadicOverload(R"(
interface I {
    func f(x: Int64): Unit
}
class H <: I {
    func f(...x: Int64): Unit
    func f(x: Int64): Unit {}
}
main() { H().f(1) }
    )", true);
}

TEST_F(TypeCheckerTest, MacroDeclTest)
{
    std::string code = R"(
    public macro
    )";

    instance->code = code;
    instance->PerformParse();
    instance->PerformImportPackage();
    instance->PerformSema();
    std::vector<uint8_t> astData;
    instance->importManager->ExportAST(false, astData, *instance->GetSourcePackages()[0]);

    EXPECT_EQ(diag.GetErrorCount(), 3);
}

TEST_F(TypeCheckerTest, MacroCallInLSPTest)
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

TEST_F(TypeCheckerTest, MacroDiagInLSPTest)
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

TEST_F(TypeCheckerTest, NoDiagInLSPMacroCallTest)
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

TEST_F(TypeCheckerTest, NoDiagInLSPMacroCallForTest)
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

TEST_F(TypeCheckerTest, MacroCallOfTopLevelInLSPTest)
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
                EXPECT_EQ(fp->type->GetTy().String(), "Struct-String");
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

TEST_F(TypeCheckerTest, AssumptionTest)
{
#ifdef _WIN32
    srcPath = projectPath + "\\unittests\\Sema\\SemaCangjieFiles\\AssumptionTest";
#elif defined(__APPLE__) || defined(__unix__)
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

TEST_F(TypeCheckerTest, SpawnTest)
{
#ifdef _WIN32
    srcPath = projectPath + "\\unittests\\Sema\\SemaCangjieFiles\\";
#elif defined(__APPLE__) || defined(__unix__)
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
        if (auto md = DynamicCast<MainDecl>(decl.get())) {
            auto main = As<ASTKind::FUNC_DECL>(md->desugarDecl.get());
            EXPECT_TRUE(main);
            EXPECT_TRUE(main->funcBody->body->body.size() == 4);
            targetFutureObj1 = main->funcBody->body->body[1].get();
            targetFutureObj2 = main->funcBody->body->body[2].get();
        }
    }

    auto var1 = As<ASTKind::VAR_DECL>(targetFutureObj1);
    auto var2 = As<ASTKind::VAR_DECL>(targetFutureObj2);
    EXPECT_TRUE(var1);
    EXPECT_TRUE(var2);
    auto ty1 = DynamicCast<ClassTy>(var1->DataTy());
    auto ty2 = DynamicCast<ClassTy>(var2->DataTy());
    EXPECT_TRUE(ty1 && ty2 && ty1 == ty2);
    EXPECT_TRUE(ty1->decl->identifier == "Future");
    EXPECT_TRUE(ty1->typeArgs.size() == 1);
    EXPECT_TRUE(ty1->typeArgs[0]->kind == TypeKind::TYPE_INT64);
}

TEST_F(TypeCheckerTest, DiagnosticNodeTest)
{
    std::string code = R"(
interface I {
    func foo(): Unit
}

class C1 <: I {}
struct S1 <: I {}

main() {}
    )";

    instance->code = code;
    instance->invocation.globalOptions.implicitPrelude = true;
    instance->Compile(CompileStage::SEMA);

    auto diags = diag.GetCategoryDiagnostic(DiagCategory::SEMA);
    int foundDiag = 0;
    for (auto& d : diags) {
        if (d.rKind == DiagKindRefactor::sema_class_need_abstract_modifier_or_func_need_impl) {
            foundDiag++;
            EXPECT_TRUE(d.node != nullptr);
            EXPECT_EQ(d.node->astKind, ASTKind::CLASS_DECL);
            auto classDecl = StaticAs<ASTKind::CLASS_DECL>(d.node);
            EXPECT_EQ(classDecl->identifier.Val(), "C1");
            EXPECT_FALSE(d.subDiags.empty());
            auto noteNode = d.subDiags[0].GetNodeSubDiagAt();
            EXPECT_TRUE(noteNode != nullptr);
            EXPECT_EQ(noteNode->astKind, ASTKind::FUNC_DECL);
            auto funcDecl = StaticAs<ASTKind::FUNC_DECL>(noteNode);
            EXPECT_EQ(funcDecl->identifier.Val(), "foo");
            EXPECT_EQ(funcDecl->outerDecl->identifier.Val(), "I");
        }
        if (d.rKind == DiagKindRefactor::sema_need_member_implementation) {
            foundDiag++;
            EXPECT_TRUE(d.node != nullptr);
            EXPECT_EQ(d.node->astKind, ASTKind::STRUCT_DECL);
            auto structDecl = StaticAs<ASTKind::STRUCT_DECL>(d.node);
            EXPECT_EQ(structDecl->identifier.Val(), "S1");
            EXPECT_FALSE(d.subDiags.empty());
            auto noteNode = d.subDiags[0].GetNodeSubDiagAt();
            EXPECT_TRUE(noteNode != nullptr);
            EXPECT_EQ(noteNode->astKind, ASTKind::FUNC_DECL);
            auto funcDecl = StaticAs<ASTKind::FUNC_DECL>(noteNode);
            EXPECT_EQ(funcDecl->identifier.Val(), "foo");
            EXPECT_EQ(funcDecl->outerDecl->identifier.Val(), "I");
        }
    }
    EXPECT_EQ(foundDiag, 2);
}

TEST_F(TypeCheckerTest, OrderOfInitializationCheck)
{
    // Source files are passed in through bufferCache, exercising the loadSrcFilesFromCache branch
    // (lines 374-385) of FullCompileStrategy::ParseOnePackage.
    srcPath = projectPath + "/unittests/Sema/SemaCangjieFiles/InitializationCheck/";
    std::string fileA = srcPath + "a.cj";
    std::string fileB = srcPath + "b.cj";
    std::string failedReason;
    auto contentA = FileUtil::ReadFileContent(fileA, failedReason);
    ASSERT_TRUE(contentA.has_value());
    auto contentB = FileUtil::ReadFileContent(fileB, failedReason);
    ASSERT_TRUE(contentB.has_value());

    instance->invocation.globalOptions.implicitPrelude = true;
    instance->invocation.globalOptions.outputMode = GlobalOptions::OutputMode::STATIC_LIB;
    instance->loadSrcFilesFromCache = true;
    instance->bufferCache[fileB] = CompilerInstance::SrcCodeCacheInfo(
        CompilerInstance::SrcCodeChangeState::ADDED, contentB.value());
    instance->bufferCache[fileA] = CompilerInstance::SrcCodeCacheInfo(
        CompilerInstance::SrcCodeChangeState::ADDED, contentA.value());

    // The bufferCache is an unordered_map whose iteration order is unspecified, so the fileID assigned
    // during the cache iteration is not guaranteed by insertion order. Pre-register b.cj before a.cj in
    // the SourceManager so their fileIDs are locked (b.cj < a.cj) regardless of the cache iteration order.
    auto& sourceManager = instance->GetSourceManager();
    unsigned int preBFileID = sourceManager.AddSource(fileB, contentB.value());
    unsigned int preAFileID = sourceManager.AddSource(fileA, contentA.value());
    EXPECT_LT(preBFileID, preAFileID);

    instance->Compile(CompileStage::SEMA);
    // Sema should finish without any error.
    EXPECT_EQ(diag.GetErrorCount(), 0);

    // Verify that in the created AST, the fileID of b.cj is smaller than the fileID of a.cj.
    auto packages = instance->GetSourcePackages();
    ASSERT_FALSE(packages.empty());
    unsigned int astAFileID = 0;
    unsigned int astBFileID = 0;
    bool foundA = false;
    bool foundB = false;
    for (auto& file : packages[0]->files) {
        if (file->fileName == "a.cj") {
            astAFileID = file->begin.fileID;
            foundA = true;
        } else if (file->fileName == "b.cj") {
            astBFileID = file->begin.fileID;
            foundB = true;
        }
    }
    EXPECT_TRUE(foundA);
    EXPECT_TRUE(foundB);
    EXPECT_LT(astBFileID, astAFileID);
}

TEST_F(TypeCheckerTest, NeedRuntimeCheck)
{
    std::string code = R"(
main() {
    let x = 3 is Nothing
}
    )";

    instance->code = code;
    instance->invocation.globalOptions.implicitPrelude = true;
    auto ret = instance->Compile(CompileStage::DESUGAR_AFTER_SEMA);
    ret = ret && instance->PerformDesugarAfterSema();
    ASSERT_TRUE(ret);
    // should finish without any error.
    EXPECT_EQ(diag.GetErrorCount(), 0);

    // Walk the AST to find the TypePattern and verify needRuntimeTypeCheck is false for "3 is Nothing".
    Walker walker(instance->GetSourcePackages()[0]->files[0].get(), [](Ptr<Node> node) -> VisitAction {
        if (auto tp = DynamicCast<TypePattern>(node); tp) {
            EXPECT_FALSE(tp->needRuntimeTypeCheck) << "3 is Nothing should not require runtime type check";
        }
        return VisitAction::WALK_CHILDREN;
    });
    walker.Walk();
}

TEST_F(TypeCheckerTest, VarWithPatternInConstFunctionProducesDiagnostic)
{
    std::string code = R"(
struct S {
    const S() {}
    const func f(): Int64 {
        var (a, b) = (1, 2)
        return 0
    }
}

main(): Int64 {
    return 0
}
)";

    instance->code = code;
    instance->invocation.globalOptions.implicitPrelude = false;
    instance->Compile(CompileStage::SEMA);

    auto diagnostics = diag.GetCategoryDiagnostic(DiagCategory::SEMA);
    bool foundDiagnostic = false;
    for (auto& diagnostic : diagnostics) {
        if (diagnostic.rKind == DiagKindRefactor::sema_cannot_define_var_in_const_funciton) {
            foundDiagnostic = true;
            EXPECT_FALSE(diagnostic.mainHint.range.HasZero());
        }
    }
    EXPECT_TRUE(foundDiagnostic);
    EXPECT_EQ(diag.GetErrorCount(), 1);
}

// The string interpolation literal is desugared into a StringBuilder-based block in
// `DesugarStrInterpolationExpr`. Every synthesized node (the StringBuilder ctor call, the
// `$tmp` var decl, the `append` calls and the final `toString` call) must carry a valid
// source position copied from the original interpolation expression. Nodes with a zero
// position used to trigger an ICE in `DiagnosticEngine::CheckRange` when a later phase
// (e.g. recursive-constructor detection) emitted a diagnostic anchored on them.
TEST_F(TypeCheckerTest, StrInterpolationDesugarNodesHaveValidPosition)
{
    // Cover both branches of `DesugarStrPartExpr` inside the InterpolationExpr arm:
    //  - `count` (Int64) has a matching `append(Int64)` overload -> matchedAppend branch.
    //  - `p` (user type P <: ToString) has no direct append overload -> toString() fallback
    //    branch, which routes a Block through `CreateMemberAccess`/`CreateFuncArg` and is the
    //    path that previously left a zero-position '$dummy' wrapper.
    std::string code = R"(
class P <: ToString {
    public func toString(): String {
        return "P"
    }
}
main(): Int64 {
    let count = 42
    let p = P()
    let s = "count is ${count}, p is ${p}."
    return 0
}
)";

    instance->code = code;
    instance->invocation.globalOptions.implicitPrelude = true;
    auto ret = instance->Compile(CompileStage::DESUGAR_AFTER_SEMA);
    ASSERT_TRUE(ret);
    // The desugared interpolation must type-check without errors.
    ASSERT_EQ(diag.GetErrorCount(), 0) << "interpolation desugar should not produce errors";

    // Locate the desugared interpolation literal and walk its synthesized sub-tree.
    Ptr<Expr> desugarRoot {nullptr};
    Walker finder(instance->GetSourcePackages()[0]->files[0].get(), [&desugarRoot](Ptr<Node> node) -> VisitAction {
        if (auto lce = DynamicCast<LitConstExpr>(node); lce && lce->siExpr && lce->desugarExpr) {
            desugarRoot = lce->desugarExpr.get();
            return VisitAction::SKIP_CHILDREN;
        }
        return VisitAction::WALK_CHILDREN;
    });
    finder.Walk();
    ASSERT_NE(desugarRoot, nullptr) << "no desugared string interpolation found";

    // `DesugarStrInterpolationExpr` synthesizes the StringBuilder ctor call, the `$tmp` var
    // decl, the `append`/`toString` calls and their args, copying the source position of the
    // original interpolation expression onto each. These synthesized nodes (Expr/Decl/Type)
    // carry the COMPILER_ADD attribute. A zero position on one of them used to trigger an ICE
    // in `DiagnosticEngine::CheckRange` when a later phase (e.g. recursive-constructor
    // detection) emitted a diagnostic anchored on it, so every synthesized node is guarded.
    Walker checker(desugarRoot.get(), [](Ptr<Node> node) -> VisitAction {
        if (!node->TestAttr(Attribute::COMPILER_ADD)) {
            return VisitAction::WALK_CHILDREN;
        }
        EXPECT_FALSE(node->begin.IsZero())
            << "synthesized interpolation desugar node has zero begin position";
        EXPECT_FALSE(node->end.IsZero())
            << "synthesized interpolation desugar node has zero end position";
        return VisitAction::WALK_CHILDREN;
    });
    checker.Walk();
}
