// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "Diags.h"
#include "JoinAndMeet.h"
#include "NodeContext.h"
#include "ScopeManager.h"
#include "TypeCheckUtil.h"
#include "TypeCheckerImpl.h"
#include "cangjie/AST/ASTContext.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/ScopeManagerApi.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Sema/TypeManager.h"

#include <algorithm>
#include <optional>
#include <string_view>
#include <vector>

namespace Cangjie {
using namespace AST;

/// Check 'Copyable' is only used as a struct supertype or a generic upper bound
struct CopyableTyChecker {
    CopyableTyChecker(DiagnosticEngine& diag, TypeManager& tm) : d(diag), type(tm)
    {
    }

    void Check(Package& pkg)
    {
        Walker walker(&pkg, [this](Node* node) -> VisitAction { return Visit(node); });
        walker.Walk();
    }

private:
    VisitAction Visit(Node* node)
    {
        if (node->astKind == ASTKind::PACKAGE || node->astKind == ASTKind::FILE) {
            return VisitAction::WALK_CHILDREN;
        }
        if (node->TestAnyAttr(Attribute::IS_BROKEN, Attribute::HAS_BROKEN)) {
            return VisitAction::SKIP_CHILDREN;
        }
        if (auto id = DynamicCast<InheritableDecl>(node)) {
            CheckInheritableDecl(*id);
        }
        if (auto gc = DynamicCast<GenericConstraint>(node)) {
            CheckGenericConstraint(*gc);
        }
        if (auto var = DynamicCast<VarDecl>(node)) {
            CheckVarDecl(*var);
        }
        if (auto pat = DynamicCast<Pattern>(node)) {
            CheckPattern(*pat);
        }
        if (node->IsFuncLike()) {
            CheckFuncReturnTy(*node);
        }
        if (auto tad = DynamicCast<TypeAliasDecl>(node)) {
            CheckTypeAliasDecl(*tad);
        }
        if (auto ed = DynamicCast<ExtendDecl>(node)) {
            CheckExtendDecl(*ed);
        }
        if (auto re = DynamicCast<NameReferenceExpr>(node)) {
            CheckExprTypeArgs(*re);
        }
        if (auto isExpr = DynamicCast<IsExpr>(node)) {
            CheckTypeNode(isExpr->isType.get());
        }
        if (auto asExpr = DynamicCast<AsExpr>(node)) {
            CheckTypeNode(asExpr->asType.get());
        }
        return VisitAction::WALK_CHILDREN;
    }

    void CheckInheritableDecl(InheritableDecl& id)
    {
        for (auto& super : id.inheritedTypes) {
            if (super && super->GetTy().IsCorrect()) {
                CheckModalTy(super->GetTy(), *super, true);
            }
        }
    }

    void CheckGenericConstraint(GenericConstraint& gc)
    {
        for (auto& upper : gc.upperBounds) {
            if (upper && upper->GetTy().IsCorrect()) {
                CheckModalTy(upper->GetTy(), *upper, true);
            }
        }
    }

    void CheckVarDecl(VarDecl& var)
    {
        if (var.parentPattern != nullptr) {
            return;
        }
        if (var.GetTy().IsCorrect()) {
            CheckModalTy(var.GetTy(), var, false);
        }
    }

    void CheckPattern(Pattern& pat)
    {
        switch (pat.astKind) {
            case ASTKind::TYPE_PATTERN: {
                auto tp = StaticCast<TypePattern>(&pat);
                if (tp->GetTy().IsCorrect()) {
                    CheckModalTy(tp->GetTy(), *tp, false);
                }
                break;
            }
            case ASTKind::EXCEPT_TYPE_PATTERN: {
                auto etp = StaticCast<ExceptTypePattern>(&pat);
                for (auto& tyNode : etp->types) {
                    CheckTypeNode(tyNode.get());
                }
                break;
            }
            case ASTKind::COMMAND_TYPE_PATTERN: {
                auto ctp = StaticCast<CommandTypePattern>(&pat);
                for (auto& tyNode : ctp->types) {
                    CheckTypeNode(tyNode.get());
                }
                break;
            }
            default:
                break;
        }
    }

    void CheckFuncReturnTy(Node& funcLike)
    {
        if (!funcLike.GetTy().IsCorrect()) {
            return;
        }
        auto body = TypeCheckUtil::GetFuncBody(funcLike);
        if (body == nullptr) {
            return;
        }
        if (body->retType && body->retType->GetTy().IsCorrect()) {
            CheckModalTy(body->retType->GetTy(), *body->retType, false);
            return;
        }
        if (auto ft = DynamicCast<FuncTy*>(funcLike.DataTy())) {
            CheckModalTy(ft->retTy, funcLike, false);
        }
    }

    void CheckTypeAliasDecl(TypeAliasDecl& tad)
    {
        if (tad.GetTy().IsCorrect()) {
            CheckModalTy(tad.GetTy(), tad, false);
        }
    }

    void CheckExtendDecl(ExtendDecl& ed)
    {
        CheckTypeNode(ed.extendedType.get());
    }

    void CheckExprTypeArgs(NameReferenceExpr& re)
    {
        if (re.compilerAddedTyArgs) {
            return;
        }
        for (auto& ta : re.typeArguments) {
            CheckTypeNode(ta.get());
        }
    }

    void CheckTypeNode(Type* tyNode)
    {
        if (tyNode && tyNode->GetTy().IsCorrect()) {
            CheckModalTy(tyNode->GetTy(), *tyNode, false);
        }
    }

    void CheckModalTy(ModalTy ty, const Node& at, bool allowRootCopyable)
    {
        if (!ty.IsCorrect()) {
            return;
        }
        CheckDataTy(ty.Ty(), at, allowRootCopyable);
    }

    void CheckDataTy(DataTy ty, const Node& at, bool allowRootCopyable)
    {
        if (!Ty::IsTyCorrect(ty)) {
            return;
        }
        if (type.IsCopyInterfaceTy(ty)) {
            if (!allowRootCopyable) {
                d.DiagnoseRefactor(DiagKindRefactor::sema_illegal_copyable_type_usage, at);
            }
            return;
        }
        if (auto ft = DynamicCast<FuncTy*>(ty.get())) {
            for (auto& paramTy : ft->paramTys) {
                CheckModalTy(paramTy, at, false);
            }
            CheckModalTy(ft->retTy, at, false);
            return;
        }
        for (auto arg : ty->TyArgs()) {
            CheckDataTy(arg, at, false);
        }
    }

    DiagnosticEngine& d;
    TypeManager& type;
};

/// All checks:
/// 1. Check return expression of a function body cannot have internal @local! type
/// 2. Check global var cannot have local type
/// 3. Check call expr args cannot be external @local! type if the param is @local! nor Copy
/// 4. Check validity of struct inheriting Copyable
/// 5. Check local of captured variables
/// 6. Check non-struct types cannot inherit Copyable (in PreCheckInvalidInherit)
/// 7. Check assignment/member-assignment
/// 8. Check exclave expr is inside a func-like body whose signature has a non-Copyable or
/// non-data return/parameter/this type; not in param default values or global/static initializer
/// 9. Check exclave expr is not nested inside another exclave or exclave function
/// 10. Check exclave expr is not in static init, finalizer, main, spawn, or try/catch/handle block
/// 11. Check instance&static member var is of data type
/// 12. Check instance member var does not have initializer when there are mixed local type init
/// 13. Check the whole body of local?/local! constructor must be in exclave expr
/// 14. Check finalizer this mode is supermode of all ctor this mode
/// 15. Check type arguments of a type are data type (function type allowed); skip for function type and fun call
/// 16. Check 'Copyable' is only used as a struct supertype or a generic upper bound
struct ModalTypeChecker {
    ModalTypeChecker(DiagnosticEngine& diag, TypeManager& m) : d(diag), type{m}
    {
    }

