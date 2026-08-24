// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "Desugar/AfterTypeCheck.h"

#include "cangjie/AST/Create.h"
#include "cangjie/AST/Utils.h"

using namespace Cangjie;
using namespace AST;

namespace {
/**
 * *************** before desugar ****************
 * e1 |> e2
 * *************** after desugar  ****************
 * e2(e1)
 * *************** after blockify ****************
 * {
 *     let v = e1
 *     e2(v)
 * }
 */
void BlockifyFlowExpr(BinaryExpr& be)
{
    CJC_ASSERT(be.op == TokenKind::PIPELINE && be.desugarExpr);
    // Get the inner most `desugarExpr`.
    Ptr<CallExpr> innerCe = StaticCast<CallExpr*>(be.desugarExpr.get());
    while (innerCe->desugarExpr) {
        innerCe = StaticCast<CallExpr*>(innerCe->desugarExpr.get());
    }
    CJC_ASSERT(innerCe->args.size() == 1 && innerCe->args.front() != nullptr);
    // Create `let v = e1`.
    auto vd = CreateVarDecl(V_COMPILER, std::move(innerCe->args.front()->expr));
    vd->fullPackageName = be.GetFullPackageName();
    CopyBasicInfo(vd->initializer.get(), vd.get());
    // Create the reference `v` and replace the argument with this `RefExpr`.
    auto re = CreateRefExpr(*vd);
    CopyBasicInfo(vd->initializer.get(), re.get());
    innerCe->args.front()->expr = std::move(re);
    // Create the block.
    std::vector<OwnedPtr<Node>> nodes;
    nodes.emplace_back(std::move(vd));
    nodes.emplace_back(std::move(be.desugarExpr));
    auto block = CreateBlock(std::move(nodes), be.GetTy());
    CopyBasicInfo(&be, block.get());
    AddCurFile(*block, be.curFile);
    be.desugarExpr = std::move(block);
}
} // namespace

namespace Cangjie::Sema::Desugar::AfterTypeCheck {
void DesugarBinaryExpr(BinaryExpr& be)
{
    if (!be.GetTy().IsCorrect()) {
        return;
    }
    if (be.op == TokenKind::PIPELINE) {
        BlockifyFlowExpr(be);
    }
}
} // namespace Cangjie::Sema::Desugar::AfterTypeCheck
