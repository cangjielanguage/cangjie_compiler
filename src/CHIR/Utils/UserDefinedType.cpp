// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Utils/UserDefinedType.h"
#include "cangjie/CHIR/Utils/CHIRCasting.h"
#include "cangjie/CHIR/IR/Type/Type.h"
#include "cangjie/CHIR/IR/Value/Value.h"
#include "cangjie/CHIR/Utils/ToStringUtils.h"
#include "cangjie/CHIR/Utils/Utils.h"
#include "cangjie/Utils/CheckUtils.h"
#include <iostream>
#include <pthread.h>
#include <sstream>

using namespace Cangjie::CHIR;

std::string FuncSigInfo::ToStringBrief() const
{
    auto temp = FuncCallType{
        .funcName = funcName,
        .funcType = funcType,
        .genericTypeArgs = std::vector<Type*>(genericTypeParams.begin(), genericTypeParams.end()),
    };
    return temp.ToString();
}

std::string FuncSigInfo::ToString() const
{
    std::stringstream ss;
    ss << ToStringBrief();
    if (auto str = GetGenericTypeConstaintsStr(genericTypeParams); !str.empty()) {
        ss << " " << str;
    }
    return ss.str();
}

void FuncSigInfo::Dump() const
{
    std::cout << ToString() << std::endl;
}

std::string FuncCallType::ToString() const
{
    std::stringstream ss;
    ss << "@" << funcName;
    ss << TypeVecToString("<", genericTypeArgs, ">");
    ss << "(" << TypeVecToString("", funcType->GetParamTypes(), "") << ")";
    ss << " -> " << funcType->GetReturnType()->ToString();
    return ss.str();
}

void FuncCallType::Dump() const
{
    std::cout << ToString() << std::endl;
}

VirtualMethodInfo::VirtualMethodInfo(
    FuncSigInfo&& c, Function* func, const AttributeInfo& a, FuncType& o, Type& p, Type& r)
    : condition(std::move(c)), instance(func), attr(a), originalType(&o), parentType(&p), returnType(&r)
{
}

AttributeInfo VirtualMethodInfo::GetAttributeInfo() const
{
    return attr;
}

Function* VirtualMethodInfo::GetVirtualMethod() const
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

bool VirtualMethodInfo::FuncSigIsMatchedImpl(const FuncCallType& other,
    bool isStatic, std::unordered_map<const GenericType*, Type*>& replaceTable, CHIRBuilder& builder) const
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
    for (size_t j = 0; j < genericParamTys.size(); ++j) {
        auto declaredInstType = ReplaceRawGenericArgType(*genericParamTys[j], replaceTable, builder);
        if (!isStatic && j == 0) {
            // for the 1st param, the modal must be the same, and their data type should have parent-child relationship
            // we don't need to check parent-child relationship, because if there isn't parent-child relationship,
            // chir checker will report error later.
            // for other params, their types should be equivalent, including the modal must be the same.
            auto modalInVtable = instArgTys[j]->StripAllRefs()->GetModalInfo();
            auto modalInMethod = declaredInstType->StripAllRefs()->GetModalInfo();
            if (modalInVtable != modalInMethod) {
                return false;
            }
        } else if (!ParamTypeIsEquivalent(*declaredInstType, *instArgTys[j])) {
            return false;
        }
    }
    return true;
}

bool VirtualMethodInfo::FuncSigIsMatched(const FuncSigInfo& other, bool isStatic, CHIRBuilder& builder) const
{
    std::unordered_map<const GenericType*, Type*> replaceTable;
    auto funcInfo = FuncCallType{
        .funcName = other.funcName,
        .funcType = other.funcType,
        .genericTypeArgs = std::vector<Type*>(other.genericTypeParams.begin(), other.genericTypeParams.end())
    };
    return FuncSigIsMatchedImpl(funcInfo, isStatic, replaceTable, builder);
}

bool VirtualMethodInfo::FuncSigIsMatched(const FuncCallType& other,
    bool isStatic, std::unordered_map<const GenericType*, Type*> replaceTable, CHIRBuilder& builder) const
{
    return FuncSigIsMatchedImpl(other, isStatic, replaceTable, builder);
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

void VirtualMethodInfo::SetVirtualMethod(Function* newFunc)
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

std::string VirtualMethodInfo::ToString(size_t indent) const
{
    CJC_NULLPTR_CHECK(originalType);
    CJC_NULLPTR_CHECK(instance);

    std::stringstream ss;
    ss << IndentToString(indent);
    ss << condition.ToStringBrief() << " => " << instance->GetIdentifier();
    return ss.str();
}

void VirtualMethodInfo::Dump() const
{
    std::cout << ToString(0) << std::endl;
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

std::string VTableInType::ToString(size_t indent) const
{
    if (IsEmpty()) {
        return "";
    }
    std::stringstream ss;
    ss << IndentToString(indent++);
    ss << srcParentType->ToString() << " {" << std::endl;
    for (const auto& funcInfo : virtualMethods) {
        ss << funcInfo.ToString(indent) << std::endl;
    }
    ss << IndentToString(--indent) << "}" << std::endl;
    return ss.str();
}

void VTableInType::Dump() const
{
    std::cout << ToString(0);
}

std::vector<VTableInType>& VTableInDef::GetModifiableTypeVTables()
{
    return vtables;
}

const std::vector<VTableInType>& VTableInDef::GetTypeVTables() const
{
    return vtables;
}

std::string VTableInDef::ToString(size_t indent) const
{
    std::stringstream ss;
    if (vtables.empty()) {
        return "";
    }
    ss << IndentToString(indent++);
    ss << "vtable {" << std::endl;
    for (const auto& vtableInType : vtables) {
        ss << vtableInType.ToString(indent);
    }
    ss << IndentToString(--indent) << "}" << std::endl;
    return ss.str();
}

void VTableInDef::Dump() const
{
    std::cout << ToString(0);
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
    ClassType& srcClassTy, size_t index, Function* newFunc, Type* newParentTy, const std::string& newName)
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