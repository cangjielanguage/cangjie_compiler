// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "Base/ExprDispatcher/ExprDispatcher.h"

#include <cinttypes>

#include "Base/AllocateImpl.h"
#include "Base/ApplyImpl.h"
#include "Base/ArrayImpl.h"
#include "Base/CGTypes/CGType.h"
#include "Base/CHIRExprWrapper.h"
#include "Base/IntrinsicsDispatcher.h"
#include "Base/InvokeImpl.h"
#include "Base/SpawnExprImpl.h"
#include "Base/TypeCastImpl.h"
#include "CGModule.h"
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
#include "CJNative/CGCFFI.h"
#endif
#include "Utils/BlockScopeImpl.h"
#include "Utils/CGCommonDef.h"
#include "cangjie/CHIR/IR/Value/Value.h"

namespace Cangjie::CodeGen {
llvm::Value* HandleExitExpression(IRBuilder2& irBuilder, const CHIR::Exit& exitExpr)
{
    auto& cgMod = irBuilder.GetCGModule();
    auto& cgCtx = irBuilder.GetCGContext();
    if (cgCtx.debugValue) {
        CJC_ASSERT(cgCtx.GetCompileOptions().enableCompileDebug);
        auto thisValue = irBuilder.GetInsertCGFunction()->GetArgByIndexFromCHIR(0);
        auto curCHIRFunc = DynamicCast<const CHIR::Func*>(&irBuilder.GetInsertCGFunction()->GetOriginal());
        CJC_NULLPTR_CHECK(curCHIRFunc);
        auto thisTy = curCHIRFunc->GetParam(0)->GetType();
        auto cgType = CGType::GetOrCreate(cgMod, thisTy);
        auto thisCGValue = CGValue(thisValue, cgType);
        auto payload = irBuilder.GetPayloadFromObject(cgCtx.debugValue);
        irBuilder.CreateStore(CGValue(payload, cgType), thisCGValue);
    }
    if (auto func = irBuilder.GetInsertFunction(); func && func->hasFnAttribute(HAS_WITH_TI_WRAPPER_ATTR)) {
        auto debugLocOfRetExpr = exitExpr.GetDebugLocation();
        cgCtx.AddDebugLocOfRetExpr(func, debugLocOfRetExpr);
    }
    auto parentFunc = exitExpr.GetTopLevelFunc();
    CJC_NULLPTR_CHECK(parentFunc);
    auto retTy = parentFunc->GetReturnType();
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    if (retTy->IsUnit() || retTy->IsNothing() || retTy->IsVoid()) {
        return irBuilder.CreateRetVoid();
    }
#endif
    auto ret = parentFunc->GetReturnValue();
    if (retTy->IsRawArray()) {
        CJC_ASSERT(ret && ret->GetExpr()->GetExprKind() == CHIR::ExprKind::RAW_ARRAY_ALLOCATE);
        return irBuilder.CreateRet(**(cgMod | ret));
    }
    CJC_ASSERT(ret && "An unexpected nullptr is passed by CHIR.");
    CJC_ASSERT(ret->GetExpr()->GetExprKind() == CHIR::ExprKind::ALLOCATE);
    auto retVal = **(cgMod | ret);
    auto retType = CGType::GetOrCreate(cgMod, ret->GetType()->GetTypeArgs()[0])->GetLLVMType();
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    auto llvmRetType = irBuilder.getCurrentFunctionReturnType();
    if (llvmRetType->isVoidTy()) {
        return irBuilder.CreateRetVoid();
    }
    auto curLLVMFunc = irBuilder.GetInsertFunction();
    CJC_NULLPTR_CHECK(curLLVMFunc);
    if (curLLVMFunc->hasFnAttribute(CodeGen::CFUNC_ATTR)) {
        retVal = cgMod.GetCGCFFI().ProcessRetValue(*llvmRetType, *retVal, irBuilder);
        return irBuilder.CreateRet(retVal);
    }
#endif
    return irBuilder.CreateRet(irBuilder.CreateLoad(retType, retVal));
}

llvm::Value* HandleTerminatorExpression(IRBuilder2& irBuilder, const CHIR::Expression& chirExpr)
{
    CJC_ASSERT(chirExpr.IsTerminator());
    auto& cgMod = irBuilder.GetCGModule();
    switch (chirExpr.GetExprKind()) {
        case CHIR::ExprKind::GOTO: {
            auto& goTo = StaticCast<const CHIR::GoTo&>(chirExpr);
            auto succ = goTo.GetSuccessors()[0];
            return irBuilder.CreateBr(cgMod.GetMappedBB(succ));
        }
        case CHIR::ExprKind::EXIT: {
            auto& exitExpr = StaticCast<const CHIR::Exit&>(chirExpr);
            return HandleExitExpression(irBuilder, exitExpr);
        }
        case CHIR::ExprKind::BRANCH: {
            auto& branch = StaticCast<const CHIR::Branch&>(chirExpr);
            auto cond = **(cgMod | branch.GetCondition());
            auto trueBranch = cgMod.GetMappedBB(branch.GetTrueBlock());
            auto falseBranch = cgMod.GetMappedBB(branch.GetFalseBlock());
            return irBuilder.CreateCondBr(cond, trueBranch, falseBranch);
        }
        case CHIR::ExprKind::MULTIBRANCH: {
            auto& multiBranch = StaticCast<const CHIR::MultiBranch&>(chirExpr);
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
            auto cond = multiBranch.GetCondition();
            CJC_ASSERT(cond->GetType()->IsInteger());
            auto condVal = **(cgMod | cond);
            auto defaultBranch = cgMod.GetMappedBB(multiBranch.GetDefaultBlock());
            auto switchInst = irBuilder.CreateSwitch(condVal, defaultBranch);
            size_t bitness = llvm::cast<llvm::IntegerType>(condVal->getType())->getBitWidth();
            size_t caseIdx = 1; // default block is the first successor, others start from the second.
            for (auto caseVal : multiBranch.GetCaseVals()) {
                auto labelBranch = cgMod.GetMappedBB(multiBranch.GetSuccessor(caseIdx++));
                switchInst->addCase(irBuilder.getIntN(bitness, caseVal), labelBranch);
            }
            return switchInst;
#endif
        }
        case CHIR::ExprKind::RAISE_EXCEPTION: {
            auto& raiseException = StaticCast<const CHIR::RaiseException&>(chirExpr);
            auto exceptionValue = raiseException.GetExceptionValue();
            auto exceptionType = exceptionValue->GetType();
            CJC_ASSERT((exceptionType->IsRef() || exceptionType->IsGeneric()) &&
                "The target of the throw must be a generic or a reference to an object.");
            auto exceptionBlock = raiseException.GetExceptionBlock();
            if (exceptionBlock) {
                CodeGenUnwindBlockScope unwindBlockScope(cgMod, cgMod.GetMappedBB(exceptionBlock));
                irBuilder.CallExceptionIntrinsicThrow(cgMod | exceptionValue);
            } else {
                irBuilder.CallExceptionIntrinsicThrow(cgMod | exceptionValue);
            }
            (void)irBuilder.CreateUnreachable();
            return nullptr;
        }
        case CHIR::ExprKind::APPLY_WITH_EXCEPTION: {
            auto& applyWithException = StaticCast<const CHIR::ApplyWithException&>(chirExpr);
            auto normalDest = applyWithException.GetSuccessor(0);
            auto unwindDest = applyWithException.GetSuccessor(1);
            CodeGenUnwindBlockScope unwindBlockScope(cgMod, cgMod.GetMappedBB(unwindDest));
            auto resultVal = GenerateApply(irBuilder, CHIRApplyWrapper(applyWithException));
            irBuilder.CreateBr(cgMod.GetMappedBB(normalDest));
            return resultVal;
        }
        case CHIR::ExprKind::INVOKE_WITH_EXCEPTION: {
            auto& invokeWithException = StaticCast<const CHIR::InvokeWithException&>(chirExpr);
            auto normalDest = invokeWithException.GetSuccessor(0);
            auto unwindDest = invokeWithException.GetSuccessor(1);
            CodeGenUnwindBlockScope unwindBlockScope(cgMod, cgMod.GetMappedBB(unwindDest));
            auto resultVal = GenerateInvoke(irBuilder, CHIRInvokeWrapper(invokeWithException));
            irBuilder.CreateBr(cgMod.GetMappedBB(normalDest));
            return resultVal;
        }
        case CHIR::ExprKind::INVOKESTATIC_WITH_EXCEPTION: {
            auto& invokeStaticWithException = StaticCast<const CHIR::InvokeStaticWithException&>(chirExpr);
            auto normalDest = invokeStaticWithException.GetSuccessor(0);
            auto unwindDest = invokeStaticWithException.GetSuccessor(1);
            CodeGenUnwindBlockScope unwindBlockScope(cgMod, cgMod.GetMappedBB(unwindDest));
            auto resultVal = GenerateInvokeStatic(irBuilder, CHIRInvokeStaticWrapper(invokeStaticWithException));
            irBuilder.CreateBr(cgMod.GetMappedBB(normalDest));
            return resultVal;
        }
        case CHIR::ExprKind::INT_OP_WITH_EXCEPTION: {
            auto& intOpWithException = StaticCast<const CHIR::IntOpWithException&>(chirExpr);
            auto normalDest = intOpWithException.GetSuccessor(0);
            auto unwindDest = intOpWithException.GetSuccessor(1);
            CodeGenUnwindBlockScope unwindBlockScope(cgMod, cgMod.GetMappedBB(unwindDest));
            llvm::Value* resultVal = nullptr;
            if (intOpWithException.GetOperands().size() == 1) {
                resultVal = HandleUnaryExpression(irBuilder, CHIRUnaryExprWrapper(intOpWithException));
            } else {
                resultVal = HandleBinaryExpression(irBuilder, CHIRBinaryExprWrapper(intOpWithException));
            }
            irBuilder.CreateBr(cgMod.GetMappedBB(normalDest));
            return resultVal;
        }
        case CHIR::ExprKind::SPAWN_WITH_EXCEPTION: {
            auto& spawnWithException = StaticCast<const CHIR::SpawnWithException&>(chirExpr);
            auto normalDest = spawnWithException.GetSuccessor(0);
            auto unwindDest = spawnWithException.GetSuccessor(1);
            CodeGenUnwindBlockScope unwindBlockScope(cgMod, cgMod.GetMappedBB(unwindDest));
            auto resultVal = GenerateSpawn(irBuilder, CHIRSpawnWrapper(spawnWithException));
            irBuilder.CreateBr(cgMod.GetMappedBB(normalDest));
            return resultVal;
        }
        case CHIR::ExprKind::TYPECAST_WITH_EXCEPTION: {
            auto& typeCastWithException = StaticCast<const CHIR::TypeCastWithException&>(chirExpr);
            auto normalDest = typeCastWithException.GetSuccessor(0);
            auto unwindDest = typeCastWithException.GetSuccessor(1);
            CodeGenUnwindBlockScope unwindBlockScope(cgMod, cgMod.GetMappedBB(unwindDest));
            auto resultVal = GenerateTypeCast(irBuilder, CHIRTypeCastWrapper(typeCastWithException));
            irBuilder.CreateBr(cgMod.GetMappedBB(normalDest));
            return resultVal;
        }
        case CHIR::ExprKind::INTRINSIC_WITH_EXCEPTION: {
            auto& intrinsicWithException = StaticCast<const CHIR::IntrinsicWithException&>(chirExpr);
            auto normalDest = intrinsicWithException.GetSuccessor(0);
            auto unwindDest = intrinsicWithException.GetSuccessor(1);
            CodeGenUnwindBlockScope unwindBlockScope(cgMod, cgMod.GetMappedBB(unwindDest));
            auto resultVal = GenerateIntrinsic(irBuilder, CHIRIntrinsicWrapper(intrinsicWithException));
            irBuilder.CreateBr(cgMod.GetMappedBB(normalDest));
            return resultVal;
        }
        case CHIR::ExprKind::ALLOCATE_WITH_EXCEPTION: {
            auto& allocateWithException = StaticCast<const CHIR::AllocateWithException&>(chirExpr);
            auto normalDest = allocateWithException.GetSuccessor(0);
            auto unwindDest = allocateWithException.GetSuccessor(1);
            CodeGenUnwindBlockScope unwindBlockScope(cgMod, cgMod.GetMappedBB(unwindDest));
            auto resultVal = GenerateAllocate(irBuilder, CHIRAllocateWrapper(allocateWithException));
            irBuilder.CreateBr(cgMod.GetMappedBB(normalDest));
            return resultVal;
        }
        case CHIR::ExprKind::RAW_ARRAY_ALLOCATE_WITH_EXCEPTION: {
            auto& rawArrayWithException = StaticCast<const CHIR::RawArrayAllocateWithException&>(chirExpr);
            auto normalDest = rawArrayWithException.GetSuccessor(0);
            auto unwindDest = rawArrayWithException.GetSuccessor(1);
            CodeGenUnwindBlockScope unwindBlockScope(cgMod, cgMod.GetMappedBB(unwindDest));
            auto resultVal = GenerateRawArrayAllocate(irBuilder, CHIRRawArrayAllocateWrapper(rawArrayWithException));
            irBuilder.CreateBr(cgMod.GetMappedBB(normalDest));
            return resultVal;
        }
        default: {
            printf("Unexpected expr kind: %" PRIu64 "\n", static_cast<uint64_t>(chirExpr.GetExprKind()));
            CJC_ASSERT(false);
            return nullptr;
        }
    }
}
} // namespace Cangjie::CodeGen
