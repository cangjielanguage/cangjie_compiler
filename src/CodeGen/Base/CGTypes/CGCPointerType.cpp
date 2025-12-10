// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "Base/CGTypes/CGCPointerType.h"

#include "CGModule.h"

namespace Cangjie::CodeGen {

llvm::Type* CGCPointerType::GenLLVMType()
{
    if (llvmType) {
        return llvmType;
    }
    auto& llvmCtx = cgCtx.GetLLVMContext();
    llvmType = llvm::Type::getInt8PtrTy(llvmCtx);

    layoutType = llvm::StructType::getTypeByName(llvmCtx, "CPointer.Type");
    if (!layoutType) {
        layoutType = llvm::StructType::create(llvmCtx, {llvmType}, "CPointer.Type");
    }
    return llvmType;
}

void CGCPointerType::GenContainedCGTypes()
{
    CJC_ASSERT(chirType.GetTypeArgs().size() == 1);
    containedCGTypes.emplace_back(CGType::GetInt8CGType(cgMod));
}

llvm::Constant* CGCPointerType::GenSuperOfTypeInfo()
{
    auto cpointerElementType = DeRef(*static_cast<const CHIR::CPointerType&>(chirType).GetElementType());
    auto ti = CGType::GetOrCreate(cgMod, cpointerElementType)->GetOrCreateTypeInfo();
    return llvm::ConstantExpr::getBitCast(ti, CGType::GetOrCreateTypeInfoPtrType(cgMod.GetLLVMContext()));
}

llvm::Constant* CGCPointerType::GenTypeArgsNumOfTypeInfo()
{
    return llvm::ConstantInt::get(llvm::Type::getInt8Ty(cgMod.GetLLVMContext()), 1U);
}

llvm::Constant* CGCPointerType::GenTypeArgsOfTypeInfo()
{
    auto genericArg = StaticCast<const CHIR::CPointerType&>(chirType).GetElementType();
    auto typeInfoPtrTy = CGType::GetOrCreateTypeInfoPtrType(cgMod.GetLLVMContext());
    auto p0i8 = llvm::Type::getInt8PtrTy(cgMod.GetLLVMContext());

    auto elemCGType = CGType::GetOrCreate(cgMod, DeRef(*genericArg));
    std::vector<llvm::Constant*> constants{elemCGType->GetOrCreateTypeInfo()};
    if (elemCGType->IsStaticGI()) {
        cgCtx.AddDependentPartialOrderOfTypes(constants[0], this->typeInfo);
    }

    auto typeOfGenericArgsGV = llvm::ArrayType::get(typeInfoPtrTy, constants.size());
    auto typeInfoOfGenericArgs = llvm::cast<llvm::GlobalVariable>(cgMod.GetLLVMModule()->getOrInsertGlobal(
        CGType::GetNameOfTypeInfoGV(chirType) + ".typeArgs", typeOfGenericArgsGV));
    typeInfoOfGenericArgs->setInitializer(llvm::ConstantArray::get(typeOfGenericArgsGV, constants));
    typeInfoOfGenericArgs->setLinkage(llvm::GlobalValue::LinkageTypes::PrivateLinkage);
    typeInfoOfGenericArgs->addAttribute(CJTI_TYPE_ARGS_ATTR);
    return llvm::ConstantExpr::getBitCast(typeInfoOfGenericArgs, p0i8);
}

void CGCPointerType::CalculateSizeAndAlign()
{
    llvm::DataLayout layOut = cgMod.GetLLVMModule()->getDataLayout();
    size = layOut.getTypeAllocSize(llvmType);
    align = layOut.getABITypeAlignment(llvmType);
}
} // namespace Cangjie::CodeGen
