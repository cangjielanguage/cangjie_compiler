// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * Sema diagnostics for @ArkTS foreign declarations: unsupported parameter,
 * return or rest types, invalid options, an empty or path-like module, and
 * generator refusals break the declaration and skip the wrapper. An omitted
 * module takes the package name; external preset types report the dedicated
 * deferral diagnostic.
 */

#include <algorithm>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

#include "../../src/Sema/NativeFFI/ArkTS/ArkTSInterop.h"
#include "../../src/Sema/NativeFFI/ArkTS/FunctionSource.h"
#include "gtest/gtest.h"
#include "TestCompilerInstance.h"
#include "cangjie/AST/Types.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Modules/ASTSerialization.h"
#include "cangjie/Modules/CjoManager.h"

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
// Support packages defined below the diagnostic tests that inject them.
extern const char* ARKTS_INTEROP_SUPPORT;
extern const char* ARKTS_CONTEXT_SUPPORT;
extern const char* ARKTS_LANG_SUPPORT;
extern const char* ARKTS_STDLIB_SUPPORT;
extern const char* ARKTS_BUSINESS_EXCEPTION_SUPPORT;
} // namespace

namespace {
std::unordered_map<std::string, std::string> GetEnvironmentVars()
{
    std::unordered_map<std::string, std::string> envVars;
    char **env = CJ_ENVIRON;
    while (env && *env) {
        std::string entry(*env);
        size_t pos = entry.find('=');
        if (pos != std::string::npos) {
            envVars[entry.substr(0, pos)] = entry.substr(pos + 1);
        }
        ++env;
    }
    return envVars;
}

// Records every error message while keeping the default compiler handler
// behavior (old-to-refactor conversion, deduplication, category gating and
// error counting) fully intact.
class RecordingDiagnosticHandler : public CompilerDiagnosticHandler {
public:
    struct RecordedError {
        std::string message;
        Range range;
        // Rendered ":line:column:" of the diagnostic anchor. Refactor
        // diagnostics keep d.start/d.end zero and store the real range only in
        // mainHint.range, so the position must be taken from there.
        std::string hint;
        std::vector<std::string> notes;
    };

    explicit RecordingDiagnosticHandler(DiagnosticEngine& diag) : CompilerDiagnosticHandler(diag)
    {
    }

    void HandleDiagnose(Diagnostic& d) override
    {
        CompilerDiagnosticHandler::HandleDiagnose(d);
        if (d.diagSeverity == DiagSeverity::DS_ERROR) {
            messages.push_back(d.errorMessage);
            const auto& mainRange = d.mainHint.range;
            const auto& begin = mainRange.begin.IsZero() ? d.start : mainRange.begin;
            const auto& end = mainRange.begin.IsZero() ? d.end : mainRange.end;
            std::vector<std::string> notes;
            for (const auto& note : d.subDiags) {
                notes.push_back(note.subDiagMessage);
            }
            errors.push_back({d.errorMessage, MakeRange(begin, end),
                ":" + std::to_string(begin.line) + ":" + std::to_string(begin.column) + ":", std::move(notes)});
        } else if (d.diagSeverity == DiagSeverity::DS_WARNING) {
            warnings.push_back(d.errorMessage);
        }
    }

    const RecordedError* FindErrorContaining(const std::string& needle) const
    {
        for (const auto& error : errors) {
            if (error.message.find(needle) != std::string::npos) {
                return &error;
            }
        }
        return nullptr;
    }

    std::vector<std::string> messages;
    std::vector<RecordedError> errors;
    std::vector<std::string> warnings;
};
} // namespace

