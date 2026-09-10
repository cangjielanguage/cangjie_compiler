// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the OverrideFunctionResolver class for resolving and managing overridden member functions.
 */

#include "OverrideFunctionResolver.h"

#include "Promotion.h"
#include "TypeCheckUtil.h"
#include "cangjie/Sema/CommonTypeAlias.h"
#include "cangjie/Utils/Utils.h"

using namespace Cangjie;
using namespace AST;

// Initialize static cache
std::unordered_map<std::pair<DataTy, std::string>, MemberFuncsWithInstTys, HashPair>
    OverrideFunctionResolver::instTy2MembersCache;

MemberFuncsWithInstTys OverrideFunctionResolver::GetInstMemberFuncWithInstTy(
    DataTy instBaseTy, const std::string& identifier)
{
    CJC_NULLPTR_CHECK(typeManager);
    auto key = std::make_pair(instBaseTy, identifier);
    if (auto found = instTy2MembersCache.find(key); found != instTy2MembersCache.end()) {
        return found->second;
    }
    MemberFuncsWithInstTys funcs;
    auto baseDecl = Ty::GetDeclPtrOfTy<InheritableDecl>(instBaseTy);
    std::set<Ptr<AST::ExtendDecl>> extendDecls;
    if (baseDecl) {
        MemberFuncsWithInstTys superInterfaceFuncs;
        GetInstMemberFromSuper(instBaseTy, baseDecl, superInterfaceFuncs, identifier, true);
        GetInstMemberFromSuper(instBaseTy, baseDecl, funcs, identifier, false);
        MergeIntoFuncs(funcs, superInterfaceFuncs);
        CollectDeclMemberFunc(*baseDecl, instBaseTy, funcs, identifier);
        extendDecls = typeManager->GetDeclExtends(*baseDecl);
    } else {
        extendDecls = typeManager->GetAllExtendsByTy(*instBaseTy);
    }

    MemberFuncsWithInstTys allExtendFuncs;
    for (auto& ed : extendDecls) {
        if (!ed || ed->TestAttr(AST::Attribute::GENERIC_INSTANTIATED)) {
            continue;
        }
        MemberFuncsWithInstTys extendFuncs;
        GetInstMemberFromSuper(instBaseTy, ed, extendFuncs, identifier, true);
        // Select the subclass version in the case of multiple default implementations.
        MergeExtendSuperMember(instBaseTy, allExtendFuncs, extendFuncs);
        // Process extend's own member functions in the same loop
        CollectDeclMemberFunc(*ed, instBaseTy, allExtendFuncs, identifier);
    }
    MergeIntoFuncs(funcs, allExtendFuncs);
    instTy2MembersCache.emplace(key, funcs);
    return funcs;
}

void OverrideFunctionResolver::CollectDeclMemberFunc(
    AST::Decl& decl, AST::DataTy instBaseTy, MemberFuncsWithInstTys& funcs, const std::string& identifier)
{
    CJC_NULLPTR_CHECK(typeManager);
    auto mapping = TypeCheckUtil::GenerateTypeMappingByTy(decl.DataTy(), instBaseTy);
    for (auto& member : decl.GetMemberDecls()) {
        TypeCheckUtil::WorkForMembers(*member, [this, &funcs, &mapping, &identifier](auto& m) {
            if (m.identifier != identifier || m.astKind != AST::ASTKind::FUNC_DECL ||
                m.TestAttr(AST::Attribute::ABSTRACT)) {
                return;
            }
            auto memberFunc = StaticCast<AST::FuncDecl>(&m);
            auto instMemberTy = typeManager->GetInstantiatedTy(memberFunc->DataTy(), mapping);
            // The instantiation type of a member function must be a FuncTy.
            CJC_ASSERT(instMemberTy && instMemberTy->IsFunc());
            auto instMemberFuncTy = StaticCast<AST::FuncTy>(instMemberTy);
            // An overriding function hides its supers, remove the implemented ones first.
            RemoveImplementedSupers(*memberFunc, instMemberFuncTy, funcs);
            AddInstTyToFuncs(memberFunc, instMemberFuncTy, funcs);
        });
    }
}