    void Check(const ASTContext& ctx, Package& pkg)
    {
        CopyableTyChecker copyableTyChecker{d, type};
        copyableTyChecker.Check(pkg);

        Walker walker(&pkg, [this, &ctx](Ptr<Node> node) { return VisitPre(ctx, node); },
            [this](Ptr<Node> node) { return VisitPost(node); });
        walker.Walk();
    }

    /// 1. external local! used in exclave expr is internal
    /// 2. internal local! used in returned expr of inside exclave expr is external
    /// 3. member access of external local! is external
    /// 4. reference to param of `T` local! is external local!
    /// 5. reference to global/static var is external
    /// 6. all other cases are internal
    bool IsExternalLocal(const ASTContext& ctx, const Expr& expr)
    {
        if (IsInExclaveExpr(ctx, expr)) {
            // rule 2
            return IsReturnedExpr(ctx, expr);
        }
        if (auto ref = DynamicCast<RefExpr>(&expr)) {
            if (ref->isThis || ref->isSuper) {
                return true;
            }
            if (auto param = DynamicCast<FuncParam>(ref->GetTarget())) {
                auto body = TypeCheckUtil::GetCurFuncBody(ctx, expr.scopeName);
                if (!body) {
                    return false;
                }
                for (auto& fp : body->paramLists[0]->params) {
                    if (fp.get() == param) {
                        // rule 4
                        return true;
                    }
                }
            }
            if (auto var = DynamicCast<VarDecl>(ref->GetTarget())) {
                if (IsGlobalOrStaticVar(*var)) {
                    // rule 5
                    return true;
                }
                if (var->IsMemberDecl()) {
                    // this is always external
                    return true;
                }
            }
        }
        if (auto ma = DynamicCast<MemberAccess>(&expr)) {
            if (auto maTarget = ma->GetTarget(); maTarget && IsGlobalOrStaticVar(*maTarget)) {
                // rule 5
                return true;
            }
            // rule 3
            return IsExternalLocal(ctx, *ma->baseExpr);
        }
        return false;
    }

private:
    VisitAction VisitPre(const ASTContext& ctx, Ptr<Node> node)
    {
        if (node->astKind == ASTKind::PACKAGE || node->astKind == ASTKind::FILE) {
            return VisitAction::WALK_CHILDREN;
        }
        if (node->TestAnyAttr(Attribute::IS_BROKEN, Attribute::HAS_BROKEN)) {
            return VisitAction::SKIP_CHILDREN;
        }
        if (auto decl = DynamicCast<StructDecl>(node)) {
            if (decl->IsCopyType()) {
                CheckCopyType(*decl);
            }
            CheckMemberVarModality(*decl);
            if (HasMixedLocalTypeInit(*decl)) {
                CheckMemberVarInitalizer(*decl);
            }
        }
        if (auto decl = DynamicCast<ClassDecl>(node)) {
            if (HasMixedLocalTypeInit(*decl)) {
                CheckMemberVarInitalizer(*decl);
            }
            CheckMemberVarModality(*decl);
        }
        if (auto var = DynamicCast<VarDecl>(node)) {
            if (!var->GetTy().IsCorrect()) {
                return VisitAction::SKIP_CHILDREN;
            }
            CheckGlobalVarModalType(*var);
        }
        if (auto call = DynamicCast<CallExpr>(node)) {
            if (!call->GetTy().IsCorrect()) {
                return VisitAction::SKIP_CHILDREN;
            }
            CheckCallExpr(ctx, *call);
        }
        if (auto assign = DynamicCast<AssignExpr>(node)) {
            CheckAssignExpr(ctx, *assign);
        }
        if (node->IsFuncLike()) {
            if (!node->GetTy().IsCorrect()) {
                return VisitAction::SKIP_CHILDREN;
            }
            CheckReturnModalType(ctx, *node);
        }
        if (auto te = DynamicCast<TryExpr>(node)) {
            WalkTryExpr(ctx, *te);
            return VisitAction::SKIP_CHILDREN;
        }
        if (auto lambda = DynamicCast<LambdaExpr>(node)) {
            PushForbiddenFrame();
            CheckNeedsRegion(*lambda);
        }
        if (auto func = DynamicCast<FuncDecl>(node)) {
            PushForbiddenFrame();
            CheckNeedsRegion(*func);
            CheckWholeBodyIsExclave(*func);
            if (func->IsFinalizer()) {
                CheckFinalizerThisParam(*func);
            }
            // Default-value exprs cannot capture a `@local!` variable of non copy type: other
            // params, in-scope locals, or implicit-`this` members when `this` is `@local!`
            // non copy type.
            if (func->funcBody && !func->funcBody->paramLists.empty()) {
                for (auto& fp : func->funcBody->paramLists[0]->params) {
                    if (fp && fp->assignment) {
                        CheckDefaultValueRefersToNonCopyLocalParam(ctx, *func, *fp->assignment);
                    }
                }
            }
        }
        if (Is<SpawnExpr>(node)) {
            PushForbiddenContext("spawn expr");
        }
        if (auto exclave = DynamicCast<ExclaveExpr>(node)) {
            CheckNestedExclave(ctx, *exclave);
            CheckExclaveInForbiddenContext(ctx, *exclave);
            CheckExclaveInInvalidFunSig(ctx, *exclave);
        }
        if (auto ref = DynamicCast<RefExpr>(node)) {
            CheckLocalVarRefOutsideExclave(ctx, *ref);
        }
        return VisitAction::WALK_CHILDREN;
    }

    /// A local variable of non-copy @local! or @local? type declared outside
    /// an exclave cannot be referred to from inside that exclave.
    void CheckLocalVarRefOutsideExclave(const ASTContext& ctx, const RefExpr& ref)
    {
        auto var = DynamicCast<VarDecl>(ref.GetTarget());
        if (!var || var->astKind == ASTKind::PROP_DECL) {
            return;
        }
        if (Is<FuncParam>(var) || IsGlobalOrStaticVar(*var)) {
            return;
        }
        auto varTy = var->GetTy();
        if (!varTy.IsCorrect() || type.ImplementsCopyInterface(varTy.Ty())) {
            return;
        }
        auto localMode = varTy.Mode().local;
        if (localMode != Mode::FULL && localMode != Mode::HALF) {
            return;
        }
        // this check is valid because exclave cannot be nested
        if (!IsInExclaveExpr(ctx, ref) || IsInExclaveExpr(ctx, *var)) {
            return;
        }
        auto builder = d.DiagnoseRefactor(
            DiagKindRefactor::sema_cannot_access_local_var_outside_exclave, ref,
            std::string{var->identifier.Val()}, varTy.String());
        builder.AddHint(MakeRange(var->begin, var->end));
    }

    VisitAction VisitPost(Ptr<Node> node)
    {
        if (Is<SpawnExpr>(node)) {
            PopForbiddenContext();
        }
        if (Is<LambdaExpr>(node) || Is<FuncDecl>(node)) {
            PopForbiddenFrame();
        }
        return VisitAction::WALK_CHILDREN;
    }

    void PushForbiddenFrame()
    {
        forbiddenContext.push_back(std::nullopt);
    }

    // Pop until the matching func-like frame (discard any unclosed spawn/try contexts).
    void PopForbiddenFrame()
    {
        while (!forbiddenContext.empty()) {
            bool isFrame = !forbiddenContext.back().has_value();
            forbiddenContext.pop_back();
            if (isFrame) {
                break;
            }
        }
    }

    void PushForbiddenContext(std::string_view ctxName)
    {
        forbiddenContext.push_back(ctxName);
    }

    void PopForbiddenContext()
    {
        if (!forbiddenContext.empty() && forbiddenContext.back().has_value()) {
            forbiddenContext.pop_back();
        }
    }

