// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/IR/Type/StructDef.h"

#include "cangjie/CHIR/IR/Type/ClassDef.h"
#include "cangjie/CHIR/Utils/CHIRCasting.h"
#include "cangjie/CHIR/Utils/Utils.h"

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

std::string StructDef::AddExtraComment() const
{
    return isC ? "isCStruct" : "";
}

namespace {
bool IsCopyableInterfaceDef(ClassDef& def)
{
    if (IsExpectedCustomType(*def.GetType(), Cangjie::CORE_PACKAGE_NAME, std::string(Cangjie::COPY_NAME))) {
        return true;
    }
    for (auto parent : def.GetImplementedInterfaceDefs()) {
        if (IsCopyableInterfaceDef(*parent)) {
            return true;
        }
    }
    return false;
}
} // namespace

bool StructDef::IsCopyable() const
{
    // @C structs are implicitly Copyable.
    if (IsCStruct()) {
        return true;
    }
    for (auto def : GetImplementedInterfaceDefs()) {
        if (IsCopyableInterfaceDef(*def)) {
            return true;
        }
    }
    return false;
}
