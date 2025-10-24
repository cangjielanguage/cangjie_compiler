// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares utility class that process inheritance of structured types.
 */

#ifndef CANGJIE_SEMA_OBJ_C_UTILS_STRUCT_INHERITANCE_CHECKER_IMPL_H
#define CANGJIE_SEMA_OBJ_C_UTILS_STRUCT_INHERITANCE_CHECKER_IMPL_H

#include "Common.h"
#include "InheritanceChecker/StructInheritanceChecker.h"
#include "cangjie/AST/Node.h"
#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Sema/TypeManager.h"

namespace Cangjie::Interop::ObjC {

void CheckForeignName(DiagnosticEngine& diag, TypeManager& typeManager, const MemberSignature& parent,
    const MemberSignature& child, const AST::Decl& checkingDecl);
void GenerateSyntheticClassMemberStubs(
    AST::ClassDecl& synthetic, const MemberMap& interfaceMembers, const MemberMap& instanceMembers);

} // namespace Cangjie::Interop::ObjC

#endif // CANGJIE_SEMA_OBJ_C_UTILS_STRUCT_INHERITANCE_CHECKER_IMPL_H
