// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the Searcher related classes.
 */

#include "cangjie/AST/Searcher.h"

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "QueryParser.h"

#include "cangjie/AST/ASTContext.h"
#include "cangjie/AST/ScopeManagerApi.h"
#include "cangjie/Basic/Print.h"
#include "cangjie/Utils/FileUtil.h"
#include "cangjie/Utils/StdUtils.h"
#include "cangjie/Utils/Utils.h"

using namespace Cangjie;
using namespace AST;
using namespace Utils;
using namespace FileUtil;

namespace {
std::string NormalizeQuery(const Query& query, const Order& order)
{
    std::string normalizedQuery;
    query.PrettyPrint(normalizedQuery);
    std::for_each(query.fileHashes.cbegin(), query.fileHashes.cend(), [&normalizedQuery](auto fileHash) {
        normalizedQuery.append("|fileHash:").append(FileUtil::GetShortHash(fileHash));
    });
    if (&order == &Sort::posAsc) {
        normalizedQuery.append("|sort:asc");
    } else if (&order == &Sort::posDesc) {
        normalizedQuery.append("|sort:desc");
    }
    return normalizedQuery;
}
} // namespace

void Trie::Insert(const std::string& value)
{
    TrieNode* n = root.get();
    for (unsigned i = 0; i < value.size(); ++i) {
        if (n->next.find(value[i]) == n->next.end()) {
            n->next.emplace(value[i], std::make_unique<TrieNode>(value[i]));
        }
        n = n->next[value[i]].get();
        n->prefix = value.substr(0, i + 1);
        n->suffixes.insert(value.substr(i + 1));
    }
    n->value = value;
}

void Trie::Insert(const std::string& value, Symbol& id)
{
    TrieNode* n = root.get();
    for (unsigned i = 0; i < value.size(); ++i) {
        if (n->next.find(value[i]) == n->next.end()) {
            n->next.emplace(value[i], std::make_unique<TrieNode>(value[i]));
        }
        auto next = n->next[value[i]].get();
        next->depth = n->depth + 1;
        next->parent = n;
        next->ids.insert(&id);
        n = next;
        n->prefix = value.substr(0, i + 1);
        n->suffixes.insert(value.substr(i + 1));
    }
    n->value = value;
}

void Trie::Delete(const std::string& value, Symbol& id)
{
    TrieNode* n = root.get();
    for (char i : value) {
        if (n->next.empty()) {
            return;
        }
        auto next = n->next[i].get();
        next->ids.erase(&id);
        n = next;
    }
}

TrieNode* Trie::GetPrefixRootNode(const std::string& prefix) const
{
    TrieNode* n = root.get();
    for (auto c : prefix) {
        TrieNode::Next::const_iterator found = n->next.find(c);
        if (found == n->next.end()) {
            return nullptr;
        }
        n = found->second.get();
    }
    return n;
}

std::vector<std::string> Trie::PrefixMatch(const std::string& prefix) const
{
    std::vector<std::string> matches;
    auto prefixRoot = GetPrefixRootNode(prefix);
    std::function<void(TrieNode*)> walk = [&matches, &walk](TrieNode* n) {
        if (!n) {
            return;
        }
        if (!n->value.empty()) {
            matches.push_back(n->value);
        }
        for (auto& i : std::as_const(n->next)) {
            walk(i.second.get());
        }
    };
    walk(prefixRoot);
    return matches;
}

std::vector<std::string> Trie::SuffixMatch(const std::string& suffix) const
{
    std::vector<std::string> matches;
    std::function<void(TrieNode*)> walk = [&suffix, &matches, &walk](TrieNode* n) {
        if (!n) {
            return;
        }
        if (n->suffixes.find(suffix) != n->suffixes.cend()) {
            matches.push_back(n->prefix + suffix);
        }
        for (auto& i : std::as_const(n->next)) {
            walk(i.second.get());
        }
    };
    walk(root.get());
    return matches;
}

