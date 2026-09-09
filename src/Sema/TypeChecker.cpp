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

#include "CJMP/MPTypeCheckerImpl.h"
#include "Collector.h"
#include "Desugar/DesugarInTypeCheck.h"
#include "DiagSuppressor.h"
#include "Diags.h"
#include "ExtraScopes.h"
#include "JoinAndMeet.h"
#include "NativeFFI/Java/AfterTypeCheck/InteropLibBridge.h"
#include "NativeFFI/Java/BeforeTypeCheck/PreTypeCheck.h"
#include "NativeFFI/ObjC/BeforeTypeCheck/Desugar.h"
#include "NativeFFI/ObjC/Utils/InteropLibBridge.h"
#include "Plugin/PluginCustomAnnoChecker.h"
#include "SearchSymbol.h"
#include "TypeCheckUtil.h"

#include "cangjie/AST/Clone.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Frontend/CompilerInstance.h"
#include "cangjie/Utils/CheckUtils.h"
#include "cangjie/Utils/Utils.h"
#include "cangjie/Utils/ProfileRecorder.h"

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
      importManager(*ci->importManager),
      backendType(ci->invocation.globalOptions.backend),
      mpImpl(new MPTypeCheckerImpl(*ci)),
      modalTypeChecker(NewModalTypeChecker())
{
}

TypeChecker::TypeCheckerImpl::~TypeCheckerImpl()
{
    if (mpImpl) {
        delete mpImpl;
        mpImpl = nullptr;
    }
    DeleteModalTypeChecker();
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
    CJC_ASSERT(fb.retType->GetTy());
    // Only set 'fb.retType->GetTy()' to invalid when it's type was questTy.
    // Otherwise return without modifying the type of any node.
    if (fb.body == nullptr) {
        // Quick fix for foreign functions. I did not add error messages currently since it is a quick fix.
        if (fb.retType->GetTy()->HasQuestTy()) {
            fb.retType->SetTy({TypeManager::GetInvalidTy()});
            return false;
        }
        return true;
    }
    // If the return type is not of QuestTy, then we use it to check the function's body.
    if (fb.retType->GetTy()->IsQuest()) {
        // Semantic analysis for function body without given return type.
        auto ret = Synthesize({ctx, SynPos::NONE}, fb.body.get());
        // In lambda, the return value type should be consistent with the body.
        // Otherwise, errors in the body that do not affect the return value type may fail to be reported.
        if (fb.funcDecl == nullptr && !ret.IsCorrect()) {
            fb.retType->SetTy({TypeManager::GetInvalidTy()});
            return false;
        }
        fb.retType->SetTy(CalcFuncRetTyFromBody(fb));
        bool isWellTyped = fb.retType->GetTy().IsCorrect();
        if (fb.retType->GetTy()->HasQuestTy()) {
            isWellTyped = false;
            fb.retType->SetTy({TypeManager::GetInvalidTy()});
            if (!CanSkipDiag(fb)) {
                DiagUnableToInferReturnType(diag, fb);
            }
        }
        if (isWellTyped) {
            if (CheckReturnThisInFuncBody(fb)) {
                ReplaceFuncRetTyWithThis(fb, fb.retType->GetTy());
            } else if (auto ctt = DynamicCast<ClassThisTy>(fb.retType->DataTy())) {
                // Replace ClassThisTy to ClassTy when the function cannot return 'This' anymore.
                fb.retType->SetTy(ctt->declPtr->GetTy());
            }
        }
        return isWellTyped;
    } else {
        bool isWellTyped = true;
        if (fb.retType->GetTy()->IsUnit()) {
            // The body eventually will be appended a 'return ()' expression, so we switch to the Synthesize mode.
            // Errors should be already reported during the synthesis.
            isWellTyped = Synthesize({ctx, SynPos::UNUSED}, fb.body.get()).IsCorrect();
        } else if (NeedCheckBodyReturn(fb)) {
            isWellTyped = Check(ctx, fb.retType->GetTy(), fb.body.get());
            if (!isWellTyped && fb.body->body.empty()) {
                DiagMismatchedTypes(diag, *fb.body, *fb.retType, "return type");
            }
            if (isWellTyped && Is<ClassThisTy>(fb.retType->DataTy())) {
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
                    fb.body->SetTy(typeManager.GetThisRealTy(fb.body->GetTy()));
                }
                if (!CheckReturnThisInFuncBody(fb) && !fb.retType->GetTy()->IsNothing()) {
                    DiagMismatchedTypes(diag, *fb.body, *fb.retType, "return type");
                    isWellTyped = false;
                }
            }
        }
        return isWellTyped;
    }
}

void TypeChecker::TypeCheckerImpl::ReplaceFuncRetTyWithThis(FuncBody& fb, ModalTy ty)
{
    if (auto ct = DynamicCast<ClassTy*>(ty.Ty()); ct) {
        auto rt = MakeOwned<RefType>();
        rt->curFile = fb.curFile;
        rt->ref.target = ct->decl;
        rt->ref.identifier = "This";
        rt->SetTy({typeManager.GetClassThisTy(*ct->declPtr, ct->TyArgs()), ty.Mode()});
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
        Synthesize({ctx, SynPos::NONE}, fb.retType.get());
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
    std::vector<ModalTy> paramTys;
    if (!CheckNormalFuncBody(ctx, fb, paramTys)) {
        return false;
    }
    return true;
}

void TypeChecker::TypeCheckerImpl::AddRetTypeNode(FuncBody& fb) const
{
    // For incremental type check's compatibility. Incremental type check will erase retType->GetTy() but not retType.
    // Adding retType node more than once leads to memory leak.
    if (!fb.retType) {
        fb.retType = MakeOwned<RefType>();
    }
    fb.retType->EnableAttr(Attribute::COMPILER_ADD);
    fb.retType->SetTy({TypeManager::GetQuestTy()});
    // Set compiler added retType as visited to avoid being checked by `CheckReferenceTypeLegality`.
    fb.retType->EnableAttr(Attribute::IS_CHECK_VISITED);
}

bool TypeChecker::TypeCheckerImpl::CheckNormalFuncBody(ASTContext& ctx, FuncBody& fb, std::vector<ModalTy>& paramTys)
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
    if (!fb.retType->GetTy().IsCorrect()) {
        if (!fb.TestAttr(Attribute::IS_CHECK_VISITED)) {
            fb.EnableAttr(Attribute::IS_CHECK_VISITED); // Avoid re-enter funcDecl check, when function is invalid.
            Synthesize({ctx, SynPos::NONE}, fb.body.get()); // Synthesize for other decl/expr in function body.
        }
        fb.SetTy({typeManager.GetFunctionTy(paramTys, fb.retType->GetTy(), {isCFunc, false, hasVariableLenArg})});
        return false;
    }

    // Set funcTy before Synthesize body, avoid recursively call typecheck loop.
    auto funcTy = typeManager.GetFunctionTy(paramTys, fb.retType->GetTy(), {isCFunc, false, hasVariableLenArg});
    if (fb.funcDecl) {
        fb.funcDecl->SetTy({funcTy});
    }
    fb.SetTy({funcTy});

    if (!CheckBodyRetType(ctx, fb)) {
        // Update 'fb.GetTy()' witch updated 'fb.retType->GetTy()'.
        fb.SetTy({typeManager.GetFunctionTy(paramTys, fb.retType->GetTy(), {isCFunc, false, hasVariableLenArg})});
        return false;
    }

    if (isCFFIBackend) {
        UnsafeCheck(fb);
    }

    funcTy = typeManager.GetFunctionTy(paramTys, fb.retType->GetTy(), {isCFunc, false, hasVariableLenArg});
    // Update funcDecl's type after body is checked.
    if (fb.funcDecl) {
        fb.funcDecl->SetTy({funcTy});
    }
    fb.SetTy({funcTy});
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
    type->SetTy({TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT)});
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
    ModalTy ctorTy = {TypeManager::GetInvalidTy()};
    if (fb.funcDecl->TestAttr(Attribute::STATIC)) {
        // Static init always has type of 'Unit' as return type.
        ctorTy = {TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT)};
    } else {
        if (fb.parentStruct) {
            ctorTy = fb.parentStruct->GetTy();
        }
        if (fb.parentClassLike) {
            ctorTy = fb.parentClassLike->GetTy();
        }
        if (fb.paramLists[0]->thisParam) {
            ctorTy = ctorTy.With(fb.paramLists[0]->thisParam->modal.ToModalInfo());
        }
    }
    if (!ctorTy.IsCorrect() || fb.paramLists.empty()) {
        return;
    }
    CheckFuncParamList(ctx, *fb.paramLists[0].get());
    auto paramTys = GetFuncBodyParamTys(fb);
    fb.SetTy({typeManager.GetFunctionTy(paramTys, ctorTy)});
    fb.funcDecl->SetTy(fb.GetTy());
    fb.retType->SetTy(ctorTy);
    Synthesize({ctx, SynPos::UNUSED}, fb.body.get());
}

