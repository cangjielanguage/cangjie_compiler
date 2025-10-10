// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "Base/CGTypes/CGFunctionType.h"

#include "Base/CGTypes/CGGenericType.h"
#include "CGContext.h"
#include "CGModule.h"
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
#include "CJNative/CGCFFI.h"
#endif
#include "Utils/CGUtils.h"
#include "cangjie/CHIR/CHIRCasting.h"
#include "cangjie/CHIR/Value.h"

namespace Cangjie::CodeGen {
CGFunctionType::CGFunctionType(
    CGModule& cgMod, CGContext& cgCtx, const CHIR::FuncType& chirType, const TypeExtraInfo& extraInfo)
    : CGType(cgMod, cgCtx, chirType, CGTypeKind::CG_FUNCTION)
{
    CJC_ASSERT(chirType.IsFunc());
    isMethod = extraInfo.isMethod;
    isStaticMethod = extraInfo.isStaticMethod;
    instantiatedParamTypes = extraInfo.instantiatedParamTypes;
    CJC_ASSERT(!forWrapper && !extraInfo.forWrapper);
}

CGFunctionType::CGFunctionType(
    CGModule& cgMod, CGContext& cgCtx, const CHIR::FuncBase& chirFunc, const TypeExtraInfo& extraInfo)
    : CGType(cgMod, cgCtx, *chirFunc.GetType(), CGTypeKind::CG_FUNCTION)
{
    this->chirFunc = &chirFunc;
    this->isStaticMethod = chirFunc.TestAttr(CHIR::Attribute::STATIC);
    this->isMethod = chirFunc.GetParentCustomTypeDef();
    this->forWrapper = extraInfo.forWrapper;
    if (this->forWrapper) {
        this->allowBasePtr = false;
    } else {
        this->allowBasePtr = extraInfo.allowBasePtr;
    }
    for (auto gt : chirFunc.GetGenericTypeParams()) {
        this->instantiatedParamTypes.emplace_back(gt);
    }
}

llvm::Type* CGFunctionType::GenLLVMType()
{
    if (llvmType) {
        return llvmType;
    }

    auto& llvmCtx = cgCtx.GetLLVMContext();
    layoutType = llvm::StructType::getTypeByName(llvmCtx, "Func.Type");
    if (!layoutType) {
        layoutType = llvm::StructType::create(llvmCtx, {llvm::Type::getInt8PtrTy(llvmCtx)}, "Func.Type");
    }

    auto& funcTy = StaticCast<const CHIR::FuncType&>(chirType);
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    if (funcTy.IsCFunc()) {
        llvmType = llvm::PointerType::get(llvm::StructType::get(llvmCtx), 0);
        auto llvmFuncTy = cgMod.GetCGCFFI().GetCFuncType(funcTy);
        return llvmFuncTy ? llvmFuncTy->getPointerTo() : llvmType;
    }
#endif
    llvmType = llvm::Type::getInt8PtrTy(llvmCtx);

    std::vector<llvm::Type*> paramTypes;
    size_t realArgIdx = 0;
    auto retCGType = CGType::GetOrCreate(cgMod, funcTy.GetReturnType());
    llvm::Type* retType = retCGType->GetLLVMType();
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    if (retType->isStructTy() || retType->isArrayTy() || !retCGType->GetSize()) {
        if (!retCGType->GetSize() && !funcTy.GetReturnType()->IsGeneric()) {
            (void)paramTypes.emplace_back(retType->getPointerTo(1));
        } else {
            (void)paramTypes.emplace_back(retType->getPointerTo());
        }
        ++realArgIdx;
        retType = llvm::Type::getVoidTy(llvmCtx);
        hasSRet = true;
    }
    size_t containedCGTypeIndex = 1;
#endif
    for (auto paramType : funcTy.GetParamTypes()) {
        auto cgType = CGType::GetOrCreate(cgMod, paramType);
        auto llvmType = cgType->GetLLVMType();

        (void)realParamIndices.emplace_back(realArgIdx);
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
        /// Effect Overview
        // The parameter that requires additional basePtr must meet and the function should not be 'mut'
        // either:
        //  - (1) Is a pointer to llvm::StructType or llvm::ArrayType.
        // or:
        //  - (2) Is llvm::StructType or llvm::ArrayType and is the first parameter of function.
        //    hint: This is designed to make it easier to invoke methods through reflect by runtime.
        //
        // The type of basePtr is determined according to the following rules:
        //  - For above (1), basePtr is i8 addrspace(1)*.
        //  - For above (2), basePtr is i8*.
        if (paramType->IsRef() &&
            (paramType->GetTypeArgs()[0]->IsStruct() || paramType->GetTypeArgs()[0]->IsTuple())) {
            llvmType = CGType::GetOrCreate(cgMod, DeRef(*paramType))->GetLLVMType()->getPointerTo(1U);
            (void)paramTypes.emplace_back(llvmType);
            if (allowBasePtr) {
                (void) paramTypes.emplace_back(llvm::Type::getInt8PtrTy(llvmCtx, 1));
                structParamNeedsBasePtr[realArgIdx] = containedCGTypeIndex;
                ++realArgIdx;
                hasBasePtr = true;
            }
        } else if (paramType->IsStruct() || paramType->IsTuple() ||
            (!cgType->GetSize() && !paramType->IsGeneric())) {
            // If current parameter is the first parameter in a struct method, aka `this`
            if (!forWrapper && containedCGTypeIndex == 1U && this->chirFunc &&
                !this->chirFunc->TestAttr(CHIR::Attribute::STATIC) && IsStructOrExtendMethod(*chirFunc)) {
                llvmType = cgType->GetLLVMType()->getPointerTo();
            } else {
                llvmType = cgType->GetLLVMType()->getPointerTo(cgType->GetSize() ? 0U : 1U);
            }
            (void)paramTypes.emplace_back(llvmType);
        } else if (cgType->IsStructType() || cgType->IsVArrayType()) {
            llvmType = cgType->GetLLVMType()->getPointerTo();
            (void)paramTypes.emplace_back(llvmType);
        } else {
            (void)paramTypes.emplace_back(llvmType);
        }
        ++containedCGTypeIndex;
        ++realArgIdx;
#endif
    }

    // Adding Supplementary parameters
    if (this->chirFunc) { // Generic parameters introduced by a function
        for (auto chirGT: this->chirFunc->GetGenericTypeParams()) {
            (void) paramTypes.emplace_back(CGType::GetOrCreateTypeInfoPtrType(llvmCtx));
            genericParamIndicesMap.emplace(chirGT, realArgIdx++); // map chirGT to ArgIdx
        }
    } else {
        for (std::size_t idx = 0; idx < this->instantiatedParamTypes.size(); ++idx) {
            (void) paramTypes.emplace_back(CGType::GetOrCreateTypeInfoPtrType(llvmCtx));
            realArgIdx++;
        }
    }
    if (this->isMethod) { // For member method, add OuterTypeInfo
        (void)paramTypes.emplace_back(CGType::GetOrCreateTypeInfoPtrType(llvmCtx));
        outerTypeInfoIndex = realArgIdx++;
    }
    if (this->isStaticMethod) { // For static member method, add ThisTypeInfo
        (void)paramTypes.emplace_back(CGType::GetOrCreateTypeInfoPtrType(llvmCtx));
        thisTypeInfoIndex = realArgIdx++;
    }

    llvmFunctionType = llvm::FunctionType::get(retType, paramTypes, funcTy.HasVarArg());
    return llvmType;
}

llvm::FunctionType* CGFunctionType::GetLLVMFunctionType() const
{
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    if (auto& chirFuncTy = StaticCast<const CHIR::FuncType&>(chirType); chirFuncTy.IsCFunc()) {
        return cgMod.GetCGCFFI().GetCFuncType(chirFuncTy);
    }
#endif
    return llvmFunctionType;
}

void CGFunctionType::GenContainedCGTypes()
{
    size_t containedCGTypeIndex = 0;
    // On CJNative backend, the parameters of structure type are passed by reference.
    auto getFixedCGType = [this, &containedCGTypeIndex](const CHIR::Type& chirType) {
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    auto cgType = CGType::GetOrCreate(cgMod, &chirType);
    if (chirType.IsRef() && chirType.GetTypeArgs()[0]->IsStruct()) {
        return CGType::GetOrCreate(cgMod, &chirType, 1U);
    } else if (chirType.IsStruct() || chirType.IsTuple() || (!cgType->GetSize() && !chirType.IsGeneric())) {
        auto refType =  CGType::GetRefTypeOf(cgCtx.GetCHIRBuilder(), chirType);
        // If current parameter is the first parameter in a struct method, aka `this`
        if (!forWrapper && containedCGTypeIndex == 1U && this->chirFunc && IsStructOrExtendMethod(*chirFunc)) {
            return CGType::GetOrCreate(cgMod, refType);
        } else {
            return CGType::GetOrCreate(cgMod, refType, cgType->GetSize() ? 0U : 1U);
        }
    } else if (cgType->IsStructType() || cgType->IsVArrayType()) {
        return CGType::GetOrCreate(cgMod, CGType::GetRefTypeOf(cgCtx.GetCHIRBuilder(), chirType), 0U);
    } else if (cgType->IsStructPtrType() || cgType->IsVArrayPtrType()) {
        return CGType::GetOrCreate(cgMod, &chirType, 1U);
    } else {
        return CGType::GetOrCreate(cgMod, &chirType);
    }
#endif
    };
    // The contained types of a function type include:
    // (1) return type
    auto& funcTy = StaticCast<const CHIR::FuncType&>(chirType);
    (void)containedCGTypes.emplace_back(getFixedCGType(*funcTy.GetReturnType()));
    ++containedCGTypeIndex;
    // (2) every parameter type
    for (auto paramType : funcTy.GetParamTypes()) {
        (void)containedCGTypes.emplace_back(getFixedCGType(*paramType));
        ++containedCGTypeIndex;
    }
    // (3) Supplementary parameters
    if (this->chirFunc) {
        // every generic parameter type
        for ([[maybe_unused]] const auto& it : this->chirFunc->GetGenericTypeParams()) {
            (void)containedCGTypes.emplace_back(CGType::GetCGTI(cgMod));
        }
        // For member method, add OuterTypeInfo
        if (this->chirFunc->GetOuterDeclaredOrExtendedDef()) {
            (void)containedCGTypes.emplace_back(CGType::GetCGTI(cgMod));
        }
        // For static member method, add ThisTypeInfo
        if (this->chirFunc->TestAttr(CHIR::Attribute::STATIC)) {
            (void)containedCGTypes.emplace_back(CGType::GetCGTI(cgMod));
        }
    }
}

llvm::Constant* CGFunctionType::GenSourceGenericOfTypeInfo()
{
    return CGType::GenSourceGenericOfTypeInfo();
}

llvm::Constant* CGFunctionType::GenTypeArgsNumOfTypeInfo()
{
    return llvm::ConstantInt::get(llvm::Type::getInt8Ty(cgMod.GetLLVMContext()),
        static_cast<const CHIR::FuncType&>(chirType).GetParamTypes().size() + 1U); // add one for the return type
}

llvm::Constant* CGFunctionType::GenTypeArgsOfTypeInfo()
{
    auto& funcType = StaticCast<const CHIR::FuncType&>(chirType);
    auto typeInfoPtrTy = CGType::GetOrCreateTypeInfoPtrType(cgMod.GetLLVMContext());

    std::vector<llvm::Constant*> constants;
    (void)constants.emplace_back(CGType::GetOrCreate(cgMod, DeRef(*funcType.GetReturnType()))->GetOrCreateTypeInfo());
    for (auto paramType : funcType.GetParamTypes()) {
        (void)constants.emplace_back(CGType::GetOrCreate(cgMod, DeRef(*paramType))->GetOrCreateTypeInfo());
    }

    auto typeOfGenericArgsGV = llvm::ArrayType::get(typeInfoPtrTy, constants.size());
    auto typeInfoOfGenericArgs = llvm::cast<llvm::GlobalVariable>(cgMod.GetLLVMModule()->getOrInsertGlobal(
        CGType::GetNameOfTypeInfoGV(chirType) + ".typeArgs", typeOfGenericArgsGV));
    typeInfoOfGenericArgs->setInitializer(llvm::ConstantArray::get(typeOfGenericArgsGV, constants));
    typeInfoOfGenericArgs->setLinkage(llvm::GlobalValue::LinkageTypes::PrivateLinkage);
    typeInfoOfGenericArgs->addAttribute(CJTI_TYPE_ARGS_ATTR);
    return llvm::ConstantExpr::getBitCast(typeInfoOfGenericArgs, llvm::Type::getInt8PtrTy(cgMod.GetLLVMContext()));
}

void CGFunctionType::CalculateSizeAndAlign()
{
    llvm::DataLayout layOut = cgMod.GetLLVMModule()->getDataLayout();
    size = layOut.getTypeAllocSize(llvmType);
    align = layOut.getABITypeAlignment(llvmType);
}
} // namespace Cangjie::CodeGen
