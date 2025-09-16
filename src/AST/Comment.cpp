// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the Comment related structs.
 */
#include "cangjie/AST/Comment.h"

#include "cangjie/Basic/StringConvertor.h"

namespace Cangjie {
std::string AST::CommentGroups::ToString() const
{
    if (IsEmpty()) {
        return "{}";
    }
    std::string str{"{"};
    bool needComma = false;
    if (!leadingComments.empty()) {
        str += "\"leadingComments\":[";
        for (auto cg : leadingComments) {
            if (needComma) {
                str += ", ";
            }
            str += cg.ToString();
            needComma = true;
        }
        str += "]";
    }
    if (!innerComments.empty()) {
        if (needComma) {
            str += ", ";
        }
        needComma = false;
        str += "\"innerComments\":[";
        for (auto cg : innerComments) {
            if (needComma) {
                str += ",";
            }
            str += cg.ToString();
            needComma = true;
        }
        str += "]";
        needComma = true;
    }
    if (!trailingComments.empty()) {
        if (needComma) {
            str += ", ";
        }
        needComma = false;
        str += "\"trailingComments\":[";
        for (auto cg : trailingComments) {
            if (needComma) {
                str += ", ";
            }
            str += cg.ToString();
            needComma = true;
        }
        str += "]";
        needComma = true;
    }
    str += "}";
    return str;
}
std::string AST::CommentGroup::ToString() const
{
    if (IsEmpty()) {
        return "{\"cms\":[]}";
    }
    std::string str{"{\"cms\":["};
    bool needComma = false;
    for (auto c : cms) {
        if (needComma) {
            str += ", ";
        }
        str += c.ToString();
        needComma = true;
    }
    str += "]}";
    return str;
}
std::string AST::Comment::ToString() const
{
    std::string str;
    str += "{\"style\":";
    switch (style) {
        case CommentStyle::LEAD_LINE:
            str += "\"leadLine\"";
            break;
        case CommentStyle::TRAIL_CODE:
            str += "\"trailCode\"";
            break;
        case CommentStyle::OTHER:
            str += "\"other\"";
            break;
    }
    str += ", \"kind\":";
    switch (kind) {
        case CommentKind::LINE:
            str += "\"line\"";
            break;
        case CommentKind::BLOCK:
            str += "\"block\"";
            break;
        case CommentKind::DOCUMENT:
            str += "\"doc\"";
            break;
    }
    str += ", \"info\":\"" + StringConvertor::EscapeToJsonString(info.Value()) + "\"}";
    return str;
}
}