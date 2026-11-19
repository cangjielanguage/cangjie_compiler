// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * Unit tests for ConditionalCompilationImpl private methods.
 *
 * These tests construct AST nodes directly to exercise the branches that are
 * hard to reach through source files, e.g. invalid intermediate expressions,
 * unsupported operators and invalid condition values.
 */

#include <regex>
#include <string>

#include "gtest/gtest.h"

// White-box hack: expose private members of ConditionalCompilationImpl and AST
// nodes so the tests can construct AST directly and call private methods.
// Must be defined AFTER including gtest and BEFORE including the headers under
// test. Do NOT remove this guard; keep the #undef below to contain its scope.
#define private public

#include "ConditionalCompilationImpl.h"
#include "cangjie/AST/Node.h"
#include "TestCompilerInstance.h"

#undef private

using namespace Cangjie;
using namespace Cangjie::AST;

namespace {

void SetPos(Node& node, int line, int beginCol, int length)
{
    node.begin = {0, line, beginCol};
    node.end = {0, line, beginCol + length};
}

void SetBinarySpan(BinaryExpr& be)
{
    if (be.leftExpr) {
        be.begin = be.leftExpr->begin;
    }
    if (be.rightExpr) {
        be.end = be.rightExpr->end;
    } else if (be.leftExpr) {
        be.end = be.leftExpr->end;
    }
}

void SetUnarySpan(UnaryExpr& ue, int opCol)
{
    ue.begin = {0, ue.expr ? ue.expr->begin.line : 1, opCol};
    if (ue.expr) {
        ue.end = ue.expr->end;
    } else {
        ue.end = {0, ue.begin.line, opCol + 1};
    }
}

OwnedPtr<RefExpr> MakeRef(const std::string& name, int line = 1, int beginCol = 1)
{
    auto ref = MakeOwned<RefExpr>();
    SetPos(*ref, line, beginCol, static_cast<int>(name.size()));
    ref->ref = Reference(name);
    return ref;
}

OwnedPtr<LitConstExpr> MakeStringLit(const std::string& value, int line = 1, int beginCol = 1)
{
    auto lit = MakeOwned<LitConstExpr>(LitConstKind::STRING, value);
    SetPos(*lit, line, beginCol, static_cast<int>(value.size()));
    return lit;
}

class ConditionalCompilationImplTest : public ::testing::Test {
protected:
    void SetUp() override
    {
    }

    DiagnosticEngine diag;
    CompilerInvocation invocation;
    std::unique_ptr<TestCompilerInstance> instance;
    std::unique_ptr<ConditionalCompilationImpl> impl;

