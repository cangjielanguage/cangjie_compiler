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

#ifndef CANGJIE_SEMA_GENERIC_INSTANTIATION_MANAGER_H
#define CANGJIE_SEMA_GENERIC_INSTANTIATION_MANAGER_H

#include "cangjie/AST/Node.h"
#include "cangjie/Frontend/CompilerInstance.h"
#include "cangjie/Sema/CommonTypeAlias.h"

namespace Cangjie {
/** GenericInfo contains the information of the generic decl used to identify a generic decl when instantiated. */
struct GenericInfo {
    Ptr<AST::Decl> decl;      /**< The raw generic decl. */
    TypeSubst gTyToTyMap; /**< The map between the generic ty to instantiated ty. */
    GenericInfo(const Ptr<AST::Decl> decl, const TypeSubst& map) : decl(decl), gTyToTyMap(map)
    {
    }
};

/**
 * The class of generic instantiation manager is a global manager that
 * maintains cache of generic and instantiated decls' information.
 */
class GenericInstantiationManager {
public:
    explicit GenericInstantiationManager(CompilerInstance& ci);
    ~GenericInstantiationManager();
    /** Generic instantiation package entrance. */
    void GenericInstantiatePackage(AST::Package& pkg) const;
    /**
     * Get the instantiated decl corresponding to the genericInfo:
     * @param genericInfo [in] generic decl instantiation parameters.
     * @param pkg [in] current processing package. MUST given, if call this api outside genericInstantiation step.
     */
    Ptr<AST::Decl> GetInstantiatedDeclWithGenericInfo(const GenericInfo& genericInfo, AST::Package& pkg) const;
    /** Get set of instantiated decl of given generic decl */
    std::unordered_set<Ptr<AST::Decl>> GetInstantiatedDecls(const AST::Decl& genericDecl) const;

    /** Prepare for generic instantiation processing:
     *  1. clear all cache generated before.
     *  2. pre-build context cache.
     */
    void ResetGenericInstantiationStage() const;
    std::unordered_map<Ptr<const AST::Decl>, std::unordered_set<Ptr<AST::Decl>>> GetAllGenericToInsDecls() const;

    friend class MockUtils;

private:
    class InstantiatedExtendRecorder;
    class GenericInstantiationManagerImpl;
    std::unique_ptr<GenericInstantiationManagerImpl> impl;
};
} // namespace Cangjie
#endif
