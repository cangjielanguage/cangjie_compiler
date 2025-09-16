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

Ptr<Ty> TypeChecker::TypeCheckerImpl::SynPerformExpr(ASTContext& ctx, PerformExpr& pe)
{
    CJC_NULLPTR_CHECK(pe.expr); // Parser guarantees.
    auto exprTy = Synthesize(ctx, pe.expr.get());
    if (!Ty::IsTyCorrect(exprTy)) {
        pe.ty = TypeManager::GetInvalidTy();
        return pe.ty;
    }
    if (auto commandTy = PromoteToCommandTy(*pe.expr, *exprTy); commandTy) {
        pe.ty = (*commandTy)->typeArgs[0];
    } else {
        pe.ty = TypeManager::GetInvalidTy();
    }
    
    return pe.ty;
}
