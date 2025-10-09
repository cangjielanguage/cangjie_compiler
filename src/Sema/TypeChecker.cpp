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

#include <algorithm>
#include <memory>
#include <unordered_set>

#include "CheckAPILevel.h"
#include "Collector.h"
#include "Desugar/DesugarInTypeCheck.h"
#include "DiagSuppressor.h"
#include "Diags.h"
#include "ExtraScopes.h"
#include "JoinAndMeet.h"
#include "TypeCheckUtil.h"

#include "cangjie/AST/Clone.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Basic/Print.h"
#include "cangjie/Frontend/CompilerInstance.h"
#include "cangjie/Utils/CheckUtils.h"
#include "cangjie/Utils/Utils.h"
#include "NativeFFI/Java/BeforeTypeCheck/GenerateJavaMirror.h"
#include "NativeFFI/ObjC/BeforeTypeCheck/Desugar.h"


namespace Cangjie {
using namespace Sema;
using namespace TypeCheckUtil;
using namespace AST;

TypeChecker::TypeChecker(CompilerInstance* ci)
{
    impl = std::make_unique<TypeCheckerImpl>(ci);
}

TypeChecker::~TypeChecker()
{
}

TypeChecker::TypeCheckerImpl::TypeCheckerImpl(CompilerInstance* ci)
    : promotion(Promotion(*ci->typeManager)),
      typeManager(*ci->typeManager),
      ci(ci),
      diag(ci->diag),
      importManager(ci->importManager),
      backendType(ci->invocation.globalOptions.backend),
      mpImpl(new MPTypeCheckerImpl(*ci))
{
}

TypeChecker::TypeCheckerImpl::~TypeCheckerImpl()
{
    if (mpImpl) {
        delete mpImpl;
        mpImpl = nullptr;
    }
}

bool TypeChecker::TypeCheckerImpl::CheckThisTypeOfFuncBody(const FuncBody& fb) const
{
    CJC_ASSERT(fb.retType);
    bool returnThis = fb.retType->astKind == ASTKind::THIS_TYPE ||
        (fb.retType->astKind == ASTKind::REF_TYPE &&
            RawStaticCast<RefType*>(fb.retType.get())->ref.identifier == "This");
    // STATIC function should not return 'This' type.
    if (returnThis && fb.funcDecl != nullptr && fb.funcDecl->TestAttr(Attribute::STATIC)) {
        return false;
    }
    // 'This' type used in non-class decl or nested function are reported by parser. Do not check again.
    return true;
}

bool TypeChecker::TypeCheckerImpl::IsIndexAssignmentOperator(const AST::FuncDecl& fd) const
{
    return fd.op == TokenKind::LSQUARE && fd.funcBody != nullptr && !fd.funcBody->paramLists.empty() &&
        !fd.funcBody->paramLists[0]->params.empty() && fd.funcBody->paramLists[0]->params.back()->isNamedParam;
}

bool TypeChecker::TypeCheckerImpl::CheckBodyRetType(ASTContext& ctx, FuncBody& fb)
{
    CJC_ASSERT(fb.retType);
    CJC_ASSERT(fb.retType->ty);
    // Only set 'fb.retType->ty' to invalid when it's type was questTy.
    // Otherwise return without modifying the type of any node.
    if (fb.body == nullptr) {
        // Quick fix for foreign functions. I did not add error messages currently since it is a quick fix.
        if (fb.retType->ty->HasQuestTy()) {
            fb.retType->ty = TypeManager::GetInvalidTy();
            return false;
        }
        return true;
    }
    // If the return type is not of QuestTy, then we use it to check the function's body.
    if (fb.retType->ty->IsQuest()) {
        // Semantic analysis for function body without given return type.
        auto ret = Synthesize(ctx, fb.body.get());
        // In lambda, the return value type should be consistent with the body.
        // Otherwise, errors in the body that do not affect the return value type may fail to be reported.
        if (fb.funcDecl == nullptr && !Ty::IsTyCorrect(ret)) {
            fb.retType->ty = TypeManager::GetInvalidTy();
            return false;
        }
        fb.retType->ty = CalcFuncRetTyFromBody(fb);
        bool isWellTyped = Ty::IsTyCorrect(fb.retType->ty);
        if (fb.retType->ty->HasQuestTy()) {
            isWellTyped = false;
            fb.retType->ty = TypeManager::GetInvalidTy();
            if (!CanSkipDiag(fb)) {
                DiagUnableToInferReturnType(diag, fb);
            }
        }
        if (isWellTyped) {
            if (CheckReturnThisInFuncBody(fb)) {
                ReplaceFuncRetTyWithThis(fb, fb.retType->ty);
            } else if (auto ctt = DynamicCast<ClassThisTy*>(fb.retType->ty)) {
                // Replace ClassThisTy to ClassTy when the function cannot return 'This' anymore.
                fb.retType->ty = ctt->declPtr->ty;
            }
        }
        return isWellTyped;
    } else {
        bool isWellTyped = true;
        if (fb.retType->ty->IsUnit()) {
            // The body eventually will be appended a 'return ()' expression, so we switch to the Synthesize mode.
            // Errors should be already reported during the synthesis.
            isWellTyped = Ty::IsTyCorrect(Synthesize(ctx, fb.body.get()));
        } else if (NeedCheckBodyReturn(fb)) {
            isWellTyped = Check(ctx, fb.retType->ty, fb.body.get());
            if (!isWellTyped && fb.body->body.empty()) {
                DiagMismatchedTypes(diag, *fb.body, *fb.retType, "return type");
            }
            if (isWellTyped && Is<ClassThisTy>(fb.retType->ty)) {
                auto node = fb.body->body.back().get();
                while (true) {
                    if (auto e = DynamicCast<Expr>(node); e && e->desugarExpr) {
                        node = e->desugarExpr.get();
                    } else {
                        break;
                    }
                }
                if (auto k = node->astKind; k != ASTKind::CALL_EXPR && k != ASTKind::MEMBER_ACCESS &&
                    k != ASTKind::REF_EXPR && k != ASTKind::RETURN_EXPR && k != ASTKind::PAREN_EXPR) {
                    fb.body->ty = ReplaceThisTy(fb.body->ty);
                }
                if (!CheckReturnThisInFuncBody(fb) && !fb.retType->ty->IsNothing()) {
                    DiagMismatchedTypes(diag, *fb.body, *fb.retType, "return type");
                    isWellTyped = false;
                }
            }
        }
        return isWellTyped;
    }
}

Ptr<Ty> TypeChecker::TypeCheckerImpl::ReplaceThisTy(Ptr<Ty> now)
{
    if (!Ty::IsTyCorrect(now)) {
        return now;
    }
    if (auto thisTy = DynamicCast<ClassThisTy>(now)) {
        return typeManager.GetClassTy(*thisTy->decl, thisTy->typeArgs);
    }
    return now;
}

void TypeChecker::TypeCheckerImpl::ReplaceFuncRetTyWithThis(FuncBody& fb, Ptr<Ty> ty)
{
    if (auto ct = DynamicCast<ClassTy*>(ty); ct) {
        auto rt = MakeOwned<RefType>();
        rt->curFile = fb.curFile;
        rt->ref.target = ct->decl;
        rt->ref.identifier = "This";
        rt->ty = typeManager.GetClassThisTy(*ct->declPtr, ct->typeArgs);
        rt->EnableAttr(Attribute::COMPILER_ADD);
        if (fb.retType) {
            CJC_ASSERT(fb.curFile);
            fb.curFile->trashBin.emplace_back(std::move(fb.retType));
        }
        fb.retType = std::move(rt);
    }
}

bool TypeChecker::TypeCheckerImpl::CheckFuncBody(ASTContext& ctx, FuncBody& fb)
{
    if (fb.retType) {
        Synthesize(ctx, fb.retType.get());
    } else {
        // The goal is that, after type checking, every function declaration has a correct return type, so that the back
        // ends do not need to test anymore.
        AddRetTypeNode(fb);
    }

    // The constructor logic is simpler and handled separately.
    if (fb.funcDecl && fb.funcDecl->TestAttr(Attribute::CONSTRUCTOR)) {
        CheckCtorFuncBody(ctx, fb);
        // Should not return true directly. Revise later.
        return true;
    }

    if (!CheckThisTypeOfFuncBody(fb)) {
        diag.Diagnose(*fb.retType, DiagKind::sema_invalid_position_of_this_type);
    }
    std::vector<Ptr<Ty>> paramTys;
    if (!CheckNormalFuncBody(ctx, fb, paramTys)) {
        return false;
    }
    return true;
}

void TypeChecker::TypeCheckerImpl::AddRetTypeNode(FuncBody& fb) const
{
    // For incremental type check's compatibility. Incremental type check will erase retType->ty but not retType.
    // Adding retType node more than once leads to memory leak.
    if (!fb.retType) {
        fb.retType = MakeOwned<RefType>();
    }
    fb.retType->EnableAttr(Attribute::COMPILER_ADD);
    fb.retType->ty = TypeManager::GetQuestTy();
    // Set compiler added retType as visited to avoid being checked by `CheckReferenceTypeLegality`.
    fb.retType->EnableAttr(Attribute::IS_CHECK_VISITED);
}

bool TypeChecker::TypeCheckerImpl::CheckNormalFuncBody(ASTContext& ctx, FuncBody& fb, std::vector<Ptr<Ty>>& paramTys)
{
    if (fb.paramLists.empty()) {
        return false;
    }
    CheckFuncParamList(ctx, *fb.paramLists[0]);
    paramTys = GetFuncBodyParamTys(fb);

    bool isCFFIBackend = IsUnsafeBackend(backendType);
    bool isCFunc =
        fb.TestAttr(Attribute::C) || (fb.funcDecl && fb.funcDecl->TestAttr(Attribute::FOREIGN) && isCFFIBackend);
    bool hasVariableLenArg = (fb.funcDecl && fb.funcDecl->hasVariableLenArg) || fb.paramLists[0]->hasVariableLenArg;
    // Check and update return type for foreign functions.
    if (!Ty::IsTyCorrect(fb.retType->ty)) {
        if (!fb.TestAttr(Attribute::IS_CHECK_VISITED)) {
            fb.EnableAttr(Attribute::IS_CHECK_VISITED); // Avoid re-enter funcDecl check, when function is invalid.
            Synthesize(ctx, fb.body.get());             // Synthesize for other decl/expr in function body.
        }
        fb.ty = typeManager.GetFunctionTy(paramTys, fb.retType->ty, {isCFunc, false, hasVariableLenArg});
        return false;
    }

    // Set funcTy before Synthesize body, avoid recursively call typecheck loop.
    auto funcTy = typeManager.GetFunctionTy(paramTys, fb.retType->ty, {isCFunc, false, hasVariableLenArg});
    if (fb.funcDecl) {
        fb.funcDecl->ty = funcTy;
    }
    fb.ty = funcTy;

    if (!CheckBodyRetType(ctx, fb)) {
        // Update 'fb.ty' witch updated 'fb.retType->ty'.
        fb.ty = typeManager.GetFunctionTy(paramTys, fb.retType->ty, {isCFunc, false, hasVariableLenArg});
        return false;
    }

    if (isCFFIBackend) {
        UnsafeCheck(fb);
    }

    funcTy = typeManager.GetFunctionTy(paramTys, fb.retType->ty, {isCFunc, false, hasVariableLenArg});
    // Update funcDecl's type after body is checked.
    if (fb.funcDecl) {
        fb.funcDecl->ty = funcTy;
    }
    fb.ty = funcTy;
    return true;
}

namespace {

/**
 * Add return type(Unit) for function
 */
void AddUnitType(const AST::FuncDecl& fd)
{
    if (fd.funcBody == nullptr || fd.funcBody->retType) {
        return;
    }
    OwnedPtr<PrimitiveType> type = MakeOwnedNode<PrimitiveType>();
    type->str = "Unit";
    type->kind = TypeKind::TYPE_UNIT;
    if (!fd.funcBody->paramLists.empty()) {
        type->begin = fd.funcBody->paramLists[0]->end;
        type->end = fd.funcBody->paramLists[0]->end;
    }
    type->ty = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
    fd.funcBody->retType = std::move(type);
    fd.funcBody->retType->EnableAttr(Attribute::COMPILER_ADD);
}

/**
 * Add return type for property member decl
 */
void AddReturnTypeForPropMemDecl(PropDecl& pd)
{
    for (auto& getter : pd.getters) {
        CJC_NULLPTR_CHECK(getter);
        CJC_NULLPTR_CHECK(getter->funcBody);
        if (pd.type && !getter->funcBody->retType) {
            getter->funcBody->retType = ASTCloner::Clone(pd.type.get());
        }
    }
    for (auto& setter : pd.setters) {
        if (!pd.type) {
            continue;
        }
        CJC_NULLPTR_CHECK(setter);
        CJC_NULLPTR_CHECK(setter->funcBody);
        if (setter->funcBody->paramLists.empty() || setter->funcBody->paramLists[0]->params.empty()) {
            return;
        }
        if (!setter->funcBody->paramLists[0]->params[0]->type) {
            setter->funcBody->paramLists[0]->params[0]->type = ASTCloner::Clone(pd.type.get());
        }
        AddUnitType(*setter);
    }
}

/**
 *  Add Getter/Setter in the abstract property(no body) of Interface/Class.
 */
void AddSetterGetterInProp(const InheritableDecl& cld)
{
    for (auto decl : cld.GetMemberDeclPtrs()) {
        CJC_ASSERT(decl);
        if (decl->astKind != ASTKind::PROP_DECL || decl->TestAttr(Attribute::IMPORTED) ||
            decl->TestAttr(Attribute::FROM_COMMON_PART)) {
            continue;
        }
        // abstract propDecl and commonWithoutDefault propDecl need add default getter and setter
        if (!decl->TestAttr(Attribute::ABSTRACT) && !IsCommonWithoutDefault(*decl)) {
            continue;
        }
        auto propDecl = RawStaticCast<PropDecl*>(decl);
        // Getter
        OwnedPtr<FuncDecl> getter = MakeOwnedNode<FuncDecl>();
        getter->propDecl = propDecl;
        getter->identifier = "get";
        getter->isGetter = true;
        getter->CloneAttrs(*propDecl);
        getter->DisableAttr(Attribute::MUT);
        getter->EnableAttr(Attribute::IMPLICIT_ADD);
        getter->EnableAttr(Attribute::COMPILER_ADD);
        if (cld.astKind == ASTKind::INTERFACE_DECL) {
            getter->EnableAttr(Attribute::PUBLIC);
        }
        auto getFuncParamList = MakeOwnedNode<FuncParamList>();
        OwnedPtr<FuncBody> getFuncBody = MakeOwnedNode<FuncBody>();
        getFuncBody->paramLists.push_back(std::move(getFuncParamList));
        getter->funcBody = std::move(getFuncBody);
        propDecl->getters.emplace_back(std::move(getter));
        if (!propDecl->isVar) {
            continue;
        }
        // Setter
        OwnedPtr<FuncDecl> setter = MakeOwnedNode<FuncDecl>();
        setter->propDecl = propDecl;
        setter->isSetter = true;
        setter->identifier = "set";
        setter->CloneAttrs(*propDecl);
        setter->DisableAttr(Attribute::MUT);
        setter->EnableAttr(Attribute::IMPLICIT_ADD);
        if (cld.astKind == ASTKind::INTERFACE_DECL) {
            setter->EnableAttr(Attribute::PUBLIC);
        }
        auto setFuncParamList = MakeOwnedNode<FuncParamList>();
        auto param = CreateFuncParam("set");
        setFuncParamList->params.push_back(std::move(param));
        OwnedPtr<FuncBody> setFuncBody = MakeOwnedNode<FuncBody>();
        setFuncBody->paramLists.push_back(std::move(setFuncParamList));
        setter->funcBody = std::move(setFuncBody);
        propDecl->setters.emplace_back(std::move(setter));
    }
}

constexpr std::string_view INOUT_TEMP_VALUE{"temporary value"};
constexpr std::string_view INOUT_IMMUTABLE{"is a immutable variable"};
} // namespace

bool TypeChecker::TypeCheckerImpl::CheckReturnThisInFuncBody(const FuncBody& fb) const
{
    // traverse funcBody to update returnThis which will be set to true only in two cases:
    // 1. return this
    // 2. return calling other member function which return This.
    // 3. return other expression which type is 'This' (NOTE: incorrect implementation, should be fixed).
    std::function<bool(Ptr<Expr>)> checkExprType = [&checkExprType](Ptr<Expr> expr) {
        while (expr->desugarExpr) {
            expr = expr->desugarExpr.get();
        }
        if (auto refExpr = DynamicCast<RefExpr*>(expr); refExpr) {
            return refExpr->isThis;
        } else if (auto ce = DynamicCast<CallExpr*>(expr); ce && ce->resolvedFunction) {
            bool isMemberCall = Is<RefExpr>(ce->baseFunc.get());
            if (auto ma = DynamicCast<MemberAccess*>(ce->baseFunc.get()); ma && ma->baseExpr) {
                isMemberCall = IsThisOrSuper(*ma->baseExpr);
            }
            return isMemberCall && IsFuncReturnThisType(*ce->resolvedFunction);
        } else if (auto paren = DynamicCast<ParenExpr>(expr)) {
            return checkExprType(paren->expr);
        }

        bool returnThis = false;
        auto visitor = [&returnThis, &checkExprType](Ptr<Node> n) {
            CJC_ASSERT(n);
            if (n->astKind != ASTKind::RETURN_EXPR) {
                return VisitAction::WALK_CHILDREN;
            }
            auto re = RawStaticCast<ReturnExpr*>(n);
            returnThis = checkExprType(re->expr.get());
            return VisitAction::SKIP_CHILDREN;
        };
        Walker(expr, visitor).Walk();
        return returnThis;
    };
    if (fb.body != nullptr && !fb.body->body.empty()) {
        auto lastNode = fb.body->body.back().get();
        if (auto expr = DynamicCast<Expr*>(lastNode); expr) {
            while (expr->desugarExpr) {
                expr = expr->desugarExpr.get();
            }
            // When function's last expression has 'This' type and current funcBody has parent class decl,
            // 'This' type is valid.
            return checkExprType(expr) && Is<ClassDecl>(fb.parentClassLike);
        }
    }
    return false;
}

void TypeChecker::TypeCheckerImpl::CheckCtorFuncBody(ASTContext& ctx, FuncBody& fb)
{
    CJC_ASSERT(fb.funcDecl); // Ctor must have related funcDecl.
    if (fb.parentClassLike) {
        if (auto attr = HasJavaAttr(*fb.parentClassLike); attr) {
            fb.funcDecl->EnableAttr(attr.value());
        }
    }
    Ptr<Ty> ctorTy = TypeManager::GetInvalidTy();
    if (fb.funcDecl->TestAttr(Attribute::STATIC)) {
        // Static init always has type of 'Unit' as return type.
        ctorTy = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
    } else {
        if (fb.parentStruct) {
            ctorTy = fb.parentStruct->ty;
        }
        if (fb.parentClassLike) {
            ctorTy = fb.parentClassLike->ty;
        }
    }
    if (!Ty::IsTyCorrect(ctorTy) || fb.paramLists.empty()) {
        return;
    }
    CheckFuncParamList(ctx, *fb.paramLists[0].get());
    auto paramTys = GetFuncBodyParamTys(fb);
    fb.ty = typeManager.GetFunctionTy(paramTys, ctorTy);
    fb.funcDecl->ty = fb.ty;
    fb.retType->ty = ctorTy;
    Synthesize(ctx, fb.body.get());
}

void TypeChecker::TypeCheckerImpl::CheckFuncParamList(ASTContext& ctx, FuncParamList& fpl)
{
    // We use the tupleType to handle the type of FuncParamList.
    std::vector<Ptr<Ty>> paramTys;
    if (fpl.params.empty()) {
        fpl.ty = typeManager.GetTupleTy(paramTys);
        return;
    }
    for (auto& param : fpl.params) {
        CJC_NULLPTR_CHECK(param);
        if (!Ty::IsTyCorrect(Synthesize(ctx, param.get()))) {
            paramTys.push_back(TypeManager::GetInvalidTy()); // Set type to get correct size of function type.
            continue;
        }
        Ptr<Ty> paramTy = param->ty;
        paramTys.push_back(paramTy);
        // Infer param assignment's type by param's type.
        if (param->assignment) {
            auto curNode = param->assignment.get();
            auto expr = param->assignment.get();
            // Need infer for desugarExpr otherwise node's type will be removed.
            while (expr != nullptr && expr->desugarExpr != nullptr) {
                curNode = expr->desugarExpr.get();
                expr = As<ASTKind::EXPR>(curNode);
            }
            (void)Check(ctx, paramTy, curNode); // Error is reported during checking, return value can be ignored.
            if (Ty::IsTyCorrect(curNode->ty)) {
                param->assignment->ty = curNode->ty;
            }
        }
    }
    fpl.ty = typeManager.GetTupleTy(paramTys);
}

bool TypeChecker::TypeCheckerImpl::ChkFuncArg(ASTContext& ctx, Ty& target, FuncArg& fa)
{
    if (!fa.expr) {
        fa.ty = TypeManager::GetInvalidTy();
        return false;
    }
    if (fa.withInout) {
        return ChkFuncArgWithInout(ctx, target, fa);
    }
    if (!Check(ctx, &target, fa.expr.get())) {
        fa.ty = TypeManager::GetInvalidTy();
        return false;
    }
    fa.ty = fa.expr->ty;
    return true;
}

bool TypeChecker::TypeCheckerImpl::ChkFuncArgWithInout(ASTContext& ctx, Ty& target, FuncArg& fa)
{
    auto realTarget = &target;
    auto exprTy = fa.expr->ty;
    if (!Ty::IsTyCorrect(fa.expr->ty)) {
        // Trying to infer fa type without target.
        exprTy = Synthesize(ctx, fa.expr.get());
    }
    auto argTy = DynamicCast<VArrayTy*>(exprTy);
    auto ptrParamTy = DynamicCast<PointerTy*>(&target);
    auto varrParamTy = DynamicCast<VArrayTy*>(&target);
    // Case 1: 'argTy' is VArray<T...>, target is CPointer<R>. 'realTarget' trans to
    // VArray<R...>.
    // Case 2: 'argTy' is VArray<T...>, target is VArray<R...>. 'realTarget' is VArray<R...>.
    // Other case: 'argTy' is T, target is CPointer<T>. 'realTarget' trans to T.
    if (argTy && ptrParamTy) {
        realTarget = typeManager.GetVArrayTy(*ptrParamTy->typeArgs[0], argTy->size);
    } else if (argTy && varrParamTy) {
        realTarget = &target;
    } else if (ptrParamTy) {
        realTarget = ptrParamTy->typeArgs[0];
    }

    if (!Ty::IsTyCorrect(exprTy) || !typeManager.IsSubtype(exprTy, realTarget)) {
        if (Ty::IsTyCorrect(exprTy)) {
            DiagMismatchedTypes(diag, *fa.expr, *realTarget);
        }
        fa.ty = TypeManager::GetInvalidTy();
        return false;
    }
    if (!ChkInoutFuncArg(fa)) {
        fa.ty = TypeManager::GetInvalidTy();
        return false;
    }
    fa.ty = typeManager.GetPointerTy(Is<VArrayTy>(realTarget) ? realTarget->typeArgs[0].get() : realTarget);
    return true;
}

bool TypeChecker::TypeCheckerImpl::ChkInoutFuncArg(const FuncArg& fa)
{
    switch (fa.expr->astKind) {
        case ASTKind::REF_EXPR:
            return ChkInoutRefExpr(*StaticCast<RefExpr*>(fa.expr.get()));
        case ASTKind::MEMBER_ACCESS:
            return ChkInoutMemberAccess(*StaticCast<MemberAccess*>(fa.expr.get()));
        case ASTKind::LIT_CONST_EXPR: {
            auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_inout_must_be_var_variable, *fa.expr);
            builder.AddMainHintArguments("literal");
            return false;
        }
        default: {
            auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_inout_must_be_var_variable, *fa.expr);
            builder.AddMainHintArguments(std::string(INOUT_TEMP_VALUE));
            return false;
        }
    }
}

