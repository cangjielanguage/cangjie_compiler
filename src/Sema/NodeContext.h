// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file This file introduces the context of a node, that is, all parents of it up to the root (file or package).
 */

#ifndef CANGJIE_SEMA_NODE_CONTEXT_H
#define CANGJIE_SEMA_NODE_CONTEXT_H

#include "cangjie/AST/Node.h"

#include <vector>

namespace Cangjie::AST {

/**
 * @brief A stack-based context for tracking parent nodes during AST traversal.
 *
 * This class maintains a stack of AST nodes and provides efficient lookup for parent nodes,
 * with special caching for funclike nodes (FuncDecl, LambdaExpr, PrimaryCtorDecl, MacroDecl).
 */
class NodeContext {
public:
    NodeContext() = default;
    ~NodeContext() = default;

    /**
     * @brief Push a node onto the context stack.
     *
     * If the node is funclike, updates the cached funclike parent.
     */
    void Push(Node* node)
    {
        nodes.push_back(node);
        if (node != nullptr && node->IsFuncLike()) {
            cachedFuncLike = node;
        }
    }

    /**
     * @brief Pop the top node from the context stack.
     *
     * If the popped node is funclike, recalculates the cached funclike parent.
     */
    void Pop()
    {
        if (nodes.empty()) {
            return;
        }
        Node* top = nodes.back();
        nodes.pop_back();
        if (top != nullptr && top->IsFuncLike()) {
            // Recalculate cached funclike by searching backwards
            cachedFuncLike = nullptr;
            for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
                if (*it != nullptr && (*it)->IsFuncLike()) {
                    cachedFuncLike = *it;
                    break;
                }
            }
        }
    }

    /**
     * @brief Get the closest parent node matching the given ASTKind.
     *
     * Searches from the most recent (top) to oldest, excluding the top node itself.
     * @param kind The ASTKind to search for.
     * @return The closest matching parent node, or nullptr if not found.
     */
    Node* GetParent(ASTKind kind) const
    {
        static constexpr size_t MIN_NODE_STACK_FOR_PARENT = 2;
        if (nodes.size() < MIN_NODE_STACK_FOR_PARENT) {
            return nullptr;
        }
        // Start from second-to-last (skip the current node at top)
        for (auto it = nodes.rbegin() + 1; it != nodes.rend(); ++it) {
            if (*it != nullptr && (*it)->astKind == kind) {
                return *it;
            }
        }
        return nullptr;
    }

    /**
     * @brief Get the closest funclike parent node.
     *
     * This method uses a cached value that is only updated when pushing/popping funclike nodes,
     * making it O(1) in most cases.
     * @return The closest funclike parent node, or nullptr if not inside a function.
     */
    Node* GetParentFuncLike() const
    {
        return cachedFuncLike;
    }

    /**
     * @brief Check if currently inside a funclike node.
     */
    bool IsInsideFuncLike() const
    {
        return cachedFuncLike != nullptr;
    }

    /**
     * @brief Get the current (top) node.
     */
    Node* Current() const
    {
        return nodes.empty() ? nullptr : nodes.back();
    }

    /**
     * @brief Check if the context stack is empty.
     */
    bool Empty() const
    {
        return nodes.empty();
    }

    /**
     * @brief Get the number of nodes in the context stack.
     */
    size_t Size() const
    {
        return nodes.size();
    }

private:
    std::vector<Node*> nodes;
    Node* cachedFuncLike = nullptr;
};

/**
 * @brief RAII guard for pushing/popping nodes from NodeContext.
 */
class NodeContextGuard {
public:
    NodeContextGuard(NodeContext& ctx, Node* node) : context(ctx)
    {
        context.Push(node);
    }

    ~NodeContextGuard()
    {
        context.Pop();
    }

    NodeContextGuard(const NodeContextGuard&) = delete;
    NodeContextGuard& operator=(const NodeContextGuard&) = delete;

private:
    NodeContext& context;
};

} // namespace Cangjie::AST
#endif
