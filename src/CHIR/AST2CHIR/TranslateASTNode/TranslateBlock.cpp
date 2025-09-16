// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/AST2CHIR/TranslateASTNode/Translator.h"

using namespace Cangjie::CHIR;
using namespace Cangjie;

static void CollectInstantiatedFuncNodes(
    std::vector<Ptr<AST::Node>>& nodes, const AST::FuncDecl& genericFunc, const GenericInstantiationManager& gim)
{
    auto decls = gim.GetInstantiatedDecls(genericFunc);
    if (decls.empty() && genericFunc.genericDecl) {
        // 'genericFunc' may be partially instantiated decl which cannot be found in map.
        decls = gim.GetInstantiatedDecls(*genericFunc.genericDecl);
    }
    for (auto instantiatedDecl : decls) {
        (void)nodes.emplace_back(instantiatedDecl);
    }
}

static bool IsUnnecessarySuperCall(const AST::Node& node)
{
    auto ce = DynamicCast<AST::CallExpr>(&node);
    if (!ce || !ce->baseFunc) {
        return false;
    }
    if (auto re = DynamicCast<AST::RefExpr>(ce->baseFunc.get()); re && re->isSuper) {
        // Super call of 'Object' can be ignored.
        return ce->ty->IsObject();
    }
    return false;
}

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
static std::vector<Ptr<AST::Node>> CollectBlockBodyNodes(
    const AST::Block& block, const GenericInstantiationManager* gim)
{
    std::vector<Ptr<AST::Node>> nodes;
    for (const auto& body : block.body) {
        // If we find a generic local function here, then we must
        // retrieve all the instantiated functions for the translation.
        auto funcDecl = DynamicCast<AST::FuncDecl*>(body.get());
        if (funcDecl != nullptr && funcDecl->TestAttr(AST::Attribute::GENERIC)) {
            nodes.emplace_back(funcDecl);
            if (gim) {
                CollectInstantiatedFuncNodes(nodes, *funcDecl, *gim);
            }
        } else {
            if (IsUnnecessarySuperCall(*body)) {
                continue;
            }
            nodes.emplace_back(body.get());
        }
    }
    return nodes;
}
#endif

Ptr<Value> Translator::Visit(const AST::Block& b)
{
    CJC_ASSERT(!blockGroupStack.empty());
    auto block = CreateBlock();
    // Current block may be changed during translation,
    // store AST block 'b' related CHIR block for caller to generate goto or branch.
    SetSymbolTable(b, *block);

    currentBlock = block;
    // Since cangjie's block has value, return the value of last block node.
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    std::vector<Ptr<AST::Node>> nodes = CollectBlockBodyNodes(b, gim);
#endif
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (i == nodes.size() - 1) {
            return TranslateSubExprAsValue(*nodes[i]);
        } else {
            TranslateSubExprToDiscarded(*nodes[i]);
        }
    }
    return nullptr;
}