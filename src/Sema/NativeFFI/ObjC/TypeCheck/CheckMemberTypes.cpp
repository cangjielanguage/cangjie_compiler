// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements checks for Objective-C mirror/subtype member declarations.
 */

#include "Handlers.h"
#include "NativeFFI/Utils.h"
#include "cangjie/AST/AttributePack.h"
#include "cangjie/AST/Match.h"
#include "NativeFFI/ObjC/Utils/ASTQuery.h"
#include "cangjie/Basic/DiagnosticEngine.h"

using namespace Cangjie::AST;
using namespace Cangjie::Interop::ObjC;
using namespace Cangjie::Native::FFI;

void CheckMemberTypes::HandleImpl(TypeCheckContext& ctx)
{
    auto isImpl = IsObjCExport(*ctx.target.GetTy());
    for (auto& decl : ctx.target.GetMemberDeclPtrs()) {
        // Only public members of exported declarations must be checked.
        if (isImpl && !decl->TestAttr(Attribute::PUBLIC)) {
            continue;
        }

        switch (decl->astKind) {
            case ASTKind::FUNC_DECL:
                CheckFuncTypes(*StaticAs<ASTKind::FUNC_DECL>(decl), ctx);
                break;
            case ASTKind::PROP_DECL:
                CheckPropTypes(*StaticAs<ASTKind::PROP_DECL>(decl), ctx);
                break;
            case ASTKind::VAR_DECL:
                CheckVarTypes(*StaticAs<ASTKind::VAR_DECL>(decl), ctx);
                break;
            default:
                break;
        }
    }
}

void CheckMemberTypes::CheckPropTypes(PropDecl& pd, TypeCheckContext& ctx)
{
    if (IsObjCCompatible(*pd.GetTy())) {
        return;
    }

    auto kind = IsObjCExport(*pd.outerDecl->GetTy())
        ? DiagKindRefactor::sema_objc_interop_prop_must_be_objc_compatible
        : pd.TestAttr(Attribute::DESUGARED_FOREIGN_FIELD)
            ? DiagKindRefactor::sema_objc_interop_field_must_be_objc_compatible
            : DiagKindRefactor::sema_objc_interop_prop_must_be_objc_compatible;

    auto builder = ctx.diag.DiagnoseRefactor(kind, *pd.type, GetObjCDeclKindInfo(*pd.outerDecl));
    builder.AddMainHintArguments(pd.GetTy().String());
    pd.EnableAttr(Attribute::IS_BROKEN);
    pd.outerDecl->EnableAttr(Attribute::HAS_BROKEN, Attribute::IS_BROKEN);
}

void CheckMemberTypes::CheckVarTypes(VarDecl& vd, TypeCheckContext& ctx)
{
    if (IsObjCCompatible(*vd.GetTy())) {
        return;
    }

    auto builder = ctx.diag.DiagnoseRefactor(DiagKindRefactor::sema_objc_interop_field_must_be_objc_compatible,
        *vd.type, GetObjCDeclKindInfo(*vd.outerDecl));
    builder.AddMainHintArguments(vd.GetTy().String());
    vd.EnableAttr(Attribute::IS_BROKEN);
    vd.outerDecl->EnableAttr(Attribute::HAS_BROKEN, Attribute::IS_BROKEN);
}

void CheckMemberTypes::CheckFuncTypes(FuncDecl& fd, TypeCheckContext& ctx)
{
    if (!fd.funcBody || IsGeneratedMember(fd)) {
        return;
    }

    if (!fd.TestAttr(Attribute::CONSTRUCTOR)) {
        CheckFuncRetType(fd, ctx);
    }

    CheckFuncParamTypes(fd, ctx);
}

void CheckMemberTypes::CheckFuncRetType(FuncDecl& fd, TypeCheckContext& ctx)
{
    if (fd.funcBody->retType && !IsObjCCompatible(*fd.funcBody->retType->GetTy(), TypePosition::OUT)) {
        auto builder = ctx.diag.DiagnoseRefactor(DiagKindRefactor::sema_objc_interop_method_ret_must_be_objc_compatible,
            *fd.funcBody->retType, GetObjCDeclKindInfo(*fd.outerDecl));
        builder.AddMainHintArguments(fd.funcBody->retType->ToString());

        fd.EnableAttr(Attribute::IS_BROKEN);
        fd.outerDecl->EnableAttr(Attribute::HAS_BROKEN, Attribute::IS_BROKEN);
    }
}

void CheckMemberTypes::CheckFuncParamTypes(FuncDecl& fd, TypeCheckContext& ctx)
{
    auto errKind = fd.TestAttr(Attribute::CONSTRUCTOR)
        ? DiagKindRefactor::sema_objc_interop_ctor_param_must_be_objc_compatible
        : DiagKindRefactor::sema_objc_interop_method_param_must_be_objc_compatible;

    for (auto& paramList : fd.funcBody->paramLists) {
        for (auto& param : paramList->params) {
            if (IsObjCCompatible(*param->GetTy(), TypePosition::IN)) {
                continue;
            }

            fd.EnableAttr(Attribute::IS_BROKEN);
            fd.outerDecl->EnableAttr(Attribute::HAS_BROKEN, Attribute::IS_BROKEN);

            auto builder = ctx.diag.DiagnoseRefactor(errKind, *param, GetObjCDeclKindInfo(*fd.outerDecl));
            builder.AddMainHintArguments(param->GetTy().String());
        }
    }
}
