// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements codegen for CHIR Tuple creation.
 */

#include "Base/TupleExprImpl.h"

#include <optional>

#include "Base/CGTypes/CGEnumType.h"
#include "CGModule.h"
#include "CJNative/CGTypes/EnumCtorTIOrTTGenerator.h"
#include "IRBuilder.h"
#include "Utils/CGUtils.h"
#include "cangjie/CHIR/Expression/Terminator.h"
#include "cangjie/CHIR/Type/Type.h"

using namespace Cangjie;
using namespace CodeGen;

llvm::Value* CodeGen::GenerateStruct(IRBuilder2& irBuilder, const CHIR::Tuple& tuple)
{
    auto chirType = tuple.GetResult()->GetType();
    CJC_ASSERT(chirType->IsStruct() && "Should not reach here.");

    auto structType = StaticCast<CHIR::StructType*>(chirType);
    auto cgStructType = CGType::GetOrCreate(irBuilder.GetCGModule(), chirType)->GetLLVMType();
#ifdef NDEBUG
    auto structVal = irBuilder.CreateEntryAlloca(cgStructType, nullptr, "struct");
#else
    auto structVal =
        irBuilder.CreateEntryAlloca(cgStructType, nullptr, "struct." + tuple.GetResult()->GetIdentifierWithoutPrefix());
#endif

    auto& cgMod = irBuilder.GetCGModule();
    auto& cgCtx = irBuilder.GetCGContext();

    for (unsigned i = 0; i < tuple.GetOperands().size(); i++) {
        auto cgVal = *(cgMod | tuple.GetOperand(i));
        CHIR::Type* fieldType = structType->GetCustomTypeDef()->GetInstanceVar(i).type;
        auto gepType = CGType::GetOrCreate(cgMod, CGType::GetRefTypeOf(cgCtx.GetCHIRBuilder(), *fieldType));
        auto cgAddr = CGValue(irBuilder.CreateStructGEP(cgStructType, structVal, i), gepType);
        irBuilder.CreateStore(cgVal, cgAddr);
    }

    return structVal;
}

