// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "Base/LogicalOpImpl.h"

#include <functional>
#include <map>

#include "Base/CHIRExprWrapper.h"
#include "CGModule.h"
#include "IRBuilder.h"
#include "cangjie/CHIR/IR/Expression/Terminator.h"
#include "cangjie/CHIR/IR/Value/Value.h"

namespace Cangjie {
namespace CodeGen {
llvm::Value* GenerateBooleanOperation(IRBuilder2& irBuilder, const CHIRBinaryExprWrapper& binOp)
{
    using GenerateFunc = std::function<llvm::Value*(llvm::IRBuilder<>&, llvm::Value*, llvm::Value*)>;
    using OperatorKind = CHIR::ExprKind;
    using namespace std::placeholders;

    static const std::map<OperatorKind, GenerateFunc> mapForFloat = {
        {OperatorKind::LT, std::bind(&IRBuilder2::CreateFCmpOLT, _1, _2, _3, "fcmpolt", nullptr)},
        {OperatorKind::GT, std::bind(&IRBuilder2::CreateFCmpOGT, _1, _2, _3, "fcmpogt", nullptr)},
        {OperatorKind::LE, std::bind(&IRBuilder2::CreateFCmpOLE, _1, _2, _3, "fcmpole", nullptr)},
        {OperatorKind::GE, std::bind(&IRBuilder2::CreateFCmpOGE, _1, _2, _3, "fcmpoge", nullptr)},
        {OperatorKind::EQUAL, std::bind(&IRBuilder2::CreateFCmpOEQ, _1, _2, _3, "fcmpoeq", nullptr)},
        {OperatorKind::NOTEQUAL, std::bind(&IRBuilder2::CreateFCmpUNE, _1, _2, _3, "fcmpune", nullptr)},
    };
    static const std::map<OperatorKind, GenerateFunc> mapForUnsigned = {
        {OperatorKind::LT, std::bind(&IRBuilder2::CreateICmpULT, _1, _2, _3, "icmpult")},
        {OperatorKind::GT, std::bind(&IRBuilder2::CreateICmpUGT, _1, _2, _3, "icmpugt")},
        {OperatorKind::LE, std::bind(&IRBuilder2::CreateICmpULE, _1, _2, _3, "icmpule")},
        {OperatorKind::GE, std::bind(&IRBuilder2::CreateICmpUGE, _1, _2, _3, "icmpuge")},
        {OperatorKind::EQUAL, std::bind(&IRBuilder2::CreateICmpEQ, _1, _2, _3, "icmpeq")},
        {OperatorKind::NOTEQUAL, std::bind(&IRBuilder2::CreateICmpNE, _1, _2, _3, "icmpne")},
    };
    static const std::map<OperatorKind, GenerateFunc> mapForOthers = {
        {OperatorKind::LT, std::bind(&IRBuilder2::CreateICmpSLT, _1, _2, _3, "icmpslt")},
        {OperatorKind::GT, std::bind(&IRBuilder2::CreateICmpSGT, _1, _2, _3, "icmpsgt")},
        {OperatorKind::LE, std::bind(&IRBuilder2::CreateICmpSLE, _1, _2, _3, "icmpsle")},
        {OperatorKind::GE, std::bind(&IRBuilder2::CreateICmpSGE, _1, _2, _3, "icmpsge")},
        {OperatorKind::EQUAL, std::bind(&IRBuilder2::CreateICmpEQ, _1, _2, _3, "icmpeq")},
        {OperatorKind::NOTEQUAL, std::bind(&IRBuilder2::CreateICmpNE, _1, _2, _3, "icmpne")},
    };

    auto& cgMod = irBuilder.GetCGModule();
    auto leftArg = binOp.GetLHSOperand();
    auto leftArgTypeInfo = leftArg->GetType();
    llvm::Value* valLeft = **(cgMod | leftArg);
    llvm::Value* valRight = **(cgMod | binOp.GetRHSOperand());
    CJC_NULLPTR_CHECK(valLeft);
    CJC_NULLPTR_CHECK(valRight);

    if (valLeft->getType()->isIntegerTy() && valRight->getType()->isIntegerTy() &&
        valLeft->getType()->getIntegerBitWidth() != valRight->getType()->getIntegerBitWidth()) {
        valLeft = irBuilder.CreateZExtOrTrunc(valLeft, valRight->getType());
    }

    // special case
    if ((leftArgTypeInfo->IsUnit() || leftArgTypeInfo->IsNothing()) &&
        (binOp.GetBinaryExprKind() == OperatorKind::EQUAL || binOp.GetBinaryExprKind() == OperatorKind::NOTEQUAL)) {
        return binOp.GetBinaryExprKind() == OperatorKind::EQUAL ? irBuilder.getTrue() : irBuilder.getFalse();
    }

    std::map<OperatorKind, GenerateFunc> currentMap;
    if (leftArgTypeInfo->IsFloat()) {
        currentMap = mapForFloat;
    } else if (leftArgTypeInfo->IsInteger() && !StaticCast<CHIR::IntType*>(leftArgTypeInfo)->IsSigned()) {
        currentMap = mapForUnsigned;
    } else {
        currentMap = mapForOthers;
    }
    auto iter = std::as_const(currentMap).find(binOp.GetBinaryExprKind());
    return iter->second(irBuilder, valLeft, valRight);
}
} // namespace CodeGen
} // namespace Cangjie
