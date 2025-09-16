// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file realizes generate overflow APIs for codegen.
 */

#include "Base/OverflowDispatcher.h"

#include "Base/ArithmeticOpImpl.h"
#include "Base/ExprDispatcher/ExprDispatcher.h"
#include "Base/IntrinsicsDispatcher.h"
#include "Base/LogicalOpImpl.h"
#include "cangjie/CHIR/Expression/Terminator.h"
#include "cangjie/CHIR/Value.h"

namespace Cangjie {
namespace CodeGen {

namespace {
const auto OVERFLOW_FUNC_TO_INFO = []() noexcept {
    return std::unordered_map<CHIR::IntrinsicKind, std::pair<CHIR::ExprKind, OverflowStrategy>> {
        {CHIR::IntrinsicKind::OVERFLOW_THROWING_ADD, {CHIR::ExprKind::ADD, OverflowStrategy::THROWING}},
        {CHIR::IntrinsicKind::OVERFLOW_SATURATING_ADD, {CHIR::ExprKind::ADD, OverflowStrategy::SATURATING}},
        {CHIR::IntrinsicKind::OVERFLOW_WRAPPING_ADD, {CHIR::ExprKind::ADD, OverflowStrategy::WRAPPING}},
        {CHIR::IntrinsicKind::OVERFLOW_CHECKED_ADD, {CHIR::ExprKind::ADD, OverflowStrategy::CHECKED}},
        {CHIR::IntrinsicKind::OVERFLOW_THROWING_SUB, {CHIR::ExprKind::SUB, OverflowStrategy::THROWING}},
        {CHIR::IntrinsicKind::OVERFLOW_SATURATING_SUB, {CHIR::ExprKind::SUB, OverflowStrategy::SATURATING}},
        {CHIR::IntrinsicKind::OVERFLOW_WRAPPING_SUB, {CHIR::ExprKind::SUB, OverflowStrategy::WRAPPING}},
        {CHIR::IntrinsicKind::OVERFLOW_CHECKED_SUB, {CHIR::ExprKind::SUB, OverflowStrategy::CHECKED}},
        {CHIR::IntrinsicKind::OVERFLOW_THROWING_MUL, {CHIR::ExprKind::MUL, OverflowStrategy::THROWING}},
        {CHIR::IntrinsicKind::OVERFLOW_SATURATING_MUL, {CHIR::ExprKind::MUL, OverflowStrategy::SATURATING}},
        {CHIR::IntrinsicKind::OVERFLOW_WRAPPING_MUL, {CHIR::ExprKind::MUL, OverflowStrategy::WRAPPING}},
        {CHIR::IntrinsicKind::OVERFLOW_CHECKED_MUL, {CHIR::ExprKind::MUL, OverflowStrategy::CHECKED}},
        {CHIR::IntrinsicKind::OVERFLOW_THROWING_DIV, {CHIR::ExprKind::DIV, OverflowStrategy::THROWING}},
        {CHIR::IntrinsicKind::OVERFLOW_SATURATING_DIV, {CHIR::ExprKind::DIV, OverflowStrategy::SATURATING}},
        {CHIR::IntrinsicKind::OVERFLOW_WRAPPING_DIV, {CHIR::ExprKind::DIV, OverflowStrategy::WRAPPING}},
        {CHIR::IntrinsicKind::OVERFLOW_CHECKED_DIV, {CHIR::ExprKind::DIV, OverflowStrategy::CHECKED}},
        {CHIR::IntrinsicKind::OVERFLOW_THROWING_MOD, {CHIR::ExprKind::MOD, OverflowStrategy::THROWING}},
        {CHIR::IntrinsicKind::OVERFLOW_SATURATING_MOD, {CHIR::ExprKind::MOD, OverflowStrategy::SATURATING}},
        {CHIR::IntrinsicKind::OVERFLOW_WRAPPING_MOD, {CHIR::ExprKind::MOD, OverflowStrategy::WRAPPING}},
        {CHIR::IntrinsicKind::OVERFLOW_CHECKED_MOD, {CHIR::ExprKind::MOD, OverflowStrategy::CHECKED}},
        {CHIR::IntrinsicKind::OVERFLOW_THROWING_POW, {CHIR::ExprKind::EXP, OverflowStrategy::THROWING}},
        {CHIR::IntrinsicKind::OVERFLOW_SATURATING_POW, {CHIR::ExprKind::EXP, OverflowStrategy::SATURATING}},
        {CHIR::IntrinsicKind::OVERFLOW_WRAPPING_POW, {CHIR::ExprKind::EXP, OverflowStrategy::WRAPPING}},
        {CHIR::IntrinsicKind::OVERFLOW_CHECKED_POW, {CHIR::ExprKind::EXP, OverflowStrategy::CHECKED}},
        {CHIR::IntrinsicKind::OVERFLOW_THROWING_NEG, {CHIR::ExprKind::NEG, OverflowStrategy::THROWING}},
        {CHIR::IntrinsicKind::OVERFLOW_SATURATING_NEG, {CHIR::ExprKind::NEG, OverflowStrategy::SATURATING}},
        {CHIR::IntrinsicKind::OVERFLOW_WRAPPING_NEG, {CHIR::ExprKind::NEG, OverflowStrategy::WRAPPING}},
        {CHIR::IntrinsicKind::OVERFLOW_CHECKED_NEG, {CHIR::ExprKind::NEG, OverflowStrategy::CHECKED}},
        {CHIR::IntrinsicKind::OVERFLOW_THROWING_INC, {CHIR::ExprKind::ADD, OverflowStrategy::THROWING}},
        {CHIR::IntrinsicKind::OVERFLOW_SATURATING_INC, {CHIR::ExprKind::ADD, OverflowStrategy::SATURATING}},
        {CHIR::IntrinsicKind::OVERFLOW_WRAPPING_INC, {CHIR::ExprKind::ADD, OverflowStrategy::WRAPPING}},
        {CHIR::IntrinsicKind::OVERFLOW_CHECKED_INC, {CHIR::ExprKind::ADD, OverflowStrategy::CHECKED}},
        {CHIR::IntrinsicKind::OVERFLOW_THROWING_DEC, {CHIR::ExprKind::SUB, OverflowStrategy::THROWING}},
        {CHIR::IntrinsicKind::OVERFLOW_SATURATING_DEC, {CHIR::ExprKind::SUB, OverflowStrategy::SATURATING}},
        {CHIR::IntrinsicKind::OVERFLOW_WRAPPING_DEC, {CHIR::ExprKind::SUB, OverflowStrategy::WRAPPING}},
        {CHIR::IntrinsicKind::OVERFLOW_CHECKED_DEC, {CHIR::ExprKind::SUB, OverflowStrategy::CHECKED}},
    };
}();

bool IsIncOrDec(const CHIR::IntrinsicKind intrinsicKind)
{
    switch (intrinsicKind) {
        case CHIR::IntrinsicKind::OVERFLOW_THROWING_INC:
        case CHIR::IntrinsicKind::OVERFLOW_SATURATING_INC:
        case CHIR::IntrinsicKind::OVERFLOW_WRAPPING_INC:
        case CHIR::IntrinsicKind::OVERFLOW_CHECKED_INC:
        case CHIR::IntrinsicKind::OVERFLOW_THROWING_DEC:
        case CHIR::IntrinsicKind::OVERFLOW_SATURATING_DEC:
        case CHIR::IntrinsicKind::OVERFLOW_WRAPPING_DEC:
        case CHIR::IntrinsicKind::OVERFLOW_CHECKED_DEC:
            return true;
        default:
            return false;
    }
}
} // namespace

llvm::Value* GenerateOverflowWrappingArithmeticOp(
    IRBuilder2& irBuilder, const CHIR::ExprKind& kind, const CHIR::Type* ty, const std::vector<CGValue*>& argGenValues)
{
    // Overflow func implicit calling reproduce `Neg`, `Inc`, `Dec` Operations.
    if (kind == CHIR::ExprKind::NEG) {
        return HandleNegExpression(irBuilder, argGenValues[0]->GetRawValue());
    }

    // Exp operation specially with fast power accelerating.
    if (kind == CHIR::ExprKind::EXP) {
        return GenerateBinaryExpOperation(irBuilder, argGenValues[0], argGenValues[1]);
    }
    return GenerateArithmeticOperation(irBuilder, kind, ty, argGenValues[0], argGenValues[1]);
}

llvm::Value* GenerateOverflowApply(IRBuilder2& irBuilder, const CHIRIntrinsicWrapper& intrinsic)
{
    const CHIR::IntrinsicKind intrinsicKind = intrinsic.GetIntrinsicKind();
    std::vector<CHIR::Value*> args = intrinsic.GetOperands();
    CJC_ASSERT(!args.empty());
    const CHIR::Type* retType = intrinsic.GetResult()->GetType();
    const CHIR::Type* paramType = args[0]->GetType();
    // There is a possibility of integer overflow when the result of an arithmetic expression is an integer type.
    if (!paramType->IsInteger()) {
#ifndef NDEBUG
        Errorln("The parameter type of the overflow intrinsic function is error.");
#endif
        return nullptr;
    }
    const CHIR::IntType* intTy = StaticCast<const CHIR::IntType*>(paramType);
    auto info = OVERFLOW_FUNC_TO_INFO.at(intrinsicKind);
    auto chirKind = info.first;
    auto strategy = info.second;
    auto& cgMod = irBuilder.GetCGModule();
    std::vector<CGValue*> argGenValues;
    for (auto arg : args) {
        argGenValues.emplace_back(cgMod | arg);
    }
    std::pair<const CHIR::IntType*, const CHIR::Type*> tys = {intTy, nullptr};
    if (strategy == OverflowStrategy::CHECKED) {
        tys = std::make_pair(intTy, retType);
    }
    if (!IsIncOrDec(intrinsicKind)) {
        return GenerateOverflow(irBuilder, strategy, chirKind, tys, argGenValues);
    }
    CGType* type = CGType::GetOrCreate(irBuilder.GetCGModule(), intTy);
    CGValue oneVal = CGValue(llvm::ConstantInt::get(type->GetLLVMType(), 1), type);
    argGenValues.emplace_back(&oneVal);
    return GenerateOverflow(irBuilder, strategy, chirKind, tys, argGenValues);
}

} // namespace CodeGen
} // namespace Cangjie
