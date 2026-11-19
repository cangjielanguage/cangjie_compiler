// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * Sema diagnostics for @ArkTS foreign enums and foreign global variables
 * (AR-07/AR-08). The error contract lives in spec.md section 5: a removed
 * `foreign let`/`foreign var`, a misplaced or duplicated `@RawValue`, and the
 * structural enum refusals (generic, super, non-exhaustive, member, payload,
 * string-then-omitted) each have one dedicated diagnostic. Positive cases
 * verify that a well-formed `foreign enum` is lowered to a plain enum and that
 * a scalar `@ArkTS[globalVar: true] foreign func` generates a live getter.
 */

#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

#include "gtest/gtest.h"
#include "TestCompilerInstance.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Basic/DiagnosticEngine.h"

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
    explicit RecordingDiagnosticHandler(DiagnosticEngine& diag) : CompilerDiagnosticHandler(diag)
    {
    }

    void HandleDiagnose(Diagnostic& d) override
    {
        CompilerDiagnosticHandler::HandleDiagnose(d);
        if (d.diagSeverity == DiagSeverity::DS_ERROR) {
            messages.push_back(d.errorMessage);
        }
    }

    std::vector<std::string> messages;
};
} // namespace

class ArkTSEnumVarTest : public testing::Test {
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

    void CompileSema(const std::string& code)
    {
        instance->code = code;
        instance->invocation.globalOptions.implicitPrelude = true;
        instance->invocation.globalOptions.outputMode = GlobalOptions::OutputMode::SHARED_LIB;
        instance->Compile(CompileStage::SEMA);
    }

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

