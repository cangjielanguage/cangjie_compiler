// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Optimization/OptFuncRetType.h"
#include "cangjie/CHIR/Utils/CHIRCasting.h"
#include "cangjie/CHIR/Utils/Utils.h"
#include "cangjie/Utils/CastingTemplate.h"
#include "cangjie/Utils/CheckUtils.h"

using namespace Cangjie::CHIR;

namespace {
std::vector<FuncBase*> GetAllGlobalFuncs(const Package& package)
{
    std::vector<FuncBase*> result;
    for (auto val : package.GetImportedVarAndFuncs()) {
        auto func = Cangjie::DynamicCast<ImportedFunc*>(val);
        if (func == nullptr || !ReturnTypeShouldBeVoid(*func)) {
            continue;
        }
        result.emplace_back(func);
    }
    for (auto func : package.GetGlobalFuncs()) {
        if (!ReturnTypeShouldBeVoid(*func)) {
            continue;
        }
        result.emplace_back(func);
    }
    return result;
}
}

OptFuncRetType::OptFuncRetType(Package& package, CHIRBuilder& builder) : package(package), builder(builder)
{
}

void OptFuncRetType::Unit2Void()
{
    auto allFuncs = GetAllGlobalFuncs(package);
    for (auto func : allFuncs) {
        CJC_ASSERT(func->GetReturnType()->IsUnit());
        LocalVar* oldRet = nullptr;
        if (auto f = DynamicCast<Func>(func)) {
            oldRet = f->GetReturnValue();
            CJC_NULLPTR_CHECK(oldRet);
        }
        func->ReplaceReturnValue(nullptr, builder);
        if (oldRet != nullptr && oldRet->GetUsers().empty()) {
            oldRet->GetExpr()->RemoveSelfFromBlock();
        }
        for (auto user : func->GetUsers()) {
            if (auto apply = DynamicCast<Apply*>(user)) {
                CJC_ASSERT(apply->GetCallee() == func);
                auto funcCallContext = FuncCallContext{
                    .args = apply->GetArgs(),
                    .instTypeArgs = apply->GetInstantiatedTypeArgs(),
                    .thisType = apply->GetThisType()
                };
                auto newApply = builder.CreateExpression<Apply>(
                    apply->GetDebugLocation(), func->GetReturnType(), apply->GetCallee(), funcCallContext, apply->GetParentBlock());
                if (apply->IsSuperCall()) {
                    newApply->SetSuperCall();
                }
                apply->ReplaceWith(*newApply);
            } else {
                auto awe = StaticCast<ApplyWithException*>(user);
                CJC_ASSERT(awe->GetCallee() == func);
                auto funcCallContext = FuncCallContext{
                    .args = awe->GetArgs(),
                    .instTypeArgs = awe->GetInstantiatedTypeArgs(),
                    .thisType = awe->GetThisType()
                };
                auto newApply = builder.CreateExpression<ApplyWithException>(
                    awe->GetDebugLocation(), func->GetReturnType(), awe->GetCallee(), funcCallContext,
                    awe->GetSuccessBlock(), awe->GetErrorBlock(), awe->GetParentBlock());
                awe->ReplaceWith(*newApply);
            }
        }
    }
}