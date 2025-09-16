// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "TypeCheckerImpl.h"

using namespace Cangjie;
using namespace AST;

Ptr<Ty> TypeChecker::TypeCheckerImpl::SynResumeExpr(ASTContext& ctx, ResumeExpr& re)
{
    Ty* resumptionParamTy = typeManager.GetAnyTy();

    if (re.enclosing) {
        resumptionParamTy = (*re.enclosing)->commandResultTy;
        re.ty = TypeManager::GetNothingTy();
    } else {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_implicit_resume_outside_handler, re);
        re.ty = TypeManager::GetInvalidTy();
    }

    if (re.throwingExpr) {
        auto exception = importManager.GetCoreDecl<ClassDecl>(CLASS_EXCEPTION);
        auto error = importManager.GetCoreDecl<ClassDecl>(CLASS_ERROR);
        Synthesize(ctx, re.throwingExpr.get());
        CJC_NULLPTR_CHECK(re.throwingExpr->ty);

        if (!typeManager.IsSubtype(re.throwingExpr->ty, exception->ty) &&
            !typeManager.IsSubtype(re.throwingExpr->ty, error->ty)) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_resume_throwing_mismatch_type, re);
            re.ty = TypeManager::GetInvalidTy();
        }
    } else if (re.withExpr) {
        if (!Check(ctx, resumptionParamTy, re.withExpr)) {
            // `Check` produces the error message.
            re.ty = TypeManager::GetInvalidTy();
        }
    } else {
        auto unitTy = typeManager.GetPrimitiveTy(TypeKind::TYPE_UNIT);
        if (!typeManager.IsSubtype(resumptionParamTy, unitTy)) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_resume_no_with, re, Ty::ToString(resumptionParamTy));
            re.ty = TypeManager::GetInvalidTy();
        }
    }
    return re.ty;
}
