// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "Base/CGTypes/CGCustomType.h"

#include "Base/CGTypes/CGEnumType.h"
#include "CGModule.h"
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
#include "CJNative/CGTypeInfo.h"
#endif
#include "DIBuilder.h"
#include "Utils/CGUtils.h"
#include "cangjie/CHIR/Utils/CHIRCasting.h"

namespace Cangjie {
namespace CodeGen {
CGCustomType::CGCustomType(CGModule& cgMod, CGContext& cgCtx, const CHIR::Type& chirType, CGTypeKind cgTypeKind)
    : CGType(cgMod, cgCtx, chirType, cgTypeKind)
{
}

llvm::Constant* CGCustomType::GenFieldsNumOfTypeInfo()
{
    auto& customType = StaticCast<const CHIR::CustomType&>(chirType);
    auto& nonConstCustomType = const_cast<CHIR::CustomType&>(customType);
    auto fieldsNum = nonConstCustomType.GetInstantiatedMemberTys(cgMod.GetCGContext().GetCHIRBuilder()).size() +
        (chirType.IsAutoEnv() ? 2U : 0U);
    CJC_ASSERT(layoutType->getNumElements() == fieldsNum);
    return llvm::ConstantInt::get(llvm::Type::getInt16Ty(cgMod.GetLLVMContext()), fieldsNum);
}

std::vector<llvm::Constant*> CGCustomType::GenTypeInfoConstantVectorForTypes(
    CGModule& cgMod, const std::vector<CHIR::Type*>& chirTypes)
{
    std::vector<llvm::Constant*> fieldConstants;
    for (auto type : chirTypes) {
        auto derefType = DeRef(*type);
        (void)fieldConstants.emplace_back(CGType::GetOrCreate(cgMod, derefType)->GetOrCreateTypeInfo());
    }
    return fieldConstants;
}

llvm::Constant* CGCustomType::GenTypeInfoArray(
    CGModule& cgMod, std::string name, std::vector<llvm::Constant*> constants, const std::string_view& attr)
{
    auto p0i8 = llvm::Type::getInt8PtrTy(cgMod.GetLLVMContext());
    if (constants.empty()) {
        return llvm::Constant::getNullValue(p0i8);
    }

    auto typeInfoPtrTy = CGType::GetOrCreateTypeInfoPtrType(cgMod.GetLLVMContext());
    auto typeOfFieldsGV = llvm::ArrayType::get(typeInfoPtrTy, constants.size());
    auto typeInfoOfFields =
        llvm::cast<llvm::GlobalVariable>(cgMod.GetLLVMModule()->getOrInsertGlobal(name, typeOfFieldsGV));
    typeInfoOfFields->setInitializer(llvm::ConstantArray::get(typeOfFieldsGV, constants));
    typeInfoOfFields->setLinkage(llvm::GlobalValue::LinkageTypes::PrivateLinkage);
    typeInfoOfFields->addAttribute(attr);
    return llvm::ConstantExpr::getBitCast(typeInfoOfFields, p0i8);
}

llvm::Constant* CGCustomType::GenFieldsOfTypeInfo()
{
    CJC_ASSERT(!chirType.IsEnum() && "CGEnumType should override this method.");
    auto& customType = StaticCast<const CHIR::CustomType&>(chirType);
    auto& nonConstCustomType = const_cast<CHIR::CustomType&>(customType);
    auto instanceMemberTypes = nonConstCustomType.GetInstantiatedMemberTys(cgMod.GetCGContext().GetCHIRBuilder());
    std::vector<llvm::Constant*> fieldConstants = GenTypeInfoConstantVectorForTypes(cgMod, instanceMemberTypes);
    if (chirType.IsAutoEnv()) {
        CJC_ASSERT(!chirType.IsAutoEnvInstBase());
        (void)fieldConstants.insert(fieldConstants.begin(), CGType::GetInt64CGType(cgMod)->GetOrCreateTypeInfo());
        (void)fieldConstants.insert(fieldConstants.begin(), CGType::GetInt64CGType(cgMod)->GetOrCreateTypeInfo());
    }
    return GenTypeInfoArray(cgMod, CGType::GetNameOfTypeInfoGV(chirType) + ".fields", fieldConstants, CJTI_FIELDS_ATTR);
}

llvm::Constant* CGCustomType::GenOffsetsArray(CGModule& cgMod, std::string name, llvm::StructType* layoutType)
{
    auto i32Ty = llvm::Type::getInt32Ty(cgMod.GetLLVMContext());
    if (layoutType->elements().empty()) {
        return llvm::ConstantPointerNull::get(i32Ty->getPointerTo());
    }

    auto arrLength = layoutType->getNumElements();
    auto i32ArrType = llvm::ArrayType::get(i32Ty, arrLength);
    std::vector<llvm::Constant*> elements(arrLength);
    auto structLayout = cgMod.GetLLVMModule()->getDataLayout().getStructLayout(layoutType);
    for (size_t idx = 0; idx < arrLength; ++idx) {
        elements[idx] = llvm::ConstantInt::get(i32Ty, structLayout->getElementOffset(idx));
    }
    auto typeInfoOfFields =
        llvm::cast<llvm::GlobalVariable>(cgMod.GetLLVMModule()->getOrInsertGlobal(name, i32ArrType));
    typeInfoOfFields->setInitializer(llvm::ConstantArray::get(i32ArrType, elements));
    typeInfoOfFields->setLinkage(llvm::GlobalValue::LinkageTypes::PrivateLinkage);
    typeInfoOfFields->addAttribute(CJTI_OFFSETS_ATTR);
    return llvm::ConstantExpr::getBitCast(typeInfoOfFields, i32Ty->getPointerTo());
}

llvm::Constant* CGCustomType::GenOffsetsOfTypeInfo()
{
    CJC_NULLPTR_CHECK(layoutType);
    return GenOffsetsArray(cgMod, CGType::GetNameOfTypeInfoGV(chirType) + ".offsets", layoutType);
}

llvm::Constant* CGCustomType::GenSourceGenericOfTypeInfo()
{
    return CGType::GenSourceGenericOfTypeInfo();
}

llvm::Constant* CGCustomType::GenTypeArgsNumOfTypeInfo()
{
    if (chirType.IsAutoEnvBase()) {
        CJC_ASSERT(!chirType.IsAutoEnvInstBase());
        return llvm::ConstantInt::get(llvm::Type::getInt8Ty(cgMod.GetLLVMContext()), 1U);
    } else {
        return llvm::ConstantInt::get(llvm::Type::getInt8Ty(cgMod.GetLLVMContext()),
            StaticCast<const CHIR::CustomType&>(chirType).GetGenericArgs().size());
    }
}

llvm::Constant* CGCustomType::GenTypeArgsOfTypeInfo()
{
    auto genericArgs = StaticCast<const CHIR::CustomType&>(chirType).GetGenericArgs();
    auto p0i8 = llvm::Type::getInt8PtrTy(cgMod.GetLLVMContext());
    if (genericArgs.empty()) {
        return llvm::ConstantPointerNull::get(p0i8);
    }

    std::vector<llvm::Constant*> constants;
    if (chirType.IsAutoEnvBase()) {
        CJC_ASSERT(!chirType.IsAutoEnvInstBase());
        std::vector<CHIR::Type*> paramTypes(genericArgs.begin(), genericArgs.end() - 1);
        auto chirFuncType = cgCtx.GetCHIRBuilder().GetType<CHIR::FuncType>(paramTypes, genericArgs.back());
        auto cgTypeOfTypeArg = CGType::GetOrCreate(cgMod, chirFuncType);
        auto it = constants.emplace_back(cgTypeOfTypeArg->GetOrCreateTypeInfo());
        if (cgTypeOfTypeArg->IsStaticGI()) {
            cgCtx.AddDependentPartialOrderOfTypes(it, this->typeInfo);
        }
    } else {
        for (auto typeArg : genericArgs) {
            auto cgTypeOfTypeArg = CGType::GetOrCreate(cgMod, DeRef(*typeArg));
            auto it = constants.emplace_back(cgTypeOfTypeArg->GetOrCreateTypeInfo());
            if (cgTypeOfTypeArg->IsStaticGI()) {
                cgCtx.AddDependentPartialOrderOfTypes(it, this->typeInfo);
            }
        }
    }
    auto typeInfoPtrTy = CGType::GetOrCreateTypeInfoPtrType(cgMod.GetLLVMContext());
    auto typeOfGenericArgsGV = llvm::ArrayType::get(typeInfoPtrTy, constants.size());
    auto typeInfoOfGenericArgs = llvm::cast<llvm::GlobalVariable>(cgMod.GetLLVMModule()->getOrInsertGlobal(
        CGType::GetNameOfTypeInfoGV(chirType) + ".typeArgs", typeOfGenericArgsGV));
    typeInfoOfGenericArgs->setInitializer(llvm::ConstantArray::get(typeOfGenericArgsGV, constants));
    typeInfoOfGenericArgs->setLinkage(llvm::GlobalValue::LinkageTypes::PrivateLinkage);
    typeInfoOfGenericArgs->addAttribute(CJTI_TYPE_ARGS_ATTR);
    return llvm::ConstantExpr::getBitCast(typeInfoOfGenericArgs, p0i8);
}

llvm::Constant* CGCustomType::GenNameOfTypeTemplate()
{
    auto& customType = static_cast<const CHIR::CustomType&>(chirType);
    auto customDefShortName = CHIR::GetCustomTypeIdentifier(customType);
    auto customDef = customType.GetCustomTypeDef();
    // to ensure compatibility, when dealing with scenarios involving package name containing organization name,
    // we need to generate names as follows:
    // "orgName::pkgName:xxx.ti.name" = "orgName/pkgName:xxx.ti"
    // oldPrefix is "orgName::pkgName" and newPrefix is "orgName/pkgName"
    auto packageName = customDef->GetPackageName();
    auto oldPrefix = (chirType.IsAutoEnvBase() ? "" : (packageName + ":"));
    ReplaceDelimiterAfterOrgName(packageName);
    auto newPrefix = (chirType.IsAutoEnvBase() ? "" : (packageName + ":"));
    return cgMod.GenerateTypeNameConstantString(oldPrefix + customDefShortName, false, newPrefix + customDefShortName);
}

llvm::Constant* CGCustomType::GenFieldsNumOfTypeTemplate()
{
    CJC_ASSERT(!chirType.IsEnum() && "CGEnumType should override this method.");
    auto& customType = static_cast<const CHIR::CustomType&>(chirType);
    auto& nonConstCustomType = const_cast<CHIR::CustomType&>(customType);
    auto fieldsNum = nonConstCustomType.GetInstantiatedMemberTys(cgMod.GetCGContext().GetCHIRBuilder()).size();
    if (chirType.IsAutoEnv()) {
        CJC_ASSERT(!chirType.IsAutoEnvBase());
        fieldsNum += 2U;
    }
    return llvm::ConstantInt::get(llvm::Type::getInt16Ty(cgMod.GetLLVMContext()), fieldsNum);
}

llvm::Constant* CGCustomType::GenFieldsFnsOfTypeTemplate()
{
    auto customTypeDef = static_cast<const CHIR::CustomType&>(chirType).GetCustomTypeDef();
    std::unordered_map<const CHIR::GenericType*, size_t> localGenericParamIndicesMap;

    size_t gTIdx = 0;
    for (auto chirGT : customTypeDef->GetGenericTypeParams()) {
        localGenericParamIndicesMap.emplace(chirGT, gTIdx);
        ++gTIdx;
    }

    CJC_ASSERT(!customTypeDef->IsExtend());
    auto fieldTypes = StaticCast<CHIR::CustomType*>(
        customTypeDef->GetType())->GetInstantiatedMemberTys(cgMod.GetCGContext().GetCHIRBuilder());
    if (chirType.IsAutoEnv()) {
        auto chirInt64Type = const_cast<CHIR::Type*>(&CGType::GetInt64CGType(cgMod)->GetOriginal());
        fieldTypes.insert(fieldTypes.begin(), chirInt64Type);
        fieldTypes.insert(fieldTypes.begin(), chirInt64Type);
    }

    return CGTypeInfo::GenFieldsFnsOfTypeTemplate(
        cgMod, CGType::GetNameOfTypeTemplateGV(chirType), fieldTypes, localGenericParamIndicesMap);
}

llvm::Constant* CGCustomType::GenSuperFnOfTypeTemplate()
{
    // classType will override this function
    auto i8PtrTy = llvm::Type::getInt8PtrTy(cgMod.GetLLVMContext());
    return llvm::ConstantPointerNull::get(i8PtrTy);
}

llvm::Constant* CGCustomType::GenFinalizerOfTypeTemplate()
{
    // classType will override this function
    auto i8PtrTy = llvm::Type::getInt8PtrTy(cgMod.GetLLVMContext());
    return llvm::ConstantPointerNull::get(i8PtrTy);
}

llvm::Constant* CGCustomType::GenKindOfTypeTemplate()
{
    static const std::unordered_map<CHIR::Type::TypeKind, unsigned> CHIR_TYPE2TYPE_INFO_KIND = {
        // reference type
        {CHIR::Type::TypeKind::TYPE_RAWARRAY, UGTypeKind::UG_RAWARRAY},
        {CHIR::Type::TypeKind::TYPE_FUNC, UGTypeKind::UG_CFUNC},
        // value type
        {CHIR::Type::TypeKind::TYPE_TUPLE, UGTypeKind::UG_TUPLE},
        {CHIR::Type::TypeKind::TYPE_STRUCT, UGTypeKind::UG_STRUCT},
        {CHIR::Type::TypeKind::TYPE_ENUM, UGTypeKind::UG_ENUM},
        {CHIR::Type::TypeKind::TYPE_VARRAY, UGTypeKind::UG_VARRAY},
        {CHIR::Type::TypeKind::TYPE_CPOINTER, UGTypeKind::UG_CPOINTER}};
    unsigned typeInfoKind;
    if (chirType.IsAutoEnvBase()) {
        typeInfoKind = UGTypeKind::UG_FUNC;
    } else if (chirType.IsClass()) {
        auto classDef = StaticCast<const CHIR::ClassType&>(chirType).GetClassDef();
        if (IsWeakRefClass(*classDef)) {
            typeInfoKind = UGTypeKind::UG_WEAKREF;
        } else {
            typeInfoKind = classDef->IsClass() ? UGTypeKind::UG_CLASS : UGTypeKind::UG_INTERFACE;
        }
    } else if (chirType.IsEnum()) {
        typeInfoKind =
            static_cast<CGEnumType*>(this)->IsCommonEnum() ? UGTypeKind::UG_COMMON_ENUM : UGTypeKind::UG_ENUM;
    } else {
        typeInfoKind = CHIR_TYPE2TYPE_INFO_KIND.at(chirType.GetTypeKind());
    }
    return llvm::ConstantInt::get(llvm::Type::getInt8Ty(cgMod.GetLLVMContext()), typeInfoKind);
}

llvm::Constant* CGCustomType::GenTypeArgsNumOfTypeTemplate()
{
    return llvm::ConstantInt::get(llvm::Type::getInt16Ty(cgMod.GetLLVMContext()),
        StaticCast<const CHIR::CustomType&>(chirType).GetGenericArgs().size());
}

void CGCustomType::GenTypeTemplate()
{
    auto& customType = StaticCast<const CHIR::CustomType&>(chirType);
    auto customTypeDef = customType.GetCustomTypeDef();

    CJC_ASSERT(customTypeDef->IsGenericDef() && "Should be a generic type here.");
    CJC_ASSERT(typeTemplate);

    PreActionOfGenTypeTemplate();

    if (typeTemplate->hasInitializer()) {
        return;
    }

    if (cgMod.GetCGContext().IsCustomTypeOfOtherLLVMModule(customType) || chirType.IsAutoEnvBase()) {
        if (customTypeDef->Get<CHIR::LinkTypeInfo>() == Linkage::EXTERNAL_WEAK) {
            typeTemplate->setLinkage(llvm::GlobalValue::ExternalWeakLinkage);
        }
        return;
    }

    auto& llvmCtx = cgMod.GetLLVMContext();
    auto i8Ty = llvm::Type::getInt8Ty(llvmCtx);

    std::vector<llvm::Constant*> typeTemplateVec(TYPE_TEMPLATE_FIELDS_NUM);
    typeTemplateVec[static_cast<size_t>(TYPETEMPLATE_NAME)] = GenNameOfTypeTemplate();
    typeTemplateVec[static_cast<size_t>(TYPETEMPLATE_TYPE_KIND)] = GenKindOfTypeTemplate();
    typeTemplateVec[static_cast<size_t>(TYPETEMPLATE_TYPE_ARGS_NUM)] = GenTypeArgsNumOfTypeTemplate();
    typeTemplateVec[static_cast<size_t>(TYPETEMPLATE_FIELDS_NUM)] = GenFieldsNumOfTypeTemplate();
    typeTemplateVec[static_cast<size_t>(TYPETEMPLATE_FIELDS_FNS)] = GenFieldsFnsOfTypeTemplate();
    typeTemplateVec[static_cast<size_t>(TYPETEMPLATE_SUPER_FN)] = GenSuperFnOfTypeTemplate();
    typeTemplateVec[static_cast<size_t>(TYPETEMPLATE_FINALIZER)] = GenFinalizerOfTypeTemplate();
    typeTemplateVec[static_cast<size_t>(TYPETEMPLATE_REFLECTION)] = GenReflectionOfTypeInfo();
    typeTemplateVec[static_cast<size_t>(TYPETEMPLATE_FLAG)] = llvm::ConstantInt::get(i8Ty, 0);
    typeTemplateVec[static_cast<size_t>(TYPETEMPLATE_EXTENSIONDEF_PTR)] = GenExtensionDefPtrOfTypeInfo();
    typeTemplateVec[static_cast<size_t>(TYPETEMPLATE_INHERITED_CLASS_NUM)] = GenInheritedClassNumOfTypeInfo();

    typeTemplate->setInitializer(
        llvm::ConstantStruct::get(CGType::GetOrCreateTypeTemplateType(llvmCtx), typeTemplateVec));
    typeTemplate->addAttribute(TYPE_TEMPLATE_ATTR);
    llvm::GlobalValue::LinkageTypes linkageType = CHIRLinkage2LLVMLinkage(customTypeDef->Get<CHIR::LinkTypeInfo>());
    if (linkageType == llvm::GlobalValue::InternalLinkage) {
        linkageType = llvm::GlobalValue::PrivateLinkage;
    }
    AddLinkageTypeMetadata(*typeTemplate, linkageType, cgMod.GetCGContext().IsCGParallelEnabled());
    cgMod.diBuilder->CreateAnonymousTypeForGenericType(typeTemplate, chirType);

    PostActionOfGenTypeTemplate();
}

bool CGCustomType::IsSized() const
{
    auto& customType = static_cast<const CHIR::CustomType&>(chirType);
    auto& nonConstCustomType = const_cast<CHIR::CustomType&>(customType);
    const auto memberVars = nonConstCustomType.GetInstantiatedMemberTys(cgMod.GetCGContext().GetCHIRBuilder());
    for (auto memberVar : memberVars) {
        if (memberVar->IsRef() || memberVar->IsCPointer() || memberVar->IsCFunc()) {
            continue;
        }
        if (!CGType::GetOrCreate(cgMod, memberVar)->GetSize()) {
            return false;
        }
    }
    return true;
}

llvm::Constant* CGCustomType::GenReflectionOfTypeInfo()
{
    return llvm::ConstantPointerNull::get(llvm::Type::getInt8PtrTy(cgMod.GetLLVMContext()));
}
} // namespace CodeGen
} // namespace Cangjie