class ArkTSForeignDiagTest : public testing::Test {
protected:
    void SetUp() override
    {
        // ArkTS interop is experimental: the checker enables its prepare and
        // lower passes only when the user passes --experimental. Tests that
        // call the ArkTS entry points directly also set the namespace gate.
        invocation.globalOptions.experimentalMode = true;
        Native::FFI::ArkTS::SetExperimentalEnabled(true);
#ifdef PROJECT_SOURCE_DIR
        // Gets the absolute path of the project from the compile parameter.
        projectPath = PROJECT_SOURCE_DIR;
#else
        // Just in case, give it a default value.
        // Assume the initial is in the build directory.
        projectPath = "..";
#endif
#ifdef __x86_64__
        invocation.globalOptions.target.arch = Cangjie::Triple::ArchType::X86_64;
#else
        invocation.globalOptions.target.arch = Cangjie::Triple::ArchType::AARCH64;
#endif
#ifdef _WIN32
        invocation.globalOptions.target.os = Cangjie::Triple::OSType::WINDOWS;
        invocation.globalOptions.executablePath = projectPath + "\\output\\bin\\";
#elif defined(__APPLE__)
        invocation.globalOptions.target.os = Cangjie::Triple::OSType::DARWIN;
        invocation.globalOptions.executablePath = projectPath + "/output/bin/";
#elif defined(__unix__)
        invocation.globalOptions.target.os = Cangjie::Triple::OSType::LINUX;
        invocation.globalOptions.executablePath = projectPath + "/output/bin/";
#endif
        std::string cangjieHome = projectPath + "/output";
        std::string cangjiePath = cangjieHome + "/modules/" + invocation.globalOptions.GetCangjieLibTargetPathName();
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
        // ArkTS interop is OHOS-only, and the host has no OHOS std module set,
        // so the front-end tests declare an OHOS target inside GlobalOptions.
        // The target is set only after the host module search path is
        // captured, so std modules still resolve from the host SDK while the
        // production target gate sees the real OHOS environment.
        invocation.globalOptions.target.env = Cangjie::Triple::Environment::OHOS;
        instance = std::make_unique<TestCompilerInstance>(invocation, diag);
        Cangjie::MacroProcMsger::GetInstance().CloseMacroSrv();
        // Swap the default handler for a recording subclass once the
        // instance is fully constructed; the engine owns the handler.
        auto recording = std::make_unique<RecordingDiagnosticHandler>(diag);
        handler = recording.get();
        diag.RegisterHandler(std::move(recording));
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

    // Compiles @p code through the full frontend up to Sema. Library output
    // mode keeps the missing-entry diagnostic out of the picture.
    void CompileSema(const std::string& code)
    {
        instance->code = code;
        instance->invocation.globalOptions.implicitPrelude = true;
        instance->invocation.globalOptions.outputMode = GlobalOptions::OutputMode::SHARED_LIB;
        instance->Compile(CompileStage::SEMA);
    }

    // Compiles @p code through the parse stage only. Used by the macro-name
    // regression tests: the builtin-annotation conflict is a parse diagnostic,
    // and stopping after Parse keeps the macro-evaluation machinery (and its
    // environment noise) out of the picture.
    void CompileParse(const std::string& code)
    {
        instance->code = code;
        instance->invocation.globalOptions.implicitPrelude = true;
        instance->invocation.globalOptions.outputMode = GlobalOptions::OutputMode::SHARED_LIB;
        instance->Compile(CompileStage::PARSE);
    }

    // Compiles the support package @p code and returns its serialized AST, so
    // a following compilation can import it without a prebuilt SDK module.
    // @p caches injects already exported dependencies into the support build.
    std::vector<uint8_t> ExportSupportPackage(
        const std::string& code, const std::vector<std::pair<std::string, std::vector<uint8_t>>>& caches = {})
    {
        diag.ClearError();
        TestCompilerInstance support(invocation, diag);
        for (auto& [name, data] : caches) {
            support.importManager->SetPackageCjoCache(name, data);
        }
        support.code = code;
        support.invocation.globalOptions.implicitPrelude = true;
        support.invocation.globalOptions.outputMode = GlobalOptions::OutputMode::SHARED_LIB;
        support.Compile(CompileStage::SEMA);
        EXPECT_EQ(diag.GetErrorCount(), 0);
        std::vector<uint8_t> cjoData;
        support.importManager->ExportAST(false, cjoData, *support.GetSourcePackages()[0]);
        return cjoData;
    }

    // Exports the support package @p code with one compiler-added class erased
    // from the serialized AST: the imported interface stays prepared while its
    // generated implementation class is missing, which is the stale-CJO shape
    // the implementation-class resolution defends against.
    std::vector<uint8_t> ExportSupportPackageWithoutCompilerAddedClass(const std::string& code,
        const std::string& className,
        const std::vector<std::pair<std::string, std::vector<uint8_t>>>& caches = {})
    {
        diag.ClearError();
        TestCompilerInstance support(invocation, diag);
        for (auto& [name, data] : caches) {
            support.importManager->SetPackageCjoCache(name, data);
        }
        support.code = code;
        support.invocation.globalOptions.implicitPrelude = true;
        support.invocation.globalOptions.outputMode = GlobalOptions::OutputMode::SHARED_LIB;
        support.Compile(CompileStage::SEMA);
        EXPECT_EQ(diag.GetErrorCount(), 0);
        std::vector<uint8_t> cjoData;
        auto pkg = support.GetSourcePackages()[0].get();
        for (auto& file : pkg->files) {
            auto& decls = file->decls;
            for (auto it = decls.begin(); it != decls.end();) {
                if ((*it)->identifier.Val() == className && (*it)->TestAttr(Attribute::COMPILER_ADD)) {
                    it = decls.erase(it);
                } else {
                    ++it;
                }
            }
        }
        support.importManager->ExportAST(false, cjoData, *pkg);
        return cjoData;
    }

    // Recreate the compiler instance with the three ArkTS support packages
    // visible; a test then compiles one snippet without repeating the export.
    void ResetWithArkTSSupport()
    {
        auto interopCjo = ExportSupportPackage(ARKTS_INTEROP_SUPPORT);
        auto contextCjo = ExportSupportPackage(ARKTS_CONTEXT_SUPPORT, {{"ohos.ark_interop", interopCjo}});
        auto langCjo = ExportSupportPackage(
            ARKTS_LANG_SUPPORT, {{"ohos.ark_interop", interopCjo}, {"cj_arkts_context_v1", contextCjo}});
        diag.ClearError();
        handler->messages.clear();
        instance = std::make_unique<TestCompilerInstance>(invocation, diag);
        instance->importManager->SetPackageCjoCache("ohos.ark_interop", interopCjo);
        instance->importManager->SetPackageCjoCache("cj_arkts_context_v1", contextCjo);
        instance->importManager->SetPackageCjoCache("arkts.lang", langCjo);
    }

    bool HasErrorContaining(const std::string& needle) const
    {
        for (const auto& message : handler->messages) {
            if (message.find(needle) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    size_t CountErrorsContaining(const std::string& needle) const
    {
        return static_cast<size_t>(std::count_if(handler->messages.begin(), handler->messages.end(),
            [&needle](const auto& message) { return message.find(needle) != std::string::npos; }));
    }

    // True when a declaration with @p name carries the refusal mark.
    bool IsDeclBroken(const std::string& name) const
    {
        bool broken = false;
        Walker walker(instance->GetSourcePackages()[0].get(), [&broken, &name](Ptr<Node> node) {
            auto decl = DynamicCast<Decl*>(node);
            if (decl && decl->identifier.Val() == name && decl->TestAttr(Attribute::IS_BROKEN)) {
                broken = true;
            }
            return VisitAction::WALK_CHILDREN;
        });
        walker.Walk();
        return broken;
    }

    // True when at least one extend declaration carries the refusal mark.
    bool HasBrokenExtend() const
    {
        bool broken = false;
        Walker walker(instance->GetSourcePackages()[0].get(), [&broken](Ptr<Node> node) {
            if (auto ed = DynamicCast<ExtendDecl*>(node); ed && ed->TestAttr(Attribute::IS_BROKEN)) {
                broken = true;
            }
            return VisitAction::WALK_CHILDREN;
        });
        walker.Walk();
        return broken;
    }

    // Finds the top-level function declaration named @p name.
    Ptr<FuncDecl> FindFunction(const std::string& name) const
    {
        Ptr<FuncDecl> found{nullptr};
        Walker walker(instance->GetSourcePackages()[0]->files[0].get(), [&found, &name](Ptr<Node> node) {
            auto candidate = DynamicCast<FuncDecl*>(node);
            if (candidate && candidate->identifier.Val() == name && candidate->funcBody) {
                found = candidate;
            }
            return VisitAction::WALK_CHILDREN;
        });
        walker.Walk();
        return found;
    }

    // Finds the top-level class declaration named @p name.
    Ptr<ClassDecl> FindClass(const std::string& name) const
    {
        Ptr<ClassDecl> found{nullptr};
        Walker walker(instance->GetSourcePackages()[0]->files[0].get(), [&found, &name](Ptr<Node> node) {
            auto candidate = DynamicCast<ClassDecl*>(node);
            if (candidate && candidate->identifier.Val() == name) {
                found = candidate;
            }
            return VisitAction::WALK_CHILDREN;
        });
        walker.Walk();
        return found;
    }

    // Export the three support packages and install them on a fresh
    // compiler instance. Interface and class wrapper tests share this setup.
    void PrepareArkTSSupport()
    {
        diag.ClearError();
        auto interopCjo = ExportSupportPackage(ARKTS_INTEROP_SUPPORT);
        auto contextCjo = ExportSupportPackage(ARKTS_CONTEXT_SUPPORT, {{"ohos.ark_interop", interopCjo}});
        auto langCjo = ExportSupportPackage(
            ARKTS_LANG_SUPPORT, {{"ohos.ark_interop", interopCjo}, {"cj_arkts_context_v1", contextCjo}});
        diag.ClearError();
        handler->messages.clear();
        instance = std::make_unique<TestCompilerInstance>(invocation, diag);
        instance->importManager->SetPackageCjoCache("ohos.ark_interop", interopCjo);
        instance->importManager->SetPackageCjoCache("cj_arkts_context_v1", contextCjo);
        instance->importManager->SetPackageCjoCache("arkts.lang", langCjo);
    }

    // Same as PrepareArkTSSupport, plus the std.interop.arkts package that
    // declares the ArkTSType root interface. The root is package-pinned, so the
    // relation tests must import it from its declaring package rather than
    // re-declaring it in a user package.
    void PrepareArkTSTypeRelationSupport()
    {
        diag.ClearError();
        auto interopCjo = ExportSupportPackage(ARKTS_INTEROP_SUPPORT);
        auto contextCjo = ExportSupportPackage(ARKTS_CONTEXT_SUPPORT, {{"ohos.ark_interop", interopCjo}});
        auto stdCjo = ExportSupportPackage(
            ARKTS_STDLIB_SUPPORT, {{"ohos.ark_interop", interopCjo}, {"cj_arkts_context_v1", contextCjo}});
        auto langCjo = ExportSupportPackage(ARKTS_LANG_SUPPORT,
            {{"ohos.ark_interop", interopCjo}, {"cj_arkts_context_v1", contextCjo}});
        diag.ClearError();
        handler->messages.clear();
        handler->errors.clear();
        instance = std::make_unique<TestCompilerInstance>(invocation, diag);
        instance->importManager->SetPackageCjoCache("ohos.ark_interop", interopCjo);
        instance->importManager->SetPackageCjoCache("cj_arkts_context_v1", contextCjo);
        instance->importManager->SetPackageCjoCache("std.interop.arkts", stdCjo);
        instance->importManager->SetPackageCjoCache("arkts.lang", langCjo);
    }

    std::string projectPath;
    std::string savedCangjieHome;
    std::string savedCangjiePath;
    DiagnosticEngine diag;
    CompilerInvocation invocation;
    std::unique_ptr<TestCompilerInstance> instance;
    RecordingDiagnosticHandler* handler{nullptr};
};

// An unsupported parameter type is a Sema error naming the parameter,
// the function and the type; the declaration is marked broken, no wrapper is
// generated and no secondary diagnostics flood the output.
TEST_F(ArkTSForeignDiagTest, UnsupportedParameterTypeIsDiagnosed)
{
    CompileSema(R"(package entry

@ArkTS[module: "actual"]
foreign func unsupported(x: IntNative): Unit
)");
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_TRUE(HasErrorContaining("unsupported type"));
    EXPECT_TRUE(HasErrorContaining("'IntNative'"));
    EXPECT_TRUE(HasErrorContaining("'unsupported'"));
    auto fd = FindFunction("unsupported");
    ASSERT_NE(fd, nullptr);
    EXPECT_EQ(fd->funcBody->body, nullptr);
}

// A Nothing parameter can never cross the boundary as an argument, so
// it is reported with the same parameter diagnostic.
TEST_F(ArkTSForeignDiagTest, NothingParameterIsDiagnosed)
{
    CompileSema(R"(package entry

@ArkTS[module: "actual"]
foreign func neverArg(x: Nothing): Unit
)");
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_TRUE(HasErrorContaining("unsupported type"));
    EXPECT_TRUE(HasErrorContaining("'Nothing'"));
}

// An unsupported return type is a Sema error on the return position.
TEST_F(ArkTSForeignDiagTest, UnsupportedReturnTypeIsDiagnosed)
{
    CompileSema(R"(package entry

@ArkTS[module: "actual"]
foreign func badReturn(x: Int32): IntNative
)");
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_TRUE(HasErrorContaining("return type"));
    EXPECT_TRUE(HasErrorContaining("is not supported"));
    EXPECT_TRUE(HasErrorContaining("'IntNative'"));
    auto fd = FindFunction("badReturn");
    ASSERT_NE(fd, nullptr);
    EXPECT_EQ(fd->funcBody->body, nullptr);
}

// Options must be string literals among 'module', 'namespace' and
// 'foreignName'; anything else (here an unknown name) is rejected.
TEST_F(ArkTSForeignDiagTest, UnknownOptionNameIsDiagnosed)
{
    CompileSema(R"(package entry

@ArkTS[bogus: "x"]
foreign func badOption(): Unit
)");
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_TRUE(HasErrorContaining("must be string literals among 'module', 'namespace' and 'foreignName'"));
    auto fd = FindFunction("badOption");
    ASSERT_NE(fd, nullptr);
    EXPECT_EQ(fd->funcBody->body, nullptr);
}

// An explicitly empty module option must not flow into
// requireArkModule("") and fail only at run time.
TEST_F(ArkTSForeignDiagTest, ExplicitEmptyModuleIsDiagnosed)
{
    CompileSema(R"(package entry

@ArkTS[module: ""]
foreign func emptyModule(x: Int32): Int32
)");
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_TRUE(HasErrorContaining("empty string"));
    EXPECT_TRUE(HasErrorContaining("'emptyModule'"));
    auto fd = FindFunction("emptyModule");
    ASSERT_NE(fd, nullptr);
    EXPECT_EQ(fd->funcBody->body, nullptr);
}

// The same explicit-empty rule covers foreignName: an empty name would reach
// callMethod("") and fail only at run time.
TEST_F(ArkTSForeignDiagTest, ExplicitEmptyForeignNameIsDiagnosed)
{
    CompileSema(R"(package entry

@ArkTS[foreignName: ""]
foreign func emptyName(x: Int32): Int32
)");
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_TRUE(HasErrorContaining("'foreignName' option"));
    EXPECT_TRUE(HasErrorContaining("empty string"));
    EXPECT_TRUE(HasErrorContaining("'emptyName'"));
    auto fd = FindFunction("emptyName");
    ASSERT_NE(fd, nullptr);
    EXPECT_EQ(fd->funcBody->body, nullptr);
    EXPECT_TRUE(fd->TestAttr(Attribute::IS_BROKEN));
}

// The empty check trims: a whitespace-only value is as unusable as an empty
// one for both options that had no empty-value rule before.
TEST_F(ArkTSForeignDiagTest, WhitespaceOnlyOptionsAreRejectedAsEmpty)
{
    CompileSema(R"(package entry

@ArkTS[module: "   "]
foreign func blankModule(x: Int32): Int32

@ArkTS[namespace: "\t"]
foreign func blankNamespace(x: Int32): Int32
)");
    EXPECT_EQ(diag.GetErrorCount(), 2);
    EXPECT_TRUE(HasErrorContaining("'module' option of @ArkTS foreign function 'blankModule' must not be an empty string"));
    EXPECT_TRUE(
        HasErrorContaining("'namespace' option of @ArkTS foreign function 'blankNamespace' must not be an empty string"));
    for (const auto& name : {"blankModule", "blankNamespace"}) {
        auto fd = FindFunction(name);
        ASSERT_NE(fd, nullptr) << name;
        EXPECT_EQ(fd->funcBody->body, nullptr) << name;
        EXPECT_TRUE(fd->TestAttr(Attribute::IS_BROKEN)) << name;
    }
}

// Declarations the function fold never lowers carry the same explicit-empty
// rule; the message names the option on the '@ArkTS' form.
TEST_F(ArkTSForeignDiagTest, DeclarationEmptyOptionValuesAreDiagnosed)
{
    CompileSema(R"(package entry

@ArkTS[module: "  "]
public class BlankModuleClass {}

@ArkTS[foreignName: ""]
public class EmptyNameClass {}

@ArkTS[namespace: ""]
public interface EmptyNamespaceInterface {}
)");
    EXPECT_EQ(diag.GetErrorCount(), 3);
    EXPECT_TRUE(HasErrorContaining("'module' option of '@ArkTS' must not be an empty string"));
    EXPECT_TRUE(HasErrorContaining("'foreignName' option of '@ArkTS' must not be an empty string"));
    EXPECT_TRUE(HasErrorContaining("'namespace' option of '@ArkTS' must not be an empty string"));
    EXPECT_FALSE(HasErrorContaining("must be string literals among"));
}

// An option can only be given once per @ArkTS layer: a repeated name is an
// illegal option set even when both values are equal. The Sema option fold
// reports it once (the parser adds no second report for the same mistake) and
// the declaration is broken without a wrapper.
TEST_F(ArkTSForeignDiagTest, RepeatedOptionIsDiagnosed)
{
    CompileSema(R"(package entry

@ArkTS[module: "actual", module: "actual"]
foreign func repeated(): Unit
)");
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_TRUE(HasErrorContaining("duplicated option 'module' in @ArkTS foreign function 'repeated'"));
    // One mistake yields one diagnostic: the declaration-level rule does not
    // add a second report for the same annotation, and the key/value shape
    // message does not fire for a fully legal repeated key.
    EXPECT_FALSE(HasErrorContaining("options of '@ArkTS' must be string literals"));
    EXPECT_FALSE(HasErrorContaining("must be string literals among"));
    auto fd = FindFunction("repeated");
    ASSERT_NE(fd, nullptr);
    EXPECT_EQ(fd->funcBody->body, nullptr);
    EXPECT_TRUE(fd->TestAttr(Attribute::IS_BROKEN));
}

// The duplicate message names the repeated key, not only the declaration, so
// the user can locate the offending option in a multi-option layer.
TEST_F(ArkTSForeignDiagTest, RepeatedForeignNameOptionIsNamedInTheDiagnostic)
{
    CompileSema(R"(package entry

@ArkTS[foreignName: "a", foreignName: "b"]
foreign func repeatedName(): Unit
)");
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_TRUE(HasErrorContaining("duplicated option 'foreignName' in @ArkTS foreign function 'repeatedName'"));
    EXPECT_FALSE(HasErrorContaining("must be string literals among"));
    auto fd = FindFunction("repeatedName");
    ASSERT_NE(fd, nullptr);
    EXPECT_EQ(fd->funcBody->body, nullptr);
    EXPECT_TRUE(fd->TestAttr(Attribute::IS_BROKEN));
}

// The option shape rule is not function-specific. The same layer contract
// applies to an @ArkTS on a class, and the declaration-level check reports an
// unknown option name instead of silently accepting the annotation. The
// declaration is not marked broken: class binding options do not take part in
// lowering in this version, so the diagnostic alone states the refusal.
TEST_F(ArkTSForeignDiagTest, ClassUnknownOptionIsDiagnosed)
{
    CompileSema(R"(package entry

@ArkTS[bogus: "x"]
public class BadClassOption {}
)");
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_TRUE(HasErrorContaining(
        "options of '@ArkTS' must be string literals among 'module', 'namespace' and 'foreignName'"));
    EXPECT_FALSE(HasErrorContaining("options of @ArkTS foreign function"));
}

// A class option must also be a string literal, not any expression that
// happens to evaluate to a string.
TEST_F(ArkTSForeignDiagTest, ClassNonLiteralOptionIsDiagnosed)
{
    CompileSema(R"(package entry

let optionValue = "utils"

@ArkTS[module: optionValue]
public class BadNonLiteralOption {}
)");
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_TRUE(HasErrorContaining(
        "options of '@ArkTS' must be string literals among 'module', 'namespace' and 'foreignName'"));
}

// A name repeated inside one layer is illegal on a class too, and the
// declaration yields exactly one diagnostic.
TEST_F(ArkTSForeignDiagTest, ClassRepeatedOptionIsDiagnosed)
{
    CompileSema(R"(package entry

@ArkTS[module: "a", module: "b"]
public class BadRepeatedOption {}
)");
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_TRUE(HasErrorContaining("duplicated option 'module' in '@ArkTS'"));
    EXPECT_FALSE(HasErrorContaining("must be string literals among"));
}

// An interface option set is checked by the same rule; an empty namespace
// segment is rejected even though the rest of the layer is well formed.
TEST_F(ArkTSForeignDiagTest, InterfaceIllFormedNamespaceIsDiagnosed)
{
    CompileSema(R"(package entry

@ArkTS[namespace: "a..b"]
public interface BadNamespaceOption {}
)");
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_TRUE(HasErrorContaining(
        "'namespace' option 'a..b' of '@ArkTS' must be a dot-separated identifier path with no empty segments"));
    EXPECT_FALSE(HasErrorContaining("must be string literals among"));
}

// An extend whose extended type carries '@ArkTS' is refused: the ArkTS side
// has no extend form, so the added members would be invisible to ArkTS. Both
// a non-foreign class and a non-foreign enum are covered.
TEST_F(ArkTSForeignDiagTest, ExtendOfArkTSDeclIsRejected)
{
    CompileSema(R"(package entry

@ArkTS
public class ExportedClass {}

extend ExportedClass {
    public func use(): Unit {}
}

@ArkTS
public enum ExportedEnum {
    | Red
}

extend ExportedEnum {
    public func label(): Int32 { return 0 }
}
)");
    EXPECT_EQ(CountErrorsContaining("cannot be extended"), 2U);
    EXPECT_TRUE(HasErrorContaining("'@ArkTS' declaration 'ExportedClass' cannot be extended"));
    EXPECT_TRUE(HasErrorContaining("'@ArkTS' declaration 'ExportedEnum' cannot be extended"));
}

// The same extend shape stays legal when the extended type is not @ArkTS.
TEST_F(ArkTSForeignDiagTest, ExtendOfPlainTypeIsAccepted)
{
    CompileSema(R"(package entry

public class Plain {}

extend Plain {
    public func use(): Unit {}
}
)");
    EXPECT_EQ(diag.GetErrorCount(), 0) << (handler->messages.empty() ? "" : handler->messages.front());
}

// A global variable carries the annotation on the same terms.
TEST_F(ArkTSForeignDiagTest, GlobalVariableUnknownOptionIsDiagnosed)
{
    CompileSema(R"(package entry

@ArkTS[foreignName: 1]
public let badGlobal: Int32 = 1
)");
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_TRUE(HasErrorContaining(
        "options of '@ArkTS' must be string literals among 'module', 'namespace' and 'foreignName'"));
}

// A parameter-level @ArkTS is validated by the same declaration walk, with
// the enclosing declaration free of any other refusal so exactly one
// diagnostic is produced.
TEST_F(ArkTSForeignDiagTest, ParameterUnknownOptionIsDiagnosed)
{
    CompileSema(R"(package entry

func plain(@ArkTS[bogus: "x"] p: Int32): Unit {}
)");
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_TRUE(HasErrorContaining(
        "options of '@ArkTS' must be string literals among 'module', 'namespace' and 'foreignName'"));
}

// The option diagnostic stays reserved for illegal annotation options: an
// unmappable Option<T> element is a type problem and reports the parameter
// diagnostic, never the option message.
TEST_F(ArkTSForeignDiagTest, UnmappableOptionReportsTheTypeDiagnostic)
{
    CompileSema(R"(package entry

@ArkTS[module: "actual"]
foreign func optional(x: Option<IntNative>): Unit
)");
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_TRUE(HasErrorContaining("parameter 'x' of @ArkTS foreign function 'optional' has unsupported type"));
    EXPECT_TRUE(HasErrorContaining("Option<IntNative>"));
    EXPECT_FALSE(HasErrorContaining("must be string literals among 'module', 'namespace' and 'foreignName'"));
    auto fd = FindFunction("optional");
    ASSERT_NE(fd, nullptr);
    EXPECT_EQ(fd->funcBody->body, nullptr);
}

// The converse pairing: a non-string option value is an illegal option and
// reports the option diagnostic on the declaration, not a type diagnostic.
TEST_F(ArkTSForeignDiagTest, NonStringOptionValueReportsTheOptionDiagnostic)
{
    CompileSema(R"(package entry

@ArkTS[module: 1]
foreign func badValue(): Unit
)");
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_TRUE(HasErrorContaining("must be string literals among 'module', 'namespace' and 'foreignName'"));
    EXPECT_FALSE(HasErrorContaining("has unsupported type"));
    auto fd = FindFunction("badValue");
    ASSERT_NE(fd, nullptr);
    EXPECT_EQ(fd->funcBody->body, nullptr);
}

// A "./"-prefixed module is a same-directory relative path; a module
// specifier never starts with a dot-relative spelling, so the value is
// rejected up front with the illegal value and function named.
TEST_F(ArkTSForeignDiagTest, DotSlashModuleIsDiagnosed)
{
    CompileSema(R"(package entry

@ArkTS[module: "./x"]
foreign func relModule(x: Int32): Unit
)");
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_TRUE(HasErrorContaining("'module' option './x'"));
    EXPECT_TRUE(HasErrorContaining("'relModule'"));
    EXPECT_TRUE(HasErrorContaining("must be an ArkTS module specifier"));
    auto fd = FindFunction("relModule");
    ASSERT_NE(fd, nullptr);
    EXPECT_EQ(fd->funcBody->body, nullptr);
}

// A "../"-prefixed module names a parent-relative path and is rejected with
// the same diagnostic.
TEST_F(ArkTSForeignDiagTest, DotDotSlashModuleIsDiagnosed)
{
    CompileSema(R"(package entry

@ArkTS[module: "../x"]
foreign func parentModule(x: Int32): Unit
)");
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_TRUE(HasErrorContaining("'module' option '../x'"));
    EXPECT_TRUE(HasErrorContaining("'parentModule'"));
    auto fd = FindFunction("parentModule");
    ASSERT_NE(fd, nullptr);
    EXPECT_EQ(fd->funcBody->body, nullptr);
}

// A "/"-prefixed module is an OS-absolute path and is rejected with the same
// diagnostic; the three path shapes are the complete format check.
TEST_F(ArkTSForeignDiagTest, ModuleAbsolutePathIsDiagnosed)
{
    CompileSema(R"(package entry

@ArkTS[module: "/x"]
foreign func absoluteModule(x: Int32): Unit
)");
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_TRUE(HasErrorContaining("'module' option '/x'"));
    EXPECT_TRUE(HasErrorContaining("'absoluteModule'"));
    EXPECT_TRUE(HasErrorContaining("must be an ArkTS module specifier"));
    auto fd = FindFunction("absoluteModule");
    ASSERT_NE(fd, nullptr);
    EXPECT_EQ(fd->funcBody->body, nullptr);
}

// A bare dot segment is a relative path, not a module specifier: it is
// rejected at compile time together with the "./x" and "../x" prefixes.
TEST_F(ArkTSForeignDiagTest, ModuleBareDotSegmentsAreDiagnosed)
{
    CompileSema(R"(package entry

@ArkTS[module: "."]
foreign func dotModule(x: Int32): Unit

@ArkTS[module: ".."]
foreign func dotDotModule(x: Int32): Unit
)");
    EXPECT_EQ(diag.GetErrorCount(), 2);
    EXPECT_TRUE(HasErrorContaining("'module' option '.'"));
    EXPECT_TRUE(HasErrorContaining("'dotModule'"));
    EXPECT_TRUE(HasErrorContaining("'module' option '..'"));
    EXPECT_TRUE(HasErrorContaining("'dotDotModule'"));
}

// A system module name ("@...") and a single-segment project module name pass
// the format check with zero format errors. The support declarations are not
// imported in this fixture, so lowering then refuses the wrapper; only those
// support-binding errors are expected.
TEST_F(ArkTSForeignDiagTest, LegalModuleFormatsPassTheFormatCheck)
{
    CompileSema(R"(package entry

class JSValue {
    func toJSValue(): JSValue { return this }
    func isNull(): Bool { return false }
    func isNumber(): Bool { return true }
    func toNumber(): Float64 { return 0.0 }
    func asObject(): JSObject { return JSObject() }
}

class JSObject {
    func callMethod(name: String, args: Array<JSValue>): JSValue { return JSValue() }
}

class JSContext {
    func requireArkModule(name: String): JSValue { return JSValue() }
    func string(value: String): JSValue { return JSValue() }
}

func __cj_arkts_current(): JSContext { return JSContext() }

class ExternConversionException <: Exception {
    public init(message: String) { super(message) }
}

@ArkTS[module: "@ohos.util"]
foreign func namedModule(text: String): Float64

@ArkTS[module: "entry"]
foreign func projectModule(text: String): Float64
)");
    EXPECT_EQ(diag.GetErrorCount(), 2);
    EXPECT_TRUE(HasErrorContaining("requires ArkTS interop declarations that are not available"));
    EXPECT_FALSE(HasErrorContaining("'module' option"));
    EXPECT_EQ(FindFunction("namedModule")->funcBody->body, nullptr);
    EXPECT_EQ(FindFunction("projectModule")->funcBody->body, nullptr);
}

// A rest parameter position must carry Array<T'> (copy semantics); any other
// type is rejected by the rest check with the dedicated diagnostic. Parse only
// checks the placement, Sema checks the type.
TEST_F(ArkTSForeignDiagTest, NonArrayRestParameterIsDiagnosed)
{
    CompileSema(R"(package entry

@ArkTS[module: "actual"]
foreign func badRest(@RestParam xs: Int32): Unit
)");
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_TRUE(HasErrorContaining("rest parameter"));
    EXPECT_TRUE(HasErrorContaining("'xs'"));
    EXPECT_TRUE(HasErrorContaining("'Int32'"));
    auto fd = FindFunction("badRest");
    ASSERT_NE(fd, nullptr);
    EXPECT_EQ(fd->funcBody->body, nullptr);
    // Pins the LowerArkTSFunctions After-side claim: the "rest parameter"
    // diagnostic marks the declaration broken.
    EXPECT_TRUE(fd->TestAttr(Attribute::IS_BROKEN));
}

// An omitted module takes the package name; the format check rejects only an
// empty name, so the sole error here is the missing support import.
TEST_F(ArkTSForeignDiagTest, OmittedModulePassesTheModuleCheck)
{
    CompileSema(R"(package entry

class JSValue {
    func toJSValue(): JSValue { return this }
    func isNull(): Bool { return false }
    func isNumber(): Bool { return true }
    func toNumber(): Float64 { return 0.0 }
    func asObject(): JSObject { return JSObject() }
}

class JSObject {
    func callMethod(name: String, args: Array<JSValue>): JSValue { return JSValue() }
}

class JSContext {
    func requireArkModule(name: String): JSValue { return JSValue() }
    func string(value: String): JSValue { return JSValue() }
}

func __cj_arkts_current(): JSContext { return JSContext() }

class ExternConversionException <: Exception {
    public init(message: String) { super(message) }
}

@ArkTS foreign func parseInt(text: String): Float64
)");
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_TRUE(HasErrorContaining("requires ArkTS interop declarations that are not available"));
    EXPECT_FALSE(HasErrorContaining("'module' option"));
    auto fd = FindFunction("parseInt");
    ASSERT_NE(fd, nullptr);
    EXPECT_EQ(fd->funcBody->body, nullptr);
}

// A position spelled with one of the external ArkTS preset types is rejected
// with the dedicated deferral diagnostic instead of the generic
// unsupported-type error, so a user following the authoritative spelling learns
// that the mapping is deferred rather than mistyped. Recognition is pinned to
// the declaring package (std.interop.arkts, like BigInt in std.math.numeric),
// so the preset declarations are spelled in place here: the 1.2.0 toolchain
// ships no std.interop.arkts module, and only the declaring package - not the
// type name - selects the deferral message. The prepared generic preset
// array lowers like reference leaves, so an ArkTSArray position spelled with
// an unprepared declaration keeps the generic message - the shape a stale CJO
// produces.
TEST_F(ArkTSForeignDiagTest, PresetExternalTypesGetDeferralDiagnostic)
{
    CompileSema(R"(package std.interop.arkts

class ArkTSString {}

interface ArkTSType {}

@ArkTS[module: "actual"]
foreign func wrap(text: ArkTSString): Unit

@ArkTS[module: "actual"]
foreign func dynamic(value: ArkTSType): Unit
)");
    ASSERT_EQ(diag.GetErrorCount(), 2);
    // The deferral message names the type and the deferral only: the reference
    // semantics of that external type are unavailable in this version. The
    // position (parameter/return) is carried by the diagnostic range, not the
    // text. The prepared generic preset array lowers like reference leaves,
    // so this diagnostic never names it; the stale-CJO array shape is pinned
    // in ArkTSTypeMappingTest.
    EXPECT_TRUE(HasErrorContaining("the reference semantics of the external ArkTS type 'ArkTSString' is not available"));
    EXPECT_TRUE(HasErrorContaining("the reference semantics of the external ArkTS type 'ArkTSType' is not available"));
    EXPECT_TRUE(HasErrorContaining("is not available in this version"));
    EXPECT_FALSE(HasErrorContaining("copy mapping"));
    EXPECT_FALSE(
        HasErrorContaining("the reference semantics of the external ArkTS type 'ArkTSArray' is not available"));
    for (const auto& name : {"wrap", "dynamic"}) {
        auto fd = FindFunction(name);
        ASSERT_NE(fd, nullptr) << name;
        // The deferral diagnostic marks the declaration broken while
        // classification keeps returning UNSUPPORTED for the position.
        EXPECT_EQ(fd->funcBody->body, nullptr) << name;
        EXPECT_TRUE(fd->TestAttr(Attribute::IS_BROKEN)) << name;
    }
}

// A class that merely shares a preset type's name is not an external ArkTS
// type: recognition is pinned to the declaring package, so a same-named user
// class keeps the generic unsupported-type diagnostics and never receives the
// deferral message.
TEST_F(ArkTSForeignDiagTest, SameNameUserTypeKeepsTheGenericDiagnostic)
{
    CompileSema(R"(package entry

class ArkTSArray<T> {}

class ArkTSString {}

@ArkTS[module: "actual"]
foreign func echoParam(values: ArkTSArray<Int32>): Unit

@ArkTS[module: "actual"]
foreign func source(): ArkTSArray<Int32>

@ArkTS[module: "actual"]
foreign func wrap(text: ArkTSString): Unit
)");
    ASSERT_EQ(diag.GetErrorCount(), 3);
    EXPECT_TRUE(HasErrorContaining("parameter 'values' of @ArkTS foreign function 'echoParam' has unsupported type"));
    EXPECT_TRUE(HasErrorContaining("of @ArkTS foreign function 'source' is not supported"));
    EXPECT_TRUE(HasErrorContaining("parameter 'text' of @ArkTS foreign function 'wrap' has unsupported type"));
    EXPECT_FALSE(HasErrorContaining("reference semantics of the external ArkTS type"));
    for (const auto& name : {"echoParam", "source", "wrap"}) {
        auto fd = FindFunction(name);
        ASSERT_NE(fd, nullptr) << name;
        EXPECT_EQ(fd->funcBody->body, nullptr) << name;
        EXPECT_TRUE(fd->TestAttr(Attribute::IS_BROKEN)) << name;
    }
}

TEST_F(ArkTSForeignDiagTest, ArkTSTypeImplicitSupertypeAcceptsForeignLeaves)
{
    PrepareArkTSTypeRelationSupport();
    // The arkts.lang facade re-exports std.interop.arkts and, like the real
    // facade, brings the interop packages into scope so the @ArkTS wrapper
    // injection resolves; the root itself stays package-pinned.
    CompileSema(R"(package entry

import arkts.lang.*
import std.interop.arkts.*

@ArkTS[module: "actual", foreignName: "Person"]
public foreign class Person {
    public init()
}

@ArkTS[module: "actual", foreignName: "Face"]
public foreign interface Face {
    func hi(): Unit
}

public func assign(p: Person): ArkTSType {
    let x: ArkTSType = p
    return x
}

public func assignFace(f: Face): ArkTSType {
    return f
}

public func store<T>(v: T): T where T <: ArkTSType {
    return v
}

public func useGeneric(p: Person): ArkTSType {
    return store<Person>(p)
}

public func pick(v: ArkTSType): Int32 { return 1 }
public func pick(v: Int32): Int32 { return 2 }

public func choose(p: Person): Int32 {
    return pick(p)
}
)");
    ASSERT_EQ(diag.GetErrorCount(), 0) << (handler->messages.empty() ? "" : handler->messages.front());
    // Overload selection consumed the relation: `pick(p)` resolves to the
    // ArkTSType overload, not the Int32 one.
    Ptr<FuncDecl> resolved{nullptr};
    Walker(instance->GetSourcePackages()[0].get(), [&resolved](Ptr<Node> node) {
        auto call = DynamicCast<CallExpr*>(node);
        if (call == nullptr || call->resolvedFunction == nullptr) {
            return VisitAction::WALK_CHILDREN;
        }
        auto fd = DynamicCast<FuncDecl*>(call->resolvedFunction);
        if (fd != nullptr && fd->identifier.Val() == "pick") {
            resolved = fd;
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    ASSERT_NE(resolved, nullptr);
    ASSERT_FALSE(resolved->funcBody->paramLists.empty());
    auto paramTy = resolved->funcBody->paramLists[0]->params[0]->type->GetTy();
    ASSERT_TRUE(Ty::IsTyCorrect(paramTy));
    ASSERT_TRUE(paramTy->IsInterface());
    EXPECT_EQ(StaticCast<InterfaceTy>(*paramTy).decl->identifier.Val(), "ArkTSType");
}

// The relation is restricted to the design §3.3 population: a plain user
// class, a primitive and an Array composite are not ArkTSType subtypes even
// though the root interface is imported and usable.
TEST_F(ArkTSForeignDiagTest, ArkTSTypeImplicitSupertypeExcludesOrdinaryLeaves)
{
    PrepareArkTSTypeRelationSupport();
    CompileSema(R"(package entry

import arkts.lang.*
import std.interop.arkts.*

class Plain {}

func neg(p: Plain, i: Int32, a: Array<Int32>): Unit {
    let x1: ArkTSType = p
    let x2: ArkTSType = i
    let x3: ArkTSType = a
}
)");
    EXPECT_EQ(diag.GetErrorCount(), 3);
    EXPECT_EQ(CountErrorsContaining("mismatched types"), 3);
}

// The root interface is recognized only in std.interop.arkts. A user package
// that declares its own `ArkTSType` interface does not become the relation's
// root, so an @ArkTS foreign type is not assignable to it.
TEST_F(ArkTSForeignDiagTest, ArkTSTypeRelationIsPinnedToTheStdlibPackage)
{
    PrepareArkTSTypeRelationSupport();
    CompileSema(R"(package entry

@ArkTS
public class Local {}

public interface ArkTSType {}

public func f(v: Local): Unit {
    let x: ArkTSType = v
}
)");
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_EQ(CountErrorsContaining("mismatched types"), 1);
}

// The relation is static-only. `is`, `as` and a type pattern keep their
// declaration-based runtime check: none is lowered to an always-matching case
// (matchBeforeRuntime false), and each still emits the runtime check
// (needRuntimeTypeCheck true) instead of silently becoming "always true".
TEST_F(ArkTSForeignDiagTest, ArkTSTypeImplicitSupertypeStaysOutOfRuntimeChecks)
{
    PrepareArkTSTypeRelationSupport();
    instance->code = R"(package entry

import arkts.lang.*
import std.interop.arkts.*

@ArkTS[module: "actual", foreignName: "Person"]
public foreign class Person {
    public init()
}

public func isCheck(p: Person): Bool {
    return p is ArkTSType
}

public func asCheck(p: Person): Option<ArkTSType> {
    return p as ArkTSType
}

public func matchCheck(p: Person): Bool {
    match (p) {
        case _: ArkTSType => true
        case _ => false
    }
}
)";
    instance->invocation.globalOptions.implicitPrelude = true;
    instance->invocation.globalOptions.outputMode = GlobalOptions::OutputMode::SHARED_LIB;
    // Generic instantiation re-derives the runtime-check decision for type
    // patterns (UpdateTypePatternMatchResult), so this stage also pins that the
    // static-only relation does not leak into that recomputation.
    ASSERT_TRUE(instance->Compile(CompileStage::GENERIC_INSTANTIATION));
    ASSERT_EQ(diag.GetErrorCount(), 0) << (handler->messages.empty() ? "" : handler->messages.front());
    size_t patterns = 0;
    Walker(instance->GetSourcePackages()[0].get(), [&patterns](Ptr<Node> node) {
        auto tp = DynamicCast<TypePattern*>(node);
        if (tp == nullptr) {
            return VisitAction::WALK_CHILDREN;
        }
        ++patterns;
        EXPECT_FALSE(tp->matchBeforeRuntime);
        EXPECT_TRUE(tp->needRuntimeTypeCheck);
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    EXPECT_EQ(patterns, 3);
}

// A nested preset type reaches the same deferral diagnostic: the routing helper
// searches the type arguments recursively, so Option<ArkTSString> is not
// demoted to the generic message. Array<ArkTSArray<T>> left the deferral
// routing with the prepared array: the unprepared inner declaration stays
// unsupported, which poisons the whole Array position into the generic
// unsupported-type message.
TEST_F(ArkTSForeignDiagTest, NestedPresetExternalTypeGetsDeferralDiagnostic)
{
    CompileSema(R"(package std.interop.arkts

class ArkTSString {}

interface ArkTSType {}

@ArkTS[module: "actual"]
foreign func nestedOption(text: Option<ArkTSString>): Unit

@ArkTS[module: "actual"]
foreign func nestedDynamic(value: Option<ArkTSType>): Unit
)");
    ASSERT_EQ(diag.GetErrorCount(), 2);
    // The routing helper searches the type arguments recursively, so an
    // Option over a deferred preset is not demoted to the generic message.
    EXPECT_TRUE(HasErrorContaining("the reference semantics of the external ArkTS type 'ArkTSString' is not available"));
    EXPECT_TRUE(HasErrorContaining("the reference semantics of the external ArkTS type 'ArkTSType' is not available"));
    EXPECT_FALSE(HasErrorContaining("has unsupported type"));
}

// A rest parameter mixed with a named parameter is rejected by the parse
// rule before lowering, with the dedicated diagnostic; no wrapper body is
// produced and the declaration never reaches the generator.
TEST_F(ArkTSForeignDiagTest, MixedRestAndNamedParameterIsRejectedBeforeLowering)
{
    CompileSema(R"(package entry

@ArkTS[module: "actual"]
foreign func mixedRest(@RestParam xs: Array<Int32>, last!: Int32): Unit
)");
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_TRUE(HasErrorContaining("a function with a rest parameter must not declare named parameters"));
    auto fd = FindFunction("mixedRest");
    ASSERT_NE(fd, nullptr);
    EXPECT_EQ(fd->funcBody->body, nullptr);
}

// The preset array is a prepared generic wrapper: the compiler installs the
// JSObject carrier storage beside the runtime-written member bodies, and the
// bodies typecheck against the hidden `T <: JSInteropType<T>` constraint with
// ordinary Cangjie default arguments. The old bodyless foreign shape is gone.
TEST_F(ArkTSForeignDiagTest, PresetArrayPreparedGenericWrapperLowersBodies)
{
    PrepareArkTSSupport();
    CompileSema(R"(package std.interop.arkts

import ohos.ark_interop.*
import ohos.ark_interop.JSInteropType

@ArkTS[foreignName: "Array"]
public class ArkTSArray<T> where T <: JSInteropType<T> {
    public init(items: Array<T>) {
        let ctx = cj_arkts_context_v1.__cj_arkts_current()
        let jsItems = Array<JSValue>(items.size, { i => items[i].toJSValue(ctx) })
        this.__cj_arkts_ref = ctx.array(jsItems).toJSValue().asObject()
    }

    public prop length: Float64 {
        get() {
            let ctx = cj_arkts_context_v1.__cj_arkts_current()
            return Float64.fromJSValue(ctx, this.__cj_arkts_ref.getProperty("length"))
        }
    }

    public func pop(): Option<T> {
        let ctx = cj_arkts_context_v1.__cj_arkts_current()
        let last = this.__cj_arkts_ref.callMethod("pop", Array<JSValue>())
        if (last.isNull() || last.isUndefined()) {
            return Option<T>.None
        }
        return Option<T>.Some(T.fromJSValue(ctx, last))
    }

    public func join(separator!: Option<String> = None): String {
        let ctx = cj_arkts_context_v1.__cj_arkts_current()
        let args = match (separator) {
            case Option.Some(sep) => [ctx.string(sep).toJSValue()]
            case Option.None => Array<JSValue>()
        }
        let joined = this.__cj_arkts_ref.callMethod("join", args)
        return String.fromJSValue(ctx, joined)
    }
}
)");
    for (const auto& message : handler->messages) {
        fprintf(stderr, "B4C-DIAG-C: %s\n", message.c_str());
    }
    ASSERT_EQ(diag.GetErrorCount(), 0) << (handler->messages.empty() ? "" : handler->messages.front());
    auto cd = FindClass("ArkTSArray");
    ASSERT_NE(cd, nullptr);
    EXPECT_TRUE(cd->TestAttr(Attribute::ARKTS_CLASS_WRAPPER));
    // Preparation installed the compiler-owned carrier storage beside the
    // written members: the reference accessor and the two wrapping
    // constructors.
    bool hasAccessor = false;
    bool hasWrappingCtor = false;
    for (auto& member : cd->GetMemberDecls()) {
        if (member->identifier.Val() == "__cj_arkts_ref_value") {
            hasAccessor = true;
        }
        if (member->TestAttr(Attribute::COMPILER_ADD) && member->identifier.Val() == "init") {
            hasWrappingCtor = true;
        }
    }
    EXPECT_TRUE(hasAccessor);
    EXPECT_TRUE(hasWrappingCtor);
}

// A member spelling a compiler-owned name would collide with the generated
// reference field/accessor, so the class is refused as a whole.
TEST_F(ArkTSForeignDiagTest, ForeignClassReservedMemberIsDiagnosed)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign class Bad {
    public init()
    public func __cj_arkts_ref(): Unit
}
)");
    EXPECT_TRUE(HasErrorContaining("reserved for generated code"));
    EXPECT_TRUE(HasErrorContaining("'Bad'"));
}

// An operator member has no method-name spelling the wrapper could call:
// lowering it would emit callMethod("[]")-style code whose semantics do not
// match the ArkTS operator contract. The member is refused with the class
// reason diagnostic before any signature classification, marked broken and
// keeps no generated body, while the ordinary member of the same class still
// gets its wrapper.
TEST_F(ArkTSForeignDiagTest, ForeignClassOperatorMemberIsRejected)
{
    diag.ClearError();
    auto interopCjo = ExportSupportPackage(ARKTS_INTEROP_SUPPORT);
    auto contextCjo = ExportSupportPackage(ARKTS_CONTEXT_SUPPORT, {{"ohos.ark_interop", interopCjo}});
    auto langCjo = ExportSupportPackage(
        ARKTS_LANG_SUPPORT, {{"ohos.ark_interop", interopCjo}, {"cj_arkts_context_v1", contextCjo}});
    diag.ClearError();
    handler->messages.clear();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->importManager->SetPackageCjoCache("ohos.ark_interop", interopCjo);
    instance->importManager->SetPackageCjoCache("cj_arkts_context_v1", contextCjo);
    instance->importManager->SetPackageCjoCache("arkts.lang", langCjo);
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign class Indexed {
    public func plain(value: Int32): Int32
    public operator func +(index: Int32): Int32
}
)");
    EXPECT_TRUE(HasErrorContaining("operator members on foreign class are not supported"));
    EXPECT_TRUE(HasErrorContaining("'Indexed'"));
    // The refused member is broken and gets no wrapper body.
    auto member = FindFunction("+");
    ASSERT_NE(member, nullptr);
    EXPECT_TRUE(member->TestAttr(Attribute::IS_BROKEN));
    ASSERT_NE(member->funcBody, nullptr);
    EXPECT_EQ(member->funcBody->body, nullptr);
    // A member refusal marks only the member; the class keeps lowering its
    // remaining members and its default-argument pass.
    Ptr<ClassDecl> indexed = nullptr;
    Walker(instance->GetSourcePackages()[0]->files[0].get(), [&indexed](Ptr<Node> node) {
        auto candidate = DynamicCast<ClassDecl*>(node);
        if (candidate && candidate->identifier.Val() == "Indexed") {
            indexed = candidate;
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    ASSERT_NE(indexed, nullptr);
    EXPECT_FALSE(indexed->TestAttr(Attribute::IS_BROKEN));
    auto plain = FindFunction("plain");
    ASSERT_NE(plain, nullptr);
    ASSERT_NE(plain->funcBody, nullptr);
    EXPECT_NE(plain->funcBody->body, nullptr);
}

// The explicit-empty-value rule covers prepared foreign classes too: the
// wrapper path owns its own option fold (ArkTSDeclOptionsDiagnose skips
// ARKTS_CLASS_WRAPPER declarations), so an empty foreignName, an empty or
// whitespace-only namespace or a whitespace-only module is rejected with the
// same '@ArkTS' message as every other declaration instead of generating
// getProperty(""), an empty namespace segment or requireArkModule("").
TEST_F(ArkTSForeignDiagTest, ForeignClassEmptyOptionValuesAreDiagnosed)
{
    diag.ClearError();
    auto interopCjo = ExportSupportPackage(ARKTS_INTEROP_SUPPORT);
    auto contextCjo = ExportSupportPackage(ARKTS_CONTEXT_SUPPORT, {{"ohos.ark_interop", interopCjo}});
    auto langCjo = ExportSupportPackage(
        ARKTS_LANG_SUPPORT, {{"ohos.ark_interop", interopCjo}, {"cj_arkts_context_v1", contextCjo}});
    diag.ClearError();
    handler->messages.clear();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->importManager->SetPackageCjoCache("ohos.ark_interop", interopCjo);
    instance->importManager->SetPackageCjoCache("cj_arkts_context_v1", contextCjo);
    instance->importManager->SetPackageCjoCache("arkts.lang", langCjo);
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual", foreignName: ""]
public foreign class EmptyForeignName {
    public func byName(): Int32
}

@ArkTS[module: "actual", namespace: "  "]
public foreign class BlankNamespace {
    public func bySegment(): Int32
}

@ArkTS[module: "  "]
public foreign class BlankModule {
    public func byModule(): Int32
}
)");
    EXPECT_TRUE(HasErrorContaining("'foreignName' option of '@ArkTS' must not be an empty string"));
    EXPECT_TRUE(HasErrorContaining("'namespace' option of '@ArkTS' must not be an empty string"));
    EXPECT_TRUE(HasErrorContaining("'module' option of '@ArkTS' must not be an empty string"));
    // Each refused class lowers none of its members: the wrapper body that
    // would carry the empty value is never generated.
    for (const auto& name : {"byName", "bySegment", "byModule"}) {
        auto fd = FindFunction(name);
        ASSERT_NE(fd, nullptr) << name;
        ASSERT_NE(fd->funcBody, nullptr) << name;
        EXPECT_EQ(fd->funcBody->body, nullptr) << name;
    }
}

// The class wrapper path applies the same module-specifier rule as the
// function path: a relative or absolute path spelling is refused at compile
// time instead of reaching requireArkModule.
TEST_F(ArkTSForeignDiagTest, ForeignClassModulePathIsRejected)
{
    diag.ClearError();
    auto interopCjo = ExportSupportPackage(ARKTS_INTEROP_SUPPORT);
    auto contextCjo = ExportSupportPackage(ARKTS_CONTEXT_SUPPORT, {{"ohos.ark_interop", interopCjo}});
    auto langCjo = ExportSupportPackage(
        ARKTS_LANG_SUPPORT, {{"ohos.ark_interop", interopCjo}, {"cj_arkts_context_v1", contextCjo}});
    diag.ClearError();
    handler->messages.clear();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->importManager->SetPackageCjoCache("ohos.ark_interop", interopCjo);
    instance->importManager->SetPackageCjoCache("cj_arkts_context_v1", contextCjo);
    instance->importManager->SetPackageCjoCache("arkts.lang", langCjo);
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "./actual"]
public foreign class Pathy {
    public func plain(value: Int32): Int32
}
)");
    EXPECT_TRUE(HasErrorContaining(
        "'module' option './actual' of @ArkTS foreign class 'Pathy' must be an ArkTS module specifier"));
    auto modulePath = handler->FindErrorContaining("of @ArkTS foreign class 'Pathy'");
    ASSERT_NE(modulePath, nullptr);
    EXPECT_EQ(modulePath->hint, ":5:16:");
    Ptr<ClassDecl> pathy = nullptr;
    Walker(instance->GetSourcePackages()[0]->files[0].get(), [&pathy](Ptr<Node> node) {
        auto candidate = DynamicCast<ClassDecl*>(node);
        if (candidate && candidate->identifier.Val() == "Pathy") {
            pathy = candidate;
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    ASSERT_NE(pathy, nullptr);
    EXPECT_TRUE(pathy->TestAttr(Attribute::IS_BROKEN));
    auto plain = FindFunction("plain");
    ASSERT_NE(plain, nullptr);
    ASSERT_NE(plain->funcBody, nullptr);
    EXPECT_EQ(plain->funcBody->body, nullptr);
}

// A prepared @ArkTS foreign class owns its own option fold (the declaration
// walk skips ARKTS_CLASS_WRAPPER), so a name repeated inside one @ArkTS layer
// keeps the dedicated duplicated-option message instead of degrading to the
// generic option-shape refusal.
TEST_F(ArkTSForeignDiagTest, ForeignClassRepeatedOptionIsDiagnosed)
{
    diag.ClearError();
    auto interopCjo = ExportSupportPackage(ARKTS_INTEROP_SUPPORT);
    auto contextCjo = ExportSupportPackage(ARKTS_CONTEXT_SUPPORT, {{"ohos.ark_interop", interopCjo}});
    auto langCjo = ExportSupportPackage(
        ARKTS_LANG_SUPPORT, {{"ohos.ark_interop", interopCjo}, {"cj_arkts_context_v1", contextCjo}});
    diag.ClearError();
    handler->messages.clear();
    handler->errors.clear();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->importManager->SetPackageCjoCache("ohos.ark_interop", interopCjo);
    instance->importManager->SetPackageCjoCache("cj_arkts_context_v1", contextCjo);
    instance->importManager->SetPackageCjoCache("arkts.lang", langCjo);
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "a", module: "b"]
public foreign class RepeatedOption {
    public func plain(): Int32
}
)");
    // Only the option message is asserted; the exact error count is not
    // asserted because refusing a prepared class can produce further
    // diagnostics outside this rule.
    EXPECT_TRUE(HasErrorContaining("duplicated option 'module' in '@ArkTS'"));
    EXPECT_FALSE(HasErrorContaining("must be string literals among"));
    auto repeated = FindClass("RepeatedOption");
    ASSERT_NE(repeated, nullptr);
    EXPECT_TRUE(repeated->TestAttr(Attribute::IS_BROKEN));
}

// The same prepared-class fold keeps the namespace-specific message for an
// ill-formed namespace value, anchored on the namespace literal.
TEST_F(ArkTSForeignDiagTest, ForeignClassIllFormedNamespaceIsDiagnosed)
{
    diag.ClearError();
    auto interopCjo = ExportSupportPackage(ARKTS_INTEROP_SUPPORT);
    auto contextCjo = ExportSupportPackage(ARKTS_CONTEXT_SUPPORT, {{"ohos.ark_interop", interopCjo}});
    auto langCjo = ExportSupportPackage(
        ARKTS_LANG_SUPPORT, {{"ohos.ark_interop", interopCjo}, {"cj_arkts_context_v1", contextCjo}});
    diag.ClearError();
    handler->messages.clear();
    handler->errors.clear();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->importManager->SetPackageCjoCache("ohos.ark_interop", interopCjo);
    instance->importManager->SetPackageCjoCache("cj_arkts_context_v1", contextCjo);
    instance->importManager->SetPackageCjoCache("arkts.lang", langCjo);
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual", namespace: "a..b"]
public foreign class IllFormedNamespace {
    public func plain(): Int32
}
)");
    // Only the namespace message and its anchor are asserted; the exact
    // error count is not asserted because refusing a prepared class can
    // produce further diagnostics outside this rule.
    EXPECT_TRUE(HasErrorContaining(
        "'namespace' option 'a..b' of '@ArkTS' must be a dot-separated identifier path with no empty segments"));
    EXPECT_FALSE(HasErrorContaining("must be string literals among"));
    const auto* error = handler->FindErrorContaining("'namespace' option 'a..b'");
    ASSERT_NE(error, nullptr);
    // The namespace literal sits on line 5, column 37 (the opening quote),
    // not on the class declaration on line 6.
    EXPECT_NE(error->hint.find(":5:37:"), std::string::npos) << error->hint;
    EXPECT_EQ(error->hint.find(":6:"), std::string::npos) << error->hint;
    auto illFormed = FindClass("IllFormedNamespace");
    ASSERT_NE(illFormed, nullptr);
    EXPECT_TRUE(illFormed->TestAttr(Attribute::IS_BROKEN));
}

// The duplicate-option anchor is the offending value, not the first
// annotation: @Deprecated sits before @ArkTS here, so anchoring on
// annotations.front() would point at the wrong annotation.
TEST_F(ArkTSForeignDiagTest, ForeignClassOptionAnchorsAtArkTSAnnotation)
{
    diag.ClearError();
    auto interopCjo = ExportSupportPackage(ARKTS_INTEROP_SUPPORT);
    auto contextCjo = ExportSupportPackage(ARKTS_CONTEXT_SUPPORT, {{"ohos.ark_interop", interopCjo}});
    auto langCjo = ExportSupportPackage(
        ARKTS_LANG_SUPPORT, {{"ohos.ark_interop", interopCjo}, {"cj_arkts_context_v1", contextCjo}});
    diag.ClearError();
    handler->messages.clear();
    handler->errors.clear();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->importManager->SetPackageCjoCache("ohos.ark_interop", interopCjo);
    instance->importManager->SetPackageCjoCache("cj_arkts_context_v1", contextCjo);
    instance->importManager->SetPackageCjoCache("arkts.lang", langCjo);
    CompileSema(R"(package entry

import arkts.lang.*

@Deprecated["legacy"]
@ArkTS[module: "a", module: "b"]
public foreign class AnchoredOption {
    public func plain(): Int32
}
)");
    // Only the duplicate-option anchor is asserted; the exact error count is
    // not asserted because refusing a prepared class can produce further
    // diagnostics outside this rule.
    const auto* error = handler->FindErrorContaining("duplicated option 'module'");
    ASSERT_NE(error, nullptr);
    // The second 'module' literal sits on line 6, column 29; line 5 carries
    // the unrelated @Deprecated annotation.
    EXPECT_NE(error->hint.find(":6:29:"), std::string::npos) << error->hint;
    EXPECT_EQ(error->hint.find(":5:"), std::string::npos) << error->hint;
}

// A user-written getter body on a property is not in GetMemberDecls() as a
// FuncDecl, so the class shape rule must inspect the PropDecl accessors too:
// lowering supplies every accessor body and a user body is refused.
TEST_F(ArkTSForeignDiagTest, ForeignClassPropertyBodyIsRejected)
{
    diag.ClearError();
    auto interopCjo = ExportSupportPackage(ARKTS_INTEROP_SUPPORT);
    auto contextCjo = ExportSupportPackage(ARKTS_CONTEXT_SUPPORT, {{"ohos.ark_interop", interopCjo}});
    auto langCjo = ExportSupportPackage(
        ARKTS_LANG_SUPPORT, {{"ohos.ark_interop", interopCjo}, {"cj_arkts_context_v1", contextCjo}});
    diag.ClearError();
    handler->messages.clear();
    handler->errors.clear();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->importManager->SetPackageCjoCache("ohos.ark_interop", interopCjo);
    instance->importManager->SetPackageCjoCache("cj_arkts_context_v1", contextCjo);
    instance->importManager->SetPackageCjoCache("arkts.lang", langCjo);
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign class GetterBody {
    public prop x: Int32 {
        get() {
            return 1
        }
    }
}
)");
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_TRUE(HasErrorContaining("member 'x' must not have a body"));
    auto getterBody = FindClass("GetterBody");
    ASSERT_NE(getterBody, nullptr);
    EXPECT_TRUE(getterBody->TestAttr(Attribute::IS_BROKEN));
}

// The same rule covers the getter of a 'mut prop' that writes no setter body:
// the accessor set on the PropDecl is what matters, not whether a setter
// exists.
TEST_F(ArkTSForeignDiagTest, ForeignClassMutPropertyGetterBodyIsRejected)
{
    diag.ClearError();
    auto interopCjo = ExportSupportPackage(ARKTS_INTEROP_SUPPORT);
    auto contextCjo = ExportSupportPackage(ARKTS_CONTEXT_SUPPORT, {{"ohos.ark_interop", interopCjo}});
    auto langCjo = ExportSupportPackage(
        ARKTS_LANG_SUPPORT, {{"ohos.ark_interop", interopCjo}, {"cj_arkts_context_v1", contextCjo}});
    diag.ClearError();
    handler->messages.clear();
    handler->errors.clear();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->importManager->SetPackageCjoCache("ohos.ark_interop", interopCjo);
    instance->importManager->SetPackageCjoCache("cj_arkts_context_v1", contextCjo);
    instance->importManager->SetPackageCjoCache("arkts.lang", langCjo);
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign class MutGetterBody {
    public mut prop y: Int32 {
        get() {
            return 2
        }
    }
}
)");
    // A mut prop without a setter trips an independent accessor-shape rule;
    // the user-written getter body is what this test asserts.
    EXPECT_TRUE(HasErrorContaining("member 'y' must not have a body"));
    auto mutGetterBody = FindClass("MutGetterBody");
    ASSERT_NE(mutGetterBody, nullptr);
    EXPECT_TRUE(mutGetterBody->TestAttr(Attribute::IS_BROKEN));
}

// The setter body is covered by the same rule: a 'mut prop' that writes both
// accessors is refused with the property name, not the accessor spelling.
// This shape carries a setter, so the independent "property must have
// accessors" rule does not fire and the class yields exactly one diagnostic.
TEST_F(ArkTSForeignDiagTest, ForeignClassMutPropertySetterBodyIsRejected)
{
    diag.ClearError();
    auto interopCjo = ExportSupportPackage(ARKTS_INTEROP_SUPPORT);
    auto contextCjo = ExportSupportPackage(ARKTS_CONTEXT_SUPPORT, {{"ohos.ark_interop", interopCjo}});
    auto langCjo = ExportSupportPackage(
        ARKTS_LANG_SUPPORT, {{"ohos.ark_interop", interopCjo}, {"cj_arkts_context_v1", contextCjo}});
    diag.ClearError();
    handler->messages.clear();
    handler->errors.clear();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->importManager->SetPackageCjoCache("ohos.ark_interop", interopCjo);
    instance->importManager->SetPackageCjoCache("cj_arkts_context_v1", contextCjo);
    instance->importManager->SetPackageCjoCache("arkts.lang", langCjo);
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign class MutSetterBody {
    public mut prop z: Int32 {
        get() {
            return 0
        }
        set(v) {
        }
    }
}
)");
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_TRUE(HasErrorContaining("member 'z' must not have a body"));
    auto mutSetterBody = FindClass("MutSetterBody");
    ASSERT_NE(mutSetterBody, nullptr);
    EXPECT_TRUE(mutSetterBody->TestAttr(Attribute::IS_BROKEN));
}

// The named-parameter default rule covers bodyless members of an @ArkTS
// foreign class/interface too. Such a member has no wrapper of its own, so
// Sema reports the parameter and marks the member broken; the defaulted
// parameter of the test above stays accepted. The fixture stays generic so
// the class keeps its external declaration form: a prepared (non-generic)
// class would first be refused for its missing JSObject import and its
// members would get generated bodies before this rule runs.
TEST_F(ArkTSForeignDiagTest, BodylessForeignClassMemberNamedParameterWithoutDefaultIsRejected)
{
    CompileSema(R"(package std.interop.arkts

@ArkTS[foreignName: "String"]
public foreign class ArkTSString<T> {
    public init(value!: Option<String> = None)
    public func slice(start!: Option<Float64>, end!: Option<Float64> = None): ArkTSString<T>
}
)");
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_TRUE(HasErrorContaining("must have a default value"));
    EXPECT_TRUE(HasErrorContaining("'start'"));
    EXPECT_TRUE(HasErrorContaining("'slice'"));
    auto slice = FindFunction("slice");
    ASSERT_NE(slice, nullptr);
    // The member path marks only the declaration broken; the offending
    // parameter keeps its spelling and is not marked itself.
    EXPECT_TRUE(slice->TestAttr(Attribute::IS_BROKEN));
    ASSERT_FALSE(slice->funcBody->paramLists.empty());
    ASSERT_FALSE(slice->funcBody->paramLists[0]->params.empty());
    EXPECT_FALSE(slice->funcBody->paramLists[0]->params[0]->TestAttr(Attribute::IS_BROKEN));
}

// Most declaration-shape rules from the authority design refuse the class
// with the one class diagnostic; the open-function optional-parameter case
// reuses the language diagnostic instead. One class per rule keeps each
// refusal attributable even though one class reports only its first error.
TEST_F(ArkTSForeignDiagTest, ForeignClassShapeRulesAreDiagnosed)
{
    ResetWithArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign class PrivateMember {
    public init()
    private func hidden(): Unit
}

@ArkTS[module: "actual"]
public foreign class MemberVariable {
    public init()
    public var field: Int32
}

@ArkTS[module: "actual"]
public foreign class Finalized {
    public init()
    ~init() {}
}

@ArkTS[module: "actual"]
public foreign class WithBody {
    public init()
    public func run(): Unit {}
}

@ArkTS[module: "actual"]
public foreign class WithAccessor {
    public init()
    public prop value: Float64 {
        get() { 1.0 }
    }
}

@ArkTS[module: "actual"]
public foreign open class OpenOptional {
    public init()
    public open func greet(prefix!: Option<String> = None): String
}
)");
    EXPECT_TRUE(HasErrorContaining("'PrivateMember': member 'hidden' must not be private"));
    EXPECT_TRUE(HasErrorContaining("'MemberVariable': member variables and primary constructors are not supported"));
    EXPECT_TRUE(HasErrorContaining("'Finalized': a finalizer is not supported"));
    EXPECT_TRUE(HasErrorContaining("'WithBody': member 'run' must not have a body"));
    // The accessor-body rule reports the shared member-body reason, so an
    // accessor with a body is refused with the same message as any other
    // user-written member body, not a declaration-level accessor message.
    EXPECT_TRUE(HasErrorContaining("'WithAccessor': member 'value' must not have a body"));
    EXPECT_TRUE(HasErrorContaining("optional parameter cannot be used in 'open' function"));
}

// The wrapper stores a JSObject, so the imported declaration is required; a
// missing import refuses the class instead of generating unresolved code.
TEST_F(ArkTSForeignDiagTest, ForeignClassWithoutJSObjectImportIsDiagnosed)
{
    diag.ClearError();
    handler->messages.clear();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    CompileSema(R"(package entry

@ArkTS[module: "actual"]
public foreign class NoObject {
    public init()
}
)");
    EXPECT_TRUE(HasErrorContaining("the wrapper needs the imported declaration 'ohos.ark_interop.JSObject'"));
    EXPECT_TRUE(HasErrorContaining("'NoObject'"));
}

// The authority design requires every parent to be an @ArkTS class or
// interface; a plain Cangjie parent has no ArkTS counterpart to reference.
TEST_F(ArkTSForeignDiagTest, ForeignClassParentMustBeArkTS)
{
    ResetWithArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

public open class Plain {
    public init() {}
}

@ArkTS[module: "actual"]
public foreign class Child <: Plain {
    public init()
}
)");
    EXPECT_TRUE(HasErrorContaining("'Child': parent type 'Plain' must be an @ArkTS class or interface"));
}
TEST_F(ArkTSForeignDiagTest, ForeignClassMultipleOptionalParametersCompile)
{
    ResetWithArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign class Point {
    public init(x: Float64, y!: Option<Float64> = None)
    public func label(prefix!: Option<String> = None, suffix!: Option<String> = None): String
}

public func use(): String {
    let point = Point(1.0)
    return point.label()
}
)");
    ASSERT_EQ(diag.GetErrorCount(), 0) << (handler->messages.empty() ? "" : handler->messages.front());
}

