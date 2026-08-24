// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * InstantiatedExtendRecorder is the class to check used extend decl for each sema type.
 * NOTE: this should be used before instantiated pointer rearrange.
 */

#ifndef CANGJIE_SEMA_INSTANTIATED_EXTEND_RECORDER_H
#define CANGJIE_SEMA_INSTANTIATED_EXTEND_RECORDER_H

#include "GenericInstantiationManagerImpl.h"

#include "Promotion.h"
#include "cangjie/AST/Node.h"
#include "cangjie/Sema/TypeManager.h"

namespace Cangjie {
class GenericInstantiationManager::InstantiatedExtendRecorder {
public:
    InstantiatedExtendRecorder(GenericInstantiationManagerImpl& gim, TypeManager& tyMgr);
    ~InstantiatedExtendRecorder() = default;

    void operator()(AST::Node& node);

private:
    /** Walker function to record extend. */
    AST::VisitAction RecordUsedExtendDecl(AST::Node& node);
    void RecordExtendForRefExpr(const AST::RefExpr& re);
    void RecordExtendForMemberAccess(const AST::MemberAccess& ma);
    void RecordImplExtendDecl(AST::ModalTy ty, AST::FuncDecl& fd, AST::ModalTy upperTy);

    GenericInstantiationManagerImpl& gim;
    TypeManager& typeManager;
    Promotion promotion;

    unsigned recorderId;
    std::function<AST::VisitAction(Ptr<AST::Node>)> extendRecorder;
};
} // namespace Cangjie
#endif