bool TypeChecker::TypeCheckerImpl::ChkInoutRefExpr(RefExpr& re, bool isBase)
{
    auto target = GetRealTarget(&re, re.GetTarget());
    bool thisOrSuper = re.isThis || re.isSuper;
    if (isBase && thisOrSuper) {
        return true;
    }
    if (thisOrSuper || !target || !Utils::In(target->astKind, {ASTKind::VAR_DECL, ASTKind::FUNC_PARAM})) {
        auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_inout_must_be_var_variable, re);
        builder.AddMainHintArguments("not a variable");
        return false;
    }
    auto vd = StaticAs<ASTKind::VAR_DECL>(target);
    if (!vd->isVar) {
        auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_inout_must_be_var_variable, re);
        builder.AddMainHintArguments(std::string(INOUT_IMMUTABLE));
        return false;
    }
    if (vd->outerDecl && vd->outerDecl->IsNominalDecl()) {
        if (vd->outerDecl->astKind != ASTKind::STRUCT_DECL && !vd->TestAttr(Attribute::STATIC)) {
            (void)diag.DiagnoseRefactor(DiagKindRefactor::sema_inout_modify_heap_variable, re);
            return false;
        }
    }
    return true;
}

/**
 * For member access a.n:
 *   - if a is VArray value and n is "size", it's desugar as a LitConstExpr, report error;
 *   - if a or n defined with 'let', report error;
 *   - if a is class object, report error.
 */
bool TypeChecker::TypeCheckerImpl::ChkInoutMemberAccess(const MemberAccess& ma)
{
    // Currently, `ma.desugarExpr` can only be `VArray.size`.
    if (ma.desugarExpr && ma.desugarExpr->astKind == ASTKind::LIT_CONST_EXPR) {
        auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_inout_must_be_var_variable, ma);
        builder.AddMainHintArguments(std::string(INOUT_IMMUTABLE));
        return false;
    }
    if (!ma.target) {
        return false;
    }
    bool meetContraints = true;
    if (ma.target->astKind == ASTKind::VAR_DECL) {
        auto vd = StaticAs<ASTKind::VAR_DECL>(ma.target);
        if (!vd->isVar) {
            auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_inout_must_be_var_variable, ma.field.Begin());
            builder.AddMainHintArguments(std::string(INOUT_IMMUTABLE));
            meetContraints = false;
        }
        if (vd->TestAttr(Attribute::STATIC)) {
            return meetContraints;
        }
    } else {
        auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_inout_must_be_var_variable, ma);
        if (ma.target->astKind == ASTKind::FUNC_DECL) {
            builder.AddMainHintArguments("function declaration");
        } else {
            builder.AddMainHintArguments(std::string(INOUT_TEMP_VALUE));
        }
        meetContraints = false;
    }
    auto& be = ma.baseExpr;
    if (Ty::IsTyCorrect(be->ty) && be->ty->IsClassLike()) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_inout_modify_heap_variable, *be);
        return false;
    }
    if (be->astKind == ASTKind::REF_EXPR) {
        return ChkInoutRefExpr(static_cast<RefExpr&>(*be), true) && meetContraints;
    }
    if (be->astKind == ASTKind::MEMBER_ACCESS) {
        if (auto beTarget = GetRealTarget(be.get(), be->GetTarget());
            beTarget && beTarget->astKind != ASTKind::PACKAGE_DECL) {
            return ChkInoutMemberAccess(static_cast<MemberAccess&>(*be)) && meetContraints;
        }
        return false;
    }
    auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_inout_must_be_var_variable, *be);
    builder.AddMainHintArguments(std::string(INOUT_TEMP_VALUE));
    return false;
}

Ptr<Ty> TypeChecker::TypeCheckerImpl::SynFuncArg(ASTContext& ctx, FuncArg& fa)
{
    if (fa.expr) {
        Synthesize(ctx, fa.expr.get());
        if (fa.withInout) {
            if (!Ty::IsTyCorrect(fa.expr->ty) || !ChkInoutFuncArg(fa)) {
                fa.ty = TypeManager::GetInvalidTy();
            } else if (!Ty::IsMetCType(*fa.expr->ty)) {
                diag.DiagnoseRefactor(DiagKindRefactor::sema_inout_modify_non_ctype, fa);
                fa.ty = TypeManager::GetInvalidTy();
            } else if (fa.expr->ty->IsCString()) {
                diag.DiagnoseRefactor(DiagKindRefactor::sema_inout_modify_cstring_or_zerosized, fa, "type 'CString'");
                fa.ty = TypeManager::GetInvalidTy();
            } else if (Is<VArrayTy>(fa.expr->ty)) {
                fa.ty = typeManager.GetPointerTy(fa.expr->ty->typeArgs[0]);
            } else {
                fa.ty = typeManager.GetPointerTy(fa.expr->ty);
            }
        } else {
            fa.ty = fa.expr->ty;
        }
    } else {
        fa.ty = TypeManager::GetInvalidTy();
    }
    return fa.ty;
}

void TypeChecker::TypeCheckerImpl::SubstituteTypeArguments(
    const TypeAliasDecl& tad, std::vector<OwnedPtr<Type>>& typeArguments, const TypeSubst& typeMapping)
{
    if (!typeArguments.empty() && typeArguments[0]->TestAttr(Attribute::COMPILER_ADD)) {
        return;
    }
    if (auto rt = DynamicCast<AST::RefType*>(tad.type.get())) {
        SubstituteTypeArguments(typeArguments, *rt, typeMapping);
    }
    if (auto qt = DynamicCast<AST::QualifiedType*>(tad.type.get())) {
        SubstituteTypeArguments(typeArguments, *qt, typeMapping);
    }
}

Ptr<AST::Ty> TypeChecker::TypeCheckerImpl::SubstituteTypeAliasInTy(
    AST::Ty& ty, bool needSubstituteGeneric, const TypeSubst& typeMapping)
{
    if (!Ty::IsTyCorrect(&ty)) {
        return TypeManager::GetInvalidTy();
    }
    if (ty.kind <= TypeKind::TYPE_BOOLEAN) {
        return &ty;
    }
    std::vector<Ptr<Ty>> typeArgs = RecursiveSubstituteTypeAliasInTy(&ty, needSubstituteGeneric, typeMapping);
    switch (ty.kind) {
        case TypeKind::TYPE_CLASS: {
            if (auto ctt = DynamicCast<ClassThisTy*>(&ty); ctt) {
                return typeManager.GetClassThisTy(*ctt->declPtr, typeArgs);
            }
            return typeManager.GetClassTy(*static_cast<ClassTy&>(ty).declPtr, typeArgs);
        }
        case TypeKind::TYPE_STRUCT: {
            return typeManager.GetStructTy(*static_cast<StructTy&>(ty).declPtr, typeArgs);
        }
        case TypeKind::TYPE_INTERFACE: {
            return typeManager.GetInterfaceTy(*static_cast<InterfaceTy&>(ty).declPtr, typeArgs);
        }
        case TypeKind::TYPE_ENUM: {
            return typeManager.GetEnumTy(*static_cast<EnumTy&>(ty).declPtr, typeArgs);
        }
        case TypeKind::TYPE_FUNC: {
            auto returnTy = typeArgs.back();
            typeArgs.pop_back();
            auto& funcTy = static_cast<FuncTy&>(ty);
            return typeManager.GetFunctionTy(typeArgs, returnTy, {funcTy.isC, false, funcTy.hasVariableLenArg});
        }
        case TypeKind::TYPE: {
            auto inner = GetUnaliasedTypeFromTypeAlias(static_cast<TypeAliasTy&>(ty), typeArgs);
            if (auto nestedAlias = DynamicCast<TypeAliasTy>(inner)) {
                auto type = Ty::GetDeclPtrOfTy(nestedAlias);
                // the aliased type is in cycle, stop recursive substitution to avoid endless loop
                if (type && type->TestAttr(Attribute::IN_REFERENCE_CYCLE)) {
                    return nestedAlias;
                }
                return SubstituteTypeAliasInTy(*nestedAlias, needSubstituteGeneric, typeMapping);
            }
            return inner;
        }
        case TypeKind::TYPE_TUPLE: {
            return typeManager.GetTupleTy(typeArgs);
        }
        case TypeKind::TYPE_ARRAY: {
            auto& arrayTy = static_cast<ArrayTy&>(ty);
            return typeManager.GetArrayTy(typeArgs[0], arrayTy.dims);
        }
        case TypeKind::TYPE_VARRAY: {
            auto& varrayTy = static_cast<VArrayTy&>(ty);
            CJC_ASSERT(!typeArgs.empty() && typeArgs[0] != nullptr);
            return typeManager.GetVArrayTy(*typeArgs[0], varrayTy.size);
        }
        case TypeKind::TYPE_POINTER: {
            return typeManager.GetPointerTy(typeArgs[0]);
        }
        case TypeKind::TYPE_GENERICS: {
            if (!needSubstituteGeneric) {
                return &ty;
            }
            auto found = typeMapping.find(StaticCast<GenericsTy*>(&ty));
            if (found != typeMapping.end()) {
                return found->second;
            }
            // This type will not be used, just for placeholder and marking current is substituted with typealias.
            return typeManager.GetIntersectionTy({&ty});
        }
        default:
            return &ty;
    }
}