void TypeChecker::TypeCheckerImpl::CheckFuncParamList(ASTContext& ctx, FuncParamList& fpl)
{
    // We use the tupleType to handle the type of FuncParamList.
    std::vector<DataTy> paramTys;
    // Synthesize thisParam first (before early return for empty params)
    if (fpl.thisParam) {
        Synthesize({ctx, SynPos::NONE}, fpl.thisParam.get());
    }
    if (fpl.params.empty()) {
        fpl.SetTy({typeManager.GetTupleTy(paramTys)});
        return;
    }
    for (auto& param : fpl.params) {
        CJC_NULLPTR_CHECK(param);
        if (!Synthesize({ctx, SynPos::NONE}, param.get()).IsCorrect()) {
            paramTys.push_back(TypeManager::GetInvalidTy()); // Set type to get correct size of function type.
            continue;
        }
        ModalTy paramTy = param->GetTy();
        paramTys.push_back(paramTy.Ty());
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
            if (curNode->GetTy().IsCorrect()) {
                param->assignment->SetTy(curNode->GetTy());
            }
        }
    }
    fpl.SetTy({typeManager.GetTupleTy(paramTys)});
}

bool TypeChecker::TypeCheckerImpl::ChkFuncArg(ASTContext& ctx, ModalTy target, FuncArg& fa)
{
    if (!fa.expr) {
        fa.SetTy({TypeManager::GetInvalidTy()});
        return false;
    }
    if (fa.withInout) {
        return ChkFuncArgWithInout(ctx, target, fa);
    }
    if (!Check(ctx, target, fa.expr.get())) {
        fa.SetTy({TypeManager::GetInvalidTy()});
        return false;
    }
    fa.SetTy(fa.expr->GetTy());
    return true;
}

