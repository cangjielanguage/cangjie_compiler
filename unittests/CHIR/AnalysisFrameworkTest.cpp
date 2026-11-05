// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * Unit tests for the CHIR analysis framework:
 * Engine / Analysis / Results, FlatSet, GenKillDomain, ValueDomain / Ref,
 * MapJoin / VectorJoin / naming helpers, FullStatePool / ActiveStatePool.
 */

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

#include "cangjie/CHIR/Analysis/ActiveStatePool.h"
#include "cangjie/CHIR/Analysis/Analysis.h"
#include "cangjie/CHIR/Analysis/Engine.h"
#include "cangjie/CHIR/Analysis/FlatSet.h"
#include "cangjie/CHIR/Analysis/GenKillAnalysis.h"
#include "cangjie/CHIR/Analysis/Results.h"
#include "cangjie/CHIR/Analysis/Utils.h"
#include "cangjie/CHIR/Analysis/ValueDomain.h"
#include "cangjie/CHIR/IR/CHIRBuilder.h"
#include "cangjie/CHIR/IR/DebugLocation.h"
#include "cangjie/CHIR/IR/Expression/Expression.h"
#include "cangjie/CHIR/IR/Expression/Terminator.h"
#include "cangjie/CHIR/IR/Package.h"
#include "cangjie/CHIR/IR/Value/LiteralValue.h"
#include "cangjie/CHIR/IR/AttributeInfo.h"

using namespace Cangjie;
using namespace Cangjie::CHIR;

// Types below live in Cangjie::CHIR so Analysis<> / GenKillDomain<> statics can be specialized.
namespace Cangjie::CHIR {

// ---------------------------------------------------------------------------
// Counting domain used to exercise Engine / Results
// ---------------------------------------------------------------------------

struct CountingDomain : public AbstractDomain<CountingDomain> {
    int counter{0};
    bool forcedTop{false};
    std::set<std::string> facts;

    CountingDomain()
    {
        kind = ReachableKind::UNREACHABLE;
    }

    static CountingDomain Reachable(int c = 0)
    {
        CountingDomain d;
        d.kind = ReachableKind::REACHABLE;
        d.counter = c;
        return d;
    }

    static CountingDomain Top()
    {
        CountingDomain d = Reachable(0);
        d.forcedTop = true;
        return d;
    }

    bool Join(const CountingDomain& rhs) override
    {
        if (rhs.IsBottom()) {
            return false;
        }
        if (IsBottom()) {
            *this = rhs;
            return true;
        }
        bool changed = false;
        if (rhs.forcedTop && !forcedTop) {
            forcedTop = true;
            changed = true;
        }
        if (rhs.counter > counter) {
            counter = rhs.counter;
            changed = true;
        }
        for (const auto& f : rhs.facts) {
            if (facts.insert(f).second) {
                changed = true;
            }
        }
        return changed;
    }

    std::string ToString() const override
    {
        if (IsBottom()) {
            return "bottom";
        }
        if (forcedTop) {
            return "top";
        }
        return "cnt=" + std::to_string(counter);
    }
};

class CountingAnalysis : public Analysis<CountingDomain> {
public:
    explicit CountingAnalysis(const Function* f, bool isDebug = false) : Analysis(f, isDebug) {}

    CountingDomain Bottom() override
    {
        return CountingDomain();
    }

    void InitializeFuncEntryState(CountingDomain& state) override
    {
        state = CountingDomain::Reachable(0);
    }

    void InitializeLambdaEntryState(CountingDomain& state) override
    {
        if (state.IsBottom()) {
            state = CountingDomain::Reachable(0);
        }
        state.facts.insert("lambda-entry");
    }

    void PreHandleLambdaExpression(CountingDomain& state, const Lambda* lambda) override
    {
        state.facts.insert(std::string("pre:") + lambda->GetSrcCodeIdentifier());
        ++state.counter;
    }

    void HandleVarStateCapturedByLambda(CountingDomain& state, const Lambda* lambda) override
    {
        state.facts.insert(std::string("cap:") + lambda->GetSrcCodeIdentifier());
        ++capturedCalls;
    }

    void PropagateExpressionEffect(CountingDomain& state, const Expression* expression) override
    {
        state.facts.insert(expression->GetExprKindName());
        ++state.counter;
    }

    std::optional<Block*> PropagateTerminatorEffect(CountingDomain& state, const Expression* terminator) override
    {
        state.facts.insert(terminator->GetExprKindName());
        ++state.counter;
        if (forceTrueSuccOnly && Is<Branch>(terminator)) {
            return StaticCast<const Branch*>(terminator)->GetTrueBlock();
        }
        return std::nullopt;
    }

    bool CheckInQueueTimes(const Block* block, CountingDomain& curState) override
    {
        auto& times = inqueueTimes[block];
        ++times;
        if (times >= maxInqueue) {
            curState = CountingDomain::Top();
            return true;
        }
        return false;
    }

