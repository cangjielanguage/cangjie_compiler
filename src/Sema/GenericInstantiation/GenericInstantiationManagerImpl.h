// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * GenericInstantiationManager is the global manager to maintain the generic information.
 */

#ifndef CANGJIE_SEMA_GENERIC_INSTANTIATION_MANAGER_IMPL_H
#define CANGJIE_SEMA_GENERIC_INSTANTIATION_MANAGER_IMPL_H

#include "Promotion.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Frontend/CompilerInstance.h"
#include "cangjie/Sema/CommonTypeAlias.h"
#include "cangjie/Sema/GenericInstantiationManager.h"
#include "cangjie/Sema/TestManager.h"
#include "cangjie/Sema/TypeManager.h"
#include "cangjie/Utils/ProfileRecorder.h"

namespace Cangjie {
/** GenericInfoHash is the hash to indicate the uniqueness of a GenericInfo. */
struct GenericInfoHash {
    size_t operator()(const GenericInfo& info) const
    {
        size_t ret = 0;
        ret = hash_combine<Ptr<const AST::Decl>>(ret, info.decl);
        for (auto n : info.gTyToTyMap) {
            ret = hash_combine<Ptr<AST::Ty>>(ret, n.first);
            ret = hash_combine<Ptr<AST::Ty>>(ret, n.second);
        }
        return ret;
    }
};

/** GenericInfoEqual is used to distinguish two GenericInfo when hash conflict encountered. */
struct GenericInfoEqual {
    bool operator()(const GenericInfo& lhs, const GenericInfo& rhs) const
    {
        if ((lhs.decl != rhs.decl) || (lhs.gTyToTyMap.size() != rhs.gTyToTyMap.size())) {
            return false;
        }
        for (auto it1 : lhs.gTyToTyMap) {
            auto it2 = rhs.gTyToTyMap.find(it1.first);
            if (it2 == rhs.gTyToTyMap.end()) {
                return false;
            } else {
                if (it1.second != it2->second) {
                    return false;
                }
            }
        }
        return true;
    }
};

/**
 * The class of generic instantiation manager is a global manager that
 * maintains cache of generic and instantiated decls' information.
 */
using Generic2InsMap = std::unordered_map<Ptr<const AST::Decl>, std::unordered_set<Ptr<AST::Decl>>>;

class GenericInstantiationManager::GenericInstantiationManagerImpl {
public:
    explicit GenericInstantiationManagerImpl(CompilerInstance& ci);
    ~GenericInstantiationManagerImpl()
    {
        curPkg = nullptr;
        curTriggerNode = nullptr;
        testManager = nullptr;
    }
    /** Generic instantiation package entrance. */
    void GenericInstantiatePackage(AST::Package& pkg);
    /**
     * Get the instantiated decl corresponding to the genericInfo:
     * @param genericInfo [in] generic decl instantiation parameters.
     * @param pkg [in] current processing package. MUST given, if call this api outside genericInstantiation step.
     */
    Ptr<AST::Decl> GetInstantiatedDeclWithGenericInfo(const GenericInfo& genericInfo, AST::Package& pkg);
    /** Get set of instantiated decl of given generic decl */
    std::unordered_set<Ptr<AST::Decl>> GetInstantiatedDecls(const AST::Decl& genericDecl) const
    {
        auto found = instantiatedDeclsMap.find(&genericDecl);
        return found == instantiatedDeclsMap.end() ? std::unordered_set<Ptr<AST::Decl>>{} : found->second;
    }

    /** Prepare for generic instantiation processing:
     *  1. clear all cache generated before.
     *  2. pre-build context cache.
     */
    void ResetGenericInstantiationStage()
    {
        ClearCache();
        // Build generic instantiateManager to rebuild instantiated cache and genericNodeToInstantiatedNodeMap.
        RebuildGenericInstantiationManager();
        Utils::ProfileRecorder recorder("ResetGenericInstantiationStage", "BuildAbstractFuncMap");
        // Build abstract function map for all type which inherited interface.
        BuildAbstractFuncMap();
    }

    Generic2InsMap GetAllGenericToInsDecls() const;

