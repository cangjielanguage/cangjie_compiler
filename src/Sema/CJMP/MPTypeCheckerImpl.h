// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.
/**
 * @file
 *
 * This file declares the MPTypeCheckerImpl related classes, which provides typecheck capabilities for CJMP.
 */
#ifndef CANGJIE_SEMA_MPTYPECHECKER_IMPL_H
#define CANGJIE_SEMA_MPTYPECHECKER_IMPL_H

#include "ScopeManager.h"
#include "cangjie/AST/Node.h"
#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Frontend/CompilerInstance.h"
#include "cangjie/Sema/CommonTypeAlias.h"
#include "cangjie/Sema/TypeManager.h"

namespace Cangjie {
class MPTypeCheckerImpl {
public:
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    explicit MPTypeCheckerImpl(const CompilerInstance& ci);
    // PrepareTypeCheck for CJMP
    void PrepareTypeCheck4CJMP(AST::Package& pkg);
    void PrepareTypeCheck4CJMPExtension(CompilerInstance& ci, ScopeManager& scopeManager, ASTContext& ctx,
        const std::unordered_set<Ptr<AST::ExtendDecl>>& extends);
    // Precheck for CJMP
    void PreCheck4CJMP(const AST::Package& pkg);
    // TypeCheck for CJMP
    void RemoveCommonCandidatesIfHasSpecific(std::vector<Ptr<AST::FuncDecl>>& candidates);
    void CheckReturnAndVariableTypes(AST::Package& pkg);
    void ValidateMatchedAnnotationsAndModifiers(AST::Package& pkg);
    void CheckMatchedFunctionReturnTypes(AST::FuncDecl& specificFunc, AST::FuncDecl& commonFunc);
    void CheckMatchedVariableTypes(AST::VarDecl& specificVar, AST::VarDecl& commonVar);
    void MatchSpecificWithCommon(AST::Package& pkg);
    void CheckNotAllowedAnnotations(AST::Package& pkg);

    static void FilterOutCommonCandidatesIfSpecificExist(std::map<Names, std::vector<Ptr<AST::FuncDecl>>>& candidates);
    void MapCJMPGenericTypeArgs(TypeSubst& genericTyMap, const AST::Decl& commonDecl, const AST::Decl& specificDecl);
    void UpdateGenericTyInMemberFromCommon(TypeSubst& genericTyMap, Ptr<AST::Decl>& member);
    void UpdateSpecificMemberGenericTy(
        ASTContext& ctx, const std::function<std::vector<AST::Symbol*>(ASTContext&, AST::ASTKind)>& getSymsFunc);
#endif
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
private:
    // PrepareTypeCheck for CJMP
    void MergeCJMPNominalsExceptExtension(AST::Package& pkg);
    void MergeCJMPExtensions(CompilerInstance& ci, ScopeManager& scopeManager, ASTContext& ctx,
        const std::unordered_set<Ptr<AST::ExtendDecl>>& extends);
    // Precheck for CJMP
    void PreCheckCJMPClass(const AST::ClassDecl& cls);
    // PostTypeCheck for CJMP
    void CheckCommonExtensions(std::vector<Ptr<AST::Decl>>& commonDecls);
    void MatchCJMPDecls(std::vector<Ptr<AST::Decl>>& commonDecls, std::vector<Ptr<AST::Decl>>& specificDecls);
    bool MatchSpecificDeclWithCommonDecls(AST::Decl& specificDecl, const std::vector<Ptr<AST::Decl>>& commonDecls);
    void CheckAbstractClassMembers(const AST::InheritableDecl& specificDecl);

    bool MatchEnumFuncTypes(const AST::FuncDecl& specific, const AST::FuncDecl& common);
    bool MatchCJMPEnumConstructor(AST::Decl& specificDecl, AST::Decl& commonDecl);
    bool MatchCJMPFunction(AST::FuncDecl& specificFunc, AST::FuncDecl& commonFunc);
    bool MatchCJMPFunctionGenerics(AST::FuncDecl& specificFunc, AST::FuncDecl& commonFunc);
    bool MatchCJMPFunctionParameters(AST::FuncParamList& specificParams, AST::FuncParamList& commonParams);
    bool MatchCJMPFunctionParameters(AST::FuncDecl& specificFunc, AST::FuncDecl& commonFunc);
    bool MatchCJMPProp(AST::PropDecl& specificProp, AST::PropDecl& commonProp);
    bool MatchCJMPVar(AST::VarDecl& specificVar, AST::VarDecl& commonVar);
    bool TryMatchVarWithPatternWithVarDecls(
        AST::VarWithPatternDecl& specificDecl, const std::vector<Ptr<AST::Decl>>& commonDecls);

    bool IsCJMPDeclMatchable(AST::Decl& lhsDecl, AST::Decl& rhsDecl) const;
    bool MatchCJMPDeclAttrs(
        const std::vector<AST::Attribute>& attrs, AST::Decl& common, AST::Decl& specific) const;
    bool MatchCJMPDeclAnnotations(const AST::Decl& common, AST::Decl& specific) const;
    void PropagateCJMPDeclAnnotations(const AST::Decl& common, AST::Decl& specific) const;
    void PropagateDefaultArguments(const AST::FuncParamList& commonParams, const AST::FuncParamList& specificParams,
        const AST::FuncDecl& specificFunc) const;
    void PropagateDefaultArguments(const AST::FuncDecl& commonFunc, const AST::FuncDecl& specificFunc) const;

    bool TrySetSpecificImpl(AST::Decl& specificDecl, AST::Decl& commonDecl, const std::string& kind);
    /// Returns `true` if there is not platform declaration,
    /// in other cases check that common can me matched with platform(e.g. modifiers conform)
    bool MatchCommonNominalDeclWithSpecific(AST::InheritableDecl& commonDecl);
    void CheckCommonSpecificGenericMatch(const AST::Decl& specificDecl, const AST::Decl& commonDecl);

public:
    /**
     * @brief Get inherited types, replacing common types with specific implementations when compiling specific code
     *
     * This function processes the inherited types list and replaces any common types
     * with their corresponding specific implementations when compiling specific code.
     * The replacement only occurs if the current declaration has a specific implementation
     * available.
     *
     * @param inheritedTypes The list of inherited types to process
     * @param hasSpecificImpl Whether the current declaration has a specific implementation
     * @param compilePlatform Whether we are currently compiling specific code
     */
    static void GetInheritedTypesWithSpecificImpl(
        std::vector<OwnedPtr<AST::Type>>& inheritedTypes, bool hasSpecificImpl, bool compilePlatform);

private:
    TypeManager& typeManager;
    DiagnosticEngine& diag;
    bool compileCommon{false};   // true if compiling common part
    bool compilePlatform{false}; // true if compiling platform part
    bool severalParents{false};  // true if there is more then one incomparable parent source sets
#endif
};
} // namespace Cangjie
#endif // CANGJIE_SEMA_MPTYPECHECKER_IMPL_H
