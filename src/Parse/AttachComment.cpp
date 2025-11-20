// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the attachment of comments to nodes.
 *
 * Line Header Comment and the comment on the same line or the next line form a comment group. Other
 * comment that are connected to the comment of multiple peers form an annotation group.
 *
 * The basic principle is to associate the nearest outermost node. The detailed rules are as follows:
 * For comment group cg, node n:
 * Rule 1: If cg comes after n and is connected to n in the same line, or is immediately followed by a non-comment,
 * non-whitespace character with at least one blank line in between, cg is called the trailing comment of n, where n is
 * the outermost node that satisfies the rule.
 * Rule 2: If Rule 1 is not satisfied, cg is located within the innermost node n, and the first outermost
 * node following n is found. cg is called the leading comment of n. If no such node is found, the first outermost node
 * preceding n on the same level is found, and cg is called the trailing comment of n. If neither can be found, cg is
 * called the internal comment of ni.
 */

#include "ParserImpl.h"

#include <stack>
#include <functional>
#include <algorithm>
#include "cangjie/AST/Walker.h"
#include "cangjie/AST/ASTCasting.h"

using namespace Cangjie;
using namespace Cangjie::AST;

namespace {
/// Either a Cangjie Node, or position of a non-node token (e.g. identifier of Decl)
/// This type is fairly small, so it is passed by value rather than by ref.
using CNode = std::variant<Node*, const Position*>;
static_assert(sizeof(CNode) <= sizeof(void*) * 2, "CNode should be small enough to be passed by value");

const Position Begin(CNode node)
{
    if (auto n = std::get_if<Node*>(&node)) {
        return (*n)->begin;
    } else {
        return *std::get<const Position*>(node);
    }
}

const Position End(CNode node)
{
    if (auto n = std::get_if<Node*>(&node)) {
        return (*n)->end;
    } else {
        return *std::get<const Position*>(node);
    }
}

#ifndef NDEBUG
std::ostream& operator<<(std::ostream& out, CNode node)
{
    if (auto n = std::get_if<Node*>(&node)) {
        out << "Node<" << (void*)*n << ", " << ASTKIND_TO_STR.at((*n)->astKind) <<
            ">[" << (*n)->begin << ", " << (*n)->end << "]";
    } else {
        out << "Pos[" << std::get<const Position*>(node)->line << ", "
            << std::get<const Position*>(node)->column << "]";
    }
    return out;
}

std::string CNodeToString(CNode node) __attribute__((used));
std::string CNodeToString(CNode node)
{
    std::stringstream ss;
    ss << node;
    return ss.str();
}

bool IsPositionContained(const Position& pos, Node* node)
{
    if (!node || pos.IsZero()) {
        return false;
    }
    return node->begin <= pos && pos <= node->end;
}

bool IsNodeContained(Node* inner, Node* outer)
{
    if (!inner || !outer || inner == outer) {
        return false;
    }
    return outer->begin <= inner->begin && inner->end <= outer->end;
}

std::string CNodeToString(const std::vector<CNode>& nodes) __attribute__((used));
std::string CNodeToString(const std::vector<CNode>& nodes)
{
    if (nodes.empty()) {
        return "[]";
    }
    
    std::vector<Node*> nodeList;
    std::vector<const Position*> positionList;
    
    for (const auto& node : nodes) {
        if (auto n = std::get_if<Node*>(&node)) {
            if (*n) {
                nodeList.push_back(*n);
            }
        } else if (auto p = std::get_if<const Position*>(&node)) {
            if (*p && !(*p)->IsZero()) {
                positionList.push_back(*p);
            }
        }
    }
    
    struct TreeNode {
        Node* node;
        std::vector<TreeNode*> children;
        std::vector<const Position*> tokens;
    };
    
    std::vector<TreeNode> treeNodes;
    treeNodes.reserve(nodeList.size());
    for (auto* n : nodeList) {
        treeNodes.push_back({n, {}, {}});
    }
    
    std::vector<TreeNode*> roots;
    for (size_t i = 0; i < treeNodes.size(); ++i) {
        TreeNode* bestParent = nullptr;
        for (size_t j = 0; j < treeNodes.size(); ++j) {
            if (i != j && IsNodeContained(treeNodes[i].node, treeNodes[j].node)) {
                if (!bestParent || IsNodeContained(treeNodes[j].node, bestParent->node)) {
                    bestParent = &treeNodes[j];
                }
            }
        }
        if (bestParent) {
            bestParent->children.push_back(&treeNodes[i]);
        } else {
            roots.push_back(&treeNodes[i]);
        }
    }
    
    for (const Position* pos : positionList) {
        TreeNode* bestNode = nullptr;
        for (auto& tn : treeNodes) {
            if (IsPositionContained(*pos, tn.node)) {
                if (!bestNode || IsNodeContained(tn.node, bestNode->node)) {
                    bestNode = &tn;
                }
            }
        }
        if (bestNode) {
            bestNode->tokens.push_back(pos);
        }
    }
    
    std::stringstream ss;
    ss << "[\n";
    std::function<void(const TreeNode*, int)> printNode = [&](const TreeNode* tn, int indent) {
        std::string indentStr(indent * 2, ' ');
        ss << indentStr;
        if (auto n = tn->node) {
            ss << "Node<" << (void*)n << ", " << ASTKIND_TO_STR.at(n->astKind)
               << ">[" << n->begin << ", " << n->end << "]";
        }
        if (!tn->tokens.empty()) {
            ss << " {\n";
            for (const Position* pos : tn->tokens) {
                ss << indentStr << "  Pos[" << pos->line << ", " << pos->column << "]\n";
            }
            ss << indentStr << "}";
        }
        ss << "\n";
        for (auto child : tn->children) {
            printNode(child, indent + 1);
        }
    };
    
    std::vector<const Position*> orphanedPositions;
    for (auto pos : positionList) {
        bool found = false;
        for (auto& tn : treeNodes) {
            if (IsPositionContained(*pos, tn.node)) {
                found = true;
                break;
            }
        }
        if (!found) {
            orphanedPositions.push_back(pos);
        }
    }
    
    for (TreeNode* root : roots) {
        printNode(root, 1);
    }
    
    if (!orphanedPositions.empty()) {
        ss << "  [ORPHANED POSITIONS - BUG IN COMPILER]\n";
        for (const Position* pos : orphanedPositions) {
            ss << "    Pos[" << pos->line << ", " << pos->column << "]\n";
        }
    }
    ss << "]";
    return ss.str();
}
#endif

struct CNodeWalker {
    std::function<void(CNode)> enterFunc;
    std::function<void(CNode)> exitFunc;

    void Visit(CNode node)
    {
        if (auto n = std::get_if<Node*>(&node)) {
            if (!*n || (*n)->TestAnyAttr(Attribute::HAS_BROKEN, Attribute::IS_BROKEN)) {
                return;
            }
        }
        if (enterFunc) {
            enterFunc(node);
        }
        if (auto n = std::get_if<Node*>(&node)) {
            VisitChildren(*n);
        }
        if (exitFunc) {
            exitFunc(node);
        }
    }

private:
    void VisitModifiers(const std::set<Modifier>& modifiers)
    {
        for (auto& mod : modifiers) {
            Visit(const_cast<Modifier*>(&mod));
        }
    }

    void VisitToken(const Position& pos)
    {
        if (pos.IsZero()) {
            return;
        }
        Visit(&pos);
    }

    void VisitAnnotations(const std::vector<OwnedPtr<Annotation>>& annotations)
    {
        for (auto& ann : annotations) {
            Visit(ann.get());
        }
    }

    void VisitIdentifier(const Identifier& id)
    {
        VisitToken(id.Begin());
    }

    template <class N>
    void VisitChild(OwnedPtr<N>& node)
    {
        static_assert(std::is_base_of<Node, N>::value, "N must be a subclass of Node");
        Visit(node.get());
    }

