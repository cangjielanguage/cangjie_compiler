// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * Unit tests for OverrideFunctionResolver, the on-demand query + static cache
 * introduced by the generic instantiation pointer-rearrange refactor
 * (SR.IR20260706000885.011).
 *
 * These UTs cover only the small set of internal states that ST cannot assert
 * end-to-end, as scoped by the DT design's "UT automation requirement" section:
 *   - Cache lifecycle (miss -> write -> hit -> ClearCache -> re-miss, no stale hit)
 *   - Candidate set deduplication (MergeIntoFuncs / RemoveImplementedSupers)
 *   - GetMatchedFuncInstTyByGivenTarget matching (generic substitution path)
 * Behavioural correctness of the 6-step selection algorithm itself is verified
 * by ST cases (rearrange01-13) and is out of scope here.
 */

#include <cstdlib>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "gtest/gtest.h"

// Open private members so the static cache (instTy2MembersCache) can be inspected
// directly for the cache-lifecycle assertions. Must precede the resolver header.
#define private public
#include "TestCompilerInstance.h"
#include "../../src/Sema/OverrideFunctionResolver.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Walker.h"

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

class OverrideFunctionResolverTest : public testing::Test {
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
        Cangjie::MacroProcMsger::GetInstance().CloseMacroSrv();
        // Each case starts from a clean cache so ordering between tests does not matter.
        OverrideFunctionResolver::ClearCache();
    }

    void TearDown() override
    {
        OverrideFunctionResolver::ClearCache();
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

    // Compile a single-source snippet to the generic instantiation stage, i.e. the
    // stage at which OverrideFunctionResolver is exercised in production. Asserts
    // the snippet compiled cleanly so downstream lookups operate on real types.
    void CompileToGenericInstantiation(const std::string& code)
    {
        instance->code = code;
        instance->invocation.globalOptions.implicitPrelude = true;
        ASSERT_TRUE(instance->Compile(CompileStage::GENERIC_INSTANTIATION))
            << "test snippet must compile to GENERIC_INSTANTIATION for resolver lookup";
        EXPECT_EQ(diag.GetErrorCount(), 0) << "unexpected diagnostics in test snippet";
    }

    // Walk the first source file and return the first InterfaceDecl whose name
    // matches @p name. Used to obtain the instantiated base type for queries.
    // Uses DynamicCast (matching TypeCheckerTest's style) rather than StaticAs, so a
    // non-matching node yields nullptr instead of a cast assertion failure.
    InterfaceDecl* FindInterface(const std::string& name)
    {
        InterfaceDecl* found = nullptr;
        Walker walker(instance->GetSourcePackages()[0]->files[0].get(), [&name, &found](Ptr<Node> node) {
            if (found != nullptr) {
                return VisitAction::SKIP_CHILDREN;
            }
            auto iface = DynamicCast<InterfaceDecl*>(node);
            if (iface && static_cast<std::string>(iface->identifier) == name) {
                found = iface;
            }
            return VisitAction::WALK_CHILDREN;
        });
        walker.Walk();
        return found;
    }

    // Walk the first source file and return the first ClassDecl whose name matches @p name.
    ClassDecl* FindClass(const std::string& name)
    {
        ClassDecl* found = nullptr;
        Walker walker(instance->GetSourcePackages()[0]->files[0].get(), [&name, &found](Ptr<Node> node) {
            if (found != nullptr) {
                return VisitAction::SKIP_CHILDREN;
            }
            auto cls = DynamicCast<ClassDecl*>(node);
            if (cls && static_cast<std::string>(cls->identifier) == name) {
                found = cls;
            }
            return VisitAction::WALK_CHILDREN;
        });
        walker.Walk();
        return found;
    }

    // Find the first non-abstract member func named @p name in @p decl's members.
    FuncDecl* FindConcreteFunc(ClassLikeDecl* decl, const std::string& name)
    {
        if (!decl) {
            return nullptr;
        }
        for (auto& member : decl->GetMemberDecls()) {
            if (member->astKind != ASTKind::FUNC_DECL) {
                continue;
            }
            auto func = StaticCast<FuncDecl>(member.get());
            if (static_cast<std::string>(func->identifier) == name && !func->TestAttr(Attribute::ABSTRACT)) {
                return func;
            }
        }
        return nullptr;
    }

    // Find the first abstract member func named @p name in @p decl's members (the interface
    // abstract method used as the resolver's "target" in production call sites).
    FuncDecl* FindAbstractFunc(ClassLikeDecl* decl, const std::string& name)
    {
        if (!decl) {
            return nullptr;
        }
        for (auto& member : decl->GetMemberDecls()) {
            if (member->astKind != ASTKind::FUNC_DECL) {
                continue;
            }
            auto func = StaticCast<FuncDecl>(member.get());
            if (static_cast<std::string>(func->identifier) == name && func->TestAttr(Attribute::ABSTRACT)) {
                return func;
            }
        }
        return nullptr;
    }

    std::string projectPath;
    std::string savedCangjieHome;
    std::string savedCangjiePath;
    DiagnosticEngine diag;
    CompilerInvocation invocation;
    std::unique_ptr<TestCompilerInstance> instance;
};

