// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/AST2CHIR/TranslateASTNode/Translator.h"

#include "cangjie/CHIR/AST2CHIR/Utils.h"

using namespace Cangjie::CHIR;
using namespace Cangjie;

Ptr<Value> Translator::Visit(const AST::PointerExpr& expr)
{
    auto ty = TranslateType(*expr.ty);
    CHIR::IntrinsicKind intrinsicKind;
    std::vector<Value*> args{};

    if (expr.arg) {
        intrinsicKind = IntrinsicKind::CPOINTER_INIT1;
        auto loc = TranslateLocation(*expr.arg);
        Value* argVal = nullptr;
        if (expr.arg->withInout) {
            auto argLeftValInfo = TranslateExprAsLeftValue(*expr.arg->expr);
            argVal = argLeftValInfo.base;
            // polish this
            if (!argLeftValInfo.path.empty()) {
                auto lhsCustomType = StaticCast<CustomType*>(argVal->GetType()->StripAllRefs());
                if (argVal->GetType()->IsRef()) {
                    argVal = CreateGetElementRefWithPath(TranslateLocation(expr), argVal,
                        argLeftValInfo.path, currentBlock, *lhsCustomType);
                } else {
                    auto memberType = GetInstMemberTypeByName(*lhsCustomType, argLeftValInfo.path, builder);
                    auto getMember = CreateAndAppendExpression<FieldByName>(
                        TranslateLocation(expr), memberType, argVal, argLeftValInfo.path, currentBlock);
                    argVal = getMember->GetResult();
                }
            }
            auto ty1 = TranslateType(*expr.arg->ty);
            auto callContext = IntrisicCallContext {
                .kind = IntrinsicKind::INOUT_PARAM,
                .args = std::vector<Value*>{argVal}
            };
            argVal = CreateAndAppendExpression<Intrinsic>(loc, ty1, callContext, currentBlock)->GetResult();
        } else {
            argVal = TranslateASTNode(*expr.arg, *this);
        }
        CJC_NULLPTR_CHECK(argVal);
        argVal = GenerateLoadIfNeccessary(*argVal, false, false, expr.arg->withInout, loc);
        args.emplace_back(argVal);
    } else {
        intrinsicKind = IntrinsicKind::CPOINTER_INIT0;
    }
    const auto& loc = TranslateLocation(expr);
    auto callContext = IntrisicCallContext {
        .kind = intrinsicKind,
        .args = args
    };
    return CreateAndAppendExpression<Intrinsic>(loc, ty, callContext, currentBlock)->GetResult();
}