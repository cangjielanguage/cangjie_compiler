// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the Collector.
 */

#include "Collector.h"

#include <memory>

#include "cangjie/AST/Create.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Utils/FileUtil.h"

#include "TypeCheckUtil.h"

using namespace Cangjie;
using namespace AST;

namespace {
void WalkMacroCall(Node& macrocall, const OwnedPtr<Decl>& decl)
{
    // No error will be reported during semantic analysis of code in macrocall. Just for lsp.
    auto visitPre = [&macrocall](Ptr<Node> curNode) -> VisitAction {
        curNode->isInMacroCall = true;
        curNode->curMacroCall = &macrocall;
        return VisitAction::WALK_CHILDREN;
    };
    Walker macroCallWalker(decl.get(), visitPre);
    macroCallWalker.Walk();
}
} // namespace

void Collector::AddSymbol(ASTContext& ctx, const NodeInfo& nodeInfo, bool buildTrie)
{
    auto& node = nodeInfo.node;
    uint64_t fileHash = 0;
    if (node.curFile) {
        fileHash = node.curFile->fileHash;
    }
    // Only 'Collector' is 'Symbol''s friend so using 'new' before 'std::unique_ptr'.
    auto sym = new Symbol(fileHash, nodeInfo.name, node, nodeInfo.scopeLevel, nodeInfo.scopeName);
    node.scopeName = nodeInfo.scopeName;
    node.scopeLevel = nodeInfo.scopeLevel;
    node.symbol = sym;
    ctx.symbolTable.push_back(std::unique_ptr<Symbol>(sym));
    ctx.invertedIndex.Index(ctx.symbolTable.back().get(), buildTrie);
}

void Collector::CollectPackageNode(ASTContext& ctx, Package& package, bool buildTrie)
{
    scopeManager.Reset();
    auto nodeInfo = NodeInfo(package, package.fullPackageName, ctx.currentScopeLevel, TOPLEVEL_SCOPE_NAME);
    AddSymbol(ctx, nodeInfo, buildTrie);
    for (auto& file : package.files) {
        BuildSymbolTable(ctx, file.get(), buildTrie);
    }
}

void Collector::CollectFileNode(ASTContext& ctx, File& file, bool buildTrie)
{
    AddCurFile(file);
    SymbolApi::ResetID();
    auto nodeInfo = NodeInfo(file, FileUtil::GetShortHash(file.fileHash), ctx.currentScopeLevel, TOPLEVEL_SCOPE_NAME);
    AddSymbol(ctx, nodeInfo, buildTrie);
    for (auto& import : file.imports) {
        BuildSymbolTable(ctx, import.get(), buildTrie);
    }
    // Must use three-stage for loop instead of iterator-style for loop because that function "__maininvoke" will be
    // generated during function declaration symbol collection.
    for (size_t index = 0; index < file.decls.size(); index++) {
        auto decl = file.decls[index].get();
        CJC_NULLPTR_CHECK(decl);
        BuildSymbolTable(ctx, decl, buildTrie);
    }
    if (enableMacroInLsp) {
        // Collect all node symbol in macroCall, for lsp.
        scopeManager.InitializeScope(ctx);
        for (auto& macroCall : file.originalMacroCallNodes) {
            BuildSymbolTable(ctx, macroCall.get(), buildTrie);
            AddCurFile(*macroCall, &file);
        }
        scopeManager.FinalizeScope(ctx);
    }
}

void Collector::CollectAnnotations(
    ASTContext& ctx, const std::vector<OwnedPtr<Annotation>>& annotations, bool buildTrie)
{
    for (auto& it : annotations) {
        BuildSymbolTable(ctx, it.get(), buildTrie);
    }
}

void Collector::CollectGeneric(ASTContext& ctx, Decl& outerDecl, const Generic& generic, bool buildTrie)
{
    for (auto& it : generic.typeParameters) {
        it->outerDecl = &outerDecl;
        BuildSymbolTable(ctx, it.get(), buildTrie);
    }
    for (auto& it : generic.genericConstraints) {
        BuildSymbolTable(ctx, it.get(), buildTrie);
    }
}

void Collector::CollectClassDecl(ASTContext& ctx, ClassDecl& cd, bool buildTrie)
{
    CJC_NULLPTR_CHECK(cd.body);
    if ((!cd.outerDecl || !cd.outerDecl->TestAttr(Attribute::TOOL_ADD)) && ctx.currentScopeLevel != 0) {
        ctx.diag.Diagnose(cd, DiagKind::sema_type_must_toplevel, "class");
    }
    CollectAnnotations(ctx, cd.annotations, buildTrie);
    auto nodeInfo = NodeInfo(cd, cd.identifier, ctx.currentScopeLevel, scopeManager.CalcScopeGateName(ctx));
    AddSymbol(ctx, nodeInfo, buildTrie);
    scopeManager.InitializeScope(ctx);
    if (cd.generic) {
        CollectGeneric(ctx, cd, *cd.generic, buildTrie);
    }
    for (auto& type : cd.inheritedTypes) {
        BuildSymbolTable(ctx, type.get(), buildTrie);
    }
    auto bodyNodeInfo = NodeInfo(*cd.body, "", ctx.currentScopeLevel, scopeManager.CalcScopeGateName(ctx));
    AddSymbol(ctx, bodyNodeInfo, buildTrie);
    BuildSymbolTable(ctx, cd.body.get(), buildTrie);
    scopeManager.FinalizeScope(ctx);
}

void Collector::CollectClassBody(ASTContext& ctx, const ClassBody& cb, bool buildTrie)
{
    scopeManager.InitializeScope(ctx);
    for (auto& bodyDecl : cb.decls) {
        bodyDecl->EnableAttr(Attribute::IN_CLASSLIKE);
        BuildSymbolTable(ctx, bodyDecl.get(), buildTrie);
    }
    scopeManager.FinalizeScope(ctx);
}

void Collector::CollectInterfaceDecl(ASTContext& ctx, InterfaceDecl& id, bool buildTrie)
{
    CJC_NULLPTR_CHECK(id.body);
    if ((!id.outerDecl || !id.outerDecl->TestAttr(Attribute::TOOL_ADD)) && ctx.currentScopeLevel != 0) {
        ctx.diag.Diagnose(id, DiagKind::sema_type_must_toplevel, "interface");
    }
    CollectAnnotations(ctx, id.annotations, buildTrie);
    auto nodeInfo = NodeInfo(id, id.identifier, ctx.currentScopeLevel, scopeManager.CalcScopeGateName(ctx));
    AddSymbol(ctx, nodeInfo, buildTrie);
    scopeManager.InitializeScope(ctx);
    if (id.generic) {
        CollectGeneric(ctx, id, *id.generic, buildTrie);
    }
    for (auto& type : id.inheritedTypes) {
        BuildSymbolTable(ctx, type.get(), buildTrie);
    }
    auto bodyNodeInfo = NodeInfo(*id.body, "", ctx.currentScopeLevel, scopeManager.CalcScopeGateName(ctx));
    AddSymbol(ctx, bodyNodeInfo, buildTrie);
    BuildSymbolTable(ctx, id.body.get(), buildTrie);
    scopeManager.FinalizeScope(ctx);
}

void Collector::CollectInterfaceBody(ASTContext& ctx, const InterfaceBody& ib, bool buildTrie)
{
    scopeManager.InitializeScope(ctx);
    for (auto& decl : ib.decls) {
        BuildSymbolTable(ctx, decl.get(), buildTrie);
    }
    scopeManager.FinalizeScope(ctx);
}

