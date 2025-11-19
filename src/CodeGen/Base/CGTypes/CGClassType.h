// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CGCLASSTYPE_H
#define CANGJIE_CGCLASSTYPE_H

#include "Base/CGTypes/CGCustomType.h"
#include "cangjie/CHIR/Type/Type.h"

namespace Cangjie {
namespace CodeGen {
class CGClassType : public CGCustomType {
    friend class CGTypeMgr;

public:
    llvm::StructType* GetLayoutType() const
    {
        return layoutType;
    }

    size_t GetNumOfAllFields() const
    {
        return numOfAllFields;
    }

protected:
    llvm::Type* GenLLVMType() override;
    void GenContainedCGTypes() override;

    llvm::Constant* GenSuperFnOfTypeTemplate() override;
    llvm::Constant* GenFinalizerOfTypeTemplate() override;
    void PreActionOfGenTypeInfo() override;
    void PreActionOfGenTypeTemplate() override;
    void PostActionOfGenTypeInfo() override;

private:
    CGClassType() = delete;

    explicit CGClassType(CGModule& cgMod, CGContext& cgCtx, const CHIR::ClassType& chirType);

    llvm::Constant* GenSuperOfTypeInfo() override;
    llvm::Constant* GenSourceGenericOfTypeInfo() override;
    void CalculateSizeAndAlign() override;

private:
    size_t numOfAllFields{0}; // including the fields inherited
};
} // namespace CodeGen
} // namespace Cangjie
#endif // CANGJIE_CGCLASSTYPE_H
