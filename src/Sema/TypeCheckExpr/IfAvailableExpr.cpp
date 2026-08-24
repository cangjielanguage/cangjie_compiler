// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "Desugar/DesugarInTypeCheck.h"
#include "Diags.h"
#include "TypeCheckerImpl.h"
#include "../Plugin/APILevelVersion.h"

using namespace Cangjie;
using namespace AST;

namespace {
const std::string LEVEL_IDENTGIFIER = "level";
const std::string SYSCAP_IDENTGIFIER = "syscap";
// For level check:
const std::string PKG_NAME_DEVICE_INFO_AT = "ohos.device_info";
// For syscap check:
const std::string PKG_NAME_CANIUSE_AT = "ohos.base";

bool IsValidIfAvailableLevelLiteral(const Expr& expr)
{
    auto lce = DynamicCast<LitConstExpr>(&expr);
    if (!lce) {
        return false;
    }
    if (lce->kind == LitConstKind::INTEGER) {
        return true;
    }
    if (lce->kind != LitConstKind::STRING) {
        return false;
    }
    return APILevelVersion::ParseChecked(lce->stringValue, APILevelVersion::ParseRule::TRIPLE_ONLY).has_value();
}

bool IsLiteralLevelCondition(const Expr& condExpr)
{
    if (condExpr.astKind == ASTKind::CALL_EXPR) {
        auto callExpr = StaticCast<CallExpr>(&condExpr);
        return callExpr->args.size() == 1 && callExpr->args[0]->expr &&
            callExpr->args[0]->expr->astKind == ASTKind::LIT_CONST_EXPR;
    }
    if (condExpr.astKind != ASTKind::BINARY_EXPR) {
        return false;
    }
    auto binaryExpr = StaticCast<BinaryExpr>(&condExpr);
    if (binaryExpr->op == TokenKind::AND) {
        return binaryExpr->leftExpr && binaryExpr->rightExpr && IsLiteralLevelCondition(*binaryExpr->leftExpr) &&
            IsLiteralLevelCondition(*binaryExpr->rightExpr);
    }
    return binaryExpr->rightExpr && binaryExpr->rightExpr->astKind == ASTKind::LIT_CONST_EXPR;
}

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

/// Aggregates the two mutable outputs of the IfAvailable argument checkers,
/// keeping function parameter counts within the G.FUN.01 threshold of 5.
struct IfAvailableCheckState {
    bool res{true};
    bool hasHardError{false};
};

/// Validate the `level:` argument and update state.
void ChkLevelArgument(DiagnosticEngine& diag, const ImportManager& importManager,
    IfAvailableExpr& iae, const IfExpr& ie, IfAvailableCheckState& state)
{
    // 'apiAvailable' lives in the same 'ohos.device_info' package as DeviceInfo,
    // so ChkIfImportDeviceInfo covers both desugar targets in one check.
    auto hasDeviceInfoImport = ChkIfImportDeviceInfo(diag, importManager, iae);
    state.res = hasDeviceInfoImport && state.res;
    state.hasHardError = !hasDeviceInfoImport || state.hasHardError;
    auto argExpr = iae.GetArg()->expr.get();
    if (!argExpr || !IsValidIfAvailableLevelLiteral(*argExpr)) {
        auto lce = DynamicCast<LitConstExpr>(argExpr);
        if (lce && lce->kind == LitConstKind::STRING) {
            diag.DiagnoseRefactor(
                DiagKindRefactor::sema_apilevel_invalid_version_format, *iae.GetArg(), lce->stringValue.c_str());
            state.hasHardError = true;
        } else {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_ifavailable_arg_not_literal, *iae.GetArg());
            state.hasHardError = true;
        }
        state.res = false;
    }
    if (!IsLiteralLevelCondition(*ie.condExpr)) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_ifavailable_arg_not_literal, *iae.GetArg());
        state.res = false;
        state.hasHardError = true;
    }
}

/// Validate the `syscap:` argument and update state.
void ChkSyscapArgument(DiagnosticEngine& diag, const ImportManager& importManager,
    IfAvailableExpr& iae, const IfExpr& ie, IfAvailableCheckState& state)
{
    auto hasBaseImport = ChkIfImportBase(diag, importManager, iae);
    state.res = hasBaseImport && state.res;
    state.hasHardError = !hasBaseImport || state.hasHardError;
    CJC_ASSERT(ie.condExpr->astKind == ASTKind::CALL_EXPR);
    auto argExpr = StaticCast<CallExpr>(ie.condExpr.get());
    CJC_ASSERT(argExpr->args.size() == 1);
    if (argExpr->args[0]->expr->astKind != ASTKind::LIT_CONST_EXPR) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_ifavailable_arg_not_literal, *iae.GetArg());
        state.res = false;
        state.hasHardError = true;
    }
}
} // namespace

bool TypeChecker::TypeCheckerImpl::ChkIfAvailableExpr(ASTContext& ctx, ModalTy ty, IfAvailableExpr& ie)
{
    auto exprTy = SynIfAvailableExpr(ctx, ie);
    if (!exprTy.IsCorrect()) {
        return false;
    }
    if (!typeManager.IsSubtype(exprTy, ty)) {
        Sema::DiagMismatchedTypes(diag, ie, ty);
        return false;
    }
    return true;
}

ModalTy TypeChecker::TypeCheckerImpl::SynIfAvailableExpr(ASTContext& ctx, IfAvailableExpr& iae)
{
    // Desugar before type checker.
    auto ie = DynamicCast<IfExpr>(iae.desugarExpr.get());
    if (!ie) {
        return {TypeManager::GetInvalidTy()};
    }
    bool res{true};
    IfAvailableCheckState state;
    auto argName = iae.GetArg()->name;
    if (argName.Empty()) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_ifavailable_arg_no_name, *iae.GetArg());
        state.res = false;
        state.hasHardError = true;
    }
    if (argName == LEVEL_IDENTGIFIER && ie->condExpr) {
        ChkLevelArgument(diag, importManager, iae, *ie, state);
    } else if (argName == SYSCAP_IDENTGIFIER && ie->condExpr) {
        ChkSyscapArgument(diag, importManager, iae, *ie, state);
    } else {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_ifavailable_unknow_arg_name, MakeRange(iae.GetArg()->name),
            iae.GetArg()->name.Val());
        state.res = false;
        state.hasHardError = true;
    }
    ModalTy lambdaFnTy{typeManager.GetFunctionTy({}, ModalTy{typeManager.GetPrimitiveTy(TypeKind::TYPE_UNIT)})};
    res = Check(ctx, lambdaFnTy, iae.GetLambda1()) && res;
    res = Check(ctx, lambdaFnTy, iae.GetLambda2()) && res;
    if (!res) {
        iae.SetTy({typeManager.GetInvalidTy()});
        return iae.GetTy();
    }
    iae.SetTy(Synthesize({ctx, SynPos::EXPR_ARG}, iae.desugarExpr));
    return iae.GetTy();
}