// R-D3: a foreign class may inherit another foreign class. The child wrapper
// inherits the root's reference storage, so it declares no field and no
// accessor of its own, redeclares both wrapping constructors as super
// delegations (constructors are not inherited) and moves its whole
// construction into the super(...) thunk; the root keeps the direct body and
// owns the field, the accessor and the two wrapping-constructor overloads.
TEST_F(ArkTSForeignDiagTest, ForeignClassInheritsForeignClass)
{
    using namespace Cangjie::Native::FFI::ArkTS;
    ResetWithArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign open class Base {
    public init(name: String)
    public func ping(): Unit
}

@ArkTS[module: "actual"]
public foreign class Child <: Base {
    public init(name: String)
    public func shout(): Unit
}

public func use(): Unit {
    let child = Child("name")
    child.ping()
    child.shout()
}
)");
    ASSERT_EQ(diag.GetErrorCount(), 0) << (handler->messages.empty() ? "" : handler->messages.front());
    auto base = FindClass("Base");
    auto child = FindClass("Child");
    ASSERT_NE(base, nullptr);
    ASSERT_NE(child, nullptr);
    // One shared shape scan: own field/accessor declarations, wrapping
    // constructors (by the shared first-parameter name) and, per constructor,
    // whether the body reaches the parent through super and carries a thunk.
    auto describe = [](ClassDecl& cd) {
        struct Shape {
            size_t fields = 0;
            size_t accessors = 0;
            size_t wrappingCtors = 0;
            size_t thunkWrappingCtors = 0;
            std::vector<std::pair<size_t, size_t>> userCtors; // <superCalls, lambdas>
        } shape;
        for (auto& member : cd.GetMemberDecls()) {
            if (member->astKind == ASTKind::VAR_DECL && member->identifier.Val() == ARKTS_CLASS_REF_FIELD) {
                ++shape.fields;
            } else if (member->identifier.Val() == ARKTS_CLASS_REF_ACCESSOR) {
                ++shape.accessors;
            }
            auto fd = DynamicCast<FuncDecl*>(member.get());
            if (!fd || !fd->funcBody || !fd->TestAttr(Attribute::CONSTRUCTOR)) {
                continue;
            }
            const auto& params = fd->funcBody->paramLists.front()->params;
            if (fd->TestAttr(Attribute::COMPILER_ADD) && !params.empty() &&
                params.front()->identifier.Val() == ARKTS_CLASS_CTOR_PARAM) {
                ++shape.wrappingCtors;
                if (params.front()->type && params.front()->type->astKind == ASTKind::FUNC_TYPE) {
                    ++shape.thunkWrappingCtors;
                }
                continue;
            }
            size_t superCalls = 0;
            size_t lambdas = 0;
            Walker(fd->funcBody->body.get(), [&superCalls, &lambdas](Ptr<Node> node) {
                if (auto call = DynamicCast<CallExpr*>(node); call) {
                    auto ref = DynamicCast<RefExpr*>(call->baseFunc.get());
                    if (ref && ref->isSuper) {
                        ++superCalls;
                    }
                }
                if (node->astKind == ASTKind::LAMBDA_EXPR) {
                    ++lambdas;
                }
                return VisitAction::WALK_CHILDREN;
            }).Walk();
            shape.userCtors.emplace_back(superCalls, lambdas);
        }
        return shape;
    };
    auto baseShape = describe(*base);
    EXPECT_EQ(baseShape.fields, 1);
    EXPECT_EQ(baseShape.accessors, 1);
    EXPECT_EQ(baseShape.wrappingCtors, 2);
    EXPECT_EQ(baseShape.thunkWrappingCtors, 1);
    ASSERT_EQ(baseShape.userCtors.size(), 1);
    EXPECT_EQ(baseShape.userCtors.front().first, 0); // the root assigns its own field
    auto childShape = describe(*child);
    EXPECT_EQ(childShape.fields, 0); // storage is inherited from the root
    EXPECT_EQ(childShape.accessors, 0);
    EXPECT_EQ(childShape.wrappingCtors, 2);
    EXPECT_EQ(childShape.thunkWrappingCtors, 1);
    ASSERT_EQ(childShape.userCtors.size(), 1);
    // The child construction reaches the parent as one super(...) thunk.
    EXPECT_EQ(childShape.userCtors.front().first, 1);
    EXPECT_GE(childShape.userCtors.front().second, 1);
}

// R-D3 follow-up: a multi-level chain C <: B <: A. Every level below the root
// redeclares the two wrapping constructors as super delegations, so the chain
// stays recoverable from an ArkTS value at every level, and each level's user
// constructor forwards its own single construction to its direct parent
// (C's super target is B, B's is A; the root A stores the value).
TEST_F(ArkTSForeignDiagTest, ForeignClassMultiLevelWrapperChain)
{
    using namespace Cangjie::Native::FFI::ArkTS;
    ResetWithArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign open class A {
    public init(name: String)
    public func ping(): Unit
}

@ArkTS[module: "actual"]
public foreign open class B <: A {
    public init(name: String)
    public func mid(): Unit
}

@ArkTS[module: "actual"]
public foreign class C <: B {
    public init(name: String)
    public func shout(): Unit
}

public func use(c: C): Unit {
    let made = C("name")
    made.ping()
    made.mid()
    made.shout()
}
)");
    ASSERT_EQ(diag.GetErrorCount(), 0) << (handler->messages.empty() ? "" : handler->messages.front());
    auto a = FindClass("A");
    auto b = FindClass("B");
    auto c = FindClass("C");
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    ASSERT_NE(c, nullptr);
    // The resolved super targets: B's parent is A, C's parent is B - the
    // child constructor's super(...) reaches its own direct parent's
    // thunk-taking wrapping constructor, never a grandparent's.
    ASSERT_FALSE(b->inheritedTypes.empty());
    ASSERT_FALSE(c->inheritedTypes.empty());
    EXPECT_EQ(Ty::GetDeclPtrOfTy(b->inheritedTypes.front()->GetTy()), (Ptr<Decl>)a);
    EXPECT_EQ(Ty::GetDeclPtrOfTy(c->inheritedTypes.front()->GetTy()), (Ptr<Decl>)b);
    // Storage: only the root owns the field and the accessor; every level
    // below redeclares the two wrapping constructors and keeps exactly one
    // super-thunk user constructor.
    auto owns = [](ClassDecl& cd) {
        size_t fields = 0;
        size_t accessors = 0;
        size_t wrappingCtors = 0;
        size_t userCtorSuperCalls = 0;
        size_t userCtors = 0;
        for (auto& member : cd.GetMemberDecls()) {
            if (member->astKind == ASTKind::VAR_DECL && member->identifier.Val() == ARKTS_CLASS_REF_FIELD) {
                ++fields;
            } else if (member->identifier.Val() == ARKTS_CLASS_REF_ACCESSOR) {
                ++accessors;
            }
            auto fd = DynamicCast<FuncDecl*>(member.get());
            if (!fd || !fd->funcBody || !fd->TestAttr(Attribute::CONSTRUCTOR)) {
                continue;
            }
            const auto& params = fd->funcBody->paramLists.front()->params;
            if (fd->TestAttr(Attribute::COMPILER_ADD) && !params.empty() &&
                params.front()->identifier.Val() == ARKTS_CLASS_CTOR_PARAM) {
                ++wrappingCtors;
                continue;
            }
            ++userCtors;
            Walker(fd->funcBody->body.get(), [&userCtorSuperCalls](Ptr<Node> node) {
                if (auto call = DynamicCast<CallExpr*>(node); call) {
                    auto ref = DynamicCast<RefExpr*>(call->baseFunc.get());
                    if (ref && ref->isSuper) {
                        ++userCtorSuperCalls;
                    }
                }
                return VisitAction::WALK_CHILDREN;
            }).Walk();
        }
        return std::make_tuple(fields, accessors, wrappingCtors, userCtors, userCtorSuperCalls);
    };
    auto [aFields, aAccessors, aCtors, aUserCtors, aSuper] = owns(*a);
    EXPECT_EQ(aFields, 1);
    EXPECT_EQ(aAccessors, 1);
    EXPECT_EQ(aCtors, 2);
    ASSERT_EQ(aUserCtors, 1);
    EXPECT_EQ(aSuper, 0); // the root assigns its own field
    auto [bFields, bAccessors, bCtors, bUserCtors, bSuper] = owns(*b);
    EXPECT_EQ(bFields, 0);
    EXPECT_EQ(bAccessors, 0);
    EXPECT_EQ(bCtors, 2);
    ASSERT_EQ(bUserCtors, 1);
    EXPECT_EQ(bSuper, 1); // B forwards to A through one super(...) thunk
    auto [cFields, cAccessors, cCtors, cUserCtors, cSuper] = owns(*c);
    EXPECT_EQ(cFields, 0);
    EXPECT_EQ(cAccessors, 0);
    EXPECT_EQ(cCtors, 2);
    ASSERT_EQ(cUserCtors, 1);
    EXPECT_EQ(cSuper, 1); // C forwards to B, not straight to A
}

// R-D3 review follow-up: a wrapper parent whose own preparation failed (here:
// a private member) breaks the child directly, and the child declares no
// wrapper member at all - its child shape would resolve the generated
// super(...) delegations against a class that never became a wrapper. Only
// the parent's own refusal diagnostic is reported: a direct child of the
// broken class and every class below it break silently.
TEST_F(ArkTSForeignDiagTest, ForeignClassBrokenParentBreaksChild)
{
    using namespace Cangjie::Native::FFI::ArkTS;
    ResetWithArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign open class Base {
    public init()
    private func f(): Unit
}

@ArkTS[module: "actual"]
public foreign class Child <: Base {
    public init()
    public func shout(): Unit
}

@ArkTS[module: "actual"]
public foreign class Mid <: Base {
    public init()
}

@ArkTS[module: "actual"]
public foreign class Leaf <: Mid {
    public init()
}
)");
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_TRUE(HasErrorContaining("'Base': member 'f' must not be private"));
    auto base = FindClass("Base");
    auto child = FindClass("Child");
    auto mid = FindClass("Mid");
    auto leaf = FindClass("Leaf");
    ASSERT_NE(base, nullptr);
    ASSERT_NE(child, nullptr);
    ASSERT_NE(mid, nullptr);
    ASSERT_NE(leaf, nullptr);
    EXPECT_TRUE(base->TestAttr(Attribute::IS_BROKEN));
    EXPECT_TRUE(child->TestAttr(Attribute::IS_BROKEN));
    EXPECT_TRUE(mid->TestAttr(Attribute::IS_BROKEN));
    EXPECT_TRUE(leaf->TestAttr(Attribute::IS_BROKEN));
    // No class below the broken parent became a wrapper, so none of them
    // carries a compiler-added member: no field, no accessor, no wrapping
    // constructor, no super delegation anywhere.
    for (auto cd : {child, mid, leaf}) {
        EXPECT_FALSE(IsArkTSClassWrapper(*cd));
        size_t generated = 0;
        for (auto& member : cd->GetMemberDecls()) {
            if (member->TestAttr(Attribute::COMPILER_ADD)) {
                ++generated;
            }
        }
        EXPECT_EQ(generated, 0);
    }
}

// R-D3 quality follow-up: preparation is idempotent over the same AST. The
// broken-parent cascade removes the child's inherited-type entry, a
// destructive edit that a repeated pass must neither repeat nor turn into a
// crash, a duplicate refusal or a second set of generated members. The
// fixture's CompileSema already ran one preparation; this test runs the pass
// twice more on the same tree.
TEST_F(ArkTSForeignDiagTest, ForeignClassBrokenParentPreparationIsIdempotent)
{
    using namespace Cangjie::Native::FFI::ArkTS;
    ResetWithArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign open class Base {
    public init()
    private func f(): Unit
}

@ArkTS[module: "actual"]
public foreign class Child <: Base {
    public init()
}

@ArkTS[module: "actual"]
public foreign class Mid <: Base {
    public init()
}

@ArkTS[module: "actual"]
public foreign class Leaf <: Mid {
    public init()
}
)");
    ASSERT_EQ(diag.GetErrorCount(), 1);
    auto pkg = instance->GetSourcePackages().front();
    auto base = FindClass("Base");
    auto child = FindClass("Child");
    auto mid = FindClass("Mid");
    auto leaf = FindClass("Leaf");
    ASSERT_NE(base, nullptr);
    ASSERT_NE(child, nullptr);
    ASSERT_NE(mid, nullptr);
    ASSERT_NE(leaf, nullptr);
    // The first pass removed the entry that named a broken parent. Once the
    // explicit parent is gone the generic checker gives the class the
    // implicit Object parent, so the list is not empty - it never names
    // Base or Mid.
    auto inheritedNames = [](ClassDecl& cd) {
        std::vector<std::string> names;
        for (auto& type : cd.inheritedTypes) {
            auto ref = DynamicCast<RefType*>(type.get());
            names.emplace_back(ref ? ref->ref.identifier.Val() : std::string("<other>"));
        }
        return names;
    };
    const std::vector<std::string> objectOnly{"Object"};
    for (auto cd : {base, child, mid, leaf}) {
        EXPECT_EQ(inheritedNames(*cd), objectOnly);
    }
    const auto declsBefore = pkg->files.front()->decls.size();
    const auto messagesBefore = handler->messages.size();
    const auto errorsBefore = diag.GetErrorCount();
    PrepareTypeCheck(*pkg, diag, instance->GetSourceManager(), instance->importManager);
    PrepareTypeCheck(*pkg, diag, instance->GetSourceManager(), instance->importManager);
    // Stable result: no new refusal record, no second error, no second set of
    // generated members, and no further erase of the inheritance lists.
    EXPECT_EQ(diag.GetErrorCount(), errorsBefore);
    EXPECT_EQ(handler->messages.size(), messagesBefore);
    EXPECT_EQ(pkg->files.front()->decls.size(), declsBefore);
    for (auto cd : {child, mid, leaf}) {
        EXPECT_EQ(inheritedNames(*cd), objectOnly);
    }
    for (auto cd : {child, mid, leaf}) {
        EXPECT_FALSE(IsArkTSClassWrapper(*cd));
        size_t generated = 0;
        for (auto& member : cd->GetMemberDecls()) {
            if (member->TestAttr(Attribute::COMPILER_ADD)) {
                ++generated;
            }
        }
        EXPECT_EQ(generated, 0);
    }
}

// @WriteOnly is valid only on a member of an @ArkTS class or interface. On a
// plain Cangjie class the annotation is misplaced and creates no write-only
// semantics, so the property stays readable.
TEST_F(ArkTSForeignDiagTest, WriteOnlyOnPlainClassPropertyIsMisplaced)
{
    ResetWithArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

public class Plain {
    @WriteOnly
    public mut prop value: Int32 {
        get() { 0 }
        set(v) {}
    }
}

public func read(plain: Plain): Int32 {
    return plain.value
}
)");
    EXPECT_TRUE(HasErrorContaining(
        "only a member of an '@ArkTS' class or interface can be annotated with '@WriteOnly'"));
    EXPECT_EQ(CountErrorsContaining("invalid @WriteOnly placement"), 1);
    EXPECT_FALSE(HasErrorContaining("is write-only and cannot be read"));
}

// A write-only member must be mutable; an immutable property is refused by the
// placement matrix before class preparation can build a wrapper.
TEST_F(ArkTSForeignDiagTest, ForeignClassWriteOnlyRequiresMutableProperty)
{
    ResetWithArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign class Counter {
    public init()
    @WriteOnly public prop value: Int32
}
)");
    EXPECT_TRUE(HasErrorContaining("a write-only property must be declared as 'mut prop'"));
    EXPECT_EQ(CountErrorsContaining("invalid @WriteOnly placement"), 1);
    EXPECT_FALSE(HasErrorContaining("invalid @ArkTS foreign class"));
    EXPECT_TRUE(IsDeclBroken("Counter"));
}

// A mutable property of a foreign class is the supported placement.
TEST_F(ArkTSForeignDiagTest, WriteOnlyAcceptsForeignClassMutableProperty)
{
    ResetWithArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign class Counter {
    public init()
    @WriteOnly public mut prop value: Int32
}
)");
    EXPECT_EQ(CountErrorsContaining("invalid @WriteOnly placement"), 0);
    EXPECT_FALSE(IsDeclBroken("Counter"));
}

// Non-property declarations are never a valid @WriteOnly target. A function, a
// top-level variable and an enum declaration each report the placement once.
TEST_F(ArkTSForeignDiagTest, WriteOnlyOnNonPropertyDeclarationsIsMisplaced)
{
    ResetWithArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@WriteOnly
public func host(): Unit {}

@WriteOnly
public let topLevel: Int32 = 1

@WriteOnly
public enum Kind {
    | A
}
)");
    EXPECT_EQ(CountErrorsContaining("only a property member can be annotated with '@WriteOnly'"), 3);
    EXPECT_EQ(CountErrorsContaining("invalid @WriteOnly placement"), 3);
}

// The remaining declaration kinds are not properties either. A member
// variable, a local binding, a type alias, a struct and an extend each report
// the placement once.
TEST_F(ArkTSForeignDiagTest, WriteOnlyOnOtherDeclarationKindsIsMisplaced)
{
    ResetWithArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

public class Holder {
    @WriteOnly
    public var field: Int32 = 0
}

@WriteOnly
public type BadAlias = Int32

@WriteOnly
public struct BadStruct {}

public class Box {}

@WriteOnly
extend Box {}

public func host(@WriteOnly p: Int32): Unit {
    @WriteOnly
    let localValue = 1
}
)");
    EXPECT_EQ(CountErrorsContaining("invalid @WriteOnly placement"), 6);
}

// An enum constructor is parser-owned; an enum member function is a
// non-property member and follows the placement rule.
TEST_F(ArkTSForeignDiagTest, WriteOnlyOnEnumMemberFunctionIsMisplaced)
{
    ResetWithArkTSSupport();
    CompileSema(R"(package entry

public enum Mode {
    A
    @WriteOnly
    public func describe(): String { "a" }
}
)");
    EXPECT_EQ(CountErrorsContaining("invalid @WriteOnly placement"), 1);
}

// The valid positions are the mutable property members of an @ArkTS class and
// an @ArkTS interface, including non-foreign declarations.
TEST_F(ArkTSForeignDiagTest, WriteOnlyPlacementAcceptsArkTSTypeMembers)
{
    ResetWithArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS
public open class Exported {
    @WriteOnly
    public mut prop value: Int32 {
        get() { 0 }
        set(v) {}
    }
}

@ArkTS
public interface IExported {
    @WriteOnly
    public mut prop value: Int32
}
)");
    EXPECT_EQ(CountErrorsContaining("invalid @WriteOnly placement"), 0);
}

// A member function and a parameter are not properties either.
TEST_F(ArkTSForeignDiagTest, WriteOnlyOnMemberFunctionAndParameterIsMisplaced)
{
    ResetWithArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

public class Host {
    @WriteOnly
    public func member(@WriteOnly p: Int32): Unit {}
}
)");
    EXPECT_EQ(CountErrorsContaining("only a property member can be annotated with '@WriteOnly'"), 2);
}

// A foreign block annotation is cloned into every member, so a member-only
// annotation written on the block must be refused once at the block.
TEST_F(ArkTSForeignDiagTest, WriteOnlyOnForeignBlockIsMisplaced)
{
    ResetWithArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
@WriteOnly
public foreign {
    func host(): Unit
}
)");
    EXPECT_TRUE(HasErrorContaining("'@WriteOnly' cannot be applied to a foreign block"));
    EXPECT_EQ(CountErrorsContaining("invalid @WriteOnly placement"), 1);
}

// The placement walk is idempotent: a repeated preparation pass over the same
// tree does not report the same misplaced annotation again.
TEST_F(ArkTSForeignDiagTest, WriteOnlyPlacementIsIdempotent)
{
    ResetWithArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

public class Plain {
    @WriteOnly
    public mut prop value: Int32 {
        get() { 0 }
        set(v) {}
    }
}
)");
    auto pkg = instance->GetSourcePackages().front();
    const auto errorsBefore = diag.GetErrorCount();
    Native::FFI::ArkTS::PrepareTypeCheck(*pkg, diag, instance->GetSourceManager(), instance->importManager);
    Native::FFI::ArkTS::PrepareTypeCheck(*pkg, diag, instance->GetSourceManager(), instance->importManager);
    EXPECT_EQ(diag.GetErrorCount(), errorsBefore);
}

// The annotation takes no arguments; the parser owns that rule, as it does for
// @RestParam. Placement is reported separately by the declaration pass.
TEST_F(ArkTSForeignDiagTest, ForeignClassWriteOnlyRejectsArguments)
{
    ResetWithArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign class Counter {
    public init()
    @WriteOnly[reason: "x"] public mut prop value: Int32
}
)");
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_TRUE(HasErrorContaining("accepts no arguments"));
}

// An optional parameter on an open member is refused by the shared default
// pass. Lowering must not rebuild the mapping-only default function for the
// same member, otherwise one parameter list is reported twice.
TEST_F(ArkTSForeignDiagTest, OpenForeignClassMemberOptionalParameterIsRejectedOnce)
{
    ResetWithArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign open class Person {
    public init()
    public open func greet(prefix!: Option<String> = None): String
}
)");
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_TRUE(HasErrorContaining("optional parameter cannot be used in 'open' function"));
}

// The ARKTS_WRITE_ONLY annotation has no CJO annotation record, so class preparation
// carries the contract in an attribute; an importing package must still get
// the write-only read diagnostic instead of a silent read.
TEST_F(ArkTSForeignDiagTest, ForeignClassWriteOnlySurvivesCjoRoundTrip)
{
    auto interopCjo = ExportSupportPackage(ARKTS_INTEROP_SUPPORT);
    auto contextCjo = ExportSupportPackage(ARKTS_CONTEXT_SUPPORT, {{"ohos.ark_interop", interopCjo}});
    auto langCjo = ExportSupportPackage(
        ARKTS_LANG_SUPPORT, {{"ohos.ark_interop", interopCjo}, {"cj_arkts_context_v1", contextCjo}});
    auto declarationCjo = ExportSupportPackage(R"(package decls

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign class Vault {
    public init()
    @WriteOnly
    public mut prop secret: String
}
)",
        {{"ohos.ark_interop", interopCjo}, {"cj_arkts_context_v1", contextCjo}, {"arkts.lang", langCjo}});
    ASSERT_FALSE(declarationCjo.empty());

    ResetWithArkTSSupport();
    instance->importManager->SetPackageCjoCache("decls", declarationCjo);
    CompileSema(R"(package entry

import decls.*

public func read(vault: Vault): String {
    return vault.secret
}
)");
    EXPECT_TRUE(HasErrorContaining("property 'secret' is write-only and cannot be read"));
}

// The same round trip for a subclassing consumer: the imported wrapper parent
// is recognized through its ARKTS_CLASS_WRAPPER attribute (the CJO carries it),
// so the local foreign child lowers with the child shape - delegating wrapping
// constructors and the super-thunk user constructor - and references the
// declaring package's members instead of regenerating storage.
TEST_F(ArkTSForeignDiagTest, ForeignClassSubclassesImportedWrapperParent)
{
    auto interopCjo = ExportSupportPackage(ARKTS_INTEROP_SUPPORT);
    auto contextCjo = ExportSupportPackage(ARKTS_CONTEXT_SUPPORT, {{"ohos.ark_interop", interopCjo}});
    auto langCjo = ExportSupportPackage(
        ARKTS_LANG_SUPPORT, {{"ohos.ark_interop", interopCjo}, {"cj_arkts_context_v1", contextCjo}});
    auto declarationCjo = ExportSupportPackage(R"(package decls

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign open class Vault {
    public init()
    public func ping(): Unit
}
)",
        {{"ohos.ark_interop", interopCjo}, {"cj_arkts_context_v1", contextCjo}, {"arkts.lang", langCjo}});
    ASSERT_FALSE(declarationCjo.empty());

    ResetWithArkTSSupport();
    instance->importManager->SetPackageCjoCache("decls", declarationCjo);
    CompileSema(R"(package entry

import arkts.lang.*
import decls.*

@ArkTS[module: "actual"]
public foreign class Safe <: Vault {
    public init()
    public func shout(): Unit
}

public func use(safe: Safe): Unit {
    let made = Safe()
    made.ping()
    made.shout()
    safe.ping()
}
)");
    ASSERT_EQ(diag.GetErrorCount(), 0) << (handler->messages.empty() ? "" : handler->messages.front());
    // The child owns no storage: its constructor forwards through super and
    // its methods call the declaring package's accessor.
    auto safe = FindClass("Safe");
    ASSERT_NE(safe, nullptr);
    size_t superCalls = 0;
    size_t fields = 0;
    for (auto& member : safe->GetMemberDecls()) {
        if (member->astKind == ASTKind::VAR_DECL && member->identifier.Val() == "__cj_arkts_ref") {
            ++fields;
        }
        auto fd = DynamicCast<FuncDecl*>(member.get());
        if (!fd || !fd->funcBody || !fd->funcBody->body || !fd->TestAttr(Attribute::CONSTRUCTOR) ||
            fd->TestAttr(Attribute::COMPILER_ADD)) {
            continue;
        }
        Walker(fd->funcBody->body.get(), [&superCalls](Ptr<Node> node) {
            if (auto call = DynamicCast<CallExpr*>(node); call) {
                auto ref = DynamicCast<RefExpr*>(call->baseFunc.get());
                if (ref && ref->isSuper) {
                    ++superCalls;
                }
            }
            return VisitAction::WALK_CHILDREN;
        }).Walk();
    }
    EXPECT_EQ(fields, 0);
    EXPECT_EQ(superCalls, 1);
}

