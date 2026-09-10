// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares cache for type check.
 */

#ifndef CANGJIE_AST_CACHE_H
#define CANGJIE_AST_CACHE_H

#include "cangjie/AST/Types.h"
#include "cangjie/Basic/DiagnosticEngine.h"

namespace Cangjie::AST {

using TargetCache = std::pair<Ptr<AST::Decl>, Ptr<AST::Decl>>;

struct CacheEntry {
    bool successful = false;
    AST::ModalTy result = {};
    DiagnosticCache diags;
    TargetCache targets;
};

struct CacheKey {
    AST::ModalTy target;
    bool isDesugared;
    DiagnosticCache::DiagCacheKey diagKey;
    bool operator==(const CacheKey& b) const;
};

struct CacheKeyHash {
    size_t operator()(const CacheKey& key) const
    {
        auto v = std::hash<AST::ModalTy>()(key.target);
        v = hash_combine(v, key.isDesugared);
        v = hash_combine(v, key.diagKey);
        return v;
    }
};

/* type check cache for one AST node */
struct TypeCheckCache {
    std::unordered_map<CacheKey, CacheEntry, CacheKeyHash> synCache;
    std::unordered_map<CacheKey, CacheEntry, CacheKeyHash> chkCache;
    std::optional<CacheKey> lastKey{};
};

// member signature information available by just syntax check
struct MemSig {
    std::string id;
    bool isVarOrProp;
    size_t arity = 0; // arity in case of member function, otherwise 0; variadic arg not considered
    size_t genArity = 0; // number of possible explicit generic args in case of member function, otherwise 0
                         // note that all generic func can possibly have 0 explicit gen args
    bool operator==(const MemSig& b) const;
};

struct MemSigHash {
    size_t operator()(const MemSig& sig) const
    {
        auto v = std::hash<std::string>()(sig.id);
        v = hash_combine(v, sig.isVarOrProp);
        v = hash_combine(v, sig.arity);
        v = hash_combine(v, sig.genArity);
        return v;
    }
};

// Collect and restore necessary target decls in the sub-tree.
// Most targets are needed only after post-check, when they are filled by the normal procedure.
// Currently, this cache is only for checking enum constructor without type args.
TargetCache CollectTargets(const AST::Node& node);
void RestoreTargets(AST::Node& node, const TargetCache& targets);
} // namespace Cangjie

#endif
