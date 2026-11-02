// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the EnumSugarTargetFiner class.
 */

#include "EnumSugarTargetsFinder.h"

#include <vector>

#include "TypeCheckUtil.h"

#include "cangjie/AST/ASTContext.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Utils/FileUtil.h"
#include "cangjie/Utils/Utils.h"

using namespace Cangjie;
using namespace AST;
using namespace TypeCheckUtil;

std::vector<Ptr<Decl>> EnumSugarTargetsFinder::FindEnumSugarTargets()
{
    if (refExpr.TestAttr(Attribute::MACRO_INVOKE_BODY)) {
        return {};
    }
    // 'Lookup' is only able to found target from exactly one enum decl.
    if (IsAllFuncDecl(enumSugarTargets)) {
        // No shadow happened if targets are all funcDecl, need to re-find all target globally.
        enumSugarTargets.clear();
    } else {
        RefineTargets();
    }
    if (enumSugarTargets.empty()) {
        size_t argSize = refExpr.OuterArgSize();
        auto decls = ctx.FindEnumConstructor(refExpr.ref.identifier, argSize);
        if (argSize != 0) { // for `()` operator overloading
            auto& varDecls = ctx.FindEnumConstructor(refExpr.ref.identifier, 0);
            decls.insert(decls.end(), varDecls.cbegin(), varDecls.cend());
        }
        Utils::EraseIf(decls, [this](auto decl) {
            CJC_NULLPTR_CHECK(decl);
            auto& ed = *StaticCast<EnumDecl*>(decl->outerDecl);
            // Filter out the `enum`s that mismatch the number of type arguments.
            return !refExpr.typeArguments.empty() &&
                (ed.generic == nullptr || (refExpr.typeArguments.size() != ed.generic->typeParameters.size()));
        });
        if (refExpr.TestAttr(Attribute::IN_CORE)) {
            std::copy_if(decls.cbegin(), decls.cend(), std::back_inserter(enumSugarTargets),
                [](auto decl) { return decl->fullPackageName == CORE_PACKAGE_NAME; });
        } else {
            // Only keep toplevel `enum`s if `refExpr` doesn't have target ty.
            std::copy_if(decls.cbegin(), decls.cend(), std::back_inserter(enumSugarTargets),
                [this](auto decl) { return ctx.HasTargetTy(&refExpr) || decl->IsSamePackage(refExpr); });
            // If toplevel is empty, keep the imported `enum`s.
            if (enumSugarTargets.empty()) {
                enumSugarTargets = decls;
            }
        }
        RefineTargets();
    }
    auto it = std::unique(enumSugarTargets.begin(), enumSugarTargets.end());
    enumSugarTargets.resize(static_cast<size_t>(std::distance(enumSugarTargets.begin(), it)));
    std::sort(enumSugarTargets.begin(), enumSugarTargets.end(), CmpNodeByPos());
    return enumSugarTargets;
}

void EnumSugarTargetsFinder::RefineTargets()
{
    if (!ctx.HasTargetTy(&refExpr) || refExpr.callOrPattern != nullptr) {
        return;
    }
    std::vector<Ptr<AST::Decl>> inCandidates = enumSugarTargets;
    for (auto it = enumSugarTargets.begin(); it != enumSugarTargets.end();) {
        auto targetTy = ctx.targetTypeMap[&refExpr];
        if (auto refinedTargetTy = RefineTargetTy(tyMgr, targetTy, *it)) {
            ctx.targetTypeMap[&refExpr] = *refinedTargetTy;
            ++it;
        } else {
            it = enumSugarTargets.erase(it);
        }
    }
    // If there is no target left after refining, restore targets in current package,
    // OR if targets are all imported, retore all of them.
    if (enumSugarTargets.empty()) {
        bool hasTargetInCurrentPkg =
            Utils::In(inCandidates, [](auto it) { return it && !it->TestAttr(Attribute::IMPORTED); });
        if (hasTargetInCurrentPkg) {
            std::copy_if(inCandidates.begin(), inCandidates.end(), std::back_inserter(enumSugarTargets),
                [](auto it) { return it && !it->TestAttr(Attribute::IMPORTED); });
        } else {
            enumSugarTargets = inCandidates;
        }
    }
}

// Get real enum type of the given target. Only return value with valid type.
std::optional<AST::ModalTy> EnumSugarTargetsFinder::RefineTargetTy(
    TypeManager& typeManager, ModalTy targetTy, Ptr<const Decl> target)
{
    if (!target || !targetTy) {
        return {};
    }
    ModalTy currentTy = targetTy;
    while (currentTy != nullptr && currentTy->kind == TypeKind::TYPE_ENUM) {
        auto targetEnumTy = RawStaticCast<EnumTy*>(currentTy.Ty());
        if (targetEnumTy->declPtr == target->outerDecl) {
            return currentTy;
        }
        // Option type allow type auto box.
        if (targetEnumTy->IsCoreOptionType()) {
            currentTy = targetEnumTy->typeArgs[0];
        } else {
            return {};
        }
    }
    CJC_ASSERT(target->outerDecl);
    // When target type is enum implemented interface type, directly return current enum type.
    if (auto currentInterfaceTy = DynamicCast<InterfaceTy*>(currentTy.Ty());
        currentInterfaceTy && target->outerDecl->GetTy()) {
        auto allInterfaceTys = typeManager.GetAllSuperTys(*target->outerDecl->GetTy());
        if (allInterfaceTys.count(currentInterfaceTy) > 0) {
            return target->outerDecl->GetTy();
        }
        for (auto ty : allInterfaceTys) {
            if (auto iTy = DynamicCast<InterfaceTy*>(ty); iTy && iTy->declPtr == currentInterfaceTy->declPtr) {
                return target->outerDecl->GetTy();
            }
        }
    }
    return {};
}
