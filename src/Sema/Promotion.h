// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.
/**
 * @file
 *
 * This file declares a class providing functions for promoting a subtype to a designated supertype if possible.
 * It also provides utility functions that handle type substitutions.
 */

#ifndef CANGJIE_SEMA_PROMOTION_H
#define CANGJIE_SEMA_PROMOTION_H

#include "cangjie/AST/Types.h"
#include "cangjie/Sema/TypeManager.h"

namespace Cangjie {
class Promotion {
public:
    explicit Promotion(TypeManager& tyMgr) : tyMgr(tyMgr)
    {
    }
    MultiTypeSubst GetPromoteTypeMapping(AST::DataTy from, AST::DataTy target);
    MultiTypeSubst GetDowngradeTypeMapping(AST::DataTy target, AST::DataTy upfrom);
    std::set<AST::ModalTy> Promote(AST::ModalTy from, AST::ModalTy target);
    std::set<AST::DataTy> Promote(AST::DataTy from, AST::DataTy target);
    // will return empty if any type arg of target (the subtype) unused in upfrom (the supertype)
    // e.g. downgrading to Future<T> from Any
    std::set<AST::ModalTy> Downgrade(AST::ModalTy target, AST::ModalTy upfrom);
    std::set<AST::DataTy> Downgrade(AST::DataTy target, AST::DataTy upfrom);

private:
    TypeManager& tyMgr;
    std::set<AST::DataTy> PromoteHandleIdealTys(AST::DataTy from, AST::DataTy target) const;
    std::set<AST::DataTy> PromoteHandleFunc(AST::DataTy from, AST::DataTy target);
    std::set<AST::DataTy> PromoteHandleTuple(AST::DataTy from, AST::DataTy target);
    std::set<AST::DataTy> PromoteHandleTyVar(AST::DataTy from, AST::DataTy target);
    std::set<AST::DataTy> PromoteHandleNominal(AST::DataTy from, AST::DataTy target);
};
} // namespace Cangjie
#endif // CANGJIE_SEMA_PROMOTION_H
