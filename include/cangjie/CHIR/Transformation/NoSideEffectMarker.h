// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * we can mark some functions with NO_SIDE_EFFECT attribute if they are known to have no side effects.
 * Then llvm opt can safely optimize these functions.
 */

#ifndef CANGJIE_CHIR_NO_SIDE_EFFECT_MARKER_H
#define CANGJIE_CHIR_NO_SIDE_EFFECT_MARKER_H

#include "cangjie/CHIR/IR/Package.h"
#include "cangjie/CHIR/Utils/Utils.h"

namespace Cangjie::CHIR {
class NoSideEffectMarker {
public:
    NoSideEffectMarker(Package& package);
    
    /**
     * Mark functions with NO_SIDE_EFFECT attribute if they match the whitelist.
     *
     * This method scans all global functions and imported functions in the package,
     * and marks those that match the predefined whitelist (such as hashCode, compare,
     * ==, round, etc.) with the NO_SIDE_EFFECT attribute. This attribute indicates
     * that the function has no side effects and can be safely optimized or reordered.
     */
    void Run();

private:
    Package& package;
    const std::vector<FuncInfo> functionWhiteList = {
        FuncInfo("hashCode", "Int64", {}, "Int64", "std.core"),
        FuncInfo("hashCode", "Float64", {}, "Int64", "std.core"),
        FuncInfo("compare", "String", {"String"}, "Ordering", "std.core"),
        FuncInfo("==", "String", {"String"}, "Bool", "std.core"),
        FuncInfo("compare", "Ordering", {"Ordering"}, "Ordering", "std.core"),
        FuncInfo("round", "", {"Float64"}, "Float64", "std.math"),
        FuncInfo("round", "", {"Float32"}, "Float32", "std.math"),
        FuncInfo("round", "", {"Float16"}, "Float16", "std.math"),
    };
};
}

#endif