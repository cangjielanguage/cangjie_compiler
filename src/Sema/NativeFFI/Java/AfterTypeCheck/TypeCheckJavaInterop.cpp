// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "JavaDesugarManager.h"
#include "JavaInteropManager.h"

#include "DiagsInterop.h"
#include "TypeCheckUtil.h"
#include "TypeCheckerImpl.h"
#include "Utils.h"

#include "cangjie/AST/Match.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Utils/Utils.h"

using namespace Cangjie;
using namespace TypeCheckUtil;
using namespace AST;
using namespace Cangjie::Interop::Java;

namespace {

/* Represents a position of value where specific type is allowed to be
 * - OUT   -- return values
 * - IN    -- methods parameters
 * - BOTH  -- OUT and IN
 */
enum class Position { OUT, IN, BOTH };

/*
 * Checks if [ty] is direct or indirect child of @JavaMirror-annotated decl.
 */
inline bool IsJavaMirrorSubtype(const Ty& ty)
{
    if (auto classTy = DynamicCast<ClassTy*>(&ty);
        classTy && classTy->GetSuperClassTy() && !classTy->GetSuperClassTy()->IsObject()) {
        if (!IsJavaMirrorSubtype(*classTy->GetSuperClassTy()) &&
            (!classTy->GetSuperClassTy()->decl || !IsMirror(*classTy->GetSuperClassTy()->decl))) {
            return false;
        }
        return true;
    }

    if (auto ity = DynamicCast<ClassLikeTy*>(&ty)) {
        if (ity->GetSuperInterfaceTys().empty()) {
            return false;
        }

        for (auto parent : ity->GetSuperInterfaceTys()) {
            if (IsMirror(*parent->decl)) {
                return true;
            }
        }
    }

    return false;
}

struct JavaInteropTypeChecker {
    bool isImpl;
    bool isCJMappingTypeCheck = false;
    DiagnosticEngine& diag;

    auto GetJavaClassKind() const
    {
        if (isCJMappingTypeCheck) {
            return "cangjie mirror decl";
        }
        if (isImpl) {
            return "@JavaImpl";
        } else {
            return "@JavaMirror";
        }
    }

    /*
     * Checks if [ty] is supported in signature.
     * - @JavaMirror class types
     * - Some built-in supported types
     * - String type in return position in Mirror class
     */
    inline bool IsSupported(const Ty& ty, Position pos)
    {
        auto areJavaMirrorTypes = [this](const std::vector<Ptr<Ty>>& typeArgs, Position pos) {
            return std::all_of(
                typeArgs.begin(), typeArgs.end(), [this, pos](const auto& argTy) { return IsSupported(*argTy, pos); });
        };

        // Use IsCJMapping to check a ty is supported or not.
        if (isCJMappingTypeCheck && IsCJMapping(ty)) {
            return true;
        }

        switch (ty.kind) {
            case TypeKind::TYPE_UNIT:
            case TypeKind::TYPE_BOOLEAN:
            case TypeKind::TYPE_INT8:
            case TypeKind::TYPE_UINT16:
            case TypeKind::TYPE_INT16:
            case TypeKind::TYPE_INT32:
            case TypeKind::TYPE_INT64:
            case TypeKind::TYPE_FLOAT32:
            case TypeKind::TYPE_FLOAT64:
                return true;
            case TypeKind::TYPE_ENUM:
                if (!ty.IsCoreOptionType() || ty.typeArgs[0]->IsCoreOptionType()) {
                    return false;
                };
                return !ty.typeArgs[0]->IsPrimitive() && areJavaMirrorTypes(ty.typeArgs, pos);
            case TypeKind::TYPE_CLASS:
            case TypeKind::TYPE_INTERFACE:
                if (ty.IsObject()) {
                    return true;
                }
                return IsMirror(*StaticCast<ClassLikeTy&>(ty).commonDecl);
            case TypeKind::TYPE_STRUCT:
                if (ty.name == INTEROPLIB_CFFI_JAVA_ENTITY) {
                    // for javaref getter stub wich is generated on Parser stage
                    return true;
                }
                if (!isImpl && pos == Position::OUT && ty.IsString()) {
                    return true;
                }
                return false;
            default:
                return false;
        }
    }

