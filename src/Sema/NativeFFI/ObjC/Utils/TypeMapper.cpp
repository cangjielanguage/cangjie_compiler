// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "TypeMapper.h"
#include "cangjie/AST/ASTCasting.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"

using namespace Cangjie;
using namespace Cangjie::AST;
using namespace Cangjie::Interop::ObjC;

namespace {
static constexpr auto VOID_TYPE = "void";
static constexpr auto UNSUPPORTED_TYPE = "UNSUPPORTED_TYPE";

static constexpr auto INT8_TYPE = "int8_t";
static constexpr auto UINT8_TYPE = "uint8_t";
static constexpr auto INT16_TYPE = "int16_t";
static constexpr auto UINT16_TYPE = "uint16_t";
static constexpr auto INT32_TYPE = "int32_t";
static constexpr auto UINT32_TYPE = "uint32_t";
static constexpr auto INT64_TYPE = "int64_t";
static constexpr auto UINT64_TYPE = "uint64_t";
static constexpr auto NATIVE_INT_TYPE = "ssize_t";
static constexpr auto NATIVE_UINT_TYPE = "size_t";
static constexpr auto FLOAT_TYPE = "float";
static constexpr auto DOUBLE_TYPE = "double";
static constexpr auto BOOL_TYPE = "BOOL";
static constexpr auto STRUCT_TYPE_PREFIX = "struct ";
} // namespace

Ptr<Ty> TypeMapper::Cj2CType(Ptr<Ty> cjty) const
{
    CJC_NULLPTR_CHECK(cjty);

    if (cjty->IsCoreOptionType()) {
        auto innerTy = cjty->typeArgs[0];
        CJC_ASSERT(innerTy);
        if (IsValidObjCMirror(*innerTy) || IsObjCImpl(*innerTy)) {
            return bridge.GetNativeObjCIdTy();
        }
    }
    if (IsValidObjCMirror(*cjty) || IsObjCImpl(*cjty)) {
        return bridge.GetNativeObjCIdTy();
    }

    if (IsObjCPointer(*cjty)) {
        CJC_ASSERT(cjty->typeArgs.size() == 1);
        return typeManager.GetPointerTy(Cj2CType(cjty->typeArgs[0]));
    }
    if (IsObjCCJMapping(*cjty)) {
        return bridge.GetRegistryIdTy();
    }

    CJC_ASSERT(cjty->IsBuiltin() || Ty::IsCStructType(*cjty));
    return cjty;
}

std::string TypeMapper::Cj2ObjCForObjC(const Ty& from) const
{
    switch (from.kind) {
        case TypeKind::TYPE_UNIT:
            return VOID_TYPE;
        case TypeKind::TYPE_INT8:
            return INT8_TYPE;
        case TypeKind::TYPE_INT16:
            return INT16_TYPE;
        case TypeKind::TYPE_INT32:
            return INT32_TYPE;
        case TypeKind::TYPE_INT64:
        case TypeKind::TYPE_IDEAL_INT: // alias for int64
            return INT64_TYPE;
        case TypeKind::TYPE_INT_NATIVE:
            return NATIVE_INT_TYPE;
        case TypeKind::TYPE_UINT8:
            return UINT8_TYPE;
        case TypeKind::TYPE_UINT16:
            return UINT16_TYPE;
        case TypeKind::TYPE_UINT32:
            return UINT32_TYPE;
        case TypeKind::TYPE_UINT64:
            return UINT64_TYPE;
        case TypeKind::TYPE_UINT_NATIVE:
            return NATIVE_UINT_TYPE;
        case TypeKind::TYPE_FLOAT32:
            return FLOAT_TYPE;
        case TypeKind::TYPE_FLOAT64:
        case TypeKind::TYPE_IDEAL_FLOAT:
            return DOUBLE_TYPE;
        case TypeKind::TYPE_BOOLEAN:
            return BOOL_TYPE;
        case TypeKind::TYPE_STRUCT:
            if (IsObjCPointer(from)) {
                return Cj2ObjCForObjC(*from.typeArgs[0]) + "*";
            } else if (Ty::IsCStructType(from)) {
                return STRUCT_TYPE_PREFIX + from.name;
            }
            CJC_ABORT();
            return UNSUPPORTED_TYPE;
        case TypeKind::TYPE_CLASS:
            if (IsObjCMirror(from) || IsObjCImpl(from) || IsObjCCJMapping(from)) {
                return from.name + "*";
            }
            return UNSUPPORTED_TYPE;
        case TypeKind::TYPE_INTERFACE:
            if (IsObjCMirror(from)) {
                return "id<" + from.name + ">";
            }
            return UNSUPPORTED_TYPE;
        case TypeKind::TYPE_POINTER:
            return Cj2ObjCForObjC(*from.typeArgs[0]) + "*";
        case TypeKind::TYPE_ENUM:
            if (!from.IsCoreOptionType()) {
                CJC_ABORT();
                return UNSUPPORTED_TYPE;
            };
            if (IsValidObjCMirror(*from.typeArgs[0]) || IsObjCImpl(*from.typeArgs[0])) {
                return Cj2ObjCForObjC(*from.typeArgs[0]);
            }
        default:
            CJC_ABORT();
            return UNSUPPORTED_TYPE;
    }
}

