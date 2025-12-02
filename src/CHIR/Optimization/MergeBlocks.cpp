// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Optimization/MergeBlocks.h"
#include "cangjie/CHIR/IR/Annotation.h"
#include "cangjie/CHIR/Utils/Utils.h"
#include "cangjie/CHIR/Utils/CHIRCasting.h"

using namespace Cangjie;
using namespace Cangjie::CHIR;

namespace {
SkipKind operator&(SkipKind l, SkipKind r)
{
    if (l != SkipKind::NO_SKIP) {
        return l;
    }
    return r;
}
SkipKind& operator&=(SkipKind& l, SkipKind r)
{
    l = l & r;
    return l;
}

// callee should have checked that there is only a single way GoTo between this block and its predecessor
void MergeIntoPredecessor(Block& block)
{
    // 1) remove the terminator of predecessor
    auto pred = block.GetPredecessors()[0];
    CJC_NULLPTR_CHECK(pred->GetTerminator());
    SkipKind appendSkipCheck{pred->Get<SkipCheck>() & pred->GetTerminator()->Get<SkipCheck>()};
    pred->GetTerminator()->RemoveSelfFromBlock();

    // 2) add the expressions in `block` to predecessor
    for (auto e : block.GetExpressions()) {
        e->MoveTo(*pred);
    }

    // 3) remove `block` itself since it's not a reachable one
    appendSkipCheck &= block.Get<SkipCheck>();
    block.RemoveSelfFromBlockGroup();
    if (appendSkipCheck != SkipKind::NO_SKIP) {
        pred->Set<SkipCheck>(appendSkipCheck);
    }
}

void MergeGotoOnlyBlock(Block& block)
{
    auto preds = block.GetPredecessors();
    auto target = block.GetSuccessors()[0];

    for (auto pred : preds) {
        auto terminator = pred->GetTerminator();
        CJC_NULLPTR_CHECK(terminator);
        target->SetDebugLocation(block.GetDebugLocation());
        terminator->ReplaceSuccessor(block, *target);
        // skip check of block.terminator is not considered, because it is a simple GoTo and is unconditionally removed
        if (auto skip = terminator->Get<SkipCheck>() & block.Get<SkipCheck>(); skip != SkipKind::NO_SKIP) {
            terminator->Set<SkipCheck>(skip);
        }
    }
    
    block.RemoveSelfFromBlockGroup();
}

/// A simple Clone call to a block is not correct to copy it, because it may contains self reference
/// e.g.
/// #1
///   %1 = Constant(1)
///   %2 = Add(%1, %1)
/// after a Clone call, we get
/// #2
///   %3 = Constant(1)
///   %4 = Add(%1, %1) --- wrong! should be Add(%3, %3)
/// It is also possible that successors of #1 has usage of %1, in this case the generated CHIR is still wrong.
/// Just in this usage, such error should not present.
/// This operation has O(n^2) complexity.
Block* CopyBlock(CHIRBuilder& builder, Block& block)
{
    auto bg = block.GetParentBlockGroup();
    auto ret = builder.CreateBlock(bg);
    std::vector<std::pair<Value*, Value*>> replaceTable{};
    auto exprs = block.GetExpressions();
    for (size_t i{0}; i < exprs.size(); ++i) {
        auto expr = exprs[i];
        auto cloned = expr->Clone(builder, *ret);
        for (auto& pair : replaceTable) {
            cloned->ReplaceOperand(pair.first, pair.second);
        }
        if (i + 1 != exprs.size()) {
            replaceTable.emplace_back(expr->GetResult(), cloned->GetResult());
        }
    }
    return ret;
}

void MergeForInCondBlock(CHIRBuilder& builder, Block& block)
{
    auto oldPreds = block.GetPredecessors();
    auto oldSuccs = block.GetSuccessors();
    CJC_ASSERT(!oldPreds.empty());
    for (size_t i{0}; i < oldPreds.size() - 1; ++i) {
        auto cpBlock = CopyBlock(builder, block);
        auto pred = oldPreds[i];
        pred->GetTerminator()->ReplaceSuccessor(block, *cpBlock);
    }
    block.Remove<GeneratedFromForIn>();
}
} // namespace

void MergeBlocks::RunOnPackage(const Package& package, CHIRBuilder& builder, const GlobalOptions& opts)
{
    for (auto func : package.GetGlobalFuncs()) {
        bool isCommonFunctionWithoutBody = func->TestAttr(Attribute::SKIP_ANALYSIS);
        if (isCommonFunctionWithoutBody) {
            continue; // Nothing to visit
        }
        RunOnFunc(*func->GetBody(), builder, opts);
    }
}

static bool SkipMergeBlock(const Block& bl, const GlobalOptions& opts)
{
    if (bl.TestAttr(Attribute::UNREACHABLE)) {
        return true;
    }
    if (!opts.enableCompileDebug) {
        return false;
    }
    auto term = bl.GetTerminator();
    return term && !term->GetDebugLocation().IsInvalidPos();
}

void MergeBlocks::RunOnFunc(const BlockGroup& body, CHIRBuilder& builder, const GlobalOptions& opts)
{
    auto checkSingleEntrySingleExit = [](const Block& block, const GlobalOptions& opts) {
        if (SkipMergeBlock(block, opts)) {
            return false;
        }
        auto preds = block.GetPredecessors();
        if (preds.size() != 1) {
            return false;
        }
        auto succs = preds[0]->GetSuccessors();
        if (succs.size() != 1) {
            return false;
        }
        // The terminator of pred must be GoTo
        auto term = preds[0]->GetTerminator();
        if (term == nullptr || term->GetExprKind() != ExprKind::GOTO) {
            return false;
        }
        if (opts.enableCompileDebug || opts.enableCoverage) {
            auto [line, column] = term->GetDebugLocation().GetBeginPos();
            return line == 0 && column == 0;
        }
        return true;
    };
    auto checkGotoOnly = [](const Block& block, const GlobalOptions& opts) {
        if (block.GetExpressions().size() != 1 || block.IsEntry() || SkipMergeBlock(block, opts)) {
            return false;
        }
        auto term = block.GetTerminator();
        if (term == nullptr || term->GetExprKind() != ExprKind::GOTO) {
            return false;
        }
        if (auto preds = block.GetPredecessors(); std::find(preds.begin(), preds.end(), &block) != preds.end()) {
            /* The following chir cannot be merged.
             *
             *  Block #1: // preds:
             *    ...
             *    Goto(#2)
             *
             *  Block #2: // preds: #1, #2
             *    Goto(#2)
             */
            return false;
        }
        if (opts.enableCompileDebug || opts.enableCoverage) {
            auto [line, column] = term->GetDebugLocation().GetBeginPos();
            return line == 0 && column == 0;
        }
        return true;
    };
    bool isStable;
    do {
        isStable = true;
        for (auto block : body.GetBlocks()) {
            for (auto expr : block->GetExpressions()) {
                if (expr->GetExprKind() == ExprKind::LAMBDA) {
                    RunOnFunc(*StaticCast<const Lambda*>(expr)->GetBody(), builder, opts);
                }
            }
            if (block->Get<GeneratedFromForIn>()) {
                isStable = false;
                MergeForInCondBlock(builder, *block);
            } else if (checkSingleEntrySingleExit(*block, opts)) {
                isStable = false;
                MergeIntoPredecessor(*block);
            } else if (checkGotoOnly(*block, opts)) {
                isStable = false;
                MergeGotoOnlyBlock(*block);
            }
        }
    } while (!isStable);
}
