// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Checker/UnreachableBranchCheck.h"

using namespace Cangjie::CHIR;

UnreachableBranchCheck::UnreachableBranchCheck(
    ConstAnalysisWrapper* constAnalysisWrapper, DiagAdapter& diag, const std::string& packageName)
    : diag(diag), analysisWrapper(constAnalysisWrapper), currentPackageName(packageName)
{
}

namespace {
std::string GetKeyWordBySourceExpr(const Branch& branch)
{
    if (branch.GetSourceExpr() == SourceExpr::IF_EXPR) {
        return "if";
    } else if (branch.GetSourceExpr() == SourceExpr::WHILE_EXPR) {
        return "while";
    } else if (branch.GetSourceExpr() == SourceExpr::FOR_IN_EXPR) {
        return "for";
    }
    return "";
}
} // namespace
void UnreachableBranchCheck::RunOnPackage(const Package& package, size_t threadNum)
{
    if (threadNum == 1) {
        for (auto func : package.GetGlobalFuncs()) {
            /* The following code should not report warning.
            interface I {
                func test() : Bool {
                match(this) {
                    case v: ?Int64 => false
                    case v: Int64 => true
                    case _ => false
                }}}
            */
            if (func->Get<SkipCheck>() == SkipKind::SKIP_DCE_WARNING) {
                continue;
            }
            RunOnFunc(func);
        }
    } else {
        Utils::TaskQueue taskQueue(threadNum);
        // Check in generic decl is not currently supported, as constant analysis does not yet support.
        for (auto func : package.GetGlobalFuncs()) {
            if (func->Get<SkipCheck>() == SkipKind::SKIP_DCE_WARNING) {
                continue;
            }
            taskQueue.AddTask<void>([this, func]() { return RunOnFunc(func); });
        }
        taskQueue.RunAndWaitForAllTasksCompleted();
    }
}

void UnreachableBranchCheck::PrintWarning(
    const Terminator& node, Block& block, std::set<Block*>& hasProcessed, bool isRecursive)
{
    if (hasProcessed.find(&block) != hasProcessed.end()) {
        return;
    }
    hasProcessed.emplace(&block);
    // Do-while expr do not need to check unreachable branch.
    if (block.Get<SkipCheck>() == SkipKind::SKIP_DCE_WARNING || node.Get<SkipCheck>() == SkipKind::SKIP_DCE_WARNING) {
        return;
    }
    CJC_ASSERT(block.GetTerminator());
    if (block.GetTerminator()->GetExprKind() == ExprKind::BRANCH ||
        block.GetTerminator()->GetExprKind() == ExprKind::MULTIBRANCH) {
        auto branchNode = StaticCast<Terminator*>(block.GetTerminator());
        bool rec{true};
        if (auto br = DynamicCast<Branch>(branchNode)) {
            if (br->GetSourceExpr() == SourceExpr::FOR_IN_EXPR) {
                rec = false;
            }
        }
        if (rec) {
            for (auto successor : branchNode->GetSuccessors()) {
                PrintWarning(*branchNode, *successor, hasProcessed, true);
            }
        }
    }
    auto [res, range] = ToRangeIfNotZero(block.GetDebugLocation());
    if (!res) {
        return;
    }
    if (IsCrossPackage(range.begin, currentPackageName, diag)) {
        return;
    }
    if (node.GetExprKind() == ExprKind::BRANCH) {
        auto branch = StaticCast<const Branch*>(&node);
        if (branch->GetSourceExpr() == SourceExpr::QUEST || branch->GetSourceExpr() == SourceExpr::BINARY) {
            if (!isRecursive) {
                (void)diag.DiagnoseRefactor(DiagKindRefactor::chir_dce_unreachable_expression, range);
            }
        } else if (branch->GetSourceExpr() == SourceExpr::MATCH_EXPR) {
            (void)diag.DiagnoseRefactor(DiagKindRefactor::chir_unreachable_pattern, range);
        } else {
            auto keyWord = GetKeyWordBySourceExpr(*branch);
            auto builder =
                diag.DiagnoseRefactor(DiagKindRefactor::chir_dce_unreachable_block_in_expression, range, keyWord);
            builder.AddMainHintArguments(keyWord);
        }
    } else {
        (void)diag.DiagnoseRefactor(DiagKindRefactor::chir_unreachable_pattern, range);
    }
}

void UnreachableBranchCheck::RunOnFunc(const Ptr<Func> func)
{
    // we should check the generic func, not the instantiated func.
    if (func->TestAttr(Attribute::GENERIC_INSTANTIATED)) {
        return;
    }
    bool isCommonFunctionWithoutBody = func->TestAttr(Attribute::SKIP_ANALYSIS);
    if (isCommonFunctionWithoutBody) {
        return; // Nothing to visit
    }
    auto result = analysisWrapper->CheckFuncResult(func);
    CJC_ASSERT(result != std::nullopt);
    if (result == nullptr) {
        return;
    }

    const auto actionBeforeVisitExpr = [](const ConstDomain&, Expression*, size_t) {};
    const auto actionAfterVisitExpr = [](const ConstDomain&, Expression*, size_t) {};

    const auto actionOnTerminator = [this](
                                        const ConstDomain&, Terminator* terminator, std::optional<Block*> targetSucc) {
        switch (terminator->GetExprKind()) {
            case ExprKind::BRANCH: {
                if (targetSucc.has_value()) {
                    auto branchNode = StaticCast<Branch*>(terminator);
                    auto successors = terminator->GetSuccessors();
                    std::set<Block*> hasProcessed;
                    for (auto successor : successors) {
                        if (successor == targetSucc.value()) {
                            continue;
                        }
                        PrintWarning(*branchNode, *successor, hasProcessed);
                    }
                }
                break;
            }
            case ExprKind::MULTIBRANCH: {
                if (targetSucc.has_value()) {
                    auto multiBranchNode = StaticCast<MultiBranch*>(terminator);
                    auto successors = terminator->GetSuccessors();
                    std::set<Block*> hasProcessed;
                    for (auto successor : successors) {
                        if (successor == targetSucc.value()) {
                            continue;
                        }
                        PrintWarning(*multiBranchNode, *successor, hasProcessed);
                    }
                }
                break;
            }
            default:
                break;
        }
    };

    result.value()->VisitWith(actionBeforeVisitExpr, actionAfterVisitExpr, actionOnTerminator);
}