void Collector::CollectEnumDecl(ASTContext& ctx, EnumDecl& ed, bool buildTrie)
{
    if (ctx.currentScopeLevel != 0) {
        ctx.diag.Diagnose(ed, DiagKind::sema_type_must_toplevel, "enum");
    }
    CollectAnnotations(ctx, ed.annotations, buildTrie);
    auto nodeInfo = NodeInfo(ed, ed.identifier, ctx.currentScopeLevel, scopeManager.CalcScopeGateName(ctx));
    AddSymbol(ctx, nodeInfo, buildTrie);
    scopeManager.InitializeScope(ctx);
    if (ed.generic) {
        CollectGeneric(ctx, ed, *ed.generic, buildTrie);
    }
    for (auto& type : ed.inheritedTypes) {
        BuildSymbolTable(ctx, type.get(), buildTrie);
    }
    CJC_ASSERT(ed.bodyScope);
    auto bodyNodeInfo = NodeInfo(*ed.bodyScope, "", ctx.currentScopeLevel, scopeManager.CalcScopeGateName(ctx));
    AddSymbol(ctx, bodyNodeInfo, buildTrie);
    scopeManager.InitializeScope(ctx); // Open for enum body scope.
    for (auto& ctor : ed.constructors) {
        if (auto fd = DynamicCast<FuncDecl*>(ctor.get()); fd) {
            fd->funcBody->parentEnum = &ed;
        }
        BuildSymbolTable(ctx, ctor.get(), buildTrie);
    }
    for (auto& it : ed.members) {
        BuildSymbolTable(ctx, it.get(), buildTrie);
        if (it->astKind != ASTKind::FUNC_DECL) {
            continue;
        }
        auto fd = StaticAs<ASTKind::FUNC_DECL>(it.get());
        fd->funcBody->parentEnum = &ed;
    }
    scopeManager.FinalizeScope(ctx); // End enum body scope.
    scopeManager.FinalizeScope(ctx);
}

void Collector::CollectStructDecl(ASTContext& ctx, StructDecl& sd, bool buildTrie)
{
    CJC_NULLPTR_CHECK(sd.body);
    if (ctx.currentScopeLevel != 0) {
        ctx.diag.Diagnose(sd, DiagKind::sema_type_must_toplevel, "struct");
    }
    CollectAnnotations(ctx, sd.annotations, buildTrie);
    auto nodeInfo = NodeInfo(sd, sd.identifier, ctx.currentScopeLevel, scopeManager.CalcScopeGateName(ctx));
    AddSymbol(ctx, nodeInfo, buildTrie);
    scopeManager.InitializeScope(ctx);
    if (sd.generic) {
        CollectGeneric(ctx, sd, *sd.generic, buildTrie);
    }
    for (auto& type : sd.inheritedTypes) {
        BuildSymbolTable(ctx, type.get(), buildTrie);
    }
    if (sd.body == nullptr) {
        scopeManager.FinalizeScope(ctx);
        return;
    }
    auto bodyNodeInfo = NodeInfo(*sd.body, "", ctx.currentScopeLevel, scopeManager.CalcScopeGateName(ctx));
    AddSymbol(ctx, bodyNodeInfo, buildTrie);
    BuildSymbolTable(ctx, sd.body.get(), buildTrie);
    scopeManager.FinalizeScope(ctx);
}

void Collector::CollectStructBody(ASTContext& ctx, const StructBody& rb, bool buildTrie)
{
    scopeManager.InitializeScope(ctx);
    for (auto& decl : rb.decls) {
        BuildSymbolTable(ctx, decl.get(), buildTrie);
    }
    scopeManager.FinalizeScope(ctx);
}

void Collector::CollectExtendDecl(ASTContext& ctx, ExtendDecl& ed, bool buildTrie)
{
    if (ctx.currentScopeLevel != 0) {
        ctx.diag.Diagnose(ed, DiagKind::sema_type_must_toplevel, "extend");
    }
    CollectAnnotations(ctx, ed.annotations, buildTrie);
    auto nodeInfo = NodeInfo(ed, ed.identifier, ctx.currentScopeLevel, scopeManager.CalcScopeGateName(ctx));
    AddSymbol(ctx, nodeInfo, buildTrie);
    scopeManager.InitializeScope(ctx);
    if (ed.generic) {
        CollectGeneric(ctx, ed, *ed.generic, buildTrie);
    }
    if (ed.extendedType) {
        BuildSymbolTable(ctx, ed.extendedType.get(), buildTrie);
    }
    for (auto& i : ed.inheritedTypes) {
        BuildSymbolTable(ctx, i.get(), buildTrie);
    }
    CJC_ASSERT(ed.bodyScope);
    auto bodyNodeInfo = NodeInfo(*ed.bodyScope, "", ctx.currentScopeLevel, scopeManager.CalcScopeGateName(ctx));
    AddSymbol(ctx, bodyNodeInfo, buildTrie);
    scopeManager.InitializeScope(ctx); // Open for extend body scope.
    for (auto& i : ed.members) {
        BuildSymbolTable(ctx, i.get(), buildTrie);
    }
    scopeManager.FinalizeScope(ctx); // End extend body scope.
    scopeManager.FinalizeScope(ctx);
}

void Collector::CollectMainDecl(ASTContext& ctx, MainDecl& md, bool buildTrie)
{
    if (md.desugarDecl) {
        BuildSymbolTable(ctx, md.desugarDecl.get(), buildTrie);
    } else {
        CollectAnnotations(ctx, md.annotations, buildTrie);
        auto nodeInfo = NodeInfo(md, md.identifier, ctx.currentScopeLevel, scopeManager.CalcScopeGateName(ctx));
        AddSymbol(ctx, nodeInfo, buildTrie);
        if (md.funcBody) {
            scopeManager.InitializeScope(ctx);
            BuildSymbolTable(ctx, md.funcBody.get(), buildTrie);
            scopeManager.FinalizeScope(ctx);
        }
    }
}

void Collector::CollectFuncDecl(ASTContext& ctx, FuncDecl& fd, bool buildTrie)
{
    CollectAnnotations(ctx, fd.annotations, buildTrie);
    auto nodeInfo = NodeInfo(fd, fd.identifier, ctx.currentScopeLevel, scopeManager.CalcScopeGateName(ctx));
    AddSymbol(ctx, nodeInfo, buildTrie);
    if (fd.funcBody) {
        scopeManager.InitializeScope(ctx);
        fd.funcBody->funcDecl = &fd;
        BuildSymbolTable(ctx, fd.funcBody.get(), buildTrie);
        scopeManager.FinalizeScope(ctx);
    }
    auto pkg = ctx.curPackage;
    static const std::unordered_set<std::string> intrinsicPkgs{CORE_PACKAGE_NAME, SYNC_PACKAGE_NAME, MATH_PACKAGE_NAME,
        OVERFLOW_PACKAGE_NAME, RUNTIME_PACKAGE_NAME, NET_PACKAGE_NAME, REFLECT_PACKAGE_NAME,
        UNITTEST_MOCK_INTERNAL_PACKAGE_NAME, EFFECT_PACKAGE_NAME, INTEROP_PACKAGE_NAME};
    static const std::unordered_set<std::string> headlessIntrinsics{GET_TYPE_FOR_TYPE_PARAMETER_FUNC_NAME,
        IS_SUBTYPE_TYPES_FUNC_NAME};

    if (Utils::In(fd.identifier.Val(), headlessIntrinsics)) {
        return;
    }
    if (fd.TestAttr(Attribute::INTRINSIC) && pkg != nullptr && !Utils::In(pkg->fullPackageName, intrinsicPkgs)) {
        ctx.diag.Diagnose(fd, DiagKind::sema_invalid_intrinsic_decl, fd.identifier.Val(), pkg->fullPackageName);
    }
}

void Collector::CollectFuncBody(ASTContext& ctx, FuncBody& fb, bool buildTrie)
{
    auto bodyNodeInfo = NodeInfo(fb, "", ctx.currentScopeLevel, scopeManager.CalcScopeGateName(ctx));
    AddSymbol(ctx, bodyNodeInfo, buildTrie);
    // Go inside func body and initialize a new scope.
    if (fb.generic && fb.funcDecl) {
        CollectGeneric(ctx, *fb.funcDecl, *fb.generic, buildTrie);
    }
    scopeManager.InitializeScope(ctx);
    for (auto& funcParamList : fb.paramLists) {
        BuildSymbolTable(ctx, funcParamList.get(), buildTrie);
    }
    BuildSymbolTable(ctx, fb.retType.get(), buildTrie);
    if (fb.body != nullptr) {
        for (auto& n : fb.body->body) {
            BuildSymbolTable(ctx, n.get(), buildTrie);
        }
    }
    scopeManager.FinalizeScope(ctx);
}

