// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/AST2CHIR/TranslateASTNode/Translator.h"

using namespace Cangjie::CHIR;
using namespace Cangjie;

Ptr<Value> Translator::Visit(const AST::TypeConvExpr& typeConvExpr)
{
    const auto& loc = TranslateLocation(typeConvExpr);
    auto chirType = TranslateType(typeConvExpr.GetTy());
    auto operand = TranslateExprArg(*typeConvExpr.expr);

    auto srcTy = typeConvExpr.expr->GetTy();
    auto targetTy = typeConvExpr.GetTy().get();
    if (srcTy->IsFunc() || srcTy->IsTuple()) {
        return CreateWrappedTypeCast(loc, chirType, operand, currentBlock)->GetResult();
    }
    auto ofs = typeConvExpr.overflowStrategy;
    auto noException = targetTy->IsInteger() && ofs != OverflowStrategy::THROWING;
    auto opLoc = TranslateLocation(*typeConvExpr.expr);
    auto newNode = TryCreateCastWithOV(currentBlock, !noException, ofs, loc, chirType, operand);
    return newNode->GetResult();
}
