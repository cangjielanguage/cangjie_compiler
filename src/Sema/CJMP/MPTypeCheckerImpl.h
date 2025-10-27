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
    void RemoveCommonCandidatesIfHasPlatform(std::vector<Ptr<AST::FuncDecl>>& candidates) const;
    void MatchPlatformWithCommon(AST::Package& pkg);

    static void FilterOutCommonCandidatesIfPlatformExist(std::map<Names, std::vector<Ptr<AST::FuncDecl>>>& candidates);
    void MapCJMPGenericTypeArgs(TypeSubst& genericTyMap, const AST::Decl& commonDecl, const AST::Decl& platformDecl);
    void UpdateGenericTyInMemberFromCommon(TypeSubst& genericTyMap, Ptr<AST::Decl>& member);
    void UpdatePlatformMemberGenericTy(
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
    void MatchCJMPDecls(std::vector<Ptr<AST::Decl>>& commonDecls, std::vector<Ptr<AST::Decl>>& platformDecls);
    bool MatchPlatformDeclWithCommonDecls(AST::Decl& platformDecl, const std::vector<Ptr<AST::Decl>>& commonDecls);

    bool MatchEnumFuncTypes(const AST::FuncDecl& platform, const AST::FuncDecl& common);
    bool MatchCJMPEnumConstructor(AST::Decl& platformDecl, AST::Decl& commonDecl);
    bool MatchCJMPFunction(AST::FuncDecl& platformFunc, AST::FuncDecl& commonFunc);
    bool MatchCJMPProp(AST::PropDecl& platformProp, AST::PropDecl& commonProp);
    bool MatchCJMPVar(AST::VarDecl& platformVar, AST::VarDecl& commonVar);
    bool TryMatchVarWithPatternWithVarDecls(
        AST::VarWithPatternDecl& platformDecl, const std::vector<Ptr<AST::Decl>>& commonDecls);

    bool IsCJMPDeclMatchable(const AST::Decl& lhsDecl, const AST::Decl& rhsDecl) const;
    bool MatchCJMPDeclAttrs(
        const std::vector<AST::Attribute>& attrs, const AST::Decl& common, const AST::Decl& platform) const;
    bool MatchCJMPDeclAnnotations(
        const std::vector<AST::AnnotationKind>& annotations, const AST::Decl& common, const AST::Decl& platform) const;

    bool TrySetPlatformImpl(AST::Decl& platformDecl, AST::Decl& commonDecl, const std::string& kind);
    bool MatchCommonNominalDeclWithPlatform(const AST::InheritableDecl& commonDecl);
    void CheckCommonSpecificGenericMatch(const AST::Decl& platformDecl, const AST::Decl& commonDecl);

private:
    TypeManager& typeManager;
    DiagnosticEngine& diag;
    bool compileCommon{false};   // true if compiling common part
    bool compilePlatform{false}; // true if compiling platform part
#endif
};
} // namespace Cangjie
#endif // CANGJIE_SEMA_MPTYPECHECKER_IMPL_H
