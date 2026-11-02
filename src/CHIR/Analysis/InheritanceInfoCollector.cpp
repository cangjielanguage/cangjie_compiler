// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Analysis/InheritanceInfoCollector.h"

#include "cangjie/CHIR/IR/Type/ExtendDef.h"
#include "cangjie/CHIR/Utils/CHIRCasting.h"
#include "cangjie/CHIR/Utils/Utils.h"

namespace Cangjie::CHIR {

void InheritanceInfoCollector::Collect(SubTypeMap& subtypeMap, DefsMap& defsMap) const
{
    // Record every parent→child edge visible in this package. Closed-world policy
    // belongs in Devirtualization::CollectAllSubTypes / IsSubtypeSetComplete.
    for (const auto customTypeDef : package->GetAllCustomTypeDef()) {
        Type* thisType = customTypeDef->GetType();
        if (customTypeDef->IsExtend()) {
            thisType = StaticCast<ExtendDef*>(customTypeDef)->GetExtendedType();
            if (auto customTy = DynamicCast<CustomType*>(thisType)) {
                thisType = customTy->GetCustomTypeDef()->GetType();
            }
        }

        defsMap[thisType].emplace_back(customTypeDef);
        for (auto parentTy : customTypeDef->GetSuperTypesInCurDef()) {
            auto parentDef = parentTy->GetClassDef();
            if (IsCoreObject(*parentDef)) {
                continue;
            }
            subtypeMap[parentDef].emplace_back(InheritanceInfo{parentTy, thisType});
        }
    }
}

} // namespace Cangjie::CHIR