// R-D3 review follow-up: a package-qualified or aliased parent spelling
// (`decls.Vault`, `d.Vault`) names the imported wrapper through its qualifier:
// preparation resolves the qualifier to the imported package (an alias
// included) and consults that package's own member, instead of matching the
// bare field name against the importing package and its imports. Both
// spellings lower with the child shape.
TEST_F(ArkTSForeignDiagTest, ForeignClassSubclassesQualifiedImportedWrapper)
{
    using namespace Cangjie::Native::FFI::ArkTS;
    auto interopCjo = ExportSupportPackage(ARKTS_INTEROP_SUPPORT);
    auto contextCjo = ExportSupportPackage(ARKTS_CONTEXT_SUPPORT, {{"ohos.ark_interop", interopCjo}});
    auto langCjo = ExportSupportPackage(
        ARKTS_LANG_SUPPORT, {{"ohos.ark_interop", interopCjo}, {"cj_arkts_context_v1", contextCjo}});
    auto declarationCjo = ExportSupportPackage(R"(package decls

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign open class Vault {
    public init()
    public func ping(): Unit
}
)",
        {{"ohos.ark_interop", interopCjo}, {"cj_arkts_context_v1", contextCjo}, {"arkts.lang", langCjo}});
    ASSERT_FALSE(declarationCjo.empty());

    ResetWithArkTSSupport();
    instance->importManager->SetPackageCjoCache("decls", declarationCjo);
    CompileSema(R"(package entry

import arkts.lang.*
import decls
import decls as d

@ArkTS[module: "actual"]
public foreign class Safe <: decls.Vault {
    public init()
    public func shout(): Unit
}

@ArkTS[module: "actual"]
public foreign class Crate <: d.Vault {
    public init()
    public func knock(): Unit
}

public func use(safe: Safe, crate: Crate): Unit {
    let made = Safe()
    made.ping()
    made.shout()
    let boxed = Crate()
    boxed.ping()
    boxed.knock()
}
)");
    ASSERT_EQ(diag.GetErrorCount(), 0) << (handler->messages.empty() ? "" : handler->messages.front());
    // Both children own no storage and reach their parent through exactly one
    // super(...) call: the qualified and the aliased spelling resolved to the
    // same imported wrapper.
    for (auto name : {"Safe", "Crate"}) {
        auto wrapper = FindClass(name);
        ASSERT_NE(wrapper, nullptr);
        size_t superCalls = 0;
        size_t fields = 0;
        for (auto& member : wrapper->GetMemberDecls()) {
            if (member->astKind == ASTKind::VAR_DECL && member->identifier.Val() == "__cj_arkts_ref") {
                ++fields;
            }
            auto fd = DynamicCast<FuncDecl*>(member.get());
            if (!fd || !fd->funcBody || !fd->funcBody->body || !fd->TestAttr(Attribute::CONSTRUCTOR) ||
                fd->TestAttr(Attribute::COMPILER_ADD)) {
                continue;
            }
            Walker(fd->funcBody->body.get(), [&superCalls](Ptr<Node> node) {
                if (auto call = DynamicCast<CallExpr*>(node); call) {
                    auto ref = DynamicCast<RefExpr*>(call->baseFunc.get());
                    if (ref && ref->isSuper) {
                        ++superCalls;
                    }
                }
                return VisitAction::WALK_CHILDREN;
            }).Walk();
        }
        EXPECT_EQ(fields, 0) << name;
        EXPECT_EQ(superCalls, 1) << name;
    }
}

// R-D3 review follow-up: a qualified parent spelling resolves through its
// qualifier, never through a same-package namesake. `plain.Plain` names the
// plain @ArkTS class exported by package `plain`, so the local foreign class
// named Plain does not capture the spelling: the child keeps the root shape
// (its own field, accessor and wrapping constructors) and compiles, instead of
// being misjudged as a wrapper child against the local namesake and lowering
// super(...) delegations into a plain class.
TEST_F(ArkTSForeignDiagTest, ForeignClassQualifiedPlainParentIgnoresLocalNamesake)
{
    using namespace Cangjie::Native::FFI::ArkTS;
    auto interopCjo = ExportSupportPackage(ARKTS_INTEROP_SUPPORT);
    auto contextCjo = ExportSupportPackage(ARKTS_CONTEXT_SUPPORT, {{"ohos.ark_interop", interopCjo}});
    auto langCjo = ExportSupportPackage(
        ARKTS_LANG_SUPPORT, {{"ohos.ark_interop", interopCjo}, {"cj_arkts_context_v1", contextCjo}});
    auto plainCjo = ExportSupportPackage(R"(package plain

import arkts.lang.*

@ArkTS
public open class Plain {
    public init() {}
    public func ping(): Unit {}
}
)",
        {{"ohos.ark_interop", interopCjo}, {"cj_arkts_context_v1", contextCjo}, {"arkts.lang", langCjo}});
    ASSERT_FALSE(plainCjo.empty());

    ResetWithArkTSSupport();
    instance->importManager->SetPackageCjoCache("plain", plainCjo);
    CompileSema(R"(package entry

import arkts.lang.*
import plain

@ArkTS[module: "actual"]
public foreign class Plain {
    public init()
}

@ArkTS[module: "actual"]
public foreign class Child <: plain.Plain {
    public init()
}

public func use(child: Child): Unit {
    let made = Child()
    made.ping()
}
)");
    ASSERT_EQ(diag.GetErrorCount(), 0) << (handler->messages.empty() ? "" : handler->messages.front());
    auto child = FindClass("Child");
    ASSERT_NE(child, nullptr);
    // The qualified parent picked the exported plain class, so the child is a
    // root wrapper: it owns the storage and no super delegation is generated
    // (the wrapping constructors' implicit super() to the plain parent is the
    // base compiler's desugar, not a wrapper-parent shape).
    size_t superCalls = 0;
    size_t fields = 0;
    for (auto& member : child->GetMemberDecls()) {
        if (member->astKind == ASTKind::VAR_DECL && member->identifier.Val() == "__cj_arkts_ref") {
            ++fields;
        }
        auto fd = DynamicCast<FuncDecl*>(member.get());
        if (!fd || !fd->funcBody || !fd->funcBody->body || !fd->TestAttr(Attribute::CONSTRUCTOR) ||
            fd->TestAttr(Attribute::COMPILER_ADD)) {
            continue;
        }
        Walker(fd->funcBody->body.get(), [&superCalls](Ptr<Node> node) {
            if (auto call = DynamicCast<CallExpr*>(node); call) {
                auto ref = DynamicCast<RefExpr*>(call->baseFunc.get());
                if (ref && ref->isSuper) {
                    ++superCalls;
                }
            }
            return VisitAction::WALK_CHILDREN;
        }).Walk();
    }
    EXPECT_EQ(fields, 1);
    EXPECT_EQ(superCalls, 0);
}

namespace {
// Minimal ohos.ark_interop surface used by the generated wrappers: module
// loading, call invocation, entry registration and JSValue/JSBigInt
// conversions.
const char* ARKTS_INTEROP_SUPPORT = R"(package ohos.ark_interop

import std.math.numeric.BigInt

public open class JSValue {
    public open func toJSValue(): JSValue { return this }
    public func isNull(): Bool { return false }
    public func isUndefined(): Bool { return false }
    public func isNumber(): Bool { return false }
    public func isString(): Bool { return false }
    public func isBoolean(): Bool { return false }
    public func isArray(): Bool { return false }
    public func isBigInt(): Bool { return false }
    public func isObject(): Bool { return false }
    public func isFunction(): Bool { return false }
    public func toNumber(): Float64 { return 0.0 }
    public func toBoolean(): Bool { return false }
    public func asString(): JSString { return JSString() }
    public func asBigInt(): JSBigInt { return JSBigInt() }
    public func asArray(): JSArray { return JSArray() }
    public func asObject(): JSObject { return JSObject() }
    public func asClass(): JSClass { return JSClass() }
    public func asFunction(): JSFunction { return JSFunction() }
    public func getProperty(key: String): JSValue { return JSValue() }
    public func setProperty(key: String, value: JSValue): Unit {}
    public func getElement(index: Int64): JSValue { return JSValue() }
}

public class JSString {
    public func toString(): String { return "" }
}

public class JSBigInt {
    public func toBigInt(): BigInt { return BigInt(0) }
    public func toInt64(): Int64 { return 0 }
    public func toUInt64(): UInt64 { return 0 }
}

public class JSArray {
    public prop size: Int64 {
        get() { return 0 }
    }
    public func getElement(index: Int64): JSValue { return JSValue() }
    public func toJSValue(): JSValue { return JSValue() }
}

// JSFunction is a JSValue subtype in the shipped interop API, so the
// construction glue's `ctx.function(lambda)` argument matches the carrier's
// JSValue wrapping constructor.
public class JSFunction <: JSValue {
    public func call(args: Array<JSValue>, thisArg!: JSValue): JSValue { return JSValue() }
    public override func toJSValue(): JSValue { return this }
}

public class JSObject {
    public func asObject(): JSObject { return this }
    public func getProperty(key: String): JSValue { return JSValue() }
    public func setProperty(key: String, value: JSValue): Unit {}
    public func callMethod(key: String, args: Array<JSValue>): JSValue { return JSValue() }
    public func toJSValue(): JSValue { return JSValue() }
}

public class JSClass {
    public func new(): JSValue { return JSValue() }
    public func new(arg: JSValue): JSValue { return JSValue() }
    public func new(args: Array<JSValue>): JSValue { return JSValue() }
    public func toJSValue(): JSValue { return JSValue() }
}

public class JSCallInfo {
    public operator func [](index: Int64): JSValue { return JSValue() }
}

public type JSLambda = (JSContext, JSCallInfo) -> JSValue

public class JSModule {
    public func asObject(): JSObject { return JSObject() }
    public static func registerFunc(name: String, lambda: JSLambda): Unit {}
}

public class JSContext {
    public prop env: Int64 {
        get() { return 0 }
    }
    public prop global: JSObject {
        get() { return JSObject() }
    }
    public func requireArkModule(name: String): JSModule { return JSModule() }
    public func array(arr: Array<JSValue>): JSArray { return JSArray() }
    public func function(lambda: JSLambda): JSFunction { return JSFunction() }
    public func number(value: Float64): JSValue { return JSValue() }
    public func string(value: String): JSValue { return JSValue() }
    public func boolean(value: Bool): JSValue { return JSValue() }
    public func bigint(value: BigInt): JSValue { return JSValue() }
    public func undefined(): JSValue { return JSValue() }
}

// js_interop_type.cj:39 `public interface JSInteropType<T>` and the element
// conformances the prepared ArkTSArray<T> bodies spell, reduced to the
// positions this file's tests use. The stub bodies return dummies.
public interface JSInteropType<T> {
    static func fromJSValue(context: JSContext, input: JSValue): T
    func toJSValue(context: JSContext): JSValue
    static func toArktsType(): String
}

extend Int32 <: JSInteropType<Int32> {
    public static func fromJSValue(_: JSContext, _: JSValue): Int32 { return 0 }
    public func toJSValue(_: JSContext): JSValue { return JSValue() }
    public static func toArktsType(): String { return "number" }
}

extend Float64 <: JSInteropType<Float64> {
    public static func fromJSValue(_: JSContext, _: JSValue): Float64 { return 0.0 }
    public func toJSValue(_: JSContext): JSValue { return JSValue() }
    public static func toArktsType(): String { return "number" }
}

extend String <: JSInteropType<String> {
    public static func fromJSValue(_: JSContext, _: JSValue): String { return "" }
    public func toJSValue(_: JSContext): JSValue { return JSValue() }
    public static func toArktsType(): String { return "string" }
}

extend<T> Option<T> <: JSInteropType<Option<T>> where T <: JSInteropType<T> {
    public static func fromJSValue(_: JSContext, _: JSValue): Option<T> { return Option<T>.None }
    public func toJSValue(_: JSContext): JSValue { return JSValue() }
    public static func toArktsType(): String { return "union" }
}
)";

// Context ABI v1 entries the wrapper calls.
const char* ARKTS_CONTEXT_SUPPORT = R"(package cj_arkts_context_v1

import ohos.ark_interop.JSContext

public class ForeignRuntimeException <: Exception {
    public init(message: String) {
        super(message)
    }
}

public func __cj_arkts_current(): JSContext { return JSContext() }
public func __cj_arkts_enter(ctx: JSContext): ?JSContext { return None }
public func __cj_arkts_restore(previous: ?JSContext): Unit {}
)";

// The stdlib package that declares the ArkTSType root interface. Only the
// marker-bearing interface is needed for the implicit-supertype tests; the
// package name is what the compiler pins.
const char* ARKTS_STDLIB_SUPPORT = R"(package std.interop.arkts

import ohos.ark_interop.JSValue

@ArkTS
public interface ArkTSType {}

// The preset function carrier, declared exactly like the shipped stdlib:
// the preparation pass installs the JSValue carrier storage and removes the
// bodyless init(f: F), and the dedicated carrier rules key on this package.
@ArkTS
public foreign class ArkTSFunc<F> {
    public init(f: F)
}

@ArkTS
public foreign class ArkTSFuncWithThis<ThisT, F> {
    public func bind(thisArg: ThisT): ArkTSFunc<F>
}
)";

// The arkts.lang facade the generated support names are pinned to: it
// re-exports the interop API, the context ABI entries and BigInt and declares
// the conversion exception, mirroring the arkts.lang facade in the ArkTS interop SDK repository.
const char* ARKTS_LANG_SUPPORT = R"(package arkts.lang

public import ohos.ark_interop.*
public import cj_arkts_context_v1.*
public import std.math.numeric.BigInt

public class ExternConversionException <: Exception {
    public init(message: String) {
        super(message)
    }
}

public class BusinessException <: Exception {
    public let code: Int32
    public init(code: Int32, message: String) {
        super(message)
        this.code = code
    }
}

// Carrier types used by the '@HasThis' contract tests. The shipped stub is
// deliberately small; the real declarations live in std.interop.arkts.
public class ArkTSFunc<F> {
    public init(f: F) {}
}

public class ArkTSFuncWithThis<ThisT, F> {
    public func bind(thisArg: ThisT): ArkTSFunc<F> {
        throw Exception("arkts carrier stub")
    }
}

)";

// The interop library package that owns the exception carrier. The facade
// re-exports it under the original package name, which the pinning rule
// accepts alongside a direct facade declaration.
const char* ARKTS_BUSINESS_EXCEPTION_SUPPORT = R"(package ohos.business_exception

public class BusinessException <: Exception {
    public let code: Int32
    public init(code: Int32, message: String) {
        super(message)
        this.code = code
    }
}
)";

// Remove one top-level class declaration from a support package, leaving the
// rest of the facade intact. Used to build the missing-support variants.
std::string WithoutSupportClass(const std::string& support, const std::string& className)
{
    const auto start = support.find("public class " + className + " ");
    if (start == std::string::npos) {
        return support;
    }
    const auto end = support.find("\n}\n", start);
    if (end == std::string::npos) {
        return support;
    }
    return support.substr(0, start) + support.substr(end + std::string("\n}\n").size());
}

// Remove the facade's BusinessException declaration and re-export the one
// from its owning package instead.
std::string ReexportBusinessException(const std::string& support)
{
    auto reduced = WithoutSupportClass(support, "BusinessException");
    const std::string anchor = "public import std.math.numeric.BigInt\n";
    auto at = reduced.find(anchor);
    if (at == std::string::npos) {
        return reduced;
    }
    at += anchor.size();
    return reduced.substr(0, at) + "public import ohos.business_exception.BusinessException\n" + reduced.substr(at);
}

} // namespace

