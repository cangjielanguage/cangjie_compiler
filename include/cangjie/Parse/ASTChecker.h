// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 */
#ifndef CANGJIE_ASTCHECKER_H
#define CANGJIE_ASTCHECKER_H
#include <unordered_map>

#include "cangjie/AST/Node.h"
#include "cangjie/Lex/Lexer.h"

#define AST_NULLPTR_CHECK(node, f)                                                                                     \
    do {                                                                                                               \
        if ((f) == nullptr) {                                                                                          \
            CollectInfo(node, #f);                                                                                     \
        }                                                                                                              \
    } while (0)

#define ATTR_NULLPTR_CHECK(node, f)                                                                                    \
    do {                                                                                                               \
        if ((f) == nullptr) {                                                                                          \
            CollectInfo(node, #f);                                                                                     \
        }                                                                                                              \
    } while (0)

#define ZERO_POSITION_CHECK(node, pos)                                                                                 \
    do {                                                                                                               \
        if ((pos).IsZero()) {                                                                                          \
            /** CollectInfo(node, #pos);            */                                                                 \
        }                                                                                                              \
    } while (0)

#define VEC_AST_NULLPTR_CHECK(node, vec)                                                                               \
    do {                                                                                                               \
        for (auto& f : (vec)) {                                                                                        \
            if (f == nullptr) {                                                                                        \
                CollectInfo(node, #vec);                                                                               \
            }                                                                                                          \
        }                                                                                                              \
    } while (0)

#define VEC_ZERO_POS_CHECK(node, vec)                                                                                  \
    do {                                                                                                               \
        for (auto& pos : (vec)) {                                                                                      \
            if (pos.IsZero()) {                                                                                        \
                /** CollectInfo(node, #vec);            */                                                             \
            }                                                                                                          \
        }                                                                                                              \
    } while (0)

#define VEC_EMPTY_STRING_CHECK(node, vec)                                                                              \
    do {                                                                                                               \
        for (auto& f : (vec)) {                                                                                        \
            if (f.empty()) {                                                                                           \
                /** CollectInfo(node, #vec);            */                                                             \
            }                                                                                                          \
        }                                                                                                              \
    } while (0)

#define EMPTY_STRING_CHECK(node, str)                                                                                  \
    do {                                                                                                               \
        if ((str).empty()) {                                                                                           \
            /** CollectInfo(node, #str);     */                                                                        \
        }                                                                                                              \
    } while (0)

#define EMPTY_IDENTIFIER_CHECK(node, id)                                                                               \
    do {                                                                                                               \
        if ((id).Empty() || (id).ZeroPos()) {                                                                          \
            /** CollectInfo(node, #id);  */                                                                            \
        }                                                                                                              \
    } while (0)
#define EMPTY_VEC_CHECK(node, vec)                                                                                     \
    do {                                                                                                               \
        if ((vec).empty()) {                                                                                           \
            /** CollectInfo(node, #vec);  */                                                                           \
        }                                                                                                              \
    } while (0)

namespace Cangjie::AST {
class ASTChecker {

public:
    void CheckAST(Node& node);
    void CheckAST(const std::vector<OwnedPtr<Package>>& pkgs);
    void CheckBeginEnd(Ptr<Node> node);
    void CheckBeginEnd(const std::vector<OwnedPtr<Package>>& pkgs);

private:
    std::unordered_map<ASTKind, std::function<void(ASTChecker*, Ptr<Node>)>> checkFuncMap{
#define ASTKIND(KIND, VALUE, NODE, SIZE) {ASTKind::KIND, &ASTChecker::Check##NODE},
#include "cangjie/AST/ASTKind.inc"
#undef ASTKIND
    };

// Function declarations of Checking several kinds of Node
#define ASTKIND(KIND, VALUE, NODE, SIZE) void Check##NODE(Ptr<Node> node);
#include "cangjie/AST/ASTKind.inc"
#undef ASTKIND
    
    std::set<std::string> checkInfoSet;
    void CollectInfo(Ptr<Node> node, const std::string& subInfo);

    void CheckInheritableDecl(Ptr<Node> node);
};
} // namespace Cangjie::AST

#endif // CANGJIE_ASTCHECKER_H
