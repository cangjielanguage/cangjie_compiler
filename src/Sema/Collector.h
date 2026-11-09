// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the Collector, which collects almost all AST Node to build SymbolTable and reverse indexes.
 */

#ifndef CANGJIE_SEMA_COLLECTOR_H
#define CANGJIE_SEMA_COLLECTOR_H

#include <utility>

#include "ScopeManager.h"
#include "cangjie/AST/ASTContext.h"

namespace Cangjie {
struct NodeInfo {
    AST::Node& node;
    std::string name;
    uint32_t scopeLevel;
    std::string scopeName;
    NodeInfo(AST::Node& node, std::string name, uint32_t scopeLevel, std::string scopeName)
        : node(node), name(std::move(name)), scopeLevel(scopeLevel), scopeName(std::move(scopeName))
    {
    }
};

class Collector {
public:
    /**
     * Build the SymbolTable from @p root.
     * @param root The root ast Node.
     */
    void BuildSymbolTable(ASTContext& ctx, Ptr<AST::Node> node, bool buildTrie = true);
    explicit Collector(ScopeManager& manager) : scopeManager(manager)
    {
    }

    Collector(ScopeManager& manager, bool enableMacroInLsp) : scopeManager(manager), enableMacroInLsp(enableMacroInLsp)
    {
    }

    /** Add a symbol with info @p nodeInfo to the symbolTable in @p ctx. The @p buildTrie is a flag to control whether
     * to build a group of trie trees to accelerate search. */
    static void AddSymbol(ASTContext& ctx, const NodeInfo& nodeInfo, bool buildTrie = true);

    /**
     * Update the pos limit of source code position.
     * @param package source package.
     */
    void UpdatePosLimit(AST::Package& package);

private:
    ScopeManager& scopeManager;
    bool enableMacroInLsp{false};
    /** A set of functions to collect symbols of corresponding AST node. */
    void CollectPackageNode(ASTContext& ctx, AST::Package& package, bool buildTrie);
    void CollectFileNode(ASTContext& ctx, AST::File& file, bool buildTrie);
    void CollectAnnotations(ASTContext& ctx, const std::vector<OwnedPtr<AST::Annotation>>& annotations, bool buildTrie);
    void CollectGeneric(ASTContext& ctx, AST::Decl& outerDecl, const AST::Generic& generic, bool buildTrie);
    void CollectClassDecl(ASTContext& ctx, AST::ClassDecl& cd, bool buildTrie);
    void CollectClassBody(ASTContext& ctx, const AST::ClassBody& cb, bool buildTrie);
    void CollectInterfaceDecl(ASTContext& ctx, AST::InterfaceDecl& id, bool buildTrie);
    void CollectInterfaceBody(ASTContext& ctx, const AST::InterfaceBody& ib, bool buildTrie);
    void CollectEnumDecl(ASTContext& ctx, AST::EnumDecl& ed, bool buildTrie);
    void CollectStructDecl(ASTContext& ctx, AST::StructDecl& sd, bool buildTrie);
    void CollectStructBody(ASTContext& ctx, const AST::StructBody& rb, bool buildTrie);
    void CollectExtendDecl(ASTContext& ctx, AST::ExtendDecl& ed, bool buildTrie);
    void CollectMainDecl(ASTContext& ctx, AST::MainDecl& md, bool buildTrie);
    void CollectFuncDecl(ASTContext& ctx, AST::FuncDecl& fd, bool buildTrie);
    void CollectFuncBody(ASTContext& ctx, AST::FuncBody& fb, bool buildTrie);
    void CollectPropDecl(ASTContext& ctx, AST::PropDecl& pd, bool buildTrie);
    void CollectVarWithPatternDecl(ASTContext& ctx, AST::VarWithPatternDecl& vpd, bool buildTrie);
    void CollectVarDecl(ASTContext& ctx, AST::VarDecl& vd, bool buildTrie);
    void CollectTypeAliasDecl(ASTContext& ctx, AST::TypeAliasDecl& tad, bool buildTrie);
    void CollectIfExpr(ASTContext& ctx, AST::IfExpr& ie, bool buildTrie);
    void CollectExclaveExpr(ASTContext& ctx, AST::ExclaveExpr& ee, bool buildTrie);
    void CollectCondition(ASTContext& ctx, AST::Expr& e, bool buildTrie);
    void CollectWhileExpr(ASTContext& ctx, AST::WhileExpr& we, bool buildTrie);
    void CollectLitConstExpr(ASTContext& ctx, AST::LitConstExpr& lce, bool buildTrie);
    void CollectStrInterpolationExpr(ASTContext& ctx, AST::StrInterpolationExpr& sie, bool buildTrie);
    void CollectInterpolationExpr(ASTContext& ctx, AST::InterpolationExpr& ie, bool buildTrie);
    void CollectTryExpr(ASTContext& ctx, AST::TryExpr& te, bool buildTrie);
    void CollectSpawnExpr(ASTContext& ctx, AST::SpawnExpr& se, bool buildTrie);
    void CollectMacroExpandDecl(ASTContext& ctx, AST::MacroExpandDecl& med, bool buildTrie);
    void CollectQuoteExpr(ASTContext& ctx, AST::QuoteExpr& qe, bool buildTrie);
    void CollectMacroExpandExpr(ASTContext& ctx, AST::MacroExpandExpr& mee, bool buildTrie);
    void CollectMacroExpandParam(ASTContext& ctx, AST::MacroExpandParam& mep, bool buildTrie);
};
} // namespace Cangjie
#endif