    bool forceTrueSuccOnly{false};
    unsigned maxInqueue{1000};
    unsigned capturedCalls{0};
    std::unordered_map<const Block*, unsigned> inqueueTimes;
};

template <> const std::string Analysis<CountingDomain>::name = "counting-analysis";
template <> const std::optional<unsigned> Analysis<CountingDomain>::blockLimit = std::nullopt;

struct LimitedDomain : public AbstractDomain<LimitedDomain> {
    LimitedDomain()
    {
        kind = ReachableKind::UNREACHABLE;
    }
    void MakeReachable()
    {
        kind = ReachableKind::REACHABLE;
    }
    bool Join(const LimitedDomain&) override
    {
        return false;
    }
    std::string ToString() const override
    {
        return IsBottom() ? "bot" : "ok";
    }
};

class LimitedAnalysis : public Analysis<LimitedDomain> {
public:
    explicit LimitedAnalysis(const Function* f, bool = false) : Analysis(f, false) {}
    LimitedDomain Bottom() override
    {
        return LimitedDomain();
    }
    void InitializeFuncEntryState(LimitedDomain& state) override
    {
        state.MakeReachable();
    }
};

template <> const std::string Analysis<LimitedDomain>::name = "limited-analysis";
template <> const std::optional<unsigned> Analysis<LimitedDomain>::blockLimit = 1;

// Minimal analysis that keeps Analysis<> default virtual bodies for coverage.
struct MinimalDomain : public AbstractDomain<MinimalDomain> {
    MinimalDomain()
    {
        kind = ReachableKind::UNREACHABLE;
    }
    static MinimalDomain Reachable()
    {
        MinimalDomain d;
        d.kind = ReachableKind::REACHABLE;
        return d;
    }
    bool Join(const MinimalDomain& rhs) override
    {
        if (rhs.IsBottom()) {
            return false;
        }
        if (IsBottom()) {
            *this = rhs;
            return true;
        }
        return false;
    }
    std::string ToString() const override
    {
        return IsBottom() ? "bot" : "ok";
    }
};

class MinimalAnalysis : public Analysis<MinimalDomain> {
public:
    explicit MinimalAnalysis(const Function* f, bool isDebug = true) : Analysis(f, isDebug) {}
    MinimalDomain Bottom() override
    {
        return MinimalDomain();
    }
};

template <> const std::string Analysis<MinimalDomain>::name = "minimal-analysis";
template <> const std::optional<unsigned> Analysis<MinimalDomain>::blockLimit = std::nullopt;

// ---------------------------------------------------------------------------
// GenKill test domains
// ---------------------------------------------------------------------------

struct TestMaybeDomain : public GenKillDomain<TestMaybeDomain> {
    explicit TestMaybeDomain(size_t n) : GenKillDomain(n)
    {
        kind = ReachableKind::REACHABLE;
    }
    void SetUnreachable()
    {
        kind = ReachableKind::UNREACHABLE;
    }
};

struct TestMustDomain : public GenKillDomain<TestMustDomain> {
    explicit TestMustDomain(size_t n) : GenKillDomain(n)
    {
        kind = ReachableKind::REACHABLE;
    }
};

template <> const AnalysisKind GenKillDomain<TestMaybeDomain>::mustOrMaybe = AnalysisKind::MAYBE;
template <> const AnalysisKind GenKillDomain<TestMustDomain>::mustOrMaybe = AnalysisKind::MUST;

struct NamedElem {
    std::string name;
    explicit NamedElem(std::string n) : name(std::move(n)) {}
    std::string ToString(size_t) const
    {
        return name;
    }
};

struct FakeAbs {
    std::string name;
    explicit FakeAbs(std::string n) : name(std::move(n)) {}
    std::unique_ptr<FakeAbs> Clone() const
    {
        return std::make_unique<FakeAbs>(name);
    }
    std::string ToString() const
    {
        return name;
    }
    std::optional<std::unique_ptr<FakeAbs>> Join(const FakeAbs& rhs) const
    {
        if (name == rhs.name) {
            return std::nullopt;
        }
        if (name == "nulljoin") {
            return std::make_optional(std::unique_ptr<FakeAbs>{nullptr});
        }
        return std::make_optional(std::make_unique<FakeAbs>(name + "+" + rhs.name));
    }
};

class PoolTestValue : public Value {
public:
    explicit PoolTestValue(const std::string& id) : Value(nullptr, id, Value::KIND_LOCALVAR) {}
    std::string ToString(size_t) const override
    {
        return GetIdentifier();
    }
};

} // namespace Cangjie::CHIR

namespace {

class FrameworkFixture : public ::testing::Test {
protected:
    void SetUp() override
    {
        nameMap = std::make_unique<std::unordered_map<unsigned int, std::string>>();
        cctx = std::make_unique<CHIRContext>(nameMap.get());
        builder = std::make_unique<CHIRBuilder>(*cctx);
        package = builder->CreatePackage("default");
        funcCounter = 0;
    }

    Function* NewFunc(const std::string& nameHint = "f")
    {
        auto name = nameHint + std::to_string(funcCounter++);
        auto* funcTy = builder->GetType<FuncType>(std::vector<Type*>{}, builder->GetUnitTy());
        auto* func = builder->CreateFunction(funcTy, name, name, "", "default");
        auto* body = builder->CreateBlockGroup(*func);
        func->InitBody(*body);
        auto* block = builder->CreateBlock(body);
        body->SetEntryBlock(block);
        curFunc = func;
        curBody = body;
        curBlock = block;
        return func;
    }

    Block* NewBlock()
    {
        return builder->CreateBlock(curBody);
    }

    Value* LitBool(bool v, Block* block = nullptr)
    {
        auto* b = block ? block : curBlock;
        auto* c = builder->CreateConstantExpression<BoolLiteral>(builder->GetBoolTy(), b, v);
        b->AppendExpression(c);
        return c->GetResult();
    }

    Value* LitInt(uint64_t v, Block* block = nullptr)
    {
        auto* b = block ? block : curBlock;
        auto* c = builder->CreateConstantExpression<IntLiteral>(builder->GetInt64Ty(), b, v);
        b->AppendExpression(c);
        return c->GetResult();
    }

    std::unique_ptr<Results<CountingDomain>> RunCounting(
        Function* func, bool forceTrue = false, unsigned maxInqueue = 1000)
    {
        auto analysis = std::make_unique<CountingAnalysis>(func, false);
        analysis->forceTrueSuccOnly = forceTrue;
        analysis->maxInqueue = maxInqueue;
        auto* raw = analysis.get();
        Engine<CountingDomain> engine(func, std::move(analysis));
        auto results = engine.IterateToFixpoint();
        lastCapturedCalls = raw->capturedCalls;
        return results;
    }

    std::unique_ptr<std::unordered_map<unsigned int, std::string>> nameMap;
    std::unique_ptr<CHIRContext> cctx;
    std::unique_ptr<CHIRBuilder> builder;
    Package* package{nullptr};
    Function* curFunc{nullptr};
    BlockGroup* curBody{nullptr};
    Block* curBlock{nullptr};
    int funcCounter{0};
    unsigned lastCapturedCalls{0};
};

} // namespace

// ============================== Engine ======================================

TEST_F(FrameworkFixture, Engine_SkipAnalysisReturnsNull)
{
    auto* func = NewFunc("skip");
    curBlock->AppendExpression(builder->CreateTerminator<Exit>(curBlock));
    func->EnableAttr(Attribute::SKIP_ANALYSIS);

    auto analysis = std::make_unique<CountingAnalysis>(func);
    Engine<CountingDomain> engine(func, std::move(analysis));
    EXPECT_EQ(engine.IterateToFixpoint(), nullptr);
}

TEST_F(FrameworkFixture, Engine_ExceedBlockLimitReturnsNull)
{
    auto* func = NewFunc("lim");
    auto* b2 = NewBlock();
    LitBool(true);
    curBlock->AppendExpression(builder->CreateTerminator<GoTo>(b2, curBlock));
    b2->AppendExpression(builder->CreateTerminator<Exit>(b2));

    auto analysis = std::make_unique<LimitedAnalysis>(func);
    Engine<LimitedDomain> engine(func, std::move(analysis));
    // body has 2 blocks, limit is 1
    EXPECT_EQ(engine.IterateToFixpoint(), nullptr);
}

