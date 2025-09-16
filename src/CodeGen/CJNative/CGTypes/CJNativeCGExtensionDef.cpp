// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the class for determine the memory layout of Class/Interface.
 */

#include "CJNative/CGTypes/CGExtensionDef.h"

#include "Base/CGTypes/CGCustomType.h"
#include "Utils/CGUtils.h"
#include "cangjie/CHIR/Type/CustomTypeDef.h"
#include "cangjie/CHIR/Value.h"

using namespace Cangjie;
using namespace CodeGen;

CGExtensionDef::CGExtensionDef(CGModule& cgMod, const CHIR::CustomTypeDef& chirDef)
    : cgMod(cgMod), cgCtx(cgMod.GetCGContext()), chirDef(chirDef)
{
    if (chirDef.GetCustomKind() == CHIR::CustomDefKind::TYPE_EXTEND) {
        targetType = StaticCast<CHIR::ExtendDef>(chirDef).GetExtendedType();
    } else {
        targetType = chirDef.GetType();
    }
    CJC_NULLPTR_CHECK(targetType);
    isForExternalType = IsExternalDefinedType(*targetType);
    typeMangle = GetTypeQualifiedName(*targetType);
}

namespace {
inline void HandleShortcutBranch(IRBuilder2& irBuilder, llvm::Value* condition, const std::string& prefix)
{
    auto [trueBB, falseBB] = Vec2Tuple<2>(
        irBuilder.CreateAndInsertBasicBlocks({GenNameForBB(prefix + "_true"), GenNameForBB(prefix + "_false")}));
    irBuilder.CreateCondBr(condition, trueBB, falseBB);
    irBuilder.SetInsertPoint(falseBB);
    irBuilder.CreateRet(irBuilder.getInt1(false));
    irBuilder.SetInsertPoint(trueBB);
}
} // namespace

llvm::Constant* CGExtensionDef::GetTargetType(CGModule& cgModule, const CHIR::Type& type)
{
    llvm::Constant* res{nullptr};
    auto cgType = CGType::GetOrCreate(cgModule, &type);
    if (cgType->IsDynamicGI()) {
        if (type.IsNominal()) {
            cgType = CGType::GetOrCreate(cgModule, StaticCast<CHIR::CustomType>(type).GetCustomTypeDef()->GetType());
        }
        res = cgType->GetOrCreateTypeTemplate();
    } else {
        res = cgType->GetOrCreateTypeInfo();
    }
    return llvm::ConstantExpr::getBitCast(res, llvm::Type::getInt8PtrTy(cgModule.GetLLVMContext()));
}

