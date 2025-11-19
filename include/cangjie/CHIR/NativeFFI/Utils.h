// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares utility functions for NativeFFI logic in CHIR.
 */

#ifndef CANGJIE_CHIR_NATIVE_FFI_UTILS
#define CANGJIE_CHIR_NATIVE_FFI_UTILS

#include "cangjie/CHIR/Type/ClassDef.h"

namespace Cangjie::CHIR::Native::FFI {
    bool IsMirror(const CHIR::ClassDef& classDef);
}

#endif