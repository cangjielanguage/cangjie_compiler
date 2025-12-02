// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements codegen for CHIR TypeCast.
 */

#include "Base/TypeCastImpl.h"

#include <optional>

#include "Base/CGTypes/CGEnumType.h"
#include "CGModule.h"
#include "IRBuilder.h"
#include "Utils/CGCommonDef.h"
#include "Utils/CGUtils.h"
#include "cangjie/CHIR/IR/Type/Type.h"

using namespace Cangjie;
using namespace CodeGen;

namespace {
std::string StringIntX(Triple::ArchType archType, const CHIR::IntType& intTy)
{
    if (archType == Triple::ArchType::X86_64 || archType == Triple::ArchType::AARCH64) {
        if (intTy.IsIntNative()) {
            return "Int64";
        } else if (intTy.IsUIntNative()) {
            return "UInt64";
        }
        return intTy.ToString();
    } else if (archType == Triple::ArchType::ARM32) {
        if (intTy.IsIntNative()) {
            return "Int32";
        } else if (intTy.IsUIntNative()) {
            return "UInt32";
        }
        return intTy.ToString();
    }
    CJC_ASSERT(false && "Unsupported ArchType.");
    return intTy.ToString();
}

// Key: AST::sourceTyString() + targetTy.String()
// Value: [lowerBound, upperBound]
const std::unordered_map<std::string, std::pair<uint64_t, uint64_t>> G_FLOAT2INT_BOUND_MAP = {
    {"Float16Int8", {0xC060200000000000, 0x4060000000000000}},
    {"Float16Int16", {0xC0E0040000000000, 0x40E0000000000000}},
    {"Float16Int32", {0x0000000000000000, 0x0000000000000000}}, // There must be no overflow
    {"Float16Int64", {0x0000000000000000, 0x0000000000000000}}, // There must be no overflow
    {"Float16UInt8", {0xBFF0000000000000, 0x4070000000000000}},
    {"Float16UInt16", {0xBFF0000000000000, 0x40F0000000000000}},
    {"Float16UInt32", {0xBFF0000000000000, 0x0000000000000000}}, // There must be no upper bound overflow.
    {"Float16UInt64", {0xBFF0000000000000, 0x0000000000000000}}, // There must be no upper bound overflow.

    {"Float32Int8", {0xC060200000000000, 0x4060000000000000}},
    {"Float32Int16", {0xC0E0002000000000, 0x40E0000000000000}},
    {"Float32Int32", {0xC1E0000020000000, 0x41E0000000000000}},
    {"Float32Int64", {0xC3E0000020000000, 0x43E0000000000000}},
    {"Float32UInt8", {0xBFF0000000000000, 0x4070000000000000}},
    {"Float32UInt16", {0xBFF0000000000000, 0x40F0000000000000}},
    {"Float32UInt32", {0xBFF0000000000000, 0x41F0000000000000}},
    {"Float32UInt64", {0xBFF0000000000000, 0x43F0000000000000}},

    {"Float64Int8", {0xC060200000000000, 0x4060000000000000}},
    {"Float64Int16", {0xC0E0002000000000, 0x40E0000000000000}},
    {"Float64Int32", {0xC1E0000000200000, 0x41E0000000000000}},
    {"Float64Int64", {0xC3E0000000000001, 0x43E0000000000000}},
    {"Float64UInt8", {0xBFF0000000000000, 0x4070000000000000}},
    {"Float64UInt16", {0xBFF0000000000000, 0x40F0000000000000}},
    {"Float64UInt32", {0xBFF0000000000000, 0x41F0000000000000}},
    {"Float64UInt64", {0xBFF0000000000000, 0x43F0000000000000}},
};

bool NeedsIntToUIntConversion(IRBuilder2& irBuilder, const CHIR::IntType& srcTy, const CHIR::Type::TypeKind typeKind)
{
    auto srcTypeKind = irBuilder.GetTypeKindFromType(srcTy);
    switch (srcTypeKind) {
        case CHIR::Type::TypeKind::TYPE_INT16:
            return typeKind >= CHIR::Type::TypeKind::TYPE_UINT16;
        case CHIR::Type::TypeKind::TYPE_INT32:
            return typeKind >= CHIR::Type::TypeKind::TYPE_UINT32;
        case CHIR::Type::TypeKind::TYPE_INT64:
            return typeKind >= CHIR::Type::TypeKind::TYPE_UINT64;
        case CHIR::Type::TypeKind::TYPE_INT_NATIVE:
            return typeKind >= CHIR::Type::TypeKind::TYPE_UINT64;
        default:
            return true;
    }
}

bool NeedsUIntToIntConversion(IRBuilder2& irBuilder, const CHIR::Type& srcTy, const CHIR::Type::TypeKind typeKind)
{
    auto srcTypeKind = irBuilder.GetTypeKindFromType(srcTy);
    switch (srcTypeKind) {
        case CHIR::Type::TypeKind::TYPE_UINT8:
            return typeKind > CHIR::Type::TypeKind::TYPE_INT8;
        case CHIR::Type::TypeKind::TYPE_UINT16:
            return typeKind > CHIR::Type::TypeKind::TYPE_INT16;
        case CHIR::Type::TypeKind::TYPE_UINT32:
            return typeKind > CHIR::Type::TypeKind::TYPE_INT32;
        default:
            return false;
    }
}

llvm::Value* GenerateIntegerToFloatConvExpr(
    IRBuilder2& irBuilder, llvm::Value* srcValue, llvm::Type* targetType, const CHIR::IntType& srcTy)
{
    if (srcTy.IsSigned()) {
        return irBuilder.CreateSIToFP(srcValue, targetType);
    }
    return irBuilder.CreateUIToFP(srcValue, targetType);
}

llvm::Value* GenerateFloatConvExpr(IRBuilder2& irBuilder, llvm::Value* srcValue, llvm::Type* targetType,
    const CHIR::NumericType& srcTy, const CHIR::NumericType& targetTy)
{
    if (targetTy.GetBitness() > srcTy.GetBitness()) {
        return irBuilder.CreateFPExt(srcValue, targetType);
    }
    return irBuilder.CreateFPTrunc(srcValue, targetType);
}

inline bool IsUInt2Int(const CHIR::IntType& srcTy, const CHIR::IntType& targetTy)
{
    return !srcTy.IsSigned() && targetTy.IsSigned();
}

inline bool IsInt2UInt(const CHIR::IntType& srcTy, const CHIR::IntType& targetTy)
{
    return srcTy.IsSigned() && !targetTy.IsSigned();
}

inline bool IsInt2Int(const CHIR::IntType& srcTy, const CHIR::IntType& targetTy)
{
    return srcTy.IsSigned() && targetTy.IsSigned();
}

inline bool IsUInt2UInt(const CHIR::IntType& srcTy, const CHIR::IntType& targetTy)
{
    return !srcTy.IsSigned() && !targetTy.IsSigned();
}

llvm::Value* GenerateOverflowThrowingBasicBlock(IRBuilder2& irBuilder,
    const std::function<llvm::Value*()>& checkInfOrNaN, const std::function<llvm::Value*()>& checkLowerBound,
    const std::function<llvm::Value*()>& checkUpperBound, const std::function<llvm::Value*(IRBuilder2&)>& typecast,
    const std::string& targetTyName)
{
    auto [notInfOrNanBB, lowerBoundOKBB, upperBoundOKBB, upperBoundOverflowBB, lowerBoundOverflow, infOrNanBB, end] =
        Vec2Tuple<7>(irBuilder.CreateAndInsertBasicBlocks({GenNameForBB("not.inf.nan"), GenNameForBB("lower.bound.ok"),
            GenNameForBB("upper.bound.ok"), GenNameForBB("upper.bound.overflow"), GenNameForBB("lower.bound.overflow"),
            GenNameForBB("inf.or.nan"), GenNameForBB("overflow.throwing.end")}));

    if (checkInfOrNaN) {
        irBuilder.CreateCondBr(checkInfOrNaN(), notInfOrNanBB, infOrNanBB);
        irBuilder.SetInsertPoint(infOrNanBB);
        irBuilder.CreateOverflowOrArithmeticException("Casting Infinite or NaN value to integer.");
        irBuilder.CreateUnreachable();
        irBuilder.SetInsertPoint(notInfOrNanBB);
    } else {
        (void)notInfOrNanBB->eraseFromParent();
        (void)infOrNanBB->eraseFromParent();
    }
    irBuilder.CreateCondBr(checkLowerBound(), lowerBoundOKBB, lowerBoundOverflow);
    // lower.bound.overflow.
    irBuilder.SetInsertPoint(lowerBoundOverflow);
    irBuilder.CreateOverflowOrArithmeticException("The result would be less than " + targetTyName + ".Min.");
    irBuilder.CreateUnreachable();
    // Emit lower.bound.ok body.
    irBuilder.SetInsertPoint(lowerBoundOKBB);
    irBuilder.CreateCondBr(checkUpperBound(), upperBoundOKBB, upperBoundOverflowBB);
    // Emit upper.bound.overflow body.
    irBuilder.SetInsertPoint(upperBoundOverflowBB);
    irBuilder.CreateOverflowOrArithmeticException("The result would be greater than " + targetTyName + ".Max.");
    irBuilder.CreateUnreachable();
    // Emit upper.bound.ok body.
    irBuilder.SetInsertPoint(upperBoundOKBB);
    auto typecastVal = typecast(irBuilder);
    irBuilder.CreateBr(end);
    // Emit end body.
    irBuilder.SetInsertPoint(end);
    return typecastVal;
}

llvm::Value* GenerateCondCheckInfiniteOrNaN(
    IRBuilder2& irBuilder, llvm::Value& srcValue, const CHIR::NumericType& srcTy)
{
    auto& llvmCtx = irBuilder.GetLLVMContext();
    using TypeKind = CHIR::Type::TypeKind;
    // Key: CHIR::TypeKind
    // Value:[bitWidth of srcTy, the vale if float is inf or nan in IEEE754, llvm::Type]
    const std::unordered_map<TypeKind, std::tuple<unsigned, uint64_t, llvm::IntegerType*>> floatInIEEE754{
        {TypeKind::TYPE_FLOAT16, {16, 0x7C00, llvm::Type::getInt16Ty(llvmCtx)}},
        {TypeKind::TYPE_FLOAT32, {32, 0x7F800000, llvm::Type::getInt32Ty(llvmCtx)}},
        {TypeKind::TYPE_FLOAT64, {64, 0x7FF0000000000000, llvm::Type::getInt64Ty(llvmCtx)}},
    };
    auto srcTyKind = irBuilder.GetTypeKindFromType(srcTy);
    unsigned bitwith = std::get<0>(floatInIEEE754.at(srcTyKind));
    uint64_t magicVal = std::get<1>(floatInIEEE754.at(srcTyKind));
    llvm::IntegerType* integerType = std::get<2>(floatInIEEE754.at(srcTyKind));
    auto integerVal = irBuilder.CreateBitCast(&srcValue, integerType);
    auto exponentVal = llvm::ConstantInt::get(llvmCtx, llvm::APInt(bitwith, magicVal));
    auto andVal = irBuilder.CreateAnd(integerVal, exponentVal);
    auto notInfOrNan = irBuilder.CreateICmpNE(andVal, exponentVal, "notInfOrNan");
    return notInfOrNan;
}

template <typename SrcT>
llvm::Value* GenerateCondCheckLowerBound(
    IRBuilder2& irBuilder, const CGType& targetType, llvm::Value& srcValue, const SrcT& srcTy)
{
    auto& targetTy = StaticCast<const CHIR::IntType&>(targetType.GetOriginal());
    if constexpr (std::is_same_v<SrcT, CHIR::IntType>) {
        if (IsUInt2Int(srcTy, targetTy)) {
            return irBuilder.getTrue();
        } else if (IsInt2UInt(srcTy, targetTy)) {
            auto minVal = llvm::ConstantInt::get(srcValue.getType(), 0);
            return irBuilder.CreateICmpSLE(minVal, &srcValue, "i2ui.lower");
        } else if (IsInt2Int(srcTy, targetTy)) {
            if (irBuilder.GetTypeKindFromType(targetTy) > irBuilder.GetTypeKindFromType(srcTy)) {
                return irBuilder.getTrue();
            }
            auto minVal = llvm::ConstantInt::getSigned(
                srcValue.getType(), GetIntMaxOrMin(irBuilder, targetTy, false));
            return irBuilder.CreateICmpSLE(minVal, &srcValue, "i2i.lower");
        } else {
            CJC_ASSERT(IsUInt2UInt(srcTy, targetTy));
            return irBuilder.getTrue();
        }
    } else {
        static_assert(std::is_same_v<SrcT, CHIR::FloatType>);
        /**
         * Conversion from floating-point to integer follows the round-toward-zero mode.
         * The round-toward-zero works by truncate the fractional part and round to the integer closest to zero.
         */
        auto archType = irBuilder.GetCGContext().GetCompileOptions().target.GetArchType();
        auto lowerBound = G_FLOAT2INT_BOUND_MAP.at(srcTy.ToString() + StringIntX(archType, targetTy)).first;
        if (lowerBound == 0) {
            return irBuilder.getTrue();
        }
        auto lowerBoundValue = llvm::ConstantFP::get(srcValue.getType(), *reinterpret_cast<double*>(&lowerBound));
        return irBuilder.CreateFCmpOLT(lowerBoundValue, &srcValue, "f2i.lt.min");
    }
}

template <typename SrcT>
llvm::Value* GenerateCondCheckUpperBound(
    IRBuilder2& irBuilder, const CGType& targetType, llvm::Value& srcValue, const SrcT& srcTy)
{
    auto& targetTy = StaticCast<const CHIR::IntType&>(targetType.GetOriginal());
    if constexpr (std::is_same_v<SrcT, CHIR::IntType>) {
        auto targetTyKind = irBuilder.GetTypeKindFromType(targetTy);
        auto srcTyKind = irBuilder.GetTypeKindFromType(srcTy);
        if (IsUInt2Int(srcTy, targetTy)) {
            auto maxType = NeedsUIntToIntConversion(irBuilder, srcTy, targetTyKind) ? targetType.GetLLVMType() : srcValue.getType();
            auto maxVal = llvm::ConstantInt::getSigned(maxType, GetIntMaxOrMin(irBuilder, targetTy, true));
            auto tempSrcValue = NeedsUIntToIntConversion(irBuilder, srcTy, targetTyKind)
                ? irBuilder.CreateZExtOrTrunc(&srcValue, targetType.GetLLVMType())
                : &srcValue;
            return irBuilder.CreateICmpULE(tempSrcValue, maxVal, "ui2i.upper");
        } else if (IsInt2UInt(srcTy, targetTy)) {
            auto maxType = NeedsIntToUIntConversion(irBuilder, srcTy, targetTyKind) ? targetType.GetLLVMType() : srcValue.getType();
            auto maxVal = llvm::ConstantInt::get(maxType, GetUIntMax(irBuilder, targetTy));
            auto tempSrcValue = NeedsIntToUIntConversion(irBuilder, srcTy, targetTyKind)
                ? irBuilder.CreateZExtOrTrunc(&srcValue, targetType.GetLLVMType())
                : &srcValue;
            return irBuilder.CreateICmpULE(tempSrcValue, maxVal, "i2ui.upper");
        } else if (IsInt2Int(srcTy, targetTy)) {
            if (targetTyKind > srcTyKind) {
                return irBuilder.getTrue();
            }
            auto maxVal = llvm::ConstantInt::getSigned(srcValue.getType(), GetIntMaxOrMin(irBuilder, targetTy, true));
            return irBuilder.CreateICmpSLE(&srcValue, maxVal, "i2i.upper");
        } else {
            CJC_ASSERT(IsUInt2UInt(srcTy, targetTy));
            if (targetTyKind > srcTyKind) {
                return irBuilder.getTrue();
            }
            auto maxVal =
                llvm::ConstantInt::getSigned(srcValue.getType(), static_cast<int64_t>(GetUIntMax(irBuilder, targetTy)));
            return irBuilder.CreateICmpULE(&srcValue, maxVal, "ui2ui.upper");
        }
    } else {
        static_assert(std::is_same_v<SrcT, CHIR::FloatType>);
        /**
         * Conversion from floating-point to integer follows the round-toward-zero mode.
         * The round-toward-zero works by truncate the fractional part and round to the integer closest to zero.
         */
        auto archType = irBuilder.GetCGContext().GetCompileOptions().target.GetArchType();
        auto upperBound = G_FLOAT2INT_BOUND_MAP.at(srcTy.ToString() + StringIntX(archType, targetTy)).second;
        if (upperBound == 0) {
            return irBuilder.getTrue();
        }
        auto upperBoundValue = llvm::ConstantFP::get(srcValue.getType(), *reinterpret_cast<double*>(&upperBound));
        return irBuilder.CreateFCmpOLT(&srcValue, upperBoundValue, "f2i.gt.max");
    }
}

llvm::Value* GenerateFloatToIntegerConvExpr(IRBuilder2& irBuilder, const OverflowStrategy& strategy,
    llvm::Value& origSrcValue, const CGType& targetType, const CHIR::FloatType& srcTy)
{
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    if (strategy == OverflowStrategy::THROWING) {
#endif
        auto checkInfOrNan = [&origSrcValue, &srcTy, &irBuilder] {
            return GenerateCondCheckInfiniteOrNaN(irBuilder, origSrcValue, srcTy);
        };
        auto checkLowerBound = [&origSrcValue, &srcTy, &targetType, &irBuilder] {
            return GenerateCondCheckLowerBound(irBuilder, targetType, origSrcValue, srcTy);
        };
        auto checkUpperBound = [&origSrcValue, &srcTy, &targetType, &irBuilder] {
            return GenerateCondCheckUpperBound(irBuilder, targetType, origSrcValue, srcTy);
        };
        auto handleTypecast = [&origSrcValue, &targetType](IRBuilder2& irBuilder) {
            auto& targetTy = StaticCast<const CHIR::IntType&>(targetType.GetOriginal());
            return targetTy.IsSigned() ? irBuilder.CreateFPToSI(&origSrcValue, targetType.GetLLVMType())
                                       : irBuilder.CreateFPToUI(&origSrcValue, targetType.GetLLVMType());
        };
        return GenerateOverflowThrowingBasicBlock(irBuilder, checkInfOrNan, checkLowerBound, checkUpperBound,
            handleTypecast, targetType.GetOriginal().ToString());
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    } else {
        auto& targetTy = StaticCast<const CHIR::IntType&>(targetType.GetOriginal());
        return irBuilder.CallIntrinsicFloatToIntegerSat(*targetType.GetLLVMType(), origSrcValue, targetTy.IsSigned());
    }
#endif
}

llvm::Value* GenerateOverflowSaturatingBasicBlock(IRBuilder2& irBuilder, const CGType& targetType,
    const std::function<llvm::Value*()>& checkLowerBound, const std::function<llvm::Value*()>& checkUpperBound,
    const std::function<llvm::Value*(IRBuilder2&)>& typecast)
{
    auto& targetTy = StaticCast<const CHIR::IntType&>(targetType.GetOriginal());
    auto [lowerBoundOKBB, upperBoundOKBB, upperBoundOverflowBB, lowerBoundOverflow, satEndBB] = Vec2Tuple<5>(
        irBuilder.CreateAndInsertBasicBlocks({GenNameForBB("lower.bound.ok"), GenNameForBB("upper.bound.ok"),
            GenNameForBB("upper.bound.overflow"), GenNameForBB("lower.bound.overflow"), GenNameForBB("sat.end")}));
    (void)irBuilder.CreateCondBr(checkLowerBound(), lowerBoundOKBB, lowerBoundOverflow);
    // Emit lower.bound.overflow body.
    irBuilder.SetInsertPoint(lowerBoundOverflow);
    llvm::Value* retMin = targetTy.IsSigned()
        ? llvm::ConstantInt::getSigned(targetType.GetLLVMType(), GetIntMaxOrMin(irBuilder, targetTy, false))
        : llvm::ConstantInt::get(targetType.GetLLVMType(), 0);
    irBuilder.CreateBr(satEndBB);
    // Emit lower.bound.ok body.
    irBuilder.SetInsertPoint(lowerBoundOKBB);
    irBuilder.CreateCondBr(checkUpperBound(), upperBoundOKBB, upperBoundOverflowBB);
    /// Emit upper.bound.overflow body.
    irBuilder.SetInsertPoint(upperBoundOverflowBB);
    llvm::Value* retMax = targetTy.IsSigned()
        ? llvm::ConstantInt::getSigned(targetType.GetLLVMType(),GetIntMaxOrMin(irBuilder, targetTy, true))
        : llvm::ConstantInt::get(targetType.GetLLVMType(), GetUIntMax(irBuilder, targetTy));
    irBuilder.CreateBr(satEndBB);
    /// Emit upper.bound.ok body.
    irBuilder.SetInsertPoint(upperBoundOKBB);
    auto typecastVal = typecast(irBuilder);
    irBuilder.CreateBr(satEndBB);
    // Emit sat.end body.
    irBuilder.SetInsertPoint(satEndBB);
    auto result = irBuilder.CreatePHI(targetType.GetLLVMType(), 3); // 3 branches for phi.
    result->addIncoming(typecastVal, upperBoundOKBB);
    result->addIncoming(retMin, lowerBoundOverflow);
    result->addIncoming(retMax, upperBoundOverflowBB);
    return result;
}

llvm::Value* GenerateIntegerConversionExpr(IRBuilder2& irBuilder, const OverflowStrategy& strategy,
    llvm::Value& srcValue, const CGType& targetType, const CHIR::IntType& srcTy)
{
    // Get typecasted value.
    auto handleTypeCast = [&srcTy, &srcValue, &targetType](IRBuilder2& nativeBuilder) {
        auto isSrcSigned = srcTy.IsSigned();
        return isSrcSigned ? nativeBuilder.CreateSExtOrTrunc(&srcValue, targetType.GetLLVMType())
                           : nativeBuilder.CreateZExtOrTrunc(&srcValue, targetType.GetLLVMType());
    };
    auto checkLowerBound = [&srcValue, &srcTy, &targetType, &irBuilder] {
        return GenerateCondCheckLowerBound(irBuilder, targetType, srcValue, srcTy);
    };
    auto checkUpperBound = [&srcValue, &srcTy, &targetType, &irBuilder] {
        return GenerateCondCheckUpperBound(irBuilder, targetType, srcValue, srcTy);
    };
    if (strategy == OverflowStrategy::SATURATING) {
        return GenerateOverflowSaturatingBasicBlock(
            irBuilder, targetType, checkLowerBound, checkUpperBound, handleTypeCast);
    } else if (strategy == OverflowStrategy::THROWING) {
        return GenerateOverflowThrowingBasicBlock(irBuilder, std::function<llvm::Value*()>(), checkLowerBound,
            checkUpperBound, handleTypeCast, targetType.GetOriginal().ToString());
    } else {
        return handleTypeCast(irBuilder);
    }
}

void GenerateUnicodeScalarValueCheck(IRBuilder2& irBuilder, llvm::Value* srcValue, const CHIR::Type& srcTy)
{
    auto srcTyKind = irBuilder.GetTypeKindFromType(srcTy);
    if (srcTyKind == CHIR::Type::TypeKind::TYPE_UINT8) {
        return;
    }
    auto type = CGType::GetOrCreate(irBuilder.GetCGModule(), &srcTy)->GetLLVMType();
    auto bound1 = llvm::ConstantInt::get(type, 0);
    auto bound2 = llvm::ConstantInt::get(type, 0xD7FF);
    auto bound3 = llvm::ConstantInt::get(type, 0xE000);
    auto bound4 = llvm::ConstantInt::get(type, 0x10FFFF);

    llvm::Value* compareResult;
    if (srcTy.IsInteger() && StaticCast<const CHIR::IntType&>(srcTy).IsSigned()) {
        auto cond1 = irBuilder.CreateICmpSLT(srcValue, bound1); // i < 0
        if (srcTyKind == CHIR::Type::TypeKind::TYPE_INT8 ||
            srcTyKind == CHIR::Type::TypeKind::TYPE_INT16) {
            compareResult = cond1; // i < 0
        } else {
            auto cond2 = irBuilder.CreateICmpSGT(srcValue, bound2); // i > 0xD7FF
            auto cond3 = irBuilder.CreateICmpSLT(srcValue, bound3); // i < 0XE000
            auto cond4 = irBuilder.CreateICmpSGT(srcValue, bound4); // i > 0x10FFFF
            auto combo1 = irBuilder.CreateAnd(cond2, cond3);        // i > 0xD7FF && i < 0XE000
            auto combo2 = irBuilder.CreateOr(cond1, combo1);        // i < 0 || (i > 0xD7FF && i < 0XE000)
            compareResult = irBuilder.CreateOr(combo2, cond4); // (i < 0 || (i > 0xD7FF && i < 0XE000)) || i > 0x10FFFF
        }
    } else {
        auto cond1 = irBuilder.CreateICmpUGT(srcValue, bound2); // i > 0xD7FF
        auto cond2 = irBuilder.CreateICmpULT(srcValue, bound3); // i < 0XE000
        auto combo1 = irBuilder.CreateAnd(cond1, cond2);        // i > 0xD7FF && i < 0XE000
        if (srcTyKind == CHIR::Type::TypeKind::TYPE_UINT16) {
            compareResult = combo1;                                 // i > 0xD7FF && i < 0XE000
        } else {                                                    // UInt32 & UInt64
            auto cond3 = irBuilder.CreateICmpUGT(srcValue, bound4); // i > 0x10FFFF
            compareResult = irBuilder.CreateOr(combo1, cond3);      // (i > 0xD7FF && i < 0XE000) || i > 0x10FFFF
        }
    }

    auto [throwBB, endBB] =
        Vec2Tuple<2>(irBuilder.CreateAndInsertBasicBlocks({GenNameForBB("if.else"), GenNameForBB("if.end")}));
    (void)irBuilder.CreateCondBr(compareResult, throwBB, endBB);

    irBuilder.SetInsertPoint(throwBB);
    irBuilder.CreateOverflowOrArithmeticException("in Rune(num), num is not a valid Unicode scalar value!", true);
    irBuilder.CreateUnreachable();

    // endBB
    irBuilder.SetInsertPoint(endBB);
}

llvm::Value* GenerateIntegerToRuneTypeCast(IRBuilder2& irBuilder, llvm::Value* srcValue, const CHIR::Type& srcTy)
{
    GenerateUnicodeScalarValueCheck(irBuilder, srcValue, srcTy);
    auto type = CGType::GetUInt32CGType(irBuilder.GetCGModule());
    if (irBuilder.GetTypeKindFromType(srcTy) == CHIR::Type::TypeKind::TYPE_UINT32) {
        return srcValue;
    } else {
        // Since we've checked the range of unicode scalar value, it's safe to use zext here.
        return irBuilder.CreateZExtOrTrunc(srcValue, type->GetLLVMType());
    }
}
} // namespace

