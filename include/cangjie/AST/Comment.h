// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares cache for node.
 */
#ifndef CANGJIE_AST_COMMENT_H
#define CANGJIE_AST_COMMENT_H

#include <unordered_map>
#include <cstdint>
#include "cangjie/Lex/Token.h"
namespace Cangjie::AST {

enum class CommentStyle : uint8_t {
    LEAD_LINE,
    TRAIL_CODE,
    OTHER,
};

enum class CommentKind : uint8_t {
    LINE,
    BLOCK,
    DOCUMENT, // block comment started with "/**" e.g. /**xxxx*/.  exclude: start with "/***", empty comment "/**/".
};

struct Comment {
    CommentStyle style;
    CommentKind kind;
    Token info;
    std::string ToString() const;
};

/// e.g.
/// // line 1
/// // line 2
/// main() { /*block 1*/ // line 3
///     // line 4
///     // line 6
/// return 0
/// }
// group 1: line 1, line 2, group 2: block 1, line 3, group 3: line 4, line6
struct CommentGroup {
    std::vector<Comment> cms;
    bool IsEmpty() const
    {
        return cms.empty();
    }
    std::string ToString() const;
};

///
/// Comments are classified into leadingComments, innerComments and trailingComments based on the location relationship
/// among nodes and comments, For details, see the description in AttachComment.cpp.
/// e.g.
/// /** c0 lead classDecl of class A */
/// class A { // c1 lead var decl of a
///     // c2 lead varDecl of a
///     var a = 1 // c3 trail varDecl of a
///     // c4 trail varDecl of a
/// } // c5 trail classDecl of A
/// // c6 lead funcDecl of foo
/// func foo(/* c7 inner funcParamList of foo */)
/// {
/// }
/// // c8 trail funcDecl of foo
///
/// main() {
///    0
/// }
///
struct CommentGroups {
    std::vector<CommentGroup> leadingComments;
    std::vector<CommentGroup> innerComments;
    std::vector<CommentGroup> trailingComments;
    bool IsEmpty() const
    {
        return leadingComments.empty() && innerComments.empty() && trailingComments.empty();
    }
    std::string ToString() const;
};

/**
 * all comment groups in the token stream and location-related information
 */
struct CommentGroupsLocInfo {
    std::vector<CommentGroup> commentGroups;
    // key: groupIndex value: preTokenIndex in tokenStream(ignore nl, semi, comment)
    std::unordered_map<size_t, size_t> cgPreInfo;
    // key: groupIndex value: followTokenIndex in tokenStream(ignore nl, comment, end)
    std::unordered_map<size_t, size_t> cgFollowInfo;
    const std::vector<Token>& tkStream;
};
}

#endif // CANGJIE_AST_COMMENT_H