// A user function that shares the name of a generated SDK support reference
// must not capture it. The wrapper body is pre-bound to the imported
// std.math.numeric.BigInt, but speculative overload checking clears and
// re-resolves the enclosing subtree; the binding must survive that clear.
// The support names reach this file through arkts.lang, the package an
// application already imports; the same-named local class/function stay
// uncaptured.
TEST_F(ArkTSForeignDiagTest, UserBigIntDoesNotCaptureGeneratedWrapperReference)
{
    diag.ClearError();
    auto interopCjo = ExportSupportPackage(ARKTS_INTEROP_SUPPORT);
    auto contextCjo = ExportSupportPackage(ARKTS_CONTEXT_SUPPORT, {{"ohos.ark_interop", interopCjo}});
    auto langCjo = ExportSupportPackage(
        ARKTS_LANG_SUPPORT, {{"ohos.ark_interop", interopCjo}, {"cj_arkts_context_v1", contextCjo}});

    diag.ClearError();
    handler->messages.clear();
    handler->errors.clear();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->importManager->SetPackageCjoCache("ohos.ark_interop", interopCjo);
    instance->importManager->SetPackageCjoCache("cj_arkts_context_v1", contextCjo);
    instance->importManager->SetPackageCjoCache("arkts.lang", langCjo);
    CompileSema(R"(package entry

import arkts.lang.*

class ExternConversionException <: Exception {
    public init(message: String) { super(message) }
}

func BigInt(value: UInt64): Int32 {
    return Int32(value)
}

@ArkTS[module: "actual", foreignName: "echo"]
public foreign func echo(value: UInt64): UInt64

public func forward(value: UInt64): UInt64 {
    return echo(value)
}
)");
    ASSERT_EQ(diag.GetErrorCount(), 0) << (handler->messages.empty() ? "" : handler->messages.front());
    auto fd = FindFunction("echo");
    ASSERT_NE(fd, nullptr);
    ASSERT_NE(fd->funcBody->body, nullptr);
    // Every generated BigInt conversion keeps the SDK declaration, not the
    // package's own func BigInt, and every conversion failure keeps the
    // arkts.lang exception, not the package's own class of that name.
    size_t generated = 0;
    size_t exceptions = 0;
    Walker(fd->funcBody->body.get(), [&generated, &exceptions](Ptr<Node> node) {
        auto re = DynamicCast<RefExpr*>(node);
        if (!re) {
            return VisitAction::WALK_CHILDREN;
        }
        if (re->ref.identifier.Val() == "BigInt") {
            ++generated;
            EXPECT_NE(re->ref.target, nullptr);
            if (re->ref.target) {
                EXPECT_EQ(re->ref.target->fullPackageName, "std.math.numeric");
            }
        } else if (re->ref.identifier.Val() == "ExternConversionException") {
            ++exceptions;
            EXPECT_NE(re->ref.target, nullptr);
            if (re->ref.target) {
                EXPECT_EQ(re->ref.target->fullPackageName, "arkts.lang");
            }
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    EXPECT_GT(generated, 0);
    EXPECT_GT(exceptions, 0);
}

// The same-name capture rule covers every support name a wrapper writes: the
// conversion exception (pinned to arkts.lang) and Option (pinned to std.core,
// with Some/None reached through the member access). Interference declarations
// of all three names exist in the package, so a name-based lookup would bind
// the generated code to them.
TEST_F(ArkTSForeignDiagTest, SameNamedDeclarationsDoNotCaptureGeneratedSupportReferences)
{
    diag.ClearError();
    auto interopCjo = ExportSupportPackage(ARKTS_INTEROP_SUPPORT);
    auto contextCjo = ExportSupportPackage(ARKTS_CONTEXT_SUPPORT, {{"ohos.ark_interop", interopCjo}});
    auto langCjo = ExportSupportPackage(
        ARKTS_LANG_SUPPORT, {{"ohos.ark_interop", interopCjo}, {"cj_arkts_context_v1", contextCjo}});

    diag.ClearError();
    handler->messages.clear();
    handler->errors.clear();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->importManager->SetPackageCjoCache("ohos.ark_interop", interopCjo);
    instance->importManager->SetPackageCjoCache("cj_arkts_context_v1", contextCjo);
    instance->importManager->SetPackageCjoCache("arkts.lang", langCjo);
    CompileSema(R"(package entry

import arkts.lang.*

class ExternConversionException <: Exception {
    public init(message: String) { super(message) }
}

func Some(value: Int32): Int32 {
    return value
}

func None(): Int32 {
    return 0
}

@ArkTS[module: "actual", foreignName: "echo"]
public foreign func echo(value: Int32): Int32

@ArkTS[module: "actual", foreignName: "optional"]
public foreign func optional(value: Option<Int32>): Option<Int32>

@ArkTS[module: "actual", foreignName: "tuple"]
public foreign func tuple(value: (Int32, Option<Int32>)): (Int32, Option<Int32>)
)");
    ASSERT_EQ(diag.GetErrorCount(), 0) << (handler->messages.empty() ? "" : handler->messages.front());
    // The Int32 wrapper has conversion guards (hence the exception) but no
    // Option position; the Option and tuple wrappers have both, and the tuple
    // one also spells Option<Int32> as a generated local's type annotation.
    struct Expectation {
        const char* name;
        bool option;
    };
    for (const auto& expectation :
        {Expectation{"echo", false}, Expectation{"optional", true}, Expectation{"tuple", true}}) {
        auto fd = FindFunction(expectation.name);
        ASSERT_NE(fd, nullptr) << expectation.name;
        ASSERT_NE(fd->funcBody->body, nullptr) << expectation.name;
        size_t exceptions = 0;
        size_t options = 0;
        Walker(fd->funcBody->body.get(), [&](Ptr<Node> node) {
            auto re = DynamicCast<RefExpr*>(node);
            if (!re) {
                return VisitAction::WALK_CHILDREN;
            }
            const auto& name = re->ref.identifier.Val();
            // Some/None are enum constructors, never package members: the
            // generated text spells them as Option.Some/Option.None, so they
            // must not appear as bare references at all.
            EXPECT_NE(name, "Some") << expectation.name;
            EXPECT_NE(name, "None") << expectation.name;
            if (name == "ExternConversionException") {
                ++exceptions;
                EXPECT_NE(re->ref.target, nullptr) << expectation.name;
                if (re->ref.target) {
                    EXPECT_EQ(re->ref.target->fullPackageName, "arkts.lang") << expectation.name;
                }
            } else if (name == "Option") {
                ++options;
                EXPECT_NE(re->ref.target, nullptr) << expectation.name;
                if (re->ref.target) {
                    EXPECT_EQ(re->ref.target->fullPackageName, "std.core") << expectation.name;
                }
            }
            return VisitAction::WALK_CHILDREN;
        }).Walk();
        EXPECT_GT(exceptions, 0) << expectation.name;
        if (expectation.option) {
            EXPECT_GT(options, 0) << expectation.name;
            // The bare member names are members, not references, so the
            // generated Option conversions resolve through Option's declaration.
            size_t someMembers = 0;
            size_t noneMembers = 0;
            Walker(fd->funcBody->body.get(), [&](Ptr<Node> node) {
                auto access = DynamicCast<MemberAccess*>(node);
                if (access && access->field == "Some") {
                    ++someMembers;
                } else if (access && access->field == "None") {
                    ++noneMembers;
                }
                return VisitAction::WALK_CHILDREN;
            }).Walk();
            EXPECT_GT(someMembers, 0) << expectation.name;
            EXPECT_GT(noneMembers, 0) << expectation.name;
        } else {
            EXPECT_EQ(options, 0) << expectation.name;
        }
    }
    // The tuple wrapper spells Option<Int32> in a generated local's type
    // annotation. That reference must still carry the instantiated type:
    // pinning only the declaration's generic type would leave the annotation
    // uninferable.
    auto tuple = FindFunction("tuple");
    ASSERT_NE(tuple, nullptr);
    ASSERT_NE(tuple->funcBody->body, nullptr);
    size_t partLets = 0;
    Walker(tuple->funcBody->body.get(), [&partLets](Ptr<Node> node) {
        auto decl = DynamicCast<VarDecl*>(node);
        if (!decl || decl->identifier.Val().rfind("__cj_arkts_part_", 0) != 0 || !decl->type) {
            return VisitAction::WALK_CHILDREN;
        }
        ++partLets;
        EXPECT_TRUE(Ty::IsTyCorrect(decl->GetTy()));
        if (Ty::IsTyCorrect(decl->GetTy())) {
            EXPECT_EQ(decl->GetTy()->String(), "Enum-Option<Int32>");
        }
        EXPECT_TRUE(Ty::IsTyCorrect(decl->type->GetTy()));
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    EXPECT_GT(partLets, 0);
}

// The generated guard throws the public runtime exception, so the facade must
// declare it. A facade without that declaration is a missing support
// declaration: the standard support diagnostic is reported and the generated
// body is dropped, never replaced by a bare name or another exception type.
TEST_F(ArkTSForeignDiagTest, MissingForeignRuntimeExceptionIsSupportUnavailable)
{
    diag.ClearError();
    auto interopCjo = ExportSupportPackage(ARKTS_INTEROP_SUPPORT);
    auto contextCjo = ExportSupportPackage(
        WithoutSupportClass(ARKTS_CONTEXT_SUPPORT, "ForeignRuntimeException"), {{"ohos.ark_interop", interopCjo}});
    auto langCjo = ExportSupportPackage(
        ARKTS_LANG_SUPPORT, {{"ohos.ark_interop", interopCjo}, {"cj_arkts_context_v1", contextCjo}});

    diag.ClearError();
    handler->messages.clear();
    handler->errors.clear();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->importManager->SetPackageCjoCache("ohos.ark_interop", interopCjo);
    instance->importManager->SetPackageCjoCache("cj_arkts_context_v1", contextCjo);
    instance->importManager->SetPackageCjoCache("arkts.lang", langCjo);
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
foreign func echo(value: Int32): Int32
)");
    EXPECT_GE(diag.GetErrorCount(), 1);
    EXPECT_TRUE(HasErrorContaining("requires ArkTS interop declarations that are not available"));
    EXPECT_TRUE(HasErrorContaining("'echo'"));
    auto fd = FindFunction("echo");
    ASSERT_NE(fd, nullptr);
    ASSERT_NE(fd->funcBody, nullptr);
    EXPECT_EQ(fd->funcBody->body, nullptr);
    EXPECT_TRUE(fd->TestAttr(Attribute::IS_BROKEN));
}

// The call translation catches the library's error carrier. A facade that
// neither declares nor re-exports it is the same missing support declaration
// and refuses the wrapper before any body is generated.
TEST_F(ArkTSForeignDiagTest, MissingBusinessExceptionIsSupportUnavailable)
{
    diag.ClearError();
    auto interopCjo = ExportSupportPackage(ARKTS_INTEROP_SUPPORT);
    auto contextCjo = ExportSupportPackage(ARKTS_CONTEXT_SUPPORT, {{"ohos.ark_interop", interopCjo}});
    auto langCjo = ExportSupportPackage(WithoutSupportClass(ARKTS_LANG_SUPPORT, "BusinessException"),
        {{"ohos.ark_interop", interopCjo}, {"cj_arkts_context_v1", contextCjo}});

    diag.ClearError();
    handler->messages.clear();
    handler->errors.clear();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->importManager->SetPackageCjoCache("ohos.ark_interop", interopCjo);
    instance->importManager->SetPackageCjoCache("cj_arkts_context_v1", contextCjo);
    instance->importManager->SetPackageCjoCache("arkts.lang", langCjo);
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
foreign func echo(value: Int32): Int32
)");
    EXPECT_GE(diag.GetErrorCount(), 1);
    EXPECT_TRUE(HasErrorContaining("requires ArkTS interop declarations that are not available"));
    EXPECT_TRUE(HasErrorContaining("'echo'"));
    auto fd = FindFunction("echo");
    ASSERT_NE(fd, nullptr);
    ASSERT_NE(fd->funcBody, nullptr);
    EXPECT_EQ(fd->funcBody->body, nullptr);
    EXPECT_TRUE(fd->TestAttr(Attribute::IS_BROKEN));
}

// The library carrier keeps its owning package when the facade re-exports it.
// The generated catch must bind that re-exported declaration, so the wrapper
// compiles and the reference resolves to the owning package, not a same-named
// declaration of the compiling package.
TEST_F(ArkTSForeignDiagTest, ReexportedBusinessExceptionFromItsOwningPackageIsAccepted)
{
    diag.ClearError();
    auto interopCjo = ExportSupportPackage(ARKTS_INTEROP_SUPPORT);
    auto contextCjo = ExportSupportPackage(ARKTS_CONTEXT_SUPPORT, {{"ohos.ark_interop", interopCjo}});
    auto businessCjo = ExportSupportPackage(ARKTS_BUSINESS_EXCEPTION_SUPPORT);
    auto langCjo = ExportSupportPackage(ReexportBusinessException(ARKTS_LANG_SUPPORT),
        {{"ohos.ark_interop", interopCjo}, {"cj_arkts_context_v1", contextCjo},
            {"ohos.business_exception", businessCjo}});

    diag.ClearError();
    handler->messages.clear();
    handler->errors.clear();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->importManager->SetPackageCjoCache("ohos.ark_interop", interopCjo);
    instance->importManager->SetPackageCjoCache("cj_arkts_context_v1", contextCjo);
    instance->importManager->SetPackageCjoCache("arkts.lang", langCjo);
    instance->importManager->SetPackageCjoCache("ohos.business_exception", businessCjo);
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
foreign func echo(value: Int32): Int32
)");
    ASSERT_EQ(diag.GetErrorCount(), 0) << (handler->messages.empty() ? "" : handler->messages.front());
    auto fd = FindFunction("echo");
    ASSERT_NE(fd, nullptr);
    ASSERT_NE(fd->funcBody, nullptr);
    ASSERT_NE(fd->funcBody->body, nullptr);
    size_t businessRefs = 0;
    Walker(fd->funcBody->body.get(), [&businessRefs](Ptr<Node> node) {
        auto type = DynamicCast<RefType*>(node);
        auto expr = DynamicCast<RefExpr*>(node);
        if (!type && !expr) {
            return VisitAction::WALK_CHILDREN;
        }
        const auto& ref = type ? type->ref : expr->ref;
        if (ref.identifier.Val() == "BusinessException") {
            ++businessRefs;
            EXPECT_NE(ref.target, nullptr);
            if (ref.target) {
                EXPECT_EQ(ref.target->fullPackageName, "ohos.business_exception");
            }
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    // The export lookup and the call are both translated, so the facade type is
    // referenced twice; both references resolve to the same declaration.
    EXPECT_EQ(businessRefs, 2);
}

// A source declaration that reuses a generated runtime support name never
// captures it: both the thrown runtime exception and the caught library
// carrier stay bound to the declarations visible through the facade.
TEST_F(ArkTSForeignDiagTest, SameNamedUserExceptionsDoNotCaptureGeneratedRuntimeSupport)
{
    diag.ClearError();
    auto interopCjo = ExportSupportPackage(ARKTS_INTEROP_SUPPORT);
    auto contextCjo = ExportSupportPackage(ARKTS_CONTEXT_SUPPORT, {{"ohos.ark_interop", interopCjo}});
    auto langCjo = ExportSupportPackage(
        ARKTS_LANG_SUPPORT, {{"ohos.ark_interop", interopCjo}, {"cj_arkts_context_v1", contextCjo}});

    diag.ClearError();
    handler->messages.clear();
    handler->errors.clear();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->importManager->SetPackageCjoCache("ohos.ark_interop", interopCjo);
    instance->importManager->SetPackageCjoCache("cj_arkts_context_v1", contextCjo);
    instance->importManager->SetPackageCjoCache("arkts.lang", langCjo);
    CompileSema(R"(package entry

import arkts.lang.*

class ForeignRuntimeException <: Exception {
    public init(message: String) { super(message) }
}

class BusinessException <: Exception {
    public let code: Int32
    public init(code: Int32, message: String) {
        super(message)
        this.code = code
    }
}

@ArkTS[module: "actual", foreignName: "echo"]
public foreign func echo(value: Int32): Int32
)");
    ASSERT_EQ(diag.GetErrorCount(), 0) << (handler->messages.empty() ? "" : handler->messages.front());
    auto userRuntime = FindClass("ForeignRuntimeException");
    auto userBusiness = FindClass("BusinessException");
    auto fd = FindFunction("echo");
    ASSERT_NE(fd, nullptr);
    ASSERT_NE(fd->funcBody, nullptr);
    ASSERT_NE(fd->funcBody->body, nullptr);
    size_t runtimeRefs = 0;
    size_t businessRefs = 0;
    Walker(fd->funcBody->body.get(), [&](Ptr<Node> node) {
        auto type = DynamicCast<RefType*>(node);
        auto expr = DynamicCast<RefExpr*>(node);
        if (!type && !expr) {
            return VisitAction::WALK_CHILDREN;
        }
        const auto& ref = type ? type->ref : expr->ref;
        const auto name = ref.identifier.Val();
        if (name == "ForeignRuntimeException") {
            ++runtimeRefs;
            EXPECT_NE(ref.target, nullptr);
            EXPECT_NE(ref.target, Ptr<Decl>(userRuntime));
            if (ref.target) {
                EXPECT_EQ(ref.target->fullPackageName, "cj_arkts_context_v1");
            }
        } else if (name == "BusinessException") {
            ++businessRefs;
            EXPECT_NE(ref.target, nullptr);
            EXPECT_NE(ref.target, Ptr<Decl>(userBusiness));
            if (ref.target) {
                EXPECT_EQ(ref.target->fullPackageName, "arkts.lang");
            }
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    EXPECT_GT(runtimeRefs, 0);
    EXPECT_EQ(businessRefs, 2);
}

// An entry whose name is also a support name keeps its own declaration as the
// callback target: ASTFactory::CreateEntry pre-binds the call, and the support
// binding walk must not replace it with the SDK declaration imported through
// arkts.lang. The callback's call would otherwise resolve BigInt(...) against
// std.math.numeric and fail (or silently call the wrong function).
TEST_F(ArkTSForeignDiagTest, EntryNamedLikeSupportItemKeepsItsCallbackTarget)
{
    diag.ClearError();
    auto interopCjo = ExportSupportPackage(ARKTS_INTEROP_SUPPORT);
    auto contextCjo = ExportSupportPackage(ARKTS_CONTEXT_SUPPORT, {{"ohos.ark_interop", interopCjo}});
    auto langCjo = ExportSupportPackage(
        ARKTS_LANG_SUPPORT, {{"ohos.ark_interop", interopCjo}, {"cj_arkts_context_v1", contextCjo}});

    diag.ClearError();
    handler->messages.clear();
    handler->errors.clear();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->importManager->SetPackageCjoCache("ohos.ark_interop", interopCjo);
    instance->importManager->SetPackageCjoCache("cj_arkts_context_v1", contextCjo);
    instance->importManager->SetPackageCjoCache("arkts.lang", langCjo);
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS
public func BigInt(): Unit {}
)");
    ASSERT_EQ(diag.GetErrorCount(), 0) << (handler->messages.empty() ? "" : handler->messages.front());
    auto entry = FindFunction("BigInt");
    ASSERT_NE(entry, nullptr);
    auto callback = FindFunction("__cj_arkts_entry_0");
    ASSERT_NE(callback, nullptr);
    ASSERT_NE(callback->funcBody->body, nullptr);
    size_t calls = 0;
    Walker(callback->funcBody->body.get(), [&](Ptr<Node> node) {
        auto re = DynamicCast<RefExpr*>(node);
        if (re && re->ref.identifier.Val() == "BigInt") {
            ++calls;
            EXPECT_EQ(re->ref.target, entry);
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    EXPECT_EQ(calls, 1);
}

// Windows path spellings are refused at compile time too: the module option
// must name an ArkTS module, and the ArkTS project may be built on Windows.
// Both separators are treated alike, so drive-absolute, UNC and backslash
// relative forms are rejected by the same diagnostic.
TEST_F(ArkTSForeignDiagTest, WindowsPathModulesAreDiagnosed)
{
    CompileSema(R"(package entry

@ArkTS[module: "C:\\x"]
foreign func winDriveBackslash(x: Int32): Unit

@ArkTS[module: "C:/x"]
foreign func winDriveSlash(x: Int32): Unit

@ArkTS[module: "\\\\srv\\share"]
foreign func winUnc(x: Int32): Unit

@ArkTS[module: "..\\x"]
foreign func winParent(x: Int32): Unit

@ArkTS[module: ".\\x"]
foreign func winCurrent(x: Int32): Unit

@ArkTS[module: "\\x"]
foreign func winRoot(x: Int32): Unit
)");
    ASSERT_EQ(diag.GetErrorCount(), 6);
    for (const auto& name : {"winDriveBackslash", "winDriveSlash", "winUnc", "winParent", "winCurrent", "winRoot"}) {
        EXPECT_TRUE(HasErrorContaining(std::string("'") + name + "'")) << name;
        auto fd = FindFunction(name);
        ASSERT_NE(fd, nullptr) << name;
        EXPECT_EQ(fd->funcBody->body, nullptr) << name;
        EXPECT_TRUE(fd->TestAttr(Attribute::IS_BROKEN)) << name;
    }
    EXPECT_TRUE(HasErrorContaining("must be an ArkTS module specifier"));
}

// A drive-relative spelling ("C:x", no separator after the colon) is not a
// path prefix and stays a module name; its resolution failure belongs to the
// runtime, like any unknown module. Pinning it here keeps the character rule
// honest: only separator-carrying shapes are rejected.
TEST_F(ArkTSForeignDiagTest, DriveRelativeModuleIsNotAPath)
{
    diag.ClearError();
    auto interopCjo = ExportSupportPackage(ARKTS_INTEROP_SUPPORT);
    auto contextCjo = ExportSupportPackage(ARKTS_CONTEXT_SUPPORT, {{"ohos.ark_interop", interopCjo}});
    auto langCjo = ExportSupportPackage(
        ARKTS_LANG_SUPPORT, {{"ohos.ark_interop", interopCjo}, {"cj_arkts_context_v1", contextCjo}});

    diag.ClearError();
    handler->messages.clear();
    handler->errors.clear();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->importManager->SetPackageCjoCache("ohos.ark_interop", interopCjo);
    instance->importManager->SetPackageCjoCache("cj_arkts_context_v1", contextCjo);
    instance->importManager->SetPackageCjoCache("arkts.lang", langCjo);
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "C:x"]
foreign func driveRelative(x: Int32): Int32
)");
    ASSERT_EQ(diag.GetErrorCount(), 0) << (handler->messages.empty() ? "" : handler->messages.front());
    EXPECT_FALSE(HasErrorContaining("'module' option"));
    auto fd = FindFunction("driveRelative");
    ASSERT_NE(fd, nullptr);
    EXPECT_NE(fd->funcBody->body, nullptr);
}

// A module specifier may be a project ETS source path or a forward-slash
// shape the runtime loader resolves. The design §2.1.1 legal range rejects a
// leading "./"/"../", an OS-absolute path, a drive-absolute path and any
// backslash, so these four spellings must pass both the format check and
// wrapper generation.
TEST_F(ArkTSForeignDiagTest, ProjectPathAndToleratedModuleFormatsPass)
{
    diag.ClearError();
    auto interopCjo = ExportSupportPackage(ARKTS_INTEROP_SUPPORT);
    auto contextCjo = ExportSupportPackage(ARKTS_CONTEXT_SUPPORT, {{"ohos.ark_interop", interopCjo}});
    auto langCjo = ExportSupportPackage(
        ARKTS_LANG_SUPPORT, {{"ohos.ark_interop", interopCjo}, {"cj_arkts_context_v1", contextCjo}});

    diag.ClearError();
    handler->messages.clear();
    handler->errors.clear();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->importManager->SetPackageCjoCache("ohos.ark_interop", interopCjo);
    instance->importManager->SetPackageCjoCache("cj_arkts_context_v1", contextCjo);
    instance->importManager->SetPackageCjoCache("arkts.lang", langCjo);
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "entry/src/main/ets/EntryFixture"]
foreign func etsPath(x: Int32): Int32

@ArkTS[module: "a/"]
foreign func trailingSeparator(x: Int32): Int32

@ArkTS[module: "a//b"]
foreign func doubleSeparator(x: Int32): Int32

@ArkTS[module: "@a/b"]
foreign func scopedModule(x: Int32): Int32
)");
    ASSERT_EQ(diag.GetErrorCount(), 0) << (handler->messages.empty() ? "" : handler->messages.front());
    EXPECT_FALSE(HasErrorContaining("'module' option"));
    for (const auto& name : {"etsPath", "trailingSeparator", "doubleSeparator", "scopedModule"}) {
        auto fd = FindFunction(name);
        ASSERT_NE(fd, nullptr) << name;
        EXPECT_NE(fd->funcBody->body, nullptr) << name;
    }
}

// The design §2.1.1 legal range forbids a backslash anywhere in the module
// value, including a Windows-style separator spelling that earlier accepted.
TEST_F(ArkTSForeignDiagTest, BackslashModuleIsRejected)
{
    CompileSema(R"(package entry

@ArkTS[module: "a\\b"]
foreign func backslashModule(x: Int32): Int32
)");
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_TRUE(HasErrorContaining("'module' option"));
    EXPECT_TRUE(HasErrorContaining("must be an ArkTS module specifier"));
}

// A missing interop dependency for the entry callback/registration pair yields
// exactly one user-facing diagnostic, anchored on the user entry and naming
// the entry: the callback and the registration carry the same source range, so
// the pair is deduplicated. The wording must not leak internal symbols or
// implementation terms.
TEST_F(ArkTSForeignDiagTest, GeneratedEntrySupportUnavailableIsOneUserFacingDiagnostic)
{
    CompileSema(R"(package entry

@ArkTS
public func run(): Unit {}
)");
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_TRUE(HasErrorContaining("requires ArkTS interop declarations that are not available"));
    EXPECT_TRUE(HasErrorContaining("check the project's ArkTS interop dependencies"));
    EXPECT_TRUE(HasErrorContaining("'run'"));
    for (const auto& forbidden : {"__cj_arkts_", "wrapper", "generated", "glue", "support declaration"}) {
        EXPECT_FALSE(HasErrorContaining(forbidden)) << forbidden;
    }
    auto supportError = handler->FindErrorContaining("requires ArkTS interop declarations");
    ASSERT_NE(supportError, nullptr);
    const std::string supportNote =
        "the entry registration requires 'ohos.ark_interop' and 'cj_arkts_context_v1' imports";
    EXPECT_NE(std::find(supportError->notes.begin(), supportError->notes.end(), supportNote),
        supportError->notes.end());
}

// The C ABI exemption is reserved for actual @ArkTS foreign declarations: the
// function must carry FOREIGN and either its own @ArkTS annotation or an
// enclosing @ArkTS foreign class/interface. An @ArkTS class that is not
// FOREIGN must not hide its foreign member from the C ABI checks, so the
// non-C parameter type is rejected.
TEST_F(ArkTSForeignDiagTest, ForeignMemberOfPlainArkTSClassIsNotCABIExempt)
{
    CompileSema(R"(package entry

@ArkTS[module: "actual"]
public class Host {
    public foreign func bad(value: String): Unit
}
)");
    EXPECT_TRUE(HasErrorContaining("arguments type of CFunc must be instantiated with CType"));
}

// A named optional parameter wrapper selects the forwarded arguments at run
// time through Option.isSome(); the generated body must survive full Sema,
// including the member call on the rebound parameter reference.
TEST_F(ArkTSForeignDiagTest, NamedOptionalParameterWrapperTypeChecks)
{
    diag.ClearError();
    auto interopCjo = ExportSupportPackage(ARKTS_INTEROP_SUPPORT);
    auto contextCjo = ExportSupportPackage(ARKTS_CONTEXT_SUPPORT, {{"ohos.ark_interop", interopCjo}});
    auto langCjo = ExportSupportPackage(
        ARKTS_LANG_SUPPORT, {{"ohos.ark_interop", interopCjo}, {"cj_arkts_context_v1", contextCjo}});

    diag.ClearError();
    handler->messages.clear();
    handler->errors.clear();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->importManager->SetPackageCjoCache("ohos.ark_interop", interopCjo);
    instance->importManager->SetPackageCjoCache("cj_arkts_context_v1", contextCjo);
    instance->importManager->SetPackageCjoCache("arkts.lang", langCjo);
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
foreign func greet(other: Int32, prefix!: Option<String> = None): String
)");
    ASSERT_EQ(diag.GetErrorCount(), 0) << (handler->messages.empty() ? "" : handler->messages.front());
    auto fd = FindFunction("greet");
    ASSERT_NE(fd, nullptr);
    ASSERT_NE(fd->funcBody->body, nullptr);
    bool gate = false;
    Walker(fd->funcBody->body.get(), [&gate](Ptr<Node> node) {
        auto access = DynamicCast<MemberAccess*>(node);
        if (access && access->field == "isSome") {
            gate = true;
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    EXPECT_TRUE(gate);
}

// Every named parameter of an @ArkTS foreign declaration must have a default
// value (user rule, 2026-11-05): the generated wrapper cannot tell an omitted
// named argument from an explicitly passed value. A named Option<T> without
// `= None` is therefore rejected at compile time, and no wrapper is generated
// for the declaration.
TEST_F(ArkTSForeignDiagTest, RequiredNamedOptionParameterWithoutDefaultIsRejected)
{
    CompileSema(R"(package entry

@ArkTS[module: "actual"]
foreign func required(other: Int32, prefix!: Option<String>): String
)");
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_TRUE(HasErrorContaining("must have a default value"));
    EXPECT_TRUE(HasErrorContaining("'prefix'"));
    EXPECT_TRUE(HasErrorContaining("'required'"));
    auto fd = FindFunction("required");
    ASSERT_NE(fd, nullptr);
    EXPECT_EQ(fd->funcBody->body, nullptr);
    EXPECT_TRUE(fd->TestAttr(Attribute::IS_BROKEN));
}

// Class- and interface-level @ArkTS options must survive CJO export: the
// serializer keys its ArkTS annotation records on Decl.ARKTS, so the parser has
// to raise that marker on class-likes too (functions get it in
// ParseFuncDeclAnnos). AR-05/06 read the imported preset class members and need
// the binding metadata, so a dropped class-level @ArkTS is a real loss even
// though AR-03 never reads it. This suite is where the write/read cycle already
// has a compiler instance.
TEST_F(ArkTSForeignDiagTest, ClassLevelArkTSOptionSurvivesCjoRoundTrip)
{
    diag.ClearError();
    auto interopCjo = ExportSupportPackage(ARKTS_INTEROP_SUPPORT);
    auto contextCjo = ExportSupportPackage(ARKTS_CONTEXT_SUPPORT, {{"ohos.ark_interop", interopCjo}});
    auto langCjo = ExportSupportPackage(
        ARKTS_LANG_SUPPORT, {{"ohos.ark_interop", interopCjo}, {"cj_arkts_context_v1", contextCjo}});
    auto presetCjo = ExportSupportPackage(R"(package presets

import arkts.lang.*

@ArkTS[module: "arkts.lang", foreignName: "String"]
public foreign class ArkTSString {
    public init(value: Option<String>)
    public func slice(start: Option<Float64>): ArkTSString
}

@ArkTS[module: "arkts.lang", foreignName: "Object"]
public foreign interface ArkTSType {
    func toString(): String
}
)",
        {{"ohos.ark_interop", interopCjo}, {"cj_arkts_context_v1", contextCjo}, {"arkts.lang", langCjo}});
    ASSERT_FALSE(presetCjo.empty());

    diag.ClearError();
    handler->messages.clear();
    handler->errors.clear();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->importManager->SetPackageCjoCache("presets", presetCjo);
    instance->importManager->SetPackageCjoCache("ohos.ark_interop", interopCjo);
    instance->importManager->SetPackageCjoCache("cj_arkts_context_v1", contextCjo);
    instance->importManager->SetPackageCjoCache("arkts.lang", langCjo);
    CompileSema(R"(package entry

import presets.*

public func describe(value: ArkTSType): ArkTSType {
    return value
}

public let presetValue: Option<ArkTSString> = None
)");
    ASSERT_EQ(diag.GetErrorCount(), 0) << (handler->messages.empty() ? "" : handler->messages.front());
    auto importedClass = instance->importManager->GetImportedDecl<ClassDecl>("presets", "ArkTSString");
    ASSERT_NE(importedClass, nullptr);
    EXPECT_TRUE(importedClass->TestAttr(Attribute::ARKTS));
    ASSERT_TRUE(importedClass->HasAnno(AnnotationKind::ARKTS));
    // The option-bearing record survives as an AST annotation, so a re-export
    // keeps the class's binding metadata instead of only its name.
    bool classModule = false;
    for (const auto& annotation : importedClass->annotations) {
        if (annotation->kind != AnnotationKind::ARKTS) {
            continue;
        }
        for (const auto& arg : annotation->args) {
            auto literal = DynamicCast<LitConstExpr*>(arg->expr.get());
            if (arg->name.Val() == "module" && literal) {
                classModule = literal->stringValue == "arkts.lang";
            }
        }
    }
    EXPECT_TRUE(classModule);
    auto importedInterface = instance->importManager->GetImportedDecl<InterfaceDecl>("presets", "ArkTSType");
    ASSERT_NE(importedInterface, nullptr);
    EXPECT_TRUE(importedInterface->TestAttr(Attribute::ARKTS));
    EXPECT_TRUE(importedInterface->HasAnno(AnnotationKind::ARKTS));
}

// The compiler recognizes a file that uses the @ArkTS capability and injects
// the interop package imports itself: `import ohos.ark_interop` (bound as
// `ark_interop`) and `import cj_arkts_context_v1`. Generated wrapper code spells
// the interop surface through those package names, so no user import of the
// interop surface is required and a `*` import is never needed.
TEST_F(ArkTSForeignDiagTest, ArkTSFileGetsInjectedInteropImports)
{
    diag.ClearError();
    auto interopCjo = ExportSupportPackage(ARKTS_INTEROP_SUPPORT);
    auto contextCjo = ExportSupportPackage(ARKTS_CONTEXT_SUPPORT, {{"ohos.ark_interop", interopCjo}});
    auto langCjo = ExportSupportPackage(
        ARKTS_LANG_SUPPORT, {{"ohos.ark_interop", interopCjo}, {"cj_arkts_context_v1", contextCjo}});

    diag.ClearError();
    handler->messages.clear();
    handler->errors.clear();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->importManager->SetPackageCjoCache("ohos.ark_interop", interopCjo);
    instance->importManager->SetPackageCjoCache("cj_arkts_context_v1", contextCjo);
    instance->importManager->SetPackageCjoCache("arkts.lang", langCjo);
    CompileSema(R"(package entry

@ArkTS
public func run(): Unit {}
)");
    ASSERT_EQ(diag.GetErrorCount(), 0) << (handler->messages.empty() ? "" : handler->messages.front());
    auto file = instance->GetSourcePackages().front()->files.front().get();
    auto findImport = [&file](const std::string& packageName) -> Ptr<ImportSpec> {
        for (auto& import : file->imports) {
            if (import->content.GetImportedPackageName() == packageName) {
                return import.get();
            }
        }
        return nullptr;
    };
    auto interop = findImport("ohos.ark_interop");
    auto context = findImport("cj_arkts_context_v1");
    auto exception = findImport("arkts.lang.ExternConversionException");
    ASSERT_NE(interop, nullptr);
    ASSERT_NE(context, nullptr);
    ASSERT_NE(exception, nullptr);
    for (auto import : {interop, context, exception}) {
        EXPECT_EQ(import->content.kind, ImportKind::IMPORT_SINGLE);
        EXPECT_TRUE(import->TestAttr(Attribute::COMPILER_ADD));
        EXPECT_TRUE(import->TestAttr(Attribute::PRIVATE));
        // Implicit: the user never sees the glue import in unused-import
        // warnings. The dependency json re-admits the two interop packages.
        EXPECT_TRUE(import->TestAttr(Attribute::IMPLICIT_ADD));
        // A non-implicit import needs a real source range and file ownership.
        EXPECT_FALSE(import->begin.IsZero());
        EXPECT_FALSE(import->end.IsZero());
        EXPECT_FALSE(import->content.identifier.Begin().IsZero());
        EXPECT_NE(import->curFile, nullptr);
    }
    EXPECT_EQ(interop->content.identifier.Val(), "ark_interop");
    EXPECT_EQ(interop->content.prefixPaths, (std::vector<std::string>{"ohos"}));
    EXPECT_EQ(context->content.identifier.Val(), "cj_arkts_context_v1");
    EXPECT_TRUE(context->content.prefixPaths.empty());
    EXPECT_EQ(exception->content.identifier.Val(), "ExternConversionException");
    EXPECT_EQ(exception->content.prefixPaths, (std::vector<std::string>{"arkts", "lang"}));

    // Idempotent, including the incremental/LSP rebuild path.
    instance->importManager->AddArkTSInteropImports(instance->GetSourcePackages());
    size_t interopImports = 0;
    size_t contextImports = 0;
    size_t exceptionImports = 0;
    for (auto& import : file->imports) {
        const auto name = import->content.GetImportedPackageName();
        interopImports += name == "ohos.ark_interop" ? 1 : 0;
        contextImports += name == "cj_arkts_context_v1" ? 1 : 0;
        exceptionImports += name == "arkts.lang.ExternConversionException" ? 1 : 0;
    }
    EXPECT_EQ(interopImports, 1);
    EXPECT_EQ(contextImports, 1);
    // The bare member import serves the function wrappers and the private
    // alias serves the global-variable getter.
    EXPECT_EQ(exceptionImports, 2);

    // The imports are implicit, so dependency generation re-admits exactly
    // these runtime packages: they stay visible in dependencies.json even
    // though the imports are hidden from the user.
    auto depInfo = instance->importManager->GeneratePkgDepInfo(instance->GetSourcePackages());
    ASSERT_EQ(depInfo.size(), 1);
    EXPECT_NE(depInfo.front().find("\"package\":\"ohos.ark_interop\""), std::string::npos) << depInfo.front();
    EXPECT_NE(depInfo.front().find("\"package\":\"cj_arkts_context_v1\""), std::string::npos) << depInfo.front();
    EXPECT_NE(depInfo.front().find("\"package\":\"arkts.lang\""), std::string::npos) << depInfo.front();
}

// A file without any @ArkTS annotation is not an ArkTS interop user: no
// interop package import is injected.
TEST_F(ArkTSForeignDiagTest, FileWithoutArkTSAnnotationGetsNoInjectedImports)
{
    diag.ClearError();
    auto interopCjo = ExportSupportPackage(ARKTS_INTEROP_SUPPORT);
    auto contextCjo = ExportSupportPackage(ARKTS_CONTEXT_SUPPORT, {{"ohos.ark_interop", interopCjo}});

    diag.ClearError();
    handler->messages.clear();
    handler->errors.clear();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->importManager->SetPackageCjoCache("ohos.ark_interop", interopCjo);
    instance->importManager->SetPackageCjoCache("cj_arkts_context_v1", contextCjo);
    CompileSema(R"(package entry

public func plain(): Unit {}
)");
    ASSERT_EQ(diag.GetErrorCount(), 0) << (handler->messages.empty() ? "" : handler->messages.front());
    auto file = instance->GetSourcePackages().front()->files.front().get();
    for (auto& import : file->imports) {
        const auto name = import->content.GetImportedPackageName();
        EXPECT_NE(name, "ohos.ark_interop");
        EXPECT_NE(name, "cj_arkts_context_v1");
    }
}

// An @ArkTS-carrying declaration that generates no wrapper must not surface
// the compiler-injected imports as unused-import warnings: the imports are
// implicit, so user-facing import checks skip them. A class annotation is the
// smallest such case.
TEST_F(ArkTSForeignDiagTest, ArkTSClassDoesNotWarnAboutInjectedImports)
{
    diag.ClearError();
    auto interopCjo = ExportSupportPackage(ARKTS_INTEROP_SUPPORT);
    auto contextCjo = ExportSupportPackage(ARKTS_CONTEXT_SUPPORT, {{"ohos.ark_interop", interopCjo}});

    diag.ClearError();
    handler->messages.clear();
    handler->errors.clear();
    handler->warnings.clear();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->importManager->SetPackageCjoCache("ohos.ark_interop", interopCjo);
    instance->importManager->SetPackageCjoCache("cj_arkts_context_v1", contextCjo);
    const auto warningsBefore = diag.GetWarningCount();
    CompileSema(R"(package entry

@ArkTS
public class Annotated {}
)");
    ASSERT_EQ(diag.GetErrorCount(), 0) << (handler->messages.empty() ? "" : handler->messages.front());
    EXPECT_EQ(diag.GetWarningCount(), warningsBefore);
    for (const auto& warning : handler->warnings) {
        EXPECT_EQ(warning.find("unused import"), std::string::npos) << warning;
    }
    auto file = instance->GetSourcePackages().front()->files.front().get();
    size_t injected = 0;
    for (auto& import : file->imports) {
        injected += import->content.GetImportedPackageName() == "ohos.ark_interop" ? 1 : 0;
        injected += import->content.GetImportedPackageName() == "cj_arkts_context_v1" ? 1 : 0;
    }
    EXPECT_EQ(injected, 2);
}

// The --scan-dependency path reports dependencies without building the import
// index (it does not call BuildIndex), so ImportPackages() must run the
// injection itself. The scan still reports both interop packages even though
// no package was loaded.
TEST_F(ArkTSForeignDiagTest, ScanDependencyReportsInjectedInteropPackages)
{
    diag.ClearError();
    auto interopCjo = ExportSupportPackage(ARKTS_INTEROP_SUPPORT);
    auto contextCjo = ExportSupportPackage(ARKTS_CONTEXT_SUPPORT, {{"ohos.ark_interop", interopCjo}});

    diag.ClearError();
    handler->messages.clear();
    handler->errors.clear();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->importManager->SetPackageCjoCache("ohos.ark_interop", interopCjo);
    instance->importManager->SetPackageCjoCache("cj_arkts_context_v1", contextCjo);
    instance->code = R"(package entry

@ArkTS
public func run(): Unit {}
)";
    instance->invocation.globalOptions.scanDepPkg = true;
    // Call the scan path directly: TestCompilerInstance::Compile runs
    // ModularizeCompilation(), which clears the file declarations that the
    // annotation scan needs, while the real `cjc --scan-dependency` does not.
    ASSERT_TRUE(instance->PerformParse());
    ASSERT_TRUE(instance->PerformConditionCompile());
    ASSERT_TRUE(instance->PerformImportPackage());
    const auto& depInfo = instance->GetDepPkgInfo();
    ASSERT_EQ(depInfo.size(), 1);
    EXPECT_NE(depInfo.front().find("\"package\":\"ohos.ark_interop\""), std::string::npos) << depInfo.front();
    EXPECT_NE(depInfo.front().find("\"package\":\"cj_arkts_context_v1\""), std::string::npos) << depInfo.front();
}

// Without a resolvable cjo (no ArkTS add-on) the imports are not injected, so
// a negative testcase does not gain a package-not-found diagnostic. The
// existing support-unavailable refusal is still reported exactly once.
TEST_F(ArkTSForeignDiagTest, UnresolvableInteropPackagesAreNotInjected)
{
    CompileSema(R"(package entry

@ArkTS
public func run(): Unit {}
)");
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_TRUE(HasErrorContaining("requires ArkTS interop declarations that are not available"));
    EXPECT_FALSE(HasErrorContaining("can not find package"));
    auto file = instance->GetSourcePackages().front()->files.front().get();
    for (auto& import : file->imports) {
        const auto name = import->content.GetImportedPackageName();
        EXPECT_NE(name, "ohos.ark_interop");
        EXPECT_NE(name, "cj_arkts_context_v1");
    }
}

// An ill-formed namespace value on an @ArkTS foreign function has its own
// message (not the generic option-shape one) anchored on the namespace
// literal the user must fix.
TEST_F(ArkTSForeignDiagTest, ForeignIllFormedNamespaceIsDiagnosedAtTheLiteral)
{
    const std::string code = R"(package entry

@ArkTS[namespace: "a..b"]
foreign func badNamespace(): Unit
)";
    CompileSema(code);
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_TRUE(HasErrorContaining("'namespace' option 'a..b' of @ArkTS foreign function 'badNamespace' must be a "
                                   "dot-separated identifier path with no empty segments"));
    EXPECT_FALSE(HasErrorContaining("must be string literals among"));
    const auto* error = handler->FindErrorContaining("'namespace' option 'a..b'");
    ASSERT_NE(error, nullptr);
    // The namespace literal sits on line 3, column 19 in the source below.
    EXPECT_NE(error->hint.find(":3:19:"), std::string::npos) << error->hint;
}

TEST_F(ArkTSForeignDiagTest, ForeignInterfaceMemberDispatchIsGenerated)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign interface IPerson {
    mut prop name: String
    prop age: Float64
    func sayHi(): String
}

@ArkTS[module: "actual"]
public foreign func makePerson(): IPerson

public func use(): String {
    let person = makePerson()
    person.name = "Bob"
    return person.sayHi() + person.age.toString()
}
)");
    ASSERT_EQ(diag.GetErrorCount(), 0) << (handler->messages.empty() ? "" : handler->messages.front());
    size_t callMethod = 0;
    size_t getProperty = 0;
    size_t setProperty = 0;
    size_t accessor = 0;
    size_t impl = 0;
    Walker(instance->GetSourcePackages()[0].get(), [&](Ptr<Node> node) {
        if (auto access = DynamicCast<MemberAccess*>(node)) {
            callMethod += access->field == "callMethod" ? 1 : 0;
            getProperty += access->field == "getProperty" ? 1 : 0;
            setProperty += access->field == "setProperty" ? 1 : 0;
        }
        if (auto access = DynamicCast<MemberAccess*>(node); access && access->field == "__cj_arkts_ref_value") {
            ++accessor;
        }
        if (auto cd = DynamicCast<ClassDecl*>(node); cd && cd->identifier.Val() == "IPerson$arkts_impl") {
            ++impl;
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    EXPECT_GE(callMethod, 1);
    EXPECT_GE(getProperty, 1);
    EXPECT_GE(setProperty, 1);
    EXPECT_GE(accessor, 1);
    EXPECT_EQ(impl, 1);
}

TEST_F(ArkTSForeignDiagTest, ForeignInterfaceCallAndIndexAreGenerated)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign interface IComparator {
    operator func()(a: Float64, b: Float64): Float64
}

@ArkTS[module: "actual"]
public foreign interface IStringMap {
    operator func[](k: String): Option<String>
    operator func[](k: String, value!: String): Unit
}

@ArkTS[module: "actual"]
public foreign interface INumberMap {
    operator func[](k: Float64): Option<Float64>
}

@ArkTS[module: "actual"]
public foreign func makeComparator(): IComparator

@ArkTS[module: "actual"]
public foreign func makeStringMap(): IStringMap

@ArkTS[module: "actual"]
public foreign func makeNumberMap(): INumberMap

public func use(): Float64 {
    let map = makeStringMap()
    map["alpha"] = "A"
    let _ = map["alpha"]
    let numbers = makeNumberMap()
    let _ = numbers[2.0]
    return makeComparator()(3.0, 1.0)
}
)");
    ASSERT_EQ(diag.GetErrorCount(), 0) << (handler->messages.empty() ? "" : handler->messages.front());
    size_t asFunction = 0;
    size_t getProperty = 0;
    size_t setProperty = 0;
    size_t integerKey = 0;
    Walker(instance->GetSourcePackages()[0].get(), [&](Ptr<Node> node) {
        if (auto access = DynamicCast<MemberAccess*>(node)) {
            asFunction += access->field == "asFunction" ? 1 : 0;
            getProperty += access->field == "getProperty" ? 1 : 0;
            setProperty += access->field == "setProperty" ? 1 : 0;
        }
        if (auto access = DynamicCast<MemberAccess*>(node); access && access->field == "toString") {
            ++integerKey;
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    EXPECT_GE(asFunction, 1);
    EXPECT_GE(getProperty, 1);
    EXPECT_GE(setProperty, 1);
    EXPECT_GE(integerKey, 1);
}

TEST_F(ArkTSForeignDiagTest, ForeignInterfaceShapeIsDiagnosed)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign interface Bad {
    static func make(): Int64
    func ok(): Int64
}
)");
    EXPECT_TRUE(HasErrorContaining("invalid @ArkTS foreign interface"));
    EXPECT_TRUE(HasErrorContaining("static member"));
}

TEST_F(ArkTSForeignDiagTest, ForeignInterfaceMemberBodyIsDiagnosed)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign interface Bad {
    func ok(): Int64 {
        return 1
    }
}
)");
    EXPECT_TRUE(HasErrorContaining("must not have a body"));
}

TEST_F(ArkTSForeignDiagTest, ForeignInterfaceReservedMemberIsDiagnosed)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign interface Bad {
    func __cj_arkts_value(): Int64
}
)");
    EXPECT_TRUE(HasErrorContaining("reserved for generated code"));
}

TEST_F(ArkTSForeignDiagTest, ForeignInterfaceNonForeignParentIsDiagnosed)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

public interface Parent {
    func name(): String
}

@ArkTS[module: "actual"]
public foreign interface Child <: Parent {
    func age(): Float64
}
)");
    EXPECT_TRUE(HasErrorContaining("may only inherit another foreign interface"));
}

TEST_F(ArkTSForeignDiagTest, UserClassImplementingForeignInterfaceIsDiagnosed)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign interface IPerson {
    func name(): String
}

public class Person <: IPerson {
    public func name(): String {
        return "x"
    }
}
)");
    EXPECT_TRUE(HasErrorContaining("cannot implement @ArkTS foreign interface"));
}

