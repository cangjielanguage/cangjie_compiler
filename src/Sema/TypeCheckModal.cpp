// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "JoinAndMeet.h"
#include "ScopeManager.h"
#include "TypeCheckUtil.h"
#include "TypeCheckerImpl.h"
#include "cangjie/AST/ASTContext.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/ScopeManagerApi.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Sema/TypeManager.h"

#include <algorithm>
#include <string_view>
#include <unordered_set>
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
                d.DiagnoseRefactor(DiagKindRefactor::sema_illegal_copy_type_usage, at);
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

/// Which clause of a TryExpr a forbidden-context entry marks (NONE for non-try sites).
enum class ForbiddenTryPart : uint8_t {
    NONE,
    TRY_BLOCK,
    CATCH_BLOCK,
    HANDLE_BLOCK,
};

/// One entry on the forbidden-context stack: an AST site where exclave is banned,
/// plus (for a TryExpr) which clause the walk is inside, so diagnostics can name it.
struct ForbiddenContextEntry {
    Ptr<const Node> node;
    ForbiddenTryPart part = ForbiddenTryPart::NONE;
};

/// All checks:
/// 1. Check return expression of a function body cannot have internal @local! type
/// 2. Check global var cannot have local type
/// 3. Check call expr args cannot be external @local! type if the param is @local! nor Copy
/// 4. Check validity of struct inheriting Copyable
/// 5. Check local of captured variables
/// 6. Check non-struct types cannot inherit Copyable (in PreCheckInvalidInherit);
///    extend of non-struct cannot inherit Copyable (in CheckExtendInterfaces)
/// 7. Check assignment/member-assignment
/// 8. Check exclave expr is inside a func-like body whose signature has a non copy local! or local? return type, or
///    non copy local! param type; not in global/static initializer
/// 9. Check exclave expr is not nested inside another exclave or exclave function
/// 10. Check exclave expr is not in static init, finalizer, main, spawn, or try/catch/handle block
/// 11. Check abstract func cannot be exclave
/// 12. Check instance&static member var is of data type
/// 13. Check instance member var does not have initializer when there are mixed local type init
/// 14. Check the whole body of local?/local! constructor must be in exclave expr
/// 15. Check finalizer this mode is supermode of all ctor this mode
/// 16. Check type arguments of a type are data type (function type allowed); skip for function type and fun call
/// 17. Check 'Copyable' is only used as a struct supertype or a generic upper bound
/// 18. Check spawn does not refer to a non-copy local variable declared outside it
/// 19. Check declaration-time initialization of a local var follows the assignment rule
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
    /// 4. tuple, varray index of external local! is external
    /// 5. reference to param of `T` local! is external local!
    /// 6. reference to global/static var is external
    /// 7. if/match/try expr is external if any of its result branches (finally block excluded) is external
    /// 8. array literal/tuple is external if any of its elements is external
    /// 9. all other cases are internal
    bool IsExternalLocal(const ASTContext& ctx, const Expr& expr)
    {
        if (IsInExclaveExpr(ctx, expr)) {
            // rule 2
            return IsReturnedExpr(ctx, expr);
        }
        if (auto block = DynamicCast<Block>(&expr)) {
            auto last = DynamicCast<Expr>(block->GetLastExprOrDecl());
            return last && IsExternalLocal(ctx, *last);
        }
        if (auto ie = DynamicCast<IfExpr>(&expr)) {
            // rule 8
            return IsExternalLocal(ctx, *ie->thenBody) || (ie->elseBody && IsExternalLocal(ctx, *ie->elseBody));
        }
        if (auto me = DynamicCast<MatchExpr>(&expr)) {
            // rule 8
            for (auto& mc : me->matchCases) {
                if (mc->exprOrDecls && IsExternalLocal(ctx, *mc->exprOrDecls)) {
                    return true;
                }
            }
            for (auto& mco : me->matchCaseOthers) {
                if (mco->exprOrDecls && IsExternalLocal(ctx, *mco->exprOrDecls)) {
                    return true;
                }
            }
            return false;
        }
        if (auto te = DynamicCast<TryExpr>(&expr)) {
            // rule 8, finally block does not contribute to the value of try expr
            if (te->tryBlock && IsExternalLocal(ctx, *te->tryBlock)) {
                return true;
            }
            for (auto& catchBlock : te->catchBlocks) {
                if (catchBlock && IsExternalLocal(ctx, *catchBlock)) {
                    return true;
                }
            }
            for (auto& handler : te->handlers) {
                if (handler.block && IsExternalLocal(ctx, *handler.block)) {
                    return true;
                }
            }
            return false;
        }
        if (auto al = DynamicCast<ArrayLit>(&expr)) {
            // rule 9
            for (auto& child : al->children) {
                if (child && IsExternalLocal(ctx, *child)) {
                    return true;
                }
            }
            return false;
        }
        if (auto tl = DynamicCast<TupleLit>(&expr)) {
            // rule 9
            for (auto& child : tl->children) {
                if (child && IsExternalLocal(ctx, *child)) {
                    return true;
                }
            }
            return false;
        }
        if (expr.desugarExpr) {
            return IsExternalLocal(ctx, *expr.desugarExpr);
        }
        if (auto id = DynamicCast<SubscriptExpr>(&expr)) {
            return IsExternalLocal(ctx, *id->baseExpr);
        }
        if (auto ref = DynamicCast<RefExpr>(&expr)) {
            if (ref->isThis || ref->isSuper) {
                return true;
            }
            if (ref->GetTarget()->TestAttr(Attribute::ENUM_CONSTRUCTOR)) { // enum creation, always local
                return false;
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
            if (auto maTarget = ma->GetTarget()) {
                // rule 5
                if (IsGlobalOrStaticVar(*maTarget)) {
                    return true;
                }
                if (maTarget->TestAttr(Attribute::ENUM_CONSTRUCTOR)) { // enum creation, always local
                    return false;
                }
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
            // Direct `struct S <: Copyable`; extend-of-struct is checked via ExtendDecl below.
            if (InheritsCopyable(*decl)) {
                CheckCopyType(*decl);
            }
            CheckMemberVarModality(*decl);
            if (HasMixedLocalTypeInit(*decl)) {
                CheckMemberVarInitalizer(*decl);
            }
        }
        if (auto ed = DynamicCast<ExtendDecl>(node)) {
            CheckExtendCopyable(*ed);
        }
        if (auto decl = DynamicCast<ClassDecl>(node)) {
            if (HasMixedLocalTypeInit(*decl)) {
                CheckMemberVarInitalizer(*decl);
            }
            CheckMemberVarModality(*decl);
        }
        if (auto var = DynamicCast<VarDeclAbstract>(node)) {
            if (!var->GetTy().IsCorrect()) {
                return VisitAction::SKIP_CHILDREN;
            }
            CheckGlobalVarModalType(*var);
            if (!var->IsStaticOrGlobal()) {
                CheckVarDeclInitializer(ctx, *var);
            }
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
            PushForbiddenContext(lambda);
        }
        if (auto func = DynamicCast<FuncDecl>(node)) {
            PushForbiddenContext(func);
            CheckAbstractExclaveFunc(*func);
            CheckWholeBodyIsExclave(*func);
            if (func->IsFinalizer()) {
                CheckFinalizerThisParam(*func);
            }
            // Default-value exprs cannot capture a `@local!` variable of non copy type
            if (func->funcBody && !func->funcBody->paramLists.empty()) {
                for (auto& fp : func->funcBody->paramLists[0]->params) {
                    if (fp && fp->assignment) {
                        CheckDefaultValueRefersToNonCopyLocalParam(ctx, *func, *fp->assignment);
                    }
                }
            }
            CheckExclaveModifierInInvalidFunSig(*func);
        }
        if (auto spawn = DynamicCast<SpawnExpr>(node)) {
            PushForbiddenContext(spawn);
            CheckLocalVarRefOutsideSpawn(*spawn);
        }
        if (auto exclave = DynamicCast<ExclaveExpr>(node)) {
            CheckNestedExclave(ctx, *exclave);
            if (!CheckExclaveInForbiddenContext(ctx, *exclave)) {
                CheckExclaveInInvalidFunSig(ctx, *exclave);
            }
        }
        if (auto ref = DynamicCast<RefExpr>(node)) {
            CheckLocalVarRefOutsideExclave(ctx, *ref);
        }
        return VisitAction::WALK_CHILDREN;
    }

    /// A non-copy local variable declared outside a spawn cannot be referred to from inside that spawn.
    void CheckLocalVarRefOutsideSpawn(const SpawnExpr& spawn)
    {
        if (!spawn.task) {
            return;
        }
        // Nested spawn is checked by its own visit. Vars declared there are not in this spawn.
        std::unordered_set<Ptr<VarDecl>> declared;
        Walker collect(spawn.task.get(), [&declared](Ptr<Node> node) {
            if (Is<SpawnExpr>(node)) { // skip nested spawn, but didn't skip nested funs
                return VisitAction::SKIP_CHILDREN;
            }
            if (auto var = DynamicCast<VarDecl>(node)) {
                declared.insert(var);
            }
            return VisitAction::WALK_CHILDREN;
        });
        collect.Walk();
        Walker refs(spawn.task.get(), [this, &declared](Ptr<Node> node) {
            if (Is<SpawnExpr>(node)) {
                return VisitAction::SKIP_CHILDREN;
            }
            if (auto ref = DynamicCast<RefExpr>(node)) {
                DiagLocalVarRefOutsideSpawn(*ref, declared);
            }
            return VisitAction::WALK_CHILDREN;
        });
        refs.Walk();
    }

    /// Referee must be a local `VarDecl` (not a param, member, global, `this`, or `super`) of non-copy
    /// local type, and not declared in this spawn.
    void DiagLocalVarRefOutsideSpawn(const RefExpr& ref, const std::unordered_set<Ptr<VarDecl>>& declared)
    {
        auto var = DynamicCast<VarDecl>(ref.GetTarget());
        if (!var || var->astKind != ASTKind::VAR_DECL || IsThisOrSuper(ref) || var->IsMemberDecl() ||
            IsGlobalOrStaticVar(*var)) {
            return;
        }
        auto varTy = var->GetTy();
        // actually @local! reference in spawn is reported sema_bad_capture_local, which is checked in TypeCheck,
        // and when that stage has error this will never be run. Only @local? reference will be reported here.
        if (!IsNonCopyLocalTy(varTy) || declared.find(var) != declared.end()) {
            return;
        }
        auto builder = d.DiagnoseRefactor(DiagKindRefactor::sema_cannot_access_local_var_outside_exclave, ref,
            std::string{var->identifier.Val()}, varTy.String(), std::string{"spawn"});
        builder.AddHint(MakeRange(var->begin, var->end), "variable declared here");
    }

    /// A local variable of non-copy @local! or @local? type declared outside
    /// an exclave cannot be referred to from inside that exclave.
    void CheckLocalVarRefOutsideExclave(const ASTContext& ctx, const RefExpr& ref)
    {
        auto var = DynamicCast<VarDecl>(ref.GetTarget());
        if (!var || var->astKind == ASTKind::PROP_DECL) {
            return;
        }
        if (IsThisOrSuper(ref) || Is<FuncParam>(var) || IsGlobalOrStaticVar(*var)) {
            return;
        }
        auto varTy = var->GetTy();
        // this excludes member var ref as well, because they're considered ref to this, which is a func param
        if (!varTy.IsCorrect() || varTy.IsDataType() || type.IsCopyType(varTy.Ty())) {
            return;
        }
        // this check is valid because exclave cannot be nested
        if (!IsInExclaveExpr(ctx, ref) || IsInExclaveExpr(ctx, *var)) {
            return;
        }
        auto builder = d.DiagnoseRefactor(
            DiagKindRefactor::sema_cannot_access_local_var_outside_exclave, ref,
            std::string{var->identifier.Val()}, varTy.String(), "exclave");
        builder.AddHint(MakeRange(var->begin, var->end), "variable declared here");
    }

    VisitAction VisitPost(Ptr<Node> node)
    {
        // VisitPre may return SKIP_CHILDREN before pushing (broken nodes, ill-typed func-like),
        // while VisitPost still runs; pop only when this node is the frame on top of the stack.
        if ((Is<SpawnExpr>(node) || Is<LambdaExpr>(node) || Is<FuncDecl>(node)) &&
            !forbiddenContext.empty() && forbiddenContext.back().node == node) {
            PopForbiddenContext();
        }
        return VisitAction::WALK_CHILDREN;
    }

    void PushForbiddenContext(Ptr<const Node> node, ForbiddenTryPart part = ForbiddenTryPart::NONE)
    {
        forbiddenContext.push_back({node, part});
    }

    void PopForbiddenContext()
    {
        if (!forbiddenContext.empty()) {
            forbiddenContext.pop_back();
        }
    }

    const ForbiddenContextEntry* GetCurrentFrameForbiddenContext() const
    {
        // Func-like frames stop the search; main/finalizer/static init frames are themselves forbidden.
        // Skip a spawn's task lambda (and only it) so the SpawnExpr below remains visible.
        for (auto it = forbiddenContext.rbegin(); it != forbiddenContext.rend(); ++it) {
            auto node = it->node;
            if (node->IsFuncLike()) {
                auto outer = std::next(it);
                if (outer != forbiddenContext.rend() && Is<SpawnExpr>(outer->node) &&
                    StaticCast<SpawnExpr>(outer->node)->task.get() == node) {
                    continue;
                }
                return IsForbiddenFuncLike(*node) ? &*it : nullptr;
            }
            return &*it;
        }
        return nullptr;
    }

    static bool IsForbiddenFuncLike(const Node& node)
    {
        auto func = DynamicCast<FuncDecl>(&node);
        if (!func) {
            return false;
        }
        if (func->TestAttr(Attribute::MAIN_ENTRY) || func->IsFinalizer()) {
            return true;
        }
        return func->TestAttr(Attribute::CONSTRUCTOR) && func->TestAttr(Attribute::STATIC);
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

    void WalkInForbiddenContext(const ASTContext& ctx, Ptr<const Node> marker, Ptr<Node> node,
        ForbiddenTryPart part = ForbiddenTryPart::NONE)
    {
        PushForbiddenContext(marker, part);
        WalkSubtree(ctx, node);
        PopForbiddenContext();
    }

    void WalkTryExpr(const ASTContext& ctx, const TryExpr& te)
    {
        for (auto& resource : te.resourceSpec) {
            WalkSubtree(ctx, resource.get());
        }
        // The marker is the TryExpr itself; the part selects the clause name in diagnostics.
        WalkInForbiddenContext(ctx, &te, te.tryBlock.get(), ForbiddenTryPart::TRY_BLOCK);
        for (auto& catchPattern : te.catchPatterns) {
            WalkSubtree(ctx, catchPattern.get());
        }
        for (auto& catchBlock : te.catchBlocks) {
            WalkInForbiddenContext(ctx, &te, catchBlock.get(), ForbiddenTryPart::CATCH_BLOCK);
        }
        if (!te.desugarExpr) {
            for (const auto& handler : te.handlers) {
                WalkInForbiddenContext(ctx, &te, handler.commandPattern.get(), ForbiddenTryPart::HANDLE_BLOCK);
                WalkInForbiddenContext(ctx, &te, handler.block.get(), ForbiddenTryPart::HANDLE_BLOCK);
                WalkInForbiddenContext(ctx, &te, handler.desugaredLambda.get(), ForbiddenTryPart::HANDLE_BLOCK);
            }
        }
        WalkSubtree(ctx, te.finallyBlock.get());
        WalkSubtree(ctx, te.tryLambda.get());
        WalkSubtree(ctx, te.finallyLambda.get());
    }

    // returned value must be either
    // inside exclave, or
    // is @~local, or
    // is external @local! or @local?, or
    // is @local! or @local? copy type, and the return type of the function is not an interface type
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
        auto retTy = StaticCast<FuncTy>(func.DataTy())->retTy;
        for (auto& e : r) {
            // per spec we should check e->ty is non local copy type, but we check retTy because there can be Some box,
            // and it is boxed after Sema.
            if (IsInExclaveExpr(ctx, *e) || IsExternalLocal(ctx, *e) || retTy.IsDataType() || e->GetTy()->IsNothing() ||
                !e->GetTy().IsCorrect()) {
                continue;
            }
            if (type.IsCopyType(retTy.Ty()) && !Is<InterfaceTy>(retTy.Ty())) {
                continue;
            }
            DiagBadInternalLocalReturn(*e);
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
            if (HasModifier(fd->modifiers, TokenKind::EXCLAVE)) {
                DiagNestedExclave(expr, *fd);
            }
        }
    }

    /// Abstract funcs (no body in abstract class / interface) cannot be `exclave`.
    void CheckAbstractExclaveFunc(const FuncDecl& fd)
    {
        if (!fd.TestAttr(Attribute::ABSTRACT) || !HasModifier(fd.modifiers, TokenKind::EXCLAVE)) {
            return;
        }
        d.DiagnoseRefactor(DiagKindRefactor::sema_abstract_exclave_func, fd, MakeRange(fd.identifier),
            std::string{fd.identifier.Val()});
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
    /// non-copy @local! parameter, or non copy local! or local? return type.
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

    /// check exclave func with the same logic as exclave block
    void CheckExclaveModifierInInvalidFunSig(const FuncDecl& func)
    {
        const Modifier* mod{};
        for (auto& m : func.modifiers) {
            if (m.modifier == TokenKind::EXCLAVE) {
                mod = &m;
                break;
            }
        }
        if (!mod) {
            return;
        }
        if (!FuncLikeSignatureAllowsExclave(func)) {
            DiagExclaveInvalidFuncSignature(*mod);
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
            return !type.IsCopyType(ty.Ty()) && ty.Mode().local == Mode::FULL;
        };
        for (auto& paramTy : funcTy->paramTys) {
            if (isNonCopyLocalFull(paramTy)) {
                return true;
            }
            // A placeholder parameter (unsolved lambda parameter) may be inferred as
            // @local! from the callee's formal type, so allow exclave during inference.
            if (paramTy.Ty() && paramTy.Ty()->IsPlaceholder()) {
                return true;
            }
        }
        if (auto func = DynamicCast<FuncDecl>(&funcLike); func && type.HasThisParam(*func)) {
            if (funcLike.TestAttr(Attribute::CONSTRUCTOR)) {
                // local! and local? ctor always allow exclave, even copy type
                if (auto mode = TypeManager::GetThisParamMode(*func); mode != Mode::NOT) {
                    return true;
                }
            }
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

    void DiagExclaveInvalidFuncSignature(const Node& expr)
    {
        // either report on an exclave func, or on a exclave modifier of a func
        d.DiagnoseRefactor(DiagKindRefactor::sema_exclave_invalid_func_signature, expr);
    }

    /// Exclave is not allowed in spawn, the try/catch/handle parts of a try (incl. bare try-finally),
    /// main, static init, or finalizer.
    /// All sites are tracked on forbiddenContext (AST nodes); diagnose names are derived from the node.
    /// Returns true if a forbidden-context diagnostic was emitted (caller should skip signature check).
    bool CheckExclaveInForbiddenContext(const ASTContext& ctx, const ExclaveExpr& expr)
    {
        // if it's already in nested exclave, don't check to avoid 2 err message on once position.
        if (IsInExclaveExpr(ctx, expr)) {
            return false;
        }
        if (auto ctxNode = GetCurrentFrameForbiddenContext()) {
            DiagExclaveInForbiddenContext(expr, GetForbiddenContextName(*ctxNode));
            return true;
        }
        return false;
    }

    std::string_view GetForbiddenContextName(const ForbiddenContextEntry& entry)
    {
        const Node& node = *entry.node;
        if (auto func = DynamicCast<FuncDecl>(&node)) {
            if (func->IsFinalizer()) {
                return "finalizer";
            }
            if (func->TestAttr(Attribute::MAIN_ENTRY)) {
                return "main";
            }
            if (func->TestAttr(Attribute::CONSTRUCTOR) && func->TestAttr(Attribute::STATIC)) {
                return "static init";
            }
        } else if (Is<SpawnExpr>(&node)) {
            return "spawn expr";
        } else if (Is<TryExpr>(node)) {
            switch (entry.part) {
                case ForbiddenTryPart::CATCH_BLOCK:
                    return "catch block";
                case ForbiddenTryPart::HANDLE_BLOCK:
                    return "handle block";
                case ForbiddenTryPart::TRY_BLOCK:
                    return "try block";
                default:
                    CJC_ABORT();
            }
        }
        CJC_ABORT();
        return "";
    }

    void DiagExclaveInForbiddenContext(const ExclaveExpr& expr, std::string_view contextName)
    {
        d.DiagnoseRefactor(DiagKindRefactor::sema_invalid_exclave, expr, std::string{contextName});
    }

    bool IsNonCopyLocalTy(ModalTy targetTy)
    {
        if (!Ty::IsTyCorrect(targetTy)) {
            return false;
        }
        return !type.IsCopyType(targetTy.Ty()) && targetTy.IsLocalType();
    }

    /// Check the whole body of local?/local! constructor must be in exclave expr.
    void CheckWholeBodyIsExclave(const FuncDecl& init)
    {
        if (!init.TestAttr(Attribute::CONSTRUCTOR) ||
            init.TestAnyAttr(Attribute::STATIC, Attribute::IS_BROKEN, Attribute::HAS_BROKEN)) {
            return;
        }
        if (HasModifier(init.modifiers, TokenKind::EXCLAVE)) {
            return;
        }
        auto& body = init.funcBody->body;
        auto initTy = type.GetThisParamTy(init);
        if (!body || !Ty::IsTyCorrect(initTy) || initTy.IsDataType()) {
            return;
        }
        if (body->body.size() == 1 && body->body[0]->astKind == ASTKind::EXCLAVE_EXPR) {
            return;
        }
        if (body->body.empty()) {
            DiagInitModalMustBeInExclave(*body, initTy);
        } else {
            DiagInitModalMustBeInExclave(*body->body[0], initTy);
        }
    }

    /// When the init has expr, use the first expr pos; otherwise use the body pos.
    void DiagInitModalMustBeInExclave(const Node& pos, ModalTy ty)
    {
        d.DiagnoseRefactor(DiagKindRefactor::sema_init_modal_must_be_in_exclave, pos, ty.Mode().ToString());
    }

    /// Local of lvalue: for RefExpr the referenced var's local, or (implicit this) the ref target's receiver (this)
    /// type; for MemberAccess the referenced object's local (base expr type).
    Mode GetLvalueLocal(const ASTContext& ctx, const Expr* left)
    {
        if (auto ref = DynamicCast<RefExpr>(left)) {
            auto target = ref->GetTarget();
            if (auto vd = DynamicCast<VarDecl>(target);
                vd && HasModifier(vd->modifiers, TokenKind::DEMODE)) {
                return Mode::NOT;
            }
            if (target && target->IsMemberDecl()) {
                // Implicit this access: use receiver type
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
                vd && HasModifier(vd->modifiers, TokenKind::DEMODE)) {
                return Mode::NOT;
            }
            return ma->baseExpr->GetTy().IsLocalType() ? ma->baseExpr->TyMode().local : Mode::NOT;
        }
        // tuple or VArray member access
        if (auto sub = DynamicCast<SubscriptExpr>(left)) {
            return sub->baseExpr->TyMode().local;
        }
        CJC_ABORT();
        return {};
    }

    /// Whether the current walk is inside an @local? constructor or any finalizer (from forbiddenContext).
    bool IsInLocalQuestCtorOrAnyFinalizer() const
    {
        for (auto it = forbiddenContext.rbegin(); it != forbiddenContext.rend(); ++it) {
            auto func = DynamicCast<FuncDecl>(it->node);
            if (!func) {
                continue;
            }
            if (func->IsFinalizer()) {
                return true;
            }
            if (func->TestAttr(Attribute::CONSTRUCTOR) && !func->TestAttr(Attribute::STATIC)) {
                return type.HasThisParam(*func) && type.GetThisParamMode(*func).local == Mode::HALF;
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
            if (IsInLocalQuestCtorOrAnyFinalizer()) {
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
            auto rext = IsExternalLocal(ctx, *assign.rightExpr);
            if (lext != rext) {
                DiagBadAssignment(assign, left->GetTy(), assign.rightExpr->GetTy(), lext, rext);
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
            if (IsThisOrSuper(*ma->baseExpr) && IsInLocalQuestCtorOrAnyFinalizer()) {
                return;
            }
            DiagBadAssigmentToLocalHalf(assign);
            return;
        }
        if (rLocal == Mode::FULL) {
            auto lext = IsExternalLocal(ctx, *left);
            auto rext = IsExternalLocal(ctx, *assign.rightExpr);
            if (lext != rext) {
                DiagBadAssignment(assign, left->GetTy(), assign.rightExpr->GetTy(), lext, rext);
            }
        }
    }

    /// VArray `arr[n] = e`: if T is copy, @local! / @local? assign both allowed; if T is non-copy,
    /// @local? always illegal, @local! only if both arr and e are internal local! (e <: T checked by
    /// ordinary assignment typing).
    void CheckAssignExprSub(
        const ASTContext& ctx, const AssignExpr& assign, const Expr* left, Mode lLocal, Mode rLocal)
    {
        auto sub = StaticCast<SubscriptExpr>(left);
        if (!sub->IsVArrayAccess()) {
            return;
        }
        if (type.IsCopyType(StaticCast<VArrayTy>(sub->baseExpr->DataTy())->TyArg(0))) {
            return;
        }
        if (lLocal == Mode::HALF) {
            DiagBadAssigmentToLocalHalf(assign);
            return;
        }
        if (lLocal == Mode::NOT) {
            if (rLocal != Mode::NOT) {
                DiagBadAssignment(assign, {}, assign.rightExpr->GetTy());
            }
            return;
        }
        if (rLocal != Mode::FULL) {
            DiagBadAssignment(assign, {lLocal}, assign.rightExpr->GetTy());
            return;
        }
        auto lext = IsExternalLocal(ctx, *left);
        auto rext = IsExternalLocal(ctx, *assign.rightExpr);
        if (lext || rext) {
            DiagBadAssignment(assign, left->GetTy(), assign.rightExpr->GetTy(), lext, rext);
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
        if (type.IsCopyType(right->DataTy())) {
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
        } else if (DynamicCast<SubscriptExpr>(left)) {
            CheckAssignExprSub(ctx, assign, left, lLocal, rLocal);
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
        const Node& position, ModalTy lTy, ModalTy rTy, bool lExternal, bool rExternal)
    {
        // Template is "cannot assign%s to expression of '%s' type":
        // arg1 describes the RHS value (leading space); arg2 is the LHS target type (no leading space).
        std::string arg1 = (rExternal ? " external " : " internal ") + rTy.Mode().LocalString();
        std::string arg2 = (lExternal ? "external " : "internal ") + lTy.String();
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
                if (var->initializer && (!type.IsCopyType(var->DataTy()) &&
                    !HasModifier(var->modifiers, TokenKind::DEMODE))) {
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
                auto fd = StaticCast<FuncDecl>(member);
                auto thisLocal = static_cast<int>(
                    type.HasThisParam(*fd) ? type.GetThisParamMode(*fd).local : Mode::NOT);
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
    void CheckGlobalVarModalType(const VarDeclAbstract& var)
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

    /// Declaration-time initialization follows the same rule as assignment, but the initializer is
    /// evaluated in place, so a value it creates is fresh and never external.
    void CheckVarDeclInitializer(const ASTContext& ctx, const VarDeclAbstract& var)
    {
        // Member/global/static vars have their own rules for local types.
        if (var.initializer == nullptr || var.IsMemberDecl() ||
            var.TestAnyAttr(Attribute::GLOBAL, Attribute::STATIC)) {
            return;
        }
        if (var.TyMode().local != Mode::FULL || type.IsCopyType(var.DataTy())) {
            return;
        }
        auto& init = *var.initializer;
        if (!init.GetTy().IsCorrect() || init.TyMode().local != Mode::FULL) {
            return;
        }
        if (IsExternalLocal(ctx, init)) {
            // A local var is always internal, so this is an external-to-internal initialization.
            DiagBadAssignment(var, var.GetTy(), init.GetTy(), false, true);
        }
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
        if (ScopeManager::GetCurSatisfiedSymbolUntilTopLevel(
            ctx, node.scopeName, [](Symbol& sym) { return sym.node->astKind == ASTKind::EXCLAVE_EXPR; })) {
            return true;
        }
        if (auto fun = ScopeManager::GetCurSymbolByKind(SymbolKind::FUNC_LIKE, ctx, node.scopeName)) {
            if (auto fd = DynamicCast<FuncDecl>(fun->node);
                fd && HasModifier(fd->modifiers, TokenKind::EXCLAVE)) {
                return true;
            }
        }
        return false;
    }

    void DiagBadExternalLocalArg(const Node& pos)
    {
        d.DiagnoseRefactor(DiagKindRefactor::sema_external_local_arg, pos);
    }

    void DiagBadInternalLocalReturn(const Expr& expr)
    {
        d.DiagnoseRefactor(DiagKindRefactor::sema_internal_local_return, expr);
    }

    /// Check call expr args cannot be external @local! type if the param is non Copy @local!
    void CheckCallExpr(const ASTContext& ctx, const CallExpr& call)
    {
        if (auto inner = DynamicCast<CallExpr>(call.desugarExpr.get())) {
            CheckCallExpr(ctx, *inner);
            return;
        }
        if (!call.GetTy().IsCorrect()) {
            return;
        }
        // ctor is always considered exclave call, so we don't check arg pass
        if (call.callKind == CallKind::CALL_OBJECT_CREATION || call.callKind == CallKind::CALL_STRUCT_CREATION) {
            return;
        }
        if (call.desugarArgs) {
            for (auto& arg : *call.desugarArgs) {
                if (arg->TyMode().local == Mode::FULL && arg->expr && IsExternalLocal(ctx, *arg->expr) &&
                    !type.IsCopyType(arg->expr->DataTy())) {
                    DiagBadExternalLocalArg(*arg->expr);
                }
            }
        } else {
            for (auto& arg : call.args) {
                if (arg->TyMode().local == Mode::FULL && arg->expr && IsExternalLocal(ctx, *arg->expr) &&
                    !type.IsCopyType(arg->expr->DataTy())) {
                    DiagBadExternalLocalArg(*arg->expr);
                }
            }
        }
        auto fd = call.resolvedFunction;
        if (!fd || fd->TestAttr(Attribute::ENUM_CONSTRUCTOR)) {
            return;
        }
        if (TypeManager::HasThisParam(*fd)) {
            auto thisType = TypeManager::GetThisParamTy(*fd);
            if (thisType.Mode().local != Mode::FULL || type.IsCopyType(thisType.Ty())) {
                return;
            }
            if (auto ref = DynamicCast<RefExpr>(&*call.baseFunc)) {
                if (IsInExclaveExpr(ctx, *ref)) { // this is always external, only valid in exclave
                    return;
                }
            } else {
                auto& ma = StaticCast<MemberAccess>(*call.baseFunc);
                if (!IsExternalLocal(ctx, ma)) {
                    return;
                }
            }
            DiagBadExternalLocalArg(call);
        }
    }

    void DiagGlobalVarLocalModalType(const VarDeclAbstract& var)
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

    bool InheritsCopyable(const InheritableDecl& id) const
    {
        for (auto& super : id.inheritedTypes) {
            if (super && super->GetTy().IsCorrect() && type.IsCopyInterfaceTy(super->DataTy())) {
                return true;
            }
        }
        return false;
    }

    /// Check field types when struct inherits Copyable (directly or via extend).
    void CheckCopyType(StructDecl& decl, const TypeSubst& subst = {})
    {
        for (auto member : decl.GetMemberDeclPtrs()) {
            if (auto var = DynamicCast<VarDecl>(member)) {
                if (type.IsCopyType(type.GetInstantiatedTy(var->DataTy(), subst))) {
                    continue;
                }
                DiagCopyStructBadField(decl, *var);
            }
        }
    }

    void CheckExtendCopyable(ExtendDecl& ed)
    {
        if (!InheritsCopyable(ed) || !ed.extendedType || !ed.extendedType->GetTy().IsCorrect()) {
            return;
        }
        auto sd = DynamicCast<StructDecl>(Ty::GetDeclOfTy(ed.extendedType->GetTy()));
        if (!sd) {
            return;
        }
        // Avoid double-checking when the struct itself also declares `<: Copyable`.
        if (InheritsCopyable(*sd)) {
            return;
        }
        auto subst = TypeCheckUtil::GenerateTypeMapping(*sd, ed.extendedType->GetTy()->TyArgs());
        CheckCopyType(*sd, subst);
    }

    void DiagCopyStructBadField(const StructDecl& decl, const VarDecl& var)
    {
        d.DiagnoseRefactor(DiagKindRefactor::sema_copy_struct_bad_field, var, var.identifier.Val(),
            var.GetTy()->String(), decl.identifier.Val());
    }

    DiagnosticEngine& d;
    TypeManager& type;
    // FuncDecl/LambdaExpr: frame boundary (also forbidden if main/finalizer/static init).
    // SpawnExpr / TryExpr: nested sites; a TryExpr entry records which clause the walk is in
    // (a bare try-finally reports "try block"; finally itself is not pushed).
    std::vector<ForbiddenContextEntry> forbiddenContext;
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
    // All local mode checks are meaningless when the feature is disabled, skip for performance.
    if (!ci->invocation.globalOptions.enableLocalMode) {
        return;
    }
    modalTypeChecker->Check(ctx, pkg);
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
    auto funcBodyTy = fb ? DynamicCast<FuncTy>(fb->DataTy()) : nullptr;
    if (!fb || !funcBodyTy) {
        // No target: do not check exclave return type; any type inside body is correct
        SynBlock({ctx, SynPos::EXPR_ARG}, *expr.body);
        expr.SetTy(ModalTy{TypeManager::GetNothingTy()});
        return expr.GetTy();
    }

    auto target = funcBodyTy->retTy;
    if (!target.Ty() || target->kind == TypeKind::TYPE_QUEST) {
        SynBlock({ctx, SynPos::EXPR_ARG}, *expr.body);
        // Use body type instead of Nothing when it is correct and has no QuestTy/
        // IdealTy/IdealModal, so CalcFuncRetTyFromBody can infer the return type
        // when the enclosing function's return type is still QuestTy (being inferred).
        auto bodyTy = expr.body->GetTy();
        expr.SetTy(bodyTy.IsCorrect() && !bodyTy->HasQuestTy() && !bodyTy->HasIdealTy() && !bodyTy->HasIdealModal()
                ? bodyTy
                : ModalTy{TypeManager::GetNothingTy()});
        return expr.GetTy();
    }

    auto fun = ScopeManager::GetCurSymbolByKind(SymbolKind::FUNC_LIKE, ctx, expr.scopeName);
    if (auto func = fun && fun->node ? DynamicCast<FuncDecl>(fun->node) : nullptr;
        func && (func->TestAttr(Attribute::CONSTRUCTOR) || func->IsFinalizer())) {
        SynBlock({ctx, SynPos::EXPR_ARG}, *expr.body);
        expr.SetTy(ModalTy{TypeManager::GetNothingTy()});
        return expr.GetTy();
    }

    if (!ChkBlock(ctx, target, *expr.body)) {
        expr.SetTy({TypeManager::GetInvalidTy()});
        return expr.GetTy();
    }
    expr.SetTy(ModalTy{TypeManager::GetNothingTy()});
    return expr.GetTy();
}
} // namespace Cangjie
