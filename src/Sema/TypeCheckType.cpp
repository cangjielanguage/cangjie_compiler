// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the TypeChecker related classes.
 */

#include "TypeCheckerImpl.h"

#include "TypeCheckUtil.h"

#include "cangjie/AST/Clone.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Basic/Print.h"

using namespace Cangjie;
using namespace TypeCheckUtil;
using namespace AST;

void TypeChecker::TypeCheckerImpl::CheckReferenceTypeLegality(ASTContext& ctx, Type& t)
{
    if (t.TestAttr(Attribute::TOOL_ADD)) {
        return;
    }
    auto typeArgs = t.GetTypeArgs();
    for (auto& it : typeArgs) {
        CheckReferenceTypeLegality(ctx, *it);
    }
    switch (t.astKind) {
        case ASTKind::REF_TYPE:
            CheckRefType(ctx, *StaticAs<ASTKind::REF_TYPE>(&t));
            break;
        case ASTKind::QUALIFIED_TYPE:
            CheckQualifiedType(ctx, *StaticAs<ASTKind::QUALIFIED_TYPE>(&t));
            break;
        case ASTKind::TUPLE_TYPE:
            CheckTupleType(ctx, *StaticAs<ASTKind::TUPLE_TYPE>(&t));
            break;
        case ASTKind::FUNC_TYPE:
            CheckFuncType(ctx, *StaticAs<ASTKind::FUNC_TYPE>(&t));
            break;
        case ASTKind::PAREN_TYPE:
            CheckReferenceTypeLegality(ctx, *StaticAs<ASTKind::PAREN_TYPE>(&t)->type);
            break;
        case ASTKind::OPTION_TYPE:
            CheckOptionType(ctx, *StaticAs<ASTKind::OPTION_TYPE>(&t));
            break;
        case ASTKind::VARRAY_TYPE:
            CheckVArrayType(ctx, *StaticAs<ASTKind::VARRAY_TYPE>(&t));
            break;
        default:
            break;
    }
    // Check all related type's sema ty, udpate current node's ty to invalid, if any of them invalid.
    bool hasInvalidTy = !t.GetTy().IsCorrect() ||
        std::any_of(typeArgs.begin(), typeArgs.end(), [](auto& it) { return !it->GetTy().IsCorrect(); });
    if (hasInvalidTy) {
        t.SetTy({TypeManager::GetInvalidTy()});
    }
}

void TypeChecker::TypeCheckerImpl::CheckOptionType(ASTContext& ctx, const OptionType& ot)
{
    if (ot.desugarType) {
        CheckReferenceTypeLegality(ctx, *ot.desugarType);
    }
}

std::tuple<bool, std::string> TypeChecker::TypeCheckerImpl::CheckVArrayWithRefType(
    ModalTy ty, std::unordered_set<ModalTy>& traversedTy)
{
    if (ty->IsStructArray() || ty->IsClassLike() || ty->IsArray() || ty->IsEnum() || ty->IsGeneric() ||
        (ty->IsFunc() && !ty->IsCFunc())) {
        return {true, ty.String()};
    }
    if (traversedTy.count(ty) != 0) {
        return {false, ""};
    }
    (void)traversedTy.insert(ty);
    if (auto sd = DynamicCast<StructDecl*>(Ty::GetDeclPtrOfTy(ty))) {
        auto typeMapping = promotion.GetPromoteTypeMapping(ty.Ty(), sd->DataTy());
        for (auto& decl : sd->GetMemberDecls()) {
            if (decl->TestAttr(Attribute::STATIC) || decl->astKind != ASTKind::VAR_DECL) {
                continue;
            }
            auto memberTy = typeManager.GetBestInstantiatedTy(decl->GetTy(), typeMapping);
            auto [needReport, reportType] = CheckVArrayWithRefType(memberTy, traversedTy);
            if (needReport) {
                return {needReport, reportType};
            }
        }
    } else if (ty->IsTuple()) {
        for (const auto& typeArg : ty->typeArgs) {
            auto [needReport, reportType] = CheckVArrayWithRefType(typeArg, traversedTy);
            if (needReport) {
                return {needReport, reportType};
            }
        }
    }
    // CPointer<T> and CString are always allowed.
    return {false, ""};
}