TEST_F(FrameworkFixture, Engine_LinearCfgPropagatesEffects)
{
    auto* func = NewFunc("lin");
    LitInt(1);
    LitInt(2);
    curBlock->AppendExpression(builder->CreateTerminator<Exit>(curBlock));

    auto results = RunCounting(func);
    ASSERT_NE(results, nullptr);

    int afterCount = -1;
    results->VisitWith(
        [](const CountingDomain&, Expression*, size_t) {},
        [&](const CountingDomain& state, Expression*, size_t) { afterCount = state.counter; },
        [&](const CountingDomain& state, Expression*, std::optional<Block*>) {
            EXPECT_GE(state.counter, 2);
            EXPECT_FALSE(state.IsBottom());
            afterCount = state.counter;
        });
    EXPECT_GE(afterCount, 2);
}

TEST_F(FrameworkFixture, Engine_DiamondJoinMergesFacts)
{
    auto* func = NewFunc("dia");
    auto* thenB = NewBlock();
    auto* elseB = NewBlock();
    auto* joinB = NewBlock();

    auto* cond = LitBool(true);
    curBlock->AppendExpression(builder->CreateTerminator<Branch>(cond, thenB, elseB, curBlock));

    LitInt(1, thenB);
    thenB->AppendExpression(builder->CreateTerminator<GoTo>(joinB, thenB));

    LitInt(2, elseB);
    elseB->AppendExpression(builder->CreateTerminator<GoTo>(joinB, elseB));

    joinB->AppendExpression(builder->CreateTerminator<Exit>(joinB));

    auto results = RunCounting(func);
    ASSERT_NE(results, nullptr);

    CountingDomain joinEntry;
    results->VisitWith(
        [](const CountingDomain&, Expression*, size_t) {},
        [](const CountingDomain&, Expression*, size_t) {},
        [&](const CountingDomain& state, Expression* term, std::optional<Block*>) {
            if (term && term->GetParentBlock() == joinB) {
                joinEntry = state;
            }
        });
    EXPECT_FALSE(joinEntry.IsBottom());
    EXPECT_GE(joinEntry.counter, 1);
}

TEST_F(FrameworkFixture, Engine_ForceTrueSuccessorOnly)
{
    auto* func = NewFunc("br");
    auto* thenB = NewBlock();
    auto* elseB = NewBlock();

    auto* cond = LitBool(true);
    curBlock->AppendExpression(builder->CreateTerminator<Branch>(cond, thenB, elseB, curBlock));
    thenB->AppendExpression(builder->CreateTerminator<Exit>(thenB));
    elseB->AppendExpression(builder->CreateTerminator<Exit>(elseB));

    auto results = RunCounting(func, /*forceTrue=*/true);
    ASSERT_NE(results, nullptr);

    bool sawThen = false;
    bool sawElse = false;
    results->VisitWith(
        [](const CountingDomain&, Expression*, size_t) {},
        [](const CountingDomain&, Expression*, size_t) {},
        [&](const CountingDomain& state, Expression* term, std::optional<Block*>) {
            if (!term) {
                return;
            }
            if (term->GetParentBlock() == thenB) {
                sawThen = !state.IsBottom();
            }
            if (term->GetParentBlock() == elseB) {
                sawElse = !state.IsBottom();
            }
        });
    EXPECT_TRUE(sawThen);
    EXPECT_FALSE(sawElse);
}

TEST_F(FrameworkFixture, Engine_UnreachableBlockSkipped)
{
    auto* func = NewFunc("unr");
    auto* dead = NewBlock();
    dead->EnableAttr(Attribute::UNREACHABLE);
    LitInt(9, dead);
    dead->AppendExpression(builder->CreateTerminator<Exit>(dead));
    curBlock->AppendExpression(builder->CreateTerminator<Exit>(curBlock));

    auto results = RunCounting(func);
    ASSERT_NE(results, nullptr);
}

TEST_F(FrameworkFixture, Engine_EmptyBlockSkipped)
{
    auto* func = NewFunc("empty");
    auto* mid = NewBlock();
    auto* end = NewBlock();
    // mid has no expressions (empty) — Engine continues without propagate
    curBlock->AppendExpression(builder->CreateTerminator<GoTo>(mid, curBlock));
    // still need terminator on mid for CFG successors of GetReachableSuccessors;
    // empty exprs path is hit when exprs.empty() — so leave mid without appending,
    // but then GetReachableSuccessors may fail. Use a block that is only empty if
    // we don't process it. Instead: entry bottoms out via Exit; create unreachable empty.
    mid->AppendExpression(builder->CreateTerminator<GoTo>(end, mid));
    end->AppendExpression(builder->CreateTerminator<Exit>(end));

    auto results = RunCounting(func);
    ASSERT_NE(results, nullptr);
}

TEST_F(FrameworkFixture, Engine_CheckInQueueTimesForcesTop)
{
    auto* func = NewFunc("loop");
    auto* loop = NewBlock();
    LitBool(true);
    curBlock->AppendExpression(builder->CreateTerminator<GoTo>(loop, curBlock));
    LitInt(1, loop);
    // self-loop to keep rejoining
    loop->AppendExpression(builder->CreateTerminator<GoTo>(loop, loop));

    auto results = RunCounting(func, false, /*maxInqueue=*/3);
    ASSERT_NE(results, nullptr);
}

TEST_F(FrameworkFixture, Engine_LambdaWorklistAndApplyCapture)
{
    auto* func = NewFunc("lam");
    auto* unitTy = builder->GetUnitTy();
    auto* lambdaTy = builder->GetType<FuncType>(std::vector<Type*>{}, unitTy);

    auto* lambda = builder->CreateExpression<Lambda>(lambdaTy, lambdaTy, curBlock, false, "L1", "L1");
    auto* lambdaBg = builder->CreateBlockGroup(*func);
    lambda->InitBody(*lambdaBg);
    auto* lambdaEntry = builder->CreateBlock(lambdaBg);
    lambdaBg->SetEntryBlock(lambdaEntry);
    LitInt(7, lambdaEntry);
    lambdaEntry->AppendExpression(builder->CreateTerminator<Exit>(lambdaEntry));

    curBlock->AppendExpression(lambda);

    FuncCallContext ctx;
    auto* apply = builder->CreateExpression<Apply>(unitTy, lambda->GetResult(), ctx, curBlock);
    curBlock->AppendExpression(apply);
    curBlock->AppendExpression(builder->CreateTerminator<Exit>(curBlock));

    auto results = RunCounting(func);
    ASSERT_NE(results, nullptr);
    EXPECT_GE(lastCapturedCalls, 1u);

    bool sawLambdaFact = false;
    results->VisitWith(
        [](const CountingDomain&, Expression*, size_t) {},
        [&](const CountingDomain& state, Expression*, size_t) {
            if (state.facts.count("lambda-entry") || state.facts.count("pre:L1")) {
                sawLambdaFact = true;
            }
        },
        [&](const CountingDomain& state, Expression*, std::optional<Block*>) {
            if (state.facts.count("lambda-entry") || state.facts.count("pre:L1") || state.facts.count("cap:L1")) {
                sawLambdaFact = true;
            }
        });
    EXPECT_TRUE(sawLambdaFact);
}

