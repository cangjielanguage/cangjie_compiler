// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.
/**
 * @file
 *
 * This file implements the header file Promotion.h
 */
#include "Promotion.h"

#include "cangjie/AST/Types.h"
#include "cangjie/AST/ASTCasting.h"
#include "cangjie/Utils/Utils.h"

using namespace Cangjie;
using namespace AST;

// Given C<X> <: D<X>, Promote(C<Bool>, D<X>) will establish a substitution [X |-> [Bool]].
// Given C<X> <: D<X> & C<X> <: D<Int64>, Promote(C<Bool>, D<X>) will establish a substitution [X |-> [Bool, Int64]].
MultiTypeSubst Promotion::GetPromoteTypeMapping(DataTy from, DataTy target)
{
    if (!Ty::IsTyCorrect(from) || !Ty::IsTyCorrect(target)) {
        return {};
    }
    auto prRes = Promote(from, target);
    if (prRes.empty()) {
        return {};
    }

    MultiTypeSubst mts;

    Utils::EraseIf(prRes, [&target](auto& e) { return e->typeArgs.size() != target->typeArgs.size(); });
    std::for_each(prRes.begin(), prRes.end(), [&target, &mts](auto prResI) {
        for (size_t i = 0; i < target->typeArgs.size(); i++) {
            if (auto targetGenParam = DynamicCast<GenericsTy*>(target->typeArgs[i].Ty())) {
                mts[targetGenParam].emplace(prResI->typeArgs[i].Ty());
            }
        }
    });

    return mts;
}

MultiTypeSubst Promotion::GetDowngradeTypeMapping(DataTy target, DataTy upfrom)
{
    if (!Ty::IsTyCorrect(target) || !Ty::IsTyCorrect(upfrom)) {
        return {};
    }
    auto prRes = Promote(target, upfrom);
    if (prRes.empty()) {
        return {};
    }

    MultiTypeSubst mts;

    Utils::EraseIf(prRes, [&upfrom](auto& e) { return e->typeArgs.size() != upfrom->typeArgs.size(); });
    std::for_each(prRes.begin(), prRes.end(), [&upfrom, &mts](auto prResI) {
        for (size_t i = 0; i < prResI->typeArgs.size(); i++) {
            if (auto tv = DynamicCast<GenericsTy*>(prResI->typeArgs[i].Ty())) {
                mts[tv].emplace(upfrom->typeArgs[i].Ty());
            }
        }
    });

    return mts;
}

// Given C<X> <: D<X>, Promote(C<Bool>, D<_>) will give a promoted singleton set {D<Bool>}.
// Given C<X> <: D<X> & C<X> <: D<Int64>, Promote(C<Bool>, D<_>) will give a promoted set {D<Bool>, D<Int64>}
std::set<DataTy> Promotion::Promote(DataTy from, DataTy target)
{
    if (!Ty::IsTyCorrect(from) || !Ty::IsTyCorrect(target)) {
        return {};
    }
    // Promoting to 'target' type when any of the following conditions is met;
    // 1. 'from' is type of 'Nothing';
    // 2. 'target' is type of 'Any';
    // 3. 'from' is a type which meets CType constraints and 'target' is 'CType';
    // 4. 'from' is a type which meets JType constraints and 'target' is 'JType'.
    bool useTarget = from->IsNothing() || target->IsAny() || (target->IsCType() && from->IsMetCType());
    if (useTarget) {
        return {target};
    }
    if (from->IsPrimitive() && target->IsPrimitive()) {
        return PromoteHandleIdealTys(from, target);
    }
    if (from->IsFunc() && target->IsFunc()) {
        return PromoteHandleFunc(from, target);
    }
    if (from->IsTuple() && target->IsTuple()) {
        return PromoteHandleTuple(from, target);
    }
    if (auto res = PromoteHandleTyVar(from, target); !res.empty()) {
        return res;
    }
    return PromoteHandleNominal(from, target);
}

std::set<ModalTy> Promotion::Promote(ModalTy from, ModalTy target)
{
    CJC_ASSERT(from.Mode() == target.Mode() || tyMgr.ImplementsCopyInterface(target.Ty()) || target.IsDataType());
    auto r = Promote(from.Ty(), target.Ty());
    std::set<ModalTy> res;
    for (auto ty : r) {
        res.emplace(ty, from.Mode());
    }
    return res;
}