std::string PosSearchApi::PosToStr(const Position& pos)
{
    std::string ret = FillZero(static_cast<int>(pos.fileID), MAX_DIGITS_FILE);
    ret += FillZero(pos.line, MAX_DIGITS_LINE);
    ret += FillZero(pos.column, MAX_DIGITS_COLUMN);
    return ret;
}

TrieNode* PosSearchApi::FindCommonRootInPosTrie(const Trie& posTrie, const Position& pos)
{
    std::string posStr = PosToStr(pos);
    TrieNode* n = posTrie.root.get();
    for (auto c : posStr) {
        TrieNode::Next::const_iterator found = n->next.find(c);
        if (found == n->next.cend()) {
            return n;
        }
        n = found->second.get();
    }
    return n;
}

TrieNode* PosSearchApi::FindCommonRootInPosTrie(const Trie& posTrie, const std::string& pos)
{
    TrieNode* n = posTrie.root.get();
    for (auto c : pos) {
        TrieNode::Next::const_iterator found = n->next.find(c);
        if (found == n->next.cend()) {
            return n;
        }
        n = found->second.get();
    }
    return n;
}

std::set<Symbol*> PosSearchApi::GetIDsGreaterThanPos(const Trie& posTrie, const Position& pos, bool isClose)
{
    std::string posStr = PosToStr(pos);
    TrieNode* n = FindCommonRootInPosTrie(posTrie, posStr);
    TrieNode::Next::const_iterator startIter;
    std::set<Symbol*> ids;
    while (n->depth > MAX_DIGITS_FILE) {
        if (n->depth == MAX_DIGITS_POSITION) {
            n = n->parent;
            continue;
        }
        if (isClose) {
            startIter = n->next.lower_bound(posStr[n->depth]);
            isClose = false;
        } else {
            startIter = n->next.upper_bound(posStr[n->depth]);
        }
        for (auto it = startIter; it != n->next.end(); ++it) {
            ids = Searcher::Union(ids, it->second->ids);
        }
        n = n->parent;
    }
    return ids;
}

void PosSearchApi::GetIDsLessThanPosOfDepth(
    TrieNode& n, const std::string& posStr, std::set<Symbol*>& ids, bool& isClose)
{
    auto endIter = n.next.lower_bound(posStr[n.depth]);
    if (endIter != n.next.cend()) {
        if (isClose) {
            if (endIter->first == posStr[n.depth]) {
                ++endIter;
            }
            isClose = false;
        }
    }
    for (auto it = n.next.cbegin(); it != endIter; ++it) {
        ids = Searcher::Union(ids, it->second->ids);
    }
}

std::set<Symbol*> PosSearchApi::GetIDsLessThanPos(const Trie& posTrie, const Position& pos, bool isClose)
{
    std::string posStr = PosToStr(pos);
    TrieNode* n = FindCommonRootInPosTrie(posTrie, posStr);
    std::set<Symbol*> ids;
    while (n->depth > MAX_DIGITS_FILE) {
        if (n->depth == MAX_DIGITS_POSITION) {
            n = n->parent;
            continue;
        }
        GetIDsLessThanPosOfDepth(*n, posStr, ids, isClose);
        n = n->parent;
    }
    return ids;
}

std::set<Symbol*> PosSearchApi::GetIDsWithinPosXByRange(
    const Trie& posTrie, const Position& startPos, const Position& endPos, bool isLeftClose, bool isRightClose)
{
    std::set<Symbol*> ids;
    if (endPos <= startPos) {
        return ids;
    }
    ids = GetIDsGreaterThanPos(posTrie, startPos, isLeftClose);
    ids = Searcher::Intersection(ids, GetIDsLessThanPos(posTrie, endPos, isRightClose));
    return ids;
}