    bool HasErrorContaining(const std::string& needle) const
    {
        for (const auto& message : handler->messages) {
            if (message.find(needle) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

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

    // Every string literal in the generated body of @p name. Generated
    // messages are string concatenations, so one message is a sequence of
    // literals; the concatenated pieces are inspected individually.
    std::vector<std::string> BodyStringLiterals(const std::string& name) const
    {
        std::vector<std::string> literals;
        auto fn = FindFunction(name);
        if (!fn || !fn->funcBody || !fn->funcBody->body) {
            return literals;
        }
        Walker(fn->funcBody->body.get(), [&literals](Ptr<Node> node) {
            auto literal = DynamicCast<LitConstExpr*>(node);
            if (literal && literal->kind == LitConstKind::STRING) {
                literals.push_back(literal->stringValue);
            }
            return VisitAction::WALK_CHILDREN;
        }).Walk();
        return literals;
    }

    static bool AnyLiteralStartsWith(const std::vector<std::string>& literals, const std::string& prefix)
    {
        for (const auto& literal : literals) {
            if (literal.rfind(prefix, 0) == 0) {
                return true;
            }
        }
        return false;
    }

    static bool AnyLiteralContains(const std::vector<std::string>& literals, const std::string& piece)
    {
        for (const auto& literal : literals) {
            if (literal.find(piece) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    Ptr<EnumDecl> FindEnum(const std::string& name) const
    {
        Ptr<EnumDecl> found{nullptr};
        Walker walker(instance->GetSourcePackages()[0]->files[0].get(), [&found, &name](Ptr<Node> node) {
            auto candidate = DynamicCast<EnumDecl*>(node);
            if (candidate && candidate->identifier.Val() == name) {
                found = candidate;
            }
            return VisitAction::WALK_CHILDREN;
        });
        walker.Walk();
        return found;
    }

    // Whether the generated body of the wrapper named @p name contains an integer
    // literal spelled exactly @p value. The value-table content is not directly
    // observable from the test, so this inspects the emitted Float64(<value>)
    // conversions instead: a correct 0b1010 -> 10 auto-increment produces
    // Float64(11), while a strtod regression produces Float64(1).
    bool BodyContainsIntLiteral(const std::string& name, const std::string& value) const
    {
        auto fn = FindFunction(name);
        if (!fn || !fn->funcBody || !fn->funcBody->body) {
            return false;
        }
        bool found = false;
        Walker walker(fn->funcBody->body.get(), [&found, &value](Ptr<Node> node) {
            auto literal = DynamicCast<LitConstExpr*>(node);
            if (literal && literal->kind == LitConstKind::INTEGER && literal->stringValue == value) {
                found = true;
                return VisitAction::STOP_NOW;
            }
            return VisitAction::WALK_CHILDREN;
        });
        walker.Walk();
        return found;
    }

    static bool IsCallToField(Ptr<Node> node, const std::string& field)
    {
        auto call = DynamicCast<CallExpr*>(node);
        if (!call || !call->baseFunc) {
            return false;
        }
        auto member = DynamicCast<MemberAccess*>(call->baseFunc.get());
        return member != nullptr && member->field == field;
    }

    // True when a conversion call appears inside the `if (<guard>())` branch of
    // the generated enum conversion. A hoisted conversion sits outside the
    // branch and does not satisfy this.
    bool ConversionIsGuarded(const std::string& name, const std::string& guard, const std::string& conversion) const
    {
        auto fn = FindFunction(name);
        if (!fn || !fn->funcBody || !fn->funcBody->body) {
            return false;
        }
        bool found = false;
        Walker walker(fn->funcBody->body.get(), [&](Ptr<Node> node) {
            auto ifExpr = DynamicCast<IfExpr*>(node);
            if (!ifExpr || !ifExpr->condExpr || !ifExpr->thenBody || !IsCallToField(ifExpr->condExpr.get(), guard)) {
                return VisitAction::WALK_CHILDREN;
            }
            Walker inner(ifExpr->thenBody.get(), [&](Ptr<Node> innerNode) {
                if (IsCallToField(innerNode, conversion)) {
                    found = true;
                    return VisitAction::STOP_NOW;
                }
                return VisitAction::WALK_CHILDREN;
            });
            inner.Walk();
            return found ? VisitAction::STOP_NOW : VisitAction::WALK_CHILDREN;
        });
        walker.Walk();
        return found;
    }

    std::string projectPath;
    std::string savedCangjieHome;
    std::string savedCangjiePath;
    DiagnosticEngine diag;
    CompilerInvocation invocation;
    std::unique_ptr<TestCompilerInstance> instance;
    RecordingDiagnosticHandler* handler{nullptr};
};

// A `@ArkTS` foreign variable (`let` or `var`) is no longer supported; the
// design moved it to `@ArkTS[globalVar: true] foreign func`. It is rejected
// before the C FFI path lowers it as an external C variable.
TEST_F(ArkTSEnumVarTest, ForeignVariableIsDiagnosed)
{
    CompileSema(R"(package entry

@ArkTS[module: "actual"]
foreign var count: Int32
)");
    EXPECT_TRUE(HasErrorContaining("ArkTS foreign variable is not supported"));
    EXPECT_TRUE(HasErrorContaining(": count"));
}

// @RawValue on the enum type itself is misplaced: it maps a constructor,
// never the enum declaration.
TEST_F(ArkTSEnumVarTest, RawValueOnEnumTypeIsMisplaced)
{
    CompileSema(R"(package entry

@ArkTS[module: "actual"]
@RawValue[1]
foreign enum E {
    A
}
)");
    EXPECT_TRUE(HasErrorContaining("@RawValue can only annotate a constructor of a 'foreign enum'"));
    EXPECT_TRUE(HasErrorContaining(": E"));
}

// Two @RawValue annotations on one constructor are a duplicate, not a
// second mapping.
TEST_F(ArkTSEnumVarTest, RawValueDuplicateIsDiagnosed)
{
    CompileSema(R"(package entry

@ArkTS[module: "actual"]
foreign enum E {
    @RawValue[1] @RawValue[2] A
}
)");
    EXPECT_TRUE(HasErrorContaining("duplicate @RawValue annotation on enum constructor 'A'"));
}

// @RawValue on the constructor of a non-foreign enum is misplaced: the
// value mapping only exists for `foreign enum` lowered by the ArkTS path.
TEST_F(ArkTSEnumVarTest, RawValueOnNonForeignEnumConstructorIsMisplaced)
{
    CompileSema(R"(package entry

enum E {
    @RawValue[1] A
}
)");
    EXPECT_TRUE(HasErrorContaining("@RawValue can only annotate a constructor of a 'foreign enum'"));
}

// A generic `foreign enum` is not supported in phase 1.
TEST_F(ArkTSEnumVarTest, ForeignEnumGenericIsDiagnosed)
{
    CompileSema(R"(package entry

@ArkTS[module: "actual"]
foreign enum E<T> {
    A
}
)");
    EXPECT_TRUE(HasErrorContaining("cannot be generic"));
    EXPECT_TRUE(HasErrorContaining(": E"));
}

// A `foreign enum` cannot declare super interfaces in phase 1.
TEST_F(ArkTSEnumVarTest, ForeignEnumSuperIsDiagnosed)
{
    CompileSema(R"(package entry

interface I {}

@ArkTS[module: "actual"]
foreign enum E <: I {
    A
}
)");
    EXPECT_TRUE(HasErrorContaining("cannot have super interfaces"));
    EXPECT_TRUE(HasErrorContaining(": E"));
}

// A `foreign enum` must be exhaustive; an ellipsis case is rejected.
TEST_F(ArkTSEnumVarTest, ForeignEnumNonExhaustiveIsDiagnosed)
{
    CompileSema(R"(package entry

@ArkTS[module: "actual"]
foreign enum E {
    A
    | ...
}
)");
    EXPECT_TRUE(HasErrorContaining("must be exhaustive"));
    EXPECT_TRUE(HasErrorContaining(": E"));
}

// A `foreign enum` cannot declare member functions in phase 1.
TEST_F(ArkTSEnumVarTest, ForeignEnumMemberIsDiagnosed)
{
    CompileSema(R"(package entry

@ArkTS[module: "actual"]
foreign enum E {
    A
    func f(): Int32 { return 1 }
}
)");
    EXPECT_TRUE(HasErrorContaining("cannot have member functions"));
    EXPECT_TRUE(HasErrorContaining(": E"));
}

// A constructor with an associated value maps a type member, so @RawValue has
// no literal value to record.
TEST_F(ArkTSEnumVarTest, RawValueOnPayloadConstructorIsDiagnosed)
{
    CompileSema(R"(package entry

@ArkTS[module: "actual"]
foreign enum E {
    | @RawValue[1] number(Int32)
}
)");
    EXPECT_TRUE(HasErrorContaining("cannot annotate a 'foreign enum' constructor with associated values"));
    EXPECT_TRUE(HasErrorContaining(": number"));
}

// A string member disables the auto-increment counter, so an omitted value
// after it has no numeric value to continue and is rejected.
TEST_F(ArkTSEnumVarTest, ForeignEnumStringThenOmittedValueIsDiagnosed)
{
    CompileSema(R"(package entry

@ArkTS[module: "actual"]
foreign enum E {
    @RawValue["red"] A
    | B
}
)");
    EXPECT_TRUE(HasErrorContaining("value type mismatch"));
    EXPECT_TRUE(HasErrorContaining("expected String"));
}

namespace {
// Minimal ohos.ark_interop surface the generated variable getters call: module
// loading and JSValue/JSBigInt conversions.
const char* ARKTS_INTEROP_SUPPORT = R"(package ohos.ark_interop

import std.math.numeric.BigInt

public class JSValue {
    public func isNull(): Bool { return false }
    public func isUndefined(): Bool { return false }
    public func isNumber(): Bool { return false }
    public func isString(): Bool { return false }
    public func isBoolean(): Bool { return false }
    public func isArray(): Bool { return false }
    public func isBigInt(): Bool { return false }
    public func isFunction(): Bool { return false }
    public func toNumber(): Float64 { return 0.0 }
    public func toBoolean(): Bool { return false }
    public func asString(): JSString { return JSString() }
    public func asBigInt(): JSBigInt { return JSBigInt() }
    public func asArray(): JSArray { return JSArray() }
    public func asObject(): JSObject { return JSObject() }
    public func asFunction(): JSFunction { return JSFunction() }
    public func getElement(index: Int64): JSValue { return JSValue() }
    public func toJSValue(): JSValue { return this }
}

public class JSString {
    public func toString(): String { return "" }
}

public class JSBigInt {
    public func toBigInt(): BigInt { return BigInt(0) }
}

public class JSArray {
    public func getElement(index: Int64): JSValue { return JSValue() }
}

public class JSFunction {
    public func call(args: Array<JSValue>, thisArg!: JSValue): JSValue { return JSValue() }
    public func toJSValue(): JSValue { return JSValue() }
}

public class JSObject {
    public func asObject(): JSObject { return this }
    public func getProperty(key: String): JSValue { return JSValue() }
    public func setProperty(key: String, value: JSValue): Unit {}
    public func callMethod(key: String, args: Array<JSValue>): JSValue { return JSValue() }
    public func toJSValue(): JSValue { return JSValue() }
}

public class JSCallInfo {}

public type JSLambda = (JSContext, JSCallInfo) -> JSValue

public class JSModule {
    public func asObject(): JSObject { return JSObject() }
    public static func registerFunc(name: String, lambda: JSLambda): Unit {}
}

public class JSContext {
    public prop global: JSObject {
        get() { return JSObject() }
    }
    public func requireArkModule(name: String): JSModule { return JSModule() }
    public func array(arr: Array<JSValue>): JSArray { return JSArray() }
    public func number(value: Float64): JSValue { return JSValue() }
    public func string(value: String): JSValue { return JSValue() }
    public func boolean(value: Bool): JSValue { return JSValue() }
    public func bigint(value: BigInt): JSValue { return JSValue() }
    public func undefined(): JSValue { return JSValue() }
}
)";

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
)";

} // namespace

// A scalar `@ArkTS[globalVar: true] foreign func` lowers to a getter that
// fetches the live ArkTS value; the generated body must survive full Sema.
TEST_F(ArkTSEnumVarTest, GlobalVarScalarGeneratesLiveGetter)
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

@ArkTS[module: "actual", globalVar: true]
foreign func count(): Int32
)");
    ASSERT_EQ(diag.GetErrorCount(), 0) << (handler->messages.empty() ? "" : handler->messages.front());
    auto getter = FindFunction("count");
    ASSERT_NE(getter, nullptr);
    EXPECT_NE(getter->funcBody->body, nullptr);
}

// A globalVar getter resolves every namespace segment through a guarded
// property read and converts the final value through the scalar conversion.
// A missing segment reports the variable, the module and the failing path,
// and the conversion failures use the two documented message prefixes.
TEST_F(ArkTSEnumVarTest, GlobalVarNamespaceGetterKeepsTheRuntimeFailureShape)
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

// A same-named package declaration must not capture the runtime support the
// generated getter throws.
class ForeignRuntimeException <: Exception {
    public init(message: String) {
        super(message)
    }
}

@ArkTS[module: "actual", namespace: "outer.inner", globalVar: true]
foreign func count(): Int32
)");
    ASSERT_EQ(diag.GetErrorCount(), 0) << (handler->messages.empty() ? "" : handler->messages.front());
    auto getter = FindFunction("count");
    ASSERT_NE(getter, nullptr);
    ASSERT_NE(getter->funcBody->body, nullptr);
    auto literals = BodyStringLiterals("count");
    // The namespace guard names the module and the failing path prefix.
    EXPECT_TRUE(AnyLiteralContains(literals, "module '"));
    EXPECT_TRUE(AnyLiteralContains(literals, " has no namespace '"));
    EXPECT_TRUE(AnyLiteralContains(literals, "outer"));
    EXPECT_TRUE(AnyLiteralContains(literals, "inner"));
    // Both conversion categories are emitted for a scalar target.
    EXPECT_TRUE(AnyLiteralStartsWith(literals, "ArkTS type mismatch: "));
    EXPECT_TRUE(AnyLiteralStartsWith(literals, "ArkTS value invalid: "));
    // Two guarded namespace reads plus the final value read.
    size_t propertyReads = 0;
    Walker(getter->funcBody->body.get(), [&propertyReads](Ptr<Node> node) {
        auto access = DynamicCast<MemberAccess*>(node);
        if (access && access->field == "getProperty") {
            ++propertyReads;
        }
        return VisitAction::WALK_CHILDREN;
    })
        .Walk();
    EXPECT_EQ(propertyReads, 3);
    // The guards throw the public runtime exception visible through the
    // facade, never the same-named package declaration; the conversion
    // failures throw the conversion exception.
    size_t runtimeThrows = 0;
    size_t conversionThrows = 0;
    Walker(getter->funcBody->body.get(), [&](Ptr<Node> node) {
        auto ref = DynamicCast<RefExpr*>(node);
        if (!ref) {
            return VisitAction::WALK_CHILDREN;
        }
        const auto& name = ref->ref.identifier.Val();
        if (name == "ForeignRuntimeException") {
            ++runtimeThrows;
            EXPECT_NE(ref->ref.target, nullptr);
            if (ref->ref.target) {
                EXPECT_EQ(ref->ref.target->fullPackageName, "cj_arkts_context_v1");
            }
        } else if (name == "ExternConversionException") {
            ++conversionThrows;
            EXPECT_NE(ref->ref.target, nullptr);
            if (ref->ref.target) {
                EXPECT_EQ(ref->ref.target->fullPackageName, "arkts.lang");
            }
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    EXPECT_EQ(runtimeThrows, 2);
    EXPECT_GT(conversionThrows, 0);
}

// A `@ArkTS[globalVar: true] foreign func` of a non-scalar type (a class) is
// reported through the general foreign return-type diagnostic.
TEST_F(ArkTSEnumVarTest, GlobalVarUnsupportedTypeIsDiagnosed)
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

class Box {}

@ArkTS[module: "actual", globalVar: true]
foreign func box(): Box
)");
    EXPECT_TRUE(HasErrorContaining("return type 'Class-Box' of @ArkTS foreign function 'box' is not supported"));
    EXPECT_FALSE(HasErrorContaining("variable type is not supported"));
}

// A well-formed `foreign enum` (implicit numbering, explicit numeric and string
// values, mixed) is lowered to a plain enum with zero errors; the explicit
// members keep their @RawValue while the implicit member has none.
TEST_F(ArkTSEnumVarTest, ForeignEnumValueRulesLowerCleanly)
{
    CompileSema(R"(package entry

@ArkTS[module: "actual"]
foreign enum Color {
    @RawValue[10] Red
    | Green
    | @RawValue["blue"] Blue
}
)");
    ASSERT_EQ(diag.GetErrorCount(), 0) << (handler->messages.empty() ? "" : handler->messages.front());
    auto ed = FindEnum("Color");
    ASSERT_NE(ed, nullptr);
    EXPECT_TRUE(ed->TestAttr(Attribute::ARKTS_LOWERED));
    EXPECT_FALSE(ed->TestAttr(Attribute::FOREIGN));
    ASSERT_EQ(ed->constructors.size(), 3);
    EXPECT_TRUE(ed->constructors[0]->HasAnno(AnnotationKind::RAW_VALUE));
    EXPECT_FALSE(ed->constructors[1]->HasAnno(AnnotationKind::RAW_VALUE));
    EXPECT_TRUE(ed->constructors[2]->HasAnno(AnnotationKind::RAW_VALUE));
}

// AR-08: an @ArkTS foreign enum used as a parameter and/or return type maps
// each constructor through @RawValue (numeric or string) and lowers to a
// live wrapper. Numeric, string and mixed enums each survive full Sema, which
// proves the generated match/guard conversion parses and type-checks.
TEST_F(ArkTSEnumVarTest, ForeignEnumFunctionMapsValuesAcrossTheBoundary)
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

@ArkTS[module: "actual", foreignName: "Sequential"]
foreign enum Sequential { | A | B | C }

@ArkTS[module: "actual", foreignName: "Color"]
foreign enum Color { | @RawValue["red"] R | @RawValue["green"] G }

@ArkTS[module: "actual", foreignName: "Labeled"]
foreign enum Labeled { | @RawValue[10] X | @RawValue["ten"] Y }

@ArkTS[module: "actual", foreignName: "Mixed3"]
foreign enum Mixed3 { | @RawValue[10] X | @RawValue["ten"] Y | @RawValue[true] Z }

@ArkTS[module: "actual", foreignName: "enumNumber"]
foreign func echoNumeric(value: Sequential): Sequential

@ArkTS[module: "actual", foreignName: "enumString"]
foreign func echoString(value: Color): Color

@ArkTS[module: "actual", foreignName: "enumMixed"]
foreign func echoMixed(value: Labeled): Labeled

@ArkTS[module: "actual", foreignName: "enumMixed3"]
foreign func echoMixed3(value: Mixed3): Mixed3
)");
    ASSERT_EQ(diag.GetErrorCount(), 0) << (handler->messages.empty() ? "" : handler->messages.front());
    auto numericFn = FindFunction("echoNumeric");
    ASSERT_NE(numericFn, nullptr);
    EXPECT_NE(numericFn->funcBody->body, nullptr);
    auto stringFn = FindFunction("echoString");
    ASSERT_NE(stringFn, nullptr);
    EXPECT_NE(stringFn->funcBody->body, nullptr);
    auto mixedFn = FindFunction("echoMixed");
    ASSERT_NE(mixedFn, nullptr);
    EXPECT_NE(mixedFn->funcBody->body, nullptr);
    auto mixed3Fn = FindFunction("echoMixed3");
    ASSERT_NE(mixed3Fn, nullptr);
    EXPECT_NE(mixed3Fn->funcBody->body, nullptr);
    // The conversion methods assert the runtime kind, so each one must be
    // evaluated only inside its kind branch.
    EXPECT_TRUE(ConversionIsGuarded("echoMixed3", "isNumber", "toNumber"));
    EXPECT_TRUE(ConversionIsGuarded("echoMixed3", "isString", "asString"));
    EXPECT_TRUE(ConversionIsGuarded("echoMixed3", "isBoolean", "toBoolean"));
}

