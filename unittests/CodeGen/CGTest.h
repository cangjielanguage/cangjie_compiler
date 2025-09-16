// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the BasicTest in CG.
 */

#ifndef CANGJIE_CG_TEST_H
#define CANGJIE_CG_TEST_H
#include <gtest/gtest.h>

#include "cangjie/CHIR/CHIRBuilder.h"
#include "cangjie/CHIR/CHIRContext.h"
#include "cangjie/CHIR/Type/ClassDef.h"
#include "cangjie/CHIR/Type/Type.h"

using namespace Cangjie::CHIR;

class CGTestTemplate : public ::testing::Test {
protected:
    CGTestTemplate() : Test(){};
    void SetUp() override{};
    void TearDown() override{};
};

// To construct CHIR types
class MangleTypeTest : public CGTestTemplate {
protected:
    MangleTypeTest() : cctx(&fileNameMap), builder(cctx)
    {
        int8Ty = builder.GetInt8Ty();
        int16Ty = builder.GetInt16Ty();
        int32Ty = builder.GetInt32Ty();
        int64Ty = builder.GetInt64Ty();
        intNativeTy = builder.GetIntNativeTy();

        uint8Ty = builder.GetUInt8Ty();
        uint16Ty = builder.GetUInt16Ty();
        uint32Ty = builder.GetUInt32Ty();
        uint64Ty = builder.GetUInt64Ty();
        uintNativeTy = builder.GetUIntNativeTy();

        float16Ty = builder.GetFloat16Ty();
        float32Ty = builder.GetFloat32Ty();
        float64Ty = builder.GetFloat64Ty();

        runeTy = builder.GetType<RuneType>();
        boolTy = builder.GetType<BooleanType>();
        unitTy = builder.GetType<UnitType>();
        nothingTy = builder.GetType<NothingType>();
        cstringTy = builder.GetType<CStringType>();
    }

    std::unordered_map<unsigned int, std::string> fileNameMap;
    CHIRContext cctx;
    CHIRBuilder builder;

    Type *int8Ty, *int16Ty, *int32Ty, *int64Ty, *intNativeTy, *uint8Ty, *uint16Ty, *uint32Ty, *uint64Ty, *uintNativeTy;
    Type *float16Ty, *float32Ty, *float64Ty;
    Type* runeTy;
    Type* boolTy;
    Type* unitTy;
    Type* nothingTy;
    Type* cstringTy;
    const std::string testFile{"test.cj"};
    DebugLocation defaultLoc{testFile, 1, {1, 1}, {1, 1}, {0}};
};
#endif // CANGJIE_CG_TEST_H