    /*
     * Checks if [ty] is one of:
     * of @JavaMirror-annotated decl
     * supported built-in type
     * direct or indirect successor of @JavaMirror-annotated decl (with already enabled JAVA_MIRROR_SUBTYPE attribute)
     */
    inline bool IsJavaCompatible(const Ty& ty, Position pos = Position::BOTH)
    {
        if (IsSupported(ty, pos)) {
            if (auto classLikeTy = DynamicCast<ClassLikeTy*>(&ty);
                classLikeTy && classLikeTy->commonDecl && IsJArray(*classLikeTy->commonDecl)) {
                return IsJavaCompatible(*ty.typeArgs[0]);
            }
            return true;
        }

        if (ty.IsCoreOptionType() && !ty.typeArgs[0]->IsCoreOptionType() && !ty.typeArgs[0]->IsPrimitive()) {
            return IsJavaCompatible(*ty.typeArgs[0]);
        }

        if (auto classLikeTy = DynamicCast<ClassLikeTy*>(&ty); classLikeTy && classLikeTy->commonDecl) {
            return classLikeTy->commonDecl->TestAttr(Attribute::JAVA_MIRROR_SUBTYPE);
        }
        return false;
    }

    inline void CheckJavaCompatibleParamTypes(FuncDecl& fdecl, DiagKindRefactor errkind)
    {
        if (!fdecl.funcBody) {
            return;
        }
        auto isJavaArray = IsJArray(*fdecl.outerDecl);
        for (auto& paramList : fdecl.funcBody->paramLists) {
            for (auto& param : paramList->params) {
                if (!IsJavaCompatible(*param->ty) && (!isJavaArray || !param->ty->IsGeneric())) {
                    diag.DiagnoseRefactor(errkind, *param);
                    fdecl.EnableAttr(Attribute::IS_BROKEN);
                    fdecl.outerDecl->EnableAttr(Attribute::HAS_BROKEN);
                    fdecl.outerDecl->EnableAttr(Attribute::IS_BROKEN);
                }
            }
        }
    }

    inline void CheckJavaMirrorMethodTypes(FuncDecl& fd)
    {
        if (fd.funcBody && fd.funcBody->retType && !IsJavaCompatible(*fd.funcBody->retType->ty, Position::OUT) &&
            (!IsJArray(*fd.outerDecl) || !fd.funcBody->retType->ty->IsGeneric())) {
            Ptr<Node> node;
            if (!fd.funcBody->retType->begin.IsZero()) {
                node = fd.funcBody->retType;
            } else {
                // Type wasn't explicitly written, so report on whole declaration
                node = &fd;
            }
            diag.DiagnoseRefactor(DiagKindRefactor::sema_java_mirror_method_ret_unsupported, *node,
                Ty::ToString(fd.funcBody->retType->ty), GetJavaClassKind());
            fd.EnableAttr(Attribute::IS_BROKEN);
        }

        CheckJavaCompatibleParamTypes(fd, DiagKindRefactor::sema_java_mirror_method_arg_must_be_java_mirror);
    }

    inline void CheckJavaMirrorConstructorTypes(FuncDecl& ctor)
    {
        CheckJavaCompatibleParamTypes(ctor, DiagKindRefactor::sema_java_mirror_ctor_arg_must_be_java_mirror);
    }

