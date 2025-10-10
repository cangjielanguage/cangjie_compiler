// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CGCSTRINGTYPE_H
#define CANGJIE_CGCSTRINGTYPE_H

#include "Base/CGTypes/CGType.h"
#include "CGContext.h"
#include "cangjie/CHIR/Type/Type.h"

namespace Cangjie {
namespace CodeGen {
class CGCStringType : public CGType {
    friend class CGTypeMgr;

protected:
    llvm::Type* GenLLVMType() override;
    void GenContainedCGTypes() override
    {
        containedCGTypes.emplace_back(CGType::GetInt8CGType(cgMod));
    }

private:
    CGCStringType() = delete;
    explicit CGCStringType(CGModule& cgMod, CGContext& cgCtx, const CHIR::Type& chirType)
        : CGType(cgMod, cgCtx, chirType)
    {
        CJC_ASSERT(chirType.GetTypeKind() == CHIR::Type::TYPE_CSTRING);
    }

    void CalculateSizeAndAlign() override;
};
} // namespace CodeGen
} // namespace Cangjie

#endif // CANGJIE_CGCSTRINGTYPE_H