// Used for sort function, caller guarantees symbol inputs are not nullptr.
Order Sort::posAsc = [](const Symbol* a, const Symbol* b) noexcept {
    CJC_ASSERT(a && b);
    if (!a->node || !b->node) {
        return a->node < b->node;
    }
    if (a->node->begin.fileID < b->node->begin.fileID) {
        return true;
    } else if (a->node->begin.fileID == b->node->begin.fileID && a->node->begin < b->node->begin) {
        return true;
    } else if (a->node->begin.fileID == b->node->begin.fileID && a->node->begin == b->node->begin) {
        // If the begin positions of different nodes are the same, the one with the bigger end position should be at the
        // front.
        return a->node->end == b->node->end ? a->astKind < b->astKind : b->node->end < a->node->end;
    } else {
        return false;
    }
};

Order Sort::posDesc = [](const Symbol* a, const Symbol* b) noexcept {
    return Sort::posAsc(b, a); // Descending is reversed usage of ascending.
};

std::pair<std::vector<Symbol*>, bool> Searcher::FindInSearchCache(
    const Query& query, const std::string& normalizedQuery)
{
    std::vector<Symbol*> results;
    bool needFilter = !query.fileHashes.empty();
    auto it = cache.find(normalizedQuery);
    if (it == cache.end()) {
        // Cached filtered results.
        return {results, false};
    }
    if (!needFilter) {
        return {it->second, true};
    }
    for (auto sym : it->second) {
        if (InFiles(query.fileHashes, *sym->id)) {
            results.push_back(sym);
        }
    }
    return {results, true};
}

std::vector<Symbol*> Searcher::FilterAndSortSearchResult(
    const std::set<Symbol*>& ids, const Query& query, const Order& order) const
{
    std::vector<Symbol*> results;
    bool needFilter = !query.fileHashes.empty();
    for (Symbol* id : ids) {
        if (needFilter && !InFiles(query.fileHashes, *id)) {
            continue;
        }
        results.push_back(id);
    }
    sort(results.begin(), results.end(), order);
    return results;
}

std::vector<Symbol*> Searcher::Search(const ASTContext& ctx, const Query* query, const Order& order)
{
    if (!query) {
        return {};
    }
    auto normalizedQuery = NormalizeQuery(*query, order);
    auto ret = FindInSearchCache(*query, normalizedQuery);
    if (ret.second) {
        return ret.first;
    }
    std::set<Symbol*> ids = PerformSearch(ctx, *query);
    auto results = FilterAndSortSearchResult(ids, *query, order);
    cache.insert_or_assign(normalizedQuery, results);
    return results;
}

std::vector<Symbol*> Searcher::Search(
    const ASTContext& ctx, const std::string& query, const Order& order, const std::unordered_set<uint64_t>& fileHashes)
{
    QueryParser qp(query, ctx.diag, ctx.diag.GetSourceManager());
    std::unique_ptr<Query> q = qp.Parse();
    if (!q) {
        return {};
    }
    if (!fileHashes.empty()) {
        q->fileHashes = fileHashes;
    }
    auto symbols = Search(ctx, q.get(), order);
    return symbols;
}

std::set<Symbol*> Searcher::GetIDsByName(const ASTContext& ctx, const std::string& name) const
{
    std::set<Symbol*> ids;
    auto it = ctx.invertedIndex.nameIndexes.find(name);
    if (it != ctx.invertedIndex.nameIndexes.end()) {
        ids.insert(it->second.begin(), it->second.end());
    }
    return ids;
}

std::set<Symbol*> Searcher::GetIDsByNamePrefix(const ASTContext& ctx, const std::string& prefix) const
{
    std::set<Symbol*> ids;
    std::vector<std::string> names = ctx.invertedIndex.nameTrie->PrefixMatch(prefix);
    for (std::string& name : names) {
        ids = Union(ids, GetIDsByName(ctx, name));
    }
    return ids;
}

std::set<Symbol*> Searcher::GetIDsByNameSuffix(const ASTContext& ctx, const std::string& suffix) const
{
    std::set<Symbol*> ids;
    std::vector<std::string> names = ctx.invertedIndex.nameTrie->SuffixMatch(suffix);
    for (std::string& name : names) {
        ids = Union(ids, GetIDsByName(ctx, name));
    }
    return ids;
}