bool TypeChecker::TypeCheckerImpl::ChkFuncArgWithInout(ASTContext& ctx, ModalTy target, FuncArg& fa)
{
    auto realTarget = target;
    auto exprTy = fa.expr->GetTy();
    if (!fa.expr->GetTy().IsCorrect()) {
        // Trying to infer fa type without target.
        exprTy = Synthesize({ctx, SynPos::EXPR_ARG}, fa.expr.get());
    }
    auto argTy = DynamicCast<VArrayTy*>(exprTy.get());
    auto ptrParamTy = DynamicCast<PointerTy*>(target.get());
    auto varrParamTy = DynamicCast<VArrayTy*>(target.get());
    // Case 1: 'argTy' is VArray<T...>, target is CPointer<R>. 'realTarget' trans to
    // VArray<R...>.
    // Case 2: 'argTy' is VArray<T...>, target is VArray<R...>. 'realTarget' is VArray<R...>.
    // Other case: 'argTy' is T, target is CPointer<T>. 'realTarget' trans to T.
    if (argTy && ptrParamTy) {
        realTarget = {typeManager.GetVArrayTy(*ptrParamTy->typeArgs[0], argTy->size), target.Mode()};
    } else if (argTy && varrParamTy) {
        realTarget = target;
    } else if (ptrParamTy) {
        realTarget = ptrParamTy->typeArgs[0];
    }

    if (!exprTy.IsCorrect() || !typeManager.IsSubtype(exprTy, realTarget)) {
        if (exprTy.IsCorrect()) {
            DiagMismatchedTypes(diag, *fa.expr, realTarget);
        }
        fa.SetTy({TypeManager::GetInvalidTy()});
        return false;
    }
    if (!ChkInoutFuncArg(fa)) {
        fa.SetTy({TypeManager::GetInvalidTy()});
        return false;
    }
    fa.SetTy({typeManager.GetPointerTy(Is<VArrayTy>(realTarget.Ty()) ? realTarget->TyArg(0) : realTarget.Ty()),
        realTarget.Mode()});
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
    if (be->GetTy().IsCorrect() && be->GetTy()->IsClassLike()) {
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

ModalTy TypeChecker::TypeCheckerImpl::SynFuncArg(ASTContext& ctx, FuncArg& fa)
{
    if (fa.expr) {
        Synthesize({ctx, SynPos::EXPR_ARG}, fa.expr.get());
        if (fa.withInout) {
            if (!Ty::IsTyCorrect(fa.expr->GetTy()) || !ChkInoutFuncArg(fa)) {
                fa.SetTy({TypeManager::GetInvalidTy()});
            } else if (!fa.expr->GetTy()->IsMetCType()) {
                diag.DiagnoseRefactor(DiagKindRefactor::sema_inout_modify_non_ctype, fa);
                fa.SetTy({TypeManager::GetInvalidTy()});
            } else if (fa.expr->GetTy()->IsCString()) {
                diag.DiagnoseRefactor(DiagKindRefactor::sema_inout_modify_cstring_or_zerosized, fa, "type 'CString'");
                fa.SetTy({TypeManager::GetInvalidTy()});
            } else if (Is<VArrayTy>(fa.expr->DataTy())) {
                fa.SetTy({typeManager.GetPointerTy(fa.expr->GetTy()->typeArgs[0].Ty()), fa.expr->TyMode()});
            } else {
                fa.SetTy({typeManager.GetPointerTy(fa.expr->DataTy()), fa.expr->TyMode()});
            }
        } else {
            fa.SetTy(fa.expr->GetTy());
        }
    } else {
        fa.SetTy({TypeManager::GetInvalidTy()});
    }
    return fa.GetTy();
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
    auto target = it != ctx.targetTypeMap.cend() ? it->second : ModalTy{};
    return CacheKey{
        .target = target, .isDesugared = IsNodeDesugared(node), .diagKey = DiagnosticCache::ExtractKey(ctx.diag)};
}

CacheKey GetCacheKeyForChk(const ASTContext& ctx, Ptr<const Node> node, ModalTy target)
{
    return CacheKey{
        .target = target, .isDesugared = IsNodeDesugared(node), .diagKey = DiagnosticCache::ExtractKey(ctx.diag)};
}

void RestoreCached(ASTContext& ctx, Ptr<Node> node, CacheEntry& cache, bool recoverDiag = true)
{
    if (Ty::IsInitialTy(node->DataTy())) {
        // if node cleared before, mark it must be rechecked in post-check before restoring its ty
        ctx.typeCheckCache[node].lastKey = {};
    }
    node->SetTy(cache.result);
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
    return node.GetTy().IsCorrect() && node.TyKind() != AST::TypeKind::TYPE_QUEST &&
        (visitedExpr || visitedDecl || (type && type->TestAttr(Attribute::IS_CHECK_VISITED)));
}

std::optional<ModalTy> TypeChecker::TypeCheckerImpl::PerformBasicChecksForSynthesize(
    ASTContext& ctx, Ptr<Node> node) const
{
    if (!node) {
        return {{TypeManager::GetInvalidTy()}};
    }
    // IS_BROKEN indicates that the node may be illegal, and it may cause unknown errors if it continues.
    if (node->TestAttr(Attribute::IS_BROKEN)) {
        node->SetTy({TypeManager::GetInvalidTy()});
        return {node->GetTy()};
    }
    if (IsChecked(ctx, *node)) {
        return {node->GetTy()};
    }
    if (node->IsDecl() && !node->symbol) {
        return {node->GetTy()};
    }
    return {};
}

ModalTy TypeChecker::TypeCheckerImpl::Synthesize(const CheckerContext& ctx, Ptr<Node> node)
{
    if (auto res = PerformBasicChecksForSynthesize(ctx.Ctx(), node)) {
        return *res;
    }
    nodeStack.Push(node);
    struct StackPop {
        NodeStack& s;
        ~StackPop()
        {
            s.Pop();
        }
    } pop{nodeStack};
    ctx.Ctx().typeCheckCache[node].lastKey = GetCacheKeyForSyn(ctx.Ctx(), node);
    ASTContext* curCtx = &ctx.Ctx();
    // If decl belongs to another package node, then switch to another AST context according to package node.
    if (ci->GetSourcePackages().size() > 1 && node->curFile) {
        if (auto ctx1 = ci->GetASTContextByPackage(node->curFile->curPackage)) {
            curCtx = ctx1;
        }
    }
    CheckerContext newCtx{*curCtx, ctx.SynthPos()};

    if (auto decl = DynamicCast<Decl*>(node)) {
        auto stashDiagnoseStatus = diag.AutoStashDisableDiagnoseStatus();
        CheckAnnotations(*curCtx, *decl);
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
            node->SetTy(SynBlock(newCtx, *StaticAs<ASTKind::BLOCK>(node)));
            break;
        }
        case ASTKind::FUNC_PARAM_LIST: {
            CheckFuncParamList(*curCtx, *StaticAs<ASTKind::FUNC_PARAM_LIST>(node));
            break;
        }
        case ASTKind::FUNC_PARAM: {
            node->SetTy(SynFuncParam(*curCtx, *StaticAs<ASTKind::FUNC_PARAM>(node)));
            break;
        }
        case ASTKind::THIS_PARAM: {
            node->SetTy(SynThisParam(*curCtx, *StaticAs<ASTKind::THIS_PARAM>(node)));
            break;
        }
        case ASTKind::FUNC_ARG: {
            node->SetTy(SynFuncArg(*curCtx, *StaticAs<ASTKind::FUNC_ARG>(node)));
            break;
        }
        case ASTKind::INC_OR_DEC_EXPR: {
            node->SetTy(SynIncOrDecExpr(*curCtx, *StaticAs<ASTKind::INC_OR_DEC_EXPR>(node)));
            break;
        }
        case ASTKind::PAREN_EXPR: {
            node->SetTy(SynParenExpr(newCtx, *StaticAs<ASTKind::PAREN_EXPR>(node)));
            break;
        }
        case ASTKind::LAMBDA_EXPR: {
            node->SetTy(SynLamExpr(*curCtx, *StaticAs<ASTKind::LAMBDA_EXPR>(node)));
            break;
        }
        case ASTKind::RETURN_EXPR: {
            node->SetTy(SynReturnExpr(*curCtx, *StaticAs<ASTKind::RETURN_EXPR>(node)));
            break;
        }
        case ASTKind::LIT_CONST_EXPR: {
            node->SetTy(SynLitConstExpr(*curCtx, *StaticAs<ASTKind::LIT_CONST_EXPR>(node)));
            break;
        }
        case ASTKind::WHILE_EXPR: {
            node->SetTy(SynWhileExpr(*curCtx, *StaticAs<ASTKind::WHILE_EXPR>(node)));
            break;
        }
        case ASTKind::DO_WHILE_EXPR: {
            node->SetTy(SynDoWhileExpr(*curCtx, *StaticAs<ASTKind::DO_WHILE_EXPR>(node)));
            break;
        }
        case ASTKind::FOR_IN_EXPR: {
            node->SetTy(SynForInExpr(*curCtx, *StaticAs<ASTKind::FOR_IN_EXPR>(node)));
            break;
        }
        case ASTKind::UNARY_EXPR: {
            node->SetTy(SynUnaryExpr(*curCtx, *StaticAs<ASTKind::UNARY_EXPR>(node)));
            break;
        }
        case ASTKind::BINARY_EXPR: {
            node->SetTy(SynBinaryExpr(*curCtx, *StaticAs<ASTKind::BINARY_EXPR>(node)));
            break;
        }
        case ASTKind::ASSIGN_EXPR: {
            node->SetTy(SynAssignExpr(*curCtx, *StaticAs<ASTKind::ASSIGN_EXPR>(node)));
            break;
        }
        case ASTKind::QUOTE_EXPR: {
            node->SetTy(SynQuoteExpr(*curCtx, *StaticAs<ASTKind::QUOTE_EXPR>(node)));
            break;
        }
        case ASTKind::IF_EXPR: {
            node->SetTy(SynIfExpr(newCtx, *StaticAs<ASTKind::IF_EXPR>(node)));
            break;
        }
        case ASTKind::TRY_EXPR: {
            node->SetTy(SynTryExpr(*curCtx, *StaticAs<ASTKind::TRY_EXPR>(node)));
            break;
        }
        case ASTKind::REF_EXPR: {
            auto re = StaticAs<ASTKind::REF_EXPR>(node);
            InferRefExpr(*curCtx, *re);
            break;
        }
        case ASTKind::PRIMITIVE_TYPE_EXPR: {
            auto te = StaticAs<ASTKind::PRIMITIVE_TYPE_EXPR>(node);
            te->SetTy({TypeManager::GetPrimitiveTy(te->typeKind)});
            break;
        }
        case ASTKind::CALL_EXPR: {
            node->SetTy(SynCallExpr(*curCtx, *StaticAs<ASTKind::CALL_EXPR>(node)));
            break;
        }
        case ASTKind::TRAIL_CLOSURE_EXPR: {
            node->SetTy(SynTrailingClosure(*curCtx, *StaticAs<ASTKind::TRAIL_CLOSURE_EXPR>(node)));
            break;
        }
        case ASTKind::MEMBER_ACCESS: {
            auto ma = StaticAs<ASTKind::MEMBER_ACCESS>(node);
            InferMemberAccess(*curCtx, *ma);
            break;
        }
        case ASTKind::TYPE_CONV_EXPR: {
            node->SetTy(SynTypeConvExpr(*curCtx, *StaticAs<ASTKind::TYPE_CONV_EXPR>(node)));
            break;
        }
        case ASTKind::IF_AVAILABLE_EXPR:
            node->SetTy(SynIfAvailableExpr(*curCtx, StaticCast<IfAvailableExpr>(*node)));
            break;
        case ASTKind::ARRAY_LIT: {
            node->SetTy(SynArrayLit(*curCtx, *StaticAs<ASTKind::ARRAY_LIT>(node)));
            break;
        }
        case ASTKind::ARRAY_EXPR: {
            auto ae = StaticAs<ASTKind::ARRAY_EXPR>(node);
            if (ae->isValueArray) {
                node->SetTy(SynVArrayExpr(*curCtx, *ae));
            } else {
                CJC_ABORT();
            }
            break;
        }
        case ASTKind::POINTER_EXPR: {
            node->SetTy(SynPointerExpr(*curCtx, *StaticAs<ASTKind::POINTER_EXPR>(node)));
            break;
        }
        case ASTKind::MATCH_EXPR: {
            node->SetTy(SynMatchExpr(*curCtx, *StaticAs<ASTKind::MATCH_EXPR>(node)));
            break;
        }
        case ASTKind::IS_EXPR: {
            node->SetTy(SynIsExpr(*curCtx, *StaticAs<ASTKind::IS_EXPR>(node)));
            break;
        }
        case ASTKind::AS_EXPR: {
            node->SetTy(SynAsExpr(*curCtx, *StaticAs<ASTKind::AS_EXPR>(node)));
            break;
        }
        case ASTKind::OPTIONAL_CHAIN_EXPR: {
            node->SetTy(SynOptionalChainExpr(newCtx, *StaticAs<ASTKind::OPTIONAL_CHAIN_EXPR>(node)));
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
            node->SetTy(SynTupleLit(*curCtx, *StaticAs<ASTKind::TUPLE_LIT>(node)));
            break;
        }
        case ASTKind::JUMP_EXPR: {
            node->SetTy(SynLoopControlExpr(*curCtx, *StaticAs<ASTKind::JUMP_EXPR>(node)));
            break;
        }
        case ASTKind::THROW_EXPR: {
            node->SetTy(SynThrowExpr(*curCtx, *StaticAs<ASTKind::THROW_EXPR>(node)));
            break;
        }
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
        // Effect handlers are only enabled in the CJNative backend, for now
        case ASTKind::PERFORM_EXPR: {
            node->SetTy(SynPerformExpr(*curCtx, *StaticAs<ASTKind::PERFORM_EXPR>(node)));
            break;
        }
        case ASTKind::RESUME_EXPR: {
            node->SetTy(SynResumeExpr(*curCtx, *StaticAs<ASTKind::RESUME_EXPR>(node)));
            break;
        }
#endif // CANGJIE_CODEGEN_CJNATIVE_BACKEND
        case ASTKind::TYPE_ALIAS_DECL: {
            CheckTypeAlias(*curCtx, *StaticAs<ASTKind::TYPE_ALIAS_DECL>(node));
            break;
        }
        case ASTKind::SUBSCRIPT_EXPR: {
            node->SetTy(SynSubscriptExpr(*curCtx, *StaticAs<ASTKind::SUBSCRIPT_EXPR>(node)));
            break;
        }
        case ASTKind::RANGE_EXPR: {
            node->SetTy(SynRangeExpr(*curCtx, *StaticAs<ASTKind::RANGE_EXPR>(node)));
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
            CheckReferenceTypeLegality(*curCtx, *StaticAs<ASTKind::TYPE>(node));
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
            node->SetTy(SynSpawnExpr(*curCtx, *StaticAs<ASTKind::SPAWN_EXPR>(node)));
            break;
        }
        case ASTKind::SYNCHRONIZED_EXPR: {
            node->SetTy(SynSyncExpr(*curCtx, *StaticAs<ASTKind::SYNCHRONIZED_EXPR>(node)));
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
            node->SetTy({TypeManager::GetInvalidTy()});
            break;
        }
        case ASTKind::EXCLAVE_EXPR: {
            node->SetTy(SynExclaveExpr(*curCtx, *StaticAs<ASTKind::EXCLAVE_EXPR>(node)));
            break;
        }
        default: {
            break;
        }
    }
    CJC_ASSERT(!node->GetTy().IsCorrect() || As<ASTKind::EXPR>(node) == nullptr ||
        StaticAs<ASTKind::EXPR>(node)->desugarExpr == nullptr ||
        node->GetTy() == StaticAs<ASTKind::EXPR>(node)->desugarExpr->GetTy());
    node->SetTy(typeManager.TryGreedySubst(node->GetTy()));
    return TypeManager::GetNonNullTy(node->GetTy());
}

std::optional<bool> TypeChecker::TypeCheckerImpl::PerformBasicChecksForCheck(
    ASTContext& ctx, ModalTy target, Ptr<Node> node) const
{
    if (!node) {
        return {false};
    }
    if (!target.IsCorrect()) {
        node->SetTy({TypeManager::GetInvalidTy()});
        return {false};
    }
    ctx.targetTypeMap[node] = target;
    // IS_BROKEN indicates that the node may be illegal, and it may cause unknown errors if it continues.
    if (node->TestAttr(Attribute::IS_BROKEN)) {
        node->SetTy({TypeManager::GetInvalidTy()});
        return {false};
    }
    if (target->HasQuestTy() && !IsQuestableNode(*node)) {
        return {false};
    }
    return {};
}

bool TypeChecker::TypeCheckerImpl::Check(ASTContext& ctx, ModalTy target, Ptr<Node> node)
{
    if (auto res = PerformBasicChecksForCheck(ctx, target, node)) {
        return *res;
    }
    nodeStack.Push(node);
    struct StackPop {
        NodeStack& s;
        ~StackPop()
        {
            s.Pop();
        }
    } pop{nodeStack};
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
        auto& cst = typeManager.constraints[RawStaticCast<GenericsTy*>(realTarget.get())];
        ModalTy lub{};
        if (!cst.ubs.empty()) {
            std::set<ModalTy> ubModals;
            for (const auto& ub : cst.ubs.raw()) {
                ubModals.insert(ModalTy{ub});
            }
            auto meetRes = JoinAndMeet(typeManager, ubModals, typeManager.GetUnsolvedTyVars()).MeetAsVisibleTy();
            if (std::holds_alternative<ModalTy>(meetRes)) {
                lub = std::get<ModalTy>(meetRes);
            }
        }
        if (lub) {
            chkRet = Check(ctx, lub, node) && typeManager.IsSubtype(node->GetTy(), realTarget);
        } else {
            Synthesize({ctx, SynPos::NONE}, node);
            ReplaceIdealTy(*node);
            chkRet = typeManager.IsSubtype(node->GetTy(), realTarget);
        }
    } else {
        switch (node->astKind) {
            case ASTKind::IF_EXPR: {
                chkRet = ChkIfExpr(*curCtx, realTarget, *StaticAs<ASTKind::IF_EXPR>(node));
                break;
            }
            case ASTKind::ASSIGN_EXPR: {
                chkRet = ChkAssignExpr(*curCtx, realTarget, *StaticAs<ASTKind::ASSIGN_EXPR>(node));
                break;
            }
            case ASTKind::LIT_CONST_EXPR: {
                auto lce = StaticAs<ASTKind::LIT_CONST_EXPR>(node);
                chkRet = ChkLitConstExpr(*curCtx, realTarget, *lce);
                InitializeLitConstValue(*lce);
                break;
            }
            case ASTKind::ARRAY_LIT: {
                chkRet = ChkArrayLit(*curCtx, realTarget, *StaticAs<ASTKind::ARRAY_LIT>(node));
                break;
            }
            case ASTKind::ARRAY_EXPR: {
                auto ae = StaticAs<ASTKind::ARRAY_EXPR>(node);
                if (ae->isValueArray) {
                    chkRet = ChkVArrayExpr(*curCtx, realTarget, *ae);
                } else {
                    CJC_ABORT();
                }
                break;
            }
            case ASTKind::POINTER_EXPR: {
                chkRet = ChkPointerExpr(*curCtx, realTarget, *StaticAs<ASTKind::POINTER_EXPR>(node));
                break;
            }
            case ASTKind::TUPLE_LIT: {
                chkRet = ChkTupleLit(*curCtx, realTarget, *StaticAs<ASTKind::TUPLE_LIT>(node));
                break;
            }
            case ASTKind::WHILE_EXPR: {
                chkRet = ChkWhileExpr(*curCtx, realTarget, *StaticAs<ASTKind::WHILE_EXPR>(node));
                break;
            }
            case ASTKind::DO_WHILE_EXPR: {
                chkRet = ChkDoWhileExpr(*curCtx, realTarget, *StaticAs<ASTKind::DO_WHILE_EXPR>(node));
                break;
            }
            case ASTKind::FOR_IN_EXPR: {
                chkRet = ChkForInExpr(*curCtx, realTarget, *StaticAs<ASTKind::FOR_IN_EXPR>(node));
                break;
            }
            case ASTKind::RANGE_EXPR: {
                chkRet = ChkRangeExpr(*curCtx, realTarget, *StaticAs<ASTKind::RANGE_EXPR>(node));
                break;
            }
            case ASTKind::PAREN_EXPR: {
                chkRet = ChkParenExpr(*curCtx, realTarget, *StaticAs<ASTKind::PAREN_EXPR>(node));
                break;
            }
            case ASTKind::BINARY_EXPR: {
                chkRet = ChkBinaryExpr(ctx, realTarget, *StaticAs<ASTKind::BINARY_EXPR>(node));
                break;
            }
            case ASTKind::INC_OR_DEC_EXPR: {
                chkRet = ChkIncOrDecExpr(*curCtx, realTarget, *StaticAs<ASTKind::INC_OR_DEC_EXPR>(node));
                break;
            }
            case ASTKind::UNARY_EXPR: {
                chkRet = ChkUnaryExpr(*curCtx, realTarget, *StaticAs<ASTKind::UNARY_EXPR>(node));
                break;
            }
            case ASTKind::TYPE_CONV_EXPR: {
                chkRet = ChkTypeConvExpr(*curCtx, realTarget, *StaticAs<ASTKind::TYPE_CONV_EXPR>(node));
                break;
            }
            case ASTKind::IF_AVAILABLE_EXPR:
                chkRet = ChkIfAvailableExpr(*curCtx, realTarget, StaticCast<IfAvailableExpr>(*node));
                break;
            case ASTKind::JUMP_EXPR:
                chkRet = ChkLoopControlExpr(*curCtx, *StaticAs<ASTKind::JUMP_EXPR>(node));
                break;
            case ASTKind::MATCH_EXPR: {
                chkRet = ChkMatchExpr(*curCtx, realTarget, *StaticAs<ASTKind::MATCH_EXPR>(node));
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
                chkRet = ChkPattern(*curCtx, realTarget, *StaticAs<ASTKind::PATTERN>(node));
                break;
            }
            case ASTKind::BLOCK: {
                chkRet = ChkBlock(*curCtx, realTarget, *StaticAs<ASTKind::BLOCK>(node));
                break;
            }
            case ASTKind::SUBSCRIPT_EXPR: {
                chkRet = ChkSubscriptExpr(*curCtx, realTarget, *StaticAs<ASTKind::SUBSCRIPT_EXPR>(node));
                break;
            }
            case ASTKind::MEMBER_ACCESS: // Fall-through.
            case ASTKind::REF_EXPR: {
                chkRet = ChkRefExpr(*curCtx, realTarget, *RawStaticCast<NameReferenceExpr*>(node));
                break;
            }
            case ASTKind::CALL_EXPR: {
                chkRet = ChkCallExpr(*curCtx, realTarget, *StaticAs<ASTKind::CALL_EXPR>(node));
                break;
            }
            case ASTKind::TRAIL_CLOSURE_EXPR: {
                chkRet = ChkTrailingClosureExpr(*curCtx, realTarget, *StaticAs<ASTKind::TRAIL_CLOSURE_EXPR>(node));
                break;
            }
            case ASTKind::TRY_EXPR: {
                chkRet = ChkTryExpr(*curCtx, realTarget, *StaticAs<ASTKind::TRY_EXPR>(node));
                break;
            }
            case ASTKind::RETURN_EXPR: {
                chkRet = ChkReturnExpr(*curCtx, *StaticAs<ASTKind::RETURN_EXPR>(node));
                break;
            }
            case ASTKind::LAMBDA_EXPR: {
                chkRet = ChkLamExpr(*curCtx, realTarget, *StaticAs<ASTKind::LAMBDA_EXPR>(node));
                break;
            }
            case ASTKind::QUOTE_EXPR: {
                chkRet = ChkQuoteExpr(*curCtx, realTarget, *StaticAs<ASTKind::QUOTE_EXPR>(node));
                break;
            }
            case ASTKind::FUNC_PARAM: {
                chkRet = ChkFuncParam(*curCtx, realTarget, *StaticAs<ASTKind::FUNC_PARAM>(node));
                break;
            }
            case ASTKind::THIS_PARAM: {
                SynThisParam(*curCtx, *StaticAs<ASTKind::THIS_PARAM>(node));
                chkRet = node->GetTy().IsCorrect();
                break;
            }
            case ASTKind::FUNC_ARG: {
                chkRet = ChkFuncArg(*curCtx, realTarget, *StaticAs<ASTKind::FUNC_ARG>(node));
                break;
            }
            case ASTKind::SPAWN_EXPR: {
                chkRet = ChkSpawnExpr(*curCtx, realTarget, *StaticAs<ASTKind::SPAWN_EXPR>(node));
                break;
            }
            case ASTKind::SYNCHRONIZED_EXPR: {
                chkRet = ChkSyncExpr(*curCtx, realTarget, *StaticAs<ASTKind::SYNCHRONIZED_EXPR>(node));
                break;
            }
            case ASTKind::IS_EXPR: {
                chkRet = ChkIsExpr(*curCtx, realTarget, *StaticAs<ASTKind::IS_EXPR>(node));
                break;
            }
            case ASTKind::AS_EXPR: {
                chkRet = ChkAsExpr(*curCtx, realTarget, *StaticAs<ASTKind::AS_EXPR>(node));
                break;
            }
            case ASTKind::OPTIONAL_CHAIN_EXPR: {
                chkRet = ChkOptionalChainExpr(*curCtx, realTarget, *StaticAs<ASTKind::OPTIONAL_CHAIN_EXPR>(node));
                break;
            }
            case ASTKind::MACRO_EXPAND_EXPR: // Just for LSP, normal process does not have macros here.
            case ASTKind::MACRO_EXPAND_PARAM:
            case ASTKind::MACRO_EXPAND_DECL: {
                CheckMacroCall(*curCtx, *node);
                break;
            }
            case ASTKind::EXCLAVE_EXPR: {
                chkRet = ChkExclaveExpr(*curCtx, realTarget, *StaticAs<ASTKind::EXCLAVE_EXPR>(node));
                break;
            }
            default: {
                Synthesize({ctx, SynPos::NONE}, node);
                ReplaceIdealTy(*node);
                chkRet = typeManager.IsSubtype(node->GetTy(), realTarget);
                break;
            }
        }
    }
    CJC_ASSERT(!node->GetTy().IsCorrect() || As<ASTKind::EXPR>(node) == nullptr ||
        StaticAs<ASTKind::EXPR>(node)->desugarExpr == nullptr ||
        node->GetTy() == StaticAs<ASTKind::EXPR>(node)->desugarExpr->GetTy());
    ctx.targetTypeMap[node] = ModalTy{};
    node->SetTy(typeManager.TryGreedySubst(node->GetTy()));
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
    if (auto exclave = DynamicCast<ExclaveExpr>(firstExprOrDecl)) {
        if (!exclave->body || exclave->body->body.empty()) {
            return;
        }
        firstExprOrDecl = exclave->body->body.front().get();
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

ModalTy TypeChecker::TypeCheckerImpl::SynthesizeWithCache(const CheckerContext& ctx, Ptr<Node> node)
{
    CJC_NULLPTR_CHECK(node);
    if (!typeManager.GetUnsolvedTyVars().empty()) {
        return Synthesize(ctx, node);
    }
    CacheKey key = GetCacheKeyForSyn(ctx.Ctx(), node);
    if (ctx.Ctx().typeCheckCache[node].synCache.count(key) != 0) {
        auto& cache = ctx.Ctx().typeCheckCache[node].synCache[key];
        RestoreCached(ctx.Ctx(), node, cache);
        return cache.result;
    } else {
        return SynthesizeAndCache(ctx, node, key);
    }
}

bool TypeChecker::TypeCheckerImpl::CheckWithCache(ASTContext& ctx, ModalTy target, Ptr<Node> node)
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

ModalTy TypeChecker::TypeCheckerImpl::SynthesizeWithNegCache(const CheckerContext& ctx, Ptr<Node> node)
{
    CJC_NULLPTR_CHECK(node);
    if (!typeManager.GetUnsolvedTyVars().empty()) {
        return Synthesize(ctx, node);
    }
    CacheKey key = GetCacheKeyForSyn(ctx.Ctx(), node);
    if (ctx.Ctx().typeCheckCache[node].synCache.count(key) != 0 && !ctx.Ctx().typeCheckCache[node].synCache[key].successful) {
        auto& cache = ctx.Ctx().typeCheckCache[node].synCache[key];
        RestoreCached(ctx.Ctx(), node, cache);
        return cache.result;
    } else {
        return SynthesizeAndCache(ctx, node, key);
    }
}

bool TypeChecker::TypeCheckerImpl::CheckWithNegCache(ASTContext& ctx, ModalTy target, Ptr<Node> node)
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
    ASTContext& ctx, ModalTy target, Ptr<Node> node, bool recoverDiag)
{
    if (!typeManager.GetUnsolvedTyVars().empty() || !node) {
        return Check(ctx, target, node);
    }
    CacheKey key = GetCacheKeyForChk(ctx, node, target);
    if (!Ty::IsInitialTy(node->DataTy())) {
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
    node->Clear();
    return CheckAndCache(ctx, target, node, key);
}

ModalTy TypeChecker::TypeCheckerImpl::SynthesizeWithEffectiveCache(
    const CheckerContext& ctx, Ptr<Node> node, bool recoverDiag)
{
    if (!typeManager.GetUnsolvedTyVars().empty() || !node) {
        return Synthesize(ctx, node);
    }
    CacheKey key = GetCacheKeyForSyn(ctx.Ctx(), node);
    if (!Ty::IsInitialTy(node->DataTy())) {
        if (ctx.Ctx().typeCheckCache[node].lastKey && ctx.Ctx().typeCheckCache[node].lastKey.value() == key) {
            if (ctx.Ctx().typeCheckCache[node].synCache.count(key) != 0) {
                auto& cache = ctx.Ctx().typeCheckCache[node].synCache[key];
                RestoreCached(ctx.Ctx(), node, cache, recoverDiag);
                return cache.result;
            } else {
                return SynthesizeAndCache(ctx, node, key);
            }
        }
    }
    return SynthesizeAndCache(ctx, node, key);
}

AST::ModalTy TypeChecker::TypeCheckerImpl::SynthesizeAndCache(
    const CheckerContext& ctx, Ptr<AST::Node> node, const CacheKey& key)
{
    DiagnosticCache dc;
    dc.ToExclude(ctx.Ctx().diag);
    auto ret = Synthesize(ctx, node);
    dc.BackUp(ctx.Ctx().diag);
    ctx.Ctx().typeCheckCache[node].synCache[key] = {.successful = ret && ret.IsCorrect() && dc.NoError(),
        .result = ret,
        .diags = std::move(dc),
        .targets = CollectTargets(*node)};
    return ret;
}

bool TypeChecker::TypeCheckerImpl::CheckAndCache(ASTContext& ctx, ModalTy target, Ptr<Node> node, const CacheKey& key)
{
    DiagnosticCache dc;
    dc.ToExclude(ctx.diag);
    bool ret = Check(ctx, target, node);
    dc.BackUp(ctx.diag);
    ctx.typeCheckCache[node].chkCache[key] = {
        .successful = ret, .result = node->GetTy(), .diags = std::move(dc), .targets = CollectTargets(*node)};
    return ret;
}

namespace {
/**  Check if a class has a non-parameter constructor. */
bool HasNonParamCtorForClass(const ClassDecl& classDecl, ModalInfo requiredModal)
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
        auto thisParamTy = StaticCast<FuncTy>(fd->DataTy())->retTy;
        if (thisParamTy.Mode() != requiredModal) {
            continue;
        }
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
    auto requiredModal = GetThisParamModal(fd);
    needEraseSuper = superCD && !HasNonParamCtorForClass(*superCD, requiredModal);
    if (!needEraseSuper) {
        return;
    }
    auto& fdName = fd.TestAttr(Attribute::PRIMARY_CONSTRUCTOR) ? fd.identifierForLsp : fd.identifier.Val();
    auto range =
        fd.TestAttr(Attribute::IMPLICIT_ADD) ? MakeRange(decl.identifier) : MakeRange(fd.identifier.Begin(), fdName);
    auto modeString = requiredModal == ModalInfo{} ? "" : " with " + requiredModal.LocalString() + " mode";
    diag.DiagnoseRefactor(DiagKindRefactor::sema_no_non_param_constructor_in_super_class, fd, range, modeString);
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
        Synthesize({ctx, SynPos::NONE}, decl.get());
        CheckCTypeMember(*decl);
    }
}

void TypeChecker::TypeCheckerImpl::CheckJavaInteropLibImport(Decl& decl)
{
    if (!Interop::Java::InteropLibBridge::IsInteropLibAccessible(importManager)) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_java_mirror_interoplib_must_be_imported, decl);
        decl.EnableAttr(Attribute::IS_BROKEN);
    }
}