// A union-mode `foreign enum` constructor carries one associated value and
// lowers to a conversion wrapper.
TEST_F(ArkTSEnumVarTest, ForeignEnumPayloadLowersCleanly)
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
foreign enum U {
    | @RawValue["a"] A
    | number(Float64)
}

@ArkTS[module: "actual"]
foreign func echo(value: U): U
)");
    ASSERT_EQ(diag.GetErrorCount(), 0) << (handler->messages.empty() ? "" : handler->messages.front());
    auto ed = FindEnum("U");
    ASSERT_NE(ed, nullptr);
    EXPECT_TRUE(ed->TestAttr(Attribute::ARKTS_LOWERED));
    auto fn = FindFunction("echo");
    ASSERT_NE(fn, nullptr);
    EXPECT_NE(fn->funcBody->body, nullptr);
}

// A payload type that refers back to the enum is refused at the boundary
// instead of rebuilding the value table recursively.
TEST_F(ArkTSEnumVarTest, RecursivePayloadEnumIsRefusedAtTheBoundary)
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
foreign enum E {
    | A(E)
    | B
}

@ArkTS[module: "actual"]
foreign func f(value: E): E
)");
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_TRUE(HasErrorContaining("not supported"));
}

// A payload constructor carries exactly one associated value; a different
// arity is rejected before conversion generation.
TEST_F(ArkTSEnumVarTest, PayloadArityIsDiagnosed)
{
    CompileSema(R"(package entry

@ArkTS[module: "actual"]
foreign enum E {
    | A(Int32, Int32)
}
)");
    EXPECT_TRUE(HasErrorContaining("must have exactly one associated value"));
    EXPECT_TRUE(HasErrorContaining("'A'"));
}

