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
#include "cangjie/AST/Walker.h"

using namespace Cangjie;
using namespace Cangjie::AST;

namespace {
// collect ptrs of ast nodes, ignore file, annotation and modifier
std::vector<Ptr<Node>> CollectPtrsOfASTNodes(Ptr<File> node)
{
    std::vector<Ptr<Node>> ptrs;
    auto collectNodes = [&ptrs](Ptr<Node> curNode) -> VisitAction {
        if (curNode->astKind == ASTKind::ANNOTATION || curNode->astKind == ASTKind::MODIFIER) {
            return VisitAction::SKIP_CHILDREN;
        }
        bool ignoreFlag = false;
        if (curNode->astKind == ASTKind::FILE) {
            ignoreFlag = true;
        } else if (auto inv = curNode->GetConstInvocation(); inv && inv->decl) {
            ignoreFlag = true;
        }
        if (ignoreFlag) {
            return VisitAction::WALK_CHILDREN;
        }
        ptrs.push_back(curNode);
        return VisitAction::WALK_CHILDREN;
    };
    Walker macWalker(node, collectNodes);
    macWalker.Walk();
    return ptrs;
}

// Sort by begin position in ascending order. If the begins are the same, the node with a larger range is first.
void SortNodesByRange(std::vector<Ptr<Node>>& nodes)
{
    auto cmpByRange = [](Ptr<Node> a, Ptr<Node> b) {
        if (a->GetBegin() < b->GetBegin()) {
            return true;
        } else if (a->GetBegin() == b->GetBegin() && a->GetEnd() > b->GetEnd()) {
            return true;
        }
        return false;
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
    const std::vector<Ptr<Node>>& nodes, size_t offsetIdx, Ptr<Node> curNode, const std::stack<size_t>& nodeStack)
{
    bool findFlag{false};
    CJC_ASSERT(!nodes.empty());
    auto searchEnd = nodes.back()->GetEnd();
    if (!nodeStack.empty()) {
        searchEnd = nodes[nodeStack.top()]->GetEnd();
    }
    for (size_t n = offsetIdx + 1; n < nodes.size(); n++) {
        if (nodes[n]->GetBegin() > searchEnd) { // out range, stop search
            break;
        }
        if (nodes[n]->GetBegin() > curNode->GetEnd() && nodes[n]->GetBegin() < searchEnd) {
            findFlag = true;
            break;
        }
    }
    return {findFlag, searchEnd};
}

size_t AttchCommentToAheadNode(
    Ptr<Node> node, const Position& searchEnd, std::vector<CommentGroup>& commentGroups, size_t cgIdx)
{
    node->comments.trailingComments.push_back(commentGroups[cgIdx]);
    while (cgIdx + 1 < commentGroups.size()) {
        CJC_ASSERT(!commentGroups[cgIdx + 1].cms.empty());
        if (commentGroups[cgIdx + 1].cms[0].info.Begin() >= searchEnd) {
            break;
        }
        ++cgIdx;
        node->comments.trailingComments.push_back(commentGroups[cgIdx]);
    }
    return cgIdx;
}

// return the index of the next comment group needs to be attched
size_t AttchCommentToOuterNode(const std::vector<Ptr<Node>>& nodes, size_t nodeOffsetIdx,
    CommentGroupsLocInfo& cgInfo, size_t cgIdx, std::stack<size_t>& nodeStack)
{
    CJC_ASSERT(!nodes.empty());
    CJC_ASSERT(!nodeStack.empty());
    auto outerNode = nodes[nodeStack.top()];
    nodeStack.pop();
    while (!nodeStack.empty() && outerNode->GetEnd() == nodes[nodeStack.top()]->GetEnd()) {
        outerNode = nodes[nodeStack.top()];
        nodeStack.pop();
    }
    for (; cgIdx < cgInfo.commentGroups.size(); ++cgIdx) {
        CJC_ASSERT(cgInfo.cgPreInfo.find(cgIdx) != cgInfo.cgPreInfo.end());
        if (outerNode->GetEnd() <= cgInfo.tkStream[cgInfo.cgPreInfo[cgIdx]].Begin()) {
            return cgIdx;
        }
        CJC_ASSERT(cgIdx < cgInfo.commentGroups.size());
        if (!IsTrailCommentsInRuleOne(
            cgInfo.commentGroups[cgIdx], cgIdx, outerNode, cgInfo.cgFollowInfo, cgInfo.tkStream)) {
            break;
        }
        outerNode->comments.trailingComments.push_back(cgInfo.commentGroups[cgIdx]);
    }
    if (cgIdx >= cgInfo.commentGroups.size()) {
        return cgIdx;
    }
    auto [findFlag, searchEnd] = WhetherExistNextNodeBeforeOuterNodeEnd(nodes, nodeOffsetIdx, outerNode, nodeStack);
    if (!findFlag) {
        cgIdx = AttchCommentToAheadNode(outerNode, searchEnd, cgInfo.commentGroups, cgIdx);
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
size_t AttachCommentToNode(const std::vector<Ptr<Node>>& nodes, size_t curNodeIdx,
    CommentGroupsLocInfo& cgInfo, size_t cgIdx, std::stack<size_t>& nodeStack)
{
    for (; cgIdx < cgInfo.commentGroups.size(); ++cgIdx) {
        auto& curCg = cgInfo.commentGroups[cgIdx];
        auto& curCgBegin = curCg.cms[0].info.Begin();
        const auto& curNodeBegin = nodes[curNodeIdx]->GetBegin();
        const auto& curNodeEnd = nodes[curNodeIdx]->GetEnd();
        CJC_ASSERT(!curCg.cms.empty());
        CJC_ASSERT(curCgBegin != curNodeBegin);
        if (curCgBegin < curNodeBegin) {
            nodes[curNodeIdx]->comments.leadingComments.push_back(curCg); // rule2
        } else if (curCgBegin < curNodeEnd) {
            if (curNodeIdx + 1 < nodes.size() && nodes[curNodeIdx + 1]->GetBegin() < curNodeEnd) {
                nodeStack.push(curNodeIdx);
                break;
            }
            nodes[curNodeIdx]->comments.innerComments.push_back(curCg); // rule2
        } else {
            if (!nodeStack.empty() && nodes[nodeStack.top()]->GetEnd() < curCgBegin) {
                cgIdx = AttchCommentToOuterNode(nodes, curNodeIdx, cgInfo, cgIdx, nodeStack);
                break;
            }
            if (cgInfo.cgPreInfo.find(cgIdx) == cgInfo.cgPreInfo.end()) {
                break; // bad node location
            }
            if (curNodeEnd <= cgInfo.tkStream[cgInfo.cgPreInfo[cgIdx]].Begin()) {
                break;
            }
            if (IsTrailCommentsInRuleOne(curCg, cgIdx, nodes[curNodeIdx], cgInfo.cgFollowInfo, cgInfo.tkStream)) {
                nodes[curNodeIdx]->comments.trailingComments.push_back(curCg);
                continue;
            }
            // check to see if there is a next node before the end of top node in stack
            auto [findNextFlag, searchEnd] =
                WhetherExistNextNodeBeforeOuterNodeEnd(nodes, curNodeIdx, nodes[curNodeIdx], nodeStack);
            if (findNextFlag) {
                break;
            }
            if (!findNextFlag) {
                cgIdx = AttchCommentToAheadNode(nodes[curNodeIdx], searchEnd, cgInfo.commentGroups, cgIdx); // rule2
            }
        }
    }
    return cgIdx;
}

} // namespace

void ParserImpl::CollectCommentGroups(CommentGroupsLocInfo& cgInfo) const
{
    Token preTokenIgnoreNL{TokenKind::SENTINEL, "", Position(0, 1, 1), Position{0, 1, 1}};
    bool needUpdateFollowInfo = false;
    std::optional<size_t> preTkIdxIgnTrivialStuff{std::nullopt}; // ignore NL, Semi, Comment
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
                CJC_ASSERT(cgInfo.commentGroups.size() > 0);
                // it won't run here if it followed by a cg or nothing
                cgInfo.cgFollowInfo[cgInfo.commentGroups.size() - 1] = i;
                needUpdateFollowInfo = false;
            }
            continue;
        }
        if (UpdateCommentGroups(
            tk, preTokenIgnoreNL, preTkIdxIgnTrivialStuff, cgInfo.commentGroups, cgInfo.cgPreInfo)) {
            needUpdateFollowInfo = true;
        }
        preTokenIgnoreNL = tk;
    }
}

