// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the Mangle Utils.
 */
#ifndef CANGJIE_MANGLE_UTILS_H
#define CANGJIE_MANGLE_UTILS_H

#include <string>
#include <unordered_set>

namespace Cangjie {
inline const std::string MANGLE_CANGJIE_PREFIX = "_C";
inline const std::string MANGLE_NESTED_PREFIX = "N";
inline const std::string MANGLE_SUFFIX = "E";
inline const std::string MANGLE_GLOBAL_VARIABLE_INIT_PREFIX = "GV";
inline const std::string MANGLE_GLOBAL_PACKAGE_INIT_PREFIX = "GP";
inline const std::string MANGLE_GLOBAL_FILE_INIT_PREFIX = "GF";
inline const std::string MANGLE_FUNC_PARA_INIT_PREFIX = "PI";
inline const std::string SPECIAL_NAME_FOR_INIT_FUNCTION = "ii";
inline const std::string SPECIAL_NAME_FOR_INIT_RESET_FUNCTION = "ir";
inline const std::string SPECIAL_NAME_FOR_INIT_LITERAL_FUNCTION = "il";
inline const std::string SPECIAL_NAME_FOR_IMPORTS_INIT_FUNCTION = "fi";
inline const std::string SPECIAL_NAME_FOR_LITERAL_IMPORTS_INIT_FUNCTION = "fl";
inline const std::string SPECIAL_NAME_FOR_INIT_FLAG_RESET_FUNCTION = "if";
inline const std::string SPECIAL_NAME_FOR_GET = "get";
inline const std::string SPECIAL_NAME_FOR_SET = "set";
inline const std::string MANGLE_CFUNC_PREFIX = "FC";
inline const std::string MANGLE_GENERAL_FUNC_PREFIX = "F0";
inline const std::string MANGLE_FILE_ID_PREFIX = "U";
inline const std::string MANGLE_TYPE_STRUCT_PREFIX = "R";
inline const std::string MANGLE_TYPE_CLASS_PREFIX = "C";
inline const std::string MANGLE_TYPE_ENUM_PREFIX = "N";
inline const std::string MANGLE_TYPE_ARRAY_PREFIX = "A";
inline const std::string MANGLE_FUNC_PARAM_TYPE_PREFIX = "H";
inline const std::string MANGLE_VOID_TY_SUFFIX = "v";
inline const std::string MANGLE_COUNT_PREFIX= "K";
inline const std::string MANGLE_LAMBDA_PREFIX = "L";
inline const std::string MANGLE_EXTEND_PREFIX = "X";
inline const std::string USER_MAIN_MANGLED_NAME = "user.main";
inline const std::string MANGLE_GENERIC_PREFIX = "I";
inline const std::string MANGLE_ANONYMOUS_VARIABLE_PREFIX = "0";
inline const std::string MANGLE_COMPRESSED_PREFIX = "Y";
inline const std::string MANGLE_POINTER_PREFIX = "P";
inline const std::string MANGLE_VARRAY_PREFIX = "V";
inline const std::string MANGLE_TUPLE_PREFIX = "T";
inline const std::string MANGLE_GENERIC_TYPE_PREFIX = "G";
inline const std::string MANGLE_AT_PREFIX = "@";
inline const std::unordered_set<char> PRIMITIVE_PREFIX_SET = {
    'n', 'u', 'c', 'b', 'f', 'd', 'a', 's', 'i', 'l', 'q', 'h', 't', 'j', 'm', 'r', 'D', 'v' };

inline const std::string MANGLE_DOLLAR_PREFIX = "$";
inline const std::string MANGLE_WILDCARD_PREFIX = "_";
inline const std::string MANGLE_DOT_PREFIX = ".";
inline const std::string MANGLE_LT_PREFIX = "<";
inline const std::string MANGLE_GT_PREFIX = ">";
inline const std::string MANGLE_LT_COLON_PREFIX = "<:";

inline const size_t MANGLE_PREFIX_LEN = 2;
inline const size_t MANGLE_CHAR_LEN = 1;
inline const size_t MANGLE_SPECIAL_NAME_LEN = 2;
inline const size_t MANGLE_PROP_LEN = 3;
inline const size_t FILE_HASH_LEN = 13;
} // namespace Cangjie
#endif // CANGJIE_MANGLE_UTILS_H