// A payload whose routing cannot be narrowed is the final catch-all; it must not
// leave an unreachable branch in the generated conversion.
TEST_F(ArkTSEnumVarTest, OptionPayloadEnumLowersWithoutUnreachableBranch)
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
foreign enum U {
    | A(Option<String>)
    | B
}

@ArkTS[module: "actual"]
foreign func echo(value: U): U
)");
    ASSERT_EQ(diag.GetErrorCount(), 0) << (handler->messages.empty() ? "" : handler->messages.front());
    EXPECT_TRUE(std::none_of(handler->messages.begin(), handler->messages.end(),
        [](const std::string& message) { return message.find("unreachable") != std::string::npos; }));
}

// AR-08: a string member whose value contains `${` must be re-escaped before it
// is spliced into generated code, otherwise the generated string triggers Cangjie
// interpolation and the mapping value drifts (or the wrapper fails to parse).
// The `\$` in the source is normalized to a literal `${` and recovered back.
TEST_F(ArkTSEnumVarTest, ForeignEnumStringMemberWithInterpolationEscapesCleanly)
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
foreign enum Template {
    @RawValue["a\${x}b"] A
}

@ArkTS[module: "actual"]
foreign func echo(value: Template): Template
)");
    ASSERT_EQ(diag.GetErrorCount(), 0) << (handler->messages.empty() ? "" : handler->messages.front());
    auto fn = FindFunction("echo");
    ASSERT_NE(fn, nullptr);
    EXPECT_NE(fn->funcBody->body, nullptr);
}