    void ResetImpl()
    {
        instance = std::make_unique<TestCompilerInstance>(invocation, diag);
        impl = std::make_unique<ConditionalCompilationImpl>(instance.get());
    }
};

/**
 * --- cjc_version literal format ---
 * Version parsing itself is shared with the compiler version parser (see Version.h). The stricter
 * x.y.z / [0-99] rule of the cfg language is enforced by ConditionCheck.
 */
TEST_F(ConditionalCompilationImplTest, CjcVersionConditionCheck_InvalidFormat)
{
    ResetImpl();
    EXPECT_FALSE(impl->ConditionCheck("cjc_version", DEFAULT_POSITION, "x.y.z"));
    EXPECT_FALSE(impl->ConditionCheck("cjc_version", DEFAULT_POSITION, "1.2"));
    EXPECT_FALSE(impl->ConditionCheck("cjc_version", DEFAULT_POSITION, ""));
    EXPECT_FALSE(impl->ConditionCheck("cjc_version", DEFAULT_POSITION, "1.2.x"));
    // Parts must be in [0, 99].
    EXPECT_FALSE(impl->ConditionCheck("cjc_version", DEFAULT_POSITION, "100.2.3"));
    EXPECT_FALSE(impl->ConditionCheck("cjc_version", DEFAULT_POSITION, "1.100.3"));
    EXPECT_FALSE(impl->ConditionCheck("cjc_version", DEFAULT_POSITION, "1.2.100"));
}

TEST_F(ConditionalCompilationImplTest, CjcVersionConditionCheck_ValidFormat)
{
    ResetImpl();
    EXPECT_TRUE(impl->ConditionCheck("cjc_version", DEFAULT_POSITION, "1.2.3"));
    EXPECT_TRUE(impl->ConditionCheck("cjc_version", DEFAULT_POSITION, "99.99.99"));
}

/**
 * --- CompareVersion ---
 * Covers all comparisons: equal, major/minor/patch difference in both orders.
 */
TEST_F(ConditionalCompilationImplTest, CompareVersion_Branches)
{
    ResetImpl();
    VersionInfo base{1, 2, 3, true};

    EXPECT_EQ(impl->CompareVersion(base, base), 0);

    VersionInfo majorUp{2, 0, 0, true};
    EXPECT_EQ(impl->CompareVersion(base, majorUp), -1);
    EXPECT_EQ(impl->CompareVersion(majorUp, base), 1);

    VersionInfo minorUp{1, 3, 0, true};
    EXPECT_EQ(impl->CompareVersion(base, minorUp), -1);
    EXPECT_EQ(impl->CompareVersion(minorUp, base), 1);

    VersionInfo patchUp{1, 2, 4, true};
    EXPECT_EQ(impl->CompareVersion(base, patchUp), -1);
    EXPECT_EQ(impl->CompareVersion(patchUp, base), 1);
}

/**
 * --- EvalVersion ---
 * All six operators.
 */
TEST_F(ConditionalCompilationImplTest, EvalVersion_Operators)
{
    ResetImpl();
    VersionInfo v{1, 2, 3, true};
    VersionInfo low{1, 2, 2, true};
    VersionInfo high{1, 2, 4, true};

    BinaryExpr be;
    be.op = TokenKind::EQUAL;
    EXPECT_TRUE(impl->EvalVersion(be, v, v));
    EXPECT_FALSE(impl->EvalVersion(be, v, low));

    be.op = TokenKind::NOTEQ;
    EXPECT_FALSE(impl->EvalVersion(be, v, v));
    EXPECT_TRUE(impl->EvalVersion(be, v, low));

    be.op = TokenKind::GT;
    EXPECT_TRUE(impl->EvalVersion(be, v, low));
    EXPECT_FALSE(impl->EvalVersion(be, v, high));

    be.op = TokenKind::LT;
    EXPECT_TRUE(impl->EvalVersion(be, v, high));
    EXPECT_FALSE(impl->EvalVersion(be, v, low));

    be.op = TokenKind::GE;
    EXPECT_TRUE(impl->EvalVersion(be, v, v));
    EXPECT_TRUE(impl->EvalVersion(be, v, low));
    EXPECT_FALSE(impl->EvalVersion(be, v, high));

    be.op = TokenKind::LE;
    EXPECT_TRUE(impl->EvalVersion(be, v, v));
    EXPECT_TRUE(impl->EvalVersion(be, v, high));
    EXPECT_FALSE(impl->EvalVersion(be, v, low));

    be.op = TokenKind::ILLEGAL; // default branch: false
    EXPECT_FALSE(impl->EvalVersion(be, v, v));
}

/**
 * --- Eval (raw string operators) ---
 * Covers GT/LT/GE/LE and the default(LE) branch of Eval.
 */
TEST_F(ConditionalCompilationImplTest, Eval_StringOperators)
{
    ResetImpl();
    BinaryExpr be;
    be.op = TokenKind::GT;
    EXPECT_TRUE(impl->Eval(be, "b", "a"));
    EXPECT_FALSE(impl->Eval(be, "a", "b"));

    be.op = TokenKind::LT;
    EXPECT_TRUE(impl->Eval(be, "a", "b"));
    EXPECT_FALSE(impl->Eval(be, "b", "a"));

    be.op = TokenKind::GE;
    EXPECT_TRUE(impl->Eval(be, "a", "a"));
    EXPECT_TRUE(impl->Eval(be, "b", "a"));
    EXPECT_FALSE(impl->Eval(be, "a", "b"));

    be.op = TokenKind::LE;
    EXPECT_TRUE(impl->Eval(be, "a", "a"));
    EXPECT_TRUE(impl->Eval(be, "a", "b"));
    EXPECT_FALSE(impl->Eval(be, "b", "a"));

    be.op = TokenKind::ILLEGAL; // default: LE
    EXPECT_TRUE(impl->Eval(be, "a", "a"));
}

/**
 * --- GetOSType ---
 */
TEST_F(ConditionalCompilationImplTest, GetOSType_EachOs)
{
    invocation.globalOptions.target.os = Triple::OSType::DARWIN;
    ResetImpl();
    EXPECT_EQ(impl->GetOSType(), "macOS");

    invocation.globalOptions.target.os = Triple::OSType::IOS;
    ResetImpl();
    EXPECT_EQ(impl->GetOSType(), "iOS");

    invocation.globalOptions.target.os = Triple::OSType::WINDOWS;
    ResetImpl();
    EXPECT_EQ(impl->GetOSType(), "Windows");

    invocation.globalOptions.target.os = Triple::OSType::UNKNOWN;
    ResetImpl();
    EXPECT_NE(impl->GetOSType(), "Linux"); // fallback to OSToString()
}

/**
 * --- GetUserDefinedInfoByName ---
 */
TEST_F(ConditionalCompilationImplTest, GetUserDefinedInfoByName)
{
    ResetImpl();
    invocation.globalOptions.passedWhenKeyValue.insert({"myvar", "X"});
    impl = std::make_unique<ConditionalCompilationImpl>(instance.get());

    // present -> value returned.
    auto v = impl->GetUserDefinedInfoByName("myvar");
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v.value(), "X");

