// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements common utils for Cangjie <-> Objective-C interop.
 */

#include "Common.h"
#include "TypeMapper.h"
#include "ASTFactory.h"

using namespace Cangjie::AST;
using namespace Cangjie::Interop::ObjC;

Ptr<ClassDecl> Cangjie::Interop::ObjC::GetMirrorSuperClass(const ClassLikeDecl& target)
{
    if (auto classDecl = DynamicCast<const ClassDecl*>(&target)) {
        auto superClass = classDecl->GetSuperClassDecl();
        if (superClass && TypeMapper::IsValidObjCMirror(*superClass->ty)) {
            return superClass;
        }
    }

    return nullptr;
}

bool Cangjie::Interop::ObjC::HasMirrorSuperClass(const ClassLikeDecl& target)
{
    return GetMirrorSuperClass(target) != nullptr;
}

Ptr<VarDecl> Cangjie::Interop::ObjC::FindNativeVarHandle(const AST::ClassLikeDecl& target)
{
    CJC_ASSERT(TypeMapper::IsValidObjCMirror(*target.ty) || TypeMapper::IsObjCImpl(*target.ty));

    auto mirrorSuperClass = GetMirrorSuperClass(target);
    if (mirrorSuperClass != nullptr) {
        return FindNativeVarHandle(*mirrorSuperClass);
    }

    return As<ASTKind::VAR_DECL>(FindMirrorMember(ASTFactory::NATIVE_HANDLE_IDENT, target));
}

Ptr<Decl> Cangjie::Interop::ObjC::FindMirrorMember(const std::string_view& mirrorMemberIdent,
    const InheritableDecl& target)
{
    for (auto& memberDecl: target.GetMemberDeclPtrs()) {
        if (memberDecl->identifier == mirrorMemberIdent) {
            return memberDecl;
        }
    }

    return Ptr<Decl>(nullptr);
}

