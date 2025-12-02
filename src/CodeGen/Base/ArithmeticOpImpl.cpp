// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "Base/ArithmeticOpImpl.h"

#include <cinttypes>

#include "llvm/IR/Module.h"

#include "Base/CHIRExprWrapper.h"
#include "IRBuilder.h"
#include "Utils/BlockScopeImpl.h"
#include "Utils/CGUtils.h"
#include "cangjie/CHIR/IR/Expression/Terminator.h"

namespace {
using namespace Cangjie;
using namespace Cangjie::CodeGen;

void HandleBaseIsOne(IRBuilder2& irBuilder, llvm::Argument* base)
{
    auto codeGenOne = llvm::ConstantInt::get(base->getType(), 1);
    auto baseEqOne = irBuilder.CreateICmpEQ(base, codeGenOne);
    auto [baseEqOneBB, baseNeqOneBB] =
        Vec2Tuple<2>(irBuilder.CreateAndInsertBasicBlocks({GenNameForBB("base.eq.one"), GenNameForBB("base.neq.one")}));
    irBuilder.CreateCondBr(baseEqOne, baseEqOneBB, baseNeqOneBB);
    irBuilder.SetInsertPoint(baseEqOneBB);
    irBuilder.CreateRet(codeGenOne);
    irBuilder.SetInsertPoint(baseNeqOneBB);
}

void HandleExponentIsZero(IRBuilder2& irBuilder, const llvm::Argument* base, llvm::Argument* exponent)
{
    auto expEqZero = irBuilder.CreateICmpEQ(exponent, llvm::ConstantInt::get(exponent->getType(), 0));
    auto [expEqZeroBB, expNeqZeroBB] =
        Vec2Tuple<2>(irBuilder.CreateAndInsertBasicBlocks({GenNameForBB("exp.eq.zero"), GenNameForBB("exp.neq.zero")}));
    irBuilder.CreateCondBr(expEqZero, expEqZeroBB, expNeqZeroBB);
    irBuilder.SetInsertPoint(expEqZeroBB);
    irBuilder.CreateRet(llvm::ConstantInt::get(base->getType(), 1));
    irBuilder.SetInsertPoint(expNeqZeroBB);
}

void HandleExponentIsPositive(
    IRBuilder2& irBuilder, llvm::Argument* base, llvm::Argument* exponent, llvm::Function* fastPowerFunc)
{
    auto codeGenOne = llvm::ConstantInt::get(exponent->getType(), 1);
    // let tmp be assigned fastPower(exp / 2) ^ 2
    auto newExp = irBuilder.CreateLShr(exponent, codeGenOne);
    llvm::Value* tmp = irBuilder.LLVMIRBuilder2::CreateCall(fastPowerFunc, {base, newExp});
    tmp = irBuilder.CreateMul(tmp, tmp);
    // 1) If exponent is an odd number, return base * tmp:
    auto expIsOdd = irBuilder.CreateICmpEQ(irBuilder.CreateAnd(exponent, codeGenOne), codeGenOne);
    auto [expIsOddBB, expIsEvenBB] =
        Vec2Tuple<2>(irBuilder.CreateAndInsertBasicBlocks({GenNameForBB("exp.is.odd"), GenNameForBB("exp.is.even")}));
    irBuilder.CreateCondBr(expIsOdd, expIsOddBB, expIsEvenBB);
    irBuilder.SetInsertPoint(expIsOddBB);
    irBuilder.CreateRet(irBuilder.CreateMul(base, tmp));
    irBuilder.SetInsertPoint(expIsEvenBB);
    // 2) If exponent is an even number, return tmp:
    irBuilder.CreateRet(tmp);
}

// Float64 ** Float64 => Float64
llvm::Value* CreateFastPowerCallForFloat64PowerFloat64(
    IRBuilder2& irBuilder, const CGValue* leftVal, const CGValue* rightVal)
{
    auto& cgMod = irBuilder.GetCGModule();
    auto paramTy = CGType::GetFloat64CGType(cgMod);
    auto float64Type = paramTy->GetLLVMType();
    auto valLeft = irBuilder.CreateFPCast(**leftVal, float64Type);
    auto valRight = irBuilder.CreateFPCast(**rightVal, float64Type);
    auto cgValLeft = CGValue(valLeft, paramTy);
    auto cgValRight = CGValue(valRight, paramTy);
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    llvm::Value* ret = irBuilder.CallIntrinsicFunction(float64Type, "CJ_CORE_CPow", {&cgValLeft, &cgValRight});
#endif
    ret = irBuilder.CreateFPCast(ret, float64Type);
    return ret;
}

// Float64 ** Int64 => Float64
llvm::Value* CreateFastPowerCallForFloat64PowerInt64(IRBuilder2& irBuilder, CGValue* leftVal, CGValue* rightVal)
{
    auto& cgMod = irBuilder.GetCGModule();
    llvm::Type* type = CGType::GetFloat64CGType(cgMod)->GetLLVMType();
    std::string runtimeFunc = "CJ_CORE_FastPowerDoubleInt64";
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    llvm::Value* ret = irBuilder.CallIntrinsicFunction(type, runtimeFunc, {leftVal, rightVal});
#endif
    return ret;
}

// Int64 ** UInt64 => Int64
llvm::Value* CreateFastPowerCallForInt64PowerUInt64(
    IRBuilder2& irBuilder, const CGValue* leftVal, const CGValue* rightVal)
{
    auto lType = (**leftVal)->getType();
    auto rType = (**rightVal)->getType();
    CJC_ASSERT(lType->isIntegerTy());
    CJC_ASSERT(rType->isIntegerTy());
    auto funcMangledName = "fastPower_" + GetTypeName(leftVal->GetCGType()) + GetTypeName(rightVal->GetCGType());
    auto function = irBuilder.GetCGModule().GetLLVMModule()->getFunction(funcMangledName);
    if (!function) {
        /* Generate the IR for the following code:
         * func fastPower_i64i64(x: Int64, y: UInt64): Int64 {
         *     if (x == 1) {
         *         return 1
         *     } else if (y == 0) {
         *         return 1
         *     }
         *     tmp = fastPower_i64i64(x, y / 2) ^ 2
         *     if (y & 1 == 1) {
         *         return tmp * x
         *     } else {
         *         return tmp
         *     }
         * }
         */
        std::vector<llvm::Type*> params{lType, rType};
        // lType is the same as return type.
        auto ft = CGType::GetCodeGenFunctionType(irBuilder.GetLLVMContext(), lType, params);
        // The return value type and parameter type of the "fastPower_*" function are the same.
        auto& cgMod = irBuilder.GetCGModule();
        auto callee = cgMod.GetLLVMModule()->getOrInsertFunction(funcMangledName, ft).getCallee();
        CJC_ASSERT(llvm::isa<llvm::Function>(callee));
        function = llvm::cast<llvm::Function>(callee);
        function->setPersonalityFn(cgMod.GetExceptionIntrinsicPersonality());
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
        SetGCCangjie(function);
#endif
        AddLinkageTypeMetadata(*function, llvm::GlobalValue::InternalLinkage, false);
        NameFunctionParam(function, {"base", "exponent"});
        irBuilder.CreateEntryBasicBlock(function, "entry");
        CodeGenFunctionScope funcScope(irBuilder, function);

        // Reset try/catch information since we are generating a new function
        auto base = function->arg_begin();
        auto exponent = function->arg_begin() + 1;
        // Handle: 1 ** exponent
        HandleBaseIsOne(irBuilder, base);

        // Handle: base ** 0
        HandleExponentIsZero(irBuilder, base, exponent);

        // Handle: base ** exponent
        HandleExponentIsPositive(irBuilder, base, exponent, function);
    }
    // use CreateCallOrInvoke so InvokeWrapper could set landingPad correctly when the pow exp is in a try-catch block.
    return irBuilder.CreateCallOrInvoke(function, {**leftVal, **rightVal});
}

// Convert integer type to integer type. Use for shift expr by now.
llvm::Value* GenerateIntegerConvExpr(IRBuilder2& irBuilder, const CHIRBinaryExprWrapper& binOp, llvm::Value* srcValue)
{
    auto leftArg = binOp.GetLHSOperand();
    auto rightArg = binOp.GetRHSOperand();

    auto targetTy = leftArg->GetType();
    auto srcTy = rightArg->GetType();
    auto targetTyKind = irBuilder.GetTypeKindFromType(*targetTy);
    auto srcTyKind = irBuilder.GetTypeKindFromType(*srcTy);
    CJC_ASSERT(targetTy && srcTy);
    // convert signed integer to signed integer.
    if (targetTy->IsInteger() && StaticCast<CHIR::IntType*>(targetTy)->IsSigned() && srcTy->IsInteger() &&
        StaticCast<CHIR::IntType*>(srcTy)->IsSigned()) {
        // target type is bigger than source type.
        if (targetTyKind > srcTyKind) {
            return irBuilder.CreateSExt(
                srcValue, CGType::GetOrCreate(irBuilder.GetCGModule(), targetTy)->GetLLVMType());
        }
        // target type is smaller than source type.
        return irBuilder.CreateTrunc(srcValue, CGType::GetOrCreate(irBuilder.GetCGModule(), targetTy)->GetLLVMType());
    }

    // convert unsigned integer to signed integer.
    if (targetTy->IsInteger() && StaticCast<CHIR::IntType*>(targetTy)->IsSigned()) {
        if (targetTyKind == INTEGER_CONVERT_MAP.at(srcTyKind)) {
            return srcValue;
        }
        // Target type is bigger than source type.
        if (targetTyKind > INTEGER_CONVERT_MAP.at(srcTyKind)) {
            return irBuilder.CreateZExt(
                srcValue, CGType::GetOrCreate(irBuilder.GetCGModule(), targetTy)->GetLLVMType());
        }
        // target type is smaller than source type.
        return irBuilder.CreateTrunc(srcValue, CGType::GetOrCreate(irBuilder.GetCGModule(), targetTy)->GetLLVMType());
    }

    // convert signed integer to unsigned integer.
    if (srcTy->IsInteger() && StaticCast<CHIR::IntType*>(srcTy)->IsSigned()) {
        if (INTEGER_CONVERT_MAP.at(targetTyKind) == srcTyKind) {
            return srcValue;
        }
        // target type is bigger than source type.
        if (INTEGER_CONVERT_MAP.at(targetTyKind) > srcTyKind) {
            return irBuilder.CreateZExt(
                srcValue, CGType::GetOrCreate(irBuilder.GetCGModule(), targetTy)->GetLLVMType());
        }
        // target type is smaller than source type.
        return irBuilder.CreateTrunc(srcValue, CGType::GetOrCreate(irBuilder.GetCGModule(), targetTy)->GetLLVMType());
    }

    // convert unsigned integer to unsigned integer.
    // target type is bigger than source type.
    if (targetTyKind > srcTyKind) {
        return irBuilder.CreateZExt(srcValue, CGType::GetOrCreate(irBuilder.GetCGModule(), targetTy)->GetLLVMType());
    }
    // target type is smaller than source type.
    return irBuilder.CreateTrunc(srcValue, CGType::GetOrCreate(irBuilder.GetCGModule(), targetTy)->GetLLVMType());
}

llvm::Value* GenerateOverShiftCheck(
    IRBuilder2& irBuilder, const CHIRBinaryExprWrapper& binOp, const CGValue* lValue, const CGValue* rValue)
{
    auto& cgMod = irBuilder.GetCGModule();
    auto rightTy = binOp.GetRHSOperand()->GetType();
    // cond: if right value of the right operand is negative
    auto zero = llvm::ConstantInt::get(CGType::GetOrCreate(cgMod, rightTy)->GetLLVMType(), 0);
    auto cmpResult = irBuilder.CreateICmpSLT(**rValue, zero, "if.cond.rightValue.negative");

    auto [thenBB, elseBB, endBB] = Vec2Tuple<3>(irBuilder.CreateAndInsertBasicBlocks(
        {GenNameForBB("if.then"), GenNameForBB("if.else"), GenNameForBB("if.end")}));
    irBuilder.CreateCondBr(cmpResult, thenBB, elseBB);

    // then: throw ArithmeticException("Overshift: Negative shift count!")
    irBuilder.SetInsertPoint(thenBB);
    irBuilder.CreateOverflowOrArithmeticException("Overshift: Negative shift count!", false);
    irBuilder.CreateUnreachable();

    irBuilder.SetInsertPoint(elseBB);
    // cond : if the value of right operand is greater than or equal to the width of left operand
    auto leftBitWidth = llvm::ConstantInt::get(
        rValue->GetCGType()->GetLLVMType(), lValue->GetCGType()->GetLLVMType()->getIntegerBitWidth());
    auto cmpResult2 = irBuilder.CreateICmpSGE(**rValue, leftBitWidth, "if.cond.bitwidth.compare");

    auto [then1BB, else1BB] =
        Vec2Tuple<2>(irBuilder.CreateAndInsertBasicBlocks({GenNameForBB("if.then"), GenNameForBB("if.else")}));
    irBuilder.CreateCondBr(cmpResult2, then1BB, else1BB);

    // then1: throw ArithmeticException("Overshift: Value of right operand is greater than or equal to the width of left
    // operand!")
    irBuilder.SetInsertPoint(then1BB);
    irBuilder.CreateOverflowOrArithmeticException(
        "Overshift: Value of right operand is greater than or equal to the width of left operand!", false);
    irBuilder.CreateUnreachable();

    // else1BB: normal shift
    irBuilder.SetInsertPoint(else1BB);
    llvm::Value* ret = nullptr;
    auto rConvValue = GenerateIntegerConvExpr(irBuilder, binOp, **rValue);
    if (binOp.GetBinaryExprKind() == CHIR::ExprKind::LSHIFT) {
        ret = irBuilder.CreateShl(**lValue, rConvValue, "shl");
    } else if (binOp.GetBinaryExprKind() == CHIR::ExprKind::RSHIFT) {
        auto opTy = binOp.GetResult()->GetType();
        if (opTy->IsInteger() && StaticCast<CHIR::IntType*>(opTy)->IsSigned()) {
            ret = irBuilder.CreateAShr(**lValue, rConvValue, "ashr");
        } else {
            ret = irBuilder.CreateLShr(**lValue, rConvValue, "lshr");
        }
    } else {
        CJC_ABORT();
    }
    irBuilder.CreateBr(endBB);

    // endBB
    irBuilder.SetInsertPoint(endBB);
    return ret;
}
} // namespace

