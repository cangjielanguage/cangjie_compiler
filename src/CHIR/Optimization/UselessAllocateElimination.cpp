// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Optimization/UselessAllocateElimination.h"

#include "cangjie/CHIR/IR/CHIRBuilder.h"
#include "cangjie/CHIR/IR/Type/ClassDef.h"
#include "cangjie/CHIR/Utils/CHIRCasting.h"
#include "cangjie/CHIR/IR/Expression/Terminator.h"

using namespace Cangjie::CHIR;

void UselessAllocateElimination::RunOnPackage(const Package& package, CHIRBuilder& builder)
{
    for (auto func : package.GetGlobalFuncsWithBody()) {
        RunOnFunc(*func, builder);
    }
}

void UselessAllocateElimination::RunOnFunc(const Function& func, CHIRBuilder& builder)
{
    for (auto block : func.GetBody()->GetBlocks()) {
        for (auto expr : block->GetExpressions()) {
            if (!Is<AllocateBase>(expr)) {
                continue;
            }
            auto allocate = StaticCast<AllocateBase*>(expr);
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
                        StaticCast<StoreElementRef*>(e)->GetBase() == res) ||
                    e->GetExprKind() == ExprKind::DEBUGEXPR;
            });
            if (onlyBeenWritten) {
                if (auto tryAllocate = DynamicCast<TryAllocate*>(allocate)) {
                    auto success = tryAllocate->GetSuccessBlock();
                    auto loc = tryAllocate->GetDebugLocation();
                    tryAllocate->RemoveSelfFromBlock();
                    block->AppendExpression(builder.CreateTerminator<GoTo>(loc, success, block));
                } else {
                    allocate->RemoveSelfFromBlock();
                }
                for (auto user : users) {
                    user->RemoveSelfFromBlock();
                }
            }
        }
    }
}