llvm::Value* CGExtensionDef::CreateTypeComparison(
    IRBuilder2& irBuilder, llvm::Value* typeInfo, const CHIR::Type& staticType, const std::string& prefix)
{
    auto derefType = DeRef(staticType);
    auto cgType = CGType::GetOrCreate(cgMod, derefType);
    if (cgType->IsConcrete()) {
        // For concrete type, determine equality based on the address.
        return irBuilder.CreateICmpEQ(typeInfo, cgType->GetOrCreateTypeInfo());
    } else if (cgType->IsStaticGI()) {
        // For StaticGI, determine equality through runtime API.
        return irBuilder.CallIntrinsicIsTypeEqualTo({typeInfo, cgType->GetOrCreateTypeInfo()});
    } else if (staticType.IsGeneric()) {
        // isSubtype(ki, Uppers); eg: T <: A<T> & I<T>
        auto& gt = StaticCast<CHIR::GenericType>(staticType);
        generatedGenerics[gt.ToString()].emplace_back(typeInfo);
        auto& uppers = gt.GetUpperBounds();
        std::vector<const CHIR::Type*> fixedUppers;
        for (auto upper : uppers) {
            derefType = DeRef(*upper);
            if (derefType->IsCType()) {
                continue;
            }
            (void)fixedUppers.emplace_back(derefType);
        }
        if (fixedUppers.empty()) {
            return irBuilder.getInt1(true);
        }
        llvm::Value* res{nullptr};
        for (size_t i = 0; i < fixedUppers.size(); ++i) {
            if (i == 0) {
                auto upperBoundTypeInfo = irBuilder.CreateTypeInfo(*fixedUppers[i], genericParamsMap, false);
                res = irBuilder.CallIntrinsicIsSubtype({typeInfo, upperBoundTypeInfo});
            } else {
                auto upperPrefix = prefix + "_cs_" + std::to_string(i);
                HandleShortcutBranch(irBuilder, res, upperPrefix);
                auto upperBoundTypeInfo = irBuilder.CreateTypeInfo(*fixedUppers[i], genericParamsMap, false);
                res = irBuilder.CallIntrinsicIsSubtype({typeInfo, upperBoundTypeInfo});
            }
        }
        return res;
    } else {
        // and(cmp(ki.template, ti.template), compare(ti.typeArgs, ki.typeArgs))
        auto typeInfoType = CGType::GetOrCreateTypeInfoType(cgMod.GetLLVMContext());
        auto realTypeTemplatePtr =
            irBuilder.CreateStructGEP(typeInfoType, typeInfo, static_cast<size_t>(TYPEINFO_SOURCE_GENERIC));
        auto realTypeTemplate =
            irBuilder.LLVMIRBuilder2::CreateLoad(irBuilder.getInt8PtrTy(), realTypeTemplatePtr, prefix + "_tt");
        auto typeCmp = irBuilder.CreateICmpEQ(realTypeTemplate, GetTargetType(cgMod, *derefType));
        HandleShortcutBranch(irBuilder, typeCmp, prefix + "_tt");
        auto backupBB = irBuilder.GetInsertBlock();
        auto backupIt = irBuilder.GetInsertPoint();
        irBuilder.SetInsertPointForPreparingTypeInfo();
        auto typeInfosPtr = irBuilder.CreateStructGEP(typeInfoType, typeInfo, static_cast<size_t>(TYPEINFO_TYPE_ARGS));
        llvm::Value* typeInfos = irBuilder.LLVMIRBuilder2::CreateLoad(irBuilder.getInt8PtrTy(), typeInfosPtr);
        typeInfos = irBuilder.CreateBitCast(typeInfos, typeInfoType->getPointerTo()->getPointerTo());
        irBuilder.SetInsertPoint(backupBB, backupIt);
        return CreateCompareArgs(irBuilder, typeInfos, derefType->GetTypeArgs(), prefix);
    }
}

llvm::Value* CGExtensionDef::CreateCompareArgs(
    IRBuilder2& irBuilder, llvm::Value* typeInfos, const std::vector<CHIR::Type*>& typeArgs, const std::string& prefix)
{
    auto typeInfoPtrType = CGType::GetOrCreateTypeInfoPtrType(cgMod.GetLLVMContext());
    auto argSize = typeArgs.size();
    llvm::Value* retVal;
    for (size_t i = 0; i < argSize; ++i) {
        auto backupBB = irBuilder.GetInsertBlock();
        auto backupIt = irBuilder.GetInsertPoint();
        irBuilder.SetInsertPointForPreparingTypeInfo();
        llvm::Value* tiPtr = irBuilder.CreateConstGEP1_32(typeInfoPtrType, typeInfos, i);
        auto ti = irBuilder.LLVMIRBuilder2::CreateLoad(typeInfoPtrType, tiPtr, std::to_string(i) + "ti");
        auto chirType = typeArgs[i];
        innerTypeInfoMap.emplace(chirType, ti);
        irBuilder.SetInsertPoint(backupBB, backupIt);
        auto idxStr = prefix + "_" + std::to_string(i);
        if (i == 0) {
            retVal = CreateTypeComparison(irBuilder, ti, *chirType, idxStr);
        } else {
            HandleShortcutBranch(irBuilder, retVal, idxStr);
            retVal = CreateTypeComparison(irBuilder, ti, *chirType, idxStr);
        }
    }
    return retVal;
}

llvm::Value* CGExtensionDef::CheckGenericParams(IRBuilder2& irBuilder, llvm::Value* retVal)
{
    for (auto it = generatedGenerics.begin(); it != generatedGenerics.end();) {
        it = it->second.size() <= 1 ? generatedGenerics.erase(it) : ++it;
    }
    if (generatedGenerics.empty()) {
        return retVal; // All generic type has 0 or 1 associated value.
    }
    llvm::Value* newRet = retVal;
    for (auto [_, values] : generatedGenerics) {
        CJC_ASSERT(!values.empty());
        for (size_t i = 0; i < values.size() - 1; ++i) {
            HandleShortcutBranch(irBuilder, newRet, "type_cs");
            newRet = irBuilder.CallIntrinsicIsTypeEqualTo({values[i], values[i + 1]});
        }
    }
    CJC_ASSERT(newRet != retVal); // Return value must be updated.
    return newRet;
}