llvm::Value* CodeGen::GenerateNativeTuple(IRBuilder2& irBuilder, const CHIR::Tuple& tuple)
{
    auto& cgMod = irBuilder.GetCGModule();

    auto tupleType = StaticCast<CHIR::TupleType*>(tuple.GetResult()->GetType());
    auto cgTupleType = CGType::GetOrCreate(cgMod, tupleType);

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    // With CJNATIVE-BE, we can enable the optimization for constant tuple.
    // As for a constant tuple, it can be directly represented by a const variable
    // rather than being created by calling ctor.
    auto [isConstantTuple, serialized] = IsConstantTuple(tuple);
    if (isConstantTuple) {
        std::vector<llvm::Value*> args;
        for (unsigned i = 0; i < tuple.GetOperands().size(); i++) {
            args.push_back((cgMod | tuple.GetOperand(i))->GetRawValue());
        }
        std::vector<llvm::Constant*> cons;
        std::for_each(args.begin(), args.end(), [&cons](llvm::Value* it) {
            auto tmp = llvm::dyn_cast<llvm::GlobalVariable>(it);
            return cons.emplace_back(tmp ? tmp->getInitializer() : llvm::cast<llvm::Constant>(it));
        });
        auto constVal = llvm::ConstantStruct::get(llvm::cast<llvm::StructType>(cgTupleType->GetLLVMType()), cons);
        return cgMod.GetOrCreateGlobalVariable(constVal, serialized, true);
    }
#endif
    auto& cgCtx = cgMod.GetCGContext();
    auto res = irBuilder.CreateEntryAlloca(*cgTupleType);
    auto tupleCGValue =
        CGValue(res, CGType::GetOrCreate(cgMod, CGType::GetRefTypeOf(cgCtx.GetCHIRBuilder(), *tupleType)));
    for (unsigned i = 0; i < tuple.GetOperands().size(); i++) {
        auto cgVal = *(cgMod | tuple.GetOperand(i));
        auto cgAddr = irBuilder.CreateGEP(tupleCGValue, {i});
        if (res->getType()->getPointerAddressSpace() == 1U) {
            cgCtx.SetBasePtr(cgAddr.GetRawValue(), res);
        }
        irBuilder.CreateStore(cgVal, cgAddr);
    }
    return res;
}
namespace {
llvm::Value* GenerateOptionLikeT(IRBuilder2& irBuilder, const CHIR::Tuple& tuple)
{
    auto& cgMod = irBuilder.GetCGModule();
    auto p1i8 = irBuilder.getInt8PtrTy(1U);
    auto chirEnumType = StaticCast<CHIR::EnumType*>(tuple.GetResult()->GetType());
    auto enumTi = irBuilder.CreateTypeInfo(chirEnumType);
    auto enumVal = irBuilder.CallIntrinsicAllocaGeneric({enumTi, irBuilder.GetSizeFromTypeInfo(enumTi)});
    auto payload = irBuilder.GetPayloadFromObject(enumVal);
    auto castedPayload = irBuilder.CreateBitCast(payload, p1i8);
    auto associatedType = chirEnumType->GetTypeArgs()[0];
    bool hasAssociatedValue = tuple.GetNumOfOperands() > 1;
    auto isRef = irBuilder.CreateTypeInfoIsReferenceCall(*associatedType);
    auto [refBB, nonRefBB, endBB] = Vec2Tuple<3>(irBuilder.CreateAndInsertBasicBlocks({"ref", "nonRef", "endBB"}));
    irBuilder.CreateCondBr(isRef, refBB, nonRefBB);
    irBuilder.SetInsertPoint(refBB);
    if (hasAssociatedValue) {
        (void)irBuilder.CallGCWrite({**(cgMod | tuple.GetOperand(1)), enumVal, castedPayload});
    } else {
        (void)irBuilder.CallGCWrite({llvm::Constant::getNullValue(p1i8), enumVal, castedPayload});
    }
    irBuilder.CreateBr(endBB);

    irBuilder.SetInsertPoint(nonRefBB);
    (void)irBuilder.CreateStore(
        **(cgMod | tuple.GetOperand(0)), irBuilder.CreateBitCast(payload, irBuilder.getInt1Ty()->getPointerTo(1U)));
    if (hasAssociatedValue) {
        auto associatedValOffset = irBuilder.CallIntrinsicGetFieldOffset({enumTi, irBuilder.getInt64(1U)});
        payload = irBuilder.CreateInBoundsGEP(irBuilder.getInt8Ty(), castedPayload, associatedValOffset);
        auto associatedTypeSize = irBuilder.GetLayoutSize_32(*associatedType);
        (void)irBuilder.CallIntrinsicGCWriteGeneric(
            {enumVal, payload, **(cgMod | tuple.GetOperand(1)), associatedTypeSize});
    }
    irBuilder.CreateBr(endBB);

    irBuilder.SetInsertPoint(endBB);
    return enumVal;
}

llvm::Value* GenerateOptionLikeRef(IRBuilder2& irBuilder, const CHIR::Tuple& tuple)
{
    auto& cgMod = irBuilder.GetCGModule();
    // On CHIR side, non-constructor enum would be represented by a tuple with only one ArgStrict.
    auto isNone = tuple.GetNumOfOperands() <= 1;
    if (isNone) {
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
        return llvm::Constant::getNullValue(CGType::GetRefType(irBuilder.GetLLVMContext()));
#endif
    }
    auto someOp = tuple.GetOperand(1);
    return irBuilder.CallIntrinsicRef2Null(**(cgMod | someOp));
}

llvm::Value* GenerateOptionLikeNonRef(IRBuilder2& irBuilder, const CHIR::Tuple& tuple)
{
    auto& cgMod = irBuilder.GetCGModule();
    bool hasAssociatedValue = tuple.GetNumOfOperands() > 1;
    auto chirEnumType = StaticCast<CHIR::EnumType*>(tuple.GetResult()->GetType());
    auto cgEnumType = StaticCast<CGEnumType*>(CGType::GetOrCreate(cgMod, chirEnumType));
    if (cgEnumType->GetSize()) {
        auto enumVal = irBuilder.CreateEntryAlloca(*cgEnumType, "enum.val");
        auto ptr0 = irBuilder.CreateStructGEP(cgEnumType->GetLLVMType(), enumVal, 0);
        (void)irBuilder.CreateStore(**(cgMod | tuple.GetOperand(0)), ptr0);
        auto associatedValueType = cgEnumType->GetAssociatedValueTypeOfOptionLike();
        auto recordVal =
            hasAssociatedValue ? **(cgMod | tuple.GetOperand(1)) : irBuilder.CreateNullValue(*associatedValueType);
        irBuilder.CreateStore(
            recordVal, irBuilder.CreateStructGEP(cgEnumType->GetLLVMType(), enumVal, 1), associatedValueType);
        return enumVal;
    } else {
        auto enumTi = irBuilder.CreateTypeInfo(chirEnumType);
        auto enumVal = irBuilder.CallIntrinsicAllocaGeneric({enumTi, irBuilder.GetSizeFromTypeInfo(enumTi)});
        auto payload = irBuilder.GetPayloadFromObject(enumVal);
        (void)irBuilder.CreateStore(**(cgMod | tuple.GetOperand(0)),
            irBuilder.CreateBitCast(payload, irBuilder.getInt1Ty()->getPointerTo(1U)));
        if (hasAssociatedValue) {
            auto associatedValOffset = irBuilder.CallIntrinsicGetFieldOffset({enumTi, irBuilder.getInt64(1U)});
            payload = irBuilder.CreateInBoundsGEP(irBuilder.getInt8Ty(),
                irBuilder.CreateBitCast(payload, irBuilder.getInt8PtrTy(1U)), associatedValOffset);
            auto associatedType = chirEnumType->GetTypeArgs()[0];
            auto associatedTypeSize = irBuilder.GetLayoutSize_32(*associatedType);
            (void)irBuilder.CallIntrinsicGCWriteGeneric(
                {enumVal, payload, **(cgMod | tuple.GetOperand(1)), associatedTypeSize});
        }
        return enumVal;
    }
}

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
llvm::Value* GenerateAssociatedNonRefEnum(IRBuilder2& irBuilder, const CHIR::Tuple& tuple)
{
    auto& cgMod = irBuilder.GetCGModule();
    auto i8Ty = irBuilder.getInt8Ty();
    irBuilder.EmitLocation(CHIRExprWrapper(tuple));
    // 1. allocate memory for the Enum's constructor.
    auto chirEnumType = StaticCast<CHIR::EnumType*>(tuple.GetResult()->GetType());
    auto cgEnumType = StaticCast<CGEnumType*>(CGType::GetOrCreate(cgMod, chirEnumType));
    auto enumVal = irBuilder.CreateEntryAlloca(*cgEnumType, "enum.val");
    // 2. store the tag and associated values into the memory.
    auto offset = 0U;
    auto casted = irBuilder.CreateBitCast(enumVal, i8Ty->getPointerTo());
    for (unsigned idx = 0U; idx < tuple.GetNumOfOperands(); ++idx) {
        auto field = tuple.GetOperand(idx);
        auto fieldCGType = CGType::GetOrCreate(cgMod, field->GetType());
        auto destPtr = irBuilder.CreateConstGEP1_32(i8Ty, casted, offset);
        auto castedDestPtr = irBuilder.CreateBitCast(destPtr, fieldCGType->GetLLVMType()->getPointerTo());
        (void)irBuilder.CreateStore(**(cgMod | field), castedDestPtr, field->GetType());
        offset += fieldCGType->GetSize().value();
    }
    return enumVal;
}

llvm::Value* GenerateCommonEnum(IRBuilder2& irBuilder, const CHIR::Tuple& tuple)
{
    auto& cgMod = irBuilder.GetCGModule();
    auto& cgCtx = cgMod.GetCGContext();
    irBuilder.EmitLocation(CHIRExprWrapper(tuple));
    // 1. get typeInfo of the Enum's constructor.
    auto enumType = StaticCast<CHIR::EnumType*>(tuple.GetResult()->GetType());
    auto enumIdx = StaticCast<CHIR::Constant*>(StaticCast<CHIR::LocalVar*>(tuple.GetOperand(0))->GetExpr());
    llvm::Value* enumCaseTi = cgMod.GetOrCreateEnumCtorTIOrTT(*enumType, enumIdx->GetUnsignedIntLitVal());
    const auto& genericArgs = GetGenericArgsFromCHIRType(*enumType);
    if (!genericArgs.empty()) {
        std::vector<CHIR::Type*> typeArgs;
        for (auto typeArg : GetGenericArgsFromCHIRType(*enumType->GetEnumDef()->GetType())) {
            auto arg = GetTypeInnerType(*enumType, typeArg);
            (void)typeArgs.emplace_back(arg);
        }
        auto typeArgsArr = irBuilder.CreateTypeInfoArray(
            typeArgs, cgMod.GetOrInsertCGFunction(tuple.GetTopLevelFunc())->genericParamsMap);
        enumCaseTi = irBuilder.CallIntrinsicGetTypeInfo({enumCaseTi, irBuilder.getInt32(typeArgs.size()), typeArgsArr});
    }
    // 2. allocate memory for the Enum's constructor.
    auto enumVal = irBuilder.CallClassIntrinsicAlloc({enumCaseTi, irBuilder.GetSizeFromTypeInfo(enumCaseTi)});
    // 3. store tag and associated values
    if (genericArgs.empty()) {
        std::vector<llvm::Type*> elemTypes;
        for (unsigned idx = 0; idx < tuple.GetNumOfOperands(); ++idx) {
            elemTypes.emplace_back(CGType::GetOrCreate(cgMod, tuple.GetOperand(idx)->GetType())->GetLLVMType());
        }
        auto layoutType = llvm::StructType::get(cgCtx.GetLLVMContext(), elemTypes);

        auto payload = irBuilder.GetPayloadFromObject(enumVal);
        auto castedEnumValPtr = irBuilder.CreateBitCast(payload, layoutType->getPointerTo(1U));
        for (unsigned idx = 0; idx < tuple.GetNumOfOperands(); ++idx) {
            auto field = tuple.GetOperand(idx);
            auto fieldType = field->GetType();
            auto fieldAddrType = CGType::GetOrCreate(cgMod, CGType::GetRefTypeOf(cgCtx.GetCHIRBuilder(), *fieldType));
            auto fieldPtr = irBuilder.CreateStructGEP(layoutType, castedEnumValPtr, idx);
            cgCtx.SetBasePtr(fieldPtr, enumVal);
            auto cgAddr = CGValue(fieldPtr, fieldAddrType);
            irBuilder.CreateStore(*(cgMod | field), cgAddr);
        }
    } else {
        for (unsigned idx = 0; idx < tuple.GetNumOfOperands(); ++idx) {
            auto field = tuple.GetOperand(idx);
            auto fieldType = field->GetType();
            auto fieldAddrType = CGType::GetOrCreate(cgMod, CGType::GetRefTypeOf(cgCtx.GetCHIRBuilder(), *fieldType));
            llvm::Value* fieldOffset = irBuilder.CallIntrinsicGetFieldOffset({enumCaseTi, irBuilder.getInt64(idx)});
            fieldOffset =
                irBuilder.CreateAdd(fieldOffset, llvm::ConstantInt::get(fieldOffset->getType(), irBuilder.GetVoidPtrSize()));
            auto fieldAddr = irBuilder.CreateInBoundsGEP(irBuilder.getInt8Ty(), enumVal, fieldOffset);
            if (!fieldType->IsGeneric()) {
                auto fieldCGType = CGType::GetOrCreate(cgMod, fieldType);
                fieldAddr = irBuilder.CreateBitCast(fieldAddr,
                    fieldCGType->GetLLVMType()->getPointerTo(fieldAddr->getType()->getPointerAddressSpace()));
            }
            cgCtx.SetBasePtr(fieldAddr, enumVal);
            auto cgAddr = CGValue(fieldAddr, fieldAddrType);
            irBuilder.CreateStore(*(cgMod | field), cgAddr);
        }
    }
    return enumVal;
}
#endif
} // namespace

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
llvm::Value* CodeGen::GenerateEnum(IRBuilder2& irBuilder, const CHIR::Tuple& tuple)
{
    auto chirEnumType = StaticCast<CHIR::EnumType*>(tuple.GetResult()->GetType());
    auto cgEnumType = StaticCast<CGEnumType*>(CGType::GetOrCreate(irBuilder.GetCGModule(), chirEnumType));
    CJC_ASSERT(!cgEnumType->IsTrivial() && "Trivial-Enum should not reach here.");

    if (cgEnumType->IsCommonEnum()) {
        return GenerateCommonEnum(irBuilder, tuple);
    } else if (cgEnumType->IsOptionLikeT()) {
        return GenerateOptionLikeT(irBuilder, tuple);
    } else if (cgEnumType->IsOptionLikeRef()) {
        return GenerateOptionLikeRef(irBuilder, tuple);
    } else if (cgEnumType->IsOptionLikeNonRef()) {
        return GenerateOptionLikeNonRef(irBuilder, tuple);
    } else if (cgEnumType->IsZeroSizeEnum()) {
        return irBuilder.CreateEntryAlloca(*cgEnumType);
    } else if (cgEnumType->IsAllAssociatedValuesAreNonRef()) {
        return GenerateAssociatedNonRefEnum(irBuilder, tuple);
    }
    CJC_ASSERT(false && "Should not reach here: UNKNOWN enum kind");
    return nullptr;
}
#endif
