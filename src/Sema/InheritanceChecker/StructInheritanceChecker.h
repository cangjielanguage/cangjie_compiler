// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares class for inheritance checking of structure declarations.
 */

#ifndef CANGJIE_SEMA_INHERITANCE_CHECKER_H
#define CANGJIE_SEMA_INHERITANCE_CHECKER_H

#include "MemberSignature.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Modules/ImportManager.h"
#include "cangjie/Sema/TypeManager.h"
#include <stack>

namespace Cangjie {
using namespace AST;

class StructInheritanceChecker {
public:
    StructInheritanceChecker(DiagnosticEngine& diag, TypeManager& manger, Package& pkg, ImportManager& importManager,
        const GlobalOptions& options)
        : diag(diag), typeManager(manger), pkg(pkg), importManager(importManager), opts{options}
    {
    }

    ~StructInheritanceChecker() = default;

    /**
     * Collect and check decls declared inside given pkg node tree when node is not imported.
     */
    void Check();

    std::unordered_map<Ptr<const InheritableDecl>, MemberMap> MoveStructInheritedMembers();

private:
    void CheckMembersWithInheritedDecls(const InheritableDecl& decl);
    MemberMap GetAndCheckInheritedInterfaces(const InheritableDecl& decl);
    MemberMap GetInheritedSuperMembers(
        const InheritableDecl& decl, ModalTy baseTy, const AST::File& curFile, bool ignoreExtends = false);
    MemberMap GetAndCheckInheritedMembers(const InheritableDecl& decl);
    void CollectExtendByInterfaceInherit(const std::set<Ptr<ExtendDecl>>& otherExtends, const ExtendDecl& curDecl,
        std::set<Ptr<ExtendDecl>, CmpNodeByPos>& ordered);
    std::optional<bool> DeterminingSkipExtendByInheritanceRelationship(
        const AST::ExtendDecl& curDecl, AST::ExtendDecl& ed, const Ptr<AST::Decl>& extendedDecl);
    std::pair<MemberMap, MemberMap> GetVisibleExtendMembersForExtend(const InheritableDecl& decl);
    void CheckExtendExportDependence(
        const InheritableDecl& curExtend, const MemberSignature& interface, const MemberMap& implDecl);
    void UpdateOverriddenFuncDeclCache(Ptr<Decl> child, Ptr<Decl> parent);
    void ComputeInconsistentPropTypes(const MemberSignature& child, const MemberSignature& parent,
        std::vector<ModalTy>& inconsistentTypes) const;
    MemberSignature UpdateInheritedMemberIfNeeded(
        MemberMap& inheritedMembers, const MemberSignature& child, bool inheritedInterfaces = false);
    bool ComputeInconsistentTypes(const MemberSignature& child, const MemberSignature& parent, MemberSignature& updated,
        const std::pair<bool, bool>& status, std::vector<AST::ModalTy>& inconsistentTypes) const;
    void MergeInheritedMembers(
        MemberMap& members, const MemberMap& otherMembers, DataTy structTy, bool inheritedInterfaces = false);
    void DiagnoseForOverriddenMember(const MemberSignature& child) const;
    void CheckInheritedMember(const MemberSignature& parent, const MemberSignature& child) const;
    void CheckInheritedInterfaces(const MemberSignature& interface, const MemberMap& implDecls) const;
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    void CheckIncompleteOverrideOrImplOfExtend(const MemberSignature& interface, const MemberSignature& child) const;
#endif
    void DiagnoseForUnimplementedInterfaces(const MemberMap& members, const Decl& structDecl);
    void DiagnoseForConflictInheritance(const InheritableDecl& decl, const MemberMap& members) const;
    void DiagnoseInheritedInsconsistType(const MemberSignature& member, const Node& node) const;
    void CheckSameNameInheritanceInfo(const MemberSignature& parent, const Decl& child) const;
    void CheckInheritanceAttributes(const MemberSignature& parent, const Decl& child) const;
    void CheckPropertyInheritance(const MemberSignature& parent, Decl& child) const;
    void CheckInheritedPropOverload(const MemberSignature& parent, const MemberSignature& child) const;
    void CheckGenericTypeArgInfo(const Decl& parent, const Decl& child);
    void CheckGenericTypeArgInfo(const MemberSignature& parent, const MemberSignature& child) const;
    void CheckInheritanceForInterface(const MemberSignature& interface, const MemberSignature& child) const;
    bool CheckPropImplRelation(const MemberSignature& parent, const MemberSignature& child) const;
    bool CheckFuncImplRelation(const MemberSignature& parent, const MemberSignature& child) const;
    void CheckMutModifierCompatible(const MemberSignature& parent, const Decl& child) const;
    void CheckAccessVisibility(const Decl& parent, const Decl& child, const Decl& diagNode) const;
    bool IsExtendedDefaultImpl(const MemberSignature& parent, const Decl& child) const;
    bool CheckExtendMemberValid(const MemberSignature& parent, const Decl& child) const;
    bool IsBuiltInOperatorFuncInExtend(const MemberSignature& member, const Decl& structDecl) const;
    bool CheckReturnOverrideByGeneric(const FuncTy& parentTy, const FuncTy& childTy) const;
    bool AreReturnTypesCompatible(
        const FuncTy& parentTy, const FuncTy& childTy, const std::unordered_set<ModalTy> inconsistentTypes = {}) const
    {
        bool isSubOfAll = std::all_of(inconsistentTypes.begin(), inconsistentTypes.end(),
            [&childTy, this](auto ty) { return typeManager.IsSubtype(childTy.retTy, ty, false); });
        if (!CheckReturnOverrideByGeneric(parentTy, childTy)) {
            return false;
        }
        // Return types are the same or return types have subtype relations.
        return isSubOfAll && typeManager.IsSubtype(childTy.retTy, parentTy.retTy, false);
    }
    void CheckAllUpperBoundsConfliction();
    void CheckUpperBoundsConfliction(const Generic& generic);

