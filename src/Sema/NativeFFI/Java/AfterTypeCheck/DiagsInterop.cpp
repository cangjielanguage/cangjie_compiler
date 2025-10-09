// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "DiagsInterop.h"
#include "Diags.h"
#include "Utils.h"

namespace Cangjie::Interop::Java {
using namespace Sema;

namespace {
Range MakeJavaImplJavaNameRange(const ClassLikeDecl& decl)
{
    if (!HasPredefinedJavaName(decl)) {
        return MakeRangeForDeclIdentifier(decl);
    }

    for (auto& anno : decl.annotations) {
        if (anno->kind != AnnotationKind::JAVA_IMPL) {
            continue;
        }

        return MakeRange(anno->GetBegin(), decl.identifier.End());
    }
    return MakeRange(decl.identifier.Begin(), decl.identifier.End());
}
} // namespace

void DiagJavaImplRedefinitionInJava(DiagnosticEngine& diag, const ClassLikeDecl& decl, const ClassLikeDecl& prevDecl)
{
    if (decl.TestAttr(Attribute::IS_BROKEN)) {
        return;
    }
    auto prevDeclFqName = GetJavaFQSourceCodeName(prevDecl);
    CJC_ASSERT(GetJavaFQSourceCodeName(decl) == prevDeclFqName);
    auto rangePrev = MakeJavaImplJavaNameRange(prevDecl);
    auto rangeNext = MakeJavaImplJavaNameRange(decl);

    auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_java_impl_redefinition, prevDecl, rangeNext,
        prevDeclFqName);
    builder.AddNote(decl, rangePrev, "'" + prevDeclFqName + "' is previously declared here");
}

void DiagJavaMirrorChildMustBeAnnotated(DiagnosticEngine& diag, const ClassLikeDecl& decl)
{
    SrcIdentifier parentId;

    for (auto& parentType: decl.inheritedTypes) {
        auto pty = parentType->ty;
        if (auto parent = DynamicCast<ClassTy>(pty)) {
            parentId = parent->decl->identifier;
            break;
        }
    }

    diag.DiagnoseRefactor(DiagKindRefactor::sema_java_mirror_subtype_must_be_annotated, decl, parentId);
}

void DiagJavaDeclCannotInheritPureCangjieType(DiagnosticEngine& diag, ClassLikeDecl& decl)
{
    CJC_ASSERT(IsMirror(decl) || IsImpl(decl));

    auto kind = IsMirror(decl) ? DiagKindRefactor::sema_java_mirror_cannot_inherit_pure_cangjie_type
                               : DiagKindRefactor::sema_java_impl_cannot_inherit_pure_cangjie_type;

    auto builder = diag.DiagnoseRefactor(kind, decl);

    for (const auto& superType : decl.inheritedTypes) {
        auto superDecl = Ty::GetDeclOfTy(superType->ty);
        CJC_ASSERT(superDecl);
        if (!IsMirror(*superDecl) && !IsImpl(*superDecl) && !superDecl->ty->IsObject() && !superDecl->ty->IsAny()) {
            builder.AddNote(*superType, "'" + superType->ToString() + "'" + " is not a java-compatible type");
        }
    }
}

void DiagJavaDeclCannotBeExtendedWithInterface(DiagnosticEngine& diag, ExtendDecl& decl)
{
    auto& ty = *decl.extendedType->ty;
    CJC_ASSERT(IsMirror(ty) || IsImpl(ty));
    CJC_ASSERT(!decl.inheritedTypes.empty());
    auto kind = IsMirror(ty) ? DiagKindRefactor::sema_java_mirror_cannot_be_extended_with_interface
                             : DiagKindRefactor::sema_java_impl_cannot_be_extended_with_interface;

    diag.DiagnoseRefactor(kind, decl);
}

} // namespace