void Collector::CollectPropDecl(ASTContext& ctx, PropDecl& pd, bool buildTrie)
{
    CollectAnnotations(ctx, pd.annotations, buildTrie);
    auto nodeInfo = NodeInfo(pd, pd.identifier, ctx.currentScopeLevel, ctx.currentScopeName);
    AddSymbol(ctx, nodeInfo, buildTrie);
    BuildSymbolTable(ctx, pd.type.get(), buildTrie);
    for (auto& propMemberdecl : pd.setters) {
        propMemberdecl->identifierForLsp = propMemberdecl->identifier;
        if (propMemberdecl->identifier == "set") {
            propMemberdecl->identifier = "$" + pd.identifier + propMemberdecl->identifier;
        }
        propMemberdecl->propDecl = &pd;
        propMemberdecl->outerDecl = pd.outerDecl;
        BuildSymbolTable(ctx, propMemberdecl.get(), buildTrie);
    }
    for (auto& propMemberdecl : pd.getters) {
        propMemberdecl->identifierForLsp = propMemberdecl->identifier;
        if (propMemberdecl->identifier == "get") {
            propMemberdecl->identifier = "$" + pd.identifier + propMemberdecl->identifier;
        }
        propMemberdecl->propDecl = &pd;
        propMemberdecl->outerDecl = pd.outerDecl;
        BuildSymbolTable(ctx, propMemberdecl.get(), buildTrie);
    }
}

void Collector::CollectVarWithPatternDecl(ASTContext& ctx, VarWithPatternDecl& vpd, bool buildTrie)
{
    auto nodeInfo = NodeInfo(vpd, "", ctx.currentScopeLevel, ctx.currentScopeName);
    AddSymbol(ctx, nodeInfo, buildTrie);
    BuildSymbolTable(ctx, vpd.irrefutablePattern.get(), buildTrie);
    BuildSymbolTable(ctx, vpd.type.get(), buildTrie);
    BuildSymbolTable(ctx, vpd.initializer.get(), buildTrie);
}

void Collector::CollectVarDecl(ASTContext& ctx, VarDecl& vd, bool buildTrie)
{
    CollectAnnotations(ctx, vd.annotations, buildTrie);
    auto nodeInfo = NodeInfo(vd, vd.identifier, ctx.currentScopeLevel, ctx.currentScopeName);
    AddSymbol(ctx, nodeInfo, buildTrie);
    BuildSymbolTable(ctx, vd.type.get(), buildTrie);
    BuildSymbolTable(ctx, vd.initializer.get(), buildTrie);
}

void Collector::CollectTypeAliasDecl(ASTContext& ctx, TypeAliasDecl& tad, bool buildTrie)
{
    auto nodeInfo = NodeInfo(tad, tad.identifier, ctx.currentScopeLevel, scopeManager.CalcScopeGateName(ctx));
    AddSymbol(ctx, nodeInfo, buildTrie);
    scopeManager.InitializeScope(ctx);
    if (tad.generic) {
        CollectGeneric(ctx, tad, *tad.generic, buildTrie);
    }
    BuildSymbolTable(ctx, tad.type.get(), buildTrie);
    scopeManager.FinalizeScope(ctx);
}

void Collector::CollectMacroExpandDecl(ASTContext& ctx, MacroExpandDecl& med, bool buildTrie)
{
    CollectAnnotations(ctx, med.annotations, buildTrie);
    auto nodeInfo = NodeInfo(med, med.invocation.fullName, ctx.currentScopeLevel, ctx.currentScopeName);
    AddSymbol(ctx, nodeInfo, buildTrie);
    if (med.invocation.decl) {
        WalkMacroCall(med, med.invocation.decl);
        scopeManager.InitializeScope(ctx);
        BuildSymbolTable(ctx, med.invocation.decl.get(), buildTrie);
        scopeManager.FinalizeScope(ctx);
    }
}

void Collector::CollectMacroExpandExpr(ASTContext& ctx, MacroExpandExpr& mee, bool buildTrie)
{
    auto nodeInfo = NodeInfo(mee, mee.invocation.fullName, ctx.currentScopeLevel, ctx.currentScopeName);
    AddSymbol(ctx, nodeInfo, buildTrie);
    if (mee.invocation.decl) {
        WalkMacroCall(mee, mee.invocation.decl);
        scopeManager.InitializeScope(ctx);
        BuildSymbolTable(ctx, mee.invocation.decl.get(), buildTrie);
        scopeManager.FinalizeScope(ctx);
    }
}

void Collector::CollectMacroExpandParam(ASTContext& ctx, MacroExpandParam& mep, bool buildTrie)
{
    auto nodeInfo = NodeInfo(mep, mep.invocation.fullName, ctx.currentScopeLevel, ctx.currentScopeName);
    AddSymbol(ctx, nodeInfo, buildTrie);
    if (mep.invocation.decl) {
        WalkMacroCall(mep, mep.invocation.decl);
        scopeManager.InitializeScope(ctx);
        BuildSymbolTable(ctx, mep.invocation.decl.get(), buildTrie);
        scopeManager.FinalizeScope(ctx);
    }
}

void Collector::CollectIfExpr(ASTContext& ctx, IfExpr& ie, bool buildTrie)
{
    auto nodeInfo = NodeInfo(ie, "", ctx.currentScopeLevel, scopeManager.CalcScopeGateName(ctx));
    AddSymbol(ctx, nodeInfo, buildTrie);
    // There are two kinds of if expressions.
    // 1. The scope of `if (let p <- i) { t }` is shown as follows:
    //     {
    //         i
    //         { p, t } // end of p-t
    //     } // end of if-then
    // 2. The scope of `if (c) { t }` is shown as follows:
    //     {
    //         c, t
    //     } // end of if-then
    scopeManager.InitializeScope(ctx); // start of condition and then-body
    CollectCondition(ctx, *ie.condExpr, buildTrie);
    if (ie.thenBody) {
        // `ie.thenBody` has the same scope as condition.
        for (auto& n : ie.thenBody->body) {
            BuildSymbolTable(ctx, n.get(), buildTrie);
        }
    }
    scopeManager.FinalizeScope(ctx);
    // Scope of else-body.
    if (ie.hasElse && ie.elseBody) {
        BuildSymbolTable(ctx, ie.elseBody.get(), buildTrie);
    }
}

void Collector::CollectCondition(ASTContext& ctx, AST::Expr& e, bool buildTrie)
{
    if (auto letPattern = DynamicCast<LetPatternDestructor>(&e)) {
        BuildSymbolTable(ctx, letPattern->initializer.get(), buildTrie);
        NodeInfo patternNodeInfo{*letPattern, "", ctx.currentScopeLevel, ctx.currentScopeName};
        AddSymbol(ctx, patternNodeInfo, buildTrie);
        for (auto& p : letPattern->patterns) {
            BuildSymbolTable(ctx, p.get(), buildTrie);
        }
        return;
    }

    if (auto paren = DynamicCast<ParenExpr>(&e)) {
        return CollectCondition(ctx, *paren->expr, buildTrie);
    }

    if (auto binop = DynamicCast<BinaryExpr>(&e)) {
        if (binop->op == TokenKind::AND || binop->op == TokenKind::OR) {
            CollectCondition(ctx, *binop->leftExpr, buildTrie);
            return CollectCondition(ctx, *binop->rightExpr, buildTrie);
        }
    }
    
    // normal expr
    BuildSymbolTable(ctx, &e, buildTrie);
    return;
}

void Collector::CollectWhileExpr(ASTContext& ctx, WhileExpr& we, bool buildTrie)
{
    auto nodeInfo = NodeInfo(we, "", ctx.currentScopeLevel, scopeManager.CalcScopeGateName(ctx));
    AddSymbol(ctx, nodeInfo, buildTrie);
    // There are two kinds of while expressions.
    // 1. The scope of `while (let p <- i) { t }` is shown as follows:
    //     {
    //         i
    //         { p, t } // end of p-t
    //     } // end of while
    // 2. The scope of `while (c) { t }` is shown as follows:
    //     {
    //         c, t
    //     } // end of while
    scopeManager.InitializeScope(ctx); // start of while
    CollectCondition(ctx, *we.condExpr, buildTrie);
    if (we.body != nullptr) {
        for (auto& n : we.body->body) {
            BuildSymbolTable(ctx, n.get(), buildTrie);
        }
    }
    scopeManager.FinalizeScope(ctx); // end of while
}