// AR-08: a non-decimal (0b/0o/0x) or underscore-separated numeric spelling must
// be parsed by its base for the auto-increment chain, so an omitted member after
// it continues from the exact value (0b1010 -> 10, next -> 11), not from a
// strtod prefix parse (0b1010 -> 0, next -> 1).
TEST_F(ArkTSEnumVarTest, ForeignEnumNonDecimalSpellingLowersCleanly)
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
foreign enum Binary {
    @RawValue[0b1010] A
    | B
}

@ArkTS[module: "actual"]
foreign enum WithUnderscores {
    @RawValue[1_000] A
    | B
}

@ArkTS[module: "actual"]
foreign func echo(value: Binary): Binary

@ArkTS[module: "actual"]
foreign func echoUnderscore(value: WithUnderscores): WithUnderscores
)");
    ASSERT_EQ(diag.GetErrorCount(), 0) << (handler->messages.empty() ? "" : handler->messages.front());
    auto binaryFn = FindFunction("echo");
    ASSERT_NE(binaryFn, nullptr);
    EXPECT_NE(binaryFn->funcBody->body, nullptr);
    // The value table must hold the exact base-parsed values, not a strtod
    // prefix parse: 0b1010 -> 10 and the omitted B -> 11 (never 0 -> 1).
    EXPECT_TRUE(BodyContainsIntLiteral("echo", "10"));
    EXPECT_TRUE(BodyContainsIntLiteral("echo", "11"));
    auto underscoreFn = FindFunction("echoUnderscore");
    ASSERT_NE(underscoreFn, nullptr);
    EXPECT_NE(underscoreFn->funcBody->body, nullptr);
    EXPECT_TRUE(BodyContainsIntLiteral("echoUnderscore", "1000"));
    EXPECT_TRUE(BodyContainsIntLiteral("echoUnderscore", "1001"));
}