    void VisitEnumCtors(EnumDecl& e)
    {
        size_t c{0};
        size_t b{0};
        if (e.hasEllipsis) {
            // enum { | A | B | ... }
            if (e.bitOrPosVector.size() > e.constructors.size()) {
                VisitToken(e.bitOrPosVector[b++]);
            }
        } else {
            // enum { | A | B }
            if (e.bitOrPosVector.size() == e.constructors.size()) {
                VisitToken(e.bitOrPosVector[b++]);
            }
        }
        for (; c < e.constructors.size(); ++c) {
            VisitChild(e.constructors[c]);
            if (b < e.bitOrPosVector.size()) {
                VisitToken(e.bitOrPosVector[b++]);
            }
        }
        if (e.hasEllipsis) {
            VisitToken(e.ellipsisPos);
        }
    }

    void VisitChildren(Node* node)
    {
        switch (node->astKind) {
            case ASTKind::VAR_DECL: {
                auto v = StaticCast<VarDecl>(node);
                VisitAnnotations(v->annotations);
                VisitModifiers(v->modifiers);
                VisitToken(v->keywordPos);
                VisitIdentifier(v->identifier);
                if (v->type) {
                    VisitToken(v->colonPos);
                    VisitChild(v->type);
                }
                if (v->initializer) {
                    VisitToken(v->assignPos);
                    VisitChild(v->initializer);
                }
                break;
            }
            case ASTKind::FUNC_DECL: {
                auto f = StaticCast<FuncDecl>(node);
                VisitAnnotations(f->annotations);
                VisitModifiers(f->modifiers);
                VisitToken(f->keywordPos);
                VisitIdentifier(f->identifier);
                VisitChild(f->funcBody);
                break;
            }
            case ASTKind::PROP_DECL: {
                auto p = StaticCast<PropDecl>(node);
                VisitAnnotations(p->annotations);
                VisitModifiers(p->modifiers);
                VisitToken(p->keywordPos);
                VisitIdentifier(p->identifier);
                if (p->type) {
                    VisitToken(p->colonPos);
                    VisitChild(p->type);
                }
                // Body braces and accessors
                VisitToken(p->leftCurlPos);
                for (auto& g : p->getters) {
                    VisitChild(g);
                }
                for (auto& s : p->setters) {
                    VisitChild(s);
                }
                VisitToken(p->rightCurlPos);
                break;
            }
            case ASTKind::INTERFACE_DECL: {
                auto c = StaticCast<InterfaceDecl>(node);
                VisitAnnotations(c->annotations);
                VisitModifiers(c->modifiers);
                VisitToken(c->keywordPos);
                VisitIdentifier(c->identifier);
                VisitGenericParams(c->generic);
                for (auto& it : c->inheritedTypes) {
                    VisitChild(it);
                    VisitToken(it->bitAndPos);
                }
                VisitGenericConstraints(c->generic);
                if (c->body) {
                    VisitChild(c->body);
                }
                break;
            }
            case ASTKind::CLASS_BODY: {
                auto b = StaticCast<ClassBody>(node);
                VisitToken(b->leftCurlPos);
                for (auto& d : b->decls) {
                    VisitChild(d);
                }
                VisitToken(b->rightCurlPos);
                break;
            }
            case ASTKind::FUNC_PARAM_LIST: {
                auto pl = StaticCast<FuncParamList>(node);
                VisitToken(pl->leftParenPos);
                for (auto& p : pl->params) {
                    VisitChild(p);
                    VisitToken(p->commaPos);
                }
                VisitToken(pl->rightParenPos);
                break;
            }
            case ASTKind::FUNC_BODY: {
                auto fb = StaticCast<FuncBody>(node);
                VisitGenericParams(fb->generic);
                for (auto& l : fb->paramLists) {
                    VisitChild(l);
                }
                VisitToken(fb->colonPos);
                VisitChild(fb->retType);
                VisitGenericConstraints(fb->generic);
                VisitChild(fb->body);
                break;
            }
            case ASTKind::BLOCK: {
                auto b = StaticCast<Block>(node);
                VisitToken(b->unsafePos);
                VisitToken(b->leftCurlPos);
                for (auto& n : b->body) {
                    VisitChild(n);
                }
                VisitToken(b->rightCurlPos);
                break;
            }
            case ASTKind::STRUCT_BODY: {
                auto b = StaticCast<StructBody>(node);
                VisitToken(b->leftCurlPos);
                for (auto& d : b->decls) {
                    VisitChild(d);
                }
                VisitToken(b->rightCurlPos);
                break;
            }
            case ASTKind::INTERFACE_BODY: {
                auto b = StaticCast<InterfaceBody>(node);
                VisitToken(b->leftCurlPos);
                for (auto& d : b->decls) {
                    VisitChild(d);
                }
                VisitToken(b->rightCurlPos);
                break;
            }
            case ASTKind::ENUM_DECL: {
                auto e = StaticCast<EnumDecl>(node);
                VisitAnnotations(e->annotations);
                VisitModifiers(e->modifiers);
                VisitToken(e->keywordPos);
                VisitIdentifier(e->identifier);
                VisitGenericParams(e->generic);
                for (auto& it : e->inheritedTypes) {
                    VisitChild(it);
                    VisitToken(it->bitAndPos);
                }
                VisitGenericConstraints(e->generic);
                VisitToken(e->leftCurlPos);
                VisitEnumCtors(*e);
                for (auto& m : e->members) {
                    VisitChild(m);
                }
                VisitToken(e->rightCurlPos);
                break;
            }
            case ASTKind::EXTEND_DECL: {
                auto ex = StaticCast<ExtendDecl>(node);
                VisitAnnotations(ex->annotations);
                VisitModifiers(ex->modifiers);
                VisitToken(ex->keywordPos);
                VisitIdentifier(ex->identifier);
                VisitGenericParams(ex->generic);
                VisitToken(ex->wherePos);
                VisitChild(ex->extendedType);
                VisitGenericConstraints(ex->generic);
                VisitToken(ex->leftCurlPos);
                for (auto& m : ex->members) {
                    VisitChild(m);
                }
                VisitToken(ex->rightCurlPos);
                break;
            }
            case ASTKind::VAR_WITH_PATTERN_DECL: {
                auto vp = StaticCast<VarWithPatternDecl>(node);
                VisitAnnotations(vp->annotations);
                VisitModifiers(vp->modifiers);
                VisitToken(vp->keywordPos);
                VisitIdentifier(vp->identifier);
                if (vp->irrefutablePattern) {
                    VisitChild(vp->irrefutablePattern);
                }
                if (vp->initializer) {
                    VisitToken(vp->assignPos);
                    VisitChild(vp->initializer);
                }
                break;
            }
            case ASTKind::MACRO_EXPAND_PARAM: {
                auto mp = StaticCast<MacroExpandParam>(node);
                VisitAnnotations(mp->annotations);
                VisitInvocation(mp->invocation);
                // Reuse FuncParam visiting
                VisitModifiers(mp->modifiers);
                VisitToken(mp->keywordPos);
                VisitIdentifier(mp->identifier);
                VisitToken(mp->notMarkPos);
                VisitToken(mp->colonPos);
                VisitChild(mp->type);
                VisitChild(mp->assignment);
                break;
            }
            case ASTKind::MACRO_DECL: {
                auto m = StaticCast<MacroDecl>(node);
                VisitAnnotations(m->annotations);
                VisitModifiers(m->modifiers);
                VisitToken(m->keywordPos);
                VisitIdentifier(m->identifier);
                VisitChild(m->funcBody);
                break;
            }
            case ASTKind::MAIN_DECL: {
                auto md = StaticCast<MainDecl>(node);
                VisitAnnotations(md->annotations);
                VisitModifiers(md->modifiers);
                VisitToken(md->keywordPos);
                VisitIdentifier(md->identifier);
                VisitChild(md->funcBody);
                break;
            }
            case ASTKind::PRIMARY_CTOR_DECL: {
                auto pc = StaticCast<PrimaryCtorDecl>(node);
                VisitAnnotations(pc->annotations);
                VisitModifiers(pc->modifiers);
                VisitToken(pc->keywordPos);
                VisitIdentifier(pc->identifier);
                VisitChild(pc->funcBody);
                break;
            }

            // ========================= Expressions =========================
            case ASTKind::IF_EXPR: {
                auto e = StaticCast<IfExpr>(node);
                VisitToken(e->ifPos);
                VisitToken(e->leftParenPos);
                VisitChild(e->condExpr);
                VisitToken(e->rightParenPos);
                VisitChild(e->thenBody);
                if (e->hasElse) {
                    VisitToken(e->elsePos);
                    VisitChild(e->elseBody);
                }
                break;
            }
            case ASTKind::LET_PATTERN_DESTRUCTOR: {
                auto e = StaticCast<LetPatternDestructor>(node);
                for (size_t i{0}; i < e->patterns.size(); ++i) {
                    VisitChild(e->patterns[i]);
                    if (i < e->orPos.size()) {
                        VisitToken(e->orPos[i]);
                    }
                }
                VisitToken(e->backarrowPos);
                VisitChild(e->initializer);
                break;
            }
            case ASTKind::MATCH_CASE: {
                auto mc = StaticCast<MatchCase>(node);
                for (size_t i{0}; i < mc->patterns.size(); ++i) {
                    VisitChild(mc->patterns[i]);
                    if (i < mc->bitOrPosVector.size()) {
                        VisitToken(mc->bitOrPosVector[i]);
                    }
                }
                VisitToken(mc->wherePos);
                VisitChild(mc->patternGuard);
                VisitToken(mc->arrowPos);
                VisitChild(mc->exprOrDecls);
                break;
            }
            case ASTKind::MATCH_CASE_OTHER: {
                auto mco = StaticCast<MatchCaseOther>(node);
                VisitChild(mco->matchExpr);
                VisitToken(mco->arrowPos);
                VisitChild(mco->exprOrDecls);
                break;
            }
            case ASTKind::MATCH_EXPR: {
                auto me = StaticCast<MatchExpr>(node);
                VisitToken(me->leftParenPos);
                VisitChild(me->selector);
                VisitToken(me->rightParenPos);
                VisitToken(me->leftCurlPos);
                for (auto& c : me->matchCases) {
                    VisitChild(c);
                }
                for (auto& c : me->matchCaseOthers) {
                    VisitChild(c);
                }
                VisitToken(me->rightCurlPos);
                break;
            }
            case ASTKind::TRY_EXPR: {
                auto te = StaticCast<TryExpr>(node);
                VisitToken(te->tryPos);
                VisitToken(te->lParen);
                for (size_t i = 0; i < te->resourceSpec.size(); ++i) {
                    VisitChild(te->resourceSpec[i]);
                    if (i < te->resourceSpecCommaPos.size()) {
                        VisitToken(te->resourceSpecCommaPos[i]);
                    }
                }
                VisitToken(te->rParen);
                VisitChild(te->tryBlock);
                for (size_t i{0}; i < te->catchPosVector.size(); ++i) {
                    VisitToken(te->catchPosVector[i]);
                    VisitToken(te->catchLParenPosVector[i]);
                    VisitChild(te->catchPatterns[i]);
                    VisitToken(te->catchRParenPosVector[i]);
                    VisitChild(te->catchBlocks[i]);
                }
                VisitToken(te->finallyPos);
                VisitChild(te->finallyBlock);
                for (auto& h : te->handlers) {
                    VisitToken(h.pos);
                    VisitChild(h.commandPattern);
                    VisitChild(h.block);
                }
                break;
            }
            case ASTKind::THROW_EXPR: {
                auto e = StaticCast<ThrowExpr>(node);
                VisitToken(e->throwPos);
                VisitChild(e->expr);
                break;
            }
            case ASTKind::PERFORM_EXPR: {
                auto e = StaticCast<PerformExpr>(node);
                VisitToken(e->performPos);
                VisitChild(e->expr);
                break;
            }
            case ASTKind::RESUME_EXPR: {
                auto e = StaticCast<ResumeExpr>(node);
                VisitToken(e->resumePos);
                VisitToken(e->withPos);
                VisitChild(e->withExpr);
                VisitToken(e->throwingPos);
                VisitChild(e->throwingExpr);
                break;
            }
            case ASTKind::RETURN_EXPR: {
                auto e = StaticCast<ReturnExpr>(node);
                VisitToken(e->returnPos);
                VisitChild(e->expr);
                break;
            }
            case ASTKind::FOR_IN_EXPR: {
                auto e = StaticCast<ForInExpr>(node);
                VisitToken(e->leftParenPos);
                VisitChild(e->pattern);
                VisitToken(e->inPos);
                VisitChild(e->inExpression);
                VisitToken(e->wherePos);
                VisitChild(e->patternGuard);
                VisitToken(e->rightParenPos);
                VisitChild(e->body);
                break;
            }
            case ASTKind::WHILE_EXPR: {
                auto e = StaticCast<WhileExpr>(node);
                VisitToken(e->whilePos);
                VisitToken(e->leftParenPos);
                VisitChild(e->condExpr);
                VisitToken(e->rightParenPos);
                VisitChild(e->body);
                break;
            }
            case ASTKind::DO_WHILE_EXPR: {
                auto e = StaticCast<DoWhileExpr>(node);
                VisitToken(e->doPos);
                VisitChild(e->body);
                VisitToken(e->whilePos);
                VisitToken(e->leftParenPos);
                VisitChild(e->condExpr);
                VisitToken(e->rightParenPos);
                break;
            }
            case ASTKind::ASSIGN_EXPR: {
                auto e = StaticCast<AssignExpr>(node);
                VisitChild(e->leftValue);
                VisitToken(e->assignPos);
                VisitChild(e->rightExpr);
                break;
            }
            case ASTKind::INC_OR_DEC_EXPR: {
                auto e = StaticCast<IncOrDecExpr>(node);
                VisitChild(e->expr);
                VisitToken(e->operatorPos);
                break;
            }
            case ASTKind::UNARY_EXPR: {
                auto e = StaticCast<UnaryExpr>(node);
                VisitToken(e->operatorPos);
                VisitChild(e->expr);
                break;
            }
            case ASTKind::BINARY_EXPR: {
                auto e = StaticCast<BinaryExpr>(node);
                VisitChild(e->leftExpr);
                VisitToken(e->operatorPos);
                VisitChild(e->rightExpr);
                break;
            }
            case ASTKind::IS_EXPR: {
                auto e = StaticCast<IsExpr>(node);
                VisitChild(e->leftExpr);
                VisitToken(e->isPos);
                VisitChild(e->isType);
                break;
            }
            case ASTKind::AS_EXPR: {
                auto e = StaticCast<AsExpr>(node);
                VisitChild(e->leftExpr);
                VisitToken(e->asPos);
                VisitChild(e->asType);
                break;
            }
            case ASTKind::RANGE_EXPR: {
                auto e = StaticCast<RangeExpr>(node);
                VisitChild(e->startExpr);
                VisitToken(e->rangePos);
                VisitChild(e->stopExpr);
                VisitToken(e->colonPos);
                VisitChild(e->stepExpr);
                break;
            }
            case ASTKind::SUBSCRIPT_EXPR: {
                auto e = StaticCast<SubscriptExpr>(node);
                VisitChild(e->baseExpr);
                VisitToken(e->leftParenPos);
                for (size_t i = 0; i < e->indexExprs.size(); ++i) {
                    VisitChild(e->indexExprs[i]);
                    if (i < e->commaPos.size()) {
                        VisitToken(e->commaPos[i]);
                    }
                }
                VisitToken(e->rightParenPos);
                break;
            }
            case ASTKind::INTERPOLATION_EXPR: {
                auto e = StaticCast<InterpolationExpr>(node);
                VisitToken(e->dollarPos);
                VisitChild(e->block);
                break;
            }
            case ASTKind::LIT_CONST_EXPR: {
                auto e = StaticCast<LitConstExpr>(node);
                if (e->siExpr) {
                    VisitChild(e->siExpr);
                } else {
                    VisitToken(e->begin);
                }
                break;
            }
            case ASTKind::TOKEN_PART: {
                auto e = StaticCast<TokenPart>(node);
                for (auto& tk : e->tokens) {
                    VisitToken(tk.Begin());
                }
                break;
            }
            case ASTKind::QUOTE_EXPR: {
                auto e = StaticCast<QuoteExpr>(node);
                VisitToken(e->quotePos);
                VisitToken(e->leftParenPos);
                for (auto& ex : e->exprs) {
                    VisitChild(ex);
                }
                VisitToken(e->rightParenPos);
                break;
            }
            case ASTKind::STR_INTERPOLATION_EXPR: {
                auto e = StaticCast<StrInterpolationExpr>(node);
                for (auto& part : e->strPartExprs) {
                    VisitChild(part);
                }
                break;
            }
            case ASTKind::PAREN_EXPR: {
                auto e = StaticCast<ParenExpr>(node);
                VisitToken(e->leftParenPos);
                VisitChild(e->expr);
                VisitToken(e->rightParenPos);
                break;
            }
            case ASTKind::LAMBDA_EXPR: {
                auto e = StaticCast<LambdaExpr>(node);
                auto fb = &StaticCast<FuncBody>(*e->funcBody);
                VisitToken(fb->body->leftCurlPos);
                for (auto& l : fb->paramLists) {
                    VisitChild(l);
                }
                VisitToken(fb->doubleArrowPos);
                for (auto& ex : fb->body->body) {
                    VisitChild(ex);
                }
                VisitToken(fb->body->rightCurlPos);
                break;
            }
            case ASTKind::TRAIL_CLOSURE_EXPR: {
                auto e = StaticCast<TrailingClosureExpr>(node);
                VisitChild(e->lambda);
                VisitChild(e->expr);
                break;
            }
            case ASTKind::OPTIONAL_EXPR: {
                auto e = StaticCast<OptionalExpr>(node);
                VisitChild(e->baseExpr);
                VisitToken(e->questPos);
                break;
            }
            case ASTKind::OPTIONAL_CHAIN_EXPR: {
                auto e = StaticCast<OptionalChainExpr>(node);
                VisitChild(e->expr);
                break;
            }
            case ASTKind::ARRAY_LIT: {
                auto e = StaticCast<ArrayLit>(node);
                VisitToken(e->leftSquarePos);
                for (size_t i = 0; i < e->children.size(); ++i) {
                    VisitChild(e->children[i]);
                    if (i < e->commaPosVector.size()) {
                        VisitToken(e->commaPosVector[i]);
                    }
                }
                VisitToken(e->rightSquarePos);
                break;
            }
            case ASTKind::ARRAY_EXPR: {
                auto e = StaticCast<ArrayExpr>(node);
                VisitChild(e->type);
                VisitToken(e->leftParenPos);
                for (size_t i = 0; i < e->args.size(); ++i) {
                    VisitChild(e->args[i]);
                    if (i < e->commaPosVector.size()) {
                        VisitToken(e->commaPosVector[i]);
                    }
                }
                VisitToken(e->rightParenPos);
                break;
            }
            case ASTKind::POINTER_EXPR: {
                auto e = StaticCast<PointerExpr>(node);
                VisitChild(e->type);
                VisitChild(e->arg);
                break;
            }
            case ASTKind::TUPLE_LIT: {
                auto e = StaticCast<TupleLit>(node);
                VisitToken(e->leftParenPos);
                for (size_t i = 0; i < e->children.size(); ++i) {
                    VisitChild(e->children[i]);
                    if (i < e->commaPosVector.size()) {
                        VisitToken(e->commaPosVector[i]);
                    }
                }
                VisitToken(e->rightParenPos);
                break;
            }
            case ASTKind::TYPE_CONV_EXPR: {
                auto e = StaticCast<TypeConvExpr>(node);
                VisitChild(e->type);
                VisitToken(e->leftParenPos);
                VisitChild(e->expr);
                VisitToken(e->rightParenPos);
                break;
            }
            case ASTKind::REF_EXPR: {
                auto e = StaticCast<RefExpr>(node);
                // Type arguments on the reference name
                VisitToken(e->leftAnglePos);
                for (auto& ta : e->typeArguments) {
                    VisitChild(ta);
                }
                VisitToken(e->rightAnglePos);
                break;
            }
            case ASTKind::MEMBER_ACCESS: {
                auto e = StaticCast<MemberAccess>(node);
                VisitChild(e->baseExpr);
                VisitToken(e->dotPos);
                VisitIdentifier(e->field);
                // generic args on the member name
                VisitToken(e->leftAnglePos);
                for (auto& ta : e->typeArguments) {
                    VisitChild(ta);
                }
                VisitToken(e->rightAnglePos);
                break;
            }
            case ASTKind::CALL_EXPR: {
                auto e = StaticCast<CallExpr>(node);
                VisitChild(e->baseFunc);
                VisitToken(e->leftParenPos);
                for (auto& a : e->args) {
                    VisitChild(a);
                    VisitToken(a->commaPos);
                }
                VisitToken(e->rightParenPos);
                break;
            }
            case ASTKind::FUNC_ARG: {
                auto a = StaticCast<FuncArg>(node);
                // Named argument and colon
                VisitToken(a->name.Begin());
                VisitToken(a->colonPos);
                VisitToken(a->inoutPos);
                VisitChild(a->expr);
                break;
            }
            case ASTKind::SPAWN_EXPR: {
                auto e = StaticCast<SpawnExpr>(node);
                VisitToken(e->spawnPos);
                VisitToken(e->leftParenPos);
                VisitChild(e->arg);
                VisitToken(e->rightParenPos);
                VisitChild(e->task);
                break;
            }
            case ASTKind::SYNCHRONIZED_EXPR: {
                auto e = StaticCast<SynchronizedExpr>(node);
                VisitToken(e->syncPos);
                VisitToken(e->leftParenPos);
                VisitChild(e->mutex);
                VisitToken(e->rightParenPos);
                VisitChild(e->body);
                break;
            }

            // ========================= Patterns =========================
            case ASTKind::CONST_PATTERN: {
                auto p = StaticCast<ConstPattern>(node);
                VisitChild(p->literal);
                break;
            }
            case ASTKind::VAR_PATTERN: {
                auto p = StaticCast<VarPattern>(node);
                VisitChild(p->varDecl);
                break;
            }
            case ASTKind::TUPLE_PATTERN: {
                auto p = StaticCast<TuplePattern>(node);
                VisitToken(p->leftBracePos);
                for (size_t i = 0; i < p->patterns.size(); ++i) {
                    VisitChild(p->patterns[i]);
                    if (i < p->commaPosVector.size()) {
                        VisitToken(p->commaPosVector[i]);
                    }
                }
                VisitToken(p->rightBracePos);
                break;
            }
            case ASTKind::TYPE_PATTERN: {
                auto p = StaticCast<TypePattern>(node);
                VisitChild(p->pattern);
                VisitToken(p->colonPos);
                VisitChild(p->type);
                break;
            }
            case ASTKind::ENUM_PATTERN: {
                auto p = StaticCast<EnumPattern>(node);
                VisitChild(p->constructor);
                VisitToken(p->leftParenPos);
                for (size_t i = 0; i < p->patterns.size(); ++i) {
                    VisitChild(p->patterns[i]);
                    if (i < p->commaPosVector.size()) {
                        VisitToken(p->commaPosVector[i]);
                    }
                }
                VisitToken(p->rightParenPos);
                break;
            }
            case ASTKind::VAR_OR_ENUM_PATTERN: {
                auto p = StaticCast<VarOrEnumPattern>(node);
                VisitChild(p->pattern);
                break;
            }
            case ASTKind::EXCEPT_TYPE_PATTERN: {
                auto p = StaticCast<ExceptTypePattern>(node);
                VisitChild(p->pattern);
                VisitToken(p->colonPos);
                for (size_t i{0}; i < p->types.size(); ++i) {
                    VisitChild(p->types[i]);
                    if (i < p->bitOrPosVector.size()) {
                        VisitToken(p->bitOrPosVector[i]);
                    }
                }
                break;
            }
            case ASTKind::COMMAND_TYPE_PATTERN: {
                auto p = StaticCast<CommandTypePattern>(node);
                VisitChild(p->pattern);
                VisitToken(p->patternPos);
                VisitToken(p->colonPos);
                for (size_t i{0}; i < p->types.size(); ++i) {
                    VisitChild(p->types[i]);
                    if (i < p->bitOrPosVector.size()) {
                        VisitToken(p->bitOrPosVector[i]);
                    }
                }
                break;
            }

            // ========================= Types =========================
            case ASTKind::REF_TYPE: {
                auto t = StaticCast<RefType>(node);
                VisitToken(t->leftAnglePos);
                for (auto& ta : t->typeArguments) {
                    VisitChild(ta);
                }
                VisitToken(t->rightAnglePos);
                break;
            }
            case ASTKind::PAREN_TYPE: {
                auto t = StaticCast<ParenType>(node);
                VisitToken(t->leftParenPos);
                VisitChild(t->type);
                VisitToken(t->rightParenPos);
                break;
            }
            case ASTKind::QUALIFIED_TYPE: {
                auto t = StaticCast<QualifiedType>(node);
                VisitChild(t->baseType);
                VisitToken(t->dotPos);
                VisitToken(t->leftAnglePos);
                for (auto& ta : t->typeArguments) {
                    VisitChild(ta);
                }
                VisitToken(t->rightAnglePos);
                break;
            }
            case ASTKind::OPTION_TYPE: {
                auto t = StaticCast<OptionType>(node);
                VisitChild(t->componentType);
                for (auto& q : t->questVector) {
                    VisitToken(q);
                }
                break;
            }
            case ASTKind::CONSTANT_TYPE: {
                auto t = StaticCast<ConstantType>(node);
                VisitToken(t->dollarPos);
                VisitChild(t->constantExpr);
                break;
            }
            case ASTKind::VARRAY_TYPE: {
                auto t = StaticCast<VArrayType>(node);
                VisitToken(t->varrayPos);
                VisitToken(t->leftAnglePos);
                VisitChild(t->typeArgument);
                VisitToken(t->typeArgument->commaPos);
                VisitChild(t->constantType);
                VisitToken(t->rightAnglePos);
                break;
            }
            case ASTKind::FUNC_TYPE: {
                auto t = StaticCast<FuncType>(node);
                VisitToken(t->leftParenPos);
                for (auto& pt : t->paramTypes) {
                    VisitChild(pt);
                    VisitToken(pt->commaPos);
                }
                VisitToken(t->rightParenPos);
                VisitToken(t->arrowPos);
                VisitChild(t->retType);
                break;
            }
            case ASTKind::TUPLE_TYPE: {
                auto t = StaticCast<TupleType>(node);
                VisitToken(t->leftParenPos);
                for (size_t i = 0; i < t->fieldTypes.size(); ++i) {
                    VisitChild(t->fieldTypes[i]);
                    if (i < t->commaPosVector.size()) {
                        VisitToken(t->commaPosVector[i]);
                    }
                }
                VisitToken(t->rightParenPos);
                break;
            }

            // ========================= Macro Expand Expr/Decl =========================
            case ASTKind::MACRO_EXPAND_EXPR: {
                auto e = StaticCast<MacroExpandExpr>(node);
                VisitAnnotations(e->annotations);
                VisitInvocation(e->invocation);
                if (e->invocation.decl) {
                    VisitChild(e->invocation.decl);
                } else {
                    for (auto& n : e->invocation.nodes) {
                        VisitChild(n);
                    }
                }
                VisitToken(e->invocation.rightParenPos);
                break;
            }
            case ASTKind::MACRO_EXPAND_DECL: {
                auto d = StaticCast<MacroExpandDecl>(node);
                VisitAnnotations(d->annotations);
                VisitInvocation(d->invocation);
                VisitChild(d->invocation.decl);
                break;
            }
            case ASTKind::IMPORT_CONTENT: {
                auto ic = StaticCast<ImportContent>(node);
                for (auto& p : ic->prefixPoses) {
                    VisitToken(p);
                }
                for (auto& d : ic->prefixDotPoses) {
                    VisitToken(d);
                }
                // single/alias/all import identifiers
                VisitToken(ic->identifier.Begin());
                VisitToken(ic->asPos);
                VisitToken(ic->aliasName.Begin());
                VisitToken(ic->leftCurlPos);
                for (size_t i = 0; i < ic->items.size(); ++i) {
                    VisitToken(ic->items[i].begin);
                    if (i < ic->commaPoses.size()) {
                        VisitToken(ic->commaPoses[i]);
                    }
                }
                VisitToken(ic->rightCurlPos);
                break;
            }
            case ASTKind::IMPORT_SPEC: {
                auto is = StaticCast<ImportSpec>(node);
                VisitChild(is->modifier);
                VisitToken(is->importPos);
                VisitAnnotations(is->annotations);
                // content is by-value node
                Visit(&is->content);
                break;
            }
            case ASTKind::PACKAGE_SPEC: {
                auto ps = StaticCast<PackageSpec>(node);
                VisitChild(ps->modifier);
                VisitToken(ps->macroPos);
                VisitToken(ps->packagePos);
                for (size_t i{0}; i < ps->prefixPoses.size(); ++i) {
                    VisitToken(ps->prefixPoses[i]);
                    if (i < ps->prefixDotPoses.size()) {
                        VisitToken(ps->prefixDotPoses[i]);
                    }
                }
                VisitToken(ps->packageName.Begin());
                break;
            }
            case ASTKind::FEATURE_ID: {
                auto fid = StaticCast<FeatureId>(node);
                for (size_t i{0}; i < fid->identifiers.size(); ++i) {
                    VisitToken(fid->identifiers[i].Begin());
                    if (i < fid->dotPoses.size()) {
                        VisitToken(fid->dotPoses[i]);
                    }
                }
                break;
            }
            case ASTKind::FEATURES_DIRECTIVE: {
                auto fd = StaticCast<FeaturesDirective>(node);
                for (size_t i = 0; i < fd->content.size(); ++i) {
                    Visit(&fd->content[i]);
                    if (i < fd->commaPoses.size()) {
                        VisitToken(fd->commaPoses[i]);
                    }
                }
                break;
            }
            case ASTKind::GENERIC_CONSTRAINT: {
                auto gc = StaticCast<GenericConstraint>(node);
                VisitToken(gc->wherePos);
                VisitChild(gc->type);
                VisitToken(gc->operatorPos);
                for (size_t i = 0; i < gc->upperBounds.size(); ++i) {
                    VisitChild(gc->upperBounds[i]);
                    if (i < gc->bitAndPos.size()) {
                        VisitToken(gc->bitAndPos[i]);
                    }
                }
                break;
            }
            case ASTKind::FUNC_PARAM: {
                auto p = StaticCast<FuncParam>(node);
                VisitAnnotations(p->annotations);
                VisitModifiers(p->modifiers);
                VisitToken(p->keywordPos);
                VisitIdentifier(p->identifier);
                VisitToken(p->notMarkPos);
                VisitToken(p->colonPos);
                VisitChild(p->type);
                VisitChild(p->assignment);
                // visit comma outside
                break;
            }
            case ASTKind::TYPE_ALIAS_DECL: {
                auto t = StaticCast<TypeAliasDecl>(node);
                VisitAnnotations(t->annotations);
                VisitModifiers(t->modifiers);
                VisitToken(t->keywordPos);
                VisitIdentifier(t->identifier);
                VisitToken(t->assignPos);
                VisitChild(t->type);
                break;
            }
            case ASTKind::CLASS_DECL: {
                auto c = StaticCast<ClassDecl>(node);
                VisitAnnotations(c->annotations);
                VisitModifiers(c->modifiers);
                VisitToken(c->keywordPos);
                VisitIdentifier(c->identifier);
                VisitGenericParams(c->generic);
                for (auto& it : c->inheritedTypes) {
                    VisitChild(it);
                    VisitToken(it->bitAndPos);
                }
                VisitGenericConstraints(c->generic);
                VisitChild(c->body);
                break;
            }
            case ASTKind::STRUCT_DECL: {
                auto s = StaticCast<StructDecl>(node);
                VisitAnnotations(s->annotations);
                VisitModifiers(s->modifiers);
                VisitToken(s->keywordPos);
                VisitIdentifier(s->identifier);
                VisitGenericParams(s->generic);
                for (auto& it : s->inheritedTypes) {
                    VisitChild(it);
                    VisitToken(it->bitAndPos);
                }
                VisitGenericConstraints(s->generic);
                VisitChild(s->body);
                break;
            }
            case ASTKind::FILE: {
                auto f = StaticCast<File>(node);
                VisitChild(f->feature);
                VisitChild(f->package);
                for (auto& i: f->imports) {
                    VisitChild(i);
                }
                for (auto& d : f->decls) {
                    VisitChild(d);
                }
                break;
            }
            case ASTKind::ANNOTATION: {
                auto a = StaticCast<Annotation>(node);
                VisitToken(a->begin);
                VisitIdentifier(a->identifier);
                VisitToken(a->lsquarePos);
                for (size_t i{0}; i < a->attrs.size(); ++i) {
                    VisitToken(a->attrs[i].Begin());
                    if (i < a->attrCommas.size()) {
                        VisitToken(a->attrCommas[i]);
                    }
                }
                VisitChild(a->condExpr);
                VisitToken(a->rsquarePos);
                for (size_t i = 0; i < a->args.size(); ++i) {
                    VisitChild(a->args[i]);
                    VisitToken(a->args[i]->commaPos);
                }
                break;
            }
            case ASTKind::PRIMITIVE_TYPE_EXPR:
            case ASTKind::MODIFIER:
            case ASTKind::THIS_TYPE:
            case ASTKind::PRIMITIVE_TYPE:
            case ASTKind::JUMP_EXPR:
            case ASTKind::WILDCARD_PATTERN:
            case ASTKind::WILDCARD_EXPR:
                VisitToken(node->begin);
                break;
            case ASTKind::GENERIC:
                CJC_ASSERT(false &&
                    "generic cannot be visited in one, use VisitGenericParams and VisitGenericConstraints instead");
                break;
            case ASTKind::IF_AVAILABLE_EXPR:
            case ASTKind::DECL:
            case ASTKind::CLASS_LIKE_DECL:
            case ASTKind::BUILTIN_DECL:
            case ASTKind::GENERIC_PARAM_DECL:
            case ASTKind::PACKAGE_DECL:
            case ASTKind::INVALID_DECL:
            case ASTKind::PATTERN:
            case ASTKind::INVALID_PATTERN:
            case ASTKind::TYPE:
            case ASTKind::INVALID_TYPE:
            case ASTKind::EXPR:
            case ASTKind::INVALID_EXPR:
            case ASTKind::DUMMY_BODY:
            case ASTKind::PACKAGE:
            case ASTKind::NODE:
                break;
        }
    }

