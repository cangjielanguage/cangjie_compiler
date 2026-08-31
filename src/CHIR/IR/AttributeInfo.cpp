// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/IR/AttributeInfo.h"
#include <iostream>
#include <sstream>
#include <unordered_map>

using namespace Cangjie::CHIR;

namespace {
const std::unordered_map<Attribute, std::string> ATTR_TO_STRING{{Attribute::STATIC, "static"},
    {Attribute::PUBLIC, "public"}, {Attribute::PRIVATE, "private"}, {Attribute::PROTECTED, "protected"},
    {Attribute::ABSTRACT, "abstract"}, {Attribute::VIRTUAL, "virtual"}, {Attribute::OVERRIDE, "override"},
    {Attribute::REDEF, "redef"}, {Attribute::SEALED, "sealed"}, {Attribute::FOREIGN, "foreign"},
    {Attribute::MUT, "mut"}, {Attribute::OPERATOR, "operator"}, {Attribute::CONST, "compileTimeVal"},
    {Attribute::READONLY, "readOnly"}, {Attribute::IMPORTED, "imported"}, {Attribute::NON_RECOMPILE, "nonRecompile"},
    {Attribute::GENERIC_INSTANTIATED, "generic_instantiated"}, {Attribute::INTERNAL, "internal"},
    {Attribute::COMPILER_ADD, "compilerAdd"}, {Attribute::GENERIC, "generic"},
    {Attribute::NO_REFLECT_INFO, "noReflectInfo"}, {Attribute::NO_INLINE, "noInline"},
    {Attribute::NO_DEBUG_INFO, "noDebugInfo"}, {Attribute::UNREACHABLE, "unreachable"},
    {Attribute::NO_SIDE_EFFECT, "noSideEffect"}, {Attribute::FINAL, "final"},
    {Attribute::COMMON, "common"}, {Attribute::SPECIFIC, "specific"},
    {Attribute::SKIP_ANALYSIS, "skip_analysis"}, {Attribute::DESERIALIZED, "deserialized"},
    {Attribute::INITIALIZER, "initializer"},
    {Attribute::UNSAFE, "unsafe"}, {Attribute::JAVA_MIRROR, "javaMirror"}, {Attribute::JAVA_IMPL, "javaImpl"},
    {Attribute::OBJ_C_MIRROR, "objCMirror"}, {Attribute::HAS_INITED_FIELD, "hasInitedField"},
    {Attribute::JAVA_HAS_DEFAULT, "javaHasDefault"}, {Attribute::PREVIOUSLY_DESERIALIZED, "previouslyDeserialized"},
    {Attribute::DEMODE, "demode"}, {Attribute::EXCLAVE, "exclave"}};
} // namespace

std::string AttributeInfo::ToString() const
{
    std::stringstream ss;
    for (int attr = static_cast<int>(Attribute::STATIC); attr < static_cast<int>(Attribute::ATTR_END); attr++) {
        if (TestAttr(static_cast<Attribute>(attr))) {
            ss << "[" << ATTR_TO_STRING.at(static_cast<Attribute>(attr)) << "] ";
        }
    }
    return ss.str();
}

void AttributeInfo::Dump() const
{
    std::cout << ToString() << std::endl;
}