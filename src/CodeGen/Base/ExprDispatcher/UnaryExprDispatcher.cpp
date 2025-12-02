// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "Base/ExprDispatcher/ExprDispatcher.h"

#include <cinttypes>

#include "Base/CHIRExprWrapper.h"
#include "Base/OverflowDispatcher.h"
#include "IRBuilder.h"
#include "cangjie/CHIR/IR/Value/Value.h"

namespace Cangjie::CodeGen {
llvm::Value* HandleNegExpression(IRBuilder2& irBuilder, llvm::Value* value)
{
    if (value->getType()->isFloatingPointTy()) {
        return irBuilder.CreateFNeg(value, "FNeg");
    } else {
        return irBuilder.CreateNeg(value, "Neg");
    }
}

llvm::Value* HandleNonOverflowUnaryExpression(IRBuilder2& irBuilder, const CHIRUnaryExprWrapper& chirExpr)
{
    auto value = **(irBuilder.GetCGModule() | chirExpr.GetOperand());
    switch (chirExpr.GetUnaryExprKind()) {
        case CHIR::ExprKind::NEG: {
            return HandleNegExpression(irBuilder, value);
        }
        case CHIR::ExprKind::NOT: {
            return irBuilder.CreateXor(value, 1, "not");
        }
        case CHIR::ExprKind::BITNOT: {
            return irBuilder.CreateNot(value, "bitNot");
        }
        default: {
            printf("Unexpected expr kind: %" PRIu64 "\n", static_cast<uint64_t>(chirExpr.GetUnaryExprKind()));
            CJC_ASSERT(false);
            return nullptr;
        }
    }
}

llvm::Value* HandleUnaryExpression(IRBuilder2& irBuilder, const CHIRUnaryExprWrapper& chirExpr)
{
    OverflowStrategy overflowStrategy = chirExpr.GetOverflowStrategy();
    const CHIR::ExprKind& kind = chirExpr.GetUnaryExprKind();
    if (OPERATOR_KIND_TO_OP_MAP.find(kind) == OPERATOR_KIND_TO_OP_MAP.end() ||
        overflowStrategy == OverflowStrategy::NA) {
        return HandleNonOverflowUnaryExpression(irBuilder, chirExpr);
    }

    const CHIR::Type* ty = chirExpr.GetResult()->GetType();
    // There is a possibility of integer overflow when the result of an arithmetic expression is an integer type.(spec)
    if (overflowStrategy == OverflowStrategy::WRAPPING || !ty->IsInteger()) {
        return HandleNonOverflowUnaryExpression(irBuilder, chirExpr);
    }
    const CHIR::IntType* intTy = StaticCast<const CHIR::IntType*>(ty);
    auto& cgMod = irBuilder.GetCGModule();
    auto cgValue = cgMod | chirExpr.GetOperand();
    irBuilder.EmitLocation(chirExpr);

    return GenerateOverflow(irBuilder, overflowStrategy, kind, std::make_pair(intTy, nullptr), {cgValue});
}
} // namespace Cangjie::CodeGen
