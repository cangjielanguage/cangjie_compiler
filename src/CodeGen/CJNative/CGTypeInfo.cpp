// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "CJNative/CGTypeInfo.h"

namespace Cangjie::CodeGen {
namespace CGTypeInfo {
llvm::Function* GenTypeInfoFns(llvm::FunctionType* fieldFnType, CGModule& cgMod, const std::string& funcName,
    const std::unordered_map<const CHIR::GenericType*, size_t>& outerGTIdxMap, const CHIR::Type& memberType,
    const std::string& memberName)
{
    llvm::Function* getTiFn =
        llvm::Function::Create(fieldFnType, llvm::Function::PrivateLinkage, funcName, cgMod.GetLLVMModule());
    getTiFn->addFnAttr("native-interface-fn");
    auto cgType = CGType::GetOrCreate(cgMod, DeRef(memberType));
    CodeGen::IRBuilder2 irBuilder(cgMod);
    auto entryBB = irBuilder.CreateEntryBasicBlock(getTiFn, "entry");
    irBuilder.SetInsertPoint(entryBB);
    std::unordered_map<const CHIR::Type*, std::function<llvm::Value*(IRBuilder2&)>> genericParamsMap;
    llvm::Value* ti{nullptr};
    if (cgType->IsConcrete() || cgType->IsStaticGI()) {
        ti = irBuilder.CreateTypeInfo(memberType, genericParamsMap);
    } else if (memberType.IsGeneric()) {
        auto gt = static_cast<const CHIR::GenericType*>(&memberType);
        std::string varName = "ti." + gt->GetSrcCodeIdentifier();
        ti = irBuilder.GetTypeInfoFromTiArray(getTiFn->getArg(1), outerGTIdxMap.at(gt), varName);
    } else {
        auto args = getTiFn->getArg(1);
        for (auto& [gt, idx] : outerGTIdxMap) {
            auto localGt = gt;
            auto localIdx = idx;
            genericParamsMap.emplace(gt, [args, localGt, localIdx](IRBuilder2& irBuilderLocal) {
                auto varName = "ti." + localGt->GetSrcCodeIdentifier();
                return irBuilderLocal.GetTypeInfoFromTiArray(args, localIdx, varName);
            });
        }
        ti = irBuilder.CreateTypeInfo(*DeRef(memberType), genericParamsMap);
    }
    llvm::Value* bitcastedValue = irBuilder.CreateBitCast(ti, llvm::Type::getInt8PtrTy(cgMod.GetLLVMContext()));
    irBuilder.CreateRet(bitcastedValue);
    return getTiFn;
}

llvm::Constant* GenSuperFnOfTypeTemplate(CGModule& cgMod, const std::string& funcName, const CHIR::Type& superType,
    const std::vector<CHIR::GenericType*>& typeArgs)
{
    std::vector<llvm::Type*> argTypes{llvm::Type::getInt32Ty(cgMod.GetLLVMContext()),
        CGType::GetOrCreateTypeInfoPtrType(cgMod.GetLLVMContext())->getPointerTo()};
    llvm::FunctionType* superFnType =
        llvm::FunctionType::get(llvm::Type::getInt8PtrTy(cgMod.GetLLVMContext()), argTypes, false);

    std::unordered_map<const CHIR::GenericType*, size_t> localGenericParamIndicesMap;
    size_t idx = 0;
    for (auto typeArg : typeArgs) {
        localGenericParamIndicesMap.emplace(typeArg, idx);
        ++idx;
    }
    llvm::Function* superFn =
        GenTypeInfoFns(superFnType, cgMod, funcName, localGenericParamIndicesMap, superType, superType.ToString());
    return llvm::ConstantExpr::getBitCast(superFn, llvm::Type::getInt8PtrTy(cgMod.GetLLVMContext()));
}

llvm::Constant* GenFieldsFnsOfTypeTemplate(
    CGModule& cgMod, const std::string& funcPrefixName, const std::vector<llvm::Constant*>& fieldsFn)
{
    auto fieldNum = fieldsFn.size();
    auto i8PtrTy = llvm::Type::getInt8PtrTy(cgMod.GetLLVMContext());
    CJC_ASSERT(fieldNum > 0 && "field num should be at least 1");
    llvm::ArrayType* arrayType = llvm::ArrayType::get(fieldsFn[0]->getType(), fieldNum);
    llvm::GlobalVariable* globalArray = llvm::cast<llvm::GlobalVariable>(
        cgMod.GetLLVMModule()->getOrInsertGlobal(funcPrefixName + ".fieldTiFns", arrayType));
    globalArray->setLinkage(llvm::GlobalValue::PrivateLinkage);
    globalArray->setInitializer(llvm::ConstantArray::get(arrayType, fieldsFn));
    globalArray->addAttribute(CJTT_FIELDS_FNS_ATTR);
    return llvm::ConstantExpr::getBitCast(globalArray, i8PtrTy);
}

llvm::Constant* GenFieldsFnsOfTypeTemplate(CGModule& cgMod,
    const std::string& funcPrefixName, const std::vector<CHIR::Type*>& fieldTypes,
    const std::unordered_map<const CHIR::GenericType*, size_t>& genericParamIndicesMap)
{
    auto fieldNum = fieldTypes.size();
    auto i8PtrTy = llvm::Type::getInt8PtrTy(cgMod.GetLLVMContext());
    if (fieldNum == 0) {
        return llvm::ConstantPointerNull::get(i8PtrTy);
    }
    // Step1 create FunctionType which return (i32, ti**)->i8*
    std::vector<llvm::Type*> argTypes;
    auto typeInfoType = CGType::GetOrCreateTypeInfoType(cgMod.GetLLVMContext());
    argTypes.emplace_back(llvm::Type::getInt32Ty(cgMod.GetLLVMContext()));
    argTypes.emplace_back(typeInfoType->getPointerTo()->getPointerTo());
    llvm::FunctionType* fieldFn =
        llvm::FunctionType::get(llvm::Type::getInt8PtrTy(cgMod.GetLLVMContext()), argTypes, false);

    // Step2 create a global array named "type_mangled_name + $GetFieldFns", which size is fieldNum, element is
    // FunctionType* fieldFn
    std::vector<llvm::Type*> fieldFnTypes(fieldNum, fieldFn->getPointerTo());
    // fill the elements inside the array
    std::vector<llvm::Constant*> initializers;

    // Step3:build Generic Param map for current customType eg:CA<T,U>
    size_t idx = 0;
    for (auto fieldType : fieldTypes) {
        auto fieldName = std::to_string(idx);
        auto funcName = funcPrefixName + ".fieldTiFn." + fieldName;
        llvm::Function* fieldFnGlobal =
            CGTypeInfo::GenTypeInfoFns(fieldFn, cgMod, funcName, genericParamIndicesMap, *fieldType, fieldName);
        initializers.emplace_back(fieldFnGlobal);
        ++idx;
    }

    return GenFieldsFnsOfTypeTemplate(cgMod, funcPrefixName, initializers);
}
} // namespace CGTypeInfo
} // namespace Cangjie::CodeGen
