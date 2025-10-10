// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file realizes generate overflow APIs for cjnative codegen.
 */

#include "Base/OverflowDispatcher.h"

#include "Base/ArithmeticOpImpl.h"
#include "Base/CGTypes/CGEnumType.h"
#include "Base/ExprDispatcher/ExprDispatcher.h"
#include "Base/TypeCastImpl.h"
#include "IRBuilder.h"
#include "Utils/CGUtils.h"
#include "cangjie/CHIR/Expression/Terminator.h"
#include "cangjie/CHIR/Value.h"

namespace {

using namespace Cangjie;
using namespace CodeGen;

// store some instructions for calculating pow.
struct OverflowCalcAllocaInsts {
    llvm::AllocaInst* base;
    llvm::AllocaInst* acc;
    llvm::AllocaInst* exp;
};

struct StructInfo {
    llvm::AllocaInst* alc;
    llvm::Type* alcTy;
    std::vector<llvm::Type*> tys;
};

struct OverflowHandler {
public:
    IRBuilder2& irBuilder;
    const OverflowStrategy strategy;
    const std::pair<const CHIR::IntType*, const CHIR::Type*>& tys;
    const CHIR::ExprKind& kind;
    const std::vector<CGValue*>& argGenValues;
    const CHIR::IntType* ty{nullptr};

    OverflowHandler(IRBuilder2& irBuilder, const OverflowStrategy& strategy, const CHIR::ExprKind& kind,
        const std::pair<const CHIR::IntType*, const CHIR::Type*>& tys, const std::vector<CGValue*>& argGenValues)
        : irBuilder(irBuilder), strategy(strategy), tys(tys), kind(kind), argGenValues(argGenValues)
    {
        ty = tys.first;
    }

    inline bool IsChecked() const
    {
        return strategy == OverflowStrategy::CHECKED;
    }

    std::vector<llvm::Value*> GetLLVMValues() const
    {
        if (argGenValues.size() == 1) {
            return {argGenValues[0]->GetRawValue()};
        } else {
            return {argGenValues[0]->GetRawValue(), argGenValues[1]->GetRawValue()};
        }
    }

    inline bool IsDivOrMod() const
    {
        return kind == CHIR::ExprKind::DIV || kind == CHIR::ExprKind::MOD;
    }

    const CHIR::Type* GetElemTy() const
    {
        return tys.first;
    }

    const CHIR::Type* GetOptionTy() const
    {
        if (tys.second == nullptr) {
            return tys.first;
        }
        return tys.second;
    }