void Collector::CollectQuoteExpr(ASTContext& ctx, QuoteExpr& qe, bool buildTrie)
{
    auto nodeInfo = NodeInfo(qe, "", ctx.currentScopeLevel, scopeManager.CalcScopeGateName(ctx));
    AddSymbol(ctx, nodeInfo, buildTrie);
    for (const auto& it : qe.exprs) {
        if (it->astKind != ASTKind::TOKEN_PART) {
            BuildSymbolTable(ctx, it.get(), buildTrie);
        }
    }
}

void Collector::CollectLitConstExpr(ASTContext& ctx, LitConstExpr& lce, bool buildTrie)
{
    std::unordered_map<LitConstKind, std::string> stringKindToRef = {
        {LitConstKind::STRING, "String"},
        {LitConstKind::JSTRING, "JString"},
    };
    if (stringKindToRef.find(lce.kind) != stringKindToRef.end() && !lce.ref) {
        auto tmp = MakeOwned<RefType>();
        tmp->begin = lce.begin;
        tmp->ref.identifier = stringKindToRef[lce.kind];
        tmp->EnableAttr(Attribute::COMPILER_ADD, Attribute::IN_CORE);
        lce.ref = std::move(tmp);
        BuildSymbolTable(ctx, lce.ref.get(), buildTrie);
    }
    if (lce.siExpr) {
        CollectStrInterpolationExpr(ctx, *lce.siExpr.get(), buildTrie);
    }
    auto nodeInfo = NodeInfo(lce, "", ctx.currentScopeLevel, ctx.currentScopeName);
    AddSymbol(ctx, nodeInfo, buildTrie);
}

void Collector::CollectStrInterpolationExpr(ASTContext& ctx, StrInterpolationExpr& sie, bool buildTrie)
{
    for (auto& expr : sie.strPartExprs) {
        BuildSymbolTable(ctx, expr.get(), buildTrie);
    }
    auto nodeInfo = NodeInfo(sie, "", ctx.currentScopeLevel, ctx.currentScopeName);
    AddSymbol(ctx, nodeInfo, buildTrie);
}

void Collector::CollectInterpolationExpr(ASTContext& ctx, InterpolationExpr& ie, bool buildTrie)
{
    BuildSymbolTable(ctx, ie.block.get(), buildTrie);
    auto nodeInfo = NodeInfo(ie, "", ctx.currentScopeLevel, ctx.currentScopeName);
    AddSymbol(ctx, nodeInfo, buildTrie);
}

void Collector::CollectTryExpr(ASTContext& ctx, TryExpr& te, bool buildTrie)
{
    auto nodeInfo = NodeInfo(te, "", ctx.currentScopeLevel, scopeManager.CalcScopeGateName(ctx));
    AddSymbol(ctx, nodeInfo, buildTrie);
    scopeManager.InitializeScope(ctx); // start of try
    for (auto& re : te.resourceSpec) {
        BuildSymbolTable(ctx, re.get(), buildTrie);
    }
    if (te.tryBlock) {
        for (auto& n : te.tryBlock->body) {
            BuildSymbolTable(ctx, n.get(), buildTrie);
        }
    }
    scopeManager.FinalizeScope(ctx); // end of try
    if (!te.handlers.empty()) {
        BuildSymbolTable(ctx, te.tryLambda.get(), buildTrie);
        BuildSymbolTable(ctx, te.finallyLambda.get(), buildTrie);
    }
    for (size_t cnt = 0; cnt < te.catchPatterns.size(); cnt++) {
        CJC_ASSERT(cnt < te.catchBlocks.size());
        auto catchBlockNodeInfo =
            NodeInfo(*te.catchBlocks[cnt], "", ctx.currentScopeLevel, scopeManager.CalcScopeGateName(ctx));
        AddSymbol(ctx, catchBlockNodeInfo, buildTrie);
        scopeManager.InitializeScope(ctx);
        BuildSymbolTable(ctx, te.catchPatterns[cnt].get(), buildTrie);
        // catch block has the same scope as catch pattern.
        for (auto& n : te.catchBlocks[cnt]->body) {
            BuildSymbolTable(ctx, n.get(), buildTrie);
        }
        scopeManager.FinalizeScope(ctx);
    }
    for (const auto& handler : te.handlers) {
        auto handleBlockNodeInfo =
            NodeInfo(*handler.block, "", ctx.currentScopeLevel, scopeManager.CalcScopeGateName(ctx));
        AddSymbol(ctx, handleBlockNodeInfo, buildTrie);
        scopeManager.InitializeScope(ctx);
        BuildSymbolTable(ctx, handler.commandPattern.get(), buildTrie);
        // handle block has the same scope as effect and resumption patterns.
        for (auto& n : handler.block->body) {
            BuildSymbolTable(ctx, n.get(), buildTrie);
        }
        scopeManager.FinalizeScope(ctx);

        if (handler.desugaredLambda) {
            BuildSymbolTable(ctx, handler.desugaredLambda, buildTrie);
        }
    }

    if (te.finallyBlock) {
        BuildSymbolTable(ctx, te.finallyBlock.get(), buildTrie);
    }
}

void Collector::CollectSpawnExpr(ASTContext& ctx, SpawnExpr& se, bool buildTrie)
{
    auto nodeInfo = NodeInfo(se, "", ctx.currentScopeLevel, ctx.currentScopeName);
    AddSymbol(ctx, nodeInfo, buildTrie);
    BuildSymbolTable(ctx, se.task.get(), buildTrie);
    BuildSymbolTable(ctx, se.arg.get(), buildTrie);
}

