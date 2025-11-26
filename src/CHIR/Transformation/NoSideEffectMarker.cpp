// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Transformation/NoSideEffectMarker.h"

using namespace Cangjie::CHIR;

NoSideEffectMarker::NoSideEffectMarker(Package& package) : package(package)
{
}

void NoSideEffectMarker::Run()
{
    for (auto func : package.GetGlobalFuncs()) {
        for (auto element : functionWhiteList) {
            if (IsExpectedFunction(*func, element)) {
                func->EnableAttr(Attribute::NO_SIDE_EFFECT);
            }
        }
    }
    for (Value* value : package.GetImportedVarAndFuncs()) {
        if (value->IsImportedVar()) {
            continue;
        }
        for (auto element : functionWhiteList) {
            if (IsExpectedFunction(*VirtualCast<FuncBase*>(value), element)) {
                value->EnableAttr(Attribute::NO_SIDE_EFFECT);
            }
        }
    }
}