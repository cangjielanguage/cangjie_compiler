// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements typecheck apis for array exprs.
 */

#include "TypeCheckerImpl.h"

#include <algorithm>
#include <list>

#include "DiagSuppressor.h"
#include "Diags.h"
#include "JoinAndMeet.h"
#include "TypeCheckUtil.h"

#include "cangjie/AST/ASTContext.h"
#include "cangjie/AST/Clone.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/RecoverDesugar.h"
#include "cangjie/AST/Symbol.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Basic/Match.h"
#include "cangjie/Sema/TypeManager.h"

using namespace Cangjie;
using namespace Sema;

namespace {
Ptr<Ty> GetArrayElementTy(TypeManager& typeManager, ArrayTy& arrayTy)
{
    // Array type parser guarantees 'dims' value at least 1.
    if (arrayTy.dims == 1) {
        return arrayTy.typeArgs[0];
    }
    return typeManager.GetArrayTy(arrayTy.typeArgs[0], arrayTy.dims - 1);
}

bool HasInheritGivenDecl(const std::unordered_set<Ptr<AST::Ty>>& superTys, const ClassLikeDecl& decl)
{
    for (auto ty : superTys) {
        CJC_NULLPTR_CHECK(ty);
        if (ty->kind == TypeKind::TYPE_INTERFACE) {
            if (RawStaticCast<InterfaceTy*>(ty)->declPtr == &decl) {
                return true;
            }
        } else {
            if (RawStaticCast<ClassTy*>(ty)->declPtr == &decl) {
                return true;
            }
        }
    }
    return false;
}
} // namespace

Ptr<Ty> TypeChecker::TypeCheckerImpl::GetArrayTypeByInterface(Ty& interfaceTy)
{
    Ptr<Ty> invalid = TypeManager::GetInvalidTy();
    auto arrayStruct = importManager.GetCoreDecl<StructDecl>("Array");
    if (!arrayStruct) {
        return invalid;
    }
    auto arrTys = promotion.Downgrade(*arrayStruct->GetTy(), interfaceTy);
    if (arrTys.empty()) {
        return invalid;
    } else {
        return *arrTys.begin();
    }
}

bool TypeChecker::TypeCheckerImpl::ChkArrayLit(ASTContext& ctx, Ty& target, ArrayLit& al)
{
    auto targetTy = TypeCheckUtil::UnboxOptionType(&target);
    // Set type first, if check succeed, type will be updated.
    al.SetTy(TypeManager::GetInvalidTy());
    if (targetTy->IsInterface()) {
        targetTy = GetArrayTypeByInterface(*targetTy);
        if (targetTy->IsInvalid()) {
            // If failed to get valid array type, there are two cases:
            // 1. array lit is empty, type is unable to be inferred.
            // 2. array lit is not empty, synthesize arrayLit 's type and check with target.
            if (al.children.empty()) {
                diag.Diagnose(al, DiagKind::sema_empty_arrayLit_type_undefined);
                return false;
            }
            auto arrayLitTy = SynArrayLit(ctx, al);
            if (typeManager.IsSubtype(arrayLitTy, &target)) {
                return true;
            }
            if (Ty::IsTyCorrect(arrayLitTy)) {
                DiagMismatchedTypes(diag, al, target);
            }
            return false;
        }
    } else if (!Ty::IsTyCorrect(targetTy) || (!targetTy->IsStructArray() && !Is<VArrayTy>(targetTy))) {
        DiagMismatchedTypesWithFoundTy(diag, al, targetTy->String(), "Array");
        return false;
    }

    if (targetTy->typeArgs.empty()) {
        return false;
    }

    bool matched = true;
    auto vt = DynamicCast<VArrayTy*>(targetTy);
    if (vt && vt->size != static_cast<int64_t>(al.children.size())) {
        auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_varray_size_match, al);
        builder.AddMainHintArguments(std::to_string(vt->size), std::to_string(al.children.size()));
        matched = false;
    }

    Ptr<Ty> arrayElemTy = targetTy->typeArgs[0];
    for (auto& child : al.children) {
        if (!Check(ctx, arrayElemTy, child.get())) {
            matched = false;
            break;
        }
    }
    if (matched) {
        al.SetTy(targetTy);
    }
    return matched;
}