std::vector<Ptr<Ty>> TypeChecker::TypeCheckerImpl::RecursiveSubstituteTypeAliasInTy(
    Ptr<const Ty> ty, bool needSubstituteGeneric, const TypeSubst& typeMapping)
{
    CJC_ASSERT(ty); // Caller guarantees;
    std::vector<Ptr<Ty>> typeArgs;
    for (auto typeArg : ty->typeArgs) {
        CJC_ASSERT(typeArg);
        if (Ty::IsTyCorrect(typeArg) || needSubstituteGeneric) {
            auto noTypeAliasArg = SubstituteTypeAliasInTy(*typeArg, needSubstituteGeneric, typeMapping);
            typeArgs.push_back(noTypeAliasArg);
        } else {
            typeArgs.push_back(typeArg);
        }
    }
    return typeArgs;
}

Ptr<Ty> TypeChecker::TypeCheckerImpl::GetUnaliasedTypeFromTypeAlias(
    const TypeAliasTy& target, const std::vector<Ptr<Ty>>& typeArgs)
{
    CJC_NULLPTR_CHECK(target.declPtr->type);
    auto aliasedType = target.declPtr->type.get();
    // Since 'SubstituteTypeAliasInTy' was called from inner to outer, we only need to substitute current type.
    // Only need to substitute with given typeArgument for given alias target.
    TypeSubst typeMapping = GenerateTypeMapping(*target.declPtr, typeArgs);
    Ptr<Ty> type = typeManager.GetInstantiatedTy(aliasedType->ty, typeMapping);
    CJC_ASSERT(target.declPtr);
    return type;
}

namespace {
bool IsNodeDesugared(Ptr<const Node> node)
{
    if (auto expr = DynamicCast<Expr>(node)) {
        return expr->desugarExpr.get();
    }
    return false;
}

CacheKey GetCacheKeyForSyn(const ASTContext& ctx, Ptr<const Node> node)
{
    auto it = ctx.targetTypeMap.find(node);
    auto target = it != ctx.targetTypeMap.cend() ? it->second : nullptr;
    return CacheKey{
        .target = target, .isDesugared = IsNodeDesugared(node), .diagKey = DiagnosticCache::ExtractKey(ctx.diag)};
}

CacheKey GetCacheKeyForChk(const ASTContext& ctx, Ptr<const Node> node, Ptr<Ty> target)
{
    return CacheKey{
        .target = target, .isDesugared = IsNodeDesugared(node), .diagKey = DiagnosticCache::ExtractKey(ctx.diag)};
}

void RestoreCached(ASTContext& ctx, Ptr<Node> node, CacheEntry& cache, bool recoverDiag = true)
{
    if (Ty::IsInitialTy(node->ty)) {
        // if node cleared before, mark it must be rechecked in post-check before restoring its ty
        ctx.typeCheckCache[node].lastKey = {};
    }
    node->ty = cache.result;
    RestoreTargets(*node, cache.targets);
    if (recoverDiag) {
        cache.diags.Restore(ctx.diag);
    }
}
} // namespace

bool TypeChecker::TypeCheckerImpl::IsChecked(ASTContext& ctx, Node& node) const
{
    auto decl = As<ASTKind::DECL>(&node);
    auto type = As<ASTKind::TYPE>(&node);
    bool visitedDecl = decl && (decl->TestAttr(Attribute::IS_CHECK_VISITED) || decl->TestAttr(Attribute::IMPORTED));
    bool visitedExpr =
        !decl && !type && ctx.typeCheckCache[&node].lastKey.has_value() && typeManager.GetUnsolvedTyVars().empty();
    return Ty::IsTyCorrect(node.ty) && node.ty->kind != AST::TypeKind::TYPE_QUEST &&
        (visitedExpr || visitedDecl || (type && type->TestAttr(Attribute::IS_CHECK_VISITED)));
}

std::optional<Ptr<Ty>> TypeChecker::TypeCheckerImpl::PerformBasicChecksForSynthesize(
    ASTContext& ctx, Ptr<Node> node) const
{
    if (!node) {
        return {TypeManager::GetInvalidTy()};
    }
    // IS_BROKEN indicates that the node may be illegal, and it may cause unknown errors if it continues.
    if (node->TestAttr(Attribute::IS_BROKEN)) {
        node->ty = TypeManager::GetInvalidTy();
        return {node->ty};
    }
    if (IsChecked(ctx, *node)) {
        return {node->ty};
    }
    if (node->IsDecl() && !node->symbol) {
        return {node->ty};
    }
    return {};
}

Ptr<Ty> TypeChecker::TypeCheckerImpl::Synthesize(ASTContext& ctx, Ptr<Node> node)
{
    if (auto res = PerformBasicChecksForSynthesize(ctx, node)) {
        return *res;
    }
    ctx.typeCheckCache[node].lastKey = GetCacheKeyForSyn(ctx, node);
    ASTContext* curCtx = &ctx;
    // If decl belongs to another package node, then switch to another AST context according to package node.
    if (ci->GetSourcePackages().size() > 1 && node->curFile) {
        if (auto ctx1 = ci->GetASTContextByPackage(node->curFile->curPackage)) {
            curCtx = ctx1;
        }
    }

    if (auto decl = DynamicCast<Decl*>(node)) {
        auto stashDiagnoseStatus = diag.AutoStashDisableDiagnoseStatus();
        CheckAnnotations(ctx, *decl);
    }

    switch (node->astKind) {
        case ASTKind::FUNC_DECL: {
            auto stashDiagnoseStatus = diag.AutoStashDisableDiagnoseStatus();
            if (StaticAs<ASTKind::FUNC_DECL>(node)->ownerFunc) {
                // Needn't report any error when check the default param desugar function.
                auto ds = DiagSuppressor(diag);
                CheckFuncDecl(*curCtx, *StaticAs<ASTKind::FUNC_DECL>(node));
            } else {
                CheckFuncDecl(*curCtx, *StaticAs<ASTKind::FUNC_DECL>(node));
            }
            break;
        }
        case ASTKind::FUNC_BODY: {
            (void)CheckFuncBody(*curCtx, *StaticAs<ASTKind::FUNC_BODY>(node));
            break;
        }
        case ASTKind::BLOCK: {
            node->ty = SynBlock(*curCtx, *StaticAs<ASTKind::BLOCK>(node));
            break;
        }
        case ASTKind::FUNC_PARAM_LIST: {
            CheckFuncParamList(*curCtx, *StaticAs<ASTKind::FUNC_PARAM_LIST>(node));
            break;
        }
        case ASTKind::FUNC_PARAM: {
            node->ty = SynFuncParam(*curCtx, *StaticAs<ASTKind::FUNC_PARAM>(node));
            break;
        }
        case ASTKind::FUNC_ARG: {
            node->ty = SynFuncArg(*curCtx, *StaticAs<ASTKind::FUNC_ARG>(node));
            break;
        }
        case ASTKind::INC_OR_DEC_EXPR: {
            node->ty = SynIncOrDecExpr(*curCtx, *StaticAs<ASTKind::INC_OR_DEC_EXPR>(node));
            break;
        }
        case ASTKind::PAREN_EXPR: {
            node->ty = SynParenExpr(*curCtx, *StaticAs<ASTKind::PAREN_EXPR>(node));
            break;
        }
        case ASTKind::LAMBDA_EXPR: {
            node->ty = SynLamExpr(*curCtx, *StaticAs<ASTKind::LAMBDA_EXPR>(node));
            break;
        }
        case ASTKind::RETURN_EXPR: {
            node->ty = SynReturnExpr(*curCtx, *StaticAs<ASTKind::RETURN_EXPR>(node));
            break;
        }
        case ASTKind::LIT_CONST_EXPR: {
            node->ty = SynLitConstExpr(*curCtx, *StaticAs<ASTKind::LIT_CONST_EXPR>(node));
            break;
        }
        case ASTKind::WHILE_EXPR: {
            node->ty = SynWhileExpr(*curCtx, *StaticAs<ASTKind::WHILE_EXPR>(node));
            break;
        }
        case ASTKind::DO_WHILE_EXPR: {
            node->ty = SynDoWhileExpr(*curCtx, *StaticAs<ASTKind::DO_WHILE_EXPR>(node));
            break;
        }
        case ASTKind::FOR_IN_EXPR: {
            node->ty = SynForInExpr(*curCtx, *StaticAs<ASTKind::FOR_IN_EXPR>(node));
            break;
        }
        case ASTKind::UNARY_EXPR: {
            node->ty = SynUnaryExpr(*curCtx, *StaticAs<ASTKind::UNARY_EXPR>(node));
            break;
        }
        case ASTKind::BINARY_EXPR: {
            node->ty = SynBinaryExpr(*curCtx, *StaticAs<ASTKind::BINARY_EXPR>(node));
            break;
        }
        case ASTKind::ASSIGN_EXPR: {
            node->ty = SynAssignExpr(*curCtx, *StaticAs<ASTKind::ASSIGN_EXPR>(node));
            break;
        }
        case ASTKind::QUOTE_EXPR: {
            node->ty = SynQuoteExpr(*curCtx, *StaticAs<ASTKind::QUOTE_EXPR>(node));
            break;
        }
        case ASTKind::IF_EXPR: {
            node->ty = SynIfExpr(*curCtx, *StaticAs<ASTKind::IF_EXPR>(node));
            break;
        }
        case ASTKind::TRY_EXPR: {
            node->ty = SynTryExpr(*curCtx, *StaticAs<ASTKind::TRY_EXPR>(node));
            break;
        }
        case ASTKind::REF_EXPR: {
            auto re = StaticAs<ASTKind::REF_EXPR>(node);
            InferRefExpr(*curCtx, *re);
            break;
        }
        case ASTKind::PRIMITIVE_TYPE_EXPR: {
            auto te = StaticAs<ASTKind::PRIMITIVE_TYPE_EXPR>(node);
            te->ty = TypeManager::GetPrimitiveTy(te->typeKind);
            break;
        }
        case ASTKind::CALL_EXPR: {
            node->ty = SynCallExpr(*curCtx, *StaticAs<ASTKind::CALL_EXPR>(node));
            break;
        }
        case ASTKind::TRAIL_CLOSURE_EXPR: {
            node->ty = SynTrailingClosure(ctx, *StaticAs<ASTKind::TRAIL_CLOSURE_EXPR>(node));
            break;
        }
        case ASTKind::MEMBER_ACCESS: {
            auto ma = StaticAs<ASTKind::MEMBER_ACCESS>(node);
            InferMemberAccess(*curCtx, *ma);
            break;
        }
        case ASTKind::TYPE_CONV_EXPR: {
            node->ty = SynTypeConvExpr(*curCtx, *StaticAs<ASTKind::TYPE_CONV_EXPR>(node));
            break;
        }
        case ASTKind::IF_AVAILABLE_EXPR:
            node->ty = SynIfAvailableExpr(*curCtx, StaticCast<IfAvailableExpr>(*node));
            break;
        case ASTKind::ARRAY_LIT: {
            node->ty = SynArrayLit(*curCtx, *StaticAs<ASTKind::ARRAY_LIT>(node));
            break;
        }
        case ASTKind::ARRAY_EXPR: {
            auto ae = StaticAs<ASTKind::ARRAY_EXPR>(node);
            node->ty = ae->isValueArray ? SynVArrayExpr(*curCtx, *ae) : SynArrayExpr(*curCtx, *ae);
            break;
        }
        case ASTKind::POINTER_EXPR: {
            node->ty = SynPointerExpr(*curCtx, *StaticAs<ASTKind::POINTER_EXPR>(node));
            break;
        }
        case ASTKind::MATCH_EXPR: {
            node->ty = SynMatchExpr(*curCtx, *StaticAs<ASTKind::MATCH_EXPR>(node));
            break;
        }
        case ASTKind::IS_EXPR: {
            node->ty = SynIsExpr(*curCtx, *StaticAs<ASTKind::IS_EXPR>(node));
            break;
        }
        case ASTKind::AS_EXPR: {
            node->ty = SynAsExpr(*curCtx, *StaticAs<ASTKind::AS_EXPR>(node));
            break;
        }
        case ASTKind::OPTIONAL_CHAIN_EXPR: {
            node->ty = SynOptionalChainExpr(*curCtx, *StaticAs<ASTKind::OPTIONAL_CHAIN_EXPR>(node));
            break;
        }
        case ASTKind::ENUM_DECL: {
            CheckEnumDecl(*curCtx, *StaticAs<ASTKind::ENUM_DECL>(node));
            break;
        }
        case ASTKind::STRUCT_DECL: {
            auto sd = StaticAs<ASTKind::STRUCT_DECL>(node);
            CheckStructDecl(*curCtx, *sd);
            break;
        }
        case ASTKind::CLASS_DECL: {
            auto cd = StaticAs<ASTKind::CLASS_DECL>(node);
            CheckClassDecl(*curCtx, *cd);
            break;
        }
        case ASTKind::INTERFACE_DECL: {
            auto id = StaticAs<ASTKind::INTERFACE_DECL>(node);
            CheckInterfaceDecl(*curCtx, *id);
            break;
        }
        case ASTKind::TUPLE_LIT: {
            node->ty = SynTupleLit(*curCtx, *StaticAs<ASTKind::TUPLE_LIT>(node));
            break;
        }
        case ASTKind::JUMP_EXPR: {
            node->ty = SynLoopControlExpr(*curCtx, *StaticAs<ASTKind::JUMP_EXPR>(node));
            break;
        }
        case ASTKind::THROW_EXPR: {
            node->ty = SynThrowExpr(*curCtx, *StaticAs<ASTKind::THROW_EXPR>(node));
            break;
        }
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
        // Effect handlers are only enabled in the CJNative backend, for now
        case ASTKind::PERFORM_EXPR: {
            node->ty = SynPerformExpr(*curCtx, *StaticAs<ASTKind::PERFORM_EXPR>(node));
            break;
        }
        case ASTKind::RESUME_EXPR: {
            node->ty = SynResumeExpr(*curCtx, *StaticAs<ASTKind::RESUME_EXPR>(node));
            break;
        }
#endif // CANGJIE_CODEGEN_CJNATIVE_BACKEND
        case ASTKind::TYPE_ALIAS_DECL: {
            CheckTypeAlias(*curCtx, *StaticAs<ASTKind::TYPE_ALIAS_DECL>(node));
            break;
        }
        case ASTKind::SUBSCRIPT_EXPR: {
            node->ty = SynSubscriptExpr(*curCtx, *StaticAs<ASTKind::SUBSCRIPT_EXPR>(node));
            break;
        }
        case ASTKind::RANGE_EXPR: {
            node->ty = SynRangeExpr(*curCtx, *StaticAs<ASTKind::RANGE_EXPR>(node));
            break;
        }
        case ASTKind::TYPE:
        case ASTKind::INVALID_TYPE:
        case ASTKind::OPTION_TYPE:
        case ASTKind::THIS_TYPE:
        case ASTKind::PAREN_TYPE:
        case ASTKind::VARRAY_TYPE:
        case ASTKind::FUNC_TYPE:
        case ASTKind::TUPLE_TYPE:
        case ASTKind::PRIMITIVE_TYPE:
        case ASTKind::REF_TYPE:
            // Fall through !.
        case ASTKind::QUALIFIED_TYPE: {
            CheckReferenceTypeLegality(ctx, *StaticAs<ASTKind::TYPE>(node));
            break;
        }
        case ASTKind::PROP_DECL: {
            auto propDecl = StaticAs<ASTKind::PROP_DECL>(node);
            CheckPropDecl(*curCtx, *propDecl);
            break;
        }
        case ASTKind::VAR_WITH_PATTERN_DECL: {
            auto vpd = StaticAs<ASTKind::VAR_WITH_PATTERN_DECL>(node);
            CheckVarWithPatternDecl(*curCtx, *vpd);
            break;
        }
        case ASTKind::VAR_DECL: {
            if (IsGlobalOrMember(*node)) {
                // When varDecl is global or member variable, the diable status should be stashed.
                auto stashDiagnoseStatus = diag.AutoStashDisableDiagnoseStatus();
                CheckVarDecl(*curCtx, *StaticAs<ASTKind::VAR_DECL>(node));
            } else {
                CheckVarDecl(*curCtx, *StaticAs<ASTKind::VAR_DECL>(node));
            }
            break;
        }
        case ASTKind::SPAWN_EXPR: {
            node->ty = SynSpawnExpr(*curCtx, *StaticAs<ASTKind::SPAWN_EXPR>(node));
            break;
        }
        case ASTKind::SYNCHRONIZED_EXPR: {
            node->ty = SynSyncExpr(*curCtx, *StaticAs<ASTKind::SYNCHRONIZED_EXPR>(node));
            break;
        }
        case ASTKind::EXTEND_DECL: {
            CheckExtendDecl(*curCtx, *StaticAs<ASTKind::EXTEND_DECL>(node));
            break;
        }
        case ASTKind::MACRO_EXPAND_EXPR: // Just for LSP, normal process does not have macros here.
        case ASTKind::MACRO_EXPAND_PARAM:
        case ASTKind::MACRO_EXPAND_DECL: {
            CheckMacroCall(*curCtx, *node);
            break;
        }
        case ASTKind::INVALID_EXPR: {
            node->ty = TypeManager::GetInvalidTy();
            break;
        }
        default: {
            break;
        }
    }
    CJC_ASSERT(!Ty::IsTyCorrect(node->ty) || As<ASTKind::EXPR>(node) == nullptr ||
        StaticAs<ASTKind::EXPR>(node)->desugarExpr == nullptr ||
        node->ty == StaticAs<ASTKind::EXPR>(node)->desugarExpr->ty);
    node->ty = typeManager.TryGreedySubst(node->ty);
    return TypeManager::GetNonNullTy(node->ty);
}