void Collector::BuildSymbolTable(ASTContext& ctx, Ptr<Node> node, bool buildTrie)
{
    if (!node) {
        return;
    }
    if (auto decl = DynamicCast<Decl*>(node); decl && decl->fullPackageName.empty()) {
        // Imported decl has fullPackageName.
        decl->fullPackageName = ctx.fullPackageName;
    }
    // For-In expr and Synchronized expr need to be collected in new scope, and will be processed later.
    if (auto expr = DynamicCast<Expr*>(node);
        expr && expr->astKind != ASTKind::FOR_IN_EXPR && expr->astKind != ASTKind::SYNCHRONIZED_EXPR) {
        BuildSymbolTable(ctx, expr->desugarExpr.get(), buildTrie);
    }
    switch (node->astKind) {
        // ----------- Packages -------------------
        case ASTKind::PACKAGE: {
            auto package = StaticAs<ASTKind::PACKAGE>(node);
            CollectPackageNode(ctx, *package, buildTrie);
            break;
        }
        // ----------- Files ----------------------
        case ASTKind::FILE: {
            auto file = StaticAs<ASTKind::FILE>(node);
            CollectFileNode(ctx, *file, buildTrie);
            break;
        }
        case ASTKind::ANNOTATION: {
            auto anno = StaticAs<ASTKind::ANNOTATION>(node);
            BuildSymbolTable(ctx, anno->baseExpr.get(), buildTrie);
            for (auto& funcArg : anno->args) {
                BuildSymbolTable(ctx, funcArg.get(), buildTrie);
            }
            break;
        }
        // ----------- Decls ----------------------
        case ASTKind::GENERIC_PARAM_DECL: {
            auto gpd = StaticAs<ASTKind::GENERIC_PARAM_DECL>(node);
            auto nodeInfo = NodeInfo(*gpd, gpd->identifier, ctx.currentScopeLevel, scopeManager.CalcScopeGateName(ctx));
            AddSymbol(ctx, nodeInfo, buildTrie);
            break;
        }
        case ASTKind::PRIMARY_CTOR_DECL: {
            auto pcd = StaticAs<ASTKind::PRIMARY_CTOR_DECL>(node);
            CollectAnnotations(ctx, pcd->annotations, buildTrie);
            auto nodeInfo = NodeInfo(*pcd, pcd->identifier, ctx.currentScopeLevel, scopeManager.CalcScopeGateName(ctx));
            AddSymbol(ctx, nodeInfo, buildTrie);
            scopeManager.InitializeScope(ctx);
            BuildSymbolTable(ctx, pcd->funcBody.get(), buildTrie);
            scopeManager.FinalizeScope(ctx);
            break;
        }
        case ASTKind::BUILTIN_DECL: {
            auto bid = StaticAs<ASTKind::BUILTIN_DECL>(node);
            auto nodeInfo = NodeInfo(*bid, bid->identifier, ctx.currentScopeLevel, scopeManager.CalcScopeGateName(ctx));
            AddSymbol(ctx, nodeInfo, buildTrie);
            if (bid->generic) {
                scopeManager.InitializeScope(ctx);
                CollectGeneric(ctx, *bid, *bid->generic, buildTrie);
                scopeManager.FinalizeScope(ctx);
            }
            break;
        }
        case ASTKind::CLASS_DECL: {
            auto cd = StaticAs<ASTKind::CLASS_DECL>(node);
            CollectClassDecl(ctx, *cd, buildTrie);
            break;
        }
        case ASTKind::CLASS_BODY: {
            auto cb = StaticAs<ASTKind::CLASS_BODY>(node);
            CollectClassBody(ctx, *cb, buildTrie);
            break;
        }
        case ASTKind::INTERFACE_DECL: {
            auto id = StaticAs<ASTKind::INTERFACE_DECL>(node);
            CollectInterfaceDecl(ctx, *id, buildTrie);
            break;
        }
        case ASTKind::INTERFACE_BODY: {
            auto ib = StaticAs<ASTKind::INTERFACE_BODY>(node);
            CollectInterfaceBody(ctx, *ib, buildTrie);
            break;
        }
        case ASTKind::ENUM_DECL: {
            auto ed = StaticAs<ASTKind::ENUM_DECL>(node);
            CollectEnumDecl(ctx, *ed, buildTrie);
            break;
        }
        case ASTKind::STRUCT_DECL: {
            auto sd = StaticAs<ASTKind::STRUCT_DECL>(node);
            CollectStructDecl(ctx, *sd, buildTrie);
            break;
        }
        case ASTKind::STRUCT_BODY: {
            auto rb = StaticAs<ASTKind::STRUCT_BODY>(node);
            CollectStructBody(ctx, *rb, buildTrie);
            break;
        }
        case ASTKind::EXTEND_DECL: {
            auto ed = StaticAs<ASTKind::EXTEND_DECL>(node);
            CollectExtendDecl(ctx, *ed, buildTrie);
            break;
        }
        case ASTKind::MAIN_DECL: {
            auto md = StaticAs<ASTKind::MAIN_DECL>(node);
            CollectMainDecl(ctx, *md, buildTrie);
            break;
        }
        case ASTKind::FUNC_DECL: {
            auto fd = StaticAs<ASTKind::FUNC_DECL>(node);
            CollectFuncDecl(ctx, *fd, buildTrie);
            break;
        }
        case ASTKind::FUNC_BODY: {
            auto fb = StaticAs<ASTKind::FUNC_BODY>(node);
            CollectFuncBody(ctx, *fb, buildTrie);
            break;
        }
        case ASTKind::MACRO_DECL: {
            auto md = StaticAs<ASTKind::MACRO_DECL>(node);
            if (md->desugarDecl) {
                CollectFuncDecl(ctx, *md->desugarDecl, buildTrie);
            }
            break;
        }
        case ASTKind::FUNC_PARAM_LIST: {
            auto fpl = StaticAs<ASTKind::FUNC_PARAM_LIST>(node);
            for (auto& param : fpl->params) {
                BuildSymbolTable(ctx, param.get(), buildTrie);
            }
            break;
        }
        case ASTKind::FUNC_PARAM: {
            auto fp = StaticAs<ASTKind::FUNC_PARAM>(node);
            // Process identifier : type.
            if (!fp->identifier.Empty()) {
                auto nodeInfo = NodeInfo(*fp, fp->identifier, ctx.currentScopeLevel, ctx.currentScopeName);
                AddSymbol(ctx, nodeInfo, buildTrie);
            }
            CollectAnnotations(ctx, fp->annotations, buildTrie);
            BuildSymbolTable(ctx, fp->type.get(), buildTrie);
            BuildSymbolTable(ctx, fp->assignment.get(), buildTrie);
            BuildSymbolTable(ctx, fp->desugarDecl.get(), buildTrie);
            break;
        }
        case ASTKind::MACRO_EXPAND_PARAM: {
            auto mep = StaticAs<ASTKind::MACRO_EXPAND_PARAM>(node);
            CollectMacroExpandParam(ctx, *mep, buildTrie);
            break;
        }
        case ASTKind::GENERIC_CONSTRAINT: {
            auto gc = StaticAs<ASTKind::GENERIC_CONSTRAINT>(node);
            BuildSymbolTable(ctx, gc->type.get(), buildTrie);
            for (const auto& upperBound : gc->upperBounds) {
                BuildSymbolTable(ctx, upperBound.get(), buildTrie);
            }
            break;
        }
        case ASTKind::PROP_DECL: {
            auto pd = StaticAs<ASTKind::PROP_DECL>(node);
            CollectPropDecl(ctx, *pd, buildTrie);
            break;
        }
        case ASTKind::VAR_WITH_PATTERN_DECL: {
            auto vpd = StaticAs<ASTKind::VAR_WITH_PATTERN_DECL>(node);
            CollectVarWithPatternDecl(ctx, *vpd, buildTrie);
            break;
        }
        case ASTKind::VAR_DECL: {
            auto vd = StaticAs<ASTKind::VAR_DECL>(node);
            CollectVarDecl(ctx, *vd, buildTrie);
            break;
        }
        case ASTKind::TYPE_ALIAS_DECL: {
            auto tad = StaticAs<ASTKind::TYPE_ALIAS_DECL>(node);
            CollectTypeAliasDecl(ctx, *tad, buildTrie);
            break;
        }
        case ASTKind::MACRO_EXPAND_DECL: {
            auto med = StaticAs<ASTKind::MACRO_EXPAND_DECL>(node);
            CollectMacroExpandDecl(ctx, *med, buildTrie);
            break;
        }
        // ----------- Expressions ----------------------
        case ASTKind::MACRO_EXPAND_EXPR: {
            auto me = StaticAs<ASTKind::MACRO_EXPAND_EXPR>(node);
            CollectMacroExpandExpr(ctx, *me, buildTrie);
            break;
        }
        case ASTKind::CALL_EXPR: {
            auto ce = StaticAs<ASTKind::CALL_EXPR>(node);
            std::string name;
            if (auto re = DynamicCast<RefExpr*>(ce->baseFunc.get()); re) {
                name = re->ref.identifier;
            }
            if (auto ma = DynamicCast<MemberAccess*>(ce->baseFunc.get()); ma) {
                name = ma->field;
            }
            CJC_NULLPTR_CHECK(ce->baseFunc);
            TypeCheckUtil::SetIsNotAlone(*ce->baseFunc);
            auto nodeInfo = NodeInfo(*ce, name, ctx.currentScopeLevel, ctx.currentScopeName);
            AddSymbol(ctx, nodeInfo, buildTrie);
            BuildSymbolTable(ctx, ce->baseFunc.get(), buildTrie);
            for (auto& funcArg : ce->args) {
                BuildSymbolTable(ctx, funcArg.get(), buildTrie);
            }
            break;
        }
        case ASTKind::MEMBER_ACCESS: {
            auto ma = StaticAs<ASTKind::MEMBER_ACCESS>(node);
            CJC_NULLPTR_CHECK(ma->baseExpr);
            TypeCheckUtil::SetIsNotAlone(*ma->baseExpr);
            auto nodeInfo = NodeInfo(*ma, ma->field, ctx.currentScopeLevel, ctx.currentScopeName);
            AddSymbol(ctx, nodeInfo, buildTrie);
            BuildSymbolTable(ctx, ma->baseExpr.get(), buildTrie);
            for (auto& typeArg : ma->typeArguments) {
                BuildSymbolTable(ctx, typeArg.get(), buildTrie);
            }
            break;
        }
        case ASTKind::SUBSCRIPT_EXPR: {
            auto se = StaticAs<ASTKind::SUBSCRIPT_EXPR>(node);
            CJC_NULLPTR_CHECK(se->baseExpr);
            TypeCheckUtil::SetIsNotAlone(*se->baseExpr);
            auto nodeInfo = NodeInfo(*se, "", ctx.currentScopeLevel, ctx.currentScopeName);
            AddSymbol(ctx, nodeInfo, buildTrie);
            BuildSymbolTable(ctx, se->baseExpr.get(), buildTrie);
            for (auto& expr : se->indexExprs) {
                BuildSymbolTable(ctx, expr.get(), buildTrie);
            }
            break;
        }
        case ASTKind::FUNC_ARG: {
            auto fa = StaticAs<ASTKind::FUNC_ARG>(node);
            if (!fa->name.Empty()) {
                auto nodeInfo = NodeInfo(*fa, fa->name, ctx.currentScopeLevel, ctx.currentScopeName);
                AddSymbol(ctx, nodeInfo, buildTrie);
            }
            BuildSymbolTable(ctx, fa->expr.get(), buildTrie);
            break;
        }
        case ASTKind::REF_EXPR: {
            auto re = StaticAs<ASTKind::REF_EXPR>(node);
            auto nodeInfo = NodeInfo(*re, re->ref.identifier, ctx.currentScopeLevel, ctx.currentScopeName);
            AddSymbol(ctx, nodeInfo, buildTrie);
            for (auto& typeArg : re->typeArguments) {
                BuildSymbolTable(ctx, typeArg.get(), buildTrie);
            }
            break;
        }
        case ASTKind::LAMBDA_EXPR: {
            auto le = StaticAs<ASTKind::LAMBDA_EXPR>(node);
            auto nodeInfo = NodeInfo(*le, "", ctx.currentScopeLevel, scopeManager.CalcScopeGateName(ctx));
            AddSymbol(ctx, nodeInfo, buildTrie);
            scopeManager.InitializeScope(ctx);
            BuildSymbolTable(ctx, le->funcBody.get(), buildTrie);
            scopeManager.FinalizeScope(ctx);
            break;
        }
        case ASTKind::ARRAY_LIT: {
            auto al = StaticAs<ASTKind::ARRAY_LIT>(node);
            auto nodeInfo = NodeInfo(*al, "", ctx.currentScopeLevel, ctx.currentScopeName);
            AddSymbol(ctx, nodeInfo, buildTrie);
            for (auto& child : al->children) {
                BuildSymbolTable(ctx, child.get(), buildTrie);
            }
            break;
        }
        case ASTKind::ARRAY_EXPR: {
            auto asl = StaticAs<ASTKind::ARRAY_EXPR>(node);
            auto nodeInfo = NodeInfo(*asl, "", ctx.currentScopeLevel, ctx.currentScopeName);
            AddSymbol(ctx, nodeInfo, buildTrie);
            BuildSymbolTable(ctx, asl->type.get(), buildTrie);
            for (auto& it : asl->args) {
                BuildSymbolTable(ctx, it.get(), buildTrie);
            }
            break;
        }
        case ASTKind::POINTER_EXPR: {
            auto cpe = StaticAs<ASTKind::POINTER_EXPR>(node);
            auto nodeInfo = NodeInfo(*cpe, "", ctx.currentScopeLevel, ctx.currentScopeName);
            AddSymbol(ctx, nodeInfo, buildTrie);
            BuildSymbolTable(ctx, cpe->type.get(), buildTrie);
            BuildSymbolTable(ctx, cpe->arg.get(), buildTrie);
            break;
        }
        case ASTKind::TUPLE_LIT: {
            auto tl = StaticAs<ASTKind::TUPLE_LIT>(node);
            auto nodeInfo = NodeInfo(*tl, "", ctx.currentScopeLevel, ctx.currentScopeName);
            AddSymbol(ctx, nodeInfo, buildTrie);
            for (auto& child : tl->children) {
                BuildSymbolTable(ctx, child.get(), buildTrie);
            }
            break;
        }
        case ASTKind::FOR_IN_EXPR: {
            auto fie = StaticAs<ASTKind::FOR_IN_EXPR>(node);
            BuildSymbolTable(ctx, fie->inExpression.get(), buildTrie);
            auto nodeInfo = NodeInfo(*fie, "", ctx.currentScopeLevel, scopeManager.CalcScopeGateName(ctx));
            AddSymbol(ctx, nodeInfo, buildTrie);
            scopeManager.InitializeScope(ctx);
            BuildSymbolTable(ctx, fie->pattern.get(), buildTrie);
            BuildSymbolTable(ctx, fie->patternGuard.get(), buildTrie);
            if (fie->body != nullptr) {
                for (auto& n : fie->body->body) {
                    BuildSymbolTable(ctx, n.get(), buildTrie);
                }
            }
            scopeManager.FinalizeScope(ctx);
            break;
        }
        case ASTKind::BLOCK: {
            auto b = StaticAs<ASTKind::BLOCK>(node);
            auto nodeInfo = NodeInfo(*b, "", ctx.currentScopeLevel, scopeManager.CalcScopeGateName(ctx));
            AddSymbol(ctx, nodeInfo, buildTrie);
            scopeManager.InitializeScope(ctx);
            for (auto& n : b->body) {
                BuildSymbolTable(ctx, n.get(), buildTrie);
            }
            scopeManager.FinalizeScope(ctx);
            break;
        }
        case ASTKind::IF_EXPR: {
            auto ie = StaticAs<ASTKind::IF_EXPR>(node);
            CollectIfExpr(ctx, *ie, buildTrie);
            break;
        }
        case ASTKind::PAREN_EXPR: {
            auto pe = StaticAs<ASTKind::PAREN_EXPR>(node);
            BuildSymbolTable(ctx, pe->expr.get(), buildTrie);
            break;
        }
        case ASTKind::QUOTE_EXPR: {
            auto qe = StaticAs<ASTKind::QUOTE_EXPR>(node);
            CollectQuoteExpr(ctx, *qe, buildTrie);
            break;
        }
        case ASTKind::WHILE_EXPR: {
            auto we = StaticAs<ASTKind::WHILE_EXPR>(node);
            CollectWhileExpr(ctx, *we, buildTrie);
            break;
        }
        case ASTKind::DO_WHILE_EXPR: {
            auto dwe = StaticAs<ASTKind::DO_WHILE_EXPR>(node);
            auto nodeInfo = NodeInfo(*dwe, "", ctx.currentScopeLevel, scopeManager.CalcScopeGateName(ctx));
            AddSymbol(ctx, nodeInfo, buildTrie);
            scopeManager.InitializeScope(ctx);
            BuildSymbolTable(ctx, dwe->condExpr.get(), buildTrie);
            BuildSymbolTable(ctx, dwe->body.get(), buildTrie);
            scopeManager.FinalizeScope(ctx);
            break;
        }
        case ASTKind::ASSIGN_EXPR: {
            auto ae = StaticAs<ASTKind::ASSIGN_EXPR>(node);
            if (ae->desugarExpr) {
                BuildSymbolTable(ctx, ae->leftValue.get(), buildTrie);
                BuildSymbolTable(ctx, ae->rightExpr.get(), buildTrie);
                BuildSymbolTable(ctx, ae->desugarExpr.get(), buildTrie);
            } else {
                auto nodeInfo = NodeInfo(*ae, "", ctx.currentScopeLevel, ctx.currentScopeName);
                AddSymbol(ctx, nodeInfo, buildTrie);
                if (ae->leftValue) {
                    ae->leftValue->EnableAttr(Attribute::LEFT_VALUE);
                    BuildSymbolTable(ctx, ae->leftValue.get(), buildTrie);
                }
                BuildSymbolTable(ctx, ae->rightExpr.get(), buildTrie);
            }
            break;
        }
        case ASTKind::INC_OR_DEC_EXPR: {
            auto ide = StaticAs<ASTKind::INC_OR_DEC_EXPR>(node);
            auto nodeInfo = NodeInfo(*ide, "", ctx.currentScopeLevel, ctx.currentScopeName);
            AddSymbol(ctx, nodeInfo, buildTrie);
            BuildSymbolTable(ctx, ide->expr.get(), buildTrie);
            break;
        }
        case ASTKind::BINARY_EXPR: {
            auto be = StaticAs<ASTKind::BINARY_EXPR>(node);
            auto nodeInfo = NodeInfo(*be, "", ctx.currentScopeLevel, ctx.currentScopeName);
            AddSymbol(ctx, nodeInfo, buildTrie);
            BuildSymbolTable(ctx, be->leftExpr.get(), buildTrie);
            BuildSymbolTable(ctx, be->rightExpr.get(), buildTrie);
            break;
        }
        case ASTKind::UNARY_EXPR: {
            auto ue = StaticAs<ASTKind::UNARY_EXPR>(node);
            auto nodeInfo = NodeInfo(*ue, "", ctx.currentScopeLevel, ctx.currentScopeName);
            AddSymbol(ctx, nodeInfo, buildTrie);
            BuildSymbolTable(ctx, ue->expr.get(), buildTrie);
            break;
        }
        case ASTKind::RANGE_EXPR: {
            auto re = StaticAs<ASTKind::RANGE_EXPR>(node);
            BuildSymbolTable(ctx, re->startExpr.get(), buildTrie);
            BuildSymbolTable(ctx, re->stopExpr.get(), buildTrie);
            BuildSymbolTable(ctx, re->stepExpr.get(), buildTrie);
            break;
        }
        case ASTKind::LIT_CONST_EXPR: {
            auto lce = StaticAs<ASTKind::LIT_CONST_EXPR>(node);
            CollectLitConstExpr(ctx, *lce, buildTrie);
            break;
        }
        case ASTKind::INTERPOLATION_EXPR: {
            auto ie = StaticAs<ASTKind::INTERPOLATION_EXPR>(node);
            CollectInterpolationExpr(ctx, *ie, buildTrie);
            break;
        }
        case ASTKind::RETURN_EXPR: {
            auto re = StaticAs<ASTKind::RETURN_EXPR>(node);
            auto nodeInfo = NodeInfo(*re, "", ctx.currentScopeLevel, ctx.currentScopeName);
            AddSymbol(ctx, nodeInfo, buildTrie);
            BuildSymbolTable(ctx, re->expr.get(), buildTrie);
            break;
        }
        case ASTKind::MATCH_EXPR: {
            auto me = StaticAs<ASTKind::MATCH_EXPR>(node);
            auto nodeInfo = NodeInfo(*me, "", ctx.currentScopeLevel, ctx.currentScopeName);
            AddSymbol(ctx, nodeInfo, buildTrie);
            BuildSymbolTable(ctx, me->selector.get(), buildTrie);
            for (auto& matchCase : me->matchCases) {
                BuildSymbolTable(ctx, matchCase.get(), buildTrie);
            }
            for (auto& matchCaseOther : me->matchCaseOthers) {
                BuildSymbolTable(ctx, matchCaseOther.get(), buildTrie);
            }
            break;
        }
        case ASTKind::MATCH_CASE: {
            auto mc = StaticAs<ASTKind::MATCH_CASE>(node);
            auto nodeInfo = NodeInfo(*mc, "", ctx.currentScopeLevel, scopeManager.CalcScopeGateName(ctx));
            AddSymbol(ctx, nodeInfo, buildTrie);
            // Go inside match case and initialize a new scope.
            scopeManager.InitializeScope(ctx);
            for (auto& pattern : mc->patterns) {
                BuildSymbolTable(ctx, pattern.get(), buildTrie);
            }
            BuildSymbolTable(ctx, mc->patternGuard.get(), buildTrie);
            if (mc->exprOrDecls != nullptr) {
                // `mc->exprOrDecls` has the same scope as pattern.
                for (auto& n : mc->exprOrDecls->body) {
                    BuildSymbolTable(ctx, n.get(), buildTrie);
                }
            }
            scopeManager.FinalizeScope(ctx);
            break;
        }
        case ASTKind::MATCH_CASE_OTHER: {
            auto mco = StaticAs<ASTKind::MATCH_CASE_OTHER>(node);
            BuildSymbolTable(ctx, mco->matchExpr.get(), buildTrie);
            BuildSymbolTable(ctx, mco->exprOrDecls.get(), buildTrie);
            break;
        }
        case ASTKind::JUMP_EXPR: {
            auto je = StaticAs<ASTKind::JUMP_EXPR>(node);
            auto nodeInfo = NodeInfo(*je, "", ctx.currentScopeLevel, ctx.currentScopeName);
            AddSymbol(ctx, nodeInfo, buildTrie);
            break;
        }
        case ASTKind::TYPE_CONV_EXPR: {
            auto tce = StaticAs<ASTKind::TYPE_CONV_EXPR>(node);
            auto nodeInfo = NodeInfo(*tce, "", ctx.currentScopeLevel, ctx.currentScopeName);
            AddSymbol(ctx, nodeInfo, buildTrie);
            BuildSymbolTable(ctx, tce->expr.get(), buildTrie);
            if (tce->type->astKind == ASTKind::FUNC_TYPE) {
                BuildSymbolTable(ctx, tce->type.get(), buildTrie);
            }
            break;
        }
        case ASTKind::IF_AVAILABLE_EXPR: {
            auto ie = StaticCast<IfAvailableExpr>(node);
            NodeInfo info{*ie, "", ctx.currentScopeLevel, ctx.currentScopeName};
            AddSymbol(ctx, info, buildTrie);
            BuildSymbolTable(ctx, ie->GetArg(), buildTrie);
            BuildSymbolTable(ctx, ie->GetLambda1(), buildTrie);
            BuildSymbolTable(ctx, ie->GetLambda2(), buildTrie);
            break;
        }
        case ASTKind::THROW_EXPR: {
            auto te = StaticAs<ASTKind::THROW_EXPR>(node);
            auto nodeInfo = NodeInfo(*te, "", ctx.currentScopeLevel, ctx.currentScopeName);
            AddSymbol(ctx, nodeInfo, buildTrie);
            BuildSymbolTable(ctx, te->expr.get(), buildTrie);
            break;
        }
        case ASTKind::PERFORM_EXPR: {
            auto pe = StaticAs<ASTKind::PERFORM_EXPR>(node);
            auto nodeInfo = NodeInfo(*pe, "", ctx.currentScopeLevel, ctx.currentScopeName);
            AddSymbol(ctx, nodeInfo, buildTrie);
            BuildSymbolTable(ctx, pe->expr.get(), buildTrie);
            break;
        }
        case ASTKind::RESUME_EXPR: {
            auto re = StaticAs<ASTKind::RESUME_EXPR>(node);
            auto nodeInfo = NodeInfo(*re, "", ctx.currentScopeLevel, ctx.currentScopeName);
            AddSymbol(ctx, nodeInfo, buildTrie);
            BuildSymbolTable(ctx, re->withExpr.get(), buildTrie);
            BuildSymbolTable(ctx, re->throwingExpr.get(), buildTrie);
            break;
        }
        case ASTKind::TRY_EXPR: {
            auto te = StaticAs<ASTKind::TRY_EXPR>(node);
            CollectTryExpr(ctx, *te, buildTrie);
            break;
        }
        case ASTKind::SPAWN_EXPR: {
            auto se = StaticAs<ASTKind::SPAWN_EXPR>(node);
            CollectSpawnExpr(ctx, *se, buildTrie);
            break;
        }
        case ASTKind::SYNCHRONIZED_EXPR: {
            auto se = StaticAs<ASTKind::SYNCHRONIZED_EXPR>(node);
            auto nodeInfo = NodeInfo(*se, "", ctx.currentScopeLevel, scopeManager.CalcScopeGateName(ctx));
            AddSymbol(ctx, nodeInfo, buildTrie);
            scopeManager.InitializeScope(ctx);
            BuildSymbolTable(ctx, se->mutex.get(), buildTrie);
            BuildSymbolTable(ctx, se->desugarExpr.get(), buildTrie);
            scopeManager.FinalizeScope(ctx);
            break;
        }
        case ASTKind::IS_EXPR: {
            auto ie = StaticAs<ASTKind::IS_EXPR>(node);
            auto nodeInfo = NodeInfo(*ie, "", ctx.currentScopeLevel, ctx.currentScopeName);
            AddSymbol(ctx, nodeInfo, buildTrie);
            BuildSymbolTable(ctx, ie->leftExpr.get(), buildTrie);
            BuildSymbolTable(ctx, ie->isType.get(), buildTrie);
            break;
        }
        case ASTKind::AS_EXPR: {
            auto ae = StaticAs<ASTKind::AS_EXPR>(node);
            auto nodeInfo = NodeInfo(*ae, "", ctx.currentScopeLevel, ctx.currentScopeName);
            AddSymbol(ctx, nodeInfo, buildTrie);
            BuildSymbolTable(ctx, ae->leftExpr.get(), buildTrie);
            BuildSymbolTable(ctx, ae->asType.get(), buildTrie);
            break;
        }
        // ----------- Patterns ----------------------
        case ASTKind::CONST_PATTERN: {
            auto cp = StaticAs<ASTKind::CONST_PATTERN>(node);
            auto nodeInfo = NodeInfo(*cp, "", ctx.currentScopeLevel, ctx.currentScopeName);
            AddSymbol(ctx, nodeInfo, buildTrie);
            BuildSymbolTable(ctx, cp->literal.get(), buildTrie);
            break;
        }
        case ASTKind::VAR_PATTERN: {
            auto vp = StaticAs<ASTKind::VAR_PATTERN>(node);
            auto nodeInfo = NodeInfo(*vp, "", ctx.currentScopeLevel, ctx.currentScopeName);
            AddSymbol(ctx, nodeInfo, buildTrie);
            BuildSymbolTable(ctx, vp->varDecl.get(), buildTrie);
            break;
        }
        case ASTKind::TUPLE_PATTERN: {
            auto tp = StaticAs<ASTKind::TUPLE_PATTERN>(node);
            auto nodeInfo = NodeInfo(*tp, "", ctx.currentScopeLevel, ctx.currentScopeName);
            AddSymbol(ctx, nodeInfo, buildTrie);
            for (auto& pattern : tp->patterns) {
                BuildSymbolTable(ctx, pattern.get(), buildTrie);
            }
            break;
        }
        case ASTKind::ENUM_PATTERN: {
            auto ep = StaticAs<ASTKind::ENUM_PATTERN>(node);
            auto nodeInfo = NodeInfo(*ep, ep->GetIdentifier(), ctx.currentScopeLevel, ctx.currentScopeName);
            AddSymbol(ctx, nodeInfo, buildTrie);
            BuildSymbolTable(ctx, ep->constructor.get(), buildTrie);
            for (auto& pattern : ep->patterns) {
                BuildSymbolTable(ctx, pattern.get(), buildTrie);
            }
            break;
        }
        case AST::ASTKind::VAR_OR_ENUM_PATTERN: {
            auto vep = StaticAs<ASTKind::VAR_OR_ENUM_PATTERN>(node);
            auto nodeInfo = NodeInfo(*vep, vep->identifier, ctx.currentScopeLevel, ctx.currentScopeName);
            AddSymbol(ctx, nodeInfo, buildTrie);
            break;
        }
        case ASTKind::TYPE_PATTERN: {
            auto tp = StaticAs<ASTKind::TYPE_PATTERN>(node);
            auto nodeInfo = NodeInfo(*tp, "", ctx.currentScopeLevel, ctx.currentScopeName);
            AddSymbol(ctx, nodeInfo, buildTrie);
            BuildSymbolTable(ctx, tp->pattern.get(), buildTrie);
            BuildSymbolTable(ctx, tp->type.get(), buildTrie);
            break;
        }
        case ASTKind::WILDCARD_PATTERN: {
            auto wp = StaticAs<ASTKind::WILDCARD_PATTERN>(node);
            auto nodeInfo = NodeInfo(*wp, "", ctx.currentScopeLevel, ctx.currentScopeName);
            AddSymbol(ctx, nodeInfo, buildTrie);
            break;
        }
        case ASTKind::EXCEPT_TYPE_PATTERN: {
            auto etp = StaticAs<ASTKind::EXCEPT_TYPE_PATTERN>(node);
            BuildSymbolTable(ctx, etp->pattern.get(), buildTrie);
            for (auto& type : etp->types) {
                BuildSymbolTable(ctx, type.get(), buildTrie);
            }
            break;
        }
        case ASTKind::COMMAND_TYPE_PATTERN: {
            auto etp = StaticAs<ASTKind::COMMAND_TYPE_PATTERN>(node);
            BuildSymbolTable(ctx, etp->pattern.get(), buildTrie);
            for (auto& type : etp->types) {
                BuildSymbolTable(ctx, type.get(), buildTrie);
            }
            break;
        }
        // ----------- Types ----------------------
        case ASTKind::REF_TYPE: {
            auto rt = StaticAs<ASTKind::REF_TYPE>(node);
            auto nodeInfo = NodeInfo(*rt, rt->ref.identifier, ctx.currentScopeLevel, ctx.currentScopeName);
            AddSymbol(ctx, nodeInfo, buildTrie);
            for (auto& type : rt->typeArguments) {
                BuildSymbolTable(ctx, type.get(), buildTrie);
            }
            break;
        }
        case ASTKind::CONSTANT_TYPE: {
            auto ct = StaticAs<ASTKind::CONSTANT_TYPE>(node);
            auto nodeInfo = NodeInfo(*ct, "", ctx.currentScopeLevel, ctx.currentScopeName);
            AddSymbol(ctx, nodeInfo, buildTrie);
            BuildSymbolTable(ctx, ct->constantExpr.get(), buildTrie);
            break;
        }
        case ASTKind::VARRAY_TYPE: {
            auto vt = StaticAs<ASTKind::VARRAY_TYPE>(node);
            auto nodeInfo = NodeInfo(*vt, "", ctx.currentScopeLevel, ctx.currentScopeName);
            AddSymbol(ctx, nodeInfo, buildTrie);
            BuildSymbolTable(ctx, vt->typeArgument.get(), buildTrie);
            BuildSymbolTable(ctx, vt->constantType.get(), buildTrie);
            break;
        }
        case ASTKind::PRIMITIVE_TYPE: {
            auto pt = StaticAs<ASTKind::PRIMITIVE_TYPE>(node);
            auto nodeInfo = NodeInfo(*pt, pt->str, ctx.currentScopeLevel, ctx.currentScopeName);
            AddSymbol(ctx, nodeInfo, buildTrie);
            break;
        }
        case ASTKind::PAREN_TYPE: {
            auto pt = StaticAs<ASTKind::PAREN_TYPE>(node);
            auto nodeInfo = NodeInfo(*pt, "", ctx.currentScopeLevel, ctx.currentScopeName);
            AddSymbol(ctx, nodeInfo, buildTrie);
            BuildSymbolTable(ctx, pt->type.get(), buildTrie);
            break;
        }
        case ASTKind::QUALIFIED_TYPE: {
            auto qt = StaticAs<ASTKind::QUALIFIED_TYPE>(node);
            auto nodeInfo = NodeInfo(*qt, qt->field, ctx.currentScopeLevel, ctx.currentScopeName);
            AddSymbol(ctx, nodeInfo, buildTrie);
            BuildSymbolTable(ctx, qt->baseType.get(), buildTrie);
            for (auto& type : qt->typeArguments) {
                BuildSymbolTable(ctx, type.get(), buildTrie);
            }
            break;
        }
        case ASTKind::FUNC_TYPE: {
            auto ft = StaticAs<ASTKind::FUNC_TYPE>(node);
            auto nodeInfo = NodeInfo(*ft, "", ctx.currentScopeLevel, ctx.currentScopeName);
            AddSymbol(ctx, nodeInfo, buildTrie);
            for (auto& paramType : ft->paramTypes) {
                BuildSymbolTable(ctx, paramType.get(), buildTrie);
            }
            BuildSymbolTable(ctx, ft->retType.get(), buildTrie);
            break;
        }
        case ASTKind::TUPLE_TYPE: {
            auto tp = StaticAs<ASTKind::TUPLE_TYPE>(node);
            auto nodeInfo = NodeInfo(*tp, "", ctx.currentScopeLevel, ctx.currentScopeName);
            AddSymbol(ctx, nodeInfo, buildTrie);
            for (auto& type : tp->fieldTypes) {
                BuildSymbolTable(ctx, type.get(), buildTrie);
            }
            break;
        }
        case ASTKind::OPTION_TYPE: {
            auto ot = StaticAs<ASTKind::OPTION_TYPE>(node);
            BuildSymbolTable(ctx, ot->componentType.get(), buildTrie);
            BuildSymbolTable(ctx, ot->desugarType.get(), buildTrie);
            break;
        }
        default:
            break;
    }
}