bool CGExtensionDef::FoundGenericTypeAndCollectPath(
    const CHIR::Type& srcType, CHIR::GenericType& gt, std::vector<size_t>& path)
{
    if (&srcType == &gt) {
        return true;
    }
    auto typeArgs = DeRef(srcType)->GetTypeArgs();
    for (size_t i = 0; i < typeArgs.size(); ++i) {
        path.emplace_back(i);
        if (FoundGenericTypeAndCollectPath(*typeArgs[i], gt, path)) {
            return true;
        }
        path.pop_back();
    }
    return false;
}

llvm::Value* CGExtensionDef::GetTypeInfoWithPath(IRBuilder2& irBuilder, const CHIR::Type& type,
    llvm::Value* entryTypeArgs, std::queue<size_t>&& remainPath,
    std::unordered_map<const CHIR::Type*, llvm::Value*>& innerTypeInfoMap)
{
    auto typeInfoType = CGType::GetOrCreateTypeInfoType(irBuilder.GetLLVMContext());
    auto typeInfoPtrType = typeInfoType->getPointerTo();
    auto curType = &type;
    auto typeArgs = entryTypeArgs;
    while (!remainPath.empty()) {
        auto idx = remainPath.front();
        remainPath.pop();
        auto backupBB = irBuilder.GetInsertBlock();
        auto backupIt = irBuilder.GetInsertPoint();
        irBuilder.SetInsertPointForPreparingTypeInfo();
        auto curTiPtr = irBuilder.CreateConstGEP1_32(typeInfoPtrType, typeArgs, idx);
        auto curTi = irBuilder.LLVMIRBuilder2::CreateLoad(typeInfoPtrType, curTiPtr);
        // Also update 'innerTypeInfoMap'.
        curType = DeRef(*curType)->GetTypeArgs()[idx];
        (void)innerTypeInfoMap.emplace(curType, curTi);
        irBuilder.SetInsertPoint(backupBB, backupIt);
        if (!remainPath.empty()) {
            auto typeInfosPtr = irBuilder.CreateStructGEP(typeInfoType, curTi, static_cast<size_t>(TYPEINFO_TYPE_ARGS));
            typeArgs = irBuilder.LLVMIRBuilder2::CreateLoad(irBuilder.getInt8PtrTy(), typeInfosPtr);
            typeArgs = irBuilder.CreateBitCast(typeArgs, typeInfoPtrType->getPointerTo());
        }
    }
    return innerTypeInfoMap.at(curType);
}

llvm::Value* CGExtensionDef::GetTypeInfoOfGeneric(IRBuilder2& irBuilder, CHIR::GenericType& gt)
{
    std::stack<std::pair<const CHIR::Type*, std::queue<size_t>>> candidates;
    std::queue<size_t> remainPath;
    auto currentType = targetType;
    for (auto idx : gtAccessPathMap[&gt]) {
        remainPath.push(idx);
    }
    candidates.push(std::make_pair(currentType, remainPath));
    while (!remainPath.empty()) {
        auto idx = remainPath.front();
        remainPath.pop();
        auto argType = currentType->GetTypeArgs()[idx];
        candidates.push(std::make_pair(argType, remainPath));
        currentType = DeRef(*argType);
    }
    while (!candidates.empty()) {
        auto [typeArg, remainPathQ] = candidates.top();
        candidates.pop();
        auto found = innerTypeInfoMap.find(typeArg);
        if (found != innerTypeInfoMap.end()) {
            return GetTypeInfoWithPath(irBuilder, *typeArg, found->second, std::move(remainPathQ), innerTypeInfoMap);
        }
    }
    InternalError("Failed to load generic type info for extended type '" + targetType->ToString() + "'");
    return nullptr;
}