void OverrideFunctionResolver::GetInstMemberFromSuper(AST::DataTy instBaseTy, Ptr<AST::InheritableDecl> baseDecl,
    MemberFuncsWithInstTys& funcs, const std::string& identifier, bool isCheckingInterface)
{
    CJC_NULLPTR_CHECK(typeManager);
    for (auto& type : baseDecl->inheritedTypes) {
        bool notSkipSuper = isCheckingInterface ? type->GetTy()->IsInterface() : type->GetTy()->IsClass();
        if (!notSkipSuper) {
            continue;
        }
        auto superFuncs = GetInstMemberFuncWithInstTy(type->DataTy(), identifier);
        MergeIntoFuncs(funcs, superFuncs);
    }
    auto mappingBasePtrDecl2InstTy = TypeCheckUtil::GenerateTypeMappingByTy(baseDecl->DataTy(), instBaseTy);
    for (auto& superFunc : funcs) {
        std::unordered_set<Ptr<AST::FuncTy>> newTySet;
        for (auto& superFuncInstTy : superFunc.second) {
            auto instTy = typeManager->GetInstantiatedTy(superFuncInstTy, mappingBasePtrDecl2InstTy);
            // The instantiation type of a member function must be a FuncTy.
            CJC_ASSERT(instTy && instTy->IsFunc());
            newTySet.emplace(StaticCast<AST::FuncTy>(instTy));
        }
        superFunc.second = std::move(newTySet);
    }
}

void OverrideFunctionResolver::MergeExtendSuperMember(
    AST::DataTy instBaseTy, MemberFuncsWithInstTys& funcs, MemberFuncsWithInstTys& newFuncs)
{
    CJC_NULLPTR_CHECK(typeManager);
    for (auto& newFunc : newFuncs) {
        auto newFuncOuterInstTys = Promotion(*typeManager).Promote(instBaseTy, newFunc.first->outerDecl->DataTy());
        for (auto newFuncInstTy : newFunc.second) {
            if (IsImplementedInSameDirection(instBaseTy, *newFunc.first, newFuncInstTy, newFuncOuterInstTys, funcs)) {
                continue;
            }
            AddInstTyToFuncs(newFunc.first, newFuncInstTy, funcs);
        }
    }
}

void OverrideFunctionResolver::MergeIntoFuncs(MemberFuncsWithInstTys& funcs, MemberFuncsWithInstTys& newFuncs)
{
    for (auto& newFunc : newFuncs) {
        for (auto newFuncInstTy : newFunc.second) {
            if (IsImplementedByAny(*newFunc.first, newFuncInstTy, funcs)) {
                continue;
            }
            AddInstTyToFuncs(newFunc.first, newFuncInstTy, funcs);
        }
    }
}

void OverrideFunctionResolver::RemoveImplementedSupers(
    AST::FuncDecl& memberFunc, const Ptr<AST::FuncTy> instMemberFuncTy, MemberFuncsWithInstTys& funcs)
{
    for (auto superFuncIt = funcs.begin(); superFuncIt != funcs.end();) {
        for (auto superFuncInstTyIt = superFuncIt->second.begin(); superFuncInstTyIt != superFuncIt->second.end();) {
            if (IsImplementationFunc(memberFunc, *superFuncIt->first, instMemberFuncTy, *superFuncInstTyIt)) {
                superFuncInstTyIt = superFuncIt->second.erase(superFuncInstTyIt);
            } else {
                ++superFuncInstTyIt;
            }
        }
        superFuncIt = superFuncIt->second.empty() ? funcs.erase(superFuncIt) : std::next(superFuncIt);
    }
}

bool OverrideFunctionResolver::IsImplementedByAny(
    const AST::FuncDecl& newFunc, const Ptr<AST::FuncTy> newFuncInstTy, const MemberFuncsWithInstTys& funcs)
{
    for (auto& func : funcs) {
        for (auto& instTy : func.second) {
            if (IsImplementationFunc(*func.first, newFunc, instTy, newFuncInstTy)) {
                return true;
            }
        }
    }
    return false;
}

