// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "Base/CGTypes/CGStructType.h"

#include "Base/CGTypes/CGTupleType.h"
#include "CGContext.h"
#include "CGModule.h"
#include "Utils/CGUtils.h"
#include "cangjie/CHIR/Type/StructDef.h"
#include "cangjie/Option/Option.h"

namespace Cangjie::CodeGen {
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
llvm::Type* CGStructType::GenLLVMType()
{
    auto& llvmCtx = cgCtx.GetLLVMContext();
    auto& structType = StaticCast<const CHIR::StructType&>(chirType);

    if (!IsSized()) {
        return llvm::Type::getInt8Ty(llvmCtx);
    }

    auto typeName = STRUCT_TYPE_PREFIX;
    if (auto defType = structType.GetStructDef()->GetType();
        &chirType != defType && CGType::GetOrCreate(cgMod, defType)->GetSize()) {
        typeName += GetTypeQualifiedName(*defType);
    } else {
        typeName += GetTypeQualifiedName(structType);
    }

    llvmType = llvm::StructType::getTypeByName(llvmCtx, typeName);
    if (llvmType && cgCtx.IsGeneratedStructType(typeName)) {
        layoutType = llvm::cast<llvm::StructType>(llvmType);
        return llvmType;
    } else if (!llvmType) {
        llvmType =  llvm::StructType::create(llvmCtx, typeName);
    }
    layoutType = llvm::cast<llvm::StructType>(llvmType);
    cgCtx.AddGeneratedStructType(typeName);

    std::vector<llvm::Type*> fieldTypes;
    std::vector<size_t> litStructPtrIdx;
    size_t idx = 0;
    auto& nonConstStructType = const_cast<CHIR::StructType&>(structType);
    const auto memberVarTypes = nonConstStructType.GetInstantiatedMemberTys(cgMod.GetCGContext().GetCHIRBuilder());
    for (auto& memberVarType : memberVarTypes) {
        auto cgType = CGType::GetOrCreate(cgMod, memberVarType);
        auto& fieldType = fieldTypes.emplace_back(cgType->GetLLVMType());
        if (IsCFunc(*memberVarType) && IsLitStructPtrType(fieldType)) {
            litStructPtrIdx.emplace_back(idx);
        }
        idx++;
    }
    SetStructTypeBody(llvm::cast<llvm::StructType>(llvmType), fieldTypes);

    for (auto i : litStructPtrIdx) {
        fieldTypes[i] = CGType::GetOrCreate(cgMod, memberVarTypes[i])->GetLLVMType();
    }

    SetStructTypeBody(llvm::cast<llvm::StructType>(llvmType), fieldTypes);

    return llvmType;
}
#endif

void CGStructType::GenContainedCGTypes()
{
    auto& structType = StaticCast<const CHIR::StructType&>(chirType);
    auto& nonConstStructType = const_cast<CHIR::StructType&>(structType);
    for (auto& type : nonConstStructType.GetInstantiatedMemberTys(cgMod.GetCGContext().GetCHIRBuilder())) {
        (void)containedCGTypes.emplace_back(CGType::GetOrCreate(cgMod, type));
    }
}

llvm::Constant* CGStructType::GenFieldsNumOfTypeInfo()
{
    auto structDef = StaticCast<const CHIR::StructType&>(chirType).GetStructDef();
    return llvm::ConstantInt::get(llvm::Type::getInt16Ty(cgMod.GetLLVMContext()), structDef->GetAllInstanceVarNum());
}

void CGStructType::CalculateSizeAndAlign()
{
    if (auto structType = llvm::dyn_cast<llvm::StructType>(llvmType)) {
        auto layOut = cgMod.GetLLVMModule()->getDataLayout();
        size = layOut.getTypeAllocSize(structType);
        align = layOut.getABITypeAlignment(structType);
    }
}

llvm::Constant* CGStructType::GenFieldsOfTypeInfo()
{
    auto& structType = StaticCast<const CHIR::StructType&>(chirType);
    auto& nonConstStructType = const_cast<CHIR::StructType&>(structType);
    auto fieldConstants = GenTypeInfoConstantVectorForTypes(cgMod, nonConstStructType.GetInstantiatedMemberTys(cgMod.GetCGContext().GetCHIRBuilder()));
    return GenTypeInfoArray(cgMod, CGType::GetNameOfTypeInfoGV(chirType) + ".fields", fieldConstants, CJTI_FIELDS_ATTR);
}

} // namespace Cangjie::CodeGen
