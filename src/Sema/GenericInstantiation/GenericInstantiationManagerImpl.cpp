// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * Implements GenericInstantiationManager related methods. Contains:
 * 1. Instantiate all referenced generic decls in non-generic/instantiated decls.
 *    - Copy generic decl and substitute generic types with instantiated types.
 *    - Imported inline functions' content should only be walked if the function is used in source package.
 * 2. Rearrange generic references to instantiated decls.
 *   - Update reference decl pointer from original generic ast to instantiated decl's pointer.
 *     eg: func test<T>(a: T) { // xxx}
 *         var a = test<Int64>
 *         After typecheck, the RefExpr ('test<Int64>')'s member ref.target was pointer to generic version of 'test'.
 *         the rearrange is to update 'refExpr.ref.target' from generic version to instantiated decl created in step 1.
 *    - Update 'typePattern''s compile-time matching status with instantiated types.
 * 3. Some 'Ty' has 'decl' and 'declPtr' members.
 *    We need to update type's corresponding 'decl' memebr to newly instantiated decl.
 *    For cjnative backend, clear imported instantiated decl which has same type with decls instantiate in current
 * package.
 * 4. Remove unused instantiated decls when compiling toy executable with 'O2' option.
 * 5. Validate all used nodes have non-generic sema type in DEBUG version.
 */

#include "GenericInstantiationManagerImpl.h"

#include "BuiltInOperatorUtil.h"
#include "ImplUtils.h"
#include "InstantiatedExtendRecorder.h"
#include "TypeCheckUtil.h"
#include "PartialInstantiation.h"

#include "cangjie/AST/ASTTypeValidator.h"
#include "cangjie/AST/Clone.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/RecoverDesugar.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Mangle/BaseMangler.h"
#include "cangjie/Sema/Desugar.h"
#include "cangjie/Sema/TypeManager.h"
#include "cangjie/Utils/ProfileRecorder.h"

using namespace Cangjie;
using namespace AST;
using namespace TypeCheckUtil;
using GIM = GenericInstantiationManager;

GIM::GenericInstantiationManagerImpl::GenericInstantiationManagerImpl(CompilerInstance& ci)
    : diag(ci.diag),
      importManager(ci.importManager),
      typeManager(*ci.typeManager),
      testManager(ci.testManager),
      promotion(*ci.typeManager),
      instantiationWalkerID(AST::Walker::GetNextWalkerID()),
      rearrangeWalkerID(AST::Walker::GetNextWalkerID()),
      backend(ci.invocation.globalOptions.backend)
{
    instantiator = [this](auto node) { return CheckNodeInstantiation(*node); };
    rearranger = [this](auto node) { return RearrangeReferencePtr(*node); };
    contextReset = [this](auto node) {
        if (auto decl = DynamicCast<Decl*>(node); decl && !structContext.empty()) {
            // Pop context if current is structure declaration or generic decl inside generic structure declaration.
            if (NeedSwitchContext(*decl) && decl == structContext.back()) {
                structContext.pop_back();
            }
        }
        return VisitAction::WALK_CHILDREN;
    };
    SetOptLevel(ci.invocation.globalOptions);
}

namespace {
std::unordered_map<Ptr<const Decl>, std::vector<size_t>> g_skippedMemberOffsets = {};

void UpdateInstantiatedDeclsLinkage(const Package& pkg)
{
    // All instantiated decls should be marked as internal for cjnative backend.
    // For other backend, only mark extend as internal since extend will be instantiated in used package.
    for (auto& decl : pkg.genericInstantiatedDecls) {
        Walker(decl.get(), [](auto node) {
            if (auto fd = DynamicCast<FuncDecl*>(node); fd) {
                fd->linkage = Linkage::INTERNAL;
            }
            return VisitAction::WALK_CHILDREN;
        }).Walk();
    }
}

inline void UnsetBoxStatus(Package& pkg)
{
    Walker(&pkg, [](auto node) {
        node->DisableAttr(Attribute::NEED_AUTO_BOX);
        return VisitAction::WALK_CHILDREN;
    }).Walk();
}

void ClearUnusedSrcImportedDecls(Package& pkg, const std::function<bool(const Ptr<Decl>&)>& checkUnvisited)
{
    for (auto it = pkg.srcImportedNonGenericDecls.begin(); it != pkg.srcImportedNonGenericDecls.end();) {
        if (checkUnvisited(*it)) {
            // Unset 'SRC_IMPORTED' for unused imported func decls.
            (*it)->DisableAttr(Attribute::SRC_IMPORTED);
            it = pkg.srcImportedNonGenericDecls.erase(it);
        } else {
            ++it;
        }
    }
    Utils::EraseIf(pkg.inlineFuncDecls, checkUnvisited);
}

void ClearInstTysIsNeeded(Node& node)
{
    if (Utils::NotIn(node.astKind, {ASTKind::REF_EXPR, ASTKind::MEMBER_ACCESS, ASTKind::CALL_EXPR})) {
        return;
    }
    if (auto ce = DynamicCast<CallExpr*>(&node);
        ce && ce->desugarExpr && !ce->desugarExpr->ty->HasGeneric() && Is<NameReferenceExpr*>(ce->baseFunc.get())) {
        StaticCast<NameReferenceExpr*>(ce->baseFunc.get())->instTys.clear();
        return;
    }
    if (auto re = DynamicCast<RefExpr*>(&node)) {
        if (auto target = re->ref.target;
            target && !target->ty->HasGeneric() && !target->TestAnyAttr(Attribute::INTRINSIC, Attribute::GENERIC)) {
            re->instTys.clear();
            if (target->astKind != ASTKind::TYPE_ALIAS_DECL) {
                re->ty = target->ty;
            }
        }
        return;
    }
    if (auto ma = DynamicCast<MemberAccess*>(&node); ma && ma->target && !ma->target->ty->HasGeneric() &&
        !ma->target->TestAnyAttr(Attribute::INTRINSIC, Attribute::GENERIC)) {
        ma->instTys.clear();
        if (ma->target->astKind != ASTKind::TYPE_ALIAS_DECL && !HasJavaAttr(*ma->target)) {
            ma->ty = ma->target->ty;
        }
    }
}

Ptr<Ty> GetOriginalTy(Ty& ty, const TypeSubst& g2gTyMap, TypeManager& typeManager)
{
    if (!ty.IsGeneric() && !ty.HasGeneric()) {
        return &ty;
    }
    if (auto gTy = DynamicCast<TyVar>(&ty)) {
        if (auto found = g2gTyMap.find(gTy); found != g2gTyMap.end()) {
            return found->second;
        }
    }
    if (!ty.IsFunc()) {
        std::vector<Ptr<Ty>> tyArgs;
        tyArgs.reserve(ty.typeArgs.size());
        for (auto tyArg : std::as_const(ty.typeArgs)) {
            tyArgs.emplace_back(GetOriginalTy(*tyArg, g2gTyMap, typeManager));
        }
        if (ty.IsTuple()) {
            return typeManager.GetTupleTy(tyArgs);
        }
        auto decl = Ty::GetDeclPtrOfTy(&ty);
        if (!decl) {
            return &ty;
        }
        if (ty.IsClass()) {
            return Is<ClassThisTy>(ty) ? typeManager.GetClassThisTy(*StaticCast<ClassDecl>(decl), tyArgs)
                                       : typeManager.GetClassTy(*StaticCast<ClassDecl>(decl), tyArgs);
        }
        if (ty.IsInterface()) {
            return typeManager.GetInterfaceTy(*StaticCast<InterfaceDecl>(decl), tyArgs);
        }
        if (ty.IsEnum()) {
            return typeManager.GetEnumTy(*StaticCast<EnumDecl>(decl), tyArgs);
        }
        if (ty.IsStruct()) {
            return typeManager.GetStructTy(*StaticCast<StructDecl>(decl), tyArgs);
        }
        return &ty;
    }
    auto& funcTy = StaticCast<FuncTy&>(ty);
    std::vector<Ptr<Ty>> paramTys;
    paramTys.reserve(funcTy.paramTys.size());
    for (auto paramTy : funcTy.paramTys) {
        paramTys.emplace_back(GetOriginalTy(*paramTy, g2gTyMap, typeManager));
    }
    return typeManager.GetFunctionTy(paramTys, GetOriginalTy(*funcTy.retTy, g2gTyMap, typeManager));
}

inline ReversedTypeSubst GetReversedTypeSubst(const TypeSubst& typeMapping)
{
    ReversedTypeSubst rts;
    for (auto [k, v] : typeMapping) {
        rts.emplace(v, k);
    }
    return rts;
}

inline bool IsGenericFuncWithDefaultParam(const Decl& decl)
{
    if (auto fd = DynamicCast<FuncDecl>(&decl);
        fd && fd->GetGeneric() && fd->funcBody && !fd->funcBody->paramLists.empty()) {
        for (const auto& param : fd->funcBody->paramLists[0]->params) {
            if (param->desugarDecl) {
                return true;
            }
        }
    }
    return false;
}

void BuildGenericsTyMap(const FuncDecl& fd, TypeSubst& g2gMap)
{
    auto generic = fd.GetGeneric();
    CJC_NULLPTR_CHECK(generic);
    const auto& typeParams = generic->typeParameters;
    for (const auto& param : fd.funcBody->paramLists[0]->params) {
        if (const auto& dd = param->desugarDecl) {
            auto ddGeneric = dd->GetGeneric();
            CJC_ASSERT(ddGeneric && ddGeneric->typeParameters.size() == typeParams.size());
            const auto& ddTypeParams = ddGeneric->typeParameters;
            for (size_t i = 0; i < ddTypeParams.size(); ++i) {
                CJC_ASSERT(Ty::IsTyCorrect(ddTypeParams[i]->ty) && Ty::IsTyCorrect(typeParams[i]->ty));
                g2gMap.emplace(StaticCast<TyVar>(ddTypeParams[i]->ty), typeParams[i]->ty);
            }
        }
    }
}

size_t CountSkippedMembersBefore(const Decl& decl, size_t offset)
{
    auto found = g_skippedMemberOffsets.find(&decl);
    if (found == g_skippedMemberOffsets.end()) {
        std::vector<size_t> offsets;
        auto members = GetRealIndexingMembers(decl.GetMemberDecls(), decl.TestAttr(Attribute::GENERIC));
        for (auto it = members.begin(); it != members.end(); ++it) {
            size_t off = static_cast<size_t>(std::distance(members.begin(), it));
            if (IsVirtualMember(**it) || IsStaticVar(**it)) {
                offsets.emplace_back(off);
            }
        }
        found = g_skippedMemberOffsets.emplace(&decl, std::move(offsets)).first;
    }
    size_t count = 0;
    for (size_t off : found->second) {
        if (off < offset) {
            count++;
        } else if (off == offset) {
            return std::numeric_limits<size_t>::max();
        } else {
            break;
        }
    }
    return count;
}

Ptr<Decl> GetMemberByOffset(const Decl& decl, size_t offset)
{
    auto members = GetRealIndexingMembers(decl.GetMemberDecls(), decl.TestAttr(Attribute::GENERIC));
    auto implMemberIt = members.begin();
    if (!decl.TestAttr(Attribute::GENERIC_INSTANTIATED)) {
        CJC_ASSERT(offset < members.size());
        std::advance(implMemberIt, offset);
    } else {
        CJC_NULLPTR_CHECK(decl.genericDecl);
        size_t count = CountSkippedMembersBefore(*decl.genericDecl, offset);
        if (count == std::numeric_limits<size_t>::max()) {
            members = GetRealIndexingMembers(
                decl.genericDecl->GetMemberDecls(), decl.genericDecl->TestAttr(Attribute::GENERIC));
            implMemberIt = members.begin();
            CJC_ASSERT(offset < members.size());
            std::advance(implMemberIt, offset);
            return implMemberIt->get();
        }
        CJC_ASSERT(count <= offset && offset - count < members.size());
        std::advance(implMemberIt, offset - count);
    }
    return implMemberIt->get();
}
} // namespace

void GIM::GenericInstantiationManagerImpl::GenericInstantiatePackage(Package& pkg)
{
    this->curPkg = &pkg;
    Utils::ProfileRecorder::Start("GenericInstantiatePackage", "instantiate");
    // Collect extend decls by usage.
    RecordExtend(*curPkg);
    if (curPkg->TestAttr(Attribute::INCRE_COMPILE)) {
        InstantiateForIncrementalPackage();
    } else {
        // When `GenericInstantiatePackage` is invoked for multiple times, ensure that global data is clean.
        PartialInstantiation::ResetGlobalMap();
        // Only walk non-generic or instantiated decl's to perform instantiation.
        Walker(curPkg, instantiationWalkerID, instantiator, contextReset).Walk();
    }
    Utils::ProfileRecorder::Stop("GenericInstantiatePackage", "instantiate");
    Utils::ProfileRecorder::Start("GenericInstantiatePackage", "testManager");
    testManager->PreparePackageForTestIfNeeded(*curPkg);
    Utils::ProfileRecorder::Stop("GenericInstantiatePackage", "testManager");

    // Do not perform rearrange, validation and deletion if errors generated.
    if (diag.GetErrorCount() != 0) {
        return;
    }
    Utils::ProfileRecorder::Start("GenericInstantiatePackage", "rearrange");
    // Erase un-visited src imported functions during instantiation step.
    ClearUnusedSrcImportedDecls(
        *curPkg, [this](const Ptr<Decl>& it) { return !it->IsConst() && usedSrcImportedDecls.count(it) == 0; });
    // After the instantiation finished and there are instantiatedDecls generated in current package,
    // rearrange the ptr of outer references' target to the instantiated decl.
    // NOTE: Walker will also walk 'genericInstantiatedDecls' in package node.
    Walker(curPkg, rearrangeWalkerID, rearranger, contextReset).Walk();
    RecoverDesugarForBuiltIn();
    Utils::ProfileRecorder::Stop("GenericInstantiatePackage", "rearrange");
    // Do not perform validation and deletion if errors generated.
    if (diag.GetErrorCount() != 0) {
        return;
    }
    Utils::ProfileRecorder recorder("GenericInstantiatePackage", "cleanup");
    UpdateInstantiatedExtendMap();
    UpdateInstantiatedDeclsLinkage(pkg);
    ClearImportedUnusedInstantiatedDecls();
    ValidateUsedNodes(diag, pkg);
    UnsetBoxStatus(pkg);
}

void GIM::GenericInstantiationManagerImpl::RecordExtend(AST::Node& node)
{
    InstantiatedExtendRecorder(*this, typeManager)(node);
}

void GIM::GenericInstantiationManagerImpl::InstantiateForIncrementalPackage()
{
    // Instantiate decls which is marked as to be compiled and collect unchanged decls.
    std::vector<Ptr<Decl>> unchanged;
    IterateToplevelDecls(*curPkg, [this, &unchanged](auto& decl) {
        if (decl->TestAttr(Attribute::GENERIC)) {
            return;
        }
        if (decl->toBeCompiled) {
            Walker(decl.get(), instantiationWalkerID, instantiator, contextReset).Walk();
            return;
        }
        unchanged.emplace_back(decl.get());
        if (!decl->IsNominalDecl()) {
            return;
        }
        // Enum constructor is part of whole enum decl, so it will not be separately marked as 'toBeCompiled'.
        // Only need using 'GetMemberDecls' here.
        structContext.push_back(decl.get());
        for (auto& it : decl->GetMemberDecls()) {
            if (it->TestAttr(Attribute::GENERIC)) {
                continue;
            }
            WorkForMembers(*it, [this, &unchanged](auto& member) {
                if (member.toBeCompiled) {
                    Walker(&member, instantiationWalkerID, instantiator, contextReset).Walk();
                } else {
                    unchanged.emplace_back(&member);
                }
            });
        }
        structContext.pop_back();
    });
    // Set 'needCompile' to mark instantiations as not to be compiled,
    // then walk and instantiate toplevel decls for full signature.
    needCompile = false;
    for (auto it : unchanged) {
        if (it->outerDecl) {
            structContext.push_back(it->outerDecl);
            Walker(it, instantiationWalkerID, instantiator, contextReset).Walk();
            structContext.pop_back();
        } else {
            Walker(it, instantiationWalkerID, instantiator, contextReset).Walk();
        }
    }
    needCompile = true;
}

void GIM::GenericInstantiationManagerImpl::UpdateInstantiatedExtendMap()
{
    // Update instantiated extend decl map after instantiation and re-arrange finished.
    for (auto& it : curPkg->genericInstantiatedDecls) {
        if (it->astKind != ASTKind::EXTEND_DECL) {
            continue;
        }
        if (it->ty->IsBuiltin()) {
            typeManager.UpdateBuiltInTyExtendDecl(*it->ty, static_cast<ExtendDecl&>(*it));
        } else if (auto extendedDecl = DynamicCast<InheritableDecl*>(Ty::GetDeclOfTy(it->ty)); extendedDecl) {
            typeManager.declToExtendMap[extendedDecl].emplace(RawStaticCast<ExtendDecl*>(it.get()));
        }
    }
}

void GIM::GenericInstantiationManagerImpl::ClearImportedUnusedInstantiatedDecls()
{
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    std::unordered_set<Ptr<Ty>> instantiatedTys;
    std::unordered_set<Ptr<Decl>> removedDecls;
    for (auto& decl : curPkg->genericInstantiatedDecls) {
        if (decl->IsNominalDecl()) {
            // Update ty's instantiated decl to make current package's decl as the first choice of type.
            RestoreInstantiatedDeclTy(*decl);
            instantiatedTys.emplace(decl->ty);
        }
    }
    // Record erasable decls which have same ty with instantiated decl in current package.
    auto recordEraseNominal = [&removedDecls, &instantiatedTys](auto& it) {
        if (instantiatedTys.find(it->ty) != instantiatedTys.end()) {
            removedDecls.emplace(it.get());
        }
    };
    // Instantiated decls in imported package will not be referenced by any other place.
    // The only 'ty->decl' references are already been replaced in previous step.
    for (const auto& it : importManager.GetAllImportedPackages()) {
        bool ignore = it->srcPackage == this->curPkg || !it->srcPackage->TestAttr(Attribute::IMPORTED);
        if (ignore) {
            continue;
        }
        // Collect and record all erasable decls which have same instantiation types.
        auto importPkg = it->srcPackage;
        std::for_each(
            importPkg->genericInstantiatedDecls.begin(), importPkg->genericInstantiatedDecls.end(), recordEraseNominal);
        // Collect all decls which are inside removed decls.
        std::unordered_set<Ptr<Decl>> removedOtherDecls;
        for (auto& decl : importPkg->genericInstantiatedDecls) {
            Ptr<Decl> outermostDecl = decl->outerDecl;
            while (outermostDecl != nullptr && outermostDecl->outerDecl != nullptr) {
                outermostDecl = outermostDecl->outerDecl;
            }
            if (removedDecls.find(outermostDecl) != removedDecls.end()) {
                removedOtherDecls.emplace(decl.get());
            }
        }
        // Remove corresponding extend type map.
        std::for_each(removedDecls.begin(), removedDecls.end(), [this](auto it) {
            if (it->astKind == ASTKind::EXTEND_DECL) {
                typeManager.RemoveExtendFromMap(*StaticAs<ASTKind::EXTEND_DECL>(it));
            }
        });
        Utils::EraseIf(importPkg->srcImportedNonGenericDecls,
            [&removedDecls](auto it) { return it && removedDecls.find(it->outerDecl) != removedDecls.end(); });
        // Remove all struct decls.
        Utils::EraseIf(importPkg->genericInstantiatedDecls,
            [&removedDecls](auto& it) { return removedDecls.find(it.get()) != removedDecls.end(); });
        // Remove all inner decls.
        Utils::EraseIf(importPkg->genericInstantiatedDecls,
            [&removedOtherDecls](auto& it) { return removedOtherDecls.find(it.get()) != removedOtherDecls.end(); });
        removedDecls.clear(); // Reset collected removed decls for next pacakge.
    }
    // Since the 'ty->decl' may be changed after above process, update extends again.
    UpdateInstantiatedExtendMap();
#endif
}

/**
 * Use Types::Type to establish the map which binds the relationships between the generic parameter type to
 * semantic funcType parameters.
 */
GenericInfo GIM::GenericInstantiationManagerImpl::ConstructGenericInfo(
    Decl& decl, const std::vector<Ptr<Ty>>& instTys) const
{
    CJC_ASSERT(decl.TestAttr(Attribute::GENERIC));
    TypeSubst gTyToTyMap = GenerateTypeMapping(decl, instTys);
    CJC_ASSERT(!gTyToTyMap.empty());
    GenericInfo genericInfo(&decl, gTyToTyMap);
    return genericInfo;
}

void GIM::GenericInstantiationManagerImpl::PerformTyInstantiationDuringClone(
    const Node& genericNode, Node& clonedNode, const GenericInfo& info, const TypeSubst& g2gTyMap)
{
    // Instantiate the generic ty to instantiated type.
    if (Ty::IsTyCorrect(genericNode.ty)) {
        clonedNode.ty =
            typeManager.GetInstantiatedTy(GetOriginalTy(*genericNode.ty, g2gTyMap, typeManager), info.gTyToTyMap);
    }
    if (auto ref = DynamicCast<NameReferenceExpr*>(&clonedNode); ref) {
        if (ref->matchedParentTy) {
            ref->matchedParentTy = typeManager.GetInstantiatedTy(
                GetOriginalTy(*ref->matchedParentTy, g2gTyMap, typeManager), info.gTyToTyMap);
        }
        for (auto& instTy : ref->instTys) {
            instTy = typeManager.GetInstantiatedTy(GetOriginalTy(*instTy, g2gTyMap, typeManager), info.gTyToTyMap);
        }
    }
}

void GIM::GenericInstantiationManagerImpl::PerformUpdateAttrDuringClone(Node& genericNode, Node& clonedNode) const
{
    auto clonedDecl = As<ASTKind::DECL>(&clonedNode);
    if (clonedDecl == nullptr) {
        return;
    }
    auto genericDecl = RawStaticCast<Decl*>(&genericNode);
    CJC_ASSERT(curPkg != nullptr);
    clonedDecl->moduleName = Utils::GetRootPackageName(curPkg->fullPackageName);
    clonedDecl->fullPackageName = curPkg->fullPackageName;
    clonedDecl->curFile = curPkg->files[0];
    clonedDecl->toBeCompiled = needCompile || genericDecl->toBeCompiled; // Mark for incremental compilation.
    clonedDecl->DisableAttr(Attribute::IMPORTED);
    // The generic static function in instantiated structDecl has attributes GENERIC and GENERIC_INSTANTIATED.
    Ptr<Generic> declGeneric = clonedDecl->GetGeneric();
    if (declGeneric == nullptr) {
        return;
    }
    bool result = false;
    for (auto& typeParameter : declGeneric->typeParameters) {
        result = result || typeParameter->ty->IsGeneric();
    }
    if (result && clonedNode.TestAttr(Attribute::GENERIC)) {
        if (clonedDecl->outerDecl && clonedDecl->outerDecl->IsFunc()) {
            return; // Nested generic function do not record original generic decl.
        }
        clonedNode.EnableAttr(Attribute::GENERIC_INSTANTIATED);
        // This may be accessed by AST2CHIR.
        clonedDecl->genericDecl = genericDecl;
    } else if (!result && clonedNode.TestAttr(Attribute::GENERIC)) {
        clonedNode.DisableAttr(Attribute::GENERIC);
        clonedNode.EnableAttr(Attribute::GENERIC_INSTANTIATED);
    }
}

Ptr<Decl> GIM::GenericInstantiationManagerImpl::GetInstantiatedDeclWithGenericInfo(const GenericInfo& genericInfo)
{
    auto genericDecl = genericInfo.decl;
    Ptr<Decl> instantiatedDecl = FindInCache(genericInfo);
    // Check if the generic function is already instantiated.
    if (instantiatedDecl) {
        // 'toBeCompiled' need be updated, since unchanged cache also may be created during incremental stage.
        instantiatedDecl->toBeCompiled = instantiatedDecl->toBeCompiled || needCompile;
        return instantiatedDecl;
    }
    TypeSubst g2gTyMap = {};
    if (IsGenericFuncWithDefaultParam(*genericDecl)) {
        BuildGenericsTyMap(*StaticCast<FuncDecl>(genericDecl), g2gTyMap);
    }
    // Clone a generic instantiated funcDecl by changing its generic types to instantiated types.
    // This is a callback function which perform generic type instantiation during the clone period.
    VisitFunc instantiateType = [this, &genericInfo, &g2gTyMap](Node& genericNode, Node& clonedNode) {
        PerformTyInstantiationDuringClone(genericNode, clonedNode, genericInfo, g2gTyMap);
        PerformUpdateAttrDuringClone(genericNode, clonedNode);
    };
    auto clonedDecl = PartialInstantiation::Instantiate<Decl>(genericDecl, instantiateType);
    if (clonedDecl == nullptr) {
        return nullptr;
    }
    if (auto fd = DynamicCast<FuncDecl*>(clonedDecl.get()); fd && fd->funcBody && !fd->funcBody->paramLists.empty()) {
        for (auto& param : std::as_const(fd->funcBody->paramLists[0]->params)) {
            if (param->desugarDecl) {
                param->desugarDecl->DisableAttr(Attribute::GENERIC);
            }
        }
    }
    clonedDecl->EnableAttr(Attribute::GENERIC_INSTANTIATED);
    clonedDecl->EnableAttr(Attribute::NO_REFLECT_INFO);
    clonedDecl->DisableAttr(Attribute::GENERIC, Attribute::IMPORTED);
    // NOTE: Remove primary ctor decl from cloned decl to get stable member index for rearrangement.
    //       This is related to the usage of 'membersIndexMap'.
    auto& members = clonedDecl->GetMemberDecls();
    Utils::EraseIf(members, [](auto& it) { return it->astKind == ASTKind::PRIMARY_CTOR_DECL; });
    for (auto& ptr : members) {
        ptr->EnableAttr(Attribute::NO_REFLECT_INFO);
        if (ptr->astKind == ASTKind::PROP_DECL) {
            for (auto& g : StaticCast<PropDecl*>(ptr.get())->getters) {
                g->EnableAttr(Attribute::NO_REFLECT_INFO);
            }
            for (auto& s : StaticCast<PropDecl*>(ptr.get())->setters) {
                s->EnableAttr(Attribute::NO_REFLECT_INFO);
            }
        }
    }

    instantiatedDecl = clonedDecl.get();
    CJC_ASSERT(curPkg != nullptr);
    (void)curPkg->genericInstantiatedDecls.emplace_back(std::move(clonedDecl));
    instantiatedDecl->genericDecl = genericDecl;
    // Record this instantiated result to maps.
    declInstantiationByTypeMap.emplace(genericInfo, instantiatedDecl);
    instantiatedDeclsMap[genericDecl].insert(instantiatedDecl);

    if (curPkg->TestAttr(Attribute::TOOL_ADD)) {
        return instantiatedDecl; // When the package is create by cjogen, the sub nodes do not need to be walked.
    }
    // Collect extend decls by usage.
    RecordExtend(*instantiatedDecl);
    Walker(instantiatedDecl, instantiationWalkerID, instantiator, contextReset).Walk();
    return instantiatedDecl;
}

Ptr<Decl> GIM::GenericInstantiationManagerImpl::GetInstantiatedDeclWithGenericInfo(
    const GenericInfo& genericInfo, Package& pkg)
{
    curPkg = &pkg;
    auto instantiatedDecl = GetInstantiatedDeclWithGenericInfo(genericInfo);
    Walker(instantiatedDecl, rearrangeWalkerID, rearranger, contextReset).Walk();
    return instantiatedDecl;
}

Ptr<Decl> GIM::GenericInstantiationManagerImpl::FindInCache(const GenericInfo& info)
{
    auto found = declInstantiationByTypeMap.equal_range(info);
    // Only using instantiated decl in current package.
    // If the first and the second of the range is not equal, it means the target GI can be found in cache.
    for (auto it = found.first; it != found.second; ++it) {
        auto instantiatedDecl = it->second;
        CJC_ASSERT(instantiatedDecl != nullptr);
        if (instantiatedDecl->fullPackageName != curPkg->fullPackageName) {
            continue;
        }
        return instantiatedDecl;
    }
    return nullptr;
}

void GIM::GenericInstantiationManagerImpl::WalkNonGenericExtendedType()
{
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    auto extends = typeManager.GetBoxUsedExtends();
    for (auto extend : extends) {
        // Should have same ignore condition with 'CollectExtendedInterfaceHelper'.
        // UG fix here do not box decls any more.
        bool ignore = extend->TestAttr(Attribute::GENERIC) || extend->fullPackageName != curPkg->fullPackageName;
        if (ignore) {
            continue;
        }
        for (auto& type : extend->inheritedTypes) {
            Walker(type.get(), instantiationWalkerID, instantiator, contextReset).Walk();
        }
    }
    auto decls = typeManager.GetBoxedNonGenericDecls();
    for (auto id : decls) {
        for (auto& type : id->inheritedTypes) {
            Walker(type.get(), instantiationWalkerID, instantiator, contextReset).Walk();
        }
    }
#endif
}

void GIM::GenericInstantiationManagerImpl::InstantiateGenericDeclWithInstTys(
    Decl& decl, const std::vector<Ptr<Ty>>& instTys)
{
    // Don't instantiate for type alias decl and builtin decl.
    if (decl.astKind == ASTKind::TYPE_ALIAS_DECL || decl.IsBuiltIn()) {
        return;
    }

    // Target of self call may be rearrange to wrong decl during instantiation. eg:
    // func foo<T> () {
    //     foo<Int32>()
    // }
    auto generalDecl = GetGeneralDecl(decl);
    auto genericTy = GetDeclTy(*generalDecl);
    if (!Ty::IsTyCorrect(genericTy)) {
        InternalError("generic instantiation failed");
        return;
    }

    if (!generalDecl->GetGeneric() || !RequireInstantiation(*generalDecl)) {
        if (!decl.TestAnyAttr(Attribute::IMPORTED, Attribute::GENERATED_TO_MOCK)) {
            return; // If decl is in current package and not mocked, it must be walked by instantiator.
        }
        bool canSkip = curPkg->TestAttr(Attribute::INCRE_COMPILE) && !decl.toBeCompiled && needCompile;
        // If 'canSkip' is true, all decls used by current decl must be unchanged,
        // so the related instantiations does not need be recompiled.
        IncrementalContext changingContext(needCompile, canSkip);
        // If the given decl is not generic decl, checking for following cases:
        // 1. If the 'generalDecl' is existed in 'srcImportedNonGenericDecls', also checking the instantiation for it
        // (including default param function).
        // 2. Otherwise only walk class/interface decls' inherited types.
        if (auto fd = As<ASTKind::FUNC_DECL>(generalDecl); fd && fd->funcBody && !fd->funcBody->paramLists.empty()) {
            for (auto& param : fd->funcBody->paramLists[0]->params) {
                Ptr<Decl> func = param->desugarDecl.get();
                if (func && func->TestAttr(Attribute::SRC_IMPORTED)) {
                    usedSrcImportedDecls.emplace(func);
                    Walker(func, instantiationWalkerID, instantiator, contextReset).Walk();
                }
            }
        }
        // Used imported source decl need to be checked for instantiation
        // since this kind of decl will be re-generated in current package.
        if (generalDecl->TestAttr(Attribute::SRC_IMPORTED)) {
            usedSrcImportedDecls.emplace(generalDecl);
            // Walk inside decl to instantiate all used generics.
            Walker(&decl, instantiationWalkerID, instantiator, contextReset).Walk();
        }
        return;
    }
    // 2. If the type arguments is empty, can not trigger generic instantiation.
    // If type arguments still has generic parameter, need to wait for the instantiation of the generic parameters.
    if (instTys.empty() || Ty::ExistGeneric(instTys)) {
        return;
    }
    // Create struct GenericInfo contains the information of the raw generic function.
    // Do instantiation.
    (void)GetInstantiatedDeclWithGenericInfo(ConstructGenericInfo(*generalDecl, instTys));
}

Ptr<Decl> GIM::GenericInstantiationManagerImpl::GetGeneralDecl(Decl& decl, bool getOriginal) const
{
    auto target = GetRealTarget(&decl);
    // If decl is toplevel or nested function or haven't been instantiated, directly returns generic decl or itself.
    if (!target->outerDecl || !target->outerDecl->genericDecl || target->outerDecl->IsFunc()) {
        return target->genericDecl ? target->genericDecl : target;
    }
    // If decl is a generic member, return itself.
    if (target->TestAttr(Attribute::GENERIC) && !getOriginal) {
        return target;
    }
    bool isEnumCtor = target->TestAttr(Attribute::ENUM_CONSTRUCTOR);
    // If getOriginal is on, we found original generic version of the target.
    // Else: For generic member, we need use current outerDecl to find generic version of 'target'.
    auto typeDecl = target->outerDecl;
    if ((getOriginal && target->outerDecl->genericDecl) || isEnumCtor || !target->GetGeneric()) {
        typeDecl = target->outerDecl->genericDecl;
    }
    return PartialInstantiation::GetGeneralDecl(decl);
    /*
    auto& targetMembers = isEnumCtor ? RawStaticCast<EnumDecl*>(typeDecl)->constructors : typeDecl->GetMemberDecls();
    auto& srcMembers =
        isEnumCtor ? RawStaticCast<EnumDecl*>(target->outerDecl)->constructors : target->outerDecl->GetMemberDecls();
    // NOTE: Filter primary constructors and members generated by the compiler for test purposes, to get fixed members.
    std::vector<Ptr<Decl>> srcDecls = GetRealIndexingMembers(srcMembers);
    std::vector<Ptr<Decl>> targetDecls = GetRealIndexingMembers(targetMembers);
    CJC_ASSERT(srcDecls.size() == targetDecls.size());
    // Found current decls's index in its owner structure declaration.
    // Get it's origin version by add offset to generic structure declaration's members.
    auto [isGetter, realDecl] = GetRealMemberDecl(*target);
    realDecl = realDecl->genericDecl ? realDecl->genericDecl : realDecl;
    for (auto it = srcDecls.begin(); it != srcDecls.end(); ++it) {
        if (*it == realDecl) {
            auto matchDecl = *(targetDecls.begin() + std::distance(srcDecls.begin(), it));
            return GetUsedMemberDecl(*matchDecl, isGetter);
        }
    }
    return target;
    */
}

/**
 * 'candidates' contains the parent decl and member offset of interface's implementation function.
 * Returns the valid instantiated implementation member.
 */
Ptr<Decl> GIM::GenericInstantiationManagerImpl::SelectTypeMatchedImplMember(
    Ty& ty, const FuncDecl& interfaceFunc, std::vector<std::pair<Ptr<Decl>, size_t>>& candidates, Ty& targetBaseTy)
{
    Ptr<Decl> member = nullptr;
    for (auto [baseDecl, offset] : candidates) {
        auto structDecl = baseDecl;
        auto instantiatedDecls = GetInstantiatedDecls(*baseDecl);
        for (auto decl : instantiatedDecls) {
            if (typeManager.IsSubtype(&ty, GetDeclTy(*decl), false)) {
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
                structDecl = decl;
#endif
                break;
            }
        }
        auto implMember = GetMemberByOffset(*structDecl, offset);
        if (implMember->astKind == ASTKind::PROP_DECL || IsVirtualMember(*implMember)) {
            member = implMember;
            break;
        }
        auto iFuncTy = StaticCast<FuncTy*>(interfaceFunc.ty);
        if (interfaceFunc.TestAttr(Attribute::GENERIC)) {
            TypeSubst typeMapping = typeManager.GenerateGenericMappingFromGeneric(interfaceFunc, *implMember);
            iFuncTy = StaticCast<FuncTy*>(typeManager.GetInstantiatedTy(iFuncTy, typeMapping));
        } else if (interfaceFunc.outerDecl && interfaceFunc.outerDecl->TestAttr(Attribute::GENERIC)) {
            TypeSubst typeMapping = GenerateTypeMappingByTy(interfaceFunc.outerDecl->ty, &targetBaseTy);
            if (auto outerDecl = implMember->outerDecl; outerDecl && outerDecl->TestAttr(Attribute::GENERIC)) {
                auto tmpMapping = GenerateTypeMappingByTy(implMember->outerDecl->ty, &ty);
                auto rts = GetReversedTypeSubst(tmpMapping);
                TyGeneralizer tg(typeManager, rts);
                for (auto it = typeMapping.begin(); it != typeMapping.end(); ++it) {
                    it->second = tg.Generalize(it->second);
                }
            }
            iFuncTy = StaticCast<FuncTy*>(typeManager.GetInstantiatedTy(iFuncTy, typeMapping));
        }
        // Found related instantiated decls, if not exist, using the found member.
        auto usableDecls = GetInstantiatedDecls(*implMember);
        if (usableDecls.empty()) {
            usableDecls.emplace(implMember);
        }
        auto found = std::find_if(usableDecls.begin(), usableDecls.end(), [this, iFuncTy](auto it) {
            auto fty = DynamicCast<FuncTy*>(it->ty);
            return fty && iFuncTy && typeManager.IsFuncParameterTypesIdentical(*fty, *iFuncTy);
        });
        if (found != usableDecls.end()) {
            member = *found;
            break;
        }
    }
    return member;
}

Ptr<FuncDecl> GIM::GenericInstantiationManagerImpl::FindImplFuncForAbstractFunc(Ty& ty, FuncDecl& fd, Ty& targetBaseTy)
{
    // Rearrange for interface or open class call. Ignore function with following conditions:
    // 1. non-member function;
    // 2. non-static function which is accessed with interface type.
    // 3. member function accessed by class type which function belongs to.
    auto baseTyDecl = Ty::GetDeclPtrOfTy<InheritableDecl>(&ty);
    bool shouldIgnore = !fd.outerDecl || (ty.IsInterface() && !fd.TestAttr(Attribute::STATIC)) ||
        (ty.IsClass() && baseTyDecl == Ty::GetDeclPtrOfTy<InheritableDecl>(fd.outerDecl->ty));
    if (shouldIgnore) {
        return &fd;
    }
    // Get base ty: primitive ty, non-generic ty or generic ty of instantiated ty.
    auto baseTy = typeManager.GetTyForExtendMap(ty);
    auto genericFd = StaticCast<FuncDecl*>(GetGeneralDecl(fd, true));
    if (genericFd->genericDecl) {
        genericFd = StaticCast<FuncDecl*>(genericFd->genericDecl);
    }

    auto keyPair = std::make_pair(baseTy, genericFd);
    auto foundAbstFunc = abstractFuncToDeclMap.find(keyPair);
    if (foundAbstFunc == abstractFuncToDeclMap.end()) {
        return &fd;
    }
    std::vector<std::pair<Ptr<AST::Decl>, size_t>> candidates;
    bool accessByInterface = ty.IsInterface();
    std::copy_if(foundAbstFunc->second.begin(), foundAbstFunc->second.end(), std::back_inserter(candidates),
        [this, accessByInterface, &fd](auto& it) {
            bool notInSameDecl = it.first != GetGeneralDecl(*fd.outerDecl, true);
            bool instInInterface = it.first->astKind == ASTKind::INTERFACE_DECL;
            // Member access through the interface, looking up only the implementation from the interface.
            if (accessByInterface) {
                return notInSameDecl && instInInterface;
            }
            return notInSameDecl;
        });
    Ptr<Decl> member = SelectTypeMatchedImplMember(ty, fd, candidates, targetBaseTy);
    if (!member) {
        return &fd;
    }
    return As<ASTKind::FUNC_DECL>(GetUsedMemberDecl(*member, fd.isGetter));
}

Ptr<AST::Decl> GIM::GenericInstantiationManagerImpl::ReinstantiatedPartialMemberDecl(
    const GenericInfo& genericInfo, Decl& structDecl, Decl& genericMember, size_t memberIndex)
{
    // Re-instantiate generic member and replace the version stored in 'structDecl'.
    // Since partially instantiated generic member must never be referenced, the replacement is allowed.
    VisitFunc instantiateType = [this, &genericInfo, &structDecl](const Node& genericNode, Node& clonedNode) {
        PerformTyInstantiationDuringClone(genericNode, clonedNode, genericInfo, {});
        if (auto decl = DynamicCast<Decl*>(&clonedNode); decl && decl->outerDecl == genericInfo.decl) {
            decl->outerDecl = &structDecl; // Update outerDecl from generic to instantiated.
        }
    };
    auto clonedDecl = PartialInstantiation::Instantiate<Decl>(&genericMember, instantiateType);
    if (!clonedDecl) {
        InternalError("generic instantiation failed");
        return &genericMember;
    }
    clonedDecl->EnableAttr(Attribute::GENERIC_INSTANTIATED);
    auto partialDecl = clonedDecl.get();
    auto& members = structDecl.GetMemberDecls();
    members[memberIndex] = std::move(clonedDecl);
    return partialDecl;
}

Ptr<Decl> GIM::GenericInstantiationManagerImpl::GetInstantiatedMemberTarget(Ty& baseTy, Decl& target, bool inRearrange)
{
    auto declTy = GetDeclTy(*target.outerDecl);
    bool ignore =
        !target.outerDecl->generic || !target.outerDecl->TestAttr(Attribute::GENERIC) || !Ty::IsTyCorrect(declTy);
    if (ignore) {
        // Same non-generic judging condition with 'BuildAbstractFuncMap'.
        return &target;
    }
    auto promotedTys = promotion.Promote(baseTy, *declTy);
    auto realStructTy = promotedTys.empty() ? TypeManager::GetInvalidTy() : *promotedTys.begin();
    if (realStructTy->HasInvalidTy() || realStructTy->HasGeneric()) {
        // Shall we really return here if realStructTy->HasGeneric()?
        return &target;
    }
    auto genericInfo = ConstructGenericInfo(*target.outerDecl, typeManager.GetTypeArgs(*realStructTy));
    auto structDecl = FindInCache(genericInfo);
    // If current in instantiation step, instantiate structure declaration if not found.
    if (!structDecl && curTriggerNode && curTriggerNode->visitedByWalkerID == instantiationWalkerID) {
        structDecl = GetInstantiatedDeclWithGenericInfo(genericInfo);
    }
    auto [isGetter, targetToFind] = GetRealMemberDecl(target);
    auto foundIndex = membersIndexMap.find(targetToFind);
    // Function's outerDecl must been instantiated.
    if (!structDecl || foundIndex == membersIndexMap.end()) {
        if (!inRearrange) {
            InternalError("generic instantiation failed");
        }
        return &target;
    }
    auto& members = target.TestAttr(Attribute::ENUM_CONSTRUCTOR) ? RawStaticCast<EnumDecl*>(structDecl)->constructors
                                                                 : structDecl->GetMemberDecls();
    auto tmpTarget = targetToFind.get();
    auto found = std::find_if(members.begin(), members.end(),
        [tmpTarget](auto& member) { return PartialInstantiation::GetGeneralDecl(*member) == tmpTarget; });
    if (found == members.end()) {
        return &target;
    }
    Ptr<Decl> resultDecl = found->get();
    CJC_ASSERT(resultDecl);
    // NOTE: If member decl has both 'GENERIC', 'GENERIC_INSTANTIATED' an 'UNREACHABLE' attributes,
    // means current is imported partially instantiated. It needs to be replaced by re-instantiated version.
    if (resultDecl->TestAttr(Attribute::GENERIC, Attribute::GENERIC_INSTANTIATED, Attribute::UNREACHABLE)) {
        resultDecl = ReinstantiatedPartialMemberDecl(genericInfo, *structDecl, *targetToFind, foundIndex->second);
    }
    resultDecl = GetUsedMemberDecl(*resultDecl, isGetter);
    return resultDecl;
}

void GIM::GenericInstantiationManagerImpl::GenericMemberAccessInstantiate(MemberAccess& ma)
{
    for (auto& it : ma.typeArguments) {
        Walker walkArg(it.get(), instantiationWalkerID, instantiator, contextReset);
        walkArg.Walk();
    }
    Walker walkBase(ma.baseExpr.get(), instantiationWalkerID, instantiator, contextReset);
    walkBase.Walk();
    auto invalid = !ma.target || !ma.baseExpr || !Ty::IsTyCorrect(ma.ty);
    if (invalid || ma.target->astKind == ASTKind::PACKAGE_DECL || TestManager::IsMockAccessor(*ma.target)) {
        return;
    }
    auto target = GetRealTarget(ma.target);
    if (target->IsBuiltIn() || !RequireInstantiation(*target)) {
        return;
    }
    auto instTys = ma.instTys;
    // If target is constructor, instantiate outer structure declaration.
    if (IsClassOrEnumConstructor(*target)) {
        target = target->outerDecl;
        CJC_ASSERT(target != nullptr);
        auto baseTy = ma.ty->IsFunc() ? RawStaticCast<FuncTy*>(ma.ty)->retTy : ma.ty;
        instTys = typeManager.GetTypeArgs(*baseTy);
    }
    // For following conditions, we need to instantiate parent decl first.
    // 1. For function in generic structure declaration. Found partially instantiated decl.
    // 2. For member call of generic decl's member.
    bool needInstantiateParent = target->outerDecl && target->outerDecl->IsNominalDecl() && ma.baseExpr->ty &&
        (IsGenericInGenericStruct(*target) || target->outerDecl->TestAttr(Attribute::GENERIC));
    if (needInstantiateParent) {
        auto baseTy = Ty::IsTyCorrect(ma.matchedParentTy) ? ma.matchedParentTy : ma.baseExpr->ty;
        target = GetInstantiatedMemberTarget(*baseTy, *target);
    }
    InstantiateGenericDeclWithInstTys(*target, instTys);
    if (!Ty::IsTyCorrect(ma.baseExpr->ty)) {
        return;
    }
    if (target->IsFunc() && target->TestAttr(Attribute::GENERIC)) {
        // Also try to instantiate implementation function of generic interface function.
        target = FindImplFuncForAbstractFunc(*ma.baseExpr->ty, *StaticCast<FuncDecl*>(target), *ma.baseExpr->ty);
        InstantiateGenericDeclWithInstTys(*target, instTys);
    } else if (!IsInDeclWithAttribute(*target, Attribute::GENERIC) && ma.isExposedAccess && target->IsFunc()) {
        // Searching for upper bound call of non-generic access to collect used inline functions earlier.
        target = FindImplFuncForAbstractFunc(*ma.baseExpr->ty, *StaticCast<FuncDecl*>(target), *ma.baseExpr->ty);
        InstantiateGenericDeclWithInstTys(*target, instTys);
    }
}

void GIM::GenericInstantiationManagerImpl::GenericRefExprInstantiate(RefExpr& re)
{
    for (auto& it : re.typeArguments) {
        Walker walkArg(it.get(), instantiationWalkerID, instantiator, contextReset);
        walkArg.Walk();
    }
    // Generic type decleration do not need to be instantiated.
    if (!re.ref.target || re.ref.target->astKind == ASTKind::PACKAGE_DECL || !Ty::IsTyCorrect(re.ty) ||
        re.ref.target->astKind == ASTKind::GENERIC_PARAM_DECL) {
        return;
    }
    auto target = GetRealTarget(re.ref.target);
    if (target->IsBuiltIn() || !RequireInstantiation(*target)) {
        return;
    }
    auto instTys = re.instTys;
    // If target is constructor, instantiate outer structure declaration.
    if (IsClassOrEnumConstructor(*target)) {
        target = re.ref.target->outerDecl;
        CJC_ASSERT(target != nullptr);
        auto baseTy = re.ty->IsFunc() ? RawStaticCast<FuncTy*>(re.ty)->retTy : re.ty;
        instTys = typeManager.GetTypeArgs(*baseTy);
    }
    // For function in generic structure declaration,
    // found decl inside the instantiated outerDecl which is not current context decl.
    bool needInstantiateParent = target->outerDecl && target->outerDecl->IsNominalDecl() &&
        (IsGenericInGenericStruct(*target) || target->outerDecl->TestAttr(Attribute::GENERIC));
    auto structDecl = structContext.empty() ? target->outerDecl : GetStructDeclByContext();
    Ptr<Ty> baseTy = nullptr;
    if (structDecl != target->outerDecl) {
        baseTy = GetDeclTy(*structDecl);
        CJC_NULLPTR_CHECK(baseTy);
    }
    if (needInstantiateParent && baseTy != nullptr) {
        target = GetInstantiatedMemberTarget(*baseTy, *target);
    }
    InstantiateGenericDeclWithInstTys(*target, instTys);
    if (baseTy != nullptr && target->IsFunc() && target->TestAttr(Attribute::GENERIC)) {
        // Also try to instantiate implementation function of generic interface function.
        target = FindImplFuncForAbstractFunc(*baseTy, *StaticCast<FuncDecl*>(target), *baseTy);
        InstantiateGenericDeclWithInstTys(*target, instTys);
    }
}

void GIM::GenericInstantiationManagerImpl::GenericTyExtendInstantiate(Ty& ty)
{
    if (ty.HasGeneric()) {
        return;
    }
    auto decl = Ty::GetDeclPtrOfTy<InheritableDecl>(&ty);
    if (decl) {
        InstantiateGenericDeclWithInstTys(*decl, ty.typeArgs);
    }

    auto extends = typeManager.GetTyUsedExtends(&ty);
    if (extends.empty()) {
        return;
    }
    for (auto it : extends) {
        if (!RequireInstantiation(*it)) {
            continue;
        }
        auto extendTy = GetDeclTy(*it);
        auto promoteRes = promotion.Promote(ty, *extendTy);
        CJC_ASSERT(!promoteRes.empty()); // The 'ty' must have promoted result with extended type.
        auto promotedTy = *promoteRes.begin();
        bool ignored = !extendTy || !typeManager.CheckExtendWithConstraint(ty, it);
        if (ignored) {
            continue;
        }
        InstantiateGenericDeclWithInstTys(*it, typeManager.GetTypeArgs(*promotedTy));
    }
}

void GIM::GenericInstantiationManagerImpl::InstantiateGenericTysForMemoryLayout(const Ty& ty)
{
    if (ty.HasGeneric()) {
        return;
    }
    for (auto it : ty.typeArgs) {
        CJC_NULLPTR_CHECK(it);
        if (!it->typeArgs.empty()) {
            InstantiateGenericTysForMemoryLayout(*it);
        }
    }
    auto decl = Ty::GetDeclPtrOfTy<InheritableDecl>(&ty);
    if (decl) {
        InstantiateGenericDeclWithInstTys(*decl, ty.typeArgs);
    }
}

void GIM::GenericInstantiationManagerImpl::GenericTypeInstantiate(const Type& type)
{
    // Ignore invalid type node & partially typealias node.
    // If the type contains generic ty, current type node may be typealias substituted node.
    if (!Ty::IsTyCorrect(type.ty) || HasIntersectionTy(*type.ty) || type.ty->HasGeneric()) {
        return;
    }
    // Instantiated type only by sema type (also for typeAlias substituted cases).
    std::function<void(Ptr<Ty>)> instantiateType = [this, &instantiateType](auto ty) {
        for (auto tyArg : ty->typeArgs) {
            instantiateType(tyArg);
        }
        auto target = Ty::GetDeclPtrOfTy(ty);
        if (!target || !RequireInstantiation(*target)) {
            return;
        }
        InstantiateGenericDeclWithInstTys(*target, ty->typeArgs);
    };
    instantiateType(type.ty);
}

void GIM::GenericInstantiationManagerImpl::GenericArrayExprInstantiate(const ArrayExpr& ae)
{
    // VArray does not have initFunc and does not need to be instantiated.
    if (Ty::IsTyCorrect(ae.ty) && ae.ty->kind != TypeKind::TYPE_ARRAY) {
        return;
    }
    auto arrayTy = RawStaticCast<ArrayTy*>(ae.ty);
    if (ae.initFunc != nullptr) {
        auto typeArgs = typeManager.GetTypeArgs(*arrayTy);
        InstantiateGenericDeclWithInstTys(*ae.initFunc, typeArgs);
    }
}

void GIM::GenericInstantiationManagerImpl::GenericArrayLitInstantiate(ArrayLit& al)
{
    for (auto& it : al.children) {
        Walker walkElement(it.get(), instantiationWalkerID, instantiator, contextReset);
        walkElement.Walk();
    }
    auto target = Ty::GetDeclPtrOfTy(al.ty);
    if (!target) {
        return;
    }

    InstantiateGenericDeclWithInstTys(*target, al.ty->typeArgs);
}

VisitAction GIM::GenericInstantiationManagerImpl::CheckVisitedNode(Ptr<Node> node, bool checkGeneric)
{
    if (diag.GetErrorCount() != 0) {
        // Error happens, stop walker.
        return VisitAction::STOP_NOW;
    }

    if (!node || (!checkGeneric && node->TestAttr(Attribute::GENERIC))) {
        // We should ignore current node if it:
        // - is invalid,
        // - is generic and we won't check generic.
        return VisitAction::SKIP_CHILDREN;
    }

    if (ignoreKinds.count(node->astKind) > 0) {
        // Current not should be ignored.
        return VisitAction::SKIP_CHILDREN;
    }

    // Ignore compiler added placeholder argument for function's default parameter.
    // NOTE: This should not be translated by CHIR, and never been used.
    if (node->astKind == ASTKind::FUNC_ARG && node->TestAttr(Attribute::HAS_INITIAL)) {
        return VisitAction::SKIP_CHILDREN;
    }

    if (auto decl = DynamicCast<Decl*>(node); decl) {
        if (decl->astKind == ASTKind::CLASS_DECL && decl->identifier.Val().find(BOX_DECL_PREFIX) == 0) {
            // Current decl is a boxed decl, should be skipped.
            return VisitAction::SKIP_CHILDREN;
        }
        // If walk into a structure declaration or generic decl inside generic structure declaration. Update context.
        if (NeedSwitchContext(*decl)) {
            structContext.push_back(decl);
        }
    }

    return VisitAction::WALK_CHILDREN;
}

VisitAction GIM::GenericInstantiationManagerImpl::CheckNodeInstantiation(Node& node)
{
    // Should not walk generated instantiated decls and source imported decls.
    if (auto pkg = DynamicCast<Package*>(&node); pkg) {
        for (auto& it : pkg->files) {
            Walker(it.get(), instantiationWalkerID, instantiator, contextReset).Walk();
        }
        return VisitAction::SKIP_CHILDREN;
    }
    // Validate input node, quit if invalid.
    VisitAction action = CheckVisitedNode(&node);
    if (action != VisitAction::WALK_CHILDREN) {
        return action;
    }

    if (auto expr = DynamicCast<Expr*>(&node); expr && expr->desugarExpr) {
        Walker(expr->desugarExpr.get(), instantiationWalkerID, instantiator, contextReset).Walk();
        return VisitAction::SKIP_CHILDREN;
    }

    curTriggerNode = &node;
    if (Ty::IsTyCorrect(node.ty) && !HasIntersectionTy(*node.ty)) {
        // For memory layout, instantiated all used generics in node's ty.
        InstantiateGenericTysForMemoryLayout(*node.ty);
        // Extends of type should be instantied by usage.
        GenericTyExtendInstantiate(*node.ty);
    }
    switch (node.astKind) {
        case ASTKind::REF_EXPR: {
            GenericRefExprInstantiate(*StaticAs<ASTKind::REF_EXPR>(&node));
            return VisitAction::SKIP_CHILDREN;
        }
        case ASTKind::MEMBER_ACCESS: {
            GenericMemberAccessInstantiate(*StaticAs<ASTKind::MEMBER_ACCESS>(&node));
            return VisitAction::SKIP_CHILDREN;
        }
        case ASTKind::REF_TYPE:
        case ASTKind::QUALIFIED_TYPE: {
            GenericTypeInstantiate(*StaticAs<ASTKind::TYPE>(&node));
            return VisitAction::SKIP_CHILDREN;
        }
        case ASTKind::ARRAY_EXPR: {
            GenericArrayExprInstantiate(*StaticAs<ASTKind::ARRAY_EXPR>(&node));
            return VisitAction::WALK_CHILDREN;
        }
        case ASTKind::ARRAY_LIT: {
            GenericArrayLitInstantiate(*StaticAs<ASTKind::ARRAY_LIT>(&node));
            return VisitAction::SKIP_CHILDREN;
        }
        default:
            return VisitAction::WALK_CHILDREN;
    }
}

VisitAction GIM::GenericInstantiationManagerImpl::RearrangeReferencePtr(Node& node)
{
    VisitAction action = CheckVisitedNode(&node, true);
    if (action != VisitAction::WALK_CHILDREN) {
        return action;
    }

    if (auto expr = DynamicCast<Expr*>(&node); expr && expr->desugarExpr) {
        Walker(expr->desugarExpr.get(), rearrangeWalkerID, rearranger, contextReset).Walk();
        ClearInstTysIsNeeded(node);
        return VisitAction::SKIP_CHILDREN;
    }

    curTriggerNode = &node;
    switch (node.astKind) {
        case ASTKind::CALL_EXPR:
            // Rearrange call expr.
            RearrangeCallExprReference(*StaticAs<ASTKind::CALL_EXPR>(&node));
            return VisitAction::WALK_CHILDREN;
        case ASTKind::REF_EXPR:
            // Rearrange ref expr.
            RearrangeRefExprReference(*StaticAs<ASTKind::REF_EXPR>(&node));
            return VisitAction::WALK_CHILDREN;
        case ASTKind::MEMBER_ACCESS:
            RearrangeMemberAccessReference(*StaticAs<ASTKind::MEMBER_ACCESS>(&node));
            return VisitAction::WALK_CHILDREN;
        case ASTKind::REF_TYPE:
        case ASTKind::QUALIFIED_TYPE:
            RearrangeTypeReference(*StaticAs<ASTKind::TYPE>(&node));
            return VisitAction::SKIP_CHILDREN;
        case ASTKind::ARRAY_EXPR:
            RearrangeArrayExprReference(*StaticAs<ASTKind::ARRAY_EXPR>(&node));
            return VisitAction::WALK_CHILDREN;
        case ASTKind::ARRAY_LIT:
            RearrangeArrayLitReference(*StaticAs<ASTKind::ARRAY_LIT>(&node));
            return VisitAction::WALK_CHILDREN;
        case ASTKind::FUNC_BODY:
            RearrangeFuncBodyReference(*StaticAs<ASTKind::FUNC_BODY>(&node));
            return VisitAction::WALK_CHILDREN;
        case ASTKind::TYPE_PATTERN:
        case ASTKind::TUPLE_PATTERN:
        case ASTKind::ENUM_PATTERN:
            UpdateTypePatternMatchResult(*StaticAs<ASTKind::PATTERN>(&node));
            return VisitAction::WALK_CHILDREN;
        default:
            return VisitAction::WALK_CHILDREN;
    }
}

void GIM::GenericInstantiationManagerImpl::RearrangeCallExprReference(CallExpr& ce)
{
    if (!ce.resolvedFunction || !ce.baseFunc || !ce.baseFunc->IsReferenceExpr()) {
        return;
    }
    Walker(ce.baseFunc.get(), rearrangeWalkerID, rearranger, contextReset).Walk();
    Ptr<Decl> target = ce.baseFunc->GetTarget(); // Get re-arranged target.
    // Sema guarantees: base's target not null when 'resolvedFunction' is not null and they are pointing to same decl.
    CJC_NULLPTR_CHECK(target);
    ce.resolvedFunction = StaticCast<FuncDecl*>(target);
    if (target->ty->HasGeneric() || target->ty->IsGeneric()) {
        return;
    }
    if (!HasJavaAttr(*target)) {
        ce.ty = StaticCast<FuncTy*>(target->ty)->retTy;
    }
    // Deal for dynamic 'This' binding call.
    // eg: tests/LLT/Sema/class/ThisType/class_generic_dynamic_binding_thistype_ok_2.cj
    if (auto thisTy = DynamicCast<ClassThisTy*>(ce.ty); thisTy && ce.baseFunc->astKind == ASTKind::MEMBER_ACCESS) {
        ce.ty = StaticAs<ASTKind::MEMBER_ACCESS>(ce.baseFunc.get())->baseExpr->ty;
    }
}

Ptr<Decl> GIM::GenericInstantiationManagerImpl::GetInstantiatedTarget(
    Ty& baseTy, Decl& target, const std::vector<Ptr<Ty>>& instTys, Ptr<Ty> upperTy)
{
    auto genericDecl = GetGeneralDecl(target);
    if (genericDecl->IsBuiltIn()) {
        return &target;
    }

    // Rearrange toplevel decls or nested function decls.
    if (genericDecl->IsNominalDecl() || !genericDecl->outerDecl || !genericDecl->outerDecl->IsNominalDecl()) {
        if (!genericDecl->GetGeneric() || !genericDecl->TestAttr(Attribute::GENERIC)) {
            // Target is not a generic decl.
            return genericDecl;
        }
        Ptr<Decl> instantiatedDecl = FindInCache(ConstructGenericInfo(*genericDecl, instTys));
        if (!instantiatedDecl) {
            return &target;
        }
        return instantiatedDecl;
    }
    auto targetBase = Ty::IsTyCorrect(upperTy) && !upperTy->HasGeneric() ? upperTy : Ptr(&baseTy);
    auto decl = GetInstantiatedMemberTarget(*targetBase, *genericDecl, true);
    if (decl && decl->TestAttr(Attribute::GENERIC)) {
        decl = FindInCache(ConstructGenericInfo(*decl, instTys));
    }
    // Decl may be an interface function which should be rearranged to implementation decl.
    if (decl && decl->IsFunc()) {
        decl = FindImplFuncForAbstractFunc(baseTy, *RawStaticCast<FuncDecl*>(decl), *targetBase);
        CJC_NULLPTR_CHECK(decl);
        // Get instantiated version if decl has been updated to implented generic function.
        if (decl->TestAttr(Attribute::GENERIC)) {
            decl = FindInCache(ConstructGenericInfo(*decl, instTys));
        }
    }
    if (!decl) {
        return &target;
    }
    return decl;
}

void GIM::GenericInstantiationManagerImpl::RearrangeRefExprReference(RefExpr& re)
{
    // Generic type decleration do not need to be instantiated.
    if (!Ty::IsTyCorrect(re.ty) || !re.ref.target || re.ref.target->IsBuiltIn() ||
        re.ref.target->astKind == ASTKind::GENERIC_PARAM_DECL || !RequireInstantiation(*re.ref.target)) {
        return;
    }
    auto baseTy = re.ty;
    auto instTys = (re.isThis || re.isSuper) ? typeManager.GetTypeArgs(*re.ty) : re.instTys;
    if (IsClassOrEnumConstructor(*re.ref.target)) {
        baseTy = baseTy->IsFunc() ? RawStaticCast<FuncTy*>(baseTy)->retTy : baseTy;
        instTys = typeManager.GetTypeArgs(*baseTy);
    } else if (!structContext.empty()) {
        auto structDecl = GetStructDeclByContext();
        // For member used inside owner decl, the base type is type of current structure declaration.
        baseTy = GetDeclTy(*structDecl);
    }
    re.ref.target = GetInstantiatedTarget(*baseTy, *re.ref.target, instTys, re.matchedParentTy);
    // If the target of RefExpr has been instantiated, we should clear the field instTys of the latter.
    ClearInstTysIsNeeded(re);
}

void GIM::GenericInstantiationManagerImpl::RearrangeMemberAccessReference(MemberAccess& ma)
{
    Walker(ma.baseExpr.get(), rearrangeWalkerID, rearranger, contextReset).Walk();
    // BaseExpr of member access may be package decl which does no have sema type.
    if (!ma.target || !ma.baseExpr || Ty::IsInitialTy(ma.ty) || ma.target->IsBuiltIn()) {
        return;
    }
    // MemberAccess's base may be package decl which does not have sema type.
    auto baseTy = Ty::IsTyCorrect(ma.baseExpr->ty) ? ma.baseExpr->ty : ma.ty;
    auto instTys = ma.instTys;
    if (IsClassOrEnumConstructor(*ma.target)) {
        baseTy = baseTy->IsFunc() ? RawStaticCast<FuncTy*>(baseTy)->retTy : baseTy;
        instTys = typeManager.GetTypeArgs(*baseTy);
    }
    ma.target = GetInstantiatedTarget(*baseTy, *ma.target, instTys, ma.matchedParentTy);
    /**
     * For such case:
     * @Java
     * class Sample<T> {
     *     var t: T
     *     init(t: T) { this.t = t }
     * }
     *
     * @Java
     * class A {
     *     var i: Int32
     *     init(i: Int32) { this.i = i }
     * }
     *
     * main() {
     *     var a = A(1)
     *     var s = Sample<A>(a)
     *     println(s.t.i) <============== here
     * }
     * Because `Sample<T>` has been generic erase to `Sample_JObject`, the target of `s.t` is type of `JObject`,
     * but actually it should be `A` here, so we don't update its `ty` to keep it `A`.
     */
    ClearInstTysIsNeeded(ma);
}

void GIM::GenericInstantiationManagerImpl::RearrangeTypeReference(Type& type)
{
    // Ignore invalid type node & partially typealias node.
    // If the type contains generic ty, current type node may be typealias substituted node.
    if (!Ty::IsTyCorrect(type.ty) || HasIntersectionTy(*type.ty) || type.ty->HasGeneric()) {
        return;
    }
    auto target = Ty::GetDeclPtrOfTy(type.ty);
    if (!target) {
        return;
    }
    auto instantiatedDecl = GetInstantiatedTarget(*type.ty, *target, type.ty->typeArgs);
    // Type only will be RefType or QualifiedType, guaranteed by caller.
    if (type.astKind == ASTKind::REF_TYPE) {
        auto& rt = static_cast<RefType&>(type);
        rt.ref.target = instantiatedDecl;
    } else {
        auto& qt = static_cast<QualifiedType&>(type);
        qt.target = instantiatedDecl;
    }
}

void GIM::GenericInstantiationManagerImpl::RearrangeArrayExprReference(ArrayExpr& ae)
{
    if (!ae.initFunc || !Ty::IsTyCorrect(ae.ty)) {
        return;
    }
    auto decl = FindInCache(ConstructGenericInfo(*ae.initFunc, typeManager.GetTypeArgs(*ae.ty)));
    if (!decl) {
        return;
    }
    ae.initFunc = RawStaticCast<FuncDecl*>(decl);
}

void GIM::GenericInstantiationManagerImpl::RearrangeArrayLitReference(ArrayLit& al)
{
    if (al.initFunc && Ty::IsTyCorrect(al.ty)) {
        al.initFunc = RawStaticCast<FuncDecl*>(GetInstantiatedTarget(*al.ty, *al.initFunc, al.ty->typeArgs));
    }
}

void GIM::GenericInstantiationManagerImpl::RearrangeFuncBodyReference(FuncBody& fb)
{
    if (structContext.empty()) {
        return;
    }

    auto structDecl = GetStructDeclByContext();
    auto baseTy = GetDeclTy(*structDecl);
    if (fb.parentClassLike) {
        fb.parentClassLike =
            RawStaticCast<ClassLikeDecl*>(GetInstantiatedTarget(*baseTy, *fb.parentClassLike, baseTy->typeArgs));
    } else if (fb.parentStruct) {
        fb.parentStruct =
            RawStaticCast<StructDecl*>(GetInstantiatedTarget(*baseTy, *fb.parentStruct, baseTy->typeArgs));
    } else if (fb.parentEnum) {
        fb.parentEnum = RawStaticCast<EnumDecl*>(GetInstantiatedTarget(*baseTy, *fb.parentEnum, baseTy->typeArgs));
    }
}

void GIM::GenericInstantiationManagerImpl::UpdateTypePatternMatchResult(Pattern& pattern)
{
    if (!Ty::IsTyCorrect(pattern.ty)) {
        return;
    }
    // Define the process of updating match result of typePattern.
    auto update = [this](auto targetTy, auto pattern) {
        auto tp = As<ASTKind::TYPE_PATTERN>(pattern);
        if (!tp || tp->matchBeforeRuntime) {
            return;
        }
        tp->matchBeforeRuntime = typeManager.IsSubtype(targetTy, tp->type->ty, true, false);
        // Only set 'needRuntimeTypeCheck' true if both selector type and pattern type are classlike type.
        tp->needRuntimeTypeCheck =
            !tp->matchBeforeRuntime && tp->type->ty && IsNeedRuntimeCheck(typeManager, *targetTy, *tp->type->ty);
    };
    // Define the process of checking child patterns of current pattern level.
    auto checkChildren = [&update](auto tys, auto& patterns) {
        CJC_ASSERT(tys.size() == patterns.size());
        for (size_t i = 0; i < patterns.size(); ++i) {
            update(tys[i], patterns[i].get());
        }
    };
    // Perform updating for current pattern level. More nesting pattern will be checked during rearrange process later.
    if (auto tuplePattern = DynamicCast<TuplePattern*>(&pattern); tuplePattern) {
        auto tupleTy = DynamicCast<TupleTy*>(tuplePattern->ty);
        if (tupleTy) {
            checkChildren(tupleTy->typeArgs, tuplePattern->patterns);
        }
    } else if (auto enumPattern = DynamicCast<EnumPattern*>(&pattern); enumPattern) {
        CJC_NULLPTR_CHECK(enumPattern->constructor);
        auto funcTy = DynamicCast<FuncTy*>(enumPattern->constructor->ty);
        if (funcTy) {
            checkChildren(funcTy->paramTys, enumPattern->patterns);
        }
    } else if (pattern.ctxExpr) {
        update(pattern.ctxExpr->ty, &pattern);
    }
}

/**
 * Remove built-in binary/unary operation's desugar after instantiation and type rearranged.
 * eg: func test<T>(a: T, b: T) where T <: Equatable<T> {
 *         a == b
 *     }
 * 'a == b' will be desugared as call expr after sema check. But if we have 'test<Int64>' then
 * 'a == b' is actually 'Int64' comparing with 'Int64' which can be directly generated as binary expr.
 * so, recover this kind of call expr to original binary/unary expr in this function.
 */
void GIM::GenericInstantiationManagerImpl::RecoverDesugarForBuiltIn() const
{
    for (auto& it : curPkg->genericInstantiatedDecls) {
        // the 'ce' should not be used after recover, so walking in postorder.
        Walker(it.get(), nullptr, [](auto node) {
            auto ce = DynamicCast<CallExpr*>(node);
            bool mayBeBuiltIn = ce && ce->resolvedFunction && ce->resolvedFunction->outerDecl;
            if (!mayBeBuiltIn) {
                return VisitAction::WALK_CHILDREN;
            }
            auto fd = ce->resolvedFunction;
            Ty* thisTy = fd->outerDecl->ty;
            FuncTy* funcTy = DynamicCast<FuncTy*>(fd->ty);
            if (ce->baseFunc && ce->baseFunc->astKind == ASTKind::MEMBER_ACCESS) {
                thisTy = StaticCast<MemberAccess*>(ce->baseFunc.get())->baseExpr->ty;
                CJC_ASSERT(ce->baseFunc->ty->IsFunc());
                funcTy = StaticCast<FuncTy*>(ce->baseFunc->ty);
            }
            if (!funcTy || !thisTy) {
                return VisitAction::WALK_CHILDREN;
            }
            // For desugared callExpr in source package we directly recover to original expression.
            // For imported callExpr, we desugar it to binary/unary expression.
            if (funcTy->paramTys.size() == 1 && funcTy->paramTys[0] &&
                TypeCheckUtil::IsBuiltinBinaryExpr(fd->op, *thisTy, *funcTy->paramTys[0])) {
                if (ce->sourceExpr) {
                    if (auto bin = DynamicCast<BinaryExpr>(ce->sourceExpr)) {
                        RecoverToBinaryExpr(*bin);
                    }
                    // otherwise sourceExpr can be a compound assignment expr, e.g. v |= v, where v is of ty Int64, and
                    // | is overloaded operator from an interface that Int64 extends
                } else {
                    auto be = MakeOwnedNode<BinaryExpr>();
                    be->leftExpr = std::move(StaticCast<MemberAccess*>(ce->baseFunc.get())->baseExpr);
                    be->rightExpr = std::move(ce->args[0]->expr);
                    be->op = fd->op;
                    be->ty = ce->ty;
                    CopyBasicInfo(ce, be.get());
                    AddCurFile(*be, ce->curFile);
                    ce->desugarExpr = std::move(be);
                }
            } else if (funcTy->paramTys.empty() && TypeCheckUtil::IsBuiltinUnaryExpr(fd->op, *thisTy)) {
                if (ce->sourceExpr) {
                    if (auto bin = DynamicCast<UnaryExpr>(ce->sourceExpr)) {
                        RecoverToUnaryExpr(*bin);
                    }
                } else {
                    auto ue = MakeOwnedNode<UnaryExpr>();
                    ue->expr = std::move(StaticCast<MemberAccess*>(ce->baseFunc.get())->baseExpr);
                    ue->op = fd->op;
                    ue->ty = ce->ty;
                    CopyBasicInfo(ce, ue.get());
                    AddCurFile(*ue, ce->curFile);
                    ce->desugarExpr = std::move(ue);
                }
            }
            return VisitAction::SKIP_CHILDREN;
        }).Walk();
    }
}