Ptr<Ty> TypeChecker::TypeCheckerImpl::SynArrayLit(ASTContext& ctx, ArrayLit& al)
{
    if (al.children.empty()) {
        diag.Diagnose(al, DiagKind::sema_empty_arrayLit_type_undefined);
        al.SetTy(TypeManager::GetInvalidTy());
        return al.GetTy();
    }
    std::set<Ptr<Ty>> arrayElemTys;
    bool hasInvalidElemTy = false;
    for (auto& child : al.children) {
        if (Synthesize({ctx, SynPos::EXPR_ARG}, child.get()) && !ReplaceIdealTy(*child)) {
            hasInvalidElemTy = true;
        }
        arrayElemTys.insert(child->GetTy());
    }

    auto arrayStruct = importManager.GetCoreDecl<StructDecl>("Array");
    if (hasInvalidElemTy || arrayStruct == nullptr) {
        // If there exists invalid element ty or 'core' package is not imported correctly,
        // error will be throwed in other process.
        al.SetTy(TypeManager::GetInvalidTy());
        return al.GetTy();
    }

    auto joinRes = JoinAndMeet(typeManager, arrayElemTys, {}, &importManager, al.curFile).JoinAsVisibleTy();
    if (auto ty = std::get_if<Ptr<Ty>>(&joinRes)) {
        al.SetTy(typeManager.GetStructTy(*arrayStruct, {*ty}));
    } else {
        al.SetTy(TypeManager::GetInvalidTy());
        auto errMsg = JoinAndMeet::CombineErrMsg(std::get<std::stack<std::string>>(joinRes));
        diag.Diagnose(al, DiagKind::sema_inconsistency_elemType, "array").AddNote(errMsg);
    }
    return al.GetTy();
}

// Caller guarantees ae. Array has 2 args.
bool TypeChecker::TypeCheckerImpl::ChkSizedArrayElement(ASTContext& ctx, Ty& elemTargetTy, ArrayExpr& ae)
{
    PData::CommitScope(typeManager.constraints);
    { // Create a scope for DiagSuppressor.
        auto ds = DiagSuppressor(diag);
        // Support for old array constructor, Array<T>(size, element: T).
        if (Check(ctx, &elemTargetTy, ae.args[1].get())) {
            ds.ReportDiag();
            ae.initFunc = nullptr; // When initialize array with given elememt, init function is needless.
            return true;
        }
    }
    PData::Reset(typeManager.constraints);
    // For new array constructor, Array<T>(size, element: (Int64)->T).
    auto sizeType = TypeManager::GetPrimitiveTy(TypeKind::TYPE_INT64);
    auto expectedExprTy = typeManager.GetFunctionTy({sizeType}, &elemTargetTy);
    if (Check(ctx, expectedExprTy, ae.args[1].get())) {
        return true;
    }
    diag.Diagnose(*ae.args[1], DiagKind::sema_array_expression_type_error);
    return false;
}

// Caller guarantees array type without component type 'RawArray(size, expr)'
bool TypeChecker::TypeCheckerImpl::ChkSizedArrayWithoutElemTy(ASTContext& ctx, Ty& target, ArrayExpr& ae)
{
    auto arrayTy = RawStaticCast<ArrayTy*>(ae.type->GetTy());
    auto sizeType = TypeManager::GetPrimitiveTy(TypeKind::TYPE_INT64);
    // There are two possible constructors:
    //     1. RawArray(size, (Int64)->T)
    //     2. RawArray(size, item: T)
    // When second argument has argument name, check ArrayExpr as case 2, and check name's legality later.
    // Otherwise check ArrayExpr as case 1.
    CJC_ASSERT(ae.args.size() >= 1);
    bool isArgLambda = ae.args[1]->name.Empty();
    if (!isArgLambda) {
        ae.initFunc = nullptr; // When initialize array with given element, init function is needless.
    }
    // Array as 'RawArray(size, expr)', need type inference. Type should match RawArray<T>(size, element: (Int64)->T).
    if (target.IsInvalid() || target.IsInterface()) {
        // No target type, synthesis expression type. Type must be (Int64)->T.
        Synthesize({ctx, SynPos::EXPR_ARG}, ae.args[1].get());
        ReplaceIdealTy(*ae.args[1]);
        ReplaceIdealTy(*ae.args[1]->expr);
        auto exprTy = ae.args[1]->GetTy();
        if (!Ty::IsTyCorrect(exprTy) || (isArgLambda && exprTy->kind != TypeKind::TYPE_FUNC)) {
            diag.Diagnose(*ae.args[0], DiagKind::sema_array_expression_type_error);
            return false;
        }
        auto elementTy = exprTy;
        if (isArgLambda) {
            // Constructor is RawArray<T>(Int64, (Int64)->T), so array type can be inferred by funcTy.
            auto funcTy = RawStaticCast<FuncTy*>(exprTy);
            if (funcTy->paramTys.size() != 1 || !typeManager.IsSubtype(funcTy->paramTys[0], sizeType)) {
                diag.Diagnose(*ae.args[0], DiagKind::sema_array_expression_param_type_error);
                return false;
            }
            elementTy = funcTy->retTy;
        }
        ae.SetTy(typeManager.GetArrayTy(elementTy, arrayTy->dims));
        return target.IsInvalid() || typeManager.IsSubtype(ae.GetTy(), &target);
    }
    // Target type is given, check with expression.
    auto elementTy = GetArrayElementTy(typeManager, static_cast<ArrayTy&>(target));
    auto expectedExprTy = isArgLambda ? typeManager.GetFunctionTy({sizeType}, elementTy) : elementTy;
    if (!Check(ctx, expectedExprTy, ae.args[1].get())) {
        diag.Diagnose(*ae.args[1], DiagKind::sema_array_element_type_error);
        return false;
    }
    ae.SetTy(&target);
    return true;
}

