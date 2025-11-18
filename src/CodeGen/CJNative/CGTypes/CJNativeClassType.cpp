// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "Base/CGTypes/CGClassType.h"

#include "CGContext.h"
#include "CGModule.h"
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
#include "CJNative/CGTypeInfo.h"
#endif
#include "Utils/CGUtils.h"
#include "cangjie/CHIR/CHIRCasting.h"
#include "cangjie/CHIR/Type/ClassDef.h"

namespace Cangjie::CodeGen {

CGClassType::CGClassType(CGModule& cgMod, CGContext& cgCtx, const CHIR::ClassType& chirType)
    : CGCustomType(cgMod, cgCtx, chirType, CGTypeKind::CG_CLASS),
      numOfAllFields(chirType.GetClassDef()->GetAllInstanceVarNum())
{
}

llvm::Type* CGClassType::GenLLVMType()
{
    auto& llvmCtx = cgCtx.GetLLVMContext();
    llvmType = llvm::Type::getInt8Ty(llvmCtx);

    if (!IsSized()) {
        return llvmType;
    }

    auto& classType = StaticCast<const CHIR::ClassType&>(chirType);

    std::string layoutName{};
    if (auto defType = classType.GetClassDef()->GetType();
        &chirType != defType && CGType::GetOrCreate(cgMod, defType)->GetSize()) {
        layoutName = GetClassObjLayoutName(GetTypeQualifiedName(*defType));
    } else {
        layoutName = GetClassObjLayoutName(GetTypeQualifiedName(classType));
    }

    layoutType = llvm::StructType::getTypeByName(llvmCtx, layoutName);
    if (layoutType && cgCtx.IsGeneratedStructType(layoutName)) {
        return llvmType;
    } else if (!layoutType) {
        layoutType = llvm::StructType::create(llvmCtx, layoutName);
    }
    cgCtx.AddGeneratedStructType(layoutName);

    std::vector<llvm::Type*> bodyVec{};
    if (classType.IsAutoEnv()) {
        (void)bodyVec.emplace_back(llvm::Type::getInt8PtrTy(llvmCtx));
        (void)bodyVec.emplace_back(llvm::Type::getInt8PtrTy(llvmCtx));
    }
    auto& nonConstClassType = const_cast<CHIR::ClassType&>(classType);
    for (auto fieldType : nonConstClassType.GetInstantiatedMemberTys(cgMod.GetCGContext().GetCHIRBuilder())) {
        (void)bodyVec.emplace_back(CGType::GetOrCreate(cgMod, fieldType)->GetLLVMType());
    }
    SetStructTypeBody(layoutType, bodyVec);
    return llvmType;
}

void CGClassType::GenContainedCGTypes()
{
    auto& classType = StaticCast<const CHIR::ClassType&>(chirType);
    if (classType.IsAutoEnv()) {
        (void)containedCGTypes.emplace_back(CGType::GetInt8PtrCGType(cgMod));
        (void)containedCGTypes.emplace_back(CGType::GetInt8PtrCGType(cgMod));
    }
    auto& nonConstClassType = const_cast<CHIR::ClassType&>(classType);
    for (auto& fieldType : nonConstClassType.GetInstantiatedMemberTys(cgMod.GetCGContext().GetCHIRBuilder())) {
        (void)containedCGTypes.emplace_back(CGType::GetOrCreate(cgMod, fieldType));
    }
}

llvm::Constant* CGClassType::GenSuperOfTypeInfo()
{
    auto& classType = StaticCast<const CHIR::ClassType&>(chirType);
    auto& nonConstClassType = const_cast<CHIR::ClassType&>(classType);
    if (auto superType = nonConstClassType.GetSuperClassTy(&cgMod.GetCGContext().GetCHIRBuilder())) {
        return CGType::GetOrCreate(cgMod, superType)->GetOrCreateTypeInfo();
    } else {
        return llvm::ConstantPointerNull::get(CGType::GetOrCreateTypeInfoPtrType(cgMod.GetLLVMContext()));
    }
}

llvm::Constant* CGClassType::GenSourceGenericOfTypeInfo()
{
    //    union {
    //        const TypeTemplate* generic_from;
    //        void* finalizer;
    //    }
    auto& classType = StaticCast<const CHIR::ClassType&>(chirType);
    if (auto finalizer = classType.GetClassDef()->GetFinalizer(); finalizer && !IsStaticGI()) {
        CJC_ASSERT(classType.GetTypeArgs().empty());
        auto p0i8 = llvm::Type::getInt8PtrTy(cgCtx.GetLLVMContext());
        return llvm::ConstantExpr::getBitCast(cgMod.GetOrInsertCGFunction(finalizer)->GetRawFunction(), p0i8);
    }
    return CGType::GenSourceGenericOfTypeInfo();
}

llvm::Constant* CGClassType::GenSuperFnOfTypeTemplate()
{
    auto& classType = static_cast<const CHIR::ClassType&>(chirType);
    auto& nonConstClassType = const_cast<CHIR::ClassType&>(classType);
    auto superType = nonConstClassType.GetSuperClassTy(&cgMod.GetCGContext().GetCHIRBuilder());
    if (!classType.GetClassDef()->IsClass() || !superType) {
        return llvm::ConstantPointerNull::get(llvm::Type::getInt8PtrTy(cgMod.GetLLVMContext()));
    }
    auto funcName = CGType::GetNameOfTypeTemplateGV(chirType) + ".superTiFn";
    return CGTypeInfo::GenSuperFnOfTypeTemplate(
        cgMod, funcName, *superType, classType.GetClassDef()->GetGenericTypeParams());
}

llvm::Constant* CGClassType::GenFinalizerOfTypeTemplate()
{
    auto& classType = static_cast<const CHIR::ClassType&>(chirType);
    auto p0i8 = llvm::Type::getInt8PtrTy(cgCtx.GetLLVMContext());
    if (auto finalizer = classType.GetClassDef()->GetFinalizer()) {
        return llvm::ConstantExpr::getBitCast(cgMod.GetOrInsertCGFunction(finalizer)->GetRawFunction(), p0i8);
    } else {
        return llvm::ConstantPointerNull::get(p0i8);
    }
}

void CGClassType::CalculateSizeAndAlign()
{
    if (layoutType) {
        auto layOut = cgMod.GetLLVMModule()->getDataLayout();
        size = layOut.getTypeAllocSize(layoutType);
        align = layOut.getABITypeAlignment(layoutType);
    }
}

void CGClassType::PreActionOfGenTypeInfo()
{
    auto& classDef = *static_cast<const CHIR::ClassType&>(chirType).GetClassDef();
    if (IsCoreFutureClass(classDef) || IsSyncRelatedClass(classDef)) {
        typeInfo->addAttribute(classDef.GetSrcCodeIdentifier());
    }
    if (classDef.GetFinalizer()) {
        typeInfo->addAttribute(GC_FINALIZER_ATTR);
    }
}

void CGClassType::PreActionOfGenTypeTemplate()
{
    auto& classDef = *static_cast<const CHIR::ClassType&>(chirType).GetClassDef();
    if (IsCoreFutureClass(classDef)) {
        typeTemplate->addAttribute(classDef.GetSrcCodeIdentifier());
    }
    if (classDef.GetFinalizer()) {
        typeTemplate->addAttribute(GC_FINALIZER_ATTR);
    }
}

void CGClassType::PostActionOfGenTypeInfo()
{
    /*
        1. Mark a class of which size needs to be updated at runtime by:
            a. not set TypeExt flag
            b. set TypeExt flag, but has no TypeExt global variable
            c. set TypeExt flag, has TypeExt global variable, but the first byte of TypeExt is '0x0'
        2. Mark a class of which size doesn't need to be updated at runtime by:
            a. set TypeExt flag, has TypeExt global variable, but the first byte of TypeExt is '0x1'
        The following is marking a class of which size doesn't need to be updated at runtime.
    */
    auto classDef = StaticCast<CHIR::ClassType>(chirType).GetClassDef();
    if (classDef->TestAttr(CHIR::Attribute::COMPILER_ADD) || !IsModifiableClass(chirType) || !IsStaticGI()) {
        typeInfo->addAttribute(HAS_EXT_PART);
        auto& llvmCtx = cgCtx.GetLLVMContext();
        auto extPartType = CGType::GetClassTTExtTypeVer1(llvmCtx);
        auto extPart = llvm::cast<llvm::GlobalVariable>(cgMod.GetLLVMModule()->getOrInsertGlobal(
            "ext_" + typeInfo->getName().str(), extPartType));
        auto size = cgMod.GetLLVMModule()->getDataLayout().getTypeAllocSize(extPartType);
        std::vector<llvm::Constant*> cons = {
            typeInfo, llvm::ConstantInt::get(llvm::Type::getInt16Ty(llvmCtx), 1U),
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(llvmCtx), size),
            llvm::ConstantInt::get(llvm::Type::getInt8Ty(llvmCtx), 1U)
        };
        extPart->setInitializer(llvm::ConstantStruct::get(extPartType, cons));
        extPart->addAttribute(GC_TI_EXT_ATTR);
        extPart->setLinkage(llvm::GlobalValue::LinkageTypes::PrivateLinkage);
        cgCtx.AddLLVMUsedVars(extPart->getName().str());
    }
}
} // namespace Cangjie::CodeGen
