// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file includes de-virtualization type analysis.
 */

#ifndef CANGJIE_CHIR_ANALYSIS_DEVIRTUALIZATION_INFO_H
#define CANGJIE_CHIR_ANALYSIS_DEVIRTUALIZATION_INFO_H

#include <unordered_map>

#include "cangjie/CHIR/Analysis/ConstMemberVarCollector.h"
#include "cangjie/CHIR/IR/Package.h"
#include "cangjie/CHIR/IR/Type/ClassDef.h"
#include "cangjie/CHIR/IR/Type/Type.h"
#include "cangjie/CHIR/IR/Value/Value.h"
#include "cangjie/Option/Option.h"

namespace Cangjie::CHIR {

/**
 * @brief type kind for devirtualization pass.
 */
enum class DevirtualTyKind : uint8_t {
    SUBTYPE_OF, // Means a type who is the sub-class or sub-interface of another type.
    EXACTLY,    // Means a type exactly.
};

/**
 * @brief collect info for devirtualization pass, such as return map, subtype map.
 */
class DevirtualizationInfo {
public:
    DevirtualizationInfo() = delete;

    /// constructor of info collector for devirtualization pass.
    explicit DevirtualizationInfo(const Package* package, const GlobalOptions& opts)
        : package(package), opts(opts)
    {
    }

    /**
     * @brief main method to collect devirtualization info.
     */
    void CollectInfo();

    /**
     * @brief re-collect ret map after other optimization pass.
     */
    void FreshRetMap();

    /**
     * @brief check custom type is internal.
     * @param def custom type to check.
     * @return flag whether is internal custom type.
     */
    bool CheckCustomTypeInternal(const CustomTypeDef& def) const;

    /**
     * @brief collect const members to devirt.
     */
    void CollectConstMemberVarType();

    /**
     * @brief subType map inheritance info
     */
    struct InheritanceInfo {
        ClassType* parentInstType;
        Type* subInstType;
    };

    /**
     * @brief subtype map from class definition to inheritance info list.
     */
    using SubTypeMap = std::unordered_map<ClassDef*, std::vector<InheritanceInfo>>;

    /// return subtype map.
    const SubTypeMap& GetSubtypeMap() const;

    /// return const member type map.
    const ConstMemberVarCollector::ConstMemberMapType& GetConstMemberMap() const;

    /// return real runtime return type map.
    const std::unordered_map<Func*, Type*>& GetReturnTypeMap() const;

    /// map from type to its custom type definition.
    std::unordered_map<const Type*, std::vector<CustomTypeDef*>> defsMap;

private:
    void CollectReturnTypeMap(Func& func);

    SubTypeMap subtypeMap;

    std::unordered_map<Func*, Type*> realRuntimeRetTyMap;

    ConstMemberVarCollector::ConstMemberMapType constMemberTypeMap;

    const Package* package;

    const GlobalOptions opts;
};
} // namespace Cangjie::CHIR
#endif // CANGJIE_DEVIRTUALIZATION_INFO_COLLECT_H
