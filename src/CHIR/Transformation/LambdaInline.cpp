// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Transformation/LambdaInline.h"

namespace Cangjie::CHIR {

namespace {
std::vector<Expression*> GetNonDebugUsers(const Value& val)
{
    std::vector<Expression*> res;
    for (auto expr: val.GetUsers()) {
        if (expr->GetExprKind() != CHIR::ExprKind::DEBUGEXPR) {
            res.emplace_back(expr);
        }
    }
    return res;
}
}  // namespace

bool CheckLambdaUsingForMultiThread(const Lambda& lambda)
{
    auto returnTy = lambda.GetReturnType();
    if (returnTy == nullptr) {
        return false;
    }
    returnTy = returnTy->StripAllRefs();
    if (!returnTy->IsClass()) {
        return false;
    }
    auto classDef = StaticCast<ClassType*>(returnTy)->GetClassDef();
    return classDef->GetPackageName() == "std.core" && classDef->GetSrcCodeIdentifier() == "Future";
}

LambdaInline::LambdaInline(CHIRBuilder& builder, const GlobalOptions& opts)
    : opts(opts), inlinePass(builder, GlobalOptions::OptimizationLevel::O2, opts.chirDebugOptimizer)
{
}

void LambdaInline::InlineLambda(const std::vector<Lambda*>& funcs)
{
    if (!opts.IsOptimizationExisted(GlobalOptions::OptimizationFlag::FUNC_INLINING)) {
        return;
    }
    for (auto func : funcs) {
        if (func->GetFuncType()->IsCFunc()) {
            // skip c func type
            continue;
        }
        RunOnLambda(*func);
    }
}

/*
 * easy func can be inline:
 *   1. lambda func as a parameter.
 *   2. lambda call once in this easy function.
 *   3. lambda will not escape in this function.
 *   func g(x: LambdaType) {
 *     // ...
 *     x(<args>)
 *     // ...
 *   }
 */
bool LambdaInline::IsLambdaPassToEasyFunc(const Lambda& lambda) const
{
    // 0. lambda is not using for multi-thread
    if (CheckLambdaUsingForMultiThread(lambda)) {
        return false;
    }

    // 1. judge if lambda is a parameter of an apply.
    auto users = lambda.GetResult()->GetUsers();
    CJC_ASSERT(users.size() == 1 && users[0]->IsApply());
    auto apply = StaticCast<Apply*>(users[0]);
    size_t index = apply->GetArgs().size();
    auto args = apply->GetArgs();
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i] == lambda.GetResult()) {
            index = i;
            break;
        }
    }
    if (index == args.size() || !apply->GetCallee()->IsFuncWithBody()) {
        return false;
    }
    // 2. judge lambda is only used in one callee apply.
    auto func = VirtualCast<Func*>(apply->GetCallee());
    CJC_ASSERT(index < func->GetParams().size());
    auto lambdaArg = func->GetParams()[index];
    auto lambdaArgUsers = GetNonDebugUsers(*lambdaArg);
    if (lambdaArgUsers.size() != 1) {
        return false;
    }
    auto onlyUser = lambdaArgUsers[0];
    if (onlyUser != nullptr && onlyUser->GetExprKind() == ExprKind::APPLY &&
        StaticCast<Apply*>(onlyUser)->GetCallee() == lambdaArg) {
        return true;
    }
    return false;
}

void LambdaInline::RunOnLambda(Lambda& lambda)
{
    auto users = lambda.GetResult()->GetUsers();
    if (GetNonDebugUsers(*lambda.GetResult()).empty() && !opts.enableCompileDebug) {
        for (auto user : users) {
            user->RemoveSelfFromBlock();
        }
        lambda.RemoveSelfFromBlock();
        PrintOptInfo(lambda, opts.chirDebugOptimizer, "EraseUselessLambdas");
        return;
    }
    if (users.size() != 1U || users[0]->GetExprKind() != ExprKind::APPLY) {
        return;
    }
    if (StaticCast<Apply*>(users[0])->GetCallee() == lambda.GetResult()) {
        // If a lambda can be inlined and then removed, it should meet the requirements:
        // 1) only has one user which is an `APPLY` instruction;
        // 2) is the callee of `APPLY` instruction.
        // In other words, if a lambda is called only once and has no other uses, it can be inlined and then removed.
        inlinePass.DoFunctionInline(*StaticCast<Apply*>(users[0]), "functionInlineForLambda");
        if (!opts.enableCompileDebug) {
            lambda.RemoveSelfFromBlock();
        }
        return;
    }
    if (IsLambdaPassToEasyFunc(lambda)) {
        // only have one consumer as a parameter to apply expression, which will not escape in new function
        inlinePass.DoFunctionInline(*StaticCast<Apply*>(users[0]), "functionInlineForLambda");
        auto newUsers = lambda.GetResult()->GetUsers();
        if (newUsers[0]->GetExprKind() == ExprKind::APPLY &&
            StaticCast<Apply*>(newUsers[0])->GetCallee() == lambda.GetResult()) {
            inlinePass.DoFunctionInline(*StaticCast<Apply*>(newUsers[0]), "functionInlineForLambda");
        }

        if (!opts.enableCompileDebug) {
            lambda.RemoveSelfFromBlock();
        }
    }
}
}  // namespace Cangjie::CHIR
