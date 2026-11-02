// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "TypeCheckerImpl.h"

#include "Diags.h"
#include "JoinAndMeet.h"

using namespace Cangjie;
using namespace Sema;

ModalTy TypeChecker::TypeCheckerImpl::SynPerformExpr(ASTContext& ctx, PerformExpr& pe)
{
    CJC_NULLPTR_CHECK(pe.expr); // Parser guarantees.
    auto exprTy = Synthesize({ctx, SynPos::EXPR_ARG}, pe.expr.get());
    if (!Ty::IsTyCorrect(exprTy)) {
        pe.SetTy({TypeManager::GetInvalidTy()});
        return pe.GetTy();
    }
    if (pe.expr->GetTy().IsLocalType()) {
        DiagExpectedDataType(*pe.expr);
        pe.SetTy({TypeManager::GetInvalidTy()});
        return pe.GetTy();
    }
    if (auto commandTy = PromoteToCommandTy(*pe.expr, exprTy); commandTy) {
        pe.SetTy((*commandTy)->typeArgs[0]);
    } else {
        pe.SetTy({TypeManager::GetInvalidTy()});
    }

    return pe.GetTy();
}