    // missing -> nullopt.
    EXPECT_FALSE(impl->GetUserDefinedInfoByName("not_exists").has_value());
}

/**
 * --- EvalJudgeBinaryExpr error branches ---
 */
TEST_F(ConditionalCompilationImplTest, EvalJudgeBinaryExpr_NullOperands)
{
    ResetImpl();
    BinaryExpr be;
    SetPos(be, 1, 1, 2);
    be.op = TokenKind::EQUAL;
    EXPECT_FALSE(impl->EvalJudgeBinaryExpr(be));
    EXPECT_GT(diag.GetErrorCount(), 0);
}

TEST_F(ConditionalCompilationImplTest, EvalJudgeBinaryExpr_NonRefLeft)
{
    ResetImpl();
    BinaryExpr be;
    be.op = TokenKind::EQUAL;
    be.leftExpr = MakeStringLit("Linux", 1, 1);
    be.rightExpr = MakeStringLit("Linux", 1, 12);
    SetBinarySpan(be);
    EXPECT_FALSE(impl->EvalJudgeBinaryExpr(be));
    EXPECT_GT(diag.GetErrorCount(), 0);
}

TEST_F(ConditionalCompilationImplTest, EvalJudgeBinaryExpr_NonLitRight)
{
    ResetImpl();
    BinaryExpr be;
    be.op = TokenKind::EQUAL;
    be.leftExpr = MakeRef("os", 1, 1);
    be.rightExpr = OwnedPtr<Expr>(MakeRef("Linux", 1, 7).release());
    SetBinarySpan(be);
    EXPECT_FALSE(impl->EvalJudgeBinaryExpr(be));
    EXPECT_GT(diag.GetErrorCount(), 0);
}

TEST_F(ConditionalCompilationImplTest, EvalJudgeBinaryExpr_NonStringLiteral)
{
    ResetImpl();
    BinaryExpr be;
    be.op = TokenKind::EQUAL;
    be.leftExpr = MakeRef("os", 1, 1);
    auto lit = MakeOwned<LitConstExpr>(LitConstKind::INTEGER, "1");
    SetPos(*lit, 1, 7, 1);
    be.rightExpr = OwnedPtr<Expr>(lit.release());
    SetBinarySpan(be);
    EXPECT_FALSE(impl->EvalJudgeBinaryExpr(be));
    EXPECT_GT(diag.GetErrorCount(), 0);
}