std::set<DataTy> Promotion::Downgrade(DataTy target, DataTy upfrom)
{
    auto dMaps = GetDowngradeTypeMapping(target, upfrom);
    if (dMaps.size() < target->typeArgs.size()) {
        return {};
    } else {
        return tyMgr.GetInstantiatedTys(target, dMaps);
    }
}

std::set<ModalTy> Promotion::Downgrade(ModalTy target, ModalTy upfrom)
{
    CJC_ASSERT(upfrom.Mode() == target.Mode() || tyMgr.ImplementsCopyInterface(target.Ty()));
    auto r = Downgrade(target.Ty(), upfrom.Ty());
    std::set<ModalTy> res;
    for (auto ty : r) {
        res.emplace(ty, target.Mode());
    }
    return res;
}

std::set<DataTy> Promotion::PromoteHandleTyVar(DataTy from, DataTy target)
{
    if (!from->IsGeneric()) {
        return {};
    }
    auto genericTy = RawStaticCast<GenericsTy*>(from.get());
    for (auto& ub : genericTy->upperBounds) {
        if (auto res = Promote(ub, target); !res.empty()) {
            return res;
        }
    }
    return {};
}

std::set<DataTy> Promotion::PromoteHandleIdealTys(DataTy from, DataTy target) const
{
    // Caller guarantees the 'from' and 'target' are primitive type.
    if (from->kind == target->kind) {
        return {from};
    }
    // Very ad hoc fix. Should be improved in the future.
    if (from->kind == TypeKind::TYPE_IDEAL_INT && target->IsInteger()) {
        return {target};
    } else if (from->kind == TypeKind::TYPE_IDEAL_FLOAT && target->IsFloating()) {
        return {target};
    } else {
        return {};
    }
}

std::set<DataTy> Promotion::PromoteHandleFunc(DataTy from, DataTy target)
{
    if (tyMgr.IsTyEqual(from, target)) {
        return {from};
    } else {
        return {};
    }
}

std::set<DataTy> Promotion::PromoteHandleTuple(DataTy from, DataTy target)
{
    if (tyMgr.IsTyEqual(from, target)) {
        return {from};
    } else {
        return {};
    }
}

std::set<DataTy> Promotion::PromoteHandleNominal(DataTy from, DataTy target)
{
    if (Ty::GetDeclPtrOfTy<InheritableDecl>(from) == Ty::GetDeclPtrOfTy<InheritableDecl>(target)) {
        return {from};
    }

    TypeSubst typeMapping;
    auto emplaceElem = [&from, &typeMapping](const std::vector<ModalTy>& tyVars) {
        if (tyVars.size() != from->typeArgs.size()) {
            return;
        }
        for (size_t i = 0; i < tyVars.size(); i++) {
            typeMapping.emplace(StaticCast<GenericsTy*>(tyVars[i].Ty()), from->typeArgs[i].Ty());
        }
    };
    if (from->IsClass()) {
        auto classTy = RawStaticCast<ClassTy*>(from);
        CJC_ASSERT(classTy->decl);
        if (classTy->decl->GetTy()) {
            emplaceElem(classTy->declPtr->GetTy()->typeArgs);
        }
    } else if (from->IsInterface()) {
        auto interfaceTy = RawStaticCast<InterfaceTy*>(from);
        CJC_ASSERT(interfaceTy->decl);
        if (interfaceTy->decl->GetTy()) {
            emplaceElem(interfaceTy->declPtr->GetTy()->typeArgs);
        }
    } else if (from->IsStruct()) {
        auto structTy = RawStaticCast<StructTy*>(from);
        CJC_ASSERT(structTy->decl);
        if (structTy->decl->GetTy()) {
            emplaceElem(structTy->declPtr->GetTy()->typeArgs);
        }
    } else if (from->IsEnum()) {
        auto enumTy = RawStaticCast<EnumTy*>(from);
        CJC_ASSERT(enumTy->decl);
        if (enumTy->decl->GetTy()) {
            emplaceElem(enumTy->declPtr->GetTy()->typeArgs);
        }
    }

    std::set<DataTy> res;
    auto supTys = tyMgr.GetAllSuperTys(*from, typeMapping);
    for (auto ty : supTys) {
        auto cly = DynamicCast<ClassLikeTy*>(ty);
        if (cly && Ty::GetDeclPtrOfTy(ModalTy{cly}) == Ty::GetDeclPtrOfTy(target)) {
            res.insert(cly);
        }
    }
    return res;
}
