// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "Base/CGTypes/CGBoxType.h"

#include "CGModule.h"

namespace Cangjie::CodeGen {
CGBoxType::CGBoxType(CGModule& cgMod, CGContext& cgCtx, const CHIR::BoxType& chirType)
    : CGType(cgMod, cgCtx, chirType, CGTypeKind::CG_REF)
{
}

llvm::Type* CGBoxType::GenLLVMType()
{
    if (llvmType) {
        return llvmType;
    }
    return llvm::Type::getInt8Ty(cgCtx.GetLLVMContext());
}

void CGBoxType::GenContainedCGTypes()
{
    auto& boxType = StaticCast<const CHIR::BoxType&>(chirType);
    (void)containedCGTypes.emplace_back(CGType::GetOrCreate(cgMod, boxType.GetBaseType()));
}

void CGBoxType::CalculateSizeAndAlign()
{
    llvm::DataLayout layOut = cgMod.GetLLVMModule()->getDataLayout();
    size = layOut.getTypeAllocSize(llvmType);
    align = layOut.getABITypeAlignment(llvmType);
}
} // namespace Cangjie::CodeGen