// Caller guarantees ae. Array has 2 args and target is arrayType or interfaceType.
bool TypeChecker::TypeCheckerImpl::ChkSizedArrayExpr(ASTContext& ctx, Ty& target, ArrayExpr& ae)
{
    auto arrayTy = RawStaticCast<ArrayTy*>(ae.type->GetTy());
    auto sizeType = TypeManager::GetPrimitiveTy(TypeKind::TYPE_INT64);
    if (!Check(ctx, sizeType, ae.args[0].get())) {
        diag.Diagnose(*ae.args[0], DiagKind::sema_array_size_type_error);
        return false;
    }
    if (ae.args[1] == nullptr) {
        return false;
    }
    ae.initFunc = importManager.GetCoreDecl<FuncDecl>("arrayInitByFunction");
    CJC_NULLPTR_CHECK(ae.initFunc);
    Synthesize({ctx, SynPos::EXPR_ARG}, ae.initFunc); // Non-public decl should synthesize manually.
    CJC_ASSERT(arrayTy && !arrayTy->typeArgs.empty());
    if (arrayTy->typeArgs[0]->IsInvalid()) {
        return ChkSizedArrayWithoutElemTy(ctx, target, ae);
    }
    // Check array elements with declared array type.
    Ptr<Ty> elemTy = GetArrayElementTy(typeManager, *arrayTy);
    bool ret = ChkSizedArrayElement(ctx, *elemTy, ae);
    if (!target.IsInvalid() && ret) {
        // Check declared array type with targetTy.Not support option box type.
        ret = typeManager.IsSubtype(arrayTy, &target, true, false);
        if (ret && target.IsArray()) {
            ae.SetTy(&target);
        }
    }
    return ret;
}

// Caller guarantees array has one arg and 'List/Collection' exists.
bool TypeChecker::TypeCheckerImpl::ChkSingeArgArrayWithoutElemTy(ASTContext& ctx, Ty& target, ArrayExpr& ae)
{
    auto collectionDecl = importManager.GetCoreDecl<InterfaceDecl>("Collection");
    CJC_NULLPTR_CHECK(collectionDecl);
    auto arrayTy = RawStaticCast<ArrayTy*>(ae.type->GetTy());
    // Array(List/Collection), need type inference.
    CJC_ASSERT(!ae.args.empty());
    auto exprTy = Synthesize({ctx, SynPos::EXPR_ARG}, ae.args[0].get());
    if (!Ty::IsTyCorrect(exprTy)) {
        diag.Diagnose(*ae.args[0], DiagKind::sema_array_single_element_type_error);
        return false;
    }
    auto superTys = typeManager.GetAllSuperTys(*exprTy);
    if (HasInheritGivenDecl(superTys, *collectionDecl)) {
        CJC_NULLPTR_CHECK(collectionDecl->GetTy());
        auto promotedTys = promotion.Promote(*exprTy, *collectionDecl->GetTy());
        auto collectionTy = *promotedTys.begin(); // Previous check guarantees 'promotedTys' has at least one value.
        ae.SetTy(typeManager.GetArrayTy(collectionTy->typeArgs[0], arrayTy->dims));
        ae.initFunc = importManager.GetCoreDecl<FuncDecl>("arrayInitByCollection");
        CJC_NULLPTR_CHECK(ae.initFunc);
        Synthesize({ctx, SynPos::EXPR_ARG}, ae.initFunc); // Non-public decl should synthesize manually.
    } else {
        diag.Diagnose(*ae.args[0], DiagKind::sema_array_single_element_type_error);
        return false;
    }
    return target.IsInvalid() || typeManager.IsSubtype(ae.GetTy(), &target);
}

