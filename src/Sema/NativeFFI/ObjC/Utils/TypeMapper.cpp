// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "TypeMapper.h"
#include "cangjie/AST/ASTCasting.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Types.h"
#include "cangjie/AST/Walker.h"
#include <algorithm>
#include "NameGenerator.h"

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
static constexpr auto CHAR_PTR_TYPE = "char*";
static constexpr auto STRUCT_TYPE_PREFIX = "struct ";

static constexpr auto TYPEDEF_PREFIX = "typedef ";
} // namespace

namespace {
void MangleTypedefName(std::string& name)
{
    size_t start_pos = 0;
    while ((start_pos = name.find("_", start_pos)) != std::string::npos) {
        name.replace(start_pos, 1, "_1");
        start_pos += 2;
    }

    std::replace(name.begin(), name.end(), '.', '_');
}
} // namespace

template <class TypeRep, class MapType>
MappedCType TypeMapper::BuildFunctionalCType(
    const FuncTy& funcType, const std::vector<TypeRep>& argTypes, TypeRep resultType, bool isBlock, MapType mapType)
{
    auto typedefNamePrefix = isBlock ? "Block" : "Func";
    auto designator = isBlock ? '^' : '*';
    static auto mangler = BaseMangler();
    auto mangledName = mangler.MangleType(funcType);
    MangleTypedefName(mangledName);
    mangledName = typedefNamePrefix + mangledName;
    auto result = MappedCType(mangledName);

    std::string decl = TYPEDEF_PREFIX;
    auto resMapped = mapType(resultType);
    if (resMapped.decl != "") {
        result.dependencies.emplace_back(resMapped);
    }
    decl.append(resMapped.usage);
    decl.append({'(', designator});
    decl.append(mangledName);
    decl.append({')', '('});
    for (auto& argType : argTypes) {
        auto argMapped = mapType(argType);
        if (argMapped.decl != "") {
            result.dependencies.emplace_back(argMapped);
        }
        decl.append(argMapped.usage);
        decl.push_back(',');
    }
    if (decl.back() == ',') {
        decl.pop_back();
    }
    decl.push_back(')');
    result.decl = decl;
    return result;
}

Ptr<Ty> TypeMapper::Cj2CType(Ptr<Ty> cjty) const
{
    CJC_NULLPTR_CHECK(cjty);

    if (IsObjCObjectType(*cjty)) {
        return bridge.GetNativeObjCIdTy();
    }

    if (IsObjCPointer(*cjty)) {
        CJC_ASSERT(cjty->TyArgs().size() == 1);
        return typeManager.GetPointerTy(Cj2CType(cjty->TyArg(0)));
    }

    if (IsObjCFunc(*cjty)) {
        CJC_ASSERT(cjty->TyArgs().size() == 1);
        std::vector<ModalTy> realTypeArgs;
        auto actualFuncType = DynamicCast<FuncTy>(cjty->TyArg(0));
        CJC_NULLPTR_CHECK(actualFuncType);
        for (auto paramTy : actualFuncType->paramTys) {
            realTypeArgs.emplace_back(Cj2CType(paramTy.Ty()));
        }
        return typeManager.GetPointerTy(
            typeManager.GetFunctionTy(realTypeArgs, {Cj2CType(actualFuncType->retTy.Ty())}, {.isC = true}));
    }
    if (IsObjCBlock(*cjty)) {
        return bridge.GetNativeObjCIdTy();
    }
    CJC_ASSERT(cjty->IsBuiltin() || cjty->IsCStructType() || cjty->IsCFunc());
    return cjty;
}

