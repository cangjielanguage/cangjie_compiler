// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements codegen for CHIR InstanceOf.
 */

#include "Base/InstanceOfImpl.h"

#include "CGModule.h"
#include "IRBuilder.h"
#include "cangjie/CHIR/Expression/Terminator.h"
#include "cangjie/CHIR/Type/ClassDef.h"
#include "cangjie/CHIR/Type/Type.h"
#include "cangjie/CHIR/Value.h"

using namespace Cangjie;
using namespace CodeGen;

llvm::Value* CodeGen::GenerateInstanceOf(IRBuilder2& irBuilder, const CHIR::InstanceOf& instanceOf)
{
    // Match pattern: type match.
    auto object = instanceOf.GetObject();
    auto targetCHIRType = instanceOf.GetType();
    auto targetTi = irBuilder.CreateTypeInfo(*targetCHIRType);
    auto objectCHIRType = DeRef(*object->GetType());
    auto objectVal = *(irBuilder.GetCGModule() | object);
    if (objectCHIRType->IsAny() || objectCHIRType->IsGeneric()) {
        auto instanceTi = irBuilder.GetTypeInfoFromObject(*objectVal);
        auto typeKind = irBuilder.GetTypeKindFromTypeInfo(instanceTi);
        auto isTuple = irBuilder.CreateICmpEQ(typeKind, irBuilder.getInt8(static_cast<uint8_t>(UGTypeKind::UG_TUPLE)));
        auto [isTupleBB, nonTupleBB, endBB] =
            Vec2Tuple<3>(irBuilder.CreateAndInsertBasicBlocks({"isTuple", "nonTuple", "end"}));
        irBuilder.CreateCondBr(isTuple, isTupleBB, nonTupleBB);

        irBuilder.SetInsertPoint(isTupleBB);
        auto nullPtr = llvm::Constant::getNullValue(irBuilder.getInt8PtrTy());
        auto isTupleRet = irBuilder.CallIntrinsicIsTupleTypeOf({*objectVal, nullPtr, targetTi});
        irBuilder.CreateBr(endBB);

        irBuilder.SetInsertPoint(nonTupleBB);
        auto nonTupleRet = irBuilder.CallIntrinsicIsSubtype({instanceTi, targetTi});
        irBuilder.CreateBr(endBB);

        irBuilder.SetInsertPoint(endBB);
        auto phi = irBuilder.CreatePHI(irBuilder.getInt1Ty(), 2U);
        phi->addIncoming(isTupleRet, isTupleBB);
        phi->addIncoming(nonTupleRet, nonTupleBB);
        return phi;
    } else if (objectCHIRType->IsClass()) {
        auto instanceTi = irBuilder.GetTypeInfoFromObject(*objectVal);
        return irBuilder.CallIntrinsicIsSubtype({instanceTi, targetTi});
    } else if (objectCHIRType->IsTuple()) {
        auto i8PtrTy = llvm::Type::getInt8PtrTy(irBuilder.GetLLVMContext());
        auto instanceTi = objectVal.GetCGType()->GetSize().has_value() ? irBuilder.CreateTypeInfo(objectCHIRType)
                                                                       : llvm::Constant::getNullValue(i8PtrTy);
        return irBuilder.CallIntrinsicIsTupleTypeOf({*objectVal, instanceTi, targetTi});
    }
    return irBuilder.CallIntrinsicIsSubtype({irBuilder.CreateTypeInfo(objectCHIRType), targetTi});
}