void CGExtensionDef::CollectGenericParamIndicesMap()
{
    for (auto gt : chirDef.GetGenericTypeParams()) {
        auto ret = FoundGenericTypeAndCollectPath(*targetType, *gt, gtAccessPathMap[gt]);
        if (!ret) {
            InternalError("Generic type of extendDef '" + chirDef.GetIdentifier() + "' is not found in target type '" +
                targetType->ToString() + "'");
        }
    }
    /**
     * For 'extend<T, K> Array<Option<(T, K)>> where T <: Collection<K>'
     * 'typeInfos' is [typeinfo_of(Option<(T, K)>)]. We need to get 'K' from the map ('T' is not used in upperBound).
     */
    for (auto gt : chirDef.GetGenericTypeParams()) {
        genericParamsMap.emplace(
            gt, [gt, this](IRBuilder2& irBuilder) { return GetTypeInfoOfGeneric(irBuilder, *gt); });
    }
}

llvm::Constant* CGExtensionDef::GenerateWhereConditionFn()
{
    if (whereCondFn) {
        return whereCondFn;
    }
    auto& llvmCtx = cgMod.GetLLVMContext();
    // Function signature is: bool (uint32_t, TypeInfo*[]).
    std::vector<llvm::Type*> argTypes{
        llvm::Type::getInt32Ty(llvmCtx), CGType::GetOrCreateTypeInfoPtrType(llvmCtx)->getPointerTo()};
    llvm::FunctionType* whereCondFnType = llvm::FunctionType::get(llvm::Type::getInt1Ty(llvmCtx), argTypes, false);

    auto needGenerateFnForGenericType = [this]() {
        // 1. If any of the genericType has upperbound, the 'whereCondFn' function must be generated.
        // 2. If all genericTypes do not have upperbound and the current 'chirDef' is not 'ExtendDef',
        // OR, if the extended type's type arguments are in same order of 'ExtendDef's GetGenericTypeParams,
        //    we do not need to generate the 'whereCondFn' function.
        // Otherwise, we always need to generate the 'whereCondFn' function.
        std::vector<CHIR::Type*> genericTypes;
        for (auto gt : chirDef.GetGenericTypeParams()) {
            if (!gt->GetUpperBounds().empty()) {
                return true;
            }
            genericTypes.emplace_back(gt);
        }
        bool isAllTypeArgsGeneric = true;
        for (auto typeArg : targetType->GetTypeArgs()) {
            if (!typeArg->IsGeneric()) {
                isAllTypeArgsGeneric = false;
                break;
            }
        }
        return !isAllTypeArgsGeneric && chirDef.GetCustomKind() == CHIR::CustomDefKind::TYPE_EXTEND &&
            genericTypes != targetType->GetTypeArgs();
    };
    auto i8PtrTy = llvm::Type::getInt8PtrTy(llvmCtx);
    auto targetCGType = CGType::GetOrCreate(cgMod, targetType);
    if (!targetCGType->IsDynamicGI() || !needGenerateFnForGenericType()) {
        return llvm::Constant::getNullValue(i8PtrTy);
    }
    auto funcName = extendDefName + "_cs";
    if (auto cs = cgMod.GetLLVMModule()->getFunction(funcName)) {
        return llvm::ConstantExpr::getBitCast(cs, i8PtrTy);
    }
    auto fn = llvm::Function::Create(whereCondFnType, llvm::Function::PrivateLinkage, funcName, cgMod.GetLLVMModule());
    fn->addFnAttr("native-interface-fn");
    auto entryBB = llvm::BasicBlock::Create(llvmCtx, "entry", fn);
    IRBuilder2 irBuilder(cgMod, entryBB);
    auto typeInfos = fn->getArg(1);
    innerTypeInfoMap.emplace(targetType, typeInfos); // Parameter with index 1 is an array of typeinfo.
    llvm::Value* retVal = CreateCompareArgs(irBuilder, typeInfos, targetType->GetTypeArgs());
    retVal = CheckGenericParams(irBuilder, retVal);
    irBuilder.CreateRet(retVal);
    innerTypeInfoMap.clear();
    whereCondFn = llvm::ConstantExpr::getBitCast(fn, i8PtrTy);
    return whereCondFn;
}

