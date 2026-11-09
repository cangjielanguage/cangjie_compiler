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
using namespace TypeCheckUtil;

namespace Cangjie::Sema::Desugar::AfterTypeCheck {
/**
 * Desugar IsExpr to TypePattern of MatchExpr.
 * *************** before desugar ****************
 * e is T
 * *************** after desugar ****************
 * match (e) {
 *     case _: T => true
 *     case _ => false
 * }
 * */
void DesugarIsExpr(TypeManager& typeManager, IsExpr& ie)
{
    if (!ie.GetTy().IsCorrect() || !ie.GetTy()->IsBoolean() || ie.desugarExpr) {
        return;
    }
    CJC_NULLPTR_CHECK(ie.leftExpr);
    CJC_NULLPTR_CHECK(ie.isType);
    CJC_NULLPTR_CHECK(ie.leftExpr->GetTy());
    CJC_NULLPTR_CHECK(ie.isType->GetTy());
    auto boolTy = ie.GetTy();
    std::vector<OwnedPtr<MatchCase>> matchCases;
    auto trueExpr = CreateLitConstExpr(LitConstKind::BOOL, "true", boolTy);
    auto falseExpr = CreateLitConstExpr(LitConstKind::BOOL, "false", boolTy);
    trueExpr->begin = ie.isPos;
    falseExpr->begin = ie.isPos;
    matchCases.emplace_back(CreateMatchCase(
        CreateRuntimePreparedTypePattern(
            typeManager, MakeOwnedNode<WildcardPattern>(), std::move(ie.isType), *ie.leftExpr
        ),
        std::move(trueExpr)));
    auto wildcard = MakeOwnedNode<WildcardPattern>();
    wildcard->SetTy(ie.leftExpr->GetTy());
    matchCases.emplace_back(
        CreateMatchCase(std::move(wildcard), std::move(falseExpr)));
    ie.desugarExpr = CreateMatchExpr(std::move(ie.leftExpr), std::move(matchCases), boolTy, Expr::SugarKind::IS);
    AddCurFile(*ie.desugarExpr, ie.curFile);
}
} // namespace Cangjie::Sema::Desugar::AfterTypeCheck