bool TypeChecker::TypeCheckerImpl::ChkSingeArgArrayExpr(ASTContext& ctx, Ty& target, ArrayExpr& ae)
{
    auto collectionDecl = importManager.GetCoreDecl<InterfaceDecl>("Collection");
    if (collectionDecl == nullptr) {
        return false;
    }

    auto arrayTy = RawStaticCast<ArrayTy*>(ae.type->GetTy());
    if (arrayTy->typeArgs[0]->IsInvalid()) {
        return ChkSingeArgArrayWithoutElemTy(ctx, target, ae);
    }

    auto elemTy =
        (arrayTy->dims > 1) ? typeManager.GetArrayTy(arrayTy->typeArgs[0], arrayTy->dims - 1) : arrayTy->typeArgs[0];
    // Check for Array<T>(Collection).
    auto collectionTy = typeManager.GetInterfaceTy(*collectionDecl, {elemTy});
    if (Check(ctx, collectionTy, ae.args[0].get())) {
        ae.initFunc = importManager.GetCoreDecl<FuncDecl>("arrayInitByCollection");
        CJC_NULLPTR_CHECK(ae.initFunc);
        Synthesize({ctx, SynPos::EXPR_ARG}, ae.initFunc); // Non-public decl should synthesize manually.
        return target.IsInvalid() || typeManager.IsSubtype(ae.GetTy(), &target);
    }
    diag.Diagnose(*ae.args[0], DiagKind::sema_array_single_element_type_error);
    return false;
}

bool TypeChecker::TypeCheckerImpl::ChkArrayExpr(ASTContext& ctx, Ty& target, ArrayExpr& ae)
{
    CJC_NULLPTR_CHECK(ae.type);
    Ptr<Ty> targetTy = TypeCheckUtil::UnboxOptionType(&target);
    // Target type must be array/interface type or option boxed array/interface type.
    bool isWellTyped = Ty::IsTyCorrect(targetTy) && (targetTy->IsArray() || targetTy->IsInterface());
    if (!isWellTyped) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_mismatched_types, ae)
            .AddMainHintArguments(targetTy->String(), "Array");
        ae.SetTy(TypeManager::GetNonNullTy(ae.GetTy()));
        return false;
    }
    ae.type->SetTy(Synthesize({ctx, SynPos::EXPR_ARG}, ae.type.get()));
    if (ae.type->GetTy() == nullptr || !ae.type->GetTy()->IsArray()) {
        ae.SetTy(TypeManager::GetNonNullTy(ae.GetTy()));
        return false;
    }
    ae.SetTy(ae.type->GetTy());
    auto arrayTy = RawStaticCast<ArrayTy*>(ae.type->GetTy());
    bool ret = true;
    if (ae.args.empty()) {
        if (!arrayTy->typeArgs[0]->IsInvalid()) {
            ret = typeManager.IsSubtype(ae.GetTy(), targetTy, true, false);
        }
        ae.SetTy(target.IsInterface() ? arrayTy : targetTy);
    } else if (ae.args.size() == 1) {
        ret = ChkSingeArgArrayExpr(ctx, *targetTy, ae);
    } else if (ae.args.size() == 2) { // Array init with 2 elements, size & initExpr.
        ret = ChkSizedArrayExpr(ctx, *targetTy, ae);
    } else {
        diag.Diagnose(ae, DiagKind::sema_array_too_much_argument);
        ret = false;
    }
    if (Ty::IsTyCorrect(ae.GetTy()) && !Ty::IsTyCorrect(ae.type->GetTy())) {
        ae.type->SetTy(ae.GetTy()); // Update array type ty, overwrite Array<Invalid>.
    }
    ChkArrayArgs(ae);
    return ret;
}

bool TypeChecker::TypeCheckerImpl::ChkVArrayArg(ASTContext& ctx, ArrayExpr& ve)
{
    // check arg.
    if (ve.args.size() != 1) {
        // Align the range computation with DiagWrongNumberOfArgumentsCommon: fall back
        // to the node range when paren positions are zero (e.g. a desugared ArrayExpr
        // whose producer forgot to copy them), and extend the end to the last argument
        // when it is a trailing lambda placed after ')'.
        bool parenPosExist = !ve.leftParenPos.IsZero() && !ve.rightParenPos.IsZero();
        auto beginPos = parenPosExist ? ve.leftParenPos : ve.begin;
        bool lastArgAfterParen = !ve.args.empty() && ve.args.back() && ve.args.back()->end > ve.rightParenPos;
        auto endPos = parenPosExist ? (lastArgAfterParen ? ve.args.back()->end : ve.rightParenPos + 1) : ve.end;
        diag.DiagnoseRefactor(
            DiagKindRefactor::sema_varray_args_number_mismatch, ve, MakeRange(beginPos, endPos));
        return false;
    }
    bool ret = false;
    if (ve.args[0]->name.Empty()) {
        // For Lambda.
        auto sizeType = TypeManager::GetPrimitiveTy(TypeKind::TYPE_INT64);
        auto expectedExprTy = typeManager.GetFunctionTy({sizeType}, ve.type->GetTy()->typeArgs[0]);
        ret = Check(ctx, expectedExprTy, ve.args[0].get());
    } else {
        if (ve.args[0]->name != "repeat") {
            auto builder = diag.Diagnose(*ve.args[0], DiagKind::sema_unknown_named_argument, ve.args[0]->name.Val());
            builder.AddNote("expect the name of the named parameter is 'item'");
            return false;
        }
        // For item: T
        auto expectedItemTy = ve.type->GetTy()->typeArgs[0];
        ret = Check(ctx, expectedItemTy, ve.args[0].get());
    }

    if (Ty::IsTyCorrect(ve.GetTy()) && !Ty::IsTyCorrect(ve.type->GetTy())) {
        ve.type->SetTy(ve.GetTy()); // Update array type ty, overwrite Array<Invalid>.
    }
    return ret;
}

