// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.
#include "TypeCheckerImpl.h"

#include "TypeCheckUtil.h"

#include "cangjie/AST/Create.h"
#include "cangjie/AST/Utils.h"

using namespace Cangjie;
using namespace AST;
using namespace TypeCheckUtil;

namespace {
OwnedPtr<MatchCase> GetValueMatchCase(
    OwnedPtr<RefExpr> ctor, OwnedPtr<Block> someExpr, RefExpr& someVar, Expr& selector)
{
    auto valMatchCase = MakeOwnedNode<MatchCase>();
    CJC_ASSERT(selector.ty->IsCoreOptionType()); // Caller guarantees.
    auto innerTy = selector.ty->typeArgs[0];
    CopyBasicInfo(&selector, valMatchCase.get());

    // 'x' in 'Some(x)'.
    auto someArg = MakeOwned<VarPattern>(someVar.ref.identifier, INVALID_POSITION);
    someArg->ty = innerTy;
    someArg->varDecl->ty = innerTy;
    
    someArg->EnableAttr(Attribute::COMPILER_ADD);
    // 'x' in '=> x'.
    someVar.ref.target = someArg->varDecl.get();
    someVar.ty = someVar.ref.target->ty;
    // Enum pattern 'Some(x)'.
    auto enumPattern = MakeOwnedNode<EnumPattern>();
    CopyBasicInfo(someExpr.get(), enumPattern.get());
    enumPattern->constructor = std::move(ctor);
    enumPattern->patterns.emplace_back(std::move(someArg));
    enumPattern->ty = selector.ty;
    // Case body of '=> x'.
    someExpr->ty = innerTy;
    someExpr->curFile = selector.curFile;
    // Entire case expression 'case Some(x) => x'.
    valMatchCase->patterns.emplace_back(std::move(enumPattern));
    valMatchCase->SetCtxExprForPatterns(&selector);
    valMatchCase->patternGuard = nullptr;
    valMatchCase->exprOrDecls = std::move(someExpr);
    valMatchCase->ty = valMatchCase->exprOrDecls->ty;
    return valMatchCase;
}
}

/**
 * Given selector A, SomeExpr B, OtherExpr C. Ref var x. Only support for 'Option'.
 * Construct as bellow,
 * match (A) {
 *  case CTOR(x) => B
 *  case _: => C
 * }
 * NOTE: this happens before generic instantiation.
 */
OwnedPtr<Expr> ConstructOptionMatch(OwnedPtr<Expr> selector, OwnedPtr<Block> someExpr,
    OwnedPtr<Block> otherExpr, RefExpr& someVar, Ptr<Ty> someTy)
{
    Ptr<FuncDecl> ctorDecl = nullptr;
    // Caller guarantees seletor is enum option type.
    auto enumTy = StaticCast<EnumTy*>(selector->ty);
    for (auto& it : enumTy->declPtr->constructors) {
        if (it->identifier == OPTION_VALUE_CTOR) {
            ctorDecl = StaticCast<FuncDecl*>(it.get());
            break;
        }
    }
    if (ctorDecl == nullptr) {
        return nullptr;
    }

    auto matchExpr = MakeOwnedNode<MatchExpr>();
    matchExpr->matchMode = true;
    matchExpr->sugarKind = Expr::SugarKind::QUEST;
    matchExpr->selector = std::move(selector);

    auto valueRef = CreateRefExpr({OPTION_VALUE_CTOR, DEFAULT_POSITION, DEFAULT_POSITION, false}, someTy);
    valueRef->ref.target = ctorDecl;
    auto valMatchCase = GetValueMatchCase(std::move(valueRef), std::move(someExpr), someVar, *matchExpr->selector);
    matchExpr->matchCases.emplace_back(std::move(valMatchCase));

    // Wild case body 'case _ => expr.
    otherExpr->curFile = matchExpr->selector->curFile;
    auto wildMatchCase = MakeOwnedNode<MatchCase>();
    wildMatchCase->patterns.emplace_back(MakeOwnedNode<WildcardPattern>());
    wildMatchCase->SetCtxExprForPatterns(matchExpr->selector.get());
    wildMatchCase->patternGuard = nullptr;
    wildMatchCase->exprOrDecls = std::move(otherExpr);
    wildMatchCase->ty = wildMatchCase->exprOrDecls->ty;
    CopyBasicInfo(matchExpr->selector.get(), wildMatchCase.get());
    matchExpr->matchCases.emplace_back(std::move(wildMatchCase));
    return matchExpr;
}

/**
 * Desugar for Binary expression for ??(coalescing).
 * Only support 'Option' in core package.
 * *************** before desugar ****************
 * var option = Option<Int32>.Some(1)
 * var val0 : Int32 = option ?? 11
 * *************** after desugar *****************
 * var option = Option<Int32>.Some(1)
 * var val0 : Int32 = match (option) {
 *     case Some(x) => x
 *     case $None => 11
 * }
 */
void TypeChecker::TypeCheckerImpl::DesugarForCoalescing(BinaryExpr& binaryExpr) const
{
    // Caller guarantees the 'binaryExpr.desugarExpr' is not existed.
    CJC_ASSERT(binaryExpr.rightExpr && binaryExpr.leftExpr);
    auto leftTy = binaryExpr.leftExpr->ty;
    if (!leftTy->IsCoreOptionType()) {
        return;
    }
    // Case body of 'Some(x) => x'.
    auto expr = CreateRefExpr("x");
    auto& refExpr = *expr;
    auto caseBody = MakeOwnedNode<Block>();
    caseBody->body.emplace_back(std::move(expr));
    // Case body 'case _ => rightExpr of binaryExpr'
    auto wildBody = MakeOwnedNode<Block>();
    auto rightTy = binaryExpr.rightExpr->ty;
    (void)wildBody->body.emplace_back(std::move(binaryExpr.rightExpr));
    wildBody->ty = rightTy;

    auto someTy = typeManager.GetFunctionTy({leftTy->typeArgs[0]}, leftTy);
    auto desugarExpr = ConstructOptionMatch(
        std::move(binaryExpr.leftExpr), std::move(caseBody), std::move(wildBody), refExpr, someTy);
    if (desugarExpr != nullptr) {
        desugarExpr->ty = binaryExpr.ty;
        binaryExpr.desugarExpr = std::move(desugarExpr);
        AddCurFile(*binaryExpr.desugarExpr, binaryExpr.curFile);
    }
}

void TypeChecker::TypeCheckerImpl::TryDesugarForCoalescing(Node& root) const
{
    std::function<VisitAction(Ptr<Node>)> visitBe = [this](Ptr<Node> node) -> VisitAction {
        if (auto be = DynamicCast<BinaryExpr*>(node); be && be->op == TokenKind::COALESCING && !be->desugarExpr) {
            DesugarForCoalescing(*be);
        }
        return VisitAction::WALK_CHILDREN;
    };
    Walker walker(&root, visitBe);
    walker.Walk();
}