TEST_F(FrameworkFixture, Engine_ApplyToLambdaAsTerminator)
{
    auto* func = NewFunc("lamT");
    auto* unitTy = builder->GetUnitTy();
    auto* lambdaTy = builder->GetType<FuncType>(std::vector<Type*>{}, unitTy);

    auto* lambda = builder->CreateExpression<Lambda>(lambdaTy, lambdaTy, curBlock, false, "L2", "L2");
    auto* lambdaBg = builder->CreateBlockGroup(*func);
    lambda->InitBody(*lambdaBg);
    auto* lambdaEntry = builder->CreateBlock(lambdaBg);
    lambdaBg->SetEntryBlock(lambdaEntry);
    lambdaEntry->AppendExpression(builder->CreateTerminator<Exit>(lambdaEntry));
    curBlock->AppendExpression(lambda);

    // Apply as last non-terminator then Exit — also cover Apply terminator path via Results
    FuncCallContext ctx;
    auto* apply = builder->CreateExpression<Apply>(unitTy, lambda->GetResult(), ctx, curBlock);
    curBlock->AppendExpression(apply);
    curBlock->AppendExpression(builder->CreateTerminator<Exit>(curBlock));

    auto results = RunCounting(func);
    ASSERT_NE(results, nullptr);
    EXPECT_GE(lastCapturedCalls, 1u);
}

TEST_F(FrameworkFixture, LambdaState_MoveSemantics)
{
    auto sets = std::make_unique<std::unordered_map<Block*, CountingDomain>>();
    LambdaState<CountingDomain> s1(nullptr, std::move(sets));
    LambdaState<CountingDomain> s2(std::move(s1));
    EXPECT_EQ(s2.lambda, nullptr);
    EXPECT_NE(s2.entrySets, nullptr);

    auto sets2 = std::make_unique<std::unordered_map<Block*, CountingDomain>>();
    LambdaState<CountingDomain> s3(nullptr, std::move(sets2));
    s3 = std::move(s2);
    EXPECT_NE(s3.entrySets, nullptr);
}

// ============================== FlatSet =====================================

TEST(FlatSetTest, JoinAllBranches)
{
    FlatSet<NamedElem*> bottom(false);
    FlatSet<NamedElem*> top(true);
    NamedElem a("a");
    NamedElem b("b");
    FlatSet<NamedElem*> ea(&a);
    FlatSet<NamedElem*> eb(&b);
    FlatSet<NamedElem*> ea2(&a);

    EXPECT_TRUE(bottom.IsBottom());
    EXPECT_TRUE(top.IsTop());
    EXPECT_EQ(top.ToString(), "top");
    EXPECT_EQ(bottom.ToString(), "bottom");
    EXPECT_EQ(ea.ToString(), "a");
    EXPECT_EQ(ea.GetElem().value(), &a);
    EXPECT_FALSE(top.GetElem().has_value());

    // Top join anything -> no change
    EXPECT_FALSE(top.Join(bottom));
    EXPECT_FALSE(top.Join(ea));

    // Non-top join Bottom -> no change
    EXPECT_FALSE(ea.Join(bottom));

    // Non-top join Top -> Top
    EXPECT_TRUE(ea2.Join(top));
    EXPECT_TRUE(ea2.IsTop());

    // Bottom join Elem -> copy
    FlatSet<NamedElem*> b2(false);
    EXPECT_TRUE(b2.Join(eb));
    EXPECT_EQ(b2.GetElem().value(), &b);

    // Elem join same Elem -> no change
    FlatSet<NamedElem*> eSame(&a);
    EXPECT_FALSE(eSame.Join(FlatSet<NamedElem*>(&a)));

    // Elem join different Elem -> Top
    FlatSet<NamedElem*> eDiff(&a);
    EXPECT_TRUE(eDiff.Join(eb));
    EXPECT_TRUE(eDiff.IsTop());

    FlatSet<NamedElem*> upd(false);
    upd.UpdateElem(&a);
    EXPECT_EQ(upd.GetElem().value(), &a);
    upd.SetToBound(true);
    EXPECT_TRUE(upd.IsTop());
    upd.SetToBound(false);
    EXPECT_TRUE(upd.IsBottom());
}

// ============================== GenKill =====================================

TEST(GenKillDomainTest, MaybeOrJoinAndMustAndJoin)
{
    TestMaybeDomain m1(3);
    TestMaybeDomain m2(3);
    m1.Gen(0);
    m2.Gen(1);
    EXPECT_TRUE(m1.IsTrueAt(0));
    EXPECT_FALSE(m1.IsTrueAt(1));
    EXPECT_TRUE(m1.Join(m2));
    EXPECT_TRUE(m1.IsTrueAt(0));
    EXPECT_TRUE(m1.IsTrueAt(1));
    EXPECT_NE(m1.ToString().find("Reachable"), std::string::npos);

    TestMustDomain u1(2);
    TestMustDomain u2(2);
    u1.Gen(0);
    u1.Gen(1);
    u2.Gen(0);
    EXPECT_TRUE(u1.Join(u2));
    EXPECT_TRUE(u1.IsTrueAt(0));
    EXPECT_FALSE(u1.IsTrueAt(1)); // MUST: 1 & 0 => 0

    TestMaybeDomain ops(2);
    ops.GenAll();
    EXPECT_TRUE(ops.IsTrueAt(0));
    EXPECT_TRUE(ops.IsTrueAt(1));
    ops.Kill(1);
    EXPECT_FALSE(ops.IsTrueAt(1));
    ops.PropagateFrom(0, 1);
    EXPECT_TRUE(ops.IsTrueAt(1));
    ops.KillAll();
    EXPECT_FALSE(ops.IsTrueAt(0));

    TestMaybeDomain unreachable(1);
    unreachable.SetUnreachable();
    EXPECT_EQ(unreachable.ToString(), "Unreachable");
}