TEST_F(ConditionalCompilationImplTest, EvalJudgeBinaryExpr_SiExpr)
{
    ResetImpl();
    BinaryExpr be;
    be.op = TokenKind::EQUAL;
    be.leftExpr = MakeRef("os", 1, 1);
    auto lit = MakeOwned<LitConstExpr>(LitConstKind::STRING, "Linux");
    SetPos(*lit, 1, 7, 7);
    lit->siExpr = MakeOwned<StrInterpolationExpr>();
    be.rightExpr = OwnedPtr<Expr>(lit.release());
    SetBinarySpan(be);
    EXPECT_FALSE(impl->EvalJudgeBinaryExpr(be));
    EXPECT_GT(diag.GetErrorCount(), 0);
}

TEST_F(ConditionalCompilationImplTest, EvalJudgeBinaryExpr_UnknownCondition)
{
    ResetImpl();
    BinaryExpr be;
    be.op = TokenKind::EQUAL;
    be.leftExpr = MakeRef("not_exists", 1, 1);
    be.rightExpr = MakeStringLit("v", 1, 15);
    SetBinarySpan(be);
    EXPECT_FALSE(impl->EvalJudgeBinaryExpr(be));
    EXPECT_GT(diag.GetErrorCount(), 0);
}

TEST_F(ConditionalCompilationImplTest, EvalJudgeBinaryExpr_UnsupportedOp)
{
    ResetImpl();
    // <, >, <=, >= are unsupported for os.
    BinaryExpr be;
    be.op = TokenKind::LT;
    be.leftExpr = MakeRef("os", 1, 1);
    be.rightExpr = MakeStringLit("Linux", 1, 6);
    SetBinarySpan(be);
    EXPECT_FALSE(impl->EvalJudgeBinaryExpr(be));
    EXPECT_GT(diag.GetErrorCount(), 0);
}

TEST_F(ConditionalCompilationImplTest, EvalJudgeBinaryExpr_OsEqual)
{
    invocation.globalOptions.target.os = Triple::OSType::LINUX;
    ResetImpl();
    BinaryExpr be;
    be.op = TokenKind::EQUAL;
    be.leftExpr = MakeRef("os", 1, 1);
    be.rightExpr = MakeStringLit("Linux", 1, 7);
    SetBinarySpan(be);
    EXPECT_TRUE(impl->EvalJudgeBinaryExpr(be));
    EXPECT_EQ(diag.GetErrorCount(), 0);
}

TEST_F(ConditionalCompilationImplTest, EvalJudgeBinaryExpr_EnvUserCondition)
{
    ResetImpl();
    invocation.globalOptions.passedWhenKeyValue.insert({"VAR", "X"});
    impl = std::make_unique<ConditionalCompilationImpl>(instance.get());
    BinaryExpr be;
    be.op = TokenKind::EQUAL;
    be.leftExpr = MakeRef("VAR", 1, 1);
    be.rightExpr = MakeStringLit("X", 1, 8);
    SetBinarySpan(be);
    EXPECT_TRUE(impl->EvalJudgeBinaryExpr(be));
}

TEST_F(ConditionalCompilationImplTest, EvalJudgeBinaryExpr_CjcVersionBadValue)
{
    ResetImpl();
    BinaryExpr be;
    be.op = TokenKind::EQUAL;
    be.leftExpr = MakeRef("cjc_version", 1, 1);
    be.rightExpr = MakeStringLit("not-a-version", 1, 16);
    SetBinarySpan(be);
    EXPECT_FALSE(impl->EvalJudgeBinaryExpr(be));
    EXPECT_GT(diag.GetErrorCount(), 0);
}