TEST_F(ArkTSForeignDiagTest, GenericForeignInterfaceIsNotTreatedAsCFFI)
{
    // A generic foreign interface keeps FOREIGN (it is not prepared), but it
    // is still an @ArkTS declaration and must not enter the C-FFI rules:
    // overloads are legal, and no C ABI/NO_MANGLE/INTERNAL linkage is applied.
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign interface Box<T> {
    func value(): T
    func value(prefix: String): T
}
)");
    EXPECT_EQ(diag.GetErrorCount(), 1) << (handler->messages.empty() ? "" : handler->messages.front());
    EXPECT_TRUE(HasErrorContaining("generic '@ArkTS foreign' declaration 'Box' is not supported"));
    Ptr<InterfaceDecl> box = nullptr;
    std::vector<Ptr<FuncDecl>> members;
    Walker(instance->GetSourcePackages()[0].get(), [&box, &members](Ptr<Node> node) {
        if (auto id = DynamicCast<InterfaceDecl*>(node); id && id->identifier.Val() == "Box") {
            box = id;
        }
        if (auto fd = DynamicCast<FuncDecl*>(node); fd && fd->identifier.Val() == "value") {
            members.emplace_back(fd);
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    ASSERT_NE(box, nullptr);
    EXPECT_FALSE(box->TestAttr(Attribute::C));
    EXPECT_NE(box->linkage, Linkage::INTERNAL);
    ASSERT_EQ(members.size(), 2U);
    for (auto fd : members) {
        EXPECT_FALSE(fd->TestAttr(Attribute::C));
        EXPECT_FALSE(fd->TestAttr(Attribute::NO_MANGLE));
        EXPECT_NE(fd->linkage, Linkage::INTERNAL);
    }
}

TEST_F(ArkTSForeignDiagTest, GenericForeignInterfaceKeepsExternalForm)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign interface Box<T> {
    func value(): T
}
)");
    EXPECT_EQ(diag.GetErrorCount(), 1) << (handler->messages.empty() ? "" : handler->messages.front());
    EXPECT_TRUE(HasErrorContaining("generic '@ArkTS foreign' declaration 'Box' is not supported"));
    bool lowered = false;
    Walker(instance->GetSourcePackages()[0].get(), [&lowered](Ptr<Node> node) {
        if (auto cd = DynamicCast<ClassDecl*>(node); cd && cd->identifier.Val() == "Box$arkts_impl") {
            lowered = true;
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    EXPECT_FALSE(lowered);
}

// Preparation keeps FOREIGN as the declaration's source identity and records
// the rewritten state in the kind-specific marker; the members of a prepared
// owner are ordinary declarations and carry no FOREIGN.
TEST_F(ArkTSForeignDiagTest, PreparedForeignDeclarationsKeepForeign)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign func echo(value: String): String

@ArkTS[module: "actual"]
public foreign class Box {
    public init()
    public func put(value: String): String
}

@ArkTS[module: "actual"]
public foreign interface Person {
    func name(): String
}
)");
    ASSERT_EQ(diag.GetErrorCount(), 0) << (handler->messages.empty() ? "" : handler->messages.front());

    auto echo = FindFunction("echo");
    ASSERT_NE(echo, nullptr);
    EXPECT_TRUE(echo->TestAttr(Attribute::FOREIGN));
    EXPECT_TRUE(echo->TestAttr(Attribute::ARKTS_FUNCTION_WRAPPER));
    EXPECT_TRUE(IsArkTSPrepared(*echo));
    EXPECT_FALSE(IsExternalForeign(*echo));

    auto box = FindClass("Box");
    ASSERT_NE(box, nullptr);
    EXPECT_TRUE(box->TestAttr(Attribute::FOREIGN));
    EXPECT_TRUE(box->TestAttr(Attribute::ARKTS_CLASS_WRAPPER));
    EXPECT_TRUE(IsArkTSPrepared(*box));
    EXPECT_FALSE(IsExternalForeign(*box));
    for (auto& member : box->GetMemberDecls()) {
        EXPECT_FALSE(member->TestAttr(Attribute::FOREIGN)) << member->identifier.Val();
    }

    Ptr<InterfaceDecl> person = nullptr;
    Walker(instance->GetSourcePackages()[0].get(), [&person](Ptr<Node> node) {
        if (auto id = DynamicCast<InterfaceDecl*>(node); id && id->identifier.Val() == "Person") {
            person = id;
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    ASSERT_NE(person, nullptr);
    EXPECT_TRUE(person->TestAttr(Attribute::FOREIGN));
    EXPECT_TRUE(person->TestAttr(Attribute::ARKTS_INTERFACE_WRAPPER));
    EXPECT_TRUE(IsArkTSPrepared(*person));
    EXPECT_FALSE(IsExternalForeign(*person));
    for (auto& member : person->GetMemberDecls()) {
        EXPECT_FALSE(member->TestAttr(Attribute::FOREIGN)) << member->identifier.Val();
    }
}

// A generic @ArkTS foreign interface is never prepared: FOREIGN stays and no
// marker is installed, so its member keeps the external declaration rules and
// a call from an ordinary function requires an 'unsafe' context.
TEST_F(ArkTSForeignDiagTest, GenericForeignInterfaceKeepsExternalForeign)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign interface Box<T> {
    func value(): T
}

public func read(box: Box<Int64>): Int64 {
    return box.value()
}
)");
    EXPECT_TRUE(HasErrorContaining("generic '@ArkTS foreign' declaration 'Box' is not supported"));
    Ptr<InterfaceDecl> box = nullptr;
    Walker(instance->GetSourcePackages()[0].get(), [&box](Ptr<Node> node) {
        if (auto id = DynamicCast<InterfaceDecl*>(node); id && id->identifier.Val() == "Box") {
            box = id;
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    ASSERT_NE(box, nullptr);
    EXPECT_TRUE(box->TestAttr(Attribute::FOREIGN));
    EXPECT_FALSE(IsArkTSPrepared(*box));
    EXPECT_TRUE(IsExternalForeign(*box));
}

TEST_F(ArkTSForeignDiagTest, ForeignInterfaceOptionsAndForeignNameAreAccepted)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual", namespace: "Inner.Sub", foreignName: "IPerson"]
public foreign interface PersonLike {
    @ForeignName["full-name"]
    mut prop name: String
}
)");
    ASSERT_EQ(diag.GetErrorCount(), 0) << (handler->messages.empty() ? "" : handler->messages.front());
}

TEST_F(ArkTSForeignDiagTest, ForeignInterfaceIndexGetterShapeIsDiagnosed)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign interface Bad {
    operator func[](k: String): String
}
)");
    EXPECT_TRUE(HasErrorContaining("an index getter must return Option<T>"));
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_TRUE(IsDeclBroken("Bad"));
    EXPECT_TRUE(IsDeclBroken("Bad$arkts_impl"));
}

TEST_F(ArkTSForeignDiagTest, ForeignInterfaceIndexSetterParameterIsDiagnosed)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign interface Bad {
    operator func[](k: String, value: String): Unit
}
)");
    EXPECT_TRUE(HasErrorContaining("an index setter's value parameter must be named"));
    EXPECT_EQ(diag.GetErrorCount(), 1);
}

TEST_F(ArkTSForeignDiagTest, ForeignInterfaceIndexSetterReturnIsDiagnosed)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign interface Bad {
    operator func[](k: String, value!: String): Int64
}
)");
    EXPECT_TRUE(HasErrorContaining("an index setter must return Unit"));
    EXPECT_EQ(diag.GetErrorCount(), 1);
}

TEST_F(ArkTSForeignDiagTest, ForeignInterfaceIndexArityIsDiagnosed)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign interface Bad {
    operator func[](k: String, value!: String, extra!: String): Unit
}
)");
    EXPECT_TRUE(HasErrorContaining("an index signature takes one key or a key and a named value"));
    EXPECT_EQ(diag.GetErrorCount(), 1);
}

TEST_F(ArkTSForeignDiagTest, ForeignInterfaceIndexRestIsDiagnosed)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign interface Bad {
    operator func[](@RestParam k: Array<String>): Option<String>
}
)");
    EXPECT_TRUE(HasErrorContaining("an index signature must not use a rest parameter"));
    EXPECT_EQ(diag.GetErrorCount(), 1);
}

TEST_F(ArkTSForeignDiagTest, ForeignInterfaceCallRestIsDiagnosed)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign interface Bad {
    operator func()(@RestParam xs: Array<Float64>): Float64
}
)");
    EXPECT_TRUE(HasErrorContaining("a call signature must not use a rest parameter"));
    EXPECT_EQ(diag.GetErrorCount(), 1);
}

TEST_F(ArkTSForeignDiagTest, ForeignInterfaceModulePathIsRejected)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "./actual"]
public foreign interface Pathy {
    func plain(value: Int32): Int32
}
)");
    EXPECT_TRUE(HasErrorContaining(
        "'module' option './actual' of @ArkTS foreign interface 'Pathy' must be an ArkTS module specifier"));
    auto modulePath = handler->FindErrorContaining("of @ArkTS foreign interface 'Pathy'");
    ASSERT_NE(modulePath, nullptr);
    EXPECT_EQ(modulePath->hint, ":5:16:");
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_TRUE(IsDeclBroken("Pathy"));
    // The option is refused during preparation, before the implementation
    // class is generated.
    EXPECT_EQ(FindClass("Pathy$arkts_impl"), nullptr);
}

TEST_F(ArkTSForeignDiagTest, ForeignInterfaceEmptyModuleIsSingleDiagnostic)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: ""]
public foreign interface EmptyModule {
    func plain(value: Int32): Int32
}
)");
    EXPECT_TRUE(HasErrorContaining("'module' option of '@ArkTS' must not be an empty string"));
    EXPECT_EQ(diag.GetErrorCount(), 1);
}

TEST_F(ArkTSForeignDiagTest, ForeignInterfaceIndexGetterUnsupportedResultIsDiagnosed)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign interface Bad {
    operator func[](k: String): Option<Array<Bad>>
}
)");
    EXPECT_TRUE(HasErrorContaining("is not supported"));
    EXPECT_EQ(diag.GetErrorCount(), 1);
}

TEST_F(ArkTSForeignDiagTest, ForeignInterfaceDefaultModuleIsAccepted)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS
public foreign interface Defaulted {
    func name(): String
}
)");
    ASSERT_EQ(diag.GetErrorCount(), 0) << (handler->messages.empty() ? "" : handler->messages.front());
    EXPECT_FALSE(IsDeclBroken("Defaulted"));
    EXPECT_FALSE(IsDeclBroken("Defaulted$arkts_impl"));
}

TEST_F(ArkTSForeignDiagTest, ForeignInterfaceDuplicateOptionIsDiagnosed)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual", module: "other"]
public foreign interface Dup {
    func name(): String
}
)");
    EXPECT_TRUE(HasErrorContaining("duplicated option 'module'"));
    EXPECT_EQ(diag.GetErrorCount(), 1);
}

TEST_F(ArkTSForeignDiagTest, ForeignInterfaceWriteOnlyPropertyIsRejected)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign interface WriteOnly {
    @WriteOnly
    mut prop name: String
}
)");
    EXPECT_TRUE(HasErrorContaining("@WriteOnly properties are not supported on a foreign interface"));
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_TRUE(IsDeclBroken("WriteOnly"));
    // The placement itself is legal on an @ArkTS interface's mutable
    // property; only the interface write-only semantics stay deferred.
    EXPECT_EQ(CountErrorsContaining("invalid @WriteOnly placement"), 0);
}

TEST_F(ArkTSForeignDiagTest, ForeignInterfaceBrokenParentDoesNotCascade)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign interface BrokenParent {
    operator func[](k: String): String
}

@ArkTS[module: "actual"]
public foreign interface Child <: BrokenParent {
    func childName(): String
}
)");
    // The parent's index-getter refusal is the only actionable diagnostic; the
    // child must not restate the broken parent relation.
    EXPECT_TRUE(HasErrorContaining("an index getter must return Option<T>"));
    EXPECT_EQ(diag.GetErrorCount(), 1) << (handler->messages.empty() ? "" : handler->messages.front());
    EXPECT_TRUE(IsDeclBroken("BrokenParent"));
    EXPECT_TRUE(IsDeclBroken("Child"));
}

TEST_F(ArkTSForeignDiagTest, ForeignInterfaceBrokenParentIsOrderIndependent)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign interface Child <: BrokenParent {
    func childName(): String
}

@ArkTS[module: "actual"]
public foreign interface BrokenParent {
    operator func[](k: String): String
}
)");
    // The child is declared before the parent; the broken mark must still
    // propagate so the only diagnostic is the parent's index-getter refusal.
    EXPECT_TRUE(HasErrorContaining("an index getter must return Option<T>"));
    EXPECT_EQ(diag.GetErrorCount(), 1) << (handler->messages.empty() ? "" : handler->messages.front());
    EXPECT_TRUE(IsDeclBroken("BrokenParent"));
    EXPECT_TRUE(IsDeclBroken("Child"));
}

TEST_F(ArkTSForeignDiagTest, ForeignInterfaceBrokenPreparationIsIdempotent)
{
    using namespace Cangjie::Native::FFI::ArkTS;
    ResetWithArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign interface Bad {
    @WriteOnly
    mut prop name: String
}
)");
    ASSERT_EQ(diag.GetErrorCount(), 1);
    auto pkg = instance->GetSourcePackages().front();
    const auto declsBefore = pkg->files.front()->decls.size();
    const auto messagesBefore = handler->messages.size();
    const auto errorsBefore = diag.GetErrorCount();
    PrepareTypeCheck(*pkg, diag, instance->GetSourceManager(), instance->importManager);
    PrepareTypeCheck(*pkg, diag, instance->GetSourceManager(), instance->importManager);
    EXPECT_EQ(diag.GetErrorCount(), errorsBefore);
    EXPECT_EQ(handler->messages.size(), messagesBefore);
    EXPECT_EQ(pkg->files.front()->decls.size(), declsBefore);
    EXPECT_TRUE(IsDeclBroken("Bad"));
}

TEST_F(ArkTSForeignDiagTest, StructImplementingForeignInterfaceIsDiagnosed)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign interface IPerson {
    func name(): String
}

public struct Person <: IPerson {
    public func name(): String {
        return "x"
    }
}
)");
    EXPECT_TRUE(HasErrorContaining("Cangjie type 'Person' cannot implement @ArkTS foreign interface 'IPerson'"));
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_TRUE(IsDeclBroken("Person"));
}

TEST_F(ArkTSForeignDiagTest, IndirectClassImplementingForeignInterfaceIsDiagnosed)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign interface IPerson {
    func name(): String
}

public interface LocalPerson <: IPerson {}

public class Person <: LocalPerson {
    public func name(): String {
        return "x"
    }
}
)");
    EXPECT_TRUE(HasErrorContaining("Cangjie type 'Person' cannot implement @ArkTS foreign interface 'IPerson'"));
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_TRUE(IsDeclBroken("Person"));
}

TEST_F(ArkTSForeignDiagTest, ExtendImplementingForeignInterfaceIsDiagnosed)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign interface IPerson {
    func name(): String
}

public struct Person {}

extend Person <: IPerson {
    public func name(): String {
        return "x"
    }
}
)");
    EXPECT_TRUE(HasErrorContaining("Cangjie type 'Person' cannot implement @ArkTS foreign interface 'IPerson'"));
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_TRUE(HasBrokenExtend());
}


TEST_F(ArkTSForeignDiagTest, AliasClassImplementingForeignInterfaceIsDiagnosed)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign interface IPerson {
    func name(): String
}

type PersonAlias = PersonAliasInner
type PersonAliasInner = IPerson

public class Person <: PersonAlias {
    public func name(): String {
        return "x"
    }
}
)");
    EXPECT_TRUE(HasErrorContaining("Cangjie type 'Person' cannot implement @ArkTS foreign interface 'IPerson'"));
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_TRUE(IsDeclBroken("Person"));
}

TEST_F(ArkTSForeignDiagTest, EnumImplementingForeignInterfaceIsDiagnosed)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign interface IPerson {
    func name(): String
}

public enum Person <: IPerson {
    | A | B
    public func name(): String {
        return "x"
    }
}
)");
    EXPECT_TRUE(HasErrorContaining("Cangjie type 'Person' cannot implement @ArkTS foreign interface 'IPerson'"));
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_TRUE(IsDeclBroken("Person"));
}


TEST_F(ArkTSForeignDiagTest, AliasParentForeignInterfaceIsAccepted)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign interface IParent {
    func parentName(): String
}

type ParentAlias = ParentAliasInner
type ParentAliasInner = IParent

@ArkTS[module: "actual"]
public foreign interface IChild <: ParentAlias {
    func childName(): String
}

@ArkTS[module: "actual"]
public foreign func makeChild(): IChild

public func use(): String {
    return makeChild().parentName() + makeChild().childName()
}
)");
    ASSERT_EQ(diag.GetErrorCount(), 0) << (handler->messages.empty() ? "" : handler->messages.front());
    EXPECT_FALSE(IsDeclBroken("IChild"));
}

// A non-foreign @ArkTS top-level function is exported only when it is public
// and satisfies this release's minimal entry shape. A non-public declaration
// is accepted but never exported; a public declaration outside the minimal
// shape is refused with the staged-limitation wording, and no derived
// missing-argument or missing-type-argument error is produced.
TEST_F(ArkTSForeignDiagTest, EntryShapeViolationsAreDiagnosed)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS
func notPublic(): Unit {}

@ArkTS
public func withParameters(value: Int32): Unit {}

@ArkTS
public func omittedUnit() {}

@ArkTS[namespace: "actual"]
public func withOption(): Unit {}
)");
    EXPECT_FALSE(HasErrorContaining("notPublic"));
    EXPECT_TRUE(HasErrorContaining("the shape of '@ArkTS' entry 'withParameters' is not supported yet: it declares parameters"));
    EXPECT_TRUE(
        HasErrorContaining("the shape of '@ArkTS' entry 'omittedUnit' is not supported yet: it does not declare an explicit 'Unit' return type"));
    EXPECT_TRUE(HasErrorContaining(
        "the shape of '@ArkTS' entry 'withOption' is not supported yet: it declares options other than 'module'"));
    EXPECT_EQ(diag.GetErrorCount(), 3);
    EXPECT_FALSE(IsDeclBroken("notPublic"));
    for (const auto& name : {"withParameters", "omittedUnit", "withOption"}) {
        EXPECT_TRUE(IsDeclBroken(name)) << name;
    }
    EXPECT_EQ(FindFunction("__cj_arkts_entry_0"), nullptr);
    auto entryError = handler->FindErrorContaining("entry 'withParameters'");
    ASSERT_NE(entryError, nullptr);
    const std::string entryNote =
        "this release implements only the minimal entry shape: a public parameterless function with an explicit 'Unit' "
        "return type";
    EXPECT_NE(std::find(entryError->notes.begin(), entryError->notes.end(), entryNote), entryError->notes.end());
}

// Entry names are package-wide registration keys. The first declaration keeps
// the name; a later declaration with the same name is refused without a
// second registration.
TEST_F(ArkTSForeignDiagTest, DuplicateEntryNameIsDiagnosed)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS
public func run(): Unit {}

@ArkTS
public func run(): Unit {}
)");
    EXPECT_EQ(CountErrorsContaining("duplicate @ArkTS entry name 'run' in package 'entry'"), 1);
    auto duplicate = handler->FindErrorContaining("duplicate @ArkTS entry name");
    ASSERT_NE(duplicate, nullptr);
    const std::string duplicateNote = "entry names are registered with JSModule.registerFunc";
    EXPECT_NE(std::find(duplicate->notes.begin(), duplicate->notes.end(), duplicateNote), duplicate->notes.end());
    size_t brokenRuns = 0;
    Walker walker(instance->GetSourcePackages()[0]->files[0].get(), [&brokenRuns](Ptr<Node> node) {
        auto fd = DynamicCast<FuncDecl*>(node);
        if (fd && fd->identifier.Val() == "run" && fd->TestAttr(Attribute::IS_BROKEN)) {
            ++brokenRuns;
        }
        return VisitAction::WALK_CHILDREN;
    });
    walker.Walk();
    EXPECT_EQ(brokenRuns, 1);
}

// An overload outside the minimal entry shape is reported by the staged
// limitation. It does not register a name, so the duplicate rule does not
// apply to it.
TEST_F(ArkTSForeignDiagTest, OverloadedEntryIsRejectedByShapeRule)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS
public func run(): Unit {}

@ArkTS
public func run(value: Int32): Unit {}
)");
    EXPECT_EQ(CountErrorsContaining(
                  "the shape of '@ArkTS' entry 'run' is not supported yet: it declares parameters"),
        1);
    EXPECT_EQ(CountErrorsContaining("duplicate @ArkTS entry name"), 0);
    EXPECT_EQ(diag.GetErrorCount(), 1);
}

// A user declaration named like the compiler's generated entry/registration
// names is reported directly; the entry is refused so no generic redefinition
// appears instead. Every occupying declaration gets its own diagnostic.
TEST_F(ArkTSForeignDiagTest, GeneratedEntryNameConflictIsDiagnosed)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS
public func run(): Unit {}

public func __cj_arkts_entry_0(): Unit {}
public func __cj_arkts_registration_0(): Unit {}
public func __cj_arkts_registration_9(): Unit {}
)");
    EXPECT_EQ(CountErrorsContaining("conflicts with a name reserved for generated ArkTS entry code"), 3);
    EXPECT_TRUE(HasErrorContaining("declaration '__cj_arkts_entry_0' conflicts"));
    EXPECT_TRUE(HasErrorContaining("declaration '__cj_arkts_registration_0' conflicts"));
    EXPECT_TRUE(HasErrorContaining("declaration '__cj_arkts_registration_9' conflicts"));
    auto conflict = handler->FindErrorContaining("declaration '__cj_arkts_entry_0'");
    ASSERT_NE(conflict, nullptr);
    const std::string conflictNote = "generated entry declarations are reserved by the compiler";
    EXPECT_NE(std::find(conflict->notes.begin(), conflict->notes.end(), conflictNote), conflict->notes.end());
    EXPECT_TRUE(IsDeclBroken("run"));
    size_t generated = 0;
    Walker walker(instance->GetSourcePackages()[0]->files[0].get(), [&generated](Ptr<Node> node) {
        auto decl = DynamicCast<Decl*>(node);
        if (decl && decl->TestAttr(Attribute::COMPILER_ADD)) {
            ++generated;
        }
        return VisitAction::WALK_CHILDREN;
    });
    walker.Walk();
    EXPECT_EQ(generated, 0);
}

// 'module' names an ArkTS module for a foreign declaration only. A
// well-formed module option is refused at its literal on every top-level
// non-foreign kind, including an entry; the entry-shape rule does not claim
// the option a second time.
TEST_F(ArkTSForeignDiagTest, ModuleOnNonForeignIsDiagnosed)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public func nonForeignFunction(): Unit {}

@ArkTS[module: "actual"]
public class NonForeignClass {}

@ArkTS[module: "actual"]
public let nonForeignVar: Int32 = 1

@ArkTS[module: "actual"]
public foreign func legalForeign(): Unit
)");
    EXPECT_EQ(CountErrorsContaining("'module' option is only valid on @ArkTS foreign declarations"), 3);
    EXPECT_TRUE(HasErrorContaining("not on 'nonForeignFunction'"));
    EXPECT_TRUE(HasErrorContaining("not on 'NonForeignClass'"));
    EXPECT_TRUE(HasErrorContaining("not on 'nonForeignVar'"));
    EXPECT_FALSE(HasErrorContaining("the shape of '@ArkTS' entry"));
    EXPECT_EQ(diag.GetErrorCount(), 3);
}

// @ArkTS is only valid on the top-level declaration kinds the design lists.
// A struct, type alias, local function and class member are reported at their
// annotation.
TEST_F(ArkTSForeignDiagTest, ArkTSPlacementOnUnsupportedKindIsDiagnosed)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS
public struct Point {}

@ArkTS
type Alias = Int32

@ArkTS
public func host(): Unit {
    @ArkTS
    func local(): Unit {}
}

@ArkTS
public class Host {
    @ArkTS
    public func member(): Unit {}
}
)");
    EXPECT_TRUE(HasErrorContaining("'@ArkTS' cannot be used on a struct 'Point'"));
    EXPECT_TRUE(HasErrorContaining("'@ArkTS' cannot be used on a type alias 'Alias'"));
    EXPECT_TRUE(HasErrorContaining("'@ArkTS' cannot be used on a local function 'local'"));
    EXPECT_TRUE(HasErrorContaining("'@ArkTS' cannot be used on a class member 'member'"));
    EXPECT_EQ(diag.GetErrorCount(), 4);
}

// A non-foreign @ArkTS function with type parameters cannot be an entry: the
// entry call cannot supply type arguments. No callback is generated.
TEST_F(ArkTSForeignDiagTest, GenericExportFunctionIsDiagnosed)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS
public func generic<T>(): Unit {}
)");
    EXPECT_TRUE(HasErrorContaining("@ArkTS function 'generic' must not be generic"));
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_TRUE(IsDeclBroken("generic"));
    size_t generated = 0;
    Walker walker(instance->GetSourcePackages()[0]->files[0].get(), [&generated](Ptr<Node> node) {
        auto decl = DynamicCast<Decl*>(node);
        if (decl && decl->TestAttr(Attribute::COMPILER_ADD)) {
            ++generated;
        }
        return VisitAction::WALK_CHILDREN;
    });
    walker.Walk();
    EXPECT_EQ(generated, 0);
}

// A defaulted named parameter can be omitted only when its type is Option<T>.
// A concrete type is refused and no wrapper body is generated; the Option
// spelling stays accepted.
TEST_F(ArkTSForeignDiagTest, NamedParameterDefaultMustBeOption)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
foreign func notOption(x!: Int32 = 1): Unit

@ArkTS[module: "actual"]
foreign func optional(x!: Option<Int32> = None): Unit

@ArkTS[module: "actual"]
foreign func optionalShort(x!: ?Int32 = None): Unit
)");
    EXPECT_TRUE(HasErrorContaining(
        "named parameter 'x' of @ArkTS foreign declaration 'notOption' must have type 'Option<T>', not 'Int32'"));
    EXPECT_EQ(diag.GetErrorCount(), 1);
    auto refused = FindFunction("notOption");
    ASSERT_NE(refused, nullptr);
    EXPECT_EQ(refused->funcBody->body, nullptr);
    for (const auto& name : {"optional", "optionalShort"}) {
        auto accepted = FindFunction(name);
        ASSERT_NE(accepted, nullptr) << name;
        EXPECT_NE(accepted->funcBody->body, nullptr) << name;
    }
}

// The named-parameter default rule also covers a bodyless member of an
// @ArkTS foreign class. The refused member is marked broken and the Option
// spelling stays accepted. The generic class keeps its external form, so the
// member bodies are still absent when the declaration rule runs.
TEST_F(ArkTSForeignDiagTest, ForeignClassMemberNamedParameterDefaultMustBeOption)
{
    CompileSema(R"(package entry

@ArkTS[foreignName: "String"]
public foreign class NamedParamMembers<T> {
    public init(value!: Option<String> = None)
    public func bad(x!: Int32 = 1): Unit
    public func good(x!: Option<Int32> = None): Unit
}
)");
    EXPECT_EQ(CountErrorsContaining("must have type 'Option<T>'"), 1);
    EXPECT_TRUE(HasErrorContaining(
        "named parameter 'x' of @ArkTS foreign declaration 'bad' must have type 'Option<T>', not 'Int32'"));
    EXPECT_FALSE(HasErrorContaining("named parameter 'x' of @ArkTS foreign declaration 'good'"));
}

// An index setter spells its value parameter as a named parameter without a
// default. The default-value type rule must not apply to that shape.
TEST_F(ArkTSForeignDiagTest, IndexSetterNamedValueParameterStaysAccepted)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign interface Indexed {
    operator func[](k: String): Option<String>
    operator func[](k: String, value!: String): Unit
}
)");
    EXPECT_FALSE(HasErrorContaining("must have type 'Option<T>'"));
    EXPECT_EQ(diag.GetErrorCount(), 0);
}

// The class twin of IndexSetterNamedValueParameterStaysAccepted: a foreign
// class index setter spells its required value parameter as a named parameter
// without a default, so the bodyless-member named-parameter rule must not
// reject the member before the class lowering classifies the index signature.
// Both directions generate the JS property access on the wrapper reference,
// and the index access type-checks on the class receiver.
TEST_F(ArkTSForeignDiagTest, ForeignClassIndexSetterNamedValueParameterIsGenerated)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign class StringBox {
    public init()
    public operator func[](k: String): Option<String>
    public operator func[](k: String, value!: String): Unit
}

public func use(): Unit {
    let box = StringBox()
    box["k"] = "v"
    let _ = box["k"]
}
)");
    ASSERT_EQ(diag.GetErrorCount(), 0) << (handler->messages.empty() ? "" : handler->messages.front());
    Ptr<FuncDecl> setter{nullptr};
    size_t getProperty = 0;
    size_t setProperty = 0;
    Walker(instance->GetSourcePackages()[0].get(), [&](Ptr<Node> node) {
        if (auto fd = DynamicCast<FuncDecl*>(node); fd && fd->identifier.Val() == "[]" && fd->funcBody &&
            !fd->funcBody->paramLists.empty() && fd->funcBody->paramLists[0] &&
            fd->funcBody->paramLists[0]->params.size() == 2) {
            setter = fd;
        }
        if (auto access = DynamicCast<MemberAccess*>(node)) {
            getProperty += access->field == "getProperty" ? 1 : 0;
            setProperty += access->field == "setProperty" ? 1 : 0;
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    ASSERT_NE(setter, nullptr);
    EXPECT_FALSE(setter->TestAttr(Attribute::IS_BROKEN));
    EXPECT_NE(setter->funcBody->body, nullptr);
    EXPECT_GE(getProperty, 1);
    EXPECT_GE(setProperty, 1);
}

// Entry diagnostics anchor at the user entry and carry the registration note.
// A valid entry's generated callback keeps the entry's source range instead of
// an internal generated location.
TEST_F(ArkTSForeignDiagTest, EntryDiagnosticsAnchorAtTheEntry)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS
public func bad(value: Int32): Unit {}

@ArkTS
public func good(): Unit {}
)");
    auto entryError = handler->FindErrorContaining("the shape of '@ArkTS' entry 'bad'");
    ASSERT_NE(entryError, nullptr);
    const std::string entryNote =
        "this release implements only the minimal entry shape: a public parameterless function with an explicit 'Unit' "
        "return type";
    EXPECT_NE(std::find(entryError->notes.begin(), entryError->notes.end(), entryNote), entryError->notes.end());
    auto bad = FindFunction("bad");
    ASSERT_NE(bad, nullptr);
    EXPECT_EQ(entryError->range.begin.line, bad->begin.line);
    EXPECT_EQ(entryError->range.begin.column, bad->begin.column);
    EXPECT_EQ(entryError->range.end.line, bad->end.line);
    EXPECT_EQ(entryError->range.end.column, bad->end.column);
    auto good = FindFunction("good");
    ASSERT_NE(good, nullptr);
    auto callback = FindFunction("__cj_arkts_entry_0");
    ASSERT_NE(callback, nullptr);
    EXPECT_EQ(callback->begin.line, good->begin.line);
    EXPECT_EQ(callback->begin.column, good->begin.column);
    EXPECT_EQ(callback->end.line, good->end.line);
    EXPECT_EQ(callback->end.column, good->end.column);
    Ptr<VarDecl> registration = nullptr;
    Walker registrationWalker(instance->GetSourcePackages()[0]->files[0].get(), [&registration](Ptr<Node> node) {
        auto candidate = DynamicCast<VarDecl*>(node);
        if (candidate && candidate->identifier.Val() == "__cj_arkts_registration_0") {
            registration = candidate;
        }
        return VisitAction::WALK_CHILDREN;
    });
    registrationWalker.Walk();
    ASSERT_NE(registration, nullptr);
    EXPECT_EQ(registration->begin.line, good->begin.line);
    EXPECT_EQ(registration->begin.column, good->begin.column);
    EXPECT_EQ(registration->end.line, good->end.line);
    EXPECT_EQ(registration->end.column, good->end.column);
}

// ArkTS interop is experimental. Without --experimental the ArkTS-only
// annotation names are not builtin annotations: `@ArkTS` parses as an ordinary
// macro call with no macro definition, so the file reports the ordinary
// undefined-macro diagnostic instead of the experimental gate. The wrapped
// declaration keeps its ordinary spelling, so references to it resolve without
// a cascade.
TEST_F(ArkTSForeignDiagTest, ArkTSAnnotationWithoutFlagIsAnOrdinaryMacroCall)
{
    invocation.globalOptions.experimentalMode = false;
    instance->invocation.globalOptions.experimentalMode = false;
    diag.ClearError();
    handler->messages.clear();
    CompileSema(R"(package entry

@ArkTS
public func run(): Unit {
}
)");
    EXPECT_FALSE(HasErrorContaining("requires the '--experimental' compiler option"));
    EXPECT_EQ(CountErrorsContaining("undeclared identifier 'ArkTS'"), 1U);
    EXPECT_EQ(CountErrorsContaining("undeclared identifier 'run'"), 0U);
}

