// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_UPDATE_OPERATOR_VTABLE_H
#define CANGJIE_CHIR_UPDATE_OPERATOR_VTABLE_H

#include <vector>

#include "cangjie/CHIR/CHIRBuilder.h"
#include "cangjie/CHIR/Package.h"
#include "cangjie/CHIR/Type/ClassDef.h"
#include "cangjie/CHIR/UserDefinedType.h"
#include "cangjie/Utils/ConstantsUtils.h"

namespace Cangjie::CHIR {
/// Integer operators that can be affected by overflow strategy, given a specific set of argument types, are called
/// overflow operator. After collecting extend and vtable info, split overflow operator in vtable into three
/// versions if that operator func (of an interface) can be extended by integer types.
class UpdateOperatorVTable {
public:
    UpdateOperatorVTable(const Package& package, CHIRBuilder& builder);
    /**
    * @brief update vtable
    */
    void Update();

private:
    using OverflowOpIndex = size_t;
    struct RewriteVtableInfo {
        std::set<OverflowOpIndex> ov;
    };
    
    void CollectOverflowOperators();
    void CollectOverflowOperatorsOnInterface(ClassDef& def);
    void AddRewriteInfo(ClassDef& def, size_t index);
    void RewriteVtable();
    void RewriteOneVtableEntry(
        ClassType& infType, CustomTypeDef& user, const VirtualMethodInfo& methodInfo, size_t index);
    Func* GenerateBuiltinOverflowOperatorFunc(
        const std::string& name, OverflowStrategy ovf, const ExtendDef& user, bool isBinary);
    void RewriteVtableEntryRec(const ClassDef& inf, CustomTypeDef& user, const RewriteVtableInfo& info);
    void CollectVTableUsers();

private:
    const Package& package;
    CHIRBuilder& builder;

    // order ClassDef* by mangled name to keep binary equality
    struct RewriteInfoOrdering {
        bool operator()(ClassDef* one, ClassDef* another) const;
    };
    std::map<ClassDef*, RewriteVtableInfo, RewriteInfoOrdering> interRewriteInfo;
    std::unordered_map<std::string, Func*> cache;
    //             parent vtable, sub vtables
    std::unordered_map<ClassDef*, std::vector<CustomTypeDef*>> vtableUsers;
};
}

#endif