    friend class InstantiatedExtendRecorder;
    friend class MockUtils;

private:
    Ptr<AST::Package> curPkg{nullptr};
    DiagnosticEngine& diag;
    ImportManager& importManager;
    TypeManager& typeManager;
    TestManager* testManager{nullptr};
    Promotion promotion;
    /** Unified walker ID for instantiation. */
    unsigned instantiationWalkerID;
    /** Unified walker ID for rearrange. */
    unsigned rearrangeWalkerID;
    /** Current compiling backend. */
    Triple::BackendType backend;
    /** The node which triggered current instantiation. */
    Ptr<AST::Node> curTriggerNode{nullptr};
    /** Lambda function for instantiation walker. */
    std::function<AST::VisitAction(Ptr<AST::Node>)> instantiator;
    /** Lambda function for rearrangement walker. */
    std::function<AST::VisitAction(Ptr<AST::Node>)> rearranger;
    std::function<AST::VisitAction(Ptr<AST::Node>)> contextReset;
    /** A map stores the original generic decl and all its instantiated decls. */
    Generic2InsMap instantiatedDeclsMap;
    /** Key: generic decl & instantiated types. Value: instantiated decl. */
    std::unordered_multimap<GenericInfo, Ptr<AST::Decl>, GenericInfoHash, GenericInfoEqual> declInstantiationByTypeMap;
    /**
     * This map saves the information of the function in which structure declaration implements the abstract function in
     * interface.
     * The key is pair of type and the abstract function in interface.
     * The value is a set that contains pair of:
     *    1. the structure declaration which contains the function which implements abstract function.
     *    2. the index of the implementation function in structure declaration.
     * */
    std::unordered_map<std::pair<Ptr<AST::Ty>, Ptr<AST::FuncDecl>>,
        std::unordered_set<std::pair<Ptr<AST::Decl>, size_t>, HashPair>, HashPair>
        abstractFuncToDeclMap;
    std::unordered_map<Ptr<AST::Decl>, size_t> membersIndexMap;
    /** Node kinds which should be ignored in walker. */
    inline static const std::unordered_set<AST::ASTKind> ignoreKinds = {
        AST::ASTKind::GENERIC_PARAM_DECL,
        AST::ASTKind::GENERIC_CONSTRAINT,
        AST::ASTKind::PRIMARY_CTOR_DECL,
        AST::ASTKind::MODIFIER,
    };
    std::vector<Ptr<AST::Decl>> structContext;
    /** Key: sema type; Value: whether contains intersection ty. */
    std::unordered_map<Ptr<AST::Ty>, bool> intersectionTyStatus;
    /** Mark whether current type has instantiated related extends for package in gim. */
    std::unordered_set<std::pair<Ptr<AST::Ty>, std::string>, HashPair> extendGenerated;
    /** Store the source imported decls which are checked with instantiation status. */
    std::unordered_set<Ptr<const AST::Decl>> usedSrcImportedDecls;
    /** Used for incremental compilation, decide whether new created instantiation need to be compiled. */
    bool needCompile = true;

    /** Implement working flow for incremental compiling package. */
    void InstantiateForIncrementalPackage();

