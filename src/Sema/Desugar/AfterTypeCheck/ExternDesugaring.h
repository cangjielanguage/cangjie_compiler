// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the class for the desugaring of Extern<T>.
 */

#ifndef CANGJIE_SEMA_EXTERN_DESUGARING_H
#define CANGJIE_SEMA_EXTERN_DESUGARING_H

#include <functional>
#include <string>
#include <vector>

#include "TypeCheckerImpl.h"

#include "cangjie/AST/Node.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Sema/TypeManager.h"

namespace Cangjie {
class ExternDesugaring {
public:
    using RuntimeFuncLookup =
        std::function<ForeignRuntimeFunc(AST::Ty& runtimeTy, const std::string& name, const AST::Expr& expr)>;
    using CallCheck = std::function<void(const AST::CallExpr& ce)>;

    ExternDesugaring(TypeManager& typeManager, RuntimeFuncLookup lookup, CallCheck checkUnsafeInvoke)
        : typeManager(typeManager), lookup(std::move(lookup)), checkUnsafeInvoke(std::move(checkUnsafeInvoke))
    {
    }

    void Run(AST::Node& root);
    bool CreatedRuntimeCalls() const
    {
        return createdRuntimeCalls;
    }

private:
    /** A static function of the foreign runtime T of a given Extern<T>, with its type instantiated. */
    struct RuntimeFunc {
        Ptr<AST::FuncDecl> decl{nullptr};
        Ptr<AST::Ty> matchedParentTy{nullptr};
        Ptr<AST::FuncTy> ty{nullptr};
    };

    // Conversions to Extern<T>.
    /** Return whether @p expr was converted to @p target, otherwise @p expr is left unchanged. */
    bool TryConvert(AST::Expr& expr, AST::Ty& target);
    void TryConvertBlock(AST::Block& block, AST::Ty& target);

    void HandleVarDecl(const AST::VarDeclAbstract& vd);
    void HandleAssignExpr(const AST::AssignExpr& ae);
    void HandleCallExpr(AST::CallExpr& ce);
    void HandleReturnExpr(const AST::ReturnExpr& re);
    void HandleFuncBody(const AST::FuncBody& fb);
    void HandleArrayExpr(AST::ArrayExpr& ae);
    void HandleArrayLit(AST::ArrayLit& al);
    void HandleTupleLit(AST::TupleLit& tl);
    void HandleIfExpr(AST::IfExpr& ie);
    void HandleMatchExpr(AST::MatchExpr& me);
    void HandleTryExpr(AST::TryExpr& te);

    // Dynamic operations on Extern<T>.
    void DesugarOperation(AST::Expr& expr);
    OwnedPtr<AST::Expr> BuildMemberAccess(AST::MemberAccess& ma, AST::Ty& externTy);
    OwnedPtr<AST::Expr> BuildIndexedAccess(AST::SubscriptExpr& se, AST::Ty& externTy);
    OwnedPtr<AST::Expr> BuildFunctionCall(AST::CallExpr& ce, AST::Ty& externTy);
    OwnedPtr<AST::Expr> BuildUpdate(AST::AssignExpr& ae, AST::Ty& externTy);
    OwnedPtr<AST::Expr> BuildCompoundAssignment(AST::AssignExpr& ae, AST::Ty& externTy);
    OwnedPtr<AST::Expr> BuildMemberCompoundUpdate(AST::AssignExpr& ae, AST::MemberAccess& ma, AST::Ty& externTy,
        std::vector<OwnedPtr<AST::Node>>& nodes);
    OwnedPtr<AST::Expr> BuildIndexedCompoundUpdate(AST::AssignExpr& ae, AST::SubscriptExpr& se, AST::Ty& externTy,
        std::vector<OwnedPtr<AST::Node>>& nodes);
    OwnedPtr<AST::Expr> CreateCompoundOpCall(AST::AssignExpr& ae, OwnedPtr<AST::Expr> current, AST::Ty& externTy,
        std::vector<OwnedPtr<AST::Node>>& nodes);
    /** The receiver e[i1]...[in-1] of the last index of e[i1, ..., in]. */
    OwnedPtr<AST::Expr> BuildIndexedReceiver(AST::SubscriptExpr& se, AST::Ty& externTy);

    RuntimeFunc LookupRuntimeFunc(
        const std::string& name, AST::Ty& externTy, const std::vector<Ptr<AST::Ty>>& instTys, const AST::Expr& expr);
    OwnedPtr<AST::CallExpr> CreateRuntimeCall(const RuntimeFunc& func, std::vector<OwnedPtr<AST::Expr>> args,
        AST::Ty& externTy, const AST::Expr& expr, std::vector<Ptr<AST::Ty>> instTys = {});
    OwnedPtr<AST::CallExpr> CreateRuntimeCall(const std::string& name, std::vector<OwnedPtr<AST::Expr>> args,
        AST::Ty& externTy, const AST::Expr& expr, std::vector<Ptr<AST::Ty>> instTys = {});
    OwnedPtr<AST::Expr> CreateMemberAccessCall(OwnedPtr<AST::Expr> receiver, const std::string& field,
        AST::Ty& externTy, const AST::Expr& expr);
    OwnedPtr<AST::Expr> CreateFunctionCall(OwnedPtr<AST::Expr> callee, std::vector<OwnedPtr<AST::Expr>> args,
        AST::Ty& externTy, const AST::Expr& expr);

    TypeManager& typeManager;
    RuntimeFuncLookup lookup;
    CallCheck checkUnsafeInvoke;
    /** The number of enclosing unsafe blocks and unsafe declarations of the visited node. */
    unsigned unsafeDepth = 0;
    bool createdRuntimeCalls = false;
};
} // namespace Cangjie

#endif
