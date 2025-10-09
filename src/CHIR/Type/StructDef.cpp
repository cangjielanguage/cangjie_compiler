// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Type/StructDef.h"

#include <sstream>
#include "cangjie/CHIR/CHIRCasting.h"
#include "cangjie/CHIR/ToStringUtils.h"

using namespace Cangjie::CHIR;

void StructDef::SetCStruct(bool value)
{
    isC = value;
}

bool StructDef::IsCStruct() const
{
    return isC;
}

void StructDef::SetType(CustomType& ty)
{
    CJC_ASSERT(ty.GetTypeKind() == Type::TypeKind::TYPE_STRUCT);
    type = &ty;
}

StructType* StructDef::GetType() const
{
    return StaticCast<StructType>(type);
}

void StructDef::PrintComment(std::stringstream& ss) const
{
    CustomTypeDef::PrintComment(ss);
    AddCommaOrNot(ss);
    if (ss.str().empty()) {
        ss << " // ";
    }
    ss << "isC: " << BoolToString(isC);
}
