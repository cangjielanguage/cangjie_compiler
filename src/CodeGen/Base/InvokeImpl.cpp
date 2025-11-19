// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements codegen for CHIR Invoke.
 */

#include "Base/InvokeImpl.h"

#include "Base/CHIRExprWrapper.h"
#include "CGModule.h"
#include "IRBuilder.h"
#include "cangjie/CHIR/Expression/Terminator.h"
#include "cangjie/CHIR/Type/ClassDef.h"
#include "cangjie/CHIR/Type/Type.h"

using namespace Cangjie;
using namespace CodeGen;
using namespace CHIR;

namespace {
llvm::MDTuple* GenObjTyMetaForVirtualCall(IRBuilder2& irBuilder, const CHIR::Type& ty)
{
    CJC_ASSERT(ty.GetTypeArgs().size() <= 1);
    llvm::LLVMContext& llvmCtx = irBuilder.GetCGContext().GetLLVMContext();
    std::vector<llvm::Metadata*> objTyMeta{};
    if (ty.GetTypeArgs().size() == 1) {
        objTyMeta = UnwindGenericRelateType(llvmCtx, ty);
    } else {
        std::string tiName = CGType::GetNameOfTypeInfoGV(ty);
        objTyMeta.emplace_back(llvm::MDString::get(llvmCtx, tiName));
    }
    return llvm::MDTuple::get(llvmCtx, objTyMeta);
}
} // namespace
llvm::Value* CodeGen::GenerateInvoke(IRBuilder2& irBuilder, const CHIRInvokeWrapper& invoke)
{
    irBuilder.SetCHIRExpr(&invoke);
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    auto& cgMod = irBuilder.GetCGModule();
    auto objVal = cgMod | invoke.GetObject();
    std::vector<CGValue*> argsVal;
    for (auto arg : invoke.GetArgs()) {
        (void)argsVal.emplace_back(cgMod | arg);
    }

    auto funcType = invoke.GetMethodType();
    auto& cgCtx = cgMod.GetCGContext();

    auto objType = DeRef(*invoke.GetObject()->GetType());
    llvm::Value* funcPtr = nullptr;
    if (!objType->IsAutoEnv()) {
        auto ti = irBuilder.GetTypeInfoFromObject(objVal->GetRawValue());
        auto vtableOffset = invoke.GetVirtualMethodOffset();
        auto idxOfVFunc = irBuilder.getInt64(vtableOffset);
        auto introType = StaticCast<const CHIR::ClassType*>(invoke.GetOuterType(cgCtx.GetCHIRBuilder()));
        if (introType->GetClassDef()->IsInterface()) {
            auto introTi = irBuilder.CreateTypeInfo(introType);
            funcPtr = irBuilder.CallIntrinsicMTable({ti, introTi, idxOfVFunc});
        } else {
            auto vtableSizeOfIntroType = cgCtx.GetVTableSizeOf(introType);
            CJC_ASSERT(vtableSizeOfIntroType > 0);
            auto idxOfIntroType = irBuilder.getInt64(vtableSizeOfIntroType - 1U);
            auto introTi = irBuilder.CreateTypeInfo(introType);
            funcPtr = irBuilder.CallIntrinsicGetVTableFunc(ti, idxOfIntroType, idxOfVFunc, introTi);
            auto& llvmCtx = cgCtx.GetLLVMContext();
            auto meta = llvm::MDTuple::get(llvmCtx, llvm::MDString::get(llvmCtx, GetTypeQualifiedName(*introType)));
            llvm::cast<llvm::Instruction>(funcPtr)->setMetadata("IntroType", meta);
        }
        if (introType->GetTypeArgs().size() <= 1) {
            auto objTyMeta = GenObjTyMetaForVirtualCall(irBuilder, *introType);
            llvm::cast<llvm::Instruction>(funcPtr)->setMetadata("objType", objTyMeta);
        }
    } else {
        auto i8PtrTy = irBuilder.getInt8PtrTy();
        auto funcName = invoke.GetMethodName();
        auto methodIdx = CHIR::GetMethodIdxInAutoEnvObject(funcName);
        auto payload = irBuilder.GetPayloadFromObject(**objVal);
        auto vritualPtr = irBuilder.CreateConstGEP1_32(i8PtrTy,
            irBuilder.LLVMIRBuilder2::CreateBitCast(payload, i8PtrTy->getPointerTo(1U)),
            static_cast<unsigned>(methodIdx), "virtualFPtr");
        auto loadInst = irBuilder.LLVMIRBuilder2::CreateLoad(i8PtrTy, vritualPtr);
        loadInst->setMetadata(llvm::LLVMContext::MD_invariant_load, llvm::MDNode::get(cgMod.GetLLVMContext(), {}));
        funcPtr = llvm::cast<llvm::Value>(loadInst);
    }
    auto concreteFuncType = static_cast<CGFunctionType*>(CGType::GetOrCreate(
        cgMod, funcType, CGType::TypeExtraInfo{0, true, false, true, invoke.GetInstantiatedTypeArgs()}));
    funcPtr = irBuilder.CreateBitCast(funcPtr, concreteFuncType->GetLLVMFunctionType()->getPointerTo());
    auto result = irBuilder.CreateCallOrInvoke(*concreteFuncType, funcPtr, argsVal);

    auto thisValue = **(cgMod | invoke.GetThisParam());
    auto originalNonRefVal = cgCtx.GetOriginalNonRefValOfBoxedValue(thisValue);
    if (originalNonRefVal && invoke.TestVritualMethodAttr(cgCtx.GetCHIRBuilder(), CHIR::Attribute::MUT)) {
        auto [handleBoxedBB, handleEndBB] =
            Vec2Tuple<2>(irBuilder.CreateAndInsertBasicBlocks({"handle_boxed", "handle_end"}));
        auto thisValueTI = irBuilder.GetTypeInfoFromObject(thisValue);
        irBuilder.CreateCondBr(irBuilder.CreateTypeInfoIsReferenceCall(thisValueTI), handleEndBB, handleBoxedBB);

        irBuilder.SetInsertPoint(handleBoxedBB);
        auto basePtr = cgCtx.GetBasePtrOf(originalNonRefVal);
        auto sizeOfTI = irBuilder.GetSizeFromTypeInfo(thisValueTI);
        std::vector<llvm::Value*> copyGenericParams{basePtr, originalNonRefVal, thisValue, sizeOfTI};
        irBuilder.CallIntrinsicGCWriteGeneric(copyGenericParams);
        irBuilder.CreateBr(handleEndBB);

        irBuilder.SetInsertPoint(handleEndBB);
    }
    return result;
#endif
}

