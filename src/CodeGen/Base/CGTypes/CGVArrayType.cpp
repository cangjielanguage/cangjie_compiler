// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "Base/CGTypes/CGVArrayType.h"

#include "CGContext.h"
#include "CGModule.h"

namespace Cangjie::CodeGen {
void CGVArrayType::CalculateSizeAndAlign()
{
    if (auto arrayType = llvm::dyn_cast<llvm::ArrayType>(llvmType)) {
        auto layOut = cgMod.GetLLVMModule()->getDataLayout();
        size = layOut.getTypeAllocSize(arrayType);
        align = layOut.getABITypeAlignment(arrayType);
    }
}

llvm::Type* CGVArrayType::GenLLVMType()
{
    if (llvmType) {
        return llvmType;
    }

    auto& varrType = StaticCast<const CHIR::VArrayType&>(chirType);
    auto varrElementType = varrType.GetElementType();
    llvmType = llvm::ArrayType::get(CGType::GetOrCreate(cgMod, varrElementType)->GetLLVMType(), varrType.GetSize());

    auto layoutName = "VArray." + std::to_string(varrType.GetSize()) + MangleType(*varrElementType);
    layoutType = llvm::StructType::getTypeByName(cgCtx.GetLLVMContext(), layoutName);
    if (layoutType && cgCtx.IsGeneratedStructType(layoutName)) {
        return llvmType;
    } else if (!layoutType) {
        layoutType = llvm::StructType::create(cgCtx.GetLLVMContext(), layoutName);
    }
    cgCtx.AddGeneratedStructType(layoutName);

    // VArrayLayout: [sizeOfVArray x T], T is element type. if Array is ref Array, T is i8 addrspece(1) *.
    SetStructTypeBody(layoutType, std::vector<llvm::Type*>{llvmType});

    return llvmType;
}

void CGVArrayType::GenContainedCGTypes()
{
    auto& varrayType = StaticCast<const CHIR::VArrayType&>(chirType);
    (void)containedCGTypes.emplace_back(CGType::GetOrCreate(cgMod, varrayType.GetElementType()));
}

llvm::Constant* CGVArrayType::GenFieldsNumOfTypeInfo()
{
    auto fieldsNum = StaticCast<const CHIR::VArrayType&>(chirType).GetSize();
    return llvm::ConstantInt::get(llvm::Type::getInt16Ty(cgMod.GetLLVMContext()), fieldsNum);
}

llvm::Constant* CGVArrayType::GenFieldsOfTypeInfo()
{
    return CGType::GenFieldsOfTypeInfo();
}

llvm::Constant* CGVArrayType::GenSourceGenericOfTypeInfo()
{
    return CGType::GenSourceGenericOfTypeInfo();
}

llvm::Constant* CGVArrayType::GenTypeArgsNumOfTypeInfo()
{
    return CGType::GenTypeArgsNumOfTypeInfo();
}

llvm::Constant* CGVArrayType::GenOffsetsOfTypeInfo()
{
    return CGType::GenOffsetsOfTypeInfo();
}

llvm::Constant* CGVArrayType::GenTypeArgsOfTypeInfo()
{
   return CGType::GenTypeArgsOfTypeInfo();
}

llvm::Constant* CGVArrayType::GenSuperOfTypeInfo()
{
    auto varrayElementType = DeRef(*StaticCast<const CHIR::VArrayType&>(chirType).GetElementType());
    auto elemCGType = CGType::GetOrCreate(cgMod, varrayElementType);
    auto ti = CGType::GetOrCreate(cgMod, varrayElementType)->GetOrCreateTypeInfo();
    if (elemCGType->IsStaticGI()) {
        cgCtx.AddDependentPartialOrderOfTypes(ti, this->typeInfo);
    }
    return llvm::ConstantExpr::getBitCast(ti, CGType::GetOrCreateTypeInfoPtrType(cgMod.GetLLVMContext()));
}
} // namespace Cangjie::CodeGen