llvm::Constant* CGExtensionDef::GenerateFuncTableForType(const std::vector<CHIR::VirtualFuncInfo>& virtualFuncInfos)
{
    auto i8PtrType = llvm::Type::getInt8PtrTy(cgCtx.GetLLVMContext());
    auto funcTableSize = virtualFuncInfos.size();
    if (funcTableSize == 0) {
        return llvm::Constant::getNullValue(i8PtrType);
    }
    auto tableType = llvm::ArrayType::get(i8PtrType, funcTableSize);
    auto funcTableGV =
        llvm::cast<llvm::GlobalVariable>(cgMod.GetLLVMModule()->getOrInsertGlobal(extendDefName + ".ft", tableType));
    if (funcTableGV->hasInitializer()) {
        return llvm::ConstantExpr::getBitCast(funcTableGV, i8PtrType);
    }
    funcTableGV->setLinkage(llvm::GlobalVariable::PrivateLinkage);
    std::vector<llvm::Constant*> funcTable(funcTableSize);
    for (size_t i = 0; i < funcTableSize; ++i) {
        auto funcInfo = virtualFuncInfos[i];
        if (funcInfo.instance) {
            auto function = cgMod.GetOrInsertCGFunction(funcInfo.instance)->GetRawFunction();
            funcTable[i] = llvm::ConstantExpr::getBitCast(function, i8PtrType);
        } else {
            funcTable[i] = llvm::ConstantPointerNull::get(llvm::Type::getInt8PtrTy(cgMod.GetLLVMContext()));
        }
    }

    funcTableGV->setInitializer(llvm::ConstantArray::get(tableType, funcTable));
    funcTableGV->addAttribute(CJED_FUNC_TABLE_ATTR);
    return llvm::ConstantExpr::getBitCast(funcTableGV, i8PtrType);
}

std::pair<llvm::Constant*, bool> CGExtensionDef::GenerateInterfaceFn(const CHIR::ClassType& inheritedType)
{
    CJC_ASSERT(!inheritedType.IsGeneric());
    auto& llvmCtx = cgMod.GetLLVMContext();
    auto i8PtrTy = llvm::Type::getInt8PtrTy(llvmCtx);
    if (auto cgType = CGType::GetOrCreate(cgMod, &inheritedType); cgType && !cgType->IsDynamicGI()) {
        auto ti = cgType->GetOrCreateTypeInfo();
        return {llvm::ConstantExpr::getBitCast(ti, i8PtrTy), true};
    }

    std::vector<llvm::Type*> argTypes{
        llvm::Type::getInt32Ty(llvmCtx), CGType::GetOrCreateTypeInfoPtrType(llvmCtx)->getPointerTo()};
    llvm::FunctionType* interfaceFnType = llvm::FunctionType::get(i8PtrTy, argTypes, false);

    std::string funcName = extendDefName + "_iFn";
    if (auto iFn = cgMod.GetLLVMModule()->getFunction(funcName)) {
        return {llvm::ConstantExpr::getBitCast(iFn, i8PtrTy), false};
    }
    llvm::Function* interfaceFn =
        llvm::Function::Create(interfaceFnType, llvm::Function::PrivateLinkage, funcName, cgMod.GetLLVMModule());
    interfaceFn->addFnAttr("native-interface-fn");
    auto entryBB = llvm::BasicBlock::Create(llvmCtx, "entry", interfaceFn);
    // Parameter with index 1 is an array of typeinfo.
    innerTypeInfoMap.emplace(targetType, interfaceFn->getArg(1));
    IRBuilder2 irBuilder(cgMod, entryBB);
    auto derefType = DeRef(inheritedType);
    llvm::Value* ti = irBuilder.CreateTypeInfo(*derefType, genericParamsMap, false);
    innerTypeInfoMap.clear();
    llvm::Value* retVal = irBuilder.CreateBitCast(ti, i8PtrTy);
    irBuilder.CreateRet(retVal);
    return {llvm::ConstantExpr::getBitCast(interfaceFn, i8PtrTy), false};
}

