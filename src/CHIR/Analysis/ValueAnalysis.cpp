// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Analysis/ValueAnalysis.h"

using namespace Cangjie::CHIR;

template <typename ValueDomain, typename ValueStatePool>
typename State<ValueDomain, ValueStatePool>::ChildrenMap
ValueAnalysis<ValueDomain, ValueStatePool>::globalChildrenMap{};

template <typename ValueDomain, typename ValueStatePool>
typename State<ValueDomain, ValueStatePool>::AllocatedRefMap
ValueAnalysis<ValueDomain, ValueStatePool>::globalAllocatedRefMap{};

template <typename ValueDomain, typename ValueStatePool>
typename State<ValueDomain, ValueStatePool>::AllocatedObjMap
ValueAnalysis<ValueDomain, ValueStatePool>::globalAllocatedObjMap{};

template <typename ValueDomain, typename ValueStatePool>
std::vector<std::unique_ptr<Ref>> ValueAnalysis<ValueDomain, ValueStatePool>::globalRefPool{};

template <typename ValueDomain, typename ValueStatePool>
std::vector<std::unique_ptr<AbstractObject>> ValueAnalysis<ValueDomain, ValueStatePool>::globalAbsObjPool{};

template <typename ValueDomain, typename ValueStatePool>
State<ValueDomain, ValueStatePool> ValueAnalysis<ValueDomain, ValueStatePool>::globalState{
    &globalChildrenMap, &globalAllocatedRefMap, &globalAllocatedObjMap, &globalRefPool, &globalAbsObjPool};