// AR-08: a legal integer type suffix (5i64) is accepted and ignored, so the
// value mapping does not refuse a well-formed spelling as "out of range".
TEST_F(ArkTSEnumVarTest, ForeignEnumIntegerTypeSuffixLowersCleanly)
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
foreign enum E {
    @RawValue[5i64] A
    | B
}

@ArkTS[module: "actual"]
foreign func echo(value: E): E
)");
    ASSERT_EQ(diag.GetErrorCount(), 0) << (handler->messages.empty() ? "" : handler->messages.front());
    auto fn = FindFunction("echo");
    ASSERT_NE(fn, nullptr);
    EXPECT_NE(fn->funcBody->body, nullptr);
    EXPECT_TRUE(BodyContainsIntLiteral("echo", "5"));
    EXPECT_TRUE(BodyContainsIntLiteral("echo", "6"));
}

// AR-08: the Int64-minimum negative value (-2^63) is exactly representable and
// must lower cleanly; a value below it is rejected by the integer literal range
// check, so this only pins the representable boundary.
TEST_F(ArkTSEnumVarTest, ForeignEnumNegativeBoundaryLowersCleanly)
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
foreign enum E {
    @RawValue[-9223372036854775808] A
    | B
}

@ArkTS[module: "actual"]
foreign func echo(value: E): E
)");
    ASSERT_EQ(diag.GetErrorCount(), 0) << (handler->messages.empty() ? "" : handler->messages.front());
    auto fn = FindFunction("echo");
    ASSERT_NE(fn, nullptr);
    EXPECT_NE(fn->funcBody->body, nullptr);
}

