// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the entrypoint to handle Objective-C mirrors and theirs subtypes desugaring.
 */

#ifndef CANGJIE_SEMA_DESUGAR_OBJ_C_INTEROP_DESUGAR_H
#define CANGJIE_SEMA_DESUGAR_OBJ_C_INTEROP_DESUGAR_H

#include "Interop/Context.h"

namespace Cangjie::Interop::ObjC {

void Desugar(InteropContext&& ctx);

} // namespace Cangjie::Interop::ObjC

#endif // CANGJIE_SEMA_DESUGAR_OBJ_C_INTEROP_DESUGAR_H