    inline void CheckJavaMirrorPropType(PropDecl& prop)
    {
        if (!IsJavaCompatible(*prop.ty)) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_java_mirror_prop_must_be_java_mirror, *prop.type);
            prop.EnableAttr(Attribute::IS_BROKEN);
        }
    }

    inline void CheckJavaCompatibleDeclSignature(ClassLikeDecl& decl)
    {
        for (auto& member : decl.GetMemberDecls()) {
            switch (member->astKind) {
                case ASTKind::FUNC_DECL:
                    if (member->TestAttr(Attribute::CONSTRUCTOR)) {
                        CheckJavaMirrorConstructorTypes(*StaticAs<ASTKind::FUNC_DECL>(member.get()));
                    } else {
                        if (isImpl && member->TestAttr(Attribute::PRIVATE)) {
                            continue;
                        }
                        CheckJavaMirrorMethodTypes(*StaticAs<ASTKind::FUNC_DECL>(member.get()));
                    }
                    break;
                case ASTKind::PROP_DECL:
                    CheckJavaMirrorPropType(*StaticAs<ASTKind::PROP_DECL>(member.get()));
                    break;
                default:
                    continue;
            }
        }
    }

    inline void CheckCJMappingMethodTypes(FuncDecl& fd)
    {
        if (fd.funcBody && fd.funcBody->retType &&
            (fd.funcBody->retType->ty->IsCoreOptionType() ||
                !IsJavaCompatible(*fd.funcBody->retType->ty, Position::OUT))) {
            Ptr<Node> node;
            if (!fd.funcBody->retType->begin.IsZero()) {
                node = fd.funcBody->retType;
            } else {
                node = &fd;
            }
            diag.DiagnoseRefactor(DiagKindRefactor::sema_cjmapping_method_ret_unsupported, *node,
                Ty::ToString(fd.funcBody->retType->ty), GetJavaClassKind());
            fd.EnableAttr(Attribute::IS_BROKEN);
        }

        CheckCJMappingCompatibleParamTypes(fd);
    }

    inline void CheckCJMappingCompatibleParamTypes(FuncDecl& fdecl)
    {
        if (!fdecl.funcBody) {
            return;
        }
        for (auto& paramList : fdecl.funcBody->paramLists) {
            for (auto& param : paramList->params) {
                bool isOptionArg = (*param->ty).IsCoreOptionType();
                if ((*param->ty).IsInterface()) {
                    continue;
                }
                if (isOptionArg || !IsJavaCompatible(*param->ty)) {
                    diag.DiagnoseRefactor(DiagKindRefactor::sema_cjmapping_method_arg_not_supported, *param);
                    fdecl.EnableAttr(Attribute::IS_BROKEN);
                    fdecl.outerDecl->EnableAttr(Attribute::HAS_BROKEN);
                    fdecl.outerDecl->EnableAttr(Attribute::IS_BROKEN);
                }
            }
        }
    }
};
} // namespace

namespace Cangjie::Interop::Java {

void JavaInteropManager::CheckInheritance(ClassLikeDecl& decl) const
{
    CheckJavaMirrorSubtypeAttrClassLikeDecl(decl);

    if (IsMirror(decl) || IsImpl(decl)) {
        CheckNonJavaSuperType(decl);
    }
}

void JavaInteropManager::CheckNonJavaSuperType(ClassLikeDecl& decl) const
{
    CJC_ASSERT(IsMirror(decl) || IsImpl(decl));

    auto superDecls = decl.GetAllSuperDecls();

    for (const auto superDecl : superDecls) {
        if (!IsMirror(*superDecl) && !IsImpl(*superDecl) && !superDecl->ty->IsObject() && !superDecl->ty->IsAny()) {
            DiagJavaDeclCannotInheritPureCangjieType(diag, decl);
            decl.EnableAttr(Attribute::IS_BROKEN);
            return;
        }
    }

    if (decl.HasAnno(AnnotationKind::JAVA_IMPL) && !IsJavaMirrorSubtype(*decl.ty)) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_java_mirror_subtype_anno_must_inherit_mirror, decl);
        decl.EnableAttr(Attribute::IS_BROKEN);
    }
}

void JavaInteropManager::CheckJavaMirrorSubtypeAttrClassLikeDecl(ClassLikeDecl& decl) const
{
    if (IsJavaMirrorSubtype(*decl.ty)) {
        if (!decl.TestAnyAttr(Attribute::JAVA_MIRROR_SUBTYPE, Attribute::JAVA_MIRROR)) {
            DiagJavaMirrorChildMustBeAnnotated(diag, decl);
            decl.EnableAttr(Attribute::IS_BROKEN);
        }
    }
}

void JavaInteropManager::CheckExtendDecl(ExtendDecl& decl) const
{
    auto& ty = *decl.extendedType->ty;
    if (!IsMirror(ty) && !IsImpl(ty)) {
        return;
    }

    if (!decl.inheritedTypes.empty()) {
        DiagJavaDeclCannotBeExtendedWithInterface(diag, decl);
        decl.EnableAttr(Attribute::IS_BROKEN);
    }
}

void JavaInteropManager::CheckJavaMirrorTypes(ClassLikeDecl& decl)
{
    JavaInteropTypeChecker checker{
        .isImpl = false,
        .diag = diag,
    };
    checker.CheckJavaCompatibleDeclSignature(decl);
}

void JavaInteropManager::CheckJavaImplTypes(ClassLikeDecl& decl)
{
    JavaInteropTypeChecker checker{
        .isImpl = true,
        .diag = diag,
    };
    checker.CheckJavaCompatibleDeclSignature(decl);
}

