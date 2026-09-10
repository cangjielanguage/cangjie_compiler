// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 *  This file declares a class for extend box marker.
 */

#ifndef CANGJIE_SEMA_EXTEND_BOX_MARKER_H
#define CANGJIE_SEMA_EXTEND_BOX_MARKER_H

#include <functional>
#include <mutex>

#include "cangjie/AST/Node.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Sema/TypeManager.h"

namespace Cangjie {
class ExtendBoxMarker {
public:
    static std::function<AST::VisitAction(Ptr<AST::Node>)> GetMarkExtendBoxFunc(TypeManager& typeMgr);
    /**
     * Return true if @p selectorTy and @p patternTy must be treated as unboxing downcast.
     * NOTE: for special case:
     *       interface I {}; class A <: I {}
     *       func f(any: Any) {
     *           let i : I = (any as I).getOrThrow()
     *           let v = i as A
     *       }
     *       main() {
     *           f(A())
     *       }
     * If do not downcast with box, the conversion 'i as A' will be failed since the instance is actually 'BOX_A'.
     * So, when converting interface to class, we always need to check both boxed condition and non-box condition.
     * And for another case:
     * If selector's type is interface and the pattern's type is class,
     * the pattern must be boxed even extension relation checking failed,
     * since the instance may be subclass of pattern's type which has extend relation with the interface.
     * eg. open class A {}; class B <: A {}
     *     interface I {}; extend B <: I {}
     *     let ins : I = B()
     * then 'ins is A' should be true.
     */
    static bool MustUnboxDownCast(const AST::Ty& selectorTy, const AST::Ty& patternTy)
    {
        return selectorTy.IsInterface() && patternTy.IsClass();
    }
    static std::mutex mtx; // Used to keep marker working as thread-safe.

private:
    static AST::VisitAction MarkBoxPointHandleVarDecl(AST::VarDecl& vd);
    static AST::VisitAction MarkBoxPointHandleAssignExpr(AST::AssignExpr& ae);
    static AST::VisitAction MarkBoxPointHandleCallExpr(AST::CallExpr& ce);
    static AST::VisitAction MarkBoxPointHandleIfExpr(AST::IfExpr& ie);
    static AST::VisitAction MarkBoxPointHandleWhileExpr(const AST::WhileExpr& we);
    static void MarkBoxPointHandleCondition(AST::Expr& e);
    static AST::VisitAction MarkBoxPointHandleReturnExpr(AST::ReturnExpr& re);
    static AST::VisitAction MarkBoxPointHandleArrayLit(AST::ArrayLit& lit);
    static AST::VisitAction MarkBoxPointHandleTryExpr(AST::TryExpr& te);
    static AST::VisitAction MarkBoxPointHandleMatchExpr(AST::MatchExpr& me);
    static AST::VisitAction MarkBoxPointHandleArrayExpr(AST::ArrayExpr& ae);
    static AST::VisitAction MarkBoxPointHandleTupleLit(AST::TupleLit& tl);
    static bool IsTypePatternNeedBox(Ptr<AST::Pattern> pattern, AST::Ty& selectorTy);
    static bool NeedAutoBox(AST::DataTy child, AST::DataTy interface, bool isUpcast = true);
    static void CheckBlockNeedBox(const AST::Block& block, AST::Ty& ty, AST::Node& nodeToCheck);

    static TypeManager* typeManager;
};
} // namespace Cangjie
#endif
