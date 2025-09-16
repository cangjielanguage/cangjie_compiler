// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares class for auto boxing extend and option.
 */
#ifndef CANGJIE_SEMA_AUTO_BOX_H
#define CANGJIE_SEMA_AUTO_BOX_H

#include "cangjie/AST/ASTContext.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Mangle/BaseMangler.h"
#include "cangjie/Modules/ImportManager.h"

namespace Cangjie {
class AutoBoxing {
public:
    explicit AutoBoxing(TypeManager& typeManager) : typeManager(typeManager)
    {
    }
    ~AutoBoxing() = default;

    void AddOptionBox(AST::Package& pkg);

private:
    /** Boxing functions for option box. */
    void TryOptionBox(AST::EnumTy& target, AST::Expr& expr);
    bool NeedBoxOption(AST::Ty& child, AST::Ty& target);
    AST::VisitAction AddOptionBoxHandleReturnExpr(const AST::ReturnExpr& re);
    AST::VisitAction AddOptionBoxHandleVarDecl(const AST::VarDecl& vd);
    AST::VisitAction AddOptionBoxHandleAssignExpr(const AST::AssignExpr& ae);
    AST::VisitAction AddOptionBoxHandleCallExpr(AST::CallExpr& ce);
    AST::VisitAction AddOptionBoxHandleIfExpr(const AST::IfExpr& ie);
    AST::VisitAction AddOptionBoxHandleTryExpr(AST::TryExpr& te);
    AST::VisitAction AddOptionBoxHandleArrayLit(AST::ArrayLit& lit);
    AST::VisitAction AddOptionBoxHandleMatchExpr(AST::MatchExpr& me);
    AST::VisitAction AddOptionBoxHandleArrayExpr(AST::ArrayExpr& ae);
    AST::VisitAction AddOptionBoxHandleTupleList(const AST::TupleLit& tl);
    void AddOptionBoxHandleBlock(AST::Block& block, AST::Ty& ty);

    TypeManager& typeManager;
};
} // namespace Cangjie

#endif
