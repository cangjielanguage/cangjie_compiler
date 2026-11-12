// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_PRIVATE_NATIVE_FFI_ARKTS_PLACEHOLDERBINDING_H
#define CANGJIE_PRIVATE_NATIVE_FFI_ARKTS_PLACEHOLDERBINDING_H

#include <string>
#include <unordered_map>

#include "cangjie/AST/Node.h"

namespace Cangjie::Native::FFI::ArkTS {
// Bind one generated type placeholder (a RefType whose identifier is a key of
// classRefs) to its class declaration. The class is recorded in pendingRefs,
// not the long-lived support map: the caller merges pendingRefs only after the
// wrapper body passes BindArkTSSupportReferences, so a rejected body leaves no
// key pointing at a dropped node. A node whose identifier is not a generated
// placeholder is left untouched for the surrounding walker.
inline void BindArkTSRefTypePlaceholder(AST::RefType& refType,
    const std::unordered_map<std::string, Ptr<AST::Decl>>& classRefs,
    std::unordered_map<Ptr<const AST::Node>, Ptr<AST::Decl>>& pendingRefs)
{
    auto found = classRefs.find(refType.ref.identifier.Val());
    if (found == classRefs.end()) {
        return;
    }
    refType.ref.identifier = found->second->identifier;
    refType.ref.target = found->second;
    refType.ref.targets = {found->second};
    refType.SetTy(found->second->GetTy());
    pendingRefs[&refType] = found->second;
}
} // namespace Cangjie::Native::FFI::ArkTS
#endif