std::vector<llvm::Constant*> CGExtensionDef::GetEmptyExtensionDefContent(CGModule& cgMod, const CHIR::Type& targetType)
{
    auto& llvmCtx = cgMod.GetLLVMContext();
    auto i8NullVal = llvm::ConstantInt::getNullValue(llvm::Type::getInt8Ty(llvmCtx));
    auto i8PtrNullVal = llvm::ConstantInt::getNullValue(llvm::Type::getInt8PtrTy(llvmCtx));
    std::vector<llvm::Constant*> defConstants(static_cast<unsigned>(EXTENSION_DEF_FIELDS_NUM));
    defConstants[static_cast<size_t>(IS_INTERFACE_TI)] = i8NullVal;
    defConstants[static_cast<size_t>(INTERFACE_FN_OR_INTERFACE_TI)] = i8PtrNullVal;
    defConstants[static_cast<size_t>(WHERE_CONDITION_FN)] = i8PtrNullVal;
    defConstants[static_cast<size_t>(FUNC_TABLE)] = i8PtrNullVal;
    defConstants[static_cast<size_t>(TARGET_TYPE)] = GetTargetType(cgMod, targetType);
    auto cgType = CGType::GetOrCreate(cgMod, &targetType);
    auto i32Ty = llvm::Type::getInt32Ty(llvmCtx);
    defConstants[static_cast<size_t>(TYPE_PARAM_COUNT)] =
        llvm::ConstantInt::get(i32Ty, cgType->IsDynamicGI() ? targetType.GetTypeArgs().size() : 0U);

    return defConstants;
}

bool CGExtensionDef::CreateExtensionDefForType(CGModule& cgMod, const std::string& extensionDefName,
    const std::vector<llvm::Constant*>& content, bool isForExternalType)
{
    auto extensionDefType = CGType::GetOrCreateExtensionDefType(cgMod.GetLLVMContext());
    auto extensionDef =
        llvm::cast<llvm::GlobalVariable>(cgMod.GetLLVMModule()->getOrInsertGlobal(extensionDefName, extensionDefType));
    CJC_ASSERT(extensionDef);
    if (extensionDef->hasInitializer()) {
        return false;
    }
    extensionDef->setLinkage(llvm::GlobalValue::PrivateLinkage);
    extensionDef->setInitializer(llvm::ConstantStruct::get(extensionDefType, content));
    extensionDef->addAttribute(GC_MTABLE_ATTR);
    if (isForExternalType) {
        cgMod.AddExternalExtensionDef(extensionDef);
    } else {
        cgMod.AddNonExternalExtensionDef(extensionDef);
    }
    return true;
}

bool CGExtensionDef::CreateExtensionDefForType(const CHIR::ClassType& inheritedType)
{
    auto& vtable = chirDef.GetVTable();
    auto found = vtable.find(const_cast<CHIR::ClassType*>(&inheritedType));
    auto funcTableSize = found == vtable.end() ? 0 : found->second.size();
    if (funcTableSize == 0 && inheritedType.GetClassDef()->IsClass()) {
        return false;
    }

    // 'inheritedType' may be instantiated type
    extendDefName = typeMangle + "_ed_" + GetTypeQualifiedName(inheritedType);
    auto content = GetEmptyExtensionDefContent(cgMod, *targetType);
    auto [iFnOrTi, isTi] = GenerateInterfaceFn(inheritedType);
    content[static_cast<size_t>(INTERFACE_FN_OR_INTERFACE_TI)] = iFnOrTi;
    content[static_cast<size_t>(IS_INTERFACE_TI)] =
        llvm::ConstantInt::get(llvm::Type::getInt8Ty(cgCtx.GetLLVMContext()), static_cast<uint64_t>(isTi));
    content[static_cast<size_t>(WHERE_CONDITION_FN)] = GenerateWhereConditionFn();
    content[static_cast<size_t>(FUNC_TABLE)] =
        GenerateFuncTableForType(funcTableSize == 0 ? std::vector<CHIR::VirtualFuncInfo>() : found->second);

    return CreateExtensionDefForType(cgMod, extendDefName, content, isForExternalType);
}