void ParserImpl::AttachCommentToSortedNodes(std::vector<Ptr<Node>>& nodes)
{
    CommentGroupsLocInfo cgInfo{{}, {}, {}, lexer->GetTokenStream()};
    CollectCommentGroups(cgInfo);
    if (cgInfo.commentGroups.empty()) {
        return;
    }
    size_t cgIdx{0};
    std::stack<size_t> nodeStack;
    for (size_t i = 0; i < nodes.size() && cgIdx < cgInfo.commentGroups.size(); ++i) {
        if (nodes[i]->GetBegin().line < 1 || nodes[i]->GetBegin().column < 1) { // bad node pos
            continue;
        }
        cgIdx = AttachCommentToNode(nodes, i, cgInfo, cgIdx, nodeStack);
    }
}

void ParserImpl::AttachCommentToNodes(std::vector<OwnedPtr<Node>>& nodes)
{
    std::vector<Ptr<Node>> nps;
    nps.reserve(nodes.size());
    for (const auto& n : nodes) {
        nps.emplace_back(n.get());
    }
    SortNodesByRange(nps);
    AttachCommentToSortedNodes(nps);
}

void ParserImpl::AttachCommentToFile(Ptr<File> node)
{
    auto nodes = CollectPtrsOfASTNodes(node);
    if (nodes.empty()) {
        return;
    }
    AttachCommentToSortedNodes(nodes);
}