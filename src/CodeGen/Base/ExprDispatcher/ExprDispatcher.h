// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_EXPRDISPATCHER_H
#define CANGJIE_EXPRDISPATCHER_H

#include "llvm/IR/Value.h"

#include "cangjie/CHIR/IR/Expression/Terminator.h"

namespace Cangjie {
namespace CodeGen {
class IRBuilder2;
class CGValue;
class CHIRUnaryExprWrapper;
class CHIRBinaryExprWrapper;

llvm::Value* HandleConstantExpression(IRBuilder2& irBuilder, const CHIR::Constant& chirConst);
llvm::Value* HandleLiteralValue(IRBuilder2& irBuilder, const CHIR::LiteralValue& chirLiteral);
llvm::Value* HandleTerminatorExpression(IRBuilder2& irBuilder, const CHIR::Expression& chirExpr);
llvm::Value* HandleNegExpression(IRBuilder2& irBuilder, llvm::Value* value);
llvm::Value* HandleUnaryExpression(IRBuilder2& irBuilder, const CHIRUnaryExprWrapper& chirExpr);
llvm::Value* HandleBinaryExpression(IRBuilder2& irBuilder, const CHIRBinaryExprWrapper& chirExpr);
llvm::Value* HandleMemoryExpression(IRBuilder2& irBuilder, const CHIR::Expression& chirExpr);
llvm::Value* HandleStructedControlFlowExpression(IRBuilder2& irBuilder, const CHIR::Expression& chirExpr);
llvm::Value* HandleOthersExpression(IRBuilder2& irBuilder, const CHIR::Expression& chirExpr);
} // namespace CodeGen
} // namespace Cangjie
#endif // CANGJIE_EXPRDISPATCHER_H