bool TypeChecker::TypeCheckerImpl::ChkVArrayExpr(ASTContext& ctx, Ty& target, ArrayExpr& ve)
{
    CJC_NULLPTR_CHECK(ve.type);
    Ptr<Ty> targetTy = TypeCheckUtil::UnboxOptionType(&target);
    ve.type->SetTy(Synthesize({ctx, SynPos::EXPR_ARG}, ve.type.get()));
    if (!Ty::IsTyCorrect(ve.type->GetTy()) || !Is<VArrayTy>(ve.type->GetTy())) {
        ve.SetTy(TypeManager::GetInvalidTy());
        return false;
    }
    if (!ChkVArrayArg(ctx, ve)) {
        ve.SetTy(TypeManager::GetInvalidTy());
        return false;
    }
    // check T and size.
    if (!typeManager.IsSubtype(ve.type->GetTy(), targetTy)) {
        // When the desugared ArrayExpr has sourceExpr, ShouldDiagnose returns false
        // and the mismatch diagnostic is silently skipped. Fall back to sourceExpr
        // to ensure the error is reported on the original CallExpr.
        const Node& diagNode = ve.sourceExpr ? *ve.sourceExpr : ve;
        DiagMismatchedTypesWithFoundTy(diag, diagNode, targetTy->String(), ve.type->GetTy()->String());
        ve.SetTy(TypeManager::GetInvalidTy());
        return false;
    }
    ve.SetTy(ve.type->GetTy());
    return true;
}

Ptr<Ty> TypeChecker::TypeCheckerImpl::SynArrayExpr(ASTContext& ctx, ArrayExpr& ae)
{
    CJC_NULLPTR_CHECK(ae.type);
    ae.type->SetTy(Synthesize({ctx, SynPos::EXPR_ARG}, ae.type.get()));
    if (ae.type->GetTy() == nullptr || !ae.type->GetTy()->IsArray()) {
        ae.SetTy(TypeManager::GetInvalidTy());
        return ae.GetTy();
    }
    ae.SetTy(ae.type->GetTy());
    auto arrayTy = RawStaticCast<ArrayTy*>(ae.type->GetTy());
    bool checkRet = true;
    if (ae.args.empty()) {
        if (arrayTy->typeArgs[0]->IsInvalid()) {
            diag.Diagnose(ae, DiagKind::sema_empty_arrayLit_type_undefined);
            checkRet = false;
        }
    } else if (ae.args.size() == 1) {
        checkRet = ChkSingeArgArrayExpr(ctx, *TypeManager::GetInvalidTy(), ae);
    } else if (ae.args.size() == 2) { // Array init with 2 elements, size & initExpr.
        checkRet = ChkSizedArrayExpr(ctx, *TypeManager::GetInvalidTy(), ae);
    } else {
        diag.Diagnose(ae, DiagKind::sema_array_too_much_argument);
        checkRet = false;
    }
    if (!checkRet || !ChkArrayArgs(ae)) {
        ae.SetTy(TypeManager::GetInvalidTy());
    } else {
        ae.type->SetTy(ae.GetTy()); // Update array type ty, overwrite Array<Invalid>.
    }
    return ae.GetTy();
}