std::optional<bool> TypeChecker::TypeCheckerImpl::PerformBasicChecksForCheck(
    ASTContext& ctx, Ptr<Ty> target, Ptr<Node> node) const
{
    if (!node) {
        return {false};
    }
    if (!Ty::IsTyCorrect(target)) {
        node->ty = TypeManager::GetInvalidTy();
        return {false};
    }
    ctx.targetTypeMap[node] = target;
    // IS_BROKEN indicates that the node may be illegal, and it may cause unknown errors if it continues.
    if (node->TestAttr(Attribute::IS_BROKEN)) {
        node->ty = TypeManager::GetInvalidTy();
        return {false};
    }
    if (target->HasQuestTy() && !IsQuestableNode(*node)) {
        return {false};
    }
    return {};
}

bool TypeChecker::TypeCheckerImpl::Check(ASTContext& ctx, Ptr<Ty> target, Ptr<Node> node)
{
    if (auto res = PerformBasicChecksForCheck(ctx, target, node)) {
        return *res;
    }
    ctx.typeCheckCache[node].lastKey = GetCacheKeyForChk(ctx, node, target);
    ASTContext* curCtx = &ctx;
    // If decl belongs to another package node, then switch to another AST context according to package node.
    if (ci->GetSourcePackages().size() > 1) {
        if (auto decl = AST::As<ASTKind::DECL>(node); decl && decl->curFile) {
            if (auto ctx1 = ci->GetASTContextByPackage(decl->curFile->curPackage)) {
                curCtx = ctx1;
            }
        }
    }

    bool chkRet = false;
    auto realTarget = typeManager.TryGreedySubst(target);
    if (realTarget->IsPlaceholder() && !AcceptPlaceholderTarget(*node)) {
        auto& cst = typeManager.constraints[RawStaticCast<GenericsTy*>(realTarget)];
        Ptr<Ty> lub = nullptr;
        if (!cst.ubs.empty()) {
            auto meetRes = JoinAndMeet(typeManager, cst.ubs.raw(), typeManager.GetUnsolvedTyVars()).MeetAsVisibleTy();
            if (std::holds_alternative<Ptr<Ty>>(meetRes)) {
                lub = std::get<Ptr<Ty>>(meetRes);
            }
        }
        if (lub) {
            chkRet = Check(ctx, lub, node) && typeManager.IsSubtype(node->ty, realTarget);
        } else {
            Synthesize(ctx, node);
            ReplaceIdealTy(*node);
            chkRet = typeManager.IsSubtype(node->ty, realTarget);
        }
    } else {
        switch (node->astKind) {
            case ASTKind::IF_EXPR: {
                chkRet = ChkIfExpr(*curCtx, *realTarget, *StaticAs<ASTKind::IF_EXPR>(node));
                break;
            }
            case ASTKind::ASSIGN_EXPR: {
                chkRet = ChkAssignExpr(*curCtx, *realTarget, *StaticAs<ASTKind::ASSIGN_EXPR>(node));
                break;
            }
            case ASTKind::LIT_CONST_EXPR: {
                auto lce = StaticAs<ASTKind::LIT_CONST_EXPR>(node);
                chkRet = ChkLitConstExpr(*curCtx, *realTarget, *lce);
                InitializeLitConstValue(*lce);
                break;
            }
            case ASTKind::ARRAY_LIT: {
                chkRet = ChkArrayLit(*curCtx, *realTarget, *StaticAs<ASTKind::ARRAY_LIT>(node));
                break;
            }
            case ASTKind::ARRAY_EXPR: {
                auto ae = StaticAs<ASTKind::ARRAY_EXPR>(node);
                chkRet = ae->isValueArray ? ChkVArrayExpr(*curCtx, *realTarget, *ae)
                                          : ChkArrayExpr(*curCtx, *realTarget, *ae);
                break;
            }
            case ASTKind::POINTER_EXPR: {
                chkRet = ChkPointerExpr(*curCtx, *realTarget, *StaticAs<ASTKind::POINTER_EXPR>(node));
                break;
            }
            case ASTKind::TUPLE_LIT: {
                chkRet = ChkTupleLit(*curCtx, *realTarget, *StaticAs<ASTKind::TUPLE_LIT>(node));
                break;
            }
            case ASTKind::WHILE_EXPR: {
                chkRet = ChkWhileExpr(*curCtx, *realTarget, *StaticAs<ASTKind::WHILE_EXPR>(node));
                break;
            }
            case ASTKind::DO_WHILE_EXPR: {
                chkRet = ChkDoWhileExpr(*curCtx, *realTarget, *StaticAs<ASTKind::DO_WHILE_EXPR>(node));
                break;
            }
            case ASTKind::FOR_IN_EXPR: {
                chkRet = ChkForInExpr(*curCtx, *realTarget, *StaticAs<ASTKind::FOR_IN_EXPR>(node));
                break;
            }
            case ASTKind::RANGE_EXPR: {
                chkRet = ChkRangeExpr(*curCtx, *realTarget, *StaticAs<ASTKind::RANGE_EXPR>(node));
                break;
            }
            case ASTKind::PAREN_EXPR: {
                chkRet = ChkParenExpr(*curCtx, *realTarget, *StaticAs<ASTKind::PAREN_EXPR>(node));
                break;
            }
            case ASTKind::BINARY_EXPR: {
                chkRet = ChkBinaryExpr(ctx, *realTarget, *StaticAs<ASTKind::BINARY_EXPR>(node));
                break;
            }
            case ASTKind::INC_OR_DEC_EXPR: {
                chkRet = ChkIncOrDecExpr(*curCtx, *realTarget, *StaticAs<ASTKind::INC_OR_DEC_EXPR>(node));
                break;
            }
            case ASTKind::UNARY_EXPR: {
                chkRet = ChkUnaryExpr(*curCtx, *realTarget, *StaticAs<ASTKind::UNARY_EXPR>(node));
                break;
            }
            case ASTKind::TYPE_CONV_EXPR: {
                chkRet = ChkTypeConvExpr(*curCtx, *realTarget, *StaticAs<ASTKind::TYPE_CONV_EXPR>(node));
                break;
            }
            case ASTKind::IF_AVAILABLE_EXPR:
                chkRet = ChkIfAvailableExpr(*curCtx, *realTarget, StaticCast<IfAvailableExpr>(*node));
                break;
            case ASTKind::JUMP_EXPR:
                chkRet = ChkLoopControlExpr(*curCtx, *StaticAs<ASTKind::JUMP_EXPR>(node));
                break;
            case ASTKind::MATCH_EXPR: {
                chkRet = ChkMatchExpr(*curCtx, *realTarget, *StaticAs<ASTKind::MATCH_EXPR>(node));
                break;
            }
                // These patterns are supported, so do type infer framework.
            case ASTKind::EXCEPT_TYPE_PATTERN:
            case ASTKind::COMMAND_TYPE_PATTERN:
            case ASTKind::WILDCARD_PATTERN:
            case ASTKind::CONST_PATTERN:
            case ASTKind::TYPE_PATTERN:
            case ASTKind::VAR_PATTERN:
            case ASTKind::TUPLE_PATTERN:
            case ASTKind::ENUM_PATTERN:
            case ASTKind::VAR_OR_ENUM_PATTERN: {
                // Use type check of local type infer framework.
                chkRet = ChkPattern(*curCtx, *realTarget, *StaticAs<ASTKind::PATTERN>(node));
                break;
            }
            case ASTKind::BLOCK: {
                chkRet = ChkBlock(*curCtx, *realTarget, *StaticAs<ASTKind::BLOCK>(node));
                break;
            }
            case ASTKind::SUBSCRIPT_EXPR: {
                chkRet = ChkSubscriptExpr(*curCtx, realTarget, *StaticAs<ASTKind::SUBSCRIPT_EXPR>(node));
                break;
            }
            case ASTKind::MEMBER_ACCESS: // Fall-through.
            case ASTKind::REF_EXPR: {
                chkRet = ChkRefExpr(*curCtx, *realTarget, *RawStaticCast<NameReferenceExpr*>(node));
                break;
            }
            case ASTKind::CALL_EXPR: {
                chkRet = ChkCallExpr(*curCtx, realTarget, *StaticAs<ASTKind::CALL_EXPR>(node));
                break;
            }
            case ASTKind::TRAIL_CLOSURE_EXPR: {
                chkRet = ChkTrailingClosureExpr(*curCtx, *realTarget, *StaticAs<ASTKind::TRAIL_CLOSURE_EXPR>(node));
                break;
            }
            case ASTKind::TRY_EXPR: {
                chkRet = ChkTryExpr(*curCtx, *realTarget, *StaticAs<ASTKind::TRY_EXPR>(node));
                break;
            }
            case ASTKind::RETURN_EXPR: {
                chkRet = ChkReturnExpr(*curCtx, *StaticAs<ASTKind::RETURN_EXPR>(node));
                break;
            }
            case ASTKind::LAMBDA_EXPR: {
                chkRet = ChkLamExpr(*curCtx, *realTarget, *StaticAs<ASTKind::LAMBDA_EXPR>(node));
                break;
            }
            case ASTKind::QUOTE_EXPR: {
                chkRet = ChkQuoteExpr(*curCtx, *realTarget, *StaticAs<ASTKind::QUOTE_EXPR>(node));
                break;
            }
            case ASTKind::FUNC_PARAM: {
                chkRet = ChkFuncParam(*curCtx, *realTarget, *StaticAs<ASTKind::FUNC_PARAM>(node));
                break;
            }
            case ASTKind::FUNC_ARG: {
                chkRet = ChkFuncArg(*curCtx, *realTarget, *StaticAs<ASTKind::FUNC_ARG>(node));
                break;
            }
            case ASTKind::SPAWN_EXPR: {
                chkRet = ChkSpawnExpr(*curCtx, *realTarget, *StaticAs<ASTKind::SPAWN_EXPR>(node));
                break;
            }
            case ASTKind::SYNCHRONIZED_EXPR: {
                chkRet = ChkSyncExpr(*curCtx, realTarget, *StaticAs<ASTKind::SYNCHRONIZED_EXPR>(node));
                break;
            }
            case ASTKind::IS_EXPR: {
                chkRet = ChkIsExpr(*curCtx, *realTarget, *StaticAs<ASTKind::IS_EXPR>(node));
                break;
            }
            case ASTKind::AS_EXPR: {
                chkRet = ChkAsExpr(*curCtx, *realTarget, *StaticAs<ASTKind::AS_EXPR>(node));
                break;
            }
            case ASTKind::OPTIONAL_CHAIN_EXPR: {
                chkRet = ChkOptionalChainExpr(*curCtx, *realTarget, *StaticAs<ASTKind::OPTIONAL_CHAIN_EXPR>(node));
                break;
            }
            case ASTKind::MACRO_EXPAND_EXPR: // Just for LSP, normal process does not have macros here.
            case ASTKind::MACRO_EXPAND_PARAM:
            case ASTKind::MACRO_EXPAND_DECL: {
                CheckMacroCall(*curCtx, *node);
                break;
            }
            default: {
                Synthesize(ctx, node);
                ReplaceIdealTy(*node);
                chkRet = typeManager.IsSubtype(node->ty, realTarget);
                break;
            }
        }
    }
    CJC_ASSERT(!Ty::IsTyCorrect(node->ty) || As<ASTKind::EXPR>(node) == nullptr ||
        StaticAs<ASTKind::EXPR>(node)->desugarExpr == nullptr ||
        node->ty == StaticAs<ASTKind::EXPR>(node)->desugarExpr->ty);
    ctx.targetTypeMap[node] = nullptr;
    node->ty = typeManager.TryGreedySubst(node->ty);
    return chkRet;
}

void TypeChecker::TypeCheckerImpl::CheckConstructor(ASTContext& ctx, const Decl& decl, FuncDecl& fd)
{
    if (decl.astKind != ASTKind::CLASS_DECL && decl.astKind != ASTKind::STRUCT_DECL) {
        return;
    }
    if (!fd.funcBody) {
        return;
    }
    bool hasBrokenBody = fd.funcBody->generic && !fd.TestAttr(Attribute::STATIC) && !HasJavaAttr(decl);
    if (hasBrokenBody) {
        diag.Diagnose(fd, DiagKind::sema_forbid_generic_constructor, fd.identifier.Val());
    }
    if (fd.funcBody->paramLists.size() > 1) {
        diag.Diagnose(*fd.funcBody->paramLists[0], DiagKind::sema_cannot_currying, "constructor");
        hasBrokenBody = true;
    }
    if (hasBrokenBody) {
        fd.funcBody->EnableAttr(Attribute::IS_BROKEN);
        fd.EnableAttr(Attribute::HAS_BROKEN);
    }
    if (hasBrokenBody || !fd.funcBody->body || fd.funcBody->body->body.empty()) {
        return;
    }
    Ptr<Node> firstExprOrDecl = fd.funcBody->body->body.front().get();
    if (firstExprOrDecl == nullptr) {
        return;
    }
    bool needEraseSuper{false};
    if (auto ce = AST::As<ASTKind::CALL_EXPR>(firstExprOrDecl); ce) {
        CheckConstructorSuper(*ce, decl, fd, needEraseSuper);
    }
    CheckCallsInConstructor(ctx, decl, fd, *firstExprOrDecl, needEraseSuper);
}

