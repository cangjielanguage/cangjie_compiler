// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "Desugar/DesugarInTypeCheck.h"
#include "Diags.h"
#include "TypeCheckerImpl.h"

using namespace Cangjie;
using namespace AST;

namespace {
const std::string LEVEL_IDENTGIFIER = "level";
const std::string SYSCAP_IDENTGIFIER = "syscap";
const std::string DEVICE_INFO = "DeviceInfo";
// For level check:
const std::string PKG_NAME_DEVICE_INFO_AT = "ohos.device_info";
// For syscap check:
const std::string PKG_NAME_CANIUSE_AT = "ohos.base";

bool ChkIfImportDeviceInfo(DiagnosticEngine& diag, const ImportManager& im, const IfAvailableExpr& iae)
{
    if (iae.GetFullPackageName() == PKG_NAME_DEVICE_INFO_AT) {
        return true;
    }
    auto importedPkgs = im.GetAllImportedPackages();
    for (auto& importedPkg : importedPkgs) {
        if (importedPkg->srcPackage && importedPkg->srcPackage->fullPackageName == PKG_NAME_DEVICE_INFO_AT) {
            return true;
        }
    }
    auto builder = diag.DiagnoseRefactor(
        DiagKindRefactor::sema_use_expr_without_import, iae, PKG_NAME_DEVICE_INFO_AT, "IfAvailable");
    builder.AddNote("depend on declaration 'DeviceInfo'");
    return false;
}

bool ChkIfImportBase(DiagnosticEngine& diag, const ImportManager& im, const IfAvailableExpr& iae)
{
    if (iae.GetFullPackageName() == PKG_NAME_CANIUSE_AT) {
        return true;
    }
    auto importedPkgs = im.GetAllImportedPackages();
    for (auto& importedPkg : importedPkgs) {
        if (importedPkg->srcPackage && importedPkg->srcPackage->fullPackageName == PKG_NAME_CANIUSE_AT) {
            return true;
        }
    }
    auto builder =
        diag.DiagnoseRefactor(DiagKindRefactor::sema_use_expr_without_import, iae, PKG_NAME_CANIUSE_AT, "IfAvailable");
    builder.AddNote("depend on declaration 'canIUse'");
    return false;
}
} // namespace

bool TypeChecker::TypeCheckerImpl::ChkIfAvailableExpr(ASTContext& ctx, Ty& ty, IfAvailableExpr& ie)
{
    auto exprTy = SynIfAvailableExpr(ctx, ie);
    if (!Ty::IsTyCorrect(exprTy)) {
        return false;
    }
    if (!typeManager.IsSubtype(exprTy, &ty)) {
        Sema::DiagMismatchedTypes(diag, ie, ty);
        return false;
    }
    return true;
}

Ptr<Ty> TypeChecker::TypeCheckerImpl::SynIfAvailableExpr(ASTContext& ctx, IfAvailableExpr& iae)
{
    // Desugar before type checker.
    auto ie = DynamicCast<IfExpr>(iae.desugarExpr.get());
    if (!ie) {
        return typeManager.GetInvalidTy();
    }
    bool res{true};
    auto argName = iae.GetArg()->name;
    if (argName.Empty()) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_ifavailable_arg_no_name, *iae.GetArg());
        res = false;
    }
    if (argName == LEVEL_IDENTGIFIER && ie->condExpr) {
        res = ChkIfImportDeviceInfo(diag, importManager, iae) && res;
        CJC_ASSERT(ie->condExpr->astKind == ASTKind::BINARY_EXPR);
        auto argExpr = StaticCast<BinaryExpr>(ie->condExpr.get())->rightExpr.get();
        if (argExpr->astKind != ASTKind::LIT_CONST_EXPR) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_ifavailable_arg_not_literal, *iae.GetArg());
            res = false;
        }
    } else if (argName == SYSCAP_IDENTGIFIER && ie->condExpr) {
        res = ChkIfImportBase(diag, importManager, iae) && res;
        CJC_ASSERT(ie->condExpr->astKind == ASTKind::CALL_EXPR);
        auto argExpr = StaticCast<CallExpr>(ie->condExpr.get());
        CJC_ASSERT(argExpr->args.size() == 1);
        if (argExpr->args[0]->expr->astKind != ASTKind::LIT_CONST_EXPR) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_ifavailable_arg_not_literal, *iae.GetArg());
            res = false;
        }
    } else {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_ifavailable_unknow_arg_name, MakeRange(iae.GetArg()->name),
            iae.GetArg()->name.Val());
        res = false;
    }
    auto targetTy = typeManager.GetFunctionTy({}, typeManager.GetPrimitiveTy(TypeKind::TYPE_UNIT));
    res = Check(ctx, targetTy, iae.GetLambda1()) && res;
    res = Check(ctx, targetTy, iae.GetLambda2()) && res;
    if (!res) {
        iae.ty = typeManager.GetInvalidTy();
        return iae.ty;
    }
    iae.ty = Synthesize(ctx, iae.desugarExpr);
    return iae.ty;
}
