// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "Desugar/AfterTypeCheck.h"

#include "TypeCheckUtil.h"

#include "cangjie/AST/Create.h"
#include "cangjie/AST/Utils.h"

using namespace Cangjie;
using namespace AST;
using namespace TypeCheckUtil;

namespace {
OwnedPtr<CallExpr> CreateAsExprSomeCall(
    FuncDecl& someDecl, Type& theAsType, VarDecl& varDecl, FuncTy& someCtorTy)
{
    CJC_ASSERT(someCtorTy.paramTys.size() == 1);
    auto some = MakeOwnedNode<CallExpr>();
    auto someRef = CreateRefExpr(someDecl);
    auto theAsTy = someCtorTy.paramTys[0];
    auto optionTy = someCtorTy.retTy;
    someRef->typeArguments.emplace_back(ASTCloner::Clone(Ptr(&theAsType)));
    someRef->instTys.emplace_back(theAsTy);
    someRef->ref.targets.emplace_back(&someDecl);
    someRef->isAlone = false;
    someRef->ty = &someCtorTy;
    someRef->callOrPattern = some.get();
    auto newVarRef = CreateRefExpr(varDecl);
    newVarRef->ty = theAsTy;
    auto someArg = MakeOwnedNode<FuncArg>();
    someArg->expr = std::move(newVarRef);
    someArg->ty = theAsTy;
    some->baseFunc = std::move(someRef);
    some->args.emplace_back(std::move(someArg));
    some->resolvedFunction = &someDecl;
    some->callKind = CallKind::CALL_DECLARED_FUNCTION;
    some->ty = optionTy;
    return some;
}
} // namespace

namespace Cangjie::Sema::Desugar::AfterTypeCheck {
/**
 * Desugar AsExpr to TypePattern of MatchExpr.
 * *************** before desugar ****************
 * e as T
 * *************** after desugar ****************
 * match (e) {
 *     case newVar : T => Some(newVar)
 *     case _ => None
 * }
 * */
void DesugarAsExpr(TypeManager& typeManager, AsExpr& ae)
{
    if (!Ty::IsTyCorrect(ae.ty) || !ae.ty->IsCoreOptionType() || ae.desugarExpr) {
        return;
    }
    CJC_NULLPTR_CHECK(ae.leftExpr);
    CJC_NULLPTR_CHECK(ae.asType);
    CJC_NULLPTR_CHECK(ae.leftExpr->ty);
    CJC_NULLPTR_CHECK(ae.asType->ty);
    auto optionTy = ae.ty;
    auto selectorTy = ae.leftExpr->ty;
    auto theAsType = ASTCloner::Clone(ae.asType.get());
    auto theAsTy = ae.asType->ty;
    auto optionDecl = StaticCast<EnumTy*>(optionTy)->decl;
    CJC_NULLPTR_CHECK(optionDecl);
    auto someDecl = StaticCast<FuncDecl*>(LookupEnumMember(optionDecl, OPTION_VALUE_CTOR));
    CJC_NULLPTR_CHECK(someDecl);
    std::vector<OwnedPtr<MatchCase>> matchCases;
    auto varPattern = CreateVarPattern(V_COMPILER, theAsTy);
    varPattern->begin = ae.asPos;
    varPattern->end = ae.asPos;
    auto varDecl = varPattern->varDecl.get();
    varDecl->fullPackageName = ae.GetFullPackageName();
    matchCases.emplace_back(
        CreateMatchCase(
            CreateRuntimePreparedTypePattern(typeManager, std::move(varPattern), std::move(ae.asType), *ae.leftExpr),
            CreateAsExprSomeCall(*someDecl, *theAsType, *varDecl, *typeManager.GetFunctionTy({theAsTy}, optionTy))));
    auto wildcard = MakeOwnedNode<WildcardPattern>();
    wildcard->ty = selectorTy;
    auto noneDecl = LookupEnumMember(optionDecl, OPTION_NONE_CTOR);
    CJC_NULLPTR_CHECK(noneDecl);
    auto none = CreateRefExpr(*noneDecl);
    CopyBasicInfo(&ae, none.get());
    none->typeArguments.emplace_back(std::move(theAsType));
    none->instTys.emplace_back(theAsTy);
    none->ty = optionTy;
    matchCases.emplace_back(CreateMatchCase(std::move(wildcard), std::move(none)));
    ae.desugarExpr = CreateMatchExpr(std::move(ae.leftExpr), std::move(matchCases), optionTy, Expr::SugarKind::AS);
    CopyBasicInfo(&ae, ae.desugarExpr.get());
    AddCurFile(*ae.desugarExpr, ae.curFile);
}
} // namespace Cangjie::Sema::Desugar::AfterTypeCheck