    void VisitGenericParams(OwnedPtr<Generic>& g)
    {
        if (!g) {
            return;
        }
        VisitToken(g->leftAnglePos);
        for (auto& tp : g->typeParameters) {
            VisitChild(tp);
        }
        VisitToken(g->rightAnglePos);
    }

    void VisitGenericConstraints(OwnedPtr<Generic>& g)
    {
        if (!g) {
            return;
        }
        for (auto& gc : g->genericConstraints) {
            VisitChild(gc);
            VisitToken(gc->commaPos);
        }
    }

    void VisitInvocation(const MacroInvocation& invocation)
    {
        VisitToken(invocation.atPos);
        VisitToken(invocation.macroNamePos);
        VisitToken(invocation.leftSquarePos);
        for (auto& tok : invocation.attrs) {
            VisitToken(tok.Begin());
        }
        VisitToken(invocation.rightSquarePos);
        VisitToken(invocation.leftParenPos);
        // ) to be visited after visiting invocation.nodes/decls
    }
};

// collect ptrs of ast nodes, ignore file, annotation and modifier
std::vector<CNode> CollectPtrsOfASTNodes(Ptr<File> node)
{
    std::vector<CNode> ptrs;
    auto collect = [&ptrs](CNode cnode) {
        bool collect{true};
        if (auto nod = std::get_if<Node*>(&cnode)) {
            if ((*nod)->astKind == ASTKind::FILE) {
                collect = false;
            }
        }
        if (collect) {
            ptrs.push_back(cnode);
        }
    };
    CNodeWalker w{collect, {}};
    w.Visit(node);
    return ptrs;
}

// Sort by begin position in ascending order. If the begins are the same, the node with a larger range is first.
void SortNodesByRange(std::vector<CNode>& nodes)
{
    auto cmpByRange = [](CNode a, CNode b) {
        return Begin(a) < Begin(b) || (Begin(a) == Begin(b) && End(a) > End(b));
    };
    std::sort(nodes.begin(), nodes.end(), cmpByRange);
}

void AppendCommentGroup(const Comment& comment, std::vector<CommentGroup>& cgs)
{
    CommentGroup cg{};
    cg.cms.push_back(comment);
    cgs.push_back(cg);
}

void AddCommentToBackGroup(const Comment& comment, std::vector<CommentGroup>& cgs)
{
    CJC_ASSERT(!cgs.empty());
    cgs.back().cms.push_back(comment);
}

void UpdateFollowInfoAndAppendCommentGroup(const std::optional<size_t>& preTkIdxIgnTrivialStuff, const Comment& comment,
    std::unordered_map<size_t, size_t>& cgPreInfo, std::vector<CommentGroup>& commentGroups)
{
    if (preTkIdxIgnTrivialStuff) {
        cgPreInfo[commentGroups.size()] = *preTkIdxIgnTrivialStuff;
    }
    AppendCommentGroup(comment, commentGroups);
}

CommentKind GetCommentKind(const Token& token)
{
    CJC_ASSERT(token.kind == TokenKind::COMMENT);
    if (token.Value().rfind("/*", 0) == std::string::npos) {
        return CommentKind::LINE;
    }
    if (token.Value().rfind("/**", 0) == std::string::npos) {
        return CommentKind::BLOCK;
    }
    if (token.Value().rfind("/***", 0) != std::string::npos) {
        return CommentKind::BLOCK;
    }
    if (token.Value().rfind("/**/", 0) != std::string::npos) {
        return CommentKind::BLOCK;
    }
    return CommentKind::DOCUMENT;
}

// if it appends a commentGroup return true
bool UpdateCommentGroups(const Token& tk, const Token& preTokenIgnoreNL,
    const std::optional<size_t>& preTkIdxIgnTrivialStuff, std::vector<CommentGroup>& commentGroups,
    std::unordered_map<size_t, size_t>& cgPreInfo)
{
    CommentKind commentKind = GetCommentKind(tk);
    int diffLine = tk.Begin().line - preTokenIgnoreNL.Begin().line;
    if (preTokenIgnoreNL.kind == TokenKind::COMMENT) {
        if (diffLine == 0) {
            AddCommentToBackGroup({CommentStyle::OTHER, commentKind, tk}, commentGroups);
        } else if (diffLine == 1) {
            if (commentGroups.back().cms.front().style == CommentStyle::LEAD_LINE) {
                AddCommentToBackGroup({CommentStyle::LEAD_LINE, commentKind, tk}, commentGroups);
            } else {
                UpdateFollowInfoAndAppendCommentGroup(
                    preTkIdxIgnTrivialStuff, {CommentStyle::LEAD_LINE, commentKind, tk}, cgPreInfo, commentGroups);
                return true;
            }
        } else {
            UpdateFollowInfoAndAppendCommentGroup(
                preTkIdxIgnTrivialStuff, {CommentStyle::LEAD_LINE, commentKind, tk}, cgPreInfo, commentGroups);
            return true;
        }
    } else {
        CommentStyle commentStyle = diffLine == 0 ? CommentStyle::TRAIL_CODE : CommentStyle::LEAD_LINE;
        UpdateFollowInfoAndAppendCommentGroup(
            preTkIdxIgnTrivialStuff, {commentStyle, commentKind, tk}, cgPreInfo, commentGroups);
        return true;
    }
    return false;
}

bool IsTrailCommentsInRuleOne(const CommentGroup cg, size_t cgIdx, Ptr<Node> node,
    std::unordered_map<size_t, size_t>& cgFollowInfo, const std::vector<Token>& tkStream)
{
    CJC_ASSERT(!cg.cms.empty());
    int diffline = cg.cms[0].info.Begin().line - node->GetEnd().line;
    if (diffline == 0) {
        return true;
    }
    if (diffline == 1) {
        if (cgFollowInfo.find(cgIdx) != cgFollowInfo.end()) {
            int blankLine = tkStream[cgFollowInfo[cgIdx]].Begin().line - cg.cms.back().info.End().line;
            if (blankLine > 1) {
                return true;
            }
        } else { // cg followed by another cg(at least one blank line) or nothing
            return true;
        }
    }
    return false;
}

std::pair<bool, Position> WhetherExistNextNodeBeforeOuterNodeEnd(
    const std::vector<CNode>& nodes, size_t offsetIdx, Ptr<Node> curNode, const std::stack<size_t>& nodeStack)
{
    bool findFlag{false};
    CJC_ASSERT(!nodes.empty());
    auto searchEnd = End(nodes.back());
    if (!nodeStack.empty()) {
        searchEnd = End(nodes[nodeStack.top()]);
    }
    for (size_t n = offsetIdx + 1; n < nodes.size(); n++) {
        if (Begin(nodes[n]) > searchEnd) { // out range, stop search
            break;
        }
        if (Begin(nodes[n]) > curNode->GetEnd() && Begin(nodes[n]) < searchEnd) {
            findFlag = true;
            break;
        }
    }
    return {findFlag, searchEnd};
}

enum class CommentPlace {
    LEADING,
    TRAILING,
    INNER
};
void AddComment(Node& node, CommentGroup& cg, CommentPlace place)
{
    switch (place) {
        case CommentPlace::LEADING:
            node.comments.leadingComments.push_back(std::move(cg));
            break;
        case CommentPlace::TRAILING:
            if (Is<File>(node)) {
                // For File node, the last comment is added first.
                node.comments.trailingComments.insert(node.comments.trailingComments.begin(), std::move(cg));
                break;
            }
            node.comments.trailingComments.push_back(std::move(cg));
            break;
        case CommentPlace::INNER:
            node.comments.innerComments.push_back(std::move(cg));
            break;
    }
}

void AddTrailingComment(Node& node, CommentGroup& cg)
{
    AddComment(node, cg, CommentPlace::TRAILING);
}

void AddLeadingComment(Node& node, CommentGroup& cg)
{
    AddComment(node, cg, CommentPlace::LEADING);
}

void AddInnerComment(Node& node, CommentGroup& cg)
{
    AddComment(node, cg, CommentPlace::INNER);
}

size_t AttachCommentToAheadNode(
    Ptr<Node> node, const Position& searchEnd, std::vector<CommentGroup>& commentGroups, size_t cgIdx)
{
    AddTrailingComment(*node, commentGroups[cgIdx]);
    while (cgIdx + 1 < commentGroups.size()) {
        CJC_ASSERT(!commentGroups[cgIdx + 1].cms.empty());
        if (commentGroups[cgIdx + 1].cms[0].info.Begin() >= searchEnd) {
            break;
        }
        ++cgIdx;
        AddTrailingComment(*node, commentGroups[cgIdx]);
    }
    return cgIdx;
}

/**
 * all comment groups in the token stream and location-related information
 */
struct CommentGroupsLocInfo {
    std::vector<CommentGroup> cgs;
    // key: groupIndex value: preTokenIndex in tokenStream(ignore nl, semi, comment)
    std::unordered_map<size_t, size_t> cgPreInfo;
    // key: groupIndex value: followTokenIndex in tokenStream(ignore nl, comment, end)
    std::unordered_map<size_t, size_t> cgFollowInfo;
    std::vector<Token> tkStream;
};

// return the index of the next comment group needs to be attached
size_t AttachCommentToOuterNode(const std::vector<CNode>& nodes, size_t nodeOffsetIdx,
    CommentGroupsLocInfo& cgInfo, size_t cgIdx, std::stack<size_t>& nodeStack)
{
    CJC_ASSERT(!nodes.empty());
    CJC_ASSERT(!nodeStack.empty());
    auto outerNode = std::get<Node*>(nodes[nodeStack.top()]);
    nodeStack.pop();
    while (!nodeStack.empty() && End(outerNode) == End(nodes[nodeStack.top()])) {
        // rule 3, choose the largest outer node before this comment
        outerNode = std::get<Node*>(nodes[nodeStack.top()]);
        nodeStack.pop();
    }
    for (; cgIdx < cgInfo.cgs.size(); ++cgIdx) {
        CJC_ASSERT(cgInfo.cgPreInfo.find(cgIdx) != cgInfo.cgPreInfo.end());
        if (End(outerNode) <= cgInfo.tkStream[cgInfo.cgPreInfo[cgIdx]].Begin()) {
            return cgIdx;
        }
        CJC_ASSERT(cgIdx < cgInfo.cgs.size());
        if (!IsTrailCommentsInRuleOne(
            cgInfo.cgs[cgIdx], cgIdx, outerNode, cgInfo.cgFollowInfo, cgInfo.tkStream)) {
            break;
        }
        AddTrailingComment(*outerNode, cgInfo.cgs[cgIdx]);
    }
    if (cgIdx >= cgInfo.cgs.size()) {
        return cgIdx;
    }
    auto [findFlag, searchEnd] = WhetherExistNextNodeBeforeOuterNodeEnd(nodes, nodeOffsetIdx, outerNode, nodeStack);
    if (!findFlag) {
        cgIdx = AttachCommentToAheadNode(outerNode, searchEnd, cgInfo.cgs, cgIdx);
        ++cgIdx;
    }
    return cgIdx;
}

/**
 * Attach comment groups to node
 * The control flow jump behavior of this loop:
 * If the comment is before the node, continue.
 * If both the comment and the next node are within range of the node, push the node to the stack and break.
 * If only the comment are within range of the node, continue.
 * If the comment is beyond the current outer node, attch comment to outer node and break
 * If the comment and the node are not closely connected then break.
 * If rule 1 is satisfied, continue.
 * If the comment is beyond the current node and rule 1 is not satisfied and there is a next node in the range of
 * the outer node then break.
 * If the comment is beyond the current node and rule 1 is not satisfied and there is no next node in the range of
 * the outer node then attach the subsequent comments in the range of the outer node and continue
 * @return the index of the next comment group needs to be attched
 */
size_t AttachCommentToNode(const std::vector<CNode>& nodes, size_t curNodeIdx,
    CommentGroupsLocInfo& cgInfo, size_t cgIdx, std::stack<size_t>& nodeStack)
{
    for (; cgIdx < cgInfo.cgs.size(); ++cgIdx) {
        auto& curCg = cgInfo.cgs[cgIdx];
        auto& curCgBegin = curCg.cms[0].info.Begin();
        const auto& curNodeBegin = Begin(nodes[curNodeIdx]);
        const auto& curNodeEnd = End(nodes[curNodeIdx]);
        CJC_ASSERT(!curCg.cms.empty());
        CJC_ASSERT(curCgBegin != curNodeBegin);
        auto cnode = nodes[curNodeIdx];
        Node* node{};
        [[maybe_unused]] const Position* ppos{};
        if (std::holds_alternative<Node*>(cnode)) {
            node = std::get<Node*>(cnode);
        } else {
            ppos = std::get<const Position*>(cnode);
        }
        if (curCgBegin < curNodeBegin) {
            if (node) {
                // /* comment */ public unsafe class A {}, unsafe is a Modifier
                AddLeadingComment(*node, curCg); // rule2
            } else {
                // the next token is not a Node, do not skip the token nor attach to the next Node
                // rather, attach to outer node
                // public /* comment */ class A {}, class is a Token
                AddInnerComment(*std::get<Node*>(nodes[nodeStack.top()]), curCg); // rule2
            }
        } else if (curCgBegin < curNodeEnd) {
            // class A    /* inner comment */ { }
            // ^curNode   ^curCg
            if (curNodeIdx + 1 < nodes.size() && Begin(nodes[curNodeIdx + 1]) < curNodeEnd) {
                nodeStack.push(curNodeIdx);
                break;
            }
        } else {
            if (!nodeStack.empty() && End(nodes[nodeStack.top()]) < curCgBegin) {
                cgIdx = AttachCommentToOuterNode(nodes, curNodeIdx, cgInfo, cgIdx, nodeStack);
                break;
            }
            if (cgInfo.cgPreInfo.find(cgIdx) == cgInfo.cgPreInfo.end()) {
                break; // bad node location
            }
            if (curNodeEnd <= cgInfo.tkStream[cgInfo.cgPreInfo[cgIdx]].Begin()) {
                break;
            }
            // curCgBegin >= curNodeEnd
            // class A { } /* ... */
            // ^ curNode   ^curCg
            while (!node && *ppos < curCgBegin && curNodeIdx + 1 < nodes.size()) {
                if (auto nn = std::get_if<Node*>(&nodes[++curNodeIdx])) {
                    node = *nn;
                } else {
                    ppos = std::get<const Position*>(nodes[curNodeIdx]);
                }
            }
            // no node, all nodes are tokens, this is a invalid Node
            // this comment adds to file or is discarded, because we do not add comments to tokens
            if (!node) {
                break;
            }
            if (IsTrailCommentsInRuleOne(curCg, cgIdx, node, cgInfo.cgFollowInfo, cgInfo.tkStream)) {
                AddTrailingComment(*node, curCg);
                continue;
            }
            // check to see if there is a next node before the end of top node in stack
            auto [findNextFlag, searchEnd] =
                WhetherExistNextNodeBeforeOuterNodeEnd(nodes, curNodeIdx, node, nodeStack);
            if (findNextFlag) {
                break;
            }
            if (!nodeStack.empty()) {
                // rule 5, no nodes after cg in current scope, attach to nearest smallest node as inner cg
                AddInnerComment(*std::get<Node*>(nodes[nodeStack.top()]), curCg);
            } else {
                cgIdx = AttachCommentToAheadNode(node, searchEnd, cgInfo.cgs, cgIdx); // rule2
            }
        }

        if (curCgBegin >= curNodeBegin && curCgBegin < curNodeEnd) {
            if (!node) {
                if (nodeStack.empty()) {
                    break; // add to file directly
                }
                AddInnerComment(*std::get<Node*>(nodes[nodeStack.top()]), curCg);
                continue;
            }
            AddInnerComment(*node, curCg); // rule2
        }
    }
    return cgIdx;
}

/**
 * Collect comment groups from token stream
 */
CommentGroupsLocInfo CollectCommentGroups(const std::set<Token>& tokens)
{
    CommentGroupsLocInfo cgInfo{{}, {}, {}, {tokens.begin(), tokens.end()}};
    Token preTokenIgnoreNL{TokenKind::SENTINEL, "", Position(0, 1, 1), Position{0, 1, 1}};
    bool needUpdateFollowInfo = false;
    std::optional<size_t> preTkIdxIgnTrivialStuff{std::nullopt}; // ignore NL, COMMENT, SEMI, COMMA
    for (size_t i = 0; i < cgInfo.tkStream.size(); ++i) {
        auto& tk = cgInfo.tkStream[i];
        if (tk.kind == TokenKind::NL || tk.commentForMacroDebug) {
            continue;
        }
        if (tk.kind != TokenKind::COMMENT) {
            if (tk.kind != TokenKind::SEMI) {
                preTkIdxIgnTrivialStuff = i;
            }
            preTokenIgnoreNL = tk;
            if (needUpdateFollowInfo && tk.kind != TokenKind::END) {
                CJC_ASSERT(cgInfo.cgs.size() > 0);
                // it won't run here if followed by a cg or nothing
                cgInfo.cgFollowInfo[cgInfo.cgs.size() - 1] = i;
                needUpdateFollowInfo = false;
            }
            continue;
        }
        if (UpdateCommentGroups(
            tk, preTokenIgnoreNL, preTkIdxIgnTrivialStuff, cgInfo.cgs, cgInfo.cgPreInfo)) {
            needUpdateFollowInfo = true;
        }
        preTokenIgnoreNL = tk;
    }
    return cgInfo;
}

/**
 * Attach comment group to the ast nodes(sorted by range)
 * \param cgIdx: attach comment, start from index cgIdx
 */
void AttachCommentToSortedNodes(std::vector<CNode>& nodes, CommentGroupsLocInfo& cgInfo, size_t cgIdx)
{
    if (cgInfo.cgs.empty()) {
        return;
    }
    std::stack<size_t> nodeStack;
    for (size_t i = 0; i < nodes.size() && cgIdx < cgInfo.cgs.size(); ++i) {
        auto begin = Begin(nodes[i]);
        if (begin.line < 1 || begin.column < 1) { // bad node pos
            continue;
        }
        cgIdx = AttachCommentToNode(nodes, i, cgInfo, cgIdx, nodeStack);
    }
}

/// Return the index of the next comment group needs to be attached
size_t CollectFileCG(const std::vector<CNode>& nodes, CommentGroupsLocInfo& cgInfo, Ptr<File> node)
{
    size_t begin{0};
    while (begin < cgInfo.cgs.size()) {
        if (nodes.empty() || cgInfo.cgs[begin].cms.back().info.End().line + 1 < Begin(nodes.front()).line) {
            AddLeadingComment(*node, cgInfo.cgs[begin++]);
        } else {
            break;
        }
    }
    if (cgInfo.cgs.empty()) {
        return 0;
    }
    auto end = cgInfo.cgs.size() - 1;
    auto endLine{0};
    for (auto& n: nodes) {
        endLine = std::max(endLine, End(n).line + 1);
    }
    while (end >= begin && cgInfo.cgs[end].cms.back().info.Begin().line > endLine) {
        AddTrailingComment(*node, cgInfo.cgs[end]);
        if (end-- == 0) {
            break;
        }
    }
    cgInfo.cgs.erase(cgInfo.cgs.begin() + end + 1, cgInfo.cgs.end());
    return begin;
}
} // namespace

/**
 * Attach comment group to the ast nodes
 * note : only run in ParseNodes in macro expansion
 */
void ParserImpl::AttachCommentToNodes(std::vector<OwnedPtr<Node>>& nodes)
{
    std::vector<CNode> nps;
    nps.reserve(nodes.size());
    for (const auto& n : nodes) {
        nps.emplace_back(n.get());
    }
    SortNodesByRange(nps);
    auto cgInfo = CollectCommentGroups(lexer->GetTokenStream());
    AttachCommentToSortedNodes(nps, cgInfo, 0);
}

void ParserImpl::AttachCommentToFile(Ptr<File> node)
{
    auto nodes = CollectPtrsOfASTNodes(node);
    auto cgInfo = CollectCommentGroups(lexer->GetTokenStream());
    auto cgIdx = CollectFileCG(nodes, cgInfo, node);
    AttachCommentToSortedNodes(nodes, cgInfo, cgIdx);
}