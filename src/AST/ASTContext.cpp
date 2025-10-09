// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the ASTContext related classes.
 */

#include "cangjie/AST/ASTContext.h"

#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Utils/Utils.h"

using namespace Cangjie;
using namespace AST;

ASTContext::ASTContext(DiagnosticEngine& diag, AST::Package& pkg)
    : diag(diag), curPackage(&pkg), fullPackageName(pkg.fullPackageName)
{
    Reset();
}

void ASTContext::DeleteDesugarExpr(OwnedPtr<Expr>& desugar)
{
    DeleteInvertedIndexes(desugar.get());
    desugar = OwnedPtr<Expr>();
}

void ASTContext::DeleteInvertedIndexes(Ptr<Node> root)
{
    if (!root) {
        return;
    }
    auto deleteInvertedIndexes = [this](Ptr<Node> node) {
        if (node && node->symbol) {
            invertedIndex.Delete(node->symbol);
        }
        return VisitAction::WALK_CHILDREN;
    };
    Walker(root, deleteInvertedIndexes).Walk();
    searcher->InvalidateCache();
}

void ASTContext::DeleteCurrentInvertedIndexes(Ptr<const Node> node)
{
    if (node && node->symbol) {
        invertedIndex.Delete(node->symbol);
    }
}

void ASTContext::Reset()
{
    currentScopeLevel = 0;
    currentScopeName = TOPLEVEL_SCOPE_NAME;
    currentCheckingNodes = {};
    SymbolApi::ResetID();
    symbolTable.clear();
    invertedIndex.Reset();
}

std::string ASTContext::GetPackageName(Ptr<Node> node)
{
    std::string name;
    if (node == nullptr) {
        return name;
    }
    auto visitor = [&name](Ptr<Node> node) {
        switch (node->astKind) {
            case AST::ASTKind::REF_EXPR: {
                auto re = RawStaticCast<RefExpr*>(node);
                name = re->ref.identifier + name;
                return VisitAction::STOP_NOW;
            }
            case AST::ASTKind::MEMBER_ACCESS: {
                auto ma = RawStaticCast<MemberAccess*>(node);
                name = "." + ma->field + name;
                return VisitAction::WALK_CHILDREN;
            }
            case AST::ASTKind::REF_TYPE: {
                auto rt = RawStaticCast<RefType*>(node);
                name = rt->ref.identifier + name;
                return VisitAction::STOP_NOW;
            }
            case AST::ASTKind::QUALIFIED_TYPE: {
                auto qt = RawStaticCast<QualifiedType*>(node);
                name = "." + qt->field + name;
                return VisitAction::WALK_CHILDREN;
            }
            default:
                break;
        }
        return VisitAction::STOP_NOW;
    };
    Walker(node, visitor).Walk();
    return name;
}

void ASTContext::StoreOuterVarWithPatternDecl(const AST::VarDecl& vd, AST::VarWithPatternDecl& vpd)
{
    varDeclToVarWithPatternDeclMap[&vd] = &vpd;
}

AST::VarDeclAbstract& ASTContext::GetOuterVarDeclAbstract(AST::VarDecl& vd) const
{
    auto iter = varDeclToVarWithPatternDeclMap.find(&vd);
    if (iter != varDeclToVarWithPatternDeclMap.cend()) {
        CJC_NULLPTR_CHECK(iter->second);
        return *iter->second;
    }
    return vd;
}

void ASTContext::InsertEnumConstructor(const std::string& name, size_t argSize, AST::Decl& decl, bool enableMacroInLsp)
{
    // Both the EnumConstructor nodes before and after expansion are added to the symbol table in LSP, need to skip
    // nodes before macro expansion.
    if (enableMacroInLsp && IsNodeInOriginalMacroCallNodes(decl)) {
        return;
    }
    auto iter = enumConstructors.find(name);
    if (iter != enumConstructors.cend()) {
        auto& argSizeToDecls = iter->second;
        auto argSizeToDeclsIter = argSizeToDecls.find(argSize);
        if (argSizeToDeclsIter != argSizeToDecls.cend()) {
            argSizeToDeclsIter->second.emplace_back(&decl);
        } else {
            argSizeToDecls.emplace(std::make_pair(argSize, std::vector<Ptr<Decl>>{&decl}));
        }
    } else {
        enumConstructors.emplace(std::make_pair(
            name, std::unordered_map<size_t, std::vector<Ptr<Decl>>>{{argSize, std::vector<Ptr<Decl>>{&decl}}}));
    }
}

bool ASTContext::IsNodeInOriginalMacroCallNodes(AST::Decl& decl) const
{
    if (!decl.curFile) {
        return false;
    }
    for (auto& originalMacroCallNode : decl.curFile->originalMacroCallNodes) {
        if (originalMacroCallNode.get()->astKind != ASTKind::MACRO_EXPAND_DECL) {
            return false;
        }
        auto med = StaticAs<ASTKind::MACRO_EXPAND_DECL>(originalMacroCallNode.get());
        if (!med->invocation.decl.get() || med->invocation.decl->astKind != AST::ASTKind::ENUM_DECL) {
            return false;
        }
        auto enumNode = StaticAs<ASTKind::ENUM_DECL>(med->invocation.decl.get());
        for (auto& constructor : enumNode->constructors) {
            if (constructor.get() == &decl) {
                return true;
            }
        }
    }
    return false;
}

bool ASTContext::IsEnumConstructor(const std::string& name) const
{
    return Utils::InKeys(name, enumConstructors);
}