bool OverrideFunctionResolver::IsImplementedInSameDirection(DataTy instBaseTy, const AST::FuncDecl& newFunc,
    const Ptr<AST::FuncTy> newFuncInstTy, const std::set<DataTy>& newFuncOuterDeclInstTys,
    const MemberFuncsWithInstTys& funcs)
{
    for (auto& func : funcs) {
        bool isImplRelation = false;
        for (auto& instTy : func.second) {
            if (IsImplementationFunc(*func.first, newFunc, instTy, newFuncInstTy)) {
                isImplRelation = true;
                break;
            }
        }
        if (!isImplRelation) {
            continue;
        }
        // Confirm subclass direction: any leaf type of the existing func must be a subtype of any root type.
        auto funcOuterDeclInstTys = Promotion(*typeManager).Promote(instBaseTy, func.first->outerDecl->DataTy());
        for (auto root : newFuncOuterDeclInstTys) {
            if (std::any_of(funcOuterDeclInstTys.begin(), funcOuterDeclInstTys.end(),
                [this, &root](auto leaf) { return typeManager->IsSubtype(leaf, root); })) {
                return true;
            }
        }
    }
    return false;
}

void OverrideFunctionResolver::AddInstTyToFuncs(
    AST::FuncDecl* funcDecl, const Ptr<AST::FuncTy> instTy, MemberFuncsWithInstTys& funcs)
{
    if (auto funcIt = funcs.find(funcDecl); funcIt != funcs.end()) {
        funcIt->second.emplace(instTy);
    } else {
        funcs.emplace(std::make_pair(funcDecl, std::unordered_set<Ptr<AST::FuncTy>>{instTy}));
    }
}

bool OverrideFunctionResolver::IsImplementationFunc(const FuncDecl& srcFunc, const FuncDecl& superFunc,
    const Ptr<FuncTy> srcInstFuncTy, const Ptr<FuncTy> superInstFuncTy)
{
    CJC_ASSERT(srcFunc.outerDecl && superFunc.outerDecl);
    // Private function cannot be overridden, it is not visible in any derived class.
    if (!IsVirtualMember(superFunc)) {
        return false;
    }
    // If function's static or generic status not equal, the will not have relation of implementation.
    bool noRelation = srcFunc.TestAttr(Attribute::STATIC) != superFunc.TestAttr(Attribute::STATIC) ||
        srcFunc.TestAttr(Attribute::GENERIC) != superFunc.TestAttr(Attribute::GENERIC) ||
        srcFunc.identifier != superFunc.identifier;
    if (noRelation) {
        return false;
    }
    if (srcFunc.TestAttr(Attribute::GENERIC) && superFunc.TestAttr(Attribute::GENERIC)) {
        TypeSubst mappingBetweenFuncs = typeManager->GenerateGenericMappingFromGeneric(srcFunc, superFunc);
        auto substSrcInstFuncTy = typeManager->GetInstantiatedTy(srcInstFuncTy, mappingBetweenFuncs);
        // The instantiation type of a member function must be a FuncTy.
        CJC_ASSERT(substSrcInstFuncTy && substSrcInstFuncTy->IsFunc());
        auto substSrcInstFuncTyF = StaticCast<FuncTy>(substSrcInstFuncTy);
        return typeManager->IsFuncTySubType(*substSrcInstFuncTyF, *superInstFuncTy);
    }
    return typeManager->IsFuncTySubType(*srcInstFuncTy, *superInstFuncTy);
}

void OverrideFunctionResolver::ClearCache()
{
    instTy2MembersCache.clear();
}