void TypeChecker::TypeCheckerImpl::CheckVArrayType(ASTContext& ctx, const VArrayType& vt)
{
    Synthesize({ctx, SynPos::NONE}, vt.typeArgument.get());
    if (auto ct = DynamicCast<ConstantType*>(vt.constantType.get()); ct) {
        ct->SetTy(Synthesize({ctx, SynPos::NONE}, ct->constantExpr.get()));
    }
    // The runtime gc cannot manage data of type llvm::array, so it cannot use a reference type as its element type,
    // which is temporarily prohibited by semantics.
    auto typeArgTy = vt.typeArgument->GetTy();
    if (!typeArgTy.IsCorrect()) {
        return;
    }
    std::unordered_set<ModalTy> traversedTy = {};
    auto [isReferenceType, type] = CheckVArrayWithRefType(typeArgTy, traversedTy);
    if (isReferenceType) {
        auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_varray_arg_type_with_reftype,
            *vt.typeArgument, typeArgTy.String());
        builder.AddMainHintArguments(type);
    }
}

bool TypeChecker::TypeCheckerImpl::IsGenericTypeWithTypeArgs(AST::Type& type) const
{
    if (auto rt = DynamicCast<RefType*>(&type); rt && rt->IsGenericThisType()) {
        return true;
    }
    auto target = type.GetTarget();
    if (!target) {
        return true; // If current type do not have target, return true for ignore current check.
    }
    auto generic = target->GetGeneric();
    // Type must have typeArguments when its target is a generic type declaration.
    return !(type.GetTypeArgs().empty() && generic != nullptr);
}

bool TypeChecker::TypeCheckerImpl::CheckTypeParametersForAliasRef(AST::RefType& rt, const AST::TypeAliasDecl& aliasDecl)
{
    auto aliasDeclGenericTypeParametersNum = aliasDecl.generic ? aliasDecl.generic->typeParameters.size() : 0;
    if (rt.typeArguments.size() != aliasDeclGenericTypeParametersNum) {
        auto range = MakeRange(rt.begin, rt.end.IsZero() ? rt.begin + 1 : rt.end);
        diag.DiagnoseRefactor(DiagKindRefactor::sema_generic_argument_no_match, rt, range);
        return false;
    }
    return true;
}

void TypeChecker::TypeCheckerImpl::HandleAliasForRefType(RefType& rt, Ptr<Decl>& target)
{
    if (!target || target->astKind != ASTKind::TYPE_ALIAS_DECL) {
        return;
    }
    auto aliasDecl = StaticCast<TypeAliasDecl*>(target);
    if (aliasDecl->type == nullptr) {
        return;
    }

    if (!CheckTypeParametersForAliasRef(rt, *aliasDecl)) {
        return;
    }
    auto innerTypeAliasTarget = GetLastTypeAliasTarget(*aliasDecl);
    if (auto realTarget = innerTypeAliasTarget->type->GetTarget(); realTarget) {
        target = realTarget;
        auto typeMapping = GenerateTypeMappingForTypeAliasUse(*aliasDecl, rt);
        SubstituteTypeArguments(*innerTypeAliasTarget, rt.typeArguments, typeMapping);
    }
}

namespace {
void BackupTypes(std::vector<OwnedPtr<Type>>& src, std::vector<OwnedPtr<Type>>& backup)
{
    for (auto& it : src) {
        backup.push_back(std::move(it));
    }
    src.clear();
    for (auto& it : backup) {
        src.push_back(AST::ASTCloner::Clone(it.get()));
        src.back()->DisableAttr(Attribute::COMPILER_ADD); // to avoid being skipped in SubstituteTypeArguments
    }
}

void RestoreTypes(std::vector<OwnedPtr<Type>>& src, std::vector<OwnedPtr<Type>>& backup)
{
    src.clear();
    for (auto& it : backup) {
        src.push_back(std::move(it));
    }
    backup.clear();
}
}