// A `foreign enum` without @ArkTS is not an ArkTS interop declaration and must
// be rejected like a bare foreign class/interface instead of silently passing.
TEST_F(ArkTSEnumVarTest, ForeignEnumWithoutArkTSIsDiagnosed)
{
    CompileSema(R"(package entry

foreign enum E {
    A
}
)");
    EXPECT_TRUE(HasErrorContaining("unexpected enum declaration"));
}

// A foreign enum's value mapping crosses a package boundary through the
// serialized @RawValue annotation; the importing package rebuilds the table.
TEST_F(ArkTSEnumVarTest, ForeignEnumFromAnotherPackageLowersCleanly)
{
    diag.ClearError();
    auto depCjo = ExportSupportPackage(R"(package dep

@ArkTS[module: "actual"]
public foreign enum E {
    | @RawValue["a"] A
    | @RawValue[7] B
    | number(Float64)
}
)");
    auto interopCjo = ExportSupportPackage(ARKTS_INTEROP_SUPPORT);
    auto contextCjo = ExportSupportPackage(ARKTS_CONTEXT_SUPPORT, {{"ohos.ark_interop", interopCjo}});
    auto langCjo = ExportSupportPackage(
        ARKTS_LANG_SUPPORT, {{"ohos.ark_interop", interopCjo}, {"cj_arkts_context_v1", contextCjo}});

    diag.ClearError();
    handler->messages.clear();
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->importManager->SetPackageCjoCache("dep", depCjo);
    instance->importManager->SetPackageCjoCache("ohos.ark_interop", interopCjo);
    instance->importManager->SetPackageCjoCache("cj_arkts_context_v1", contextCjo);
    instance->importManager->SetPackageCjoCache("arkts.lang", langCjo);
    CompileSema(R"(package entry

import arkts.lang.*
import dep.E

@ArkTS[module: "actual"]
foreign func f(value: E): E
)");
    ASSERT_EQ(diag.GetErrorCount(), 0) << (handler->messages.empty() ? "" : handler->messages.front());
    auto fn = FindFunction("f");
    ASSERT_NE(fn, nullptr);
    EXPECT_NE(fn->funcBody->body, nullptr);
}
