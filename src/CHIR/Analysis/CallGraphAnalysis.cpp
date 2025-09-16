// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the callgraph analysis.
 * The main process will be:
 * 1. Collect all the function in the current package, add it to call graph
 *    (a map collect function with call graph node) in the format of call graph node.
 * 2. PopulateCallGraphNode by traverse all the expression in the function of current node. If
 *    it is a apply expression add a DIRECT kind edge to the edges(functions with specific kind
 *    that is called by the function of current node). If it is a invoke expression andd a VIRTUAL kind edge
 *    to the edges.
 * 3. Finish the build of call graph with first two step then BuildSCC to collect all the Strongly
 *    connected component(SCC) nodes in the current call graph.
 */

#include "cangjie/CHIR/Analysis/CallGraphAnalysis.h"

#include "cangjie/CHIR/Transformation/Devirtualization.h"
#include "cangjie/CHIR/Expression/Terminator.h"
#include "cangjie/CHIR/Visitor/Visitor.h"

namespace Cangjie::CHIR {

CallGraph::Node::Iterator CallGraph::Node::Begin()
{
    return calledEdges.begin();
}

CallGraph::Node::Iterator CallGraph::Node::End()
{
    return calledEdges.end();
}

CallGraph::Node::ConstIterator CallGraph::Node::Begin() const
{
    return calledEdges.begin();
}

CallGraph::Node::ConstIterator CallGraph::Node::End() const
{
    return calledEdges.end();
}

bool CallGraph::Node::Empty() const
{
    return calledEdges.empty();
}

Func* CallGraph::Node::GetFunction() const
{
    return func;
}

void CallGraph::Node::AddCalledEdge(const Edge& edge)
{
    calledEdges.emplace_back(edge);
}

void CallGraph::Node::DeleteCalledEdge(const Edge& edge)
{
    auto it = find(calledEdges.begin(), calledEdges.end(), edge);
    if (it != calledEdges.end()) {
        calledEdges.erase(it);
    }
}

CallGraph::Edge::Edge(Node* n, Kind k)
{
    edgeValue = std::make_pair(n, k);
}

inline CallGraph::Node* CallGraph::Edge::GetNode() const
{
    return edgeValue.first;
}

inline CallGraph::Edge::Kind CallGraph::Edge::GetKind() const
{
    return edgeValue.second;
}

bool CallGraph::Edge::operator==(const Edge& other) const
{
    return this->edgeValue.first == other.edgeValue.first;
}

CallGraph::CallGraph(const Package* package, DevirtualizationInfo& devirtFuncInfo)
    : devirtFuncInfo(devirtFuncInfo),
      entryNode(std::make_unique<Node>(nullptr)),
      exitNode(std::make_unique<Node>(nullptr))
{
    // build the call graph.
    for (auto func : package->GetGlobalFuncs()) {
        if (func->GetUsers().size() == 0) {
            AddToCallGraph(*func, true);
        } else {
            AddToCallGraph(*func, false);
        }
    }
}

void CallGraph::AddToCallGraph(const Func& func, bool isCalledByEntryNode)
{
    CallGraph::Node* node = GetOrCreateNode(func);

    // If this function has zero uses, then anything could call it.
    // add it to the calledFunctions of entryNode.
    if (isCalledByEntryNode) {
        Edge calledEdge(node, Edge::Kind::VIRTUAL);
        entryNode->AddCalledEdge(calledEdge);
    }

    PopulateCallGraphNode(*node, *(node->GetFunction()->GetBody()));
}

// GetOrCreateNode - This method will insert a new call graph Node for the specified function
// if one does not already exist.
CallGraph::Node* CallGraph::GetOrCreateNode(const Func& func)
{
    auto& callGraphNode = functionMap[&func];
    if (callGraphNode) {
        return callGraphNode.get();
    }

    callGraphNode = std::make_unique<Node>(const_cast<Func*>(&func));
    return callGraphNode.get();
}

void CallGraph::PopulateCallGraphNode(Node& node, BlockGroup& funcBlockGroup)
{
    auto preVisit = [&node, this](Expression& expr) {
        if (expr.GetExprKind() == ExprKind::INVOKE || expr.GetExprKind() == ExprKind::INVOKE_WITH_EXCEPTION) {
            // Get all the possible callee of virtual function from Devirtualization info collection.
            // exclude it from EntryNode and add it here.
            AddVirtualEdgeToNode(node, expr);
        } else if (expr.GetExprKind() == ExprKind::APPLY || expr.GetExprKind() == ExprKind::APPLY_WITH_EXCEPTION) {
            AddDirectEdgeToNode(node, expr);
        } else if (expr.GetExprKind() == ExprKind::LAMBDA) {
            PopulateCallGraphNode(node, *(StaticCast<const Lambda*>(&expr)->GetBody()));
        }
        return VisitResult::CONTINUE;
    };

    Visitor::Visit(funcBlockGroup, preVisit, []([[maybe_unused]] Expression& e) { return VisitResult::CONTINUE; });
}

void CallGraph::AddVirtualEdgeToNode(Node& node, const Expression& expression)
{
    // Get all the possible callee of virtual function from Devirtualization info collection.
    // exclude it from EntryNode and add it here.
    Type* resTy;
    std::vector<Type*> types;
    std::string methodName;
    if (expression.GetExprKind() == ExprKind::INVOKE) {
        auto invoke = StaticCast<const Invoke*>(&expression);
        resTy = invoke->GetObject()->GetType();
        types = invoke->GetMethodType()->GetParamTypes();
        methodName = invoke->GetMethodName();
    } else {
        auto invoke = StaticCast<const InvokeWithException*>(&expression);
        resTy = invoke->GetObject()->GetType();
        types = invoke->GetMethodType()->GetParamTypes();
        methodName = invoke->GetMethodName();
    }
    while (resTy->IsRef()) {
        resTy = StaticCast<RefType*>(resTy)->GetBaseType();
    }
    if (resTy->IsClass()) {
        std::vector<Type*> paramTys;
        for (size_t i = 1; i < types.size(); i++) {
            paramTys.emplace_back(types[i]);
        }
        auto allPossibleCalleeOfInvoke =
            Utils::SetToVec<FuncBase*>(GetAllPossibleCalleeOfInvoke(std::make_pair(methodName, paramTys)));
        std::sort(allPossibleCalleeOfInvoke.begin(), allPossibleCalleeOfInvoke.end(),
            [](const Ptr<const FuncBase> v1, const Ptr<const FuncBase> v2) {
                return v1->GetIdentifier() < v2->GetIdentifier();
            });
        for (auto possibleCalleeOfInvoke : allPossibleCalleeOfInvoke) {
            if (auto callee = DynamicCast<const Func*>(possibleCalleeOfInvoke); callee) {
                Edge edge(GetOrCreateNode(*callee), Edge::Kind::VIRTUAL);
                entryNode->DeleteCalledEdge(edge);
                node.AddCalledEdge(edge);
            } else {
                Edge edge(exitNode.get(), Edge::Kind::VIRTUAL);
                entryNode->DeleteCalledEdge(edge);
                node.AddCalledEdge(edge);
            }
        }
    }
}

void CallGraph::AddDirectEdgeToNode(Node& node, const Expression& expression)
{
    const Func* calledFunc;
    if (expression.GetExprKind() == ExprKind::APPLY) {
        calledFunc = DynamicCast<const Func*>(StaticCast<const Apply*>(&expression)->GetCallee());
    } else {
        auto apply = StaticCast<const ApplyWithException*>(&expression);
        calledFunc = DynamicCast<const Func*>(apply->GetCallee());
    }
    if (calledFunc) {
        Edge edge(GetOrCreateNode(*calledFunc), Edge::Kind::DIRECT);
        node.AddCalledEdge(edge);
    } else {
        Edge edge(exitNode.get(), Edge::Kind::DIRECT);
        node.AddCalledEdge(edge);
    }
}

// Get all the possible callee function of a virtual function from Devirtualization info collection.
// Only care about the method name and type, ignore the classType of invoker.
std::unordered_set<FuncBase*> CallGraph::GetAllPossibleCalleeOfInvoke(
    const std::pair<std::string, std::vector<Type*>>& method) const
{
    (void)method;
    (void)devirtFuncInfo;
    return std::unordered_set<FuncBase*>();
}

void CallGraphAnalysis::DoCallGraphAnalysis(bool isDebug)
{
    CallGraph callGraph(package, devirtFuncInfo);
    BuildSCC(callGraph);
    if (isDebug) {
        PrintCallGraph(callGraph);
    }
}

void CallGraphAnalysis::PrintCallGraph(const CallGraph& callGraph) const
{
    std::vector<CallGraph::Node*> nodeStack;
    std::set<CallGraph::Node*> nodeSet;
    auto entry = callGraph.GetEntryNode();
    for (auto it = entry->Begin(); it != entry->End(); ++it) {
        nodeStack.push_back(it->GetNode());
        nodeSet.insert(it->GetNode());
    }
    while (!nodeStack.empty()) {
        auto node = nodeStack.back();
        nodeStack.pop_back();
        // No callFunctions for this node, it is a leaf node
        if (node->Empty()) {
            std::string message = "[CallGraphAnalysis] Call Graph found ";
            if (node->GetFunction()) {
                message += node->GetFunction()->GetIdentifierWithoutPrefix() + "\n";
                std::cout << message;
            }
            continue;
        }

        std::string message =
            "[CallGraphAnalysis] Call Graph found " + node->GetFunction()->GetIdentifierWithoutPrefix();
        for (auto it = node->Begin(); it != node->End(); ++it) {
            message += (it->GetKind()) ? ", DIRECT CALL:" : ", VIRTUAL CALL:";
            message += (it->GetNode()->GetFunction()) ? it->GetNode()->GetFunction()->GetIdentifierWithoutPrefix()
                                                      : "Unknown Function";
            if (auto pos = nodeSet.find(it->GetNode()); pos == nodeSet.end()) {
                nodeStack.push_back(it->GetNode());
                nodeSet.insert(it->GetNode());
            }
        }
        message += "\n";
        std::cout << message;
    }
}

// Enumerate the SCCs of a directed graph in reverse topological order
// Implement a Tarjan's DFS algorithm using an internal stack to build
// up a vector of nodes in a particular SCC.
// 1. Start DFS traverse from the entryNode. Increase the DFS flag visitNum with the visit of
//    child node(calledFunctions)
// 2. Stop the DFS traverse when there is no child node
// 3. Pop out node from the VisitStack and propagate the minimum DFS flag of node to parent.
// 4. If the original DFS flag number of node equal to the minimum one after propagate.
//    node in the sccNodeStack is SCC.
void CallGraphAnalysis::BuildSCC(const CallGraph& callGraph)
{
    DFSVisitOne(*callGraph.GetEntryNode());
    do {
        GetNextSCC();
        for (unsigned i = 0; i < currentSCC.size(); i++) {
            postOrderSCCFunctionlist.push_back(currentSCC[i]->GetFunction());
        }
    } while (!currentSCC.empty());
}

void CallGraphAnalysis::DFSVisitOne(CallGraph::Node& node)
{
    ++visitNum;
    nodeVisitNumbers[&node] = visitNum;
    sccNodeStack.push_back(&node);
    visitStack.push_back(StackElement(&node, node.Begin(), visitNum));
}

void CallGraphAnalysis::GetNextSCC()
{
    currentSCC.clear(); // Prepare to compute the next SCC
    while (!visitStack.empty()) {
        DFSVisitChildren();

        // Pop the leaf on the top of the VisitStack.
        CallGraph::Node* visitingN = visitStack.back().node;
        unsigned minVisitNum = visitStack.back().minVisited;
        visitStack.pop_back();

        // Propagate minVisitNum to parent so we can detect the SCC starting node.
        if (!visitStack.empty() && visitStack.back().minVisited > minVisitNum) {
            visitStack.back().minVisited = minVisitNum;
        }

        if (minVisitNum != nodeVisitNumbers[visitingN]) {
            continue;
        }

        // A full SCC is on the sccNodeStack! It includes all nodes below
        // visitingN on the stack. Copy those nodes to the currentSCC,
        // reset their minVisit values, and return (this suspends
        // the DFS traversal till the GetNextSCC).
        do {
            currentSCC.push_back(sccNodeStack.back());
            sccNodeStack.pop_back();
            nodeVisitNumbers[currentSCC.back()] = ~0U;
        } while (currentSCC.back() != visitingN);
        return;
    }
}

void CallGraphAnalysis::DFSVisitChildren()
{
    while (visitStack.back().nextChild != visitStack.back().node->End()) {
        // TOS has at least one more child so continue DFS
        CallGraph::Node* childN = (*visitStack.back().nextChild++).GetNode();
        typename std::map<CallGraph::Node*, unsigned>::iterator visited = nodeVisitNumbers.find(childN);
        if (visited == nodeVisitNumbers.end()) {
            // this node has never been seen.
            DFSVisitOne(*childN);
            continue;
        }
        unsigned childNum = visited->second;
        if (visitStack.back().minVisited > childNum) {
            visitStack.back().minVisited = childNum;
        }
    }
}
} // namespace Cangjie::CHIR