// ============================== ValueDomain / Ref ============================

TEST(ValueDomainTest, JoinAndAccessors)
{
    ValueDomain<FakeAbs> bottom(false);
    ValueDomain<FakeAbs> top(true);
    EXPECT_TRUE(bottom.IsBottom());
    EXPECT_TRUE(top.IsTop());
    EXPECT_EQ(bottom.ToString(), "Bottom");
    EXPECT_EQ(top.ToString(), "TOP");

    // Top / Bottom short-circuits
    EXPECT_FALSE(top.Join(bottom));
    EXPECT_FALSE(ValueDomain<FakeAbs>(std::make_unique<FakeAbs>("x")).Join(bottom));

    // Bottom := rhs
    ValueDomain<FakeAbs> b2(false);
    EXPECT_TRUE(b2.Join(ValueDomain<FakeAbs>(std::make_unique<FakeAbs>("v"))));
    EXPECT_EQ(b2.CheckAbsVal()->ToString(), "v");

    // Val join Top
    ValueDomain<FakeAbs> vTop(std::make_unique<FakeAbs>("v"));
    EXPECT_TRUE(vTop.Join(top));
    EXPECT_TRUE(vTop.IsTop());

    // Val join same / different / null join
    ValueDomain<FakeAbs> v1(std::make_unique<FakeAbs>("a"));
    EXPECT_FALSE(v1.Join(ValueDomain<FakeAbs>(std::make_unique<FakeAbs>("a"))));
    EXPECT_TRUE(v1.Join(ValueDomain<FakeAbs>(std::make_unique<FakeAbs>("b"))));
    EXPECT_EQ(v1.CheckAbsVal()->ToString(), "a+b");

    ValueDomain<FakeAbs> vNull(std::make_unique<FakeAbs>("nulljoin"));
    EXPECT_TRUE(vNull.Join(ValueDomain<FakeAbs>(std::make_unique<FakeAbs>("z"))));
    EXPECT_TRUE(vNull.IsTop());

    // Ref joins
    Ref r1("r1", false);
    Ref r2("r2", false);
    ValueDomain<FakeAbs> ref1(&r1);
    EXPECT_FALSE(ref1.Join(ValueDomain<FakeAbs>(&r1)));
    EXPECT_TRUE(ref1.Join(ValueDomain<FakeAbs>(&r2)));
    EXPECT_TRUE(ref1.GetRef()->IsTopRefInstance());

    ValueDomain<FakeAbs> refTop(Ref::GetTopRefInstance());
    EXPECT_FALSE(refTop.Join(ValueDomain<FakeAbs>(&r1)));

    ValueDomain<FakeAbs> refToTop(&r1);
    EXPECT_TRUE(refToTop.Join(ValueDomain<FakeAbs>(Ref::GetTopRefInstance())));
    EXPECT_TRUE(refToTop.GetRef()->IsTopRefInstance());

    ValueDomain<FakeAbs> refJoinTop(&r1);
    EXPECT_TRUE(refJoinTop.Join(top));
    EXPECT_TRUE(refJoinTop.IsTop());

    // operators / setters
    ValueDomain<FakeAbs> assign = false;
    EXPECT_TRUE(assign.IsBottom());
    assign = true;
    EXPECT_TRUE(assign.IsTop());
    assign = &r1;
    EXPECT_TRUE(assign.IsRef());
    EXPECT_EQ(assign.GetKind(), ValueDomain<FakeAbs>::ValueKind::REF);
    assign.SetRef(&r2);
    EXPECT_EQ(assign.GetRef(), &r2);
    assign.SetSelfToBound(false);
    EXPECT_TRUE(assign.IsBottom());

    ValueDomain<FakeAbs> fromVal = std::make_unique<FakeAbs>("q");
    EXPECT_EQ(fromVal.CheckAbsVal()->ToString(), "q");
    ValueDomain<FakeAbs> copied(fromVal);
    EXPECT_EQ(copied.CheckAbsVal()->ToString(), "q");
    ValueDomain<FakeAbs> moved(std::move(copied));
    EXPECT_EQ(moved.CheckAbsVal()->ToString(), "q");
    ValueDomain<FakeAbs> assignedCopy = false;
    assignedCopy = moved;
    EXPECT_EQ(assignedCopy.CheckAbsVal()->ToString(), "q");
    ValueDomain<FakeAbs> assignedMove = false;
    assignedMove = std::move(assignedCopy);
    EXPECT_EQ(assignedMove.CheckAbsVal()->ToString(), "q");
}

TEST(RefAndAbstractObjectTest, RootsCacheAndTop)
{
    auto* topObj = AbstractObject::GetTopObjInstance();
    EXPECT_TRUE(topObj->IsTopObjInstance());
    EXPECT_EQ(topObj->ToString(0), "TopObj");
    AbstractObject other("o1");
    EXPECT_FALSE(other.IsTopObjInstance());
    EXPECT_EQ(other.ToString(0), "o1");

    Ref root1("1", false);
    Ref root2("2", false);
    Ref child("c", false);
    child.AddRoots(&root1, &root2);
    EXPECT_TRUE(child.CanRepresent(&root1));
    EXPECT_TRUE(child.CanRepresent(&root1)); // cache hit
    EXPECT_FALSE(root1.CanRepresent(&root2));

    Ref child2("c2", false);
    child2.AddRoots(&root1, &root1);
    EXPECT_TRUE(child.IsEquivalent(&child2) || !child.IsEquivalent(&root1));

    Ref nested("n", false);
    nested.AddRoots(&child, &root2);
    EXPECT_TRUE(nested.CanRepresent(&child) || nested.CanRepresent(&root1));

    Ref staticRef("s", true);
    staticRef.WriteCache(&root1, true);
    EXPECT_EQ(staticRef.CheckCache(&root1).value(), true);
    EXPECT_FALSE(staticRef.CheckCache(&root2).has_value());
    EXPECT_EQ(staticRef.GetUniqueID().front(), 's');
    EXPECT_EQ(root1.GetUniqueID(), "1");

    EXPECT_TRUE(Ref::GetTopRefInstance()->IsTopRefInstance());
}

// ============================== Utils =======================================

