// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "Base/CGTypes/CGRefType.h"

#include "CGModule.h"

namespace Cangjie::CodeGen {
llvm::Type* CGRefType::GenLLVMType()
{
    if (llvmType) {
        return llvmType;
    }
    auto baseType = StaticCast<const CHIR::RefType&>(chirType).GetBaseType();
    if (baseType->IsClass() || baseType->IsRawArray() || baseType->IsBox()) {
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
        return GetRefType(cgCtx.GetLLVMContext());
#endif
    }
    auto cgType = CGType::GetOrCreate(cgMod, baseType);
    if (!cgType->GetSize() && !baseType->IsGeneric() && addrspace == 1U) {
        return llvm::Type::getInt8PtrTy(cgCtx.GetLLVMContext(), 1U);
    }
    return CGType::GetOrCreate(cgMod, baseType)->GetLLVMType()->getPointerTo(addrspace);
}

void CGRefType::GenContainedCGTypes()
{
    auto& refType = StaticCast<const CHIR::RefType&>(chirType);
    (void)containedCGTypes.emplace_back(CGType::GetOrCreate(cgMod, refType.GetBaseType()));
}

void CGRefType::CalculateSizeAndAlign()
{
    llvm::DataLayout layOut = cgMod.GetLLVMModule()->getDataLayout();
    size = layOut.getTypeAllocSize(llvmType);
    align = layOut.getABITypeAlignment(llvmType);
}
} // namespace Cangjie::CodeGen