void TypeChecker::TypeCheckerImpl::CheckCallsInConstructor(
    ASTContext& ctx, const Decl& decl, FuncDecl& fd, Node& firstExprOrDecl, bool needEraseSuper)
{
    CJC_ASSERT(!fd.funcBody->body->body.empty()); // Caller guarantees.
    auto firstExprOrDeclPtr = &firstExprOrDecl;
    std::vector<Ptr<CallExpr>> calls;
    Walker(fd.funcBody.get(), [&calls](auto node) {
        if (auto ce = DynamicCast<CallExpr*>(node); ce) {
            calls.emplace_back(ce);
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    for (auto ce : calls) {
        if (ce->baseFunc == nullptr || ce->baseFunc->astKind != ASTKind::REF_EXPR) {
            continue;
        }
        auto re = RawStaticCast<RefExpr*>(ce->baseFunc.get());
        if (!re->isThis && !re->isSuper) {
            continue;
        }
        if (re->isThis && fd.TestAttr(Attribute::PRIMARY_CONSTRUCTOR)) {
            diag.Diagnose(*ce, DiagKind::sema_illegal_place_of_calling_this_primary_constructor);
        } else if (ce != firstExprOrDeclPtr) {
            Ptr<TrailingClosureExpr> tce = nullptr;
            if (firstExprOrDeclPtr && firstExprOrDeclPtr->astKind == ASTKind::TRAIL_CLOSURE_EXPR) {
                tce = RawStaticCast<TrailingClosureExpr*>(firstExprOrDeclPtr);
            }
            auto notIllegalPlace = !tce || (ce->TestAttr(Attribute::COMPILER_ADD) && ce != tce->desugarExpr.get());
            if (notIllegalPlace) {
                diag.Diagnose(*ce, DiagKind::sema_illegal_place_of_calling_this_or_super, re->ref.identifier.Val(),
                    decl.astKind == ASTKind::CLASS_DECL ? "class" : "struct", decl.identifier.Val());
            }
        }
        // Only should remove super call when current callExpr is the first expression in the function body.
        auto needReset = needEraseSuper && re->isSuper && ce->TestAttr(Attribute::COMPILER_ADD) && ce->args.empty() &&
            decl.astKind == ASTKind::CLASS_DECL && ce == fd.funcBody->body->body.front().get();
        if (needReset) {
            needEraseSuper = false; // Avoid re-enter erasion.
            // remove compiler added super() when there is no non-parameter constructor in super class
            auto it = fd.funcBody->body->body.begin();
            ctx.DeleteInvertedIndexes(it->get());
            it->reset();
            fd.funcBody->body->body.erase(it);
            firstExprOrDeclPtr = nullptr;
        }
        if (decl.astKind == ASTKind::STRUCT_DECL) {
            fd.constructorCall = ConstructorCall::OTHER_INIT;
        }
    }
}

Ptr<Ty> TypeChecker::TypeCheckerImpl::SynthesizeWithCache(ASTContext& ctx, Ptr<Node> node)
{
    CJC_NULLPTR_CHECK(node);
    if (!typeManager.GetUnsolvedTyVars().empty()) {
        return Synthesize(ctx, node);
    }
    CacheKey key = GetCacheKeyForSyn(ctx, node);
    if (ctx.typeCheckCache[node].synCache.count(key) != 0) {
        auto& cache = ctx.typeCheckCache[node].synCache[key];
        RestoreCached(ctx, node, cache);
        return cache.result;
    } else {
        return SynthesizeAndCache(ctx, node, key);
    }
}

bool TypeChecker::TypeCheckerImpl::CheckWithCache(ASTContext& ctx, Ptr<Ty> target, Ptr<Node> node)
{
    CJC_NULLPTR_CHECK(node);
    if (!typeManager.GetUnsolvedTyVars().empty()) {
        return Check(ctx, target, node);
    }
    CacheKey key = GetCacheKeyForChk(ctx, node, target);
    if (ctx.typeCheckCache[node].chkCache.count(key) != 0) {
        auto& cache = ctx.typeCheckCache[node].chkCache[key];
        RestoreCached(ctx, node, cache);
        return cache.successful;
    } else {
        return CheckAndCache(ctx, target, node, key);
    }
}

Ptr<Ty> TypeChecker::TypeCheckerImpl::SynthesizeWithNegCache(ASTContext& ctx, Ptr<Node> node)
{
    CJC_NULLPTR_CHECK(node);
    if (!typeManager.GetUnsolvedTyVars().empty()) {
        return Synthesize(ctx, node);
    }
    CacheKey key = GetCacheKeyForSyn(ctx, node);
    if (ctx.typeCheckCache[node].synCache.count(key) != 0 && !ctx.typeCheckCache[node].synCache[key].successful) {
        auto& cache = ctx.typeCheckCache[node].synCache[key];
        RestoreCached(ctx, node, cache);
        return cache.result;
    } else {
        return SynthesizeAndCache(ctx, node, key);
    }
}

bool TypeChecker::TypeCheckerImpl::CheckWithNegCache(ASTContext& ctx, Ptr<Ty> target, Ptr<Node> node)
{
    CJC_NULLPTR_CHECK(node);
    if (!typeManager.GetUnsolvedTyVars().empty()) {
        return Check(ctx, target, node);
    }
    CacheKey key = GetCacheKeyForChk(ctx, node, target);
    if (ctx.typeCheckCache[node].chkCache.count(key) != 0 && !ctx.typeCheckCache[node].chkCache[key].successful) {
        auto& cache = ctx.typeCheckCache[node].chkCache[key];
        RestoreCached(ctx, node, cache);
        return false;
    } else {
        return CheckAndCache(ctx, target, node, key);
    }
}

bool TypeChecker::TypeCheckerImpl::CheckWithEffectiveCache(
    ASTContext& ctx, Ptr<Ty> target, Ptr<Node> node, bool recoverDiag)
{
    if (!typeManager.GetUnsolvedTyVars().empty() || !node) {
        return Check(ctx, target, node);
    }
    CacheKey key = GetCacheKeyForChk(ctx, node, target);
    if (!Ty::IsInitialTy(node->ty)) {
        if (ctx.typeCheckCache[node].lastKey && ctx.typeCheckCache[node].lastKey.value() == key) {
            if (ctx.typeCheckCache[node].chkCache.count(key) != 0) {
                auto& cache = ctx.typeCheckCache[node].chkCache[key];
                RestoreCached(ctx, node, cache, recoverDiag);
                return cache.successful;
            } else {
                return CheckAndCache(ctx, target, node, key);
            }
        }
    }
    return CheckAndCache(ctx, target, node, key);
}

Ptr<Ty> TypeChecker::TypeCheckerImpl::SynthesizeWithEffectiveCache(ASTContext& ctx, Ptr<Node> node, bool recoverDiag)
{
    if (!typeManager.GetUnsolvedTyVars().empty() || !node) {
        return Synthesize(ctx, node);
    }
    CacheKey key = GetCacheKeyForSyn(ctx, node);
    if (!Ty::IsInitialTy(node->ty)) {
        if (ctx.typeCheckCache[node].lastKey && ctx.typeCheckCache[node].lastKey.value() == key) {
            if (ctx.typeCheckCache[node].synCache.count(key) != 0) {
                auto& cache = ctx.typeCheckCache[node].synCache[key];
                RestoreCached(ctx, node, cache, recoverDiag);
                return cache.result;
            } else {
                return SynthesizeAndCache(ctx, node, key);
            }
        }
    }
    return SynthesizeAndCache(ctx, node, key);
}

Ptr<AST::Ty> TypeChecker::TypeCheckerImpl::SynthesizeAndCache(ASTContext& ctx, Ptr<AST::Node> node, const CacheKey& key)
{
    DiagnosticCache dc;
    dc.ToExclude(ctx.diag);
    auto ret = Synthesize(ctx, node);
    dc.BackUp(ctx.diag);
    ctx.typeCheckCache[node].synCache[key] = {.successful = ret && Ty::IsTyCorrect(ret) && dc.NoError(),
        .result = ret,
        .diags = std::move(dc),
        .targets = CollectTargets(*node)};
    return ret;
}

bool TypeChecker::TypeCheckerImpl::CheckAndCache(ASTContext& ctx, Ptr<Ty> target, Ptr<Node> node, const CacheKey& key)
{
    DiagnosticCache dc;
    dc.ToExclude(ctx.diag);
    bool ret = Check(ctx, target, node);
    dc.BackUp(ctx.diag);
    ctx.typeCheckCache[node].chkCache[key] = {
        .successful = ret, .result = node->ty, .diags = std::move(dc), .targets = CollectTargets(*node)};
    return ret;
}

namespace {
/**  Check if a class has a non-parameter constructor. */
bool HasNonParamCtorForClass(const ClassDecl& classDecl)
{
    for (auto& decl : classDecl.body->decls) {
        auto fd = DynamicCast<FuncDecl*>(decl.get());
        if (!fd || !IsInstanceConstructor(*fd)) {
            continue;
        }
        if (fd->funcBody->paramLists.empty()) {
            return false;
        }
        auto& params = fd->funcBody->paramLists[0]->params;
        if (params.empty()) {
            if (fd->TestAttr(Attribute::PRIVATE)) {
                return false;
            }
            return true;
        }
        bool allHasDefaultValue{true};
        for (auto& param : params) {
            if (!(param->assignment)) {
                allHasDefaultValue = false;
                break;
            }
        }
        if (allHasDefaultValue) {
            if (fd->TestAttr(Attribute::PRIVATE)) {
                // There may exist another constructor with different number of parameters which all have default value.
                continue;
            }
            return true;
        }
    }
    return false;
}
} // namespace

void TypeChecker::TypeCheckerImpl::CheckConstructorSuper(
    const CallExpr& ce, const Decl& decl, FuncDecl& fd, bool& needEraseSuper)
{
    if (!ce.baseFunc || ce.baseFunc->astKind != ASTKind::REF_EXPR) {
        return;
    }
    auto re = RawStaticCast<RefExpr*>(ce.baseFunc.get());
    if (!re->isSuper || !ce.TestAttr(Attribute::COMPILER_ADD) || !ce.args.empty()) {
        return;
    }
    if (decl.astKind != ASTKind::CLASS_DECL) {
        return;
    }
    auto cd = RawStaticCast<const ClassDecl*>(&decl);
    // remove compiler added super() when there is no non-parameter constructor in super class
    Ptr<ClassDecl> superCD = cd->GetSuperClassDecl();
    needEraseSuper = superCD && !HasNonParamCtorForClass(*superCD);
    if (!needEraseSuper) {
        return;
    }
    auto& fdName = fd.TestAttr(Attribute::PRIMARY_CONSTRUCTOR) ? fd.identifierForLsp : fd.identifier.Val();
    auto range =
        fd.TestAttr(Attribute::IMPLICIT_ADD) ? MakeRange(decl.identifier) : MakeRange(fd.identifier.Begin(), fdName);
    (void)diag.DiagnoseRefactor(DiagKindRefactor::sema_no_non_param_constructor_in_super_class, fd, range);
    fd.constructorCall = ConstructorCall::NONE;
}

void TypeChecker::TypeCheckerImpl::CheckFinalizer(const FuncDecl& fd)
{
    if (!fd.funcBody) {
        return;
    }
    bool invalidGeneric =
        fd.funcBody->generic && !fd.TestAttr(Attribute::STATIC) && fd.outerDecl && !HasJavaAttr(*fd.outerDecl);
    if (invalidGeneric) {
        diag.Diagnose(fd, DiagKind::sema_forbid_generic_finalizer, fd.identifier.Val());
    }
    if (!fd.funcBody->paramLists.empty()) {
        if (fd.funcBody->paramLists.size() > 1) {
            diag.Diagnose(*fd.funcBody->paramLists[0], DiagKind::sema_cannot_currying, "finalizer");
        }
        if (!fd.funcBody->paramLists[0]->params.empty()) {
            diag.Diagnose(fd, DiagKind::sema_cannot_have_parameter, "finalizer");
        }
    }
}

void TypeChecker::TypeCheckerImpl::CheckPrimaryCtorForClassOrStruct(InheritableDecl& id)
{
    if (id.TestAttr(Attribute::IMPORTED)) {
        return; // Do not desugar primary ctor for imported type decl.
    }
    bool primaryCtor = false;
    bool hasDesugared = false; // For incremental case.
    Ptr<PrimaryCtorDecl> target = nullptr;
    for (auto& decl : id.GetMemberDecls()) {
        if (auto fd = DynamicCast<PrimaryCtorDecl*>(decl.get()); fd) {
            if (primaryCtor) {
                auto typeName = id.astKind == ASTKind::CLASS_DECL ? "class" : "struct";
                diag.Diagnose(*fd, DiagKind::sema_multiple_primary_constructors, typeName, id.identifier.Val());
            } else {
                primaryCtor = true;
                target = fd;
            }
            if (fd->funcBody && fd->funcBody->generic) {
                diag.Diagnose(*decl, DiagKind::sema_forbid_generic_constructor, decl->identifier.Val());
            }
            if (fd->funcBody && fd->funcBody->paramLists.size() > 1) {
                diag.Diagnose(*fd->funcBody->paramLists[0], DiagKind::sema_cannot_currying, "constructor");
            }
        }
        if (decl->TestAttr(Attribute::PRIMARY_CONSTRUCTOR) && decl->astKind == ASTKind::FUNC_DECL) {
            hasDesugared = true;
        }
    }
    // The corresponding init constructor with primary constructor auto-generated
    if (target != nullptr && !hasDesugared) {
        DesugarPrimaryCtor(id, *target);
    }
}

void TypeChecker::TypeCheckerImpl::TypeCheckCompositeBody(
    ASTContext& ctx, const Decl& structDecl, const std::vector<OwnedPtr<Decl>>& body)
{
    for (auto& decl : body) {
        CJC_ASSERT(decl);
        if (auto fd = DynamicCast<FuncDecl*>(decl.get()); fd) {
            if (fd->TestAttr(Attribute::CONSTRUCTOR)) {
                CheckConstructor(ctx, structDecl, *fd);
            }
            if (fd->IsFinalizer()) {
                CheckFinalizer(*fd);
            }
        }
        Synthesize(ctx, decl.get());
        CheckCTypeMember(*decl);
    }
}

void TypeChecker::TypeCheckerImpl::CheckJavaInteropLibImport(Decl& decl)
{
    constexpr auto INTEROPLIB_JAVA_PACKAGE_NAME = "interoplib.interop";
    auto interopPackage = importManager.GetPackageDecl(INTEROPLIB_JAVA_PACKAGE_NAME);
    if (!interopPackage) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_java_mirror_interoplib_must_be_imported, decl);
        decl.EnableAttr(Attribute::IS_BROKEN);
    }
}

void TypeChecker::TypeCheckerImpl::CheckObjCInteropLibImport(Decl& decl)
{
    constexpr auto INTEROPLIB_OBJ_C_PACKAGE_NAME = "interoplib.objc";
    auto interopPackage = importManager.GetPackageDecl(INTEROPLIB_OBJ_C_PACKAGE_NAME);
    if (!interopPackage) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_objc_mirror_interoplib_must_be_imported, decl);
        decl.EnableAttr(Attribute::IS_BROKEN);
    }
}

bool TypeChecker::TypeCheckerImpl::IsCapturedInCFuncLambda(const ASTContext& ctx, const AST::RefExpr& re) const
{
    if (re.ref.target == nullptr || re.ref.target->fullPackageName != ctx.fullPackageName) {
        return false;
    }
    auto fb = GetCurFuncBody(ctx, re.scopeName);
    auto targetFb = GetCurFuncBody(ctx, re.ref.target->scopeName);
    // Fast path 1: RefExpr and target are in the same function body. Or target is a global node.
    if (fb == nullptr || targetFb == nullptr || fb == targetFb || re.ref.target->TestAttr(Attribute::GLOBAL)) {
        return false;
    }
    // Fast path 2: RefExpr and target are not in the same function body and current function is a CFunc.
    if (auto fty = DynamicCast<AST::FuncTy*>(fb->ty); fty && fty->isC) {
        return true;
    }
    if (!Is<AST::VarDecl*>(re.ref.target) && !Is<AST::FuncDecl*>(re.ref.target)) {
        return false;
    }
    // Check whether outer function of current function body is CFunc.
    auto outerFb = GetCurFuncBody(ctx, ScopeManagerApi::GetParentScopeName(fb->scopeName));
    while (outerFb != nullptr) {
        if (outerFb == targetFb) {
            return false;
        }
        if (auto fty = DynamicCast<AST::FuncTy*>(outerFb->ty); fty && fty->isC) {
            return true;
        }
        outerFb = GetCurFuncBody(ctx, ScopeManagerApi::GetParentScopeName(outerFb->scopeName));
    }
    return false;
}

