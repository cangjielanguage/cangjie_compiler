// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "Desugar/AfterTypeCheck.h"

#include "cangjie/AST/Create.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/AST/ASTCasting.h"

namespace Cangjie::Sema::Desugar::AfterTypeCheck {
Ptr<Decl> LookupEnumMember(Ptr<Decl> decl, const std::string& identifier)
{
    if (decl == nullptr || decl->astKind != ASTKind::ENUM_DECL) {
        return nullptr;
    }
    auto enumDecl = RawStaticCast<EnumDecl*>(decl);
    for (auto& member : enumDecl->constructors) {
        if (member->identifier == identifier) {
            return member.get();
        }
    }
    return nullptr;
}

void UnitifyBlock(const Expr& posSrc, Block& b, ModalTy unitTy)
{
    auto unitExpr = CreateUnitExpr();
    unitExpr->begin = posSrc.begin;
    unitExpr->begin.Mark(PositionStatus::IGNORE);
    unitExpr->end = posSrc.end;
    unitExpr->SetTy(unitTy);
    b.body.push_back(std::move(unitExpr));
    b.SetTy(unitTy);
}

void RearrangeRefLoop(const Expr& src, Expr& dst, Ptr<Node> loopBody)
{
    if (loopBody == nullptr) {
        return;
    }
    std::function<VisitAction(Ptr<Node>)> visitFunc = [&src, &dst](Ptr<Node> node) {
        if (auto je = DynamicCast<JumpExpr*>(node); je) {
            if (je->refLoop == &src) {
                je->refLoop = &dst;
            }
        }
        // skip the nested loop structure and lambda
        if (node->astKind == ASTKind::FUNC_DECL || node->astKind == ASTKind::LAMBDA_EXPR) {
            return VisitAction::SKIP_CHILDREN;
        }
        return VisitAction::WALK_CHILDREN;
    };
    Walker(loopBody, visitFunc).Walk();
}
} // namespace Cangjie::Sema::Desugar::AfterTypeCheck
