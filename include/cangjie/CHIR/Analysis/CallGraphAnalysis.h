// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_CALLGRAPH_ANALYSIS_H
#define CANGJIE_CHIR_CALLGRAPH_ANALYSIS_H

#include "cangjie/CHIR/IR/Package.h"
#include "cangjie/CHIR/Optimization/Devirtualization.h"
#include "cangjie/CHIR/IR/Value/Value.h"

namespace Cangjie::CHIR {

class CallGraph {
public:
    class Node;
    class Edge;

    /// A node in the call graph for a package.
    /// Typically represents a function in the call graph. There are also special
    /// "null" nodes used to represent theoretical entries in the call graph.
    class Node {
    public:
        /// Creates a node for the specified function.
        explicit Node(Func* func) : func(func)
        {
        }

        Node(const Node&) = delete;
        Node& operator=(const Node&) = delete;

        using Iterator = std::vector<Edge>::iterator;
        using ConstIterator = std::vector<Edge>::const_iterator;

        /// iterator operation of call graph.
        Iterator Begin();
        Iterator End();
        ConstIterator Begin() const;
        ConstIterator End() const;
        bool Empty() const;

        /// Returns the function that this call graph node represents.
        Func* GetFunction() const;

        /// Adds a edge to current node.
        void AddCalledEdge(const Edge& edge);

        /// Delete a edge from current node.
        void DeleteCalledEdge(const Edge& edge);

    private:
        friend class CallGraph;

        Func* func;

        /// The edge called by the function of current node.
        std::vector<Edge> calledEdges;
    };

    class Edge {
    public:
        /// The kind of call in call graph
        /// Edge kind of apply expression is DIRECT.
        /// Edge kind of invoke expression or the abstract call from entryNode is VIRTUAL.
        enum Kind : bool { VIRTUAL = false, DIRECT = true };

        Edge() = default;

        explicit Edge(Node* n, Kind k);

        inline Node* GetNode() const;

        inline Kind GetKind() const;

        bool operator==(const Edge& other) const;

    private:
        friend class Node;

        std::pair<Node*, Kind> edgeValue;
    };

    explicit CallGraph(const Package* package, DevirtualizationInfo& devirtFuncInfo);

    /// This will insert a new call graph node for
    /// Func if one does not already exist.
    Node* GetOrCreateNode(const Func& func);

    /// Populate call graph node based on the calls inside the associated function's block group.
    void PopulateCallGraphNode(Node& node, BlockGroup& funcBlockGroup);

    /// Add a function to the call graph, and link the node to all of the
    /// functions that it calls.
    void AddToCallGraph(const Func& func, bool isCalledByEntryNode);

    /// Returns the Node which is used to represent
    /// undetermined calls into the callgraph.
    Node* GetEntryNode() const
    {
        return entryNode.get();
    }

    /// Get all the possible callee func of invoke.
    std::unordered_set<FuncBase*> GetAllPossibleCalleeOfInvoke(
        const std::pair<std::string, std::vector<Type*>>& method) const;

private:
    DevirtualizationInfo& devirtFuncInfo;

    using FunctionMapTy = std::map<const Func*, std::unique_ptr<Node>>;

    /// A map from Func* to Node*.
    FunctionMapTy functionMap;

    /// This node has edges to all external functions and those internal
    /// functions that have their address taken.
    std::unique_ptr<Node> entryNode;

    /// This node has edges to it from all functions making indirect calls
    /// or calling an external function.
    std::unique_ptr<Node> exitNode;

    void AddVirtualEdgeToNode(Node& node, const Expression& expression);

    void AddDirectEdgeToNode(Node& node, const Expression& expression);
};

class CallGraphAnalysis {
public:
    explicit CallGraphAnalysis(const Package* package, DevirtualizationInfo& devirtFuncInfo)
        : package(package), devirtFuncInfo(devirtFuncInfo)
    {
    }
    /// Call Graph Analysis for specific Package.
    void DoCallGraphAnalysis(bool isDebug);

    /// The Function list of post-order sequence of SCCs.
    /// This list is formed the first time we walk the graph.
    std::vector<Func*> postOrderSCCFunctionlist;

private:
    const Package* package;
    DevirtualizationInfo& devirtFuncInfo;

    /// Build the SCCs for Call Graph.
    void BuildSCC(const CallGraph& callGraph);

    /// Print the Call Graph for debug.
    void PrintCallGraph(const CallGraph& callGraph) const;

    /// Element of VisitStack during DFS.
    struct StackElement {
        CallGraph::Node* node;               // The current node pointer.
        CallGraph::Node::Iterator nextChild; // The next child node, modified inplace during DFS.
        unsigned minVisited;                 // Minmum uplink value of all children of Node.

        StackElement(CallGraph::Node* node, const CallGraph::Node::Iterator& child, unsigned min)
            : node(node), nextChild(child), minVisited(min)
        {
        }

        bool operator==(const StackElement& other) const
        {
            return node == other.node && nextChild == other.nextChild && minVisited == other.minVisited;
        }
    };

    /// The visit counters used to detect when a complete SCC is on the stack.
    /// visitNum is the global counter.
    ///
    /// nodeVisitNumbers are per-node visit numbers, also used as DFS flags.
    unsigned visitNum = 0;
    std::map<CallGraph::Node*, unsigned> nodeVisitNumbers;

    /// Stack holding nodes of the SCC.
    std::vector<CallGraph::Node*> sccNodeStack;

    /// The current SCC
    std::vector<CallGraph::Node*> currentSCC;

    /// DFS stack, Used to maintain the ordering. The top contains the current node,
    /// the next child to visit, and the mininum unplink value of all child
    std::vector<StackElement> visitStack;

    /// A single "visit" within the non-recursive DFS traversal.
    void DFSVisitOne(CallGraph::Node& node);
    /// The stack-based DFS traversal; defined below.
    void DFSVisitChildren();
    /// Compute the next SCC using the DFS traversal.
    void GetNextSCC();
};
} // namespace Cangjie::CHIR

#endif