// ArkTS interop is OHOS-only. With --experimental but a non-OHOS target the
// capability reports one target diagnostic per file and no wrapper is
// generated.
TEST_F(ArkTSForeignDiagTest, TargetGateRejectsNonOhosTarget)
{
    // The fixture declares an OHOS target; switch it back to the real host
    // target so the gate sees a non-OHOS compilation. Recreating the instance
    // also materializes the host target module directory the frontend probes.
    instance->invocation.globalOptions.target.env = Cangjie::Triple::Environment::GNU;
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    diag.ClearError();
    handler->messages.clear();
    CompileSema(R"(package entry

@ArkTS[module: "m"]
foreign func echo(): Int32

public func call(): Int32 {
    unsafe {
        echo()
    }
}
)");
    EXPECT_TRUE(HasErrorContaining("only available when targeting OHOS"));
    EXPECT_EQ(CountErrorsContaining("only available when targeting OHOS"), 1U);
    // No cascading unsafe-call/undeclared diagnostics and no generated roots:
    // the refused capability suppresses the ArkTS contract for the whole file.
    EXPECT_EQ(handler->messages.size(), 1U);
    bool generated = false;
    Walker(instance->GetSourcePackages()[0].get(), [&generated](Ptr<Node> node) {
        auto decl = DynamicCast<Decl*>(node);
        if (decl != nullptr && decl->TestAttr(Attribute::COMPILER_ADD) &&
            decl->identifier.Val().rfind("__cj_arkts_", 0) == 0) {
            generated = true;
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    EXPECT_FALSE(generated);
}

// The same input is not refused by the gate when the flag is present; any
// remaining error belongs to the normal ArkTS contract (missing support
// packages in this bare fixture), not to the experimental gate. That support
// error is the positive control proving the file passed the gate.
TEST_F(ArkTSForeignDiagTest, ExperimentalGateAllowsArkTSWithFlag)
{
    diag.ClearError();
    handler->messages.clear();
    CompileSema(R"(package entry

@ArkTS
public func run(): Unit {
}
)");
    EXPECT_FALSE(HasErrorContaining("requires the '--experimental'"));
    EXPECT_TRUE(HasErrorContaining("requires ArkTS interop declarations that are not available"));
}

// @RawValue is an ArkTS-only annotation too. Without --experimental it is an
// ordinary macro call: the enum constructor keeps its spelling and the file
// reports the ordinary undefined-macro diagnostic instead of the gate.
TEST_F(ArkTSForeignDiagTest, RawValueAnnotationWithoutFlagIsAnOrdinaryMacroCall)
{
    invocation.globalOptions.experimentalMode = false;
    instance->invocation.globalOptions.experimentalMode = false;
    diag.ClearError();
    handler->messages.clear();
    CompileSema(R"(package entry

enum Color {
    @RawValue[1] Red
}
)");
    EXPECT_FALSE(HasErrorContaining("requires the '--experimental' compiler option"));
    EXPECT_EQ(CountErrorsContaining("undeclared identifier 'RawValue'"), 1U);
}


// A referenced declaration wrapped by an undefined macro: without the flag the
// gate is gone and the ordinary macro path reports the missing macro once. The
// reference cascades in this fixture only because the macro-evaluation runtime
// is unavailable here, so the wrapped class is never re-declared; the real
// no-cascade behavior is pinned by the guard/requires_experimental.cj LLT,
// whose helper still resolves the wrapped declarations.
TEST_F(ArkTSForeignDiagTest, ArkTSAnnotationWithoutFlagReportsTheMacroPath)
{
    invocation.globalOptions.experimentalMode = false;
    instance->invocation.globalOptions.experimentalMode = false;
    diag.ClearError();
    handler->messages.clear();
    CompileSema(R"(package entry

@ArkTS
public class C {
    public init() {}
}

public func make(): C {
    C()
}
)");
    EXPECT_EQ(CountErrorsContaining("requires the '--experimental' compiler option"), 0U);
    EXPECT_EQ(CountErrorsContaining("undeclared identifier 'ArkTS'"), 1U);
}

// Without the flag the annotation does not lift the public x FOREIGN conflict,
// so the wrapped foreign function takes the ordinary foreign-declaration rules
// and the conflict diagnostic fires instead of the gate. (The fixture cannot
// run the macro-evaluation runtime that re-declares the wrapped function, so
// the call site cascades here; guard/requires_experimental.cj pins the real
// behavior where the wrapped spelling still resolves.)
TEST_F(ArkTSForeignDiagTest, ArkTSForeignFunctionWithoutFlagKeepsOrdinaryForeignRules)
{
    invocation.globalOptions.experimentalMode = false;
    instance->invocation.globalOptions.experimentalMode = false;
    diag.ClearError();
    handler->messages.clear();
    CompileSema(R"(package entry

@ArkTS[module: "m"]
public foreign func echo(): Unit

public func callEcho(): Unit {
    echo()
}
)");
    EXPECT_EQ(CountErrorsContaining("requires the '--experimental' compiler option"), 0U);
    EXPECT_EQ(CountErrorsContaining("'foreign' and 'public' modifiers conflict"), 1U);
}

// The same ordinary-rules shape applies to a foreign class: without the flag
// the annotation is an undefined macro call and the bodyless constructor takes
// the ordinary class diagnostic instead of the gate. (The wrapped-class
// reference cascade is a fixture artifact; see the foreign-function case.)
TEST_F(ArkTSForeignDiagTest, ArkTSForeignClassWithoutFlagKeepsOrdinaryClassRules)
{
    invocation.globalOptions.experimentalMode = false;
    instance->invocation.globalOptions.experimentalMode = false;
    diag.ClearError();
    handler->messages.clear();
    CompileSema(R"(package entry

@ArkTS[foreignName: "C"]
public foreign class C {
    public init()
}

public func make(): C {
    C()
}
)");
    EXPECT_EQ(CountErrorsContaining("requires the '--experimental' compiler option"), 0U);
    EXPECT_EQ(CountErrorsContaining("body of constructor is missing"), 1U);
}

// Without the flag an empty-attribute annotation call is an ordinary macro
// call too: the parser accepts the shape, and the macro stage reports the
// missing macro once instead of a parser diagnostic.
TEST_F(ArkTSForeignDiagTest, RawValueEmptyArgumentsWithoutFlagTakeTheMacroPath)
{
    invocation.globalOptions.experimentalMode = false;
    instance->invocation.globalOptions.experimentalMode = false;
    diag.ClearError();
    handler->messages.clear();
    CompileSema(R"(package entry

enum Color {
    @RawValue[] Red
}
)");
    EXPECT_EQ(CountErrorsContaining("requires the '--experimental' compiler option"), 0U);
    EXPECT_EQ(CountErrorsContaining("undeclared identifier 'RawValue'"), 1U);
}

// A source without any ArkTS-only annotation is untouched by the gate.
TEST_F(ArkTSForeignDiagTest, ExperimentalGateLeavesPlainSourceAlone)
{
    invocation.globalOptions.experimentalMode = false;
    instance->invocation.globalOptions.experimentalMode = false;
    diag.ClearError();
    handler->messages.clear();
    CompileSema(R"(package entry

public func plain(): Int64 {
    return 1
}
)");
    EXPECT_EQ(handler->messages.size(), 0U);
}

// @WriteOnly takes no arguments; its parser rule is gated like the rest.
TEST_F(ArkTSForeignDiagTest, WriteOnlyArgumentsWithoutFlagTakeTheMacroPath)
{
    invocation.globalOptions.experimentalMode = false;
    instance->invocation.globalOptions.experimentalMode = false;
    diag.ClearError();
    handler->messages.clear();
    CompileSema(R"(package entry

public class C {
    @WriteOnly[1]
    public mut prop p: Int64 {
        get() { 0 }
        set(v) {}
    }
}
)");
    EXPECT_EQ(CountErrorsContaining("requires the '--experimental' compiler option"), 0U);
    EXPECT_EQ(CountErrorsContaining("undeclared identifier 'WriteOnly'"), 1U);
}

// With the flag on, parser ArkTS diagnostics are not over-suppressed.
TEST_F(ArkTSForeignDiagTest, ExperimentalGateKeepsParserDiagnosticsWithFlag)
{
    diag.ClearError();
    handler->messages.clear();
    CompileSema(R"(package entry

enum Color {
    @RawValue[] Red
}
)");
    EXPECT_FALSE(HasErrorContaining("requires the '--experimental'"));
    EXPECT_TRUE(HasErrorContaining("@RawValue"));
}

// The ArkTS-only annotation names are reserved only under --experimental.
// Without the flag a user macro named like the annotation defines as an
// ordinary macro: the parse stage reports nothing.
TEST_F(ArkTSForeignDiagTest, UserMacroNamedWriteOnlyDefinesWithoutFlag)
{
    invocation.globalOptions.experimentalMode = false;
    instance->invocation.globalOptions.experimentalMode = false;
    diag.ClearError();
    handler->messages.clear();
    CompileParse(R"(macro package entry

internal import std.ast.*

public macro WriteOnly(input: Tokens): Tokens {
    return input
}
)");
    EXPECT_EQ(handler->messages.size(), 0U);
}

// With the flag on the same name collides with the builtin annotation, so the
// macro definition is refused at parse time.
TEST_F(ArkTSForeignDiagTest, UserMacroNamedWriteOnlyConflictsWithFlag)
{
    diag.ClearError();
    handler->messages.clear();
    CompileParse(R"(macro package entry

internal import std.ast.*

public macro WriteOnly(input: Tokens): Tokens {
    return input
}
)");
    EXPECT_EQ(CountErrorsContaining("conflicted with builtin annotation identifier"), 1U);
    EXPECT_EQ(handler->messages.size(), 1U);
}

// Without the flag a @RestParam parameter is an ordinary macro call in
// parameter position: the file reports the missing macro once and the ordinary
// foreign-call rules run without a second ArkTS-specific diagnostic.
TEST_F(ArkTSForeignDiagTest, RestParamWithoutFlagTakesTheMacroPath)
{
    invocation.globalOptions.experimentalMode = false;
    instance->invocation.globalOptions.experimentalMode = false;
    diag.ClearError();
    handler->messages.clear();
    CompileSema(R"(package entry

foreign func f(@RestParam xs: Array<Int32>): Unit

public func call(): Unit {
    f([])
}
)");
    EXPECT_EQ(CountErrorsContaining("requires the '--experimental' compiler option"), 0U);
    EXPECT_EQ(CountErrorsContaining("undeclared identifier 'RestParam'"), 1U);
}

// Without the flag the generic placement allow-lists no longer see ArkTS-only
// annotations at all: `@ArkTS` in a getter position is an unexpected token and
// takes the ordinary parse diagnostic.
TEST_F(ArkTSForeignDiagTest, ArkTSGetterPlacementWithoutFlagIsAnOrdinaryParseError)
{
    invocation.globalOptions.experimentalMode = false;
    instance->invocation.globalOptions.experimentalMode = false;
    diag.ClearError();
    handler->messages.clear();
    CompileSema(R"(package entry

public class C {
    public prop p: Int64 {
        @ArkTS
        get() { 0 }
    }
}
)");
    EXPECT_EQ(CountErrorsContaining("requires the '--experimental' compiler option"), 0U);
    EXPECT_EQ(CountErrorsContaining("expected 'get' or 'set' in prop body"), 1U);
    EXPECT_EQ(handler->messages.size(), 1U);
}

// A sealed foreign class cannot be extended and has no ArkTS-side spelling;
// the class path refuses it like the interface path does.
TEST_F(ArkTSForeignDiagTest, ForeignClassSealedIsRejected)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public sealed foreign class Sealed {
    public init()
}
)");
    EXPECT_TRUE(HasErrorContaining("a sealed class is not supported"));
    EXPECT_TRUE(HasErrorContaining("'Sealed'"));
}

// The constructor maps to a fixed `new` call, so @ForeignName has no member
// name to bind; accepting it silently would mislead the user.
TEST_F(ArkTSForeignDiagTest, ForeignNameOnArkTSConstructorIsRejected)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign class Holder {
    @ForeignName["make"]
    public init()
}
)");
    EXPECT_TRUE(HasErrorContaining("'@ForeignName' cannot be used on an '@ArkTS foreign' constructor"));
}

// A globalVar function is excluded from the ordinary wrapper option pass, so
// its module option must still be checked by the variable lowering path.
TEST_F(ArkTSForeignDiagTest, GlobalVarModulePathIsRejected)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "./actual", globalVar: true]
public foreign func liveValue(): Int32
)");
    EXPECT_TRUE(HasErrorContaining("must be an ArkTS module specifier"));
    EXPECT_TRUE(HasErrorContaining("'liveValue'"));
}

// The wrapper option pass refuses an explicitly empty string option; a
// globalVar function, which that pass skips, must be refused too.
TEST_F(ArkTSForeignDiagTest, GlobalVarEmptyForeignNameIsRejected)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual", foreignName: "", globalVar: true]
public foreign func liveValue(): Int32
)");
    EXPECT_TRUE(HasErrorContaining("'foreignName' option of '@ArkTS' must not be an empty string"));
}

// A block-level annotation is cloned to every member; '@HasThis' names one
// method's `this`, so the block form is refused once instead of silently
// applying to all members.
TEST_F(ArkTSForeignDiagTest, HasThisOnForeignBlockIsRejected)
{
    CompileSema(R"(package entry

@ArkTS[module: "actual"]
@HasThis[Person]
public foreign {
    func greet(): Unit
}

public class Person {}
)");
    EXPECT_TRUE(HasErrorContaining("'@HasThis' cannot be applied to a foreign block"));
}

// The base contract of '@HasThis[TypeName]': it is accepted on an '@ArkTS'
// foreign function, records the marker attribute, leaves the source type
// reference intact, and gives the declaration the runtime carrier type
// ArkTSFuncWithThis<ThisT, F>. The full call lowering of that value is a later
// requirement.
TEST_F(ArkTSForeignDiagTest, HasThisOnArkTSForeignFunctionIsAccepted)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign class Person {
    public init()
}

@ArkTS[module: "actual"]
@HasThis[Person]
public foreign func greet(message: String): Unit
)");
    ASSERT_EQ(diag.GetErrorCount(), 0) << (handler->messages.empty() ? "" : handler->messages.front());
    auto fd = FindFunction("greet");
    ASSERT_NE(fd, nullptr);
    EXPECT_TRUE(fd->TestAttr(Attribute::ARKTS_HAS_THIS));
    EXPECT_TRUE(fd->HasAnno(AnnotationKind::ARKTS_HAS_THIS));
    const Annotation* hasThis = nullptr;
    for (auto& annotation : fd->annotations) {
        if (annotation->kind == AnnotationKind::ARKTS_HAS_THIS) {
            hasThis = annotation.get();
        }
    }
    ASSERT_NE(hasThis, nullptr);
    ASSERT_EQ(hasThis->args.size(), 1);
    ASSERT_NE(hasThis->args.front()->expr, nullptr);
    // The source AST keeps the type reference, not a literal: the CJO writer
    // is what spells it as a string.
    EXPECT_EQ(hasThis->args.front()->expr->astKind, ASTKind::REF_EXPR);
    EXPECT_EQ(hasThis->args.front()->expr->ToString(), "Person");
    auto carrier = DynamicCast<ClassTy*>(fd->DataTy());
    ASSERT_NE(carrier, nullptr);
    ASSERT_NE(carrier->decl, nullptr);
    EXPECT_EQ(carrier->decl->identifier.Val(), "ArkTSFuncWithThis");
    ASSERT_EQ(carrier->typeArgs.size(), 2);
    EXPECT_TRUE(carrier->typeArgs[0]->IsClass());
    EXPECT_TRUE(carrier->typeArgs[1]->IsFunc());
}

// An '@HasThis' value carries its ArkTS `this` argument, so a direct call is
// refused with the dedicated diagnostic instead of falling through to a
// generic "not callable" error.
TEST_F(ArkTSForeignDiagTest, HasThisDirectCallIsRejected)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign class Person {
    public init()
}

@ArkTS[module: "actual"]
@HasThis[Person]
public foreign func greet(message: String): Unit

func callGreet(): Unit {
    greet("x")
}
)");
    EXPECT_TRUE(HasErrorContaining("is an '@HasThis' function value and cannot be called directly"));
    EXPECT_EQ(CountErrorsContaining("is an '@HasThis' function value and cannot be called directly"), 1U);
}

// The carrier type exposes `bind`, but producing and calling the bound value
// needs the full '@HasThis' lowering. The call is refused with the staged
// limitation instead of reaching code generation.
TEST_F(ArkTSForeignDiagTest, HasThisBindIsRejectedAsUnsupported)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign class Person {
    public init()
}

@ArkTS[module: "actual"]
@HasThis[Person]
public foreign func greet(message: String): Unit

func bindGreet(person: Person): Unit {
    let bound = greet.bind(person)
}
)");
    EXPECT_EQ(CountErrorsContaining("'ArkTSFuncWithThis.bind' is not supported yet in this release"), 1U);
    EXPECT_EQ(diag.GetErrorCount(), 1);
}

// '@HasThis' on anything other than an '@ArkTS' foreign function is a
// placement error naming the actual declaration kind.
TEST_F(ArkTSForeignDiagTest, HasThisOnNonFunctionDeclarationIsMisplaced)
{
    CompileSema(R"(package entry

@HasThis[Plain]
public class Plain {
    public init() {}
}
)");
    EXPECT_EQ(CountErrorsContaining("'@HasThis' can only annotate an '@ArkTS' foreign function"), 1U);
    EXPECT_TRUE(HasErrorContaining("a class declaration"));
}

// An ordinary Cangjie function is not an '@ArkTS' foreign function.
TEST_F(ArkTSForeignDiagTest, HasThisOnNonForeignFunctionIsMisplaced)
{
    CompileSema(R"(package entry

public class Plain {
    public init() {}
}

@HasThis[Plain]
public func host(): Unit {}
)");
    EXPECT_EQ(CountErrorsContaining("'@HasThis' can only annotate an '@ArkTS' foreign function"), 1U);
    EXPECT_TRUE(HasErrorContaining("not an '@ArkTS' foreign function"));
}

// 'globalVar: true' maps a global variable; '@HasThis' maps a method. The two
// contracts cannot hold for one declaration and the conflict is reported once.
TEST_F(ArkTSForeignDiagTest, HasThisConflictsWithGlobalVar)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

public class Plain {
    public init() {}
}

@ArkTS[module: "actual", globalVar: true]
@HasThis[Plain]
foreign func count(): Int32
)");
    EXPECT_EQ(CountErrorsContaining("'@HasThis' conflicts with the '@ArkTS' option 'globalVar: true'"), 1U);
    auto fd = FindFunction("count");
    ASSERT_NE(fd, nullptr);
    EXPECT_FALSE(fd->TestAttr(Attribute::ARKTS_HAS_THIS));
}

// The parser owns the repeated-annotation rule for builtin annotations, so a
// second '@HasThis' on one declaration is refused before Sema.
TEST_F(ArkTSForeignDiagTest, DuplicateHasThisIsDiagnosed)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign class Person {
    public init()
}

@ArkTS[module: "actual"]
@HasThis[Person]
@HasThis[Person]
public foreign func greet(): Unit
)");
    EXPECT_EQ(CountErrorsContaining("duplicated annotation"), 1U);
    EXPECT_TRUE(HasErrorContaining("@HasThis"));
}

// '@HasThis' takes exactly one unnamed type name: a literal, a named argument,
// an empty list and a multiple list are each refused with the dedicated parser
// diagnostic rather than silently stored.
TEST_F(ArkTSForeignDiagTest, HasThisRequiresOneUnnamedTypeArgument)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

public class Person {
    public init() {}
}

@ArkTS[module: "actual"]
@HasThis[]
foreign func empty(): Unit

@ArkTS[module: "actual"]
@HasThis[Person, Person]
foreign func multiple(): Unit

@ArkTS[module: "actual"]
@HasThis[name: Person]
foreign func named(): Unit

@ArkTS[module: "actual"]
@HasThis["Person"]
foreign func literal(): Unit
)");
    // The empty and multiple lists use the shared one-argument diagnostic;
    // the named argument and the literal keep the dedicated one.
    EXPECT_EQ(CountErrorsContaining("@HasThis requires exactly one argument"), 2U);
    EXPECT_EQ(CountErrorsContaining("requires exactly one unnamed type argument"), 2U);
}

// The resolved TypeName must be an ArkTS-compatible type. A plain Cangjie
// class is not an ArkTS object reference, so the contract is refused after
// type checking rather than carried into the CJO.
TEST_F(ArkTSForeignDiagTest, HasThisRejectsNonArkTSCompatibleType)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

public class Plain {
    public init() {}
}

@ArkTS[module: "actual"]
@HasThis[Plain]
public foreign func greet(): Unit
)");
    EXPECT_EQ(CountErrorsContaining("is not an ArkTS-compatible"), 1U);
    EXPECT_TRUE(HasErrorContaining("@HasThis"));
    EXPECT_TRUE(HasErrorContaining("'Plain'"));
}

// A '@HasThis' declaration fixes the ArkTS method type, so it cannot join an
// overload set: sharing its name with another foreign function is refused
// once, on the annotated declaration, which is then broken.
TEST_F(ArkTSForeignDiagTest, HasThisOverloadWithSameNameForeignFunctionIsDiagnosed)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign class Person {
    public init()
}

@ArkTS[module: "actual"]
public foreign func greet(message: String): Unit

@ArkTS[module: "actual"]
@HasThis[Person]
public foreign func greet(): Unit
)");
    EXPECT_EQ(CountErrorsContaining("cannot be overloaded"), 1U);
    EXPECT_EQ(CountErrorsContaining("'@HasThis' function 'greet'"), 1U);
    Ptr<FuncDecl> hasThisFd{nullptr};
    Walker walker(instance->GetSourcePackages()[0].get(), [&hasThisFd](Ptr<Node> node) {
        auto fd = DynamicCast<FuncDecl*>(node);
        if (fd != nullptr && fd->HasAnno(AnnotationKind::ARKTS_HAS_THIS)) {
            hasThisFd = fd;
        }
        return VisitAction::WALK_CHILDREN;
    });
    walker.Walk();
    ASSERT_NE(hasThisFd, nullptr);
    EXPECT_TRUE(hasThisFd->TestAttr(Attribute::IS_BROKEN));
    EXPECT_FALSE(hasThisFd->TestAttr(Attribute::ARKTS_HAS_THIS));
}

// Two '@HasThis' functions with distinct names do not form an overload set and
// both keep their contracts.
TEST_F(ArkTSForeignDiagTest, HasThisFunctionsWithDifferentNamesAreAccepted)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign class Person {
    public init()
}

@ArkTS[module: "actual"]
@HasThis[Person]
public foreign func greet(message: String): Unit

@ArkTS[module: "actual"]
@HasThis[Person]
public foreign func salute(message: String): Unit
)");
    ASSERT_EQ(diag.GetErrorCount(), 0) << (handler->messages.empty() ? "" : handler->messages.front());
    auto greet = FindFunction("greet");
    ASSERT_NE(greet, nullptr);
    EXPECT_TRUE(greet->TestAttr(Attribute::ARKTS_HAS_THIS));
    auto salute = FindFunction("salute");
    ASSERT_NE(salute, nullptr);
    EXPECT_TRUE(salute->TestAttr(Attribute::ARKTS_HAS_THIS));
}

// The rule is not limited to foreign declarations: any same-name package-level
// function makes the '@HasThis' declaration an overload participant.
TEST_F(ArkTSForeignDiagTest, HasThisOverloadWithOrdinaryFunctionIsDiagnosed)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign class Person {
    public init()
}

public func greet(message: String): Unit {}

@ArkTS[module: "actual"]
@HasThis[Person]
public foreign func greet(): Unit
)");
    EXPECT_EQ(CountErrorsContaining("cannot be overloaded"), 1U);
    EXPECT_EQ(CountErrorsContaining("'@HasThis' function 'greet'"), 1U);
    EXPECT_TRUE(IsDeclBroken("greet"));
}

// The carrier type ArkTSFuncWithThis<ThisT, F> crosses the package boundary as
// the declaration's serialized type, so an imported '@HasThis' declaration is
// usable as a value exactly like a same-package one. The annotation record
// still carries the ThisT spelling, and the direct-call refusal stays.
TEST_F(ArkTSForeignDiagTest, HasThisSurvivesCjoRoundTrip)
{
    diag.ClearError();
    auto interopCjo = ExportSupportPackage(ARKTS_INTEROP_SUPPORT);
    auto contextCjo = ExportSupportPackage(ARKTS_CONTEXT_SUPPORT, {{"ohos.ark_interop", interopCjo}});
    auto langCjo = ExportSupportPackage(
        ARKTS_LANG_SUPPORT, {{"ohos.ark_interop", interopCjo}, {"cj_arkts_context_v1", contextCjo}});
    auto depCjo = ExportSupportPackage(R"(package dep

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign class Person {
    public init()
}

@ArkTS[module: "actual"]
@HasThis[Person]
public foreign func greet(): Unit
)", {{"ohos.ark_interop", interopCjo}, {"cj_arkts_context_v1", contextCjo}, {"arkts.lang", langCjo}});

    diag.ClearError();
    handler->messages.clear();
    handler->errors.clear();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->importManager->SetPackageCjoCache("ohos.ark_interop", interopCjo);
    instance->importManager->SetPackageCjoCache("cj_arkts_context_v1", contextCjo);
    instance->importManager->SetPackageCjoCache("arkts.lang", langCjo);
    instance->importManager->SetPackageCjoCache("dep", depCjo);
    CompileSema(R"(package entry

import dep.*

// The imported declaration has the same carrier type as a same-package one,
// so binding it to a value is accepted; only the direct call is refused.
public func call(): Unit {
    let value = greet
    greet()
}
)");
    // Exactly one error: the direct-call refusal. The value use adds none.
    EXPECT_EQ(handler->messages.size(), 1U);
    EXPECT_EQ(CountErrorsContaining("cannot be called directly"), 1U);
    auto imported = instance->importManager->GetImportedDecl("dep", "greet");
    ASSERT_NE(imported, nullptr);
    EXPECT_TRUE(imported->TestAttr(Attribute::ARKTS_HAS_THIS));
    EXPECT_TRUE(imported->HasAnno(AnnotationKind::ARKTS_HAS_THIS));
    const Annotation* hasThis = nullptr;
    for (auto& annotation : imported->annotations) {
        if (annotation->kind == AnnotationKind::ARKTS_HAS_THIS) {
            hasThis = annotation.get();
        }
    }
    ASSERT_NE(hasThis, nullptr);
    // The writer carries the type spelling as one unnamed string literal.
    ASSERT_EQ(hasThis->args.size(), 1);
    auto literal = DynamicCast<LitConstExpr*>(hasThis->args.front()->expr.get());
    ASSERT_NE(literal, nullptr);
    EXPECT_EQ(literal->stringValue, "Person");
    // The imported static type is the carrier, not a function type. A
    // signature-shaped substitution would have dropped ThisT.
    auto carrier = DynamicCast<ClassTy*>(imported->DataTy());
    ASSERT_NE(carrier, nullptr);
    ASSERT_NE(carrier->decl, nullptr);
    EXPECT_EQ(carrier->decl->identifier.Val(), "ArkTSFuncWithThis");
    ASSERT_EQ(carrier->typeArgs.size(), 2U);
    EXPECT_EQ(carrier->typeArgs[0].Ty()->String(), "Class-Person");
    EXPECT_TRUE(carrier->typeArgs[0]->IsClass());
    EXPECT_TRUE(carrier->typeArgs[1]->IsFunc());

    // The imported carrier keeps the same staged refusal as the source one:
    // 'bind' is found through the carrier type and rejected before codegen.
    diag.ClearError();
    handler->messages.clear();
    handler->errors.clear();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->importManager->SetPackageCjoCache("ohos.ark_interop", interopCjo);
    instance->importManager->SetPackageCjoCache("cj_arkts_context_v1", contextCjo);
    instance->importManager->SetPackageCjoCache("arkts.lang", langCjo);
    instance->importManager->SetPackageCjoCache("dep", depCjo);
    CompileSema(R"(package entry

import dep.*

public func use(person: Person): Unit {
    let bound = greet.bind(person)
}
)");
    EXPECT_EQ(CountErrorsContaining("'ArkTSFuncWithThis.bind' is not supported yet"), 1U);
    EXPECT_EQ(handler->messages.size(), 1U);
}

