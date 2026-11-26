// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the ArkTSFunc<F> construction rewriting and the
 * dedicated constraint diagnostics (B4b): the explicit construction is
 * lowered at the use site onto the carrier's JSValue wrapping constructor,
 * and a failed function-type constraint routes the position to its own
 * diagnostic instead of the generic unsupported-type message. The lowering
 * requires the file to carry an @ArkTS annotation: the generated glue
 * resolves the interop context through the compiler-injected imports, the
 * same rule every other generated-body site follows.
 */

#ifndef CANGJIE_PRIVATE_SEMA_ARKTS_UTILS_FUNC_VALUE_SOURCE_H
#define CANGJIE_PRIVATE_SEMA_ARKTS_UTILS_FUNC_VALUE_SOURCE_H

#include "cangjie/AST/Node.h"

namespace Cangjie {
class DiagnosticEngine;
namespace Native::FFI::ArkTS {
// Report the dedicated diagnostic for a signature position spelled with the
// package-pinned ArkTSFunc<F> carrier whose function-type constraint failed
// (F is not a function type, a position of F is not ArkTS-compatible, or F
// mentions an enclosing declaration's type parameter). Returns true when a
// diagnostic was emitted; every other position returns false and keeps the
// generic unsupported-type or deferral routing of the caller.
bool DiagnoseArkTSFuncCarrierPosition(DiagnosticEngine& diag, AST::Node& position, Ptr<AST::Ty> ty);
} // namespace Native::FFI::ArkTS
} // namespace Cangjie
#endif
