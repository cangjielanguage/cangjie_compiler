// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "gtest/gtest.h"

#include "cangjie/CHIR/Analysis/ActiveStatePool.h"
#include "cangjie/CHIR/Analysis/ValueDomain.h"

using namespace Cangjie::CHIR;

// Lightweight test helper: a minimal concrete Value implementation we can
// instantiate in unit tests. We pass nullptr for Type* since ActiveStatePool
// only queries GetType() optionally.
class TestValue : public Value {
public:
    explicit TestValue(const std::string& id)
        : Value(nullptr, id, Value::KIND_LOCALVAR)
    {
    }

    std::string ToString() const override { return "TestValue(" + GetIdentifier() + ")"; }
};

// Minimal abstract value used with ValueDomain in tests. It implements the
// small interface required by ValueDomain<AbstractValue> (Clone, ToString,
// Join).
struct FakeAbs {
    std::string name;
    explicit FakeAbs(std::string n) : name(std::move(n)) {}
    std::unique_ptr<FakeAbs> Clone() const { return std::make_unique<FakeAbs>(name); }
    std::string ToString() const { return name; }

    // If names differ return a new value to indicate a join changed the value.
    std::optional<std::unique_ptr<FakeAbs>> Join(const FakeAbs& rhs) const
    {
        if (name == rhs.name) {
            return std::nullopt; // no change
        }
        return std::make_optional(std::unique_ptr<FakeAbs>(new FakeAbs(name + "+" + rhs.name)));
    }
};

TEST(ActiveStatePoolTest, InsertFindAt)
{
    ActiveStatePool<ValueDomain<FakeAbs>> pool;

    TestValue v1("v1");
    TestValue v2("v2");

    // initially absent -> At should return TOP state
    auto& top = pool.At(&v1);
    EXPECT_TRUE(top.IsTop());

    // insert a concrete value domain for v1
    ValueDomain<FakeAbs> dv1(std::make_unique<FakeAbs>("A"));
    auto* node = pool.Insert(&v1, std::move(dv1));
    EXPECT_TRUE(node != nullptr);

    // now Find/At should return the stored domain (not top)
    auto it = pool.Find(&v1);
    EXPECT_NE(it, pool.End());
    EXPECT_FALSE(pool.At(&v1).IsTop());

    // other values remain top
    EXPECT_TRUE(pool.At(&v2).IsTop());
}

TEST(ActiveStatePoolTest, CopyAndMoveSemantics)
{
    ActiveStatePool<ValueDomain<FakeAbs>> p1;
    TestValue a("a");
    TestValue b("b");

    p1.Insert(&a, ValueDomain<FakeAbs>(std::make_unique<FakeAbs>("one")));
    p1.Insert(&b, ValueDomain<FakeAbs>(std::make_unique<FakeAbs>("two")));

    // copy-construct
    ActiveStatePool<ValueDomain<FakeAbs>> p2(p1);
    EXPECT_FALSE(p2.At(&a).IsTop());
    EXPECT_FALSE(p2.At(&b).IsTop());

    // move-construct: source should be left in a valid (empty) state regarding roots
    ActiveStatePool<ValueDomain<FakeAbs>> p3(std::move(p1));
    // moved-from p1 should return TOP for previously present keys
    EXPECT_TRUE(p1.At(&a).IsTop());
    EXPECT_FALSE(p3.At(&a).IsTop());
}

TEST(ActiveStatePoolTest, JoinExistingKey)
{
    ActiveStatePool<ValueDomain<FakeAbs>> lhs;
    ActiveStatePool<ValueDomain<FakeAbs>> rhs;

    TestValue x("x");

    // lhs holds a concrete value
    lhs.Insert(&x, ValueDomain<FakeAbs>(std::make_unique<FakeAbs>("foo")));

    // rhs holds TOP for the same key
    rhs.Insert(&x, ValueDomain<FakeAbs>(true));

    // joining rhs into lhs should change lhs (foo -> TOP)
    bool changed = lhs.Join(rhs);
    EXPECT_TRUE(changed);
    EXPECT_TRUE(lhs.At(&x).IsTop());
}
