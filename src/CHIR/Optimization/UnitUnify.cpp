// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Optimization/UnitUnify.h"
#include "cangjie/CHIR/Analysis/Utils.h"
#include "cangjie/CHIR/Utils/CHIRCasting.h"
#include "cangjie/CHIR/Utils/Visitor/Visitor.h"

using namespace Cangjie::CHIR;
namespace {
bool NeedUnify(const Expression& expr)
{
    auto result = expr.GetResult();
    if (result == nullptr) {
        return false;
    }
    if (!result->GetType()->IsUnit()) {
        return false;
    }
    if (result->GetUsers().empty()) {
        return false;
    }
    if (auto constant = Cangjie::DynamicCast<const Constant*>(&expr)) {
        if (constant->GetValue()->IsNullLiteral() || constant->GetValue()->IsUnitLiteral()) {
            return false;
        }
    }
    return true;
}
}

UnitUnify::UnitUnify(CHIRBuilder& builder) : builder(builder)
{
}

void UnitUnify::RunOnPackage(const Ptr<const Package>& package, bool isDebug)
{
    for (auto func : package->GetGlobalFuncsWithBody()) {
        RunOnFunc(func, isDebug);
    }
}

void UnitUnify::RunOnFunc(const Ptr<Function>& func, bool isDebug)
{
    std::unordered_map<Mode, Constant*> localUnit;
    auto preAcation = [this, isDebug, &localUnit](Expression& expr) {
        // skip lambda to avoid create many Constant expression to effect function inline
        // maybe we can discuss later
        if (Is<Lambda>(expr)) {
            return VisitResult::SKIP;
        }
        if (Is<GetRTTI>(expr) || Is<GetRTTIStatic>(expr)) {
            return VisitResult::CONTINUE;
        }
        if (NeedUnify(expr)) {
            auto localInfo = expr.GetResult()->GetType()->GetModalInfo().Local();
            auto it = localUnit.find(localInfo);
            Constant* constant = nullptr;
            if (it != localUnit.end()) {
                constant = it->second;
            } else {
                auto entryBlock = expr.GetParentBlockGroup()->GetEntryBlock();
                auto unitTy = builder.WithModal(builder.GetUnitTy(), localInfo);
                constant = builder.CreateConstantExpression<UnitLiteral>(unitTy, entryBlock);
                entryBlock->InsertExprIntoHead(*constant);
                localUnit[localInfo] = constant;
            }
            expr.GetResult()->ReplaceWith(*constant->GetResult(), expr.GetParentBlockGroup());
            if (isDebug) {
                std::cout << "[UnitUnify] unit unify" << ToPosInfo(expr.GetDebugLocation()) << ".\n";
            }
        }
        return VisitResult::CONTINUE;
    };
    Visitor::Visit(*func, preAcation);
}