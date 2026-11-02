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

#include "InstantiatedExtendRecorder.h"

#include "ExtendBoxMarker.h"
#include "ImplUtils.h"
#include "OverrideFunctionResolver.h"
#include "TypeCheckUtil.h"

#include "cangjie/AST/Match.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Utils/Utils.h"

using namespace Cangjie;
using namespace AST;

GenericInstantiationManager::InstantiatedExtendRecorder::InstantiatedExtendRecorder(
    GenericInstantiationManagerImpl& gim, TypeManager& tyMgr)
    : gim(gim), typeManager(tyMgr), promotion(tyMgr), recorderId(AST::Walker::GetNextWalkerID())
{
    extendRecorder = [this](auto node) { return node ? RecordUsedExtendDecl(*node) : VisitAction::SKIP_CHILDREN; };
}

void GenericInstantiationManager::InstantiatedExtendRecorder::operator()(AST::Node& node)
{
    auto process = [this](auto node) {
        // Do not consider boxed extends.
        // Walk node to record used extends as function call.
        // eg: 1. 'obj.extendFunction' -> collect extend decl which defined the 'extendFunction'.
        //     2. func test<T>(a: T) where T <: I { a.interfaceFunction }
        //        collect extend decl of type T <: I which implement the 'interfaceFunction'.
        Walker(node, recorderId, extendRecorder, gim.contextReset).Walk();
    };
    if (auto pkg = DynamicCast<Package*>(&node); pkg) {
        for (auto& it : pkg->files) {
            process(it.get());
        }
        for (auto srcFunc : pkg->srcImportedNonGenericDecls) {
            process(srcFunc);
        }
    } else {
        process(&node);
    }
}

VisitAction GenericInstantiationManager::InstantiatedExtendRecorder::RecordUsedExtendDecl(Node& node)
{
    VisitAction action = gim.CheckVisitedNode(&node);
    if (action != VisitAction::WALK_CHILDREN) {
        return action;
    }

    if (auto expr = DynamicCast<Expr*>(&node); expr && expr->desugarExpr) {
        Walker(expr->desugarExpr.get(), recorderId, extendRecorder, gim.contextReset).Walk();
        return VisitAction::SKIP_CHILDREN;
    }

    switch (node.astKind) {
        case ASTKind::REF_EXPR:
            RecordExtendForRefExpr(*StaticAs<ASTKind::REF_EXPR>(&node));
            break;
        case ASTKind::MEMBER_ACCESS:
            RecordExtendForMemberAccess(*StaticAs<ASTKind::MEMBER_ACCESS>(&node));
            break;
        default:
            break;
    }
    return VisitAction::WALK_CHILDREN;
}

/**
 * For the case: 'extendFunc()' directly called in structDecl, record extendDecl which defined extendFunc.
 */
void GenericInstantiationManager::InstantiatedExtendRecorder::RecordExtendForRefExpr(const RefExpr& re)
{
    bool ignored = !re.GetTy().IsCorrect() || !re.ref.target || re.ref.target->IsBuiltIn() ||
        !re.ref.target->TestAttr(Attribute::IN_EXTEND);
    if (ignored || gim.structContext.empty()) {
        return;
    }
    auto extend = re.ref.target->outerDecl;
    CJC_ASSERT(extend && extend->astKind == ASTKind::EXTEND_DECL);
    auto structDecl = gim.GetStructDeclByContext();
    CJC_ASSERT(structDecl);
    auto baseTy = GetDeclTy(*structDecl);
    bool invalid = !Ty::IsTyCorrect(baseTy) || !extend->GetTy().IsCorrect() || baseTy->HasGeneric();
    if (invalid) {
        return;
    }
    auto promoteRes = promotion.Promote(baseTy, extend->GetTy());
    if (promoteRes.empty()) {
        InternalError("generic instantiation failed");
        return;
    }
    auto promotedTy = *promoteRes.begin();
    typeManager.RecordUsedGenericExtend(*promotedTy, RawStaticCast<ExtendDecl*>(extend));
}

/**
 * For cases:
 * 1.  'obj.extendFunc', record extendDecl which defined extendFunc.
 * 2.  'obj.interfaceFunc' which belong to a generic definition, eg:
 *        func test<T>(a: T) where T <: ToString { a.toString() }
 *      'toString' may be rearraged to extend function after rearrange step in generic instantiation.
 */
void GenericInstantiationManager::InstantiatedExtendRecorder::RecordExtendForMemberAccess(const MemberAccess& ma)
{
    auto ignored = !ma.target || !ma.baseExpr || !ma.baseExpr->GetTy().IsCorrect() ||
        ma.baseExpr->GetTy()->HasGeneric() || !ma.target->outerDecl;
    if (ignored) {
        return;
    }
    auto outerDecl = ma.target->outerDecl;
    if (auto fd = DynamicCast<FuncDecl*>(ma.target); fd && outerDecl->astKind == ASTKind::INTERFACE_DECL) {
        RecordImplExtendDecl(ma.baseExpr->GetTy(), *fd, ma.matchedParentTy);
    } else if (outerDecl->astKind == ASTKind::EXTEND_DECL) {
        auto promoteRes = promotion.Promote(ma.baseExpr->GetTy(), outerDecl->GetTy());
        if (promoteRes.empty()) {
            InternalError("generic instantiation failed");
            return;
        }
        auto promotedTy = *promoteRes.begin();
        typeManager.RecordUsedGenericExtend(*promotedTy, RawStaticCast<ExtendDecl*>(outerDecl));
    }
}

void GenericInstantiationManager::InstantiatedExtendRecorder::RecordImplExtendDecl(
    ModalTy ty, FuncDecl& fd, ModalTy upperTy)
{
    OverrideFunctionResolver resolver(typeManager);
    MemberFuncsWithInstTys funcs = resolver.GetInstMemberFuncWithInstTy(ty.Ty(), fd.identifier);
    Ptr<Decl> extend = nullptr;
    // All candidates have satisfied functions, choose most matched decl.
    auto baseTy = Ty::IsTyCorrect(upperTy) ? upperTy : ty;
    for (MemberFuncWithInstTys func : funcs) {
        auto matchedInstTy = resolver.GetMatchedFuncInstTyByGivenTarget(func, fd, baseTy.Ty());
        if (!Ty::IsTyCorrect(matchedInstTy)) {
            continue;
        }
        auto baseDecl = func.first->outerDecl;
        CJC_ASSERT(baseDecl);
        if (baseDecl->astKind != ASTKind::EXTEND_DECL) {
            continue; // Function implemented in origin decl, ignored.
        }
        if (Ty::IsTyCorrect(upperTy)) {
            auto& inheritedTys = RawStaticCast<InheritableDecl*>(baseDecl)->inheritedTypes;
            bool isImplemented = std::any_of(inheritedTys.begin(), inheritedTys.end(),
                [this, upperTy](auto& type) { return typeManager.IsSubtype(type->GetTy(), upperTy); });
            if (isImplemented) {
                // If interface implementation check passed, store and break;
                extend = baseDecl;
                break;
            }
        }
        // If 'upperTy' check ignored or failed, set decl and continue if exist next match.
        extend = baseDecl;
    }
    if (extend) {
        typeManager.RecordUsedGenericExtend(*ty, RawStaticCast<ExtendDecl*>(extend));
    }
}
