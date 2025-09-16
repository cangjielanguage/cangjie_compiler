// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_TRANSFORMATION_NO_SIDE_EFFECT_MARKER_H
#define CANGJIE_CHIR_TRANSFORMATION_NO_SIDE_EFFECT_MARKER_H

#include "cangjie/CHIR/Expression/Expression.h"
#include "cangjie/CHIR/Package.h"
#include "cangjie/CHIR/Value.h"

namespace Cangjie::CHIR {
class NoSideEffectMarker {
public:
    static void RunOnPackage(const Ptr<const Package>& package, bool isDebug);

    static void RunOnFunc(const Ptr<Value>& value, bool isDebug);

private:
    static bool CheckPackage(const std::string& packageName);

    static bool CheckNoSideEffectList(const Func& func);
};
}  // namespace CHIR

#endif