    std::optional<std::string_view> GetCurrentFrameForbiddenContext() const
    {
        for (auto it = forbiddenContext.rbegin(); it != forbiddenContext.rend(); ++it) {
            if (!it->has_value()) {
                return std::nullopt;
            }
            return *it;
        }
        return std::nullopt;
    }

    void WalkSubtree(const ASTContext& ctx, Ptr<Node> node)
    {
        if (!node) {
            return;
        }
        Walker walker(node, [this, &ctx](Ptr<Node> n) { return VisitPre(ctx, n); },
            [this](Ptr<Node> n) { return VisitPost(n); });
        walker.Walk();
    }

    void WalkInForbiddenContext(const ASTContext& ctx, std::string_view ctxName, Ptr<Node> node)
    {
        if (!node) {
            return;
        }
        PushForbiddenContext(ctxName);
        WalkSubtree(ctx, node);
        PopForbiddenContext();
    }

    void WalkTryExpr(const ASTContext& ctx, const TryExpr& te)
    {
        for (auto& resource : te.resourceSpec) {
            WalkSubtree(ctx, resource.get());
        }
        WalkInForbiddenContext(ctx, "try block", te.tryBlock.get());
        for (auto& catchPattern : te.catchPatterns) {
            WalkSubtree(ctx, catchPattern.get());
        }
        for (auto& catchBlock : te.catchBlocks) {
            WalkInForbiddenContext(ctx, "catch block", catchBlock.get());
        }
        if (!te.desugarExpr) {
            for (const auto& handler : te.handlers) {
                PushForbiddenContext("handle block");
                WalkSubtree(ctx, handler.commandPattern.get());
                WalkSubtree(ctx, handler.block.get());
                WalkSubtree(ctx, handler.desugaredLambda.get());
                PopForbiddenContext();
            }
        }
        WalkSubtree(ctx, te.finallyBlock.get());
        WalkSubtree(ctx, te.tryLambda.get());
        WalkSubtree(ctx, te.finallyLambda.get());
    }

    /// Functions that return internal @local! must return so via exclave. External @local! or Copy is ok.
    void CheckReturnModalType(const ASTContext& ctx, Node& func)
    {
        auto body = TypeCheckUtil::GetFuncBody(func);
        if (!body || !body->retType || !body->retType->GetTy().IsCorrect()) {
            return;
        }
        if (body->retType->GetTy().IsDataType()) {
            return;
        }
        auto r = CollectReturnedExpr(*body);
        for (auto& e : r) {
            if (!IsExternalLocal(ctx, *e) && !type.ImplementsCopyInterface(e->DataTy()) && !IsInExclaveExpr(ctx, *e) &&
                e->TyMode().local == Mode::FULL) {
                DiagBadInternalLocalReturn(*e);
            }
        }
    }

    void CheckNestedExclave(const ASTContext& ctx, const ExclaveExpr& expr)
    {
        auto outFun = ScopeManager::GetCurSymbolByKind(SymbolKind::FUNC_LIKE, ctx, expr.scopeName);
        if (!outFun) {
            // skip exclave outside fun
            return;
        }
        if (auto outerExclave = ScopeManager::GetCurSatisfiedSymbolUntilTopLevel(ctx, expr.scopeName,
            [e = &expr](Symbol& sym) { return sym.node != e && sym.node->astKind == ASTKind::EXCLAVE_EXPR; })) {
            auto outerOutFun = ScopeManager::GetCurSymbolByKind(SymbolKind::FUNC_LIKE, ctx, outerExclave->scopeName);
            if (outerOutFun && outerOutFun->node == outFun->node) {
                DiagNestedExclave(expr, *outerExclave->node);
            }
        } else if (auto fd = DynamicCast<FuncDecl>(outFun->node)) {
            if (TypeCheckUtil::HasModifier(fd->modifiers, TokenKind::EXCLAVE)) {
                DiagNestedExclave(expr, *fd);
            }
        }
    }

    void DiagNestedExclave(const ExclaveExpr& expr, const Node& outerNode)
    {
        auto db = d.DiagnoseRefactor(DiagKindRefactor::sema_nested_exclave, expr);
        db.AddHint(MakeRange(outerNode.begin, outerNode.end));
    }

    /// Default-value exprs cannot capture a `@local!` variable of non copy type: other params,
    /// in-scope locals, or implicit-`this` members when `this` is `@local!` non copy type.
    void CheckDefaultValueRefersToNonCopyLocalParam(
        const ASTContext& ctx, const FuncDecl& ownerFunc, Node& assignment)
    {
        if (!ownerFunc.funcBody || ownerFunc.funcBody->paramLists.empty()) {
            return;
        }
        std::vector<FuncParam*> ownerParams;
        for (auto& p : ownerFunc.funcBody->paramLists[0]->params) {
            if (p) {
                ownerParams.push_back(p.get());
            }
        }
        Walker walker(&assignment, [this, &ctx, &ownerParams](Ptr<Node> node) -> VisitAction {
            auto ref = DynamicCast<RefExpr>(node);
            if (!ref) {
                return VisitAction::WALK_CHILDREN;
            }
            // FuncParam ref: flag if it is `@local!` non copy type.
            if (auto param = DynamicCast<FuncParam>(ref->GetTarget())) {
                auto paramTy = param->GetTy();
                if (std::find(ownerParams.begin(), ownerParams.end(), param) != ownerParams.end() &&
                    IsNonCopyLocalTy(paramTy) && paramTy.Mode().local == Mode::FULL) {
                    DiagBadCaptureInDefaultParam(*ref, std::string{param->identifier.Val()});
                }
                return VisitAction::WALK_CHILDREN;
            }
            // VarDecl ref: local var or implicit-`this` member access.
            if (auto var = DynamicCast<VarDecl>(ref->GetTarget())) {
                if (var->TestAnyAttr(Attribute::GLOBAL, Attribute::STATIC)) {
                    return VisitAction::WALK_CHILDREN;
                }
                // Implicit-`this` member: flag if `this` is `@local!` non copy type.
                if (var->IsMemberDecl() && IsInNonCopyFullLocalThis(ctx, *ref)) {
                    DiagBadCaptureInDefaultParam(*ref, std::string{var->identifier.Val()});
                    return VisitAction::WALK_CHILDREN;
                }
                // Local var: flag if it is `@local!` non copy type.
                auto varTy = var->GetTy();
                if (IsNonCopyLocalTy(varTy) && varTy.Mode().local == Mode::FULL) {
                    DiagBadCaptureInDefaultParam(*ref, std::string{var->identifier.Val()});
                }
                return VisitAction::WALK_CHILDREN;
            }
            return VisitAction::WALK_CHILDREN;
        });
        walker.Walk();
    }

    /// True if the enclosing function (looked up from `ref`) is a non-static member function whose
    /// `this` is `@local!` non copy type.
    bool IsInNonCopyFullLocalThis(const ASTContext& ctx, const Expr& ref)
    {
        auto fn = ScopeManager::GetCurSatisfiedSymbolUntilTopLevel(
            ctx, ref.scopeName, [](const Symbol& sym) {
                if (auto func = DynamicCast<FuncDecl>(sym.node)) {
                    return Is<InheritableDecl>(func->outerDecl);
                }
                return false;
            });
        if (!fn) {
            return false;
        }
        auto fd = StaticCast<FuncDecl>(fn->node);
        if (!type.HasThisParam(*fd)) {
            return false;
        }
        auto thisTy = type.GetThisParamTy(*fd);
        return IsNonCopyLocalTy(thisTy) && thisTy.Mode().local == Mode::FULL;
    }

    /// Emit `sema_bad_capture_local` for a `@local!` non copy type var referenced from a default-value expr.
    void DiagBadCaptureInDefaultParam(const Expr& ref, std::string name)
    {
        d.DiagnoseRefactor(DiagKindRefactor::sema_bad_capture_local, ref, "@local!",
            std::move(name), "default", "parameter");
    }