std::set<Symbol*> Searcher::GetIDsByScopeName(const ASTContext& ctx, const std::string& scopeName) const
{
    std::set<Symbol*> ids;
    auto it = ctx.invertedIndex.scopeNameIndexes.find(scopeName);
    if (it != ctx.invertedIndex.scopeNameIndexes.end()) {
        ids.insert(it->second.begin(), it->second.end());
    }
    return ids;
}

void Searcher::InvalidateCacheBut(const std::string& query)
{
    for (auto it = cache.begin(); it != cache.end();) {
        if (it->first.find(query) == std::string::npos) {
            it = cache.erase(it);
        } else {
            ++it;
        }
    }
}

std::vector<std::string> Searcher::GetScopeNamesByPrefix(const ASTContext& ctx, const std::string& prefix) const
{
    return ctx.invertedIndex.scopeNameTrie->PrefixMatch(prefix);
}

std::set<Symbol*> Searcher::GetIDsByScopeLevel(const ASTContext& ctx, uint32_t scopeLevel) const
{
    std::set<Symbol*> ids;
    auto it = ctx.invertedIndex.scopeLevelIndexes.find(scopeLevel);
    if (it != ctx.invertedIndex.scopeLevelIndexes.end()) {
        ids.insert(it->second.begin(), it->second.end());
    }
    return ids;
}

std::set<Symbol*> Searcher::GetIDsByASTKind(const ASTContext& ctx, const std::string& astKind) const
{
    std::set<Symbol*> ids;
    auto it = ctx.invertedIndex.astKindIndexes.find(astKind);
    if (it != ctx.invertedIndex.astKindIndexes.end()) {
        ids.insert(it->second.begin(), it->second.end());
    }
    return ids;
}

std::vector<std::string> Searcher::GetAstKindsBySuffix(const ASTContext& ctx, const std::string& suffix) const
{
    return ctx.invertedIndex.astKindTrie->SuffixMatch(suffix);
}

std::set<Symbol*> Searcher::GetIDsByPosEQ(
    const ASTContext& ctx, const Position& pos, bool isLeftClose, bool isRightClose) const
{
    // Equivalent to finding symbol n whose position meets n->node->begin <= pos && pos < n->node->end.
    std::set<Symbol*> ids;
    if (ctx.invertedIndex.posBeginTrie != nullptr) {
        ids = PosSearchApi::GetIDsLessThanPos(*ctx.invertedIndex.posBeginTrie, pos, isLeftClose);
    }
    std::set<Symbol*> ids1;
    if (ctx.invertedIndex.posEndTrie != nullptr) {
        ids1 = PosSearchApi::GetIDsGreaterThanPos(*ctx.invertedIndex.posEndTrie, pos, isRightClose);
    }
    if (!ids1.empty()) {
        return Intersection(ids, ids1);
    }
    // We use scope_level to filter the node contain the pos.
    if (ids.empty()) {
        return {};
    }
    std::set<Symbol*> result;
    int scopeLevel = static_cast<int>((*ids.cbegin())->scopeLevel);
    for (auto id : ids) {
        if (scopeLevel >= 0) {
            result.insert(id);
            scopeLevel--;
        } else {
            break;
        }
    }
    return result;
}

std::set<Symbol*> Searcher::GetIDsByPosLT(const ASTContext& ctx, const Position& pos, bool contain) const
{
    // Equivalent to finding symbol n whose position meets n->node->begin <= pos && pos < n->node->end.
    std::set<Symbol*> ids;
    if (ctx.invertedIndex.posEndTrie != nullptr) {
        ids = PosSearchApi::GetIDsLessThanPos(*ctx.invertedIndex.posEndTrie, pos, false);
    }
    if (contain) {
        ids = Union(ids, GetIDsByPosEQ(ctx, pos));
    }
    return ids;
}