    llvm::Value* GenerateExpectValueAsFalse(llvm::Value* val);
    std::tuple<llvm::BasicBlock*, llvm::BasicBlock*, llvm::BasicBlock*> GenerateCheckOverflowFlag(llvm::Value* flag);
    llvm::Value* GenerateOverflowLogicCondition(const std::function<llvm::Value*()>& condLeft,
        const std::function<llvm::Value*()>& condRight, bool isLogicAnd = true);
    void GenerateOverflowElseBody(llvm::AllocaInst* ifValue, const StructInfo& valInfo);
    void GenerateOverflowThenBody(llvm::AllocaInst* ifValue, bool& needUnreachableTerminator);
    void GenerateOverflowOption(bool isSome, llvm::Value* val, llvm::AllocaInst* ifValue);
    llvm::Value* GenerateOverflowOpKindOption();
    llvm::Value* GenerateOverflowDivOrMod();
    void GenerateOverflowCalcMul(const OverflowCalcAllocaInsts& allocaInsts, bool isOddExp, const StructInfo& valInfo,
        llvm::BasicBlock* powEndBB);
    void GenerateOverflowCalcPowBody(
        const OverflowCalcAllocaInsts& allocaInsts, const StructInfo& valInfo, llvm::BasicBlock* powEndBB);
    void GenerateOverflowPowCalc(const StructInfo& valInfo, llvm::BasicBlock* powEndBB);
    void GenerateOverflowPowCheckParamBaseEqNegOne(const StructInfo& valInfo, llvm::BasicBlock* powEndBB);
    void GenerateOverflowPowCheckParam(
        const StructInfo& valInfo, llvm::BasicBlock* powCalcBB, llvm::BasicBlock* powEndBB);
    void GenerateOverflowCheckPow(const StructInfo& valInfo);
    void GenerateOverflowCheck(const StructInfo& valInfo);
    void GenerateOverflowSaturatingOp(llvm::AllocaInst* retValue);
    void GenerateOverflowSaturating(llvm::AllocaInst* retValue);
    void GenerateOverflowStrategy(llvm::AllocaInst* ifValue, bool& needUnreachableTerminator);
};

llvm::Value* OverflowHandler::GenerateExpectValueAsFalse(llvm::Value* val)
{
    CJC_ASSERT(val->getType()->isIntegerTy(1) && "val should be bool type!");
    CGType* boolType = CGType::GetBoolCGType(irBuilder.GetCGModule());
    llvm::Value* falseVal = irBuilder.GetFalse();
    // Call the intrinsic llvm.expect.i1(ov-flag, false)
    return irBuilder.GenerateCallExpectFunction(boolType, val, falseVal);
}

std::tuple<llvm::BasicBlock*, llvm::BasicBlock*, llvm::BasicBlock*> OverflowHandler::GenerateCheckOverflowFlag(
    llvm::Value* flag)
{
    auto condVal = GenerateExpectValueAsFalse(flag);
    auto [normalBB, overflowBB, endBB] = Vec2Tuple<3>(
        irBuilder.CreateAndInsertBasicBlocks({GenNameForBB("normal"), GenNameForBB("overflow"), GenNameForBB("end")}));
    (void)irBuilder.CreateCondBr(condVal, overflowBB, normalBB);
    return std::make_tuple(overflowBB, normalBB, endBB);
}

void OverflowHandler::GenerateOverflowElseBody(llvm::AllocaInst* ifValue, const StructInfo& valInfo)
{
    // emit else body.
    llvm::Value* res = nullptr;
    if (valInfo.alc == nullptr) {
        res = GenerateArithmeticOperation(irBuilder, kind, GetElemTy(), argGenValues[0], argGenValues[1]);
    } else {
        auto s0 = irBuilder.CreateStructGEP(valInfo.alcTy, valInfo.alc, 0);
        res = irBuilder.CreateLoad(valInfo.tys[0], s0);
    }

    if (IsChecked()) {
        // generate Option<Int8>.Some(num)
        GenerateOverflowOption(true, res, ifValue);
    } else {
        (void)irBuilder.CreateStore(res, ifValue);
    }
}

void OverflowHandler::GenerateOverflowThenBody(llvm::AllocaInst* ifValue, bool& needUnreachableTerminator)
{
    if (IsChecked()) {
        // generate Option<Int8>.None
        CGType* elemType = CGType::GetOrCreate(irBuilder.GetCGModule(), GetElemTy());
        auto zeroVal = llvm::ConstantInt::get(elemType->GetLLVMType(), 0);
        GenerateOverflowOption(false, zeroVal, ifValue);
        needUnreachableTerminator = false;
    } else {
        // throwing/saturating/wrapping.
        GenerateOverflowStrategy(ifValue, needUnreachableTerminator);
    }
}

/* Generate the IR for `if (condLeft && condRight)` or `if (condLeft || condRight)`:
 * If `isLogicAnd` is `true`, generate IR as follows:
 * entry:
 *   %and.val = alloca i1, align 1
 *   ...
 *   %0 = condLeft()
 *   br %0 %land.rsh, %tmpLabel
 *
 * tmpLabel:
 *   store i1 false, i1* %and.val, align 1
 *   br %land.end
 *
 * land.rsh:
 *   %1 = condRight()
 *   store i1 %1, i1* %and.val, align 1
 *   br %land.end
 *
 * land.end:
 *   %2 = load i1, i1* %and.val
 *
 * If `isLogicAnd` is false, here it refers to logic or, generate IR as follows:
 * entry:
 *   %or.val = alloca i1, align 1
 *   ...
 *   %0 = condLeft()
 *   br %0 %tmpLabel, %lor.rsh
 *
 * tmpLabel:
 *   store i1 true, i1* %or.val, align 1
 *   br %lor.end
 *
 * land.rsh:
 *   %1 = condRight()
 *   store i1 %1, i1* %or.val, align 1
 *   br %lor.end
 *
 * lor.end:
 *   %2 = load i1, i1* %or.val, align 1
 */
llvm::Value* OverflowHandler::GenerateOverflowLogicCondition(
    const std::function<llvm::Value*()>& condLeft, const std::function<llvm::Value*()>& condRight, bool isLogicAnd)
{
    std::string logicValueName = isLogicAnd ? "and.val" : "or.val";
    std::string rhsLabel = "l" + std::string(isLogicAnd ? "and" : "or") + ".rhs";
    std::string endLabel = "l" + std::string(isLogicAnd ? "and" : "or") + ".end";
    CGType* boolType = CGType::GetBoolCGType(irBuilder.GetCGModule());
    llvm::AllocaInst* logicValue = irBuilder.CreateEntryAlloca(boolType->GetLLVMType(), nullptr, logicValueName);
    llvm::Value* shortValue = isLogicAnd ? irBuilder.GetFalse() : irBuilder.GetTrue();

    auto [tmpBB, rhsBB, endBB] = Vec2Tuple<3>(irBuilder.CreateAndInsertBasicBlocks(
        {GenNameForBB("tmpLabel"), GenNameForBB(rhsLabel), GenNameForBB(endLabel)}));
    llvm::Value* condLeftVal = condLeft();

    CJC_ASSERT(condLeftVal->getType() == boolType->GetLLVMType());
    if (isLogicAnd) {
        (void)irBuilder.CreateCondBr(condLeftVal, rhsBB, tmpBB);
    } else {
        (void)irBuilder.CreateCondBr(condLeftVal, tmpBB, rhsBB);
    }

    // Append br instruction to tmp block.
    irBuilder.SetInsertPoint(tmpBB);
    (void)irBuilder.CreateStore(shortValue, logicValue);
    (void)irBuilder.CreateBr(endBB);

    irBuilder.SetInsertPoint(rhsBB);
    llvm::Value* condRightVal = condRight();
    CJC_ASSERT(condRightVal->getType() == boolType->GetLLVMType());
    (void)irBuilder.CreateStore(condRightVal, logicValue);
    (void)irBuilder.CreateBr(endBB);

    irBuilder.SetInsertPoint(endBB);
    return irBuilder.CreateLoad(boolType->GetLLVMType(), logicValue);
}

void OverflowHandler::GenerateOverflowOption(bool isSome, llvm::Value* val, llvm::AllocaInst* ifValue)
{
    llvm::Value* firstVal = isSome ? irBuilder.getFalse() : irBuilder.getTrue();
    (void)irBuilder.CreateStore(firstVal, irBuilder.CreateStructGEP(ifValue->getAllocatedType(), ifValue, 0));
    (void)irBuilder.CreateStore(val, irBuilder.CreateStructGEP(ifValue->getAllocatedType(), ifValue, 1));
}

llvm::Value* OverflowHandler::GenerateOverflowOpKindOption()
{
    if (!IsChecked()) {
        return GenerateOverflowWrappingArithmeticOp(irBuilder, kind, ty, argGenValues);
    }
    llvm::Value* val = GenerateArithmeticOperation(irBuilder, kind, GetElemTy(), argGenValues[0], argGenValues[1]);
    const CHIR::Type* optionTy = GetOptionTy();
    CGType* optionType = CGType::GetOrCreate(irBuilder.GetCGModule(), optionTy);
    llvm::AllocaInst* retValue = irBuilder.CreateEntryAlloca(optionType->GetLLVMType());

    // Generate option some.
    GenerateOverflowOption(true, val, retValue);
    return irBuilder.CreateLoad(retValue->getAllocatedType(), retValue);
}

llvm::Value* OverflowHandler::GenerateOverflowDivOrMod()
{
    auto& cgMod = irBuilder.GetCGModule();
    const CHIR::Type* elemTy = GetElemTy();
    if (!ty->IsSigned()) {
        return GenerateOverflowOpKindOption();
    }
    const CHIR::Type* optionTy = GetOptionTy();
    CGType* elemType = CGType::GetOrCreate(cgMod, elemTy);
    CGType* optionType = CGType::GetOrCreate(cgMod, optionTy);

    // Overflow condition: x <= MinInt8 && y == -1.
    auto minVal = llvm::ConstantInt::getSigned(elemType->GetLLVMType(), GetIntMaxOrMin(cgMod, *ty, false));
    auto leftCond = [this, &minVal]() { return irBuilder.CreateICmpSLE(argGenValues[0]->GetRawValue(), minVal); };
    auto negativeOne = llvm::ConstantInt::getSigned(elemType->GetLLVMType(), -1);
    auto rightCond = [this, &negativeOne]() {
        return irBuilder.CreateICmpEQ(argGenValues[1]->GetRawValue(), negativeOne);
    };
    llvm::Value* condV = GenerateOverflowLogicCondition(leftCond, rightCond);
    llvm::BasicBlock* overflowBB = nullptr;
    llvm::BasicBlock* normalBB = nullptr;
    llvm::BasicBlock* endBB = nullptr;
    std::tie(overflowBB, normalBB, endBB) = GenerateCheckOverflowFlag(condV);
    CJC_NULLPTR_CHECK(normalBB);
    CJC_NULLPTR_CHECK(overflowBB);
    CJC_NULLPTR_CHECK(endBB);

    CGType* type = IsChecked() ? optionType : elemType;
    llvm::AllocaInst* ifValue = irBuilder.CreateEntryAlloca(type->GetLLVMType());
    // Emit non-overflow body first to make it closer to the above block.
    irBuilder.SetInsertPoint(normalBB);
    GenerateOverflowElseBody(ifValue, {nullptr, nullptr, {}});
    (void)irBuilder.CreateBr(endBB);

    // Emit overflow body.
    irBuilder.SetInsertPoint(overflowBB);
    bool needUnrachableTerminator = false;
    GenerateOverflowThenBody(ifValue, needUnrachableTerminator);
    needUnrachableTerminator ? (void)irBuilder.CreateUnreachable() : (void)irBuilder.CreateBr(endBB);

    irBuilder.SetInsertPoint(endBB);
    return irBuilder.CreateLoad(ifValue->getAllocatedType(), ifValue);
}

void OverflowHandler::GenerateOverflowCheck(const StructInfo& valInfo)
{
    std::vector<llvm::Value*> values = GetLLVMValues();
    auto rightVal = irBuilder.GenerateOverflowCheckedFunc(kind, *ty, values);
    CJC_ASSERT(rightVal);
    (void)irBuilder.CreateStore(rightVal, valInfo.alc);
}

void OverflowHandler::GenerateOverflowCheckPow(const StructInfo& valInfo)
{
    auto [powCheckParamBB, powCalcBB, powEndBB] = Vec2Tuple<3>(irBuilder.CreateAndInsertBasicBlocks(
        {GenNameForBB("pow.check.param"), GenNameForBB("pow.calc"), GenNameForBB("pow.end")}));
    (void)irBuilder.CreateBr(powCheckParamBB);

    // emit pow.check.param.
    irBuilder.SetInsertPoint(powCheckParamBB);
    GenerateOverflowPowCheckParam(valInfo, powCalcBB, powEndBB);

    // emit pow.calc.
    irBuilder.SetInsertPoint(powCalcBB);
    GenerateOverflowPowCalc(valInfo, powEndBB);
    (void)irBuilder.CreateBr(powEndBB);

    irBuilder.SetInsertPoint(powEndBB);
}

/** Generate the IR for the following code:
 *  if (base == 1 || exp == 0) {
 *      return (1, false)
 *  } else if (base == 0) {
 *      return (0, false)
 *  } else if (base == -1) {
 *      if ((exp & 1) == 1) {
 *          return (-1, false)
 *      } else {
 *          return (1, false)
 *      }
 *  }
 */
void OverflowHandler::GenerateOverflowPowCheckParam(
    const StructInfo& valInfo, llvm::BasicBlock* powCalcBB, llvm::BasicBlock* powEndBB)
{
    llvm::Type* type = CGType::GetOrCreate(irBuilder.GetCGModule(), ty)->GetLLVMType();
    auto zeroVal = llvm::ConstantInt::get(type, 0);
    auto oneVal = llvm::ConstantInt::get(type, 1);
    auto negOneVal = llvm::ConstantInt::getSigned(type, -1);
    auto falseVal = irBuilder.GetFalse();

    auto [baseEqOneBB, baseNeqOneBB, baseEqZeroBB, baseNeqZeroBB, baseEqNegOneBB] =
        Vec2Tuple<5>(irBuilder.CreateAndInsertBasicBlocks({GenNameForBB("base.eq.one"), GenNameForBB("base.neq.one"),
            GenNameForBB("base.eq.zero"), GenNameForBB("base.neq.zero"), GenNameForBB("base.eq.neg.one")}));

    // condition: (base == 1 || exp == 0).
    auto leftCond = [this, &oneVal]() { return irBuilder.CreateICmpEQ(argGenValues[0]->GetRawValue(), oneVal); };
    auto rightCond = [this, &zeroVal]() { return irBuilder.CreateICmpEQ(argGenValues[1]->GetRawValue(), zeroVal); };
    auto baseEqOneCond = GenerateOverflowLogicCondition(leftCond, rightCond, false);
    (void)irBuilder.CreateCondBr(baseEqOneCond, baseEqOneBB, baseNeqOneBB);

    irBuilder.SetInsertPoint(baseEqOneBB);
    // retVal: (1, false).
    (void)irBuilder.CreateStore(oneVal, irBuilder.CreateStructGEP(valInfo.alcTy, valInfo.alc, 0));
    (void)irBuilder.CreateStore(falseVal, irBuilder.CreateStructGEP(valInfo.alcTy, valInfo.alc, 1));
    (void)irBuilder.CreateBr(powEndBB);

    irBuilder.SetInsertPoint(baseNeqOneBB);
    // condition: (base == 0).
    auto baseEqZeroCond = irBuilder.CreateICmpEQ(argGenValues[0]->GetRawValue(), zeroVal);
    (void)irBuilder.CreateCondBr(baseEqZeroCond, baseEqZeroBB, baseNeqZeroBB);

    irBuilder.SetInsertPoint(baseEqZeroBB);
    // retVal: (0, false)
    (void)irBuilder.CreateStore(zeroVal, irBuilder.CreateStructGEP(valInfo.alcTy, valInfo.alc, 0));
    (void)irBuilder.CreateStore(falseVal, irBuilder.CreateStructGEP(valInfo.alcTy, valInfo.alc, 1));
    (void)irBuilder.CreateBr(powEndBB);

    irBuilder.SetInsertPoint(baseNeqZeroBB);
    // condition: (base == -1).
    auto baseEqNegOneCond = irBuilder.CreateICmpEQ(argGenValues[0]->GetRawValue(), negOneVal);
    (void)irBuilder.CreateCondBr(baseEqNegOneCond, baseEqNegOneBB, powCalcBB);

    irBuilder.SetInsertPoint(baseEqNegOneBB);
    GenerateOverflowPowCheckParamBaseEqNegOne(valInfo, powEndBB);
}

/** Generate the IR for the following code:
 *  if ((exp & 1) == 1) {
 *      return (-1, false)
 *  } else {
 *      return (1, false)
 *  }
 */
void OverflowHandler::GenerateOverflowPowCheckParamBaseEqNegOne(const StructInfo& valInfo, llvm::BasicBlock* powEndBB)
{
    llvm::Type* type = CGType::GetOrCreate(irBuilder.GetCGModule(), ty)->GetLLVMType();
    auto oneVal = llvm::ConstantInt::get(type, 1);
    auto negOneVal = llvm::ConstantInt::getSigned(type, -1);
    auto falseVal = irBuilder.GetFalse();
    auto [expIsOddBB, expIsEvenBB] =
        Vec2Tuple<2>(irBuilder.CreateAndInsertBasicBlocks({GenNameForBB("exp.is.odd"), GenNameForBB("exp.is.even")}));

    // condition: ((exp & 1) == 1).
    auto expAndOne = irBuilder.CreateAnd(argGenValues[1]->GetRawValue(), oneVal);
    auto expIsOddCond = irBuilder.CreateICmpEQ(expAndOne, oneVal);
    (void)irBuilder.CreateCondBr(expIsOddCond, expIsOddBB, expIsEvenBB);

    irBuilder.SetInsertPoint(expIsOddBB);
    // retVal: (-1, false)
    (void)irBuilder.CreateStore(negOneVal, irBuilder.CreateStructGEP(valInfo.alcTy, valInfo.alc, 0));
    (void)irBuilder.CreateStore(falseVal, irBuilder.CreateStructGEP(valInfo.alcTy, valInfo.alc, 1));
    (void)irBuilder.CreateBr(powEndBB);

    irBuilder.SetInsertPoint(expIsEvenBB);
    // retVal: (1, false)
    (void)irBuilder.CreateStore(oneVal, irBuilder.CreateStructGEP(valInfo.alcTy, valInfo.alc, 0));
    (void)irBuilder.CreateStore(falseVal, irBuilder.CreateStructGEP(valInfo.alcTy, valInfo.alc, 1));
    (void)irBuilder.CreateBr(powEndBB);
}

/* Generate the IR for the following code:
 *  var base = base(param)
 *  var exp = exponent(param)
 *  var acc: Int64 = 1
 *  while (exp > 1) {
 *      if ((exp & 1) == 1) {
 *          let (ret, bOverflow) = overflowingMul(acc, base)
 *          if (bOverflow && bRet) {
 *              return (ret, true)
 *          }
 *          acc = ret
 *      }
 *      exp /= 2
 *      let (ret, bOverflow) = overflowingMul(base, base)
 *      if (bOverflow && bRet) {
 *          return (ret, true)
 *      }
 *      base = ret
 *  }
 *  let (ret, bOverflow) = overflowingMul(acc, base)
 *  return (ret, bOverflow)
 */
void OverflowHandler::GenerateOverflowPowCalc(const StructInfo& valInfo, llvm::BasicBlock* powEndBB)
{
    OverflowCalcAllocaInsts allocaInsts;
    CGType* type = CGType::GetOrCreate(irBuilder.GetCGModule(), ty);
    auto oneVal = llvm::ConstantInt::get(type->GetLLVMType(), 1);

    allocaInsts.base = irBuilder.CreateEntryAlloca(type->GetLLVMType(), nullptr, "base");
    allocaInsts.acc = irBuilder.CreateEntryAlloca(type->GetLLVMType(), nullptr, "acc");
    allocaInsts.exp = irBuilder.CreateEntryAlloca(argGenValues[1]->GetRawValue()->getType(), nullptr, "exp");
    (void)irBuilder.CreateStore(argGenValues[0]->GetRawValue(), allocaInsts.base);
    (void)irBuilder.CreateStore(argGenValues[1]->GetRawValue(), allocaInsts.exp);
    (void)irBuilder.CreateStore(oneVal, allocaInsts.acc);

    auto [whileBB, thenBB, endBB] = Vec2Tuple<3>(irBuilder.CreateAndInsertBasicBlocks(
        {GenNameForBB("while"), GenNameForBB("while.then"), GenNameForBB("while.end")}));

    (void)irBuilder.CreateBr(whileBB);
    irBuilder.SetInsertPoint(whileBB);
    // while Condition: exp > 1.
    auto condV = irBuilder.CreateICmpUGT(
        irBuilder.CreateLoad(allocaInsts.exp->getAllocatedType(), allocaInsts.exp), oneVal, "icmpugt");
    (void)irBuilder.CreateCondBr(condV, thenBB, endBB);

    // emit while then body.
    irBuilder.SetInsertPoint(thenBB);
    GenerateOverflowCalcPowBody(allocaInsts, valInfo, powEndBB);
    (void)irBuilder.CreateBr(whileBB);

    // emit while end body.
    irBuilder.SetInsertPoint(endBB);
    auto accVal = irBuilder.CreateLoad(allocaInsts.acc->getAllocatedType(), allocaInsts.acc);
    auto baseVal = irBuilder.CreateLoad(allocaInsts.base->getAllocatedType(), allocaInsts.base);
    std::vector<llvm::Value*> argGenNewValues{accVal, baseVal};
    auto rightVal = irBuilder.GenerateOverflowCheckedFunc(CHIR::ExprKind::MUL, *ty, argGenNewValues);
    (void)irBuilder.CreateStore(rightVal, valInfo.alc);
}

/* Generate the IR for the following code:
 *  if ((exp & 1) == 1) {
 *      let (ret, bOverflow) = overflowingMul(acc, base)
 *      if (bOverflow) {
 *          return (ret, true)
 *      }
 *      acc = ret
 *  }
 *  exp /= 2
 *  let (ret, bOverflow) = overflowingMul(base, base)
 *  if (bOverflow) {
 *      return (ret, true)
 *  }
 *  base = ret
 */
void OverflowHandler::GenerateOverflowCalcPowBody(
    const OverflowCalcAllocaInsts& allocaInsts, const StructInfo& valInfo, llvm::BasicBlock* powEndBB)
{
    CGType* type = CGType::GetOrCreate(irBuilder.GetCGModule(), ty);
    auto [thenBB, endBB] =
        Vec2Tuple<2>(irBuilder.CreateAndInsertBasicBlocks({GenNameForBB("if.then"), GenNameForBB("if.end")}));
    auto expVal = irBuilder.CreateLoad(allocaInsts.exp->getAllocatedType(), allocaInsts.exp);
    auto oneVal = llvm::ConstantInt::get(type->GetLLVMType(), 1);

    // condition: (exp & 1) == 1
    auto left = irBuilder.CreateAnd(expVal, oneVal, "and");
    auto condV = irBuilder.CreateICmpEQ(left, oneVal, "icmpeq");
    (void)irBuilder.CreateCondBr(condV, thenBB, endBB);

    irBuilder.SetInsertPoint(thenBB);
    GenerateOverflowCalcMul(allocaInsts, true, valInfo, powEndBB);
    (void)irBuilder.CreateBr(endBB);

    irBuilder.SetInsertPoint(endBB);
    auto twoVal = llvm::ConstantInt::get(type->GetLLVMType(), 2); // "2" is the value of divisor.
    llvm::Value* newExpVal = irBuilder.CreateUDiv(expVal, twoVal);
    (void)irBuilder.CreateStore(newExpVal, allocaInsts.exp);
    GenerateOverflowCalcMul(allocaInsts, false, valInfo, powEndBB);
}

void OverflowHandler::GenerateOverflowCalcMul(
    const OverflowCalcAllocaInsts& allocaInsts, bool isOddExp, const StructInfo& valInfo, llvm::BasicBlock* powEndBB)
{
    auto [thenBB, endBB] =
        Vec2Tuple<2>(irBuilder.CreateAndInsertBasicBlocks({GenNameForBB("if.then"), GenNameForBB("if.end")}));
    auto rVal = irBuilder.CreateLoad(allocaInsts.base->getAllocatedType(), allocaInsts.base);
    auto lVal = isOddExp ? irBuilder.CreateLoad(allocaInsts.acc->getAllocatedType(), allocaInsts.acc) : rVal;
    std::vector<llvm::Value*> argGenNewValues{lVal, rVal};
    auto rightVal = irBuilder.GenerateOverflowCheckedFunc(CHIR::ExprKind::MUL, *ty, argGenNewValues);
    (void)irBuilder.CreateStore(rightVal, valInfo.alc);
    auto s1 = irBuilder.CreateStructGEP(valInfo.alcTy, valInfo.alc, 1);
    auto condV = irBuilder.CreateLoad(valInfo.tys[1], s1);
    (void)irBuilder.CreateCondBr(condV, thenBB, endBB);

    irBuilder.SetInsertPoint(thenBB);
    (void)irBuilder.CreateBr(powEndBB);

    irBuilder.SetInsertPoint(endBB);
    auto s0 = irBuilder.CreateStructGEP(valInfo.alcTy, valInfo.alc, 0);
    auto retNewVal = irBuilder.CreateLoad(valInfo.tys[0], s0);
    if (isOddExp) {
        (void)irBuilder.CreateStore(retNewVal, allocaInsts.acc);
    } else {
        (void)irBuilder.CreateStore(retNewVal, allocaInsts.base);
    }
}

void OverflowHandler::GenerateOverflowSaturatingOp(llvm::AllocaInst* retValue)
{
    CJC_ASSERT(argGenValues.size() == 2 && "should have two operands"); // Binary operations should have 2 operands.
    auto& cgMod = irBuilder.GetCGModule();
    llvm::Type* type = CGType::GetOrCreate(cgMod, ty)->GetLLVMType();
    auto zeroVal = llvm::ConstantInt::get(type, 0);

    llvm::Value* condV = nullptr;
    if (kind == CHIR::ExprKind::ADD) {
        // condition: x > 0 && y > 0.
        auto leftCond = [this, &zeroVal]() { return irBuilder.CreateICmpSGT(argGenValues[0]->GetRawValue(), zeroVal); };
        auto rightCond = [this, &zeroVal]() {
            return irBuilder.CreateICmpSGT(argGenValues[1]->GetRawValue(), zeroVal);
        };
        condV = GenerateOverflowLogicCondition(leftCond, rightCond);
    }
    if (kind == CHIR::ExprKind::SUB) {
        // condition: x >= 0 && y < 0.
        auto leftCond = [this, &zeroVal]() { return irBuilder.CreateICmpSGE(argGenValues[0]->GetRawValue(), zeroVal); };
        auto rightCond = [this, &zeroVal]() {
            return irBuilder.CreateICmpSLT(argGenValues[1]->GetRawValue(), zeroVal);
        };
        condV = GenerateOverflowLogicCondition(leftCond, rightCond);
    }
    if (kind == CHIR::ExprKind::MUL) {
        // condition: (x > 0) == (y > 0).
        auto leftCond = irBuilder.CreateICmpSGT(argGenValues[0]->GetRawValue(), zeroVal, "icmpsgt");
        auto rightCond = irBuilder.CreateICmpSGT(argGenValues[1]->GetRawValue(), zeroVal, "icmpsgt");
        condV = irBuilder.CreateICmpEQ(leftCond, rightCond, "icmpeq");
    }
    if (kind == CHIR::ExprKind::EXP) {
        // condition: base > 0 || (exponent & 1) == 0
        auto leftCond = [this, &zeroVal]() { return irBuilder.CreateICmpSGT(argGenValues[0]->GetRawValue(), zeroVal); };
        auto rightCond = [this, &type]() {
            auto right = irBuilder.CreateAnd(argGenValues[1]->GetRawValue(), llvm::ConstantInt::get(type, 1));
            return irBuilder.CreateICmpEQ(right, llvm::ConstantInt::get(type, 0));
        };
        condV = GenerateOverflowLogicCondition(leftCond, rightCond, false);
    }

    auto [thenBB, elseBB, endBB] = Vec2Tuple<3>(irBuilder.CreateAndInsertBasicBlocks(
        {GenNameForBB("if.then"), GenNameForBB("if.else"), GenNameForBB("if.end")}));
    (void)irBuilder.CreateCondBr(condV, thenBB, elseBB);

    // emit then body.
    irBuilder.SetInsertPoint(thenBB);
    // Saturating: MaxInt8.
    auto maxVal = llvm::ConstantInt::getSigned(type, GetIntMaxOrMin(cgMod, *ty, true));
    (void)irBuilder.CreateStore(maxVal, retValue);
    (void)irBuilder.CreateBr(endBB);

    // emit else body.
    irBuilder.SetInsertPoint(elseBB);
    // Saturating: MinInt8.
    auto minVal = llvm::ConstantInt::getSigned(type, GetIntMaxOrMin(cgMod, *ty, false));
    (void)irBuilder.CreateStore(minVal, retValue);
    (void)irBuilder.CreateBr(endBB);

    irBuilder.SetInsertPoint(endBB);
}

void OverflowHandler::GenerateOverflowSaturating(llvm::AllocaInst* retValue)
{
    auto& cgMod = irBuilder.GetCGModule();
    llvm::Type* type = CGType::GetOrCreate(cgMod, ty)->GetLLVMType();
    if (!ty->IsSigned()) {
        // Unsigned Integer: MaxUInt8/0.
        if (kind == CHIR::ExprKind::ADD || kind == CHIR::ExprKind::MUL || kind == CHIR::ExprKind::EXP) {
            // add/mul/pow: MaxUInt8
            auto maxVal = llvm::ConstantInt::get(type, GetUIntMax(cgMod, *ty));
            (void)irBuilder.CreateStore(maxVal, retValue);
        } else {
            // sub/dec/neg: 0
            auto minVal = llvm::ConstantInt::get(type, 0);
            (void)irBuilder.CreateStore(minVal, retValue);
        }
        return;
    }

    if (kind == CHIR::ExprKind::ADD || kind == CHIR::ExprKind::SUB || kind == CHIR::ExprKind::MUL ||
        kind == CHIR::ExprKind::EXP) {
        GenerateOverflowSaturatingOp(retValue);
        return;
    }
    llvm::Value* val;
    if (kind == CHIR::ExprKind::MOD) {
        val = llvm::ConstantInt::getSigned(type, 0);
    } else {
        // inc/neg: MaxInt8
        val = llvm::ConstantInt::getSigned(type, GetIntMaxOrMin(cgMod, *ty, true));
    }
    (void)irBuilder.CreateStore(val, retValue);
}

void OverflowHandler::GenerateOverflowStrategy(llvm::AllocaInst* ifValue, bool& needUnreachableTerminator)
{
    // Throwing.
    if (strategy == OverflowStrategy::THROWING) {
        // If the operator is remainder, according to previous logic,
        // the expression must be INT_MIN % -1.
        // The result of INT_MIN % -1 is 0 which is defined in spec.
        if (kind == CHIR::ExprKind::MOD) {
            llvm::Type* type = CGType::GetOrCreate(irBuilder.GetCGModule(), ty)->GetLLVMType();
            (void)irBuilder.CreateStore(llvm::ConstantInt::getSigned(type, 0), ifValue);
            needUnreachableTerminator = false;
        } else if (kind == CHIR::ExprKind::NEG) {
            irBuilder.CreateOverflowOrArithmeticException("sub");
            needUnreachableTerminator = true;
        } else {
            auto op = OPERATOR_KIND_TO_OP_MAP.at(kind);
            irBuilder.CreateOverflowOrArithmeticException(op);
            needUnreachableTerminator = true;
        }
        return;
    }

    // Wrapping.
    if (strategy == OverflowStrategy::WRAPPING) {
        auto values = GetLLVMValues();
        llvm::Value* minVal = irBuilder.GenerateOverflowWrappingFunc(kind, *ty, values);
        (void)irBuilder.CreateStore(minVal, ifValue);
        needUnreachableTerminator = false;
        return;
    }
    // Saturating.
    GenerateOverflowSaturating(ifValue);
    needUnreachableTerminator = false;
    return;
}

} // namespace

