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
#include "cangjie/CHIR/IR/Expression/Terminator.h"
#include "cangjie/CHIR/IR/Type/ClassDef.h"
#include "cangjie/CHIR/IR/Type/Type.h"

using namespace Cangjie;
using namespace CodeGen;
using namespace CHIR;

namespace {
inline bool VirtualCallCanBeHoisted(IRBuilder2& irBuilder, const CHIRCallExpr& callExprWrapper)
{
    if (irBuilder.GetCGContext().GetCompileOptions().optimizationLevel < GlobalOptions::OptimizationLevel::O2) {
        return false;
    }
    return callExprWrapper.GetThisType(false)->IsGeneric();
}

llvm::Value* GenerateFuncPtrForNonAutoEnv(IRBuilder2& irBuilder, llvm::Value* thisTI, CHIRInvokeExpr& invokeExpr)
{
    auto& cgCtx = irBuilder.GetCGContext();
    auto& llvmCtx = cgCtx.GetLLVMContext();
    if (VirtualCallCanBeHoisted(irBuilder, invokeExpr)) {
        auto [prepForVirtualCallBB] =
            Vec2Tuple<1>(irBuilder.CreateAndInsertBasicBlocks({GenNameForBB("prepForVirtualCall.bb")}));
        invokeExpr.SetPrepForVirtualCallBB(prepForVirtualCallBB);
        irBuilder.CreateBr(prepForVirtualCallBB);
        irBuilder.SetInsertPoint(prepForVirtualCallBB);
    }

    llvm::Value* funcPtr = nullptr;
    auto introType = StaticCast<const CHIR::ClassType*>(invokeExpr.GetOuterType(cgCtx.GetCHIRBuilder()));
    auto introTi = irBuilder.CreateTypeInfo(introType);
    auto vtableOffset = invokeExpr.GetVirtualMethodOffset();
    auto idxOfVFunc = irBuilder.getInt64(vtableOffset);
    if (introType->GetClassDef()->IsInterface()) {
        funcPtr = irBuilder.CallIntrinsicMTable({thisTI, introTi, idxOfVFunc});
    } else {
        auto vtableSizeOfIntroType = cgCtx.GetVTableSizeOf(introType);
        CJC_ASSERT(vtableSizeOfIntroType > 0);
        auto idxOfIntroType = irBuilder.getInt64(vtableSizeOfIntroType - 1U);
        funcPtr = irBuilder.CallIntrinsicGetVTableFunc(thisTI, idxOfIntroType, idxOfVFunc, introTi);
        auto meta = llvm::MDTuple::get(llvmCtx, llvm::MDString::get(llvmCtx, GetTypeQualifiedName(*introType)));
        llvm::cast<llvm::Instruction>(funcPtr)->setMetadata("IntroType", meta);
    }
    auto objTyMeta = llvm::MDTuple::get(llvmCtx, UnwindGenericRelateType(llvmCtx, *introType));
    llvm::cast<llvm::Instruction>(funcPtr)->setMetadata("objType", objTyMeta);

    auto prepForVirtualCallBB = invokeExpr.GetPrepForVirtualCallBB();
    if (prepForVirtualCallBB) {
        cgCtx.AddVirtualCallInfo4LICM(irBuilder.GetInsertCGFunction(),
            {prepForVirtualCallBB, funcPtr, invokeExpr.GetThisType(false), introType, vtableOffset});
        auto [prepForVirtualCallEndBB] =
            Vec2Tuple<1>(irBuilder.CreateAndInsertBasicBlocks({GenNameForBB("prepForVirtualCall.end")}));
        irBuilder.CreateBr(prepForVirtualCallEndBB);
        irBuilder.SetInsertPoint(prepForVirtualCallEndBB);
    }
    return funcPtr;
}

llvm::Value* GenerateFuncPtrForAutoEnv(IRBuilder2& irBuilder, CHIRInvokeWrapper& invoke)
{
    auto i8PtrTy = irBuilder.getInt8PtrTy();
    auto funcName = invoke.GetMethodName();
    auto methodIdx = CHIR::GetMethodIdxInAutoEnvObject(funcName);
    auto payload = irBuilder.GetPayloadFromObject(**(irBuilder.GetCGModule() | invoke.GetObject()));
    auto vritualPtr = irBuilder.CreateConstGEP1_32(i8PtrTy,
        irBuilder.LLVMIRBuilder2::CreateBitCast(payload, i8PtrTy->getPointerTo(1U)), static_cast<unsigned>(methodIdx),
        "virtualFPtr");
    auto loadInst = irBuilder.LLVMIRBuilder2::CreateLoad(i8PtrTy, vritualPtr);
    loadInst->setMetadata(llvm::LLVMContext::MD_invariant_load, llvm::MDNode::get(irBuilder.GetLLVMContext(), {}));
    return llvm::cast<llvm::Value>(loadInst);
}

void GenIRToUnboxResult(IRBuilder2& irBuilder, const CHIRInvokeWrapper& invoke)
{
    auto& cgCtx = irBuilder.GetCGModule().GetCGContext();
    auto thisValue = **(irBuilder.GetCGModule() | invoke.GetThisParam());
    auto originalNonRefVal = cgCtx.GetOriginalNonRefValOfBoxedValue(thisValue);
    if (!originalNonRefVal || !invoke.TestVritualMethodAttr(CHIR::Attribute::MUT)) {
        return;
    }

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
} // namespace

llvm::Value* CodeGen::GenerateInvoke(IRBuilder2& irBuilder, CHIRInvokeWrapper invoke)
{
    irBuilder.SetCHIRExpr(&invoke);
    auto& cgMod = irBuilder.GetCGModule();
    std::vector<CGValue*> argsVal;
    for (auto arg : invoke.GetArgs()) {
        (void)argsVal.emplace_back(cgMod | arg);
    }

    llvm::Value* funcPtr = nullptr;
    auto objType = DeRef(*invoke.GetObject()->GetType());
    if (objType->IsAutoEnv()) {
        funcPtr = GenerateFuncPtrForAutoEnv(irBuilder, invoke);
    } else {
        auto thisTI = irBuilder.GetTypeInfoFromObject(**(irBuilder.GetCGModule() | invoke.GetObject()));
        funcPtr = GenerateFuncPtrForNonAutoEnv(irBuilder, thisTI, invoke);
    }
    auto funcType = invoke.GetMethodType();
    auto concreteFuncType = static_cast<CGFunctionType*>(CGType::GetOrCreate(
        cgMod, funcType, CGType::TypeExtraInfo{0, true, false, true, invoke.GetInstantiatedTypeArgs()}));
    funcPtr = irBuilder.CreateBitCast(funcPtr, concreteFuncType->GetLLVMFunctionType()->getPointerTo());

    auto result = irBuilder.CreateCallOrInvoke(*concreteFuncType, funcPtr, argsVal);
    GenIRToUnboxResult(irBuilder, invoke);
    return result;
}

llvm::Value* CodeGen::GenerateInvokeStatic(IRBuilder2& irBuilder, CHIRInvokeStaticWrapper invokeStatic)
{
    irBuilder.SetCHIRExpr(&invokeStatic);
    auto& cgMod = irBuilder.GetCGModule();
    std::vector<CGValue*> argsVal{};
    for (auto arg : invokeStatic.GetArgs()) {
        (void)argsVal.emplace_back(cgMod | arg);
    }

    llvm::Value* thisTI = **(cgMod | invokeStatic.GetRTTIValue());
    llvm::Value* funcPtr = GenerateFuncPtrForNonAutoEnv(irBuilder, thisTI, invokeStatic);
    auto funcType = invokeStatic.GetMethodType();
    auto concreteFuncType = static_cast<CGFunctionType*>(CGType::GetOrCreate(
        cgMod, funcType, CGType::TypeExtraInfo{0, true, true, false, invokeStatic.GetInstantiatedTypeArgs()}));
    funcPtr = irBuilder.CreateBitCast(funcPtr, concreteFuncType->GetLLVMFunctionType()->getPointerTo());

    return irBuilder.CreateCallOrInvoke(*concreteFuncType, funcPtr, argsVal, false, thisTI);
}
