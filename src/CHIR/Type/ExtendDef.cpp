// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Type/ExtendDef.h"

#include <sstream>
#include "cangjie/CHIR/CHIRCasting.h"
#include "cangjie/CHIR/ToStringUtils.h"
#include "cangjie/Utils/CastingTemplate.h"

using namespace Cangjie;
using namespace Cangjie::CHIR;

ExtendDef::ExtendDef(
    const std::string& identifier, const std::string& pkgName, std::vector<GenericType*> genericParams)
    : CustomTypeDef("", identifier, pkgName, CustomDefKind::TYPE_EXTEND), genericParams(genericParams)
{
}

CustomTypeDef* ExtendDef::GetExtendedCustomTypeDef() const
{
    if (auto customTy = DynamicCast<const CustomType*>(extendedType); customTy) {
        return customTy->GetCustomTypeDef();
    }
    return nullptr;
}

void ExtendDef::PrintAttrAndTitle(std::stringstream& ss) const
{
    ss << attributeInfo.ToString();
    std::string extendedTyStr;
    CJC_NULLPTR_CHECK(extendedType);
    if (auto customTy = DynamicCast<const CustomType*>(extendedType); customTy) {
        extendedTyStr = customTy->GetCustomTypeDef()->GetIdentifier() + GenericInsArgsToString(*customTy);
    } else {
        extendedTyStr = extendedType->ToString();
    }
    ss << CustomTypeKindToString(*this) << GenericDefArgsToString() << " " << extendedTyStr;
    PrintParent(ss);
}

void ExtendDef::PrintComment(std::stringstream& ss) const
{
    CustomTypeDef::PrintComment(ss);
    AddCommaOrNot(ss);
    if (ss.str().empty()) {
        ss << " // ";
    }
    ss << "id: " << identifier;
}

void ExtendDef::RemoveParent(ClassType& parent)
{
    implementedInterfaceTys.erase(
        std::remove(implementedInterfaceTys.begin(), implementedInterfaceTys.end(), &parent),
        implementedInterfaceTys.end());
}

Type* ExtendDef::GetExtendedType() const
{
    CJC_NULLPTR_CHECK(extendedType);
    return extendedType;
}

Type* ExtendDef::GetType() const
{
    return GetExtendedType();
}

void ExtendDef::SetExtendedType(Type& ty)
{
    extendedType = &ty;
}

void ExtendDef::SetType(CustomType& ty)
{
    (void)ty;
    CJC_ABORT(); // extend decl doesn't have type
}

std::vector<GenericType*> ExtendDef::GetGenericTypeParams() const
{
    return genericParams;
}