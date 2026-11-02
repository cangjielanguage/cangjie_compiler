// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "TypeCheckerImpl.h"

#include "cangjie/AST/Create.h"
#include "cangjie/AST/Utils.h"

using namespace Cangjie;
using namespace AST;

// The desugar of a `SpawnArg` is the form `arg.getSchedulerHandle()`
void TypeChecker::TypeCheckerImpl::DesugarSpawnArgExpr(const ASTContext& ctx, const AST::SpawnExpr& se)
{
    if (!se.GetTy().IsCorrect() || se.arg->desugarExpr) {
        return;
    }
    Ptr<Expr> arg = se.arg.get();
    CJC_ASSERT(arg && arg->GetTy().IsCorrect() && arg->GetTy()->IsClassLike());
    // Get `getSchedulerHandle` method from spawn argument, whose signature is `()->CPointer<Unit>`.
    auto classLikeTy = StaticCast<ClassLikeTy*>(arg->DataTy());
    auto retTy = typeManager.GetPointerTy(TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT));
    auto funcTy = typeManager.GetFunctionTy({}, {retTy});
    const auto fieldName = "getSchedulerHandle";
    auto decls = FieldLookup(ctx, classLikeTy->commonDecl, fieldName, {.file = se.curFile});
    CJC_ASSERT(decls.size() == 1); // `getSchedulerHandle(): CPointer<Unit>` is the private method.
    auto decl = decls.front();
    CJC_ASSERT(decl->GetTy().IsCorrect() && decl->GetTy()->IsFunc() && typeManager.IsSubtype(decl->GetTy(), {funcTy}));
    auto ma = CreateMemberAccess(ASTCloner::Clone(arg), fieldName);
    CopyBasicInfo(arg, ma.get());
    auto ce = CreateCallExpr(std::move(ma), {});
    CopyBasicInfo(arg, ce.get());
    ce->callKind = CallKind::CALL_DECLARED_FUNCTION;
    ce->resolvedFunction = StaticCast<FuncDecl*>(decl);
    ce->SetTy({retTy});
    AddCurFile(*ce, se.curFile);
    arg->desugarExpr = std::move(ce);
}

// The `futureObj` of a `SpawnExpr` is `VarDecl` of the form `let futureObj = Future(task)`
// NOTE: This syntax sugar is stored in `futureObj` rather than `desugarExpr`.
void TypeChecker::TypeCheckerImpl::DesugarSpawnExpr(const ASTContext& ctx, AST::SpawnExpr& se)
{
    if (!se.GetTy().IsCorrect() || se.futureObj) {
        return;
    }
    Ptr<Expr> task = se.task.get();
    CJC_ASSERT(task && task->GetTy().IsCorrect() && task->GetTy()->IsFunc());
    Ptr<FuncTy> taskTy = RawStaticCast<FuncTy*>(task->DataTy());
    CJC_ASSERT(se.GetTy()->IsClass());
    Ptr<ClassDecl> futureClass = RawStaticCast<ClassTy*>(se.DataTy())->declPtr;
    CJC_ASSERT(futureClass->GetTy().IsCorrect() && futureClass->GetTy()->typeArgs.size() == 1);
    std::vector<Ptr<FuncDecl>> inits;
    for (auto& decl : futureClass->GetMemberDecls()) {
        CJC_NULLPTR_CHECK(decl);
        if (decl->astKind == ASTKind::FUNC_DECL && decl.get()->TestAttr(Attribute::CONSTRUCTOR)) {
            inits.emplace_back(StaticCast<FuncDecl*>(decl.get()));
        }
    }
    CJC_ASSERT(inits.size() == 1); // `init(fn: ()->T)` is the only constructor for `Future`
    auto initDecl = inits.front();
    CJC_ASSERT(initDecl->GetTy().IsCorrect() && initDecl->GetTy()->IsFunc());
    // Prepare the `baseFunc` of the `Future` function call.
    auto re = CreateRefExprInCore("Future");
    re->isAlone = false;
    re->ref.target = initDecl;
    re->instTys.emplace_back(taskTy->retTy.Ty());
    re->SetTy(typeManager.GetInstantiatedTy(
        initDecl->DataTy(), {{StaticCast<GenericsTy>(futureClass->GetTy()->TyArgs().front()), taskTy->retTy.Ty()}}));
    CopyBasicInfo(task, re.get());
    // Prepare the arguments of the `CallExpr`.
    std::vector<OwnedPtr<FuncArg>> callArgs;
    auto fa = CreateFuncArg(std::move(se.task));
    CopyBasicInfo(task, fa.get());
    fa->SetTy(task->GetTy());
    callArgs.emplace_back(std::move(fa));
    // Create the `CallExpr`.
    auto ce = CreateCallExpr(std::move(re), std::move(callArgs));
    CopyBasicInfo(task, ce.get());
    ce->callKind = CallKind::CALL_OBJECT_CREATION;
    ce->resolvedFunction = initDecl;
    ce->SetTy(se.GetTy());

    auto futureObj = CreateVarDecl("futureObj", std::move(ce));
    CopyBasicInfo(task, futureObj.get());
    AddCurFile(*futureObj, se.curFile);
    se.futureObj = std::move(futureObj);

    if (!se.arg) {
        return;
    }
    DesugarSpawnArgExpr(ctx, se);
}
