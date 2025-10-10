// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "Base/CGTypes/CGCStringType.h"

#include "CGModule.h"
#include "CGContext.h"

namespace Cangjie::CodeGen {
llvm::Type* CGCStringType::GenLLVMType()
{
    if (llvmType) {
        return llvmType;
    }
    auto& llvmCtx = cgCtx.GetLLVMContext();
    llvmType = llvm::Type::getInt8PtrTy(llvmCtx);

    layoutType = llvm::StructType::getTypeByName(llvmCtx, "CString.Type");
    if (!layoutType) {
        layoutType = llvm::StructType::create(llvmCtx, {llvmType}, "CString.Type");
    }
    return llvmType;
}

void CGCStringType::CalculateSizeAndAlign()
{
    llvm::DataLayout layOut = cgMod.GetLLVMModule()->getDataLayout();
    size = layOut.getTypeAllocSize(llvmType);
    align = layOut.getABITypeAlignment(llvmType);
}
} // namespace Cangjie::CodeGen
