// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "CJNative/CGTypes/EnumCtorTIOrTTGenerator.h"

#include "Base/CGTypes/CGEnumType.h"
#include "CGModule.h"
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
#include "CJNative/CGTypeInfo.h"
#endif
#include "IRBuilder.h"
#include "cangjie/CHIR/Type/EnumDef.h"

namespace Cangjie::CodeGen {
EnumCtorTIOrTTGenerator::EnumCtorTIOrTTGenerator(
    CGModule& cgMod, const CHIR::EnumType& chirEnumType, std::size_t ctorIndex)
    : cgMod(cgMod), cgCtx(cgMod.GetCGContext()), chirEnumType(chirEnumType), ctorIndex(ctorIndex)
{
}

llvm::Constant* EnumCtorTIOrTTGenerator::GenTypeArgsNumOfTypeInfo()
{
    return llvm::ConstantInt::get(llvm::Type::getInt8Ty(cgMod.GetLLVMContext()), chirEnumType.GetGenericArgs().size());
}

llvm::Constant* EnumCtorTIOrTTGenerator::GenTypeArgsOfTypeInfo()
{
    auto& llvmCtx = cgMod.GetLLVMContext();
    auto i8Ptr = llvm::Type::getInt8PtrTy(llvmCtx);
    auto typeArgsNum = chirEnumType.GetGenericArgs().size();
    if (!typeArgsNum) {
        return llvm::ConstantPointerNull::get(i8Ptr);
    }

    auto typeInfoPtrTy = CGType::GetOrCreateTypeInfoPtrType(llvmCtx);
    auto typeOfGenericArgsGV = llvm::ArrayType::get(typeInfoPtrTy, typeArgsNum);
    auto typeInfoOfGenericArgs = llvm::cast<llvm::GlobalVariable>(cgMod.GetLLVMModule()->getOrInsertGlobal(
        CGType::GetNameOfTypeInfoGV(chirEnumType) + ".typeArgs", typeOfGenericArgsGV));
    typeInfoOfGenericArgs->addAttribute(CJTI_TYPE_ARGS_ATTR);
    AddLinkageTypeMetadata(*typeInfoOfGenericArgs, llvm::GlobalVariable::PrivateLinkage, cgCtx.IsCGParallelEnabled());
    return llvm::ConstantExpr::getBitCast(typeInfoOfGenericArgs, llvm::Type::getInt8PtrTy(llvmCtx));
}

llvm::Constant* EnumCtorTIOrTTGenerator::GenSourceGenericOfTypeInfo()
{
    auto p0i8 = llvm::Type::getInt8PtrTy(cgMod.GetLLVMContext());
    if (chirEnumType.GetCustomTypeDef()->IsGenericDef()) {
        auto genericDecl = chirEnumType.GetCustomTypeDef();
        return llvm::ConstantExpr::getBitCast(
            cgMod.GetOrCreateEnumCtorTIOrTT(*StaticCast<CHIR::EnumType*>(genericDecl->GetType()), ctorIndex), p0i8);
    } else {
        return llvm::ConstantPointerNull::get(p0i8);
    }
}

void EnumCtorTIOrTTGenerator::GenerateNonGenericEnumCtorTypeInfo(llvm::GlobalVariable& ti)
{
    auto& llvmCtx = cgMod.GetLLVMContext();
    auto i8Ty = llvm::Type::getInt8Ty(llvmCtx);
    auto i16Ty = llvm::Type::getInt16Ty(llvmCtx);
    auto i32Ty = llvm::Type::getInt32Ty(llvmCtx);
    auto i8PtrNull = llvm::ConstantPointerNull::get(i8Ty->getPointerTo());

    auto tiName = ti.getName().str();
    auto className = tiName.substr(0, tiName.size() - 3); // 3: the length of ".ti" or ".tt"
    const auto& ctors = chirEnumType.GetConstructorInfos(cgMod.GetCGContext().GetCHIRBuilder());
    CJC_ASSERT(ctorIndex < ctors.size());
    const auto& paramTypes = ctors[ctorIndex].funcType->GetParamTypes();
    std::vector<CHIR::Type*> fieldsTypes;
    StaticCast<CGEnumType*>(CGType::GetOrCreate(cgMod, &chirEnumType))->IsOptionLike()
        ? fieldsTypes.emplace_back(cgMod.GetCGContext().GetCHIRBuilder().GetBoolTy())
        : fieldsTypes.emplace_back(cgMod.GetCGContext().GetCHIRBuilder().GetInt32Ty());
    fieldsTypes.insert(fieldsTypes.end(), paramTypes.cbegin(), paramTypes.cend());
    std::vector<llvm::Constant*> typeInfoVec(TYPE_INFO_FIELDS_NUM);
    typeInfoVec[static_cast<size_t>(TYPEINFO_NAME)] = cgMod.GenerateTypeNameConstantString(className, false);
    typeInfoVec[static_cast<size_t>(TYPEINFO_TYPE_KIND)] = llvm::ConstantInt::get(i8Ty, UGTypeKind::UG_COMMON_ENUM);
    typeInfoVec[static_cast<size_t>(TYPEINFO_FIELDS_NUM)] = llvm::ConstantInt::get(i16Ty, fieldsTypes.size());

    auto fieldConstants = CGCustomType::GenTypeInfoConstantVectorForTypes(cgMod, fieldsTypes);
    typeInfoVec[static_cast<size_t>(TYPEINFO_FIELDS)] =
        CGCustomType::GenTypeInfoArray(cgMod, tiName + ".fields", fieldConstants, CJTI_FIELDS_ATTR);

    auto layoutType = GetLLVMStructType(cgMod, fieldsTypes, GetClassObjLayoutName(className));
    typeInfoVec[static_cast<size_t>(TYPEINFO_SIZE)] = llvm::ConstantInt::get(i32Ty, 8U);
    typeInfoVec[static_cast<size_t>(TYPEINFO_UUID)] = llvm::Constant::getNullValue(i32Ty);
    typeInfoVec[static_cast<size_t>(TYPEINFO_ALIGN)] = llvm::ConstantInt::get(i8Ty, 8U);
    typeInfoVec[static_cast<size_t>(TYPEINFO_SOURCE_GENERIC)] = GenSourceGenericOfTypeInfo();
    typeInfoVec[static_cast<size_t>(TYPEINFO_TYPE_ARGS_NUM)] = GenTypeArgsNumOfTypeInfo();
    typeInfoVec[static_cast<size_t>(TYPEINFO_INHERITED_CLASS_NUM)] =
        llvm::ConstantInt::get(i16Ty, INHERITED_CLASS_NUM_FE_FLAG);
    typeInfoVec[static_cast<size_t>(TYPEINFO_OFFSETS)] =
        CGCustomType::GenOffsetsArray(cgMod, tiName + ".offsets", layoutType);
    typeInfoVec[static_cast<size_t>(TYPEINFO_TYPE_ARGS)] = GenTypeArgsOfTypeInfo();
    typeInfoVec[static_cast<size_t>(TYPEINFO_SUPER)] =
        CGType::GetOrCreate(cgMod, &chirEnumType)->GetOrCreateTypeInfo();
    typeInfoVec[static_cast<size_t>(TYPEINFO_EXTENSIONDEF_PTR)] =
        llvm::ConstantPointerNull::get(CGType::GetOrCreateExtensionDefPtrType(llvmCtx)->getPointerTo());
    typeInfoVec[static_cast<size_t>(TYPEINFO_MTABLE)] = i8PtrNull;
    typeInfoVec[static_cast<size_t>(TYPEINFO_REFLECTION)] = i8PtrNull;
    typeInfoVec[static_cast<size_t>(TYPEINFO_GC_TIB)] =
        llvm::ConstantPointerNull::get(CGType::GetBitMapType(llvmCtx)->getPointerTo());
    typeInfoVec[static_cast<size_t>(TYPEINFO_FLAG)] =
        llvm::ConstantInt::get(llvm::Type::getInt8Ty(cgMod.GetLLVMContext()), 0);

    ti.setInitializer(llvm::ConstantStruct::get(CGType::GetOrCreateTypeInfoType(llvmCtx), typeInfoVec));
    ti.addAttribute(GC_KLASS_ATTR);
    auto meta = llvm::MDTuple::get(llvmCtx, {llvm::MDString::get(llvmCtx, layoutType->getStructName().str())});
    ti.setMetadata(GC_TYPE_META_NAME, meta);
    // This line seems only for Parallel-Compilation:
    cgCtx.AddGeneratedStructType(layoutType->getStructName().str());
    cgCtx.RegisterStaticGIName(ti.getName());
}

llvm::Constant* EnumCtorTIOrTTGenerator::GenTypeArgsNumOfTypeTemplate()
{
    return llvm::ConstantInt::get(llvm::Type::getInt16Ty(cgMod.GetLLVMContext()), chirEnumType.GetGenericArgs().size());
}

llvm::Constant* EnumCtorTIOrTTGenerator::GenSuperFnOfTypeTemplate(const std::string& funcName)
{
    auto i8PtrTy = llvm::Type::getInt8PtrTy(cgMod.GetLLVMContext());
    std::vector<llvm::Type*> argTypes{
        llvm::Type::getInt32Ty(cgMod.GetLLVMContext()),
        CGType::GetOrCreateTypeInfoPtrType(cgMod.GetLLVMContext())->getPointerTo()
    };
    auto superTiFnType = llvm::FunctionType::get(i8PtrTy, argTypes, false);
    auto superTiFn =
        llvm::Function::Create(superTiFnType, llvm::Function::PrivateLinkage, funcName, cgMod.GetLLVMModule());
    superTiFn->addFnAttr("native-interface-fn");
    CodeGen::IRBuilder2 irBuilder(cgMod);
    auto entryBB = irBuilder.CreateEntryBasicBlock(superTiFn, "entry");
    irBuilder.SetInsertPoint(entryBB);
    auto tt = CGType::GetOrCreate(cgMod, &chirEnumType)->GetOrCreateTypeTemplate();
    auto ti = irBuilder.CallIntrinsicGetTypeInfo({tt, superTiFn->getArg(0), superTiFn->getArg(1)});
    irBuilder.CreateRet(irBuilder.CreateBitCast(ti, i8PtrTy));
    return llvm::ConstantExpr::getBitCast(superTiFn, i8PtrTy);
}

void EnumCtorTIOrTTGenerator::GenerateGenericEnumCtorTypeTemplate(llvm::GlobalVariable& tt)
{
    auto& llvmCtx = cgMod.GetLLVMContext();
    auto i8Ty = llvm::Type::getInt8Ty(llvmCtx);
    auto i8PtrTy = llvm::Type::getInt8PtrTy(llvmCtx);
    auto i16Ty = llvm::Type::getInt16Ty(llvmCtx);
    auto i8PtrNull = llvm::ConstantPointerNull::get(i8PtrTy);

    auto ttName = tt.getName().str();
    auto className = ttName.substr(0, ttName.size() - 3); // 3: the length of ".ti" or ".tt"
    const auto& ctors = chirEnumType.GetConstructorInfos(cgMod.GetCGContext().GetCHIRBuilder());
    CJC_ASSERT(ctorIndex < ctors.size());
    const auto& paramTypes = ctors[ctorIndex].funcType->GetParamTypes();
    std::vector<CHIR::Type*> fieldsTypes;
    StaticCast<CGEnumType*>(CGType::GetOrCreate(cgMod, &chirEnumType))->IsOptionLike()
        ? fieldsTypes.emplace_back(cgMod.GetCGContext().GetCHIRBuilder().GetBoolTy())
        : fieldsTypes.emplace_back(cgMod.GetCGContext().GetCHIRBuilder().GetInt32Ty());
    fieldsTypes.insert(fieldsTypes.end(), paramTypes.cbegin(), paramTypes.cend());
    std::vector<llvm::Constant*> typeTemplateVec(TYPE_TEMPLATE_FIELDS_NUM);
    typeTemplateVec[static_cast<size_t>(TYPETEMPLATE_NAME)] = cgMod.GenerateTypeNameConstantString(className, false);
    typeTemplateVec[static_cast<size_t>(TYPETEMPLATE_TYPE_KIND)] =
        llvm::ConstantInt::get(i8Ty, UGTypeKind::UG_COMMON_ENUM);
    typeTemplateVec[static_cast<size_t>(TYPETEMPLATE_TYPE_ARGS_NUM)] = GenTypeArgsNumOfTypeTemplate();
    typeTemplateVec[static_cast<size_t>(TYPETEMPLATE_FIELDS_NUM)] = llvm::ConstantInt::get(i16Ty, fieldsTypes.size());

    std::unordered_map<const CHIR::GenericType*, size_t> localGenericParamIndicesMap;
    std::size_t genericTypeIdx = 0;
    for (auto typeArg : GetGenericArgsFromCHIRType(chirEnumType)) {
        auto genericType = typeArg.GetGenericType();
        localGenericParamIndicesMap.emplace(genericType, genericTypeIdx++);
    }
    typeTemplateVec[static_cast<size_t>(TYPETEMPLATE_FIELDS_FNS)] =
        CGTypeInfo::GenFieldsFnsOfTypeTemplate(cgMod, ttName, fieldsTypes, localGenericParamIndicesMap);
    typeTemplateVec[static_cast<size_t>(TYPETEMPLATE_SUPER_FN)] = GenSuperFnOfTypeTemplate(ttName + ".superTiFn");
    typeTemplateVec[static_cast<size_t>(TYPETEMPLATE_FINALIZER)] = i8PtrNull;
    typeTemplateVec[static_cast<size_t>(TYPETEMPLATE_REFLECTION)] = llvm::ConstantPointerNull::get(i8PtrTy);
    typeTemplateVec[static_cast<size_t>(TYPETEMPLATE_FLAG)] = llvm::ConstantInt::get(i8Ty, 0);
    typeTemplateVec[static_cast<size_t>(TYPETEMPLATE_EXTENSIONDEF_PTR)] =
        llvm::ConstantPointerNull::get(CGType::GetOrCreateExtensionDefPtrType(llvmCtx)->getPointerTo());
    typeTemplateVec[static_cast<size_t>(TYPETEMPLATE_INHERITED_CLASS_NUM)] = llvm::ConstantInt::get(i16Ty, 0U);

    tt.setInitializer(llvm::ConstantStruct::get(CGType::GetOrCreateTypeTemplateType(llvmCtx), typeTemplateVec));
    tt.addAttribute(TYPE_TEMPLATE_ATTR);
}

void EnumCtorTIOrTTGenerator::EmitForDynamicGI()
{
    auto enumDef = chirEnumType.GetCustomTypeDef();
    auto tt = cgMod.GetOrCreateEnumCtorTIOrTT(chirEnumType, ctorIndex);
    if (tt->hasInitializer() || enumDef->TestAttr(CHIR::Attribute::IMPORTED) ||
        enumDef->TestAttr(CHIR::Attribute::NON_RECOMPILE)) {
        return;
    }
    const auto& subCHIRPkg = cgCtx.GetSubCHIRPackage();
    bool definedInOtherLLVMModule = subCHIRPkg.chirCustomDefs.find(enumDef) == subCHIRPkg.chirCustomDefs.end();
    if (definedInOtherLLVMModule) {
        return;
    }

    GenerateGenericEnumCtorTypeTemplate(*tt);
    auto linkageType = CHIRLinkage2LLVMLinkage(enumDef->Get<CHIR::LinkTypeInfo>());
    if (linkageType == llvm::GlobalValue::InternalLinkage) {
        linkageType = llvm::GlobalValue::PrivateLinkage;
    }
    AddLinkageTypeMetadata(*tt, linkageType, cgCtx.IsCGParallelEnabled());
}

void EnumCtorTIOrTTGenerator::EmitForStaticGI()
{
    auto ti = cgMod.GetOrCreateEnumCtorTIOrTT(chirEnumType, ctorIndex);
    if (ti->hasInitializer()) {
        return;
    }

    GenerateNonGenericEnumCtorTypeInfo(*ti);
    AddLinkageTypeMetadata(*ti, llvm::GlobalValue::PrivateLinkage, false);
}

void EnumCtorTIOrTTGenerator::EmitForConcrete()
{
    auto enumDef = chirEnumType.GetCustomTypeDef();
    auto ti = cgMod.GetOrCreateEnumCtorTIOrTT(chirEnumType, ctorIndex);
    if (ti->hasInitializer() || enumDef->TestAttr(CHIR::Attribute::IMPORTED) ||
        enumDef->TestAttr(CHIR::Attribute::NON_RECOMPILE)) {
        return;
    }
    const auto& subCHIRPkg = cgCtx.GetSubCHIRPackage();
    bool definedInOtherLLVMModule = subCHIRPkg.chirCustomDefs.find(enumDef) == subCHIRPkg.chirCustomDefs.end();
    if (definedInOtherLLVMModule) {
        return;
    }

    GenerateNonGenericEnumCtorTypeInfo(*ti);
    auto linkageType = CHIRLinkage2LLVMLinkage(enumDef->Get<CHIR::LinkTypeInfo>());
    if (linkageType == llvm::GlobalValue::InternalLinkage) {
        linkageType = llvm::GlobalValue::PrivateLinkage;
    }
    AddLinkageTypeMetadata(*ti, linkageType, cgCtx.IsCGParallelEnabled());
}

void EnumCtorTIOrTTGenerator::Emit()
{
    auto cgEnumType = CGType::GetOrCreate(cgMod, &chirEnumType);
    if (cgEnumType->IsDynamicGI()) {
        EmitForDynamicGI();
    } else if (cgEnumType->IsStaticGI()) {
        EmitForStaticGI();
    } else if (cgEnumType->IsConcrete()) {
        EmitForConcrete();
    } else {
        CJC_ASSERT(false && "shouldn't reach here.");
    }
}
} // namespace Cangjie::CodeGen