TEST(AnalysisUtilsTest, MapJoinVectorJoinAndNames)
{
    CountingDomain a = CountingDomain::Reachable(1);
    CountingDomain b = CountingDomain::Reachable(3);
    std::unordered_map<int, CountingDomain> lhs{{1, a}};
    std::unordered_map<int, CountingDomain> rhs{{1, b}, {2, CountingDomain::Reachable(5)}};
    EXPECT_TRUE(MapJoin(lhs, rhs));
    EXPECT_EQ(lhs.at(1).counter, 3);
    EXPECT_EQ(lhs.at(2).counter, 5);
    EXPECT_FALSE(MapJoin(lhs, rhs)); // second time no change for existing; new keys already present

    std::vector<CountingDomain> vl{CountingDomain::Reachable(1), CountingDomain::Reachable(2)};
    std::vector<CountingDomain> vr{CountingDomain::Reachable(4), CountingDomain::Reachable(2)};
    EXPECT_TRUE(VectorJoin(vl, vr));
    EXPECT_EQ(vl[0].counter, 4);
    EXPECT_FALSE(VectorJoin(vl, vr));

    EXPECT_EQ(GetRefName(3), "Ref3");
    EXPECT_EQ(GetObjName(4), "Obj4");
    EXPECT_EQ(GetObjChildName("Obj0", 2), "Obj0.2");
}

TEST(AnalysisUtilsTest, CutOffHighBits)
{
    EXPECT_EQ(CutOffHighBits<uint64_t>(0x1FF, Type::TypeKind::TYPE_UINT8), 0xFFu);
    EXPECT_EQ(CutOffHighBits<uint64_t>(0x1FFFF, Type::TypeKind::TYPE_UINT16), 0xFFFFu);
    EXPECT_EQ(CutOffHighBits<uint64_t>(0x1FFFFFFFFULL, Type::TypeKind::TYPE_UINT32), 0xFFFFFFFFu);
    EXPECT_EQ(CutOffHighBits<uint64_t>(7, Type::TypeKind::TYPE_UINT64), 7u);
    EXPECT_EQ(CutOffHighBits<int64_t>(-1, Type::TypeKind::TYPE_INT8), static_cast<int64_t>(static_cast<int8_t>(-1)));
    EXPECT_EQ(CutOffHighBits<int64_t>(-1, Type::TypeKind::TYPE_INT16), static_cast<int64_t>(static_cast<int16_t>(-1)));
    EXPECT_EQ(CutOffHighBits<int64_t>(-1, Type::TypeKind::TYPE_INT32), static_cast<int64_t>(static_cast<int32_t>(-1)));
    EXPECT_EQ(CutOffHighBits<int64_t>(-5, Type::TypeKind::TYPE_INT64), static_cast<int64_t>(-5));
    EXPECT_EQ(CutOffHighBits<double>(3.5, Type::TypeKind::TYPE_FLOAT32), static_cast<double>(static_cast<float>(3.5)));
    EXPECT_EQ(CutOffHighBits<double>(3.5, Type::TypeKind::TYPE_FLOAT64), 3.5);
    EXPECT_EQ(CutOffHighBits<size_t>(9, Type::TypeKind::TYPE_UINT_NATIVE), static_cast<size_t>(9));
    EXPECT_EQ(CutOffHighBits<ssize_t>(-3, Type::TypeKind::TYPE_INT_NATIVE), static_cast<ssize_t>(-3));
}

TEST_F(FrameworkFixture, IsApplyToLambdaHelper)
{
    auto* func = NewFunc("ial");
    auto* unitTy = builder->GetUnitTy();
    auto* lambdaTy = builder->GetType<FuncType>(std::vector<Type*>{}, unitTy);
    auto* lambda = builder->CreateExpression<Lambda>(lambdaTy, lambdaTy, curBlock, false, "Lx", "Lx");
    auto* lambdaBg = builder->CreateBlockGroup(*func);
    lambda->InitBody(*lambdaBg);
    auto* le = builder->CreateBlock(lambdaBg);
    lambdaBg->SetEntryBlock(le);
    le->AppendExpression(builder->CreateTerminator<Exit>(le));
    curBlock->AppendExpression(lambda);

    FuncCallContext ctx;
    auto* apply = builder->CreateExpression<Apply>(unitTy, lambda->GetResult(), ctx, curBlock);
    curBlock->AppendExpression(apply);
    curBlock->AppendExpression(builder->CreateTerminator<Exit>(curBlock));

    EXPECT_EQ(IsApplyToLambda(apply), lambda);
    EXPECT_EQ(IsApplyToLambda(curBlock->GetTerminator()), nullptr);

    auto* direct = builder->CreateExpression<Apply>(unitTy, func, FuncCallContext{}, curBlock);
    // not appended; just check helper with non-local callee
    EXPECT_EQ(IsApplyToLambda(direct), nullptr);
}

// ============================== State pools =================================

TEST(FullStatePoolTest, InsertFindJoin)
{
    FullStatePool<ValueDomain<FakeAbs>> pool;
    PoolTestValue v1("v1");
    PoolTestValue v2("v2");
    pool.Insert(&v1, ValueDomain<FakeAbs>(std::make_unique<FakeAbs>("A")));
    EXPECT_NE(pool.Find(&v1), pool.End());
    EXPECT_EQ(pool.Find(&v2), pool.End());
    EXPECT_EQ(pool.At(&v1).CheckAbsVal()->ToString(), "A");
    EXPECT_NE(pool.Begin(), pool.End());

    FullStatePool<ValueDomain<FakeAbs>> rhs;
    rhs.Insert(&v1, ValueDomain<FakeAbs>(true));
    rhs.Insert(&v2, ValueDomain<FakeAbs>(std::make_unique<FakeAbs>("B")));
    EXPECT_TRUE(pool.Join(rhs));
    EXPECT_TRUE(pool.At(&v1).IsTop());
    EXPECT_EQ(pool.At(&v2).CheckAbsVal()->ToString(), "B");

    auto [it, inserted] = pool.emplace(&v2, ValueDomain<FakeAbs>(std::make_unique<FakeAbs>("C")));
    EXPECT_FALSE(inserted);
    (void)it;
}