void TypeChecker::TypeCheckerImpl::CheckLegalUseOfClosure(Expr& e, DiagKind kind) const
{
    if (e.TestAttr(Attribute::IS_BROKEN)) {
        return;
    }
    if (auto target = DynamicCast<LambdaExpr*>(&e); target && target->funcBody) {
        if (target->funcBody->captureKind == CaptureKind::CAPTURE_VAR) {
            DiagUseClosureCaptureVarAlone(diag, e);
        } else if (target->funcBody->captureKind == CaptureKind::TRANSITIVE_CAPTURE) {
            diag.Diagnose(e, kind, "lambda", "transitively", "lambda");
        }
    }
    if (auto ref = DynamicCast<RefExpr*>(&e); ref) {
        if (auto target = DynamicCast<FuncDecl*>(ref->ref.target); target) {
            if (target->funcBody->captureKind == CaptureKind::CAPTURE_VAR) {
                DiagUseClosureCaptureVarAlone(diag, e);
            } else if (target->funcBody->captureKind == CaptureKind::TRANSITIVE_CAPTURE) {
                diag.Diagnose(e, kind, target->identifier.Val(), "transitively", target->identifier.Val());
            }
        }
    }
}

void TypeChecker::TypeCheckerImpl::CheckLegalUseOfClosure(const ASTContext& ctx, Node& node) const
{
    if (auto vd = DynamicCast<VarDecl*>(&node); vd) {
        if (vd->initializer) {
            CheckLegalUseOfClosure(*vd->initializer, DiagKind::sema_func_capture_var_cannot_assign);
        }
    } else if (auto re = DynamicCast<ReturnExpr*>(&node); re) {
        if (re->expr) {
            CheckLegalUseOfClosure(*re->expr, DiagKind::sema_func_capture_var_cannot_return);
        }
    } else if (auto ce = DynamicCast<CallExpr*>(&node); ce) {
        if (auto baseRe = DynamicCast<RefExpr*>(ce->baseFunc.get()); baseRe && IsCapturedInCFuncLambda(ctx, *baseRe)) {
            diag.Diagnose(*baseRe, DiagKind::sema_cfunc_cannot_capture_var, baseRe->ref.identifier.Val());
        }
        for (auto& arg : ce->args) {
            if (arg && arg->expr) {
                CheckLegalUseOfClosure(*arg->expr, DiagKind::sema_func_capture_var_cannot_param);
            }
        }
    } else if (auto ref = DynamicCast<RefExpr*>(&node); ref && !ref->isBaseFunc) {
        CheckLegalUseOfClosure(*ref, DiagKind::sema_func_capture_var_cannot_expr);
        if (auto refVd = DynamicCast<VarDecl*>(ref->ref.target); refVd) {
            if (IsCapturedCStructOfClosure(*refVd)) {
                diag.Diagnose(*ref, DiagKind::sema_func_capture_var_not_ctype);
            }
        }
        if (IsCapturedInCFuncLambda(ctx, *ref)) {
            diag.Diagnose(*ref, DiagKind::sema_cfunc_cannot_capture_var, ref->ref.identifier.Val());
        }
    } else if (auto lambda = DynamicCast<LambdaExpr*>(&node); lambda && !lambda->isBaseFunc) {
        CheckLegalUseOfClosure(*lambda, DiagKind::sema_func_capture_var_cannot_expr);
    }
}

bool TypeChecker::TypeCheckerImpl::IsCapturedCStructOfClosure(const VarDecl& decl) const
{
    return decl.TestAttr(Attribute::IS_CAPTURE) && decl.ty && Ty::IsCTypeConstraint(*decl.ty);
}

void TypeChecker::TypeCheckerImpl::CheckCHIRClassDependencies()
{
    auto objectDecl = importManager.GetImportedDecl(CORE_PACKAGE_NAME, "Object");
    if (objectDecl == nullptr) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_core_object_not_found_when_no_prelude, DEFAULT_POSITION);
    }
}