std::set<Symbol*> Searcher::GetIDsByPosGT(const ASTContext& ctx, const Position& pos, bool contain) const
{
    // Equivalent to finding symbol n whose position meets n->node->begin > pos && pos <= n->node->end.
    std::set<Symbol*> ids;
    if (ctx.invertedIndex.posBeginTrie != nullptr) {
        ids = PosSearchApi::GetIDsGreaterThanPos(*ctx.invertedIndex.posBeginTrie, pos, false);
    }
    if (contain) {
        ids = Union(ids, GetIDsByPosEQ(ctx, pos));
    }
    return ids;
}

std::set<Symbol*> Searcher::PerformSearch(const ASTContext& ctx, const Query& query)
{
    std::set<Symbol*> ids;
    if (query.type == QueryType::OP) {
        if (!query.left || !query.right) {
            return {};
        }
        if (query.op == Operator::AND) {
            ids = PerformSearch(ctx, *query.left);
            ids = Intersection(ids, PerformSearch(ctx, *query.right));
        } else if (query.op == Operator::OR) {
            ids = PerformSearch(ctx, *query.left);
            ids = Union(ids, PerformSearch(ctx, *query.right));
        } else if (query.op == Operator::NOT) {
            ids = PerformSearch(ctx, *query.left);
            ids = Difference(ids, PerformSearch(ctx, *query.right));
        }
    } else {
        ids = GetIDs(ctx, query);
    }
    return ids;
}

std::set<Symbol*> Searcher::GetIDsByPos(const ASTContext& ctx, const Query& query) const
{
    std::set<Symbol*> ids;
    if (query.type != QueryType::POS) {
        return ids;
    }
    if (query.sign == "=") {
        ids = GetIDsByPosEQ(ctx, query.pos);
    } else if (query.sign == "<") {
        ids = GetIDsByPosLT(ctx, query.pos);
    } else if (query.sign == "<=") {
        ids = GetIDsByPosLT(ctx, query.pos, true);
    } else if (query.sign == ">") {
        ids = GetIDsByPosGT(ctx, query.pos);
    } else if (query.sign == ">=") {
        ids = GetIDsByPosGT(ctx, query.pos, true);
    }
    return ids;
}

std::set<Symbol*> Searcher::GetIDsByName(const ASTContext& ctx, const Query& query) const
{
    std::set<Symbol*> ids;
    if (query.key != "name") {
        return ids;
    }
    if (query.matchKind == MatchKind::PRECISE) {
        ids = GetIDsByName(ctx, query.value);
    } else if (query.matchKind == MatchKind::PREFIX) {
        ids = GetIDsByNamePrefix(ctx, query.value);
    } else {
        ids = GetIDsByNameSuffix(ctx, query.value);
    }
    return ids;
}

std::set<Symbol*> Searcher::GetIDsByScopeLevel(const ASTContext& ctx, const Query& query) const
{
    std::set<Symbol*> ids;
    constexpr int decimalBase = 10;
    if (query.key != "scope_level") {
        return ids;
    }
    if (query.sign == "=") {
        unsigned long level = StrToUint(ctx, query.value);
        if (level < UINT32_MAX) {
            ids = GetIDsByScopeLevel(ctx, static_cast<uint32_t>(level));
        }
    } else if (query.sign == "<") {
        for (uint32_t scopelevel = 0; scopelevel < std::strtoul(query.value.c_str(), nullptr, decimalBase);
             ++scopelevel) {
            ids = Union(ids, GetIDsByScopeLevel(ctx, scopelevel));
        }
    } else if (query.sign == "<=") {
        for (uint32_t scopelevel = 0; scopelevel <= std::strtoul(query.value.c_str(), nullptr, decimalBase);
             ++scopelevel) {
            ids = Union(ids, GetIDsByScopeLevel(ctx, scopelevel));
        }
    }
    return ids;
}

std::set<Symbol*> Searcher::GetIDsByScopeName(const ASTContext& ctx, const Query& query) const
{
    std::set<Symbol*> ids;
    if (query.key != "scope_name") {
        return ids;
    }
    if (query.matchKind == MatchKind::PRECISE) {
        ids = GetIDsByScopeName(ctx, query.value);
    } else if (query.matchKind == MatchKind::PREFIX) {
        auto scopeNames = GetScopeNamesByPrefix(ctx, query.value);
        for (auto& n : scopeNames) {
            ids = Union(ids, GetIDsByScopeName(ctx, n));
        }
    } else {
        ctx.diag.DiagnoseRefactor(DiagKindRefactor::searcher_invalid_scope_name, DEFAULT_POSITION, query.value);
    }
    return ids;
}