    /** Rebuild type stored decl pointer for imported instantiated decls. */
    void RestoreInstantiatedDeclTy() const;
    void RestoreInstantiatedDeclTy(AST::Decl& decl) const;
    void RebuildGenericInstantiationManager();
    void WalkImportedInstantiations(const std::function<void(AST::Decl&)>& processFunc,
        const std::function<bool(AST::Package&)>& skipChecker) const;
    void UpdateInstantiatedExtendMap();
    void ClearCache();
    void RecordExtend(AST::Node& node);
    /**
     * Since cjnative backend only generate instantiated decls as local symbols,
     * we need to remove decls in other package which have same type of instantiation in current package.
     */
    void ClearImportedUnusedInstantiatedDecls();
    void RecoverDesugarForBuiltIn() const;
    /** Build type mapping from instantiated decl. */
    TypeSubst BuildTypeMapping(const AST::Decl& instantiatedDecl) const;
    /**
     * Check whether the genericInfo is found in the declInstantiationByTypeMap
     * and the decl is instantiated in dependent package of current package.
     */
    Ptr<AST::Decl> FindInCache(const GenericInfo& info);
    /** Construct GenericInfo. */
    GenericInfo ConstructGenericInfo(AST::Decl& decl, const std::vector<Ptr<AST::Ty>>& instTys) const;
    void AppendGenericMemberMap(const AST::Decl& genericDecl,
        const std::unordered_set<Ptr<AST::Decl>>& insNominalDecls, Generic2InsMap& result) const;
    /**
     * Get the instantiated decl corresponding to the genericInfo:
     * 1. Find in the declInstantiationByTypeMap, if found, there is no need to instantiate again.
     * 2. If not found in declInstantiationByTypeMap, perform instantiation according to the genericInfo.
     * 3. After the generic decl is instantiated, all its extend decls need to be instantiate, too.
     * @param genericInfo [in] generic decl instantiation parameters.
     */
    Ptr<AST::Decl> GetInstantiatedDeclWithGenericInfo(const GenericInfo& genericInfo);
    /**
     * Instantiate a generic decl @p genericDecl with type arguments @p instTys.
     */
    void InstantiateGenericDeclWithInstTys(AST::Decl& decl, const std::vector<Ptr<AST::Ty>>& instTys);
    /**
     * Walk inherited types which are used by non-generic be boxed extend decls.
     * Used to guarantee all related generic types are instantiated.
     */
    void WalkNonGenericExtendedType();
    /** Helper functions during clone instantiated decl. */
    void PerformTyInstantiationDuringClone(
        const AST::Node& genericNode, AST::Node& clonedNode, const GenericInfo& info, const TypeSubst& g2gTyMap);
    void PerformUpdateAttrDuringClone(AST::Node& genericNode, AST::Node& clonedNode) const;
    /** Find implemented version function of abstract function @p interfaceFunc in the decl of Ty @p ty. */
    Ptr<AST::FuncDecl> FindImplFuncForAbstractFunc(AST::Ty& ty, AST::FuncDecl& fd, AST::Ty& targetBaseTy);
    /** Find matched implementation function of abstract function @p fd from candidates */
    Ptr<AST::Decl> SelectTypeMatchedImplMember(AST::Ty& ty, const AST::FuncDecl& interfaceFunc,
        std::vector<std::pair<Ptr<AST::Decl>, size_t>>& candidates, AST::Ty& targetBaseTy);
    /** Walker function for node instantiation. */
    AST::VisitAction CheckNodeInstantiation(AST::Node& node);
    /** Walker function for reference pointer rearrangement. */
    AST::VisitAction RearrangeReferencePtr(AST::Node& node);
    AST::VisitAction CheckVisitedNode(Ptr<AST::Node> node, bool checkGeneric = false);

    /** Instantiate generic MemberAccess @p ma. */
    void GenericMemberAccessInstantiate(AST::MemberAccess& ma);
    /** Instantiate generic RefExpr @p re. */
    void GenericRefExprInstantiate(AST::RefExpr& re);
    /** Instantiate generic RefType or QualifiedType @p type. */
    void GenericTypeInstantiate(const AST::Type& type);
    /** Instantiate generic ArrayExpr @p ae. */
    void GenericArrayExprInstantiate(const AST::ArrayExpr& ae);
    /** Instantiate generic ArrayList @p al which has Struct-Array type. */
    void GenericArrayLitInstantiate(AST::ArrayLit& al);
    /** Instantiate extend of generic sema type @p ty 's extends. */
    void GenericTyExtendInstantiate(AST::Ty& ty);
    void InstantiateGenericTysForMemoryLayout(const AST::Ty& ty);
    /** Instantiate imported partial instantiated member decl. */
    Ptr<AST::Decl> ReinstantiatedPartialMemberDecl(
        const GenericInfo& genericInfo, AST::Decl& structDecl, AST::Decl& genericMember, size_t memberIndex);

    /** Rearrange the ptr of outer references' target to the instantiated decl. */
    void RearrangeTypeReference(AST::Type& type);
    void RearrangeCallExprReference(AST::CallExpr& ce);
    void RearrangeRefExprReference(AST::RefExpr& re);
    void RearrangeMemberAccessReference(AST::MemberAccess& ma);
    void RearrangeArrayExprReference(AST::ArrayExpr& ae);
    void RearrangeArrayLitReference(AST::ArrayLit& al);
    void RearrangeFuncBodyReference(AST::FuncBody& fb);
    void UpdateTypePatternMatchResult(AST::Pattern& pattern);
    Ptr<AST::Decl> GetInstantiatedTarget(
        AST::Ty& baseTy, AST::Decl& target, const std::vector<Ptr<AST::Ty>>& instTys, Ptr<AST::Ty> upperTy = nullptr);
    Ptr<AST::Decl> GetInstantiatedMemberTarget(AST::Ty& baseTy, AST::Decl& target, bool inRearrange = false);

