// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the Walker related classes.
 */

#ifndef CANGJIE_AST_WALKER_H
#define CANGJIE_AST_WALKER_H

#include <atomic>
#include <functional>
#include <utility>

#include "cangjie/AST/Node.h"

namespace Cangjie::AST {
/**
 * Actions returned by Walker callbacks. These are ordinary enum values, not bit flags;
 * there is no VisitAction operator| for combining them.
 *
 * The pre-visit action controls whether children are visited. STOP_NOW immediately
 * terminates traversal and propagates through ancestor nodes. After visiting children,
 * KEEP_DECISION from the post-visit callback preserves the current action; any other
 * post-visit action replaces it. KEEP_DECISION is intended for post-visit cleanup.
 */
enum class VisitAction : uint8_t {
    WALK_CHILDREN, /**< Continue to walk into child items. */
    SKIP_CHILDREN, /**< Continue walking, but don't enter child items. */
    STOP_NOW,       /**< Stop walking immediately. */
    KEEP_DECISION   /**< Only clean up states. Keep action as it is. */
};

/**
 * The main class used for walking the Rune AST.
 */
template <class NodeT>
class WalkerT {
    /**
     * A typealias for the visit function callback. It accepts a pointer to the Node being visited as its only
     * parameter and returns the VisitAction after walking into it.
     */
    using VisitFunc = std::function<VisitAction(Ptr<NodeT>)>;

public:
    /**
     * The constructor to create an AST walker.
     * @param node The AST node being visited.
     * @param VisitPre The function executed before walking into its children.
     * @param VisitPost The function executed after walking into its children.
     */
    explicit WalkerT(Ptr<NodeT> node, VisitFunc VisitPre = nullptr, VisitFunc VisitPost = nullptr)
        : node(node), VisitPre(std::move(VisitPre)), VisitPost(std::move(VisitPost))
    {
        ID = GetNextWalkerID();
    }

    /**
     * The constructor to create an AST walker.
     * @param node The AST node being visited.
     * @param id Given walker id for current walker.
     * @param VisitPre The function executed before walking into its children.
     * @param VisitPost The function executed after walking into its children.
     */
    WalkerT(Ptr<NodeT> node, unsigned id, VisitFunc VisitPre = nullptr, VisitFunc VisitPost = nullptr)
        : node(node), VisitPre(std::move(VisitPre)), VisitPost(std::move(VisitPost)), ID(id)
    {
    }
    /**
     * The function starts an AST walking.
     */
    void Walk() const
    {
        Walk(node);
    };

    static unsigned GetNextWalkerID();

private:
    /**
     * The AST node as walking entry.
     */
    Ptr<NodeT> node;
    /**
     * The function executed before walking into its children.
     */
    VisitFunc VisitPre;
    /**
     * The function executed after walking into its children.
     */
    VisitFunc VisitPost;
    /**
     * Walker ID.
     */
    unsigned ID{0};
    /**
     * Next Walker ID.
     */
    static std::atomic_uint nextWalkerID;

    /**
     * The function used internally for walking a certain AST node.
     * @param curNode The node to be walked.
     * @return VisitAction decision after walking into it.
     */
    VisitAction Walk(Ptr<NodeT> curNode) const;
    template <typename T> friend class WalkerT;
};
using Walker = WalkerT<Node>;
using ConstWalker = WalkerT<const Node>;
extern template class WalkerT<Node>;
extern template class WalkerT<const Node>;
} // namespace Cangjie::AST

#endif // CANGJIE_AST_WALKER_H
