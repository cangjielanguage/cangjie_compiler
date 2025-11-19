// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements utility functions for NativeFFI logic in CHIR.
 */

#include "cangjie/CHIR/NativeFFI/Utils.h"

using namespace Cangjie::CHIR;
using namespace Cangjie::CHIR::Native::FFI;

namespace {
bool IsObjCMirror(const ClassDef& classDef)
{
    return classDef.TestAttr(Attribute::OBJ_C_MIRROR);
}

bool IsJavaMirror(const ClassDef& classDef)
{
    return classDef.TestAttr(Attribute::JAVA_MIRROR);
}
} // namespace

bool Cangjie::CHIR::Native::FFI::IsMirror(const ClassDef& classDef)
{
    return IsObjCMirror(classDef) || IsJavaMirror(classDef);
}