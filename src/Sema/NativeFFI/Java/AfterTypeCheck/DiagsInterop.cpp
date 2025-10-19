// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "DiagsInterop.h"
#include "Diags.h"
#include "Utils.h"
#include "cangjie/AST/Utils.h"

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

    auto builder =
        diag.DiagnoseRefactor(DiagKindRefactor::sema_java_impl_redefinition, prevDecl, rangeNext, prevDeclFqName);
    builder.AddNote(decl, rangePrev, "'" + prevDeclFqName + "' is previously declared here");
}

void DiagJavaMirrorChildMustBeAnnotated(DiagnosticEngine& diag, const ClassLikeDecl& decl)
{
    Ptr<Decl> parentDecl;

    for (auto& parentType : decl.inheritedTypes) {
        auto pty = parentType->ty;
        if (auto parent = DynamicCast<ClassTy>(pty)) {
            parentDecl = parent->decl;
        } else if (auto parentI = DynamicCast<InterfaceTy>(pty)) {
            parentDecl = parentI->decl;
        }
        if (parentDecl && IsMirror(*parentDecl)) {
            break;
        }
    }

    diag.DiagnoseRefactor(DiagKindRefactor::sema_java_mirror_subtype_must_be_annotated, decl, parentDecl->identifier);
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

const std::string& GetVarKindName(const Decl& varDecl)
{
    static const std::string GLOBAL_VAR_NAME = "global variable";
    static const std::string MEMBER_VAR_NAME = "member variable";
    static const std::string ENUM_CONSTRUCTOR_PARAMETER = "enum constructor parameter";

    if (varDecl.TestAttr(Attribute::GLOBAL)) {
        return GLOBAL_VAR_NAME;
    } else if (varDecl.outerDecl && varDecl.outerDecl->TestAttr(Attribute::IN_ENUM)) {
        return ENUM_CONSTRUCTOR_PARAMETER;
    } else if (varDecl.outerDecl) {
        return MEMBER_VAR_NAME;
    } else {
        CJC_ABORT();
        static const std::string UNDEFINED = "";
        return UNDEFINED;
    }
}

const std::string& GetOuterDeclKindName(const Decl& outerDecl)
{
    static const std::string CLASS_NAME = "class";
    static const std::string ENUM_NAME = "enum";
    static const std::string STRUCT_NAME = "struct";

    switch (outerDecl.astKind) {
        case ASTKind::CLASS_DECL:
            return CLASS_NAME;
        case ASTKind::STRUCT_DECL:
            return STRUCT_NAME;
        case ASTKind::ENUM_DECL:
            return ENUM_NAME;
        default:
            CJC_ABORT();
            static const std::string UNDEFINED = "";
            return UNDEFINED;
    }
}

void DiagUsageOfJavaTypes(
    DiagnosticEngine& diag, const Decl& varDecl, std::vector<Ptr<Decl>>&& javaDecls, Ptr<Decl> nonJavaOuterDecl)
{
    if (javaDecls.empty()) {
        return;
    }

    auto primaryDiagJavaDecl = javaDecls.back();
    javaDecls.pop_back();

    auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_variable_of_java_type, varDecl, GetVarKindName(varDecl),
        primaryDiagJavaDecl->identifier);

    for (auto javaDecl : javaDecls) {
        builder.AddNote("Also uses java interoperability type '" + javaDecl->identifier + "'");
    }

    if (nonJavaOuterDecl) {
        builder.AddNote(*nonJavaOuterDecl,
            "Declared inside non java interoperability " + GetOuterDeclKindName(*nonJavaOuterDecl) + " '" +
                nonJavaOuterDecl->identifier + "'");
    }
}

void DiagJavaTypesAsGenericParam(DiagnosticEngine& diag, const Node& expr, std::vector<Ptr<Decl>>&& javaDecls)
{
    if (javaDecls.empty()) {
        return;
    }

    auto primaryDiagJavaDecl = javaDecls.back();
    javaDecls.pop_back();

    auto genericDecl = expr.GetTarget();
    CJC_ASSERT(genericDecl);

    auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_generic_parameter_of_java_type, expr,
        genericDecl->identifier, primaryDiagJavaDecl->identifier);

    for (auto javaDecl : javaDecls) {
        builder.AddNote("Also uses java interoperability type '" + javaDecl->identifier + "'");
    }
}

} // namespace Cangjie::Interop::Java