TEST_F(ConditionalCompilationImplTest, EvalJudgeBinaryExpr_CjcVersionValid)
{
    ResetImpl();
    BinaryExpr be;
    be.op = TokenKind::GT;
    be.leftExpr = MakeRef("cjc_version", 1, 1);
    be.rightExpr = MakeStringLit("0.0.0", 1, 15);
    SetBinarySpan(be);
    EXPECT_TRUE(impl->EvalJudgeBinaryExpr(be));
    EXPECT_EQ(diag.GetErrorCount(), 0);
}

/**
 * --- ConditionCheck ---
 */
TEST_F(ConditionalCompilationImplTest, ConditionCheck_NotSupported)
{
    ResetImpl();
    EXPECT_FALSE(impl->ConditionCheck("fake", DEFAULT_POSITION, "v"));
    EXPECT_GT(diag.GetErrorCount(), 0);
}

TEST_F(ConditionalCompilationImplTest, ConditionCheck_BuiltinValueNotSupported)
{
    ResetImpl();
    EXPECT_FALSE(impl->ConditionCheck("os", DEFAULT_POSITION, "FreeBSD"));
    EXPECT_GT(diag.GetErrorCount(), 0);
}

TEST_F(ConditionalCompilationImplTest, ConditionCheck_BadCjcVersionFormat)
{
    ResetImpl();
    EXPECT_FALSE(impl->ConditionCheck("cjc_version", DEFAULT_POSITION, "1.2"));
    EXPECT_FALSE(impl->ConditionCheck("cjc_version", DEFAULT_POSITION, "x.y.z"));
    EXPECT_GT(diag.GetErrorCount(), 0);
}

TEST_F(ConditionalCompilationImplTest, ConditionCheck_Valid)
{
    ResetImpl();
    EXPECT_TRUE(impl->ConditionCheck("os", DEFAULT_POSITION, "Linux"));
    EXPECT_TRUE(impl->ConditionCheck("debug", DEFAULT_POSITION, "1"));
    EXPECT_TRUE(impl->ConditionCheck("env", DEFAULT_POSITION, "gnu"));
}

/**
 * --- EvalUnaryExpr ---
 */
TEST_F(ConditionalCompilationImplTest, EvalUnaryExpr_NullExpr)
{
    ResetImpl();
    UnaryExpr ue;
    SetPos(ue, 1, 1, 1);
    ue.op = TokenKind::NOT;
    EXPECT_FALSE(impl->EvalUnaryExpr(ue));
    EXPECT_GT(diag.GetErrorCount(), 0);
}

TEST_F(ConditionalCompilationImplTest, EvalUnaryExpr_NonRef)
{
    ResetImpl();
    UnaryExpr ue;
    ue.op = TokenKind::NOT;
    ue.expr = OwnedPtr<Expr>(MakeStringLit("x", 1, 2).release());
    SetUnarySpan(ue, 1);
    EXPECT_FALSE(impl->EvalUnaryExpr(ue));
    EXPECT_GT(diag.GetErrorCount(), 0);
}

TEST_F(ConditionalCompilationImplTest, EvalUnaryExpr_NotDebugOrTest)
{
    ResetImpl();
    UnaryExpr ue;
    ue.op = TokenKind::NOT;
    ue.expr = OwnedPtr<Expr>(MakeRef("os", 1, 2).release());
    SetUnarySpan(ue, 1);
    EXPECT_FALSE(impl->EvalUnaryExpr(ue));
    EXPECT_GT(diag.GetErrorCount(), 0);
}

TEST_F(ConditionalCompilationImplTest, EvalUnaryExpr_NonNotOperator)
{
    ResetImpl();
    UnaryExpr ue;
    ue.op = TokenKind::BITNOT;
    ue.expr = OwnedPtr<Expr>(MakeRef("debug", 1, 2).release());
    SetUnarySpan(ue, 1);
    EXPECT_FALSE(impl->EvalUnaryExpr(ue));
    EXPECT_GT(diag.GetErrorCount(), 0);
}

