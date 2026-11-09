// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file manages type check information that should be controlled by scope.
 */

#ifndef CANGJIE_SEMA_EXTRASCOPES_H
#define CANGJIE_SEMA_EXTRASCOPES_H

#include "cangjie/Sema/TypeManager.h"
#include "cangjie/AST/ASTContext.h"
#include "TypeCheckerImpl.h"
#include "Diags.h"

namespace Cangjie {
// scope of introducing placeholder type var
class TyVarScope {
public:
    explicit TyVarScope(TypeManager& tyMgr);
    ~TyVarScope();

    TyVarScope(const TyVarScope& other) = delete;
    TyVarScope& operator =(const TyVarScope& other) = delete;

private:
    friend class TypeManager;
    void AddTyVar(Ptr<AST::GenericsTy> tyVar);
    std::vector<Ptr<AST::GenericsTy>> tyVars;
    TypeManager& tyMgr;
    std::string scope;
};

// scope of type instantiation context
class InstCtxScope {
public:
    explicit InstCtxScope(TypeChecker::TypeCheckerImpl& typeChecker);
    ~InstCtxScope();

    /**
     * If we are using FuncA within FuncB, then current decl is FuncB,
     * referenced decl is FuncA.
     */
    // generate mapping between decl and its instantiated ty
    void SetRefDecl(const AST::Decl& decl, AST::ModalTy instTy);
    // generate all needed mappings with all available info for a CallExpr,
    // ty vars remaining to be solved are not mapped in inst map
    bool SetRefDecl(ASTContext& ctx, AST::FuncDecl& fd, AST::CallExpr& ce);
    // simply generate u2i mapping for all universal ty vars used for a CallExpr
    void SetRefDeclSimple(const AST::FuncDecl& fd, const AST::CallExpr& ce);

    InstCtxScope(const InstCtxScope& other) = delete;
    InstCtxScope& operator =(const InstCtxScope& other) = delete;

private:
    friend class TypeManager;
    SubstPack curMaps; // mapping only from current decl
    SubstPack refMaps; // mapping only from referenced decl
    SubstPack maps; // merged mapping, users should use this

    TypeManager& tyMgr;
    DiagnosticEngine& diag;
    TypeChecker::TypeCheckerImpl& typeChecker;

    bool GenerateExtendGenericTypeMapping(
        const ASTContext& ctx, const AST::FuncDecl& fd, const AST::CallExpr& ce, SubstPack& typeMapping);
    bool GenerateTypeMappingByCallContext(
        const ASTContext& ctx, const AST::FuncDecl& fd, const AST::CallExpr& ce, SubstPack& typeMapping);
    void GenerateSubstPackByTyArgs(
        SubstPack& tmaps, const std::vector<Ptr<AST::Type>>& typeArgs, const AST::Generic& generic) const;
};
}

#endif
