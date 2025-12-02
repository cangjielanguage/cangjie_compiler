// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CGREFTYPE_H
#define CANGJIE_CGREFTYPE_H

#include "Base/CGTypes/CGType.h"
#include "CGContext.h"
#include "cangjie/CHIR/IR/Type/Type.h"

namespace Cangjie {
namespace CodeGen {
class CGRefType : public CGType {
    friend class CGTypeMgr;

protected:
    llvm::Type* GenLLVMType() override;
    void GenContainedCGTypes() override;

private:
    CGRefType() = delete;

    explicit CGRefType(CGModule& cgMod, CGContext& cgCtx, const CHIR::Type& chirType, unsigned addrspace)
        : CGType(cgMod, cgCtx, chirType, CGTypeKind::CG_REF)
    {
        this->addrspace = addrspace;
    }

    void CalculateSizeAndAlign() override;
};
} // namespace CodeGen
} // namespace Cangjie

#endif // CANGJIE_CGREFTYPE_H
