// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the TypeChecker related classes, which provides typecheck capabilities.
 */

#ifndef CANGJIE_SEMA_TYPECHECKER_H
#define CANGJIE_SEMA_TYPECHECKER_H

#include "cangjie/AST/ASTContext.h"
#include "cangjie/AST/Node.h"
#include "cangjie/Frontend/CompilerInstance.h"

namespace Cangjie {
class InstCtxScope;
class TypeChecker {
public:
    explicit TypeChecker(CompilerInstance* ci);
    ~TypeChecker();

    /**
     * Using control statement "for" to finish packages' typecheck. It invokes two functions as followed.
     * @see PrepareTypeCheck
     * @see TypeCheck
     */
    void TypeCheckForPackages(const std::vector<Ptr<AST::Package>>& pkgs) const;
    void SetOverflowStrategy(const std::vector<Ptr<AST::Package>>& pkgs) const;
    /**
     * Perform autobox and recursive type resolving of enum.
     */
    void PerformDesugarAfterInstantiation(ASTContext& ctx, AST::Package& pkg) const;

    // Desugar after sema.
    void PerformDesugarAfterSema(std::vector<Ptr<AST::Package>>& pkgs) const;

    /**
     * Synthesize the given @p expr in given @p scopeName and return the found candidate decls or types.
     * If the @p hasLocal is true, the target will be found from local scope firstly.
     * @param ctx cached sema context.
     * @param scopeName the scopeName of current position.
     * @param expr the expression waiting to found candidate decls or types.
     * @param hasLocalDecl whether the given expression is existed in the given scope.
     * @return found candidate decls or types.
     */
    Candidate SynReferenceSeparately(
        ASTContext& ctx, const std::string& scopeName, AST::Expr& expr, bool hasLocalDecl) const;
    /**
     * Remove members from @p targets that do not satisfy extensions of generic instantiated types.
     * @param baseTy specialized extended type.
     * @param targets all candidate members.
     */
    void RemoveTargetNotMeetExtendConstraint(const Ptr<AST::Ty> baseTy, std::vector<Ptr<AST::Decl>>& targets);

private:
    friend class InstCtxScope;
    /**
     * The class used to synthesize fuzzy results for LSP usage.
     */
    class Synthesizer;
    /** The class for checking enum sugar. */
    class EnumSugarChecker;
    class TypeCheckerImpl;
    std::unique_ptr<TypeCheckerImpl> impl;
}; // class TypeChecker
} // namespace Cangjie
#endif
