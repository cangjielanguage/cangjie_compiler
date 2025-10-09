// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the EnumSugarChecker class.
 */

#include "EnumSugarChecker.h"

#include "Diags.h"
#include "TypeCheckUtil.h"

using namespace Cangjie;
using namespace TypeCheckUtil;
using namespace AST;
using namespace Sema;

bool TypeChecker::EnumSugarChecker::CheckVarDeclTargets()
{
    // Type for FuncDecls are inferred and decided later.
    // Enum member without parameter is not supported to do type inference.
    std::vector<Ptr<Decl>> varDeclTargets;
    for (auto& target : enumSugarTargets) {
        if (target->astKind == ASTKind::VAR_DECL && !target->TestAttr(Attribute::COMMON)) {
            varDeclTargets.push_back(target);
        }
    }
    if (varDeclTargets.size() > 1) {
        DiagnosticBuilder diagBuilder = ctx.diag.Diagnose(
            refExpr, DiagKind::sema_multiple_constructor_in_enum, varDeclTargets[0]->identifier.Val());
        std::sort(varDeclTargets.begin(), varDeclTargets.end(),
            [](Ptr<const Decl> a, Ptr<const Decl> b) { return a->begin < b->begin; });
        for (auto& it : varDeclTargets) {
            diagBuilder.AddNote(*it, DiagKind::sema_found_candidate_decl);
        }
        refExpr.ty = TypeManager::GetInvalidTy();
        return false;
    }
    return true;
}

void TypeChecker::EnumSugarChecker::CheckGenericEnumSugarWithTypeArgs(Ptr<EnumDecl> ed)
{
    if (!ed) {
        enumSugarTargets.clear();
        return;
    }
    UpdateInstTysWithTypeArgs(refExpr);
    // Check generic constraint.
    if (!typeChecker.CheckGenericDeclInstantiation(ed, refExpr.GetTypeArgs(), refExpr)) {
        enumSugarTargets.clear();
        return;
    }
    // Build generic type mapping.
    TypeSubst typeMapping;
    // ed->generic is guaranteed to be not null because of CheckGenericDeclInstantiation invoked before.
    if (ed->generic->typeParameters.size() != refExpr.typeArguments.size()) {
        return;
    }
    for (size_t i = 0; i < refExpr.typeArguments.size(); ++i) {
        typeMapping[StaticCast<GenericsTy*>(ed->generic->typeParameters[i]->ty)] = refExpr.typeArguments[i]->ty;
    }
    refExpr.ty = typeChecker.typeManager.GetInstantiatedTy(refExpr.ty, typeMapping);
}

void TypeChecker::EnumSugarChecker::CheckGenericEnumSugarWithoutTypeArgs(Ptr<const EnumDecl> ed)
{
    auto argSize = refExpr.OuterArgSize();
    auto foundTargetType = ctx.targetTypeMap.find(&refExpr);
    bool referenceNeedTypeInfer = (foundTargetType == ctx.targetTypeMap.end() || foundTargetType->second == nullptr) &&
        !refExpr.isInFlowExpr;
    if (ed && ed->generic && Is<VarDecl>(enumSugarTargets[0]) && referenceNeedTypeInfer && argSize == 0) {
        ctx.diag.Diagnose(refExpr, DiagKind::sema_generic_type_without_type_argument);
        enumSugarTargets.clear();
    }
}

Ptr<Decl> TypeChecker::EnumSugarChecker::CheckEnumSugarTargets()
{
    auto it = std::find_if(enumSugarTargets.cbegin(), enumSugarTargets.cend(),
        [](Ptr<const Decl> decl) { return decl->astKind == ASTKind::VAR_DECL; });
    Ptr<Decl> target = it != enumSugarTargets.cend() ? *it : enumSugarTargets.front();
    refExpr.ty = target->ty;
    // Handle generic enum field sugar like: .century<Int32>.
    auto ed = DynamicCast<EnumDecl*>(target->outerDecl);
    if (refExpr.typeArguments.empty()) {
        CheckGenericEnumSugarWithoutTypeArgs(ed);
    } else {
        CheckGenericEnumSugarWithTypeArgs(ed);
    }
    return target;
}

std::pair<bool, std::vector<Ptr<Decl>>> TypeChecker::EnumSugarChecker::Resolve()
{
    enumSugarTargets = enumSugarTargetsFinder.FindEnumSugarTargets();
    if (enumSugarTargets.empty()) {
        return {false, {}};
    } else if (!CheckVarDeclTargets()) {
        return {true, {}};
    }

    Ptr<Decl> target = CheckEnumSugarTargets();
    ModifyTargetOfRef(refExpr, target, enumSugarTargets);
    return {true, enumSugarTargets};
}
