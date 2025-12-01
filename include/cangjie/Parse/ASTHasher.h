// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 */
#ifndef CANGJIE_ASTHASHER_H
#define CANGJIE_ASTHASHER_H
#include <unordered_map>

#include "cangjie/AST/Node.h"
#include "cangjie/Lex/Lexer.h"

namespace Cangjie::AST {
class ASTHasher {
public:
    // pass global options to ASTHasher before creating any instance of ASTHasher
    static void Init(const GlobalOptions& op);

    using hash_type = size_t;
    static hash_type HashDeclBody(Ptr<const AST::Decl> decl);
    static hash_type HashDeclSignature(Ptr<const AST::Decl> decl);
    // hash package specs and import specs of package
    static hash_type HashSpecs(const Package& pk);

    static inline size_t CombineHash(const size_t acc, const size_t value)
    {
        // 6, 2 are specific constants in the hash algorithm.
        return acc ^ (value + 0x9e3779b9 + (acc << 6) + (acc >> 2));
    }

    // incr 2.0
    static hash_type SigHash(const AST::Decl& decl);
    static hash_type SrcUseHash(const AST::Decl& decl);
    // hashAnnos true to consider/ignore the annotations in the hash computation
    static hash_type BodyHash(const AST::Decl& decl, const std::pair<bool, bool>& srcInfo, bool hashAnnos = true);
    static hash_type ImportedDeclBodyHash(const AST::Decl& decl);
    static hash_type VirtualHash(const Decl& decl);
    static hash_type HashMemberAPIs(std::vector<Ptr<const Decl>>&& memberAPIs);
};
} // namespace Cangjie::AST

#endif // CANGJIE_ASTHASHER_H
