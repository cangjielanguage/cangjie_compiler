// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements how to merge inherited parent member to child member.
 */
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Types.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Sema/TypeManager.h"
#include "TypeCheckUtil.h"
#include "NativeFFI/Java/TypeCheck/InheritanceChecker.h"
#include "InheritanceChecker/MemberSignature.h"

using namespace Cangjie;
using namespace AST;
using namespace TypeCheckUtil;

namespace {
void UpdateInconsistentTypes(
    MemberSignature& member, const MemberSignature& parent, const std::vector<Ptr<const Ty>>& inconsistentTypes)
{
    member.inconsistentTypes.insert(parent.inconsistentTypes.begin(), parent.inconsistentTypes.end());
    member.inconsistentTypes.insert(inconsistentTypes.begin(), inconsistentTypes.end());
}

std::vector<std::unordered_set<Ptr<Ty>>> UpdateUpperBoundsSet(
    TypeManager& tyMgr, const std::vector<std::unordered_set<Ptr<Ty>>>& upperBounds, const TypeSubst& typeMapping)
{
    if (typeMapping.empty() ||
        std::all_of(upperBounds.begin(), upperBounds.end(), [](auto it) { return it.empty(); })) {
        // If typeMapping is empty or 'upperBounds' is empty or
        // All elements in 'upperBounds' are empty, quick quit.
        return upperBounds;
    }
    std::vector<std::unordered_set<Ptr<Ty>>> allUpperBounds;
    for (const auto& uppers : std::as_const(upperBounds)) {
        std::unordered_set<Ptr<Ty>> newUppers;
        for (auto it : uppers) {
            newUppers.emplace(tyMgr.GetInstantiatedTy(it, typeMapping));
        }
        allUpperBounds.emplace_back(newUppers);
    }
    return allUpperBounds;
}

// Caller guarantees 'src' and 'other' are both generic function.
void MergeUpperBounds(TypeManager& tyMgr, MemberSignature& src, const MemberSignature& other, bool shouldMerge)
{
    bool ignored = !shouldMerge || src.decl == other.decl || src.upperBounds.empty() ||
        src.upperBounds.size() != other.upperBounds.size();
    if (ignored) {
        return;
    }
    TypeSubst typeMapping = tyMgr.GenerateGenericMappingFromGeneric(
        *RawStaticCast<FuncDecl*>(other.decl), *RawStaticCast<FuncDecl*>(src.decl));
    for (size_t i = 0; i < other.upperBounds.size(); ++i) {
        for (auto upper : other.upperBounds[i]) {
            src.upperBounds[i].emplace(tyMgr.GetInstantiatedTy(upper, typeMapping));
        }
    }
}

void SetPossibleInconsistentType(
    const MemberSignature& target, const MemberSignature& src, std::vector<Ptr<const Ty>>& inconsistentTypes)
{
    auto getTy = [](auto member) {
        if (auto ty = DynamicCast<FuncTy*>(member.ty)) {
            return ty->retTy;
        } else {
            return member.ty;
        }
    };
    inconsistentTypes.emplace_back(getTy(target));
    inconsistentTypes.emplace_back(getTy(src));
}
} // namespace

/**
 * Compute whether 'parent' and 'child' contains inconsistent return type. If so, update 'inconsistentTypes'
 * Update 'updated' result's type if 'parent' and 'child' are both from inherited interfaces.
 * and return type of 'parent' is subtype of return type of 'child'.
 * @p status means {parent and child have same signature, parent and child are both from inherited interfaces}.
 */
bool StructInheritanceChecker::ComputeInconsistentTypes(const MemberSignature& child, const MemberSignature& parent,
    MemberSignature& updated, const std::pair<bool, bool>& status, std::vector<Ptr<const Ty>>& inconsistentTypes) const
{
    auto parentTy = DynamicCast<FuncTy*>(parent.ty);
    auto childTy = DynamicCast<FuncTy*>(child.ty);
    if (!Ty::IsTyCorrect(parentTy) || !Ty::IsTyCorrect(childTy)) {
        return true;
    }
    bool hasConsistentReturnTy = true;
    if (status.first) {
        // If child's ret type is subtype of parent, and parent is abstract or both not abstract, return type is valid.
        bool isValidOverride = parent.decl->TestAttr(Attribute::ABSTRACT) || !child.decl->TestAttr(Attribute::ABSTRACT);
        hasConsistentReturnTy = isValidOverride && AreReturnTypesCompatible(*parentTy, *childTy);
        // If two decls are both interface members that are inherited from current type
        // and parent's return type is subtype of child, using parentTy.
        // The overridden return type is consistent.
        bool bothInterfaceMember = status.second && child.decl->outerDecl->astKind == ASTKind::INTERFACE_DECL &&
            parent.decl->outerDecl->astKind == ASTKind::INTERFACE_DECL;
        bool needUpdateTy = bothInterfaceMember && AreReturnTypesCompatible(*childTy, *parentTy);
        hasConsistentReturnTy = hasConsistentReturnTy || needUpdateTy;
        if (needUpdateTy) {
            updated.ty = parentTy;
        }
    }

    if (!hasConsistentReturnTy) {
        SetPossibleInconsistentType(parent, child, inconsistentTypes);
    }
    return hasConsistentReturnTy;
}

void StructInheritanceChecker::UpdateOverriddenFuncDeclCache(Ptr<Decl> child, Ptr<Decl> parent)
{
    if (checkingDecls.empty()) {
        return;
    }

    if (child->outerDecl == checkingDecls.back()) {
        typeManager.UpdateTopOverriddenFuncDeclCache(child, parent);
    }
}

/**
 * Update @p child value into the @p members
 * NOTE: Do not report error here.
 * @param inheritedInterfaces Indicates parent and child are members of interfaces that inherited by same type.
 *                            This will also be true if parent and child's parent types are from generic upper bounds.
 */
