// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/IR/Type/CHIRType.h"

#include "cangjie/CHIR/IR/Type/ClassDef.h"
#include "cangjie/CHIR/IR/Type/EnumDef.h"
#include "cangjie/CHIR/IR/Type/StructDef.h"
#include "cangjie/CHIR/AST2CHIR/Utils.h"
#include "cangjie/CHIR/Utils/Utils.h"

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
        argTys.emplace_back(TranslateType(argTy));
    }
    return builder.GetType<TupleType>(argTys);
}

Type* CHIRType::TranslateFuncType(const AST::FuncTy& fnTy)
{
    Type* retTy = TranslateType(fnTy.retTy);
    std::vector<Type*> paramTys;
    for (auto paramTy : fnTy.paramTys) {
        auto pType = TranslateType(paramTy);
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
        typeArgs.emplace_back(TranslateType(arg));
    }
    auto def = chirTypeCache.globalNominalCache.Get(*structTy.declPtr);
    auto type = builder.GetType<StructType>(StaticCast<StructDef*>(def), typeArgs);
    // Early cache breaks recursive TranslateType on the same nominal type (Mode::NOT).
    chirTypeCache.typeMap[AST::ModalTy{&structTy}] = type;

    return type;
}

Type* CHIRType::TranslateClassType(AST::ClassTy& classTy)
{
    std::vector<Type*> typeArgs;
    for (auto arg : classTy.typeArgs) {
        typeArgs.emplace_back(TranslateType(arg));
    }
    auto def = chirTypeCache.globalNominalCache.Get(*classTy.declPtr);
    auto type = builder.GetType<ClassType>(StaticCast<ClassDef*>(def), typeArgs);
    chirTypeCache.typeMap[AST::ModalTy{&classTy}] = type;

    return type;
}

Type* CHIRType::TranslateInterfaceType(AST::InterfaceTy& interfaceTy)
{
    std::vector<Type*> typeArgs;
    for (auto arg : interfaceTy.typeArgs) {
        typeArgs.emplace_back(TranslateType(arg));
    }
    auto def = chirTypeCache.globalNominalCache.Get(*interfaceTy.declPtr);
    auto type = builder.GetType<ClassType>(StaticCast<ClassDef*>(def), typeArgs);
    chirTypeCache.typeMap[AST::ModalTy{&interfaceTy}] = type;

    return type;
}

Type* CHIRType::TranslateEnumType(AST::EnumTy& enumTy)
{
    std::vector<Type*> typeArgs;
    for (auto arg : enumTy.typeArgs) {
        typeArgs.emplace_back(TranslateType(arg));
    }
    auto def = chirTypeCache.globalNominalCache.Get(*enumTy.declPtr);
    auto type = builder.GetType<EnumType>(StaticCast<EnumDef*>(def), typeArgs);
    chirTypeCache.typeMap[AST::ModalTy{&enumTy}] = type;
    return type;
}

Type* CHIRType::TranslateArrayType(AST::ArrayTy& arrayTy)
{
    // RawArrayType [elementTy, dims]
    auto elementTy = TranslateType(arrayTy.typeArgs[0]);
    return builder.GetType<RawArrayType>(elementTy, arrayTy.dims);
}

Type* CHIRType::TranslateVArrayType(AST::VArrayTy& varrayTy)
{
    // VArrayType size, [elementTy]
    auto elementTy = TranslateType(varrayTy.typeArgs[0]);
    return builder.GetType<VArrayType>(elementTy, varrayTy.size);
}

Type* CHIRType::TranslateCPointerType(AST::PointerTy& pointerTy)
{
    auto elementTy = TranslateType(pointerTy.typeArgs[0]);
    return builder.GetType<CPointerType>(elementTy);
}

void CHIRType::FillGenericTypeUpperBounds(AST::ModalTy ty)
{
    auto it = chirTypeCache.typeMap.find(ty);
    CJC_ASSERT(it != chirTypeCache.typeMap.end());
    std::vector<Type*> chirTy;
    for (auto argTy : StaticCast<AST::GenericsTy*>(ty.Ty())->upperBounds) {
        CJC_ASSERT(!argTy->IsGeneric());
        // Keep upper-bound modals aligned with the generic itself (same as WithModal).
        chirTy.emplace_back(TranslateType(AST::ModalTy{argTy, ty.Mode()}));
    }
    StaticCast<GenericType*>(it->second)->SetUpperBounds(chirTy);
}

void CHIRType::FillAllGenericTypeUpperBounds()
{
    // Snapshot keys first: FillGenericTypeUpperBounds may insert non-generic types into typeMap.
    std::vector<AST::ModalTy> keys;
    for (const auto& [modalTy, _] : chirTypeCache.typeMap) {
        if (Is<AST::GenericsTy>(modalTy.Ty())) {
            keys.emplace_back(modalTy);
        }
    }
    for (const auto& key : keys) {
        FillGenericTypeUpperBounds(key);
    }
}

Type* CHIRType::TranslateDataType(AST::Ty& ty)
{
    Type* type = nullptr;
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
    return type;
}

Type* CHIRType::TranslateType(AST::ModalTy ty)
{
    CJC_ASSERT(ty && ty.Ty());
    std::lock_guard<std::recursive_mutex> lock(chirTypeMtx);
    if (auto it = chirTypeCache.typeMap.find(ty); it != chirTypeCache.typeMap.end()) {
        return it->second;
    }

    Type* type = nullptr;
    auto chirModal = ASTModal2CHIRModal(ty.Mode());
    if (chirModal == Mode::NONE) {
        // Mode::NOT: translate the underlying data type and cache under ModalTy{ty, NOT}.
        // Nominal helpers may temporarily cache an unwrapped ClassType for recursion;
        // always overwrite with the final type (Ref-wrapped when needed), matching the
        // pre-ModalTy typeMap behavior.
        type = TranslateDataType(*ty.Ty());
    } else {
        // Local modal: derive from the Mode::NOT entry (created on demand), then cache.
        auto base = TranslateType(AST::ModalTy{ty.Ty()});
        type = builder.WithModal(base, chirModal);
    }
    if (type->IsReferenceType()) {
        type = builder.GetType<RefType>(type);
    }
    chirTypeCache.typeMap[ty] = type;
    return type;
}

} // namespace Cangjie::CHIR
