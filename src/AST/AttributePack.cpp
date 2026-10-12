// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * Implementation of AttributePack class.
 */

#include "cangjie/AST/AttributePack.h"
#include "cangjie/Utils/Macros.h"

using namespace Cangjie;
using namespace Cangjie::AST;

namespace {
SUPPRESS_WARNING("-Wdeprecated-declarations")
// Define ATTR2STR mapping
const std::unordered_map<AST::Attribute, std::string> ATTR2STR{
    {AST::Attribute::IN_REFERENCE_CYCLE, "IN_REFERENCE_CYCLE"},
    {AST::Attribute::UNREACHABLE, "UNREACHABLE"},
    {AST::Attribute::IMPLICIT_USED, "IMPLICIT_USED"},
    {AST::Attribute::IMPORTED, "IMPORTED"},
    {AST::Attribute::GLOBAL, "GLOBAL"},
    {AST::Attribute::INITIALIZED, "INITIALIZED"},
    {AST::Attribute::COMPILER_ADD, "COMPILER_ADD"},
    {AST::Attribute::GENERIC, "GENERIC"},
    {AST::Attribute::DEFAULT, "DEFAULT"},
    {AST::Attribute::STATIC, "STATIC"},
    {AST::Attribute::PUBLIC, "PUBLIC"},
    {AST::Attribute::PRIVATE, "PRIVATE"},
    {AST::Attribute::PROTECTED, "PROTECTED"},
    {AST::Attribute::EXTERNAL, "EXTERNAL"},
    {AST::Attribute::INTERNAL, "INTERNAL"},
    {AST::Attribute::OVERRIDE, "OVERRIDE"},
    {AST::Attribute::REDEF, "REDEF"},
    {AST::Attribute::ABSTRACT, "ABSTRACT"},
    {AST::Attribute::SEALED, "SEALED"},
    {AST::Attribute::OPEN, "OPEN"},
    {AST::Attribute::OPERATOR, "OPERATOR"},
    {AST::Attribute::FOREIGN, "FOREIGN"},
    {AST::Attribute::UNSAFE, "UNSAFE"},
    {AST::Attribute::MUT, "MUT"},
    {AST::Attribute::HAS_BROKEN, "HAS_BROKEN"},
    {AST::Attribute::IN_STRUCT, "IN_STRUCT"},
    {AST::Attribute::IN_EXTEND, "IN_EXTEND"},
    {AST::Attribute::IN_ENUM, "IN_ENUM"},
    {AST::Attribute::IN_CLASSLIKE, "IN_CLASSLIKE"},
    {AST::Attribute::IN_MACRO, "IN_MACRO"},
    {AST::Attribute::PRIMARY_CONSTRUCTOR, "PRIMARY_CONSTRUCTOR"},
    {AST::Attribute::CONSTRUCTOR, "CONSTRUCTOR"},
    {AST::Attribute::ENUM_CONSTRUCTOR, "ENUM_CONSTRUCTOR"},
    {AST::Attribute::FINALIZER, "FINALIZER"},
    {AST::Attribute::IS_CLONED_SOURCE_CODE, "IS_CLONED_SOURCE_CODE"},
    {AST::Attribute::IS_CAPTURE, "IS_CAPTURE"},
    {AST::Attribute::IN_CORE, "IN_CORE"},
    {AST::Attribute::NEED_AUTO_BOX, "NEED_AUTO_BOX"},

    // deprecated: Redundant with Node->curMacroCall; will be removed in the future.
    {AST::Attribute::MACRO_EXPANDED_NODE, "MACRO_EXPANDED_NODE(deprecated)"},

    {AST::Attribute::MACRO_FUNC, "MACRO_FUNC"},
    {AST::Attribute::MACRO_INVOKE_FUNC, "MACRO_INVOKE_FUNC"},
    {AST::Attribute::MACRO_INVOKE_BODY, "MACRO_INVOKE_BODY"},
    {AST::Attribute::LEFT_VALUE, "LEFT_VALUE"},
    {AST::Attribute::C, "C"},
    {AST::Attribute::SRC_IMPORTED, "SRC_IMPORTED"},
    {AST::Attribute::JAVA_APP, "JAVA_APP"},
    {AST::Attribute::JAVA_EXT, "JAVA_EXT"},
    {AST::Attribute::STD_CALL, "STD_CALL"},
    {AST::Attribute::NO_MANGLE, "NO_MANGLE"},
    {AST::Attribute::INITIALIZATION_CHECKED, "INITIALIZATION_CHECKED"},
    {AST::Attribute::IS_BROKEN, "IS_BROKEN"},
    {AST::Attribute::GENERIC_INSTANTIATED, "GENERIC_INSTANTIATED"},
    {AST::Attribute::HAS_INITIAL, "HAS_INITIAL"},
    {AST::Attribute::IS_VOLATILE, "IS_VOLATILE"},
    {AST::Attribute::NUMERIC_OVERFLOW, "NUMERIC_OVERFLOW"},
    {AST::Attribute::INTRINSIC, "INTRINSIC"},
    {AST::Attribute::TOOL_ADD, "TOOL_ADD"},
    {AST::Attribute::IS_CHECK_VISITED, "IS_CHECK_VISITED"},
    {AST::Attribute::INCRE_COMPILE, "INCRE_COMPILE"},
    {AST::Attribute::SIDE_EFFECT, "SIDE_EFFECT"},
    {AST::Attribute::IMPLICIT_ADD, "IMPLICIT_ADD"},
    {AST::Attribute::MAIN_ENTRY, "MAIN_ENTRY"},
    {AST::Attribute::GENERATED_TO_MOCK, "GENERATED_TO_MOCK"},
    {AST::Attribute::IS_OUTERMOST, "IS_OUTERMOST"},
    {AST::Attribute::IS_ANNOTATION, "IS_ANNOTATION"},
    {AST::Attribute::NO_REFLECT_INFO, "NO_REFLECT_INFO"},
    {AST::Attribute::MOCK_SUPPORTED, "MOCK_SUPPORTED"},
    {AST::Attribute::OPEN_TO_MOCK, "OPEN_TO_MOCK"},
    {AST::Attribute::INTERFACE_IMPL, "INTERFACE_IMPL"},
    {AST::Attribute::FOR_TEST, "FOR_TEST"},
    {AST::Attribute::CONTAINS_MOCK_CREATION_CALL, "CONTAINS_MOCK_CREATION_CALL"},
    {AST::Attribute::COMMON, "COMMON"},
    {AST::Attribute::FROM_COMMON_PART, "FROM_COMMON_PART"},
    {AST::Attribute::COMMON_NON_EXHAUSTIVE, "COMMON_NON_EXHAUSTIVE"},
    {AST::Attribute::SPECIFIC, "SPECIFIC"},
    {AST::Attribute::COMMON_WITH_DEFAULT, "COMMON_WITH_DEFAULT"},
    {AST::Attribute::JAVA_FOREIGN, "JAVA_FOREIGN"},
    {AST::Attribute::JAVA_EXPORT, "JAVA_EXPORT"},
    {AST::Attribute::JAVA_HAS_DEFAULT, "JAVA_HAS_DEFAULT"},
    {AST::Attribute::JAVA_FOREIGN_SYNTHETIC_WRAPPER, "JAVA_FOREIGN_SYNTHETIC_WRAPPER"},
    {AST::Attribute::OBJ_C_FOREIGN, "OBJ_C_FOREIGN"},
    {AST::Attribute::OBJ_C_EXPORT, "OBJ_C_EXPORT"},
    {AST::Attribute::OBJ_C_INIT, "OBJ_C_INIT"},
    {AST::Attribute::OBJ_C_OPTIONAL, "OBJ_C_OPTIONAL"},
    /*
     * deprecated: reverse-direction interop has been removed.
     * This attribute exists for compatibility persistance only and could be considered for removal.
     */
    {AST::Attribute::JAVA_CJ_MAPPING, "JAVA_CJ_MAPPING(deprecated)"},
    /*
     * deprecated: reverse-direction interop has been removed.
     * This attribute exists for compatibility persistance only and could be considered for removal.
     */
    {AST::Attribute::OBJ_C_CJ_MAPPING, "OBJ_C_CJ_MAPPING(deprecated)"},
    /*
     * deprecated: reverse-direction interop has been removed.
     * This attribute exists for compatibility persistance only and could be considered for removal.
     */
    {AST::Attribute::CJ_MIRROR_JAVA_INTERFACE_FWD, "CJ_MIRROR_JAVA_INTERFACE_FWD(deprecated)"},
    {AST::Attribute::DESUGARED_FOREIGN_FIELD, "DESUGARED_FOREIGN_FIELD"},
    {AST::Attribute::HAS_INITED_FIELD, "HAS_INITED_FIELD(deprecated)"},
    {AST::Attribute::OBJ_C_FOREIGN_INTERFACE_HANDLE_WRAPPER, "OBJ_C_FOREIGN_INTERFACE_HANDLE_WRAPPER"},
    /*
     * deprecated: reverse-direction interop has been removed.
     * This attribute exists for compatibility persistance only and could be considered for removal.
     */
    {AST::Attribute::CJ_MIRROR_JAVA_INTERFACE_DEFAULT, "CJ_MIRROR_JAVA_INTERFACE_DEFAULT(deprecated)"},
    /*
     * deprecated: reverse-direction interop has been removed.
     * This attribute exists for compatibility persistance only and could be considered for removal.
     */
    {AST::Attribute::CJ_MIRROR_OBJC_INTERFACE_FWD, "CJ_MIRROR_OBJC_INTERFACE_FWD(deprecated)"},
    {AST::Attribute::ALREADY_LOADED, "ALREADY_LOADED"},
    {AST::Attribute::JAVA_EXPORT_REGISTRY_COMPANION, "JAVA_EXPORT_REGISTRY_COMPANION"},
    {AST::Attribute::DOES_NOT_THROW, "DOES_NOT_THROW"},
    {AST::Attribute::OBJ_C_EXPORT_REGISTRY_COMPANION, "OBJ_C_EXPORT_REGISTRY_COMPANION"},
    {AST::Attribute::OBJ_C_EXPORT_MOVED_MEMBER_PROXY, "OBJ_C_EXPORT_MOVED_MEMBER_PROXY"},
    {AST::Attribute::ARKTS, "ARKTS"},
    {AST::Attribute::AST_ATTR_END, "AST_ATTR_END"},
};
UNSUPPRESS_WARNING()
}