    /// Exclave must be in a func that has a
    /// non-copy @local (or equivalent) parameter, return, or this type.
    void CheckExclaveInInvalidFunSig(const ASTContext& ctx, const ExclaveExpr& expr)
    {
        std::string scopeName = ScopeManagerApi::GetScopeNameWithoutTail(expr.scopeName);
        auto refFuncBody = TypeCheckUtil::GetCurFuncBody(ctx, scopeName);
        if (!refFuncBody || !IsExclaveInFuncBodyStatement(*refFuncBody, expr)) {
            DiagExclaveOutsideFunc(expr);
            return;
        }
        auto funcSym = ScopeManager::GetCurSymbolByKind(SymbolKind::FUNC_LIKE, ctx, scopeName);
        if (!funcSym || !funcSym->node || !FuncLikeSignatureAllowsExclave(*funcSym->node)) {
            DiagExclaveInvalidFuncSignature(expr);
        }
    }

    bool IsExclaveInFuncBodyStatement(const FuncBody& fb, const ExclaveExpr& ee)
    {
        if (!fb.body) {
            return false;
        }
        bool found = false;
        Walker walker(fb.body.get(), [&ee, &found](Node* node) -> VisitAction {
            if (node == &ee) {
                found = true;
                return VisitAction::STOP_NOW;
            }
            return VisitAction::WALK_CHILDREN;
        });
        walker.Walk();
        return found;
    }

    bool FuncLikeSignatureAllowsExclave(const Node& funcLike)
    {
        if (!funcLike.GetTy().IsCorrect()) {
            return false;
        }
        auto funcTy = DynamicCast<FuncTy>(funcLike.DataTy());
        if (!funcTy) {
            return false;
        }
        if (IsNonCopyLocalTy(funcTy->retTy)) {
            return true;
        }
        // Params and `this`: only non-copy @local! enables exclave.
        auto isNonCopyLocalFull = [this](ModalTy ty) {
            return !type.ImplementsCopyInterface(ty.Ty()) && ty.Mode().local == Mode::FULL;
        };
        for (auto& paramTy : funcTy->paramTys) {
            if (isNonCopyLocalFull(paramTy)) {
                return true;
            }
        }
        if (auto func = DynamicCast<FuncDecl>(&funcLike); func && type.HasThisParam(*func)) {
            if (isNonCopyLocalFull(type.GetThisParamTy(*func))) {
                return true;
            }
        }
        return false;
    }

    void DiagExclaveOutsideFunc(const ExclaveExpr& expr)
    {
        d.DiagnoseRefactor(DiagKindRefactor::sema_exclave_outside_function, expr);
    }

    void DiagExclaveInvalidFuncSignature(const ExclaveExpr& expr)
    {
        d.DiagnoseRefactor(DiagKindRefactor::sema_exclave_invalid_func_signature, expr);
    }

    /// Exclave is not allowed in spawn, try/catch/handle, main, static init, or finalizer.
    /// Uses forbiddenContext for the former; scope lookup for the latter.
    void CheckExclaveInForbiddenContext(const ASTContext& ctx, const ExclaveExpr& expr)
    {
        // if it's already in nested exclave, don't check to avoid 2 err message on once position.
        if (IsInExclaveExpr(ctx, expr)) {
            return;
        }
        if (auto ctxName = GetCurrentFrameForbiddenContext()) {
            DiagExclaveInForbiddenContext(expr, *ctxName);
            return;
        }
        if (auto funcSym = ScopeManager::GetCurSatisfiedSymbolUntilTopLevel(ctx, expr.scopeName, [](Symbol& sym) {
            if (auto func = DynamicCast<FuncDecl>(sym.node)) {
                return func->TestAttr(Attribute::MAIN_ENTRY) || func->IsFinalizer() ||
                    (func->TestAttr(Attribute::CONSTRUCTOR) && func->TestAttr(Attribute::STATIC));
            }
            return false;
        })) {
            DiagExclaveInForbiddenContext(expr, GetForbiddenContextName(*funcSym->node));
        }
    }

    std::string_view GetForbiddenContextName(const Node& node)
    {
        if (auto func = DynamicCast<FuncDecl>(&node)) {
            if (func->IsFinalizer()) {
                return "finalizer";
            } else if (func->TestAttr(Attribute::MAIN_ENTRY)) {
                return "main";
            } else if (func->TestAttr(Attribute::STATIC)) {
                return "static init";
            }
        }
        CJC_ABORT();
        return "";
    }

    void DiagExclaveInForbiddenContext(const ExclaveExpr& expr, std::string_view contextName)
    {
        d.DiagnoseRefactor(DiagKindRefactor::sema_exclave_in_main, expr, std::string{contextName});
    }

    bool IsNonCopyLocalTy(ModalTy targetTy)
    {
        if (!Ty::IsTyCorrect(targetTy)) {
            return false;
        }
        return !type.ImplementsCopyInterface(targetTy.Ty()) && targetTy.IsLocalType();
    }

    /// Check the whole body of local?/local! constructor must be in exclave expr.
    void CheckWholeBodyIsExclave(const FuncDecl& init)
    {
        if (!init.TestAttr(Attribute::CONSTRUCTOR) ||
            init.TestAnyAttr(Attribute::STATIC, Attribute::IS_BROKEN, Attribute::HAS_BROKEN)) {
            return;
        }
        if (TypeCheckUtil::HasModifier(init.modifiers, TokenKind::EXCLAVE)) {
            return;
        }
        auto& body = init.funcBody->body;
        auto initTy = type.GetThisParamTy(init);
        if (!body || !Ty::IsTyCorrect(initTy) || initTy.IsDataType()) {
            return;
        }
        if (body->body.empty()) {
            DiagInitModalMustBeInExclave(*body, initTy);
        } else {
            if (body->body[0]->astKind != ASTKind::EXCLAVE_EXPR) {
                DiagInitModalMustBeInExclave(*body->body[0], initTy);
            }
        }
    }

    /// When the init has expr, use the first expr pos; otherwise use the body pos.
    void DiagInitModalMustBeInExclave(const Node& pos, ModalTy ty)
    {
        d.DiagnoseRefactor(DiagKindRefactor::sema_init_modal_must_be_in_exclave, pos, ty.Mode().ToString());
    }

    /// The following functions need mark needsRegion:
    /// The function has in its body a func call that returns a non copy non @~local type (including constructor call
    /// and enum constructor call)
    /// specifically when the whole function body is an exclave, don't generate region; otherwise the new region will
    /// be immediately closed, so this is an optimization.
    void CheckNeedsRegion(const Node& body, bool& needsRegion)
    {
        if (!body.GetTy().IsCorrect()) {
            return;
        }
        ConstWalker w{&body, [this, &needsRegion](Ptr<const Node> node) {
            if (Is<ExclaveExpr>(node)) {
                return VisitAction::SKIP_CHILDREN;
            }
            if (auto ref = DynamicCast<NameReferenceExpr>(node)) {
                if (auto target = ref->GetTarget();
                    target && target->TestAttr(Attribute::ENUM_CONSTRUCTOR) && IsNonCopyLocalTy(ref->GetTy())) {
                    needsRegion = true;
                    return VisitAction::SKIP_CHILDREN;
                }
            }
            if (auto call = DynamicCast<CallExpr>(node)) {
                if (!call->baseFunc || !call->GetTy().IsCorrect()) {
                    return VisitAction::SKIP_CHILDREN;
                }
                if (call->GetTy()->IsNothing()) {
                    // nothing call itself does not require a region, but exprs before the call may
                    // require a region
                    return VisitAction::WALK_CHILDREN;
                }
                ModalTy targetTy{};
                if (call->callKind == CallKind::CALL_FUNCTION_PTR) {
                    // fp call, no target
                    if (auto funcTy = DynamicCast<FuncTy*>(call->baseFunc->DataTy())) {
                        targetTy = funcTy->retTy;
                    } else {
                        targetTy = call->baseFunc->GetTy();
                    }
                    // could be a generic ty with funcTy upperbound
                } else if (call->callKind == CallKind::CALL_OBJECT_CREATION ||
                    call->callKind == CallKind::CALL_STRUCT_CREATION) {
                    targetTy = call->GetTy(); // do not use retTy because only targetTy has the correct
                        // modal type, retTy is of data type
                    if (auto funcTy = DynamicCast<FuncTy*>(targetTy.Ty())) {
                        targetTy = funcTy->retTy;
                    }
                } else {
                    targetTy = call->GetTy();
                }
                CJC_NULLPTR_CHECK(targetTy);
                // non copy non @~local type, needs a region
                if (IsNonCopyLocalTy(targetTy)) {
                    needsRegion = true;
                    return VisitAction::STOP_NOW;
                }
            }
            if (Is<FuncDecl>(node) || Is<LambdaExpr>(node)) {
                // skip nested func
                return VisitAction::SKIP_CHILDREN;
            }
            return VisitAction::WALK_CHILDREN;
        }};
        w.Walk();
    }