Ptr<Ty> TypeChecker::TypeCheckerImpl::SynVArrayExpr(ASTContext& ctx, ArrayExpr& ve)
{
    CJC_NULLPTR_CHECK(ve.type);
    ve.type->SetTy(Synthesize({ctx, SynPos::EXPR_ARG}, ve.type.get()));
    if (!Ty::IsTyCorrect(ve.type->GetTy()) || !Is<VArrayTy>(ve.type->GetTy())) {
        ve.SetTy(TypeManager::GetInvalidTy());
        return ve.GetTy();
    }
    ve.SetTy(ve.type->GetTy());
    if (!ChkVArrayArg(ctx, ve)) {
        ve.SetTy(TypeManager::GetInvalidTy());
        return ve.GetTy();
    }
    return ve.GetTy();
}

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
bool TypeChecker::TypeCheckerImpl::ChkPointerExpr(ASTContext& ctx, Ty& target, PointerExpr& cpe)
{
    CJC_ASSERT(cpe.type && cpe.type->GetTy());
    bool ret = true;
    // 'var a: CPointer<T> = CPointer()': Generic types should need to be derived.
    Ptr<Ty> targetTy = TypeCheckUtil::UnboxOptionType(&target);
    if (!targetTy->typeArgs.empty() && Ty::IsTyCorrect(targetTy->typeArgs[0]) &&
        (!Ty::IsTyCorrect(cpe.type->GetTy()->typeArgs[0]) || cpe.type->GetTy()->HasGeneric())) {
        cpe.type->SetTy(targetTy);
    }
    if (Ty::IsTyCorrect(targetTy)) {
        ret = Check(ctx, targetTy, cpe.type.get());
    } else {
        // 'var a = CPointer<T>()': Type derivation does not depend on target information.
        cpe.type->SetTy(Synthesize({ctx, SynPos::NONE}, cpe.type.get()));
    }
    cpe.SetTy(cpe.type->GetTy());
    // 'var a = CPointer()': Generic type cannot be derived.
    if (!Ty::IsTyCorrect(cpe.GetTy())) {
        diag.Diagnose(cpe, DiagKind::sema_pointer_unknow_generic_type);
        return false;
    } else if (!ret) {
        DiagMismatchedTypesWithFoundTy(diag, *cpe.sourceExpr, *targetTy, *cpe.GetTy());
    }

    // One arg.
    if (cpe.arg) {
        auto argTy = Synthesize({ctx, SynPos::EXPR_ARG}, cpe.arg.get());
        if (!Ty::IsTyCorrect(argTy) || !(argTy->IsPointer() || argTy->IsCFunc())) {
            if (!TypeCheckUtil::CanSkipDiag(*cpe.arg)) {
                diag.Diagnose(cpe, DiagKind::sema_pointer_single_element_type_error);
            }
            return false;
        }
        if (!cpe.arg->name.Empty()) {
            diag.Diagnose(cpe.arg->name.Begin(), cpe.arg->name.End(),
                DiagKind::sema_unknown_named_argument, cpe.arg->name.Val());
            return false;
        }
    }

    if (Ty::IsTyCorrect(targetTy) && !typeManager.IsSubtype(cpe.GetTy(), targetTy)) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_mismatched_types, cpe)
            .AddMainHintArguments(target.String(), Ty::ToString(cpe.GetTy()));
        return false;
    }
    return ret;
}
#endif

Ptr<Ty> TypeChecker::TypeCheckerImpl::SynPointerExpr(ASTContext& ctx, PointerExpr& cptrExpr)
{
    if (!ChkPointerExpr(ctx, *TypeManager::GetInvalidTy(), cptrExpr)) {
        return TypeManager::GetInvalidTy();
    }
    return cptrExpr.GetTy();
}

bool TypeChecker::TypeCheckerImpl::SynCFuncCall(ASTContext& ctx, CallExpr& ce)
{
    auto callee = DynamicCast<RefExpr>(&*ce.baseFunc);
    if (!callee) {
        ce.SetTy(TypeManager::GetInvalidTy());
        return false;
    }
    ce.SetTy(Synthesize({ctx, SynPos::EXPR_ARG}, ce.baseFunc.get()));
    if (!Ty::IsTyCorrect(ce.baseFunc->GetTy()) || !Ty::IsTyCorrect(ce.GetTy())) {
        ce.SetTy(TypeManager::GetInvalidTy());
        return false;
    }
    if (ce.GetTy()->IsCFunc()) {
        if (ce.args.size() != 1) {
            diag.Diagnose(*ce.baseFunc, DiagKind::sema_cfunc_too_many_arguments);
            ce.SetTy(TypeManager::GetInvalidTy());
            return false;
        }
        Synthesize({ctx, SynPos::EXPR_ARG}, ce.args[0]);
        if (!Ty::IsTyCorrect(ce.args[0]->GetTy())) {
            ce.SetTy(TypeManager::GetInvalidTy());
            return false;
        }
        bool res{true};
        if (!ce.args[0]->name.Empty()) {
            diag.Diagnose(ce.args[0]->name.Begin(), ce.args[0]->name.End(), DiagKind::sema_unknown_named_argument,
                ce.args[0]->name.Val());
            // the program is invalid, yet the type check can still pass if all other checks hold, do not goto INVALID
            // here
            res = false;
        }
        // only CFunc<...>(CPointer(...)) is valid
        if (ce.args[0]->GetTy()->IsPointer()) {
            ce.SetTy(ce.baseFunc->GetTy());
            return res;
        }
    }
    // Otherwise, report error and return a invalid ty.
    diag.Diagnose(*ce.args[0], DiagKind::sema_cfunc_ctor_must_be_cpointer);
    ce.SetTy(TypeManager::GetInvalidTy());
    return false;
}

