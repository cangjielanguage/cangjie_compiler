// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "Base/ExprDispatcher/ExprDispatcher.h"

#include <cinttypes>

#include "Base/ArithmeticOpImpl.h"
#include "Base/LogicalOpImpl.h"
#include "Base/OverflowDispatcher.h"
#include "CGModule.h"
#include "IRBuilder.h"
#include "cangjie/CHIR/IR/Value/Value.h"

using namespace Cangjie::CHIR;

namespace Cangjie::CodeGen {
llvm::Value* HandleNonOverflowBinaryExpression(IRBuilder2& irBuilder, const CHIRBinaryExprWrapper& chirExpr)
{
    switch (chirExpr.GetBinaryExprKind()) {
        case CHIR::ExprKind::ADD:
        case CHIR::ExprKind::SUB:
        case CHIR::ExprKind::MUL:
        case CHIR::ExprKind::DIV:
        case CHIR::ExprKind::MOD: {
            return GenerateArithmeticOperation(irBuilder, chirExpr);
        }
        case CHIR::ExprKind::EXP: {
            return GenerateBinaryExpOperation(irBuilder, chirExpr);
        }
        case CHIR::ExprKind::LSHIFT:
        case CHIR::ExprKind::RSHIFT:
        case CHIR::ExprKind::BITAND:
        case CHIR::ExprKind::BITOR:
        case CHIR::ExprKind::BITXOR: {
            return GenerateBitwiseOperation(irBuilder, chirExpr);
        }
        case CHIR::ExprKind::LT:
        case CHIR::ExprKind::GT:
        case CHIR::ExprKind::LE:
        case CHIR::ExprKind::GE:
        case CHIR::ExprKind::EQUAL:
        case CHIR::ExprKind::NOTEQUAL: {
            return GenerateBooleanOperation(irBuilder, chirExpr);
        }
        // AND and OR are short circuit operators. They are transformed to simple branches by CHIR
        // thus we are free from generating code for these expression kinds.
        case CHIR::ExprKind::AND:
        case CHIR::ExprKind::OR:
        default: {
            printf("Unexpected expr kind: %" PRIu64 "\n", static_cast<uint64_t>(chirExpr.GetBinaryExprKind()));
            CJC_ASSERT(false);
            return nullptr;
        }
    }
}

llvm::Value* HandleBinaryExpression(IRBuilder2& irBuilder, const CHIRBinaryExprWrapper& chirExpr)
{
    const CHIR::Type* ty = chirExpr.GetResult()->GetType();
    const CHIR::ExprKind& kind = chirExpr.GetBinaryExprKind();
    OverflowStrategy overflowStrategy = chirExpr.GetOverflowStrategy();
    if (!ty) {
        return nullptr;
    }
    if (OPERATOR_KIND_TO_OP_MAP.find(kind) == OPERATOR_KIND_TO_OP_MAP.end()) {
        return HandleNonOverflowBinaryExpression(irBuilder, chirExpr);
    }
    if ((overflowStrategy == OverflowStrategy::NA || overflowStrategy == OverflowStrategy::WRAPPING) &&
        kind != CHIR::ExprKind::DIV && kind != CHIR::ExprKind::MOD) {
        return HandleNonOverflowBinaryExpression(irBuilder, chirExpr);
    }
    // There is a possibility of integer overflow when the result of an arithmetic expression is an integer type.(spec)
    if (!ty->IsInteger()) {
        return HandleNonOverflowBinaryExpression(irBuilder, chirExpr);
    }
    const CHIR::IntType* intTy = StaticCast<const CHIR::IntType*>(ty);
    auto& cgMod = irBuilder.GetCGModule();
    CGValue* valLeft = cgMod | chirExpr.GetLHSOperand();
    CGValue* valRight = cgMod | chirExpr.GetRHSOperand();
    irBuilder.EmitLocation(chirExpr);
    return GenerateOverflow(irBuilder, overflowStrategy, kind, std::make_pair(intTy, nullptr), {valLeft, valRight});
}

} // namespace Cangjie::CodeGen
