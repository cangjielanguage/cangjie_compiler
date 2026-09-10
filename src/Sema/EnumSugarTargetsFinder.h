// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the EnumSugarTargetFiner class.
 */

#ifndef CANGJIE_SEMA_ENUMSUGARTARGETSFINDER_H
#define CANGJIE_SEMA_ENUMSUGARTARGETSFINDER_H

#include <vector>

#include "TypeCheckerImpl.h"
#include "cangjie/AST/ASTContext.h"
#include "cangjie/AST/Node.h"
#include "cangjie/Modules/ImportManager.h"

namespace Cangjie {
class EnumSugarTargetsFinder {
public:
    EnumSugarTargetsFinder(TypeManager& typeManager, ASTContext& ctx, AST::RefExpr& re)
        : tyMgr(typeManager), ctx(ctx), refExpr(re)
    {
    }
    std::vector<Ptr<AST::Decl>> FindEnumSugarTargets();
    static std::optional<AST::ModalTy> RefineTargetTy(
        TypeManager& typeManager, AST::ModalTy targetTy, Ptr<const AST::Decl> target);

private:
    void RefineTargets();
    TypeManager& tyMgr;
    ASTContext& ctx;
    AST::RefExpr& refExpr;
    std::vector<Ptr<AST::Decl>> enumSugarTargets;
};
} // namespace Cangjie

#endif
