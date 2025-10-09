// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares common utils for Cangjie <-> Objective-C interop.
 * Needs to be structured properly in the future.
 */

#ifndef CANGJIE_SEMA_OBJ_C_UTILS_COMMON_H
#define CANGJIE_SEMA_OBJ_C_UTILS_COMMON_H

#include "cangjie/AST/Node.h"
#include "cangjie/Utils/SafePointer.h"

namespace Cangjie::Interop::ObjC {

/**
 *  Returns native handle field declaration for `target` mirror/impl declaration
 *
 */
Ptr<AST::VarDecl> FindNativeVarHandle(const AST::ClassLikeDecl& target);
Ptr<AST::Decl> FindMirrorMember(const std::string_view& mirrorMemberIdent, const AST::InheritableDecl& target);

Ptr<AST::ClassDecl> GetMirrorSuperClass(const AST::ClassLikeDecl& target);
bool HasMirrorSuperClass(const AST::ClassLikeDecl& target);

} // namespace Cangjie::Interop::ObjC

#endif // CANGJIE_SEMA_OBJ_C_UTILS_COMMON_H
