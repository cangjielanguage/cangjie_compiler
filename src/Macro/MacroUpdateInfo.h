// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the UpdateMacroInfo API.
 */

#ifndef CANGJIE_MACROUPDATEINFO_H
#define CANGJIE_MACROUPDATEINFO_H

#include "cangjie/AST/Node.h"

namespace {
    
using namespace Cangjie;
using namespace AST;

// Update parent for macro node.
template <typename T> void UpdateExpr(OwnedPtr<T>& item, const Ptr<Node> parent, MacroCollector& collector)
{
    if (!item) {
        return;
    }
    if (item->IsMacroCallNode()) {
        auto macroNode = item.get();
        (void)collector.macCalls.emplace_back(macroNode);
        collector.macCalls.back().replaceLoc = &item;
        collector.macCalls.back().isOuterMost = true;
        auto pinvocation = macroNode->GetInvocation();
        if (pinvocation && pinvocation->newTokens.empty()) {
            pinvocation->parent = parent;
            (void)pinvocation->newTokens.emplace_back(Token(TokenKind::ILLEGAL, ""));
        }
    }
}

// Update parent for macro node.
template <typename T> void UpdateContainer(PtrVector<T>& container, const Ptr<Node> parent, MacroCollector& collector)
{
    CJC_NULLPTR_CHECK(parent);
    for (size_t i = 0; i < container.size(); i++) {
        if (container[i]->IsMacroCallNode()) {
            auto macroNode = container[i].get();
            (void)collector.macCalls.emplace_back(macroNode);
            collector.macCalls.back().replaceLoc = PtrType(VectorTarget<OwnedPtr<T>>{&container, i});
            collector.macCalls.back().isOuterMost = true;
            auto pinvocation = macroNode->GetInvocation();
            if (pinvocation && pinvocation->newTokens.empty()) {
                pinvocation->parent = parent;
                (void)pinvocation->newTokens.emplace_back(Token(TokenKind::ILLEGAL, ""));
            }
        }
    }
}

template <ASTKind kind>
void UpdateSingleMacroInfo([[maybe_unused]] Node& curNode, [[maybe_unused]] MacroCollector& collector)
{
}

#define UPDATE(KIND, FIELD)                                                                                            \
    template <> void UpdateSingleMacroInfo<ASTKind::KIND>(Node & curNode, MacroCollector & collector)                  \
    {                                                                                                                  \
        auto parent = StaticAs<ASTKind::KIND>(&curNode);                                                               \
        UpdateContainer(parent->FIELD, parent, collector);                                                             \
    }
UPDATE(FILE, decls)
UPDATE(CLASS_BODY, decls)
UPDATE(STRUCT_BODY, decls)
UPDATE(INTERFACE_BODY, decls)
UPDATE(EXTEND_DECL, members)
UPDATE(BLOCK, body)
UPDATE(ARRAY_LIT, children)
UPDATE(TUPLE_LIT, children)
UPDATE(QUOTE_EXPR, exprs)
#undef UPDATE

#define UPDATE(KIND, FIELD)                                                                                            \
    template <> void UpdateSingleMacroInfo<ASTKind::KIND>(Node & curNode, MacroCollector & collector)                  \
    {                                                                                                                  \
        auto parent = StaticAs<ASTKind::KIND>(&curNode);                                                               \
        UpdateExpr(parent->FIELD, parent, collector);                                                                  \
    }
UPDATE(VAR_DECL, initializer)
UPDATE(LET_PATTERN_DESTRUCTOR, initializer)
UPDATE(ASSIGN_EXPR, rightExpr)
UPDATE(FUNC_PARAM, assignment)
UPDATE(CALL_EXPR, baseFunc)
UPDATE(MATCH_EXPR, selector)
UPDATE(MATCH_CASE, patternGuard)
UPDATE(MATCH_CASE_OTHER, matchExpr)
UPDATE(MEMBER_ACCESS, baseExpr)
UPDATE(IF_EXPR, condExpr)
UPDATE(WHILE_EXPR, condExpr)
UPDATE(DO_WHILE_EXPR, condExpr)
UPDATE(FUNC_ARG, expr)
UPDATE(RETURN_EXPR, expr)
UPDATE(PAREN_EXPR, expr)
UPDATE(TYPE_CONV_EXPR, expr)
UPDATE(UNARY_EXPR, expr)
UPDATE(VAR_WITH_PATTERN_DECL, initializer)
UPDATE(SPAWN_EXPR, task)
UPDATE(THROW_EXPR, expr)
UPDATE(PERFORM_EXPR, expr)
UPDATE(TRAIL_CLOSURE_EXPR, expr)
UPDATE(IS_EXPR, leftExpr)
UPDATE(AS_EXPR, leftExpr)
#undef UPDATE

template <> void UpdateSingleMacroInfo<ASTKind::BINARY_EXPR>(Node& curNode, MacroCollector& collector)
{
    auto parent = StaticAs<ASTKind::BINARY_EXPR>(&curNode);
    UpdateExpr(parent->rightExpr, parent, collector);
    UpdateExpr(parent->leftExpr, parent, collector);
}

template <> void UpdateSingleMacroInfo<ASTKind::ENUM_DECL>(Node& curNode, MacroCollector& collector)
{
    auto parent = StaticAs<ASTKind::ENUM_DECL>(&curNode);
    UpdateContainer(parent->members, parent, collector);
    UpdateContainer(parent->constructors, parent, collector);
}

template <> void UpdateSingleMacroInfo<ASTKind::RANGE_EXPR>(Node& curNode, MacroCollector& collector)
{
    auto parent = StaticAs<ASTKind::RANGE_EXPR>(&curNode);
    UpdateExpr(parent->startExpr, parent, collector);
    UpdateExpr(parent->stopExpr, parent, collector);
    UpdateExpr(parent->stepExpr, parent, collector);
}

template <> void UpdateSingleMacroInfo<ASTKind::FOR_IN_EXPR>(Node& curNode, MacroCollector& collector)
{
    auto parent = StaticAs<ASTKind::FOR_IN_EXPR>(&curNode);
    UpdateExpr(parent->inExpression, parent, collector);
    UpdateExpr(parent->patternGuard, parent, collector);
}

template <> void UpdateSingleMacroInfo<ASTKind::SUBSCRIPT_EXPR>(Node& curNode, MacroCollector& collector)
{
    auto parent = StaticAs<ASTKind::SUBSCRIPT_EXPR>(&curNode);
    UpdateExpr(parent->baseExpr, parent, collector);
    for (auto& expr : parent->indexExprs) {
        UpdateExpr(expr, parent, collector);
    }
}

template <> void UpdateSingleMacroInfo<ASTKind::FUNC_PARAM_LIST>(Node& curNode, MacroCollector& collector)
{
    auto parent = StaticAs<ASTKind::FUNC_PARAM_LIST>(&curNode);
    UpdateContainer(parent->params, parent, collector);
}

void UpdateMacroInfo(const Ptr<AST::Node> node, MacroCollector& collector)
{
    switch (node->astKind) {
#define ASTKIND(KIND, VALUE, NODE, SIZE)                                                                               \
    case AST::ASTKind::KIND: {                                                                                         \
        UpdateSingleMacroInfo<AST::ASTKind::KIND>(*node, collector);                                                   \
        break;                                                                                                         \
    }
#include "cangjie/AST/ASTKind.inc"
#undef ASTKIND
    }
}

}

#endif