bool TypeChecker::TypeCheckerImpl::ChkBuiltinCall(ASTContext& ctx, Ty& target, CallExpr& ce)
{
    static bool (TypeCheckerImpl::*const(CHK_FUNCS[]))(ASTContext&, AST::Ty&, AST::CallExpr&) = {
        &TypeCheckerImpl::ChkPointerCall,
        &TypeCheckerImpl::ChkCStringCall,
        &TypeCheckerImpl::ChkArrayCall,
        &TypeCheckerImpl::ChkVArrayCall,
        &TypeCheckerImpl::ChkCFuncCall,
    };
    for (auto& chkFunc : CHK_FUNCS) {
        if ((this->*chkFunc)(ctx, target, ce)) {
            return true;
        }
    }
    return false;
}

bool TypeChecker::TypeCheckerImpl::IsCallOfBuiltInType(const CallExpr& ce, const AST::TypeKind kind) const
{
    if (!ce.baseFunc) {
        return false;
    }
    auto baseTarget = ce.baseFunc->GetTarget();
    if (!baseTarget) {
        return false;
    }
    auto isTypeAlias = IsBuiltinTypeAlias(*baseTarget, kind);
    BuiltInType builtInType;
    switch (kind) {
        case AST::TypeKind::TYPE_POINTER:
            builtInType = BuiltInType::POINTER;
            break;
        case AST::TypeKind::TYPE_CSTRING:
            builtInType = BuiltInType::CSTRING;
            break;
        case AST::TypeKind::TYPE_ARRAY:
            builtInType = BuiltInType::ARRAY;
            break;
        case AST::TypeKind::TYPE_VARRAY:
            builtInType = BuiltInType::VARRAY;
            break;
        default:
            return false;
    }
    auto isSpecifyBuiltinType = baseTarget->IsBuiltIn() && RawStaticCast<BuiltInDecl*>(baseTarget)->IsType(builtInType);
    return isTypeAlias || isSpecifyBuiltinType;
}

bool TypeChecker::TypeCheckerImpl::ChkPointerCall(ASTContext& ctx, Ty& target, CallExpr& ce)
{
    if (IsCallOfBuiltInType(ce, AST::TypeKind::TYPE_POINTER)) {
        bool ret = false;
        Ptr<Ty> resultTy = TypeManager::GetInvalidTy();
        DesugarPointerCall(ctx, ce);
        auto pointerExpr = StaticAs<ASTKind::POINTER_EXPR>(ce.desugarExpr.get());
        if (target.IsInvalid()) {
            resultTy = SynPointerExpr(ctx, *pointerExpr);
            ret = Ty::IsTyCorrect(resultTy);
        } else {
            ret = ChkPointerExpr(ctx, target, *pointerExpr);
            resultTy = pointerExpr->GetTy();
        }
        if (!ret) {
            if (pointerExpr->arg) {
                (void)ce.args.emplace_back(std::move(pointerExpr->arg));
            }
            ce.desugarExpr = nullptr;
        }
        ce.SetTy(resultTy);
        return ret;
    }
    return false;
}

bool TypeChecker::TypeCheckerImpl::ChkCStringCall(ASTContext& ctx, Ty& target, CallExpr& ce)
{
    if (IsCallOfBuiltInType(ce, AST::TypeKind::TYPE_CSTRING)) {
        // Check arg.
        auto cptrTy = typeManager.GetPointerTy(TypeManager::GetPrimitiveTy(TypeKind::TYPE_UINT8));
        if (ce.args.size() != 1) {
            DiagWrongNumberOfArguments(diag, ce, {cptrTy});
            ce.SetTy(TypeManager::GetInvalidTy());
            return false;
        }
        if (!Check(ctx, cptrTy, ce.args[0].get())) {
            ce.SetTy(TypeManager::GetInvalidTy());
            return false;
        }
        // Check whether target is CString or CType.
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
        Ptr<Ty> targetTy = TypeCheckUtil::UnboxOptionType(&target);
        if (Ty::IsTyCorrect(targetTy) && !targetTy->IsCString() && !targetTy->IsCType()) {
            DiagMismatchedTypesWithFoundTy(diag, ce, target.String(), std::string{CSTRING_NAME});
            ce.SetTy(TypeManager::GetInvalidTy());
            return false;
        }
#endif
        ce.SetTy(TypeManager::GetCStringTy());
        return true;
    }
    return false;
}

