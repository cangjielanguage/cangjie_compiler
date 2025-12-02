// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements a translation from CHIR to BCHIR.
 */
#include "cangjie/CHIR/Interpreter/CHIR2BCHIR.h"
#include "cangjie/CHIR/IR/Type/ClassDef.h"
#include "cangjie/CHIR/IR/Type/StructDef.h"

using namespace Cangjie::CHIR;
using namespace Interpreter;

void CHIR2BCHIR::TranslateMemoryExpression(Context& ctx, const Expression& expr)
{
    switch (expr.GetExprKind()) {
        case ExprKind::ALLOCATE: {
            CJC_ASSERT(expr.GetNumOfOperands() == 0U);
            TranslateAllocate(ctx, expr);
            break;
        }
        case ExprKind::LOAD: {
            CJC_ASSERT(expr.GetNumOfOperands() == 1U);
            PushOpCodeWithAnnotations(ctx, OpCode::DEREF, expr);
            break;
        }
        case ExprKind::STORE: {
            CJC_ASSERT(expr.GetNumOfOperands() == Bchir::FLAG_TWO);
            PushOpCodeWithAnnotations(ctx, OpCode::ASG, expr);
            break;
        }
        case ExprKind::GET_ELEMENT_REF: {
            CJC_ASSERT(expr.GetNumOfOperands() == 1U);
            auto getElementRefExpr = StaticCast<const GetElementRef*>(&expr);
            PushOpCodeWithAnnotations(
                ctx, OpCode::GETREF, expr, static_cast<unsigned>(getElementRefExpr->GetPath().size()));
            for (auto i : getElementRefExpr->GetPath()) {
                CJC_ASSERT(i <= Bchir::BYTECODE_CONTENT_MAX);
                ctx.def.Push(static_cast<Bchir::ByteCodeContent>(i));
            }
            break;
        }
        case ExprKind::STORE_ELEMENT_REF: {
            CJC_ASSERT(expr.GetNumOfOperands() == 2U);
            auto storeElementRefExpr = StaticCast<const StoreElementRef*>(&expr);
            PushOpCodeWithAnnotations(
                ctx, OpCode::STOREINREF, expr, static_cast<unsigned>(storeElementRefExpr->GetPath().size()));
            for (auto i : storeElementRefExpr->GetPath()) {
                CJC_ASSERT(i <= Bchir::BYTECODE_CONTENT_MAX);
                ctx.def.Push(static_cast<Bchir::ByteCodeContent>(i));
            }
            break;
        }
        default: {
            // unreachable
            CJC_ASSERT(false);
            PushOpCodeWithAnnotations(ctx, OpCode::ABORT, expr);
        }
    }
}