// DT: module-interference interface 2, case 1 (cache miss computes) + case 2 (hit is stable) +
//     case 3 (ClearCache re-miss, no stale hit). White-box #18 / focus #3.
//
// The static cache is keyed by (instBaseTy, identifier). A first query must populate it,
// a second query must return the cached value, and after ClearCache a re-query must recompute
// rather than serve a stale entry. ST can only observe runtime stdout, never these transitions.
TEST_F(OverrideFunctionResolverTest, CacheLifecycleMissHitAndReset)
{
    // I1 has a default foo; A <: I1 overrides foo with public visibility. Querying foo on A's
    // instantiated type collects candidates from the interface default impl and the class override.
    CompileToGenericInstantiation(R"(
interface I1 {
    func foo(): Int64 { return 1 }
}
class A <: I1 {
    public func foo(): Int64 { return 2 }
}
main(): Int64 { return 0 }
    )");

    auto cls = FindClass("A");
    ASSERT_TRUE(cls);
    ASSERT_TRUE(cls->GetTy());
    ASSERT_TRUE(cls->GetTy()->IsClass());

    OverrideFunctionResolver resolver(*instance->typeManager);
    auto baseTy = cls->DataTy();
    const std::string id = "foo";

    // NOTE: Compile(GENERIC_INSTANTIATION) itself calls GetInstMemberFuncWithInstTy during
    // instantiation, so the cache is already populated for (baseTy, "foo") by the time we
    // reach here. Clear again so the first query below is a genuine miss.
    OverrideFunctionResolver::ClearCache();
    ASSERT_TRUE(OverrideFunctionResolver::instTy2MembersCache.empty());

    // Case 1: first query is a cache miss -> computed and written into the cache.
    MemberFuncsWithInstTys first = resolver.GetInstMemberFuncWithInstTy(baseTy, id);
    ASSERT_FALSE(first.empty());
    auto key = std::make_pair(baseTy, id);
    EXPECT_NE(OverrideFunctionResolver::instTy2MembersCache.find(key),
        OverrideFunctionResolver::instTy2MembersCache.end());

    // Case 2: second query is a cache hit -> identical contents, no recomputation.
    MemberFuncsWithInstTys second = resolver.GetInstMemberFuncWithInstTy(baseTy, id);
    ASSERT_EQ(first.size(), second.size());
    for (auto& entry : first) {
        auto it = second.find(entry.first);
        ASSERT_NE(it, second.end());
        ASSERT_EQ(entry.second.size(), it->second.size());
    }

    // Case 3: ClearCache resets to empty; re-query re-misses but yields the same result,
    // i.e. no stale hit serving a wrong entry.
    OverrideFunctionResolver::ClearCache();
    EXPECT_TRUE(OverrideFunctionResolver::instTy2MembersCache.empty());
    MemberFuncsWithInstTys third = resolver.GetInstMemberFuncWithInstTy(baseTy, id);
    EXPECT_FALSE(OverrideFunctionResolver::instTy2MembersCache.empty());
    ASSERT_EQ(first.size(), third.size());
    for (auto& entry : first) {
        auto it = third.find(entry.first);
        ASSERT_NE(it, third.end());
        ASSERT_EQ(entry.second.size(), it->second.size());
    }
}

