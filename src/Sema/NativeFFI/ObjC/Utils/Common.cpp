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
#include "ASTFactory.h"
#include "TypeMapper.h"
#include "cangjie/AST/Clone.h"

namespace Cangjie::Interop::ObjC {
using namespace Cangjie::AST;

namespace {

Ptr<ClassDecl> GetMirrorSuperClass(const ClassLikeDecl& target)
{
    if (auto classDecl = DynamicCast<const ClassDecl*>(&target)) {
        auto superClass = classDecl->GetSuperClassDecl();
        if (superClass && TypeMapper::IsObjCMirror(*superClass->ty)) {
            return superClass;
        }
    }

    return nullptr;
}

Ptr<Decl> FindMirrorMember(const std::string_view& mirrorMemberIdent, const InheritableDecl& target)
{
    for (auto memberDecl : target.GetMemberDeclPtrs()) {
        if (memberDecl->identifier == mirrorMemberIdent) {
            return memberDecl;
        }
    }

    return Ptr<Decl>(nullptr);
}

/**
    * @brief Generates a synthetic function stub based on an existing function declaration.
    *
    * This function creates a clone of the provided function declaration (fd),
    * replaces its outerDecl to synthetic class, and then inserts the
    * modified function declaration into the specified synthetic class declaration.
    *
    * @param synthetic The class declaration where the cloned function stub will be inserted.
    * @param fd The original function declaration that will be cloned and modified.
    */
void GenerateSyntheticClassFuncStub(ClassDecl& synthetic, FuncDecl& fd)
{
    OwnedPtr<FuncDecl> funcStub = ASTCloner::Clone(Ptr(&fd));

    // remove foreign anno from cloned func decl
    for (auto it = funcStub->annotations.begin(); it != funcStub->annotations.end(); ++it) {
        if ((*it)->kind == AnnotationKind::FOREIGN_NAME) {
            funcStub->annotations.erase(it);
            break;
        }
    }

    funcStub->outerDecl = Ptr(&synthetic);
    synthetic.body->decls.emplace_back(std::move(funcStub));
}

void GenerateSyntheticClassPropStub([[maybe_unused]] ClassDecl& synthetic, [[maybe_unused]] PropDecl& pd)
{
    auto propStub = ASTCloner::Clone(Ptr(&pd));

    // remove foreign anno from cloned func decl
    for (auto it = propStub->annotations.begin(); it != propStub->annotations.end(); ++it) {
        if ((*it)->kind == AnnotationKind::FOREIGN_NAME) {
            propStub->annotations.erase(it);
            break;
        }
    }

    propStub->outerDecl = Ptr(&synthetic);
    synthetic.body->decls.emplace_back(std::move(propStub));
}
    
} // namespace

bool HasMirrorSuperClass(const ClassLikeDecl& target)
{
    return GetMirrorSuperClass(target) != nullptr;
}

bool HasMirrorSuperInterface(const ClassLikeDecl& target)
{
    for (auto parentTy : target.GetSuperInterfaceTys()) {
        if (TypeMapper::IsObjCMirror(*parentTy)) {
            return true;
        }
    }

    return false;
}

Ptr<VarDecl> GetNativeVarHandle(const ClassDecl& target)
{
    CJC_ASSERT(TypeMapper::IsObjCMirror(*target.ty) || TypeMapper::IsObjCImpl(*target.ty) ||
        TypeMapper::IsSyntheticWrapper(target) || TypeMapper::IsObjCFwdClass(target));

    auto mirrorSuperClass = GetMirrorSuperClass(target);
    if (mirrorSuperClass != nullptr) {
        return GetNativeVarHandle(*mirrorSuperClass);
    }

    return As<ASTKind::VAR_DECL>(FindMirrorMember(NATIVE_HANDLE_IDENT, target));
}

bool IsStaticInitMethod(const Node& node)
{
    const auto fd = DynamicCast<const FuncDecl*>(&node);
    if (!fd) {
        return false;
    }

    return fd->TestAttr(Attribute::OBJ_C_INIT);
}

Ptr<FuncDecl> GetNativeHandleGetter(const ClassLikeDecl& target)
{
    CJC_ASSERT(TypeMapper::IsObjCMirror(*target.ty) || TypeMapper::IsObjCImpl(*target.ty) ||
        TypeMapper::IsSyntheticWrapper(target) || TypeMapper::IsObjCFwdClass(target));

    auto mirrorSuperClass = GetMirrorSuperClass(target);
    if (mirrorSuperClass != nullptr) {
        return GetNativeHandleGetter(*mirrorSuperClass);
    }

    return As<ASTKind::FUNC_DECL>(FindMirrorMember(NATIVE_HANDLE_GETTER_IDENT, target));
}

Ptr<ClassDecl> GetSyntheticWrapper(const ImportManager& importManager, const ClassLikeDecl& target)
{
    CJC_ASSERT(TypeMapper::IsObjCMirror(*target.ty));
    auto* synthetic =
        importManager.GetImportedDecl<ClassDecl>(target.fullPackageName, target.identifier + SYNTHETIC_CLASS_SUFFIX);

    CJC_NULLPTR_CHECK(synthetic);
    CJC_ASSERT(IsSyntheticWrapper(*synthetic));

    return Ptr(synthetic);
}

Ptr<FuncDecl> GetFinalizer(const ClassDecl& target)
{
    for (auto member : target.GetMemberDeclPtrs()) {
        if (member->TestAttr(Attribute::FINALIZER)) {
            return StaticAs<ASTKind::FUNC_DECL>(member);
        }
    }

    return nullptr;
}

bool IsSyntheticWrapper(const AST::Decl& decl)
{
    return TypeMapper::IsSyntheticWrapper(*decl.ty);
}

void GenerateSyntheticClassAbstractMemberImplStubs(ClassDecl& synthetic, const MemberMap& members)
{
    for (const auto& idMemberSignature : members) {
        const auto& signature = idMemberSignature.second;

        // only abstract functions must be inside synthetic class
        if (!signature.decl->TestAttr(Attribute::ABSTRACT)) {
            continue;
        }

        switch (signature.decl->astKind) {
            case ASTKind::FUNC_DECL:
                GenerateSyntheticClassFuncStub(synthetic, *StaticAs<ASTKind::FUNC_DECL>(signature.decl));
                break;
            case ASTKind::PROP_DECL:
                GenerateSyntheticClassPropStub(synthetic, *StaticAs<ASTKind::PROP_DECL>(signature.decl));
                break;
            default:
                continue;
        }
    }
}

} // namespace Cangjie::Interop::ObjC
