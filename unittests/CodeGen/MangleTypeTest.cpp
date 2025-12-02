// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "CGTest.h"
#include "cangjie/CHIR/IR/Type/Type.h"
#include "cangjie/CodeGen/CGUtils.h"

using TypeKind = Cangjie::CHIR::Type::TypeKind;
using namespace Cangjie::CodeGen;

TEST_F(MangleTypeTest, BuiltinTypes)
{
    EXPECT_EQ(MangleType(*int8Ty), "a");
    EXPECT_EQ(MangleType(*int16Ty), "s");
    EXPECT_EQ(MangleType(*int32Ty), "i");
    EXPECT_EQ(MangleType(*int64Ty), "l");
    EXPECT_EQ(MangleType(*intNativeTy), "q");

    EXPECT_EQ(MangleType(*uint8Ty), "h");
    EXPECT_EQ(MangleType(*uint16Ty), "t");
    EXPECT_EQ(MangleType(*uint32Ty), "j");
    EXPECT_EQ(MangleType(*uint64Ty), "m");
    EXPECT_EQ(MangleType(*uintNativeTy), "r");

    EXPECT_EQ(MangleType(*float16Ty), "Dh");
    EXPECT_EQ(MangleType(*float32Ty), "f");
    EXPECT_EQ(MangleType(*float64Ty), "d");

    EXPECT_EQ(MangleType(*runeTy), "c");
    EXPECT_EQ(MangleType(*boolTy), "b");

    EXPECT_EQ(MangleType(*unitTy), "u");
    EXPECT_EQ(MangleType(*nothingTy), "n");
    EXPECT_EQ(MangleType(*cstringTy), "k");
}

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
TEST_F(MangleTypeTest, CustomTypes)
{
    // construct a ClassType a.Alpha
    auto classDef = builder.CreateClass(defaultLoc, "Alpha", "_CN1a5AlphaE", "a", true, true);
    auto classTy = builder.GetType<ClassType>(classDef);
    EXPECT_EQ(MangleType(*classTy), "_CCN1a5AlphaE");
    // construct the StructType: a.SomeStruct
    auto structDef = builder.CreateStruct(defaultLoc, "Some", "_CN1a10SomeStructE", "a", true);
    auto structTy = builder.GetType<StructType>(structDef);
    EXPECT_EQ(MangleType(*structTy), "Rrecord._CN1a10SomeStructE");
}
#endif

TEST_F(MangleTypeTest, FuncTypes)
{
    // construct a funcTy (Int64, Int64) -> Int64
    auto funcTy = builder.GetType<FuncType>(std::vector<Type*>{int64Ty, int64Ty}, int64Ty);
    EXPECT_EQ(MangleType(*funcTy), "lll");
}

TEST_F(MangleTypeTest, TupleTypes)
{
    // construct a tupleType (Int8, Int16, Int32)
    auto tupleTy = builder.GetType<TupleType>(std::vector<Type*>{int8Ty, int16Ty, int32Ty});
    EXPECT_EQ(MangleType(*tupleTy), "T3_asiE");
    // construct a tupleType ((Int8, Int16, Int32), Int64)
    auto nestedTupleTy = builder.GetType<TupleType>(std::vector<Type*>{tupleTy, int64Ty});
    EXPECT_EQ(MangleType(*nestedTupleTy), "T2_T3_asiElE");
}

TEST_F(MangleTypeTest, RawArrayTypes)
{
    // construct a RawArrayType Int8[3]
    auto rawArrayTy = builder.GetType<RawArrayType>(int8Ty, 3);
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    EXPECT_EQ(MangleType(*rawArrayTy), "A3_aE");
#endif
}

TEST_F(MangleTypeTest, RefTypes)
{
    // construct a RefType of Int8
    auto refTy = builder.GetType<RefType>(int8Ty);
    EXPECT_EQ(MangleType(*refTy), "a");
}