// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares a class for resolving and managing overridden member functions.
 */

#ifndef CANGJIE_SEMA_OVERRIDE_FUNCTION_RESOLVER_H
#define CANGJIE_SEMA_OVERRIDE_FUNCTION_RESOLVER_H

#include <string>
#include <unordered_map>
#include <unordered_set>

#include "cangjie/AST/Node.h"
#include "cangjie/Sema/TypeManager.h"

namespace Cangjie {

using MemberFuncsWithInstTys = std::unordered_map<Ptr<AST::FuncDecl>, std::unordered_set<Ptr<AST::FuncTy>>>;
using MemberFuncWithInstTys = std::pair<Ptr<AST::FuncDecl>, std::unordered_set<Ptr<AST::FuncTy>>>;

/**
 * A utility class to resolve and manage overridden member functions for a given type.
 * This class provides functionality to:
 * 1. Collect member functions from various sources (super interfaces, super classes, type itself, extend declarations)
 * 2. Handle function overriding and implementation relationships
 * 3. Manage function override sets and instantiation types
 * 4. Cache results for efficient lookup
 */
class OverrideFunctionResolver {
public:
    /**
     * Constructor.
     * @param typeManager The type manager to use.
     */
    explicit OverrideFunctionResolver(TypeManager& typeManager) : typeManager(&typeManager)
    {
    }

    /**
     * Get instantiated member functions with their instantiation types for a given instantiated type.
     * This function collects member functions from:
     * 1. Super interfaces of the base type
     * 2. Super classes of the base type
     * 3. The base type itself
     * 4. All extend declarations of the base type
     * 5. Super interfaces of all extend declarations
     * 6. Extend declarations themselves
     * OverrideFunctionResolver will get function decl with instTy by it's generic outerDecl.
     *
     * When merging member functions from extend declarations, if functions have the same type,
     * the existing function is preserved.
     *
     * eg1: open class A<T1> { func f<R>(a: T, b: R): Unit }
     *      class B<T2> <: A<T2> {}
     *   Got function decleration is 'func f<R>(a: T, b: R): Unit', and instTy is '(T2, R)->Unit'.
     *      class B <: A<C> {}
     *   Got function decleration is 'func f<R>(a: T, b: R): Unit', and instTy is '(C, R)->Unit'.
     *
     * @param instBaseTy The instantiated base type to get member functions from
     * @param identifier The name of the member functions to retrieve
     * @return The collection of retrieved member functions with their instantiation types
     */
    MemberFuncsWithInstTys GetInstMemberFuncWithInstTy(AST::DataTy instBaseTy, const std::string& identifier);

    Ptr<AST::Ty> GetMatchedFuncInstTyByGivenTarget(
        MemberFuncWithInstTys& candidates, const AST::FuncDecl& target, AST::DataTy targetBaseTy);

    /**
     * Clear the global cache.
     */
    static void ClearCache();

private:
    /**
     * Collect member functions from a declaration.
     */
    void CollectDeclMemberFunc(
        AST::Decl& decl, AST::DataTy instBaseTy, MemberFuncsWithInstTys& funcs, const std::string& identifier);

    /**
     * Get instantiated members from super types (interfaces or classes).
     */
    void GetInstMemberFromSuper(
        AST::DataTy instBaseTy, Ptr<AST::InheritableDecl> baseDecl, MemberFuncsWithInstTys& funcs,
        const std::string& identifier, bool isCheckingInterface);

    /**
     * Merge member functions from extend declarations, selecting the subclass version in case of conflicts.
     */
    void MergeExtendSuperMember(
        AST::DataTy instBaseTy, MemberFuncsWithInstTys& funcs, MemberFuncsWithInstTys& newFuncs);

    /**
     * Merge new functions into existing functions, avoiding duplicates.
     */
    void MergeIntoFuncs(MemberFuncsWithInstTys& funcs, MemberFuncsWithInstTys& newFuncs);

    /**
     * Check if two functions have an implementation/override relation.
     */
    bool IsImplementationFunc(const AST::FuncDecl& srcFunc, const AST::FuncDecl& superFunc,
        const Ptr<AST::FuncTy> srcInstFuncTy, const Ptr<AST::FuncTy> superInstFuncTy);

    /**
     * Remove supers of @p memberFunc that are implemented by it (with instantiation type
     * @p instMemberFuncTy) from @p funcs, since an overriding function hides its supers.
     */
    void RemoveImplementedSupers(
        AST::FuncDecl& memberFunc, const Ptr<AST::FuncTy> instMemberFuncTy, MemberFuncsWithInstTys& funcs);

    /**
     * Check whether @p newFunc (with instantiation type @p newFuncInstTy) is implemented by any
     * existing function in @p funcs. Used to skip already-implemented functions during merge.
     */
    bool IsImplementedByAny(
        const AST::FuncDecl& newFunc, const Ptr<AST::FuncTy> newFuncInstTy, const MemberFuncsWithInstTys& funcs);

    /**
     * Check whether @p newFunc (with instantiation type @p newFuncInstTy) is implemented by some existing
     * function in @p funcs along the subclass direction: i.e. some func implements it AND any leaf type of
     * that func's outer-decl promoted types is a subtype of any root type of @p newFuncOuterDeclInstTys.
     */
    bool IsImplementedInSameDirection(AST::DataTy instBaseTy, const AST::FuncDecl& newFunc,
        const Ptr<AST::FuncTy> newFuncInstTy, const std::set<AST::DataTy>& newFuncOuterDeclInstTys,
        const MemberFuncsWithInstTys& funcs);

    /**
     * Insert @p instTy into the set of @p funcDecl in @p funcs, creating the entry if absent.
     */
    void AddInstTyToFuncs(
        AST::FuncDecl* funcDecl, const Ptr<AST::FuncTy> instTy, MemberFuncsWithInstTys& funcs);

    // Dependencies
    TypeManager* typeManager = nullptr;

    // Global cache to store results for faster lookup
    static std::unordered_map<std::pair<AST::DataTy, std::string>, MemberFuncsWithInstTys, HashPair>
        instTy2MembersCache;
};

} // namespace Cangjie

#endif // CANGJIE_SEMA_OVERRIDE_FUNCTION_RESOLVER_H