TEST(ActiveStatePoolExtraTest, JoinCopyMoveAndDuplicateInsert)
{
    ActiveStatePool<ValueDomain<FakeAbs>> pool;
    PoolTestValue v0("v0");
    PoolTestValue v1("v1");
    auto* node0 = pool.Insert(&v0, ValueDomain<FakeAbs>(std::make_unique<FakeAbs>("x0")));
    EXPECT_NE(node0, nullptr);
    // duplicate insert returns existing node
    auto* nodeDup = pool.Insert(&v0, ValueDomain<FakeAbs>(std::make_unique<FakeAbs>("dup")));
    EXPECT_EQ(nodeDup, node0);
    EXPECT_EQ(pool.emplace(&v0, ValueDomain<FakeAbs>(std::make_unique<FakeAbs>("dup2"))), node0);
    pool.Insert(&v1, ValueDomain<FakeAbs>(std::make_unique<FakeAbs>("x1")));
    EXPECT_NE(pool.Begin(), pool.End());
    EXPECT_FALSE(pool.At(&v0).IsTop());

    // Join existing keys; values missing from either pool remain unknown.
    ActiveStatePool<ValueDomain<FakeAbs>> lhs;
    ActiveStatePool<ValueDomain<FakeAbs>> rhs;
    PoolTestValue a("a");
    PoolTestValue b("b");
    PoolTestValue c("c");
    Ref r("rr", false);
    lhs.Insert(&a, ValueDomain<FakeAbs>(std::make_unique<FakeAbs>("A")));
    lhs.Insert(&b, ValueDomain<FakeAbs>(&r));
    rhs.Insert(&a, ValueDomain<FakeAbs>(std::make_unique<FakeAbs>("B")));
    rhs.Insert(&c, ValueDomain<FakeAbs>(std::make_unique<FakeAbs>("C")));
    rhs.Insert(&b, ValueDomain<FakeAbs>(&r));
    bool changed = lhs.Join(rhs);
    EXPECT_TRUE(changed);
    EXPECT_TRUE(lhs.At(&b).IsRef());
    EXPECT_TRUE(lhs.At(&c).IsTop());

    // copy / move assign
    ActiveStatePool<ValueDomain<FakeAbs>> copied;
    copied = lhs;
    EXPECT_NE(copied.Find(&a), copied.End());
    ActiveStatePool<ValueDomain<FakeAbs>> moved;
    moved = std::move(copied);
    EXPECT_NE(moved.Find(&a), moved.End());
}

TEST(ActiveStatePoolExtraTest, JoinForgetsMissingConstant)
{
    ActiveStatePool<ValueDomain<FakeAbs>> lhs;
    ActiveStatePool<ValueDomain<FakeAbs>> rhs;
    PoolTestValue d("d");
    lhs.Insert(&d, ValueDomain<FakeAbs>(std::make_unique<FakeAbs>("D")));

    EXPECT_TRUE(lhs.Join(rhs));
    EXPECT_TRUE(lhs.At(&d).IsTop());
    EXPECT_FALSE(lhs.Join(rhs));
}

TEST(ActiveStatePoolExtraTest, JoinPreservesMissingAbstractObjectReference)
{
    ActiveStatePool<ValueDomain<FakeAbs>> lhs;
    ActiveStatePool<ValueDomain<FakeAbs>> rhs;
    AbstractObject field("field");
    Ref ref("field_ref", false);
    ASSERT_EQ(field.GetType(), nullptr);
    lhs.Insert(&field, ValueDomain<FakeAbs>(&ref));

    EXPECT_TRUE(lhs.Join(rhs));
    const auto& value = lhs.At(&field);
    ASSERT_TRUE(value.IsRef());
    ASSERT_NE(value.GetRef(), nullptr);
    EXPECT_TRUE(value.GetRef()->IsTopRefInstance());
    EXPECT_FALSE(lhs.Join(rhs));
}

TEST(AbstractDomainDefaultsTest, BaseVirtuals)
{
    struct Tiny : public AbstractDomain<Tiny> {
        Tiny()
        {
            kind = ReachableKind::REACHABLE;
        }
    };
    Tiny t;
    EXPECT_FALSE(t.IsBottom());
    EXPECT_FALSE(t.Join(t));
    EXPECT_EQ(t.ToString(), "");
}

TEST_F(FrameworkFixture, Analysis_DefaultVirtualBodies)
{
    auto* func = NewFunc("min");
    LitInt(1);
    curBlock->AppendExpression(builder->CreateTerminator<Exit>(curBlock));

    MinimalAnalysis analysis(func, true);
    MinimalDomain state;
    analysis.InitializeFuncEntryState(state);
    analysis.InitializeLambdaEntryState(state);
    analysis.HandleVarStateCapturedByLambda(state, nullptr);
    analysis.PreHandleLambdaExpression(state, nullptr);
    analysis.PropagateExpressionEffect(state, curBlock->GetExpressions().front());
    auto succ = analysis.PropagateTerminatorEffect(state, curBlock->GetTerminator());
    EXPECT_FALSE(succ.has_value());
    EXPECT_FALSE(analysis.CheckInQueueTimes(curBlock, state));
    analysis.UpdateCurrentLambda(nullptr);
    EXPECT_TRUE(Analysis<MinimalDomain>::Filter(*func));
    EXPECT_EQ(Analysis<MinimalDomain>::GetAnalysisName(), "minimal-analysis");
    EXPECT_FALSE(Analysis<MinimalDomain>::GetBlockLimit().has_value());
    analysis.SetToStable();
}

TEST_F(FrameworkFixture, Engine_UnreachableAndEmptyAndLambdaRevisit)
{
    auto* func = NewFunc("uel");
    auto* dead = NewBlock();
    auto* loop = NewBlock();
    dead->EnableAttr(Attribute::UNREACHABLE);

    auto* cond = LitBool(true);
    // entry -> loop (with lambda) ; also branch to unreachable dead
    curBlock->AppendExpression(builder->CreateTerminator<Branch>(cond, loop, dead, curBlock));

    auto* unitTy = builder->GetUnitTy();
    auto* lambdaTy = builder->GetType<FuncType>(std::vector<Type*>{}, unitTy);
    auto* lambda = builder->CreateExpression<Lambda>(lambdaTy, lambdaTy, loop, false, "LR", "LR");
    auto* lambdaBg = builder->CreateBlockGroup(*func);
    lambda->InitBody(*lambdaBg);
    auto* le = builder->CreateBlock(lambdaBg);
    lambdaBg->SetEntryBlock(le);
    le->AppendExpression(builder->CreateTerminator<Exit>(le));
    loop->AppendExpression(lambda);
    // self-loop so lambda block is processed multiple times -> lambdaEnvState assignment branch
    loop->AppendExpression(builder->CreateTerminator<GoTo>(loop, loop));

    // Separate function for empty-block path
    auto* func2 = NewFunc("empty2");
    auto* mid = NewBlock();
    LitBool(true);
    curBlock->AppendExpression(builder->CreateTerminator<GoTo>(mid, curBlock));
    // mid intentionally has no expressions
    auto results2 = RunCounting(func2);
    EXPECT_NE(results2, nullptr);

    auto results = RunCounting(func, false, 5);
    EXPECT_NE(results, nullptr);
}

