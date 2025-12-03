// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_THISTYPE_H
#define CANGJIE_THISTYPE_H

#include "Base/CGTypes/CGType.h"
#include "CGContext.h"
#include "cangjie/CHIR/IR/Type/Type.h"

namespace Cangjie {
namespace CodeGen {
class CGThisType : public CGType {
    friend class CGTypeMgr;

private:
    CGThisType() = delete;
    explicit CGThisType(CGModule& cgMod, CGContext& cgCtx, const CHIR::Type& chirType)
        : CGType(cgMod, cgCtx, chirType)
    {
        CJC_ASSERT(chirType.GetTypeKind() == CHIR::Type::TYPE_THIS);
    }

    llvm::Type* GenLLVMType() override
    {
        // `ThisType` is not allowed to be used for memory allocation.
        return nullptr;
    }
    void GenContainedCGTypes() override
    {
        // No contained types
    }
    void CalculateSizeAndAlign() override
    {
        size = std::nullopt;
        align = std::nullopt;
    }
};
} // namespace CodeGen
} // namespace Cangjie

#endif // CANGJIE_THISTYPE_H