namespace Cangjie {
namespace CodeGen {
llvm::Value* GenerateArithmeticOperation(IRBuilder2& irBuilder, const CHIRBinaryExprWrapper& binExpr)
{
    auto& cgMod = irBuilder.GetCGModule();
    CGValue* valLeft = cgMod | binExpr.GetLHSOperand();
    CGValue* valRight = cgMod | binExpr.GetRHSOperand();
    CJC_NULLPTR_CHECK(valLeft);
    CJC_NULLPTR_CHECK(valRight);
    return GenerateArithmeticOperation(
        irBuilder, binExpr.GetBinaryExprKind(), binExpr.GetResult()->GetType(), valLeft, valRight);
}

llvm::Value* GenerateArithmeticOperation(IRBuilder2& irBuilder, CHIR::ExprKind exprKind, const CHIR::Type* ty,
    const CGValue* valLeft, const CGValue* valRight)
{
    switch (exprKind) {
        case CHIR::ExprKind::ADD: {
            if (ty->IsFloat()) {
                return irBuilder.CreateFAdd(**valLeft, **valRight);
            } else {
                return irBuilder.CreateAdd(**valLeft, **valRight);
            }
        }
        case CHIR::ExprKind::SUB: {
            if (ty->IsFloat()) {
                return irBuilder.CreateFSub(**valLeft, **valRight);
            } else {
                return irBuilder.CreateSub(**valLeft, **valRight);
            }
        }
        case CHIR::ExprKind::MUL: {
            if (ty->IsFloat()) {
                return irBuilder.CreateFMul(**valLeft, **valRight);
            } else {
                return irBuilder.CreateMul(**valLeft, **valRight);
            }
        }
        case CHIR::ExprKind::DIV: {
            if (ty->IsFloat()) {
                return irBuilder.CreateFDiv(**valLeft, **valRight, "div");
            } else {
                bool isSignedInteger = ty->IsInteger() && StaticCast<const CHIR::IntType*>(ty)->IsSigned();
                return irBuilder.GenerateDivLikeCheck(**valLeft, **valRight, isSignedInteger, true);
            }
        }
        case CHIR::ExprKind::MOD: {
            if (ty->IsFloat()) {
                return irBuilder.CreateFRem(**valLeft, **valRight);
            } else {
                bool isSignedInteger = ty->IsInteger() && StaticCast<const CHIR::IntType*>(ty)->IsSigned();
                return irBuilder.GenerateDivLikeCheck(**valLeft, **valRight, isSignedInteger, false);
            }
        }
        default:
            printf("Unexpected expr kind: %" PRIu64 "\n", static_cast<uint64_t>(exprKind));
            CJC_ASSERT(false);
            return nullptr;
    }
}

llvm::Value* GenerateBitwiseOperation(IRBuilder2& irBuilder, const CHIRBinaryExprWrapper& binExpr)
{
    auto& cgMod = irBuilder.GetCGModule();
    CGValue* valLeft = cgMod | binExpr.GetLHSOperand();
    CGValue* valRight = cgMod | binExpr.GetRHSOperand();
    CJC_NULLPTR_CHECK(valLeft);
    CJC_NULLPTR_CHECK(valRight);
    irBuilder.EmitLocation(binExpr);
    switch (binExpr.GetBinaryExprKind()) {
        case CHIR::ExprKind::BITAND: {
            return irBuilder.CreateAnd(**valLeft, **valRight, "and");
        }
        case CHIR::ExprKind::BITOR: {
            return irBuilder.CreateOr(**valLeft, **valRight, "or");
        }
        case CHIR::ExprKind::BITXOR: {
            return irBuilder.CreateXor(**valLeft, **valRight, "xor");
        }
        case CHIR::ExprKind::LSHIFT:
        case CHIR::ExprKind::RSHIFT:
            return GenerateOverShiftCheck(irBuilder, binExpr, valLeft, valRight);
        default:
            printf("Unexpected expr kind: %" PRIu64 "\n", static_cast<uint64_t>(binExpr.GetBinaryExprKind()));
            CJC_ASSERT(false);
            return nullptr;
    }
}

llvm::Value* GenerateBinaryExpOperation(IRBuilder2& irBuilder, const CHIRBinaryExprWrapper& binExpr)
{
    auto& cgMod = irBuilder.GetCGModule();
    auto leftArg = binExpr.GetLHSOperand();
    auto rightArg = binExpr.GetRHSOperand();
    auto valLeft = cgMod | leftArg;
    auto valRight = cgMod | rightArg;
    irBuilder.EmitLocation(binExpr);
    return GenerateBinaryExpOperation(irBuilder, valLeft, valRight);
}

llvm::Value* GenerateBinaryExpOperation(IRBuilder2& irBuilder, CGValue* valLeft, CGValue* valRight)
{
    auto leftType = valLeft->GetCGType()->GetLLVMType();
    auto rightType = valRight->GetCGType()->GetLLVMType();
    if (leftType->isDoubleTy() && rightType->isDoubleTy()) {
        return CreateFastPowerCallForFloat64PowerFloat64(irBuilder, valLeft, valRight);
    } else if (leftType->isDoubleTy() && rightType->isIntegerTy()) {
        return CreateFastPowerCallForFloat64PowerInt64(irBuilder, valLeft, valRight);
    } else if (leftType->isIntegerTy() && rightType->isIntegerTy()) {
        return CreateFastPowerCallForInt64PowerUInt64(irBuilder, valLeft, valRight);
    } else {
        // There are only three scenarios for Exp operation:
        // 1. FLOAT64 exp FLOAT64 => FLOAT64
        // 2. FLOAT64 exp INT64 => FLOAT64
        // 3. INT64 exp UINT64 => INT64
        CJC_ABORT();
        return nullptr;
    }
}
} // namespace CodeGen
} // namespace Cangjie