TEST_F(FrameworkFixture, Results_LambdaWithoutBodySkipped)
{
    auto* func = NewFunc("rnb");
    auto* unitTy = builder->GetUnitTy();
    auto* lambdaTy = builder->GetType<FuncType>(std::vector<Type*>{}, unitTy);
    // Lambda without InitBody => GetBody() == nullptr
    auto* lambda = builder->CreateExpression<Lambda>(lambdaTy, lambdaTy, curBlock, false, "RB", "RB");
    curBlock->AppendExpression(lambda);
    curBlock->AppendExpression(builder->CreateTerminator<Exit>(curBlock));

    auto analysis = std::make_unique<CountingAnalysis>(func);
    auto entrySets = std::make_unique<std::unordered_map<Block*, CountingDomain>>();
    entrySets->emplace(curBlock, CountingDomain::Reachable(1));
    auto lambdaSets = std::make_unique<std::unordered_map<Block*, CountingDomain>>();
    std::vector<LambdaState<CountingDomain>> lambdaResults;
    lambdaResults.emplace_back(lambda, std::move(lambdaSets));

    Results<CountingDomain> results(func, std::move(analysis), std::move(entrySets), std::move(lambdaResults));
    int visits = 0;
    results.VisitWith(
        [&](const CountingDomain&, Expression*, size_t) { ++visits; },
        [&](const CountingDomain&, Expression*, size_t) {},
        [&](const CountingDomain&, Expression*, std::optional<Block*>) { ++visits; });
    EXPECT_GE(visits, 1);
}

TEST(AnalysisUtilsTest, ToRangeHelpers)
{
    DebugLocation zero;
    auto [ok0, r0] = ToRangeIfNotZero(zero);
    EXPECT_FALSE(ok0);
    (void)r0;

    static const std::string path = "/tmp/t.cj";
    DebugLocation loc(path, 1, {3, 4}, {5, 6});
    auto range = ToRange(loc);
    EXPECT_EQ(range.begin.line, 3);
    EXPECT_EQ(range.begin.column, 4);
    auto [ok1, r1] = ToRangeIfNotZero(loc);
    EXPECT_TRUE(ok1);
    EXPECT_EQ(r1.begin.line, 3);
    (void)ToPosition(loc);
    (void)ToPosInfo(loc, false);
    (void)ToPosInfo(loc, true);
}

TEST(AnalysisUtilsTest, VectorJoinNoChange)
{
    std::vector<CountingDomain> vl{CountingDomain::Reachable(2), CountingDomain::Reachable(3)};
    std::vector<CountingDomain> vr{CountingDomain::Reachable(2), CountingDomain::Reachable(3)};
    EXPECT_FALSE(VectorJoin(vl, vr));
}

TEST(ValueDomainExtraTest, RefToStringAndAssignFromBottom)
{
    Ref r1("rx", false);
    ValueDomain<FakeAbs> fromBottom = false;
    fromBottom = &r1;
    EXPECT_TRUE(fromBottom.IsRef());
    EXPECT_EQ(fromBottom.ToString(), "rx");
    EXPECT_EQ(fromBottom.GetRef()->GetUniqueID(), "rx");

    ValueDomain<FakeAbs> bottom2(false);
    bottom2.SetRef(&r1);
    EXPECT_TRUE(bottom2.IsRef());

    ValueDomain<FakeAbs> val(std::make_unique<FakeAbs>("vv"));
    EXPECT_EQ(val.ToString(), "vv");

    ValueDomain<FakeAbs> refOnly(&r1);
    refOnly.SetSelfToTopRef();
    EXPECT_TRUE(refOnly.GetRef()->IsTopRefInstance());
}

TEST(ActiveStatePoolExtraTest, EmptyCopyAssignOverflowAndRefTop)
{
    // empty copy / assign early-return
    ActiveStatePool<ValueDomain<FakeAbs>> empty;
    ActiveStatePool<ValueDomain<FakeAbs>> copied(empty);
    EXPECT_EQ(copied.Begin(), copied.End());
    ActiveStatePool<ValueDomain<FakeAbs>> assigned;
    assigned = empty;
    EXPECT_EQ(assigned.Begin(), assigned.End());

    // overflow eviction at default thresholds (MAX=120, BASE=80)
    ActiveStatePool<ValueDomain<FakeAbs>> pool;
    std::vector<std::unique_ptr<PoolTestValue>> vals;
    for (int i = 0; i < 121; ++i) {
        vals.emplace_back(std::make_unique<PoolTestValue>("o" + std::to_string(i)));
        pool.Insert(vals.back().get(), ValueDomain<FakeAbs>(std::make_unique<FakeAbs>("x")));
    }
    size_t count = 0;
    for (auto it = pool.Begin(); it != pool.End(); ++it) {
        ++count;
    }
    EXPECT_LE(count, 120u);
    EXPECT_GE(count, 80u);

    // At() on missing ref-typed value -> TOP_REF_STATE
    auto nameMap = std::make_unique<std::unordered_map<unsigned int, std::string>>();
    CHIRContext cctx(nameMap.get());
    CHIRBuilder builder(cctx);
    auto* refTy = builder.GetType<RefType>(builder.GetInt64Ty());
    class RefTypedValue : public Value {
    public:
        RefTypedValue(Type* ty, const std::string& id) : Value(ty, id, Value::KIND_LOCALVAR) {}
        std::string ToString(size_t) const override
        {
            return GetIdentifier();
        }
    };
    RefTypedValue refVal(refTy, "refv");
    EXPECT_TRUE(pool.At(&refVal).IsTop() || pool.At(&refVal).IsRef());

    // MapJoin adaptor
    ActiveStatePool<ValueDomain<FakeAbs>> lhs;
    ActiveStatePool<ValueDomain<FakeAbs>> rhs;
    PoolTestValue x("x");
    lhs.Insert(&x, ValueDomain<FakeAbs>(std::make_unique<FakeAbs>("1")));
    rhs.Insert(&x, ValueDomain<FakeAbs>(std::make_unique<FakeAbs>("2")));
    EXPECT_TRUE(MapJoin(lhs, rhs));

    // Join discards new ref-only keys from rhs
    PoolTestValue y("y");
    Ref r("rj", false);
    rhs.Insert(&y, ValueDomain<FakeAbs>(&r));
    lhs.Join(rhs);
    EXPECT_EQ(lhs.Find(&y), lhs.End());
}