const std::vector<Ptr<Decl>>& ASTContext::FindEnumConstructor(const std::string& name, size_t argSize) const
{
    const static std::vector<Ptr<Decl>> EMPTY_DECLS = {};
    auto iter = enumConstructors.find(name);
    if (iter != enumConstructors.cend()) {
        auto& argSizeToDecls = iter->second;
        auto argSizeToDeclsIter = argSizeToDecls.find(argSize);
        if (argSizeToDeclsIter != argSizeToDecls.cend()) {
            return argSizeToDeclsIter->second;
        }
    }
    return EMPTY_DECLS;
}

void ASTContext::ClearTypeCheckCache(const AST::Node& root)
{
    AST::ConstWalker cleaner(&root, [this](Ptr<const Node> node) {
        typeCheckCache.erase(node);
        return VisitAction::WALK_CHILDREN;
    });
    cleaner.Walk();
}

void ASTContext::RemoveTypeCheckCache(const AST::Node& node)
{
    typeCheckCache.erase(&node);
}

void ASTContext::SkipSynForCorrectTyRec(const AST::Node& root)
{
    AST::ConstWalker enabler(&root, [this](Ptr<const Node> node) {
        if (Ty::IsTyCorrect(node->ty)) {
            SkipSynForCorrectTy(*node);
        }
        return VisitAction::WALK_CHILDREN;
    });
    enabler.Walk();
}

void ASTContext::SkipSynForCorrectTy(const AST::Node& node)
{
    auto key = CacheKey{.target = node.ty, .isDesugared = false, .diagKey = 0};
    if (auto expr = DynamicCast<Expr>(&node); expr && expr->desugarExpr) {
        key.isDesugared = true;
    }
    typeCheckCache[&node].lastKey = key;
}

void ASTContext::RemoveDeclByName(const Names& names, const Decl& decl)
{
    auto found = declMap.find(names);
    if (found != declMap.end()) {
        Utils::EraseIf(found->second, [&decl](auto it) { return it == &decl; });
    }
}

bool ASTContext::HasTargetTy(Ptr<const AST::Node> node) const
{
    auto find = targetTypeMap.find(node);
    return find != targetTypeMap.end() && find->second != nullptr;
}

void ASTContext::AddDeclName(const Names& names, AST::Decl& decl)
{
    declMap[names].emplace_back(&decl);
}

std::vector<Ptr<AST::Decl>> ASTContext::GetDeclsByName(const Names& names) const
{
    auto found = declMap.find(names);
    return found != declMap.end() ? found->second : std::vector<Ptr<AST::Decl>>{};
}

void InvertedIndex::Reset()
{
    nameIndexes.clear();
    scopeNameIndexes.clear();
    scopeGateMap.clear();
    scopeLevelIndexes.clear();
    astKindIndexes.clear();
    nameTrie->Reset();
    scopeNameTrie->Reset(TOPLEVEL_SCOPE_NAME);
    astKindTrie->Reset();
    for (auto& kindString : AST::ASTKIND_TO_STRING_MAP) {
        astKindTrie->Insert(kindString.second);
    }
    posBeginTrie->Reset();
    posEndTrie->Reset();
}

void InvertedIndex::Index(AST::Symbol* symbol, bool withTrie)
{
    if (symbol == nullptr) {
        return;
    }
    std::string scopeName = ScopeManagerApi::GetScopeNameWithoutTail(symbol->scopeName);
    nameIndexes[symbol->name].insert(symbol->id);
    scopeNameIndexes[scopeName].insert(symbol->id);
    if (symbol->scopeName.find(ScopeManagerApi::childScopeNameSplit) != std::string::npos) {
        scopeGateMap[symbol->scopeName] = symbol;
    }
    scopeLevelIndexes[symbol->scopeLevel].insert(symbol->id);
    astKindIndexes[AST::ASTKIND_TO_STRING_MAP[symbol->astKind]].insert(symbol->id);
    if (withTrie) {
        nameTrie->Insert(symbol->name, *symbol->id);
        posBeginTrie->Insert(PosSearchApi::PosToStr(symbol->node->begin), *symbol->id);
        posEndTrie->Insert(PosSearchApi::PosToStr(symbol->node->end), *symbol->id);
    }
}

void InvertedIndex::Delete(AST::Symbol* symbol)
{
    if (symbol == nullptr) {
        return;
    }
    if (symbol->invertedIndexBeenDeleted) {
        return;
    }
    std::string scopeName = ScopeManagerApi::GetScopeNameWithoutTail(symbol->scopeName);
    nameIndexes[symbol->name].erase(symbol);
    scopeNameIndexes[scopeName].erase(symbol);
    if (symbol->scopeName.find(ScopeManagerApi::childScopeNameSplit) != std::string::npos) {
        scopeGateMap.erase(symbol->scopeName);
    }
    scopeLevelIndexes[symbol->scopeLevel].erase(symbol);
    astKindIndexes[AST::ASTKIND_TO_STRING_MAP[symbol->astKind]].erase(symbol);
    nameTrie->Delete(symbol->name, *symbol);
    posBeginTrie->Delete(PosSearchApi::PosToStr(symbol->node->begin), *symbol);
    posEndTrie->Delete(PosSearchApi::PosToStr(symbol->node->end), *symbol);
    symbol->invertedIndexBeenDeleted = true;
}

std::set<Ptr<Decl>> ASTContext::Mem2Decls(const AST::MemSig& memSig)
{
    auto decls = mem2Decls[memSig];
    for (auto d : mem2Decls[memSig]) {
        decls.insert(subtypeDeclsMap[d].begin(), subtypeDeclsMap[d].end());
    }
    return decls;
}
