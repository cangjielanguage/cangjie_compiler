// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "Base/CGTypes/CGPrimitiveType.h"

#include "CGModule.h"
#include "cangjie/CHIR/IR/CHIRContext.h"

namespace Cangjie::CodeGen {
llvm::Type* CGPrimitiveType::GenLLVMType()
{
    if (llvmType) {
        return llvmType;
    }

    auto& llvmCtx = cgCtx.GetLLVMContext();
    switch (chirType.GetTypeKind()) {
        case CHIR::Type::TypeKind::TYPE_INT8:
        case CHIR::Type::TypeKind::TYPE_UINT8:
            llvmType = llvm::Type::getInt8Ty(llvmCtx);
            break;
        case CHIR::Type::TypeKind::TYPE_INT16:
        case CHIR::Type::TypeKind::TYPE_UINT16:
            llvmType = llvm::Type::getInt16Ty(llvmCtx);
            break;
        case CHIR::Type::TypeKind::TYPE_INT32:
        case CHIR::Type::TypeKind::TYPE_UINT32:
        case CHIR::Type::TypeKind::TYPE_RUNE:
            llvmType = llvm::Type::getInt32Ty(llvmCtx);
            break;
        case CHIR::Type::TypeKind::TYPE_INT64:
        case CHIR::Type::TypeKind::TYPE_UINT64:
            llvmType = llvm::Type::getInt64Ty(llvmCtx);
            break;
        case CHIR::Type::TypeKind::TYPE_INT_NATIVE:
        case CHIR::Type::TypeKind::TYPE_UINT_NATIVE: {
            auto archType = cgCtx.GetCompileOptions().target.GetArchType();
            if (archType == Triple::ArchType::X86_64 || archType == Triple::ArchType::AARCH64) {
                llvmType = llvm::Type::getInt64Ty(llvmCtx);
                break;
            }
            if (archType == Triple::ArchType::ARM32) {
                llvmType = llvm::Type::getInt32Ty(llvmCtx);
                break;
            }
            CJC_ASSERT(false && "Unsupported ArchType.");
            return nullptr;
        }
        case CHIR::Type::TypeKind::TYPE_FLOAT16:
            llvmType = llvm::Type::getHalfTy(llvmCtx);
            break;
        case CHIR::Type::TypeKind::TYPE_FLOAT32:
            llvmType = llvm::Type::getFloatTy(llvmCtx);
            break;
        case CHIR::Type::TypeKind::TYPE_FLOAT64:
            llvmType = llvm::Type::getDoubleTy(llvmCtx);
            break;
        case CHIR::Type::TypeKind::TYPE_BOOLEAN:
            llvmType = llvm::Type::getInt1Ty(llvmCtx);
            break;
        case CHIR::Type::TypeKind::TYPE_UNIT:
        case CHIR::Type::TypeKind::TYPE_NOTHING:
            llvmType = CGType::GetUnitType(llvmCtx);
            break;
        case CHIR::Type::TypeKind::TYPE_VOID:
            llvmType = llvm::Type::getVoidTy(llvmCtx);
            layoutType = llvm::StructType::getTypeByName(llvmCtx, UNIT_TYPE_STR);
            if (!layoutType) {
                layoutType = llvm::StructType::create(llvmCtx, {}, UNIT_TYPE_STR);
            }
            return llvmType;
        default:
            CJC_ASSERT(false && "Should not reach here: unsupported chir type");
            return nullptr;
    }

    auto layoutName = chirType.ToString() + ".Type";
    layoutType = llvm::StructType::getTypeByName(llvmCtx, layoutName);
    if (!layoutType) {
        layoutType = llvm::StructType::create(llvmCtx, {llvmType}, layoutName);
    }
    return llvmType;
}

void CGPrimitiveType::GenContainedCGTypes()
{
    // For primitive type, no contained types.
}

void CGPrimitiveType::CalculateSizeAndAlign()
{
    if (auto t = DynamicCast<const CHIR::NumericType*>(&chirType)) {
        size= t->GetBitness() / 8U;
    } else if (chirType.IsRune()) {
        size = 4U;
    } else if (chirType.IsBoolean()) {
        size = 1U;
    } else if (chirType.IsUnit() || chirType.IsNothing() || chirType.IsVoid()) {
        size = 0U;
    } else {
        CJC_ASSERT(false && "Unsupported type");
    }
    align = size == 0 ? 1 : size;
}
} // namespace Cangjie::CodeGen
