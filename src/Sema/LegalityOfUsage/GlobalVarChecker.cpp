// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements initialization checking for global variables.
 */

#include "TypeCheckerImpl.h"

#include <map>
#include <vector>

#include "TypeCheckUtil.h"

#include "cangjie/AST/Match.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Basic/Match.h"

namespace Cangjie {
using namespace AST;
using namespace Meta;
using namespace TypeCheckUtil;

namespace {
/**
 * For each global variable `g`, we wrap it in a `DefNode`
 * and collect other global variables *MAY* be use by `g` during its initialization.
 * See @DoCollect for more details.
 * For example,
 *
 * ```
 * let g1 = foo() // g1 may use { g2 }
 * let g2 = 3     // g2 may use { }
 * let g3 = bar() // g2 may use { g1, g2 }
 * func foo() { g2 = 4 }
 * func bar() { g1 = g2 }
 * ```
 *
 * We build a def-use graph for all global variables, and do two checking phrases:
 * 1. For global variables in a same file,
 *    we check whether they are initialized in (their declaration) order.
 *    In the above example, `g2` is used before being initialized.
 *
 * 2. For global variables from different files, the checking becomes tricky:
 *    because we can reorder the initialization order of variables from different files.
 *    I.e., the checking is valid as long as we can find a valid initialization order.
 *    To enforce this checking, we attempt to find a topological order between variables.
 *
 *
 * Data structure for the def-use graph.
 */
struct DefNode;
struct UseEdge;

/**
 * Auxiliary functions
 */
bool IsGlobalOrStaticOrConstVar(const Decl& decl)
{
    if ((decl.astKind == ASTKind::VAR_DECL || decl.astKind == ASTKind::VAR_WITH_PATTERN_DECL) &&
        (decl.TestAnyAttr(Attribute::GLOBAL, Attribute::STATIC) || decl.IsConst())) {
        return true;
    }
    return false;
}

bool IsRefGlobalOrStaticVar(const RefExpr& re)
{
    CJC_ASSERT(re.ref.target);
    return IsGlobalOrStaticOrConstVar(*re.ref.target);
}

Ptr<Decl> GetTargetIfShouldCollect(const Expr& expr)
{
    auto target = expr.GetTarget();
    if (target == nullptr) {
        return target;
    }
    // Get real used target. eg: if target is property, get real used getter or setter which is decided by expr.
    target = GetUsedMemberDecl(*target, !expr.TestAttr(Attribute::LEFT_VALUE));
    // The target only need be collected when it is funcDecl.
    return target->astKind == ASTKind::FUNC_DECL ? target : nullptr;
}

inline bool CanBeIgnored(const Decl& decl)
{
    return decl.TestAnyAttr(Attribute::UNSAFE, Attribute::IMPORTED, Attribute::FOREIGN);
}

bool IsInSameFile(const Node& n1, const Node& n2)
{
    if (n1.astKind == ASTKind::FILE || n2.astKind == ASTKind::FILE) {
        return false;
    }
    return n1.curFile && n2.curFile && n1.curFile->fileHash == n2.curFile->fileHash;
}

/**
 * A wrapper of `DefNode` with `RefExpr`.
 * The `RefExpr` records the position where the `DefNode` is used
 * and it's used for a better error reporting.
 */
struct UseEdge {
    const Node& refNode;
    const Position refPos;
    std::string refName;
    DefNode& node;

    explicit UseEdge(DefNode& n);
    UseEdge(const Node& node, const Position& pos, const std::string& name, DefNode& n)
        : refNode(node), refPos(pos), refName(name), node(n)
    {
    }
};

/**
 * The concept of tri-color marking is borrowed from tracing garbage collection.
 * The white set is the set of objects that are candidates for having their memory recycled.
 * The black set is the set of objects that can be shown to have no outgoing references to objects in the white set,
 * and to be reachable from the roots. Objects in the black set are not candidates for collection.
 * The grey set contains all objects reachable from the roots but yet to be scanned for references to "white" objects.
 * Since they are known to be reachable from the roots, they cannot be garbage-collected and will end up
 * in the black set after being scanned.
 */
enum class Color {
    WHITE,
    BLACK,
    GRAY,
};

/**
 * A `DefNode` is a wrapper of `VarDecl` with `UseEdge`s.
 * The `visitOrder` field ensures that:
 *   if two variable `a` and `b` are defined in the same file and `a` is defined first;
 *   then, `a.visitOrder` < `b.visitOrder`.
 */
struct DefNode {
    const Decl& var;
    std::vector<UseEdge> usage;  // Current variable uses other variables.
    std::vector<UseEdge> usedBy; // Current variable is used by other variables.
    int visitOrder = -1;         // This field reflects the declaration order of current variable in the file
    Color color{Color::WHITE};

