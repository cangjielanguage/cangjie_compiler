// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "JavaInteropManager.h"

#include "DiagsInterop.h"
#include "Utils.h"

#include "cangjie/AST/Utils.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Utils/Utils.h"

using namespace Cangjie;
using namespace AST;
using namespace Cangjie::Interop::Java;

namespace Cangjie::Interop::Java {

namespace {

void CollectJavaTypes(Ptr<Ty> ty, std::vector<Ptr<Decl>>& javaDecls)
{
    if (ty->IsTuple()) {
        for (auto typeArg : ty->typeArgs) {
            CollectJavaTypes(typeArg, javaDecls);
        }
    }

    if (ty->IsCoreOptionType()) {
        CJC_ASSERT(ty->typeArgs.size() == 1);
        CollectJavaTypes(ty->typeArgs[0], javaDecls);
    }

    if (auto decl = Ty::GetDeclOfTy(ty)) {
        if (IsMirror(*decl) || IsImpl(*decl)) {
            javaDecls.push_back(decl);
        }
    }
}

void CollectJavaTypesAndDiag(DiagnosticEngine& diag, const Decl& decl, Ptr<Decl> nonJavaOuterDecl = nullptr)
{
    std::vector<Ptr<Decl>> javaDecls;
    CollectJavaTypes(decl.ty, javaDecls);
    DiagUsageOfJavaTypes(diag, decl, std::move(javaDecls), nonJavaOuterDecl);
}

void CheckMemberDeclsUseJavaTypes(DiagnosticEngine& diag, Decl& decl)
{
    for (auto& member : decl.GetMemberDecls()) {
        if (auto memberVarDecl = DynamicCast<VarDecl>(member.get())) {
            CollectJavaTypesAndDiag(diag, *memberVarDecl, &decl);
        }
    }
}

void CheckEnumConstructorsUseJavaTypes(DiagnosticEngine& diag, EnumDecl& enumDecl)
{
    for (auto& enumCtr : enumDecl.constructors) {
        if (auto funcEnumCtr = DynamicCast<FuncDecl>(enumCtr.get())) {
            for (auto& argDecl : funcEnumCtr->funcBody->paramLists[0]->params) {
                CollectJavaTypesAndDiag(diag, *argDecl, &enumDecl);
            }
        }
    }
}

void CollectJavaTypesAndDiag(DiagnosticEngine& diag, const NameReferenceExpr& expr)
{
    std::vector<Ptr<Decl>> javaDecls;
    for (auto ty : expr.instTys) {
        CollectJavaTypes(ty, javaDecls);
    }
    DiagJavaTypesAsGenericParam(diag, expr, std::move(javaDecls));
}

void CollectJavaTypesAndDiag(DiagnosticEngine& diag, const RefType& type)
{
    std::vector<Ptr<Decl>> javaDecls;
    for (auto& typeArg : type.typeArguments) {
        CollectJavaTypes(typeArg->ty, javaDecls);
    }
    DiagJavaTypesAsGenericParam(diag, type, std::move(javaDecls));
}

bool IsInstantiationWithJavaTypeAllowed(Ptr<Ty> ty)
{
    return ty->IsCoreOptionType() || IsJArray(*ty);
}

bool IsInstantiationWithJavaTypeAllowed(NameReferenceExpr& expr)
{
    auto target = expr.GetTarget();
    if (!target) {
        return true;
    }

    if (IsInstantiationWithJavaTypeAllowed(target->ty)) {
        return true;
    }
    if (target->outerDecl && IsInstantiationWithJavaTypeAllowed(target->outerDecl->ty)) {
        return true;
    }

    return false;
}

} // namespace

void JavaInteropManager::CheckUsageOfJavaTypes(Decl& decl)
{
    switch (decl.astKind) {
        case ASTKind::VAR_DECL:
        case ASTKind::VAR_WITH_PATTERN_DECL: {
            CollectJavaTypesAndDiag(diag, decl);
            return;
        }
        case ASTKind::CLASS_DECL: {
            if (IsMirror(decl) || IsImpl(decl)) {
                return;
            }
            CheckMemberDeclsUseJavaTypes(diag, decl);
            return;
        }
        case ASTKind::STRUCT_DECL: {
            CheckMemberDeclsUseJavaTypes(diag, decl);
            return;
        }
        case ASTKind::ENUM_DECL: {
            CheckEnumConstructorsUseJavaTypes(diag, *StaticCast<EnumDecl>(&decl));
            return;
        }
        default:
            return;
    }
}

void JavaInteropManager::CheckGenericsInstantiation(Decl& decl)
{
    Walker(&decl, [this](Ptr<Node> node) -> VisitAction {
        if (auto nameRefExpr = DynamicCast<NameReferenceExpr>(node)) {
            if (!IsInstantiationWithJavaTypeAllowed(*nameRefExpr)) {
                CollectJavaTypesAndDiag(diag, *nameRefExpr);
            }
        } else if (auto refType = DynamicCast<RefType>(node)) {
            if (!IsInstantiationWithJavaTypeAllowed(refType->ty)) {
                CollectJavaTypesAndDiag(diag, *refType);
            }
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
}

} // namespace Cangjie::Interop::Java
