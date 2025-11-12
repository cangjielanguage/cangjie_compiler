// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares MemberSignature struct.
 */

#ifndef CANGJIE_SEMA_MEMBER_SIGNATURE_H
#define CANGJIE_SEMA_MEMBER_SIGNATURE_H

#include "cangjie/AST/Node.h"

namespace Cangjie {

struct MemberSignature {
    Ptr<AST::Decl> decl = nullptr;
    Ptr<AST::Ty> ty = nullptr;
    Ptr<AST::Ty> structTy = nullptr;
    Ptr<AST::ExtendDecl> extendDecl = nullptr; // If the member is a member of another visible extension, it points to the
                                          // extension declaration. Otherwise, it is null.
    std::vector<std::unordered_set<Ptr<AST::Ty>>> upperBounds;
    std::unordered_set<Ptr<const AST::Ty>> inconsistentTypes; // List of the corresponding types came from super-types which
                                                         // are inconsistent
    bool shouldBeImplemented = false;                    // True: if this member has multiple default implementation.
    bool replaceOther = false;                           // True: if this member override others.
    bool isInheritedInterface = false; // True: if current member is implementing inherited interface decl.
};

using MemberMap = std::multimap<std::string, MemberSignature>;

} // namespace Cangjie

#endif