// A prepared @WriteOnly property crosses the package boundary through the
// ARKTS_WRITE_ONLY_PROP attribute: the annotation itself has no CJO record, so
// the importer relies on the attribute and still rejects a read while keeping
// the write.
TEST_F(ArkTSForeignDiagTest, WriteOnlySurvivesCjoRoundTrip)
{
    diag.ClearError();
    auto interopCjo = ExportSupportPackage(ARKTS_INTEROP_SUPPORT);
    auto contextCjo = ExportSupportPackage(ARKTS_CONTEXT_SUPPORT, {{"ohos.ark_interop", interopCjo}});
    auto langCjo = ExportSupportPackage(
        ARKTS_LANG_SUPPORT, {{"ohos.ark_interop", interopCjo}, {"cj_arkts_context_v1", contextCjo}});
    auto depCjo = ExportSupportPackage(R"(package dep

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign class Counter {
    public init()
    @WriteOnly public mut prop value: Int32
}
)", {{"ohos.ark_interop", interopCjo}, {"cj_arkts_context_v1", contextCjo}, {"arkts.lang", langCjo}});

    diag.ClearError();
    handler->messages.clear();
    handler->errors.clear();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->importManager->SetPackageCjoCache("ohos.ark_interop", interopCjo);
    instance->importManager->SetPackageCjoCache("cj_arkts_context_v1", contextCjo);
    instance->importManager->SetPackageCjoCache("arkts.lang", langCjo);
    instance->importManager->SetPackageCjoCache("dep", depCjo);
    CompileSema(R"(package entry

import dep.*

public func write(c: Counter): Unit {
    c.value = 1
}

public func read(c: Counter): Int32 {
    return c.value
}
)");
    // The write stays legal; only the read of the imported property is refused.
    EXPECT_EQ(diag.GetErrorCount(), 1) << (handler->messages.empty() ? "" : handler->messages.front());
    EXPECT_EQ(CountErrorsContaining("is write-only and cannot be read"), 1U);
    auto counter = DynamicCast<ClassDecl*>(instance->importManager->GetImportedDecl("dep", "Counter"));
    ASSERT_NE(counter, nullptr);
    bool writeOnly = false;
    Walker(counter, [&writeOnly](Ptr<Node> node) {
        auto prop = DynamicCast<PropDecl*>(node);
        if (prop != nullptr && prop->identifier.Val() == "value") {
            writeOnly = prop->TestAttr(Attribute::ARKTS_WRITE_ONLY_PROP);
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    EXPECT_TRUE(writeOnly);
}

// @RestParam shapes the wrapper generated in the declaring package and has no
// CJO record: the importer sees the lowered array parameter and can call it,
// without the rest annotation reappearing on the imported declaration.
TEST_F(ArkTSForeignDiagTest, RestParamSignatureSurvivesCjoRoundTrip)
{
    diag.ClearError();
    auto interopCjo = ExportSupportPackage(ARKTS_INTEROP_SUPPORT);
    auto contextCjo = ExportSupportPackage(ARKTS_CONTEXT_SUPPORT, {{"ohos.ark_interop", interopCjo}});
    auto langCjo = ExportSupportPackage(
        ARKTS_LANG_SUPPORT, {{"ohos.ark_interop", interopCjo}, {"cj_arkts_context_v1", contextCjo}});
    auto depCjo = ExportSupportPackage(R"(package dep

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign func names(@RestParam items: Array<String>): Float64
)", {{"ohos.ark_interop", interopCjo}, {"cj_arkts_context_v1", contextCjo}, {"arkts.lang", langCjo}});

    diag.ClearError();
    handler->messages.clear();
    handler->errors.clear();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->importManager->SetPackageCjoCache("ohos.ark_interop", interopCjo);
    instance->importManager->SetPackageCjoCache("cj_arkts_context_v1", contextCjo);
    instance->importManager->SetPackageCjoCache("arkts.lang", langCjo);
    instance->importManager->SetPackageCjoCache("dep", depCjo);
    CompileSema(R"(package entry

import dep.*

public func call(): Float64 {
    return names(["a", "b"])
}
)");
    ASSERT_EQ(diag.GetErrorCount(), 0) << (handler->messages.empty() ? "" : handler->messages.front());
    auto names = DynamicCast<FuncDecl*>(instance->importManager->GetImportedDecl("dep", "names"));
    ASSERT_NE(names, nullptr);
    ASSERT_NE(names->funcBody, nullptr);
    ASSERT_EQ(names->funcBody->paramLists.size(), 1U);
    ASSERT_EQ(names->funcBody->paramLists.front()->params.size(), 1U);
    auto& param = *names->funcBody->paramLists.front()->params.front();
    // @RestParam is a parameter annotation and has no CJO record, so it must
    // not reappear on the imported wrapper's parameter.
    EXPECT_TRUE(std::none_of(param.annotations.begin(), param.annotations.end(), [](const auto& anno) {
        return anno != nullptr && anno->kind == AnnotationKind::ARKTS_REST_PARAM;
    }));
    // The consumer call above compiles with an array argument, which pins the
    // imported parameter shape as the lowered Array<String>.
}

// The injected support imports are link dependencies only: they must not bind
// a name for user code or conflict with a user declaration that reuses one of
// the support names. The generated wrappers keep their pre-bound SDK targets.
TEST_F(ArkTSForeignDiagTest, InjectedSupportImportsStayOutOfUserScope)
{
    ResetWithArkTSSupport();
    CompileSema(R"(
class BusinessException <: Exception {
    public init() {}
}

@ArkTS[module: "m"]
foreign func foo(value: Int32): Int32

func useIt(): Unit {
    let error = BusinessException()
    try {
        throw error
    } catch (e: BusinessException) {
        println(e.message)
    }
}
)");
    ASSERT_EQ(diag.GetErrorCount(), 0) << (handler->messages.empty() ? "" : handler->messages.front());
    EXPECT_TRUE(FindClass("BusinessException") != nullptr);
}

// The experimental gate is a source-file gate: it skips packages marked
// IMPORTED, so a prepared @ArkTS declaration imported from a dependency CJO
// must not re-trigger it when the importing package compiles without
// --experimental. The consumer here carries no ArkTS-only annotation of its
// own and only calls the imported declaration.
TEST_F(ArkTSForeignDiagTest, ImportedArkTSDoesNotReTriggerExperimentalGate)
{
    diag.ClearError();
    auto interopCjo = ExportSupportPackage(ARKTS_INTEROP_SUPPORT);
    auto contextCjo = ExportSupportPackage(ARKTS_CONTEXT_SUPPORT, {{"ohos.ark_interop", interopCjo}});
    auto langCjo = ExportSupportPackage(
        ARKTS_LANG_SUPPORT, {{"ohos.ark_interop", interopCjo}, {"cj_arkts_context_v1", contextCjo}});
    auto depCjo = ExportSupportPackage(R"(package dep

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign func answer(): Int32
)", {{"ohos.ark_interop", interopCjo}, {"cj_arkts_context_v1", contextCjo}, {"arkts.lang", langCjo}});

    // Fresh instance with the flag off, as a user compiling a plain consumer
    // against an already prepared dependency would see it.
    diag.ClearError();
    handler->messages.clear();
    handler->errors.clear();
    invocation.globalOptions.experimentalMode = false;
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->importManager->SetPackageCjoCache("ohos.ark_interop", interopCjo);
    instance->importManager->SetPackageCjoCache("cj_arkts_context_v1", contextCjo);
    instance->importManager->SetPackageCjoCache("arkts.lang", langCjo);
    instance->importManager->SetPackageCjoCache("dep", depCjo);
    CompileSema(R"(package entry

import dep.*

public func use(): Int32 {
    answer()
}
)");
    // Zero gate diagnostics proves the gate did not inspect the imported
    // package; zero errors of any kind proves the imported declaration is
    // still usable with the capability disabled.
    EXPECT_EQ(CountErrorsContaining("requires the '--experimental' compiler option"), 0U);
    EXPECT_EQ(handler->messages.size(), 0U);

    // Positive control: the flag really is off for this compilation, so source
    // ArkTS in the consumer takes the ordinary macro path (the annotation is an
    // undefined macro call) even while the imported @ArkTS declaration stays
    // silent.
    diag.ClearError();
    handler->messages.clear();
    handler->errors.clear();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->importManager->SetPackageCjoCache("ohos.ark_interop", interopCjo);
    instance->importManager->SetPackageCjoCache("cj_arkts_context_v1", contextCjo);
    instance->importManager->SetPackageCjoCache("arkts.lang", langCjo);
    instance->importManager->SetPackageCjoCache("dep", depCjo);
    CompileSema(R"(package entry

import dep.*

@ArkTS
public func run(): Unit {
}
)");
    EXPECT_EQ(CountErrorsContaining("requires the '--experimental' compiler option"), 0U);
    EXPECT_EQ(CountErrorsContaining("undeclared identifier 'ArkTS'"), 1U);
}

// The target gate is scoped the same way: with the consumer switched back to
// the real host target, source ArkTS is refused on this Linux target, but an
// @ArkTS declaration imported from a prepared dependency CJO must not
// re-report the target gate.
TEST_F(ArkTSForeignDiagTest, ImportedArkTSDoesNotReTriggerTargetGate)
{
    diag.ClearError();
    auto interopCjo = ExportSupportPackage(ARKTS_INTEROP_SUPPORT);
    auto contextCjo = ExportSupportPackage(ARKTS_CONTEXT_SUPPORT, {{"ohos.ark_interop", interopCjo}});
    auto langCjo = ExportSupportPackage(
        ARKTS_LANG_SUPPORT, {{"ohos.ark_interop", interopCjo}, {"cj_arkts_context_v1", contextCjo}});
    auto depCjo = ExportSupportPackage(R"(package dep

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign func answer(): Int32
)", {{"ohos.ark_interop", interopCjo}, {"cj_arkts_context_v1", contextCjo}, {"arkts.lang", langCjo}});

    // The dependency was exported with the fixture's OHOS target; switch the
    // consumer back to the real host target so the gate would fire for any
    // source-file ArkTS.
    instance->invocation.globalOptions.target.env = Cangjie::Triple::Environment::GNU;
    diag.ClearError();
    handler->messages.clear();
    handler->errors.clear();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->importManager->SetPackageCjoCache("ohos.ark_interop", interopCjo);
    instance->importManager->SetPackageCjoCache("cj_arkts_context_v1", contextCjo);
    instance->importManager->SetPackageCjoCache("arkts.lang", langCjo);
    instance->importManager->SetPackageCjoCache("dep", depCjo);
    CompileSema(R"(package entry

import dep.*

public func use(): Int32 {
    answer()
}
)");
    // The imported package is skipped, so the non-OHOS target produces no
    // diagnostic at all for this plain consumer.
    EXPECT_EQ(CountErrorsContaining("only available when targeting OHOS"), 0U);
    EXPECT_EQ(handler->messages.size(), 0U);

    // Positive control: source ArkTS on the same target is still refused once,
    // proving the gate is active and only the imported package escaped it.
    diag.ClearError();
    handler->messages.clear();
    handler->errors.clear();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    CompileSema(R"(package entry

@ArkTS[module: "m"]
public foreign func echo(): Int32
)");
    EXPECT_EQ(CountErrorsContaining("only available when targeting OHOS"), 1U);
    EXPECT_EQ(handler->messages.size(), 1U);

    // Restore the fixture's OHOS target for any later compilation in this
    // test.
    instance->invocation.globalOptions.target.env = Cangjie::Triple::Environment::OHOS;
}

// ---- B4b: ArkTSFunc<F> function carrier ----

// The tests below use the preset-package carrier declared in the mock
// std.interop.arkts (the package pin decides); the arkts.lang stub carrier
// stays untouched for the '@HasThis' suites, so these files import only the
// preset package.

// Passing and returning ArkTSFunc<F> is supported: the position classifies
// as the carrier and lowers with identity conversions (isFunction guard on
// the way in, JSValue handle on the way out); no dedicated constraint
// diagnostic and no generic "unsupported type" text fires.
TEST_F(ArkTSForeignDiagTest, ArkTSFuncSignaturePositionsAreAccepted)
{
    PrepareArkTSTypeRelationSupport();
    CompileSema(R"(package entry

import std.interop.arkts.*

@ArkTS[module: "actual"]
public foreign func takeFunc(cb: ArkTSFunc<(Int32) -> Int32>): Unit

@ArkTS[module: "actual"]
public foreign func giveFunc(): ArkTSFunc<(String) -> Bool>
)");
    EXPECT_EQ(diag.GetErrorCount(), 0);
    auto give = FindFunction("giveFunc");
    ASSERT_NE(give, nullptr);
    EXPECT_TRUE(give->funcBody != nullptr && give->funcBody->body != nullptr);
}

// F is not a function type: the dedicated invalid-type-argument diagnostic
// names the written argument; the ordinary constructor machinery never
// answers with the generic unsupported-type text.
TEST_F(ArkTSForeignDiagTest, ArkTSFuncNonFunctionArgumentIsRejected)
{
    PrepareArkTSTypeRelationSupport();
    CompileSema(R"(package entry

import std.interop.arkts.*

func cb(x: Int32): Int32 { return x }

public func build(): Unit {
    let f = ArkTSFunc<Int32>(cb)
}
)");
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_EQ(CountErrorsContaining("must be a concrete function type"), 1);
}

// A parameter of F is not ArkTS-compatible: the dedicated member diagnostic
// fires on the construction, naming the written function type.
TEST_F(ArkTSForeignDiagTest, ArkTSFuncUnsupportedMemberIsRejected)
{
    PrepareArkTSTypeRelationSupport();
    CompileSema(R"(package entry

import std.interop.arkts.*

class Point {
    public var x: Int32 = 0
}

func cb(p: Point): Unit {}

public func build(): Unit {
    let f = ArkTSFunc<(Point) -> Unit>(cb)
}
)");
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_EQ(CountErrorsContaining("not ArkTS-compatible"), 1);
}

// Design 4.2.1 e3/e4: an ordinary (non-@ArkTS) generic function may construct
// a carrier, but only for a concrete F. An F that mentions the enclosing
// declaration's type parameter has nothing to instantiate against.
TEST_F(ArkTSForeignDiagTest, ArkTSFuncOuterGenericArgumentIsRejected)
{
    PrepareArkTSTypeRelationSupport();
    CompileSema(R"(package entry

import std.interop.arkts.*

public func wrap<T>(f: (T) -> Int32): ArkTSFunc<(T) -> Int32> {
    return ArkTSFunc<(T) -> Int32>(f)
}
)");
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_EQ(CountErrorsContaining("must not depend on an enclosing declaration's type parameter"), 1);
}

// The same constraint inside a @ArkTS foreign signature reports the carrier
// diagnostic on the written position, not the generic parameter-type text.
TEST_F(ArkTSForeignDiagTest, ArkTSFuncBadSignaturePositionIsDiagnosed)
{
    PrepareArkTSTypeRelationSupport();
    CompileSema(R"(package entry

import std.interop.arkts.*

@ArkTS[module: "actual"]
public foreign func takeFunc(cb: ArkTSFunc<Int32>): Unit
)");
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_EQ(CountErrorsContaining("must be a concrete function type"), 1);
    EXPECT_EQ(CountErrorsContaining("has unsupported type"), 0);
}

// A well-shaped explicit construction is lowered at the use site onto the
// carrier's JSValue wrapping constructor through the generated `(ctx, info)`
// lambda: no diagnostic at all, never the generic constructor-mismatch text
// and never the unsupported-type text. The file carries an @ArkTS annotation,
// so the injected interop imports select the capability for the generated
// glue — the same rule every generated-body site follows.
TEST_F(ArkTSForeignDiagTest, ArkTSFuncConstructionIsLowered)
{
    PrepareArkTSTypeRelationSupport();
    CompileSema(R"(package entry

import std.interop.arkts.*

@ArkTS[module: "actual"]
public foreign func takeFunc(cb: ArkTSFunc<(Int32) -> Int32>): Unit

func cb(x: Int32): Int32 { return x }

public func build(): ArkTSFunc<(Int32) -> Int32> {
    let f = ArkTSFunc<(Int32) -> Int32>(cb)
    return f
}
)");
    EXPECT_EQ(diag.GetErrorCount(), 0);
    EXPECT_EQ(CountErrorsContaining("has unsupported type"), 0);
    EXPECT_EQ(CountErrorsContaining("no matching constructor"), 0);
}

// The generated glue resolves the interop context through the compiler-injected
// imports, so a construction in a file that carries no @ArkTS annotation has no
// capability selected and is refused with the support-availability diagnostic
// naming the enclosing function.
TEST_F(ArkTSForeignDiagTest, ArkTSFuncConstructionWithoutCapabilityIsRefused)
{
    PrepareArkTSTypeRelationSupport();
    CompileSema(R"(package entry

import std.interop.arkts.*

func cb(x: Int32): Int32 { return x }

public func build(): ArkTSFunc<(Int32) -> Int32> {
    return ArkTSFunc<(Int32) -> Int32>(cb)
}
)");
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_EQ(
        CountErrorsContaining("requires ArkTS interop declarations that are not available"), 1);
}

// The wrong construction shape (named argument) is told apart from the
// constraint failures and from the ordinary overload machinery.
TEST_F(ArkTSForeignDiagTest, ArkTSFuncNamedConstructionArgumentIsRefused)
{
    PrepareArkTSTypeRelationSupport();
    CompileSema(R"(package entry

import std.interop.arkts.*

func cb(x: Int32): Int32 { return x }

public func build(): Unit {
    let f = ArkTSFunc<(Int32) -> Int32>(f: cb)
}
)");
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_EQ(CountErrorsContaining("exactly one positional function value"), 1);
}

// The carrier value is passed and returned, never invoked from Cangjie: a
// direct call reports the dedicated refusal instead of the generic
// "not callable" overload text (design 4.2.1 e6).
TEST_F(ArkTSForeignDiagTest, ArkTSFuncDirectCallIsRefusedCleanly)
{
    PrepareArkTSTypeRelationSupport();
    CompileSema(R"(package entry

import std.interop.arkts.*

public func callIt(cb: ArkTSFunc<(Int32) -> Int32>): Unit {
    cb(1)
}
)");
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_EQ(CountErrorsContaining("cannot be called directly"), 1);
}

// An ordinary call written with one explicit type argument is not carrier
// construction syntax: the recognition hook must leave it to the ordinary
// base check. The hook runs before the base check has assigned the call
// context (callOrPattern), so a speculative resolution of any other name
// used to answer a one-parameter enum constructor sugar with a premature
// undeclared-identifier error that the ordinary (successful) resolution
// could no longer prevent.
TEST_F(ArkTSForeignDiagTest, OneTypeArgumentCallsWithoutCarrierNameAreOrdinaryCalls)
{
    PrepareArkTSTypeRelationSupport();
    CompileSema(R"(package entry

import std.interop.arkts.*

enum E<T> { | A(T) | B }

class Box<T> {
    public init(x: T) {}
}

func id<T>(x: T): T { return x }

func g<T>(x: T): E<T> { return A<T>(x) }

func f<T>(x: T): Option<T> { return Some<T>(x) }

public func build(): Unit {
    let e = A<Int64>(1)
    let o = Some<Int64>(1)
    let b = Box<Int64>(1)
    let i = id<Int64>(1)
}
)");
    EXPECT_EQ(diag.GetErrorCount(), 0);
    EXPECT_EQ(CountErrorsContaining("undeclared identifier"), 0);
}

// The gate keeps the carrier construction itself intact while the same shape
// (one type argument, one value argument) on other names resolves on the
// ordinary path in the same file: no name steals the carrier's lowering and
// the carrier does not steal their resolution.
TEST_F(ArkTSForeignDiagTest, CarrierConstructionCoexistsWithOneTypeArgumentCalls)
{
    PrepareArkTSTypeRelationSupport();
    CompileSema(R"(package entry

import std.interop.arkts.*

@ArkTS[module: "actual"]
public foreign func takeFunc(cb: ArkTSFunc<(Int32) -> Int32>): Unit

func cb(x: Int32): Int32 { return x }

enum E<T> { | A(T) | B }

func g<T>(x: T): E<T> { return A<T>(x) }

func f<T>(x: T): Option<T> { return Some<T>(x) }

public func build(): ArkTSFunc<(Int32) -> Int32> {
    let carrier = ArkTSFunc<(Int32) -> Int32>(cb)
    let e = A<Int64>(1)
    let o = Some<Int64>(1)
    return carrier
}
)");
    EXPECT_EQ(diag.GetErrorCount(), 0);
    EXPECT_EQ(CountErrorsContaining("undeclared identifier"), 0);
    EXPECT_EQ(CountErrorsContaining("no matching constructor"), 0);
    EXPECT_EQ(CountErrorsContaining("has unsupported type"), 0);
}

// String literals of one generated body, in source order: generated messages
// are concatenations, so the pieces are inspected individually.
static std::vector<std::string> GeneratedBodyStringLiterals(Ptr<FuncDecl> fn)
{
    std::vector<std::string> literals;
    if (!fn || !fn->funcBody || !fn->funcBody->body) {
        return literals;
    }
    Walker walker(fn->funcBody->body.get(), [&literals](Ptr<Node> node) {
        auto literal = DynamicCast<LitConstExpr*>(node);
        if (literal && literal->kind == LitConstKind::STRING) {
            literals.push_back(literal->stringValue);
        }
        return VisitAction::WALK_CHILDREN;
    });
    walker.Walk();
    return literals;
}

// ---- B4b: the carrier result and construction fail-closed rules ----

// A composite Nothing in F's result has no to-JS conversion row: Option, Array
// and tuple shapes are each refused with the dedicated unsupported-member
// message naming the written function type, exactly one diagnostic per shape.
TEST_F(ArkTSForeignDiagTest, ArkTSFuncOptionNothingResultIsRejected)
{
    PrepareArkTSTypeRelationSupport();
    CompileSema(R"(package entry

import std.interop.arkts.*

func cb(x: Int32): Int32 { return x }

public func build(): Unit {
    let f = ArkTSFunc<()->Option<Nothing>>(cb)
}
)");
    ASSERT_EQ(diag.GetErrorCount(), 1) << (handler->messages.empty() ? "" : handler->messages.front());
    EXPECT_EQ(CountErrorsContaining("not ArkTS-compatible"), 1);
}

TEST_F(ArkTSForeignDiagTest, ArkTSFuncArrayNothingResultIsRejected)
{
    PrepareArkTSTypeRelationSupport();
    CompileSema(R"(package entry

import std.interop.arkts.*

func cb(x: Int32): Int32 { return x }

public func build(): Unit {
    let f = ArkTSFunc<()->Array<Nothing>>(cb)
}
)");
    ASSERT_EQ(diag.GetErrorCount(), 1) << (handler->messages.empty() ? "" : handler->messages.front());
    EXPECT_EQ(CountErrorsContaining("not ArkTS-compatible"), 1);
}

TEST_F(ArkTSForeignDiagTest, ArkTSFuncTupleNothingResultIsRejected)
{
    PrepareArkTSTypeRelationSupport();
    CompileSema(R"(package entry

import std.interop.arkts.*

func cb(x: Int32): Int32 { return x }

public func build(): Unit {
    let f = ArkTSFunc<()->(Int32, Nothing)>(cb)
}
)");
    ASSERT_EQ(diag.GetErrorCount(), 1) << (handler->messages.empty() ? "" : handler->messages.front());
    EXPECT_EQ(CountErrorsContaining("not ArkTS-compatible"), 1);
}

// The control: a top-level Nothing result is the never contract, not a
// composite. The construction is accepted, generates the glue and the lambda
// ends in the never-contract throw instead of a result conversion.
TEST_F(ArkTSForeignDiagTest, ArkTSFuncTopLevelNothingResultKeepsNeverContract)
{
    PrepareArkTSTypeRelationSupport();
    CompileSema(R"(package entry

import std.interop.arkts.*

@ArkTS[module: "actual"]
public foreign func takeFunc(cb: ArkTSFunc<()->Nothing>): Unit

func cb(): Nothing { throw Exception("never") }

public func build(): ArkTSFunc<()->Nothing> {
    let f = ArkTSFunc<()->Nothing>(cb)
    return f
}
)");
    ASSERT_EQ(diag.GetErrorCount(), 0) << (handler->messages.empty() ? "" : handler->messages.front());
    auto build = FindFunction("build");
    ASSERT_NE(build, nullptr);
    auto literals = GeneratedBodyStringLiterals(build);
    EXPECT_NE(std::find(literals.begin(), literals.end(), std::string("ArkTS never function returned")),
        literals.end());
}

// An imported foreign enum enters F through its serialized value table: the
// construction succeeds and the generated glue converts the enum parameter
// (null guard and dispatch throws name the enum).
TEST_F(ArkTSForeignDiagTest, ArkTSFuncEnumMemberFromImportedCjoIsConverted)
{
    diag.ClearError();
    auto interopCjo = ExportSupportPackage(ARKTS_INTEROP_SUPPORT);
    auto depCjo = ExportSupportPackage(R"(package dep

@ArkTS[module: "actual"]
public foreign enum E {
    | @RawValue["a"] A
    | @RawValue[7] B
    | number(Float64)
}
)");
    auto contextCjo = ExportSupportPackage(ARKTS_CONTEXT_SUPPORT, {{"ohos.ark_interop", interopCjo}});
    auto stdCjo = ExportSupportPackage(
        ARKTS_STDLIB_SUPPORT, {{"ohos.ark_interop", interopCjo}, {"cj_arkts_context_v1", contextCjo}});
    auto langCjo = ExportSupportPackage(ARKTS_LANG_SUPPORT,
        {{"ohos.ark_interop", interopCjo}, {"cj_arkts_context_v1", contextCjo}});
    diag.ClearError();
    handler->messages.clear();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->importManager->SetPackageCjoCache("dep", depCjo);
    instance->importManager->SetPackageCjoCache("ohos.ark_interop", interopCjo);
    instance->importManager->SetPackageCjoCache("cj_arkts_context_v1", contextCjo);
    instance->importManager->SetPackageCjoCache("std.interop.arkts", stdCjo);
    instance->importManager->SetPackageCjoCache("arkts.lang", langCjo);
    CompileSema(R"(package entry

import std.interop.arkts.*
import dep.E

@ArkTS[module: "actual"]
public foreign func takeFunc(cb: ArkTSFunc<(E) -> Unit>): Unit

func cb(e: E): Unit {}

public func build(): ArkTSFunc<(E) -> Unit> {
    let f = ArkTSFunc<(E) -> Unit>(cb)
    return f
}
)");
    ASSERT_EQ(diag.GetErrorCount(), 0) << (handler->messages.empty() ? "" : handler->messages.front());
    auto build = FindFunction("build");
    ASSERT_NE(build, nullptr);
    auto literals = GeneratedBodyStringLiterals(build);
    EXPECT_NE(std::find(literals.begin(), literals.end(), std::string("ArkTS type mismatch: null for E")),
        literals.end());
    EXPECT_NE(std::find(literals.begin(), literals.end(), std::string("ArkTS value invalid: unknown value for E")),
        literals.end());
}

// A same-package foreign class enters F: the construction succeeds and the
// generated glue converts the reference parameter through the wrapping
// constructor (the object-kind guard names the class).
TEST_F(ArkTSForeignDiagTest, ArkTSFuncForeignClassMemberEntersCarrier)
{
    PrepareArkTSTypeRelationSupport();
    CompileSema(R"(package entry

import ohos.ark_interop.JSObject
import std.interop.arkts.*

@ArkTS[module: "actual"]
public foreign class Point {
    public init(x: Float64)
}

@ArkTS[module: "actual"]
public foreign func takeFunc(cb: ArkTSFunc<(Point) -> Float64>): Unit

func cb(p: Point): Float64 { return 0.0 }

public func build(): ArkTSFunc<(Point) -> Float64> {
    let f = ArkTSFunc<(Point) -> Float64>(cb)
    return f
}
)");
    ASSERT_EQ(diag.GetErrorCount(), 0) << (handler->messages.empty() ? "" : handler->messages.front());
    auto build = FindFunction("build");
    ASSERT_NE(build, nullptr);
    auto literals = GeneratedBodyStringLiterals(build);
    EXPECT_NE(std::find(literals.begin(), literals.end(), std::string("ArkTS type mismatch: not an object for Point")),
        literals.end());
}

// A same-package foreign interface enters F: its generated implementation
// class is resolvable, so the construction succeeds and the glue constructs
// the implementation class for the parameter.
TEST_F(ArkTSForeignDiagTest, ArkTSFuncForeignInterfaceMemberEntersCarrier)
{
    PrepareArkTSTypeRelationSupport();
    CompileSema(R"(package entry

import ohos.ark_interop.JSObject
import std.interop.arkts.*

@ArkTS[module: "actual"]
public foreign interface IWorker {
    func work(): Unit
}

@ArkTS[module: "actual"]
public foreign func takeFunc(cb: ArkTSFunc<(IWorker) -> Unit>): Unit

func cb(w: IWorker): Unit {}

public func build(): ArkTSFunc<(IWorker) -> Unit> {
    let f = ArkTSFunc<(IWorker) -> Unit>(cb)
    return f
}
)");
    ASSERT_EQ(diag.GetErrorCount(), 0) << (handler->messages.empty() ? "" : handler->messages.front());
    auto build = FindFunction("build");
    ASSERT_NE(build, nullptr);
    bool implConstructed = false;
    Walker walker(build->funcBody->body.get(), [&implConstructed](Ptr<Node> node) {
        auto ref = DynamicCast<RefExpr*>(node);
        if (ref && ref->ref.identifier.Val() == "IWorker$arkts_impl") {
            implConstructed = true;
        }
        return VisitAction::WALK_CHILDREN;
    });
    walker.Walk();
    EXPECT_TRUE(implConstructed);
}

// A prepared foreign interface whose implementation class cannot be resolved
// in this compilation enters F: the dedicated unsupported-member message
// replaces the silent CALL_INVALID the ungeneratable glue used to produce.
// The dep package is exported with the compiler-added implementation class
// erased, so the imported interface stays prepared while the resolution of
// its implementation class fails.
TEST_F(ArkTSForeignDiagTest, ArkTSFuncUnresolvableInterfaceImplIsDiagnosed)
{
    diag.ClearError();
    auto interopCjo = ExportSupportPackage(ARKTS_INTEROP_SUPPORT);
    auto contextCjo = ExportSupportPackage(ARKTS_CONTEXT_SUPPORT, {{"ohos.ark_interop", interopCjo}});
    auto langCjo = ExportSupportPackage(ARKTS_LANG_SUPPORT,
        {{"ohos.ark_interop", interopCjo}, {"cj_arkts_context_v1", contextCjo}});
    auto depCjo = ExportSupportPackageWithoutCompilerAddedClass(R"(package dep

import ohos.ark_interop.JSObject

@ArkTS[module: "actual"]
public foreign interface IWorker {
    func work(): Unit
}
)",
        "IWorker$arkts_impl",
        {{"ohos.ark_interop", interopCjo}, {"cj_arkts_context_v1", contextCjo}, {"arkts.lang", langCjo}});
    auto stdCjo = ExportSupportPackage(
        ARKTS_STDLIB_SUPPORT, {{"ohos.ark_interop", interopCjo}, {"cj_arkts_context_v1", contextCjo}});
    diag.ClearError();
    handler->messages.clear();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->importManager->SetPackageCjoCache("dep", depCjo);
    instance->importManager->SetPackageCjoCache("ohos.ark_interop", interopCjo);
    instance->importManager->SetPackageCjoCache("cj_arkts_context_v1", contextCjo);
    instance->importManager->SetPackageCjoCache("std.interop.arkts", stdCjo);
    instance->importManager->SetPackageCjoCache("arkts.lang", langCjo);
    CompileSema(R"(package entry

import std.interop.arkts.*
import dep.IWorker

func cb(w: IWorker): Unit {}

public func build(): Unit {
    let f = ArkTSFunc<(IWorker) -> Unit>(cb)
}
)");
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_EQ(CountErrorsContaining("not ArkTS-compatible"), 1);
}

// A @ForeignName argument that is not a string literal has no readable value:
// the parser reports it and marks the annotation broken, and lowering falls
// back to the Cangjie identifier instead of dereferencing a missing literal.
TEST_F(ArkTSForeignDiagTest, ForeignNameNonLiteralArgumentDoesNotBreakSema)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign class Holder {
    public init()
    @ForeignName[hashCode]
    public func echo(): Unit
}
)");
    EXPECT_TRUE(HasErrorContaining("requires exactly one 'String' literal"));
    // The wrapper still lowers with the Cangjie identifier as the member name,
    // so the class and its member stay usable.
    auto holder = FindClass("Holder");
    ASSERT_NE(holder, nullptr);
    EXPECT_TRUE(holder->TestAttr(Attribute::ARKTS_CLASS_WRAPPER));
    EXPECT_FALSE(IsDeclBroken("Holder"));
    EXPECT_FALSE(IsDeclBroken("echo"));
}

// A compound assignment and an increment read the property through its getter
// before writing it, so a write-only property refuses them exactly like a
// plain read; the plain assignment stays legal.
TEST_F(ArkTSForeignDiagTest, WriteOnlyCompoundAssignmentAndIncrementAreDiagnosed)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual"]
public foreign class Counter {
    public init()
    @WriteOnly public mut prop value: Int32
}

public func bump(c: Counter): Unit {
    c.value += 1
}

public func tick(c: Counter): Unit {
    c.value++
}

public func set(c: Counter): Unit {
    c.value = 1
}
)");
    EXPECT_EQ(CountErrorsContaining("is write-only and cannot be read"), 2U);
    EXPECT_FALSE(IsDeclBroken("Counter"));
}

// A user string value that spells a placeholder name (sentinel prefix plus a
// real key) is emitted verbatim: the rebinding walker matches the raw source
// spelling, which carries the escape, so only compiler-generated placeholders
// are rewritten. The module name binding still works.
TEST_F(ArkTSForeignDiagTest, RawValueSpellingAPlaceholderNameStaysVerbatim)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual", foreignName: "Marked"]
foreign enum Marked {
    | @RawValue["__cj_arkts_literal____module__"] A
    | @RawValue["plain"] B
}

@ArkTS[module: "actual", foreignName: "echoMarked"]
foreign func echoMarked(value: Marked): Marked
)");
    ASSERT_EQ(diag.GetErrorCount(), 0) << (handler->messages.empty() ? "" : handler->messages.front());
    auto fn = FindFunction("echoMarked");
    ASSERT_NE(fn, nullptr);
    ASSERT_NE(fn->funcBody->body, nullptr);
    auto literals = GeneratedBodyStringLiterals(fn);
    // The colliding value survives both the conversion and the kind guard
    // verbatim; only the module placeholder is rebound to the module name.
    EXPECT_NE(std::find(literals.begin(), literals.end(), "__cj_arkts_literal____module__"), literals.end());
    EXPECT_NE(std::find(literals.begin(), literals.end(), "plain"), literals.end());
    EXPECT_NE(std::find(literals.begin(), literals.end(), "actual"), literals.end());
}

// The getter's conversion throws use the private alias injected for this file.
// The alias pass rewrites bare identifiers in code only, never the inside of
// string literals, so a user-supplied name that spells the same text keeps its
// bytes in the failure message while the throws still bind to the alias.
TEST_F(ArkTSForeignDiagTest, GetterAliasDoesNotTouchUserStrings)
{
    PrepareArkTSSupport();
    CompileSema(R"(package entry

import arkts.lang.*

@ArkTS[module: "actual", foreignName: "ExternConversionException", globalVar: true]
foreign func flag(): Bool
)");
    ASSERT_EQ(diag.GetErrorCount(), 0) << (handler->messages.empty() ? "" : handler->messages.front());
    auto getter = FindFunction("flag");
    ASSERT_NE(getter, nullptr);
    ASSERT_NE(getter->funcBody->body, nullptr);
    auto literals = GeneratedBodyStringLiterals(getter);
    // The variable name reaches the failure context as string bytes, untouched
    // by the alias pass; the alias identifier only appears as a reference.
    EXPECT_NE(std::find(literals.begin(), literals.end(), "ExternConversionException"), literals.end());
    for (const auto& literal : literals) {
        EXPECT_EQ(literal.find("__cj_arkts_extern_conversion"), std::string::npos) << literal;
    }
    size_t aliasThrows = 0;
    Walker walker(getter->funcBody->body.get(), [&aliasThrows](Ptr<Node> node) {
        auto ref = DynamicCast<RefExpr*>(node);
        if (ref && ref->ref.identifier.Val() == "__cj_arkts_extern_conversion") {
            ++aliasThrows;
            EXPECT_NE(ref->ref.target, nullptr);
            if (ref->ref.target) {
                EXPECT_EQ(ref->ref.target->fullPackageName, "arkts.lang");
            }
        }
        return VisitAction::WALK_CHILDREN;
    });
    walker.Walk();
    EXPECT_GT(aliasThrows, 0);
}

// A pattern whose serialized tyModes vector holds fewer entries than its type
// list is a corrupt cjo: the loader treats every missing entry as "no mode"
// instead of indexing out of bounds, and the declaration still loads.
TEST_F(ArkTSForeignDiagTest, PatternTyModesShorterThanTypesLoadAsMissing)
{
    CompileSema(R"(package patmod

@Frozen
public func run(v: Int64): Unit {
    match (v) {
        case num: Int64 => ()
    }
}
)");
    ASSERT_EQ(diag.GetErrorCount(), 0) << (handler->messages.empty() ? "" : handler->messages.front());
    auto& sourcePkg = *instance->GetSourcePackages()[0];
    // Give the type pattern a modal type so the writer serializes a tyModes
    // entry alongside its single type; a healthy writer never emits a pattern
    // whose tyModes list is shorter than its type list, so the shortfall below
    // is a corruption the loader must survive.
    Walker patternWalker(&sourcePkg, [](Ptr<Node> node) {
        if (auto tp = DynamicCast<TypePattern*>(node)) {
            tp->SetTy({tp->GetTy().Ty(), ModalInfo{Mode::FULL}});
        }
        return VisitAction::WALK_CHILDREN;
    });
    patternWalker.Walk();

    ExportConfig config;
    config.exportContent = true;
    config.exportForIncr = true;
    auto& cjoManager = *instance->importManager->GetCjoManager();
    auto packageDecl = cjoManager.GetPackageDecl("patmod");
    ASSERT_NE(packageDecl, nullptr);
    ASTWriter writer(diag, "", config, cjoManager, *instance->typeManager);
    writer.PreSaveFullExportDecls(sourcePkg);
    writer.ExportAST(*packageDecl);
    std::vector<uint8_t> buffer;
    writer.AST2FB(buffer, *packageDecl);
    ASSERT_FALSE(buffer.empty());

    // Locate the serialized type pattern and truncate its tyModes vector to
    // zero entries: the bytes of the former entry stay in place, so a loader
    // that indexes without a length check reads a stale mode instead of
    // treating the entry as missing.
    const auto* rawPackage = PackageFormat::GetPackage(buffer.data());
    ASSERT_NE(rawPackage->allExprs(), nullptr);
    const PackageFormat::Pattern* patternObj = nullptr;
    for (auto* exprObj : *rawPackage->allExprs()) {
        if (exprObj->kind() != PackageFormat::ExprKind_MatchCase || exprObj->info() == nullptr) {
            continue;
        }
        auto info = exprObj->info_as_MatchCaseInfo();
        if (info == nullptr || info->patterns() == nullptr) {
            continue;
        }
        for (auto* candidate : *info->patterns()) {
            if (candidate->kind() == PackageFormat::PatternKind_TypePattern && candidate->tyModes() != nullptr) {
                patternObj = candidate;
            }
        }
    }
    ASSERT_NE(patternObj, nullptr);
    ASSERT_EQ(patternObj->tyModes()->size(), 1U);
    // Zero the vector's length field (the first member of the flatbuffers
    // Vector header): the entry becomes "missing" for the reader while its
    // bytes stay in place, so a loader that indexes without a length check
    // reads a stale mode instead of treating the entry as absent.
    const auto* modes = patternObj->tyModes();
    const_cast<uint32_t*>(reinterpret_cast<const uint32_t*>(modes))[0] = 0;

    diag.ClearError();
    handler->messages.clear();
    handler->errors.clear();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->importManager->SetPackageCjoCache("patmod", buffer);
    CompileSema(R"(package entry

import patmod.*

public func use(): Unit {
    run(1)
}
)");
    ASSERT_EQ(diag.GetErrorCount(), 0) << (handler->messages.empty() ? "" : handler->messages.front());
    auto run = DynamicCast<FuncDecl*>(instance->importManager->GetImportedDecl("patmod", "run"));
    ASSERT_NE(run, nullptr);
    bool found = false;
    Walker runWalker(run, [&found](Ptr<Node> node) {
        auto tp = DynamicCast<TypePattern*>(node);
        if (tp) {
            found = true;
            EXPECT_EQ(tp->GetTy().Mode().local, Mode::NOT);
            EXPECT_TRUE(Ty::IsTyCorrect(tp->GetTy().Ty()));
        }
        return VisitAction::WALK_CHILDREN;
    });
    runWalker.Walk();
    EXPECT_TRUE(found);
}
