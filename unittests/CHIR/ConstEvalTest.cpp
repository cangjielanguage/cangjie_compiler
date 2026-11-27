// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "cangjie/CHIR/Interpreter/ConstEval.h"

using namespace Cangjie::CHIR;
using namespace Cangjie::CHIR::Interpreter;

namespace {
// One fixture per test: a function body block is the cheapest parent that
// constant creation accepts.
struct Fixture {
    std::unordered_map<unsigned int, std::string> fileNameMap;
    CHIRContext cctx;
    CHIRBuilder builder;
    Bchir bchir;
    Package* package;
    Function* func;
    Block* entry;
    IVal2CHIR converter;

    explicit Fixture(const std::string& name)
        : cctx(&fileNameMap), builder(cctx), package(builder.CreatePackage(name)),
        func(nullptr), entry(nullptr), converter(builder, bchir, *package)
    {
        auto funcTy = builder.GetType<FuncType>(std::vector<Type*>{}, builder.GetUnitTy());
        func = builder.CreateFunction(funcTy, name + "_fixture", name, name, name);
        auto body = builder.CreateBlockGroup(*func);
        func->InitBody(*body);
        entry = builder.CreateBlock(body);
        body->SetEntryBlock(entry);
    }
};
} // namespace

// A value decoded from a foreign cjo can carry a representation that does not
// match the type kind recorded next to it. Conversion must answer null instead
// of an unchecked std::get abort, and a matching value must still produce its
// constant.
TEST(ConstEvalTest, MismatchedPrimitiveValuesYieldNullInsteadOfAborting)
{
    Fixture fx("mismatch");
    const std::vector<std::pair<Type*, IVal>> mismatches{
        {fx.builder.GetInt8Ty(), IBool{true}},
        {fx.builder.GetInt16Ty(), IUInt8{1}},
        {fx.builder.GetInt32Ty(), ITuple{{}}},
        {fx.builder.GetInt64Ty(), IFloat64{1.0}},
        {fx.builder.GetIntNativeTy(), IBool{false}},
        {fx.builder.GetUInt8Ty(), IInt8{-1}},
        {fx.builder.GetUInt16Ty(), IUInt32{2}},
        {fx.builder.GetUInt32Ty(), IBool{true}},
        {fx.builder.GetUInt64Ty(), IInt64{-1}},
        {fx.builder.GetUIntNativeTy(), IBool{true}},
        {fx.builder.GetFloat16Ty(), IUInt8{1}},
        {fx.builder.GetFloat32Ty(), IBool{true}},
        {fx.builder.GetFloat64Ty(), ITuple{{}}},
        {fx.builder.GetRuneTy(), IBool{true}},
        {fx.builder.GetBoolTy(), IInt32{1}},
    };
    for (size_t index = 0; index < mismatches.size(); ++index) {
        const auto& [ty, wrongVal] = mismatches[index];
        auto* constant = fx.converter.TryConvertToConstant(*ty, wrongVal, *fx.entry);
        EXPECT_EQ(constant, nullptr) << "mismatch case " << index;
    }
}

TEST(ConstEvalTest, MatchingPrimitiveValuesStillConvert)
{
    Fixture fx("match");
    const std::vector<std::pair<Type*, IVal>> matches{
        {fx.builder.GetInt8Ty(), IInt8{-1}},
        {fx.builder.GetInt64Ty(), IInt64{-5}},
        {fx.builder.GetUInt8Ty(), IUInt8{7}},
        {fx.builder.GetUInt64Ty(), IUInt64{9}},
        {fx.builder.GetFloat64Ty(), IFloat64{1.5}},
        {fx.builder.GetRuneTy(), IRune{U'x'}},
        {fx.builder.GetBoolTy(), IBool{true}},
    };
    for (const auto& [ty, val] : matches) {
        auto* constant = fx.converter.TryConvertToConstant(*ty, val, *fx.entry);
        ASSERT_NE(constant, nullptr);
    }
}

