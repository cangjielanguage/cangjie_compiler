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
#include "cangjie/CHIR/Interpreter/Utils.h"

using namespace Cangjie::CHIR;
using namespace Interpreter;

void CHIR2BCHIR::TranslateUnaryExpression(Context& ctx, const Expression& expr)
{
    CJC_ASSERT(expr.GetNumOfOperands() == Bchir::FLAG_ONE);
    auto opCode = Cangjie::CHIR::Interpreter::UnExprKind2OpCode(expr.GetExprKind());
    auto unaryExpression = StaticCast<const UnaryExpression*>(&expr);
    auto typeKind = expr.GetResult()->GetType()->GetTypeKind();
    auto overflow = static_cast<Bchir::ByteCodeContent>(unaryExpression->GetOverflowStrategy());
    PushOpCodeWithAnnotations<false, true>(ctx, opCode, expr, typeKind, overflow);
}