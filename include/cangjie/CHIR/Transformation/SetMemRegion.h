// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_TRANSFORMATION_SET_MEM_REGION_H
#define CANGJIE_CHIR_TRANSFORMATION_SET_MEM_REGION_H

#include "cangjie/CHIR/IR/CHIRBuilder.h"
#include "cangjie/CHIR/IR/Package.h"

namespace Cangjie::CHIR {
class SetMemRegion {
public:
    SetMemRegion(CHIRBuilder& builder);
    ~SetMemRegion();
    void RunOnPackage(const Package& package);

private:
    void RunOnFunc(const Function& func);
    std::unordered_set<BlockGroup*> CollectFunctionRegionBlockGroup(const Function& func) const;
    void SetRegions(const std::unordered_set<BlockGroup*>& bgsNeedRegion);
    void FlattenExclave(BlockGroup& funcBody);

    CHIRBuilder& builder;
};
}

#endif