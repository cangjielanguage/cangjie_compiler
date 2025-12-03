// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements codegen for CHIR Allocate.
 */

#include "Base/AllocateImpl.h"

#include "Base/CGTypes/CGClassType.h"
#include "Base/CHIRExprWrapper.h"
#include "IRBuilder.h"
#include "cangjie/CHIR/IR/Type/ClassDef.h"

using namespace Cangjie;
using namespace CodeGen;

llvm::Value* CodeGen::GenerateAllocate(IRBuilder2& irBuilder, const CHIRAllocateWrapper& alloca)
{
    auto allocaType = alloca.GetType();
    if (allocaType->IsClass()) {
        auto classType = StaticCast<CHIR::ClassType*>(allocaType);
        auto ret = irBuilder.CallClassIntrinsicAlloc(*classType);
        if (classType->IsAutoEnv()) {
            CJC_ASSERT(!classType->IsAutoEnvBase());
            auto payload = irBuilder.GetPayloadFromObject(ret);
            auto methods = classType->GetClassDef()->GetMethods();
            for (size_t idx = 0; idx < methods.size(); ++idx) {
                auto virtualFunc = methods[idx];
                auto function = irBuilder.GetCGModule().GetOrInsertCGFunction(virtualFunc)->GetRawValue();
                auto addr = irBuilder.CreateConstInBoundsGEP1_32(
                    irBuilder.getInt8PtrTy(), payload, static_cast<unsigned>(idx));
                irBuilder.CreateStore(irBuilder.CreateBitCast(function, irBuilder.getInt8PtrTy()), addr);
            }
        }
        return ret;
    } else {
        CJC_ASSERT(!allocaType->IsThis() && "CHIR should not try to allocate memory for `ThisType`.");
        auto cgType = CGType::GetOrCreate(irBuilder.GetCGModule(), allocaType);
        return irBuilder.CreateEntryAlloca(*cgType);
    }
}
