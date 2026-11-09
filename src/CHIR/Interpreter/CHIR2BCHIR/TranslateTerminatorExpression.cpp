// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements a translation from CHIR to BCHIR.
 */
#include "cangjie/CHIR/Interpreter/CHIR2BCHIR.h"
#include "cangjie/CHIR/Interpreter/Utils.h"
#include "cangjie/CHIR/IR/Type/StructDef.h"

using namespace Cangjie::CHIR;
using namespace Interpreter;

void CHIR2BCHIR::TranslateTerminatorExpression(Context& ctx, const Expression& expr)
{
    switch (expr.GetExprKind()) {
        case ExprKind::GOTO: {
            CJC_ASSERT(expr.GetNumOfNonSuccessorOperands() == 0);
            auto gotoExpr = StaticCast<const GoTo*>(&expr);
            CJC_ASSERT(gotoExpr->GetNumOfSuccessor() == 1);
            auto dest = gotoExpr->GetSuccessor(0);
            PushOpCodeWithAnnotations(ctx, OpCode::JUMP, expr);
            auto bbIndex = BlockIndex(ctx, *dest, ctx.def.NextIndex());
            ctx.def.Push(bbIndex);
            break;
        }
        case ExprKind::BRANCH: {
            CJC_ASSERT(expr.GetNumOfNonSuccessorOperands() == 1);
            auto branchExpr = StaticCast<const Branch*>(&expr);
            CJC_ASSERT(branchExpr->GetNumOfSuccessor() == Bchir::FLAG_TWO);
            PushOpCodeWithAnnotations(ctx, OpCode::BRANCH, expr);
            auto trueBB = branchExpr->GetSuccessor(0);
            auto bbTrueIndex = BlockIndex(ctx, *trueBB, ctx.def.NextIndex());
            ctx.def.Push(bbTrueIndex);
            auto falseBB = branchExpr->GetSuccessor(1);
            auto bbFalseIndex = BlockIndex(ctx, *falseBB, ctx.def.NextIndex());
            ctx.def.Push(bbFalseIndex);
            break;
        }
        case ExprKind::MULTIBRANCH: {
            auto multi = StaticCast<const MultiBranch*>(&expr);
            TranslateMultiBranch(ctx, *multi);
            break;
        }
        case ExprKind::EXIT: {
            CJC_ASSERT(expr.GetNumOfNonSuccessorOperands() == 0);
            CJC_ASSERT(expr.GetTopLevelFunc() != nullptr);
            auto ret = expr.GetTopLevelFunc()->GetReturnValue();
            if (ret == nullptr) {
                // this function does not have a return var
                PushOpCodeWithAnnotations(ctx, OpCode::UNIT, expr);
            } else {
                // load the return value
                PushOpCodeWithAnnotations(ctx, OpCode::LVAR, expr, LVarId(ctx, *ret));
                PushOpCodeWithAnnotations(ctx, OpCode::DEREF, expr);
            }
            PushOpCodeWithAnnotations(ctx, OpCode::RETURN, expr);
            break;
        }
        case ExprKind::RAISE_EXCEPTION: {
            CJC_ASSERT(expr.GetNumOfNonSuccessorOperands() == 1);
            auto raise = StaticCast<const RaiseException*>(&expr);
            if (raise->GetNumOfSuccessor() == 0) {
                PushOpCodeWithAnnotations(ctx, OpCode::RAISE, expr);
            } else {
                CJC_ASSERT(raise->GetNumOfSuccessor() == 1);
                PushOpCodeWithAnnotations(ctx, OpCode::RAISE_EXC, expr);
                auto exceptionBbIndex = BlockIndex(ctx, *raise->GetSuccessor(0), ctx.def.NextIndex());
                ctx.def.Push(exceptionBbIndex);
            }
            break;
        }
        case ExprKind::TRY_APPLY: {
            // :: APPLY_EXC :: number_of_args :: idx_when_exception :: LVAR_SET :: lvar_id
            // :: JUMP :: idx_when_normal_return
            auto apply = StaticCast<const TryApply*>(&expr);
            TranslateTryApplyExpression(ctx, *apply);
            break;
        }
        case ExprKind::TRY_INVOKE: {
            // :: INVOKE_EXC :: number_of_args :: method_name :: idx_when_exception :: LVAR_SET :: lvar_id
            // :: JUMP :: idx_when_normal_return
            auto invoke = StaticCast<const TryInvoke*>(&expr);
            auto numberArgs = invoke->GetArgs().size();
            CJC_ASSERT(numberArgs > 0);
            CJC_ASSERT(numberArgs <= static_cast<size_t>(Bchir::BYTECODE_CONTENT_MAX));
            auto idx = ctx.def.NextIndex();
            // we dont store mangled name here
            PushOpCodeWithAnnotations<false, true>(
                ctx, OpCode::INVOKE_EXC, expr, static_cast<unsigned>(numberArgs), 0u);
            auto methodName = MangleMethodName<true>(invoke->GetMethodName(), *invoke->GetMethodType());
            ctx.def.AddMangledNameAnnotation(idx, methodName);
            TranslateTryTerminatorJumps(ctx, *invoke);
            break;
        }
        case ExprKind::TRY_INVOKESTATIC: {
            PushOpCodeWithAnnotations(ctx, OpCode::ABORT, expr);
            break;
        }
        case ExprKind::TRY_NEG: {
            auto& tryUnary = StaticCast<const TryUnaryExpression&>(expr);
            TranslateUnaryExpression(ctx, tryUnary);
            TranslateTryTerminatorJumps(ctx, tryUnary);
            break;
        }
        case ExprKind::TRY_ADD:
        case ExprKind::TRY_SUB:
        case ExprKind::TRY_MUL:
        case ExprKind::TRY_DIV:
        case ExprKind::TRY_MOD:
        case ExprKind::TRY_EXP:
        case ExprKind::TRY_LSHIFT:
        case ExprKind::TRY_RSHIFT: {
            auto& tryBinary = StaticCast<const TryBinaryExpression&>(expr);
            TranslateBinaryExpression(ctx, tryBinary);
            TranslateTryTerminatorJumps(ctx, tryBinary);
            break;
        }
        case ExprKind::TRY_ALLOCATE: {
            CJC_ASSERT(expr.GetNumOfNonSuccessorOperands() == 0);
            TranslateAllocate(ctx, StaticCast<const TryAllocate&>(expr));
            TranslateTryTerminatorJumps(ctx, expr);
            break;
        }
        case ExprKind::EXCLAVE: {
            CJC_ASSERT(expr.GetNumOfOperands() == 0);
            TranslateBlockGroup(ctx, *StaticCast<const Exclave&>(expr).GetBody());
            break;
        }
        default: {
            // unreachable
            CJC_ASSERT(false);
            PushOpCodeWithAnnotations(ctx, OpCode::ABORT, expr);
            break;
        }
    }
}