// An array position holding a non-array value (for example a tuple from a
// corrupt cjo) is not foldable; the same shape with a real array still builds
// its VArray expression.
TEST(ConstEvalTest, VArrayPositionRejectsNonArrayValue)
{
    Fixture fx("varray");
    auto* varrayTy = fx.builder.GetType<VArrayType>(fx.builder.GetInt64Ty(), 2);

    std::function<void(Expression*)> insert = [](Expression*) {};
    auto* mismatch = fx.converter.ConvertToChir(*varrayTy, ITuple{{IInt64{1}}}, insert, *fx.entry);
    EXPECT_EQ(mismatch, nullptr);

    IArray array;
    array.content.push_back(IInt64{1});
    array.content.push_back(IInt64{2});
    auto* converted = fx.converter.ConvertToChir(*varrayTy, array, insert, *fx.entry);
    EXPECT_NE(converted, nullptr);
}

// ConvertToChir is the production entry point. A primitive whose value does
// not match the type must be reported as not foldable instead of reaching the
// unsupported-kind path.
TEST(ConstEvalTest, PrimitiveMismatchAtConvertToChirYieldsNull)
{
    Fixture fx("toplevel-mismatch");
    std::function<void(Expression*)> insert = [](Expression*) {};
    EXPECT_EQ(fx.converter.ConvertToChir(*fx.builder.GetInt64Ty(), IBool{true}, insert, *fx.entry), nullptr);
}

// A reference decoded from a foreign cjo may carry no pointer/object
// representation at all, or may name a class the class table does not hold.
// Both are not foldable rather than an abort.
TEST(ConstEvalTest, ReferenceWithUnexpectedRepresentationYieldsNull)
{
    Fixture fx("ref-mismatch");
    auto* classDef = fx.builder.CreateClass(INVALID_LOCATION, "C", "C", "default", true, false);
    auto* classTy = fx.builder.GetType<ClassType>(classDef);
    classDef->SetType(*classTy);
    auto* refTy = fx.builder.GetType<RefType>(classTy);

    std::function<void(Expression*)> insert = [](Expression*) {};
    EXPECT_EQ(fx.converter.ConvertToChir(*refTy, ITuple{{}}, insert, *fx.entry), nullptr);

    IObject object{999U, {}};
    IVal objectVal = object;
    IPointer pointer{&objectVal};
    IVal pointerVal = pointer;
    EXPECT_EQ(fx.converter.ConvertToChir(*refTy, pointerVal, insert, *fx.entry), nullptr);
}

// A pointer constant whose content is null is a shape only a damaged cjo can
// produce. Like the String path, the value is not foldable; the pointer must
// not be dereferenced.
TEST(ConstEvalTest, ReferenceWithNullPointerContentYieldsNull)
{
    Fixture fx("ref-null-content");
    auto* classDef = fx.builder.CreateClass(INVALID_LOCATION, "C", "C", "default", true, false);
    auto* classTy = fx.builder.GetType<ClassType>(classDef);
    classDef->SetType(*classTy);
    auto* refTy = fx.builder.GetType<RefType>(classTy);

    std::function<void(Expression*)> insert = [](Expression*) {};
    IPointer pointer{nullptr};
    IVal pointerVal = pointer;
    EXPECT_EQ(fx.converter.ConvertToChir(*refTy, pointerVal, insert, *fx.entry), nullptr);
}

// A tuple value from a corrupt cjo can disagree with its declared type on the
// element count. The declared types bound the conversion loop, so any
// disagreement must answer null instead of indexing past them; a matching
// shape still converts.
TEST(ConstEvalTest, TupleValueShapeMismatchYieldsNull)
{
    Fixture fx("tuple-shape");
    auto* tupleTy = fx.builder.GetType<TupleType>(std::vector<Type*>{fx.builder.GetInt64Ty()}, ModalInfo{});

    std::function<void(Expression*)> insert = [](Expression*) {};
    // More elements than the type describes.
    EXPECT_EQ(fx.converter.ConvertToChir(*tupleTy, ITuple{{IInt64{1}, IInt64{2}}}, insert, *fx.entry), nullptr);
    // Fewer elements than the type describes.
    EXPECT_EQ(fx.converter.ConvertToChir(*tupleTy, ITuple{{}}, insert, *fx.entry), nullptr);
    // The matching shape still produces the tuple expression.
    EXPECT_NE(fx.converter.ConvertToChir(*tupleTy, ITuple{{IInt64{1}}}, insert, *fx.entry), nullptr);
}
