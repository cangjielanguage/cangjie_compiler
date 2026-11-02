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
    // Expected element types must follow GetFieldOfType (inherit the tuple's modal).
    // Using bare TranslateType(typeArgs[i]) forces ClassStaticCast that drops @local!/@local?
    // on non-copyable payloads (e.g. Array / RawArray&), which CHIRChecker rejects.
    auto ty = TranslateType(tuple.GetTy());
    for (size_t i = 0; i < tuple.children.size(); i++) {
        auto elemTy = GetFieldOfType(*ty, static_cast<uint64_t>(i), builder);
        CJC_NULLPTR_CHECK(elemTy);
        auto arg = TranslateExprArg(*tuple.children[i], *elemTy);
        argVals.push_back(arg);
    }
    return CreateAndAppendExpression<Tuple>(TranslateLocation(tuple), ty, argVals, currentBlock)
        ->GetResult();
}