    explicit DefNode(const Decl& v) : var(v)
    {
    }
};

UseEdge::UseEdge(DefNode& n) : refNode(n.var), node(n)
{
}

class VarWithPatternDeclMap {
public:
    void Add(const VarWithPatternDecl& vpd)
    {
        if (!Utils::In(&vpd, cachedVPDs)) {
            ConstWalker(&vpd, [this, &vpd](auto node) {
                if (node->astKind == ASTKind::VAR_DECL) {
                    outerVPDMap.emplace(StaticCast<VarDecl*>(node), &vpd);
                    return VisitAction::SKIP_CHILDREN;
                }
                return VisitAction::WALK_CHILDREN;
            }).Walk();
            cachedVPDs.emplace(&vpd);
        }
    }

    Ptr<const VarWithPatternDecl> GetOuterVPD(const VarDecl& vd) const
    {
        auto iter = outerVPDMap.find(&vd);
        if (iter == outerVPDMap.cend()) {
            return nullptr;
        }
        return iter->second;
    }

private:
    std::unordered_set<Ptr<const VarWithPatternDecl>> cachedVPDs;
    std::unordered_map<Ptr<const VarDecl>, Ptr<const VarWithPatternDecl>> outerVPDMap;
};

/**
 * The def-use graph
 */
struct DefUseGraph {
    DefNode& GetOrBuildNode(const Decl& var)
    {
        Ptr<const Decl> vda = &var;
        if (auto vpd = DynamicCast<const VarWithPatternDecl*>(vda)) {
            vpdMap.Add(*vpd);
        } else if (auto vd = DynamicCast<const VarDecl*>(vda)) {
            auto outerVPD = vpdMap.GetOuterVPD(*vd);
            if (outerVPD != nullptr) {
                vda = outerVPD;
            }
        }
        auto it = nodes.find(vda);
        if (it != nodes.cend()) {
            return *it->second;
        }
        auto node = MakeOwned<DefNode>(*vda);
        auto rawPtr = node.get();
        nodes[vda] = std::move(node);
        return *rawPtr;
    }

    void AddEdge(const Decl& user, const VarDeclAbstract& used, const Expr& refExpr)
    {
        auto& userNode = GetOrBuildNode(user);
        auto& usedNode = GetOrBuildNode(used);
        Position pos;
        std::string refName;
        if (auto re = DynamicCast<const RefExpr*>(&refExpr); re) {
            pos = re->begin;
            refName = re->ref.identifier;
        } else if (auto ma = DynamicCast<const MemberAccess*>(&refExpr); ma) {
            pos = ma->field.Begin();
            refName = ma->field;
        } else {
            CJC_ABORT();
        }
        userNode.usage.emplace_back(refExpr, pos, refName, usedNode);
        usedNode.usedBy.emplace_back(refExpr, pos, refName, userNode);
    }

    void AddEdge(const Decl& user, const VarDeclAbstract& used)
    {
        auto& userNode = GetOrBuildNode(user);
        auto& usedNode = GetOrBuildNode(used);
        userNode.usage.emplace_back(usedNode);
        usedNode.usedBy.emplace_back(userNode);
    }
    std::map<Ptr<const Decl>, OwnedPtr<DefNode>, CmpNodeByPos> nodes;
    VarWithPatternDeclMap vpdMap;
};

class GlobalVarChecker {
public:
    explicit GlobalVarChecker(DiagnosticEngine& d) : diag(d)
    {
    }
    void DoCollect(const Package& package);
    void DoCheck(const Package& package);

