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
        for (auto typeArg : ty->TyArgs()) {
            CollectJavaTypes(typeArg, javaDecls);
        }
    }

    if (ty->IsCoreOptionType()) {
        CJC_ASSERT(ty->TyArgs().size() == 1);
        CollectJavaTypes(ty->TyArg(0), javaDecls);
    }

    if (auto decl = Ty::GetDeclOfTy(ty)) {
        if (decl->IsJavaMirror() || decl->IsJavaImpl()) {
            javaDecls.push_back(decl);
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
        CollectJavaTypes(typeArg->DataTy(), javaDecls);
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

    if (IsInstantiationWithJavaTypeAllowed(target->DataTy())) {
        return true;
    }
    if (target->outerDecl && IsInstantiationWithJavaTypeAllowed(target->outerDecl->DataTy())) {
        return true;
    }

    return false;
}

} // namespace

void JavaInteropManager::CheckGenericsInstantiation(Decl& decl)
{
    Walker(&decl, [this](Ptr<Node> node) -> VisitAction {
        if (auto nameRefExpr = DynamicCast<NameReferenceExpr>(node)) {
            if (!IsInstantiationWithJavaTypeAllowed(*nameRefExpr)) {
                CollectJavaTypesAndDiag(diag, *nameRefExpr);
            }
        } else if (auto refType = DynamicCast<RefType>(node)) {
            if (!IsInstantiationWithJavaTypeAllowed(refType->DataTy())) {
                CollectJavaTypesAndDiag(diag, *refType);
            }
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
}

} // namespace Cangjie::Interop::Java