    /** Build interface function to implemented function map for all type decls */
    void BuildAbstractFuncMap();
    void BuildAbstractFuncMapHelper(AST::Ty& ty);
    bool IsImplementationFunc(AST::Ty& ty, const AST::FuncDecl& interfaceFunc, const AST::FuncDecl& fd);
    MultiTypeSubst GetTypeMapping(Ptr<AST::Ty>& baseTy, AST::Ty& interfaceTy);
    void MapFuncWithDecl(AST::Ty& ty, AST::FuncDecl& interfaceFunc, const AST::FuncDecl& target);
    void CollectDeclMemberFuncs(AST::Decl& decl, std::unordered_set<Ptr<AST::FuncDecl>>& funcs) const;
    std::unordered_set<Ptr<AST::FuncDecl>> GetInheritedMemberFuncs(AST::Ty& ty);
    std::unordered_set<Ptr<AST::InheritableDecl>> GetInheritedInterfaces(AST::Ty& ty);
    /** Collect inherited members for imported decls which is not check during sema stage. */
    std::unordered_set<Ptr<AST::FuncDecl>> MergeMemberFuncs(
        AST::Ty& ty, AST::Decl& decl, const std::unordered_set<Ptr<AST::FuncDecl>>& inheritedMembers);
    std::unordered_set<Ptr<AST::FuncDecl>> CollectInheritedMembers(AST::Ty& ty, AST::Decl& decl);
    std::unordered_set<Ptr<AST::FuncDecl>> CollectInheritedMembersVisit(
        AST::Ty& ty, AST::Decl& decl, std::set<std::pair<Ptr<AST::Ty>, Ptr<AST::Decl>>>& visited);

    /**
     * Get general version of given @p decl. If getOriginal is on:
     *  Get original generic version of the 'decl'.
     * Else:
     *  1. if decl is toplevel decl or nested function, return it's original decl that input may be instantiated.
     *  2. if decl is a generic member decl, return itself since decl may be partial instantiated.
     *  3. if decl is a non-generic member, but outerDecl is instantiated decl, find origin one in generic outerDecl.
     */
    Ptr<AST::Decl> GetGeneralDecl(AST::Decl& decl, bool getOriginal = false) const;

    Ptr<AST::Decl> GetStructDeclByContext()
    {
        // Caller guarantees 'structContext' not empty.
        auto decl = structContext.back();
        while (decl && !decl->IsNominalDecl()) {
            decl = decl->outerDecl;
        }
        return decl;
    }

    bool HasIntersectionTy(AST::Ty& ty)
    {
        auto found = intersectionTyStatus.find(&ty);
        if (found != intersectionTyStatus.end()) {
            return found->second;
        }
        if (ty.IsIntersection()) {
            intersectionTyStatus.emplace(&ty, true);
            return true;
        }
        for (auto typeArg : ty.typeArgs) {
            if (typeArg && HasIntersectionTy(*typeArg)) {
                intersectionTyStatus.emplace(&ty, true);
                return true;
            }
        }
        intersectionTyStatus.emplace(&ty, false);
        return false;
    }
    void RemoveFromCache(AST::Decl& decl)
    {
        auto genericDecl = decl.genericDecl;
        instantiatedDeclsMap[genericDecl].erase(&decl);
        GenericInfo genericInfo(genericDecl, BuildTypeMapping(decl));
        auto decls = declInstantiationByTypeMap.equal_range(genericInfo);
        for (auto it = decls.first; it != decls.second; ++it) {
            if (it->second == &decl) {
                declInstantiationByTypeMap.erase(it);
                break;
            }
        }
    }
    bool IsDeclCanRestoredForTy(const AST::Decl& decl) const;
    class IncrementalContext {
    public:
        explicit IncrementalContext(bool& reCompile, bool unchanged) : needReCompile(reCompile), prevStatus(reCompile)
        {
            needReCompile = unchanged ? false : needReCompile;
        }
        ~IncrementalContext()
        {
            needReCompile = prevStatus;
        }
    private:
        bool& needReCompile;
        bool prevStatus;
    };
};
} // namespace Cangjie

#endif // CANGJIE_SEMA_GENERIC_INSTANTIATION_MANAGER_IMPL_H