void TypeChecker::TypeCheckerImpl::CheckRefTypeWithRealTarget(RefType& rt)
{
    std::vector<OwnedPtr<Type>> backup;
    BackupTypes(rt.typeArguments, backup);
    auto target = rt.ref.target;
    auto realTarget = target;
    HandleAliasForRefType(rt, realTarget);
    auto typeArgs = rt.GetTypeArgs();
    if (!realTarget || typeArgs.empty()) {
        RestoreTypes(rt.typeArguments, backup);
        return;
    }
    if (auto aliasTarget = DynamicCast<TypeAliasDecl*>(target)) {
        std::vector<ModalTy> diffs = GetUnusedTysInTypeAlias(*aliasTarget);
        Utils::EraseIf(typeArgs, [&diffs](auto type) { return Utils::In(type->GetTy(), diffs); });
    }
    if (!CheckGenericDeclInstantiation(realTarget, typeArgs, rt)) {
        // Do not clear type target when constraints mismatched.
        // Guarantees constraint error can be thrown when re-checking.
        rt.SetTy({TypeManager::GetInvalidTy()});
    }
    RestoreTypes(rt.typeArguments, backup);
}

void TypeChecker::TypeCheckerImpl::CheckRefType(ASTContext& ctx, RefType& rt)
{
    auto target = rt.ref.target;
    // If no target found in name resolution stage, return directly.
    if (!target) {
        return; // 'PreCheck' will not set non typeDecl to target, eg: refType may be name of package for pkg.Type.
    }
    // Get field target and check access legality.
    if (!CheckRefTypeCheckAccessLegality(ctx, rt, *target)) {
        return;
    }
    if (auto ref = DynamicCast<BuiltInDecl>(&*target); ref && ref->type == BuiltInType::CFUNC) {
        CheckCFuncType(ctx, rt);
        return;
    }
    // Type must have typeArguments when its target is a generic type declaration.
    if (!IsGenericTypeWithTypeArgs(rt)) {
        diag.Diagnose(rt, DiagKind::sema_generic_type_without_type_argument);
        // Unbind target-user relationship when setting type to invalid.
        rt.SetTy({TypeManager::GetInvalidTy()});
        return;
    }
    // Returns true if further checks can be omitted.
    if (CheckRefExprCheckTyArgs(rt, *target)) {
        return;
    }
    CheckRefTypeWithRealTarget(rt);
}

void TypeChecker::TypeCheckerImpl::CheckCFuncType(ASTContext& ctx, const RefType& rt)
{
    (void)ctx;
    auto args = rt.GetTypeArgs();
    // do not check invalid CFunc type.
    if (args.size() != 1) {
        return;
    }
    auto arg = DynamicCast<FuncType>(args[0]);
    if (!arg) {
        return;
    }
    for (auto& it : arg->paramTypes) {
        CheckCFuncParamType(*it);
    }
    if (!arg->retType->GetTy().IsCorrect()) {
        return;
    }
    CheckCFuncReturnType(*arg->retType);
}

bool TypeChecker::TypeCheckerImpl::CheckRefExprCheckTyArgs(const RefType& rt, const Decl& target)
{
    bool skipFurther = false;
    if (rt.typeArguments.empty()) {
        skipFurther = true;
    }
    for (auto& type : rt.typeArguments) {
        if (!HasJavaAttr(target)) {
            continue;
        }
        if (auto prt = DynamicCast<RefType*>(type.get()); prt && prt->ref.target) {
            auto tg = TypeCheckUtil::GetRealTarget(prt->ref.target);
            if (tg && (HasJavaAttr(*tg) || tg->astKind == ASTKind::GENERIC_PARAM_DECL)) {
                continue;
            }
        } else if (auto qt = DynamicCast<QualifiedType*>(type.get()); qt && qt->target) {
            auto tgt = TypeCheckUtil::GetRealTarget(qt->target);
            if (tgt && HasJavaAttr(*tgt)) {
                continue;
            }
        }
        skipFurther = true;
        break;
    }
    return skipFurther;
}

