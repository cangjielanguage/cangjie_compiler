// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements checks of types used with ObjCPointer
 */


#include "NativeFFI/Utils.h"
#include "NativeFFI/ObjC/Utils/Common.h"
#include "cangjie/AST/Walker.h"
#include "Handlers.h"

using namespace Cangjie;
using namespace Cangjie::AST;
using namespace Cangjie::Interop::ObjC;

namespace {

void ReportObjCIncompatibleType(const InteropContext& ctx, Ptr<Type> typeUsage) {
    Ptr<Node> errorRef = typeUsage;
    // find the concrete type node that is not compatible
    if (typeUsage->GetTypeArgs().size() > 0) {
        auto tyArg = typeUsage->GetTypeArgs()[0];
        errorRef = tyArg;
        if (auto fTy = As<ASTKind::FUNC_TYPE>(tyArg)) {
            if (!ctx.typeMapper.IsObjCCompatible(*fTy->retType->ty)) {
                errorRef = fTy->retType;
            }
            for (Ptr<Type> arg : fTy->paramTypes) {
                if (!ctx.typeMapper.IsObjCCompatible(*arg->ty)) {
                    errorRef = arg;
                    break;
                }
            }
        }
    }
    ctx.diag.DiagnoseRefactor(
        DiagKindRefactor::sema_objc_func_argument_must_be_objc_compatible,
        *errorRef,
        Ty::GetDeclOfTy(typeUsage->ty)->identifier.Val());
    typeUsage->EnableAttr(Attribute::IS_BROKEN);
}

} // namespace

void CheckObjCFuncTypeArguments::HandleImpl(InteropContext& ctx)
{
    for (auto& file : ctx.pkg.files) {
        Walker(file, Walker::GetNextWalkerID(), [&file, &ctx](auto node) {
            if (!node->IsSamePackage(*file->curPackage)) {
                return VisitAction::WALK_CHILDREN;
            }
            if (Ptr<Decl> decl = As<ASTKind::DECL>(node);
                decl && ctx.typeMapper.IsObjCFuncOrBlock(*decl)) {
                return VisitAction::SKIP_CHILDREN;
            }
            Ptr<Type> typeUsage = As<ASTKind::TYPE>(node);
            if (typeUsage && typeUsage->TestAttr(Attribute::COMPILER_ADD)) {
                return VisitAction::SKIP_CHILDREN;
            }
            if (typeUsage
             && typeUsage->ty
             && typeUsage->ty->typeArgs.size() == 1
             && ctx.typeMapper.IsObjCFuncOrBlock(*typeUsage->ty)) {
                auto tyArg = typeUsage->ty->typeArgs[0];
                auto valid = tyArg->IsFunc();
                valid &= !tyArg->IsCFunc();
                for (auto subTy : tyArg->typeArgs) {
                    valid &= ctx.typeMapper.IsObjCCompatible(*subTy);
                }
                if (!valid) {
                    ReportObjCIncompatibleType(ctx, typeUsage);
                }
            }
            return VisitAction::WALK_CHILDREN;
        }).Walk();
    }
}