std::set<Symbol*> Searcher::GetIDsByASTKind(const ASTContext& ctx, const Query& query) const
{
    std::set<Symbol*> ids;
    if (query.key != "ast_kind") {
        return ids;
    }
    if (query.matchKind == MatchKind::PRECISE) {
        ids = GetIDsByASTKind(ctx, query.value);
    } else if (query.matchKind == MatchKind::SUFFIX) {
        auto astKinds = GetAstKindsBySuffix(ctx, query.value);
        for (auto& n : astKinds) {
            ids = Union(ids, GetIDsByASTKind(ctx, n));
        }
    }
    return ids;
}

std::set<Symbol*> Searcher::GetIDs(const ASTContext& ctx, const Query& query) const
{
    std::set<Symbol*> ids;
    if (query.type == QueryType::POS) {
        ids = GetIDsByPos(ctx, query);
    } else {
        std::string key = query.key;
        if (key == "name") {
            ids = GetIDsByName(ctx, query);
        } else if (key == "scope_level") {
            ids = GetIDsByScopeLevel(ctx, query);
        } else if (key == "scope_name") {
            ids = GetIDsByScopeName(ctx, query);
        } else if (key == "ast_kind") {
            ids = GetIDsByASTKind(ctx, query);
        } else {
            Errorf("Unknow query term: %s!\n", key.c_str());
        }
    }
    return ids;
}

unsigned long Searcher::StrToUint(const ASTContext& ctx, const std::string& queryVal) const
{
    if (queryVal.empty()) {
        ctx.diag.DiagnoseRefactor(DiagKindRefactor::searcher_empty_number, DEFAULT_POSITION);
        return UINT32_MAX;
    }
    unsigned long uintValue = UINT32_MAX;
    bool validDigits = std::all_of(queryVal.cbegin(), queryVal.cend(), [](auto c) { return std::isdigit(c); });
    if (validDigits) {
        if (auto v = Stoul(queryVal)) {
            uintValue = *v;
        } else {
            validDigits = false;
        }
    }
    if (!validDigits) {
        ctx.diag.DiagnoseRefactor(DiagKindRefactor::searcher_invalid_number, DEFAULT_POSITION, queryVal);
        return UINT32_MAX;
    }
    if (uintValue >= ctx.symbolTable.size() && !ctx.symbolTable.empty()) {
        ctx.diag.DiagnoseRefactor(DiagKindRefactor::searcher_past_the_end_of_array, DEFAULT_POSITION, queryVal);
        return UINT32_MAX;
    }
    return uintValue;
}

std::set<Symbol*> Searcher::Intersection(const std::set<Symbol*>& set1, const std::set<Symbol*>& set2)
{
    std::set<Symbol*> ret;
    std::set_intersection(set1.begin(), set1.end(), set2.begin(), set2.end(), std::inserter(ret, ret.begin()));
    return ret;
}

std::set<Symbol*> Searcher::Union(const std::set<Symbol*>& set1, const std::set<Symbol*>& set2)
{
    std::set<Symbol*> ret;
    std::set_union(set1.begin(), set1.end(), set2.begin(), set2.end(), std::inserter(ret, ret.begin()));
    return ret;
}

std::set<Symbol*> Searcher::Difference(const std::set<Symbol*>& set1, const std::set<Symbol*>& set2) const
{
    std::set<Symbol*> ret;
    std::set_difference(set1.begin(), set1.end(), set2.begin(), set2.end(), std::inserter(ret, ret.begin()));
    return ret;
}

bool Searcher::InFiles(const std::unordered_set<uint64_t>& files, const Symbol& id) const
{
    return files.find(id.hashID.hash64) != files.end();
}
