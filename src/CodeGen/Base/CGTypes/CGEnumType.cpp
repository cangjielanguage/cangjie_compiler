// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "Base/CGTypes/CGEnumType.h"

#include "CGContext.h"
#include "CGModule.h"
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
#include "CJNative/CGTypeInfo.h"
#endif
#include "Utils/CGUtils.h"
#include "cangjie/CHIR/Type/EnumDef.h"
#include "cangjie/Option/Option.h"

namespace Cangjie::CodeGen {
namespace {
bool ContainsRefType(const llvm::Type& llvmType)
{
    if (llvmType.isPointerTy() && llvmType.getPointerAddressSpace() == 1U) {
        return true;
    } else if (auto structType = llvm::dyn_cast<const llvm::StructType>(&llvmType); structType) {
        for (auto& elementType : structType->elements()) {
            if (ContainsRefType(*elementType)) {
                return true;
            }
        }
    }
    return false;
}

// check if enum has RefTypeAssociatedValue and get the size of constructor union.
std::tuple<bool, std::optional<std::size_t>> ObtainingAuxiliaryInformation(
    CGModule& cgMod, const CHIR::EnumType& chirEnumType)
{
    bool hasRefTypeAssociatedValue = false;
    std::optional<std::size_t> sizeOfConstructorUnion = std::nullopt;

    const auto& ctors = chirEnumType.GetConstructorInfos(cgMod.GetCGContext().GetCHIRBuilder());
    CJC_ASSERT(!ctors.empty() && "The enum type has at least one constructor.");
    for (auto ctor : ctors) {
        const auto& paramTypes = ctor.funcType->GetParamTypes();
        std::optional<std::size_t> sizeOfConstructorUnionTemp =
            paramTypes.empty() ? std::optional<std::size_t>(0U) : std::nullopt;
        for (auto associatedValueCHIRType : paramTypes) {
            auto associatedValueCGType = CGType::GetOrCreate(cgMod, associatedValueCHIRType);
            hasRefTypeAssociatedValue |= ContainsRefType(*associatedValueCGType->GetLLVMType());
            auto associatedValueSize = associatedValueCGType->GetSize();
            if (associatedValueSize.has_value()) {
                sizeOfConstructorUnionTemp = sizeOfConstructorUnionTemp.value_or(0U) + associatedValueSize.value();
            } else {
                sizeOfConstructorUnionTemp = std::nullopt;
                break;
            }
        }
        if (sizeOfConstructorUnionTemp.has_value()) {
            sizeOfConstructorUnion = sizeOfConstructorUnion.value_or(0U) < sizeOfConstructorUnionTemp.value()
                ? sizeOfConstructorUnionTemp.value()
                : sizeOfConstructorUnion.value_or(0U);
        } else {
            sizeOfConstructorUnion = std::nullopt;
            break;
        }
    }

    return std::make_tuple(hasRefTypeAssociatedValue, sizeOfConstructorUnion);
}
}

CGEnumType::CGEnumType(CGModule& cgMod, CGContext& cgCtx, const CHIR::Type& chirType)
    : CGCustomType(cgMod, cgCtx, chirType, CGTypeKind::CG_ENUM),
      chirEnumType(StaticCast<const CHIR::EnumType&>(chirType)),
      optionLikeInfo(nullptr),
      sizeOfConstructorUnion(std::nullopt),
      cgEnumTypeKind(CalculateCGEnumTypeKind())
{
}

CGEnumType::CGEnumTypeKind CGEnumType::CalculateCGEnumTypeKind()
{
    // 1. check if enum is non_exhaustive
    if (!chirEnumType.GetEnumDef()->IsExhaustive()) {
        return chirEnumType.GetEnumDef()->IsAllCtorsTrivial() ? CGEnumTypeKind::NON_EXHAUSTIVE_UNASSOCIATED
                                                      : CGEnumTypeKind::NON_EXHAUSTIVE_ASSOCIATED;
    }
    // 2. check if enum has RefTypeAssociatedValue and get the size of constructor union.
    auto [hasRefTypeAssociatedValue, sizeOfConstructorUnionTemp] = ObtainingAuxiliaryInformation(cgMod, chirEnumType);
    sizeOfConstructorUnion = sizeOfConstructorUnionTemp;
    // 3. determine other scenarios.
    const auto& ctors = chirEnumType.GetConstructorInfos(cgMod.GetCGContext().GetCHIRBuilder());
    if (ctors.size() == 1U && sizeOfConstructorUnion.has_value() && sizeOfConstructorUnion.value() == 0U &&
        chirEnumType.GetGenericArgs().empty()) {
        return CGEnumTypeKind::EXHAUSTIVE_ZERO_SIZE;
    }
    if (chirEnumType.GetEnumDef()->IsAllCtorsTrivial()) {
        return CGEnumTypeKind::EXHAUSTIVE_UNASSOCIATED;
    }
    if (ctors.size() == 2U && // OptionLike always has two constructors
        ((ctors[0].funcType->GetParamTypes().size() == 0U && ctors[1].funcType->GetParamTypes().size() == 1U) ||
        (ctors[0].funcType->GetParamTypes().size() == 1U && ctors[1].funcType->GetParamTypes().size() == 0U))) {
        bool isAntiOptionLike = ctors[0].funcType->GetParamTypes().empty();
        CHIR::Type* associatedValueType = isAntiOptionLike ? ctors[1].funcType->GetParamTypes()[0]
                                                            : ctors[0].funcType->GetParamTypes()[0];
        optionLikeInfo = std::make_unique<CGEnumType::OptionLikeInfo>(isAntiOptionLike, associatedValueType);

        if (associatedValueType->IsGeneric()) {
            return CGEnumTypeKind::EXHAUSTIVE_ASSOCIATED_OPTION_LIKE_T;
        }
        auto baseTypeOfAssociatedValue = DeRef(*associatedValueType);
        if (baseTypeOfAssociatedValue->IsEnum() &&
            StaticCast<CGEnumType*>(CGType::GetOrCreate(cgMod, baseTypeOfAssociatedValue))->IsCommonEnum()) {
            return CGEnumTypeKind::EXHAUSTIVE_ASSOCIATED_OPTION_LIKE_REF;
        }
        if (baseTypeOfAssociatedValue->IsReferenceType()) {
            return CGEnumTypeKind::EXHAUSTIVE_ASSOCIATED_OPTION_LIKE_REF;
        } else {
            return CGEnumTypeKind::EXHAUSTIVE_ASSOCIATED_OPTION_LIKE_NONREF;
        }
    }
    if (!hasRefTypeAssociatedValue && chirEnumType.GetGenericArgs().empty()) {
        CJC_ASSERT(sizeOfConstructorUnion.has_value());
        return CGEnumTypeKind::EXHAUSTIVE_ASSOCIATED_NONREF;
    }
    return CGEnumTypeKind::EXHAUSTIVE_OTHER;
}

std::string CGEnumType::GetEnumTypeName() const
{
    switch (cgEnumTypeKind) {
        case CGEnumTypeKind::NON_EXHAUSTIVE_UNASSOCIATED:
        case CGEnumTypeKind::EXHAUSTIVE_UNASSOCIATED: {
            return ENUM_TYPE_PREFIX + "Trivial";
        }
        case CGEnumTypeKind::EXHAUSTIVE_ASSOCIATED_OPTION_LIKE_T: {
            return ENUM_TYPE_PREFIX + chirEnumType.GetEnumDef()->GetIdentifierWithoutPrefix() + "<T>";
        }
        case CGEnumTypeKind::EXHAUSTIVE_ASSOCIATED_OPTION_LIKE_REF: {
            return ENUM_TYPE_PREFIX + "OptionLike.Ref";
        }
        case CGEnumTypeKind::EXHAUSTIVE_ASSOCIATED_OPTION_LIKE_NONREF: {
            return ENUM_TYPE_PREFIX + GetTypeQualifiedName(chirEnumType);
        }
        case CGEnumTypeKind::EXHAUSTIVE_ASSOCIATED_NONREF: {
            return ENUM_TYPE_PREFIX + GetTypeQualifiedName(chirEnumType);
        }
        case CGEnumTypeKind::NON_EXHAUSTIVE_ASSOCIATED:
        case CGEnumTypeKind::EXHAUSTIVE_OTHER: {
            return ENUM_TYPE_PREFIX + "Common";
        }
        case CGEnumTypeKind::EXHAUSTIVE_ZERO_SIZE: {
            return ENUM_TYPE_PREFIX + "Empty";
        }
        default:
            CJC_ASSERT(false && "Should not reach here: UNKNOWN enum kind");
            return "";
    }
}

llvm::Type* CGEnumType::GenLLVMType()
{
    if (llvmType) {
        return llvmType;
    }

    switch (cgEnumTypeKind) {
        case CGEnumTypeKind::NON_EXHAUSTIVE_UNASSOCIATED:
        case CGEnumTypeKind::EXHAUSTIVE_UNASSOCIATED: {
            return GenTrivialLLVMType();
        }
        case CGEnumTypeKind::EXHAUSTIVE_ASSOCIATED_NONREF: {
            return GenAssociatedNonRefEnumLLVMType();
        }
        case CGEnumTypeKind::NON_EXHAUSTIVE_ASSOCIATED:
        case CGEnumTypeKind::EXHAUSTIVE_OTHER: {
            return GenCommonEnumLLVMType();
        }
        case CGEnumTypeKind::EXHAUSTIVE_ASSOCIATED_OPTION_LIKE_T: {
            return llvm::Type::getInt8Ty(cgCtx.GetLLVMContext());
        }
        case CGEnumTypeKind::EXHAUSTIVE_ASSOCIATED_OPTION_LIKE_REF: {
            return GenOptionLikeRefLLVMType();
        }
        case CGEnumTypeKind::EXHAUSTIVE_ASSOCIATED_OPTION_LIKE_NONREF: {
            return GenOptionLikeNonRefLLVMType();
        }
        case CGEnumTypeKind::EXHAUSTIVE_ZERO_SIZE: {
            return GenZeroSizeEnumLLVMType();
        }
        default: {
            CJC_ASSERT(false && "Should not reach here: UNKNOWN enum kind");
            return nullptr;
        }
    }
}

llvm::Type* CGEnumType::GenTrivialLLVMType()
{
    auto& llvmCtx = cgCtx.GetLLVMContext();
    const auto& typeName = GetEnumTypeName();
    layoutType = llvm::StructType::getTypeByName(llvmCtx, typeName);
    if (!layoutType) {
        layoutType = llvm::StructType::create(llvmCtx, typeName);
        SetStructTypeBody(layoutType, {llvm::Type::getInt32Ty(llvmCtx)});
    }
    return llvm::Type::getInt32Ty(llvmCtx);
}

llvm::Type* CGEnumType::GenCommonEnumLLVMType()
{
    auto& llvmCtx = cgCtx.GetLLVMContext();
    const auto& typeName = GetEnumTypeName();
    layoutType = llvm::StructType::getTypeByName(llvmCtx, typeName);
    if (!layoutType) {
        layoutType = llvm::StructType::create(llvmCtx, typeName);
        SetStructTypeBody(layoutType, {llvm::Type::getInt8PtrTy(llvmCtx, 1U)});
    }
    return GetRefType(llvmCtx);
}

llvm::Type* CGEnumType::GenZeroSizeEnumLLVMType()
{
    auto& llvmCtx = cgCtx.GetLLVMContext();
    const auto& typeName = GetEnumTypeName();
    layoutType = llvm::StructType::getTypeByName(llvmCtx, typeName);
    if (!layoutType) {
        layoutType = llvm::StructType::create(llvmCtx, {}, typeName);
    }
    return layoutType;
}

llvm::Type* CGEnumType::GenOptionLikeRefLLVMType()
{
    auto& llvmCtx = cgCtx.GetLLVMContext();
    const auto& typeName = GetEnumTypeName();
    layoutType = llvm::StructType::getTypeByName(llvmCtx, typeName);
    if (!layoutType) {
        layoutType = llvm::StructType::create(llvmCtx, typeName);
        auto i8Ty = llvm::Type::getInt8Ty(llvmCtx);
        SetStructTypeBody(layoutType, {llvm::Type::getInt8PtrTy(llvmCtx, 1U), llvm::ArrayType::get(i8Ty, 0U)});
    }
    cgCtx.AddGeneratedStructType(layoutType->getName().str());
    return GetRefType(llvmCtx);
}

llvm::Type* CGEnumType::GenOptionLikeNonRefLLVMType()
{
    CJC_NULLPTR_CHECK(optionLikeInfo);
    auto elemCGType = CGType::GetOrCreate(cgMod, optionLikeInfo->associatedValueType);
    auto& llvmCtx = cgCtx.GetLLVMContext();
    if (!elemCGType->GetSize()) {
        return llvm::Type::getInt8Ty(llvmCtx);
    }
    const auto& enumTypeName = GetEnumTypeName();
    layoutType = llvm::StructType::getTypeByName(llvmCtx, enumTypeName);
    llvmType = layoutType;
    if (llvmType && cgCtx.IsGeneratedStructType(enumTypeName)) {
        return llvmType;
    } else if (!llvmType) {
        layoutType = llvm::StructType::create(llvmCtx, enumTypeName);
        llvmType = layoutType;
    }
    cgCtx.AddGeneratedStructType(enumTypeName);

    std::vector<llvm::Type*> elemTypes{};
    elemTypes.emplace_back(llvm::Type::getInt1Ty(llvmCtx));
    elemTypes.emplace_back(elemCGType->GetLLVMType());
    SetStructTypeBody(llvm::cast<llvm::StructType>(llvmType), elemTypes);

    return llvmType;
}

llvm::Type* CGEnumType::GenAssociatedNonRefEnumLLVMType()
{
    auto& llvmCtx = cgCtx.GetLLVMContext();
    CJC_ASSERT(sizeOfConstructorUnion.has_value());
    llvmType =
        llvm::ArrayType::get(llvm::Type::getInt8Ty(llvmCtx), 4U + sizeOfConstructorUnion.value()); // 4U: size of int32

    const auto& enumTypeName = GetEnumTypeName();
    layoutType = llvm::StructType::getTypeByName(llvmCtx, enumTypeName);
    if (layoutType && cgCtx.IsGeneratedStructType(enumTypeName)) {
        return llvmType;
    } else if (!layoutType) {
        layoutType = llvm::StructType::create(llvmCtx, enumTypeName);
    }
    cgCtx.AddGeneratedStructType(enumTypeName);
    SetStructTypeBody(layoutType, std::vector<llvm::Type*>{llvmType});
    return llvmType;
}

void CGEnumType::GenContainedCGTypes()
{
    switch (cgEnumTypeKind) {
        case CGEnumTypeKind::NON_EXHAUSTIVE_ASSOCIATED:
        case CGEnumTypeKind::EXHAUSTIVE_OTHER: {
            containedCGTypes.emplace_back(CGType::GetInt32CGType(cgMod));
            break;
        }
        case CGEnumTypeKind::EXHAUSTIVE_ASSOCIATED_NONREF: {
            break;
        }
        case CGEnumTypeKind::EXHAUSTIVE_ASSOCIATED_OPTION_LIKE_T: {
            // Can't be determined at compile time.
            break;
        }
        case CGEnumTypeKind::EXHAUSTIVE_ASSOCIATED_OPTION_LIKE_REF: {
            CJC_NULLPTR_CHECK(optionLikeInfo);
            containedCGTypes.emplace_back(CGType::GetOrCreate(cgMod, optionLikeInfo->associatedValueType));
            break;
        }
        case CGEnumTypeKind::EXHAUSTIVE_ASSOCIATED_OPTION_LIKE_NONREF: {
            CJC_NULLPTR_CHECK(optionLikeInfo);
            containedCGTypes.emplace_back(CGType::GetBoolCGType(cgMod));
            containedCGTypes.emplace_back(CGType::GetOrCreate(cgMod, optionLikeInfo->associatedValueType));
            break;
        }
        default: {
            CJC_ASSERT(false && "Should not reach here: UNKNOWN enum kind");
            break;
        }
    }
}

void CGEnumType::CalculateSizeAndAlign()
{
    llvm::DataLayout layOut = cgMod.GetLLVMModule()->getDataLayout();
    switch (cgEnumTypeKind) {
        case CGEnumTypeKind::NON_EXHAUSTIVE_UNASSOCIATED:
        case CGEnumTypeKind::EXHAUSTIVE_UNASSOCIATED: {
            size = llvmType->getPrimitiveSizeInBits() / 8U;
            align = size;
            return;
        }
        case CGEnumTypeKind::NON_EXHAUSTIVE_ASSOCIATED:
        case CGEnumTypeKind::EXHAUSTIVE_OTHER:
        case CGEnumTypeKind::EXHAUSTIVE_ASSOCIATED_OPTION_LIKE_REF: {
            size = layOut.getTypeAllocSize(llvmType);
            align = layOut.getABITypeAlignment(llvmType);
            return;
        }
        case CGEnumTypeKind::EXHAUSTIVE_ASSOCIATED_NONREF: {
            CJC_ASSERT(sizeOfConstructorUnion.has_value());
            size = 4U + sizeOfConstructorUnion.value(); // 4U: size of int32
            align = 1U;
            return;
        }
        case CGEnumTypeKind::EXHAUSTIVE_ASSOCIATED_OPTION_LIKE_NONREF: {
            if (layoutType) {
                auto layOut = cgMod.GetLLVMModule()->getDataLayout();
                size = layOut.getTypeAllocSize(layoutType);
                align = layOut.getABITypeAlignment(layoutType);
            } else {
                size = std::nullopt;
                align = std::nullopt;
            }
            return;
        }
        case CGEnumTypeKind::EXHAUSTIVE_ASSOCIATED_OPTION_LIKE_T: {
            // size and align can't be determined at compile time
            size = std::nullopt;
            align = std::nullopt;
            return;
        }
        case CGEnumTypeKind::EXHAUSTIVE_ZERO_SIZE: {
            size = 0U;
            align = 1U;
            return;
        }
        default:    // unexpected CGEnumTypeKind
            CJC_ASSERT(false && "Should not reach here: UNKNOWN enum kind");
            return;
    }
}

llvm::Constant* CGEnumType::GenFieldsNumOfTypeInfo()
{
    switch (cgEnumTypeKind) {
        case CGEnumTypeKind::NON_EXHAUSTIVE_UNASSOCIATED:
        case CGEnumTypeKind::EXHAUSTIVE_UNASSOCIATED:
        case CGEnumTypeKind::NON_EXHAUSTIVE_ASSOCIATED:
        case CGEnumTypeKind::EXHAUSTIVE_OTHER: {
            return llvm::ConstantInt::get(llvm::Type::getInt16Ty(cgMod.GetLLVMContext()), 1U);
        }
        case CGEnumTypeKind::EXHAUSTIVE_ASSOCIATED_OPTION_LIKE_NONREF:
        case CGEnumTypeKind::EXHAUSTIVE_ASSOCIATED_OPTION_LIKE_REF:
        case CGEnumTypeKind::EXHAUSTIVE_ASSOCIATED_OPTION_LIKE_T: {
            return llvm::ConstantInt::get(llvm::Type::getInt16Ty(cgMod.GetLLVMContext()), 2U);
        }
        case CGEnumTypeKind::EXHAUSTIVE_ASSOCIATED_NONREF:
        case CGEnumTypeKind::EXHAUSTIVE_ZERO_SIZE: {
            return llvm::ConstantInt::get(llvm::Type::getInt16Ty(cgMod.GetLLVMContext()), 0U);
        }
        default:    // unexpected CGEnumTypeKind
            CJC_ASSERT(false && "Should not reach here: UNKNOWN enum kind");
            return nullptr;
    }
}

llvm::Constant* CGEnumType::GenFieldsOfTypeInfo()
{
    std::vector<CHIR::Type*> fieldTypes;
    switch (cgEnumTypeKind) {
        case CGEnumTypeKind::NON_EXHAUSTIVE_UNASSOCIATED:
        case CGEnumTypeKind::EXHAUSTIVE_UNASSOCIATED:
        case CGEnumTypeKind::NON_EXHAUSTIVE_ASSOCIATED:
        case CGEnumTypeKind::EXHAUSTIVE_OTHER: {
            fieldTypes.emplace_back(const_cast<CHIR::Type*>(&CGType::GetInt32CGType(cgMod)->GetOriginal()));
            break;
        }
        case CGEnumTypeKind::EXHAUSTIVE_ASSOCIATED_OPTION_LIKE_REF: {
            CJC_NULLPTR_CHECK(optionLikeInfo);
            fieldTypes.emplace_back(optionLikeInfo->associatedValueType);
            fieldTypes.emplace_back(const_cast<CHIR::Type*>(&CGType::GetZeroSizedCGType(cgMod)->GetOriginal()));
            break;
        }
        case CGEnumTypeKind::EXHAUSTIVE_ASSOCIATED_OPTION_LIKE_NONREF: {
            CJC_NULLPTR_CHECK(optionLikeInfo);
            fieldTypes.emplace_back(const_cast<CHIR::Type*>(&CGType::GetBoolCGType(cgMod)->GetOriginal()));
            fieldTypes.emplace_back(optionLikeInfo->associatedValueType);
            break;
        }
        case CGEnumTypeKind::EXHAUSTIVE_ASSOCIATED_NONREF:
        case CGEnumTypeKind::EXHAUSTIVE_ZERO_SIZE: {
            break;
        }
        case CGEnumTypeKind::EXHAUSTIVE_ASSOCIATED_OPTION_LIKE_T:
        default:
            CJC_ASSERT(false && "Should not reach here");
            break;
    }
    std::vector<llvm::Constant*> fieldConstants = GenTypeInfoConstantVectorForTypes(cgMod, fieldTypes);
    return GenTypeInfoArray(cgMod, CGType::GetNameOfTypeInfoGV(chirType) + ".fields", fieldConstants, CJTI_FIELDS_ATTR);
}

llvm::Constant* CGEnumType::GenOffsetsOfTypeInfo()
{
    if (cgEnumTypeKind == CGEnumTypeKind::EXHAUSTIVE_ASSOCIATED_NONREF) {
        return llvm::ConstantPointerNull::get(llvm::Type::getInt32PtrTy(cgMod.GetLLVMContext()));
    } else {
        return CGCustomType::GenOffsetsOfTypeInfo();
    }
}

llvm::Constant* CGEnumType::GenFieldsNumOfTypeTemplate()
{
    switch (cgEnumTypeKind) {
        case CGEnumTypeKind::NON_EXHAUSTIVE_UNASSOCIATED:
        case CGEnumTypeKind::EXHAUSTIVE_UNASSOCIATED:
        case CGEnumTypeKind::NON_EXHAUSTIVE_ASSOCIATED:
        case CGEnumTypeKind::EXHAUSTIVE_OTHER: {
            return llvm::ConstantInt::get(llvm::Type::getInt16Ty(cgMod.GetLLVMContext()), 1U);
        }
        case CGEnumTypeKind::EXHAUSTIVE_ASSOCIATED_OPTION_LIKE_T:
        case CGEnumTypeKind::EXHAUSTIVE_ASSOCIATED_OPTION_LIKE_REF:
        case CGEnumTypeKind::EXHAUSTIVE_ASSOCIATED_OPTION_LIKE_NONREF: {
            return llvm::ConstantInt::get(llvm::Type::getInt16Ty(cgMod.GetLLVMContext()), 2U);
        }
        case CGEnumTypeKind::EXHAUSTIVE_ZERO_SIZE: {
            return llvm::ConstantInt::get(llvm::Type::getInt16Ty(cgMod.GetLLVMContext()), 0U);
        }
        case CGEnumTypeKind::EXHAUSTIVE_ASSOCIATED_NONREF:
        default:
            CJC_ASSERT(false && "Should not reach here: UNKNOWN enum kind");
            return nullptr;
    }
}

llvm::Constant* GenFieldsFnsOfTypeTemplateForOptionLikeT(CGModule& cgMod, const std::string& funcPrefixName)
{
    auto& llvmCtx = cgMod.GetLLVMContext();
    auto p0i8 = llvm::Type::getInt8PtrTy(llvmCtx);
    if (auto gv = cgMod.GetLLVMModule()->getNamedGlobal(funcPrefixName + ".fieldTiFns");
        gv && gv->hasInitializer()) {
        return llvm::ConstantExpr::getBitCast(gv, p0i8);
    }

    auto typeInfoPtrType = CGType::GetOrCreateTypeInfoPtrType(llvmCtx);
    std::vector<llvm::Type*> argTypes{llvm::Type::getInt32Ty(llvmCtx), typeInfoPtrType->getPointerTo()};
    auto fieldFnType = llvm::FunctionType::get(p0i8, argTypes, false);

    // 1. create fieldTiFn 0
    auto getTiFn1 = llvm::Function::Create(
        fieldFnType, llvm::Function::PrivateLinkage, funcPrefixName + ".fieldTiFn.0", cgMod.GetLLVMModule());
    getTiFn1->addFnAttr("native-interface-fn");
    CodeGen::IRBuilder2 irBuilder(cgMod);
    auto entryBB = irBuilder.CreateEntryBasicBlock(getTiFn1, "entry");
    irBuilder.SetInsertPoint(entryBB);
    auto ti = irBuilder.LLVMIRBuilder2::CreateLoad(typeInfoPtrType, getTiFn1->getArg(1));
    auto isRef = irBuilder.CreateTypeInfoIsReferenceCall(ti);
    auto [refBB, nonRefBB] = Vec2Tuple<2>(irBuilder.CreateAndInsertBasicBlocks({"ref", "nonRef"}));
    irBuilder.CreateCondBr(isRef, refBB, nonRefBB);
    irBuilder.SetInsertPoint(refBB);
    irBuilder.CreateRet(irBuilder.CreateBitCast(ti, p0i8));
    irBuilder.SetInsertPoint(nonRefBB);
    irBuilder.CreateRet(irBuilder.CreateBitCast(CGType::GetBoolCGType(cgMod)->GetOrCreateTypeInfo(), p0i8));

    // 2. create fieldTiFn 1
    auto getTiFn2 = llvm::Function::Create(
        fieldFnType, llvm::Function::PrivateLinkage, funcPrefixName + ".fieldTiFn.1", cgMod.GetLLVMModule());
    getTiFn2->addFnAttr("native-interface-fn");
    entryBB = irBuilder.CreateEntryBasicBlock(getTiFn2, "entry");
    irBuilder.SetInsertPoint(entryBB);
    ti = irBuilder.LLVMIRBuilder2::CreateLoad(typeInfoPtrType, getTiFn2->getArg(1));
    isRef = irBuilder.CreateTypeInfoIsReferenceCall(ti);
    auto [refBB2, nonRefBB2] = Vec2Tuple<2>(irBuilder.CreateAndInsertBasicBlocks({"ref", "nonRef"}));
    irBuilder.CreateCondBr(isRef, refBB2, nonRefBB2);
    irBuilder.SetInsertPoint(refBB2);
    irBuilder.CreateRet(
        irBuilder.CreateBitCast(CGType::GetZeroSizedCGType(cgMod)->GetOrCreateTypeInfo(), p0i8));
    irBuilder.SetInsertPoint(nonRefBB2);
    irBuilder.CreateRet(irBuilder.CreateBitCast(ti, p0i8));

    return CGTypeInfo::GenFieldsFnsOfTypeTemplate(cgMod, funcPrefixName, {getTiFn1, getTiFn2});
}

llvm::Constant* CGEnumType::GenFieldsFnsOfTypeTemplate()
{
    auto funcPrefixName = CGType::GetNameOfTypeTemplateGV(chirType);
    std::vector<CHIR::Type*> fieldTypes;
    std::unordered_map<const CHIR::GenericType*, size_t> localGenericParamIndicesMap;
    switch (cgEnumTypeKind) {
        case CGEnumTypeKind::NON_EXHAUSTIVE_UNASSOCIATED:
        case CGEnumTypeKind::EXHAUSTIVE_UNASSOCIATED:
        case CGEnumTypeKind::NON_EXHAUSTIVE_ASSOCIATED:
        case CGEnumTypeKind::EXHAUSTIVE_OTHER: {
            fieldTypes.emplace_back(const_cast<CHIR::Type*>(&CGType::GetInt32CGType(cgMod)->GetOriginal()));
            break;
        }
        case CGEnumTypeKind::EXHAUSTIVE_ASSOCIATED_OPTION_LIKE_REF: {
            fieldTypes.emplace_back(
                CGType::GetRefTypeOf(cgCtx.GetCHIRBuilder(), CGType::GetObjectCGType(cgMod)->GetOriginal()));
            fieldTypes.emplace_back(const_cast<CHIR::Type*>(&CGType::GetZeroSizedCGType(cgMod)->GetOriginal()));
            break;
        }
        case CGEnumTypeKind::EXHAUSTIVE_ASSOCIATED_OPTION_LIKE_NONREF: {
            CJC_NULLPTR_CHECK(optionLikeInfo);
            fieldTypes.emplace_back(const_cast<CHIR::Type*>(&CGType::GetBoolCGType(cgMod)->GetOriginal()));
            fieldTypes.emplace_back(optionLikeInfo->associatedValueType);
            std::size_t gTIdx = 0;
            for (auto typeArg : GetGenericArgsFromCHIRType(chirEnumType)) {
                auto gt = typeArg.GetGenericType();
                localGenericParamIndicesMap.emplace(gt, gTIdx++);
            }
            break;
        }
        case CGEnumTypeKind::EXHAUSTIVE_ASSOCIATED_OPTION_LIKE_T: {
            return GenFieldsFnsOfTypeTemplateForOptionLikeT(cgMod, funcPrefixName);
        }
        case CGEnumTypeKind::EXHAUSTIVE_ZERO_SIZE: {
            break;
        }
        case CGEnumTypeKind::EXHAUSTIVE_ASSOCIATED_NONREF:
        default:
            CJC_ASSERT(false && "Should not reach here: UNKNOWN enum kind");
            break;
    }
    return CGTypeInfo::GenFieldsFnsOfTypeTemplate(cgMod, funcPrefixName, fieldTypes, localGenericParamIndicesMap);
}
} // namespace Cangjie::CodeGen
