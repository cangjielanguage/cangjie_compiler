// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/IR/Type/CHIRType.h"

#include "cangjie/CHIR/IR/Type/ClassDef.h"
#include "cangjie/CHIR/IR/Type/EnumDef.h"
#include "cangjie/CHIR/IR/Type/StructDef.h"
#include "cangjie/CHIR/Utils/Utils.h"
#include "cangjie/Mangle/BaseMangler.h"

namespace Cangjie::CHIR {
std::recursive_mutex CHIRType::chirTypeMtx;

static inline std::string GetIdentifierOfGenericTy(const AST::GenericsTy& ty)
{
    CJC_ASSERT(ty.decl->mangledName != "");
    return ty.decl->mangledName;
}

Type* CHIRType::TranslateTupleType(AST::TupleTy& tupleTy)
{
    std::vector<Type*> argTys;
    for (auto argTy : tupleTy.typeArgs) {
        argTys.emplace_back(TranslateType(*argTy));
    }
    return builder.GetType<TupleType>(argTys);
}

Type* CHIRType::TranslateFuncType(const AST::FuncTy& fnTy)
{
    Type* retTy = TranslateType(*fnTy.retTy);
    std::vector<Type*> paramTys;
    for (auto paramTy : fnTy.paramTys) {
        auto pType = TranslateType(*paramTy);
        if (fnTy.IsCFunc() && pType->IsVArray()) {
            pType = builder.GetType<RefType>(pType);
        }
        paramTys.emplace_back(pType);
    }
    auto funcTy = builder.GetType<FuncType>(paramTys, retTy, fnTy.hasVariableLenArg, fnTy.IsCFunc());
    return funcTy;
}

Type* CHIRType::TranslateStructType(AST::StructTy& structTy)
{
    std::vector<Type*> typeArgs;
    for (auto arg : structTy.typeArgs) {
        typeArgs.emplace_back(TranslateType(*arg));
    }
    auto def = chirTypeCache.globalNominalCache.Get(*structTy.declPtr);
    auto type = builder.GetType<StructType>(StaticCast<StructDef*>(def), typeArgs);
    chirTypeCache.typeMap[&structTy] = type;

    return type;
}

Type* CHIRType::TranslateClassType(AST::ClassTy& classTy)
{
    std::vector<Type*> typeArgs;
    for (auto arg : classTy.typeArgs) {
        typeArgs.emplace_back(TranslateType(*arg));
    }
    auto def = chirTypeCache.globalNominalCache.Get(*classTy.declPtr);
    auto type = builder.GetType<ClassType>(StaticCast<ClassDef*>(def), typeArgs);
    chirTypeCache.typeMap[&classTy] = type;

    return type;
}

Type* CHIRType::TranslateInterfaceType(AST::InterfaceTy& interfaceTy)
{
    std::vector<Type*> typeArgs;
    for (auto arg : interfaceTy.typeArgs) {
        typeArgs.emplace_back(TranslateType(*arg));
    }
    auto def = chirTypeCache.globalNominalCache.Get(*interfaceTy.declPtr);
    auto type = builder.GetType<ClassType>(StaticCast<ClassDef*>(def), typeArgs);
    chirTypeCache.typeMap[&interfaceTy] = type;

    return type;
}

Type* CHIRType::TranslateEnumType(AST::EnumTy& enumTy)
{
    std::vector<Type*> typeArgs;
    for (auto arg : enumTy.typeArgs) {
        typeArgs.emplace_back(TranslateType(*arg));
    }
    auto def = chirTypeCache.globalNominalCache.Get(*enumTy.declPtr);
    auto type = builder.GetType<EnumType>(StaticCast<EnumDef*>(def), typeArgs);
    chirTypeCache.typeMap[&enumTy] = type;
    return type;
}

Type* CHIRType::TranslateArrayType(AST::ArrayTy& arrayTy)
{
    // RawArrayType [elementTy, dims]
    auto elementTy = TranslateType(*arrayTy.typeArgs[0]);
    return builder.GetType<RawArrayType>(elementTy, arrayTy.dims);
}

Type* CHIRType::TranslateVArrayType(AST::VArrayTy& varrayTy)
{
    // VArrayType size, [elementTy]
    auto elementTy = TranslateType(*varrayTy.typeArgs[0]);
    return builder.GetType<VArrayType>(elementTy, varrayTy.size);
}

Type* CHIRType::TranslateCPointerType(AST::PointerTy& pointerTy)
{
    auto elementTy = TranslateType(*pointerTy.typeArgs[0]);
    return builder.GetType<CPointerType>(elementTy);
}

void CHIRType::FillGenericArgType(AST::GenericsTy& ty)
{
    auto it = chirTypeCache.typeMap.find(&ty);
    CJC_ASSERT(it != chirTypeCache.typeMap.end());
    CJC_ASSERT(it->second->GetTypeKind() == Type::TypeKind::TYPE_GENERIC);

    std::vector<Type*> chirTy;
    for (auto argTy : ty.upperBounds) {
        CJC_ASSERT(!argTy->IsGeneric());
        chirTy.emplace_back(TranslateType(*argTy));
    }
    StaticCast<GenericType*>(it->second)->SetUpperBounds(chirTy);
}

Type* CHIRType::TranslateType(AST::Ty& ty)
{
    Type* type = nullptr;
    std::lock_guard<std::recursive_mutex> lock(chirTypeMtx);
    if (auto it = chirTypeCache.typeMap.find(&ty); it != chirTypeCache.typeMap.end()) {
        return it->second;
    }
    switch (ty.kind) {
        case AST::TypeKind::TYPE_UNIT: {
            type = builder.GetUnitTy();
            break;
        }
        case AST::TypeKind::TYPE_INT8: {
            type = builder.GetInt8Ty();
            break;
        }
        case AST::TypeKind::TYPE_INT16: {
            type = builder.GetInt16Ty();
            break;
        }
        case AST::TypeKind::TYPE_INT32: {
            type = builder.GetInt32Ty();
            break;
        }
        case AST::TypeKind::TYPE_INT64: {
            type = builder.GetInt64Ty();
            break;
        }
        case AST::TypeKind::TYPE_INT_NATIVE: {
            type = builder.GetIntNativeTy();
            break;
        }
        case AST::TypeKind::TYPE_UINT8: {
            type = builder.GetUInt8Ty();
            break;
        }
        case AST::TypeKind::TYPE_UINT16: {
            type = builder.GetUInt16Ty();
            break;
        }
        case AST::TypeKind::TYPE_UINT32: {
            type = builder.GetUInt32Ty();
            break;
        }
        case AST::TypeKind::TYPE_UINT64: {
            type = builder.GetUInt64Ty();
            break;
        }
        case AST::TypeKind::TYPE_UINT_NATIVE: {
            type = builder.GetUIntNativeTy();
            break;
        }
        case AST::TypeKind::TYPE_FLOAT16: {
            type = builder.GetFloat16Ty();
            break;
        }
        case AST::TypeKind::TYPE_FLOAT32: {
            type = builder.GetFloat32Ty();
            break;
        }
        case AST::TypeKind::TYPE_FLOAT64: {
            type = builder.GetFloat64Ty();
            break;
        }
        case AST::TypeKind::TYPE_RUNE: {
            type = builder.GetRuneTy();
            break;
        }
        case AST::TypeKind::TYPE_BOOLEAN: {
            type = builder.GetBoolTy();
            break;
        }
        case AST::TypeKind::TYPE_TUPLE: {
            type = TranslateTupleType(StaticCast<AST::TupleTy&>(ty));
            break;
        }
        case AST::TypeKind::TYPE_FUNC: {
            type = TranslateFuncType(StaticCast<AST::FuncTy&>(ty));
            break;
        }
        case AST::TypeKind::TYPE_ENUM: {
            type = TranslateEnumType(StaticCast<AST::EnumTy&>(ty));
            break;
        }
        case AST::TypeKind::TYPE_STRUCT: {
            type = TranslateStructType(StaticCast<AST::StructTy&>(ty));
            break;
        }
        case AST::TypeKind::TYPE_CLASS: {
            type = TranslateClassType(StaticCast<AST::ClassTy&>(ty));
            break;
        }
        case AST::TypeKind::TYPE_INTERFACE: {
            type = TranslateInterfaceType(StaticCast<AST::InterfaceTy&>(ty));
            break;
        }
        case AST::TypeKind::TYPE_ARRAY: {
            type = TranslateArrayType(StaticCast<AST::ArrayTy&>(ty));
            break;
        }
        case AST::TypeKind::TYPE_VARRAY: {
            type = TranslateVArrayType(StaticCast<AST::VArrayTy&>(ty));
            break;
        }
        case AST::TypeKind::TYPE_NOTHING: {
            type = builder.GetType<NothingType>();
            break;
        }
        case AST::TypeKind::TYPE_POINTER: {
            type = TranslateCPointerType(StaticCast<AST::PointerTy&>(ty));
            break;
        }
        case AST::TypeKind::TYPE_CSTRING: {
            type = builder.GetType<CStringType>();
            break;
        }
        case AST::TypeKind::TYPE_GENERICS: {
            auto identifier = GetIdentifierOfGenericTy(StaticCast<AST::GenericsTy&>(ty));
            type = builder.GetType<GenericType>(identifier, ty.name);
            break;
        }
        default: {
            CJC_ABORT();
        }
    }

    if (type->IsClassOrArray() || type->IsBox()) {
        type = builder.GetType<RefType>(type);
    }
    chirTypeCache.typeMap[&ty] = type;

    return type;
}

} // namespace Cangjie::CHIR