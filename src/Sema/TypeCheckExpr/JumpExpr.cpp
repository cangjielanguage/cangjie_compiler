// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "TypeCheckerImpl.h"

#include "cangjie/AST/ASTCasting.h"

using namespace Cangjie;
using namespace AST;

namespace {
// Find the closest loop expression where the jump expression locates.
Ptr<Expr> FindLoopExpr(const ASTContext& ctx, const JumpExpr& jumpExpr)
{
    auto sym = ScopeManager::GetRefLoopSymbol(ctx, jumpExpr);
    return sym ? DynamicCast<Expr*>(sym->node) : nullptr;
}
} // namespace

ModalTy TypeChecker::TypeCheckerImpl::SynLoopControlExpr(const ASTContext& ctx, JumpExpr& je) const
{
    je.refLoop = FindLoopExpr(ctx, je);
    // je.refLoop may be a null pointer, but the errors are already reported by CheckReturnAndJump in PreCheck
    je.SetTy(je.refLoop ? ModalTy{TypeManager::GetNothingTy()} : ModalTy{TypeManager::GetInvalidTy()});
    return je.GetTy();
}

bool TypeChecker::TypeCheckerImpl::ChkLoopControlExpr(const ASTContext& ctx, JumpExpr& je) const
{
    SynLoopControlExpr(ctx, je);
    return je.refLoop != nullptr;
}
