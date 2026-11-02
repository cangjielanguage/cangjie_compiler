// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file defines functions for collecting type constraints recursively. This process
 * is called assumption.
 */

#include "TypeCheckerImpl.h"

#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"

using namespace Cangjie;
using namespace AST;

namespace {
/** Add the subtype relation subTy <: upperBoundTy to the type constraint collection @p typeConstraintCollection. */
void AddConstraint(TyVarEnv& typeConstraintCollection, DataTy subTy, DataTy upperBoundTy)
{
    if (!subTy->IsGeneric()) {
        return;
    }
    Ptr<TyVar> subGen = RawStaticCast<GenericsTy*>(subTy);
    subGen->upperBounds.insert(upperBoundTy);
    auto found = typeConstraintCollection.find(subGen);
    if (found == typeConstraintCollection.end()) {
        typeConstraintCollection.emplace(subGen, std::set<ModalTy>{{upperBoundTy}});
    } else {
        found->second.insert({upperBoundTy});
    }
}

/** Check whether the type constraint collection @p typeConstraintCollection has subtype relashion: subTy <: baseTy. */
bool LookUpConstraintCollection(DataTy subTy, DataTy baseTy, const TyVarEnv& typeConstraintCollection)
{
    if (subTy == baseTy) {
        return true;
    }
    if (!subTy->IsGeneric()) {
        return false;
    }
    auto found = typeConstraintCollection.find(StaticCast<TyVar>(subTy));
    // If the subTy cannot be found in typeConstraintCollection, return false.
    if (found == typeConstraintCollection.end()) {
        return false;
    }
    return found->second.find({baseTy}) != found->second.end();
}

/**
 * Checking Whether the Upper Bound of Generics Has InvalidTy.
 */
bool IsUpperBoundsValid(const std::vector<OwnedPtr<Type>>& upperBounds)
{
    for (auto& upper : upperBounds) {
        if (!upper->GetTy().IsCorrect()) {
            return false;
        }
    }
    return true;
}
} // namespace

void TypeChecker::TypeCheckerImpl::PerformAssumeReferenceTypeUpperBound(TyVarEnv& typeConstraintCollection,
    GCBlames& blames, const Type& referenceTypeUpperBound, const TypeSubst& typeMapping)
{
    ModalTy upperBoundTy = referenceTypeUpperBound.GetTy();
    Ptr<Decl> baseDecl = Ty::GetDeclPtrOfTy(upperBoundTy.Ty());
    // If the upperBound is a generic Type and has associate declaration, perform assumption recursively.
    if (baseDecl != nullptr && Ty::IsTyCorrect(upperBoundTy) && upperBoundTy->HasGeneric()) {
        // 1. Create substitute Map between generic tys of upperBound's decl and current uppBound's tys which
        // can be generic or not.
        TypeSubst substituteMap = typeManager.GetSubstituteMapping(*upperBoundTy, typeMapping);
        // 2. Perform assumption recursively.
        Assumption(typeConstraintCollection, blames, *baseDecl, substituteMap);
    }
}

void TypeChecker::TypeCheckerImpl::AssumeOneUpperBound(
    TyVarEnv& typeConstraintCollection, GCBlames& blames, const Type& upperBound, const TypeSubst& typeMapping)
{
    switch (upperBound.astKind) {
        case ASTKind::REF_TYPE:
        case ASTKind::QUALIFIED_TYPE: {
            PerformAssumeReferenceTypeUpperBound(typeConstraintCollection, blames, upperBound, typeMapping);
            break;
        }
        default:
            break;
    }
}

void TypeChecker::TypeCheckerImpl::PerformAssumptionForOneGenericConstraint(
    TyVarEnv& typeConstraintCollection, GCBlames& blames, const GenericConstraint& gc, const TypeSubst& typeMapping)
{
    auto subTypeTy = gc.type->DataTy();
    if (!Ty::IsTyCorrect(subTypeTy)) {
        return;
    }
    auto subTy = typeManager.GetInstantiatedTy(subTypeTy, typeMapping);
    for (const auto& upperBound : gc.upperBounds) {
        if (!upperBound) {
            continue;
        }
        auto upperBoundTy = upperBound->DataTy();
        if (!Ty::IsTyCorrect(upperBoundTy)) {
            continue;
        }
        auto baseTy = typeManager.GetInstantiatedTy(upperBoundTy, typeMapping);
        // If the constraint is already exist in typeConstraintCollection, no need to do assumption recursively.
        if (!subTy->IsGeneric() || LookUpConstraintCollection(subTy, baseTy, typeConstraintCollection)) {
            continue;
        }
        // Add the constraint to the typeConstraintCollection.
        AddConstraint(typeConstraintCollection, subTy, baseTy);
        blames[ModalTy{subTy}][ModalTy{baseTy}].emplace(&gc);
        AssumeOneUpperBound(typeConstraintCollection, blames, *upperBound, typeMapping);
    }
}

void TypeChecker::TypeCheckerImpl::Assumption(
    TyVarEnv& typeConstraintCollection, GCBlames& blames, const Decl& decl, const TypeSubst& typeMapping)
{
    Ptr<Generic> generic = decl.GetGeneric();
    if (generic == nullptr) {
        return;
    }
    for (auto& gc : generic->genericConstraints) {
        bool shouldCheckUpperBounds = gc && gc->type && !gc->upperBounds.empty() && IsUpperBoundsValid(gc->upperBounds);
        if (shouldCheckUpperBounds) {
            PerformAssumptionForOneGenericConstraint(typeConstraintCollection, blames, *gc, typeMapping);
        }
    }
}
