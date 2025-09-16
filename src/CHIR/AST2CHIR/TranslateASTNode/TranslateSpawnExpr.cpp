// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/AST2CHIR/TranslateASTNode/Translator.h"

using namespace Cangjie::CHIR;
using namespace Cangjie;

Ptr<Value> Translator::Visit(const AST::SpawnExpr& spawnExpr)
{
    auto loc = TranslateLocation(spawnExpr);
    // VarDecl of 'futureObj' can be ignored, only generate initializer here.
    auto futureObj = TranslateExprArg(*spawnExpr.futureObj->initializer);

    Value* spawnArg = nullptr;
    if (spawnExpr.arg && spawnExpr.arg->desugarExpr) {
        spawnArg = TranslateExprArg(*spawnExpr.arg->desugarExpr.get());
    }

    if (spawnArg) {
        TryCreate<Spawn>(currentBlock, loc, chirTy.TranslateType(*spawnExpr.ty), futureObj, spawnArg);
    } else {
        TryCreate<Spawn>(currentBlock, loc, chirTy.TranslateType(*spawnExpr.ty), futureObj);
    }
    return futureObj;
}