void TypeChecker::TypeCheckerImpl::CheckObjCInteropLibImport(Decl& decl)
{
    if (!Interop::ObjC::InteropLibBridge::IsInteropLibAccessible(importManager)) {
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
    if (auto fty = DynamicCast<AST::FuncTy>(fb->DataTy()); fty && fty->isC) {
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
        if (auto fty = DynamicCast<AST::FuncTy>(outerFb->DataTy()); fty && fty->isC) {
            return true;
        }
        outerFb = GetCurFuncBody(ctx, ScopeManagerApi::GetParentScopeName(outerFb->scopeName));
    }
    return false;
}

void TypeChecker::TypeCheckerImpl::CheckLegalUseOfClosure(Expr& e, DiagKind kind, LambdaSource ls) const
{
    if (e.TestAttr(Attribute::IS_BROKEN)) {
        return;
    }
    if (auto target = DynamicCast<LambdaExpr*>(&e); target && target->funcBody) {
        if (target->funcBody->captureKind == CaptureKind::CAPTURE_VAR) {
            DiagUseClosureCaptureVarAlone(diag, e, ls);
        } else if (target->funcBody->captureKind == CaptureKind::TRANSITIVE_CAPTURE) {
            diag.Diagnose(e, kind, "lambda", "transitively", "lambda");
        }
    }
    if (auto ref = DynamicCast<RefExpr*>(&e); ref) {
        if (auto target = DynamicCast<FuncDecl*>(ref->ref.target); target) {
            if (target->funcBody->captureKind == CaptureKind::CAPTURE_VAR) {
                DiagUseClosureCaptureVarAlone(diag, e, ls);
            } else if (target->funcBody->captureKind == CaptureKind::TRANSITIVE_CAPTURE) {
                diag.Diagnose(e, kind, target->identifier.Val(), "transitively", target->identifier.Val());
            }
        }
    }
}

void TypeChecker::TypeCheckerImpl::CheckLegalUseOfClosure(const ASTContext& ctx, Node& node, LambdaSource ls) const
{
    if (auto vd = DynamicCast<VarDecl*>(&node); vd) {
        if (vd->initializer) {
            CheckLegalUseOfClosure(*vd->initializer, DiagKind::sema_func_capture_var_cannot_assign, ls);
        }
    } else if (auto re = DynamicCast<ReturnExpr*>(&node); re) {
        if (re->expr) {
            CheckLegalUseOfClosure(*re->expr, DiagKind::sema_func_capture_var_cannot_return, ls);
        }
    } else if (auto ce = DynamicCast<CallExpr*>(&node); ce) {
        if (auto baseRe = DynamicCast<RefExpr*>(ce->baseFunc.get()); baseRe && IsCapturedInCFuncLambda(ctx, *baseRe)) {
            diag.Diagnose(*baseRe, DiagKind::sema_cfunc_cannot_capture_var, baseRe->ref.identifier.Val());
        }
        for (auto& arg : ce->args) {
            if (arg && arg->expr) {
                CheckLegalUseOfClosure(*arg->expr, DiagKind::sema_func_capture_var_cannot_param, ls);
            }
        }
    } else if (auto ref = DynamicCast<RefExpr*>(&node); ref && !ref->isBaseFunc) {
        CheckLegalUseOfClosure(*ref, DiagKind::sema_func_capture_var_cannot_expr, ls);
        if (auto refVd = DynamicCast<VarDecl*>(ref->ref.target); refVd) {
            if (IsCapturedCStructOfClosure(*refVd)) {
                diag.Diagnose(*ref, DiagKind::sema_func_capture_var_not_ctype);
            }
        }
        if (IsCapturedInCFuncLambda(ctx, *ref)) {
            diag.Diagnose(*ref, DiagKind::sema_cfunc_cannot_capture_var, ref->ref.identifier.Val());
        }
    } else if (auto lambda = DynamicCast<LambdaExpr*>(&node); lambda && !lambda->isBaseFunc) {
        CheckLegalUseOfClosure(*lambda, DiagKind::sema_func_capture_var_cannot_expr, ls);
    }
}

bool TypeChecker::TypeCheckerImpl::IsCapturedCStructOfClosure(const VarDecl& decl) const
{
    return decl.TestAttr(Attribute::IS_CAPTURE) && decl.GetTy() && decl.GetTy()->IsCTypeConstraint();
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
                if (lce->GetTy() && lce->GetTy()->IsInteger()) {
                    auto primitiveTy = RawStaticCast<PrimitiveTy*>(lce->DataTy());
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

void AddAttrForDefaultFuncParam(Package& pkg)
{
    Utils::ProfileRecorder recorder("Post TypeCheck", "AddAttrForDefaultFuncParam");
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
    Utils::ProfileRecorder recorder("Post TypeCheck", "MarkImplicitUsedFunctions");
    // NOTE: These are special toplevel functions that are not exported but will be reference in other package.
    static const std::unordered_map<std::string, std::unordered_set<std::string>> SPECIAL_EXPORTED_FUNCS{
        {CORE_PACKAGE_NAME,
            {"arrayInitByCollection", "arrayInitByFunction", "composition", "handleException",
                "createOverflowExceptionMsg", "createArithmeticExceptionMsg", "getCommandLineArgs"}},
        {AST_PACKAGE_NAME,
            {MACRO_OBJECT_NAME, "refreshTokensPosition", "refreshPos", "unsafePointerCastFromUint8Array", "transformTokens"}}};
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
    const ASTContext& ctx, ModalTy interfaceTy, Decl& target, const MultiTypeSubst& typeMapping)
{
    auto targetInstanceTy = typeManager.GetBestInstantiatedTy(target.GetTy(), typeMapping);
    auto id = Ty::GetDeclPtrOfTy<InheritableDecl>(interfaceTy);
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
            auto memberFuncTy = DynamicCast<FuncTy*>(member->DataTy());
            auto targetFuncTy = DynamicCast<FuncTy*>(targetInstanceTy.Ty());
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
            bool thisParamMatch{true};
            if (typeManager.HasThisParam(*memberFuncDecl)) {
                thisParamMatch =
                    typeManager.CheckTypeCompatibility(typeManager.GetThisParamTy(*memberFuncDecl),
                        typeManager.GetThisParamTy(*targetFuncDecl), false) == TypeCompatibility::IDENTICAL;
            }
            isSameSignature =
                typeManager.IsFuncParameterTypesIdentical(memberParamTys, targetParamTys) && thisParamMatch;
        } else if (member->astKind == ASTKind::PROP_DECL && target.astKind == ASTKind::PROP_DECL) {
            if (!TypeCheckUtil::IsOverrideOrShadow(
                typeManager, *RawStaticCast<PropDecl*>(member), static_cast<PropDecl&>(target))) {
                continue;
            }
            isSameSignature = member->GetTy() == targetInstanceTy;
        }
        if (isSameSignature && !member->TestAttr(Attribute::ABSTRACT)) {
            return member;
        }
    }
    return nullptr;
}

std::pair<bool, Ptr<RefExpr>> TypeChecker::TypeCheckerImpl::CheckInvokeTargetHasImpl(const ASTContext& ctx,
    ModalTy interfaceTy, Decl& decl, MultiTypeSubst& typeMapping, std::unordered_set<Ptr<AST::Decl>>& traversedDecls)
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
            if (re->matchedParentTy && target->outerDecl->GetTy()) {
                typeMapping.merge(
                    promotion.GetPromoteTypeMapping(re->matchedParentTy.Ty(), target->outerDecl->DataTy()));
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
    Utils::ProfileRecorder::Start("Pre TypeCheck", "PrepareTypeCheck");
    for (auto& ctx : contexts) {
        PrepareTypeCheck(*ctx, *ctx->curPackage);
    }
    Utils::ProfileRecorder::Stop("Pre TypeCheck", "PrepareTypeCheck");
    // Pre checking for resolving types and rules without semantic type.
    PreCheck(contexts);

    Utils::ProfileRecorder::Start("Pre TypeCheck", "CollectDeclsWithMember");
    for (auto pkg : pkgs) {
        if (auto ctx = ci->GetASTContextByPackage(pkg)) {
            CollectDeclsWithMember(pkg, *ctx);
        }
    }
    Utils::ProfileRecorder::Stop("Pre TypeCheck", "CollectDeclsWithMember");

    Utils::ProfileRecorder::Start("Pre TypeCheck", "MatchSpecificWithCommon");
    for (auto pkg : pkgs) {
        mpImpl->MatchSpecificWithCommon(*pkg);
        mpImpl->CheckNotAllowedAnnotations(*pkg);
    }
    Utils::ProfileRecorder::Stop("Pre TypeCheck", "MatchSpecificWithCommon");

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
        Utils::ProfileRecorder::Start("Post TypeCheck", "CheckCJMPRules");
        mpImpl->CheckReturnAndVariableTypes(*ctx->curPackage);
        mpImpl->ValidateMatchedAnnotationsAndModifiers(*ctx->curPackage);
        Utils::ProfileRecorder::Stop("Post TypeCheck", "CheckCJMPRules");
        AddAttrForDefaultFuncParam(*ctx->curPackage);
        // Because of the cjlint checking policy, desugar of propDecl should be done in sema stage for now.
        DesugarForPropDecl(*ctx->curPackage);
        CheckModalType(*ctx, *ctx->curPackage);
        CheckConstEvaluation(*ctx->curPackage);
        MarkImplicitUsedFunctions(*ctx->curPackage);
        // Collect program entry separately.
        IterateToplevelDecls(*ctx->curPackage, [this](auto& decl) {
            if (auto md = DynamicCast<MainDecl*>(decl.get()); md && md->desugarDecl) {
                (void)mainFunctionMap[md->curFile].emplace(md->desugarDecl.get());
            }
        });
        Utils::ProfileRecorder::Start("Post TypeCheck", "PluginCheck");
        PluginCheck::PluginCustomAnnoChecker(*ci, diag, importManager).Check(*ctx->curPackage);
        Utils::ProfileRecorder::Stop("Post TypeCheck", "PluginCheck");
    }
    CheckWhetherHasProgramEntry();
}