    /** Checks related to NativeFFI */
    void CheckNativeFFI(const MemberSignature& parent, const MemberSignature& child) const;

    /**
     * Generates the built-in operator function and adds to ed.members.
     * e.g. operator func -(): Int64 { return -this }
     */
    void CreateBuiltInUnaryOperatorFunc(TokenKind op, ExtendDecl& ed) const;
    /**
     * Generates the built-in operator function and adds to ed.members.
     * e.g. operator func +(right: Int64): Int64 { return this + right }
     */
    void CreateBuiltInBinaryOperatorFunc(TokenKind op, ModalTy rightTy, ExtendDecl& ed, TypeKind returnTyKind) const;
    /**
     * Walk source package and checking inside instantiated decls to find whether
     * there conflict members existing in instantiated nominal decls.
     */
    void CheckInstDupFuncsInNominalDecls();
    VisitAction CheckInstDupFuncsRecursively(Node& node);
    void CheckInstMemberSignatures(const InheritableDecl& decl, const std::vector<DataTy>& instTys);
    void CheckInstantiatedDecl(Decl& decl, const std::vector<DataTy>& instTys);
    /**
     * Get visible extend decls in stable order for given @p decl with @p instTys .
     */
    std::set<Ptr<ExtendDecl>, CmpNodeByPos> GetVisibleExtendsForInstantiation(
        const Decl& decl, const std::vector<DataTy>& instTys);
    /**
     * Check whether the given decl instantiation info may cause infinite instantiation.
     */
    bool WillCauseInfiniteInstantiation(const Node& triggerNode, const Decl& decl, const std::vector<DataTy>& instTys);
    void DiagnoseForInstantiatedMember(const MemberSignature& parent, const MemberSignature& child) const;

    /** CStruct checking methods. */
    void CheckInstWithCStructTypeArg(const Node& node);
    void CheckCStruct(ModalTy ty, const Type& typeArg);
    void CheckCStructArguments(const Node& node, AST::ModalTy ty, const Position& leftAnglePos,
        const std::vector<Ptr<Cangjie::AST::Type>>& typeArgs);
    void CheckCStructArgument(ModalTy ty, const Type& typeArg);

    bool IsExtendVisibleInCurpkg(const ExtendDecl& ed)
    {
        return !pkg.files.empty() && *pkg.files.begin() && importManager.IsExtendAccessible(**pkg.files.begin(), ed);
    }
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    std::vector<Ptr<const ExtendDecl>> GetAllNeedCheckExtended();
#endif

    DiagnosticEngine& diag;
    TypeManager& typeManager;
    Package& pkg;
    ImportManager& importManager; // Used to check whether the extend declaration is imported.
    const GlobalOptions& opts;

    std::unordered_map<Ptr<const InheritableDecl>, MemberMap> structInheritedMembers;
    std::vector<Ptr<const Decl>> checkingDecls;
    /** Following members are used to cache instantiation checking status. */
    std::set<std::pair<Ptr<const Decl>, const std::vector<DataTy>>> instantiatedDecls;
    std::map<std::pair<Ptr<const Decl>, const std::vector<DataTy>>, std::unordered_map<DataTy, DataTy>>
        instantiatedTyCache;
    /**
     * Used to cache instantiation checking status which need to report diagnoses.
     * key: generic decl with instantiated type, value: pair of member signatures to report diagnose.
     */
    std::map<std::pair<Ptr<const Decl>, const std::vector<DataTy>>,
        std::vector<std::pair<MemberSignature, MemberSignature>>>
        genericMembersForInstantiatedDecl;
    std::vector<std::tuple<Ptr<const Node>, Ptr<const Decl>, const std::vector<DataTy>>> instTriggerInfos;
    std::stack<TypeSubst> institutionMaps;
    bool infiniteInstantiationOccured{false};

    /**
     * Used for store & re-store context info for checking declaration status in instantiated status.
     */
    class InstantiatedContext {
    public:
        InstantiatedContext(StructInheritanceChecker& checker, Ptr<AST::Node> node, Ptr<const Decl> target,
            const std::vector<DataTy>& instTys, bool updateTrigger)
            : checker(checker), needUpdate(updateTrigger)
        {
            if (needUpdate) {
                checker.instTriggerInfos.emplace_back(std::make_tuple(node, target, instTys));
            }
        }
        ~InstantiatedContext()
        {
            if (needUpdate) {
                checker.instTriggerInfos.pop_back();
            }
        }

    private:
        StructInheritanceChecker& checker;
        bool needUpdate;
    };
};

std::vector<std::unordered_set<DataTy>> GetAllGenericUpperBounds(TypeManager& tyMgr, const Decl& decl);

void CheckGenericTypeBoundsMapped(const Decl& parent, const Decl& child,
    std::vector<std::unordered_set<DataTy>> parentBounds, std::vector<std::unordered_set<DataTy>> childBounds,
    DiagnosticEngine& diag, TypeManager& typeManager);

} // namespace Cangjie
#endif
