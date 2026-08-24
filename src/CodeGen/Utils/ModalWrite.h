// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * Decides, at CodeGen time, whether a store to an instance member variable needs a local-mode
 * aware write barrier instead of the ordinary GC write barrier.
 *
 */

#ifndef CANGJIE_CODEGEN_MODAL_WRITE_H
#define CANGJIE_CODEGEN_MODAL_WRITE_H

#include <cstdint>

namespace Cangjie {
namespace CHIR {
class Expression;
class Type;
class CHIRBuilder;
} // namespace CHIR
namespace CodeGen {

/// Which write barrier a store to an instance member variable must use.
enum class ModalWriteKind : uint8_t {
    NONE,        ///< ordinary GC write barrier (llvm.cj.gcwrite.ref)
    MAYBE_LOCAL, ///< llvm.cj.maybe.local.write.ref -- local-aware write for local! / local? fields
    DEMODE,      ///< llvm.cj.demode.write.ref -- demoded field, value is always on the heap
};

/// Returns the barrier kind for the store expression \p store, or NONE when the ordinary GC
/// write barrier (llvm.cj.gcwrite.ref) suffices. \p store must be a StoreElementRef, a Store
/// whose location is a GetElementRef, or -- for non-member stores -- anything else, in which
/// case the result is NONE. Resolves the field path and the effective modal itself using
/// \p builder, which the caller obtains from its IRBuilder2 context.
ModalWriteKind ClassifyModalWrite(Cangjie::CHIR::CHIRBuilder& builder, const Cangjie::CHIR::Expression& store);
} // namespace CodeGen
} // namespace Cangjie

#endif // CANGJIE_CODEGEN_MODAL_WRITE_H
