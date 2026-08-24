// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "TypeCheckerImpl.h"

#include "Diags.h"

using namespace Cangjie;
using namespace Sema;

ModalTy TypeChecker::TypeCheckerImpl::SynOptionalChainExpr(const CheckerContext& ctx, OptionalChainExpr& oce)
{
    CJC_NULLPTR_CHECK(oce.desugarExpr);
    oce.SetTy(Synthesize(ctx, oce.desugarExpr.get()));
    return oce.GetTy();
}

bool TypeChecker::TypeCheckerImpl::ChkOptionalChainExpr(ASTContext& ctx, ModalTy target, OptionalChainExpr& oce)
{
    CJC_NULLPTR_CHECK(oce.desugarExpr);
    if (!Ty::IsTyCorrect(SynOptionalChainExpr({ctx, SynPos::EXPR_ARG}, oce))) {
        return false;
    }
    if (!CheckOptionBox(target, oce.desugarExpr->GetTy())) {
        DiagMismatchedTypes(diag, oce, target);
        oce.desugarExpr->SetTy({TypeManager::GetInvalidTy()});
        oce.SetTy({TypeManager::GetInvalidTy()});
        return false;
    }
    return true;
}