TEST_F(ConditionalCompilationImplTest, EvalUnaryExpr_Debug)
{
    ResetImpl();
    UnaryExpr ue;
    ue.op = TokenKind::NOT;
    ue.expr = OwnedPtr<Expr>(MakeRef("debug", 1, 2).release());
    SetUnarySpan(ue, 1);
    // debug is false by default -> !debug == true
    EXPECT_TRUE(impl->EvalUnaryExpr(ue));
}

TEST_F(ConditionalCompilationImplTest, EvalUnaryExpr_Test)
{
    ResetImpl();
    UnaryExpr ue;
    ue.op = TokenKind::NOT;
    ue.expr = OwnedPtr<Expr>(MakeRef("test", 1, 2).release());
    SetUnarySpan(ue, 1);
    EXPECT_TRUE(impl->EvalUnaryExpr(ue));
}

/**
 * --- EvalRefExpr ---
 */
TEST_F(ConditionalCompilationImplTest, EvalRefExpr_NotDebugOrTest)
{
    ResetImpl();
    RefExpr re;
    SetPos(re, 1, 1, 2);
    re.ref = Reference("os");
    EXPECT_FALSE(impl->EvalRefExpr(re));
    EXPECT_GT(diag.GetErrorCount(), 0);
}

TEST_F(ConditionalCompilationImplTest, EvalRefExpr_Debug)
{
    ResetImpl();
    RefExpr re;
    SetPos(re, 1, 1, 5);
    re.ref = Reference("debug");
    // debug false -> returns false
    EXPECT_FALSE(impl->EvalRefExpr(re));

    // Use enableCompileDebug to make it true.
    invocation.globalOptions.enableCompileDebug = true;
    instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    impl = std::make_unique<ConditionalCompilationImpl>(instance.get());
    EXPECT_TRUE(impl->EvalRefExpr(re));
}

/**
 * --- EvalParenExpr ---
 */
TEST_F(ConditionalCompilationImplTest, EvalParenExpr_Null)
{
    ResetImpl();
    ParenExpr pe;
    SetPos(pe, 1, 1, 2);
    EXPECT_FALSE(impl->EvalParenExpr(pe));
}

TEST_F(ConditionalCompilationImplTest, EvalParenExpr_Nested)
{
    ResetImpl();
    ParenExpr pe;
    pe.expr = OwnedPtr<Expr>(MakeRef("debug", 1, 2).release());
    pe.begin = {0, 1, 1};
    pe.end = {0, 1, 8};
    EXPECT_FALSE(impl->EvalParenExpr(pe)); // debug is false
    EXPECT_EQ(diag.GetErrorCount(), 0);
}

/**
 * --- EvalLogicBinaryExpr ---
 */
TEST_F(ConditionalCompilationImplTest, EvalLogicBinaryExpr_AndOr)
{
    ResetImpl();
    // AND: left true, right false -> false
    auto andLeft = MakeOwned<UnaryExpr>();
    andLeft->op = TokenKind::NOT;
    andLeft->expr = OwnedPtr<Expr>(MakeRef("debug", 1, 2).release());
    SetUnarySpan(*andLeft, 1);
    BinaryExpr andBe;
    andBe.op = TokenKind::AND;
    andBe.leftExpr = OwnedPtr<Expr>(andLeft.release());
    andBe.rightExpr = OwnedPtr<Expr>(MakeRef("debug", 1, 11).release());
    SetBinarySpan(andBe);
    EXPECT_FALSE(impl->EvalLogicBinaryExpr(andBe));

    // OR: left true -> true (right deliberately invalid)
    auto orLeft = MakeOwned<UnaryExpr>();
    orLeft->op = TokenKind::NOT;
    orLeft->expr = OwnedPtr<Expr>(MakeRef("debug", 1, 2).release());
    SetUnarySpan(*orLeft, 1);
    BinaryExpr orBe;
    orBe.op = TokenKind::OR;
    orBe.leftExpr = OwnedPtr<Expr>(orLeft.release());
    orBe.rightExpr = OwnedPtr<Expr>(MakeRef("os", 1, 11).release()); // invalid -> false
    SetBinarySpan(orBe);
    EXPECT_TRUE(impl->EvalLogicBinaryExpr(orBe));
}

