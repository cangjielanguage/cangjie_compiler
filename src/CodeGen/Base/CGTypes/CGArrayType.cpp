// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "Base/CGTypes/CGArrayType.h"

#include "Base/CGTypes/CGEnumType.h"
#include "CGContext.h"
#include "CGModule.h"
#include "Utils/CGUtils.h"
#include "cangjie/CHIR/Type/ClassDef.h"
#include "cangjie/CHIR/Type/EnumDef.h"
#include "cangjie/CHIR/Type/StructDef.h"
#include "cangjie/Mangle/CHIRTypeManglingUtils.h"

namespace Cangjie::CodeGen {
bool CGArrayType::IsRefArray(const CGType& elemType)
{
    if (!elemType.GetSize() || elemType.GetOriginal().IsCPointer() || elemType.GetOriginal().IsCString() ||
        IsCFunc(elemType.GetOriginal())) {
        return false;
    } else {
        return elemType.IsRefType();
    }
}

llvm::StructType* CGArrayType::GenerateRefArrayLayoutType(CGContext& cgCtx)
{
    return GenerateArrayLayoutTypeInfo(
        cgCtx, ARRAY_LAYOUT_PREFIX + "refArray", llvm::Type::getInt8PtrTy(cgCtx.GetLLVMContext(), 1u));
}

llvm::StructType* CGArrayType::GenerateArrayLayoutTypeInfo(
    CGContext& cgCtx, const std::string& layoutName, llvm::Type* elemType)
{
    auto layoutType = llvm::StructType::getTypeByName(cgCtx.GetLLVMContext(), layoutName);
    if (layoutType && cgCtx.IsGeneratedStructType(layoutName)) {
        return layoutType;
    } else if (!layoutType) {
        layoutType = llvm::StructType::create(cgCtx.GetLLVMContext(), layoutName);
    }

    /**
     * ArrayLayout has 2 fields.
     * 1. ARRAY_BASE : ArrayBase Type.
     * 2. ARRAY_TYPE : [0 x T], T is element type. if Array is ref Array, T is i8 addrspece(1) *.
     */
    constexpr size_t arrayBase{0U};
    constexpr size_t arrayType{1U};
    constexpr size_t numOfArrayLayout{2U};
    std::vector<llvm::Type*> bodyVec(numOfArrayLayout);
    bodyVec[arrayBase] = CGType::GetArrayBaseType(cgCtx);
    bodyVec[arrayType] = llvm::ArrayType::get(elemType, 0);
    SetStructTypeBody(layoutType, bodyVec);
    cgCtx.AddGeneratedStructType(layoutName);
    return layoutType;
}

llvm::Type* CGArrayType::GenerateArrayLayoutType(CGModule& cgMod, const CHIR::RawArrayType& arrTy)
{
    auto& cgCtx = cgMod.GetCGContext();
    auto elemType = CGType::GetOrCreate(cgMod, arrTy.GetElementType());
    auto isRefArray = IsRefArray(*elemType);
    if (isRefArray) {
        return CGArrayType::GenerateRefArrayLayoutType(cgCtx);
    }
    auto typeName = CGArrayType::GetTypeNameByArrayType(cgMod, arrTy);
    auto layoutName = ARRAY_LAYOUT_PREFIX + typeName;
    return GenerateArrayLayoutTypeInfo(cgCtx, layoutName, elemType->GetLLVMType());
}

std::string CGArrayType::GetTypeNameByArrayType(CGModule& cgMod, const CHIR::RawArrayType& arrTy)
{
    std::string prefix = (arrTy.GetDims() > 1) ?
        ("A" + MangleUtils::DecimalToManglingNumber(std::to_string(arrTy.GetDims()))) : "";
    std::string typeName = GetTypeNameByArrayElementType(cgMod, *arrTy.GetElementType());
    return prefix + typeName;
}

std::string CGArrayType::GetTypeNameByArrayElementType(CGModule& cgMod, CHIR::Type& elemType)
{
    auto elementTy = &elemType;
    if (elemType.IsRef()) {
        elementTy = StaticCast<CHIR::RefType*>(&elemType)->GetTypeArgs()[0];
    }
    if (elementTy->IsEnum() && static_cast<CGEnumType*>(CGType::GetOrCreate(cgMod, elementTy))->IsTrivial()) {
        return "UInt32"; // Enum without argument is presented by UInt32.
    }
    if (elemType.IsNominal()) {
        auto defType = static_cast<CHIR::CustomType&>(elemType).GetCustomTypeDef()->GetType();
        if (CGType::GetOrCreate(cgMod, defType)->GetSize()) {
            return GetTypeQualifiedName(*defType);
        }
    }
    return GetTypeQualifiedName(*elementTy);
}

llvm::Type* CGArrayType::GenLLVMType()
{
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    llvmType = llvm::Type::getInt8Ty(cgCtx.GetLLVMContext());
    auto& arrTy = StaticCast<const CHIR::RawArrayType&>(chirType);
    auto elemType = CGType::GetOrCreate(cgMod, arrTy.GetElementType());
    if (!elemType->GetSize()) {
        return llvmType;
    }

    auto isRefArray = IsRefArray(*elemType);
    if (isRefArray) {
        layoutType = CGArrayType::GenerateRefArrayLayoutType(cgCtx);
        return llvmType;
    }
    auto typeName = CGArrayType::GetTypeNameByArrayType(cgMod, arrTy);
    auto layoutName = ARRAY_LAYOUT_PREFIX + typeName;
    layoutType = CGArrayType::GenerateArrayLayoutTypeInfo(cgCtx, layoutName, elemType->GetLLVMType());
    return llvmType;
#endif
}

void CGArrayType::GenContainedCGTypes()
{
    auto elemCHIRType = StaticCast<const CHIR::RawArrayType*>(&chirType)->GetElementType();
    (void)containedCGTypes.emplace_back(CGType::GetOrCreate(cgMod, elemCHIRType));
}

llvm::Constant* CGArrayType::GenSourceGenericOfTypeInfo()
{
    return CGType::GenSourceGenericOfTypeInfo();
}

llvm::Constant* CGArrayType::GenTypeArgsNumOfTypeInfo()
{
    return CGType::GenTypeArgsNumOfTypeInfo();
}

llvm::Constant* CGArrayType::GenTypeArgsOfTypeInfo()
{
    return CGType::GenTypeArgsOfTypeInfo();
}

llvm::Constant* CGArrayType::GenSuperOfTypeInfo()
{
    auto rawArrayElementType = DeRef(*StaticCast<const CHIR::RawArrayType&>(chirType).GetElementType());
    auto elemCGType = CGType::GetOrCreate(cgMod, rawArrayElementType);
    auto ti = elemCGType->GetOrCreateTypeInfo();
    if (elemCGType->IsStaticGI()) {
        cgCtx.AddDependentPartialOrderOfTypes(ti, this->typeInfo);
    }
    return llvm::ConstantExpr::getBitCast(ti, CGType::GetOrCreateTypeInfoPtrType(cgMod.GetLLVMContext()));
}

void CGArrayType::CalculateSizeAndAlign()
{
    if (layoutType) {
        auto arrEleTy = StaticCast<const CHIR::RawArrayType&>(chirType).GetElementType();
        size = CGType::GetOrCreate(cgMod, arrEleTy)->GetSize();
        align = 1U;
    }
};
} // namespace Cangjie::CodeGen