namespace Cangjie {
namespace CodeGen {
llvm::Value* GenerateGenericTypeCast(IRBuilder2& irBuilder, const CGValue& cgSrcValue,
    const CHIR::Type& srcTy, const CHIR::Type& targetTy)
{
    auto& cgMod = irBuilder.GetCGModule();
    auto srcValue = *cgSrcValue;
    auto targetCGType = CGType::GetOrCreate(cgMod, &targetTy);
    auto srcCGType = CGType::GetOrCreate(cgMod, &srcTy);
    bool targetIsOptionLike = targetTy.IsEnum() ? StaticCast<CGEnumType*>(targetCGType)->IsOptionLike() : false;
    bool srcIsOptionLike = srcTy.IsEnum() ? StaticCast<CGEnumType*>(srcCGType)->IsOptionLike() : false;
    // auto cgSrcValue = (cgMod | typeCast.GetSourceValue());
    if (srcTy.IsGeneric() && targetTy.IsGeneric()) { // T1 -> T2: do nothing
        return srcValue;
    } else if (srcTy.IsGeneric() && IsGenericRef(targetTy)) { // T1 -> T1&: Allocate and Store
        auto result = irBuilder.CreateEntryAlloca(targetCGType->GetLLVMType());
        irBuilder.CreateStore(srcValue, result);
        return result;
    } else if (IsGenericRef(srcTy) && targetTy.IsGeneric()) { // T1& -> T1: Load
        auto targetType = CGType::GetOrCreate(cgMod, &targetTy);
        return irBuilder.CreateLoad(targetType->GetLLVMType(), srcValue);
    } else if (srcTy.IsGeneric() &&
        (targetTy.IsStruct() || targetTy.IsRawArray() || targetTy.IsTuple() || IsTrivialEnum(targetTy) ||
            targetIsOptionLike)) { // T1 -> struct/rawArray/Tuple/Enum
        if (targetCGType->GetSize()) {
            auto elementType = targetCGType->GetLLVMType();
            auto srcPayload =
                irBuilder.CreateBitCast(irBuilder.GetPayloadFromObject(srcValue), elementType->getPointerTo(1U));
            return irBuilder.CreateLoad(elementType, srcPayload);
        }
        return srcValue;
    } else if ((srcTy.IsStruct() || srcTy.IsRawArray() || srcTy.IsTuple() || IsTrivialEnum(srcTy) || srcIsOptionLike) &&
        targetTy.IsGeneric()) { // struct/rawArray/Tuple/Enum -> T1
        if (srcCGType->GetSize()) {
            // 1. Allocate memory for boxing srcValue.
            auto typeInfoOfSrc = irBuilder.CreateTypeInfo(srcTy);
            llvm::Value* temp =
                irBuilder.CallIntrinsicAllocaGeneric({typeInfoOfSrc, irBuilder.GetLayoutSize_32(srcTy)});
            // 2. store srcValue to temp
            auto payloadPtr = irBuilder.GetPayloadFromObject(temp);
            auto addr = irBuilder.CreateBitCast(payloadPtr, srcCGType->GetLLVMType()->getPointerTo(1));
            auto addrType =
                CGType::GetOrCreate(cgMod, CGType::GetRefTypeOf(cgMod.GetCGContext().GetCHIRBuilder(), srcTy), 1U);
            (void)irBuilder.CreateStore(cgSrcValue, CGValue(addr, addrType));
            return temp;
        }
        return srcValue;
    } else if (srcTy.IsGeneric() && IsStructRef(targetTy)) { // T1 -> struct&: Incorrect CHIR node
        CJC_ASSERT(false && "Illegal GenericTypeCast.");
        return nullptr;
    } else if (IsStructRef(srcTy) && targetTy.IsGeneric()) { // struct& -> T1: Incorrect CHIR node
        CJC_ASSERT(false && "Illegal GenericTypeCast");
        return nullptr;
    } else if (srcTy.IsGeneric() && (IsClassRef(targetTy) || targetTy.IsEnum())) { // T1 -> class&: do nothing
        return srcValue;
    } else if ((IsClassRef(srcTy) || srcTy.IsEnum()) && targetTy.IsGeneric()) { // class& -> T1: do nothing
        return srcValue;
    } else if (srcTy.IsGeneric() &&
        IsDynamicStruct(targetTy)) {
        return nullptr;
    } else if (IsDynamicStruct(srcTy) &&
        targetTy.IsGeneric()) {
        return nullptr;
    } else if (srcTy.IsGeneric() &&
        IsDynamicStructRef(targetTy)) {
        return nullptr;
    } else if (IsDynamicStructRef(srcTy) &&
        targetTy.IsGeneric()) {
        return nullptr;
    } else if (srcTy.IsGeneric() && IsDynamicClassRef(targetTy)) { // T1 -- class<T1>&: do nothing
        return srcValue;
    } else if (IsDynamicClassRef(srcTy) && targetTy.IsGeneric()) { // class<T1>& -- T1: do nothing
        return srcValue;
    } else if (IsStructRef(srcTy) && IsDynamicClassRef(targetTy)) { // struct& -> class<T1>&
        auto ti = irBuilder.CreateTypeInfo(srcTy);
        auto size = irBuilder.GetLayoutSize_32(srcTy);
        llvm::Value* temp = irBuilder.CallIntrinsicAllocaGeneric({ti, size});
        irBuilder.CallGCWriteGenericPayload({temp, srcValue, size});
        return temp;
    } else if (IsDynamicClassRef(srcTy) && IsStructRef(targetTy)) { // class<T1>& -> struct&
        return nullptr;
    } else if (srcTy.IsStructArray() && targetTy.IsStructArray()) {
        auto targetCGType = CGType::GetOrCreate(cgMod, &targetTy);
        auto tmp = irBuilder.CreateEntryAlloca(*targetCGType);
        llvm::MaybeAlign align{};
        irBuilder.CreateMemCpy(tmp, align, srcValue, align, irBuilder.GetLayoutSize_32(targetTy));
        return tmp;
    } else if (IsStaticStruct(srcTy) &&
        IsDynamicStruct(targetTy)) { // struct<Int64> -> struct<T1>
        auto ti = irBuilder.CreateTypeInfo(srcTy);
        auto size = irBuilder.GetLayoutSize_32(srcTy);
        llvm::Value* temp = irBuilder.CallIntrinsicAllocaGeneric({ti, size});
        irBuilder.CallGCWriteGenericPayload({temp, srcValue, size});
        return temp;
    } else if (IsDynamicStruct(srcTy) &&
        IsStaticStruct(targetTy)) { // struct<T1> -> struct<Int64>
        auto targetCGType = CGType::GetOrCreate(cgMod, &targetTy);
        auto tmp = irBuilder.CreateEntryAlloca(*targetCGType);
        auto dataPtr = irBuilder.GetPayloadFromObject(srcValue);
        if (IsTypeContainsRef(targetCGType->GetLLVMType())) {
            irBuilder.CallGCReadAgg({tmp, srcValue, dataPtr, irBuilder.GetLayoutSize_64(targetTy)});
        } else {
            llvm::MaybeAlign align{};
            irBuilder.CreateMemCpy(tmp, align, dataPtr, align, irBuilder.GetLayoutSize_32(targetTy));
        }
        return tmp;
    } else if (IsStaticStruct(srcTy) &&
        IsDynamicStruct(targetTy)) { // struct<Int64> -> struct<T1>
        auto ti = irBuilder.CreateTypeInfo(srcTy);
        auto size = irBuilder.GetLayoutSize_32(srcTy);
        llvm::Value* temp = irBuilder.CallIntrinsicAllocaGeneric({ti, size});
        irBuilder.CallGCWriteGenericPayload({temp, srcValue, size});
        return temp;
    } else if (IsDynamicClass(srcTy) && IsStaticClass(targetTy)) { // class<T1> -> class<Int64>: do nothing
        return srcValue;
    } else if (IsStaticClass(srcTy) && IsDynamicClass(targetTy)) { // class<Int64> -> class<T1>: do nothing
        return srcValue;
    } else if ((srcTy.IsTuple() && targetTy.IsTuple()) &&
        (!srcCGType->GetSize() && targetCGType->GetSize())) {
        auto elementType = targetCGType->GetLLVMType();
        auto srcPayload =
            irBuilder.CreateBitCast(irBuilder.GetPayloadFromObject(srcValue), elementType->getPointerTo(1U));
        return irBuilder.CreateLoad(elementType, srcPayload);
    } else {
        return nullptr;
    }
}

llvm::Value* GenerateTypeCast(IRBuilder2& irBuilder, const CHIRTypeCastWrapper& typeCast)
{
    auto targetTy = typeCast.GetTargetTy();
    auto srcTy = typeCast.GetSourceTy();
    CJC_ASSERT(srcTy && targetTy);

    auto& cgMod = irBuilder.GetCGModule();
    auto cgSrcValue = cgMod | typeCast.GetSourceValue();
    auto srcValue = **cgSrcValue;
    if (srcTy == targetTy) {
        return srcValue;
    }

    auto result = GenerateGenericTypeCast(irBuilder, *cgSrcValue, *srcTy, *targetTy);
    if (result) {
        return result;
    }

    // No instructions are needed while typecasting an enum type to a tuple type.
    if (srcTy->IsEnum() && targetTy->IsTuple()) {
        return srcValue;
    }

    auto srcType = CGType::GetOrCreate(cgMod, srcTy);
    auto targetType = CGType::GetOrCreate(cgMod, targetTy);
    // No instructions are needed while typecasting enum and integer types.
    if ((targetTy->IsEnum() && srcTy->IsInteger()) || (targetTy->IsInteger() && srcTy->IsEnum())) {
        if (StaticCast<CGEnumType*>(CGType::GetOrCreate(cgMod, targetTy->IsEnum() ? targetTy : srcTy))->IsTrivial()) {
            return srcValue;
        } else {
            return irBuilder.CreateNullValue(*targetTy);
        }
    }

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    if (srcType->IsPointerType(1) && targetType->IsPointerType(1)) {
        return srcValue;
    }
#endif

    if (targetTy->IsFloat() && srcTy->IsFloat()) {
        return GenerateFloatConvExpr(irBuilder, srcValue, targetType->GetLLVMType(),
            *StaticCast<CHIR::NumericType*>(srcTy), *StaticCast<CHIR::NumericType*>(targetTy));
    } else if (targetTy->IsFloat() && srcTy->IsInteger()) {
        return GenerateIntegerToFloatConvExpr(
            irBuilder, srcValue, targetType->GetLLVMType(), *StaticCast<CHIR::IntType*>(srcTy));
    } else if (targetTy->IsInteger() && srcTy->IsFloat()) {
        return GenerateFloatToIntegerConvExpr(
            irBuilder, typeCast.GetOverflowStrategy(), *srcValue, *targetType, *StaticCast<CHIR::FloatType*>(srcTy));
    } else if (irBuilder.GetTypeKindFromType(*targetTy) == CHIR::Type::TypeKind::TYPE_UINT32 && srcTy->IsRune()) {
        return srcValue;
    } else if (targetTy->IsRune() && srcTy->IsInteger()) {
        return GenerateIntegerToRuneTypeCast(irBuilder, srcValue, *srcTy);
    } else if (targetTy->IsInteger() && srcTy->IsRune()) {
        return irBuilder.CreateZExtOrTrunc(srcValue, targetType->GetLLVMType());
    } else if (targetTy->IsTuple() && srcTy->IsTuple()) {
        return GenerateTupleTypeCast(irBuilder, *cgSrcValue, *targetType);
    } else if (targetTy->IsFunc() && srcTy->IsFunc()) {
        CJC_ASSERT(targetTy->GetTypeArgs().size() == srcTy->GetTypeArgs().size());
        auto castedTo = static_cast<CGFunctionType*>(targetType)
                            ->GetLLVMFunctionType()
                            ->getPointerTo(srcValue->getType()->getPointerAddressSpace());
        return irBuilder.CreateBitOrPointerCast(srcValue, castedTo);
    } else if (targetTy->IsInteger() && srcTy->IsInteger()) {
        return GenerateIntegerConversionExpr(
            irBuilder, typeCast.GetOverflowStrategy(), *srcValue, *targetType, *StaticCast<CHIR::IntType*>(srcTy));
    } else if (targetType->GetLLVMType() == srcType->GetLLVMType()) {
        return srcValue;
    } else if (IsCFunc(*targetTy) && srcTy->IsCPointer()) {
        return irBuilder.CreateBitOrPointerCast(srcValue, targetType->GetLLVMType());
    } else if (targetTy->IsInteger() && srcTy->IsBoolean()) { // Handle TypeCast(Bool, Int) in CHIR
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
        return irBuilder.CreateZExtOrTrunc(srcValue, targetType->GetLLVMType());
#endif
    }

    // In CHIR, if VArray as CFunc parameter type, it will translate to a reference.
    // `@C func c_f(a: VArray<...>&)`
    // c_f(inout varr) should typecast from CPointer<VArray<...>> to VArray<...>&.
    if (IsVArrayRef(*targetTy) && srcTy->IsCPointer()) {
        return irBuilder.CreateBitOrPointerCast(srcValue, targetType->GetLLVMType());
    }

    return irBuilder.CreateNullValue(*targetTy);
}
} // namespace CodeGen
} // namespace Cangjie