/**
 * --- EvalConditionExpr default ---
 */
TEST_F(ConditionalCompilationImplTest, EvalConditionExpr_UnknownKind)
{
    ResetImpl();
    Expr e; // ASTKind::EXPR hits the default branch.
    SetPos(e, 1, 1, 1);
    EXPECT_FALSE(impl->EvalConditionExpr(e));
    EXPECT_GT(diag.GetErrorCount(), 0);
}

/**
 * --- EvalBinaryExpr invalid op ---
 */
TEST_F(ConditionalCompilationImplTest, EvalBinaryExpr_InvalidOp)
{
    ResetImpl();
    BinaryExpr be;
    be.op = TokenKind::ADD;
    be.leftExpr = MakeRef("os", 1, 1);
    be.rightExpr = MakeStringLit("Linux", 1, 6);
    SetBinarySpan(be);
    EXPECT_FALSE(impl->EvalBinaryExpr(be));
    EXPECT_GT(diag.GetErrorCount(), 0);
}

/**
 * --- EvalNodeCondition ---
 */
TEST_F(ConditionalCompilationImplTest, EvalNodeCondition_WhenWithoutCondExpr)
{
    ResetImpl();
    auto decl = MakeOwned<Decl>();
    auto anno = MakeOwned<Annotation>();
    SetPos(*anno, 1, 1, 4);
    anno->kind = AnnotationKind::WHEN;
    decl->annotations.push_back(std::move(anno));
    Ptr<Decl> node = decl.get();
    EXPECT_TRUE(impl->EvalNodeCondition<Decl>(node));
    EXPECT_GT(diag.GetErrorCount(), 0);
}

TEST_F(ConditionalCompilationImplTest, EvalNodeCondition_NonWhenUntouched)
{
    ResetImpl();
    auto decl = MakeOwned<Decl>();
    auto anno = MakeOwned<Annotation>();
    anno->kind = AnnotationKind::DEPRECATED;
    decl->annotations.push_back(std::move(anno));
    Ptr<Decl> node = decl.get();
    EXPECT_TRUE(impl->EvalNodeCondition<Decl>(node));
    EXPECT_EQ(decl->annotations.size(), 1);
}

TEST_F(ConditionalCompilationImplTest, EvalNodeCondition_WhenEval)
{
    ResetImpl();
    auto decl = MakeOwned<Decl>();
    auto anno = MakeOwned<Annotation>();
    SetPos(*anno, 1, 1, 4);
    anno->kind = AnnotationKind::WHEN;
    auto ref = MakeRef("debug", 1, 6);
    anno->condExpr = OwnedPtr<Expr>(ref.release());
    decl->annotations.push_back(std::move(anno));
    Ptr<Decl> node = decl.get();
    // debug false -> EvalNodeCondition returns false, WHEN erased.
    EXPECT_FALSE(impl->EvalNodeCondition<Decl>(node));
    EXPECT_TRUE(decl->annotations.empty());
}

/**
 * --- GetRelatedInfo ---
 */
TEST_F(ConditionalCompilationImplTest, GetRelatedInfo)
{
    ResetImpl();
    EXPECT_TRUE(impl->GetRelatedInfo("os").has_value());
    EXPECT_TRUE(impl->GetRelatedInfo("arch").has_value());
    EXPECT_TRUE(impl->GetRelatedInfo("backend").has_value());
    EXPECT_TRUE(impl->GetRelatedInfo("cjc_version").has_value());
    EXPECT_TRUE(impl->GetRelatedInfo("debug").has_value());
    EXPECT_TRUE(impl->GetRelatedInfo("test").has_value());
    EXPECT_TRUE(impl->GetRelatedInfo("env").has_value());

    // user-defined missing value -> nullopt
    EXPECT_FALSE(impl->GetRelatedInfo("nothing").has_value());
}
} // namespace