// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "TypeCheckerImpl.h"

using namespace Cangjie;
using namespace AST;

// The function does not need a target type since bottom type is a subtype of all (target) types.
bool TypeChecker::TypeCheckerImpl::ChkReturnExpr(ASTContext& ctx, ReturnExpr& re)
{
    return Ty::IsTyCorrect(SynReturnExpr(ctx, re));
}

ModalTy TypeChecker::TypeCheckerImpl::SynReturnExpr(ASTContext& ctx, ReturnExpr& re)
{
    if (!re.refFuncBody || !re.refFuncBody->retType) {
        re.SetTy({TypeManager::GetInvalidTy()});
        return re.GetTy();
    }

    CJC_ASSERT(re.expr);
    bool isWellTyped = true;
    re.SetTy({TypeManager::GetInvalidTy()});

    // Analyse re.expr.
    auto retTy = re.refFuncBody->retType->GetTy();
    if (Ty::IsTyCorrect(retTy) && !retTy->IsQuest()) {
        bool isInConstructor = re.refFuncBody->funcDecl && IsInstanceConstructor(*re.refFuncBody->funcDecl);
        if (isInConstructor) {
            isWellTyped = CheckReturnInConstructors(ctx, re) && isWellTyped;
        } else {
            isWellTyped = Check(ctx, retTy, re.expr.get());
        }
        if (isWellTyped) {
            ctx.targetTypeMap[re.expr.get()] = re.expr->GetTy();
        }
    } else {
        isWellTyped = Synthesize({ctx, SynPos::EXPR_ARG}, re.expr.get()) ? true : false;
        // Keep ideal literal types pending for the return expression of a lambda that is a direct
        // argument of a generic call, so generic type argument inference can unify it against the
        // expected contextual type (e.g. `functest({ => return 0 }, 2)` where the expected type is
        // `() -> Int32`). Mirrors the implicit-return handling in `SynthesizeAndReplaceIdealTy`.
        if (ctx.inFuncArgLambdaBody == 0) {
            isWellTyped = isWellTyped && ReplaceIdealTy(*re.expr);
        }
    }

    // Replace ClassThisTy to ClassTy when the function's outer declaration is not Class or Extend which extends class.
    if (!Is<ClassDecl>(re.refFuncBody->parentClassLike)) {
        if (auto ctt = DynamicCast<ClassThisTy>(re.expr->DataTy()); ctt && ctt->decl) {
            re.expr->SetTy(ctt->decl->GetTy());
        }
    }

    // Generic decls imported from foreign code and created by auto-sdk have no body, no need to check return.
    if (!isWellTyped && NeedCheckBodyReturn(*re.refFuncBody)) {
        re.SetTy({TypeManager::GetInvalidTy()});
    } else {
        re.SetTy({TypeManager::GetNothingTy()});
    }

    return re.GetTy();
}

bool TypeChecker::TypeCheckerImpl::CheckReturnInConstructors(ASTContext& ctx, const ReturnExpr& re)
{
    CJC_NULLPTR_CHECK(re.expr);
    return Check(ctx, ModalTy{TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT)}, re.expr.get());
}

bool TypeChecker::TypeCheckerImpl::NeedCheckBodyReturn(const FuncBody& fb) const
{
    if (fb.parentClassLike && HasJavaAttr(*fb.parentClassLike) &&
        fb.parentClassLike->TestAttr(Attribute::GENERIC, Attribute::IMPORTED)) {
        return false;
    }
    return !(fb.funcDecl && HasJavaAttr(*fb.funcDecl) &&
        fb.funcDecl->TestAttr(Attribute::GENERIC, Attribute::IMPORTED));
}