llvm::Value* CodeGen::GenerateInvokeStatic(IRBuilder2& irBuilder, const CHIRInvokeStaticWrapper& invokeStatic)
{
    irBuilder.SetCHIRExpr(&invokeStatic);
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    auto& cgMod = irBuilder.GetCGModule();
    std::vector<CGValue*> argsVal{};
    for (auto arg : invokeStatic.GetArgs()) {
        (void)argsVal.emplace_back(cgMod | arg);
    }

    llvm::Value* ti = **(cgMod | invokeStatic.GetRTTIValue());
    llvm::Value* funcPtr = nullptr;
    auto vtableOffset = invokeStatic.GetVirtualMethodOffset();
    auto idxOfVFunc = irBuilder.getInt64(vtableOffset);
    auto& cgCtx = cgMod.GetCGContext();
    auto introType = StaticCast<const CHIR::ClassType*>(invokeStatic.GetOuterType(cgCtx.GetCHIRBuilder()));
    if (introType->GetClassDef()->IsInterface()) {
        auto introTi = irBuilder.CreateTypeInfo(introType);
        funcPtr = irBuilder.CallIntrinsicMTable({ti, introTi, idxOfVFunc});
    } else {
        auto vtableSizeOfIntroType = cgCtx.GetVTableSizeOf(introType);
        CJC_ASSERT(vtableSizeOfIntroType > 0);
        auto idxOfIntroType = irBuilder.getInt64(vtableSizeOfIntroType - 1U);
        auto introTi = irBuilder.CreateTypeInfo(introType);
        funcPtr = irBuilder.CallIntrinsicGetVTableFunc(ti, idxOfIntroType, idxOfVFunc, introTi);
        auto& llvmCtx = cgCtx.GetLLVMContext();
        auto meta = llvm::MDTuple::get(llvmCtx, llvm::MDString::get(llvmCtx, GetTypeQualifiedName(*introType)));
        llvm::cast<llvm::Instruction>(funcPtr)->setMetadata("IntroType", meta);
    }
    if (introType->GetTypeArgs().size() <= 1) {
        auto objTyMeta = GenObjTyMetaForVirtualCall(irBuilder, *introType);
        llvm::cast<llvm::Instruction>(funcPtr)->setMetadata("objType", objTyMeta);
    }

    auto funcType = invokeStatic.GetMethodType();
    auto concreteFuncType = static_cast<CGFunctionType*>(CGType::GetOrCreate(
        cgMod, funcType, CGType::TypeExtraInfo{0, true, true, false, invokeStatic.GetInstantiatedTypeArgs()}));
    funcPtr = irBuilder.CreateBitCast(funcPtr, concreteFuncType->GetLLVMFunctionType()->getPointerTo());
    return irBuilder.CreateCallOrInvoke(*concreteFuncType, funcPtr, argsVal, false, ti);
#endif
}
