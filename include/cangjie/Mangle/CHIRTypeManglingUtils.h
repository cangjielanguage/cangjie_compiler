// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file provides some utils for CHIR type mangling.
 */

#ifndef CANGJIE_MANGLE_CHIRTYPEMANGLINGUTILS_H
#define CANGJIE_MANGLE_CHIRTYPEMANGLINGUTILS_H

#include "cangjie/CHIR/IR/Type/Type.h"
#include "cangjie/Mangle/MangleUtils.h"

namespace Cangjie::CHIR {
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
inline const std::string MANGLE_GENERIC_PREFIX = "G";
inline const std::string MANGLE_ARRAY_SLICE_PREFIX = "S_";
inline const std::string MANGLE_CLOSURE_STR = "Closure";
inline const std::string MANGLE_UNIT_STR = "Unit";
inline const std::string MANGLE_CSTRING_STR = "CString";
inline const std::string MANGLE_RAWARR_STR = "RawArray";
inline const std::string MANGLE_VARR_STR = "VArray";
inline const std::string MANGLE_CPTR_STR = "CPointer";
inline const std::string MANGLE_TUPLE_STR = "Tuple";
inline const std::string MANGLE_BOX_STR = "Box";
#endif

/**
 * @brief Get identifier without prefix '_C'.
 *
 * @param identifier The substring identifier.
 * @return std::string The mangled name without prefix '_C'.
 */
inline std::string StripCangjiePrefix(std::string identifier)
{
    if (!identifier.empty()) {
        return identifier.substr(MANGLE_PREFIX_LEN);
    }
    return identifier;
}

/**
 * @brief Helper function for mangling chir type name.
 *
 * @param t The chir type.
 * @return std::string The mangled chir type name.
 */
std::string MangleType(const Cangjie::CHIR::Type& t);

/**
 * @brief Helper function for mangling chir type name.
 *
 * @param t The chir type to be mangled.
 * @param genericsTypeStack The generic type vector.
 * @return std::string The mangled chir type name.
 */
std::string MangleType(const Cangjie::CHIR::Type& t, const std::vector<std::string>& genericsTypeStack);

/**
 * @brief Generate mangled name for primitive type.
 *
 * @param kind The chir type kind.
 * @return std::string The mangled chir primitive type name.
 */
std::string ManglePrimitive(const Cangjie::CHIR::Type::TypeKind& kind);

/**
 * @brief Generate mangled name for type args qualified.
 *
 * @param typeArgs The type args vector to be printed.
 * @param forNameFieldOfTi For name field of introType.
 * @return std::string The mangled chir type args qualified name.
 */
std::string GetTypeArgsQualifiedName(const std::vector<Type*>& typeArgs, bool forNameFieldOfTi = false);

/**
 * @brief Generate mangled name for type qualified.
 *
 * @param t The chir type to be mangled.
 * @param forNameFieldOfTi For name field of introType.
 * @return std::string The mangled chir type qualified name.
 */
std::string GetTypeQualifiedName(const CHIR::Type& t, bool forNameFieldOfTi = false);

/**
 * @brief Get identifier for custom type.
 *
 * @param type The chir custom type.
 * @return std::string The mangled chir custom type name.
 */
std::string GetCustomTypeIdentifier(const CHIR::CustomType& type);

/**
 * @brief Get identifier for custom type deference.
 *
 * @param def The chir custom type deference.
 * @return std::string The mangled chir custom type deference name.
 */
std::string GetCustomTypeDefIdentifier(const CHIR::CustomTypeDef& def);
} // namespace Cangjie::CHIR
#endif // CANGJIE_MANGLE_CHIRTYPEMANGLINGUTILS_H
