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
#include "ASTQuery.h"
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
            if (IsObjCForeign(from)) {
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
