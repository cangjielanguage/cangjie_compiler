// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "TypeCheckerImpl.h"

using namespace Cangjie;
using namespace AST;

ModalTy TypeChecker::TypeCheckerImpl::SynThrowExpr(ASTContext& ctx, ThrowExpr& te)
{
    CJC_NULLPTR_CHECK(te.expr); // Parser guarantees.
    Synthesize({ctx, SynPos::EXPR_ARG}, te.expr.get());
    te.SetTy({TypeManager::GetNothingTy()});
    if (!te.expr->GetTy().IsCorrect()) {
        return {TypeManager::GetInvalidTy()};
    }
    if (te.expr->GetTy()->IsEnum()) {
        if (auto refExpr = DynamicCast<RefExpr*>(te.expr.get()); refExpr && refExpr->ref.identifier == RESOURCE_NAME) {
            return {TypeManager::GetNothingTy()};
        }
    } else if (te.expr->GetTy()->IsClass() || te.expr->GetTy()->IsGeneric()) {
        // Check if the type of expression thrown is derived from `core.Exception` class
        // For class type and generic type.
        auto exception = importManager.GetCoreDecl<ClassDecl>(CLASS_EXCEPTION);
        auto error = importManager.GetCoreDecl<ClassDecl>(CLASS_ERROR);
        bool foundClass = exception && error;
        if (foundClass &&
            (typeManager.IsSubtype(te.expr->GetTy(), exception->GetTy()) ||
                typeManager.IsSubtype(te.expr->GetTy(), error->GetTy()))) {
            return {TypeManager::GetNothingTy()};
        }
    }
    diag.Diagnose(te, DiagKind::sema_throw_expr_with_wrong_type);
    return {TypeManager::GetInvalidTy()};
}
