// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "CGContextImpl.h"

#include "Base/CGTypes/CGFunctionType.h"
#include "Base/CGTypes/CGRefType.h"
#include "Base/CGTypes/CGTupleType.h"
#include "cangjie/CHIR/IR/Type/StructDef.h"

namespace Cangjie::CodeGen {
void CGContextImpl::Clear()
{
    for (auto cgType : cgTypePool) {
        CJC_ASSERT(cgType != nullptr);
        delete cgType;
        cgType = nullptr;
    }
    cgTypePool.clear();
    chirType2CGTypeMap.clear();
    chirTypeName2CGTypeMap.clear();
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    valueAndBasePtrMap.clear();
    nullableReference.clear();
#endif
}
} // namespace Cangjie::CodeGen