    void CheckNeedsRegion(LambdaExpr& lambda)
    {
        CheckNeedsRegion(*lambda.funcBody, lambda.needsRegion);
    }

    void CheckNeedsRegion(FuncDecl& func)
    {
        if (TypeCheckUtil::HasModifier(func.modifiers, TokenKind::EXCLAVE)) {
            func.needsRegion = false;
            return;
        }
        // constructor uses its caller's region implicitly
        if (func.TestAttr(Attribute::CONSTRUCTOR)) {
            if (auto& thisParam = func.funcBody->paramLists[0]->thisParam) {
                if (thisParam->modal.ToModalInfo() != Mode::NOT) {
                    func.needsRegion = false;
                    return;
                }
            }
            return;
        }
        if (!func.funcBody->body) {
            return;
        }
        CheckNeedsRegion(*func.funcBody, func.needsRegion);
    }

    /// Local of lvalue: for RefExpr the referenced var's local, or (implicit this) the ref target's receiver (this)
    /// type; for MemberAccess the referenced object's local (base expr type).
    Mode GetLvalueLocal(const ASTContext& ctx, const Expr* left)
    {
        if (auto ref = DynamicCast<RefExpr>(left)) {
            auto target = ref->GetTarget();
            if (auto vd = DynamicCast<VarDecl>(target);
                vd && TypeCheckUtil::HasModifier(vd->modifiers, TokenKind::DEMODE)) {
                return Mode::NOT;
            }
            if (target && target->IsMemberDecl()) {
                // Implicit this access: use receiver (this param) type, same as GetThisArgTy.
                if (auto fn = ScopeManager::GetCurSatisfiedSymbolUntilTopLevel(
                    ctx, ref->scopeName, [](const Symbol& sym) {
                    if (auto func = DynamicCast<FuncDecl>(sym.node)) {
                        return Is<InheritableDecl>(func->outerDecl);
                    }
                    return false;
                })) {
                    auto fd = StaticCast<FuncDecl>(fn->node);
                    if (fd->TestAttr(Attribute::STATIC)) {
                        return target->TyMode().local;
                    }
                    ModalTy thisTy = type.GetThisParamTy(*fd);
                    if (thisTy && Ty::IsTyCorrect(thisTy)) {
                        return thisTy.IsLocalType() ? thisTy.Mode().local : Mode::NOT;
                    }
                }
            }
            return left->GetTy().IsLocalType() ? left->TyMode().local : Mode::NOT;
        }
        if (auto ma = DynamicCast<MemberAccess>(left)) {
            if (auto vd = DynamicCast<VarDecl>(ma->GetTarget());
                vd && TypeCheckUtil::HasModifier(vd->modifiers, TokenKind::DEMODE)) {
                return Mode::NOT;
            }
            return ma->baseExpr->GetTy().IsLocalType() ? ma->baseExpr->TyMode().local : Mode::NOT;
        }
        // VArray member access
        if (auto sub = DynamicCast<SubscriptExpr>(left)) {
            return sub->baseExpr->TyMode().local;
        }
        CJC_ABORT();
        return {};
    }

    /// Whether the assignment is inside an @local? constructor or any finalizer.
    bool IsInLocalQuestCtorOrAnyFinalizer(const ASTContext& ctx, const std::string& scopeName)
    {
        auto sym = ScopeManager::GetCurSatisfiedSymbolUntilTopLevel(ctx, scopeName, [](Symbol& s) {
            if (auto func = DynamicCast<FuncDecl>(s.node)) {
                return func->TestAttr(Attribute::CONSTRUCTOR) || func->IsFinalizer();
            }
            return false;
        });
        if (!sym) {
            return false;
        }
        auto func = DynamicCast<FuncDecl>(sym->node);
        if (!func || func->IsFinalizer()) {
            return true;
        }
        if (!func->funcBody->paramLists.empty()) {
            if (auto& thisParam = func->funcBody->paramLists[0]->thisParam) {
                return thisParam->TyMode().local == Mode::HALF;
            }
        }
        return false;
    }

    // Check bad member assignment
    void CheckAssignExprRef(
        const ASTContext& ctx, const AssignExpr& assign, const Expr* left, Mode lLocal, Mode rLocal)
    {
        // When llocal is @~local, rlocal must be @~local
        if (lLocal == Mode::NOT) {
            if (rLocal != Mode::NOT) {
                DiagBadAssignment(assign, {}, assign.rightExpr->GetTy());
            }
            return;
        }
        // @local?: only member assignment to `this` in @local? ctor or finalizer is allowed.
        if (lLocal == Mode::HALF) {
            auto ref = StaticCast<RefExpr>(left);
            auto target = ref->GetTarget();
            if (!target || !target->IsMemberDecl()) {
                return; // whole var assignment: rule does not apply
            }
            if (IsInLocalQuestCtorOrAnyFinalizer(ctx, assign.scopeName)) {
                return;
            }
            DiagBadAssigmentToLocalHalf(assign);
            return;
        }
        if (IsCapture(ctx, *left)) {
            if (lLocal != Mode::FULL && rLocal == Mode::NOT) {
                return;
            }
            DiagBadAssignment(assign, {lLocal}, assign.rightExpr->GetTy());
            return;
        }
        // @local!: both @local!, external vs internal
        if (rLocal == Mode::FULL) {
            auto lext = IsExternalLocal(ctx, *left);
            auto rext = IsExternalLocal(ctx, *assign.rightExpr.get());
            if (lext != rext) {
                DiagBadAssignment(assign, {Mode::FULL}, assign.rightExpr->GetTy(), lext, rext);
            }
        } else {
            DiagBadAssignment(assign, {lLocal}, assign.rightExpr->GetTy());
        }
    }

    void CheckAssignExprMa(
        const ASTContext& ctx, const AssignExpr& assign, const Expr* left, Mode lLocal, Mode rLocal)
    {
        auto ma = StaticCast<MemberAccess>(left);
        // When llocal is @~local, rlocal must be @~local
        if (lLocal == Mode::NOT) {
            if (rLocal != Mode::NOT) {
                DiagBadAssignment(assign, {}, assign.rightExpr->GetTy());
            }
            return;
        }
        // @local? member assignment is allowed only in @local? ctor or finalizer and the ref is this
        if (lLocal == Mode::HALF) {
            if (IsThisOrSuper(*ma->baseExpr) && IsInLocalQuestCtorOrAnyFinalizer(ctx, assign.scopeName)) {
                return;
            }
            DiagBadAssigmentToLocalHalf(assign);
            return;
        }
        if (rLocal == Mode::FULL) {
            auto lext = IsExternalLocal(ctx, *left);
            auto rext = IsExternalLocal(ctx, *assign.rightExpr);
            if (lext != rext) {
                DiagBadAssignment(assign, {Mode::FULL}, assign.rightExpr->GetTy(), lext, rext);
            }
        }
    }