namespace {
void MarkOverflow(Node& node)
{
    Walker checkOverflowWalker(&node, nullptr, [](Ptr<Node> node) -> VisitAction {
        switch (node->astKind) {
            case ASTKind::LIT_CONST_EXPR: {
                auto lce = StaticAs<ASTKind::LIT_CONST_EXPR>(node);
                if (lce->ty && lce->ty->IsInteger()) {
                    auto primitiveTy = RawStaticCast<PrimitiveTy*>(lce->ty);
                    lce->constNumValue.asInt.SetOutOfRange(primitiveTy);
                }
                return VisitAction::WALK_CHILDREN;
            }
            default: {
                return VisitAction::WALK_CHILDREN;
            }
        }
    });
    checkOverflowWalker.Walk();
}

void AddBuiltInArrayDecl(Package& pkg)
{
    auto bid = MakeOwned<BuiltInDecl>(BuiltInType::ARRAY);
    bid->identifier = RAW_ARRAY_NAME;
    bid->generic = MakeOwned<Generic>();
    auto gpd = MakeOwnedNode<GenericParamDecl>();
    gpd->identifier = "T";
    gpd->outerDecl = bid.get();
    bid->generic->typeParameters.emplace_back(std::move(gpd));
    bid->EnableAttr(Attribute::GLOBAL, Attribute::GENERIC);
    CopyFileID(bid.get(), pkg.files[0].get());
    pkg.files[0]->decls.emplace_back(std::move(bid)); // Caller guarantees file not empty.
}

void AddBuiltInPointerDecl(Package& pkg)
{
    auto bid = MakeOwned<BuiltInDecl>(BuiltInType::POINTER);
    bid->identifier = CPOINTER_NAME;
    bid->generic = MakeOwned<Generic>();
    auto gpd = MakeOwnedNode<GenericParamDecl>();
    gpd->identifier = "T";
    gpd->outerDecl = bid.get();
    bid->generic->typeParameters.emplace_back(std::move(gpd));
    bid->generic->genericConstraints.emplace_back(CreateConstraintForFFI(CTYPE_NAME));

    bid->EnableAttr(Attribute::PUBLIC, Attribute::GLOBAL, Attribute::GENERIC);
    CopyFileID(bid.get(), pkg.files[0].get());
    pkg.files[0]->decls.emplace_back(std::move(bid)); // Caller guarantees file not empty.
}

void AddBuiltInCStringDecl(Package& pkg)
{
    auto bid = MakeOwned<BuiltInDecl>(BuiltInType::CSTRING);
    bid->identifier = CSTRING_NAME;
    bid->generic = nullptr;
    bid->EnableAttr(Attribute::PUBLIC, Attribute::GLOBAL);
    CopyFileID(bid.get(), pkg.files[0].get());
    pkg.files[0]->decls.emplace_back(std::move(bid)); // Caller guarantees file not empty.
}

void AddBuiltInVArrayDecl(Package& pkg)
{
    auto bid = MakeOwned<BuiltInDecl>(BuiltInType::VARRAY);
    bid->identifier = VARRAY_NAME;
    bid->generic = MakeOwned<Generic>();
    auto gpd = MakeOwnedNode<GenericParamDecl>();
    gpd->identifier = "T";
    gpd->outerDecl = bid.get();
    bid->generic->typeParameters.emplace_back(std::move(gpd));
    bid->EnableAttr(Attribute::GLOBAL);
    bid->EnableAttr(Attribute::PUBLIC);
    bid->EnableAttr(Attribute::GENERIC);
    CopyFileID(bid.get(), pkg.files[0].get());
    pkg.files[0]->decls.emplace_back(std::move(bid)); // Caller guarantees file not empty.
}

void AddBuiltinCFuncDecl(Package& pkg)
{
    auto bid = MakeOwned<BuiltInDecl>(BuiltInType::CFUNC);
    bid->identifier = CFUNC_NAME;
    bid->generic = MakeOwned<Generic>();
    auto gpd = MakeOwnedNode<GenericParamDecl>();
    gpd->identifier = "T";
    gpd->outerDecl = &*bid;
    bid->generic->typeParameters.push_back(std::move(gpd));
    bid->EnableAttr(Attribute::GLOBAL);
    bid->EnableAttr(Attribute::PUBLIC);
    bid->EnableAttr(Attribute::GENERIC);
    CopyFileID(&*bid, &*pkg.files[0]);
    pkg.files[0]->decls.push_back(std::move(bid));
}

void AddAttrForDefaultFuncParam(Package& pkg)
{
    Walker(&pkg, [](auto node) {
        if (node->astKind != ASTKind::FUNC_DECL) {
            return VisitAction::WALK_CHILDREN;
        }
        auto fd = StaticAs<ASTKind::FUNC_DECL>(node);
        if (!fd->funcBody || fd->funcBody->paramLists.empty()) {
            return VisitAction::SKIP_CHILDREN;
        }
        for (auto& param : fd->funcBody->paramLists[0]->params) {
            if (param->desugarDecl) {
                if (fd->TestAttr(Attribute::GLOBAL)) {
                    param->desugarDecl->EnableAttr(Attribute::GLOBAL);
                }
            }
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
}

void MarkImplicitUsedFunctions(const Package& pkg)
{
    // NOTE: These are special toplevel functions that are not exported but will be reference in other package.
    static const std::unordered_map<std::string, std::unordered_set<std::string>> SPECIAL_EXPORTED_FUNCS{
        {CORE_PACKAGE_NAME,
            {"arrayInitByCollection", "arrayInitByFunction", "composition", "handleException",
                "createOverflowExceptionMsg", "createArithmeticExceptionMsg", "getCommandLineArgs"}},
        {AST_PACKAGE_NAME,
            {MACRO_OBJECT_NAME, "refreshTokensPosition", "refreshPos", "unsafePointerCastFromUint8Array"}}};
    auto found = SPECIAL_EXPORTED_FUNCS.find(pkg.fullPackageName);
    if (found == SPECIAL_EXPORTED_FUNCS.end()) {
        return;
    }
    IterateToplevelDecls(pkg, [&found](auto& decl) {
        if (found->second.count(decl->identifier) != 0) {
            decl->EnableAttr(Attribute::IMPLICIT_USED);
        }
    });
}
} // namespace

Ptr<Decl> TypeChecker::TypeCheckerImpl::GetImplementedTargetIfExist(
    const ASTContext& ctx, const Ty& interfaceTy, Decl& target, const MultiTypeSubst& typeMapping)
{
    auto targetInstanceTy = typeManager.GetBestInstantiatedTy(target.ty, typeMapping);
    auto id = Ty::GetDeclPtrOfTy<InheritableDecl>(&interfaceTy);
    auto members = FieldLookup(ctx, id, target.identifier);
    for (auto& member : members) {
        bool isSameSignature = false;
        if (member->IsFunc() && target.IsFunc()) {
            if (!IsOverrideOrShadow(typeManager, *RawStaticCast<FuncDecl*>(member), static_cast<FuncDecl&>(target))) {
                continue;
            }
            // Use the typeMapping provided by the call chain to perform instantiation comparison and confirm that the
            // override version is correct.
            // eg: interface I<T> { f(T) }; interface I1 <: I<A> & I<B> { f(A) }
            // I1<B>.f(b) should diag error.
            // f(A) is the overwritten version, but not the correct version of the final call.
            auto memberFuncTy = DynamicCast<FuncTy*>(member->ty);
            auto targetFuncTy = DynamicCast<FuncTy*>(targetInstanceTy);
            if (!Ty::IsTyCorrect(memberFuncTy) || !Ty::IsTyCorrect(targetFuncTy)) {
                continue;
            }
            auto mts = typeMapping;
            auto memberFuncDecl = DynamicCast<FuncDecl*>(member);
            auto targetFuncDecl = DynamicCast<const FuncDecl*>(&target);
            CJC_NULLPTR_CHECK(memberFuncDecl);
            CJC_NULLPTR_CHECK(targetFuncDecl);

            auto ts = GenerateTypeMappingBetweenFuncs(typeManager, *memberFuncDecl, *targetFuncDecl);
            mts.merge(ts);
            // Cannot be a reference type. Otherwise, SemaTy will be modified.
            auto memberParamTys = memberFuncTy->paramTys;
            auto targetParamTys = targetFuncTy->paramTys;
            for (auto& it : memberParamTys) {
                it = typeManager.GetBestInstantiatedTy(it, mts);
            }
            for (auto& it : targetParamTys) {
                it = typeManager.GetBestInstantiatedTy(it, mts);
            }
            isSameSignature = typeManager.IsFuncParameterTypesIdentical(memberParamTys, targetParamTys);
        } else if (member->astKind == ASTKind::PROP_DECL && target.astKind == ASTKind::PROP_DECL) {
            if (!IsOverrideOrShadow(typeManager, *RawStaticCast<PropDecl*>(member), static_cast<PropDecl&>(target))) {
                continue;
            }
            isSameSignature = member->ty == targetInstanceTy;
        }
        if (isSameSignature && !member->TestAttr(Attribute::ABSTRACT)) {
            return member;
        }
    }
    return nullptr;
}

std::pair<bool, Ptr<RefExpr>> TypeChecker::TypeCheckerImpl::CheckInvokeTargetHasImpl(const ASTContext& ctx,
    Ty& interfaceTy, Decl& decl, MultiTypeSubst& typeMapping, std::unordered_set<Ptr<AST::Decl>>& traversedDecls)
{
    std::pair<bool, Ptr<RefExpr>> ret{false, nullptr};
    auto preVisit = [this, &ret, &ctx, &interfaceTy, &typeMapping, &traversedDecls](Ptr<Node> node) -> VisitAction {
        // Find interface members that are directly referenced by name in the interface member function/property
        // definition.
        if (node->astKind != ASTKind::REF_EXPR) {
            return AST::VisitAction::WALK_CHILDREN;
        }
        const auto re = RawStaticCast<RefExpr*>(node);
        auto target = re->GetTarget();
        // If it is a function defined in the current function, Use the ast check directly.
        // Otherwise, 'GetImplementedTargetIfExist' cannot find the member of the implementation version.
        if (target && target->TestAttr(Attribute::STATIC) && target->outerDecl && !target->outerDecl->IsFunc()) {
            // Check whether traversal has been performed.
            if (traversedDecls.find(target) != traversedDecls.end()) {
                return VisitAction::SKIP_CHILDREN;
            }
            traversedDecls.emplace(target);
            // Update all generic derivation results to typeMapping.
            if (re->matchedParentTy && target->outerDecl->ty) {
                typeMapping.merge(promotion.GetPromoteTypeMapping(*re->matchedParentTy, *target->outerDecl->ty));
            }
            // Check whether the current invoking is implemented in the 'interfaceTy'.
            auto newTarget = GetImplementedTargetIfExist(ctx, interfaceTy, *target, typeMapping);
            if (newTarget == nullptr) {
                ret.first = true;
                ret.second = re;
                return VisitAction::STOP_NOW;
            }
            ret = CheckInvokeTargetHasImpl(ctx, interfaceTy, *newTarget, typeMapping, traversedDecls);
            if (ret.first) {
                return VisitAction::STOP_NOW;
            }
        }
        return AST::VisitAction::WALK_CHILDREN;
    };
    Walker walker(&decl, preVisit);
    walker.Walk();
    return ret;
}

void TypeChecker::TypeCheckForPackages(const std::vector<Ptr<Package>>& pkgs) const
{
    impl->TypeCheckForPackages(pkgs);
}

std::vector<Ptr<ASTContext>> TypeChecker::TypeCheckerImpl::PreTypeCheck(const std::vector<Ptr<AST::Package>>& pkgs)
{
    Utils::ProfileRecorder recorder("Semantic", "Pre TypeCheck");
    std::vector<Ptr<ASTContext>> contexts;
    // Only check for packages that have corresponding ASTContext.
    std::for_each(pkgs.begin(), pkgs.end(), [this, &contexts](Ptr<Package> pkg) {
        CJC_NULLPTR_CHECK(pkg); // There should not be any nullptr Ptr<Package> in pkgs.
        if (auto ctx = ci->GetASTContextByPackage(pkg)) {
            contexts.emplace_back(ctx);
        }
    });
    for (auto& ctx : contexts) {
        PrepareTypeCheck(*ctx, *ctx->curPackage);
    }
    // Pre checking for resolving types and rules without semantic type.
    PreCheck(contexts);

    for (auto pkg : pkgs) {
        if (auto ctx = ci->GetASTContextByPackage(pkg)) {
            CollectDeclsWithMember(pkg, *ctx);
        }
    }
    return contexts;
}

void TypeChecker::TypeCheckerImpl::PostTypeCheck(std::vector<Ptr<ASTContext>>& contexts)
{
    Utils::ProfileRecorder recorder("Semantic", "Post TypeCheck");
    // Post checking for legality of semantic.
    for (auto& ctx : contexts) {
        CheckOverflow(*ctx->curPackage);
        CheckUnusedImportSpec(*ctx->curPackage);
        // Check duplicated super interfaces in class, interface when type arguments applied.
        CheckInstDupSuperInterfacesEntry(*ctx->curPackage);
        // Check legality of usage after sema type completed.
        CheckLegalityOfUsage(*ctx, *ctx->curPackage);
        // Check cjmp match rules.
        mpImpl->MatchPlatformWithCommon(*ctx->curPackage);
        AddAttrForDefaultFuncParam(*ctx->curPackage);
        // Because of the cjlint checking policy, desugar of propDecl should be done in sema stage for now.
        DesugarForPropDecl(*ctx->curPackage);
        CheckConstEvaluation(*ctx->curPackage);
        MarkImplicitUsedFunctions(*ctx->curPackage);
        // Collect program entry separately.
        IterateToplevelDecls(*ctx->curPackage, [this](auto& decl) {
            if (auto md = DynamicCast<MainDecl*>(decl.get()); md && md->desugarDecl) {
                (void)mainFunctionMap[md->curFile].emplace(md->desugarDecl.get());
            }
        });
        APILevelCheck::APILevelAnnoChecker(*ci, diag, importManager).Check(*ctx->curPackage);
    }
    CheckWhetherHasProgramEntry();
}

void TypeChecker::TypeCheckerImpl::PrepareTypeCheck(ASTContext& ctx, Package& pkg)
{
    // Reset search's cache.
    ctx.searcher->InvalidateCache();

    CheckPrimaryCtorBeforeMerge(pkg);
    // Merging common classes into platform if any
    mpImpl->PrepareTypeCheck4CJMP(pkg);
    
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    Interop::Java::PrepareTypeCheck(pkg);
    Interop::ObjC::PrepareTypeCheck(pkg);
#endif

    // Phase: add some default function.
    AddDefaultFunction(pkg);
    // Add built-in decls to core package.
    if (pkg.fullPackageName == CORE_PACKAGE_NAME && !pkg.files.empty() && !pkg.TestAttr(Attribute::IMPORTED)) {
        AddBuiltInArrayDecl(pkg);
        AddBuiltInVArrayDecl(pkg);
        AddBuiltInPointerDecl(pkg);
        AddBuiltinCFuncDecl(pkg);
        AddBuiltInCStringDecl(pkg);
    }

    // Phase: build symbol table.
    Collector collector(scopeManager, ci->invocation.globalOptions.enableMacroInLSP);
    collector.BuildSymbolTable(ctx, &pkg, ci->buildTrie);
    // Phase: mark outermost binary expressions.
    MarkOutermostBinaryExpressions(pkg);
    AddCurFile(pkg);
    MarkParamWithInitialValue(pkg);
    // Warmup cache to speed up search.
    WarmupCache(ctx);
}

void TypeChecker::TypeCheckerImpl::TypeCheckTopLevelDecl(ASTContext& ctx, Decl& decl)
{
    TyVarScope ts(typeManager); // to release local placeholder ty var
    Synthesize(ctx, &decl);
    MarkOverflow(decl);
}

// Check generic members declared inside non-generic inheritable decls.
void TypeChecker::TypeCheckerImpl::TypeCheckImportedGenericMember(ASTContext& ctx)
{
    std::vector<Symbol*> syms = GetAllStructDecls(ctx);
    for (auto sym : syms) {
        CJC_ASSERT(sym && sym->node);
        auto id = StaticCast<InheritableDecl*>(sym->node);
        // Generic toplevel decls will be checked before by 'TypeCheckTopLevelDecl'.
        if (id->TestAttr(Attribute::GENERIC)) {
            continue;
        }
        for (auto& member : id->GetMemberDecls()) {
            if (member->TestAttr(Attribute::GENERIC)) {
                Synthesize(ctx, member.get());
            }
        }
    }
}

void TypeChecker::TypeCheckerImpl::TypeCheck(ASTContext& ctx, Package& pkg)
{
    std::vector<Symbol*> syms = GetToplevelDecls(ctx);
    // 1. Check from toplevel decls.
    for (auto sym : syms) {
        CJC_ASSERT(sym && sym->node);
        if (!Is<Decl*>(sym->node)) {
            return;
        }
        auto decl = StaticAs<ASTKind::DECL>(sym->node);
        TypeCheckTopLevelDecl(ctx, *decl);
    }
    // 2. check source imported decls.
    for (auto& node : pkg.srcImportedNonGenericDecls) {
        Synthesize(ctx, node);
    }
    // 3. check imported generic member decls which is defined in non-generic decl.
    // NOTE: This kind of decls will not be checked in step 1.
    if (pkg.TestAttr(Attribute::IMPORTED)) {
        TypeCheckImportedGenericMember(ctx);
    }

    // For lsp, need to find the target of all macrocalls, include case: var a = call(@M(1)).
    std::function<VisitAction(Ptr<Node>)> visitMacrocall = [this, &visitMacrocall, &ctx](
                                                               Ptr<Node> curNode) -> VisitAction {
        if (curNode->astKind == ASTKind::FILE) {
            auto file = StaticAs<ASTKind::FILE>(curNode);
            for (auto& it : file->originalMacroCallNodes) {
                Walker(it.get(), visitMacrocall).Walk();
            }
        }
        if (curNode->IsMacroCallNode()) {
            CheckMacroCall(ctx, *curNode);
        }
        return VisitAction::WALK_CHILDREN;
    };
    if (ci->invocation.globalOptions.enableMacroInLSP) {
        for (auto& file : pkg.files) {
            Walker(file.get(), visitMacrocall).Walk();
        }
    }
}

void TypeChecker::TypeCheckerImpl::CheckWhetherHasProgramEntry()
{
    CJC_ASSERT(ci);
    if (!ci->invocation.globalOptions.CompileExecutable() || ci->invocation.globalOptions.enableCompileTest ||
        ci->invocation.frontendOptions.dumpAction == FrontendOptions::DumpAction::TYPE_CHECK ||
        !mainFunctionMap.empty() || ci->invocation.globalOptions.compileCjd) {
        return;
    }
    if (ci->srcPkgs.empty() || ci->srcPkgs[0] == nullptr || ci->srcPkgs[0]->files.empty()) {
        return;
    }
    auto& file = ci->srcPkgs[0]->files[0];
    if (file != nullptr) {
        diag.Diagnose(*file, DiagKind::sema_missing_entry);
    }
}

namespace {
/** Check if a type decl (class/strcut) has any constructor. */
bool HasCtorForTypeDecl(const InheritableDecl& id)
{
    for (auto& decl : id.GetMemberDeclPtrs()) {
        CJC_NULLPTR_CHECK(decl);
        if (decl->TestAttr(Attribute::CONSTRUCTOR) && !decl->TestAttr(Attribute::STATIC)) {
            return true;
        }
    }
    return false;
}
} // namespace

VisitAction TypeChecker::TypeCheckerImpl::CheckDefaultParamFunc(StructDecl& sd) const
{
    CJC_ASSERT(sd.body);
    // Do not desugar for broken body.
    if (sd.body->TestAttr(Attribute::IS_BROKEN)) {
        return VisitAction::SKIP_CHILDREN;
    }
    if (!HasCtorForTypeDecl(sd)) {
        AddDefaultCtor(sd);
    }
    AddSetterGetterInProp(sd);
    return VisitAction::WALK_CHILDREN;
}

VisitAction TypeChecker::TypeCheckerImpl::CheckDefaultParamFunc(ClassDecl& cd, const File& file) const
{
    CJC_ASSERT(cd.body);
    // Do not desugar for broken body.
    if (cd.body->TestAttr(Attribute::IS_BROKEN)) {
        return VisitAction::SKIP_CHILDREN;
    }
    if (!HasCtorForTypeDecl(cd)) {
        AddDefaultCtor(cd);
    }
    AddSetterGetterInProp(cd);
    // Add 'super()' to the constructor which doesn't call either init(..) or super(..) inside.
    for (auto& decl : cd.body->decls) {
        CJC_ASSERT(decl);
        if (decl->astKind != ASTKind::FUNC_DECL) {
            continue;
        }
        auto& fd = *RawStaticCast<FuncDecl*>(decl.get());
        if (fd.funcBody == nullptr || fd.funcBody->body == nullptr || !IsInstanceConstructor(fd)) {
            continue;
        }
        // The constructorCall only need to do once when there is a `init` function.
        SetFuncDeclConstructorCall(fd);
        // For now, the classes in `HLIR` do not automatically inherit from core.Object,
        // so in these two backends, it is not necessary to add super call to the constructors
        // of the classes which have no super class.
        if (fd.constructorCall == ConstructorCall::NONE) {
            std::string_view fullPackageName;
            if (file.curPackage) {
                fullPackageName = file.curPackage->fullPackageName;
            }
            auto objPkgName = CORE_PACKAGE_NAME;
            bool nonObjectClass = !(cd.identifier == OBJECT_NAME && fullPackageName == objPkgName);
            if (nonObjectClass) {
                AddDefaultSuperCall(*fd.funcBody);
                fd.constructorCall = ConstructorCall::SUPER;
            }
        }
    }
    return VisitAction::WALK_CHILDREN;
}

void TypeChecker::TypeCheckerImpl::SetFuncDeclConstructorCall(FuncDecl& fd) const
{
    CJC_ASSERT(fd.funcBody && fd.funcBody->body);
    if (fd.funcBody->body->body.empty()) {
        return;
    }
    Ptr<RefExpr> refExpr = nullptr;
    if (auto ce = DynamicCast<CallExpr*>(fd.funcBody->body->body.begin()->get()); ce) {
        if (auto re = DynamicCast<RefExpr*>(ce->baseFunc.get()); re) {
            refExpr = re;
        }
    } else if (auto tce = DynamicCast<TrailingClosureExpr*>(fd.funcBody->body->body.begin()->get()); tce) {
        if (auto callExpr = DynamicCast<CallExpr*>(tce->desugarExpr.get()); callExpr) {
            if (auto re = DynamicCast<RefExpr*>(callExpr->baseFunc.get()); re) {
                refExpr = re;
            }
        } else if (auto re = DynamicCast<RefExpr*>(tce->desugarExpr.get()); re) {
            refExpr = re;
        }
    }
    if (refExpr != nullptr) {
        if (refExpr->isThis && !fd.TestAttr(Attribute::PRIMARY_CONSTRUCTOR)) {
            fd.constructorCall = ConstructorCall::OTHER_INIT;
        }
        if (refExpr->isSuper) {
            fd.constructorCall = ConstructorCall::SUPER;
        }
    }
}

VisitAction TypeChecker::TypeCheckerImpl::CheckDefaultParamFunc(const InterfaceDecl& ifd) const
{
    CJC_ASSERT(ifd.body);
    // Do not desugar for broken body.
    if (ifd.body->TestAttr(Attribute::IS_BROKEN)) {
        return VisitAction::SKIP_CHILDREN;
    }
    AddSetterGetterInProp(ifd);
    return VisitAction::WALK_CHILDREN;
}

VisitAction TypeChecker::TypeCheckerImpl::CheckDefaultParamFunc(const EnumDecl& ed) const
{
    AddSetterGetterInProp(ed);
    return VisitAction::WALK_CHILDREN;
}

void TypeChecker::TypeCheckerImpl::CheckDefaultParamFuncsEntry(File& file)
{
    auto visitFunc = [&file, this](Ptr<Node> node) -> VisitAction {
        switch (node->astKind) {
            case ASTKind::FUNC_DECL: {
                auto fd = StaticAs<ASTKind::FUNC_DECL>(node);
                GetSingleParamFunc(*fd);
                if (fd->IsFinalizer()) {
                    AddUnitType(*fd);
                }
                return VisitAction::WALK_CHILDREN;
            }
            case ASTKind::PROP_DECL: {
                auto pd = StaticAs<ASTKind::PROP_DECL>(node);
                AddReturnTypeForPropMemDecl(*pd);
                return VisitAction::WALK_CHILDREN;
            }
            case ASTKind::STRUCT_DECL: {
                auto sd = StaticAs<ASTKind::STRUCT_DECL>(node);
                return CheckDefaultParamFunc(*sd);
            }
            case ASTKind::CLASS_DECL: {
                auto cd = StaticAs<ASTKind::CLASS_DECL>(node);
                return CheckDefaultParamFunc(*cd, file);
            }
            case ASTKind::INTERFACE_DECL: {
                auto ifd = StaticAs<ASTKind::INTERFACE_DECL>(node);
                return CheckDefaultParamFunc(*ifd);
            }
            case ASTKind::ENUM_DECL: {
                auto ed = StaticAs<ASTKind::ENUM_DECL>(node);
                return CheckDefaultParamFunc(*ed);
            }
            case ASTKind::EXTEND_DECL: {
                auto ed = StaticAs<ASTKind::EXTEND_DECL>(node);
                AddSetterGetterInProp(*ed);
                return VisitAction::WALK_CHILDREN;
            }
            case ASTKind::FUNC_PARAM:
                // Nested function inside default param value is dealt inside GetSingleParamFunc, should skip here.
                return VisitAction::SKIP_CHILDREN;
            default:
                return VisitAction::WALK_CHILDREN;
        }
    };
    for (auto& decl : file.decls) {
        Walker walker(decl.get(), visitFunc);
        walker.Walk();
    }
    // Walk exported internal decls for any generic decl defined in toplevel or defined in nominal decls.
    for (auto& decl : file.exportedInternalDecls) {
        Walker(decl.get(), visitFunc).Walk();
    }
    // Walk macroCall nodes for lsp.
    if (ci->invocation.globalOptions.enableMacroInLSP) {
        for (auto& node : file.originalMacroCallNodes) {
            Walker(node.get(), visitFunc).Walk();
        }
    }
}

namespace {
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
void CollectGenericParam(const FuncDecl& funcDecl, Ptr<FuncDecl> desugared)
{
    if (!funcDecl.funcBody || !funcDecl.funcBody->generic || funcDecl.funcBody->generic->typeParameters.empty()) {
        return;
    }
    OwnedPtr<Generic> generic = MakeOwnedNode<Generic>();
    int typeParamIdx = 0;
    // Rename the type param decl: <T, P> ===> <T$0, P$1>.
    std::map<std::string, std::string> orig2New;
    for (auto& param : std::as_const(funcDecl.funcBody->generic->typeParameters)) {
        OwnedPtr<GenericParamDecl> tp = MakeOwnedNode<GenericParamDecl>();
        tp->identifier = param->identifier + "$" + std::to_string(typeParamIdx++);
        tp->outerDecl = desugared;
        // If the generic parameter name same as outerDecl's, the outerDecl's will be shadowed.
        orig2New.emplace(param->identifier, tp->identifier);
        generic->typeParameters.emplace_back(std::move(tp));
    }
    for (auto& constraint : std::as_const(funcDecl.funcBody->generic->genericConstraints)) {
        auto gc = MakeOwnedNode<GenericConstraint>();
        auto& constraintType = constraint->type;
        auto rt = MakeOwnedNode<RefType>();
        rt->ref.identifier = orig2New[constraintType->ref.identifier];
        gc->type = std::move(rt);
        for (auto& upperBound : std::as_const(constraint->upperBounds)) {
            auto ub = ASTCloner::Clone(upperBound.get());
            gc->upperBounds.emplace_back(std::move(ub));
        }
        generic->genericConstraints.emplace_back(std::move(gc));
    }
    desugared->funcBody->generic = std::move(generic);
    auto replacesTypeParams = [&orig2New](Ptr<Node> node) {
        // Replaces the name of all generic declaration references, which can only be refer node.
        if (auto rt = DynamicCast<RefType>(node)) {
            auto found = orig2New.find(rt->ref.identifier);
            if (found != orig2New.end()) {
                rt->ref.identifier = found->second;
            }
        } else if (auto re = DynamicCast<RefExpr>(node)) {
            auto found = orig2New.find(re->ref.identifier);
            if (found != orig2New.end()) {
                re->ref.identifier = found->second;
            }
        }
        return VisitAction::WALK_CHILDREN;
    };
    Walker(desugared, replacesTypeParams).Walk();
    desugared->EnableAttr(Attribute::GENERIC);
}
#endif

/**  Each optional parameter generates a function. NOTICE: it will change AST Node!
 * *************** before desugar ****************
 * class A<T> {
 *     func foo<R>(a: T, b!: T = a) {}
 * }
 * *************** after desugar ****************
 * class A<T> {
 *     // chir will generic apply for 'b' when call 'foo', like 'foo<Int64>(a, b.0<Int64>(a))'
 *     func foo<R>(a: T, b: T) {}
 *     func b.1<R$0>(a: T) {
 *         return a
 *     }
 * }
 */
OwnedPtr<FuncDecl> MakeDefaultParamFunction(
    FuncParam& fp, FuncDecl& funcDecl, const std::vector<Ptr<FuncParam>>& funcParams)
{
    fp.EnableAttr(Attribute::HAS_INITIAL);
    OwnedPtr<FuncDecl> ret = MakeOwnedNode<FuncDecl>();
    CopyBasicInfo(fp.assignment.get(), ret.get());
    ret->isFrozen = funcDecl.isFrozen || funcDecl.HasAnno(AnnotationKind::FROZEN);
    // The desugar function declaration is in the same scope as the current function to ensure that external generic
    // parameters can be accessed.
    CopyNodeScopeInfo(&funcDecl, ret.get());
    // Default param's function's name is 'paramName.paramIndex'
    // 'funcParams' is vector of previous params, so the size of 'funcParams' is the index of curren param.
    ret->identifier = fp.identifier + "." + std::to_string(funcParams.size());
    // Make FuncBody.
    OwnedPtr<FuncBody> funcBody = MakeOwnedNode<FuncBody>();
    funcBody->body = MakeOwnedNode<Block>();
    funcBody->paramLists.push_back(MakeOwnedNode<FuncParamList>());
    std::vector<OwnedPtr<FuncParam>> params;
    for (auto& param : funcParams) {
        if (param == nullptr) {
            continue; // Double Check.
        }
        params.emplace_back(CreateFuncParamForOptional(*param));
    }
    funcBody->paramLists[0]->params = std::move(params);
    ret->funcBody = std::move(funcBody);
    // Set return expr.
    auto returnExpr = MakeOwnedNode<ReturnExpr>();
    returnExpr->begin = fp.assignment->begin;
    returnExpr->end = fp.assignment->end;
    returnExpr->expr = ASTCloner::Clone(fp.assignment.get());
    returnExpr->refFuncBody = ret->funcBody.get();
    ret->funcBody->body->body.emplace_back(std::move(returnExpr));
    ret->funcBody->retType = ASTCloner::Clone(fp.type.get());
    // The resolvedFunction contains the assignment, so mark it as UNREACHABLE.
    fp.assignment->EnableAttr(Attribute::UNREACHABLE);
    ret->funcBody->funcDecl = ret.get();
    ret->EnableAttr(Attribute::HAS_INITIAL, Attribute::IMPLICIT_ADD, Attribute::NO_REFLECT_INFO);
    ret->toBeCompiled = funcDecl.toBeCompiled; // For incremental compilation.
    ret->ownerFunc = &funcDecl;
    ret->fullPackageName = funcDecl.fullPackageName;
    ret->outerDecl = funcDecl.outerDecl;
    if (funcDecl.TestAttr(Attribute::CONSTRUCTOR)) {
        // Desugared default param function should be considered as a static function.
        // NOTE: used to avoid unexpected 'this' insertion during AST2CHIR.
        ret->EnableAttr(Attribute::STATIC);
    }
    // Default value should inherit 'mut' attribute.
    if (funcDecl.TestAttr(Attribute::MUT)) {
        ret->EnableAttr(Attribute::MUT);
    }
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    // It may be a static constructor, but export is not allowed.
    // This prevents link problems when chir creates virtual tables of upstream packets in downstream packets.
    ret->EnableAttr(Attribute::PRIVATE);
    CollectGenericParam(funcDecl, ret.get());
#endif
    return ret;
}
} // namespace

void TypeChecker::TypeCheckerImpl::GetSingleParamFunc(Decl& decl)
{
    auto fd = As<ASTKind::FUNC_DECL>(&decl);
    // Source imported decl does not inherit initial state and desugar decl, need to generate again.
    bool notInherit = !fd || !fd->funcBody || fd->funcBody->paramLists.empty() || fd->TestAttr(Attribute::IMPORTED);
    if (notInherit) {
        return;
    }
    std::vector<Ptr<FuncParam>> funcParams;
    bool isStatic = fd->TestAttr(Attribute::STATIC);
    auto walkFunc = [isStatic, this](Ptr<Node> node) -> VisitAction {
        CJC_ASSERT(node);
        if (node->astKind == ASTKind::FUNC_DECL) {
            if (isStatic) {
                node->EnableAttr(Attribute::STATIC); // Functions inside static function also has static attribute.
            }
            GetSingleParamFunc(*StaticAs<ASTKind::FUNC_DECL>(node));
        }
        return VisitAction::WALK_CHILDREN;
    };
    // Decls inside origin default assignment will report error if exists.
    // Compiler added function only used for code generation.
    auto walkOrigin = [isStatic](Ptr<Node> node) -> VisitAction {
        if (node->astKind == ASTKind::FUNC_DECL && isStatic) {
            node->EnableAttr(Attribute::STATIC); // Functions inside static function also has static attribute.
        }
        return VisitAction::WALK_CHILDREN;
    };
    for (auto& fp : fd->funcBody->paramLists[0]->params) {
        if (fp && fp->assignment && !fp->TestAttr(Attribute::HAS_INITIAL)) {
            if (fd->op != TokenKind::ILLEGAL || fd->TestAttr(Attribute::OPEN) || fd->TestAttr(Attribute::ABSTRACT) ||
                fd->TestAttr(Attribute::DEFAULT)) {
                DiagCannotHaveDefaultParam(diag, *fd, *fp);
                return;
            }
            fp->desugarDecl = MakeDefaultParamFunction(*fp, *fd, funcParams);
            // There may be nested functions inside default value function.
            Walker walker(fp->desugarDecl.get(), walkFunc);
            walker.Walk();
            Walker originWalker(fp->assignment.get(), walkOrigin);
            originWalker.Walk();
            MarkParamWithInitialValue(*fp->assignment);
        }
        funcParams.push_back(fp.get());
    }
}

Ptr<Decl> TypeChecker::TypeCheckerImpl::GetDupInterfaceRecursively(const Node& triggerNode, Ty& interfaceTy,
    const TypeSubst& instantiateMap, std::unordered_set<Ptr<InterfaceTy>>& res,
    std::unordered_set<Ptr<ClassLikeDecl>>& passedClassLikeDecls)
{
    auto insTy = typeManager.GetInstantiatedTy(&interfaceTy, instantiateMap);
    // When `ity` is invalid, it indicates that an inheritance cycle appeared.
    // In this case, we do not need to continue with the following steps.
    if (!Ty::IsTyCorrect(insTy) || !insTy->IsInterface()) {
        return nullptr;
    }
    auto ity = RawStaticCast<InterfaceTy*>(insTy);
    auto insertRes = res.insert(ity);
    if (!insertRes.second) {
        return ity->declPtr;
    }
    CJC_NULLPTR_CHECK(ity->declPtr);
    TypeSubst superInstantiateMap = GenerateTypeMapping(*ity->declPtr, ity->typeArgs);
    if (superInstantiateMap.empty()) {
        return nullptr;
    }
    return GetDupSuperInterface(triggerNode, *ity->declPtr, superInstantiateMap, passedClassLikeDecls);
}

Ptr<Decl> TypeChecker::TypeCheckerImpl::GetExtendDupSuperInterface(const Node& triggerNode, const InheritableDecl& decl,
    const TypeSubst& instantiateMap, std::unordered_set<Ptr<InterfaceTy>>& res,
    std::unordered_set<Ptr<ClassLikeDecl>>& passedClassLikeDecls)
{
    if (!decl.TestAttr(Attribute::GENERIC) || !Ty::IsTyCorrect(decl.ty)) {
        return nullptr;
    }
    auto extends = typeManager.GetDeclExtends(decl);
    for (auto& extend : extends) {
        // Generate typeMapping from extend decl's generic type to original decl type.
        TypeSubst extendInstMap = GenerateTypeMapping(*extend, decl.ty->typeArgs);
        if (extendInstMap.size() != instantiateMap.size()) {
            continue;
        }
        extendInstMap.insert(instantiateMap.begin(), instantiateMap.end());
        for (auto& interfaceType : extend->inheritedTypes) {
            if (!Ty::IsTyCorrect(interfaceType->ty)) {
                continue;
            }
            auto ret =
                GetDupInterfaceRecursively(triggerNode, *interfaceType->ty, extendInstMap, res, passedClassLikeDecls);
            if (ret) {
                return ret;
            }
        }
    }
    return nullptr;
}

Ptr<Decl> TypeChecker::TypeCheckerImpl::GetDupSuperInterface(const Node& triggerNode, InheritableDecl& decl,
    const TypeSubst& instantiateMap, std::unordered_set<Ptr<ClassLikeDecl>>& passedClassLikeDecls, bool checkExtend)
{
    if (decl.IsClassLikeDecl()) {
        auto cld = RawStaticCast<ClassLikeDecl*>(&decl);
        if (passedClassLikeDecls.find(cld) != passedClassLikeDecls.end()) {
            return nullptr;
        }
        passedClassLikeDecls.insert(cld);
    }
    std::unordered_set<Ptr<InterfaceTy>> instInterfaceTys;
    for (auto& interfaceType : decl.inheritedTypes) {
        if (!Ty::IsTyCorrect(interfaceType->ty)) {
            continue;
        }
        auto ret = GetDupInterfaceRecursively(
            triggerNode, *interfaceType->ty, instantiateMap, instInterfaceTys, passedClassLikeDecls);
        if (ret) {
            return ret;
        }
    }
    if (checkExtend) {
        return GetExtendDupSuperInterface(triggerNode, decl, instantiateMap, instInterfaceTys, passedClassLikeDecls);
    } else {
        return nullptr;
    }
}

void TypeChecker::TypeCheckerImpl::CheckInstDupSuperInterfaces(
    const Node& triggerNode, InheritableDecl& decl, const TypeSubst& instantiateMap, bool checkExtend)
{
    std::unordered_set<Ptr<ClassLikeDecl>> passedClassLikeDecls;
    auto interfaceDecl = GetDupSuperInterface(triggerNode, decl, instantiateMap, passedClassLikeDecls, checkExtend);
    auto baseDecl = Ty::GetDeclPtrOfTy(decl.ty);
    std::string name = baseDecl ? baseDecl->identifier.Val() : Ty::ToString(decl.ty);
    if (interfaceDecl) {
        diag.Diagnose(triggerNode, DiagKind::sema_inherit_duplicate_interface, DeclKindToString(decl), name,
            interfaceDecl->identifier.Val());
    }
}

VisitAction TypeChecker::TypeCheckerImpl::CheckInstDupSuperInterfaces(const Type& type)
{
    if (!Ty::IsTyCorrect(type.ty)) {
        return VisitAction::SKIP_CHILDREN;
    }
    auto typeTarget = TypeCheckUtil::GetRealTarget(type.GetTarget());
    if (!typeTarget || !typeTarget->IsNominalDecl()) {
        return VisitAction::WALK_CHILDREN;
    }
    TypeSubst typeMapping = GenerateTypeMapping(*typeTarget, type.ty->typeArgs);
    if (typeMapping.empty()) {
        return VisitAction::SKIP_CHILDREN;
    }
    // Duplicate check of extend's parent class has been completed in Extend check.
    CheckInstDupSuperInterfaces(type, *StaticCast<InheritableDecl*>(typeTarget), typeMapping, false);
    return VisitAction::WALK_CHILDREN;
}

VisitAction TypeChecker::TypeCheckerImpl::CheckInstDupSuperInterfaces(const Expr& expr)
{
    if (!Ty::IsTyCorrect(expr.ty)) {
        return VisitAction::WALK_CHILDREN;
    }
    auto target = TypeCheckUtil::GetRealTarget(expr.GetTarget());
    auto instTys = TypeCheckUtil::GetInstanationTys(expr);
    if (!target || instTys.empty()) {
        return VisitAction::WALK_CHILDREN;
    }
    if (IsClassOrEnumConstructor(*target) && target->outerDecl) {
        target = target->outerDecl;
    }
    if (!target->IsNominalDecl()) {
        return VisitAction::WALK_CHILDREN;
    }
    TypeSubst instantiateMap = GenerateTypeMapping(*target, instTys);
    if (instantiateMap.empty()) {
        return VisitAction::SKIP_CHILDREN;
    }
    CheckInstDupSuperInterfaces(expr, *StaticCast<InheritableDecl*>(target), instantiateMap);
    return VisitAction::WALK_CHILDREN;
}

void TypeChecker::TypeCheckerImpl::CheckInstDupSuperInterfacesEntry(Node& n)
{
    std::function<VisitAction(Ptr<Node>)> visitor = [this, &visitor](Ptr<Node> n) {
        switch (n->astKind) {
            case ASTKind::PACKAGE: {
                auto& pkg = *RawStaticCast<Package*>(n);
                for (auto& it : pkg.files) {
                    Walker(it.get(), visitor).Walk();
                }
                return VisitAction::STOP_NOW;
            }
            case ASTKind::REF_TYPE:
            case ASTKind::QUALIFIED_TYPE: {
                return CheckInstDupSuperInterfaces(*RawStaticCast<Type*>(n));
            }
            case ASTKind::REF_EXPR:
            case ASTKind::MEMBER_ACCESS: {
                return CheckInstDupSuperInterfaces(*RawStaticCast<Expr*>(n));
            }
            default: {
                return VisitAction::WALK_CHILDREN;
            }
        }
    };
    Walker walker(&n, visitor);
    walker.Walk();
}

// Remove after Chir's constant folding really works.
void TypeChecker::TypeCheckerImpl::CheckOverflow(Node& node)
{
    Walker walker(&node, nullptr, [this](Ptr<Node> node) -> VisitAction {
        switch (node->astKind) {
            case ASTKind::LIT_CONST_EXPR: {
                auto& lce = *StaticAs<ASTKind::LIT_CONST_EXPR>(node);
                if (!lce.desugarExpr && Ty::IsTyCorrect(lce.ty)) {
                    ChkLitConstExprRange(lce);
                }
                return VisitAction::WALK_CHILDREN;
            }
            default: {
                return VisitAction::WALK_CHILDREN;
            }
        }
    });
    walker.Walk();
}

Ptr<Ty> TypeChecker::TypeCheckerImpl::CalcFuncRetTyFromBody(const FuncBody& fb)
{
    if (!fb.body) {
        return TypeManager::GetInvalidTy();
    }

    if (fb.body->body.empty()) {
        return TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
    }

    auto& lastNode = fb.body->body.back();
    Ptr<Ty> bodyTy = lastNode->IsDecl() ? TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT) : lastNode->ty;
    Ptr<Ty> retTy = bodyTy;
    std::set<Ptr<Ty>> retTys;
    Walker(fb.body.get(), [&retTys](auto node) {
        CJC_ASSERT(node);
        if (node->astKind == ASTKind::FUNC_DECL || node->astKind == ASTKind::LAMBDA_EXPR) {
            return VisitAction::SKIP_CHILDREN;
        } else if (auto re = DynamicCast<ReturnExpr*>(node); re && re->expr) {
            if (Ty::IsTyCorrect(re->expr->ty)) {
                retTys.emplace(re->expr->ty);
            }
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    CJC_NULLPTR_CHECK(fb.retType);
    // Only calculate return type with the function body ty when the given return type is not exist or is not unit.
    bool returnUnit = Ty::IsTyCorrect(fb.retType->ty) && fb.retType->ty->IsUnit();
    if (returnUnit) {
        // It no return expression existed, return the user given unit type.
        if (retTys.empty()) {
            return fb.retType->ty;
        }
    } else {
        retTys.emplace(bodyTy);
    }
    if (Ty::IsTyCorrect(bodyTy)) {
        auto joinAndMeet = JoinAndMeet(typeManager, retTys, {}, &importManager, fb.curFile);
        auto joinRes = joinAndMeet.JoinAsVisibleTy();
        if (auto optErrs = JoinAndMeet::SetJoinedType(retTy, joinRes)) {
            auto builder = diag.Diagnose(*lastNode, DiagKind::sema_incompatible_func_body_and_return_type);
            builder.AddNote(*optErrs);
        }
        return retTy;
    } else {
        return TypeManager::GetInvalidTy();
    }
}
} // namespace Cangjie
