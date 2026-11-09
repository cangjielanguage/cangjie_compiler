// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/AST2CHIR/TranslateASTNode/Translator.h"

using namespace Cangjie::CHIR;
using namespace Cangjie;

Ptr<Value> Translator::Visit(const AST::ExclaveExpr& exclaveExpr)
{
    // 1. Create exclave terminator and body.
    auto subGroup = builder.CreateBlockGroup(*currentBlock->GetTopLevelFunc());
    auto exclave = CreateAndAppendTerminator<Exclave>(currentBlock);
    exclave->InitBody(*subGroup);
    auto loc = TranslateLocation(exclaveExpr);
    exclave->SetDebugLocation(loc);

    // 2. Translate body.
    blockGroupStack.emplace_back(subGroup);
    auto entry = builder.CreateBlock(subGroup);
    subGroup->SetEntryBlock(entry);
    const auto& body = exclaveExpr.body;
    CJC_NULLPTR_CHECK(body);
    auto retValue = Visit(*body);
    CreateAndAppendTerminator<GoTo>(GetBlockByAST(*body), entry);

    // 3. Yield body value as return of the enclosing func/lambda, then Exit.
    if (retValue != nullptr) {
        if (auto ret = GetReturnValueFromBlockGroup(*blockGroupStack.back())) {
            CreateAndAppendWrappedStore(*retValue, *ret, loc);
        }
    }
    auto exit = CreateAndAppendTerminator<Exit>(loc, currentBlock);
    // Body already returned via the store above; this Exit is structural (like a
    // compiler-added return wrapping exclave as the function's last expression).
    exit->Set<SkipCheck>(SkipKind::SKIP_DCE_WARNING);

    // 4. Restore translation context to an unreachable outer block so subsequent
    // statements are not emitted inside the Exclave body.
    blockGroupStack.pop_back();
    currentBlock = CreateBlock();
    return nullptr;
}
