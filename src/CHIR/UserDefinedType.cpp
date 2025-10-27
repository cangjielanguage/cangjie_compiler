// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/UserDefinedType.h"
#include "cangjie/CHIR/CHIRCasting.h"
#include "cangjie/CHIR/Type/Type.h"
#include "cangjie/CHIR/Utils.h"
#include "cangjie/Utils/CheckUtils.h"
#include <pthread.h>

using namespace Cangjie::CHIR;

VirtualMethodInfo::VirtualMethodInfo(
    FuncSigInfo&& c, FuncBase* func, const AttributeInfo& a, FuncType& o, Type& p, Type& r)
    : condition(std::move(c)), instance(func), attr(a), originalType(&o), parentType(&p), returnType(&r)
{
}

AttributeInfo VirtualMethodInfo::GetAttributeInfo() const
{
    return attr;
}

FuncBase* VirtualMethodInfo::GetVirtualMethod() const
{
    return instance;
}

const FuncSigInfo& VirtualMethodInfo::GetCondition() const
{
    return condition;
}

Type* VirtualMethodInfo::GetInstParentType() const
{
    return parentType;
}

bool VirtualMethodInfo::FuncSigIsMatched(const FuncSigInfo& other, CHIRBuilder& builder) const
{
    // func name not matched
    if (condition.funcName != other.funcName) {
        return false;
    }
    auto paramTysInMethod = other.funcType->GetParamTypes();
    auto paramTysInVtable = condition.funcType->GetParamTypes();
    // param size not matched
    if (paramTysInVtable.size() != paramTysInMethod.size()) {
        return false;
    }
    // generic type param size not matched
    if (other.genericTypeParams.size() != condition.genericTypeParams.size()) {
        return false;
    }
    std::unordered_map<const GenericType*, Type*> replaceTable;
    for (size_t i = 0; i < condition.genericTypeParams.size(); ++i) {
        replaceTable[other.genericTypeParams[i]] = condition.genericTypeParams[i];
    }
    bool typeMatch = true;
    // check param types
    for (size_t i = 0; i < paramTysInMethod.size(); ++i) {
        auto paramTyInMethod = ReplaceRawGenericArgType(*paramTysInMethod[i], replaceTable, builder);
        if (!ParamTypeIsEquivalent(*paramTyInMethod, *paramTysInVtable[i])) {
            typeMatch = false;
            break;
        }
    }
    return typeMatch;
}

bool VirtualMethodInfo::FuncSigIsMatched(const FuncCallType& other,
    std::unordered_map<const GenericType*, Type*>& replaceTable, CHIRBuilder& builder) const
{
    if (condition.funcName != other.funcName) {
        return false;
    }
    auto genericParamTys = condition.funcType->GetParamTypes();
    auto instArgTys = other.funcType->GetParamTypes();
    if (genericParamTys.size() != instArgTys.size()) {
        return false;
    }
    auto genericTypeParams = condition.genericTypeParams;
    auto funcInstTypeArgs = other.genericTypeArgs;
    if (genericTypeParams.size() != funcInstTypeArgs.size()) {
        return false;
    }
    for (size_t j = 0; j < genericTypeParams.size(); ++j) {
        replaceTable.emplace(genericTypeParams[j], funcInstTypeArgs[j]);
    }
    bool matched = true;
    for (size_t j = 0; j < genericParamTys.size(); ++j) {
        auto declaredInstType = ReplaceRawGenericArgType(*genericParamTys[j], replaceTable, builder);
        if (!ParamTypeIsEquivalent(*declaredInstType, *instArgTys[j])) {
            matched = false;
            break;
        }
    }
    return matched;
}

void VirtualMethodInfo::UpdateMethodInfo(const VirtualMethodInfo& newInfo)
{
    instance = newInfo.instance;
    attr = newInfo.attr;
    originalType = newInfo.originalType;
    parentType = newInfo.parentType;
    returnType = newInfo.returnType;
}

bool VirtualMethodInfo::TestAttr(Attribute a) const
{
    return attr.TestAttr(a);
}

std::string VirtualMethodInfo::GetMethodName() const
{
    return condition.funcName;
}

FuncType* VirtualMethodInfo::GetMethodSigType() const
{
    return condition.funcType;
}

Type* VirtualMethodInfo::GetMethodInstRetType() const
{
    return returnType;
}

void VirtualMethodInfo::SetVirtualMethod(FuncBase* newFunc)
{
    instance = newFunc;
}

void VirtualMethodInfo::SetFuncName(const std::string& newName)
{
    condition.funcName = newName;
}

void VirtualMethodInfo::SetInstParentType(Type& newParentTy)
{
    parentType = &newParentTy;
}

void VirtualMethodInfo::SetOriginalFuncType(FuncType& newFuncType)
{
    originalType = &newFuncType;
}

