// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the symbol table of CHIR.
 */

#ifndef CANGJIE_CHIR_SYMBOLTABLE_H
#define CANGJIE_CHIR_SYMBOLTABLE_H

#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"
#include "cangjie/CHIR/IR/Value/Value.h"
#include "cangjie/Mangle/CHIRMangler.h"
#include "cangjie/Sema/TypeManager.h"

namespace Cangjie::CHIR {
template <typename T> class AST2CHIRNodeMap {
public:
    AST2CHIRNodeMap()
    {
    }
    ~AST2CHIRNodeMap() = default;
    bool Has(const Cangjie::AST::Node& node) const
    {
        return cache.find(&node) != cache.end();
    }

    void Set(const Cangjie::AST::Node& node, T& chirNode)
    {
        CJC_ASSERT(cache.emplace(&node, &chirNode).second);
    }

    T* Get(const Cangjie::AST::Node& node) const
    {
        auto chirNode = cache.at(&node);
        CJC_NULLPTR_CHECK(chirNode);
        return chirNode;
    }

    T* TryGet(const Cangjie::AST::Node& node) const
    {
        auto it = cache.find(&node);
        if (it != cache.end()) {
            return it->second;
        }
        return nullptr;
    }

    const std::unordered_map<const Cangjie::AST::Node*, T*>& GetALL() const
    {
        return cache;
    }

    void Erase(const Cangjie::AST::Node& node)
    {
        (void)cache.erase(&node);
    }

private:
    std::unordered_map<const Cangjie::AST::Node*, T*> cache;
};
} // namespace Cangjie::CHIR
#endif