    void CheckAssignExpr(const ASTContext& ctx, const AssignExpr& assign)
    {
        if (assign.desugarExpr) {
            return;
        }
        auto right = assign.rightExpr.get();
        if (!right->GetTy().IsCorrect()) {
            return;
        }
        if (type.ImplementsCopyInterface(right->DataTy())) {
            return;
        }
        auto rLocal = right->TyMode().local != Mode::NOT ? right->TyMode().local : Mode::NOT;
        auto left = assign.leftValue.get();
        if (!left->GetTy().IsCorrect() || Is<WildcardExpr>(left)) {
            return;
        }
        auto lLocal = GetLvalueLocal(ctx, left);
        if (DynamicCast<RefExpr>(left)) {
            CheckAssignExprRef(ctx, assign, left, lLocal, rLocal);
        } else if (DynamicCast<MemberAccess>(left)) {
            CheckAssignExprMa(ctx, assign, left, lLocal, rLocal);
        }
    }

    bool IsCapture(const ASTContext& ctx, const Expr& expr)
    {
        auto ref = DynamicCast<RefExpr>(&expr);
        if (!ref) {
            return false;
        }
        auto target = ref->GetTarget();
        if (target->TestAnyAttr(Attribute::GLOBAL, Attribute::STATIC)) {
            return false;
        }
        auto defSite = ScopeManager::GetCurSymbolByKind(SymbolKind::FUNC_LIKE, ctx, target->scopeName);
        auto useSite = ScopeManager::GetCurSymbolByKind(SymbolKind::FUNC_LIKE, ctx, expr.scopeName);
        if (!defSite || !useSite) {
            return false;
        }
        return defSite->node != useSite->node;
    }

    DiagnosticBuilder DiagBadAssignment(const Node& position, const ModalInfo& lModal, ModalTy rTy)
    {
        std::string arg1 = lModal.local != Mode::NOT ? " " + lModal.LocalString() : "";
        std::string arg2 = rTy.String();
        return d.DiagnoseRefactor(DiagKindRefactor::sema_bad_local_assignment, position, arg1, arg2);
    }

    DiagnosticBuilder DiagBadAssignment(
        const Node& position, const ModalInfo& lModal, ModalTy rTy, bool lExternal, bool rExternal)
    {
        std::string arg1 = (lExternal ? " external " : " internal ") + lModal.LocalString();
        std::string arg2 = (rExternal ? " external " : " internal ") + rTy.String();
        return d.DiagnoseRefactor(DiagKindRefactor::sema_bad_local_assignment, position, arg1, arg2);
    }

    void DiagBadAssigmentToLocalHalf(const Node& position)
    {
        d.DiagnoseRefactor(DiagKindRefactor::sema_bad_local_half_ma, position);
    }

    ModalInfo GetCommonInitThisMode(const InheritableDecl& decl)
    {
        std::set<ModalTy> initModalTys;
        for (auto member : decl.GetMemberDeclPtrs()) {
            if (member->TestAttr(Attribute::CONSTRUCTOR) && !member->TestAttr(Attribute::STATIC) &&
                !Is<PrimaryCtorDecl>(member)) {
                initModalTys.insert(type.GetThisParamTy(*StaticCast<FuncDecl>(member)));
            }
        }
        return JoinAndMeet::JoinMode(type, initModalTys);
    }

    void CheckFinalizerThisParam(const FuncDecl& func)
    {
        auto outerDecl = DynamicCast<InheritableDecl>(func.outerDecl);
        if (!outerDecl) {
            return;
        }
        auto commonMode = GetCommonInitThisMode(*outerDecl);
        auto finalizerMode = type.GetThisParamTy(func).Mode();
        if (commonMode.IsSubModal(finalizerMode)) {
            return;
        }
        d.DiagnoseRefactor(DiagKindRefactor::sema_finalizer_this_mode_mismatch, func, finalizerMode.LocalString(),
            commonMode.LocalString());
    }

    /// Any non-static member var with modal type (local type) is not allowed
    void CheckMemberVarModality(const InheritableDecl& decl)
    {
        for (auto member : decl.GetMemberDeclPtrs()) {
            if (auto var = DynamicCast<VarDecl>(member)) {
                CheckMemberVarModalType(*var);
            }
        }
    }

    /// When type has mixed local type init, the member var of non-copy type cannot have initializer
    void CheckMemberVarInitalizer(const InheritableDecl& decl)
    {
        for (auto member : decl.GetMemberDeclPtrs()) {
            if (auto var = DynamicCast<VarDecl>(member)) {
                if (!var->GetTy().IsCorrect() || var->TestAttr(Attribute::STATIC)) {
                    continue;
                }
                if (var->initializer && (!type.ImplementsCopyInterface(var->DataTy()) &&
                    !TypeCheckUtil::HasModifier(var->modifiers, TokenKind::DEMODE))) {
                    DiagMemberVarInitalizerInMixedInit(*var, decl);
                }
            }
        }
    }

    void DiagMemberVarInitalizerInMixedInit(const VarDecl& var, const InheritableDecl& decl)
    {
        d.DiagnoseRefactor(DiagKindRefactor::sema_member_var_initalizer_in_mixed_init, var, var.identifier.Val(),
            var.GetTy().String(), ASTKIND_TO_STR.at(decl.astKind));
    }

    bool HasMixedLocalTypeInit(const InheritableDecl& decl)
    {
        int lastUsedLocal{-1};
        for (auto member : decl.GetMemberDeclPtrs()) {
            if (member->TestAttr(Attribute::CONSTRUCTOR) && !member->TestAttr(Attribute::STATIC) &&
                !Is<PrimaryCtorDecl>(member)) {
                // primary ctor is already desugared
                auto& init = *StaticCast<FuncDecl>(member)->funcBody->paramLists[0];
                auto thisLocal =
                    static_cast<int>(init.thisParam ? init.thisParam->TyMode().local : Mode::NOT);
                if (lastUsedLocal == -1) {
                    lastUsedLocal = thisLocal;
                } else if (lastUsedLocal != thisLocal) {
                    return true;
                }
            }
        }
        return false;
    }

    void CheckMemberVarModalType(const VarDecl& var)
    {
        if (!var.GetTy().IsCorrect() || var.TestAttr(Attribute::STATIC)) {
            return;
        }
        // PropDecl allows modal on its type (modal overloading is expressed there), skip it.
        if (var.astKind == ASTKind::PROP_DECL) {
            return;
        }
        // member var with modal type is not allowed
        if (var.GetTy().IsLocalType()) {
            DiagMemberVarLocalModalType(var);
        }
    }

    void DiagMemberVarLocalModalType(const VarDecl& var)
    {
        // Use written type's position if available, otherwise use variable location
        if (var.type) {
            DiagExpectedDataType(*var.type, var.GetTy().String());
        } else {
            DiagExpectedDataType(var, var.GetTy().String());
        }
    }

    /// Check global var cannot have local type
    void CheckGlobalVarModalType(const VarDecl& var)
    {
        // PropDecl allows modal on its type; skip the local-type ban.
        if (var.astKind == ASTKind::PROP_DECL) {
            return;
        }
        if (var.TestAnyAttr(Attribute::GLOBAL, Attribute::STATIC)) {
            if (var.GetTy().IsCorrect() && var.GetTy().IsLocalType()) {
                DiagGlobalVarLocalModalType(var);
            }
        }
    }

    static bool IsNonStaticMemberFunction(const FuncDecl& func)
    {
        if (func.ownerFunc || func.TestAnyAttr(Attribute::CONSTRUCTOR, Attribute::ENUM_CONSTRUCTOR)) {
            return false;
        }
        if (func.propDecl) {
            Is<InheritableDecl>(func.outerDecl) && !func.propDecl->TestAttr(Attribute::STATIC);
        }
        return Is<InheritableDecl>(func.outerDecl) && !func.TestAttr(Attribute::STATIC);
    }

