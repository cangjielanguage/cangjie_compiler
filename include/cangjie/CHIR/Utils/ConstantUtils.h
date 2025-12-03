// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares some utility constants.
 */

#ifndef CANGJIE_CHIR_CONSTANTUTILS_H
#define CANGJIE_CHIR_CONSTANTUTILS_H

#include <string>

namespace Cangjie::CHIR {
inline const std::string FUNC_MANGLE_NAME_MALLOC_CSTRING = "_CNat4LibC13mallocCStringHRNat6StringE";
inline const std::string FUNC_MANGLE_NAME_CSTRING_SIZE = "_CNatXk4sizeHv";
inline const std::string GLOBAL_VALUE_PREFIX = "@"; // identifier prefix
constexpr size_t CLASS_REF_DIM{2};
} // namespace Cangjie::CHIR
#endif // CANGJIE_CHIR_CONSTANTUTILS_H