void CHIR2BCHIR::TranslateTryApplyExpression(Context& ctx, const TryApply& apply)
{
    PushOpCodeWithAnnotations<false, true>(
        ctx, OpCode::APPLY_EXC, apply, static_cast<unsigned>(apply.GetNumOfNonSuccessorOperands()));
    TranslateTryTerminatorJumps(ctx, apply);
}

void CHIR2BCHIR::TranslateTryTerminatorJumps(Context& ctx, const Expression& expr)
{
    CJC_ASSERT(expr.GetNumOfSuccessor() == Bchir::FLAG_TWO);
    auto exceptionBB = expr.GetSuccessor(1);
    auto exceptionBbIndex = BlockIndex(ctx, *exceptionBB, ctx.def.NextIndex());
    ctx.def.Push(exceptionBbIndex);

    // a statement "%1 = expr" essentially represents a local var
    PushOpCodeWithAnnotations(ctx, OpCode::LVAR_SET, expr, LVarId(ctx, *expr.GetResult()));

    PushOpCodeWithAnnotations(ctx, OpCode::JUMP, expr);
    auto normalBB = expr.GetSuccessor(0);
    auto normalBbIndex = BlockIndex(ctx, *normalBB, ctx.def.NextIndex());
    ctx.def.Push(normalBbIndex);
}

void CHIR2BCHIR::TranslateMultiBranch(Context& ctx, const MultiBranch& branch)
{
    // Assuming there are no values repeated
    // [| MultiBranch(selector, b0, [c1, b1], ..., [cn, bn] |] =

    // BSEARCH
    // SWITCH :: TYPE :: number_values :: case_1 (8 bytes) :: ... :: case_n (8 bytes) ::
    // default_target :: case_1_target :: ... :: case_n_target

    auto& cases = branch.GetCaseVals();
    auto successors = branch.GetSuccessors();
    auto ty = branch.GetOperand(0)->GetType();
    auto tyKind = ty->IsEnum() ? CHIR::Type::TypeKind::TYPE_UINT64 : ty->GetTypeKind();

    std::vector<std::pair<uint64_t, Block*>> casesToSuccessors;
    for (size_t i = 0; i < cases.size(); ++i) {
        casesToSuccessors.emplace_back(cases[i], successors[i + 1]);
    }

    std::sort(casesToSuccessors.begin(), casesToSuccessors.end(),
        [](auto& left, auto& right) { return left.first < right.first; });

    PushOpCodeWithAnnotations(ctx, OpCode::SWITCH, branch, static_cast<Bchir::ByteCodeContent>(tyKind),
        static_cast<Bchir::ByteCodeContent>(cases.size()));

    for (auto it = casesToSuccessors.begin(); it != casesToSuccessors.end(); ++it) {
        // These are sorted
        ctx.def.Push8bytes(it->first);
    }

    auto defaultBB = BlockIndex(ctx, *successors[0], ctx.def.NextIndex());
    ctx.def.Push(defaultBB);
    for (auto it = casesToSuccessors.begin(); it != casesToSuccessors.end(); ++it) {
        // These are sorted
        auto bbIndex = BlockIndex(ctx, *it->second, ctx.def.NextIndex());
        ctx.def.Push(bbIndex);
    }
}
