// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 * Unit tests for TypeManager::GetTopOverriddenFuncDecl.
 *
 * GetTopOverriddenFuncDecl walks the overrideMap and then continues up the
 * inheritance chain so that the true top-most overridden function is reached
 * even when the map only records a partial prefix of the chain (which is the
 * common case for imported declarations, e.g. an override of Equatable.!=).
 * These tests drive real Cangjie source through Sema, then assert the resolved
 * top-most declaration for selected override functions.
 */

#include <cstdlib>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "gtest/gtest.h"

// Open private members of TypeManager so the partial-overrideMap test can
// directly simulate the "imported / incomplete map" condition that the API's
// inheritance-walk fallback is designed for. Standard library headers must be
// included BEFORE this macro to avoid altering their internal access.
#define private public
#include "TestCompilerInstance.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Sema/TypeManager.h"
#undef private

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
} // namespace

class GetTopOverriddenFuncDeclTest : public testing::Test {
protected:
    void SetUp() override
    {
        instance = std::make_unique<TestCompilerInstance>(invocation, diag);
#ifdef PROJECT_SOURCE_DIR
        projectPath = PROJECT_SOURCE_DIR;
#else
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

    std::string projectPath;
    std::string savedCangjieHome;
    std::string savedCangjiePath;
    DiagnosticEngine diag;
    CompilerInvocation invocation;
    std::unique_ptr<TestCompilerInstance> instance;

    /**
     * Collect every FuncDecl declared directly inside a class/interface body,
     * indexed by "OuterClassName.funcName". The override target of a function
     * is itself a FuncDecl, so we need a way to look up the declaration by the
     * class that owns it.
     */
    using FuncMap = std::unordered_map<std::string, Ptr<const FuncDecl>>;

    FuncMap CollectFuncDecls()
    {
        FuncMap result;
        Walker walker(instance->GetSourcePackages()[0]->files[0].get(),
            [&result](Ptr<Node> node) -> VisitAction {
                auto fd = DynamicCast<FuncDecl*>(node);
                if (!fd || !fd->outerDecl) {
                    return VisitAction::WALK_CHILDREN;
                }
                // Only index methods whose outer decl is a nominal (class/interface/extend)
                // declared in the user source; skip local closures/lambdas.
                if (!fd->outerDecl->IsNominalDecl()) {
                    return VisitAction::WALK_CHILDREN;
                }
                std::string outerName = static_cast<std::string>(fd->outerDecl->identifier);
                std::string funcName = static_cast<std::string>(fd->identifier);
                // Methods defined inside an `extend` block have an empty outerDecl
                // identifier (the extend block itself has no name). Disambiguate them
                // with the "extend." prefix so tests can locate them deterministically.
                if (outerName.empty()) {
                    outerName = "extend";
                }
                result[outerName + "." + funcName] = Ptr<const FuncDecl>(fd);
                return VisitAction::WALK_CHILDREN;
            });
        walker.Walk();
        return result;
    }

    Ptr<const FuncDecl> GetTop(const FuncDecl* fd)
    {
        return instance->typeManager->GetTopOverriddenFuncDecl(fd);
    }

    /**
     * Compile a dependency package from @p src (which must start with a
     * `package <name>` directive) and return its serialized AST (cjo) buffer.
     * The returned buffer can be handed to a downstream (main) package via
     * SetPackageCjoCache so the main package can `import` it.
     */
    std::vector<uint8_t> CompileDependencyPackage(const std::string& pkgName, const std::string& src)
    {
        instance = std::make_unique<TestCompilerInstance>(invocation, diag);
        instance->code = src;
        instance->invocation.globalOptions.implicitPrelude = true;
        // A dependency package is a library: static-lib output does not require a
        // main entry, and compilePackage packages the AST for downstream import.
        instance->invocation.globalOptions.outputMode = GlobalOptions::OutputMode::STATIC_LIB;
        instance->invocation.globalOptions.compilePackage = true;
        bool ok = instance->Compile(CompileStage::DESUGAR_AFTER_SEMA);
        EXPECT_TRUE(ok) << "dependency package " << pkgName << " failed to compile";
        std::vector<uint8_t> astData;
        instance->importManager->ExportAST(false, astData, *instance->GetSourcePackages()[0]);
        // Restore the default output mode for the next (main) package.
        instance->invocation.globalOptions.outputMode = GlobalOptions::OutputMode::EXECUTABLE;
        return astData;
    }

    /**
     * Begin a fresh main-package compilation that imports the dependency package
     * whose cjo buffer is @p depCjo (registered under @p depPkgName). After this
     * returns, `instance` is the main-package instance ready for Compile(SEMA).
     */
    void BeginMainPackage(const std::string& depPkgName, const std::vector<uint8_t>& depCjo,
        const std::string& mainSrc)
    {
        instance = std::make_unique<TestCompilerInstance>(invocation, diag);
        instance->importManager->SetPackageCjoCache(depPkgName, depCjo);
        // Resolve imports from the in-memory cjo cache rather than searching disk.
        instance->importManager->SetSourceCodeImportStatus(true);
        instance->invocation.globalOptions.implicitPrelude = true;
        instance->code = mainSrc;
    }

    /**
     * Locate the FuncDecl that a call expression `expr.field(...)` resolves to.
     * Walks the main package AST looking for a MemberAccess whose @field equals
     * @p calleeName inside a CallExpr, and returns the call's resolvedFunction.
     * For `B().f()` this is the imported B.f declaration.
     */
    Ptr<const FuncDecl> FindResolvedCallee(const std::string& calleeName, Ptr<Node> root)
    {
        Ptr<const FuncDecl> found{nullptr};
        Walker walker(root,
            [&found, &calleeName](Ptr<Node> node) -> VisitAction {
                auto call = DynamicCast<CallExpr*>(node);
                if (!call) {
                    return VisitAction::WALK_CHILDREN;
                }
                auto ma = DynamicCast<MemberAccess*>(call->baseFunc.get());
                if (ma && static_cast<std::string>(ma->field) == calleeName && call->resolvedFunction) {
                    found = Ptr<const FuncDecl>(
                        RawStaticCast<FuncDecl*>(call->resolvedFunction));
                }
                return VisitAction::WALK_CHILDREN;
            });
        walker.Walk();
        return found;
    }

    /**
     * Semantic identity for a FuncDecl: "<outerClass>.<funcName>". We compare
     * by name rather than by raw pointer because GetTopOverriddenFuncDecl is
     * documented to return the top-most declaration of the override chain,
     * which may be a different node instance than the one captured by the AST
     * walker (e.g. a desugared or canonical entry). Name equality captures the
     * behavior the API contract guarantees to its callers.
     */
    static std::string QualName(Ptr<const FuncDecl> fd)
    {
        if (!fd || !fd->outerDecl) {
            return "<null>";
        }
        std::string outer = static_cast<std::string>(fd->outerDecl->identifier);
        std::string name = static_cast<std::string>(fd->identifier);
        return outer + "." + name;
    }
};

// Cross-package version of the interface-implementation scenario.
//
// Dependency package `dep` declares interface I and two classes A (which
// implements I.f) and B (which overrides A.f). The main package imports `dep`
// and calls B().f(). The B.f declaration the call resolves to is an IMPORTED
// decl whose overrideMap edges are typically only partially recorded (exactly
// the "imported / partial map" condition GetTopOverriddenFuncDecl's inheritance
// walk is designed to handle, e.g. an override of Equatable.!=). Despite that,
// resolving the imported B.f must still reach the abstract interface root I.f.
TEST_F(GetTopOverriddenFuncDeclTest, ImplOfInterfaceResolvesToAbstractRoot)
{
    // 1) Compile the dependency package.
    auto depCjo = CompileDependencyPackage("dep", R"(
package dep
public interface I {
    func f(): Int64
}
public open class A <: I {
    public open func f(): Int64 {
        return 1
    }
}
public open class B <: A {
    public func f(): Int64 {
        return 2
    }
}
    )");
    ASSERT_FALSE(depCjo.empty());

    // 2) Compile the main package, which imports dep and calls B().f().
    BeginMainPackage("dep", depCjo, R"(
import dep.*
main(): Int64 {
    return B().f()
}
    )");
    ASSERT_TRUE(instance->Compile(CompileStage::SEMA));
    ASSERT_EQ(diag.GetErrorCount(), 0u);

    // 3) The call B().f() must have resolved to the imported B.f declaration.
    auto callee = FindResolvedCallee("f", instance->GetSourcePackages()[0]->files[0]);
    ASSERT_TRUE(callee) << "B().f() call did not resolve to a function";
    EXPECT_TRUE(callee->TestAttr(Attribute::IMPORTED))
        << "expected the callee to be the imported B.f";

    // 4) Resolving the imported B.f must reach the abstract interface root I.f,
    //    not B.f or A.f. This exercises the inheritance-walk fallback that the
    //    API uses when the overrideMap is incomplete for imported declarations.
    auto top = GetTop(callee.get());
    ASSERT_TRUE(top);
    EXPECT_EQ(QualName(top), "I.f");
    EXPECT_TRUE(top->TestAttr(Attribute::ABSTRACT));
    EXPECT_NE(QualName(top), "B.f");
    EXPECT_NE(QualName(top), "A.f");
}

TEST_F(GetTopOverriddenFuncDeclTest, GetTopOverriddenFuncWithGeneric1)
{
    // 1) Compile the dependency package.
    auto depCjo = CompileDependencyPackage("dep", R"(
package dep
public interface I<T> {
    func f(a: T): Int64
}
public open class A<T> <: I<T> {
    public open func f(a: T): Int64 {
        return 1
    }
}
public open class B <: A<Int64> {
    public func f(a: Int64): Int64 {
        return 2
    }
}
    )");
    ASSERT_FALSE(depCjo.empty());

    // 2) Compile the main package, which imports dep and calls B().f().
    BeginMainPackage("dep", depCjo, R"(
import dep.*
main(): Int64 {
    return B().f(5)
}
    )");
    ASSERT_TRUE(instance->Compile(CompileStage::SEMA));
    ASSERT_EQ(diag.GetErrorCount(), 0u);

    // 3) The call B().f() must have resolved to the imported B.f declaration.
    auto callee = FindResolvedCallee("f", instance->GetSourcePackages()[0]->files[0]);
    ASSERT_TRUE(callee) << "B().f() call did not resolve to a function";
    EXPECT_TRUE(callee->TestAttr(Attribute::IMPORTED)) << "expected the callee to be the imported B.f";

    // 4) Resolving the imported B.f must reach the abstract interface root I.f,
    //    not B.f or A.f. This exercises the inheritance-walk fallback that the
    //    API uses when the overrideMap is incomplete for imported declarations.
    auto top = GetTop(callee.get());
    ASSERT_TRUE(top);
    EXPECT_EQ(QualName(top), "I.f");
    EXPECT_TRUE(top->TestAttr(Attribute::ABSTRACT));
    EXPECT_NE(QualName(top), "B.f");
    EXPECT_NE(QualName(top), "A.f");
}

TEST_F(GetTopOverriddenFuncDeclTest, GetTopOverriddenFuncThatSourceExported01)
{
    // 1) Compile the dependency package.
    auto depCjo = CompileDependencyPackage("dep", R"(
package dep
public interface I<T> {
    func f(a: T): Int64
}
public open class A<T> <: I<T> {
    public open func f(a: T): Int64 {
        return 1
    }
}
public open class B <: A<Int64> {
    public func f(a: Int64): Int64 {
        return 2
    }
}

@Frozen
public func foo() {
    return B().f(5)
}
    )");
    ASSERT_FALSE(depCjo.empty());

    // 2) Compile the main package, which imports dep and calls B().f().
    BeginMainPackage("dep", depCjo, R"(
import dep.*
main(): Int64 {
    0
}
    )");
    instance->invocation.globalOptions.optimizationLevel = GlobalOptions::OptimizationLevel::O2;
    ASSERT_TRUE(instance->Compile(CompileStage::SEMA));
    ASSERT_EQ(diag.GetErrorCount(), 0u);

    // 3) The call B().f() must have resolved to the imported B.f declaration.
    auto foo = instance->importManager->GetImportedDecl("dep", "foo");
    auto callee = FindResolvedCallee("f", foo);
    ASSERT_TRUE(callee) << "B().f() call did not resolve to a function";
    EXPECT_TRUE(callee->TestAttr(Attribute::IMPORTED)) << "expected the callee to be the imported B.f";

    // 4) Resolving the imported B.f must reach the abstract interface root I.f,
    //    not B.f or A.f. This exercises the inheritance-walk fallback that the
    //    API uses when the overrideMap is incomplete for imported declarations.
    auto top = GetTop(callee.get());
    ASSERT_TRUE(top);
    EXPECT_EQ(QualName(top), "I.f");
    EXPECT_TRUE(top->TestAttr(Attribute::ABSTRACT));
    EXPECT_NE(QualName(top), "B.f");
    EXPECT_NE(QualName(top), "A.f");
}
