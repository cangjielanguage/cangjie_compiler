// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Optimization/UselessAllocateElimination.h"

#include "cangjie/CHIR/Analysis/Utils.h"
#include "cangjie/CHIR/Utils/CHIRCasting.h"
#include "cangjie/CHIR/IR/Expression/Terminator.h"

using namespace Cangjie::CHIR;

void UselessAllocateElimination::RunOnPackage(const Package& package, bool isDebug)
{
    for (auto func : package.GetGlobalFuncs()) {
        RunOnFunc(*func, isDebug);
    }
}

void UselessAllocateElimination::RunOnFunc(const Func& func, bool isDebug)
{
    for (auto block : func.GetBody()->GetBlocks()) {
        for (auto expr : block->GetExpressions()) {
            if (expr->GetExprKind() != ExprKind::ALLOCATE) {
                continue;
            }
            auto allocate = StaticCast<Allocate*>(expr);
            if (auto allocatedTy = allocate->GetType();
                allocatedTy->IsClass() && StaticCast<ClassType*>(allocatedTy)->GetClassDef()->GetFinalizer()) {
                continue;
            }
            auto res = allocate->GetResult();
            if (func.GetReturnValue() == res) {
                continue;
            }
            auto users = res->GetUsers();
            auto onlyBeenWritten = std::all_of(users.begin(), users.end(), [res](auto e) {
                return (e->GetExprKind() == ExprKind::STORE && StaticCast<Store*>(e)->GetLocation() == res) ||
                    (e->GetExprKind() == ExprKind::STORE_ELEMENT_REF &&
                        StaticCast<StoreElementRef*>(e)->GetLocation() == res) ||
                    e->GetExprKind() == ExprKind::DEBUGEXPR;
            });
            if (onlyBeenWritten) {
                allocate->RemoveSelfFromBlock();
                for (auto user : users) {
                    user->RemoveSelfFromBlock();
                }
                if (isDebug && !allocate->GetDebugLocation().GetBeginPos().IsZero()) {
                    std::string message = "[UselessAllocateElimination] Allocate" +
                        ToPosInfo(allocate->GetDebugLocation()) + " and its users have been deleted\n";
                    std::cout << message;
                }
            }
        }
    }
}