Ptr<Ty> OverrideFunctionResolver::GetMatchedFuncInstTyByGivenTarget(
    MemberFuncWithInstTys& candidates, const FuncDecl& target, DataTy targetBaseTy)
{
    CJC_NULLPTR_CHECK(typeManager);
    auto implFunc = candidates.first;
    if (!implFunc) {
        return typeManager->GetInvalidTy();
    }
    // Handle code: class B<T> { func a(a: T): Unit {} }; class A <: B<B1> {}, given 'targetBaseTy' is 'Class-A',
    // we need get mapping if [T |-> B1], and substitute function type '(T)->Unit' to '(B1)->T'.
    MultiTypeSubst typeMappings =
        Promotion(*typeManager).GetPromoteTypeMapping(targetBaseTy, target.outerDecl->DataTy());
    auto instFuncTys = typeManager->GetInstantiatedTys(target.DataTy(), typeMappings);
    Ptr<Ty> matchedTy = typeManager->GetInvalidTy();
    for (auto instTy : candidates.second) {
        Ptr<Ty> substImplFuncTy = instTy;
        if (implFunc->TestAttr(Attribute::GENERIC) && target.TestAttr(Attribute::GENERIC)) {
            TypeSubst mappingBetweenFuncs = typeManager->GenerateGenericMappingFromGeneric(*implFunc, target);
            substImplFuncTy = typeManager->GetInstantiatedTy(instTy, mappingBetweenFuncs);
            // The instantiation type of a member function must be a FuncTy.
            CJC_ASSERT(substImplFuncTy && substImplFuncTy->IsFunc());
        }
        for (auto instFuncTy : instFuncTys) {
            auto substImplFuncTyF = StaticCast<FuncTy>(substImplFuncTy);
            auto instFuncTyF = StaticCast<FuncTy>(instFuncTy);
            if (typeManager->IsFuncTySubType(*substImplFuncTyF, *instFuncTyF)) {
                matchedTy = instTy;
                break;
            }
        }
        if (Ty::IsTyCorrect(matchedTy)) {
            break;
        }
    }
    return matchedTy;
}

bool OverrideFunctionResolver::IsImplementationFunc(const AST::FuncDecl& srcFunc, const AST::FuncDecl& superFunc)
{
    auto fastQuit =
        !srcFunc.outerDecl || !superFunc.outerDecl || !srcFunc.DataTy()->IsFunc() || !superFunc.DataTy()->IsFunc();
    if (fastQuit) {
        return false;
    }
    auto srcFuncTy = StaticCast<AST::FuncTy>(srcFunc.DataTy());
    // Handle code: 'class B<T1> { func a(a: T1): Unit {} }; class A<T2> <: B<T2> { func a(a: T): Unit {} }', we need
    // get mapping if [T1 |-> T2], and substitute function type '(T1)->Unit' to '(T2)->Unit'.
    auto typeMappings =
        Promotion(*typeManager).GetPromoteTypeMapping(srcFunc.outerDecl->DataTy(), superFunc.outerDecl->DataTy());
    auto instSuperFuncTys = typeManager->GetInstantiatedTys(superFunc.DataTy(), typeMappings);
    for (auto instSuperFuncTy : instSuperFuncTys) {
        if (IsImplementationFunc(srcFunc, superFunc, srcFuncTy, StaticCast<AST::FuncTy>(instSuperFuncTy))) {
            return true;
        }
    }
    return false;
}

MemberFuncSet OverrideFunctionResolver::GetTopOverriddenFuncs(AST::Ty& instBaseTy, const AST::FuncDecl& funcDecl)
{
    CJC_NULLPTR_CHECK(typeManager);
    auto baseDecl = AST::Ty::GetDeclPtrOfTy<AST::InheritableDecl>(&instBaseTy);
    std::set<Ptr<ClassLikeDecl>> superDecls;
    std::set<Ptr<AST::ExtendDecl>> extendDecls;
    MemberFuncSet result;
    if (baseDecl) {
        auto superDeclsVec = baseDecl->GetAllSuperDecls();
        superDecls.insert(superDeclsVec.begin(), superDeclsVec.end());
        extendDecls = typeManager->GetDeclExtends(*baseDecl);
    } else {
        extendDecls = typeManager->GetAllExtendsByTy(instBaseTy);
    }
    for (auto extendDecl : extendDecls) {
        auto superDeclsVec = extendDecl->GetAllSuperDecls();
        superDecls.insert(superDeclsVec.begin(), superDeclsVec.end());
    }
    for (auto superDecl : superDecls) {
        if (superDecl == baseDecl) {
            continue;
        }
        auto visit = [this, &funcDecl, &superDecl, &result](AST::Decl& memberFunc) {
            auto parentFunc = DynamicCast<FuncDecl>(&memberFunc);
            if (!parentFunc || parentFunc == &funcDecl || !IsImplementationFunc(funcDecl, *parentFunc)) {
                return;
            }
            auto parentTops = GetTopOverriddenFuncs(*superDecl->GetTy(), *parentFunc);
            if (parentTops.empty()) {
                result.emplace(parentFunc);
            } else {
                result.insert(parentTops.begin(), parentTops.end());
            }
        };
        TypeCheckUtil::WorkForMembersOfDecl(*superDecl, visit);
    }
    return result;
}