void VirtualMethodInfo::ConvertPrivateType(
    std::function<FuncType*(FuncType&)>& convertFuncParamsAndRetType, std::function<Type*(Type&)>& convertType)
{
    condition.funcType = convertFuncParamsAndRetType(*condition.funcType);
    if (instance) {
        instance->ty = convertFuncParamsAndRetType(*instance->GetFuncType());
    }
    originalType = convertFuncParamsAndRetType(*originalType);
    if (parentType) {
        parentType = convertType(*parentType);
    }
    returnType = convertType(*returnType);
}

FuncType* VirtualMethodInfo::GetOriginalFuncType() const
{
    return originalType;
}

std::vector<GenericType*> VirtualMethodInfo::GetGenericTypeParams() const
{
    return condition.genericTypeParams;
}

VTableInType::VTableInType()
{
}

VTableInType::VTableInType(ClassType& p) : srcParentType(&p)
{
}

VTableInType::VTableInType(ClassType& p, std::vector<VirtualMethodInfo>&& methods)
    : srcParentType(&p), virtualMethods(methods)
{
}

std::vector<VirtualMethodInfo>& VTableInType::GetModifiableVirtualMethods()
{
    return virtualMethods;
}

const std::vector<VirtualMethodInfo>& VTableInType::GetVirtualMethods() const
{
    return virtualMethods;
}

ClassType* VTableInType::GetSrcParentType() const
{
    return srcParentType;
}

void VTableInType::AppendNewMethod(VirtualMethodInfo&& newMethod)
{
    virtualMethods.emplace_back(std::move(newMethod));
}

void VTableInType::ConvertPrivateType(
    std::function<FuncType*(FuncType&)>& convertFuncParamsAndRetType, std::function<Type*(Type&)>& convertType)
{
    srcParentType = StaticCast<ClassType*>(convertType(*srcParentType));
    for (auto& method : virtualMethods) {
        method.ConvertPrivateType(convertFuncParamsAndRetType, convertType);
    }
}

bool VTableInType::IsEmpty() const
{
    return srcParentType == nullptr;
}

size_t VTableInType::GetMethodNum() const
{
    return virtualMethods.size();
}

std::vector<VTableInType>& VTableInDef::GetModifiableTypeVTables()
{
    return vtables;
}

const std::vector<VTableInType>& VTableInDef::GetTypeVTables() const
{
    return vtables;
}

const VTableInType& VTableInDef::GetExpectedTypeVTable(const ClassType& srcParentType) const
{
    auto it = srcParentIndex.find(const_cast<ClassType*>(&srcParentType));
    if (it != srcParentIndex.end()) {
        return vtables[it->second];
    }
    return empty;
}

void VTableInDef::AddNewItemToTypeVTable(ClassType& srcParent, VirtualMethodInfo&& funcInfo)
{
    auto it = srcParentIndex.find(&srcParent);
    if (it != srcParentIndex.end()) {
        vtables[it->second].AppendNewMethod(std::forward<VirtualMethodInfo>(funcInfo));
    } else {
        vtables.emplace_back(VTableInType(srcParent));
        srcParentIndex[&srcParent] = vtables.size() - 1;
        vtables.back().AppendNewMethod(std::forward<VirtualMethodInfo>(funcInfo));
    }
}

void VTableInDef::AddNewItemToTypeVTable(ClassType& srcParent, std::vector<VirtualMethodInfo>&& funcInfos)
{
    auto it = srcParentIndex.find(&srcParent);
    CJC_ASSERT(it == srcParentIndex.end());
    vtables.emplace_back(srcParent, std::forward<std::vector<VirtualMethodInfo>>(funcInfos));
    srcParentIndex[&srcParent] = vtables.size() - 1;
}

void VTableInDef::ConvertPrivateType(
    std::function<FuncType*(FuncType&)>& convertFuncParamsAndRetType, std::function<Type*(Type&)>& convertType)
{
    for (auto& vtable : vtables) {
        vtable.ConvertPrivateType(convertFuncParamsAndRetType, convertType);
    }
    std::unordered_map<ClassType*, size_t> newTypeMap;
    for (auto& it : srcParentIndex) {
        newTypeMap[StaticCast<ClassType*>(convertType(*it.first))] = it.second;
    }
    srcParentIndex = std::move(newTypeMap);
}

void VTableInDef::UpdateItemInTypeVTable(
    ClassType& srcClassTy, size_t index, FuncBase* newFunc, Type* newParentTy, const std::string& newName)
{
    for (auto& vtableIt : vtables) {
        if (vtableIt.GetSrcParentType() != &srcClassTy) {
            continue;
        }
        auto& methodInfo = vtableIt.GetModifiableVirtualMethods()[index];
        methodInfo.SetVirtualMethod(newFunc);
        if (newFunc != nullptr) {
            methodInfo.SetOriginalFuncType(*newFunc->GetFuncType());
        }
        if (newParentTy != nullptr) {
            methodInfo.SetInstParentType(*newParentTy);
        }
        if (!newName.empty()) {
            methodInfo.SetFuncName(newName);
        }
    }
}