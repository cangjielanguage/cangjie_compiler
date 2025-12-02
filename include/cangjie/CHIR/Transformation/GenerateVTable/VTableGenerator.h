// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_VTABLE_CREATOR_H
#define CANGJIE_CHIR_VTABLE_CREATOR_H

#include <vector>

#include "cangjie/CHIR/Utils/UserDefinedType.h"
#include "cangjie/CHIR/IR/Type/CustomTypeDef.h"

namespace Cangjie::CHIR {
class VTableGenerator {
public:
    explicit VTableGenerator(CHIRBuilder& builder);
    /**
    * @brief generate vtable for CustomTypeDef
    *
    * @param customTypeDef generate and set this def's vtable
    */
    void GenerateVTable(CustomTypeDef& customTypeDef);

private:
    void MergeVtable(ClassType& instParentTy, VTableInDef& vtable);
    void CollectCurDefMethodsMayBeInVtable(const CustomTypeDef& def, std::vector<FuncBase*>& publicFuncs);
    std::vector<FuncBase*> GetAllMethods(const CustomTypeDef& def);
    std::vector<FuncBase*> GetAllMethods(const Type& ty);
    VirtualMethodInfo CreateVirtualFuncInfo(const AbstractMethodInfo& method,
        Type& originalParentType, const std::unordered_map<const GenericType*, Type*>& replaceTable);
    VirtualMethodInfo CreateVirtualFuncInfo(
        FuncBase& method, Type& originalParentType, const std::unordered_map<const GenericType*, Type*>& replaceTable);
    bool UpdateVtable(VirtualMethodInfo& curFuncInfo, VTableInDef& vtable);
    bool IsSigTypeMatched(const VirtualMethodInfo& curFuncInfo, const VirtualMethodInfo& funcInfoInVtable);
    bool VirtualFuncShouldAddToVTableInItsOwnParent(ClassType& ownParent, ClassType& alreadyIn);
    void UpdateAbstractMethodInVtable(VTableInDef& vtable);
    void UpdateAbstractMethodWithImplementedMethod(
        VTableInDef& vtable, const ClassType& curParentTy, VirtualMethodInfo& abstractFuncInfo);
    std::unordered_map<std::string, VirtualMethodInfo> CollectAllPublicAndProtectedMethods(const CustomTypeDef& curDef);
    std::unordered_map<const GenericType*, Type*> GetInstMapFromDefIncludeParents(
        const CustomTypeDef& def, const Type& curType);
    std::vector<FuncBase*> CollectMethodsIncludeParentsMayBeInVtable(const CustomTypeDef& curDef);
    void CollectMethodsFromAncestorInterfaceMayBeInVTable(
        const CustomTypeDef& curDef, std::vector<FuncBase*>& methods);

private:
    CHIRBuilder& builder;
};
}

#endif