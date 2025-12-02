// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file includes de-virtualization information collector for const member.
 */

#ifndef CANGJIE_CHIR_ANALYSIS_CONST_MEMBER_VAR_COLLECTOR_H
#define CANGJIE_CHIR_ANALYSIS_CONST_MEMBER_VAR_COLLECTOR_H

#include <unordered_map>

#include "cangjie/CHIR/IR/Package.h"
#include "cangjie/CHIR/IR/Expression/Expression.h"

namespace Cangjie::CHIR {
class ConstMemberVarCollector {
public:
    using ConstMemberMapType = std::unordered_map<const CustomTypeDef*, std::unordered_map<size_t, Type*>>;

    explicit ConstMemberVarCollector(const Package* package,
        ConstMemberMapType& constMemberMap) : package(package), constMemberMap(constMemberMap)
    {
    }

    /// mark member info containing its original type and its derived class
    struct MemberInfo {
        MemberInfo() = default;
        explicit MemberInfo(Type* orig) : oriType(orig)
        {
        }
        Type* oriType = nullptr;
        Type* derivedType = nullptr;
    };

    /// collect memher which can be using for devirtualization pass.
    void CollectConstMemberVarType();

    /// judge if a member is declared as base type, and only init as one devrived type.
    void JudgeIfOnlyDerivedType(const CustomTypeDef& def, std::unordered_map<size_t, MemberInfo>& index2Type);

    /// handle StoreElementRef expression in CHIR IR.
    void HandleStoreElementRef(
        const StoreElementRef* stf, const Value* firstParam, std::unordered_map<size_t, MemberInfo>& index2Type) const;

    /// get source for a location.
    static const Value* GetSourceTargetRecursively(const Value* value);

private:
    const Package* package = nullptr;
    ConstMemberMapType& constMemberMap;
};

}  // namespace Cangjie::CHIR

#endif