std::vector<AttrSizeType> AttributePack::GetAllIdxOfAttr() const
{
    std::vector<AttrSizeType> enableAttrIdxs;
    for (size_t i = 0; i < attributes.size(); ++i) {
        for (size_t j = 0; j < ATTR_SIZE; ++j) {
            if (attributes[i].test(j)) {
                enableAttrIdxs.emplace_back(i * ATTR_SIZE + j);
            }
        }
    }
    return enableAttrIdxs;
}

std::string AttributePack::ToString() const
{
    if (ATTR2STR.size() != static_cast<size_t>(AST::Attribute::AST_ATTR_END) + 1) {
        return "ATTR2STR has invalid mapping(" + std::to_string(ATTR2STR.size()) +
            "!=" + std::to_string(static_cast<size_t>(AST::Attribute::AST_ATTR_END) + 1) + ")";
    }
    std::vector<AttrSizeType> enableAttrIdxs = GetAllIdxOfAttr();
    std::stringstream ret;
    ret << "[";
    for (auto i : enableAttrIdxs) {
        if (i >= ATTR2STR.size()) {
            ret << "UnknownAttr(" << i << ")";
            continue;
        }
        ret << ATTR2STR.at(static_cast<AST::Attribute>(i));
        if (i != enableAttrIdxs.back()) {
            ret << ", ";
        }
    }
    ret << "]";
    return ret.str();
}