    const Expr* GetFuncArg(const CallExpr& call, size_t index)
    {
        if (auto inner = DynamicCast<CallExpr>(call.desugarExpr.get())) {
            return GetFuncArg(*inner, index);
        }
        if (auto array = DynamicCast<ArrayExpr>(call.desugarExpr.get())) {
            return array->args[index]->expr.get();
        }
        auto func = call.resolvedFunction;
        if (func && IsNonStaticMemberFunction(*func)) {
            // non static member function call, the first arg is this
            if (auto ma = DynamicCast<MemberAccess>(&*call.baseFunc)) {
                if (index == 0) {
                    return ma->baseExpr.get();
                }
                if (call.desugarArgs) {
                    return call.desugarArgs->at(index - 1)->expr.get();
                }
                return call.args[index - 1]->expr.get();
            }
            // RefExpr, using implicit this, no need to check
            return nullptr;
        }
        if (call.desugarArgs.has_value()) {
            return call.desugarArgs.value()[index]->expr.get();
        }
        return call.args[index]->expr.get();
    }

    struct FuncParamInfo {
        ModalTy ty;
        ModalInfo modal;
        std::string_view name;
    };

    FuncParamInfo GetFuncParam(const CallExpr& call, size_t index)
    {
        if (auto inner = DynamicCast<CallExpr>(call.desugarExpr.get())) {
            return GetFuncParam(*inner, index);
        }
        if (auto array = DynamicCast<ArrayExpr>(call.desugarExpr.get())) {
            return {array->args[index]->GetTy(), array->args[index]->TyMode(), ""};
        }
        auto func = call.resolvedFunction;
        if (!func) {
            // function pointer call, no need to check 'this' param, no arg name
            return {call.args[index]->GetTy(), call.args[index]->TyMode(), ""};
        }
        if (call.baseFunc->GetTy()->IsPointer()) {
            return {call.args[index]->GetTy(), call.args[index]->TyMode(), ""};
        }
        if (func->funcBody->paramLists[0]->thisParam) {
            if (index == 0) {
                return {func->funcBody->paramLists[0]->thisParam->GetTy(),
                    func->funcBody->paramLists[0]->thisParam->modal, "this"};
            }
            return {func->funcBody->paramLists[0]->params[index - 1]->GetTy(),
                func->funcBody->paramLists[0]->params[index - 1]->TyMode(),
                func->funcBody->paramLists[0]->params[index - 1]->identifier.Val()};
        }
        if (IsNonStaticMemberFunction(*func)) {
            if (index == 0) {
                return {func->outerDecl->GetTy(), TypeCheckUtil::GetThisParamModal(*func), "this"};
            }
            return {func->funcBody->paramLists[0]->params[index - 1]->GetTy(),
                func->funcBody->paramLists[0]->params[index - 1]->TyMode(),
                func->funcBody->paramLists[0]->params[index - 1]->identifier.Val()};
        }
        return {func->funcBody->paramLists[0]->params[index]->GetTy(),
            func->funcBody->paramLists[0]->params[index]->TyMode(),
            func->funcBody->paramLists[0]->params[index]->identifier.Val()};
    }

    size_t GetParamNum(const CallExpr& call)
    {
        if (auto inner = DynamicCast<CallExpr>(call.desugarExpr.get())) {
            return GetParamNum(*inner);
        }
        auto func = call.resolvedFunction;
        if (!func) {
            if (auto array = DynamicCast<ArrayExpr>(call.desugarExpr.get())) {
                return array->args.size();
            }
            if (call.baseFunc->GetTy()->IsPointer()) {
                return call.args.size();
            }
            if (call.baseFunc->GetTy()->kind == TypeKind::TYPE_CSTRING) {
                return 1UL;
            }
            // function pointer call, no need to check 'this' param
            if (auto fty = DynamicCast<FuncTy*>(call.baseFunc->DataTy())) {
                return fty->paramTys.size();
            }
            // invalid
            return -1UL;
        }
        if (IsNonStaticMemberFunction(*func)) {
            return func->funcBody->paramLists[0]->params.size() + 1;
        }
        return func->funcBody->paramLists[0]->params.size();
    }

    std::unordered_map<FuncBody*, std::vector<const Expr*>> returnedExprMap;
    bool IsReturnedExpr(const ASTContext& ctx, const Expr& expr)
    {
        auto body = TypeCheckUtil::GetCurFuncBody(ctx, expr.scopeName);
        if (!body) {
            return false;
        }

        auto cache = returnedExprMap.find(body);
        auto& rets =
            cache != returnedExprMap.end() ? cache->second : (returnedExprMap[body] = CollectReturnedExpr(*body));
        return std::find(rets.begin(), rets.end(), &expr) != rets.end();
    }

    std::vector<const Expr*> CollectReturnedExpr(FuncBody& body)
    {
        if (!body.body || body.body->body.empty()) {
            return {};
        }
        std::vector<const Expr*> ret;
        Walker(&body, [&ret](auto n) {
            if (auto re = DynamicCast<ReturnExpr>(n)) {
                ret.push_back(re->expr.get());
                return VisitAction::SKIP_CHILDREN;
            }
            if (n->IsFuncLike()) {
                return VisitAction::SKIP_CHILDREN;
            }
            return VisitAction::WALK_CHILDREN;
        }).Walk();
        auto lastExpr = DynamicCast<Expr>(body.body->body.back().get());
        if (!lastExpr) { // last node may be a decl
            return ret;
        }
        while (lastExpr && lastExpr->desugarExpr) {
            lastExpr = lastExpr->desugarExpr.get();
        }
        if (lastExpr->astKind == ASTKind::RETURN_EXPR) {
            return ret;
        }

        if (auto exclave = DynamicCast<ExclaveExpr>(lastExpr); exclave && !exclave->body->body.empty()) {
            lastExpr = DynamicCast<Expr>(exclave->body->body.back().get());
            if (!lastExpr) {
                return ret;
            }
            while (lastExpr && lastExpr->desugarExpr) {
                // push last expr of exclave
                lastExpr = lastExpr->desugarExpr.get();
            }
        }
        if (auto funcTy = DynamicCast<FuncTy>(body.DataTy())) {
            if (funcTy->retTy->IsUnit() || (body.funcDecl && body.funcDecl->TestAttr(Attribute::CONSTRUCTOR))) {
                // do not collect last expr if it is return type is Unit
                return ret;
            }
        }
        // or push the last expr
        ret.push_back(lastExpr);
        return ret;
    }

    bool IsInExclaveExpr(const ASTContext& ctx, const Node& node)
    {
        auto sym = ScopeManager::GetCurSatisfiedSymbolUntilTopLevel(
            ctx, node.scopeName, [](Symbol& sym) { return sym.node->astKind == ASTKind::EXCLAVE_EXPR; });
        if (sym && sym->node) {
            return true;
        }
        if (auto fun = ScopeManager::GetCurSymbolByKind(SymbolKind::FUNC_LIKE, ctx, node.scopeName)) {
            if (auto fd = DynamicCast<FuncDecl>(fun->node)) {
                if (std::any_of(fd->modifiers.begin(), fd->modifiers.end(),
                    [](const Modifier& m) { return m.modifier == TokenKind::EXCLAVE; })) {
                    return true;
                }
                // Default-value body of a non copy type not @~local is translated as if wrapped in
                // `exclave {}` (CHIR does the wrapping); Sema exclave-context checks must recognize it.
                if (IsInImplicitExclaveOfDefaultParam(*fd)) {
                    return true;
                }
            }
        }
        return false;
    }

