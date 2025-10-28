// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares Symbol related classes.
 */

#ifndef CANGJIE_AST_SYMBOL_H
#define CANGJIE_AST_SYMBOL_H

#include <atomic>
#include <string>

#include "cangjie/AST/Node.h"

namespace Cangjie {
class Collector;
struct HashID {
    uint64_t hash64 = 0;
    uint32_t fieldID = 0;
    friend bool operator==(const HashID& lhs, const HashID& rhs)
    {
        return std::tie(lhs.hash64, lhs.fieldID) == std::tie(rhs.hash64, rhs.fieldID);
    }
    friend bool operator<(const HashID& lhs, const HashID& rhs)
    {
        return std::tie(lhs.hash64, lhs.fieldID) < std::tie(rhs.hash64, rhs.fieldID);
    }
};

namespace AST {
static std::unordered_map<ASTKind, std::string> ASTKIND_TO_STRING_MAP{
#define ASTKIND(KIND, VALUE, NODE, SIZE) {ASTKind::KIND, VALUE},
#include "cangjie/AST/ASTKind.inc"
#undef ASTKIND
};

class SymbolApi {
public:
    static HashID NextHashID(uint64_t fileHash)
    {
        ids++;
        HashID hashIDs{
            .hash64 = fileHash,
            .fieldID = ids
        };
        return hashIDs;
    }
    static void ResetID()
    {
        ids = -1u;
    }

private:
    inline static std::atomic_uint32_t ids = -1u;
};

struct Symbol {
    const std::string name;               /**< Symbol name. */
    Symbol* const id{nullptr};            /**< Symbol id. */
    Ptr<Node> const node{nullptr};            /**< AST node. */
    const HashID hashID{0, 0};            /**< Combine file content hash id and symbol id. */
    const uint32_t scopeLevel{0};         /**< Scope level, toplevel scope is 0. */
    const std::string scopeName;          /**< Managed by ScopeManager. */
    const ASTKind astKind{ASTKind::NODE}; /**< AST kind, for quick filter. */
    Ptr<Decl> target{nullptr};                /**< Target for all ref symbol. */
    bool invertedIndexBeenDeleted{false}; /**< Mark whether inverted index has been deleted. */
    bool isNeedExposedToInterop{false};   /**< CJinterop need expose symbol for target language */
    void UnbindTarget()
    {
        target = nullptr;
    }
private:
    // Only allow 'Cangjie::Collector' to create symbol.
    friend class Cangjie::Collector;
    Symbol(uint64_t fileHash, std::string name, Node& src, uint32_t scopeLevel, std::string scopeName)
        : name(std::move(name)), id(this), node(&src), hashID(SymbolApi::NextHashID(fileHash)),
          scopeLevel(scopeLevel), scopeName(std::move(scopeName)), astKind(src.astKind)
    {
    }
    Symbol(const Symbol& sym) = delete;
};
} // namespace AST
} // namespace Cangjie
#endif // CANGJIE_AST_SYMBOL_H