void TypeChecker::TypeCheckerImpl::PrepareTypeCheck(ASTContext& ctx, Package& pkg)
{
    // Reset search's cache.
    ctx.searcher->InvalidateCache();

    CheckPrimaryCtorBeforeMerge(pkg);
    // Merging common classes into specific if any
    mpImpl->PrepareTypeCheck4CJMP(pkg);

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    Utils::ProfileRecorder::Start("PrepareTypeCheck", "InteropPrepare");
    Cangjie::Native::FFI::Java::PrepareTypeCheck(pkg, importManager, typeManager);
    Interop::ObjC::PrepareTypeCheck(pkg);
    Utils::ProfileRecorder::Stop("PrepareTypeCheck", "InteropPrepare");
#endif

    // Phase: add some default function.
    Utils::ProfileRecorder::Start("PrepareTypeCheck", "AddDefaultFuncAndBuiltinDecl");
    AddDefaultFunction(pkg);
    // Add built-in decls to core package.
    if (pkg.fullPackageName == CORE_PACKAGE_NAME && !pkg.files.empty() && !pkg.TestAttr(Attribute::IMPORTED)) {
        CreateBuiltinDecls(pkg);
    }
    Utils::ProfileRecorder::Stop("PrepareTypeCheck", "AddDefaultFuncAndBuiltinDecl");
    // Phase: build symbol table.
    Utils::ProfileRecorder::Start("PrepareTypeCheck", "BuildSymbolTable");
    Collector collector(scopeManager, ci->invocation.globalOptions.enableMacroInLSP);
    collector.BuildSymbolTable(ctx, &pkg, ci->buildTrie);
    Utils::ProfileRecorder::Stop("PrepareTypeCheck", "BuildSymbolTable");
    // Phase: mark outermost binary expressions.
    Utils::ProfileRecorder::Start("PrepareTypeCheck", "MarkAndPrepare");
    MarkOutermostBinaryExpressions(pkg);
    AddCurFile(pkg);
    MarkParamWithInitialValue(pkg);
    Utils::ProfileRecorder::Stop("PrepareTypeCheck", "MarkAndPrepare");
    // Warmup cache to speed up search.
    SearchSymbol::WarmupCache(ctx);
}