bool TypeMapper::IsObjCCompatible(const Ty& ty)
{
    if (IsObjCCJMapping(ty)) {
        return true;
    }
    switch (ty.kind) {
        case TypeKind::TYPE_UNIT:
        case TypeKind::TYPE_INT8:
        case TypeKind::TYPE_INT16:
        case TypeKind::TYPE_INT32:
        case TypeKind::TYPE_INT64:
        case TypeKind::TYPE_INT_NATIVE:
        case TypeKind::TYPE_IDEAL_INT:
        case TypeKind::TYPE_UINT8:
        case TypeKind::TYPE_UINT16:
        case TypeKind::TYPE_UINT32:
        case TypeKind::TYPE_UINT64:
        case TypeKind::TYPE_UINT_NATIVE:
        case TypeKind::TYPE_FLOAT32:
        case TypeKind::TYPE_FLOAT64:
        case TypeKind::TYPE_IDEAL_FLOAT:
        case TypeKind::TYPE_BOOLEAN:
            return true;
        case TypeKind::TYPE_STRUCT:
            if (IsObjCPointer(ty)) {
                CJC_ASSERT(ty.typeArgs.size() == 1);
                return IsObjCCompatible(*ty.typeArgs[0]);
            } else if (Ty::IsCStructType(ty)) {
                return true;
            }
            return false;
        case TypeKind::TYPE_CLASS:
        case TypeKind::TYPE_INTERFACE:
            if (IsValidObjCMirror(ty) || IsObjCImpl(ty)) {
                return true;
            }
        case TypeKind::TYPE_ENUM:
            if (!ty.IsCoreOptionType()) {
                return false;
            };
            CJC_ASSERT(ty.typeArgs[0]);
            if (ty.typeArgs[0]->IsCoreOptionType()) { // no nested options
                return false;
            }
            if (IsValidObjCMirror(*ty.typeArgs[0]) || IsObjCImpl(*ty.typeArgs[0])) {
                return true;
            }
        default:
            return false;
    }
}

bool TypeMapper::IsObjCMirror(const Decl& decl)
{
    return decl.TestAttr(Attribute::OBJ_C_MIRROR);
}

bool TypeMapper::IsSyntheticWrapper(const Decl& decl)
{
    return decl.TestAttr(Attribute::OBJ_C_MIRROR_SYNTHETIC_WRAPPER);
}

bool TypeMapper::IsObjCMirrorSubtype(const Decl& decl)
{
    return IsValidObjCMirrorSubtype(*decl.ty);
}

bool TypeMapper::IsObjCImpl(const Decl& decl)
{
    if (!decl.TestAttr(Attribute::OBJ_C_MIRROR_SUBTYPE) || decl.TestAttr(Attribute::OBJ_C_MIRROR)) {
        return false;
    }

    return decl.HasAnno(AnnotationKind::OBJ_C_IMPL);
}

bool TypeMapper::IsValidObjCMirror(const Ty& ty)
{
    auto classLikeTy = DynamicCast<ClassLikeTy*>(&ty);
    if (!classLikeTy) {
        return false;
    }

    auto hasAttr = classLikeTy->commonDecl && IsObjCMirror(*classLikeTy->commonDecl);
    if (!hasAttr) {
        return false;
    }

    for (auto parent : classLikeTy->GetSuperInterfaceTys()) {
        if (!IsValidObjCMirror(*parent)) {
            return false;
        }
    }

    // superclass must be @ObjCMirror
    if (auto classTy = DynamicCast<ClassTy*>(&ty); classTy) {
        // Hierarchy root @ObjCMirror class
        if (!classTy->GetSuperClassTy() || classTy->GetSuperClassTy()->IsObject()) {
            return true;
        }
        return IsValidObjCMirror(*classTy->GetSuperClassTy());
    }

    return true;
}

bool TypeMapper::IsValidObjCMirrorSubtype(const Ty& ty)
{
    if (auto classTy = DynamicCast<ClassTy*>(&ty);
        classTy && classTy->GetSuperClassTy() && !classTy->GetSuperClassTy()->IsObject()) {
        return IsValidObjCMirrorSubtype(*classTy->GetSuperClassTy()) || IsValidObjCMirror(*classTy->GetSuperClassTy());
    }

    return false;
}

bool TypeMapper::IsObjCImpl(const Ty& ty)
{
    auto classLikeTy = DynamicCast<ClassLikeTy*>(&ty);
    return classLikeTy && classLikeTy->commonDecl && IsObjCImpl(*classLikeTy->commonDecl);
}

bool TypeMapper::IsObjCMirror(const Ty& ty)
{
    auto classLikeTy = DynamicCast<ClassLikeTy*>(&ty);
    return classLikeTy && classLikeTy->commonDecl && IsObjCMirror(*classLikeTy->commonDecl);
}

bool TypeMapper::IsSyntheticWrapper(const Ty& ty)
{
    auto classLikeTy = DynamicCast<ClassLikeTy*>(&ty);
    return classLikeTy && classLikeTy->commonDecl && IsSyntheticWrapper(*classLikeTy->commonDecl);
}

namespace {

bool IsObjCPointerImpl(const StructDecl& structDecl)
{
    if (structDecl.fullPackageName != OBJ_C_LANG_PACKAGE_IDENT) {
        return false;
    }
    if (structDecl.identifier.Val() != OBJ_C_POINTER_IDENT) {
        return false;
    }
    return true;
}

} // namespace

bool TypeMapper::IsObjCPointer(const Decl& decl)
{
    if (auto structDecl = DynamicCast<StructDecl*>(&decl)) {
        return IsObjCPointerImpl(*structDecl);
    }
    return false;
}

bool TypeMapper::IsObjCPointer(const Ty& ty)
{
    if (auto structTy = DynamicCast<StructTy*>(&ty)) {
        return IsObjCPointerImpl(*structTy->decl);
    }
    return false;
}

bool TypeMapper::IsObjCCJMapping(const Decl& decl)
{
    return decl.TestAttr(Attribute::OBJ_C_CJ_MAPPING);
}

bool TypeMapper::IsObjCCJMapping(const Ty& ty)
{
    auto structTy = DynamicCast<StructTy*>(&ty);
    return structTy && structTy->decl && IsObjCCJMapping(*structTy->decl);
}