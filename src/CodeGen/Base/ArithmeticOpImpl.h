// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_ARITHMETICOP_IMPL_H
#define CANGJIE_ARITHMETICOP_IMPL_H

#include "llvm/IR/Value.h"

#include "cangjie/CHIR/IR/Expression/Terminator.h"

namespace Cangjie {
namespace CodeGen {
class IRBuilder2;
class CGValue;
class CHIRBinaryExprWrapper;
llvm::Value* GenerateArithmeticOperation(IRBuilder2& irBuilder, const CHIRBinaryExprWrapper& binExpr);
llvm::Value* GenerateArithmeticOperation(IRBuilder2& irBuilder, CHIR::ExprKind exprKind, const CHIR::Type* ty,
    const CGValue* valLeft, const CGValue* valRight);
llvm::Value* GenerateBitwiseOperation(IRBuilder2& irBuilder, const CHIRBinaryExprWrapper& binExpr);
llvm::Value* GenerateBinaryExpOperation(IRBuilder2& irBuilder, const CHIRBinaryExprWrapper& binExpr);
llvm::Value* GenerateBinaryExpOperation(IRBuilder2& irBuilder, CGValue* valLeft, CGValue* valRight);
} // namespace CodeGen
} // namespace Cangjie
#endif // CANGJIE_ARITHMETICOP_IMPL_H