void TypeChecker::TypeCheckerImpl::TypeCheckTopLevelDecl(ASTContext& ctx, Decl& decl)
{
    TyVarScope ts(typeManager); // to release local placeholder ty var
    Synthesize({ctx, SynPos::NONE}, &decl);
    MarkOverflow(decl);
}

// Check generic members declared inside non-generic inheritable decls.
void TypeChecker::TypeCheckerImpl::TypeCheckImportedGenericMember(ASTContext& ctx)
{
    std::vector<Symbol*> syms = SearchSymbol::GetAllStructDecls(ctx);
    for (auto sym : syms) {
        CJC_ASSERT(sym && sym->node);
        auto id = StaticCast<InheritableDecl*>(sym->node);
        // Generic toplevel decls will be checked before by 'TypeCheckTopLevelDecl'.
        if (id->TestAttr(Attribute::GENERIC)) {
            continue;
        }
        for (auto& member : id->GetMemberDecls()) {
            if (member->TestAttr(Attribute::GENERIC)) {
                Synthesize({ctx, SynPos::NONE}, member.get());
            }
        }
    }
}

void TypeChecker::TypeCheckerImpl::TypeCheck(ASTContext& ctx, Package& pkg)
{
    std::vector<Symbol*> syms = SearchSymbol::GetToplevelDecls(ctx);
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
        Synthesize({ctx, SynPos::NONE}, node);
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
    auto first = &*fd.funcBody->body->body.front();
    if (auto exclave = DynamicCast<ExclaveExpr*>(first)) {
        if (exclave->body && exclave->body->body.empty()) {
            return;
        }
        first = &*exclave->body->body.front();
    }
    if (auto ce = DynamicCast<CallExpr>(first)) {
        if (auto re = DynamicCast<RefExpr*>(ce->baseFunc.get()); re) {
            refExpr = re;
        }
    } else if (auto tce = DynamicCast<TrailingClosureExpr>(first)) {
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
        CopyBasicInfo(constraint.get(), gc.get());
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
    if (funcDecl.funcBody->paramLists[0]->thisParam) {
        auto clonedThis = MakeOwnedNode<ThisParam>();
        clonedThis->thisPos = funcDecl.funcBody->paramLists[0]->thisParam->thisPos;
        clonedThis->modal = funcDecl.funcBody->paramLists[0]->thisParam->modal;
        clonedThis->outerDecl = &*ret;
        for (auto& anno : funcDecl.funcBody->paramLists[0]->thisParam->annotations) {
            clonedThis->annotations.emplace_back(ASTCloner::Clone(anno.get(), SetIsClonedSourceCode));
        }
        funcBody->paramLists[0]->thisParam = std::move(clonedThis);
    }
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

Ptr<Decl> TypeChecker::TypeCheckerImpl::GetDupInterfaceRecursively(const Node& triggerNode, ModalTy interfaceTy,
    const TypeSubst& instantiateMap, std::unordered_set<Ptr<InterfaceTy>>& res,
    std::unordered_set<Ptr<ClassLikeDecl>>& passedClassLikeDecls)
{
    auto insTy = typeManager.GetInstantiatedTy(interfaceTy, instantiateMap);
    // When `ity` is invalid, it indicates that an inheritance cycle appeared.
    // In this case, we do not need to continue with the following steps.
    if (!insTy.IsCorrect() || !insTy->IsInterface()) {
        return nullptr;
    }
    auto ity = RawStaticCast<InterfaceTy*>(insTy.Ty());
    auto insertRes = res.insert(ity);
    if (!insertRes.second) {
        return ity->declPtr;
    }
    CJC_NULLPTR_CHECK(ity->declPtr);
    TypeSubst superInstantiateMap = GenerateTypeMapping(*ity->declPtr, ity->TyArgs());
    if (superInstantiateMap.empty()) {
        return nullptr;
    }
    return GetDupSuperInterface(triggerNode, *ity->declPtr, superInstantiateMap, passedClassLikeDecls);
}

Ptr<Decl> TypeChecker::TypeCheckerImpl::GetExtendDupSuperInterface(const Node& triggerNode, const InheritableDecl& decl,
    const TypeSubst& instantiateMap, std::unordered_set<Ptr<InterfaceTy>>& res,
    std::unordered_set<Ptr<ClassLikeDecl>>& passedClassLikeDecls)
{
    if (!decl.TestAttr(Attribute::GENERIC) || !decl.GetTy().IsCorrect()) {
        return nullptr;
    }
    auto extends = typeManager.GetDeclExtends(decl);
    for (auto& extend : extends) {
        // Generate typeMapping from extend decl's generic type to original decl type.
        TypeSubst extendInstMap = GenerateTypeMapping(*extend, decl.GetTy()->TyArgs());
        if (extendInstMap.size() != instantiateMap.size()) {
            continue;
        }
        extendInstMap.insert(instantiateMap.begin(), instantiateMap.end());
        for (auto& interfaceType : extend->inheritedTypes) {
            if (!interfaceType->GetTy().IsCorrect()) {
                continue;
            }
            auto ret = GetDupInterfaceRecursively(
                triggerNode, interfaceType->GetTy(), extendInstMap, res, passedClassLikeDecls);
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
        if (!interfaceType->GetTy().IsCorrect()) {
            continue;
        }
        auto ret = GetDupInterfaceRecursively(
            triggerNode, interfaceType->GetTy(), instantiateMap, instInterfaceTys, passedClassLikeDecls);
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
    auto baseDecl = Ty::GetDeclPtrOfTy(decl.GetTy());
    std::string name = baseDecl ? baseDecl->identifier.Val() : Ty::ToString(decl.DataTy());
    if (interfaceDecl) {
        diag.Diagnose(triggerNode, DiagKind::sema_inherit_duplicate_interface, DeclKindToString(decl), name,
            interfaceDecl->identifier.Val());
    }
}

VisitAction TypeChecker::TypeCheckerImpl::CheckInstDupSuperInterfaces(const Type& type)
{
    if (!type.GetTy().IsCorrect()) {
        return VisitAction::SKIP_CHILDREN;
    }
    auto typeTarget = TypeCheckUtil::GetRealTarget(type.GetTarget());
    if (!typeTarget || !typeTarget->IsNominalDecl()) {
        return VisitAction::WALK_CHILDREN;
    }
    TypeSubst typeMapping = GenerateTypeMapping(*typeTarget, type.GetTy()->TyArgs());
    if (typeMapping.empty()) {
        return VisitAction::SKIP_CHILDREN;
    }
    // Duplicate check of extend's parent class has been completed in Extend check.
    CheckInstDupSuperInterfaces(type, *StaticCast<InheritableDecl*>(typeTarget), typeMapping, false);
    return VisitAction::WALK_CHILDREN;
}

VisitAction TypeChecker::TypeCheckerImpl::CheckInstDupSuperInterfaces(const Expr& expr)
{
    if (!expr.GetTy().IsCorrect()) {
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
    Utils::ProfileRecorder recorder("Post TypeCheck", "CheckInstDupSuperInterfacesEntry");
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
    Utils::ProfileRecorder recorder("Post TypeCheck", "CheckOverflow");
    Walker walker(&node, nullptr, [this](Ptr<Node> node) -> VisitAction {
        switch (node->astKind) {
            case ASTKind::LIT_CONST_EXPR: {
                auto& lce = *StaticAs<ASTKind::LIT_CONST_EXPR>(node);
                if (!lce.desugarExpr && lce.GetTy().IsCorrect()) {
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

ModalTy TypeChecker::TypeCheckerImpl::CalcFuncRetTyFromBody(const FuncBody& fb)
{
    if (!fb.body) {
        return {TypeManager::GetInvalidTy()};
    }

    if (fb.body->body.empty()) {
        return {TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT)};
    }

    auto& lastNode = fb.body->body.back();
    ModalTy bodyTy = lastNode->IsDecl() ? ModalTy{TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT)} : lastNode->GetTy();
    ModalTy retTy = bodyTy;
    std::set<ModalTy> retTys;
    Walker(fb.body.get(), [&retTys](auto node) {
        CJC_ASSERT(node);
        if (node->astKind == ASTKind::FUNC_DECL || node->astKind == ASTKind::LAMBDA_EXPR) {
            return VisitAction::SKIP_CHILDREN;
        } else if (auto re = DynamicCast<ReturnExpr*>(node); re && re->expr) {
            if (re->expr->GetTy().IsCorrect()) {
                retTys.emplace(re->expr->GetTy());
            }
        } else if (auto exclave = DynamicCast<ExclaveExpr>(node)) {
            if (exclave->body->GetTy().IsCorrect()) {
                // An exclave block yields a local value. Its contribution to the inferred
                // return type carries the body's modal as-is: when the body's last expression
                // is a literal in a func-arg lambda's implicit-return position, that modal is
                // IDEAL (pending) and will be unified with the expected contextual modal during
                // generic type argument inference (then default to ~local if unresolved);
                // otherwise the body keeps its concrete modal.
                retTys.emplace(exclave->body->GetTy());
            }
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    CJC_NULLPTR_CHECK(fb.retType);
    // Only calculate return type with the function body ty when the given return type is not exist or is not unit.
    bool returnUnit = fb.retType->GetTy().IsCorrect() && fb.retType->GetTy()->IsUnit();
    if (returnUnit) {
        // It no return expression existed, return the user given unit type.
        if (retTys.empty()) {
            return fb.retType->GetTy();
        }
    } else {
        retTys.emplace(bodyTy);
    }
    if (bodyTy.IsCorrect()) {
        auto joinAndMeet = JoinAndMeet(typeManager, retTys, {}, &importManager, fb.curFile);
        auto joinRes = joinAndMeet.JoinAsVisibleTy();
        auto [optErrs, joinedRet] = JoinAndMeet::SetJoinedType(retTy, joinRes);
        retTy = joinedRet;
        if (optErrs) {
            auto builder = diag.Diagnose(*lastNode, DiagKind::sema_incompatible_func_body_and_return_type);
            builder.AddNote(*optErrs);
        }
        // A function's inferred return type is a concrete type, not a func-arg lambda whose modal
        // may stay pending for generic inference. Resolve any IDEAL (pending literal modal) here
        // so it never reaches the back end; per spec an unresolved modal defaults to ~local (NOT).
        // Only for named functions (funcDecl != nullptr): lambda bodies keep IDEAL pending for the
        // enclosing generic call's type argument inference.
        if (fb.funcDecl != nullptr) {
            retTy = typeManager.ReplaceIdealTy(retTy);
        }
        return retTy;
    } else {
        return {TypeManager::GetInvalidTy()};
    }
}
} // namespace Cangjie