MemberSignature StructInheritanceChecker::UpdateInheritedMemberIfNeeded(
    MemberMap& inheritedMembers, const MemberSignature& child, bool inheritedInterfaces)
{
    MemberSignature updated = child;
    bool needImplement = child.decl->outerDecl->astKind == ASTKind::INTERFACE_DECL || inheritedInterfaces;
    auto foundMembers = inheritedMembers.equal_range(child.decl->identifier);
    std::vector<Ptr<const Ty>> inconsistentTypes;
    for (auto it = foundMembers.first; it != foundMembers.second; ++it) {
        auto parent = it->second;
        if (parent.decl->astKind != child.decl->astKind) {
            continue;
        }
        bool shouldUpdate = true;
        bool inconsistentType = needImplement && parent.ty != child.ty;
        if (parent.decl->IsFunc()) {
            if (child.decl->TestAttr(Attribute::GENERIC) && parent.decl->TestAttr(Attribute::GENERIC)) {
                TypeSubst typeMapping = typeManager.GenerateGenericMappingFromGeneric(
                    *RawStaticCast<FuncDecl*>(parent.decl), *RawStaticCast<FuncDecl*>(child.decl));
                parent.ty = typeManager.GetInstantiatedTy(parent.ty, typeMapping);
            }
            auto parentFuncTy = DynamicCast<FuncTy*>(parent.ty);
            auto childFuncTy = DynamicCast<FuncTy*>(child.ty);
            bool sameStatus = parent.decl->TestAttr(Attribute::STATIC) == child.decl->TestAttr(Attribute::STATIC) &&
                child.decl->TestAttr(Attribute::GENERIC) == parent.decl->TestAttr(Attribute::GENERIC);
            shouldUpdate = sameStatus && parentFuncTy && childFuncTy &&
                typeManager.IsFuncParameterTypesIdentical(*parentFuncTy, *childFuncTy);
            auto consistent = ComputeInconsistentTypes(
                child, parent, updated, {shouldUpdate, inheritedInterfaces}, inconsistentTypes);
            inconsistentType = inconsistentType && !consistent;
        } else if (inconsistentType) {
            SetPossibleInconsistentType(parent, child, inconsistentTypes);
        }

        CheckNativeFFI(parent, child);

        if (shouldUpdate) {
            if (parent.decl->TestAttr(Attribute::ABSTRACT) || !child.decl->TestAttr(Attribute::ABSTRACT)) {
                UpdateOverriddenFuncDeclCache(child.decl, parent.decl);
                it->second = updated; // Override stored value to child.
            }
            it->second.replaceOther = true;
            if (needImplement && (!parent.inconsistentTypes.empty() || inconsistentType)) {
                UpdateInconsistentTypes(it->second, parent, inconsistentTypes);
            }
            MergeUpperBounds(typeManager, it->second, parent, inheritedInterfaces);
            MergeUpperBounds(typeManager, it->second, child, inheritedInterfaces);
            if (!parent.extendDecl || !child.extendDecl) {
                it->second.extendDecl = nullptr;
            }
            it->second.isInheritedInterface = child.isInheritedInterface && inheritedInterfaces; // Update status.
            // If default interface member merged to other default interface member, and:
            // 1. current is merging members of type inherited interfaces, or
            // 2. current is merging extend visible members in other extend
            // eg: interface I1 { func foo(): Unit {} }; interface I2 <: I1 { func foo(): Unit {} };
            //     open class C1 <: I1; class C2 <: C1 & I1; is allowed to ignore 'foo' in C2.
            //     class A <: I1 & I2; the 'foo' must be implemented.
            //     extend A <: I1 {}; extend A <: I2; the 'foo' does not have to be implemented.
            //     interface I3 { func foo(): Unit {} }; extend A <: I1 {}; extend A <: I3; the 'foo' must be
            //     implemented.
            bool isRelationExtend = true;
            if (!checkingDecls.empty() && checkingDecls[0]->astKind == ASTKind::EXTEND_DECL && child.extendDecl) {
                auto curExtend = StaticCast<ExtendDecl*>(checkingDecls[0].get());
                isRelationExtend = typeManager.IsExtendInheritRelation(*curExtend, *child.extendDecl).first;
            }
            it->second.shouldBeImplemented = (it->second.isInheritedInterface || !isRelationExtend) &&
                child.decl->TestAttr(Attribute::DEFAULT) && parent.decl->TestAttr(Attribute::DEFAULT);
            return it->second;
        }
    }
    inheritedMembers.emplace(child.decl->identifier, updated);
    return updated;
}

/**
 * Merge @p otherMembers to @p members
 * If member of the otherMembers implements member in the 'members', replace the member in 'members'.
 * NOTE: Do not report error here.
 */
void StructInheritanceChecker::MergeInheritedMembers(
    MemberMap& members, const MemberMap& otherMembers, Ty& structTy, bool inheritedInterfaces)
{
    MultiTypeSubst mts;
    typeManager.GenerateGenericMapping(mts, structTy);
    auto typeMapping = MultiTypeSubstToTypeSubst(mts);
    for (auto& member : otherMembers) {
        auto memberSig = member.second;
        if (inheritedInterfaces) {
            memberSig.isInheritedInterface = true;
        }
        memberSig.ty = typeManager.GetInstantiatedTy(memberSig.ty, typeMapping);
        memberSig.structTy = typeManager.GetInstantiatedTy(memberSig.structTy, typeMapping);
        memberSig.upperBounds = UpdateUpperBoundsSet(typeManager, memberSig.upperBounds, typeMapping);
        UpdateInheritedMemberIfNeeded(members, memberSig, inheritedInterfaces);
    }
}