bool TypeChecker::TypeCheckerImpl::CheckRefTypeCheckAccessLegality(
    const ASTContext& ctx, RefType& rt, const Decl& target)
{
    auto sym = ScopeManager::GetCurSymbolByKind(SymbolKind::STRUCT, ctx, rt.scopeName);
    if (!IsLegalAccess(sym, target, rt, importManager, typeManager)) {
        diag.Diagnose(rt, DiagKind::sema_invalid_access_control, target.identifier.Val());
        // Unbind target-user relationship when error happens.
        ReplaceTarget(&rt, nullptr);
        rt.SetTy({TypeManager::GetInvalidTy()});
        return false;
    }
    return true;
}

void TypeChecker::TypeCheckerImpl::CheckTupleType(ASTContext& ctx, TupleType& tt)
{
    for (auto& it : tt.fieldTypes) {
        CJC_NULLPTR_CHECK(it);
        Synthesize({ctx, SynPos::NONE}, it.get());
        if (it->DataTy() && it->GetTy()->IsCTypeConstraint()) {
            diag.Diagnose(*it, DiagKind::sema_invalid_tuple_field_ctype);
            return;
        }
    }
}

void TypeChecker::TypeCheckerImpl::CheckFuncType(ASTContext& ctx, FuncType& ft)
{
    for (auto& it : ft.paramTypes) {
        CheckReferenceTypeLegality(ctx, *it);
    }
    CJC_NULLPTR_CHECK(ft.retType);
    CheckReferenceTypeLegality(ctx, *ft.retType);
    if (!ft.isC) {
        return;
    }
    for (auto& it : ft.paramTypes) {
        CheckCFuncParamType(*it);
    }
    if (!ft.retType->GetTy().IsCorrect()) {
        return;
    }
    CheckCFuncReturnType(*ft.retType);
}

void TypeChecker::TypeCheckerImpl::CheckCFuncReturnType(const Type& type)
{
    if (!type.GetTy()->IsMetCType()) {
        auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_invalid_cfunc_return_type, type);
        builder.AddNote("return type is " + type.GetTy().String());
    } else if (Is<VArrayTy>(type.DataTy())) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_varray_in_cfunc, type);
    }
}

void TypeChecker::TypeCheckerImpl::CheckQualifiedType(const ASTContext& ctx, QualifiedType& qt)
{
    auto target = qt.target;
    // If no target found in name resolution stage, return directly.
    if (target == nullptr) {
        // 'PreCheck' will not set non typeDecl to target
        // eg: refType may be name of package for pkg.Type or pkga.pkgb.Type.
        return;
    }
    // Get field target and check access legality.
    auto sym = ScopeManager::GetCurSymbolByKind(SymbolKind::STRUCT, ctx, qt.scopeName);
    if (!IsLegalAccess(sym, *target, qt, importManager, typeManager)) {
        diag.Diagnose(qt, DiagKind::sema_invalid_access_control, target->identifier.Val());
    }
    // Type must have typeArguments when its target is a generic type declaration.
    if (!IsGenericTypeWithTypeArgs(qt)) {
        diag.Diagnose(qt, DiagKind::sema_generic_type_without_type_argument);
        // Unbind target-user relationship when setting type to invalid.
        ReplaceTarget(&qt, nullptr);
        qt.SetTy({TypeManager::GetInvalidTy()});
        return;
    }
    if (!qt.GetTy().IsCorrect() || qt.typeArguments.empty()) {
        return;
    }
    if (!CheckGenericDeclInstantiation(target, qt.GetTypeArgs(), qt)) {
        // Do not clear type target when constraints mismatched.
        // Guarantees constraint error can be thrown when re-checking.
        qt.SetTy({TypeManager::GetInvalidTy()});
    }
}