MappedCType TypeMapper::Cj2ObjCForObjC(const Ty& from)
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
        case TypeKind::TYPE_CSTRING:
            return CHAR_PTR_TYPE;
        case TypeKind::TYPE_STRUCT:
            if (IsObjCPointer(from)) {
                auto result = Cj2ObjCForObjC(*from.TyArg(0));
                result.usage += "*";
                return result;
            } else if (from.IsCStructType()) {
                return STRUCT_TYPE_PREFIX + from.name;
            }
            if (IsObjCFunc(from)) {
                auto actualFuncType = DynamicCast<FuncTy>(from.TyArg(0));
                if (!actualFuncType) {
                    return UNSUPPORTED_TYPE;
                }
                return BuildFunctionalCType(*actualFuncType, actualFuncType->paramTys, actualFuncType->retTy, false,
                    [](ModalTy t) { return Cj2ObjCForObjC(*t); });
            }
            CJC_ABORT();
            return UNSUPPORTED_TYPE;
        case TypeKind::TYPE_CLASS:
            if (IsObjCBlock(from)) {
                auto actualFuncType = DynamicCast<FuncTy>(from.TyArg(0));
                if (!actualFuncType) {
                    return UNSUPPORTED_TYPE;
                }
                return BuildFunctionalCType(*actualFuncType, actualFuncType->paramTys, actualFuncType->retTy, true,
                    [](ModalTy t) { return Cj2ObjCForObjC(*t); });
            }
            if (IsObjCObjectType(from)) {
                auto decl = Ty::GetDeclOfTy(&from);
                auto userDefinedName = NameGenerator::GetUserDefinedObjCName(*decl);
                return userDefinedName
                    ? *userDefinedName + "*"
                    : from.name + "*";
            }
            return UNSUPPORTED_TYPE;
        case TypeKind::TYPE_INTERFACE:
            if (IsObjCId(from)) {
                return "id";
            }
            if (IsObjCMirror(from)) {
                return "id<" + from.name + ">";
            }
            return UNSUPPORTED_TYPE;
        case TypeKind::TYPE_POINTER: {
            if (from.TyArg(0)->kind == TypeKind::TYPE_FUNC) {
                return Cj2ObjCForObjC(*from.TyArg(0));
            }
            auto result = Cj2ObjCForObjC(*from.TyArg(0));
            result.usage += "*";
            return result;
        }
        case TypeKind::TYPE_FUNC: {
            Ty& fromMut = const_cast<Ty&>(from);
            auto actualFuncType = DynamicCast<FuncTy>(Ptr<Ty>(&fromMut));
            CJC_NULLPTR_CHECK(actualFuncType);
            return BuildFunctionalCType(*actualFuncType, actualFuncType->paramTys, actualFuncType->retTy, false,
                [](ModalTy t) { return Cj2ObjCForObjC(*t); });
        }
        case TypeKind::TYPE_ENUM:
            if (!from.IsCoreOptionType()) {
                CJC_ABORT();
                return UNSUPPORTED_TYPE;
            };
            if (IsObjCObjectType(*from.TyArg(0))) {
                return Cj2ObjCForObjC(*from.TyArg(0));
            }
        default:
            CJC_ABORT();
            return UNSUPPORTED_TYPE;
    }
}

