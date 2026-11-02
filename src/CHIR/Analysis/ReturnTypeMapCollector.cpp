// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Analysis/ReturnTypeMapCollector.h"

#include "cangjie/CHIR/IR/CHIRContext.h"
#include "cangjie/CHIR/Utils/CHIRCasting.h"

namespace Cangjie::CHIR {
namespace {
Type* GetRuntimeTypeFromFunc(const Value* retVal, bool isInLambda);

Type* GetRuntimeTypeFromLambda(const Apply& apply, Type* type)
{
    if (!apply.GetCallee()->IsLocalVar()) {
        return type;
    }
    auto localVar = StaticCast<LocalVar*>(apply.GetCallee());
    if (localVar->GetExpr()->GetExprKind() != ExprKind::LAMBDA) {
        return type;
    }
    auto lambda = StaticCast<Lambda*>(localVar->GetExpr());
    return GetRuntimeTypeFromFunc(lambda->GetReturnValue(), true);
}

Type* GetRuntimeTypeFromFunc(const Value* retVal, bool isInLambda = false)
{
    if (!retVal) {
        return nullptr;
    }
    auto rtTy = retVal->GetType()->StripAllRefs();
    // Do not process the return type that is not class type.
    if (!rtTy->IsClass()) {
        return nullptr;
    }
    auto users = retVal->GetUsers();
    if (users.size() == 1 && users[0]->GetExprKind() == ExprKind::STORE) {
        auto val = StaticCast<Store*>(users[0])->GetValue();
        if (!val->IsLocalVar()) {
            return nullptr;
        }
        auto expr = StaticCast<LocalVar*>(val)->GetExpr();
        auto srcTy = val->GetType();
        if (expr->GetExprKind() == ExprKind::CLASS_STATIC_CAST) {
            auto cast = StaticCast<ClassStaticCast*>(expr);
            srcTy = cast->GetSourceType();
        } else if (expr->GetExprKind() == ExprKind::APPLY && !isInLambda) {
            auto applyResType = GetRuntimeTypeFromLambda(*StaticCast<Apply*>(expr), srcTy);
            srcTy = applyResType != nullptr ? applyResType : srcTy;
        }
        srcTy = srcTy->StripAllRefs();
        if (srcTy == rtTy) {
            // if type is same, skip
            return nullptr;
        }
        // Ensure that the actual return value type is not a reference.
        return srcTy;
    }
    return nullptr;
}
} // namespace

void ReturnTypeMapCollector::Collect(ReturnTypeMap& returnTypeMap) const
{
    // Collect global functions which have a more concrete runtime type than the
    // explicit return type, so de-virtualization can infer more precisely.
    for (auto func : package->GetGlobalFuncsWithBody()) {
        auto res = GetRuntimeTypeFromFunc(func->GetReturnValue());
        if (res != nullptr) {
            returnTypeMap.emplace(func, res);
        }
    }
}

} // namespace Cangjie::CHIR
