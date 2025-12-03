// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CGARRAYTYPE_H
#define CANGJIE_CGARRAYTYPE_H

#include "Base/CGTypes/CGType.h"
#include "cangjie/CHIR/IR/Type/Type.h"

namespace Cangjie {
namespace CodeGen {

class CGArrayType : public CGType {
    friend class CGTypeMgr;

public:
    CGType* GetElementCGType() const
    {
        auto tmp = GetContainedTypes();
        CJC_ASSERT(tmp.size() == 1);
        return tmp[0];
    }
    llvm::Type* GetLayoutType() const
    {
        return layoutType;
    }

    static llvm::StructType* GenerateArrayLayoutTypeInfo(
        CGContext& cgCtx, const std::string& layoutName, llvm::Type* elemType);
    static llvm::Type* GenerateArrayLayoutType(CGModule& cgMod, const CHIR::RawArrayType& arrTy);
    static bool IsRefArray(const CGType& elemType);
    static llvm::StructType* GenerateRefArrayLayoutType(CGContext& cgCtx);
    static std::string GetTypeNameByArrayType(CGModule& cgMod, const CHIR::RawArrayType& arrTy);
    static std::string GetTypeNameByArrayElementType(CGModule& cgMod, CHIR::Type& elemType);

protected:
    llvm::Type* GenLLVMType() override;
    void GenContainedCGTypes() override;

private:
    CGArrayType() = delete;

    explicit CGArrayType(CGModule& cgMod, CGContext& cgCtx, const CHIR::RawArrayType& chirType)
        : CGType(cgMod, cgCtx, chirType)
    {
    }

    llvm::Constant* GenSourceGenericOfTypeInfo() override;
    llvm::Constant* GenTypeArgsNumOfTypeInfo() override;
    llvm::Constant* GenTypeArgsOfTypeInfo() override;
    llvm::Constant* GenSuperOfTypeInfo() override;

    void CalculateSizeAndAlign() override;
};
} // namespace CodeGen
} // namespace Cangjie

#endif // CANGJIE_CGARRAYTYPE_H