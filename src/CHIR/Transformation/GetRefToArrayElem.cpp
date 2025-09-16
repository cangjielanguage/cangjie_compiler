// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Transformation/GetRefToArrayElem.h"

#include "cangjie/CHIR/CHIRCasting.h"
#include "cangjie/CHIR/Expression/Terminator.h"

using namespace Cangjie::CHIR;

void GetRefToArrayElem::RunOnPackage(const Package& package, CHIRBuilder& builder)
{
    for (auto func : package.GetGlobalFuncs()) {
        RunOnFunc(*func, builder);
    }
}

void GetRefToArrayElem::RunOnFunc(const Func& func, CHIRBuilder& builder)
{
    for (auto block : func.GetBody()->GetBlocks()) {
        for (auto expr : block->GetExpressions()) {
            if (expr->GetExprKind() != ExprKind::INTRINSIC) {
                continue;
            }
            auto intrinsic = StaticCast<Intrinsic*>(expr);
            if (intrinsic->GetIntrinsicKind() != CHIR::IntrinsicKind::ARRAY_GET_UNCHECKED) {
                continue;
            }
            auto users = intrinsic->GetResult()->GetUsers();
            if (!std::all_of(users.begin(), users.end(), [](auto e) { return e->GetExprKind() == ExprKind::FIELD; })) {
                continue;
            }
            auto callContext = IntrisicCallContext {
                .kind = IntrinsicKind::ARRAY_GET_REF_UNCHECKED,
                .args = intrinsic->GetOperands()
            };
            auto arrayGetRef = builder.CreateExpression<Intrinsic>(
                builder.GetType<RefType>(intrinsic->GetResult()->GetType()), callContext, intrinsic->GetParentBlock());
            arrayGetRef->CopyAnnotationMapFrom(*intrinsic);
            for (auto user : users) {
                auto field = StaticCast<Field*>(user);
                auto fieldTy = field->GetResult()->GetType();
                auto getElemRef = builder.CreateExpression<GetElementRef>(builder.GetType<RefType>(fieldTy),
                    arrayGetRef->GetResult(), field->GetPath(), field->GetParentBlock());
                getElemRef->CopyAnnotationMapFrom(*field);
                getElemRef->GetResult()->EnableAttr(Attribute::READONLY);
                auto load = builder.CreateExpression<Load>(fieldTy, getElemRef->GetResult(), field->GetParentBlock());
                load->CopyAnnotationMapFrom(*field);
                getElemRef->MoveBefore(user);
                field->ReplaceWith(*load);
            }
            intrinsic->ReplaceWith(*arrayGetRef);
        }
    }
}