namespace {
bool HasStaticMethods(const CHIR::CustomTypeDef& chirDef)
{
    for (auto method : chirDef.GetMethods()) {
        if (method->TestAttr(Cangjie::CHIR::Attribute::STATIC)) {
            return true;
        }
    }
    return false;
}

void GetOrderedParentTypesRecusively(
    CHIR::ClassType& type, std::list<const CHIR::ClassType*>& parents, CHIR::CHIRBuilder& builder)
{
    if (auto superClass = type.GetSuperClassTy(&builder)) {
        parents.emplace_front(superClass);
        GetOrderedParentTypesRecusively(*superClass, parents, builder);
    }

    auto classDef = type.GetClassDef();
    auto [res, replaceTable] = classDef->GetType()->CalculateGenericTyMapping(type);
    CJC_ASSERT(res);
    for (auto ty : classDef->GetImplementedInterfaceTys()) {
        auto instType = StaticCast<CHIR::ClassType*>(ReplaceRawGenericArgType(*ty, replaceTable, builder));
        if (std::find(parents.begin(), parents.end(), instType) != parents.end()) {
            continue;
        }
        parents.emplace_back(instType);
        GetOrderedParentTypesRecusively(*instType, parents, builder);
    }
}

/** @brief the order is {grandparent class, parent class, sub class ... , interface1, interface2, ... interfaceN}
 *  all classes are front of all interfaces, and parent class must be front of sub class
 *  but we don't care about the order of interfaces
 */
void GetOrderedParentTypesRecusively(
    const CHIR::CustomTypeDef& def, std::list<const CHIR::ClassType*>& parents, CHIR::CHIRBuilder& builder)
{
    if (auto classDef = DynamicCast<const CHIR::ClassDef*>(&def)) {
        if (auto superClass = classDef->GetSuperClassTy()) {
            parents.emplace_front(superClass);
            GetOrderedParentTypesRecusively(*superClass, parents, builder);
        }
    }
    for (auto interface : def.GetImplementedInterfaceTys()) {
        if (std::find(parents.begin(), parents.end(), interface) != parents.end()) {
            continue;
        }
        parents.emplace_back(interface);
        GetOrderedParentTypesRecusively(*interface, parents, builder);
    }
}
} // namespace

std::pair<uint32_t, std::vector<std::pair<const CHIR::Type*, const CHIR::Type*>>> CGExtensionDef::Emit()
{
    uint32_t extensionDefsNum = 0U;
    if (!chirDef.TestAttr(CHIR::Attribute::IMPORTED) && !chirDef.TestAttr(CHIR::Attribute::GENERIC_INSTANTIATED)) {
        CollectGenericParamIndicesMap();
        std::list<const CHIR::ClassType*> orderedInheritedTypes;
        if (chirDef.GetCustomKind() == CHIR::CustomDefKind::TYPE_CLASS) {
            auto& def = StaticCast<CHIR::ClassDef>(chirDef);
            auto isClassOrHasStaticMethods = def.IsClass() || HasStaticMethods(chirDef);
            if (isClassOrHasStaticMethods) {
                orderedInheritedTypes.push_back(StaticCast<CHIR::ClassType>(targetType));
            }
        }
        CHIR::CHIRBuilder& chirBuilder = cgMod.GetCGContext().GetCHIRBuilder();
        GetOrderedParentTypesRecusively(chirDef, orderedInheritedTypes, chirBuilder);
        for (auto iTy : orderedInheritedTypes) {
            if (iTy->IsAny()) {
                continue;
            }
            if (CreateExtensionDefForType(*iTy)) {
                ++extensionDefsNum;
                if (iTy->GetClassDef()->IsInterface()) {
                    extendInterfaces.emplace_back(iTy, chirDef.IsExtend() ? targetType : nullptr);
                }
            }
        }
    }
    for (auto extend : chirDef.GetExtends()) {
        auto rst = CGExtensionDef(cgMod, *extend).Emit();
        extensionDefsNum += rst.first;
        auto tt = rst.second;
        extendInterfaces.insert(extendInterfaces.end(), tt.begin(), tt.end());
    }
    if (!isForExternalType && extensionDefsNum != 0) {
        auto totalExtensionDefsNum = cgMod.GetNonExternalExtensionDefsNum();
        CJC_ASSERT(totalExtensionDefsNum >= extensionDefsNum);
        startIdxOfNonExternalExtensionDef = totalExtensionDefsNum - extensionDefsNum;
        auto targetDefType = targetType->IsNominal()
            ? static_cast<const CHIR::CustomType*>(targetType)->GetCustomTypeDef()->GetType()
            : targetType;
        CGType::GetOrCreate(cgMod, targetDefType)->SetCGExtensionDef(this);
    }
    return {extensionDefsNum, extendInterfaces};
}
