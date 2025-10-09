// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements internal type use in public decl.
 */

#include "TypeCheckerImpl.h"

#include "Diags.h"

#include "TypeCheckUtil.h"

namespace Cangjie {
using namespace AST;
using namespace Sema;
using namespace TypeCheckUtil;

namespace {
std::pair<Ptr<Decl>, bool> IsAccessible(Ptr<const AST::Ty> type, AccessLevel srcLevel)
{
    if (!Ty::IsTyCorrect(type)) {
        return {nullptr, true};
    }
    for (const auto& ty : std::as_const(type->typeArgs)) {
        if (!Ty::IsTyCorrect(ty)) {
            continue;
        }
        if (auto [decl, accessible] = IsAccessible(ty, srcLevel); !accessible) {
            return {decl, false};
        }
    }
    if (type->IsNominal()) {
        auto decl = Ty::GetDeclPtrOfTy(type);
        if (decl && !IsCompatibleAccessLevel(srcLevel, GetAccessLevel(*decl))) {
            return {decl, false};
        }
    }
    return {nullptr, true};
}

void CollectGenericTyAccessibility(const AST::Decl& decl, std::vector<std::pair<AST::Node&, AST::Decl&>>& limitedDecls)
{
    auto generic = decl.GetGeneric();
    if (!generic) {
        return;
    }
    auto declLevel = GetAccessLevel(decl);
    for (auto& it : generic->genericConstraints) {
        for (auto& upperBound : it->upperBounds) {
            if (!upperBound->ty) {
                continue;
            }
            if (auto [ubDecl, accessible] = IsAccessible(upperBound->ty, declLevel); !accessible) {
                (void)limitedDecls.emplace_back(*upperBound, *ubDecl);
            }
        }
    }
}
} // namespace

void TypeChecker::TypeCheckerImpl::CheckAccessLevelValidity(Package& package)
{
    for (auto& file : package.files) {
        for (auto& decl : file->decls) {
            CJC_ASSERT(decl);
            if (decl->TestAttr(Attribute::PRIVATE)) {
                continue;
            }
            if (decl->TestAttr(Attribute::FROM_COMMON_PART)) {
                continue;
            }
            CheckNonPrivateDeclAccessLevelValidity(*decl);
        }
    }
}

void TypeChecker::TypeCheckerImpl::CheckNonPrivateDeclAccessLevelValidity(Decl& decl)
{
    if (!Ty::IsTyCorrect(decl.ty)) {
        return;
    }
    if (auto id = DynamicCast<InheritableDecl>(&decl)) {
        CheckNominalDeclAccessLevelValidity(*id);
    } else if (auto fd = DynamicCast<FuncDecl>(&decl)) {
        CheckFuncAccessLevelValidity(*fd);
    } else if (auto vpd = DynamicCast<VarWithPatternDecl>(&decl)) {
        CheckPatternVarAccessLevelValidity(*vpd->irrefutablePattern);
    } else if (auto tad = DynamicCast<TypeAliasDecl>(&decl)) {
        std::vector<std::pair<Node&, Decl&>> limitedDecls;
        CJC_NULLPTR_CHECK(tad->type);
        if (auto [inDecl, accessible] = IsAccessible(tad->type->ty, GetAccessLevel(*tad)); !accessible) {
            (void)limitedDecls.emplace_back(*tad->type, *inDecl);
        }
        CollectGenericTyAccessibility(*tad, limitedDecls);
        DiagLowerAccessLevelTypesUse(diag, *tad, limitedDecls);
    } else if (auto pd = DynamicCast<PropDecl>(&decl)) {
        CJC_NULLPTR_CHECK(pd->type);
        if (auto [inDecl, accessible] = IsAccessible(pd->ty, GetAccessLevel(*pd)); !accessible) {
            std::vector<std::pair<Node&, Decl&>> limitedDecls;
            (void)limitedDecls.emplace_back(*pd->type, *inDecl);
            DiagLowerAccessLevelTypesUse(diag, *pd, limitedDecls);
        }
    } else if (auto vd = DynamicCast<VarDecl>(&decl)) {
        auto [inDecl, accessible] = IsAccessible(vd->ty, GetAccessLevel(*vd));
        if (accessible) {
            return;
        }
        std::vector<std::pair<Node&, Decl&>> limitedDecls;
        if (vd->type) {
            (void)limitedDecls.emplace_back(*vd->type, *inDecl);
            DiagLowerAccessLevelTypesUse(diag, *vd, limitedDecls);
        } else {
            // The type of variable is obtained by inference.
            DiagLowerAccessLevelTypesUse(diag, *vd, limitedDecls, {inDecl});
        }
    }
}

void TypeChecker::TypeCheckerImpl::CheckNominalDeclAccessLevelValidity(const InheritableDecl& id)
{
    if (id.astKind == AST::ASTKind::EXTEND_DECL) {
        return;
    }
    std::vector<std::pair<Node&, Decl&>> limitedDecls;
    CollectGenericTyAccessibility(id, limitedDecls);
    DiagLowerAccessLevelTypesUse(diag, id, limitedDecls);
    for (auto& it : id.GetMemberDeclPtrs()) {
        CJC_NULLPTR_CHECK(it);
        if (!(it->TestAttr(Attribute::PRIVATE)) && !(it->TestAttr(Attribute::FROM_COMMON_PART))) {
            CheckNonPrivateDeclAccessLevelValidity(*it);
        }
    }
}

void TypeChecker::TypeCheckerImpl::CheckFuncAccessLevelValidity(const FuncDecl& fd)
{
    CJC_NULLPTR_CHECK(fd.funcBody);
    std::vector<std::pair<Node&, Decl&>> limitedDecls;
    std::vector<Ptr<Decl>> hintDecls;
    if (fd.funcBody->retType) {
        if (auto [decl, accessible] = IsAccessible(fd.funcBody->retType->ty, GetAccessLevel(fd));
            !accessible) {
            if (!fd.funcBody->retType->TestAttr(Attribute::COMPILER_ADD)) {
                (void)limitedDecls.emplace_back(*fd.funcBody->retType, *decl);
            } else {
                // The type of function return type is obtained by inference.
                (void)hintDecls.emplace_back(decl);
            }
        }
    }
    for (auto& param : (*fd.funcBody->paramLists[0]).params) {
        if (fd.TestAttr(Attribute::FROM_COMMON_PART)) {
            continue;
        }
        CJC_ASSERT(param && param->type);
        if (auto [inDecl, accessible] = IsAccessible(param->ty, GetAccessLevel(fd)); !accessible) {
            (void)limitedDecls.emplace_back(*param->type, *inDecl);
        }
    }
    CollectGenericTyAccessibility(fd, limitedDecls);
    DiagLowerAccessLevelTypesUse(diag, fd, limitedDecls, hintDecls);
}

void TypeChecker::TypeCheckerImpl::CheckPatternVarAccessLevelValidity(AST::Pattern& pattern)
{
    std::vector<std::pair<Node&, Decl&>> limitedDecls;
    Walker(&pattern, [&limitedDecls](Ptr<Node> node) -> VisitAction {
        if (auto vd = DynamicCast<VarDecl>(node)) {
            if (auto [inDecl, accessible] = IsAccessible(vd->ty, GetAccessLevel(*vd)); !accessible) {
                (void)limitedDecls.emplace_back(*vd, *inDecl);
            }
            return VisitAction::SKIP_CHILDREN;
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    DiagPatternInternalTypesUse(diag, limitedDecls);
}
} // namespace Cangjie
