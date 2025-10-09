// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares inheritance related checks for JavaFFI features
 */
#ifndef CANGJIE_SEMA_NATIVE_FFI_JAVA_TYPE_CHECK_INHERITANCE_CHECKER_H
#define CANGJIE_SEMA_NATIVE_FFI_JAVA_TYPE_CHECK_INHERITANCE_CHECKER_H

#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/AST/Node.h"
#include "cangjie/Sema/TypeManager.h"

#include "InheritanceChecker/StructInheritanceChecker.h"

namespace Cangjie::Interop::Java {

/** Checks @ForeignName annotation usage and propagates it */
void CheckForeignName(DiagnosticEngine& diag, TypeManager& typeManager, const MemberSignature& parent,
    const MemberSignature& child, const Decl& checkingDecl);

} // namespace Cangjie::Interop::Java

#endif // CANGJIE_SEMA_NATIVE_FFI_JAVA_TYPE_CHECK_INHERITANCE_CHECKER_H
