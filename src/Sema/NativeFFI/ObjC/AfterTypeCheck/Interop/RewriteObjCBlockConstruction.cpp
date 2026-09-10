// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements desugaring of ObjCBlock constructor from lambda (function)
 */

#include "NativeFFI/Utils.h"
#include "NativeFFI/ObjC/Utils/Common.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Clone.h"
#include "Handlers.h"

using namespace Cangjie::AST;
using namespace Cangjie::Interop::ObjC;
using namespace Cangjie::Native::FFI;
using namespace Cangjie;

/**
 * ObjCBlock<F>(f) ->
 *      ObjCBlock(registerCangjieLambdaAsBlock(f, { nativeAbi: CPointer<NativeBlockAbi>, args: WRAP(Args)... =>
 *          let cangjieAbi = CPointer<CangjieBlockAbi>(nativeAbi)
 *          let f = getCangjieLambdaFromBlock<F>(cangjieAbi)
 *          return WRAP(f(UNWRAP(args)...))
 *      })
 *      ^-- this lambda expression needs to be a CFunc lambda expression, compiling to a native function
 */
void RewriteObjCBlockConstruction::HandleImpl(InteropContext& ctx)
{
    for (auto& file : ctx.pkg.files) {
        Walker(file, Walker::GetNextWalkerID(), [&file, &ctx](Ptr<Node> node) {
            if (!node->IsSamePackage(*file->curPackage)) {
                return VisitAction::WALK_CHILDREN;
            }
            if (node->astKind != ASTKind::CALL_EXPR) {
                return VisitAction::WALK_CHILDREN;
            }
            auto callExpr = StaticCast<CallExpr>(node);
            CJC_ASSERT(callExpr);

            if (callExpr->callKind != CallKind::CALL_OBJECT_CREATION) {
                return VisitAction::WALK_CHILDREN;
            }
            auto calledFunc = callExpr->resolvedFunction;
            if (calledFunc == nullptr || !calledFunc->TestAttr(Attribute::CONSTRUCTOR)) {
                return VisitAction::WALK_CHILDREN;
            }
            auto containingClass = calledFunc->outerDecl;
            
            if (containingClass == nullptr || containingClass->astKind != ASTKind::CLASS_DECL) {
                return VisitAction::WALK_CHILDREN;
            }
            if (!ctx.typeMapper.IsObjCBlock(*containingClass)) {
                return VisitAction::WALK_CHILDREN;
            }
            if (callExpr->args.size() != 1) {
                return VisitAction::WALK_CHILDREN;
            }
            auto&& arg = callExpr->args[0];
            if (!arg->DataTy()->IsFunc()) {
                return VisitAction::WALK_CHILDREN;
            }

            ctx.factory.SetDesugarExpr(
                callExpr,
                ctx.factory.CreateObjCBlockFromLambdaCall(ASTCloner::Clone<Expr>(arg->expr)));
            return VisitAction::WALK_CHILDREN;
        }).Walk();
    }
}
