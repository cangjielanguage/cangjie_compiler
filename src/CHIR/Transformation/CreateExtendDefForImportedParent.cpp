// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Transformation/CreateExtendDefForImportedParent.h"
#include "cangjie/CHIR/Utils/Utils.h"

using namespace Cangjie::CHIR;

CreateExtendDefForImportedParent::CreateExtendDefForImportedParent(Package& package, CHIRBuilder& builder)
    : package(package), builder(builder)
{
}

void CreateExtendDefForImportedParent::CreateNewExtendDef(
    CustomTypeDef& curDef, ClassType& parentType, std::vector<VirtualMethodInfo>& virtualMethods)
{
    auto mangledName = "extend_" + curDef.GetIdentifier() + "_p_" + parentType.ToString();
    auto genericParams = curDef.GetGenericTypeParams();
    auto extendDef = builder.CreateExtend(
        INVALID_LOCATION, mangledName, package.GetName(), false, genericParams);
    extendDef->SetExtendedType(*curDef.GetType());
    extendDef->AddImplementedInterfaceTy(parentType);
    extendDef->EnableAttr(Attribute::COMPILER_ADD);
    if (curDef.TestAttr(Attribute::GENERIC)) {
        extendDef->EnableAttr(Attribute::GENERIC);
    }

    VTableInDef vtable;
    vtable.AddNewItemToTypeVTable(parentType, std::move(virtualMethods));
    extendDef->SetVTable(std::move(vtable));
}

void CreateExtendDefForImportedParent::Run()
{
    for (auto def : package.GetAllImportedCustomTypeDef()) {
        if (def->IsExtend()) {
            continue;
        }
        for (auto& it : def->GetModifiableDefVTable().GetModifiableTypeVTables()) {
            if (ParentDefIsFromExtend(*def, *(it.GetSrcParentType()->GetClassDef()))) {
                CreateNewExtendDef(*def, *it.GetSrcParentType(), it.GetModifiableVirtualMethods());
            }
        }
    }
}