    // Returns the FuncParam whose default-value function is `fd`, or null.
    FuncParam* GetCorrespondingFuncParam(const FuncDecl& fd)
    {
        if (!fd.ownerFunc || !fd.ownerFunc->funcBody || fd.ownerFunc->funcBody->paramLists.empty()) {
            return nullptr;
        }
        for (auto& fp : fd.ownerFunc->funcBody->paramLists[0]->params) {
            if (!fp) {
                continue;
            }
            if (fd.identifier.Val() == fp->identifier.Val() ||
                fd.identifier.Val().rfind(fp->identifier.Val() + ".", 0) == 0) {
                return fp.get();
            }
        }
        return nullptr;
    }

    // True for a desugared default-value func whose param is a non copy type not @~local; CHIR
    // translates its body as if wrapped in `exclave {}`. Sema exclave-context checks treat it so.
    bool IsInImplicitExclaveOfDefaultParam(const FuncDecl& fd)
    {
        if (!fd.ownerFunc || !fd.TestAttr(Attribute::HAS_INITIAL)) {
            return false;
        }
        auto fp = GetCorrespondingFuncParam(fd);
        if (!fp) {
            return false;
        }
        return IsNonCopyLocalTy(fp->GetTy());
    }

    void DiagBadExternalLocalArg(const FuncParamInfo& param, const Expr& arg)
    {
        d.DiagnoseRefactor(DiagKindRefactor::sema_bad_external_local_arg, arg, std::string{param.name});
    }

    void DiagBadInternalLocalReturn(const Expr& expr)
    {
        d.DiagnoseRefactor(DiagKindRefactor::sema_bad_internal_local_return, expr);
    }

    /// Check call expr args cannot be external @local! type if the param is @local! nor Copy
    void CheckCallExpr(const ASTContext& ctx, const CallExpr& call)
    {
        size_t paramNum = GetParamNum(call);
        if (paramNum == -1UL) {
            // invalid call node, skip
            return;
        }
        // ctor is always considered exclave call, so we don't check arg pass
        if (call.callKind == CallKind::CALL_OBJECT_CREATION || call.callKind == CallKind::CALL_STRUCT_CREATION) {
            return;
        }
        for (size_t i = 0; i < paramNum; ++i) {
            auto arg = GetFuncArg(call, i);
            if (!arg) {
                continue;
            }
            auto param = GetFuncParam(call, i);
            if (param.modal.local == Mode::FULL && arg->TyMode().local == Mode::FULL &&
                IsExternalLocal(ctx, *arg) && !type.ImplementsCopyInterface(arg->DataTy())) {
                DiagBadExternalLocalArg(param, *arg);
            }
        }
    }

    void DiagGlobalVarLocalModalType(const VarDecl& var)
    {
        // Use written type's position if available, otherwise use variable location
        if (var.type) {
            DiagExpectedDataType(*var.type, var.GetTy().String());
        } else {
            DiagExpectedDataType(var, var.GetTy().String());
        }
    }

    void DiagExpectedDataType(const Node& node, const std::string& typeStr)
    {
        d.DiagnoseRefactor(DiagKindRefactor::sema_expected_data_type, node, typeStr);
    }

    /// Check field types when struct inherits Copyable
    void CheckCopyType(StructDecl& decl)
    {
        for (auto member : decl.GetMemberDeclPtrs()) {
            if (auto var = DynamicCast<VarDecl>(member)) {
                if (type.ImplementsCopyInterface(var->DataTy())) {
                    continue;
                }
                DiagCopyStructBadField(decl, *var);
            }
        }
    }

    void DiagCopyStructBadField(const StructDecl& decl, const VarDecl& var)
    {
        d.DiagnoseRefactor(DiagKindRefactor::sema_copy_struct_bad_field, var, var.identifier.Val(),
            var.GetTy()->String(), decl.identifier.Val());
    }

    DiagnosticEngine& d;
    TypeManager& type;
    // Stack of forbidden-context markers for sema_exclave_in_main (%s).
    // - nullopt: boundary of a func-like body (FuncDecl / LambdaExpr). Exclave checks only
    //   consider entries above the innermost frame, so outer try/catch/spawn do not apply
    //   inside nested functions or lambdas.
    // - string: human-readable site name, e.g. "spawn expr", "try block", "catch block",
    //   "handle block" (finally is intentionally not pushed).
    std::vector<std::optional<std::string_view>> forbiddenContext;
};

ModalTypeChecker* TypeChecker::TypeCheckerImpl::NewModalTypeChecker()
{
    return new ModalTypeChecker(diag, typeManager);
}

void TypeChecker::TypeCheckerImpl::DeleteModalTypeChecker()
{
    delete modalTypeChecker;
}

bool TypeChecker::TypeCheckerImpl::IsExternalLocal(const ASTContext& ctx, const Expr& expr)
{
    return modalTypeChecker->IsExternalLocal(ctx, expr);
}

void TypeChecker::TypeCheckerImpl::CheckModalType(const ASTContext& ctx, Package& pkg)
{
    modalTypeChecker->Check(ctx, pkg);
}

void TypeChecker::TypeCheckerImpl::ExpectSubtypeOf(
    Ptr<AST::Node> node, AST::ModalTy expect, AST::ModalTy actual, ModalMatchMode modal)
{
    if (!typeManager.IsSubtype(expect, actual, true, true, modal)) {
        Sema::DiagMismatchedTypesWithFoundTy(diag, *node, expect, actual);
        node->SetTy({TypeManager::GetInvalidTy()});
    }
}

bool TypeChecker::TypeCheckerImpl::ChkExclaveExpr(ASTContext& ctx, ModalTy target, ExclaveExpr& expr)
{
    if (!ChkBlock(ctx, target, *expr.body)) {
        expr.SetTy({TypeManager::GetInvalidTy()});
        return false;
    }
    expr.SetTy({TypeManager::GetNothingTy()});
    return true;
}

void TypeChecker::TypeCheckerImpl::DiagExpectedDataType(const AST::Node& node)
{
    diag.DiagnoseRefactor(DiagKindRefactor::sema_expected_data_type, node, node.GetTy().String());
}

ModalTy TypeChecker::TypeCheckerImpl::SynExclaveExpr(ASTContext& ctx, ExclaveExpr& expr)
{
    // Target from enclosing function return type
    auto fb = TypeCheckUtil::GetCurFuncBody(ctx, expr.scopeName);
    if (fb) {
        if (auto funcBodyTy = DynamicCast<FuncTy>(fb->DataTy())) {
            auto target = funcBodyTy->retTy;
            if (!target.Ty() || target->kind == TypeKind::TYPE_QUEST) {
                SynBlock({ctx, SynPos::EXPR_ARG}, *expr.body);
                expr.SetTy(ModalTy{TypeManager::GetNothingTy()});
                return expr.GetTy();
            }
            auto fun = ScopeManager::GetCurSymbolByKind(SymbolKind::FUNC_LIKE, ctx, expr.scopeName);
            if (auto func = fun && fun->node ? DynamicCast<FuncDecl>(fun->node) : nullptr) {
                if (func->TestAttr(Attribute::CONSTRUCTOR) || func->IsFinalizer()) {
                    SynBlock({ctx, SynPos::EXPR_ARG}, *expr.body);
                    expr.SetTy(ModalTy{TypeManager::GetNothingTy()});
                    return expr.GetTy();
                }
            }
            if (!ChkBlock(ctx, target, *expr.body)) {
                expr.SetTy({TypeManager::GetInvalidTy()});
                return expr.GetTy();
            }
            expr.SetTy(ModalTy{TypeManager::GetNothingTy()});
            return expr.GetTy();
        }
    }

    // No target: do not check exclave return type; any type inside body is correct
    SynBlock({ctx, SynPos::EXPR_ARG}, *expr.body);
    expr.SetTy(ModalTy{TypeManager::GetNothingTy()});
    return expr.GetTy();
}
} // namespace Cangjie