llvm::Value* Cangjie::CodeGen::GenerateOverflow(IRBuilder2& irBuilder, const OverflowStrategy& strategy,
    const CHIR::ExprKind& kind, const std::pair<const CHIR::IntType*, const CHIR::Type*>& tys,
    const std::vector<CGValue*>& argGenValues)
{
    OverflowHandler handler = OverflowHandler(irBuilder, strategy, kind, tys, argGenValues);
    if (strategy == OverflowStrategy::SATURATING) {
        // Simply handle `add op` and non-signed-integer `sub op`.
        auto values = handler.GetLLVMValues();
        auto rightValue = irBuilder.GenerateOverflowSaturatingFunc(kind, *handler.ty, values);
        if (rightValue != nullptr) {
            return rightValue;
        }
    }

    if (handler.IsDivOrMod()) {
        return handler.GenerateOverflowDivOrMod();
    }
    if (strategy == OverflowStrategy::WRAPPING) {
        return handler.GenerateOverflowOpKindOption();
    }
    const CHIR::Type* elemTy = handler.GetElemTy();
    const CHIR::Type* optionTy = handler.GetOptionTy();
    CGType* elemType = CGType::GetOrCreate(irBuilder.GetCGModule(), elemTy);
    CGType* optionType = CGType::GetOrCreate(irBuilder.GetCGModule(), optionTy);

    // Compute the result with an extra overflow flag.
    // Return value: (result, ov-flag: indicating whether overflow happens).
    llvm::Type* boolType = CGType::GetBoolCGType(irBuilder.GetCGModule())->GetLLVMType();
    std::vector<llvm::Type*> types{elemType->GetLLVMType(), boolType};
    llvm::Type* structTy = llvm::StructType::get(irBuilder.GetLLVMContext(), types);
    StructInfo valInfo = {nullptr, structTy, types};
    if (kind == CHIR::ExprKind::EXP) {
        valInfo.alc = irBuilder.CreateEntryAlloca(structTy, nullptr, "pow.ov");
        handler.GenerateOverflowCheckPow(valInfo);
    } else {
        valInfo.alc = irBuilder.CreateEntryAlloca(structTy, nullptr, "val.ov");
        handler.GenerateOverflowCheck(valInfo);
    }

    auto ovFlag = irBuilder.CreateLoad(valInfo.tys[1], irBuilder.CreateStructGEP(valInfo.alcTy, valInfo.alc, 1));
    llvm::BasicBlock* overflowBB = nullptr;
    llvm::BasicBlock* normalBB = nullptr;
    llvm::BasicBlock* endBB = nullptr;
    std::tie(overflowBB, normalBB, endBB) = handler.GenerateCheckOverflowFlag(ovFlag);
    CJC_NULLPTR_CHECK(normalBB);
    CJC_NULLPTR_CHECK(overflowBB);
    CJC_NULLPTR_CHECK(endBB);
    CGType* ifType = handler.IsChecked() ? optionType : elemType;
    llvm::AllocaInst* ifValue = irBuilder.CreateEntryAlloca(ifType->GetLLVMType());

    // Emit non-overflow body first to make it closer to the above block.
    irBuilder.SetInsertPoint(normalBB);
    handler.GenerateOverflowElseBody(ifValue, valInfo);
    (void)irBuilder.CreateBr(endBB);

    // Emit overflow body.
    irBuilder.SetInsertPoint(overflowBB);
    bool needUnrachableTerminator = false;
    handler.GenerateOverflowThenBody(ifValue, needUnrachableTerminator);
    needUnrachableTerminator ? (void)irBuilder.CreateUnreachable() : (void)irBuilder.CreateBr(endBB);

    irBuilder.SetInsertPoint(endBB);
    return irBuilder.CreateLoad(ifValue->getAllocatedType(), ifValue);
}