bool TypeMapper::IsObjCCompatible(const Ty& ty)
{
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
                CJC_ASSERT(ty.TyArgs().size() == 1);
                return IsObjCCompatible(*ty.TyArg(0));
            }
            if (ty.IsCStructType()) {
                return true;
            }
            if (IsObjCFunc(ty)) {
                CJC_ASSERT(ty.typeArgs.size() == 1);
                auto tyArg = ty.typeArgs[0];
                if (!tyArg->IsFunc() || tyArg->IsCFunc()) {
                    return false;
                }
                return std::all_of(std::begin(tyArg->typeArgs), std::end(tyArg->typeArgs),
                    [](auto ty) { return IsObjCCompatible(*ty); });
            }
            return false;
        case TypeKind::TYPE_CLASS:
        case TypeKind::TYPE_INTERFACE:
            if (IsValidObjCMirror(ty) || IsObjCImpl(ty)) {
                return true;
            }
            if (IsObjCBlock(ty)) {
                CJC_ASSERT(ty.typeArgs.size() == 1);
                auto tyArg = ty.typeArgs[0];
                if (!tyArg->IsFunc() || tyArg->IsCFunc()) {
                    return false;
                }
                return std::all_of(std::begin(tyArg->typeArgs), std::end(tyArg->typeArgs),
                    [](auto ty) { return IsObjCCompatible(*ty); });
            }
            return false;
        case TypeKind::TYPE_ENUM:
            if (!ty.IsCoreOptionType()) {
                return false;
            };
            CJC_ASSERT(ty.TyArg(0));
            if (IsValidObjCMirror(*ty.TyArg(0)) || IsObjCImpl(*ty.TyArg(0))) {
                return true;
            }
        case TypeKind::TYPE_FUNC:
            if (ty.IsCFunc()) {
                return true;
            }
        case TypeKind::TYPE_CSTRING:
            if (ty.IsCString()) {
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
    return IsValidObjCMirrorSubtype(*decl.GetTy());
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
    auto classTy = DynamicCast<ClassTy*>(&ty);
    if (!classTy) {
        return false;
    }

    auto hasMirrorSuperInterface = false;
    for (auto superInterfaceTy : classTy->GetSuperInterfaceTys()) {
        if (!IsValidObjCMirror(*superInterfaceTy)) {
            return false;
        }
        hasMirrorSuperInterface = true;
    }
    if (!classTy->GetSuperClassTy() || classTy->GetSuperClassTy()->IsObject()) {
        return hasMirrorSuperInterface;
    }

    return IsValidObjCMirrorSubtype(*classTy->GetSuperClassTy()) || IsValidObjCMirror(*classTy->GetSuperClassTy());
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

bool TypeMapper::IsObjCObjectType(const Ty& ty)
{
    if (ty.IsCoreOptionType()) {
        CJC_ASSERT(ty.TyArgs().size() == 1);
        return IsObjCObjectType(*ty.TyArg(0));
    }
    return IsObjCMirror(ty) || IsObjCImpl(ty) || IsSyntheticWrapper(ty);
}

namespace {

bool IsObjCPointerImpl(const StructDecl& structDecl)
{
    if (structDecl.fullPackageName != OBJ_C_LANG_PACKAGE_IDENT) {
        return false;
    }
    if (structDecl.identifier != OBJ_C_POINTER_IDENT) {
        return false;
    }
    return true;
}

bool IsObjCFuncImpl(const StructDecl& structDecl)
{
    if (structDecl.fullPackageName != OBJ_C_LANG_PACKAGE_IDENT) {
        return false;
    }
    if (structDecl.identifier != OBJ_C_FUNC_IDENT) {
        return false;
    }
    return true;
}

bool IsObjCBlockImpl(const ClassDecl& decl)
{
    if (decl.fullPackageName != OBJ_C_LANG_PACKAGE_IDENT) {
        return false;
    }
    if (decl.identifier != OBJ_C_BLOCK_IDENT) {
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

bool TypeMapper::IsObjCFunc(const Decl& decl)
{
    if (auto structDecl = DynamicCast<StructDecl*>(&decl)) {
        return IsObjCFuncImpl(*structDecl);
    }
    return false;
}

bool TypeMapper::IsObjCFunc(const Ty& ty)
{
    if (auto structTy = DynamicCast<StructTy*>(&ty)) {
        return IsObjCFuncImpl(*structTy->decl);
    }
    return false;
}

bool TypeMapper::IsObjCBlock(const Decl& decl)
{
    if (auto classDecl = DynamicCast<ClassDecl*>(&decl)) {
        return IsObjCBlockImpl(*classDecl);
    }
    return false;
}

bool TypeMapper::IsObjCBlock(const Ty& ty)
{
    if (auto classTy = DynamicCast<ClassTy*>(&ty)) {
        return IsObjCBlockImpl(*classTy->decl);
    }
    return false;
}

bool TypeMapper::IsObjCFuncOrBlock(const Decl& decl)
{
    return IsObjCFunc(decl) || IsObjCBlock(decl);
}

bool TypeMapper::IsObjCFuncOrBlock(const Ty& ty)
{
    return IsObjCFunc(ty) || IsObjCBlock(ty);
}

bool TypeMapper::IsObjCId(const Ty& ty)
{
    auto interfaceTy = DynamicCast<InterfaceTy*>(&ty);
    return interfaceTy && interfaceTy->declPtr && IsObjCId(*interfaceTy->declPtr);
}

bool TypeMapper::IsObjCId(const Decl& decl)
{
    if (decl.fullPackageName != OBJ_C_LANG_PACKAGE_IDENT) {
        return false;
    }
    if (decl.identifier != OBJ_C_ID_IDENT) {
        return false;
    }
    return true;
}
