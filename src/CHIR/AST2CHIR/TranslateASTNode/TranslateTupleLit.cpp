// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/AST2CHIR/TranslateASTNode/Translator.h"

using namespace Cangjie::CHIR;
using namespace Cangjie;

Ptr<Value> Translator::Visit(const AST::TupleLit& tuple)
{
    std::vector<Value*> argVals;
    auto tupleType = StaticCast<AST::TupleTy*>(tuple.DataTy());
    for (size_t i = 0; i < tuple.children.size(); i++) {
        auto arg = TranslateExprArg(*tuple.children[i], *TranslateType(tupleType->typeArgs[i]));
        argVals.push_back(arg);
    }
    auto ty = TranslateType(tuple.GetTy());
    return CreateAndAppendExpression<Tuple>(TranslateLocation(tuple), ty, argVals, currentBlock)
        ->GetResult();
}