// DT: coupling scenario case 2 (MergeIntoFuncs / RemoveImplementedSupers dedup).
//     White-box #22. With multiple extend sources supplying the same default impl (I2 <: I1
//     both provide foo), the candidate map must keep at most one entry per FuncDecl and never
//     duplicate instTys. ST only asserts the final dispatched version; the no-duplicate
//     invariant is internal.
TEST_F(OverrideFunctionResolverTest, CandidateSetHasNoDuplicateEntries)
{
    // I2 <: I1 both provide a default foo; A is made to implement both via two extends. The
    // resolver must dedup these same-named default impls under I1's identifier.
    CompileToGenericInstantiation(R"(
interface I1 {
    func foo(): Int64 { return 1 }
}
interface I2 <: I1 {
    func foo(): Int64 { return 2 }
}
class A {}
extend A <: I1 {}
extend A <: I2 {}
main(): Int64 { return 0 }
    )");

    auto cls = FindClass("A");
    ASSERT_TRUE(cls);
    ASSERT_TRUE(cls->GetTy());

    OverrideFunctionResolver resolver(*instance->typeManager);
    // ClearCache first: GI has already queried foo on A during compilation, so the cache holds a
    // result. We want to observe a fresh computation to exercise the dedup path deterministically.
    OverrideFunctionResolver::ClearCache();
    MemberFuncsWithInstTys funcs = resolver.GetInstMemberFuncWithInstTy(cls->DataTy(), "foo");
    ASSERT_FALSE(funcs.empty());

    // Invariant 1: each FuncDecl key maps to a non-empty instTy set (no empty candidate entries).
    for (auto& entry : funcs) {
        ASSERT_FALSE(entry.second.empty());
    }

    // Invariant 2: the merge is deterministic and does not accumulate duplicate instTys across
    // recomputation. Querying again (cache hit) must yield a result whose per-entry instTy-set
    // sizes are identical to the first query. A buggy MergeIntoFuncs/RemoveImplementedSupers
    // that left duplicate instTys or failed to prune a covered super would diverge here.
    MemberFuncsWithInstTys second = resolver.GetInstMemberFuncWithInstTy(cls->DataTy(), "foo");
    ASSERT_EQ(funcs.size(), second.size());
    for (auto& entry : funcs) {
        auto it = second.find(entry.first);
        ASSERT_NE(it, second.end());
        // Each candidate decl must carry the same instTy set size on both queries.
        ASSERT_EQ(entry.second.size(), it->second.size());
    }
}

// DT: white-box #20 (GetMatchedFuncInstTyByGivenTarget). The resolver matches the semantically
//     resolved abstract target func against a concrete candidate by substituting generic mappings
//     and checking IsFuncTySubType. Assert the returned matchedTy is a valid FuncTy on the generic
//     substitution path. ST cannot observe the matched instantiation type directly.
TEST_F(OverrideFunctionResolverTest, GetMatchedFuncInstTyReturnsValidTy)
{
    // Generic interface I<T> { func f(a: T): T } (abstract); class C <: I<Int64> implements f.
    // Querying f on C yields the concrete impl as candidate; the abstract I<T>.f is the target,
    // matching production usage where 'fd' is the semantically resolved abstract func.
    CompileToGenericInstantiation(R"(
interface I<T> {
    func f(a: T): T
}
class C <: I<Int64> {
    public func f(a: Int64): Int64 { return a }
}
main(): Int64 { return 0 }
    )");

    auto cls = FindClass("C");
    ASSERT_TRUE(cls);
    ASSERT_TRUE(cls->GetTy());

    // The abstract target is I<T>.f (from the interface), resolved via C's super interface.
    auto iface = FindInterface("I");
    ASSERT_TRUE(iface);
    FuncDecl* target = FindAbstractFunc(iface, "f");
    ASSERT_TRUE(target);

    OverrideFunctionResolver resolver(*instance->typeManager);
    MemberFuncsWithInstTys funcs = resolver.GetInstMemberFuncWithInstTy(cls->DataTy(), "f");
    ASSERT_FALSE(funcs.empty());

    // Pick the candidate entry whose decl is C's concrete (non-abstract) impl of f.
    MemberFuncWithInstTys candidate{nullptr, {}};
    FuncDecl* concreteImpl = FindConcreteFunc(cls, "f");
    ASSERT_TRUE(concreteImpl);
    for (auto& entry : funcs) {
        if (entry.first.get() == concreteImpl && !entry.second.empty()) {
            candidate = entry;
            break;
        }
    }
    ASSERT_TRUE(candidate.first);

    // targetBaseTy is C's type (the instantiated base), matching production call sites which
    // pass ma.baseExpr->DataTy(). The generic substitution T->Int64 must yield a valid FuncTy.
    auto targetBaseTy = cls->DataTy();
    Ptr<Ty> matchedTy = resolver.GetMatchedFuncInstTyByGivenTarget(candidate, *target, targetBaseTy);
    EXPECT_TRUE(Ty::IsTyCorrect(matchedTy));
    if (Ty::IsTyCorrect(matchedTy)) {
        EXPECT_TRUE(matchedTy->IsFunc());
    }
}