    ~GlobalVarChecker()
    {
        currentNode = nullptr;
    }

private:
    template <typename DeclType> void CollectForStaticVar(const Decl& structDecl);
    void CollectForVar(const Decl& decl);
    void CollectVarUsageBFS(const Node& node);
    void CollectVarUsageBFSImpl(const Node& node, std::queue<Ptr<const Node>>& worklist);
    void CollectForStaticInit(const FuncDecl& staticInit, const std::unordered_set<Ptr<Decl>>& staticVars);
    std::vector<Ptr<VarDecl>> GetDefaultInitVariables(Decl& outerDecl) const;
    template <ASTKind Kind> std::vector<Ptr<VarDecl>> GetDefaultInitVariablesInStruct(Decl& structDecl) const;
    void CollectInRefExpr(const AST::RefExpr& re);
    void CollectInMemberAccessExpr(const MemberAccess& ma);
    bool CheckInSameFile() const;
    bool CheckVarUsageForDefNode(const DefNode& defNode) const;
    void CheckCrossFile(const Package& package);
    void AddInitializationOrderEdge(const File& file);
    void CheckByToposort();
    bool ToposortDFS(DefNode& node);

    DiagnosticEngine& diag;
    DefUseGraph graph;
    Ptr<DefNode> currentNode{nullptr};
    int visitOrder = 0;
};

void GlobalVarChecker::DoCollect(const Package& package)
{
    for (auto& file : package.files) {
        if (file == nullptr) {
            continue;
        }
        // For each global or static variable,
        // we collect dependent variables it may use during its initialization.
        for (auto& decl : file->decls) {
            CJC_ASSERT(decl);
            if (IsGlobalOrStaticOrConstVar(*decl)) {
                CollectForVar(*decl);
            } else if (decl->astKind == ASTKind::CLASS_DECL) {
                CollectForStaticVar<ClassDecl>(*decl);
            } else if (decl->astKind == ASTKind::STRUCT_DECL) {
                CollectForStaticVar<StructDecl>(*decl);
            }
        }
    }
}

/**
 * For each global variable, we collect all other global variables it MAY use during the initialization.
 * With a conservative strategy, the collecting phrase analyzes recursively all functions it refers,
 * i.e., it conducts a context-insensitive reachability analysis.
 *
 * ```
 * let g1 = foo() // g1 may use { g2 } though it will not use g2 actually.
 * let g2 = 3     // g2 may use { }
 * func foo() { let _ = bar // NO calling }
 * func bar() { g2 = 4 }
 */
void GlobalVarChecker::CollectForVar(const Decl& decl)
{
    auto& varDecl = StaticCast<const VarDeclAbstract&>(decl);
    CJC_ASSERT(varDecl.TestAnyAttr(Attribute::GLOBAL, Attribute::STATIC) || varDecl.IsConst());
    currentNode = &graph.GetOrBuildNode(varDecl);
    currentNode->visitOrder = visitOrder++;
    CollectVarUsageBFS(varDecl);
}

void GlobalVarChecker::CollectForStaticInit(const FuncDecl& staticInit, const std::unordered_set<Ptr<Decl>>& staticVars)
{
    std::unordered_set<Ptr<Decl>> visited;
    std::function<VisitAction(Ptr<Node>)> preVisit = [this, &preVisit, &visited, &staticVars](auto node) {
        if (auto expr = DynamicCast<Expr*>(node); expr && expr->desugarExpr) {
            Walker(expr->desugarExpr.get(), preVisit).Walk();
            return VisitAction::SKIP_CHILDREN;
        }
        if (auto ae = DynamicCast<AssignExpr*>(node); ae && ae->leftValue && ae->rightExpr) {
            auto target = DynamicCast<VarDecl*>(ae->leftValue->GetTarget());
            if (target && staticVars.count(target) != 0 && visited.count(target) == 0) {
                target->EnableAttr(AST::Attribute::INITIALIZED);
                bool isLet = !target->isVar;
                bool isCommon = target->TestAttr(AST::Attribute::COMMON);
                bool isStatic = target->TestAttr(AST::Attribute::STATIC);
                if (isLet && isCommon && isStatic) {
                    diag.DiagnoseRefactor(DiagKindRefactor::sema_common_static_let_cant_be_initialized_in_static_init,
                        *node, target->identifier.Val());
                }
                currentNode = &graph.GetOrBuildNode(*target);
                // Update visitOrder when initializing static members inside static init.
                currentNode->visitOrder = visitOrder++;
                CollectVarUsageBFS(*ae->rightExpr);
                visited.emplace(target);
                return VisitAction::SKIP_CHILDREN;
            }
        }
        return VisitAction::WALK_CHILDREN;
    };
    Walker(staticInit.funcBody.get(), preVisit).Walk();
    currentNode = &graph.GetOrBuildNode(staticInit);
    currentNode->visitOrder = visitOrder++;
    CollectVarUsageBFS(staticInit);
}

/**
 * Collect all static variables in the class or struct.
 * DeclType will be `ClassDecl` or `StructDecl`.
 */
template <typename DeclType> void GlobalVarChecker::CollectForStaticVar(const Decl& structDecl)
{
    auto* outerDecl = StaticCast<const DeclType*>(&structDecl);
    CJC_NULLPTR_CHECK(outerDecl->body);
    std::unordered_set<Ptr<Decl>> uninitStaticVars;
    Ptr<FuncDecl> staticInit = nullptr;
    for (auto& decl : outerDecl->body->decls) {
        if (IsGlobalOrStaticOrConstVar(*decl)) {
            CollectForVar(*decl);
            if (auto vd = DynamicCast<VarDecl*>(decl.get()); vd && !vd->initializer) {
                uninitStaticVars.emplace(decl.get());
            }
        } else if (auto fd = DynamicCast<FuncDecl*>(decl.get()); fd && IsStaticInitializer(*fd)) {
            staticInit = fd;
        }
    }
    if (staticInit != nullptr) {
        // Static constructor is used to initialize static member variables,
        // we should collect usage inside 'static init' after all static member has been collected.
        CollectForStaticInit(*staticInit, uninitStaticVars);
    }
}

template <ASTKind Kind>
std::vector<Ptr<VarDecl>> GlobalVarChecker::GetDefaultInitVariablesInStruct(Decl& structDecl) const
{
    std::vector<Ptr<VarDecl>> result;
    auto outerDecl = StaticAs<Kind>(&structDecl);
    CJC_NULLPTR_CHECK(outerDecl->body);
    for (auto& decl : outerDecl->body->decls) {
        if (decl && decl->astKind == ASTKind::VAR_DECL && !decl->TestAttr(Attribute::STATIC)) {
            result.push_back(StaticAs<ASTKind::VAR_DECL>(decl.get()));
        }
    }
    return result;
}

std::vector<Ptr<VarDecl>> GlobalVarChecker::GetDefaultInitVariables(Decl& outerDecl) const
{
    if (outerDecl.astKind == ASTKind::CLASS_DECL) {
        return GetDefaultInitVariablesInStruct<ASTKind::CLASS_DECL>(outerDecl);
    } else if (outerDecl.astKind == ASTKind::STRUCT_DECL) {
        return GetDefaultInitVariablesInStruct<ASTKind::STRUCT_DECL>(outerDecl);
    }
    return std::vector<Ptr<VarDecl>>();
}

void GlobalVarChecker::CollectVarUsageBFS(const Node& node)
{
    std::queue<Ptr<const Node>> worklist;
    std::set<Ptr<const Node>> visited;
    worklist.push(&node);
    while (!worklist.empty()) {
        auto currNode = worklist.front();
        worklist.pop();
        if (visited.count(currNode) > 0) {
            continue;
        }
        visited.insert(currNode);
        CollectVarUsageBFSImpl(*currNode, worklist);
    }
}

void GlobalVarChecker::CollectVarUsageBFSImpl(const Node& node, std::queue<Ptr<const Node>>& worklist)
{
    std::function<VisitAction(Ptr<const Node>)> visitor = [this, &worklist, &visitor](Ptr<const Node> n) {
        if (auto expr = DynamicCast<Expr*>(n); expr && expr->desugarExpr) {
            Walker(expr->desugarExpr.get(), visitor).Walk();
            return VisitAction::SKIP_CHILDREN;
        }
        switch (n->astKind) {
            case ASTKind::FUNC_DECL: {
                const auto& fd = StaticCast<const FuncDecl&>(*n);
                if (CanBeIgnored(fd)) {
                    return VisitAction::SKIP_CHILDREN;
                } else if (!fd.outerDecl || !IsInstanceConstructor(fd)) {
                    return VisitAction::WALK_CHILDREN;
                }
                // If the function is a constructor, we add member variables with
                // default initializations into the worklist because
                // initializers will not be inlined into the constructor (in the AST).
                // For the example code: `var g = 3; def-class A { let a = g; init(){} } `
                // When the function is `init`, we should add `a = g` into the worklist as well.
                for (auto varDecl : GetDefaultInitVariables(*fd.outerDecl)) {
                    worklist.push(varDecl);
                }
                return VisitAction::WALK_CHILDREN;
            }
            // Since a lambda expression may capture a global/static variable but not called immediately,
            // there is no need to analyze the lambda expr.
            // E.g., if a lambda expr is assigned: `a = { global_x }`,
            // we should analyze it only when `a()` where `a` ref to the lambda expr.
            // However, this pattern can only be guranteed for VarDecl and AssignExpr.
            // If a lambda is passed as an argument, it's hard to analyze whether it is called or not,
            // so we treat lambda exprs (as arguments) called conservatively.
            case ASTKind::VAR_DECL: {
                const auto& varDecl = StaticCast<const VarDecl&>(*n);
                if (varDecl.initializer && varDecl.initializer->astKind != ASTKind::LAMBDA_EXPR) {
                    worklist.push(varDecl.initializer.get());
                }
                return VisitAction::SKIP_CHILDREN;
            }
            case ASTKind::VAR_WITH_PATTERN_DECL: {
                const auto& vpd = StaticCast<const VarWithPatternDecl&>(*n);
                if (vpd.initializer) {
                    worklist.emplace(vpd.initializer.get());
                }
                return VisitAction::SKIP_CHILDREN;
            }
            case ASTKind::ASSIGN_EXPR: {
                const auto& ae = StaticCast<const AssignExpr&>(*n);
                if (ae.rightExpr && ae.rightExpr->astKind != ASTKind::LAMBDA_EXPR) {
                    worklist.push(ae.rightExpr.get());
                }
                // Left value of assignExpr may be property which should be collected.
                if (auto target = ae.leftValue ? GetTargetIfShouldCollect(*ae.leftValue) : nullptr) {
                    worklist.push(target);
                }
                return VisitAction::SKIP_CHILDREN;
            }
            case ASTKind::MEMBER_ACCESS: {
                const auto& ma = StaticCast<const MemberAccess&>(*n);
                if (auto target = GetTargetIfShouldCollect(ma)) {
                    worklist.push(target);
                }
                CollectInMemberAccessExpr(ma);
                return VisitAction::WALK_CHILDREN;
            }
            case ASTKind::REF_EXPR: {
                const auto& re = StaticCast<const RefExpr&>(*n);
                CollectInRefExpr(re);
                // If referring a function or property, collect recursively.
                if (auto target = GetTargetIfShouldCollect(re)) {
                    worklist.push(target);
                }
                return VisitAction::SKIP_CHILDREN;
            }
            default:
                return VisitAction::WALK_CHILDREN;
        }
    };
    ConstWalker walker(&node, visitor);
    walker.Walk();
}

void GlobalVarChecker::CollectInRefExpr(const RefExpr& re)
{
    if (re.ref.target == nullptr || re.ref.target->astKind == ASTKind::GENERIC_PARAM_DECL || re.isThis ||
        re.ref.target->TestAnyAttr(Attribute::IMPORTED, Attribute::FOREIGN, Attribute::ENUM_CONSTRUCTOR)) {
        return;
    }
    // If referring a global or static variable, add it in the graph.
    if (IsRefGlobalOrStaticVar(re)) {
        auto targetVarDecl = StaticAs<ASTKind::VAR_DECL>(re.ref.target);
        CJC_ASSERT(targetVarDecl);
        graph.AddEdge(currentNode->var, *targetVarDecl, re);
    }
    return;
}

void GlobalVarChecker::CollectInMemberAccessExpr(const MemberAccess& ma)
{
    if (!ma.target) {
        return;
    }
    // If referring a global or static variable via a member access,
    // e.g., SomeClass.some_static_variable,
    // add it in the graph.
    if (IsGlobalOrStaticOrConstVar(*ma.target)) {
        auto varDecl = StaticAs<ASTKind::VAR_DECL>(ma.target);
        CJC_ASSERT(varDecl);
        graph.AddEdge(currentNode->var, *varDecl, ma);
    }
    return;
}

/**
 * @Return value:
 *   true -> Checking success
 *   false -> Checking failure and detecting issues
 */
bool GlobalVarChecker::CheckInSameFile() const
{
    bool result = true;
    for (auto& it : graph.nodes) {
        auto& defNode = it.second;
        if (!CheckVarUsageForDefNode(*defNode)) {
            result = false;
        }
    }
    return result;
}

/**
 * @Return value:
 *   true -> Checking success
 *   false -> The variable wrapped in the `defNode` uses an uninitialized variable,
 *            and these two variables are in the same file.
 */
bool GlobalVarChecker::CheckVarUsageForDefNode(const DefNode& defNode) const
{
    for (auto usage : defNode.usage) {
        auto& usedNode = usage.node;
        if (!IsInSameFile(defNode.var, usedNode.var)) {
            continue;
        }
        if (usedNode.visitOrder >= defNode.visitOrder) {
            std::string identifierPrefix = (defNode.var.astKind == ASTKind::FUNC_DECL && defNode.var.outerDecl)
                ? (defNode.var.outerDecl->identifier.Val() + ".")
                : "";
            std::string identifier = (defNode.var.astKind != ASTKind::VAR_WITH_PATTERN_DECL)
                ? (identifierPrefix + defNode.var.identifier.Val())
                : usage.refNode.GetTarget()->identifier.Val();
            diag.Diagnose(usage.refNode, usage.refPos, DiagKind::sema_global_var_used_before_initialization,
                usage.refName, identifier);
            return false;
        }
    }
    return true;
}

/**
 * Though we can reorder the initialization order of global variables from different files,
 * we must obey that variables in a same file are initialized in their definition order.
 * Thus, we add a fake def-use edge between variables in the same file.
 * ```
 * let g = bar()
 * let h = baz()
 * let u = baz()
 * ```
 * We add fake def-use edges: h <- g, u <- g, u <- h;
 * thus, we ensure that `g` will be initialized first; then, `h` and `u` are initialized.
 */
void GlobalVarChecker::CheckCrossFile(const Package& package)
{
    for (auto& file : package.files) {
        if (file == nullptr) {
            continue;
        }
        AddInitializationOrderEdge(*file);
    }
    CheckByToposort();
}

void GlobalVarChecker::AddInitializationOrderEdge(const File& file)
{
    Ptr<VarDecl> prevDecl = nullptr;
    for (auto& decl : file.decls) {
        if (decl && decl->astKind == ASTKind::VAR_DECL) {
            auto* varDecl = StaticAs<ASTKind::VAR_DECL>(decl.get());
            if (prevDecl) {
                // A fake def-use edge: current variable uses previous defined variable.
                graph.AddEdge(*varDecl, *prevDecl);
            }
            prevDecl = varDecl;
        }
    }
}

void GlobalVarChecker::CheckByToposort()
{
    for (auto& it : graph.nodes) {
        auto& defNode = it.second;
        if (defNode->color == Color::WHITE) {
            // If a cycle is detected, early return.
            if (!ToposortDFS(*defNode)) {
                return;
            }
        }
    }
}

/**
 * @Return value:
 *   true -> toposort success
 *   false -> a cycle detected
 */
bool GlobalVarChecker::ToposortDFS(DefNode& node)
{
    if (node.color == Color::BLACK) {
        return true;
    }
    // Detect a cycle
    if (node.color == Color::GRAY) {
        return false;
    }
    node.color = Color::GRAY;
    for (auto& usage : node.usage) {
        if (!ToposortDFS(usage.node)) {
            if (usage.node.color == Color::GRAY) {
                diag.Diagnose(usage.refNode, usage.refPos, DiagKind::sema_used_before_initialization, usage.refName);
            }
            // Also set black for early quit. Used to distinguish the situation from detecting cycle.
            node.color = Color::BLACK;
            return false;
        }
    }
    node.color = Color::BLACK;
    return true;
}

void GlobalVarChecker::DoCheck(const Package& package)
{
    bool result = CheckInSameFile();
    // If checking in the same file fails, there is no need to check cross files.
    if (!result) {
        return;
    }
    CheckCrossFile(package);
}
} // namespace

void TypeChecker::TypeCheckerImpl::CheckGlobalVarInitialization([[maybe_unused]]ASTContext& ctx, const Package& package)
{
    GlobalVarChecker checker(diag);
    checker.DoCollect(package);
    checker.DoCheck(package);
}
} // namespace Cangjie
