// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements codegen for CHIR Varray creation.
 */

#include "Base/VArrayExprImpl.h"

#include <optional>

#include "CGModule.h"
#include "IRBuilder.h"
#include "Utils/CGUtils.h"
#include "cangjie/CHIR/Expression/Terminator.h"
#include "cangjie/CHIR/Type/Type.h"
#include "cangjie/CHIR/Value.h"

using namespace Cangjie;
using namespace CodeGen;

namespace {
llvm::Value* GenerateConstantVArray(
    const IRBuilder2& irBuilder, const CHIR::VArray& varray, const std::string& serialized)
{
    auto chirType = varray.GetResult()->GetType();
    CJC_ASSERT(chirType->IsVArray() && "Should not reach here.");
    auto varrayChirType = StaticCast<const CHIR::VArrayType*>(chirType);

    auto varrayCGType = CGType::GetOrCreate(irBuilder.GetCGModule(), varrayChirType);
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    std::vector<llvm::Constant*> params;
    for (size_t i = 0; i < varray.GetOperands().size(); ++i) {
        auto value = (irBuilder.GetCGModule() | varray.GetOperand(i))->GetRawValue();
        auto tmp = llvm::dyn_cast<llvm::GlobalVariable>(value);
        params.emplace_back(tmp ? tmp->getInitializer() : llvm::cast<llvm::Constant>(value));
    }
    auto arrayType = llvm::cast<llvm::ArrayType>(varrayCGType->GetLLVMType());
    auto constVal = llvm::ConstantArray::get(arrayType, params);
    return irBuilder.GetCGModule().GetOrCreateGlobalVariable(constVal, serialized, false);
#endif
}
} // namespace

llvm::Value* CodeGen::GenerateVArray(IRBuilder2& irBuilder, const CHIR::VArray& varray)
{
    // let arr1: VArray<Int64, $5> = [1,2,3,4,5]
    auto [isConstantVArray, serialized] = IsConstantVArray(varray);
    if (isConstantVArray) {
        return GenerateConstantVArray(irBuilder, varray, serialized);
    }
    auto chirType = varray.GetResult()->GetType();
    CJC_ASSERT(chirType->IsVArray() && "Should not reach here.");
    auto varrayChirType = StaticCast<const CHIR::VArrayType*>(chirType);

    auto varrayCGType = CGType::GetOrCreate(irBuilder.GetCGModule(), varrayChirType);
    auto varrayType = varrayCGType->GetLLVMType();
    auto varrayPtr = irBuilder.CreateEntryAlloca(varrayType, nullptr, "varray");

    for (size_t i = 0; i < varrayChirType->GetSize(); ++i) {
        auto indexName = "varray.idx" + std::to_string(i) + "E";
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
        auto elementPtr =
            irBuilder.CreateGEP(varrayType, varrayPtr, {irBuilder.getInt64(0), irBuilder.getInt64(i)}, indexName);
#endif
        auto cGValue = (irBuilder.GetCGModule() | varray.GetOperand(i));
        auto& cgCtx = irBuilder.GetCGContext();
        auto& cgMod = irBuilder.GetCGModule();
        auto elementPtrCGType =
            CGType::GetOrCreate(cgMod, CGType::GetRefTypeOf(cgCtx.GetCHIRBuilder(), *varrayChirType->GetElementType()));
        (void)irBuilder.CreateStore(*cGValue, CGValue(elementPtr, elementPtrCGType));
    }
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    return varrayPtr;
#endif
}

llvm::Value* CodeGen::GenerateVArrayBuilder(IRBuilder2& irBuilder, const CHIR::VArrayBuilder& varrayBuilder)
{
    auto& cgMod = irBuilder.GetCGModule();
    auto varrayType = StaticCast<CHIR::VArrayType*>(varrayBuilder.GetResult()->GetType());
    auto varrayLen = (cgMod | varrayBuilder.GetSize())->GetRawValue();

    auto item = DynamicCast<CHIR::LocalVar*>(varrayBuilder.GetItem());
    CJC_NULLPTR_CHECK(item);
    bool isInitedByItem = item && !item->GetExpr()->IsConstantNull();
    if (!isInitedByItem) {
        // VArrayBuilder(size, nullptr, initLambda: Class-$Auto_Env_Base_XXXX)
        auto autoEnvOfInitFunc = varrayBuilder.GetInitFunc();
        auto autoEnvType = DeRef(*autoEnvOfInitFunc->GetType());
        CJC_ASSERT(autoEnvType->IsAutoEnvBase());
        auto cgValue = (cgMod | autoEnvOfInitFunc);
        return irBuilder.VArrayInitedByLambda(varrayLen, *cgValue, *varrayType);
    } else {
        // VArrayBuilder(size, value, initLambda: nullptr)
        auto cgValue = (cgMod | item);
        return irBuilder.VArrayInitedByItem(varrayLen, *cgValue, *varrayType);
    }
}