void JavaInteropManager::CheckImplRedefinition(Package& package)
{
    std::unordered_map<std::string, ClassLikeDecl*> javaNameToDecl;

    for (auto& file : package.files) {
        for (auto& decl : file->decls) {
            auto classLikeDecl = As<ASTKind::CLASS_LIKE_DECL>(decl);
            if (!classLikeDecl || classLikeDecl->TestAttr(Attribute::IS_BROKEN)) {
                continue;
            }

            if (!IsImpl(*classLikeDecl)) {
                continue;
            }

            auto name = GetJavaFQName(*classLikeDecl);
            if (javaNameToDecl.find(name) != javaNameToDecl.end()) {
                DiagJavaImplRedefinitionInJava(diag, *classLikeDecl, *(javaNameToDecl[name]));
                classLikeDecl->EnableAttr(Attribute::IS_BROKEN);
                continue;
            }
            javaNameToDecl[name] = classLikeDecl;
        }
    }
}

void JavaInteropManager::CheckTypes(File& file)
{
    for (auto& decl : file.decls) {
        if (auto classLikeDecl = As<ASTKind::CLASS_LIKE_DECL>(decl)) {
            CheckInheritance(*classLikeDecl);
            CheckTypes(*classLikeDecl);
        } else if (auto extendDecl = As<ASTKind::EXTEND_DECL>(decl)) {
            CheckExtendDecl(*extendDecl);
        }

        CheckCJMappingType(*decl);
        CheckUsageOfJavaTypes(*decl);
        CheckGenericsInstantiation(*decl);
    }
}

void JavaInteropManager::CheckTypes(ClassLikeDecl& classLikeDecl)
{
    if (!classLikeDecl.TestAnyAttr(Attribute::JAVA_MIRROR, Attribute::JAVA_MIRROR_SUBTYPE)) {
        return;
    }

    hasMirrorOrImpl = true;

    if (auto id = As<ASTKind::INTERFACE_DECL>(&classLikeDecl);
        id && id->TestAttr(Attribute::JAVA_MIRROR_SUBTYPE) && !id->TestAttr(Attribute::JAVA_MIRROR)) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_java_interop_not_supported, *id, "@JavaImpl interface");
        classLikeDecl.EnableAttr(Attribute::IS_BROKEN);
        return;
    }

    if (auto cd = As<ASTKind::CLASS_DECL>(&classLikeDecl); cd && cd->TestAttr(Attribute::ABSTRACT) && IsImpl(*cd)) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_java_interop_not_supported, *cd, "@JavaImpl abstract");
        classLikeDecl.EnableAttr(Attribute::IS_BROKEN);
        return;
    }

    if (classLikeDecl.TestAttr(Attribute::JAVA_MIRROR)) {
        CheckJavaMirrorTypes(classLikeDecl);
    } else if (classLikeDecl.TestAttr(Attribute::JAVA_MIRROR_SUBTYPE)) {
        CheckJavaImplTypes(classLikeDecl);
    }
}

void JavaInteropManager::CheckCJMappingDeclSupportRange(Decl& decl)
{
    switch (decl.astKind) {
        case ASTKind::STRUCT_DECL:
        case ASTKind::ENUM_DECL:
        case ASTKind::CLASS_DECL:
        case ASTKind::INTERFACE_DECL:
        case ASTKind::EXTEND_DECL:
            break;
        default:
            diag.DiagnoseRefactor(DiagKindRefactor::sema_cjmapping_decl_not_supported, MakeRange(decl.identifier),
                std::string{decl.identifier});
            decl.EnableAttr(Attribute::IS_BROKEN);
    }
}

void JavaInteropManager::CheckCJMappingType(Decl& decl)
{
    if (!IsCJMapping(decl)) {
        return;
    }

    CheckCJMappingDeclSupportRange(decl);
    JavaInteropTypeChecker checker{
        .isImpl = false,
        .isCJMappingTypeCheck = true,
        .diag = diag,
    };
    for (auto& member : decl.GetMemberDecls()) {
        switch (member->astKind) {
            case ASTKind::FUNC_DECL:
                if (!member->TestAttr(Attribute::PUBLIC)) {
                    continue;
                }
                checker.CheckCJMappingMethodTypes(*StaticAs<ASTKind::FUNC_DECL>(member.get()));
                break;
            default:
                continue;
        }
    }
}

} // namespace Cangjie::Interop::Java