bool TypeChecker::TypeCheckerImpl::ChkArrayCall(ASTContext& ctx, Ty& target, CallExpr& ce)
{
    // Check TypeAlias or constructor call of 'Array'.
    if (IsCallOfBuiltInType(ce, AST::TypeKind::TYPE_ARRAY)) {
        bool ret = false;
        Ptr<Ty> resultTy = TypeManager::GetInvalidTy();
        DesugarArrayCall(ctx, ce);
        auto arrayExpr = StaticAs<ASTKind::ARRAY_EXPR>(ce.desugarExpr.get());
        if (target.IsInvalid()) {
            resultTy = SynArrayExpr(ctx, *arrayExpr);
            ret = Ty::IsTyCorrect(resultTy);
        } else {
            ret = ChkArrayExpr(ctx, target, *arrayExpr);
            resultTy = arrayExpr->GetTy();
        }
        if (!ret) {
            RecoverCallFromArrayExpr(ce);
        }
        ce.SetTy(resultTy);
        return ret;
    }
    ce.SetTy(TypeManager::GetNonNullTy(ce.GetTy()));
    return false;
}

bool TypeChecker::TypeCheckerImpl::ChkVArrayCall(ASTContext& ctx, Ty& target, CallExpr& ce)
{
    if (IsCallOfBuiltInType(ce, AST::TypeKind::TYPE_VARRAY)) {
        bool ret = false;
        Ptr<Ty> resultTy = TypeManager::GetInvalidTy();
        DesugarArrayCall(ctx, ce);
        auto arrayExpr = StaticAs<ASTKind::ARRAY_EXPR>(ce.desugarExpr.get());
        if (target.IsInvalid()) {
            resultTy = SynVArrayExpr(ctx, *arrayExpr);
            ret = Ty::IsTyCorrect(resultTy);
        } else {
            ret = ChkVArrayExpr(ctx, target, *arrayExpr);
            resultTy = arrayExpr->GetTy();
        }
        if (!ret) {
            RecoverCallFromArrayExpr(ce);
        }
        ce.SetTy(resultTy);
        return ret;
    }
    return false;
}

bool TypeChecker::TypeCheckerImpl::ChkCFuncCall([[maybe_unused]] ASTContext& ctx, AST::Ty& target, AST::CallExpr& ce)
{
    if (!ce.baseFunc) {
        return false;
    }

    if (!ce.baseFunc->GetTarget()) {
        return false;
    }
    Ptr<Decl> realTarget = GetRealTarget(ce.baseFunc, ce.baseFunc->GetTarget());

    if (!realTarget->IsBuiltIn() || !RawStaticCast<BuiltInDecl*>(realTarget)->IsType(BuiltInType::CFUNC)) {
        return false;
    }

    if (!target.IsInvalid() && !target.IsCFunc()) {
        ce.SetTy(TypeManager::GetInvalidTy());
        return false;
    }

    ce.baseFunc->SetTarget(realTarget);

    bool ret = SynCFuncCall(ctx, ce);
    if (!ret) {
        ce.SetTy(TypeManager::GetInvalidTy());
    }
    return ret;
}

bool TypeChecker::TypeCheckerImpl::IsBuiltinTypeAlias(const Decl& decl, const AST::TypeKind kind) const
{
    if (decl.astKind != ASTKind::TYPE_ALIAS_DECL) {
        return false;
    }
    auto& typeAliasDecl = static_cast<const TypeAliasDecl&>(decl);
    // If typealias' base type is array, expr is not a normal call base.
    Ptr<Ty> origTy = typeAliasDecl.type->GetTy();
    if (origTy) {
        if (kind == AST::TypeKind::TYPE_ANY) {
            return (origTy->kind == TypeKind::TYPE_ARRAY || origTy->kind == TypeKind::TYPE_POINTER ||
                origTy->kind == TypeKind::TYPE_CSTRING || origTy->kind == TypeKind::TYPE_VARRAY);
        } else {
            return origTy->kind == kind;
        }
    }
    return false;
}

bool TypeChecker::TypeCheckerImpl::ChkArrayArgs(ArrayExpr& ae)
{
    if (ae.args.empty()) {
        return true;
    }
    if (ae.args[0] && !ae.args[0]->name.Empty()) {
        diag.Diagnose(*ae.args[0], DiagKind::sema_array_first_arg_cannot_be_named);
        return false;
    }
    // Other valid array constructors must have 2 arguments.
    if (ae.args.size() != 2 || !ae.args[1]) {
        return true;
    }
    if (ae.initFunc) {
        if (!ae.args[1]->name.Empty()) {
            diag.Diagnose(*ae.args[1], DiagKind::sema_array_second_arg_cannot_be_named);
            return false;
        }
    } else {
        if (ae.args[1]->name != "repeat") {
            diag.Diagnose(*ae.args[1], DiagKind::sema_array_second_wrong_named_arg);
            return false;
        }
    }
    return true;
}
